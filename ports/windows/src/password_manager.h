/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct SavedCredential {
    std::string origin;     // Normalized origin e.g. "https://www.sxuk.org"
    std::string username;   // User ID, email, login name
    std::string password;   // Decrypted plaintext in memory
    uint64_t timestamp = 0;
};

class PasswordManager {
public:
    static std::string NormalizeOrigin(const std::string& url);
    static std::vector<SavedCredential> LoadCredentials();
    static bool SaveCredential(const std::string& origin, const std::string& username, const std::string& password);
    static bool FindCredential(const std::string& origin, SavedCredential& outCred);
    static bool HasCredential(const std::string& origin, const std::string& username, const std::string& password);
    static bool DeleteCredential(const std::string& origin, const std::string& username);
    static void ClearAllCredentials();

private:
    static std::wstring GetPasswordsFilePath();
    static std::string EncryptPassword(const std::string& plaintext);
    static std::string DecryptPassword(const std::string& ciphertextHex);
};
