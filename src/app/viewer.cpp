#include "app/viewer.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace davepdf {
namespace {
constexpr float kPresets[] = {0.25f, 0.33f, 0.5f, 0.67f, 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f,
                              2.0f, 2.5f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f};
}

float clamp_zoom(float zoom) {
    if (!std::isfinite(zoom) || zoom <= 0) return 1.0f;
    return std::clamp(zoom, kMinZoom, kMaxZoom);
}

float zoom_step(float zoom, bool in) {
    // A small tolerance so a fit zoom of 0.9999 steps to 1.1 rather than 1.0.
    const float eps = 0.005f;
    if (in) {
        for (float preset : kPresets) if (preset > zoom + eps) return preset;
        return kMaxZoom;
    }
    for (int i = static_cast<int>(std::size(kPresets)) - 1; i >= 0; --i)
        if (kPresets[static_cast<size_t>(i)] < zoom - eps) return kPresets[static_cast<size_t>(i)];
    return kMinZoom;
}

PageInfo ViewState::rotated(int page) const {
    PageInfo info = pages[static_cast<size_t>(page)];
    if (rotation == 90 || rotation == 270) std::swap(info.width, info.height);
    return info;
}

void ViewState::rotate(int quarter_turns) {
    rotation = ((rotation + quarter_turns * 90) % 360 + 360) % 360;
}

Layout layout_pages(const ViewState& view, float pixel_scale) {
    Layout layout;
    const float k = kPointsToPixels * view.zoom * pixel_scale;
    float y = view.margin;
    float widest = 0;
    for (int i = 0; i < view.page_count(); ++i) {
        const PageInfo info = view.rotated(i);
        // Whole pixels: a page texture is rendered at exactly this size, so a
        // fractional size would be resampled and blur the text.
        const float w = std::max(1.0f, std::round(info.width * k));
        const float h = std::max(1.0f, std::round(info.height * k));
        layout.pages.push_back({i, {0, y, w, h}});
        widest = std::max(widest, w);
        y += h + view.gap;
    }
    layout.width = widest + 2 * view.margin;
    layout.height = view.page_count() ? y - view.gap + view.margin : 0;
    for (auto& page : layout.pages) page.rect.x = std::round((layout.width - page.rect.w) / 2);
    return layout;
}

float fit_zoom(const ViewState& view, float view_w, float view_h, float pixel_scale, FitMode mode) {
    if (mode == FitMode::Manual || view.page_count() == 0) return view.zoom;
    const float k = kPointsToPixels * pixel_scale;
    const float usable_w = std::max(32.0f, view_w - 2 * view.margin);
    if (mode == FitMode::Width) {
        float widest = 0;
        for (int i = 0; i < view.page_count(); ++i) widest = std::max(widest, view.rotated(i).width);
        return widest > 0 ? clamp_zoom(usable_w / (widest * k)) : 1.0f;
    }
    const int page = std::clamp(view.current_page, 0, view.page_count() - 1);
    const PageInfo info = view.rotated(page);
    if (info.width <= 0 || info.height <= 0) return 1.0f;
    const float usable_h = std::max(32.0f, view_h - 2 * view.margin);
    return clamp_zoom(std::min(usable_w / (info.width * k), usable_h / (info.height * k)));
}

int page_at(const Layout& layout, float scroll_y, float view_h) {
    if (layout.pages.empty()) return 0;
    const float top = scroll_y, bottom = scroll_y + view_h;
    int best = -1;
    float best_overlap = 0;
    for (const auto& page : layout.pages) {
        const float overlap = std::min(bottom, page.rect.y + page.rect.h) - std::max(top, page.rect.y);
        if (overlap > best_overlap) { best_overlap = overlap; best = page.page; }
    }
    if (best >= 0) return best;
    // Nothing overlaps (viewport sits in a gap): whichever edge is nearest.
    float best_distance = 1e30f;
    for (const auto& page : layout.pages) {
        const float distance = std::min(std::fabs(page.rect.y - bottom), std::fabs(page.rect.y + page.rect.h - top));
        if (distance < best_distance) { best_distance = distance; best = page.page; }
    }
    return best;
}

float scroll_to_page(const Layout& layout, int page, float gap) {
    if (page < 0 || page >= static_cast<int>(layout.pages.size())) return 0;
    return std::max(0.0f, layout.pages[static_cast<size_t>(page)].rect.y - gap);
}

std::vector<int> visible_pages(const Layout& layout, float scroll_y, float view_h, int overscan) {
    std::vector<int> result;
    const int count = static_cast<int>(layout.pages.size());
    int first = -1, last = -1;
    for (int i = 0; i < count; ++i) {
        const auto& r = layout.pages[static_cast<size_t>(i)].rect;
        if (r.y + r.h >= scroll_y && r.y <= scroll_y + view_h) { if (first < 0) first = i; last = i; }
    }
    if (first < 0) return result;
    for (int i = std::max(0, first - overscan); i <= std::min(count - 1, last + overscan); ++i) result.push_back(i);
    return result;
}

float clamp_scroll(float scroll, float content, float view) {
    const float max_scroll = std::max(0.0f, content - view);
    if (!std::isfinite(scroll)) return 0;
    return std::clamp(scroll, 0.0f, max_scroll);
}

float scroll_after_zoom(float scroll, float cursor_offset, float old_zoom, float new_zoom) {
    if (old_zoom <= 0) return scroll;
    const float ratio = new_zoom / old_zoom;
    return (scroll + cursor_offset) * ratio - cursor_offset;
}

RectF rotate_normalized(RectF r, int rotation) {
    switch (((rotation % 360) + 360) % 360) {
    case 90:  return {1 - (r.y + r.h), r.x, r.h, r.w};
    case 180: return {1 - (r.x + r.w), 1 - (r.y + r.h), r.w, r.h};
    case 270: return {r.y, 1 - (r.x + r.w), r.h, r.w};
    default:  return r;
    }
}

} // namespace davepdf
