/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "new_tab_page.h"
#include "servo_logo.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

static const char* NEW_TAB_HEAD = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>New Tab — Servo</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
  :root {
    --bg-mesh: radial-gradient(at 10% 20%, rgba(0, 150, 255, 0.08) 0px, transparent 50%),
               radial-gradient(at 90% 10%, rgba(96, 205, 255, 0.08) 0px, transparent 50%),
               radial-gradient(at 50% 90%, rgba(0, 95, 184, 0.05) 0px, transparent 50%),
               #f8fafc;
    --card-bg: rgba(255, 255, 255, 0.85);
    --card-border: rgba(0, 0, 0, 0.07);
    --card-shadow: 0 4px 20px rgba(0, 0, 0, 0.04), 0 1px 3px rgba(0, 0, 0, 0.02);
    --text-primary: #0f172a;
    --text-secondary: #475569;
    --text-tertiary: #94a3b8;
    --search-bg: #ffffff;
    --search-border: rgba(0, 0, 0, 0.12);
    --search-focus-glow: rgba(0, 95, 184, 0.22);
    --tile-hover: #ffffff;
    --tile-shadow-hover: 0 12px 28px rgba(0, 95, 184, 0.12), 0 4px 10px rgba(0, 0, 0, 0.04);
    --accent: #005fb8;
    --chip-bg: rgba(255, 255, 255, 0.75);
    --chip-hover: #ffffff;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg-mesh: radial-gradient(at 15% 15%, rgba(0, 150, 255, 0.15) 0px, transparent 50%),
                 radial-gradient(at 85% 20%, rgba(96, 205, 255, 0.12) 0px, transparent 50%),
                 radial-gradient(at 50% 85%, rgba(10, 30, 90, 0.25) 0px, transparent 50%),
                 #0f1117;
      --card-bg: rgba(26, 29, 38, 0.75);
      --card-border: rgba(255, 255, 255, 0.08);
      --card-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
      --text-primary: #f8fafc;
      --text-secondary: #94a3b8;
      --text-tertiary: #64748b;
      --search-bg: rgba(24, 27, 36, 0.9);
      --search-border: rgba(255, 255, 255, 0.14);
      --search-focus-glow: rgba(96, 205, 255, 0.28);
      --tile-hover: rgba(38, 42, 54, 0.9);
      --tile-shadow-hover: 0 12px 32px rgba(0, 0, 0, 0.45), 0 0 20px rgba(96, 205, 255, 0.15);
      --accent: #60cdff;
      --chip-bg: rgba(26, 29, 38, 0.7);
      --chip-hover: rgba(38, 42, 54, 0.9);
    }
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    background: var(--bg-mesh);
    color: var(--text-primary);
    font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif;
    user-select: none;
    -webkit-user-select: none;
    overflow-x: hidden;
    padding: 30px 20px;
  }
  .container {
    width: 100%;
    max-width: 760px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    animation: fadeIn 220ms ease-out;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }
  .brand-header {
    display: flex;
    flex-direction: column;
    align-items: center;
    margin-bottom: 28px;
    gap: 12px;
  }
  .logo-wrapper {
    filter: drop-shadow(0 8px 24px rgba(0, 102, 224, 0.3));
    transition: transform 0.3s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .logo-wrapper:hover {
    transform: scale(1.05) rotate(4deg);
  }
  .greeting {
    font-size: 2.1rem;
    font-weight: 700;
    letter-spacing: -0.025em;
    color: var(--text-primary);
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .date-chip {
    font-size: 0.88rem;
    font-weight: 500;
    color: var(--text-secondary);
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .clock {
    font-weight: 600;
    color: var(--accent);
  }
  .search-container {
    width: 100%;
    max-width: 660px;
    margin-bottom: 34px;
    position: relative;
  }
  .search-box {
    display: flex;
    align-items: center;
    background: var(--search-bg);
    border: 1px solid var(--search-border);
    border-radius: 28px;
    padding: 6px 14px 6px 20px;
    box-shadow: var(--card-shadow);
    backdrop-filter: blur(20px);
    -webkit-backdrop-filter: blur(20px);
    transition: all 0.22s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .search-box:focus-within {
    border-color: var(--accent);
    box-shadow: 0 0 0 4px var(--search-focus-glow), var(--card-shadow);
    transform: translateY(-1px);
  }
  .search-icon {
    width: 20px;
    height: 20px;
    fill: var(--text-secondary);
    margin-right: 12px;
    flex-shrink: 0;
  }
  .search-input {
    flex: 1;
    height: 44px;
    border: none;
    outline: none;
    background: transparent;
    font-family: inherit;
    font-size: 1.05rem;
    color: var(--text-primary);
  }
  .search-input::placeholder {
    color: var(--text-tertiary);
  }
  .engine-pills {
    display: flex;
    gap: 4px;
    background: rgba(0, 0, 0, 0.04);
    border-radius: 18px;
    padding: 3px;
    margin-left: 8px;
  }
  @media (prefers-color-scheme: dark) {
    .engine-pills { background: rgba(255, 255, 255, 0.06); }
  }
  .engine-btn {
    border: none;
    background: transparent;
    font-family: inherit;
    font-size: 0.76rem;
    font-weight: 500;
    padding: 5px 11px;
    border-radius: 14px;
    cursor: pointer;
    color: var(--text-secondary);
    transition: all 0.15s ease;
  }
  .engine-btn.active {
    background: var(--accent);
    color: #ffffff;
    font-weight: 600;
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.15);
  }
  .shortcuts-grid {
    display: grid;
    grid-template-columns: repeat(5, 1fr);
    gap: 16px;
    width: 100%;
    margin-bottom: 30px;
  }
  @media (max-width: 680px) {
    .shortcuts-grid { grid-template-columns: repeat(3, 1fr); }
  }
  .tile {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 16px 12px;
    border-radius: 16px;
    background: var(--card-bg);
    border: 1px solid var(--card-border);
    box-shadow: var(--card-shadow);
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    text-decoration: none;
    color: var(--text-primary);
    transition: all 0.22s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .tile:hover {
    background: var(--tile-hover);
    transform: translateY(-3px);
    box-shadow: var(--tile-shadow-hover);
    border-color: var(--accent);
  }
  .tile-icon-box {
    width: 48px;
    height: 48px;
    border-radius: 13px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-bottom: 10px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
  }
  .tile-title {
    font-size: 0.82rem;
    font-weight: 600;
    color: var(--text-secondary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 90px;
  }
  .tile:hover .tile-title {
    color: var(--text-primary);
  }
  .actions-bar {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 10px;
  }
  .action-chip {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    font-size: 0.84rem;
    font-weight: 500;
    color: var(--text-secondary);
    text-decoration: none;
    padding: 7px 15px;
    border-radius: 20px;
    background: var(--chip-bg);
    border: 1px solid var(--card-border);
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.02);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    transition: all 0.18s ease;
  }
  .action-chip:hover {
    background: var(--chip-hover);
    color: var(--accent);
    transform: translateY(-1px);
    border-color: var(--accent);
  }
  .ficon {
    font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets', sans-serif;
    font-style: normal;
  }
</style>
</head>
<body>
)HTML";

static const char* NEW_TAB_BODY = R"HTML(
<div class="container">
  <div class="brand-header">
    <div class="logo-wrapper">
      <svg width="56" height="56" viewBox="0 0 64 64" fill="none">
        <defs>
          <linearGradient id="servo_core" x1="8" y1="8" x2="56" y2="56" gradientUnits="userSpaceOnUse">
            <stop offset="0%" stop-color="#00D2FF"/>
            <stop offset="50%" stop-color="#0066E0"/>
            <stop offset="100%" stop-color="#0A1E5A"/>
          </linearGradient>
          <linearGradient id="servo_sw" x1="12" y1="20" x2="52" y2="44" gradientUnits="userSpaceOnUse">
            <stop offset="0%" stop-color="#FFFFFF" stop-opacity="0.95"/>
            <stop offset="100%" stop-color="#60CDFF" stop-opacity="0.6"/>
          </linearGradient>
        </defs>
        <circle cx="32" cy="32" r="28" fill="none" stroke="#0078D4" stroke-width="1.5" stroke-opacity="0.35"/>
        <circle cx="32" cy="32" r="25" fill="url(#servo_core)"/>
        <path d="M14 34C15 22 24 14 36 14C45 14 51 20 51 29" stroke="url(#servo_sw)" stroke-width="3" stroke-linecap="round"/>
        <path d="M18 42C23 48 33 50 42 46C47 43 50 38 50 34" stroke="#00D2FF" stroke-width="2.2" stroke-linecap="round" stroke-opacity="0.8"/>
        <circle cx="37" cy="25" r="4.8" fill="#FFFFFF"/>
        <circle cx="27" cy="39" r="3.8" fill="#D2F5FF"/>
        <path d="M34 27L25 36L29 41L39 30Z" fill="#FFFFFF" fill-opacity="0.9"/>
      </svg>
    </div>
    <h1 class="greeting" id="greeting">Welcome</h1>
    <div class="date-chip">
      <span class="clock" id="clockDisplay">12:00</span> &bull; <span id="dateDisplay">Today</span>
    </div>
  </div>

  <div class="search-container">
    <form class="search-box" id="searchForm" autocomplete="off">
      <svg class="search-icon" viewBox="0 0 24 24">
        <path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/>
      </svg>
      <input type="text" class="search-input" id="searchInput" placeholder="Search the web or enter address..." autofocus/>
      <div class="engine-pills">
        <button type="button" class="engine-btn active" data-engine="ddg" onclick="setEngine('ddg', this)">DuckDuckGo</button>
        <button type="button" class="engine-btn" data-engine="google" onclick="setEngine('google', this)">Google</button>
        <button type="button" class="engine-btn" data-engine="bing" onclick="setEngine('bing', this)">Bing</button>
      </div>
    </form>
  </div>

  <div class="shortcuts-grid">
    <a class="tile" href="https://www.youtube.com" title="YouTube">
      <div class="tile-icon-box" style="background:#ff0000;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M23.5 6.2c-.3-1-1-1.8-2-2.1C19.6 3.5 12 3.5 12 3.5s-7.6 0-9.5.6c-1 .3-1.7 1.1-2 2.1C0 8.1 0 12 0 12s0 3.9.5 5.8c.3 1 1 1.8 2 2.1 1.9.6 9.5.6 9.5.6s7.6 0 9.5-.6c1-.3 1.7-1.1 2-2.1.5-1.9.5-5.8.5-5.8s0-3.9-.5-5.8zM9.5 15.5v-7l6.2 3.5-6.2 3.5z"/></svg></div>
      <span class="tile-title">YouTube</span>
    </a>
    <a class="tile" href="https://github.com" title="GitHub">
      <div class="tile-icon-box" style="background:#24292e;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path fill-rule="evenodd" clip-rule="evenodd" d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.53 1.032 1.53 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z"/></svg></div>
      <span class="tile-title">GitHub</span>
    </a>
    <a class="tile" href="https://www.instagram.com" title="Instagram">
      <div class="tile-icon-box" style="background:linear-gradient(45deg, #f09433 0%, #e6683c 25%, #dc2743 50%, #cc2366 75%, #bc1888 100%);color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2.163c3.204 0 3.584.012 4.85.07 3.252.148 4.771 1.691 4.919 4.919.058 1.265.069 1.645.069 4.849 0 3.205-.012 3.584-.069 4.849-.149 3.225-1.664 4.771-4.919 4.919-1.266.058-1.644.07-4.85.07-3.204 0-3.584-.012-4.849-.07-3.26-.149-4.771-1.699-4.919-4.92-.058-1.265-.07-1.644-.07-4.849 0-3.204.013-3.583.07-4.849.149-3.227 1.664-4.771 4.919-4.919 1.266-.057 1.645-.069 4.849-.069zm0-2.163c-3.259 0-3.667.014-4.947.072-4.358.2-6.78 2.618-6.98 6.98-.059 1.281-.073 1.689-.073 4.948 0 3.259.014 3.668.072 4.948.2 4.358 2.618 6.78 6.98 6.98 1.281.058 1.689.072 4.948.072 3.259 0 3.668-.014 4.948-.072 4.354-.2 6.782-2.618 6.979-6.98.059-1.28.073-1.689.073-4.948 0-3.259-.014-3.667-.072-4.947-.196-4.354-2.617-6.78-6.979-6.98-1.281-.059-1.69-.073-4.949-.073zm0 5.838c-3.403 0-6.162 2.759-6.162 6.162s2.759 6.163 6.162 6.163 6.162-2.759 6.162-6.163c0-3.403-2.759-6.162-6.162-6.162zm0 10.162c-2.209 0-4-1.79-4-4 0-2.209 1.791-4 4-4s4 1.791 4 4c0 2.21-1.791 4-4 4zm6.406-11.845c-.796 0-1.441.645-1.441 1.44s.645 1.44 1.441 1.44c.795 0 1.439-.645 1.439-1.44s-.644-1.44-1.439-1.44z"/></svg></div>
      <span class="tile-title">Instagram</span>
    </a>
    <a class="tile" href="https://www.reddit.com" title="Reddit">
      <div class="tile-icon-box" style="background:#ff4500;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0A12 12 0 0 0 0 12a12 12 0 0 0 12 12 12 12 0 0 0 12-12A12 12 0 0 0 12 0zm5.01 4.744c.688 0 1.25.56 1.25 1.249a1.25 1.25 0 0 1-2.498.056l-2.597-.547-.8 3.747c1.824.07 3.48.632 4.674 1.488.308-.309.73-.491 1.207-.491.968 0 1.754.786 1.754 1.754 0 .716-.435 1.333-1.01 1.614a3.111 3.111 0 0 1 .042.52c0 2.694-3.13 4.87-7.004 4.87-3.874 0-7.004-2.176-7.004-4.87 0-.183.015-.366.043-.534A1.748 1.748 0 0 1 4.028 12c0-.968.786-1.754 1.754-1.754.463 0 .898.196 1.207.49 1.207-.883 2.878-1.43 4.744-1.487l.885-4.182a.342.342 0 0 1 .14-.197.35.35 0 0 1 .238-.042l2.906.617a1.214 1.214 0 0 1 1.108-.702z"/></svg></div>
      <span class="tile-title">Reddit</span>
    </a>
    <a class="tile" href="https://chatgpt.com" title="ChatGPT">
      <div class="tile-icon-box" style="background:#10a37f;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M22.28 9.87a5.985 5.985 0 0 0-.52-4.86c-1.1-1.9-3.06-3.12-5.24-3.34a6.22 6.22 0 0 0-4.78 1.13 6.07 6.07 0 0 0-6.19.46 6.03 6.03 0 0 0-2.61 4.61 5.98 5.98 0 0 0 .73 4.84 5.98 5.98 0 0 0 .52 4.86c1.1 1.9 3.06 3.12 5.24 3.34a6.2 6.2 0 0 0 4.78-1.13 6.07 6.07 0 0 0 6.19-.46 6.03 6.03 0 0 0 2.61-4.61 5.98 5.98 0 0 0-.73-4.84zM12 13.5a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3z"/></svg></div>
      <span class="tile-title">ChatGPT</span>
    </a>
    <a class="tile" href="https://www.wikipedia.org" title="Wikipedia">
      <div class="tile-icon-box" style="background:#ffffff;color:#202122;border:1px solid #cbd5e1"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M12.09 13.12L9.57 6.75H7.72L3 17.25h1.93l1.19-2.92h4.59l1.15 2.92h2.04l-1.81-4.13zm-4.75-.02l1.64-4.04 1.59 4.04H7.34zm13.66 4.15h-1.98l-2.66-6.41-2.58 6.41h-1.93l3.65-8.52h1.86l3.64 8.52z"/></svg></div>
      <span class="tile-title">Wikipedia</span>
    </a>
    <a class="tile" href="https://x.com" title="X (Twitter)">
      <div class="tile-icon-box" style="background:#000000;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231zm-1.161 17.52h1.833L7.084 4.126H5.117z"/></svg></div>
      <span class="tile-title">X</span>
    </a>
    <a class="tile" href="https://www.spotify.com" title="Spotify">
      <div class="tile-icon-box" style="background:#1ed760;color:#000"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C5.4 0 0 5.4 0 12s5.4 12 12 12 12-5.4 12-12S18.66 0 12 0zm5.521 17.34c-.24.359-.66.48-1.021.24-2.82-1.74-6.36-2.101-10.561-1.141-.418.122-.779-.179-.899-.539-.12-.421.18-.78.54-.9 4.56-1.021 8.52-.6 11.64 1.32.42.18.479.659.301 1.02zm1.44-3.3c-.301.42-.841.6-1.262.3-3.239-1.98-8.159-2.58-11.939-1.38-.479.12-1.02-.12-1.14-.6-.12-.48.12-1.021.6-1.141C9.6 9.9 15 10.561 18.72 12.84c.361.181.54.78.241 1.2zm.12-3.36C15.24 8.4 8.82 8.16 5.16 9.301c-.6.179-1.2-.181-1.38-.721-.18-.601.18-1.2.72-1.381 4.26-1.26 11.28-1.02 15.721 1.621.539.3.719 1.02.419 1.56-.299.421-1.02.599-1.559.3z"/></svg></div>
      <span class="tile-title">Spotify</span>
    </a>
    <a class="tile" href="https://www.netflix.com" title="Netflix">
      <div class="tile-icon-box" style="background:#e50914;color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M5.398 0v24c1.391-.256 3.018-.614 4.524-.969V0H5.398zm8.678 0v20.916c1.551-.314 3.102-.686 4.526-.97V0h-4.526zM9.922 0l4.316 20.849c-1.506.355-3.133.713-4.524.969L5.398 0h4.524z"/></svg></div>
      <span class="tile-title">Netflix</span>
    </a>
    <a class="tile" href="https://servo.org" title="Servo Project">
      <div class="tile-icon-box" style="background:linear-gradient(135deg, #00D2FF, #0066E0);color:#fff"><svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><circle cx="12" cy="12" r="10" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="12" r="4"/><path d="M12 2C6.5 2 2 6.5 2 12M22 12c0 5.5-4.5 10-10 10" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></div>
      <span class="tile-title">Servo</span>
    </a>
  </div>

  <div class="actions-bar">
    <a href="servo://session-restore" class="action-chip"><span class="ficon">&#xE777;</span> Restore Session</a>
    <a href="servo://bookmarks" class="action-chip"><span class="ficon">&#xE734;</span> Bookmarks</a>
    <a href="servo://history" class="action-chip"><span class="ficon">&#xE81C;</span> History</a>
    <a href="servo://settings" class="action-chip"><span class="ficon">&#xE713;</span> Settings</a>
  </div>
</div>
)HTML";

static const char* NEW_TAB_SCRIPT = R"HTML(
<script>
(function() {
  function updateTime() {
    var now = new Date();
    var hrs = now.getHours();
    var greeting = 'Welcome';
    if (hrs >= 5 && hrs < 12) greeting = 'Good morning';
    else if (hrs >= 12 && hrs < 17) greeting = 'Good afternoon';
    else if (hrs >= 17 && hrs < 22) greeting = 'Good evening';
    else greeting = 'Good night';

    var greetingEl = document.getElementById('greeting');
    if (greetingEl) greetingEl.textContent = greeting;

    var dateEl = document.getElementById('dateDisplay');
    if (dateEl) {
      dateEl.textContent = now.toLocaleDateString(undefined, {
        weekday: 'long', month: 'short', day: 'numeric'
      });
    }

    var clockEl = document.getElementById('clockDisplay');
    if (clockEl) {
      var hStr = String(hrs).padStart(2, '0');
      var mStr = String(now.getMinutes()).padStart(2, '0');
      clockEl.textContent = hStr + ':' + mStr;
    }
  }

  updateTime();
  setInterval(updateTime, 30000);

  var activeEngine = 'ddg';
  window.setEngine = function(engine, btn) {
    activeEngine = engine;
    document.querySelectorAll('.engine-btn').forEach(function(b) { b.classList.remove('active'); });
    if (btn) btn.classList.add('active');
    var input = document.getElementById('searchInput');
    if (input) {
      if (engine === 'google') input.placeholder = 'Search Google or enter address...';
      else if (engine === 'bing') input.placeholder = 'Search Bing or enter address...';
      else input.placeholder = 'Search DuckDuckGo or enter address...';
      input.focus();
    }
  };

  var form = document.getElementById('searchForm');
  if (form) {
    form.addEventListener('submit', function(e) {
      e.preventDefault();
      var input = document.getElementById('searchInput');
      if (!input) return;
      var query = input.value.trim();
      if (!query) return;

      if (/^(servo|chrome|edge):/i.test(query) || query === 'settings' || query === 'history' || query === 'bookmarks' || query === 'about' || query === 'newtab' || query === 'about:settings' || query === 'about:bookmarks' || query === 'about:history') {
        console.log('__servo_ipc__:navigate:' + query);
        return;
      }
      if (query.indexOf('://') !== -1 || query.startsWith('about:')) {
        window.location.href = query;
        return;
      }
      if (query.indexOf('.') !== -1 && query.indexOf(' ') === -1) {
        window.location.href = 'https://' + query;
        return;
      }

      var q = encodeURIComponent(query);
      if (activeEngine === 'google') window.location.href = 'https://www.google.com/search?q=' + q;
      else if (activeEngine === 'bing') window.location.href = 'https://www.bing.com/search?q=' + q;
      else window.location.href = 'https://duckduckgo.com/html/?q=' + q;
    });
  }
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

    // Overwrite with fresh modern Fluent 2 Start Page
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out.write(NEW_TAB_HEAD, strlen(NEW_TAB_HEAD));
        out.write(NEW_TAB_BODY, strlen(NEW_TAB_BODY));
        out.write(NEW_TAB_SCRIPT, strlen(NEW_TAB_SCRIPT));
        out.close();
    }

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
