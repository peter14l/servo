// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Servo.Embedding;
using ServoBrowser.WinUI.Controls;
using ServoBrowser.WinUI.Models;
using ServoBrowser.WinUI.Services;
using Windows.Foundation;
using Windows.System;
using WinRT.Interop;
using DispatcherQueue = Microsoft.UI.Dispatching.DispatcherQueue;
using DispatcherQueueTimer = Microsoft.UI.Dispatching.DispatcherQueueTimer;

namespace ServoBrowser.WinUI
{
    public sealed partial class MainWindow : Window
    {
        private const string HomeUrl = "https://duckduckgo.com/";

        private readonly nint _hwnd;
        private readonly DispatcherQueue _dispatcher;
        private readonly DispatcherQueueTimer _animTimer;
        private readonly Dictionary<TabViewItem, ServoViewportHost> _tabs = new();
        private readonly BrowserDataService _dataService = new();

        private ServoViewportHost? _activeHost;
        private int _activeFindId = -1;
        private bool _modalOpen;
        private int _animatingTabs;

        public MainWindow()
        {
            InitializeComponent();

            _hwnd = WindowNative.GetWindowHandle(this);
            _dispatcher = DispatcherQueue.GetForCurrentThread();

            Title = "Servo Browser";
            SystemBackdrop = new MicaBackdrop { Kind = MicaKind.BaseAlt };
            ExtendsContentIntoTitleBar = true;
            SetTitleBar(TitleBarDragRegion);

            _animTimer = _dispatcher.CreateTimer();
            _animTimer.Interval = TimeSpan.FromMilliseconds(16);
            _animTimer.Tick += (_, _) => App.SpinEventLoopOnce();

            RootGrid.KeyDown += OnRootKeyDown;
            ViewportPlaceholder.Loaded += (_, _) => UpdateViewportBounds();
            Closed += OnWindowClosed;

            CreateNewTab(HomeUrl);
        }

        private void CreateNewTab(string url, nint newWebViewRequest = default)
        {
            var session = new TabSession { Url = url, Title = "New Tab" };
            var tabItem = new TabViewItem
            {
                Header = session.Title,
                IconSource = new FontIconSource { Glyph = "\uE774" },
                Tag = session,
            };

            float scale = (float)(ViewportPlaceholder.XamlRoot?.RasterizationScale ?? 1.0);
            var host = new ServoViewportHost(_hwnd, _dispatcher, session, url, scale, newWebViewRequest);

            host.TitleChanged += (s, title) =>
            {
                tabItem.Header = string.IsNullOrWhiteSpace(title) ? "New Tab" : title;
                if (host == _activeHost)
                {
                    Title = $"{tabItem.Header} - Servo Browser";
                }
            };

            host.UrlChanged += (s, newUrl) =>
            {
                _dataService.RecordHistory(newUrl, s.Title);
                if (host == _activeHost && Omnibox.FocusState == FocusState.Unfocused)
                {
                    Omnibox.Text = newUrl;
                }
                UpdateBookmarkIcon();
            };

            host.LoadStatusChanged += (s, status) =>
            {
                if (host == _activeHost)
                {
                    UpdateToolbarFromSession(s);
                }
            };

            host.AnimatingChanged += (_, animating) =>
            {
                _animatingTabs = Math.Max(0, _animatingTabs + (animating ? 1 : -1));
                if (_animatingTabs > 0 && !_animTimer.IsRunning)
                {
                    _animTimer.Start();
                }
                else if (_animatingTabs == 0 && _animTimer.IsRunning)
                {
                    _animTimer.Stop();
                }
            };

            host.FindResultReceived += (_, identifier, count) =>
            {
                _activeFindId = identifier;
                FindCountLabel.Text = count > 0 ? $"{count} matches" : "0 matches";
            };

            host.AlertRequested += async (_, message) =>
            {
                await ShowAlertAsync(message);
            };

            host.NewTabRequested += request =>
            {
                CreateNewTab(string.Empty, request);
            };

            _tabs[tabItem] = host;
            BrowserTabView.TabItems.Add(tabItem);
            BrowserTabView.SelectedItem = tabItem;
        }

        private async System.Threading.Tasks.Task ShowAlertAsync(string message)
        {
            if (_modalOpen || RootGrid.XamlRoot == null) return;
            _modalOpen = true;
            UpdateViewportBounds();
            try
            {
                var dialog = new ContentDialog
                {
                    Title = "Page message",
                    Content = message,
                    CloseButtonText = "OK",
                    XamlRoot = RootGrid.XamlRoot,
                };
                await dialog.ShowAsync();
            }
            finally
            {
                _modalOpen = false;
                UpdateViewportBounds();
            }
        }

        private void OnTabSelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (BrowserTabView.SelectedItem is TabViewItem selected && _tabs.TryGetValue(selected, out var host))
            {
                _activeHost = host;
                Omnibox.Text = host.Session.Url;
                Title = $"{host.Session.Title} - Servo Browser";
                UpdateToolbarFromSession(host.Session);
                UpdateBookmarkIcon();
                UpdateViewportBounds();
            }
        }

        private void OnAddTabButtonClick(TabView sender, object args) => CreateNewTab(HomeUrl);

        private void OnTabCloseRequested(TabView sender, TabViewTabCloseRequestedEventArgs args)
        {
            if (_tabs.TryGetValue(args.Tab, out var host))
            {
                _tabs.Remove(args.Tab);
                if (_activeHost == host)
                {
                    _activeHost = null;
                }
                host.Dispose();
            }

            sender.TabItems.Remove(args.Tab);
            if (sender.TabItems.Count == 0)
            {
                Close();
            }
        }

        private void UpdateToolbarFromSession(TabSession session)
        {
            BackButton.IsEnabled = session.CanGoBack;
            ForwardButton.IsEnabled = session.CanGoForward;
            ReloadIcon.Glyph = session.IsLoading ? "\uE711" : "\uE72C";
            LoadProgressBar.Visibility = session.IsLoading ? Visibility.Visible : Visibility.Collapsed;
        }

        private void UpdateBookmarkIcon()
        {
            bool bookmarked = _activeHost != null && _dataService.IsBookmarked(_activeHost.Session.Url);
            BookmarkIcon.Glyph = bookmarked ? "\uE735" : "\uE734";
        }

        private void UpdateViewportBounds()
        {
            if (ViewportPlaceholder.XamlRoot == null)
            {
                return;
            }

            double scale = ViewportPlaceholder.XamlRoot.RasterizationScale;
            var transform = ViewportPlaceholder.TransformToVisual(RootGrid);
            Point topLeft = transform.TransformPoint(new Point(0, 0));

            int x = (int)Math.Round(topLeft.X * scale);
            int y = (int)Math.Round(topLeft.Y * scale);
            int w = Math.Max(1, (int)Math.Round(ViewportPlaceholder.ActualWidth * scale));
            int h = Math.Max(1, (int)Math.Round(ViewportPlaceholder.ActualHeight * scale));

            foreach (var kvp in _tabs)
            {
                bool isVisible = !_modalOpen && kvp.Value == _activeHost;
                kvp.Value.SetBounds(x, y, w, h, (float)scale, isVisible);
            }
        }

        private void OnViewportSizeChanged(object sender, SizeChangedEventArgs e) => UpdateViewportBounds();

        // Normalize URLs and percent-encode search queries (GEMINI.md invariant #2).
        public static string NormalizeOmniboxInput(string rawInput)
        {
            string trimmed = rawInput.Trim();
            if (string.IsNullOrEmpty(trimmed))
            {
                return HomeUrl;
            }

            if (trimmed.Contains("://", StringComparison.Ordinal) ||
                trimmed.StartsWith("about:", StringComparison.OrdinalIgnoreCase) ||
                trimmed.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
            {
                return trimmed;
            }

            if (!trimmed.Contains(' ') && trimmed.Contains('.'))
            {
                return "https://" + trimmed;
            }

            return "https://duckduckgo.com/html/?q=" + Uri.EscapeDataString(trimmed);
        }

        private void OnOmniboxQuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
        {
            string query = args.ChosenSuggestion as string ?? args.QueryText;
            string url = NormalizeOmniboxInput(query);
            _activeHost?.Load(url);
        }

        private void OnOmniboxTextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
        {
            if (args.Reason == AutoSuggestionBoxTextChangeReason.UserInput)
            {
                sender.ItemsSource = _dataService.GetSuggestions(sender.Text).ToList();
            }
        }

        private void OnOmniboxGotFocus(object sender, RoutedEventArgs e)
        {
            // Keep editing ergonomic on focus.
        }

        private void OnBackClick(object sender, RoutedEventArgs e) => _activeHost?.GoBack();
        private void OnForwardClick(object sender, RoutedEventArgs e) => _activeHost?.GoForward();
        private void OnReloadClick(object sender, RoutedEventArgs e)
        {
            if (_activeHost == null) return;
            if (_activeHost.Session.IsLoading) _activeHost.Stop();
            else _activeHost.Reload();
        }
        private void OnHomeClick(object sender, RoutedEventArgs e) => _activeHost?.Load(HomeUrl);

        private void OnBookmarkClick(object sender, RoutedEventArgs e)
        {
            if (_activeHost == null) return;
            _dataService.ToggleBookmark(_activeHost.Session.Url, _activeHost.Session.Title);
            UpdateBookmarkIcon();
        }

        private void OnToggleFindClick(object sender, RoutedEventArgs e)
        {
            bool show = FindBar.Visibility == Visibility.Collapsed;
            FindBar.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
            UpdateViewportBounds();
            if (show)
            {
                FindTextBox.Focus(FocusState.Programmatic);
            }
            else if (_activeHost != null && _activeFindId >= 0)
            {
                ServoCapi.servo_webview_find_clear(_activeHost.WebView, _activeFindId);
                _activeFindId = -1;
                FindCountLabel.Text = string.Empty;
            }
        }

        private void OnCloseFindClick(object sender, RoutedEventArgs e)
        {
            FindBar.Visibility = Visibility.Collapsed;
            if (_activeHost != null && _activeFindId >= 0)
            {
                ServoCapi.servo_webview_find_clear(_activeHost.WebView, _activeFindId);
                _activeFindId = -1;
            }
            UpdateViewportBounds();
        }

        private void RunFind(bool forward)
        {
            if (_activeHost == null || string.IsNullOrEmpty(FindTextBox.Text)) return;
            if (_activeFindId >= 0)
            {
                ServoCapi.servo_webview_find_next(_activeHost.WebView, _activeFindId, forward);
            }
            else
            {
                _activeFindId = ServoCapi.servo_webview_find(_activeHost.WebView, FindTextBox.Text, forward, false);
            }
        }

        private void OnFindNextClick(object sender, RoutedEventArgs e) => RunFind(true);
        private void OnFindPrevClick(object sender, RoutedEventArgs e) => RunFind(false);

        private void OnFindTextBoxKeyDown(object sender, KeyRoutedEventArgs e)
        {
            if (e.Key == VirtualKey.Enter)
            {
                RunFind(true);
                e.Handled = true;
            }
            else if (e.Key == VirtualKey.Escape)
            {
                OnCloseFindClick(sender, e);
                e.Handled = true;
            }
        }

        private void OnResetZoomClick(object sender, RoutedEventArgs e)
        {
            if (_activeHost == null) return;
            _activeHost.SetZoom(1.0f);
            ZoomLabel.Text = "100%";
        }

        private async void OnSettingsClick(object sender, RoutedEventArgs e)
        {
            if (_modalOpen || RootGrid.XamlRoot == null) return;
            _modalOpen = true;
            UpdateViewportBounds();
            try
            {
                var themeCombo = new ComboBox { Header = "Theme", SelectedIndex = RootGrid.RequestedTheme == ElementTheme.Dark ? 1 : 0 };
                themeCombo.Items.Add("Light");
                themeCombo.Items.Add("Dark");

                var crashToggle = new ToggleSwitch
                {
                    Header = "Opt-in local crash diagnostics (REL-7)",
                    IsOn = CrashTelemetryService.IsOptedIn,
                    OnContent = "Enabled (local minidumps only)",
                    OffContent = "Disabled",
                };

                bool sandboxed = ServoCapi.servo_capi_is_sandboxing_supported();
                var statusText = new TextBlock
                {
                    Text = $"Developer Preview · OS Renderer Sandbox: {(sandboxed ? "Supported" : "Not available on Windows (mitigations only)")}",
                    TextWrapping = TextWrapping.Wrap,
                    FontSize = 12,
                };

                var panel = new StackPanel { Spacing = 12 };
                panel.Children.Add(themeCombo);
                panel.Children.Add(crashToggle);
                panel.Children.Add(statusText);

                var dialog = new ContentDialog
                {
                    Title = "Browser Settings",
                    Content = panel,
                    PrimaryButtonText = "Apply",
                    CloseButtonText = "Cancel",
                    XamlRoot = RootGrid.XamlRoot,
                };

                if (await dialog.ShowAsync() == ContentDialogResult.Primary)
                {
                    CrashTelemetryService.IsOptedIn = crashToggle.IsOn;
                    bool dark = themeCombo.SelectedIndex == 1;
                    RootGrid.RequestedTheme = dark ? ElementTheme.Dark : ElementTheme.Light;
                    foreach (var host in _tabs.Values)
                    {
                        host.NotifyTheme(dark);
                    }
                }
            }
            finally
            {
                _modalOpen = false;
                UpdateViewportBounds();
            }
        }

        private void OnRootKeyDown(object sender, KeyRoutedEventArgs e)
        {
            var ctrl = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control);
            bool isCtrl = ctrl.HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
            if (!isCtrl) return;

            if (e.Key == VirtualKey.T)
            {
                CreateNewTab(HomeUrl);
                e.Handled = true;
            }
            else if (e.Key == VirtualKey.L)
            {
                Omnibox.Focus(FocusState.Programmatic);
                e.Handled = true;
            }
            else if (e.Key == VirtualKey.F)
            {
                OnToggleFindClick(sender, e);
                e.Handled = true;
            }
        }

        private void OnWindowClosed(object sender, WindowEventArgs args)
        {
            _animTimer.Stop();
            foreach (var host in _tabs.Values)
            {
                host.Dispose();
            }
            _tabs.Clear();
        }
    }
}
