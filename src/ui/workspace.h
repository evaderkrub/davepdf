#pragma once
// Everything that draws. These functions read the Session and call back into
// the platform layer for anything that needs the engine or the OS.
#include "app/session.h"

struct ImFont;

namespace davepdf::ui {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* italic = nullptr;
    ImFont* icons = nullptr;  // standalone Material Icons face for large glyphs
};
void set_fonts(Fonts fonts);
const Fonts& fonts();

void draw_workspace(Session& session);

// Pieces, each in its own file.
void draw_canvas(Session& session);            // canvas.cpp
void draw_pages_panel(Session& session);       // panels.cpp
void draw_outline_panel(Session& session);
void draw_properties_panel(Session& session);
void draw_search_results_panel(Session& session);
void draw_dialogs(Session& session);           // dialogs.cpp

// Shared helpers (workspace.cpp).
float ui_scale(const Session& session);        // GUI scale times monitor scale
bool icon_button(const char* icon, const char* label, bool enabled = true, bool active = false);
void toolbar_divider();
void zoom_to(Session& session, float zoom);    // switches to manual zoom
void zoom_step_view(Session& session, bool in);
void set_fit(Session& session, FitMode mode);
void rotate_view(Session& session, int quarter_turns);
void open_find(Session& session);
void close_find(Session& session);
void find_step(Session& session, int direction);

} // namespace davepdf::ui
