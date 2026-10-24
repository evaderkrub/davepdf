#include "app/settings.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace davepdf {

std::string serialize_settings(const Settings& s) {
    std::ostringstream out;
    out << "theme=" << s.theme << '\n';
    char scale[32];
    std::snprintf(scale, sizeof scale, "%.3f", static_cast<double>(s.scale));
    out << "scale=" << scale << '\n';
    out << "sidebar=" << (s.show_sidebar ? 1 : 0) << '\n';
    out << "sidebar_tab=" << s.sidebar_tab << '\n';
    out << "properties=" << (s.show_properties ? 1 : 0) << '\n';
    out << "invert=" << (s.invert_pages ? 1 : 0) << '\n';
    out << "fit=" << s.fit << '\n';
    for (const auto& path : s.recent) out << "recent=" << path << '\n';
    return out.str();
}

Settings parse_settings(const std::string& text) {
    Settings s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "theme") s.theme = std::atoi(value.c_str());
        else if (key == "scale") { const float f = std::strtof(value.c_str(), nullptr); if (f > 0) s.scale = f; }
        else if (key == "sidebar") s.show_sidebar = value != "0";
        else if (key == "sidebar_tab") s.sidebar_tab = std::atoi(value.c_str());
        else if (key == "properties") s.show_properties = value != "0";
        else if (key == "invert") s.invert_pages = value != "0";
        else if (key == "fit") { const int f = std::atoi(value.c_str()); if (f >= 0 && f <= 2) s.fit = f; }
        else if (key == "recent" && !value.empty() && s.recent.size() < kMaxRecentFiles) s.recent.push_back(value);
    }
    return s;
}

void add_recent(std::vector<std::string>& recent, const std::string& path) {
    if (path.empty()) return;
    remove_recent(recent, path);
    recent.insert(recent.begin(), path);
    if (recent.size() > kMaxRecentFiles) recent.resize(kMaxRecentFiles);
}

void remove_recent(std::vector<std::string>& recent, const std::string& path) {
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
}

} // namespace davepdf
