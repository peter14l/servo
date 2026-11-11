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

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <mutex>
#include <regex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace SchannelGateway {

static std::atomic<bool> s_running{false};
static std::thread s_workerThread;
static SOCKET s_listenSocket = INVALID_SOCKET;
static unsigned short s_port = 51820;

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

static void HandleClient(SOCKET clientSock) {
    DWORD timeout = 15000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    std::vector<char> buffer(65536);
    int bytesRead = recv(clientSock, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
    if (bytesRead <= 0) {
        closesocket(clientSock);
        return;
    }
    buffer[bytesRead] = '\0';
    std::string request(buffer.data(), bytesRead);

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
    } else if (!uri.empty() && uri[0] == '/') {
        // Direct path preservation: /services/student-registration.aspx, /Style/style.css, /services/Login.aspx
        // Handle common root-relative login redirect if requested as /Login.aspx -> /services/Login.aspx
        if (uri == "/Login.aspx" || uri.find("/Login.aspx?") == 0) {
            std::string redirectTarget = "/services" + uri;
            std::string redir = "HTTP/1.1 302 Found\r\nLocation: " + redirectTarget + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(clientSock, redir.c_str(), (int)redir.size(), 0);
            closesocket(clientSock);
            return;
        } else {
            targetUrl = "https://www.sxuk.org" + uri;
        }
    }

    if (targetUrl.empty()) {
        const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSock, notFound, (int)strlen(notFound), 0);
        closesocket(clientSock);
        return;
    }

    // Convert target URL to wide characters
    int wlen = MultiByteToWideChar(CP_UTF8, 0, targetUrl.c_str(), -1, nullptr, 0);
    std::wstring wTargetUrl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, targetUrl.c_str(), -1, &wTargetUrl[0], wlen);

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {};
    wchar_t urlPath[2048] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = ARRAYSIZE(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);

    if (!WinHttpCrackUrl(wTargetUrl.c_str(), 0, 0, &urlComp)) {
        const char* badReq = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSock, badReq, (int)strlen(badReq), 0);
        closesocket(clientSock);
        return;
    }

    HINTERNET hSession = WinHttpOpen(L"Servo-Schannel-Bridge/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        closesocket(clientSock);
        return;
    }

    // Enable TLS 1.2
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        closesocket(clientSock);
        return;
    }

    int methodWLen = MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, nullptr, 0);
    std::wstring wMethod(methodWLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, &wMethod[0], methodWLen);

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), urlComp.lpszUrlPath,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        closesocket(clientSock);
        return;
    }

    // Defensive option to allow legacy server certificates if needed
    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    // Do NOT follow redirects automatically in WinHTTP!
    // The browser must receive 302/301 responses with Set-Cookie headers intact,
    // and update its navigation location so that relative URLs and session states remain consistent.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    // Disable WinHTTP's internal cookie engine so WinHTTP passes through client Cookie headers
    // and returns upstream Set-Cookie headers untouched.
    DWORD disableCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disableCookies, sizeof(disableCookies));

    // Extract POST payload if present
    std::string body;
    size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos && method == "POST") {
        body = request.substr(headerEnd + 4);
        size_t clPos = request.find("Content-Length: ");
        if (clPos != std::string::npos) {
            size_t clEnd = request.find("\r\n", clPos);
            if (clEnd != std::string::npos) {
                int expectedLen = std::atoi(request.substr(clPos + 16, clEnd - (clPos + 16)).c_str());
                while ((int)body.size() < expectedLen) {
                    char extraBuf[4096];
                    int r = recv(clientSock, extraBuf, sizeof(extraBuf), 0);
                    if (r <= 0) break;
                    body.append(extraBuf, r);
                }
            }
        }
    }

    // Forward client headers (Content-Type, Cookie, User-Agent, Accept, Referer, etc.)
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
            if (lowerH.find("host:") == 0 || lowerH.find("connection:") == 0 || lowerH.find("content-length:") == 0) {
                continue;
            }
            if (lowerH.find("referer:") == 0 || lowerH.find("origin:") == 0) {
                // Rewrite 127.0.0.1:51820 to www.sxuk.org and http:// to https://
                std::string portStr = std::to_string(s_port);
                size_t p = hLine.find("127.0.0.1:" + portStr);
                if (p != std::string::npos) {
                    hLine.replace(p, 10 + portStr.length(), "www.sxuk.org");
                }
                size_t hp = hLine.find("http://www.sxuk.org");
                if (hp != std::string::npos) {
                    hLine.replace(hp, 19, "https://www.sxuk.org");
                }
            }
            forwardHdrStream << hLine << "\r\n";
        }
    }
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

    if (bResults) {
        DWORD headerSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headerSize, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerSize > 0) {
            std::vector<wchar_t> wHeaderBuf(headerSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, wHeaderBuf.data(), &headerSize, WINHTTP_NO_HEADER_INDEX)) {
                int aHeaderLen = WideCharToMultiByte(CP_UTF8, 0, wHeaderBuf.data(), -1, nullptr, 0, nullptr, nullptr);
                if (aHeaderLen > 0) {
                    std::string rawHeaders(aHeaderLen - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wHeaderBuf.data(), -1, &rawHeaders[0], aHeaderLen, nullptr, nullptr);

                    // Filter and clean headers:
                    // 1. Remove Transfer-Encoding (WinHttp already de-chunks body, so sending chunked header breaks browser parsing)
                    // 2. Remove Connection and replace with Connection: close
                    // 3. Rewrite Location header to keep browser on gateway
                    // 4. Strip '; secure' and '; domain=...' from Set-Cookie so the browser retains cookies on 127.0.0.1
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

                        if (lowerLine.find("location:") == 0) {
                            // Rewrite Location: https://www.sxuk.org/path -> http://127.0.0.1:51820/path
                            std::string locVal = line.substr(9);
                            while (!locVal.empty() && locVal.front() == ' ') locVal.erase(0, 1);
                            std::string rewritten = locVal;
                            size_t sxPos = rewritten.find("sxuk.org");
                            if (sxPos != std::string::npos) {
                                size_t slashPos = rewritten.find('/', sxPos);
                                if (slashPos != std::string::npos) {
                                    rewritten = "http://127.0.0.1:" + std::to_string(s_port) + rewritten.substr(slashPos);
                                }
                            } else if (!rewritten.empty() && rewritten[0] == '/') {
                                rewritten = "http://127.0.0.1:" + std::to_string(s_port) + rewritten;
                            } else if (!rewritten.empty() && rewritten.find("http://") != 0 && rewritten.find("https://") != 0) {
                                // Relative redirect like "StudentHome.aspx"
                                std::string dir = "/";
                                size_t lastSlash = uri.rfind('/');
                                if (lastSlash != std::string::npos) {
                                    dir = uri.substr(0, lastSlash + 1);
                                }
                                rewritten = "http://127.0.0.1:" + std::to_string(s_port) + dir + rewritten;
                            }
                            cleanedHeaders << "Location: " << rewritten << "\r\n";
                            continue;
                        }

                        if (lowerLine.find("set-cookie:") == 0) {
                            std::string cookieVal = line;
                            std::regex secRegex(R"(;\s*secure)", std::regex_constants::icase);
                            cookieVal = std::regex_replace(cookieVal, secRegex, "");
                            std::regex domRegex(R"(;\s*domain=[^;]+)", std::regex_constants::icase);
                            cookieVal = std::regex_replace(cookieVal, domRegex, "");
                            std::regex sameSiteNone(R"(;\s*samesite=none)", std::regex_constants::icase);
                            cookieVal = std::regex_replace(cookieVal, sameSiteNone, "; SameSite=Lax");
                            cleanedHeaders << cookieVal << "\r\n";
                            continue;
                        }

                        cleanedHeaders << line << "\r\n";
                    }
                    cleanedHeaders << "Connection: close\r\n\r\n";
                    std::string finalHdr = cleanedHeaders.str();
                    send(clientSock, finalHdr.c_str(), (int)finalHdr.size(), 0);
                }
            }
        }

        // Stream body data
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                std::vector<char> outBuf(dwSize);
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, outBuf.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
                    send(clientSock, outBuf.data(), (int)dwDownloaded, 0);
                }
            }
        } while (dwSize > 0);
    } else {
        const char* gatewayErr = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nGateway Error: WinHTTP failed to connect to legacy TLS server.";
        send(clientSock, gatewayErr, (int)strlen(gatewayErr), 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    closesocket(clientSock);
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
                std::thread(HandleClient, client).detach();
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

    s_running.store(true);
    s_workerThread = std::thread(ServerLoop);
    return true;
}

void Stop() {
    if (!s_running.load()) return;
    s_running.store(false);
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

bool ShouldUseGateway(const std::string& url) {
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    // Legacy servers requiring outdated CBC/SHA-1 cipher suites rejected by modern Rustls
    if (lower.find("sxuk.org") != std::string::npos) return true;
    return false;
}

bool IsGatewayUrl(const std::string& url) {
    return url.find("http://127.0.0.1:51820") == 0 ||
           url.find("http://localhost:51820") == 0;
}

std::string WrapUrl(const std::string& originalUrl) {
    if (IsGatewayUrl(originalUrl)) return originalUrl;

    // Direct path preservation: https://www.sxuk.org/path -> http://127.0.0.1:51820/path
    // This guarantees that relative links (Login.aspx) and relative assets (../Style/style.css)
    // resolve to their exact matching paths on the gateway!
    const std::string prefixes[] = {
        "https://www.sxuk.org",
        "http://www.sxuk.org",
        "https://sxuk.org",
        "http://sxuk.org"
    };

    for (const auto& prefix : prefixes) {
        if (originalUrl.find(prefix) == 0) {
            std::string path = originalUrl.substr(prefix.length());
            if (path.empty() || path[0] != '/') path = "/" + path;
            return "http://127.0.0.1:" + std::to_string(s_port) + path;
        }
    }

    return "http://127.0.0.1:" + std::to_string(s_port) + "/gateway?url=" + UrlEncode(originalUrl);
}

std::string UnwrapUrl(const std::string& url) {
    if (!IsGatewayUrl(url)) return url;
    size_t qPos = url.find("?url=");
    if (qPos != std::string::npos) {
        return UrlDecode(url.substr(qPos + 5));
    }
    // Unwrap http://127.0.0.1:51820/path -> https://www.sxuk.org/path
    size_t portPos = url.find(":51820");
    if (portPos != std::string::npos) {
        std::string path = url.substr(portPos + 6);
        return "https://www.sxuk.org" + path;
    }
    return url;
}

} // namespace SchannelGateway
