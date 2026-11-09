/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct BookmarkItem {
    std::string url;
    std::string title;
    uint64_t timestamp = 0;
};

struct HistoryItem {
    std::string url;
    std::string title;
    uint64_t timestamp = 0;
};

class BrowserData {
public:
    static std::vector<BookmarkItem> LoadBookmarks();
    static void SaveBookmarks(const std::vector<BookmarkItem>& items);
    static bool IsBookmarked(const std::string& url);
    static bool ToggleBookmark(const std::string& url, const std::string& title);
    static void AddBookmark(const std::string& url, const std::string& title);
    static void RemoveBookmark(const std::string& url);

    static std::vector<HistoryItem> LoadHistory();
    static void AddHistory(const std::string& url, const std::string& title);
    static void ClearHistory();

    static std::string EnsureBookmarksPageExists();
    static std::string EnsureHistoryPageExists();
    static std::string EnsureAboutPageExists();

private:
    static std::wstring GetConfigDirectory();
};
