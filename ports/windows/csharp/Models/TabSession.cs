// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Microsoft.UI.Xaml.Media;

namespace ServoBrowser.WinUI.Models
{
    public sealed class TabSession : INotifyPropertyChanged
    {
        private string _title = "New Tab";
        private string _url = "https://duckduckgo.com/";
        private bool _isLoading;
        private bool _canGoBack;
        private bool _canGoForward;
        private ImageSource? _favicon;

        public Guid Id { get; } = Guid.NewGuid();
        public bool IsPrivate { get; set; }
        public nint WebView { get; set; }
        public nint ChildHwnd { get; set; }

        public string Title
        {
            get => _title;
            set => SetField(ref _title, string.IsNullOrWhiteSpace(value) ? "New Tab" : value);
        }

        public string Url
        {
            get => _url;
            set => SetField(ref _url, value);
        }

        public bool IsLoading
        {
            get => _isLoading;
            set => SetField(ref _isLoading, value);
        }

        public bool CanGoBack
        {
            get => _canGoBack;
            set => SetField(ref _canGoBack, value);
        }

        public bool CanGoForward
        {
            get => _canGoForward;
            set => SetField(ref _canGoForward, value);
        }

        public ImageSource? Favicon
        {
            get => _favicon;
            set => SetField(ref _favicon, value);
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        private void SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
        {
            if (!Equals(field, value))
            {
                field = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
            }
        }
    }
}
