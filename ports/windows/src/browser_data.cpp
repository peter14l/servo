/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "browser_data.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

std::wstring BrowserData::GetConfigDirectory() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring baseDir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(baseDir.c_str(), nullptr);
        return baseDir;
    }
    return L".";
}

static std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

static std::string UnescapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            char next = input[++i];
            if (next == '"') out += '"';
            else if (next == '\\') out += '\\';
            else if (next == '/') out += '/';
            else if (next == 'b') out += '\b';
            else if (next == 'f') out += '\f';
            else if (next == 'n') out += '\n';
            else if (next == 'r') out += '\r';
            else if (next == 't') out += '\t';
            else out += next;
        } else {
            out += input[i];
        }
    }
    return out;
}

static std::string ExtractJsonField(const std::string& obj, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == ':' || obj[pos] == '\t')) pos++;
    if (pos >= obj.size()) return "";

    if (obj[pos] == '"') {
        pos++;
        size_t end = pos;
        while (end < obj.size()) {
            if (obj[end] == '"' && obj[end - 1] != '\\') break;
            end++;
        }
        return UnescapeJsonString(obj.substr(pos, end - pos));
    } else {
        size_t end = pos;
        while (end < obj.size() && obj[end] != ',' && obj[end] != '}' && obj[end] != ' ' && obj[end] != '\r' && obj[end] != '\n') {
            end++;
        }
        return obj.substr(pos, end - pos);
    }
}

std::vector<BookmarkItem> BrowserData::LoadBookmarks() {
    std::vector<BookmarkItem> items;
    std::wstring path = GetConfigDirectory() + L"\\bookmarks.json";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return items;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        std::string obj = content.substr(pos, end - pos + 1);

        std::string url = ExtractJsonField(obj, "url");
        std::string title = ExtractJsonField(obj, "title");
        std::string tsStr = ExtractJsonField(obj, "timestamp");
        uint64_t ts = 0;
        try {
            if (!tsStr.empty()) ts = std::stoull(tsStr);
        } catch (...) {}

        if (!url.empty()) {
            if (title.empty()) title = url;
            items.push_back({ url, title, ts });
        }
        pos = end + 1;
    }
    return items;
}

void BrowserData::SaveBookmarks(const std::vector<BookmarkItem>& items) {
    std::wstring path = GetConfigDirectory() + L"\\bookmarks.json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        file << "  {\n";
        file << "    \"url\": \"" << EscapeJsonString(items[i].url) << "\",\n";
        file << "    \"title\": \"" << EscapeJsonString(items[i].title) << "\",\n";
        file << "    \"timestamp\": " << items[i].timestamp << "\n";
        file << "  }" << (i + 1 < items.size() ? ",\n" : "\n");
    }
    file << "]\n";
}

bool BrowserData::IsBookmarked(const std::string& url) {
    if (url.empty() || url == "about:blank" || url.rfind("servo:", 0) == 0 || url.rfind("about:", 0) == 0) {
        return false;
    }
    auto items = LoadBookmarks();
    for (const auto& item : items) {
        if (item.url == url) return true;
    }
    return false;
}

bool BrowserData::ToggleBookmark(const std::string& url, const std::string& title) {
    if (url.empty() || url == "about:blank" || url.rfind("servo:", 0) == 0 || url.rfind("about:", 0) == 0) {
        return false;
    }
    auto items = LoadBookmarks();
    auto it = std::find_if(items.begin(), items.end(), [&](const BookmarkItem& b) {
        return b.url == url;
    });

    if (it != items.end()) {
        items.erase(it);
        SaveBookmarks(items);
        return false; // Removed
    } else {
        uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1));
        std::string effectiveTitle = title.empty() ? url : title;
        items.push_back({ url, effectiveTitle, ts });
        SaveBookmarks(items);
        return true; // Added
    }
}

void BrowserData::AddBookmark(const std::string& url, const std::string& title) {
    if (url.empty() || url == "about:blank" || url.rfind("servo:", 0) == 0 || url.rfind("about:", 0) == 0) return;
    auto items = LoadBookmarks();
    if (!std::any_of(items.begin(), items.end(), [&](const BookmarkItem& b) { return b.url == url; })) {
        uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1));
        std::string effectiveTitle = title.empty() ? url : title;
        items.push_back({ url, effectiveTitle, ts });
        SaveBookmarks(items);
    }
}

void BrowserData::RemoveBookmark(const std::string& url) {
    auto items = LoadBookmarks();
    auto it = std::remove_if(items.begin(), items.end(), [&](const BookmarkItem& b) {
        return b.url == url;
    });
    if (it != items.end()) {
        items.erase(it, items.end());
        SaveBookmarks(items);
    }
}

std::vector<HistoryItem> BrowserData::LoadHistory() {
    std::vector<HistoryItem> items;
    std::wstring path = GetConfigDirectory() + L"\\history.json";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return items;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        std::string obj = content.substr(pos, end - pos + 1);

        std::string url = ExtractJsonField(obj, "url");
        std::string title = ExtractJsonField(obj, "title");
        std::string tsStr = ExtractJsonField(obj, "timestamp");
        uint64_t ts = 0;
        try {
            if (!tsStr.empty()) ts = std::stoull(tsStr);
        } catch (...) {}

        if (!url.empty()) {
            if (title.empty()) title = url;
            items.push_back({ url, title, ts });
        }
        pos = end + 1;
    }
    return items;
}

void BrowserData::AddHistory(const std::string& url, const std::string& title) {
    if (url.empty() || url == "about:blank" || url.rfind("servo:", 0) == 0 || url.rfind("about:", 0) == 0 || url.rfind("file://", 0) == 0) {
        return;
    }
    auto items = LoadHistory();
    if (!items.empty() && items.back().url == url) {
        return; // Avoid recording duplicate consecutive navigations
    }

    uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1));
    std::string effectiveTitle = title.empty() ? url : title;
    items.push_back({ url, effectiveTitle, ts });

    if (items.size() > 500) {
        items.erase(items.begin(), items.begin() + (items.size() - 500));
    }

    std::wstring path = GetConfigDirectory() + L"\\history.json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        file << "  {\n";
        file << "    \"url\": \"" << EscapeJsonString(items[i].url) << "\",\n";
        file << "    \"title\": \"" << EscapeJsonString(items[i].title) << "\",\n";
        file << "    \"timestamp\": " << items[i].timestamp << "\n";
        file << "  }" << (i + 1 < items.size() ? ",\n" : "\n");
    }
    file << "]\n";
}

void BrowserData::ClearHistory() {
    std::wstring path = GetConfigDirectory() + L"\\history.json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file << "[]\n";
    }
}

static const char* COMMON_PAGE_CSS = R"CSS(
:root {
  --bg: #1c1c1f;
  --card-bg: rgba(255, 255, 255, 0.06);
  --card-border: rgba(255, 255, 255, 0.1);
  --text: #f0f0f4;
  --text-muted: #8e8e93;
  --accent: #0078d4;
  --accent-hover: #1084d8;
  --danger: #e81123;
  --radius: 10px;
}
@media (prefers-color-scheme: light) {
  :root {
    --bg: #f3f3f3;
    --card-bg: #ffffff;
    --card-border: rgba(0, 0, 0, 0.08);
    --text: #1c1c1e;
    --text-muted: #6e6e73;
    --accent: #0067b8;
    --accent-hover: #005da6;
  }
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: 'Segoe UI Variable Text', 'Segoe UI', system-ui, -apple-system, sans-serif;
  background: var(--bg);
  color: var(--text);
  padding: 40px 20px;
  display: flex;
  justify-content: center;
}
.container {
  width: 100%;
  max-width: 800px;
}
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;
}
.title-group {
  display: flex;
  align-items: center;
  gap: 12px;
}
.title-group h1 {
  font-size: 28px;
  font-weight: 600;
}
.search-box {
  width: 100%;
  padding: 10px 14px;
  border-radius: var(--radius);
  border: 1px solid var(--card-border);
  background: var(--card-bg);
  color: var(--text);
  font-size: 14px;
  margin-bottom: 20px;
  outline: none;
}
.search-box:focus {
  border-color: var(--accent);
}
.item-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.item-card {
  background: var(--card-bg);
  border: 1px solid var(--card-border);
  border-radius: var(--radius);
  padding: 14px 18px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  text-decoration: none;
  color: inherit;
  transition: background 0.15s, border-color 0.15s;
}
.item-card:hover {
  border-color: var(--accent);
}
.item-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
  overflow: hidden;
}
.item-title {
  font-size: 15px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.item-url {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.btn {
  padding: 8px 16px;
  border-radius: 6px;
  border: 1px solid var(--card-border);
  background: var(--card-bg);
  color: var(--text);
  font-size: 13px;
  cursor: pointer;
  text-decoration: none;
  transition: all 0.15s;
}
.btn:hover {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}
.btn-danger:hover {
  background: var(--danger);
  border-color: var(--danger);
}
.empty-state {
  text-align: center;
  padding: 60px 20px;
  color: var(--text-muted);
}
.nav-footer {
  margin-top: 36px;
  padding-top: 18px;
  border-top: 1px solid var(--card-border);
  display: flex;
  gap: 16px;
  justify-content: center;
  font-size: 13px;
}
.nav-footer a {
  color: var(--accent);
  text-decoration: none;
}
.nav-footer a:hover {
  text-decoration: underline;
}
)CSS";

std::string BrowserData::EnsureBookmarksPageExists() {
    auto bookmarks = LoadBookmarks();
    std::wstring filePath = GetConfigDirectory() + L"\\bookmarks.html";
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return "about:blank";

    out << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>Bookmarks - Servo</title><style>"
        << COMMON_PAGE_CSS << "</style></head><body>\n"
        << "<div class=\"container\">\n"
        << "  <div class=\"header\">\n"
        << "    <div class=\"title-group\"><h1>Bookmarks</h1></div>\n"
        << "  </div>\n"
        << "  <input type=\"text\" class=\"search-box\" id=\"search\" placeholder=\"Search bookmarks...\" oninput=\"filterBookmarks()\">\n"
        << "  <div class=\"item-list\" id=\"list\">\n";

    if (bookmarks.empty()) {
        out << "    <div class=\"empty-state\"><p>No bookmarks saved yet. Click the star &#9733; in the address bar or press <b>Ctrl+D</b> to bookmark any page!</p></div>\n";
    } else {
        for (auto it = bookmarks.rbegin(); it != bookmarks.rend(); ++it) {
            out << "    <a class=\"item-card\" href=\"" << it->url << "\">\n"
                << "      <div class=\"item-info\">\n"
                << "        <span class=\"item-title\">" << it->title << "</span>\n"
                << "        <span class=\"item-url\">" << it->url << "</span>\n"
                << "      </div>\n"
                << "    </a>\n";
        }
    }

    out << "  </div>\n"
        << "  <div class=\"nav-footer\">\n"
        << "    <a href=\"servo://newtab\">New Tab</a> &bull; "
        << "    <a href=\"servo://history\">History</a> &bull; "
        << "    <a href=\"servo://settings\">Settings</a> &bull; "
        << "    <a href=\"servo://about\">About Servo</a>\n"
        << "  </div>\n"
        << "</div>\n"
        << "<script>\n"
        << "function filterBookmarks() {\n"
        << "  var q = document.getElementById('search').value.toLowerCase();\n"
        << "  var cards = document.querySelectorAll('.item-card');\n"
        << "  cards.forEach(function(card) {\n"
        << "    var text = card.textContent.toLowerCase();\n"
        << "    card.style.display = text.includes(q) ? 'flex' : 'none';\n"
        << "  });\n"
        << "}\n"
        << "</script>\n"
        << "</body></html>\n";
    out.close();

    char utf8Path[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);
    std::string pathStr = utf8Path;
    for (char& c : pathStr) if (c == '\\') c = '/';
    return "file:///" + pathStr;
}

std::string BrowserData::EnsureHistoryPageExists() {
    auto history = LoadHistory();
    std::wstring filePath = GetConfigDirectory() + L"\\history.html";
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return "about:blank";

    out << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>History - Servo</title><style>"
        << COMMON_PAGE_CSS << "</style></head><body>\n"
        << "<div class=\"container\">\n"
        << "  <div class=\"header\">\n"
        << "    <div class=\"title-group\"><h1>Browsing History</h1></div>\n"
        << "    <button class=\"btn btn-danger\" onclick=\"window.location.href='servo://clear-history'\">Clear History</button>\n"
        << "  </div>\n"
        << "  <input type=\"text\" class=\"search-box\" id=\"search\" placeholder=\"Search history...\" oninput=\"filterHistory()\">\n"
        << "  <div class=\"item-list\" id=\"list\">\n";

    if (history.empty()) {
        out << "    <div class=\"empty-state\"><p>No browsing history recorded yet.</p></div>\n";
    } else {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            out << "    <a class=\"item-card\" href=\"" << it->url << "\">\n"
                << "      <div class=\"item-info\">\n"
                << "        <span class=\"item-title\">" << it->title << "</span>\n"
                << "        <span class=\"item-url\">" << it->url << "</span>\n"
                << "      </div>\n"
                << "    </a>\n";
        }
    }

    out << "  </div>\n"
        << "  <div class=\"nav-footer\">\n"
        << "    <a href=\"servo://newtab\">New Tab</a> &bull; "
        << "    <a href=\"servo://bookmarks\">Bookmarks</a> &bull; "
        << "    <a href=\"servo://settings\">Settings</a> &bull; "
        << "    <a href=\"servo://about\">About Servo</a>\n"
        << "  </div>\n"
        << "</div>\n"
        << "<script>\n"
        << "function filterHistory() {\n"
        << "  var q = document.getElementById('search').value.toLowerCase();\n"
        << "  var cards = document.querySelectorAll('.item-card');\n"
        << "  cards.forEach(function(card) {\n"
        << "    var text = card.textContent.toLowerCase();\n"
        << "    card.style.display = text.includes(q) ? 'flex' : 'none';\n"
        << "  });\n"
        << "}\n"
        << "</script>\n"
        << "</body></html>\n";
    out.close();

    char utf8Path[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);
    std::string pathStr = utf8Path;
    for (char& c : pathStr) if (c == '\\') c = '/';
    return "file:///" + pathStr;
}

std::string BrowserData::EnsureAboutPageExists() {
    std::wstring filePath = GetConfigDirectory() + L"\\about.html";
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return "about:blank";

    out << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>About - Servo</title><style>"
        << COMMON_PAGE_CSS
        << ".about-card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: var(--radius); padding: 24px; margin-bottom: 20px; line-height: 1.6; }\n"
        << ".about-card h2 { font-size: 20px; margin-bottom: 12px; font-weight: 600; }\n"
        << ".about-card p { margin-bottom: 8px; color: var(--text-muted); font-size: 14px; }\n"
        << ".about-card b { color: var(--text); }\n"
        << "</style></head><body>\n"
        << "<div class=\"container\">\n"
        << "  <div class=\"header\">\n"
        << "    <div class=\"title-group\"><h1>About Servo</h1></div>\n"
        << "  </div>\n"
        << "  <div class=\"about-card\">\n"
        << "    <h2>Servo Browser (Native Windows Port)</h2>\n"
        << "    <p><b>Engine:</b> Servo Web Rendering Engine (Rust + SpiderMonkey)</p>\n"
        << "    <p><b>Shell:</b> Windows 11 Native C++ Shell (Mica Alt & DirectComposition)</p>\n"
        << "    <p><b>Graphics:</b> WebRender DirectWrite / ANGLE GLES backend</p>\n"
        << "    <p><b>Storage:</b> SQLite WebStorage & IndexedDB (%LOCALAPPDATA%\\Servo Browser)</p>\n"
        << "  </div>\n"
        << "  <div class=\"about-card\">\n"
        << "    <h2>Features & Shortcuts</h2>\n"
        << "    <p><b>Ctrl+T:</b> New Tab &nbsp;|&nbsp; <b>Ctrl+W:</b> Close Tab &nbsp;|&nbsp; <b>Ctrl+Shift+T:</b> Reopen Closed Tab</p>\n"
        << "    <p><b>Ctrl+D:</b> Bookmark Page &nbsp;|&nbsp; <b>Ctrl+H:</b> History &nbsp;|&nbsp; <b>Ctrl+J:</b> Downloads</p>\n"
        << "    <p><b>Ctrl+L / Alt+D:</b> Focus Omnibox &nbsp;|&nbsp; <b>Ctrl+F:</b> Find in Page &nbsp;|&nbsp; <b>F12:</b> DevTools</p>\n"
        << "    <p><b>F11:</b> Fullscreen &nbsp;|&nbsp; <b>Ctrl++ / Ctrl+-:</b> Zoom In/Out</p>\n"
        << "  </div>\n"
        << "  <div class=\"nav-footer\">\n"
        << "    <a href=\"servo://newtab\">New Tab</a> &bull; "
        << "    <a href=\"servo://bookmarks\">Bookmarks</a> &bull; "
        << "    <a href=\"servo://history\">History</a> &bull; "
        << "    <a href=\"servo://settings\">Settings</a>\n"
        << "  </div>\n"
        << "</div>\n"
        << "</body></html>\n";
    out.close();

    char utf8Path[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);
    std::string pathStr = utf8Path;
    for (char& c : pathStr) if (c == '\\') c = '/';
    return "file:///" + pathStr;
}
