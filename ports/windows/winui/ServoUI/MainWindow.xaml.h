#pragma once

#include "MainWindow.g.h"
#include "pch.h"
#include <cstdint>
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

        // Native-only entry points used by the WinUI shell. These are not part
        // of the IDL surface because they carry raw engine popup handles.
        void ActivateWithRequest(ServoCreateNewWebViewRequest* request);
        void OpenUrlInNewTabFromShell(std::string const& url);
        void OpenSettingsFromShell();
        bool OpenPopupTab(ServoCreateNewWebViewRequest* request);

        // XAML event handlers.
        void RootLoaded(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RootKeyDown(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void ViewportSizeChanged(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void OmniboxKeyDown(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void BackClicked(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ForwardClicked(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ReloadClicked(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeClicked(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddTabClicked(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabsSelectionChanged(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void TabsTabCloseRequested(Microsoft::UI::Xaml::Controls::TabView const& sender,
                                   Microsoft::UI::Xaml::Controls::TabViewTabCloseRequestedEventArgs const& args);
        void FindOpenClicked(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FindBoxKeyDown(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void FindCloseClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsCloseClicked(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // Called by WinUIShell when the engine reports state changes.
        void OnUrlChanged(uint32_t index, std::string const& url);
        void OnTitleChanged(uint32_t index, std::string const& title);
        void OnLoadStatusChanged(uint32_t index, int32_t status);
        void OnStatusTextChanged(std::string const& status);
        void OnCrashed(uint32_t index, char const* reason);
        void OnFindResult(int identifier, int count, int activeOrdinal);
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
        void ApplySettingsFromPane();
        void PopulateSettingsPane();
        void LoadInActiveTab(std::string const& url);
        void SetTabHeader(uint32_t index, std::wstring const& header);

        std::unique_ptr<WinUIShell> m_shell;
        std::string m_initialUrl;
        ServoCreateNewWebViewRequest* m_pendingRequest = nullptr;
        HWND m_xamlHwnd = nullptr;
        uint32_t m_activeTab = 0;
        bool m_loaded = false;
        bool m_updatingTabs = false;
        std::vector<std::string> m_tabUrls;
        std::vector<std::wstring> m_tabTitles;
    };
}

namespace winrt::ServoUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
