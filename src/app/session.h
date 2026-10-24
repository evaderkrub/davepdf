#pragma once
// Everything the interface reads and the platform layer updates. A plain
// struct: no handles, no textures, nothing that needs a window to exist.
#include "app/settings.h"
#include "app/viewer.h"
#include <string>
#include <vector>

namespace davepdf {

struct OutlineItem {
    std::string title;
    int page = -1;   // -1 when the bookmark has no page destination
    int depth = 0;
};

struct LinkBox {
    RectF rect;      // normalised to the unrotated page
    int page = -1;   // internal destination, or
    std::string uri; // external destination
};

struct SearchHit {
    int page = 0;
    std::vector<RectF> rects;  // normalised to the unrotated page
};

struct DocumentInfo {
    std::string path;       // empty for the in-memory sample
    std::string file_name;
    std::string title, author, subject, keywords, creator, producer, created, modified;
    std::string version;    // "1.7"
    unsigned long long bytes = 0;
    bool encrypted = false;
};

enum class Action { None, Open, OpenPath, OpenSample, Close, Quit };

// A one-shot request from the interface or platform to move the view.
struct ScrollRequest {
    bool pending = false;
    int page = 0;
    float fraction = 0;   // 0..1 down the page
    bool center = false;  // place the fraction mid-viewport rather than at the top
};

struct Session {
    ViewState view;
    DocumentInfo info;
    std::vector<OutlineItem> outline;
    bool has_document = false;
    std::string status;
    std::string error;    // non-empty opens the message modal

    // Find.
    bool show_find = false;
    bool focus_find = false;
    std::string find_text;
    bool find_match_case = false;
    bool find_dirty = false;         // text or options changed: restart the scan
    int find_progress = 0;           // pages scanned so far
    std::vector<SearchHit> hits;     // in scan order starting at the current page
    int current_hit = -1;
    bool find_step_requested = false;  // next/previous requested from the interface
    int find_step_direction = 1;

    ScrollRequest scroll;
    float scroll_y = 0;              // document pixels, kept for the status and tests
    float view_width = 0, view_height = 0;

    // Interface state.
    bool show_sidebar = true;
    int sidebar_tab = 0;          // 0 pages, 1 outline: the visible tab of the sidebar
    // After the dock layout is (re)built the remembered tab is focused for a
    // few frames, because newly docked windows settle over more than one.
    int sidebar_focus_tab = -1;
    int sidebar_focus_frames = 0;
    bool show_properties = false;
    bool show_about = false;
    bool show_goto = false;
    bool show_password = false;
    bool show_shortcuts = false;
    std::string password;
    int goto_input = 1;
    int theme = 0;
    float scale = 1.0f;
    float dpi = 1.0f;
    bool dock_initialized = false;
    bool reset_layout = false;
    bool quit = false;
    std::vector<std::string> recent;

    Action pending = Action::None;
    std::string pending_path;
    // Bumped whenever page images must be re-rendered (new document, rotation).
    unsigned render_generation = 0;

    void request_scroll(int page, float fraction = 0, bool center = false) {
        scroll = {true, page, fraction, center};
    }
};

} // namespace davepdf
