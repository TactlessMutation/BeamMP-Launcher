/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <filesystem>
#include "Utils.h"

#if defined(_WIN32)
#include <shlobj_core.h>
#elif defined(__linux__)
#include "vdf_parser.hpp"
#include <pwd.h>
#include <unistd.h>
#include <vector>
#endif

#include "Logger.h"
#include "Options.h"

#include <fstream>
#include <string>
#include <thread>

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

int TraceBack = 0;
beammp_fs_string GameDir;

void lowExit(int code) {
    TraceBack = 0;

    std::string msg =
        "Failed to find the game please launch it. Report this if the issue persists code ";

    error(msg + std::to_string(code));

    std::this_thread::sleep_for(std::chrono::seconds(10));

    exit(2);
}

beammp_fs_string GetGameDir() {
    return GameDir;
}

#ifdef _WIN32

LONG OpenKey(HKEY root, const char* path, PHKEY hKey) {
    return RegOpenKeyEx(
        root,
        reinterpret_cast<LPCSTR>(path),
        0,
        KEY_READ,
        hKey
    );
}

std::wstring QueryKey(HKEY hKey, int ID) {
    wchar_t* achKey;
    DWORD cbName;

    TCHAR achClass[MAX_PATH] = TEXT("");
    DWORD cchClassName = MAX_PATH;

    DWORD cSubKeys = 0;
    DWORD cbMaxSubKey;
    DWORD cchMaxClass;

    DWORD cValues;
    DWORD cchMaxValue;
    DWORD cbMaxValueData;

    DWORD cbSecurityDescriptor;
    FILETIME ftLastWriteTime;

    DWORD i, retCode;

    wchar_t* achValue = new wchar_t[MAX_VALUE_NAME];
    DWORD cchValue = MAX_VALUE_NAME;

    retCode = RegQueryInfoKey(
        hKey,
        achClass,
        &cchClassName,
        nullptr,
        &cSubKeys,
        &cbMaxSubKey,
        &cchMaxClass,
        &cValues,
        &cchMaxValue,
        &cbMaxValueData,
        &cbSecurityDescriptor,
        &ftLastWriteTime
    );

    BYTE* buffer = new BYTE[cbMaxValueData];

    ZeroMemory(buffer, cbMaxValueData);

    if (cSubKeys) {
        for (i = 0; i < cSubKeys; i++) {
            cbName = MAX_KEY_LENGTH;

            retCode = RegEnumKeyExW(
                hKey,
                i,
                achKey,
                &cbName,
                nullptr,
                nullptr,
                nullptr,
                &ftLastWriteTime
            );

            if (retCode == ERROR_SUCCESS) {
                if (wcscmp(achKey, L"Steam App 284160") == 0) {
                    return achKey;
                }
            }
        }
    }

    if (cValues) {
        for (i = 0, retCode = ERROR_SUCCESS; i < cValues; i++) {
            cchValue = MAX_VALUE_NAME;
            achValue[0] = '\0';

            retCode = RegEnumValueW(
                hKey,
                i,
                achValue,
                &cchValue,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            );

            if (retCode == ERROR_SUCCESS) {
                DWORD lpData = cbMaxValueData;

                buffer[0] = '\0';

                LONG dwRes = RegQueryValueExW(
                    hKey,
                    achValue,
                    nullptr,
                    nullptr,
                    buffer,
                    &lpData
                );

                (void)dwRes;

                std::wstring data = (wchar_t*)(buffer);
                std::wstring key = achValue;

                switch (ID) {
                case 1:
                    if (key == L"SteamExe") {
                        auto p = data.find_last_of(L"/\\");

                        if (p != std::wstring::npos) {
                            return data.substr(0, p);
                        }
                    }
                    break;

                case 2:
                    if (key == L"Name" && data == L"BeamNG.drive") {
                        return data;
                    }
                    break;

                case 3:
                    if (key == L"rootpath") {
                        return data;
                    }
                    break;

                case 4:
                    if (key == L"userpath_override") {
                        return data;
                    }
                    break;

                case 5:
                    if (key == L"Local AppData") {
                        return data;
                    }
                    break;

                default:
                    break;
                }
            }
        }
    }

    delete[] achValue;
    delete[] buffer;

    return L"";
}

#endif

namespace fs = std::filesystem;

bool NameValid(const std::string& N) {
    if (N == "config" || N == "librarycache") {
        return true;
    }

    if (
        N.find_first_not_of("0123456789") ==
        std::string::npos
    ) {
        return true;
    }

    return false;
}

void FileList(
    std::vector<std::string>& a,
    const std::string& Path
) {
    for (const auto& entry : fs::directory_iterator(Path)) {
        const auto& DPath = entry.path();

        if (!entry.is_directory()) {
            a.emplace_back(DPath.string());
        } else if (NameValid(DPath.filename().string())) {
            FileList(a, DPath.string());
        }
    }
}

void LegitimacyCheck() {

    /*
     * --game-dir is an explicit override.
     *
     * When supplied, do not attempt to discover the game through
     * Steam, BeamNG.Drive.ini, the Windows registry, etc.
     *
     * integrity.json is not required for this mode.
     */
    if (!options.game_dir.empty()) {

#if defined(_WIN32)

        std::filesystem::path customGameDir =
            std::filesystem::absolute(
                std::filesystem::path(
                    Utils::ToWString(options.game_dir)
                )
            ).lexically_normal();

        if (!std::filesystem::exists(customGameDir)) {
            throw std::runtime_error(
                "The directory specified by --game-dir does not exist: " +
                customGameDir.string()
            );
        }

        if (!std::filesystem::is_directory(customGameDir)) {
            throw std::runtime_error(
                "The path specified by --game-dir is not a directory: " +
                customGameDir.string()
            );
        }

        GameDir = customGameDir.wstring();

        debug(L"Using custom game directory: " + GameDir);

#elif defined(__linux__)

        std::filesystem::path customGameDir =
            std::filesystem::absolute(
                std::filesystem::path(options.game_dir)
            ).lexically_normal();

        if (!std::filesystem::exists(customGameDir)) {
            throw std::runtime_error(
                "The directory specified by --game-dir does not exist: " +
                customGameDir.string()
            );
        }

        if (!std::filesystem::is_directory(customGameDir)) {
            throw std::runtime_error(
                "The path specified by --game-dir is not a directory: " +
                customGameDir.string()
            );
        }

        GameDir = customGameDir.string();

        debug("Using custom game directory: " + GameDir);

#endif

        return;
    }

#if defined(_WIN32)

    wchar_t* appDataPath = new wchar_t[MAX_PATH];

    HRESULT result = SHGetFolderPathW(
        NULL,
        CSIDL_LOCAL_APPDATA,
        NULL,
        0,
        appDataPath
    );

    if (!SUCCEEDED(result)) {
        fatal("Cannot get Local Appdata directory");
    }

    auto BeamNGAppdataPath =
        std::filesystem::path(appDataPath) / "BeamNG";

    if (
        const auto beamngIniPath =
            BeamNGAppdataPath / "BeamNG.Drive.ini";
        exists(beamngIniPath)
    ) {
        if (std::ifstream beamngIni(beamngIniPath);
            beamngIni.is_open()) {

            std::string contents(
                (std::istreambuf_iterator<char>(beamngIni)),
                std::istreambuf_iterator<char>()
            );

            beamngIni.close();

            if (
                contents.size() >= 3 &&
                (unsigned char)contents[0] == 0xEF &&
                (unsigned char)contents[1] == 0xBB &&
                (unsigned char)contents[2] == 0xBF
            ) {
                contents = contents.substr(3);
            }

            auto ini = Utils::ParseINI(contents);

            if (ini.empty()) {
                lowExit(3);
            } else {
                debug("Successfully parsed BeamNG.Drive.ini");
            }

            if (ini.contains("installPath")) {
                std::wstring installPath =
                    Utils::ToWString(
                        std::get<std::string>(
                            ini["installPath"]
                        )
                    );

                installPath.erase(
                    0,
                    installPath.find_first_not_of(L" \t")
                );

                installPath =
                    std::filesystem::path(
                        Utils::ExpandEnvVars(installPath)
                    );

                if (std::filesystem::exists(installPath)) {
                    GameDir = installPath;

                    debug(
                        L"GameDir from BeamNG.Drive.ini: " +
                        installPath
                    );
                } else {
                    lowExit(4);
                }
            } else {
                lowExit(5);
            }
        }
    } else {

        std::wstring Result;

        std::string K3 =
            R"(Software\BeamNG\BeamNG.drive)";

        HKEY hKey;

        LONG dwRegOpenKey =
            OpenKey(
                HKEY_CURRENT_USER,
                K3.c_str(),
                &hKey
            );

        if (dwRegOpenKey == ERROR_SUCCESS) {

            Result = QueryKey(hKey, 3);

            if (Result.empty()) {
                debug(
                    "Failed to QUERY key "
                    "HKEY_CURRENT_USER\\Software\\BeamNG\\BeamNG.drive"
                );

                lowExit(6);
            }

            GameDir = Result;

            debug(
                L"GameDir from registry: " +
                Result
            );

        } else {

            debug(
                "Failed to OPEN key "
                "HKEY_CURRENT_USER\\Software\\BeamNG\\BeamNG.drive"
            );

            lowExit(7);
        }

        K3.clear();
        Result.clear();

        RegCloseKey(hKey);
    }

    delete[] appDataPath;

#elif defined(__linux__)

    struct passwd* pw = getpwuid(getuid());

    if (pw == nullptr || pw->pw_dir == nullptr) {
        error("Unable to determine the current user's home directory.");
        return;
    }

    std::filesystem::path homeDir = pw->pw_dir;

    std::vector<std::filesystem::path>
        steamappsCommonPaths = {
            ".steam/root/steamapps",
            ".steam/steam/steamapps",
            ".var/app/com.valvesoftware.Steam/.steam/root/steamapps",
            "snap/steam/common/.local/share/Steam/steamapps"
        };

    std::filesystem::path steamappsPath;
    std::filesystem::path libraryFoldersPath;

    bool steamappsFolderFound = false;
    bool libraryFoldersFound = false;

    for (const auto& path : steamappsCommonPaths) {
        steamappsPath = homeDir / path;

        if (std::filesystem::exists(steamappsPath)) {
            steamappsFolderFound = true;

            libraryFoldersPath =
                steamappsPath / "libraryfolders.vdf";

            if (std::filesystem::exists(libraryFoldersPath)) {
                libraryFoldersFound = true;
                break;
            }
        }
    }

    if (!steamappsFolderFound) {
        error("Unsupported Steam installation.");
        return;
    }

    if (!libraryFoldersFound) {
        error("libraryfolders.vdf is missing.");
        return;
    }

    std::ifstream libraryFolders(libraryFoldersPath);

    if (!libraryFolders.is_open()) {
        error("Unable to open libraryfolders.vdf.");
        return;
    }

    auto root = tyti::vdf::read(libraryFolders);

    for (auto folderInfo : root.childs) {

        if (
            folderInfo.second->childs["apps"]->attribs.contains("284160") &&
            std::filesystem::exists(
                folderInfo.second->attribs["path"] +
                "/steamapps/common/BeamNG.drive"
            )
        ) {
            GameDir =
                folderInfo.second->attribs["path"] +
                "/steamapps/common/BeamNG.drive/";

            break;
        }
    }

    if (GameDir.empty()) {
        error("The game directory was not found.");
        return;
    }

#endif
}

/*
 * integrity.json is NOT required to locate or launch BeamNG.
 *
 * The launcher only uses this function to display the game version.
 * If the file is unavailable, return an empty version instead of
 * preventing the launcher from continuing.
 *
 * This is particularly important on Linux/Proton installations,
 * where relying on integrity.json is unnecessary for locating the
 * game or installing the BeamMP mod.
 */
std::string CheckVer(const std::filesystem::path& dir) {

    const std::filesystem::path Path =
        dir / beammp_wide("integrity.json");

    if (!std::filesystem::exists(Path)) {
        warn(
            "integrity.json was not found. "
            "Skipping game version check."
        );

        return "";
    }

    if (!std::filesystem::is_regular_file(Path)) {
        warn(
            "integrity.json is not a regular file. "
            "Skipping game version check."
        );

        return "";
    }

    std::ifstream f(
        Path.c_str(),
        std::ios::binary
    );

    if (!f.is_open()) {
        warn(
            "Could not open integrity.json. "
            "Skipping game version check."
        );

        return "";
    }

    const auto fileSize = std::filesystem::file_size(Path);

    if (fileSize == 0) {
        warn(
            "integrity.json is empty. "
            "Skipping game version check."
        );

        return "";
    }

    std::string vec(
        static_cast<size_t>(fileSize),
        '\0'
    );

    f.read(
        vec.data(),
        static_cast<std::streamsize>(vec.size())
    );

    if (!f) {
        warn(
            "Failed to read integrity.json. "
            "Skipping game version check."
        );

        return "";
    }

    f.close();

    /*
     * Find the version field without requiring a full JSON parser.
     * This preserves the behavior of the old implementation while
     * making failure non-fatal.
     */
    const auto versionPos = vec.find("version");

    if (versionPos == std::string::npos) {
        warn(
            "No version field found in integrity.json. "
            "Skipping game version check."
        );

        return "";
    }

    const auto quoteStart =
        vec.find('"', versionPos + 7);

    if (quoteStart == std::string::npos) {
        warn(
            "Malformed version field in integrity.json. "
            "Skipping game version check."
        );

        return "";
    }

    const auto quoteEnd =
        vec.find('"', quoteStart + 1);

    if (quoteEnd == std::string::npos) {
        warn(
            "Malformed version field in integrity.json. "
            "Skipping game version check."
        );

        return "";
    }

    std::string temp =
        vec.substr(
            quoteStart + 1,
            quoteEnd - quoteStart - 1
        );

    std::string result;

    for (const char& a : temp) {
        if (
            std::isdigit(static_cast<unsigned char>(a)) ||
            a == '.'
        ) {
            result += a;
        }
    }

    if (result.empty()) {
        warn(
            "Could not determine the game version from integrity.json. "
            "Skipping game version check."
        );
    }

    return result;
}
