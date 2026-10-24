#include "platform/host.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <iterator>

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
} // namespace host
} // namespace davepdf::platform
