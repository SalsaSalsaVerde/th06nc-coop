// th06nc_native_coop_installer.exe -- drop this single file into the th06nc
// folder and run it (docs/09). Installs, updates or removes the native co-op
// mod by swapping steam_api64.dll for the mod's DLL proxy (the real Steam DLL
// is kept as steam_api64_orig.dll, which the proxy forwards to).
//
// Also recognizes the older overlay co-op mod's proxy and upgrades over it,
// keeping that DLL as steam_api64.previous_proxy.dll. The mod DLL and the
// example settings file are embedded (installer.rc); the settings file is
// only written when the folder doesn't have one yet.
//
// Flags (all optional): --path <dir>, --install (install or update without
// asking), --uninstall, --no-pause.

#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "resource.h"

namespace {

const char* kDllName = "steam_api64.dll";
const char* kOrigDllName = "steam_api64_orig.dll";
const char* kPreviousProxyName = "steam_api64.previous_proxy.dll";
const char* kIniName = "th06nc_native_coop.ini";
const char* kExeName = "th06nc.exe";

// Log filename literals compiled into each mod's DLL: how the installer
// tells a mod proxy from the real Steam DLL before touching anything.
const char* kNativeMarker = "th06nc_native_coop.log";
const char* kOverlayMarker = "th06nc_mod_proxy.log";

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') return dir + "\\" + name;
    return dir + name;
}

bool FileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadWholeFile(const std::string& path, std::string* out) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(file, nullptr);
    bool ok = false;
    if (size != INVALID_FILE_SIZE) {
        out->assign(size, '\0');
        DWORD read = 0;
        ok = size == 0 || (ReadFile(file, &(*out)[0], size, &read, nullptr) && read == size);
    }
    CloseHandle(file);
    return ok;
}

bool FileContains(const std::string& path, const char* marker) {
    std::string data;
    return ReadWholeFile(path, &data) && data.find(marker) != std::string::npos;
}

std::string GetExeDir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    size_t pos = s.find_last_of('\\');
    return pos == std::string::npos ? std::string() : s.substr(0, pos);
}

bool Embedded(int id, const void** data, DWORD* size) {
    HMODULE self = GetModuleHandle(nullptr);
    HRSRC res = FindResourceA(self, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10)); // RT_RCDATA
    if (!res) return false;
    HGLOBAL handle = LoadResource(self, res);
    *data = handle ? LockResource(handle) : nullptr;
    *size = SizeofResource(self, res);
    return *data != nullptr && *size > 0;
}

void ExplainError(const char* what, const std::string& path) {
    DWORD error = GetLastError();
    printf("ERROR: could not %s %s (error %lu).\n", what, path.c_str(), error);
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED || error == ERROR_LOCK_VIOLATION) {
        printf("       Is the game still running? Close it and run this again.\n");
    }
}

bool WriteEmbedded(int id, const std::string& path) {
    const void* data = nullptr;
    DWORD size = 0;
    if (!Embedded(id, &data, &size)) {
        printf("ERROR: this installer is missing its embedded files -- it wasn't built correctly.\n");
        return false;
    }
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ExplainError("write", path);
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!ok) ExplainError("write", path);
    return ok;
}

bool SameAsEmbedded(int id, const std::string& path) {
    const void* data = nullptr;
    DWORD size = 0;
    std::string current;
    return Embedded(id, &data, &size) && ReadWholeFile(path, &current) && current.size() == size &&
           memcmp(current.data(), data, size) == 0;
}

void WriteDefaultSettings(const std::string& gameDir) {
    std::string ini = JoinPath(gameDir, kIniName);
    if (FileExists(ini)) {
        printf("Keeping your existing settings (%s).\n", kIniName);
        return;
    }
    if (WriteEmbedded(IDR_EXAMPLE_INI, ini)) {
        printf("Wrote default settings to %s (F8 in the game's menus changes them too).\n", kIniName);
    }
}

enum class State {
    Vanilla,          // only the real steam_api64.dll
    NativeInstalled,  // this mod
    OverlayInstalled, // the older overlay co-op mod
    Unknown,
};

State Detect(const std::string& gameDir) {
    std::string dll = JoinPath(gameDir, kDllName);
    std::string orig = JoinPath(gameDir, kOrigDllName);
    bool haveDll = FileExists(dll);
    bool haveOrig = FileExists(orig);
    if (haveDll && !haveOrig) {
        // A proxy without the real DLL next to it would be broken, not vanilla.
        return FileContains(dll, kNativeMarker) || FileContains(dll, kOverlayMarker) ? State::Unknown : State::Vanilla;
    }
    if (haveDll && haveOrig) {
        if (FileContains(dll, kNativeMarker)) return State::NativeInstalled;
        if (FileContains(dll, kOverlayMarker)) return State::OverlayInstalled;
    }
    return State::Unknown;
}

bool InstallFresh(const std::string& gameDir) {
    std::string dll = JoinPath(gameDir, kDllName);
    std::string orig = JoinPath(gameDir, kOrigDllName);
    if (!MoveFileA(dll.c_str(), orig.c_str())) {
        ExplainError("rename", dll);
        return false;
    }
    if (!WriteEmbedded(IDR_MOD_DLL, dll)) {
        printf("Putting the original back...\n");
        MoveFileA(orig.c_str(), dll.c_str());
        return false;
    }
    printf("Installed. The real %s is kept as %s.\n", kDllName, kOrigDllName);
    WriteDefaultSettings(gameDir);
    return true;
}

bool Update(const std::string& gameDir) {
    std::string dll = JoinPath(gameDir, kDllName);
    if (SameAsEmbedded(IDR_MOD_DLL, dll)) {
        printf("Already up to date.\n");
        WriteDefaultSettings(gameDir);
        return true;
    }
    if (!WriteEmbedded(IDR_MOD_DLL, dll)) return false;
    printf("Updated the mod DLL.\n");
    WriteDefaultSettings(gameDir);
    return true;
}

bool ReplaceOverlay(const std::string& gameDir) {
    std::string dll = JoinPath(gameDir, kDllName);
    std::string backup = JoinPath(gameDir, kPreviousProxyName);
    if (!CopyFileA(dll.c_str(), backup.c_str(), FALSE)) {
        ExplainError("back up", dll);
        return false;
    }
    if (!WriteEmbedded(IDR_MOD_DLL, dll)) return false;
    printf("Replaced the overlay co-op mod (kept as %s).\n", kPreviousProxyName);
    WriteDefaultSettings(gameDir);
    return true;
}

bool Uninstall(const std::string& gameDir) {
    std::string dll = JoinPath(gameDir, kDllName);
    std::string orig = JoinPath(gameDir, kOrigDllName);
    if (!FileExists(orig)) {
        printf("ERROR: %s not found -- nothing to restore.\n", kOrigDllName);
        return false;
    }
    if (FileExists(dll)) {
        if (!FileContains(dll, kNativeMarker) && !FileContains(dll, kOverlayMarker)) {
            printf("ERROR: the current %s isn't a co-op mod DLL -- refusing to touch it.\n", kDllName);
            return false;
        }
        if (!DeleteFileA(dll.c_str())) {
            ExplainError("remove", dll);
            return false;
        }
    }
    if (!MoveFileA(orig.c_str(), dll.c_str())) {
        ExplainError("restore", orig);
        return false;
    }
    printf("Uninstalled: %s is the original Steam DLL again.\n", kDllName);
    printf("(Settings, log and any %s backup were left in place.)\n", kPreviousProxyName);
    return true;
}

char Ask(const char* question) {
    printf("%s ", question);
    std::string line;
    std::getline(std::cin, line);
    return line.empty() ? '\0' : static_cast<char>(tolower(static_cast<unsigned char>(line[0])));
}

bool HasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

int Run(int argc, char** argv) {
    printf("th06nc Native Co-op Installer\n=============================\n\n");

    std::string gameDir = GetExeDir();
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--path") == 0) gameDir = argv[i + 1];
    }
    if (!FileExists(JoinPath(gameDir, kExeName))) {
        printf("Could not find %s in:\n  %s\n\n", kExeName, gameDir.c_str());
        printf("This installer normally runs from inside your th06nc folder.\n");
        printf("Enter the full path to that folder (or press Enter to quit):\n> ");
        std::string typed;
        std::getline(std::cin, typed);
        if (typed.empty() || !FileExists(JoinPath(typed, kExeName))) {
            printf("Still can't find %s there.\n", kExeName);
            return 1;
        }
        gameDir = typed;
    }
    printf("Game folder: %s\n\n", gameDir.c_str());

    bool forceInstall = HasFlag(argc, argv, "--install");
    bool forceUninstall = HasFlag(argc, argv, "--uninstall");
    State state = Detect(gameDir);

    if (forceUninstall) return Uninstall(gameDir) ? 0 : 1;

    switch (state) {
        case State::Vanilla:
            printf("The mod isn't installed yet. Installing...\n");
            return InstallFresh(gameDir) ? 0 : 1;

        case State::OverlayInstalled: {
            printf("The older overlay co-op mod is installed.\n");
            char c = forceInstall ? 'y' : Ask("Replace it with native co-op? [Y]es / [U]ninstall it / [C]ancel:");
            if (c == 'y' || c == '\0') return ReplaceOverlay(gameDir) ? 0 : 1;
            if (c == 'u') return Uninstall(gameDir) ? 0 : 1;
            printf("Cancelled.\n");
            return 0;
        }

        case State::NativeInstalled: {
            printf("Native co-op is already installed.\n");
            char c = forceInstall ? 'u' : Ask("[U]pdate to this version / [R]emove the mod / [C]ancel:");
            if (c == 'u' || c == '\0') return Update(gameDir) ? 0 : 1;
            if (c == 'r') return Uninstall(gameDir) ? 0 : 1;
            printf("Cancelled.\n");
            return 0;
        }

        case State::Unknown:
        default:
            printf("Unexpected files in this folder -- not touching anything.\n"
                   "Expected either just %s (not installed) or %s plus a co-op mod's\n"
                   "%s. If Steam re-downloaded %s, delete %s and run this again.\n",
                   kDllName, kOrigDllName, kDllName, kDllName, kOrigDllName);
            return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    int result = Run(argc, argv);
    if (!HasFlag(argc, argv, "--no-pause")) {
        printf("\nPress Enter to exit...");
        std::string dummy;
        std::getline(std::cin, dummy);
    }
    return result;
}
