/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "schannel_gateway.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <shlobj.h>

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <mutex>
#include <map>
#include <set>
#include <condition_variable>
#include <memory>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <ctime>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace SchannelGateway {

static std::atomic<bool> s_running{false};
// Bound detached client threads so a pathological page cannot exhaust the
// process with unbounded concurrent gateway connections.
static std::atomic<int> s_activeClients{0};
static const int kMaxConcurrentClients = 64;
static std::thread s_workerThread;
static SOCKET s_listenSocket = INVALID_SOCKET;
static unsigned short s_port = 51820;

// The gateway is an explicit, local fallback. Only hosts the user has put in
// this file may be proxied; there are no built-in site-specific defaults.
static std::mutex s_allowlistMutex;
static std::set<std::string> s_allowedHosts;

static std::string GetAllowlistPath() {
    wchar_t localAppData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        return "";
    }
    std::wstring wide(localAppData);
    wide += L"\\Servo Browser\\gateway_allowlist.txt";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string path(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &path[0], len, nullptr, nullptr);
    return path;
}

static void LoadAllowlist() {
    std::lock_guard<std::mutex> lock(s_allowlistMutex);
    s_allowedHosts.clear();
    std::string path = GetAllowlistPath();
    if (path.empty()) return;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(0, 1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.empty()) continue;
        std::transform(line.begin(), line.end(), line.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        s_allowedHosts.insert(line);
    }
}

static bool IsAllowedHost(const std::wstring& host) {
    if (host.empty()) return false;
    std::string narrow;
    int len = WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    narrow.resize(static_cast<size_t>(len) - 1);
    WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, &narrow[0], len, nullptr, nullptr);
    std::transform(narrow.begin(), narrow.end(), narrow.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::lock_guard<std::mutex> lock(s_allowlistMutex);
    return s_allowedHosts.count(narrow) != 0;
}

// WinHTTP keeps keep-alive connections alive for the lifetime of a session.
// Reuse a small pool of long-lived sessions instead of opening (and closing) a
// fresh session per request, which otherwise forces a new TCP + TLS handshake
// for every script, style and image on a page.
struct GatewayContext {
    HINTERNET session = nullptr;
    std::map<std::wstring, HINTERNET> connects;

    ~GatewayContext() {
        for (auto& entry : connects) {
            WinHttpCloseHandle(entry.second);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
    }
};

static std::mutex s_contextMutex;
static std::condition_variable s_contextCv;
static std::vector<std::unique_ptr<GatewayContext>> s_freeContexts;
static const int kGatewayContexts = 64;

// Exclusive ownership for one request. A context is never used by two threads
// at once, so its connect handles are safe to reuse sequentially.
class ScopedGatewayContext {
public:
    ScopedGatewayContext() {
        std::unique_lock<std::mutex> lock(s_contextMutex);
        s_contextCv.wait(lock, [] { return !s_freeContexts.empty() || !s_running.load(); });
        if (!s_freeContexts.empty()) {
            m_context = std::move(s_freeContexts.back());
            s_freeContexts.pop_back();
        }
    }
    ~ScopedGatewayContext() {
        if (!m_context) return;
        std::lock_guard<std::mutex> lock(s_contextMutex);
        s_freeContexts.push_back(std::move(m_context));
        s_contextCv.notify_one();
    }
    GatewayContext& operator*() const { return *m_context; }
    explicit operator bool() const { return static_cast<bool>(m_context); }

private:
    std::unique_ptr<GatewayContext> m_context;
};

static void EnsureSession(GatewayContext& ctx) {
    if (ctx.session) return;
    ctx.session = WinHttpOpen(L"Servo-Schannel-Bridge/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ctx.session) return;
    // Bound every phase so a stuck upstream cannot pin a worker thread forever.
    WinHttpSetTimeouts(ctx.session, 10000, 10000, 30000, 30000);
    // Modern TLS only. Certificate-chain, hostname and validity checks stay on.
    DWORD sessionProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(ctx.session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &sessionProtocols, sizeof(sessionProtocols));
}

static HINTERNET GetConnect(GatewayContext& ctx, const std::wstring& host, INTERNET_PORT port) {
    if (!ctx.session) return nullptr;
    std::wstring key = host + L":" + std::to_wstring(port);
    auto it = ctx.connects.find(key);
    if (it != ctx.connects.end()) return it->second;
    HINTERNET connect = WinHttpConnect(ctx.session, host.c_str(), port, 0);
    if (!connect) return nullptr;
    ctx.connects.emplace(std::move(key), connect);
    return connect;
}

static void DropConnect(GatewayContext& ctx, const std::wstring& host, INTERNET_PORT port) {
    std::wstring key = host + L":" + std::to_wstring(port);
    auto it = ctx.connects.find(key);
    if (it == ctx.connects.end()) return;
    WinHttpCloseHandle(it->second);
    ctx.connects.erase(it);
}

static std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = { in[i + 1], in[i + 2], '\0' };
            char* end = nullptr;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                out += static_cast<char>(val);
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

static std::string UrlEncode(const std::string& in) {
    std::ostringstream escaped;
    for (char c : in) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            escaped << hex;
        }
    }
    return escaped.str();
}

static std::string GetOrigin(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "";
    size_t slash = url.find('/', schemeEnd + 3);
    return slash == std::string::npos ? url : url.substr(0, slash);
}

static std::string Trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

// Cookies belong to an upstream origin, not to the gateway origin. Prefix the
// name so one gateway session cannot forward another site's cookies upstream.
static std::string CookiePrefix(const std::wstring& host) {
    std::string narrow;
    int len = WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        narrow.resize(static_cast<size_t>(len) - 1);
        WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, &narrow[0], len, nullptr, nullptr);
    }
    uint32_t hash = 2166136261u;
    for (unsigned char c : narrow) {
        hash ^= c;
        hash *= 16777619u;
    }
    char prefix[16] = {};
    snprintf(prefix, sizeof(prefix), "gw_%08x_", hash);
    return prefix;
}

static std::string PrefixSetCookie(const std::string& line, const std::string& prefix) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return line;
    std::string name = Trim(line.substr(0, eq));
    if (name.rfind(prefix, 0) == 0) return line;
    return prefix + name + line.substr(eq);
}

static std::string FilterCookieHeader(const std::string& line, const std::string& prefix) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return "";
    std::string value = line.substr(colon + 1);
    std::string filtered;
    std::istringstream pairs(value);
    std::string pair;
    while (std::getline(pairs, pair, ';')) {
        size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        std::string name = Trim(pair.substr(0, eq));
        if (name.rfind(prefix, 0) != 0) continue;
        if (!filtered.empty()) filtered += "; ";
        filtered += name.substr(prefix.size()) + "=" + Trim(pair.substr(eq + 1));
    }
    return filtered.empty() ? "" : "Cookie: " + filtered;
}

// Send the whole buffer, handling partial sends and transient would-block errors.
static bool SendAll(SOCKET sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int chunk = static_cast<int>(std::min<size_t>(len - sent, 1u << 20));
        int r = send(sock, data + sent, chunk, 0);
        if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
                fd_set writeSet;
                FD_ZERO(&writeSet);
                FD_SET(sock, &writeSet);
                timeval tv = { 15, 0 };
                if (select(0, nullptr, &writeSet, nullptr, &tv) <= 0) return false;
                continue;
            }
            return false;
        }
        if (r == 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

static bool SendAll(SOCKET sock, const std::string& data) {
    return SendAll(sock, data.data(), data.size());
}

// Half-close and drain briefly so the peer receives every byte before the socket disappears.
static void CloseGracefully(SOCKET sock) {
    shutdown(sock, SD_SEND);
    char drain[4096];
    int total = 0;
    while (total < 262144) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval tv = { 0, 4000 }; // 4ms: enough for a loopback FIN without stalling a worker
        if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) break;
        int r = recv(sock, drain, sizeof(drain), 0);
        if (r <= 0) break;
        total += r;
    }
    closesocket(sock);
}

static void SendErrorResponse(SOCKET sock, const char* status, const std::string& body) {
    std::string resp = std::string("HTTP/1.1 ") + status + "\r\n";
    resp += "Content-Type: text/plain\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    if (SendAll(sock, resp)) {
        CloseGracefully(sock);
    } else {
        closesocket(sock);
    }
}

// Read until the end of the request headers (or a hard cap) instead of assuming one recv is enough.
static bool ReadRequest(SOCKET sock, std::string& request) {
    std::vector<char> buffer(65536);
    while (true) {
        int bytesRead = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytesRead <= 0) return false;
        request.append(buffer.data(), static_cast<size_t>(bytesRead));
        if (request.find("\r\n\r\n") != std::string::npos) return true;
        if (request.size() > 256 * 1024) return false;
    }
}

static size_t ParseContentLength(const std::string& headerBlock) {
    std::istringstream in(headerBlock);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("content-length:", 0) == 0) {
            return static_cast<size_t>(strtoul(line.c_str() + 15, nullptr, 10));
        }
    }
    return 0;
}

// Cache for static assets. Servo requests every script twice (preload scanner
// + parser), so this also halves upstream requests. The disk tier means a
// fresh browser launch does not refetch every asset from the slow legacy
// server; only dynamic .aspx pages still go upstream.
static std::mutex s_cacheMutex;
static std::mutex s_diskMutex;
static std::map<std::string, std::string> s_cache;
static size_t s_cacheBytes = 0;
static const size_t kCacheLimit = 64u * 1024u * 1024u;
static const uint64_t kCacheTtlSeconds = 7ull * 24ull * 3600ull;

static bool IsCacheableUrl(const std::string& url) {
    static const char* fragments[] = {
        "/ScriptResource.axd", "/WebResource.axd", "/Script/", "/Style/", "/images/", "/fonts/"
    };
    for (const char* fragment : fragments) {
        if (url.find(fragment) != std::string::npos) return true;
    }
    return false;
}

static uint64_t HashUrl(const std::string& url) {
    uint64_t hash = 1469598103934665603ull; // FNV-1a 64
    for (unsigned char c : url) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string GetCacheDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring dir = (len > 0 && len < MAX_PATH) ? std::wstring(buf, len) : std::wstring(L".");
    dir += L"\\Servo Browser\\gateway-cache";
    CreateDirectoryW(dir.c_str(), nullptr);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out;
    if (utf8Len > 0) {
        out.resize(static_cast<size_t>(utf8Len) - 1);
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, &out[0], utf8Len, nullptr, nullptr);
    }
    return out;
}

static std::string CacheFilePath(const std::string& key) {
    char name[32] = {};
    snprintf(name, sizeof(name), "%016llx.cache", static_cast<unsigned long long>(HashUrl(key)));
    return GetCacheDir() + "\\" + name;
}

// On-disk format: "SGC1", uint64 unix timestamp, uint32 key length,
// uint32 body length, key bytes, response bytes.
static bool LoadFromDisk(const std::string& key, std::string& out) {
    std::ifstream file(CacheFilePath(key), std::ios::binary);
    if (!file) return false;
    char magic[4] = {};
    file.read(magic, sizeof(magic));
    if (std::memcmp(magic, "SGC1", 4) != 0) return false;
    uint64_t written = 0;
    file.read(reinterpret_cast<char*>(&written), sizeof(written));
    if (!file) return false;
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    if (now < written || now - written > kCacheTtlSeconds) return false;
    uint32_t keyLen = 0;
    uint32_t bodyLen = 0;
    file.read(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
    file.read(reinterpret_cast<char*>(&bodyLen), sizeof(bodyLen));
    if (!file || keyLen != key.size() || bodyLen == 0 || bodyLen > kCacheLimit / 4) return false;
    std::string storedKey(keyLen, '\0');
    file.read(&storedKey[0], keyLen);
    if (!file || storedKey != key) return false;
    std::string body(bodyLen, '\0');
    file.read(&body[0], bodyLen);
    if (!file) return false;
    out = std::move(body);
    return true;
}

static void SaveToDisk(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(s_diskMutex);
    std::ofstream file(CacheFilePath(key), std::ios::binary | std::ios::trunc);
    if (!file) return;
    const char magic[4] = { 'S', 'G', 'C', '1' };
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    uint32_t keyLen = static_cast<uint32_t>(key.size());
    uint32_t bodyLen = static_cast<uint32_t>(value.size());
    file.write(magic, sizeof(magic));
    file.write(reinterpret_cast<const char*>(&now), sizeof(now));
    file.write(reinterpret_cast<const char*>(&keyLen), sizeof(keyLen));
    file.write(reinterpret_cast<const char*>(&bodyLen), sizeof(bodyLen));
    file.write(key.data(), key.size());
    file.write(value.data(), value.size());
}

static void CachePut(const std::string& key, const std::string& value);

static bool CacheGet(const std::string& key, std::string& out) {
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            out = it->second;
            return true;
        }
    }
    if (!LoadFromDisk(key, out)) return false;
    CachePut(key, out); // repopulate the memory tier and refresh the timestamp
    return true;
}

static void CachePut(const std::string& key, const std::string& value) {
    if (value.empty() || value.size() > kCacheLimit / 4) return;
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            s_cacheBytes -= it->second.size();
            it->second = value;
            s_cacheBytes += value.size();
        } else {
            if (s_cacheBytes + value.size() > kCacheLimit) {
                // ponytail: map order is not LRU; add an LRU queue only if evictions hurt hit rate.
                for (auto entry = s_cache.begin();
                     entry != s_cache.end() && s_cacheBytes + value.size() > kCacheLimit;) {
                    s_cacheBytes -= entry->second.size();
                    entry = s_cache.erase(entry);
                }
            }
            s_cache.emplace(key, value);
            s_cacheBytes += value.size();
        }
    }
    SaveToDisk(key, value);
}

static void HandleClient(SOCKET clientSock) {
    DWORD timeout = 15000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    BOOL nodelay = TRUE;
    setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    int sndBufSize = 256 * 1024;
    setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBufSize, sizeof(sndBufSize));

    std::string request;
    if (!ReadRequest(clientSock, request)) {
        closesocket(clientSock);
        return;
    }

    size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) {
        closesocket(clientSock);
        return;
    }
    std::string reqLine = request.substr(0, lineEnd);
    std::istringstream reqStream(reqLine);
    std::string method, uri, httpVer;
    reqStream >> method >> uri >> httpVer;

    std::string targetUrl;
    size_t qPos = uri.find("/gateway?url=");
    if (qPos != std::string::npos) {
        targetUrl = UrlDecode(uri.substr(qPos + 13));
    } else if (uri.find("/gateway/") == 0) {
        targetUrl = "https://" + uri.substr(9);
    }
    // Bare paths are intentionally unsupported: only an explicit absolute
    // upstream URL can select an origin.

    if (targetUrl.empty()) {
        SendErrorResponse(clientSock, "404 Not Found", "");
        return;
    }

    bool cacheable = (method == "GET") && IsCacheableUrl(targetUrl);
    if (cacheable) {
        std::string cached;
        if (CacheGet(targetUrl, cached)) {
            if (SendAll(clientSock, cached)) {
                CloseGracefully(clientSock);
            } else {
                closesocket(clientSock);
            }
            return;
        }
    }

    // Convert target URL to wide characters. Check the size query before
    // indexing an empty std::wstring: malformed input must fail closed with a
    // 400 rather than reaching WinHTTP as an empty URL (UB in &wstring[0]).
    int wlen = MultiByteToWideChar(CP_UTF8, 0, targetUrl.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: invalid upstream URL encoding.");
        return;
    }
    std::wstring wTargetUrl(static_cast<size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, targetUrl.c_str(), -1, &wTargetUrl[0], wlen) <= 0) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: invalid upstream URL encoding.");
        return;
    }

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {};
    wchar_t urlPath[2048] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = ARRAYSIZE(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);

    if (!WinHttpCrackUrl(wTargetUrl.c_str(), 0, 0, &urlComp)) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: malformed upstream URL.");
        return;
    }

    if (urlComp.nScheme != INTERNET_SCHEME_HTTPS && urlComp.nScheme != INTERNET_SCHEME_HTTP) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: only http and https upstream URLs are allowed.");
        return;
    }

    if (!IsAllowedHost(hostName)) {
        SendErrorResponse(clientSock, "403 Forbidden",
                          "Gateway Error: upstream host is not in gateway_allowlist.txt. "
                          "Add one hostname per line to %LOCALAPPDATA%\\Servo Browser\\gateway_allowlist.txt.");
        return;
    }

    ScopedGatewayContext scopedCtx;
    if (!scopedCtx) {
        SendErrorResponse(clientSock, "503 Service Unavailable", "Gateway Error: gateway is shutting down.");
        return;
    }
    GatewayContext& ctx = *scopedCtx;
    EnsureSession(ctx);
    HINTERNET hSession = ctx.session;
    if (!hSession) {
        SendErrorResponse(clientSock, "502 Bad Gateway", "Gateway Error: WinHTTP session init failed.");
        return;
    }

    HINTERNET hConnect = GetConnect(ctx, hostName, urlComp.nPort);
    if (!hConnect) {
        SendErrorResponse(clientSock, "502 Bad Gateway", "Gateway Error: WinHTTP connect failed.");
        return;
    }

    int methodWLen = MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, nullptr, 0);
    if (methodWLen <= 0) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: invalid request method encoding.");
        return;
    }
    std::wstring wMethod(static_cast<size_t>(methodWLen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, &wMethod[0], methodWLen) <= 0) {
        SendErrorResponse(clientSock, "400 Bad Request", "Gateway Error: invalid request method encoding.");
        return;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), urlComp.lpszUrlPath,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        SendErrorResponse(clientSock, "502 Bad Gateway", "Gateway Error: WinHTTP request init failed.");
        return;
    }

    // WinHTTP uses Schannel with full chain, hostname and validity checks.
    // Never relax validation here: the gateway is an explicit fallback for
    // sites whose cipher suites rustls rejects, not a certificate bypass.

    // Do NOT follow redirects automatically in WinHTTP!
    // The browser must receive 302/301 responses with Set-Cookie headers intact,
    // and update its navigation location so that relative URLs and session states remain consistent.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    // Disable WinHTTP's internal cookie engine so WinHTTP passes through client Cookie headers
    // and returns upstream Set-Cookie headers untouched.
    DWORD disableCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disableCookies, sizeof(disableCookies));

    // Extract the request payload, if any (any method with a Content-Length).
    std::string body;
    size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        size_t expectedLen = ParseContentLength(request.substr(0, headerEnd));
        body = request.substr(headerEnd + 4);
        while (body.size() < expectedLen) {
            char extraBuf[16384];
            int r = recv(clientSock, extraBuf, sizeof(extraBuf), 0);
            if (r <= 0) break;
            body.append(extraBuf, r);
        }
        if (body.size() > expectedLen) body.resize(expectedLen);
    }

    // Forward client headers (Content-Type, Cookie, User-Agent, Accept, Referer, etc.)
    const std::string upstreamOrigin = GetOrigin(targetUrl);
    const std::string cookiePrefix = CookiePrefix(hostName);
    std::ostringstream forwardHdrStream;
    size_t headerStart = request.find("\r\n");
    if (headerStart != std::string::npos && headerEnd != std::string::npos) {
        std::string rawReqHeaders = request.substr(headerStart + 2, headerEnd - (headerStart + 2));
        std::istringstream hIn(rawReqHeaders);
        std::string hLine;
        while (std::getline(hIn, hLine)) {
            if (!hLine.empty() && hLine.back() == '\r') hLine.pop_back();
            if (hLine.empty()) continue;
            std::string lowerH = hLine;
            std::transform(lowerH.begin(), lowerH.end(), lowerH.begin(), ::tolower);
            if (lowerH.find("host:") == 0 || lowerH.find("connection:") == 0 ||
                lowerH.find("content-length:") == 0 || lowerH.find("accept-encoding:") == 0) {
                continue;
            }
            if (lowerH.find("cookie:") == 0) {
                std::string filtered = FilterCookieHeader(hLine, cookiePrefix);
                if (filtered.empty()) continue;
                hLine = filtered;
            } else if (lowerH.find("referer:") == 0 || lowerH.find("origin:") == 0) {
                // Replace the gateway origin with the actual upstream origin.
                const std::string gatewayOrigins[] = {
                    "http://127.0.0.1:" + std::to_string(s_port),
                    "http://localhost:" + std::to_string(s_port),
                };
                for (const auto& gatewayOrigin : gatewayOrigins) {
                    size_t pos = 0;
                    while ((pos = hLine.find(gatewayOrigin, pos)) != std::string::npos) {
                        hLine.replace(pos, gatewayOrigin.size(), upstreamOrigin);
                        pos += upstreamOrigin.size();
                    }
                }
            }
            forwardHdrStream << hLine << "\r\n";
        }
    }
    // Always request an identity body: the gateway buffers and re-frames every response,
    // so compression would only add a second failure mode.
    forwardHdrStream << "Accept-Encoding: identity\r\n";
    std::string extraHeadersA = forwardHdrStream.str();
    std::wstring extraHeaders;
    if (!extraHeadersA.empty()) {
        int wLen = MultiByteToWideChar(CP_UTF8, 0, extraHeadersA.c_str(), -1, nullptr, 0);
        if (wLen > 0) {
            extraHeaders.resize(wLen);
            MultiByteToWideChar(CP_UTF8, 0, extraHeadersA.c_str(), -1, &extraHeaders[0], wLen);
        }
    }

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str(),
                                       extraHeaders.empty() ? 0 : -1L,
                                       body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                       (DWORD)body.size(),
                                       (DWORD)body.size(), 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    std::string responseBody;
    DWORD statusCode = 0;
    bool tooLarge = false;
    const size_t kMaxResponseBody = 256u * 1024u * 1024u;

    if (bResults) {
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        // Read the entire body before touching the client socket. This guarantees the
        // browser receives a fully framed response and can never observe a truncated script.
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0) break;
            std::vector<char> outBuf(available);
            DWORD downloaded = 0;
            if (!WinHttpReadData(hRequest, outBuf.data(), available, &downloaded) || downloaded == 0) break;
            if (responseBody.size() + downloaded > kMaxResponseBody) {
                tooLarge = true;
                break;
            }
            responseBody.append(outBuf.data(), downloaded);
        }
    }

    if (!bResults || tooLarge) {
        WinHttpCloseHandle(hRequest);
        DropConnect(ctx, hostName, urlComp.nPort);
        SendErrorResponse(clientSock, "502 Bad Gateway",
                          tooLarge ? "Gateway Error: upstream response too large."
                                   : "Gateway Error: WinHTTP failed to connect to legacy TLS server.");
        return;
    }

    DWORD headerSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headerSize, WINHTTP_NO_HEADER_INDEX);

    std::string rawHeaders;
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerSize > 0) {
        std::vector<wchar_t> wHeaderBuf(headerSize / sizeof(wchar_t) + 1);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, wHeaderBuf.data(), &headerSize, WINHTTP_NO_HEADER_INDEX)) {
            int aHeaderLen = WideCharToMultiByte(CP_UTF8, 0, wHeaderBuf.data(), -1, nullptr, 0, nullptr, nullptr);
            if (aHeaderLen > 0) {
                rawHeaders.resize(aHeaderLen - 1);
                WideCharToMultiByte(CP_UTF8, 0, wHeaderBuf.data(), -1, &rawHeaders[0], aHeaderLen, nullptr, nullptr);
            }
        }
    }
    if (rawHeaders.empty()) {
        rawHeaders = "HTTP/1.1 " + std::to_string(statusCode ? statusCode : 200) + " OK\r\n";
    }

    // Filter and clean headers:
    // 1. Remove Transfer-Encoding (WinHttp already de-chunks body, so sending chunked header breaks browser parsing)
    // 2. Remove Connection and replace with Connection: close
    // 3. Remove Content-Length and emit our own after buffering the body
    // 4. Rewrite Location header to keep browser on gateway
    // 5. Namespace Set-Cookie names per upstream host; attributes are unchanged
    std::istringstream hStream(rawHeaders);
    std::string line;
    std::ostringstream cleanedHeaders;
    bool isFirst = true;
    while (std::getline(hStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (isFirst) {
            cleanedHeaders << line << "\r\n";
            isFirst = false;
            continue;
        }
        if (line.empty()) continue;

        std::string lowerLine = line;
        std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);

        if (lowerLine.find("transfer-encoding:") == 0) continue;
        if (lowerLine.find("connection:") == 0) continue;
        if (lowerLine.find("content-length:") == 0) continue;

        if (lowerLine.find("location:") == 0) {
            std::string locVal = line.substr(9);
            while (!locVal.empty() && locVal.front() == ' ') locVal.erase(0, 1);
            if (!locVal.empty()) {
                // Resolve relative redirects against the upstream URL before
                // wrapping, so the browser stays on the gateway without any
                // site-specific host knowledge.
                if (locVal.rfind("http://", 0) != 0 && locVal.rfind("https://", 0) != 0) {
                    size_t schemeEnd = targetUrl.find("://");
                    if (schemeEnd != std::string::npos) {
                        size_t slash = targetUrl.find('/', schemeEnd + 3);
                        std::string base = slash == std::string::npos ? targetUrl : targetUrl.substr(0, slash);
                        if (locVal.front() == '/') {
                            locVal = base + locVal;
                        } else {
                            size_t firstSlash = targetUrl.find('/', schemeEnd + 3);
                            std::string path = firstSlash == std::string::npos ? "" : targetUrl.substr(firstSlash);
                            size_t lastSlash = path.rfind('/');
                            std::string dir = lastSlash == std::string::npos ? "/" : path.substr(0, lastSlash + 1);
                            locVal = base + dir + locVal;
                        }
                    }
                }
                cleanedHeaders << "Location: " << WrapUrl(locVal) << "\r\n";
            }
            continue;
        }

        if (lowerLine.find("set-cookie:") == 0) {
            // Keep Secure, Domain and SameSite attributes exactly as sent by
            // the upstream. Only the cookie name is namespaced per host.
            cleanedHeaders << PrefixSetCookie(line, cookiePrefix) << "\r\n";
            continue;
        }

        cleanedHeaders << line << "\r\n";
    }
    cleanedHeaders << "Content-Length: " << responseBody.size() << "\r\n";
    cleanedHeaders << "Connection: close\r\n\r\n";

    std::string fullResponse = cleanedHeaders.str();
    fullResponse += responseBody;

    WinHttpCloseHandle(hRequest);

    if (cacheable && statusCode == 200 && !responseBody.empty()) {
        CachePut(targetUrl, fullResponse);
    }

    if (SendAll(clientSock, fullResponse)) {
        CloseGracefully(clientSock);
    } else {
        closesocket(clientSock);
    }
}


static void ServerLoop() {
    while (s_running.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s_listenSocket, &readSet);
        timeval tv = { 1, 0 };
        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(s_listenSocket, &readSet)) {
            SOCKET client = accept(s_listenSocket, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                // HandleClient closes `client` on every return path, so the
                // worker must not close it again. Rejected connections are
                // closed here so Servo sees an immediate failure and retries.
                if (s_activeClients.fetch_add(1) < kMaxConcurrentClients) {
                    std::thread([client]() {
                        HandleClient(client);
                        s_activeClients.fetch_sub(1);
                    }).detach();
                } else {
                    s_activeClients.fetch_sub(1);
                    closesocket(client);
                }
            }
        }
    }
}

bool Start(unsigned short port) {
    if (s_running.load()) return true;
    s_port = port;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(s_port);

    if (bind(s_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    if (listen(s_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_contextMutex);
        while (s_freeContexts.size() < static_cast<size_t>(kGatewayContexts)) {
            s_freeContexts.push_back(std::make_unique<GatewayContext>());
        }
    }

    LoadAllowlist();
    s_running.store(true);
    s_workerThread = std::thread(ServerLoop);
    return true;
}

void Stop() {
    if (!s_running.load()) return;
    s_running.store(false);
    s_contextCv.notify_all();
    if (s_listenSocket != INVALID_SOCKET) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
    }
    if (s_workerThread.joinable()) {
        s_workerThread.join();
    }
    WSACleanup();
}

unsigned short GetPort() {
    return s_port;
}

bool IsGatewayUrl(const std::string& url) {
    const std::string port = std::to_string(s_port);
    return url.find("http://127.0.0.1:" + port) == 0 ||
           url.find("http://localhost:" + port) == 0;
}

std::string WrapUrl(const std::string& originalUrl) {
    if (IsGatewayUrl(originalUrl)) return originalUrl;
    return "http://127.0.0.1:" + std::to_string(s_port) + "/gateway?url=" + UrlEncode(originalUrl);
}

std::string UnwrapUrl(const std::string& url) {
    if (!IsGatewayUrl(url)) return url;
    size_t qPos = url.find("?url=");
    if (qPos != std::string::npos) {
        return UrlDecode(url.substr(qPos + 5));
    }
    return url;
}

} // namespace SchannelGateway
