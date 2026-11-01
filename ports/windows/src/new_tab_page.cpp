/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "new_tab_page.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

static const char* NEW_TAB_HTML_CONTENT = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>New Tab</title>
<style>
  :root {
    --bg-gradient: radial-gradient(circle at 50% 20%, #fbfbfb 0%, #eceff1 100%);
    --card-bg: rgba(255, 255, 255, 0.85);
    --card-border: rgba(0, 0, 0, 0.08);
    --card-shadow: 0 4px 16px rgba(0, 0, 0, 0.05);
    --text-primary: #1a1a1a;
    --text-secondary: #5f6368;
    --search-bg: #ffffff;
    --search-border: #dadce0;
    --search-focus-border: #0067c0;
    --search-focus-glow: rgba(0, 103, 192, 0.25);
    --tile-hover: #f1f3f4;
    --tile-border: rgba(0, 0, 0, 0.06);
    --accent: #0067c0;
  }

  @media (prefers-color-scheme: dark) {
    :root {
      --bg-gradient: radial-gradient(circle at 50% 20%, #202023 0%, #121214 100%);
      --card-bg: rgba(36, 36, 39, 0.75);
      --card-border: rgba(255, 255, 255, 0.08);
      --card-shadow: 0 4px 24px rgba(0, 0, 0, 0.4);
      --text-primary: #f5f5f5;
      --text-secondary: #9aa0a6;
      --search-bg: rgba(45, 45, 48, 0.85);
      --search-border: rgba(255, 255, 255, 0.12);
      --search-focus-border: #60cdff;
      --search-focus-glow: rgba(96, 205, 255, 0.3);
      --tile-hover: rgba(255, 255, 255, 0.08);
      --tile-border: rgba(255, 255, 255, 0.06);
      --accent: #60cdff;
    }
  }

  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }

  body {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    background: var(--bg-gradient);
    color: var(--text-primary);
    font-family: 'Segoe UI Variable Display', 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    user-select: none;
    -webkit-user-select: none;
    overflow-x: hidden;
  }

  .container {
    width: 100%;
    max-width: 680px;
    padding: 40px 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
  }

  .header {
    margin-bottom: 32px;
  }

  .greeting {
    font-size: 2.2rem;
    font-weight: 600;
    letter-spacing: -0.02em;
    margin-bottom: 6px;
    color: var(--text-primary);
  }

  .date-display {
    font-size: 0.95rem;
    font-weight: 400;
    color: var(--text-secondary);
  }

  .search-wrapper {
    width: 100%;
    margin-bottom: 36px;
  }

  .search-form {
    width: 100%;
    position: relative;
    display: flex;
    align-items: center;
  }

  .search-icon {
    position: absolute;
    left: 20px;
    width: 18px;
    height: 18px;
    fill: var(--text-secondary);
    pointer-events: none;
  }

  .search-input {
    width: 100%;
    height: 52px;
    padding: 0 20px 0 52px;
    font-size: 1.05rem;
    font-family: inherit;
    color: var(--text-primary);
    background: var(--search-bg);
    border: 1px solid var(--search-border);
    border-radius: 26px;
    box-shadow: var(--card-shadow);
    outline: none;
    transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  }

  .search-input:focus {
    border-color: var(--search-focus-border);
    box-shadow: 0 0 0 4px var(--search-focus-glow), var(--card-shadow);
  }

  .search-input::placeholder {
    color: var(--text-secondary);
  }

  .shortcuts-grid {
    display: grid;
    grid-template-columns: repeat(6, 1fr);
    gap: 16px;
    width: 100%;
  }

  @media (max-width: 600px) {
    .shortcuts-grid {
      grid-template-columns: repeat(3, 1fr);
      gap: 12px;
    }
  }

  .tile {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 14px 10px;
    border-radius: 14px;
    background: var(--card-bg);
    border: 1px solid var(--tile-border);
    text-decoration: none;
    color: var(--text-primary);
    transition: all 0.18s cubic-bezier(0.16, 1, 0.3, 1);
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.03);
  }

  .tile:hover {
    transform: translateY(-2px);
    background: var(--tile-hover);
    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.08);
  }

  .tile-icon-box {
    width: 44px;
    height: 44px;
    border-radius: 11px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-bottom: 8px;
  }

  .tile-title {
    font-size: 0.8rem;
    font-weight: 500;
    color: var(--text-secondary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 80px;
  }

  .tile:hover .tile-title {
    color: var(--text-primary);
  }

  /* Shortcut icons */
  .icon-instagram {
    background: linear-gradient(45deg, #f09433 0%, #e6683c 25%, #dc2743 50%, #cc2366 75%, #bc1888 100%);
    color: #fff;
  }
  .icon-youtube {
    background: #ff0000;
    color: #fff;
  }
  .icon-github {
    background: #24292e;
    color: #fff;
  }
  .icon-reddit {
    background: #ff4500;
    color: #fff;
  }
  .icon-wikipedia {
    background: #ffffff;
    color: #000;
    border: 1px solid #ccc;
  }
  .icon-settings {
    background: #0078d4;
    color: #fff;
  }

  svg {
    width: 22px;
    height: 22px;
    fill: currentColor;
  }
</style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1 class="greeting" id="greeting">Welcome</h1>
      <p class="date-display" id="dateDisplay"></p>
    </div>

    <div class="search-wrapper">
      <form class="search-form" id="searchForm" autocomplete="off">
        <svg class="search-icon" viewBox="0 0 24 24">
          <path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/>
        </svg>
        <input
          type="text"
          class="search-input"
          id="searchInput"
          placeholder="Search DuckDuckGo or enter URL..."
          autofocus
        />
      </form>
    </div>

    <div class="shortcuts-grid">
      <a class="tile" href="https://www.instagram.com" title="Instagram">
        <div class="tile-icon-box icon-instagram">
          <svg viewBox="0 0 24 24"><path d="M12 2.163c3.204 0 3.584.012 4.85.07 3.252.148 4.771 1.691 4.919 4.919.058 1.265.069 1.645.069 4.849 0 3.205-.012 3.584-.069 4.849-.149 3.225-1.664 4.771-4.919 4.919-1.266.058-1.644.07-4.85.07-3.204 0-3.584-.012-4.849-.07-3.26-.149-4.771-1.699-4.919-4.92-.058-1.265-.07-1.644-.07-4.849 0-3.204.013-3.583.07-4.849.149-3.227 1.664-4.771 4.919-4.919 1.266-.057 1.645-.069 4.849-.069zm0-2.163c-3.259 0-3.667.014-4.947.072-4.358.2-6.78 2.618-6.98 6.98-.059 1.281-.073 1.689-.073 4.948 0 3.259.014 3.668.072 4.948.2 4.358 2.618 6.78 6.98 6.98 1.281.058 1.689.072 4.948.072 3.259 0 3.668-.014 4.948-.072 4.354-.2 6.782-2.618 6.979-6.98.059-1.28.073-1.689.073-4.948 0-3.259-.014-3.667-.072-4.947-.196-4.354-2.617-6.78-6.979-6.98-1.281-.059-1.69-.073-4.949-.073zm0 5.838c-3.403 0-6.162 2.759-6.162 6.162s2.759 6.163 6.162 6.163 6.162-2.759 6.162-6.163c0-3.403-2.759-6.162-6.162-6.162zm0 10.162c-2.209 0-4-1.79-4-4 0-2.209 1.791-4 4-4s4 1.791 4 4c0 2.21-1.791 4-4 4zm6.406-11.845c-.796 0-1.441.645-1.441 1.44s.645 1.44 1.441 1.44c.795 0 1.439-.645 1.439-1.44s-.644-1.44-1.439-1.44z"/></svg>
        </div>
        <span class="tile-title">Instagram</span>
      </a>

      <a class="tile" href="https://www.youtube.com" title="YouTube">
        <div class="tile-icon-box icon-youtube">
          <svg viewBox="0 0 24 24"><path d="M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814zM9.545 15.568V8.432L15.818 12l-6.273 3.568z"/></svg>
        </div>
        <span class="tile-title">YouTube</span>
      </a>

      <a class="tile" href="https://github.com" title="GitHub">
        <div class="tile-icon-box icon-github">
          <svg viewBox="0 0 24 24"><path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0 0 24 12c0-6.63-5.37-12-12-12z"/></svg>
        </div>
        <span class="tile-title">GitHub</span>
      </a>

      <a class="tile" href="https://www.reddit.com" title="Reddit">
        <div class="tile-icon-box icon-reddit">
          <svg viewBox="0 0 24 24"><path d="M12 0A12 12 0 0 0 0 12a12 12 0 0 0 12 12 12 12 0 0 0 12-12A12 12 0 0 0 12 0zm5.01 4.744c.688 0 1.25.561 1.25 1.249a1.25 1.25 0 0 1-2.498.056l-2.597-.547-.8 3.747c1.824.07 3.48.632 4.674 1.488.308-.309.73-.491 1.207-.491.968 0 1.754.786 1.754 1.754 0 .716-.435 1.333-1.01 1.614a3.111 3.111 0 0 1 .042.52c0 2.694-3.13 4.87-7.004 4.87-3.874 0-7.004-2.176-7.004-4.87 0-.183.015-.366.043-.534A1.748 1.748 0 0 1 4.028 12c0-.968.786-1.754 1.754-1.754.463 0 .898.196 1.207.49 1.207-.883 2.878-1.43 4.744-1.487l.885-4.182a.342.342 0 0 1 .14-.197.35.35 0 0 1 .238-.042l2.906.617a1.214 1.214 0 0 1 1.108-.701zM9.25 12C8.56 12 8 12.56 8 13.25c0 .688.56 1.25 1.25 1.25.688 0 1.25-.562 1.25-1.25 0-.69-.562-1.25-1.25-1.25zm5.5 0c-.688 0-1.25.56-1.25 1.25 0 .688.562 1.25 1.25 1.25.69 0 1.25-.562 1.25-1.25 0-.69-.56-1.25-1.25-1.25zm-5.465 4.312a.343.343 0 0 0-.257.576c.712.712 1.83 1.082 2.972 1.082 1.143 0 2.26-.37 2.972-1.082a.343.343 0 1 0-.486-.486c-.571.571-1.503.87-2.486.87-.984 0-1.915-.299-2.486-.87a.34.34 0 0 0-.229-.09z"/></svg>
        </div>
        <span class="tile-title">Reddit</span>
      </a>

      <a class="tile" href="https://www.wikipedia.org" title="Wikipedia">
        <div class="tile-icon-box icon-wikipedia">
          <svg viewBox="0 0 24 24"><path d="M12.09 13.119l2.673-6.551 2.68 6.551zm-7.973 6.94c.323-.008.618-.112.883-.315.266-.202.457-.468.574-.797l4.908-12.02c.118-.328.163-.642.135-.941a1.246 1.246 0 0 0-.441-.813c-.22-.172-.489-.27-.808-.292l-.666-.041.055-.44h6.059l.056.44-.666.041c-.389.025-.71.163-.965.416-.255.253-.418.598-.49 1.035l-1.393 3.411 5.922-.008 1.488-3.642c.105-.336.143-.646.113-.93-.03-.284-.139-.516-.328-.696-.188-.18-.431-.285-.729-.315l-.665-.041.055-.44h5.275l.056.44-.666.041c-.389.025-.71.163-.965.416-.255.253-.418.598-.49 1.035l-4.908 12.02c-.118.328-.27.606-.456.833a1.436 1.436 0 0 1-.762.45c-.324.08-.667.098-1.03.055l-.665-.04-.056-.441.666-.041c.324-.008.618-.112.883-.315.266-.202.457-.468.574-.797l1.096-2.687-6.22-.008-1.124 2.756c-.118.328-.27.606-.456.833a1.436 1.436 0 0 1-.762.45c-.324.08-.667.098-1.03.055l-.665-.04-.056-.441.666-.041z"/></svg>
        </div>
        <span class="tile-title">Wikipedia</span>
      </a>

      <a class="tile" href="servo://settings" title="Settings">
        <div class="tile-icon-box icon-settings">
          <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
        </div>
        <span class="tile-title">Settings</span>
      </a>
    </div>
  </div>

  <script>
    (function() {
      // Dynamic time greeting
      var now = new Date();
      var hour = now.getHours();
      var greetingText = "Good evening";
      if (hour >= 5 && hour < 12) greetingText = "Good morning";
      else if (hour >= 12 && hour < 18) greetingText = "Good afternoon";

      var greetingEl = document.getElementById("greeting");
      if (greetingEl) greetingEl.textContent = greetingText;

      // Date string
      var dateEl = document.getElementById("dateDisplay");
      if (dateEl) {
        var options = { weekday: 'long', month: 'long', day: 'numeric' };
        dateEl.textContent = now.toLocaleDateString(undefined, options);
      }

      // Search handler
      var form = document.getElementById("searchForm");
      var input = document.getElementById("searchInput");

      form.addEventListener("submit", function(e) {
        e.preventDefault();
        var q = (input.value || "").trim();
        if (!q) return;

        // Direct URL detection
        if (/^(https?:\/\/|servo:\/\/|file:\/\/)/i.test(q)) {
          window.location.href = q;
        } else if (q.indexOf('.') > 0 && q.indexOf(' ') === -1) {
          window.location.href = "https://" + q;
        } else {
          // Default to DuckDuckGo HTML endpoint matching Servo Browser default
          window.location.href = "https://duckduckgo.com/html/?q=" + encodeURIComponent(q);
        }
      });
    })();
  </script>
</body>
</html>
)HTML";

std::string NewTabPage::EnsureNewTabPageExists() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
        return "about:blank";
    }

    std::wstring dirPath = std::wstring(localAppData) + L"\\Servo Browser";
    CreateDirectoryW(dirPath.c_str(), nullptr);

    std::wstring filePath = dirPath + L"\\newtab.html";
    CoTaskMemFree(localAppData);

    // Overwrite to keep HTML template fresh with latest UI improvements
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out.write(NEW_TAB_HTML_CONTENT, strlen(NEW_TAB_HTML_CONTENT));
        out.close();
    }

    // Convert file path to file:/// URI
    char utf8Path[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);

    std::string pathStr = utf8Path;
    for (char& c : pathStr) {
        if (c == '\\') c = '/';
    }

    return "file:///" + pathStr;
}

bool NewTabPage::IsNewTabUrl(const std::string& url) {
    if (url.empty() || url == "servo://newtab" || url == "about:newtab" || url == "about:blank") {
        return true;
    }
    if (url.find("newtab.html") != std::string::npos) {
        return true;
    }
    return false;
}
