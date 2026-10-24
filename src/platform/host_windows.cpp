#include "platform/host.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

namespace davepdf::platform {

std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

namespace host {
namespace {
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}
bool writable(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const auto probe = directory / ".davepdf-write-test";
    { std::ofstream stream(probe); if (!stream) return false; }
    std::filesystem::remove(probe, ec);
    return true;
}
}

bool choose_pdf(SDL_Window* window, std::filesystem::path& result, std::string& error) {
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrDefExt = L"pdf";
    dialog.lpstrTitle = L"Open PDF document";
    dialog.lpstrFilter = L"PDF documents\0*.pdf\0All files\0*.*\0\0";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&dialog)) { result = buffer; return true; }
    if (CommDlgExtendedError() != 0) error = "Windows could not open the file dialog.";
    return false;
}

std::filesystem::path settings_directory() {
    const auto beside = executable_directory();
    if (writable(beside)) return beside;
    PWSTR local = nullptr;
    std::filesystem::path fallback;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) fallback = std::filesystem::path(local) / "davepdf";
    if (local) CoTaskMemFree(local);
    if (fallback.empty()) fallback = std::filesystem::temp_directory_path() / "davepdf";
    writable(fallback);
    return fallback;
}

void decorate_window(SDL_Window* window) {
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    for (const auto [kind, size] : {std::pair{ICON_SMALL, 32}, std::pair{ICON_BIG, 256}}) {
        const HANDLE icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_SHARED);
        if (icon) SendMessageW(hwnd, WM_SETICON, static_cast<WPARAM>(kind), reinterpret_cast<LPARAM>(icon));
    }
}

bool open_external(const std::string& uri, std::string& error) {
    const auto starts = [&](const char* prefix) { return _strnicmp(uri.c_str(), prefix, strlen(prefix)) == 0; };
    // A PDF can carry any URI at all; only the schemes a browser handles are
    // forwarded so a crafted file cannot launch arbitrary programs.
    if (!(starts("http://") || starts("https://") || starts("mailto:"))) { error = "Only web and mail links are opened: " + uri; return false; }
    const auto wide = widen(uri);
    const auto status = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (status <= 32) { error = "Windows could not open the link."; return false; }
    return true;
}

namespace {
bool set_string(const wchar_t* key, const wchar_t* value_name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key, value_name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
std::wstring exe_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return buffer;
}
} // namespace

bool register_pdf_handler(std::string& error) {
    // The layout Windows documents for a desktop app that offers file
    // associations: a ProgID under Classes, a Capabilities block, and an
    // entry in RegisteredApplications so Default Apps can list it. All under
    // HKEY_CURRENT_USER so a portable copy can do it without elevation.
    const std::wstring exe = exe_path();
    const std::wstring command = L"\"" + exe + L"\" \"%1\"";
    const std::wstring icon = L"\"" + exe + L"\",0";
    bool ok = true;
    ok &= set_string(L"Software\\Classes\\davepdf.Document", nullptr, L"PDF Document");
    ok &= set_string(L"Software\\Classes\\davepdf.Document", L"FriendlyTypeName", L"PDF Document");
    ok &= set_string(L"Software\\Classes\\davepdf.Document\\DefaultIcon", nullptr, icon);
    ok &= set_string(L"Software\\Classes\\davepdf.Document\\shell\\open", nullptr, L"Open with davepdf");
    ok &= set_string(L"Software\\Classes\\davepdf.Document\\shell\\open\\command", nullptr, command);
    ok &= set_string(L"Software\\Classes\\.pdf\\OpenWithProgids", L"davepdf.Document", L"");
    ok &= set_string(L"Software\\Classes\\Applications\\davepdf.exe", L"FriendlyAppName", L"davepdf");
    ok &= set_string(L"Software\\Classes\\Applications\\davepdf.exe\\shell\\open\\command", nullptr, command);
    ok &= set_string(L"Software\\Classes\\Applications\\davepdf.exe\\SupportedTypes", L".pdf", L"");
    ok &= set_string(L"Software\\davepdf\\Capabilities", L"ApplicationName", L"davepdf");
    ok &= set_string(L"Software\\davepdf\\Capabilities", L"ApplicationDescription", L"A fast, portable PDF viewer");
    ok &= set_string(L"Software\\davepdf\\Capabilities\\FileAssociations", L".pdf", L"davepdf.Document");
    ok &= set_string(L"Software\\RegisteredApplications", L"davepdf", L"Software\\davepdf\\Capabilities");
    if (!ok) { error = "Windows refused to write the registration keys for the current user."; return false; }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool pdf_handler_registered() {
    wchar_t buffer[4096]{};
    DWORD size = sizeof buffer;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\davepdf.Document\\shell\\open\\command", nullptr, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return false;
    std::wstring command(buffer), exe = exe_path();
    for (auto& c : command) c = static_cast<wchar_t>(towlower(c));
    for (auto& c : exe) c = static_cast<wchar_t>(towlower(c));
    return command.find(exe) != std::wstring::npos;
}

bool open_default_apps_settings(std::string& error) {
    // Windows 11 opens the app's own page; older builds fall back to the list.
    const auto status = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=davepdf", nullptr, nullptr, SW_SHOWNORMAL));
    if (status <= 32) { error = "Windows could not open the Default apps settings page."; return false; }
    return true;
}

bool reveal_in_folder(const std::string& utf8_path, std::string& error) {
    const std::wstring arguments = L"/select,\"" + widen(utf8_path) + L"\"";
    const auto status = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
    if (status <= 32) { error = "Windows could not open the folder."; return false; }
    return true;
}
} // namespace host
} // namespace davepdf::platform
