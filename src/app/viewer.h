#pragma once
// View geometry for a continuously scrolled stack of pages. Everything here is
// plain arithmetic over plain structs so it can be unit tested without a
// window; the drawing code reads the results and never owns them.
#include <vector>

namespace davepdf {

struct RectF { float x = 0, y = 0, w = 0, h = 0; };
struct PageInfo { float width = 0, height = 0; }; // PDF points (1/72 in)

enum class FitMode { Manual, Width, Page };

// 100% shows a page at its physical size on a 96 dpi display, which is what
// every mainstream viewer calls "actual size".
inline constexpr float kPointsToPixels = 96.0f / 72.0f;
inline constexpr float kMinZoom = 0.1f;
inline constexpr float kMaxZoom = 16.0f;

float clamp_zoom(float zoom);
// Next preset above (in) or below (out) the current zoom.
float zoom_step(float zoom, bool in);

struct ViewState {
    std::vector<PageInfo> pages;
    float zoom = 1.0f;
    FitMode fit = FitMode::Width;
    int rotation = 0;      // 0, 90, 180, 270 clockwise, applied to every page
    int current_page = 0;  // zero based
    // Space around and between pages, already in pixels.
    float gap = 14.0f;
    float margin = 24.0f;

    int page_count() const { return static_cast<int>(pages.size()); }
    bool valid_page(int page) const { return page >= 0 && page < page_count(); }
    PageInfo rotated(int page) const;
    void rotate(int quarter_turns);
};

struct PageRect { int page = 0; RectF rect; };
// Document space: pixels, origin at the top left of the whole scrolled stack.
struct Layout {
    std::vector<PageRect> pages;
    float width = 0, height = 0;
};

Layout layout_pages(const ViewState& view, float pixel_scale);
// Zoom that fits the widest page (Width) or the current page (Page) in a
// viewport of the given size. Manual returns the current zoom unchanged.
float fit_zoom(const ViewState& view, float view_w, float view_h, float pixel_scale, FitMode mode);
// The page a reader would call "current": the one covering most of the viewport.
int page_at(const Layout& layout, float scroll_y, float view_h);
float scroll_to_page(const Layout& layout, int page, float gap);
std::vector<int> visible_pages(const Layout& layout, float scroll_y, float view_h, int overscan);
float clamp_scroll(float scroll, float content, float view);
// Zooming around a point keeps the document position under the cursor fixed.
float scroll_after_zoom(float scroll, float cursor_offset, float old_zoom, float new_zoom);

// Text and link boxes come from the engine normalised to the unrotated page
// (0..1, top-left origin). Rotating the view rotates them the same way.
RectF rotate_normalized(RectF r, int rotation);

} // namespace davepdf
