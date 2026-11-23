/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "MainWindow.g.h"
#include "pch.h"
#include "../../src/browser_shell.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ServoCreateNewWebViewRequest;

namespace winrt::ServoUI::implementation
{
    class WinUIShell;

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        // Public surface declared in ServoUI.idl.
        void ActivateAsInitial(hstring const& url);
        void ShutdownEngine();

        // Native-only entry points used by the WinUI shell. These are not part
        // of the IDL surface because they carry raw engine popup handles or
        // asynchronous UI callbacks.
        void ActivateWithRequest(ServoCreateNewWebViewRequest* request);
        void OpenUrlInNewTabFromShell(std::string const& url);
        void OpenSettingsFromShell();
        bool OpenPopupTab(ServoCreateNewWebViewRequest* request);

        // Native-only state callbacks invoked by WinUIShell.
        void OnSecurityStateChanged(bool secure, std::wstring const& host);
        void OnDownloadItemsChanged(std::vector<BrowserDownloadItem> const& items);
        void OnZoomChanged(double zoom);
        void OnFullscreenChanged(bool fullscreen);
        void OnNavigationError(int errorCode, std::wstring const& errorText);
        void OnPermissionRequested(std::wstring const& origin, std::wstring const& permission,
                                   std::function<void(bool)> callback);
        void OnAuthenticationRequested(std::wstring const& host, std::wstring const& realm,
                                       std::function<void(std::wstring, std::wstring)> callback);
        void OnPromptRequested(std::wstring const& message, std::wstring const& defaultValue,
                               std::function<void(std::wstring)> callback);

        // XAML event handlers.
        void RootLoaded(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RootKeyDown(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void ViewportSizeChanged(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void OmniboxKeyDown(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OmniboxTextChanged(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void BackClicked(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ForwardClicked(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ReloadClicked(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void StopClicked(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeClicked(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SecurityChipClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SecurityInfoCloseClicked(Windows::Foundation::IInspectable const& sender,
                                      Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BookmarkClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DownloadsClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DownloadsCloseClicked(Windows::Foundation::IInspectable const& sender,
                                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ClearDownloadsClicked(Windows::Foundation::IInspectable const& sender,
                                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SuggestionSelectionChanged(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void TabSearchBoxTextChanged(Windows::Foundation::IInspectable const& sender,
                                     Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void TabSearchSelectionChanged(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void ErrorRetryClicked(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ErrorDismissClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ZoomResetClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ZoomBubbleCloseClicked(Windows::Foundation::IInspectable const& sender,
                                    Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FullscreenClicked(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddTabClicked(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void VerticalTabsToggleClicked(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void VerticalTabsSplitterPointerPressed(Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void VerticalTabsSplitterPointerMoved(Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void VerticalTabsSplitterPointerReleased(Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void VerticalTabsSelectionChanged(Windows::Foundation::IInspectable const& sender,
                                          Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void SettingsClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabsSelectionChanged(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void TabsTabCloseRequested(Microsoft::UI::Xaml::Controls::TabView const& sender,
                                   Microsoft::UI::Xaml::Controls::TabViewTabCloseRequestedEventArgs const& args);
        void TabContextRequested(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);
        void TabContextCloseClicked(Windows::Foundation::IInspectable const& sender,
                                    Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextCloseLeftClicked(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextCloseOthersClicked(Windows::Foundation::IInspectable const& sender,
                                          Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextCloseRightClicked(Windows::Foundation::IInspectable const& sender,
                                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextPinClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextMuteClicked(Windows::Foundation::IInspectable const& sender,
                                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextDuplicateClicked(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextReloadClicked(Windows::Foundation::IInspectable const& sender,
                                     Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextReloadIgnoringCacheClicked(Windows::Foundation::IInspectable const& sender,
                                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextCopyUrlClicked(Windows::Foundation::IInspectable const& sender,
                                      Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextRestoreClosedClicked(Windows::Foundation::IInspectable const& sender,
                                            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabContextTabSearchClicked(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppRestoreTabClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppTabSearchClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppHistoryClicked(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppBookmarksClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppDownloadsClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppPrintClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AppFullscreenClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindOpenClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindBoxKeyDown(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void FindBoxTextChanged(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void FindOptionClicked(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindPreviousClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindNextClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindCloseClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsSaveClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsCloseClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsSearchTextChanged(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void ClearHistoryClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ClearDataClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ResetSettingsClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void PermissionNotificationsChanged(Windows::Foundation::IInspectable const& sender,
                                            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void PermissionGeolocationChanged(Windows::Foundation::IInspectable const& sender,
                                          Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void CheckDefaultBrowserClicked(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MakeDefaultBrowserClicked(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // Called by WinUIShell when the engine reports state changes.
        void OnUrlChanged(uint32_t index, std::string const& url);
        void OnTitleChanged(uint32_t index, std::string const& title);
        void OnLoadStatusChanged(uint32_t index, int32_t status);
        void OnStatusTextChanged(std::string const& status);
        void OnCrashed(uint32_t index, char const* reason);
        void OnFindResult(int identifier, int count, int activeOrdinal);
        void OnTabFavicon(uint32_t index, std::vector<uint8_t> const& pixels,
                          uint32_t width, uint32_t height);
        void OnDownloadStarted(std::string const& url);
        void OnDownloadUpdated(std::string const& url, int64_t received, int64_t total);
        void HandleWebIpcMessage(std::string const& message);
        void OpenSettingsPane();

    private:
        HWND GetXamlHwnd();
        void EnsureShell();
        void ConfigureTitleBar();
        void UpdateDragRegions();
        void LayoutViewport();
        void AddTab(std::string url, ServoCreateNewWebViewRequest* request = nullptr);
        void CloseTab(uint32_t index);
        void SelectTab(uint32_t index);
        void UpdateActiveTab(uint32_t index);
        void UpdateStatus(std::wstring const& status);
        void UpdateNavigationButtons();
        void SetStopReloadState(bool loading);
        void UpdateBookmarkState();
        void ApplySettingsFromPane();
        void PopulateSettingsPane();
        void ApplyTheme(int themeMode);
        void ApplyVerticalTabs(bool enable);
        void SetVerticalTabsColumnWidth(int width);
        void PersistVerticalTabsWidth();
        void RefreshVerticalTabs();
        void LoadInActiveTab(std::string const& url);
        void SetTabHeader(uint32_t index, std::wstring const& header);
        Windows::Foundation::IInspectable CreateTabHeader(uint32_t index, std::wstring const& title);
        uint32_t TabIndexFromContextMenu(Windows::Foundation::IInspectable const& sender);
        void HideSuggestions();
        void PopulateSuggestions();
        void SubmitOmnibox(std::wstring const& text);
        void PopulateDownloads();
        void HandleDownloadAction(uint64_t id, int action);
        void ShowTabSearch();
        void PopulateTabSearch();
        void RestoreRecentlyClosedTab();
        void CycleFocus(bool forward);
        void ToggleFullscreen();
        void RefreshDefaultBrowserStatus();
        void ApplyPermissionChoice(std::string const& origin, int32_t permission, int choice);
        void ShowErrorBanner(int errorCode, std::wstring const& text);

        winrt::event_token m_rootLayoutUpdatedToken{};
        std::unique_ptr<WinUIShell> m_shell;
        std::string m_initialUrl;
        ServoCreateNewWebViewRequest* m_pendingRequest = nullptr;
        HWND m_xamlHwnd = nullptr;
        uint32_t m_activeTab = 0;
        uint32_t m_contextTabIndex = 0;
        bool m_loaded = false;
        bool m_updatingTabs = false;
        bool m_engineShutdown = false;
        bool m_resizingVerticalTabs = false;
        uint32_t m_resizePointerId = 0;
        double m_resizeStartX = 0.0;
        int m_resizeStartWidth = 180;
        bool m_loading = false;
        bool m_fullscreen = false;
        bool m_secure = false;
        bool m_updatingOmnibox = false;
        std::wstring m_securityHost;
        std::wstring m_customSearchTemplate;
        std::vector<std::string> m_tabUrls;
        std::vector<std::wstring> m_tabTitles;
        std::vector<bool> m_tabPinned;
        std::vector<bool> m_tabMuted;
        std::vector<std::string> m_recentlyClosedUrls;
        std::vector<std::wstring> m_suggestions;
        int m_suggestionIndex = -1;
        Microsoft::UI::Xaml::DispatcherTimer m_zoomTimer;
        Microsoft::UI::Xaml::Controls::ContentDialog m_activeDialog;
        std::vector<Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource> m_tabFaviconSources;
    };
}

namespace winrt::ServoUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
