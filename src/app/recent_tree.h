#pragma once
// Turns the flat recent-files list into a folder tree for the Recent panel.
// Pure string work on UTF-8 paths, so it is testable without a file system.
#include <string>
#include <vector>

namespace davepdf {

struct RecentNode {
    std::string label;             // folder segment(s) or file name
    std::string path;              // full path of the file, or of the folder
    bool is_file = false;
    int recent_index = -1;         // position in Session::recent for files
    std::vector<RecentNode> children;
};

// Root node whose children are drives or top folders. Folders with a single
// child folder and no files are merged into one node ("C:\Users\dave\Docs"),
// the way IDEs compact empty package chains. Files keep the order of the
// recent list (most recent first); folders are sorted by label.
RecentNode build_recent_tree(const std::vector<std::string>& recent);

// Path helpers shared with the interface.
std::string recent_file_name(const std::string& path);
std::string recent_directory(const std::string& path);

} // namespace davepdf
