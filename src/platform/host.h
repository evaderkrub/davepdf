#pragma once
// Services that talk to the operating system directly. desktop.cpp owns the
// application loop and calls these; host_windows.cpp implements them.
#include <filesystem>
#include <string>

struct SDL_Window;

namespace davepdf::platform {
std::filesystem::path executable_directory();
}

namespace davepdf::platform::host {
// Native open dialog. Returns false without an error when the user cancels.
bool choose_pdf(SDL_Window* window, std::filesystem::path& result, std::string& error);
// Beside the executable when that folder is writable, otherwise the user's
// local application data, so a read-only install still remembers settings.
std::filesystem::path settings_directory();
void decorate_window(SDL_Window* window);
// Hands an http(s) or mailto link to the default browser. Other schemes are refused.
bool open_external(const std::string& uri, std::string& error);
// Opens the file's folder in the shell with the file selected.
bool reveal_in_folder(const std::string& utf8_path, std::string& error);
}
