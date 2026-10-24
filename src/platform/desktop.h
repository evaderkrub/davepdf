#pragma once
// The application loop and the services the drawing code calls back into.
#include "app/session.h"
#include <filesystem>
#include <string>
#include <vector>

namespace davepdf::platform {

int run(int argc, char** argv);
std::filesystem::path executable_directory();

// Actions are immediate: a viewer has no unsaved state to protect.
void request_action(Session& session, Action action, const std::string& path = {});

// A page rendered as a GPU texture. `texture` is an ImTextureID. When the
// exact size is not ready yet the nearest cached rendering of the same page
// comes back with exact == false, so the view can draw it stretched instead
// of flashing a blank page while the new one renders.
struct PageImage {
    unsigned long long texture = 0;
    int width = 0, height = 0;
    bool exact = false;
};
PageImage page_image(Session& session, int page, int width, int height, bool thumbnail = false);
bool page_image_pending();  // true while any requested size is still queued

const std::vector<LinkBox>& page_links(Session& session, int page);
void activate_link(Session& session, const LinkBox& link);
void copy_page_text(Session& session, int page);
// Text selection: character under a normalised page point (-1 if none),
// updating the selection's boxes, and copying it (or, with no selection,
// the whole current page) to the clipboard.
int text_index_at(Session& session, int page, float nx, float ny);
void set_selection(Session& session, int page, int anchor, int focus);
void copy_selection_or_page(Session& session);
void set_invert_pages(Session& session, bool invert);
void go_to_page(Session& session, int page);
void close_document(Session& session);

} // namespace davepdf::platform
