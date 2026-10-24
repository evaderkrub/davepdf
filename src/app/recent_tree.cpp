#include "app/recent_tree.h"
#include <algorithm>

namespace davepdf {
namespace {

bool is_separator(char c) { return c == '\\' || c == '/'; }

std::vector<std::string> split_path(const std::string& path) {
    // "C:\a\b\f.pdf" -> ["C:", "a", "b", "f.pdf"]; "\\server\share\f" -> ["\\server", "share", "f"].
    std::vector<std::string> parts;
    std::string current;
    size_t i = 0;
    if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1])) { current = "\\\\"; i = 2; }
    for (; i < path.size(); ++i) {
        if (is_separator(path[i])) { if (!current.empty()) parts.push_back(current); current.clear(); }
        else current += path[i];
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

RecentNode& child_folder(RecentNode& parent, const std::string& label, const std::string& path) {
    for (auto& child : parent.children) if (!child.is_file && child.label == label) return child;
    RecentNode node;
    node.label = label;
    node.path = path;
    parent.children.push_back(std::move(node));
    return parent.children.back();
}

void compact(RecentNode& node) {
    for (auto& child : node.children) compact(child);
    // A folder holding exactly one folder and nothing else is merged with it.
    while (!node.is_file && node.children.size() == 1 && !node.children.front().is_file && !node.path.empty()) {
        RecentNode only = std::move(node.children.front());
        node.label += "\\" + only.label;
        node.path = only.path;
        node.children = std::move(only.children);
    }
    std::stable_sort(node.children.begin(), node.children.end(), [](const RecentNode& a, const RecentNode& b) {
        if (a.is_file != b.is_file) return !a.is_file;  // folders before files
        if (a.is_file) return a.recent_index < b.recent_index;
        return a.label < b.label;
    });
}

} // namespace

std::string recent_file_name(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string recent_directory(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

RecentNode build_recent_tree(const std::vector<std::string>& recent) {
    RecentNode root;
    for (size_t i = 0; i < recent.size(); ++i) {
        const auto parts = split_path(recent[i]);
        if (parts.empty()) continue;
        RecentNode* node = &root;
        std::string folder_path;
        for (size_t k = 0; k + 1 < parts.size(); ++k) {
            folder_path = folder_path.empty() ? parts[k] : folder_path + "\\" + parts[k];
            node = &child_folder(*node, parts[k], folder_path);
        }
        RecentNode file;
        file.label = parts.back();
        file.path = recent[i];
        file.is_file = true;
        file.recent_index = static_cast<int>(i);
        node->children.push_back(std::move(file));
    }
    compact(root);
    return root;
}

} // namespace davepdf
