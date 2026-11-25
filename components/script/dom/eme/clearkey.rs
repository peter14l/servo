/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Native ClearKey (`org.w3.clearkey`) Content Decryption Module.
//!
//! ClearKey is the mandatory baseline key system of the Encrypted Media
//! Extensions specification. It uses plain-text, "in the clear" symmetric
//! keys to decrypt media samples and requires no proprietary binaries.
//!
//! This module implements the key-system-specific logic of the W3C EME
//! specification, section 9.1 ("Clear Key"):
//!
//! * <https://w3c.github.io/encrypted-media/#clear-key> — License Request Format (9.1.3)
//! * <https://w3c.github.io/encrypted-media/#clear-key-license-format> — License Format (9.1.4)
//! * <https://w3c.github.io/encrypted-media/#clear-key-release-format> — License Release Format (9.1.5)
//! * <https://w3c.github.io/encrypted-media/#clear-key-release-ack-format> — Release Acknowledgement Format (9.1.6)
//! * <https://w3c.github.io/encrypted-media/#using-base64url> — base64url usage (9.1.7)
//!
//! The module is deliberately free of DOM / SpiderMonkey dependencies so that
//! the parsing and decryption logic can be unit-tested in isolation. The DOM
//! layer (`mediakeysession.rs`) drives this module.

use aes::Aes128;
use base64::Engine;
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use ctr::Ctr128BE;
use ctr::cipher::{KeyIvInit, StreamCipher};
use serde::{Deserialize, Serialize};

/// The key system string of the native ClearKey CDM.
pub(crate) const CLEARKEY_KEY_SYSTEM: &str = "org.w3.clearkey";

/// AES-128 uses a 128-bit (16-byte) key and block size.
const AES_BLOCK_SIZE: usize = 16;

/// A single key entry of a ClearKey JWK Set (License Format, EME § 9.1.4).
///
/// ```json
/// { "kty": "oct", "kid": "<base64url>", "k": "<base64url>" }
/// ```
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub(crate) struct Jwk {
    /// Key type. ClearKey only defines the `"oct"` (octet sequence) type.
    pub(crate) kty: String,
    /// Key ID, base64url-encoded octet sequence.
    #[serde(default)]
    pub(crate) kid: Option<String>,
    /// Key value, base64url-encoded octet sequence.
    pub(crate) k: String,
}

/// The body of a ClearKey license response (EME § 9.1.4): a JWK Set.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub(crate) struct License {
    /// The set of keys. RFC 7517 names this member `"keys"`.
    pub(crate) keys: Vec<Jwk>,
    /// Optional session type; defaults to `"temporary"`.
    #[serde(default)]
    pub(crate) r#type: Option<String>,
}

/// A parsed ClearKey license request (EME § 9.1.3).
///
/// ```json
/// { "kids": ["<base64url>", ...], "type": "temporary" }
/// ```
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub(crate) struct LicenseRequest {
    /// The requested key IDs, each base64url-encoded.
    pub(crate) kids: Vec<String>,
    /// The requested `MediaKeySessionType`.
    #[serde(rename = "type")]
    pub(crate) session_type: String,
}

/// A single resolved key: the raw key ID together with the raw AES-128 key.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct ClearKey {
    /// The raw (decoded) key ID bytes.
    pub(crate) kid: Vec<u8>,
    /// The raw 16-byte AES-128 key.
    pub(crate) key: [u8; AES_BLOCK_SIZE],
}

/// Errors produced while parsing or applying a ClearKey license.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ClearKeyError {
    /// The input bytes were not valid UTF-8.
    InvalidUtf8,
    /// The JSON payload did not match the expected ClearKey format.
    InvalidJson,
    /// A base64url field could not be decoded.
    InvalidBase64,
    /// A JWK was missing the `"kty": "oct"` member.
    NotOctetKey,
    /// A JWK `"k"` member did not decode to a valid AES-128 key length.
    InvalidKeyLength,
    /// The license contained no usable keys.
    NoKeys,
    /// The response was a valid JSON object but not a JWK Set.
    InvalidLicense,
}

impl ClearKey {
    /// Parse a license request body (the CDM -> application message).
    ///
    /// The request is a UTF-8 encoded JSON object of the form
    /// `{ "kids": [...], "type": "temporary" }`.
    pub(crate) fn parse_license_request(bytes: &[u8]) -> Result<LicenseRequest, ClearKeyError> {
        let text = std::str::from_utf8(bytes).map_err(|_| ClearKeyError::InvalidUtf8)?;
        serde_json::from_str(text).map_err(|_| ClearKeyError::InvalidJson)
    }

    /// Parse a license response body (the application -> CDM message).
    ///
    /// The response is a UTF-8 encoded JWK Set of the form
    /// `{ "keys": [ { "kty": "oct", "kid": ..., "k": ... }, ... ] }`.
    pub(crate) fn parse_license(bytes: &[u8]) -> Result<License, ClearKeyError> {
        let text = std::str::from_utf8(bytes).map_err(|_| ClearKeyError::InvalidUtf8)?;
        serde_json::from_str(text).map_err(|_| ClearKeyError::InvalidJson)
    }

    /// Decode a single JWK into a [`ClearKey`] value.
    fn jwk_to_clear_key(jwk: &Jwk) -> Result<ClearKey, ClearKeyError> {
        if jwk.kty != "oct" {
            return Err(ClearKeyError::NotOctetKey);
        }
        let key = URL_SAFE_NO_PAD
            .decode(jwk.k.as_bytes())
            .map_err(|_| ClearKeyError::InvalidBase64)?;
        let key: [u8; AES_BLOCK_SIZE] = key
            .try_into()
            .map_err(|_| ClearKeyError::InvalidKeyLength)?;
        let kid = match &jwk.kid {
            Some(kid) => URL_SAFE_NO_PAD
                .decode(kid.as_bytes())
                .map_err(|_| ClearKeyError::InvalidBase64)?,
            None => Vec::new(),
        };
        Ok(ClearKey { kid, key })
    }

    /// Convert a parsed license into the set of usable ClearKey keys.
    ///
    /// As required by EME § 9.1.2, the sanitized response is invalid unless it
    /// is a valid JWK Set with at least one valid JWK key of a valid length.
    pub(crate) fn keys_from_license(license: &License) -> Result<Vec<ClearKey>, ClearKeyError> {
        let mut keys = Vec::with_capacity(license.keys.len());
        for jwk in &license.keys {
            keys.push(Self::jwk_to_clear_key(jwk)?);
        }
        if keys.is_empty() {
            return Err(ClearKeyError::NoKeys);
        }
        Ok(keys)
    }

    /// Parse a license response and extract its keys in one step.
    pub(crate) fn keys_from_response(bytes: &[u8]) -> Result<Vec<ClearKey>, ClearKeyError> {
        let license = Self::parse_license(bytes)?;
        Self::keys_from_license(&license)
    }

    /// Decrypt an encrypted media sample with AES-128-CTR.
    ///
    /// ClearKey uses AES-128 in counter mode. The counter block is the 16-byte
    /// key ID; when the key ID is shorter than a block it is zero-padded on the
    /// right, and when it is absent an all-zero IV is used. This matches the
    /// behaviour required for ISO Common Encryption (`cenc`) ` senc ` samples.
    pub(crate) fn decrypt_sample(key: &ClearKey, iv: &[u8], ciphertext: &[u8]) -> Vec<u8> {
        let mut counter = [0u8; AES_BLOCK_SIZE];
        let iv = if iv.is_empty() { &key.kid } else { iv };
        let n = iv.len().min(AES_BLOCK_SIZE);
        counter[..n].copy_from_slice(&iv[..n]);

        let mut plaintext = ciphertext.to_vec();
        // `new_from_slices` accepts plain `&[u8]` slices for key and IV, avoiding
        // any explicit GenericArray plumbing.
        let mut cipher = Ctr128BE::<Aes128>::new_from_slices(&key.key, &counter)
            .expect("AES-128 key and IV are always valid lengths");
        cipher.apply_keystream(&mut plaintext);
        plaintext
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn b64url(data: &[u8]) -> String {
        URL_SAFE_NO_PAD.encode(data)
    }

    #[test]
    fn parse_license_request() {
        let body = br#"{"kids":["LwVHf8JLtPrv2GUXFW2v_A"],"type":"temporary"}"#;
        let req = ClearKey::parse_license_request(body).unwrap();
        assert_eq!(req.kids, vec!["LwVHf8JLtPrv2GUXFW2v_A"]);
        assert_eq!(req.session_type, "temporary");
    }

    #[test]
    fn parse_license_request_invalid_utf8() {
        assert_eq!(
            ClearKey::parse_license_request(&[0xff, 0xfe]),
            Err(ClearKeyError::InvalidUtf8)
        );
    }

    #[test]
    fn parse_license_request_invalid_json() {
        assert_eq!(
            ClearKey::parse_license_request(b"not json"),
            Err(ClearKeyError::InvalidJson)
        );
    }

    #[test]
    fn parse_license_and_extract_keys() {
        let key = [0x11u8; 16];
        let kid = [0x22u8; 16];
        let body = format!(
            r#"{{"keys":[{{"kty":"oct","kid":"{}","k":"{}"}}]}}"#,
            b64url(&kid),
            b64url(&key)
        );
        let keys = ClearKey::keys_from_response(body.as_bytes()).unwrap();
        assert_eq!(keys.len(), 1);
        assert_eq!(keys[0].key, key);
        assert_eq!(keys[0].kid, kid.to_vec());
    }

    #[test]
    fn spec_example_key() {
        // Example 5 from EME § 13.1: k = tQ0bJVWb6b0KPL6KtZIy_A, kid = LwVHf8JLtPrv2GUXFW2v_A
        let body = br#"{"keys":[{"kty":"oct","k":"tQ0bJVWb6b0KPL6KtZIy_A","kid":"LwVHf8JLtPrv2GUXFW2v_A"}],"type":"temporary"}"#;
        let keys = ClearKey::keys_from_response(body).unwrap();
        assert_eq!(keys.len(), 1);
        assert_eq!(
            keys[0].key,
            [
                0xb5, 0x0d, 0x1b, 0x25, 0x55, 0x9b, 0xe9, 0xbd, 0x0a, 0x3c, 0xbe, 0x8a, 0xb5, 0x92,
                0x32, 0xfc
            ]
        );
        assert_eq!(
            keys[0].kid,
            [
                0x2f, 0x05, 0x47, 0x7f, 0xc2, 0x4b, 0xb4, 0xfa, 0xef, 0xd8, 0x65, 0x17, 0x15, 0x6d,
                0xaf, 0xfc
            ]
        );
    }

    #[test]
    fn reject_empty_key_set() {
        let body = br#"{"keys":[]}"#;
        assert_eq!(
            ClearKey::keys_from_response(body),
            Err(ClearKeyError::NoKeys)
        );
    }

    #[test]
    fn reject_non_octet_key() {
        let body = br#"{"keys":[{"kty":"RSA","k":"AAAA"}]}"#;
        assert_eq!(
            ClearKey::keys_from_response(body),
            Err(ClearKeyError::NotOctetKey)
        );
    }

    #[test]
    fn reject_wrong_key_length() {
        let short = URL_SAFE_NO_PAD.encode([0u8; 8]);
        let body = format!(r#"{{"keys":[{{"kty":"oct","k":"{}"}}]}}"#, short);
        assert_eq!(
            ClearKey::keys_from_response(body.as_bytes()),
            Err(ClearKeyError::InvalidKeyLength)
        );
    }

    #[test]
    fn reject_invalid_base64() {
        let body = br#"{"keys":[{"kty":"oct","k":"!!!!"}]}"#;
        assert_eq!(
            ClearKey::keys_from_response(body),
            Err(ClearKeyError::InvalidBase64)
        );
    }

    #[test]
    fn decrypt_roundtrip() {
        // AES-128-CTR is symmetric: encrypt and decrypt with the same key/IV.
        let key = ClearKey {
            kid: vec![0x33u8; 16],
            key: [0x44u8; 16],
        };
        let plaintext = b"the quick brown fox jumps over the lazy dog!!";
        // Encrypt by decrypting (CTR is symmetric).
        let ciphertext = ClearKey::decrypt_sample(&key, &[], plaintext);
        assert_ne!(ciphertext, plaintext);
        let recovered = ClearKey::decrypt_sample(&key, &[], &ciphertext);
        assert_eq!(recovered, plaintext);
    }

    #[test]
    fn decrypt_uses_explicit_iv() {
        let key = ClearKey {
            kid: vec![0u8; 16],
            key: [0x55u8; 16],
        };
        let iv = [0x66u8; 16];
        let data = b"0123456789abcdef";
        let a = ClearKey::decrypt_sample(&key, &iv, data);
        let b = ClearKey::decrypt_sample(&key, &iv, data);
        assert_eq!(a, b);
        // A different IV must yield a different keystream.
        let c = ClearKey::decrypt_sample(&key, &[0x77u8; 16], data);
        assert_ne!(a, c);
    }

    #[test]
    fn zero_iv_when_kid_absent() {
        let key = ClearKey {
            kid: vec![],
            key: [0x11u8; 16],
        };
        let data = [0u8; 32];
        let out = ClearKey::decrypt_sample(&key, &[], &data);
        // With an all-zero IV the keystream is the raw AES-CTR output; decrypting
        // zeros must not yield zeros (AES of the counter block is non-zero).
        assert!(out.iter().any(|&b| b != 0));
    }
}
