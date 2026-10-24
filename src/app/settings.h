#pragma once
// Persistent preferences as a line-oriented key=value file. Hand rolled
// rather than a library because the whole format is six keys.
#include <string>
#include <vector>

namespace davepdf {

inline constexpr size_t kMaxRecentFiles = 10;

struct Settings {
    int theme = 0;
    float scale = 1.0f;
    bool show_sidebar = true;
    int sidebar_tab = 0;  // 0 pages, 1 outline
    bool show_properties = false;
    std::vector<std::string> recent;  // most recent first, UTF-8 paths
};

std::string serialize_settings(const Settings& settings);
Settings parse_settings(const std::string& text);
// Moves (or inserts) path at the front and trims the list to kMaxRecentFiles.
void add_recent(std::vector<std::string>& recent, const std::string& path);
void remove_recent(std::vector<std::string>& recent, const std::string& path);

} // namespace davepdf
