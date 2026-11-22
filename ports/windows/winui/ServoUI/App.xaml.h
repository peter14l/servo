#pragma once

#include "App.xaml.g.h"
#include "pch.h"

namespace winrt::ServoUI::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}
