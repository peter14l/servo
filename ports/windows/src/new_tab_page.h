/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>

class NewTabPage {
public:
    /// Ensures that newtab.html exists in %LOCALAPPDATA%\Servo Browser\
    /// and returns the file:/// URI string.
    static std::string EnsureNewTabPageExists();

    /// Checks if a given URL is the local new tab page.
    static bool IsNewTabUrl(const std::string& url);
};
