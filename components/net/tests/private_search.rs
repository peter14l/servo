/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Unit tests for the engine-level private search architecture.

use cookie::Cookie;
use http::{HeaderMap, HeaderValue, header};
use net::cookie::ServoCookie;
use net::cookie_storage::CookieStorage;
use net_traits::CookieSource;
use net_traits::private_search::{
    GENERIC_ACCEPT_LANGUAGE, GENERIC_USER_AGENT, SEARCH_HARDWARE_CONCURRENCY, SearchOriginPolicy,
    is_search_origin, spoofed_hardware_concurrency,
};
use servo_url::ServoUrl;

fn search_url() -> ServoUrl {
    ServoUrl::parse("https://www.google.com/search?q=test").unwrap()
}

fn normal_url() -> ServoUrl {
    ServoUrl::parse("https://example.com/page").unwrap()
}

#[test]
fn search_host_matching_respects_label_boundaries() {
    let policy = SearchOriginPolicy::default();

    assert!(policy.matches_host("google.com"));
    assert!(policy.matches_host("www.google.com"));
    assert!(policy.matches_host("WWW.GOOGLE.COM."));
    assert!(policy.matches_host("duckduckgo.com"));
    assert!(policy.matches_host("search.brave.com"));
    assert!(policy.matches_host("startpage.com"));

    // Substring lookalikes must never be treated as search origins.
    assert!(!policy.matches_host("evilgoogle.com"));
    assert!(!policy.matches_host("google.com.example.net"));
    assert!(!policy.matches_host("notbrave.com"));
    assert!(!policy.matches_host(""));
}

#[test]
fn search_request_headers_are_stripped_and_genericized() {
    let policy = SearchOriginPolicy::default();
    let mut headers = HeaderMap::new();
    headers.insert(
        header::USER_AGENT,
        HeaderValue::from_static("IdentifyingBrowser/9.9 (Windows NT 10.0)"),
    );
    headers.insert(
        "sec-ch-ua",
        HeaderValue::from_static("\"Chromium\";v=\"120\""),
    );
    headers.insert(
        "sec-ch-ua-platform",
        HeaderValue::from_static("\"Windows\""),
    );
    headers.insert(
        header::ACCEPT_LANGUAGE,
        HeaderValue::from_static("de-DE,de;q=0.9"),
    );
    headers.insert("x-forwarded-for", HeaderValue::from_static("203.0.113.9"));
    headers.insert(header::ACCEPT, HeaderValue::from_static("text/html"));

    policy.sanitize_headers(&search_url(), &mut headers);

    assert_eq!(
        headers.get(header::USER_AGENT).unwrap().to_str().unwrap(),
        GENERIC_USER_AGENT
    );
    assert_eq!(
        headers
            .get(header::ACCEPT_LANGUAGE)
            .unwrap()
            .to_str()
            .unwrap(),
        GENERIC_ACCEPT_LANGUAGE
    );
    assert!(!headers.contains_key("sec-ch-ua"));
    assert!(!headers.contains_key("sec-ch-ua-platform"));
    assert!(!headers.contains_key("x-forwarded-for"));
    // Unrelated headers must survive unchanged.
    assert_eq!(
        headers.get(header::ACCEPT).unwrap().to_str().unwrap(),
        "text/html"
    );

    // The operation is idempotent: the call sites are deliberately duplicated
    // across fetch steps.
    let once = headers.clone();
    policy.sanitize_headers(&search_url(), &mut headers);
    assert_eq!(once, headers);
}

#[test]
fn normal_domain_headers_are_untouched() {
    let policy = SearchOriginPolicy::default();
    let mut headers = HeaderMap::new();
    headers.insert(
        header::USER_AGENT,
        HeaderValue::from_static("IdentifyingBrowser/9.9 (Windows NT 10.0)"),
    );
    headers.insert(
        header::ACCEPT_LANGUAGE,
        HeaderValue::from_static("de-DE,de;q=0.9"),
    );
    headers.insert(
        "sec-ch-ua-platform",
        HeaderValue::from_static("\"Windows\""),
    );
    let original = headers.clone();

    policy.sanitize_headers(&normal_url(), &mut headers);

    assert_eq!(headers, original);
    assert!(!is_search_origin(&normal_url()));
}

#[test]
fn disabled_policy_does_not_sanitize_headers() {
    let policy = SearchOriginPolicy::new(false, ["google.com"], "generic", "en-US");
    let mut headers = HeaderMap::new();
    headers.insert(
        header::USER_AGENT,
        HeaderValue::from_static("IdentifyingBrowser/9.9"),
    );
    let original = headers.clone();

    policy.sanitize_headers(&search_url(), &mut headers);

    assert_eq!(headers, original);
}

#[test]
fn search_cookies_are_readable_but_never_serialized() {
    let search = search_url();
    let normal = normal_url();
    let mut jar = CookieStorage::new(150);

    let search_cookie = Cookie::parse("search_tracker=secret123; Domain=google.com").unwrap();
    let search_cookie =
        ServoCookie::new_wrapped(search_cookie, &search, CookieSource::HTTP).unwrap();
    jar.push(search_cookie, &search, CookieSource::HTTP);

    let normal_cookie = Cookie::parse("normal_tracker=visible456; Domain=example.com").unwrap();
    let normal_cookie =
        ServoCookie::new_wrapped(normal_cookie, &normal, CookieSource::HTTP).unwrap();
    jar.push(normal_cookie, &normal, CookieSource::HTTP);

    // Search cookies still work for the lifetime of the session.
    assert_eq!(
        jar.cookies_for_url(&search, CookieSource::HTTP).as_deref(),
        Some("search_tracker=secret123")
    );

    // But they are excluded from disk serialization, while normal cookies are
    // still persisted.
    let bytes = postcard::to_stdvec(&jar).unwrap();
    let serialized = String::from_utf8_lossy(&bytes);
    assert!(
        !serialized.contains("secret123"),
        "search cookies must not be serialized to disk"
    );
    assert!(
        serialized.contains("visible456"),
        "normal cookies must remain serializable"
    );

    // Closing the browsing context purges the ephemeral partition only.
    jar.purge_ephemeral_cookies();
    assert!(jar.cookies_for_url(&search, CookieSource::HTTP).is_none());
    assert_eq!(
        jar.cookies_for_url(&normal, CookieSource::HTTP).as_deref(),
        Some("normal_tracker=visible456")
    );
}

#[test]
fn hardware_concurrency_is_spoofed_for_search_origins_only() {
    assert_eq!(
        spoofed_hardware_concurrency(&search_url()),
        Some(SEARCH_HARDWARE_CONCURRENCY)
    );
    assert_eq!(spoofed_hardware_concurrency(&normal_url()), None);
    assert!(is_search_origin(&search_url()));
    assert!(!is_search_origin(&normal_url()));
}
