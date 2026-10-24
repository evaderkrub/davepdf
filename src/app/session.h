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
    int start = 0, count = 0;  // character range on the page
    std::vector<RectF> rects;  // normalised to the unrotated page
    // Context for the results list: the words before, the match itself as
    // it appears on the page, and the words after. Kept as three strings so
    // the match can be coloured without byte-offset arithmetic over UTF-8.
    std::string before, match, after;
};

// A run of characters on one page, chosen with the mouse.
struct TextSelection {
    int page = -1;
    int anchor = -1;            // character index where the drag started
    int focus = -1;             // character index under the pointer
    std::vector<RectF> rects;   // normalised to the unrotated page
    bool empty() const { return page < 0 || anchor < 0 || focus < 0; }
    int first() const { return anchor < focus ? anchor : focus; }
    int count() const { return (anchor < focus ? focus - anchor : anchor - focus) + 1; }
    void clear() { page = anchor = focus = -1; rects.clear(); }
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
    int find_select_hit = -1;          // a hit chosen from the results list
    bool show_results = false;         // the Search results panel
    bool focus_results = false;        // bring its tab to the front this frame

    TextSelection selection;
    bool selecting = false;          // a selection drag is in progress

    // Night reading: page pixels are inverted at upload, chrome is untouched.
    bool invert_pages = false;

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
