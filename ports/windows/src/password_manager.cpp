/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "password_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <iomanip>

#pragma comment(lib, "crypt32.lib")

std::wstring PasswordManager::GetPasswordsFilePath() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring baseDir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(baseDir.c_str(), nullptr);
        return baseDir + L"\\passwords.json";
    }
    return L"passwords.json";
}

std::string PasswordManager::NormalizeOrigin(const std::string& url) {
    if (url.empty()) return "";
    std::string s = url;
    // Unwrap gateway URL: http://127.0.0.1:51820/... -> https://www.sxuk.org
    if (s.find("127.0.0.1:51820") != std::string::npos || s.find("localhost:51820") != std::string::npos) {
        return "https://www.sxuk.org";
    }
    size_t schemeEnd = s.find("://");
    if (schemeEnd == std::string::npos) {
        // Domain only
        size_t slashPos = s.find('/');
        if (slashPos != std::string::npos) s = s.substr(0, slashPos);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return "https://" + s;
    }
    size_t slashPos = s.find('/', schemeEnd + 3);
    if (slashPos != std::string::npos) {
        s = s.substr(0, slashPos);
    }
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string PasswordManager::EncryptPassword(const std::string& plaintext) {
    if (plaintext.empty()) return "";
    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    inBlob.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB outBlob = {};
    if (CryptProtectData(&inBlob, L"ServoBrowserCredential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::ostringstream hexStream;
        for (DWORD i = 0; i < outBlob.cbData; ++i) {
            hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(outBlob.pbData[i]);
        }
        LocalFree(outBlob.pbData);
        return hexStream.str();
    }
    // Fallback: hex encode if DPAPI unavailable
    std::ostringstream hexStream;
    for (unsigned char c : plaintext) {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return "raw:" + hexStream.str();
}

std::string PasswordManager::DecryptPassword(const std::string& ciphertextHex) {
    if (ciphertextHex.empty()) return "";
    if (ciphertextHex.find("raw:") == 0) {
        std::string raw = ciphertextHex.substr(4);
        std::string res;
        for (size_t i = 0; i + 1 < raw.size(); i += 2) {
            std::string byteStr = raw.substr(i, 2);
            res += static_cast<char>(std::strtoul(byteStr.c_str(), nullptr, 16));
        }
        return res;
    }
    if (ciphertextHex.size() % 2 != 0) return "";
    std::vector<BYTE> cipherBytes(ciphertextHex.size() / 2);
    for (size_t i = 0; i < cipherBytes.size(); ++i) {
        std::string byteStr = ciphertextHex.substr(i * 2, 2);
        cipherBytes[i] = static_cast<BYTE>(std::strtoul(byteStr.c_str(), nullptr, 16));
    }
    DATA_BLOB inBlob;
    inBlob.pbData = cipherBytes.data();
    inBlob.cbData = static_cast<DWORD>(cipherBytes.size());
    DATA_BLOB outBlob = {};
    if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::string result(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
        LocalFree(outBlob.pbData);
        return result;
    }
    return "";
}

static std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static std::string ExtractJsonField(const std::string& obj, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = obj.find(search);
    if (pos == std::string::npos) return "";
    size_t start = pos + search.size();
    size_t end = obj.find("\"", start);
    if (end == std::string::npos) return "";
    std::string val = obj.substr(start, end - start);
    // Unescape basic quotes and slashes
    std::string out;
    for (size_t i = 0; i < val.size(); ++i) {
        if (val[i] == '\\' && i + 1 < val.size()) {
            char next = val[++i];
            if (next == '"') out += '"';
            else if (next == '\\') out += '\\';
            else if (next == 'n') out += '\n';
            else if (next == 'r') out += '\r';
            else if (next == 't') out += '\t';
            else out += next;
        } else {
            out += val[i];
        }
    }
    return out;
}

std::vector<SavedCredential> PasswordManager::LoadCredentials() {
    std::vector<SavedCredential> list;
    std::wstring path = GetPasswordsFilePath();
    std::ifstream file(path);
    if (!file.is_open()) return list;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t endPos = content.find('}', pos);
        if (endPos == std::string::npos) break;
        std::string obj = content.substr(pos, endPos - pos + 1);

        std::string origin = ExtractJsonField(obj, "origin");
        std::string username = ExtractJsonField(obj, "username");
        std::string encPwd = ExtractJsonField(obj, "password_enc");

        uint64_t timestamp = 0;
        size_t tsPos = obj.find("\"timestamp\":");
        if (tsPos != std::string::npos) {
            timestamp = std::strtoull(obj.c_str() + tsPos + 12, nullptr, 10);
        }

        if (!origin.empty() && !username.empty() && !encPwd.empty()) {
            std::string decPwd = DecryptPassword(encPwd);
            if (!decPwd.empty()) {
                SavedCredential c;
                c.origin = origin;
                c.username = username;
                c.password = decPwd;
                c.timestamp = timestamp;
                list.push_back(c);
            }
        }
        pos = endPos + 1;
    }
    return list;
}

static void SaveCredentialsToDisk(const std::vector<SavedCredential>& list) {
    std::wstring path = PasswordManager::GetPasswordsFilePath();
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& c = list[i];
        file << "  {\n";
        file << "    \"origin\": \"" << EscapeJsonString(c.origin) << "\",\n";
        file << "    \"username\": \"" << EscapeJsonString(c.username) << "\",\n";
        // Encrypt with DPAPI before writing to disk
        // (Access private static method by calling PasswordManager::SaveCredential or inline)
        // Here we call Encrypt through friend or save logic
    }
}

bool PasswordManager::SaveCredential(const std::string& origin, const std::string& username, const std::string& password) {
    if (origin.empty() || username.empty() || password.empty()) return false;
    std::string normOrigin = NormalizeOrigin(origin);
    auto creds = LoadCredentials();

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    bool updated = false;
    for (auto& c : creds) {
        if (c.origin == normOrigin && c.username == username) {
            c.password = password;
            c.timestamp = now;
            updated = true;
            break;
        }
    }

    if (!updated) {
        SavedCredential newCred;
        newCred.origin = normOrigin;
        newCred.username = username;
        newCred.password = password;
        newCred.timestamp = now;
        creds.push_back(newCred);
    }

    std::wstring path = GetPasswordsFilePath();
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "[\n";
    for (size_t i = 0; i < creds.size(); ++i) {
        const auto& c = creds[i];
        file << "  {\n";
        file << "    \"origin\": \"" << EscapeJsonString(c.origin) << "\",\n";
        file << "    \"username\": \"" << EscapeJsonString(c.username) << "\",\n";
        file << "    \"password_enc\": \"" << EncryptPassword(c.password) << "\",\n";
        file << "    \"timestamp\": " << c.timestamp << "\n";
        file << "  }" << (i + 1 < creds.size() ? ",\n" : "\n");
    }
    file << "]\n";
    return true;
}

bool PasswordManager::FindCredential(const std::string& origin, SavedCredential& outCred) {
    std::string normOrigin = NormalizeOrigin(origin);
    auto creds = LoadCredentials();
    for (const auto& c : creds) {
        if (c.origin == normOrigin) {
            outCred = c;
            return true;
        }
    }
    return false;
}

bool PasswordManager::HasCredential(const std::string& origin, const std::string& username, const std::string& password) {
    std::string normOrigin = NormalizeOrigin(origin);
    auto creds = LoadCredentials();
    for (const auto& c : creds) {
        if (c.origin == normOrigin && c.username == username && c.password == password) {
            return true;
        }
    }
    return false;
}

bool PasswordManager::DeleteCredential(const std::string& origin, const std::string& username) {
    std::string normOrigin = NormalizeOrigin(origin);
    auto creds = LoadCredentials();
    auto it = std::remove_if(creds.begin(), creds.end(), [&](const SavedCredential& c) {
        return c.origin == normOrigin && c.username == username;
    });
    if (it == creds.end()) return false;
    creds.erase(it, creds.end());

    std::wstring path = GetPasswordsFilePath();
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "[\n";
    for (size_t i = 0; i < creds.size(); ++i) {
        const auto& c = creds[i];
        file << "  {\n";
        file << "    \"origin\": \"" << EscapeJsonString(c.origin) << "\",\n";
        file << "    \"username\": \"" << EscapeJsonString(c.username) << "\",\n";
        file << "    \"password_enc\": \"" << EncryptPassword(c.password) << "\",\n";
        file << "    \"timestamp\": " << c.timestamp << "\n";
        file << "  }" << (i + 1 < creds.size() ? ",\n" : "\n");
    }
    file << "]\n";
    return true;
}

void PasswordManager::ClearAllCredentials() {
    std::wstring path = GetPasswordsFilePath();
    DeleteFileW(path.c_str());
}
