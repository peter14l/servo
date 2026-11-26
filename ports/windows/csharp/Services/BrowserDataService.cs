// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace ServoBrowser.WinUI.Services
{
    public sealed class BrowserDataService
    {
        private sealed class BrowserStore
        {
            public List<string> Bookmarks { get; set; } = new();
            public List<string> History { get; set; } = new();
        }

        private readonly string _storePath;
        private readonly BrowserStore _store;

        public BrowserDataService()
        {
            string dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ServoBrowser");
            Directory.CreateDirectory(dir);
            _storePath = Path.Combine(dir, "winui_profile.json");
            _store = LoadStore();
        }

        public bool IsBookmarked(string url) =>
            !string.IsNullOrWhiteSpace(url) &&
            _store.Bookmarks.Contains(url, StringComparer.OrdinalIgnoreCase);

        public void ToggleBookmark(string url, string title)
        {
            if (string.IsNullOrWhiteSpace(url)) return;
            int idx = _store.Bookmarks.FindIndex(b => string.Equals(b, url, StringComparison.OrdinalIgnoreCase));
            if (idx >= 0)
            {
                _store.Bookmarks.RemoveAt(idx);
            }
            else
            {
                _store.Bookmarks.Add(url);
            }
            SaveStore();
        }

        public void RecordHistory(string url, string title)
        {
            if (string.IsNullOrWhiteSpace(url) || url.StartsWith("about:", StringComparison.OrdinalIgnoreCase))
            {
                return;
            }
            _store.History.RemoveAll(h => string.Equals(h, url, StringComparison.OrdinalIgnoreCase));
            _store.History.Insert(0, url);
            if (_store.History.Count > 200)
            {
                _store.History.RemoveRange(200, _store.History.Count - 200);
            }
            SaveStore();
        }

        public IEnumerable<string> GetSuggestions(string query)
        {
            if (string.IsNullOrWhiteSpace(query))
            {
                return _store.Bookmarks.Take(5);
            }

            return _store.Bookmarks
                .Concat(_store.History)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .Where(u => u.Contains(query, StringComparison.OrdinalIgnoreCase))
                .Take(8);
        }

        private BrowserStore LoadStore()
        {
            try
            {
                if (File.Exists(_storePath))
                {
                    string json = File.ReadAllText(_storePath);
                    return JsonSerializer.Deserialize<BrowserStore>(json) ?? new BrowserStore();
                }
            }
            catch
            {
                // Start with an empty store if the profile file is corrupt or unreadable.
            }
            return new BrowserStore();
        }

        private void SaveStore()
        {
            try
            {
                string json = JsonSerializer.Serialize(_store, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(_storePath, json);
            }
            catch
            {
                // Ignore transient disk write failures.
            }
        }
    }
}
