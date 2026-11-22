/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
