/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Engine-level private search policy.
//!
//! This module is the single source of truth for deciding whether an origin is
//! a search engine origin. It is intentionally kept in `net_traits` so that the
//! network, storage and script components can all apply the *same* decision
//! without depending on one another.
//!
//! The policy is deliberately host based (rather than substring based) so that
//! an attacker-controlled host such as `google.com.example.net` is never
//! mistaken for a search origin.

use http::{HeaderMap, HeaderValue, header};
use servo_url::ServoUrl;

/// Registrable domains that are treated as search engines when the preference
/// does not provide an explicit list.
pub const DEFAULT_SEARCH_DOMAINS: &[&str] = &[
    "google.com",
    "google.co.uk",
    "google.de",
    "google.fr",
    "duckduckgo.com",
    "brave.com",
    "search.brave.com",
    "startpage.com",
    "startpage.org",
    "bing.com",
    "yahoo.com",
    "ecosia.org",
    "qwant.com",
    "mojeek.com",
    "searx.be",
];

/// A stable, low-entropy user agent used for search requests. It contains no
/// platform, architecture, locale or engine-version tokens.
pub const GENERIC_USER_AGENT: &str =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36";

/// A single, fixed language. Servo already reduces locale fingerprinting to a
/// single language; search requests use a fixed value regardless of host OS.
pub const GENERIC_ACCEPT_LANGUAGE: &str = "en-US";

/// Value reported for `navigator.hardwareConcurrency` on search origins.
pub const SEARCH_HARDWARE_CONCURRENCY: u64 = 4;

/// Return the spoofed hardware concurrency for a search origin, or `None` for
/// every other origin. Keeping the decision here makes it directly testable
/// without a full script runtime.
pub fn spoofed_hardware_concurrency(url: &ServoUrl) -> Option<u64> {
    is_search_origin(url).then_some(SEARCH_HARDWARE_CONCURRENCY)
}

/// The set of search origins, together with the mitigation policy that should
/// be applied to them.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SearchOriginPolicy {
    enabled: bool,
    domains: Vec<String>,
    user_agent: String,
    accept_language: String,
}

impl Default for SearchOriginPolicy {
    fn default() -> Self {
        Self {
            enabled: true,
            domains: DEFAULT_SEARCH_DOMAINS.iter().map(|s| (*s).to_owned()).collect(),
            user_agent: GENERIC_USER_AGENT.to_owned(),
            accept_language: GENERIC_ACCEPT_LANGUAGE.to_owned(),
        }
    }
}

impl SearchOriginPolicy {
    /// Build a policy from the current global preferences.
    ///
    /// Empty preference values fall back to the compiled-in defaults, so a
    /// partially configured profile never accidentally disables protection.
    pub fn from_prefs() -> Self {
        let domains = servo_config::pref!(privacy_private_search_domains);
        let domains: Vec<String> = if domains.trim().is_empty() {
            DEFAULT_SEARCH_DOMAINS.iter().map(|s| (*s).to_owned()).collect()
        } else {
            domains
                .split(',')
                .map(str::trim)
                .filter(|domain| !domain.is_empty())
                .map(str::to_owned)
                .collect()
        };

        let user_agent = servo_config::pref!(privacy_private_search_user_agent);
        let accept_language = servo_config::pref!(privacy_private_search_accept_language);

        Self {
            enabled: servo_config::pref!(privacy_private_search_enabled),
            domains,
            user_agent: if user_agent.is_empty() {
                GENERIC_USER_AGENT.to_owned()
            } else {
                user_agent
            },
            accept_language: if accept_language.is_empty() {
                GENERIC_ACCEPT_LANGUAGE.to_owned()
            } else {
                accept_language
            },
        }
    }

    /// Construct a policy explicitly. Useful for tests and embedders.
    pub fn new(
        enabled: bool,
        domains: impl IntoIterator<Item = impl Into<String>>,
        user_agent: impl Into<String>,
        accept_language: impl Into<String>,
    ) -> Self {
        Self {
            enabled,
            domains: domains.into_iter().map(Into::into).collect(),
            user_agent: user_agent.into(),
            accept_language: accept_language.into(),
        }
    }

    /// Whether private-search protections are enabled at all.
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    /// The proxy URI configured specifically for search traffic, if any.
    pub fn search_proxy_uri() -> String {
        servo_config::pref!(privacy_private_search_proxy_uri)
    }

    /// Match a bare host (no scheme, no port) against the configured domains.
    ///
    /// Matching is case-insensitive and respects label boundaries, so
    /// `evilgoogle.com` and `google.com.example.net` do not match `google.com`.
    pub fn matches_host(&self, host: &str) -> bool {
        if !self.enabled {
            return false;
        }

        let host = host.trim_end_matches('.');
        if host.is_empty() {
            return false;
        }

        self.domains.iter().any(|domain| {
            let domain = domain.trim_end_matches('.');
            if domain.is_empty() {
                return false;
            }
            host.eq_ignore_ascii_case(domain) ||
                (host.len() > domain.len() &&
                    host.as_bytes()[host.len() - domain.len() - 1] == b'.' &&
                    host[host.len() - domain.len()..].eq_ignore_ascii_case(domain))
        })
    }

    /// Whether the given URL targets a search origin.
    pub fn matches(&self, url: &ServoUrl) -> bool {
        url.host_str().is_some_and(|host| self.matches_host(host))
    }

    /// Strip or genericize all identifying request headers for search origins.
    ///
    /// This is intentionally idempotent: it may be called at more than one
    /// point in the fetch pipeline as headers are added in several stages.
    /// Non-search origins are left completely untouched.
    pub fn sanitize_headers(&self, url: &ServoUrl, headers: &mut HeaderMap) {
        if !self.matches(url) {
            return;
        }

        // Client Hints are high-entropy and must never leave the engine for a
        // search origin. Servo does not currently emit them, but script and
        // request interceptors may add headers that look like them. Collect the
        // names first because this HeaderMap version has no `retain` method.
        let client_hints: Vec<_> = headers
            .keys()
            .filter(|name| name.as_str().to_ascii_lowercase().starts_with("sec-ch-"))
            .cloned()
            .collect();
        for name in client_hints {
            headers.remove(name);
        }

        // Proxy-forwarding headers can disclose the original client address
        // even when the network socket itself has been anonymized.
        headers.remove("forwarded");
        headers.remove("x-forwarded-for");
        headers.remove("x-real-ip");

        let user_agent = match HeaderValue::from_str(&self.user_agent) {
            Ok(value) => value,
            Err(_) => HeaderValue::from_static(GENERIC_USER_AGENT),
        };
        headers.insert(header::USER_AGENT, user_agent);

        let accept_language = match HeaderValue::from_str(&self.accept_language) {
            Ok(value) => value,
            Err(_) => HeaderValue::from_static(GENERIC_ACCEPT_LANGUAGE),
        };
        headers.insert(header::ACCEPT_LANGUAGE, accept_language);
    }
}

/// Convenience helper for call sites that only need a yes/no answer using the
/// current preferences.
pub fn is_search_origin(url: &ServoUrl) -> bool {
    SearchOriginPolicy::from_prefs().matches(url)
}

/// Whether a bare host is a search host using the current preferences.
pub fn is_search_host(host: &str) -> bool {
    SearchOriginPolicy::from_prefs().matches_host(host)
}
