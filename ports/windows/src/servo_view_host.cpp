/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "servo_view_host.h"
#include "servo_raw_window.h"
#include "browser_window.h"
#include <windowsx.h>
#include <commdlg.h>
#include <vector>

static const wchar_t* SERVO_VIEW_CLASS_NAME = L"ServoViewHostWindow";

// Script 1: Platform Web Standards Compatibility
static const char* s_platformCompatScript = R"JS(
(function() {
    try {
        // 1. Cooperative Scheduling (requestIdleCallback)
        if (typeof window !== 'undefined' && typeof window.requestIdleCallback === 'undefined') {
            window.requestIdleCallback = function(cb) {
                var start = Date.now();
                return setTimeout(function() {
                    cb({ didTimeout: false, timeRemaining: function() { return Math.max(0, 50 - (Date.now() - start)); } });
                }, 1);
            };
            window.cancelIdleCallback = function(id) { clearTimeout(id); };
        }

        // 2. Notification API safe fallback
        if (typeof window !== 'undefined' && typeof window.Notification === 'undefined') {
            window.Notification = function() {};
            window.Notification.permission = 'default';
            window.Notification.requestPermission = function(cb) {
                var p = Promise.resolve('default');
                if (typeof cb === 'function') p.then(cb);
                return p;
            };
        }

        // 3. AudioContext standard fallback
        if (typeof window !== 'undefined' && typeof window.AudioContext === 'undefined' && typeof window.webkitAudioContext !== 'undefined') {
            window.AudioContext = window.webkitAudioContext;
        }

        // 4. ResizeObserver defensive shield
        if (typeof window !== 'undefined' && typeof window.ResizeObserver === 'undefined') {
            window.ResizeObserver = function(callback) {
                this.observe = function() {};
                this.unobserve = function() {};
                this.disconnect = function() {};
            };
        }

        // 5. BroadcastChannel safe fallback
        if (typeof window !== 'undefined' && typeof window.BroadcastChannel === 'undefined') {
            window.BroadcastChannel = function(name) {
                this.name = name;
                this.onmessage = null;
                this.onmessageerror = null;
                this.postMessage = function() {};
                this.close = function() {};
                this.addEventListener = function() {};
                this.removeEventListener = function() {};
                this.dispatchEvent = function() { return false; };
            };
        }

        // 6. ReportingObserver safe fallback
        if (typeof window !== 'undefined' && typeof window.ReportingObserver === 'undefined') {
            window.ReportingObserver = function(callback, options) {
                this.observe = function() {};
                this.disconnect = function() {};
                this.takeRecords = function() { return []; };
            };
        }

        // 7. Clipboard API safe fallback
        if (typeof navigator !== 'undefined' && !navigator.clipboard) {
            navigator.clipboard = {
                readText: function() { return Promise.resolve(''); },
                writeText: function(text) { return Promise.resolve(); }
            };
        }

        // 8. Web Share API safe fallback
        if (typeof navigator !== 'undefined' && !navigator.share) {
            navigator.share = function() { return Promise.reject(new Error('Share not supported')); };
        }

        // 9. Alert / Confirm dialog limiter to protect against spam loops
        if (typeof window !== 'undefined' && !window.__servo_dialog_limiter_installed) {
            window.__servo_dialog_limiter_installed = true;
            var _dlgCount = 0;
            var _lastDlg = 0;
            var _origAlert = window.alert;
            window.alert = function() {
                var now = Date.now();
                if (now - _lastDlg > 2000) _dlgCount = 0;
                _lastDlg = now;
                if (++_dlgCount > 4) return;
                return _origAlert ? _origAlert.apply(this, arguments) : undefined;
            };
        }
    } catch(e) {}
})();
)JS";

// Script 2: Specific Web Application Enhancements (YouTube ShadyCSS, Video MediaSource, Instagram DM hooks)
static const char* s_appEnhancementsScript = R"JS(
(function() {
    try {
        // Escape hatch: `servo_noshim=1` in the URL disables app-specific shims
        // so page behaviour can be A/B compared against stock web standards.
        var shimsDisabled = typeof location !== 'undefined' && location.href &&
            /[?&#]servo_noshim(=1)?(&|#|$)/.test(location.href);

        // 1. YouTube WebComponents ShadyCSS Shims (strictly scoped to YouTube)
        if (!shimsDisabled && typeof window !== 'undefined' && typeof location !== 'undefined' && location.hostname && /(^|\.)youtube\.com$/.test(location.hostname)) {
            function setupShadyCSS(obj) {
                if (!obj || typeof obj !== 'object') obj = {};
                var noop = function() {};
                var emptyStr = function() { return ''; };
                if (typeof obj.prepareTemplate !== 'function') obj.prepareTemplate = noop;
                if (typeof obj.prepareTemplateStyles !== 'function') obj.prepareTemplateStyles = noop;
                if (typeof obj.prepareTemplateDom !== 'function') obj.prepareTemplateDom = noop;
                if (typeof obj.styleElement !== 'function') obj.styleElement = noop;
                if (typeof obj.styleSubtree !== 'function') obj.styleSubtree = noop;
                if (typeof obj.styleDocument !== 'function') obj.styleDocument = noop;
                if (typeof obj.getComputedStyleValue !== 'function') obj.getComputedStyleValue = emptyStr;
                if (typeof obj.flush !== 'function') obj.flush = noop;
                if (typeof obj.flushCustomStyles !== 'function') obj.flushCustomStyles = noop;
                if (obj.nativeShadow === undefined) obj.nativeShadow = true;
                if (obj.nativeCss === undefined) obj.nativeCss = true;
                if (!obj.ScopingShim) {
                    obj.ScopingShim = {
                        prepareTemplate: noop,
                        prepareTemplateStyles: noop,
                        prepareTemplateDom: noop,
                        styleElement: noop,
                        styleSubtree: noop,
                        styleDocument: noop,
                        flush: noop,
                        flushCustomStyles: noop,
                        getComputedStyleValue: emptyStr
                    };
                }
                if (!obj.CustomStyleInterface) {
                    obj.CustomStyleInterface = {
                        addCustomStyle: noop
                    };
                }
                return obj;
            }

            if (window.ShadyCSS) setupShadyCSS(window.ShadyCSS);
            if (!window.__shadycss_trap_installed) {
                window.__shadycss_trap_installed = true;
                var _currentShady = setupShadyCSS(window.ShadyCSS || {});
                try {
                    Object.defineProperty(window, 'ShadyCSS', {
                        configurable: true, enumerable: true,
                        get: function() { return _currentShady; },
                        set: function(val) { _currentShady = setupShadyCSS(val); }
                    });
                } catch(e) { window.ShadyCSS = _currentShady; }
            }

            if (typeof window.customElements !== 'undefined' && !window.customElements.polyfillWrapFlushCallback) {
                window.customElements.polyfillWrapFlushCallback = function(fn) { fn(); };
            }
        }

        // 2. MediaSource stub for video players
        if (typeof window !== 'undefined' && typeof window.MediaSource === 'undefined') {
            var DummyMediaSource = function() { this.sourceBuffers = []; this.activeSourceBuffers = []; this.readyState = 'closed'; };
            DummyMediaSource.isTypeSupported = function() { return false; };
            DummyMediaSource.prototype.addSourceBuffer = function() { throw new DOMException('Not supported', 'NotSupportedError'); };
            DummyMediaSource.prototype.endOfStream = function() {};
            window.MediaSource = DummyMediaSource;
            window.WebKitMediaSource = DummyMediaSource;
        }

        // 3. Instagram DM hooks and chat scroller
        if (typeof location !== 'undefined' && location.hostname && /(^|\.)instagram\.com$/.test(location.hostname)) {
            if (!window.__servo_instagram_hooks_installed && typeof document !== 'undefined') {
                window.__servo_instagram_hooks_installed = true;

                document.addEventListener('click', function(e) {
                    if (!location.pathname.startsWith('/direct/')) return;
                    var target = e.target;
                    if (!target || target.closest('a, button, [role="button"], [role="link"], nav, header, article')) return;
                    var composer = target.closest('[data-lexical-editor="true"], form[role="presentation"], div[role="textbox"]');
                    if (!composer) return;
                    var editable = target.isContentEditable ? target : (target.closest('[contenteditable="true"]') || composer.querySelector('[contenteditable="true"]'));
                    if (editable && typeof editable.focus === 'function') {
                        editable.focus();
                    }
                }, true);

                document.addEventListener('keydown', function(e) {
                    if (!location.pathname.startsWith('/direct/')) return;
                    if (e.key === 'Enter' && !e.shiftKey) {
                        var target = e.target;
                        if (!target) return;
                        var composer = target.closest('footer, form, [data-lexical-editor="true"]');
                        if (!composer) return;
                        if (target.isContentEditable || target.getAttribute('role') === 'textbox' || target.closest('[contenteditable="true"], [role="textbox"]')) {
                            setTimeout(function() {
                                var buttons = composer.querySelectorAll('button, div[role="button"]');
                                for (var i = 0; i < buttons.length; i++) {
                                    var b = buttons[i];
                                    var text = (b.innerText || b.textContent || '').trim().toLowerCase();
                                    var aria = (b.getAttribute('aria-label') || '').toLowerCase();
                                    if (text === 'send' || aria === 'send' || b.querySelector('svg[aria-label="Send"]')) {
                                        b.click();
                                        break;
                                    }
                                }
                            }, 50);
                        }
                    }
                }, true);
            }
        }
    } catch(e) {}
})();
)JS";

// Script 3: Opt-in page diagnostics. Append `servo_diag=1` to the URL (query or
// hash) to dump DOM / custom-element state and uncaught page errors to the log.
static const char* s_diagnosticsScript = R"JS(
(function() {
    try {
        if (typeof location === 'undefined' || !location.href || location.href.indexOf('servo_diag') === -1) return;
        if (typeof window === 'undefined' || window.__servo_diag_installed) return;
        window.__servo_diag_installed = true;

        var errors = [];
        function note(msg) {
            if (errors.length < 30) errors.push(msg);
            console.warn('[servo-diag] ' + msg);
        }
        window.addEventListener('error', function(e) {
            note('error: ' + ((e && e.message) || 'unknown') +
                 ' @ ' + ((e && e.filename) || '?') + ':' + ((e && e.lineno) || 0));
        }, true);
        window.addEventListener('unhandledrejection', function(e) {
            var r = e && e.reason;
            note('rejection: ' + String((r && (r.message || r)) || 'unknown'));
        }, true);

        function describe(el) {
            var out = { tag: el.tagName ? el.tagName.toLowerCase() : '?' };
            try {
                var cs = getComputedStyle(el);
                out.display = cs.display;
                out.visibility = cs.visibility;
                out.opacity = cs.opacity;
            } catch (e) { out.display = 'computedStyle-failed'; }
            var r = el.getBoundingClientRect ? el.getBoundingClientRect() : null;
            out.rect = r ? (Math.round(r.width) + 'x' + Math.round(r.height)) : 'none';
            out.shadow = el.shadowRoot ? ('yes:' + el.shadowRoot.childNodes.length) : 'no';
            out.kids = el.children ? el.children.length : 0;
            out.htmlLen = el.innerHTML ? el.innerHTML.length : 0;
            return out;
        }

        function snapshot(tag) {
            try {
                var probe = [
                    'ytd-app', 'ytd-search', 'ytd-two-column-search-results-renderer',
                    'ytd-section-list-renderer', 'ytd-item-section-renderer',
                    'ytd-video-renderer', 'ytd-thumbnail', 'ytd-rich-item-renderer',
                    'yt-lockup-view-model', 'ytd-watch-metadata'
                ];
                var counts = [];
                for (var i = 0; i < probe.length; i++) {
                    var els = document.getElementsByTagName(probe[i]);
                    var shadowed = 0;
                    for (var j = 0; j < els.length; j++) if (els[j].shadowRoot) shadowed++;
                    counts.push(probe[i] + '=' + els.length + '/' + shadowed);
                }
                var ce = 'n/a';
                try {
                    if (typeof customElements === 'undefined') {
                        ce = 'no-api';
                    } else {
                        ce = 'video=' + (customElements.get('ytd-video-renderer') ? 'defined' : 'undefined') +
                             ',itemSection=' + (customElements.get('ytd-item-section-renderer') ? 'defined' : 'undefined') +
                             ',app=' + (customElements.get('ytd-app') ? 'defined' : 'undefined');
                    }
                } catch (e) { ce = 'probe-failed'; }
                var io = typeof IntersectionObserver === 'undefined' ? 'missing' : 'present';
                var data = 'missing';
                try {
                    data = window.ytInitialData
                        ? ('keys=' + Object.keys(window.ytInitialData).slice(0, 6).join('|'))
                        : 'missing';
                } catch (e) { data = 'probe-failed'; }
                var first = document.querySelector('ytd-video-renderer') ||
                            document.querySelector('yt-lockup-view-model') ||
                            document.querySelector('ytd-item-section-renderer');
                var bodyText = (document.body && document.body.innerText)
                    ? document.body.innerText.replace(/\s+/g, ' ').slice(0, 200)
                    : '';
                console.warn('[servo-diag:' + tag + '] url=' + location.href);
                console.warn('[servo-diag:' + tag + '] shims=' +
                    (/[?&#]servo_noshim(=1)?(&|#|$)/.test(location.href) ? 'disabled' : 'enabled'));
                console.warn('[servo-diag:' + tag + '] counts(tag=n/shadow) ' + counts.join(' '));
                console.warn('[servo-diag:' + tag + '] ce(' + ce + ') io=' + io + ' ytInitialData=' + data);
                console.warn('[servo-diag:' + tag + '] scrollH=' +
                    (document.documentElement ? document.documentElement.scrollHeight : -1) +
                    ' first=' + (first ? JSON.stringify(describe(first)) : 'none'));
                console.warn('[servo-diag:' + tag + '] bodyText="' + bodyText + '"');
                var container = document.querySelector('ytd-search ytd-section-list-renderer #contents') ||
                                document.querySelector('ytd-section-list-renderer #contents') ||
                                document.querySelector('ytd-item-section-renderer #contents') ||
                                document.querySelector('#contents');
                var containerHtml = container && container.innerHTML
                    ? container.innerHTML.replace(/\s+/g, ' ').slice(0, 400)
                    : '';
                console.warn('[servo-diag:' + tag + '] container(' +
                    (container ? container.tagName.toLowerCase() + '#' + (container.id || '?') +
                        ' kids=' + container.children.length : 'none') +
                    ') html="' + containerHtml + '"');
                console.warn('[servo-diag:' + tag + '] errors(' + errors.length + ') ' +
                    JSON.stringify(errors.slice(0, 8)));
            } catch (e) {
                console.warn('[servo-diag:' + tag + '] snapshot failed: ' + String((e && e.message) || e));
            }
        }

        var delays = [1500, 4000, 8000, 15000, 25000];
        for (var d = 0; d < delays.length; d++) {
            (function(ms) { setTimeout(function() { snapshot(ms + 'ms'); }, ms); })(delays[d]);
        }
    } catch (e) {}
})();
)JS";

static void ConfigureUserContentManager(struct Servo* servo, ServoWebViewBuilder* builder) {
    if (!servo || !builder) return;
    auto* ucm = servo_user_content_manager_create(servo);
    if (!ucm) return;

    // Universal font fallback mappings and selective visibility guarantees:
    // 1. Map web font names (Mona Sans, Hubot Sans) to local native fonts (Segoe UI, Arial)
    //    so glyphs are never invisible during variable font loading or format stalls.
    // 2. Unhide GitHub Intro hero text without affecting hidden menus or icon fonts.
    const char* s_userCss =
        "@font-face { font-family: 'Mona Sans'; src: local('Segoe UI'), local('Arial'); }\n"
        "@font-face { font-family: 'Hubot Sans'; src: local('Segoe UI'), local('Arial'); }\n"
        "@font-face { font-family: 'Mona Sans Mono'; src: local('Consolas'), local('Courier New'); }\n"
        "html, body {\n"
        "    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Segoe UI Emoji', Arial, sans-serif;\n"
        "}\n"
        "[class*=\"Intro__content\"], [class*=\"Intro__mona\"] {\n"
        "    opacity: 1 !important;\n"
        "    visibility: visible !important;\n"
        "}\n";

    auto* ss = servo_user_style_sheet_create(s_userCss, "about:blank");
    if (ss) {
        LogDebug("User stylesheet created successfully, adding to content manager.");
        servo_user_content_manager_add_style_sheet(ucm, ss);
        servo_user_style_sheet_free(ss);
    } else {
        LogDebug("WARNING: servo_user_style_sheet_create returned NULL - user CSS will NOT be applied!");
    }

    // Register UserScripts to execute at document_start before any page scripts
    auto* script = servo_user_script_create(s_platformCompatScript, "compat_shield.js");
    if (script) {
        servo_user_content_manager_add_script(ucm, script);
        servo_user_script_free(script);
        LogDebug("Platform compat user script registered successfully.");
    }

    auto* appScript = servo_user_script_create(s_appEnhancementsScript, "app_enhancements.js");
    if (appScript) {
        servo_user_content_manager_add_script(ucm, appScript);
        servo_user_script_free(appScript);
        LogDebug("App enhancements user script registered successfully.");
    }

    auto* diagScript = servo_user_script_create(s_diagnosticsScript, "servo_diagnostics.js");
    if (diagScript) {
        servo_user_content_manager_add_script(ucm, diagScript);
        servo_user_script_free(diagScript);
        LogDebug("Opt-in diagnostics user script registered successfully.");
    }

    servo_webview_builder_set_user_content_manager(builder, ucm);
}

void ServoViewHost::RegisterWindowClass(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = ServoViewHost::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Let Servo render directly
    wc.lpszClassName = SERVO_VIEW_CLASS_NAME;
    RegisterClassExW(&wc);
}

ServoViewHost::ServoViewHost(HWND parentHwnd, struct Servo* servo, BrowserWindow* browserWindow, const std::string& initialUrl)
    : m_hwnd(nullptr)
    , m_parentHwnd(parentHwnd)
    , m_servo(servo)
    , m_webview(nullptr)
    , m_context(nullptr)
    , m_browserWindow(browserWindow)
    , m_trackingMouse(false)
    , m_currentCursor(LoadCursorW(nullptr, IDC_ARROW))
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    LogDebug("ServoViewHost creating child window...");
    // Create child window
    m_hwnd = CreateWindowExW(
        0,
        SERVO_VIEW_CLASS_NAME,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 800, 600,
        parentHwnd,
        nullptr,
        hInstance,
        this
    );
    LogDebug("ServoViewHost child window created: 0x%p", m_hwnd);

    // Create windowed rendering context
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    uint32_t width = rc.right > 0 ? rc.right : 800;
    uint32_t height = rc.bottom > 0 ? rc.bottom : 600;

    LogDebug("Creating rendering context width=%u height=%u...", width, height);
    m_context = servo_win::create_win32_rendering_context(m_hwnd, width, height);
    if (!m_context) {
        LogDebug("Win32 window context failed, attempting software rendering context...");
        m_context = servo_rendering_context_create_software(width, height);
    }
    LogDebug("RenderingContext ready: 0x%p", m_context);

    // Build the WebView
    LogDebug("Building Servo WebView with URL %s...", initialUrl.c_str());
    ServoWebViewBuilder* builder = servo_webview_builder_create(m_servo, m_context);
    if (builder) {
        m_context = nullptr; // Ownership transferred to builder
        ServoError urlErr = servo_webview_builder_set_url(builder, initialUrl.empty() ? "about:blank" : initialUrl.c_str());
        LogDebug("servo_webview_builder_set_url returned %d", (int)urlErr);

        UINT dpi = GetDpiForWindow(m_hwnd);
        float scale = dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
        servo_webview_builder_set_hidpi_scale_factor(builder, scale);
        LogDebug("Setting HiDPI scale factor: %.2f (DPI: %u)", scale, dpi);

        ConfigureUserContentManager(m_servo, builder);

        ServoWebViewDelegate delegate = {};
        delegate.struct_size = sizeof(ServoWebViewDelegate);
        delegate.user_data = this;
        delegate.notify_new_frame_ready = OnNewFrameReady;
        delegate.notify_url_changed = OnUrlChanged;
        delegate.notify_page_title_changed = OnTitleChanged;
        delegate.notify_load_status_changed = OnLoadStatusChanged;
        delegate.notify_load_error = reinterpret_cast<decltype(delegate.notify_load_error)>(
            static_cast<void(*)(struct WebView*, int32_t, const char*, void*)>(OnLoadError)
        );
        delegate.notify_cursor_changed = OnCursorChanged;
        delegate.notify_favicon_changed = reinterpret_cast<decltype(delegate.notify_favicon_changed)>(OnFaviconChanged);
        delegate.show_alert_dialog = reinterpret_cast<decltype(delegate.show_alert_dialog)>(OnAlertDialog);
        delegate.show_confirm_dialog = reinterpret_cast<decltype(delegate.show_confirm_dialog)>(OnConfirmDialog);
        delegate.show_prompt_dialog = reinterpret_cast<decltype(delegate.show_prompt_dialog)>(OnPromptDialog);
        delegate.show_file_picker = reinterpret_cast<decltype(delegate.show_file_picker)>(OnFilePicker);
        delegate.show_context_menu = reinterpret_cast<decltype(delegate.show_context_menu)>(OnContextMenu);
        delegate.show_console_message = reinterpret_cast<decltype(delegate.show_console_message)>(OnConsoleMessage);
        delegate.on_before_download = reinterpret_cast<decltype(delegate.on_before_download)>(OnBeforeDownload);
        delegate.on_download_updated = reinterpret_cast<decltype(delegate.on_download_updated)>(OnDownloadUpdated);
        delegate.request_permission = reinterpret_cast<decltype(delegate.request_permission)>(OnRequestPermission);
        delegate.request_create_new = reinterpret_cast<decltype(delegate.request_create_new)>(OnRequestCreateNew);
        delegate.notify_crashed = reinterpret_cast<decltype(delegate.notify_crashed)>(OnNotifyCrashed);
        delegate.notify_status_text_changed = reinterpret_cast<decltype(delegate.notify_status_text_changed)>(OnStatusTextChanged);
        delegate.request_authentication = reinterpret_cast<decltype(delegate.request_authentication)>(OnRequestAuthentication);

        ServoError delErr = servo_webview_builder_set_delegate(builder, delegate);
        LogDebug("servo_webview_builder_set_delegate returned %d", (int)delErr);
        m_webview = servo_webview_builder_build(builder);
        LogDebug("servo_webview_builder_build returned: 0x%p", m_webview);
    }
}

ServoViewHost::ServoViewHost(
    HWND parentHwnd,
    struct Servo* servo,
    BrowserWindow* browserWindow,
    struct ServoCreateNewWebViewRequest* request
)
    : m_parentHwnd(parentHwnd)
    , m_servo(servo)
    , m_browserWindow(browserWindow)
    , m_hwnd(nullptr)
    , m_webview(nullptr)
    , m_context(nullptr)
    , m_trackingMouse(false)
    , m_currentCursor(LoadCursorW(nullptr, IDC_ARROW))
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    m_hwnd = CreateWindowExW(
        0,
        SERVO_VIEW_CLASS_NAME,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 800, 600,
        parentHwnd,
        nullptr,
        hInstance,
        this
    );

    if (m_hwnd) {
        UINT dpi = GetDpiForWindow(m_hwnd);
        float scale = dpi / 96.0f;
        uint32_t width = static_cast<uint32_t>(800 * scale);
        uint32_t height = static_cast<uint32_t>(600 * scale);

        m_context = servo_rendering_context_create_windows_hwnd(
            m_hwnd,
            hInstance,
            width,
            height
        );
        if (!m_context) {
            m_context = servo_rendering_context_create_software(width, height);
        }

        auto* builder = servo_create_new_webview_builder_create(request, m_context);
        if (builder) {
            m_context = nullptr; // Ownership transferred to builder
            servo_webview_builder_set_hidpi_scale_factor(builder, scale);
            ConfigureUserContentManager(m_servo, builder);

            ServoWebViewDelegate delegate = {};
            delegate.struct_size = sizeof(ServoWebViewDelegate);
            delegate.user_data = this;
            delegate.notify_new_frame_ready = OnNewFrameReady;
            delegate.notify_url_changed = OnUrlChanged;
            delegate.notify_page_title_changed = OnTitleChanged;
            delegate.notify_load_status_changed = OnLoadStatusChanged;
            delegate.notify_load_error = reinterpret_cast<decltype(delegate.notify_load_error)>(
                static_cast<void(*)(struct WebView*, int32_t, const char*, void*)>(OnLoadError)
            );
            delegate.notify_cursor_changed = OnCursorChanged;
            delegate.notify_favicon_changed = reinterpret_cast<decltype(delegate.notify_favicon_changed)>(OnFaviconChanged);
            delegate.show_alert_dialog = reinterpret_cast<decltype(delegate.show_alert_dialog)>(OnAlertDialog);
            delegate.show_confirm_dialog = reinterpret_cast<decltype(delegate.show_confirm_dialog)>(OnConfirmDialog);
            delegate.show_prompt_dialog = reinterpret_cast<decltype(delegate.show_prompt_dialog)>(OnPromptDialog);
            delegate.show_file_picker = reinterpret_cast<decltype(delegate.show_file_picker)>(OnFilePicker);
            delegate.show_context_menu = reinterpret_cast<decltype(delegate.show_context_menu)>(OnContextMenu);
            delegate.show_console_message = reinterpret_cast<decltype(delegate.show_console_message)>(OnConsoleMessage);
            delegate.on_before_download = reinterpret_cast<decltype(delegate.on_before_download)>(OnBeforeDownload);
            delegate.on_download_updated = reinterpret_cast<decltype(delegate.on_download_updated)>(OnDownloadUpdated);
            delegate.request_permission = reinterpret_cast<decltype(delegate.request_permission)>(OnRequestPermission);
            delegate.request_create_new = reinterpret_cast<decltype(delegate.request_create_new)>(OnRequestCreateNew);
            delegate.notify_crashed = reinterpret_cast<decltype(delegate.notify_crashed)>(OnNotifyCrashed);
        delegate.notify_status_text_changed = reinterpret_cast<decltype(delegate.notify_status_text_changed)>(OnStatusTextChanged);
        delegate.request_authentication = reinterpret_cast<decltype(delegate.request_authentication)>(OnRequestAuthentication);

            servo_webview_builder_set_delegate(builder, delegate);
            m_webview = servo_webview_builder_build(builder);
            LogDebug("Popup WebView created: 0x%p", m_webview);
        }
    }
}

ServoViewHost::~ServoViewHost() {
    if (m_webview) {
        servo_webview_free(m_webview);
        m_webview = nullptr;
    }
    if (m_context) {
        servo_rendering_context_free(m_context);
        m_context = nullptr;
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void ServoViewHost::SetBounds(int x, int y, int width, int height) {
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_webview && width > 0 && height > 0) {
            servo_webview_resize(m_webview, width, height);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void ServoViewHost::Show(bool show) {
    if (m_hwnd) {
        ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
        if (show && m_webview) {
            servo_webview_show(m_webview);
        } else if (!show && m_webview) {
            servo_webview_hide(m_webview);
        }
    }
}

void ServoViewHost::SetFocus() {
    if (m_hwnd) {
        ::SetFocus(m_hwnd);
        if (m_webview) {
            servo_webview_set_focused(m_webview, true);
        }
    }
}

void ServoViewHost::LoadUrl(const std::string& url) {
    if (m_webview && !url.empty()) {
        ServoError err = servo_webview_load(m_webview, url.c_str());
        LogDebug("servo_webview_load('%s') returned %d", url.c_str(), (int)err);
    }
}

void ServoViewHost::Reload() {
    if (m_webview) {
        servo_webview_reload(m_webview);
    }
}

void ServoViewHost::Stop() {
    if (m_webview) {
        servo_webview_stop(m_webview);
    }
}

void ServoViewHost::GoBack() {
    if (m_webview && servo_webview_can_go_back(m_webview)) {
        servo_webview_go_back(m_webview, 1);
    }
}

void ServoViewHost::GoForward() {
    if (m_webview && servo_webview_can_go_forward(m_webview)) {
        servo_webview_go_forward(m_webview, 1);
    }
}

bool ServoViewHost::CanGoBack() const {
    return m_webview ? servo_webview_can_go_back(m_webview) : false;
}

bool ServoViewHost::CanGoForward() const {
    return m_webview ? servo_webview_can_go_forward(m_webview) : false;
}

std::string ServoViewHost::GetUrl() const {
    if (!m_webview) return "";
    uintptr_t len = servo_webview_get_url(m_webview, nullptr, 0);
    if (len <= 1) return "";
    std::vector<char> buf(len);
    servo_webview_get_url(m_webview, buf.data(), len);
    return std::string(buf.data());
}

std::string ServoViewHost::GetTitle() const {
    if (!m_webview) return "";
    uintptr_t len = servo_webview_get_title(m_webview, nullptr, 0);
    if (len <= 1) return "";
    std::vector<char> buf(len);
    servo_webview_get_title(m_webview, buf.data(), len);
    return std::string(buf.data());
}

uint32_t ServoViewHost::GetServoModifiers() const {
    uint32_t mod = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mod |= SERVO_MODIFIER_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mod |= SERVO_MODIFIER_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)    mod |= SERVO_MODIFIER_ALT;
    return mod;
}

LRESULT CALLBACK ServoViewHost::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ServoViewHost* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ServoViewHost*>(cs->lpCreateParams);
        if (self) {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<ServoViewHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ServoViewHost::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        if (m_webview) {
            servo_webview_paint(m_webview);
            servo_webview_present(m_webview);
        }

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        // Return 1 to prevent GDI flicker; Servo paints entire region
        return 1;

    case WM_SIZE: {
        uint32_t w = LOWORD(lParam);
        uint32_t h = HIWORD(lParam);
        if (m_webview && w > 0 && h > 0) {
            servo_webview_resize(m_webview, w, h);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            UpdateWindow(m_hwnd);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wParam);
        float scale = dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
        if (m_webview) {
            servo_webview_set_hidpi_scale_factor(m_webview, scale);
        }
        return 0;
    }

    case WM_SETFOCUS:
        if (m_webview) {
            servo_webview_set_focused(m_webview, true);
        }
        return 0;

    case WM_KILLFOCUS:
        if (m_webview) {
            servo_webview_set_focused(m_webview, false);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_currentCursor) {
            ::SetCursor(m_currentCursor);
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
        if (m_webview) {
            servo_webview_notify_mouse_move_event(m_webview, x, y, GetServoModifiers());
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_webview) {
            servo_webview_notify_mouse_leave_event(m_webview, false);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        ::SetFocus(m_hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int32_t button = (msg == WM_LBUTTONDOWN) ? SERVO_MOUSE_BUTTON_PRIMARY
                       : (msg == WM_RBUTTONDOWN) ? SERVO_MOUSE_BUTTON_SECONDARY
                       : SERVO_MOUSE_BUTTON_AUXILIARY;
        if (m_webview) {
            servo_webview_set_focused(m_webview, true);
            servo_webview_notify_mouse_move_event(m_webview, x, y, GetServoModifiers());
            servo_webview_notify_mouse_button_event(
                m_webview, SERVO_MOUSE_EVENT_DOWN, button, x, y, GetServoModifiers()
            );
        }
        SetCapture(m_hwnd);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int32_t button = (msg == WM_LBUTTONUP) ? SERVO_MOUSE_BUTTON_PRIMARY
                       : SERVO_MOUSE_BUTTON_AUXILIARY;
        if (m_webview) {
            servo_webview_notify_mouse_button_event(
                m_webview, SERVO_MOUSE_EVENT_UP, button, x, y, GetServoModifiers()
            );
        }
        ReleaseCapture();
        return 0;
    }

    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (m_webview) {
            servo_webview_notify_mouse_button_event(
                m_webview, SERVO_MOUSE_EVENT_UP, SERVO_MOUSE_BUTTON_SECONDARY, x, y, GetServoModifiers()
            );
        }
        ReleaseCapture();

        POINT pt = { x, y };
        ClientToScreen(m_hwnd, &pt);
        ShowContextMenu(pt.x, pt.y);
        return 0;
    }

    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.x == -1 && pt.y == -1) {
            GetCursorPos(&pt);
        }
        ShowContextMenu(pt.x, pt.y);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            // Ctrl + Wheel: Smooth Zoom In / Zoom Out
            if (m_browserWindow) {
                if (delta > 0) m_browserWindow->ZoomIn();
                else if (delta < 0) m_browserWindow->ZoomOut();
            }
            return 0;
        }

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &pt);
        if (m_webview) {
            // Smooth pixel-based scrolling (SERVO_WHEEL_DELTA_PIXEL)
            servo_webview_notify_wheel_event(
                m_webview, 0, delta, 0, SERVO_WHEEL_DELTA_PIXEL, pt.x, pt.y
            );
        }
        return 0;
    }

    case WM_XBUTTONUP: {
        WORD btn = GET_XBUTTON_WPARAM(wParam);
        if (btn == XBUTTON1) {
            GoBack();
            return TRUE;
        } else if (btn == XBUTTON2) {
            GoForward();
            return TRUE;
        }
        return FALSE;
    }

    case WM_MOUSEHWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &pt);
        if (m_webview) {
            servo_webview_notify_wheel_event(
                m_webview, delta, 0, 0, SERVO_WHEEL_DELTA_PIXEL, pt.x, pt.y
            );
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        // Only forward true browser chrome shortcuts to the top-level BrowserWindow.
        // Editing keys (Ctrl+A, Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+Z, Ctrl+Y, Enter, etc.)
        // must remain with the active webview for typing, message submission, and text editing.
        bool isBrowserShortcut = false;
        if (ctrl) {
            switch (wParam) {
            case 'T': case 'W': case 'R': case 'L': case 'E': case 'K':
            case 'F': case 'J': case 'P': case 'U': case 'N': case 'H':
            case VK_OEM_COMMA: case VK_OEM_PLUS: case VK_OEM_MINUS: case VK_ADD:
            case VK_SUBTRACT: case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': case VK_TAB:
            case VK_PRIOR: case VK_NEXT: case VK_F4:
                isBrowserShortcut = true;
                break;
            default:
                break;
            }
        } else if (alt) {
            switch (wParam) {
            case VK_LEFT: case VK_RIGHT: case VK_HOME: case 'D':
                isBrowserShortcut = true;
                break;
            default:
                break;
            }
        } else {
            if (wParam == VK_F5 || wParam == VK_F6 || wParam == VK_F11 || wParam == VK_F12 || wParam == VK_ESCAPE) {
                isBrowserShortcut = true;
            }
        }

        if (isBrowserShortcut) {
            SendMessageW(m_parentHwnd, msg, wParam, lParam);
            return 0;
        }

        if (m_webview) {
            std::string key, code;
            servo_view::TranslateWin32Key(wParam, lParam, key, code);
            servo_webview_notify_key_event(
                m_webview, SERVO_KEY_EVENT_DOWN, key.c_str(), code.c_str(), 0, GetServoModifiers(), false, false
            );
        }
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (m_webview) {
            std::string key, code;
            servo_view::TranslateWin32Key(wParam, lParam, key, code);
            servo_webview_notify_key_event(
                m_webview, SERVO_KEY_EVENT_UP, key.c_str(), code.c_str(), 0, GetServoModifiers(), false, false
            );
        }
        return 0;
    }
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void ServoViewHost::OnNewFrameReady(struct WebView* webview, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (self && self->m_hwnd) {
        InvalidateRect(self->m_hwnd, nullptr, FALSE);
    }
}

void ServoViewHost::OnUrlChanged(struct WebView* webview, const char* url, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (url) {
        LogDebug("[NAV] url changed: %s", url);
    }
    if (self && self->m_browserWindow && url) {
        self->m_browserWindow->OnActiveTabUrlChanged(url);
    }
}

void ServoViewHost::OnTitleChanged(struct WebView* webview, const char* title, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (title) {
        LogDebug("[NAV] title changed: %s", title);
    }
    if (self && self->m_browserWindow && title) {
        self->m_browserWindow->OnActiveTabTitleChanged(title);
    }
}

void ServoViewHost::OnLoadStatusChanged(struct WebView* webview, int32_t status, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    LogDebug("[NAV] load status: %d", (int)status);
    if (self && self->m_browserWindow) {
        self->m_browserWindow->OnActiveTabLoadStatusChanged(status);
    }
    if (self && self->m_webview && (status == 1 || status == 2)) {
        self->InjectPageEnhancements();
    }
}

void ServoViewHost::OnLoadError(struct WebView* webview, int32_t errorCode, const char* errorText, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    std::string url = self ? self->GetUrl() : "";
    OnLoadError(webview, url.c_str(), errorCode, errorText, userData);
}

void ServoViewHost::OnLoadError(struct WebView* webview, const char* url, int32_t errorCode, const char* errorText, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    LogDebug("[NET ERR] Load error url='%s', code=%d, text='%s'",
             url ? url : "", errorCode, errorText ? errorText : "");
    if (self && self->m_browserWindow && errorCode < 0) {
        self->m_browserWindow->OnActiveTabLoadStatusChanged(-1);
    }
}

void ServoViewHost::OnCursorChanged(struct WebView* webview, int32_t cursorType, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (!self) return;

    LPCWSTR idc = servo_view::MapServoCursorToWin32(cursorType);
    self->m_currentCursor = LoadCursorW(nullptr, idc);
    ::SetCursor(self->m_currentCursor);
}

void ServoViewHost::OnFaviconChanged(struct WebView* webview, const char* url, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (self) {
        self->HandleFaviconChanged(url);
    }
}

void ServoViewHost::HandleFaviconChanged(const char* /*url*/) {
    if (!m_webview) return;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    size_t len = 0;
    ServoError err = (ServoError)servo_webview_get_favicon_image(m_webview, &width, &height, &format, nullptr, &len);
    if ((err == SERVO_ERROR_CAPACITY || err == SERVO_ERROR_OK) && len > 0 && width > 0 && height > 0) {
        size_t requiredBytes = (size_t)width * height * 4;
        if (len < requiredBytes) {
            // Buffer does not contain uncompressed raw RGBA pixels (e.g. encoded ICO/PNG/SVG)
            return;
        }
        std::vector<uint8_t> buffer(len);
        err = (ServoError)servo_webview_get_favicon_image(m_webview, &width, &height, &format, buffer.data(), &len);
        if (err == SERVO_ERROR_OK && buffer.size() >= requiredBytes) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -(LONG)height; // top-down DIB
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HDC screenDC = GetDC(nullptr);
            HBITMAP hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, screenDC);

            if (hBitmap && bits) {
                uint8_t* dst = static_cast<uint8_t*>(bits);
                const uint8_t* src = buffer.data();
                size_t numPixels = (size_t)width * height;
                for (size_t p = 0; p < numPixels; ++p) {
                    uint8_t r = (format == 0) ? src[p * 4 + 0] : src[p * 4 + 2];
                    uint8_t g = src[p * 4 + 1];
                    uint8_t b = (format == 0) ? src[p * 4 + 2] : src[p * 4 + 0];
                    uint8_t a = src[p * 4 + 3];

                    // Premultiply alpha for GDI AlphaBlend
                    dst[p * 4 + 0] = (uint8_t)((b * a) / 255);
                    dst[p * 4 + 1] = (uint8_t)((g * a) / 255);
                    dst[p * 4 + 2] = (uint8_t)((r * a) / 255);
                    dst[p * 4 + 3] = a;
                }

                if (m_browserWindow) {
                    m_browserWindow->OnTabFaviconUpdated(this, hBitmap, width, height);
                } else {
                    DeleteObject(hBitmap);
                }
            }
        }
    }
}

void ServoViewHost::OnAlertDialog(struct WebView* webview, const char* message, void (*callback)(bool, void*), void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    int len = MultiByteToWideChar(CP_UTF8, 0, message ? message : "", -1, nullptr, 0);
    std::vector<wchar_t> wmsg(len);
    MultiByteToWideChar(CP_UTF8, 0, message ? message : "", -1, wmsg.data(), len);

    MessageBoxW(self ? self->m_hwnd : nullptr, wmsg.data(), L"Alert", MB_OK | MB_ICONINFORMATION);
    if (callback) callback(true, userData);
}

void ServoViewHost::OnConfirmDialog(struct WebView* webview, const char* message, void (*callback)(bool, void*), void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    int len = MultiByteToWideChar(CP_UTF8, 0, message ? message : "", -1, nullptr, 0);
    std::vector<wchar_t> wmsg(len);
    MultiByteToWideChar(CP_UTF8, 0, message ? message : "", -1, wmsg.data(), len);

    int res = MessageBoxW(self ? self->m_hwnd : nullptr, wmsg.data(), L"Confirm", MB_YESNO | MB_ICONQUESTION);
    if (callback) callback(res == IDYES, userData);
}

void ServoViewHost::OnPromptDialog(struct WebView* webview, const char* message, const char* defaultVal, void (*callback)(const char*, void*), void* userData) {
    // For prompt, return default value
    if (callback) callback(defaultVal ? defaultVal : "", userData);
}

void ServoViewHost::OnFilePicker(struct WebView* webview, bool allowMultiple, void (*callback)(const char* const*, size_t, void*), void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    wchar_t szFile[MAX_PATH * 4] = { 0 };

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = self ? self->m_hwnd : nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0HTML Files (*.html;*.htm)\0*.html;*.htm\0\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | (allowMultiple ? OFN_ALLOWMULTISELECT | OFN_EXPLORER : 0);

    if (GetOpenFileNameW(&ofn)) {
        char utf8Path[MAX_PATH * 3] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, szFile, -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);
        const char* paths[] = { utf8Path };
        if (callback) callback(paths, 1, userData);
    } else {
        if (callback) callback(nullptr, 0, userData);
    }
}

void ServoViewHost::ShowContextMenu(int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING | (CanGoBack() ? MF_ENABLED : MF_GRAYED), 1, L"Back\tAlt+Left");
    AppendMenuW(hMenu, MF_STRING | (CanGoForward() ? MF_ENABLED : MF_GRAYED), 2, L"Forward\tAlt+Right");
    AppendMenuW(hMenu, MF_STRING, 3, L"Reload\tF5");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 4, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, 6, L"Select All\tCtrl+A");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 7, L"View Page Source\tCtrl+U");
    AppendMenuW(hMenu, MF_STRING, 5, L"Inspect Element\tF12");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 8, L"Settings...\tCtrl+,");

    int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    switch (cmd) {
    case 1:
        if (m_browserWindow) m_browserWindow->NavigateBack();
        break;
    case 2:
        if (m_browserWindow) m_browserWindow->NavigateForward();
        break;
    case 3:
        if (m_browserWindow) m_browserWindow->NavigateReload();
        break;
    case 4:
        SendMessageW(m_hwnd, WM_COPY, 0, 0);
        break;
    case 5:
        if (m_browserWindow) m_browserWindow->OpenDevTools();
        break;
    case 6:
        if (m_webview) {
            servo_webview_notify_key_event(m_webview, SERVO_KEY_EVENT_DOWN, "a", "KeyA", 0, SERVO_MODIFIER_CONTROL, false, false);
            servo_webview_notify_key_event(m_webview, SERVO_KEY_EVENT_UP, "a", "KeyA", 0, SERVO_MODIFIER_CONTROL, false, false);
        }
        break;
    case 7:
        if (m_browserWindow) {
            std::string srcUrl = "view-source:" + GetUrl();
            m_browserWindow->OpenUrlInNewTab(srcUrl);
        }
        break;
    case 8:
        if (m_browserWindow) {
            m_browserWindow->OpenSettings();
        }
        break;
    }
}

void ServoViewHost::OnContextMenu(struct WebView* webview, const struct ServoContextMenuParams* params, void (*callback)(int32_t, void*), void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (!self) return;

    POINT pt;
    GetCursorPos(&pt);
    self->ShowContextMenu(pt.x, pt.y);
    if (callback) callback(-1, userData);
}

void ServoViewHost::OnConsoleMessage(struct WebView* webview, int32_t level, const char* message, void* userData) {
    (void)webview;
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (message && strncmp(message, "__servo_ipc__:", 14) == 0) {
        if (self && self->m_browserWindow) {
            self->m_browserWindow->HandleWebIpcMessage(message + 14);
        }
        return;
    }
    const char* lvlStr = (level == 1) ? "ERROR" : (level == 2) ? "WARN" : (level == 3) ? "INFO" : "LOG";
    LogDebug("[JS %s] %s", lvlStr, message ? message : "");
}

void ServoViewHost::OnBeforeDownload(struct WebView* webview, struct ServoDownloadItem* download, void (*callback)(bool, void*), void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (self && self->m_browserWindow) {
        self->m_browserWindow->HandleNewDownload(download);
    }
    if (callback) callback(true, userData);
}

void ServoViewHost::OnDownloadUpdated(struct WebView* webview, struct ServoDownloadItem* download, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (self && self->m_browserWindow) {
        self->m_browserWindow->HandleDownloadUpdated(download);
    }
}

void ServoViewHost::OnRequestPermission(
    struct WebView* /*webview*/,
    const char* origin,
    int32_t permission,
    void (*callback)(bool, void*),
    void* callbackUserData,
    void* /*userData*/
) {
    LogDebug("Permission requested: type=%d for origin '%s'", permission, origin ? origin : "");
    if (callback) {
        callback(true, callbackUserData);
    }
}

bool ServoViewHost::OnRequestCreateNew(
    struct WebView* /*webview*/,
    struct ServoCreateNewWebViewRequest* request,
    void* userData
) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (!self || !request) return false;
    LogDebug("ServoViewHost::OnRequestCreateNew: request=0x%p", request);
    if (self->m_browserWindow) {
        BrowserWindow* popup = BrowserWindow::CreateForPopup(self->m_servo, request);
        return popup != nullptr;
    }
    return false;
}

void ServoViewHost::OnNotifyCrashed(
    struct WebView* /*webview*/,
    const char* reason,
    const char* backtrace,
    void* userData
) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    LogDebug("[CRASH] WebView crashed: reason='%s', backtrace='%s'",
             reason ? reason : "unknown",
             backtrace ? backtrace : "none");
    if (self && self->m_browserWindow) {
        self->m_browserWindow->OnActiveTabCrashed(reason ? reason : "Unknown error");
    }
}

void ServoViewHost::OnStatusTextChanged(struct WebView* /*webview*/, const char* status, void* userData) {
    auto self = reinterpret_cast<ServoViewHost*>(userData);
    if (self && self->m_browserWindow) {
        self->m_browserWindow->OnStatusTextChanged(status);
    }
}

void ServoViewHost::OnRequestAuthentication(
    struct WebView* /*webview*/,
    const char* host,
    uint16_t port,
    const char* realm,
    void (*callback)(const char*, const char*, void*),
    void* userData
) {
    LogDebug("[AUTH] HTTP authentication requested for %s:%u (realm: %s)",
             host ? host : "unknown",
             port,
             realm ? realm : "none");
    if (!callback) return;

    // Default to cancelling HTTP auth if user dismisses or no credentials
    callback(nullptr, nullptr, userData);
}

static void OnJsEvalCallback(const char* /*res*/, const char* err, void* /*userData*/) {
    if (err && *err) {
        LogDebug("[JS EVAL ERR] %s", err);
    }
}

void ServoViewHost::InjectPageEnhancements() {
    if (!m_webview) return;
    servo_webview_evaluate_javascript(m_webview, s_platformCompatScript, OnJsEvalCallback, nullptr);
    servo_webview_evaluate_javascript(m_webview, s_appEnhancementsScript, OnJsEvalCallback, nullptr);
}



