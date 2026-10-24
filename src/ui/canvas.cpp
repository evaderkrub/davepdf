#include "ui/workspace.h"
#include "ui/fwTheme.h"
#include "platform/desktop.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace davepdf::ui {
namespace {
// Transient drawing state: what the canvas showed last frame, so a zoom or
// rotation change made anywhere (toolbar, menu, shortcut) can be anchored.
struct CanvasMemory {
    float last_zoom = -1;
    int last_rotation = 0;
    unsigned last_generation = ~0u;
    ImVec2 drag_origin;
    bool dragging = false;
};
CanvasMemory memory;

ImU32 canvas_background() {
    const fwThemeTokens& t = fwThemeAt(fwThemeCurrentIndex());
    // A touch away from the window colour in both directions so the pages
    // read as objects lying on a desk rather than holes in the chrome.
    return fwLift(t.background, t.bIsDark ? 0.045f : 0.09f, t.bIsDark);
}

void draw_page(Session& s, ImDrawList* dl, const PageRect& pr, ImVec2 origin, float us) {
    const auto& p = fwPaletteCurrent();
    const ImVec2 a(origin.x + pr.rect.x, origin.y + pr.rect.y);
    const ImVec2 b(a.x + pr.rect.w, a.y + pr.rect.h);
    // Soft shadow: three expanding translucent rectangles.
    for (int i = 3; i >= 1; --i) {
        const float spread = static_cast<float>(i) * 2.0f * us;
        dl->AddRectFilled(ImVec2(a.x - spread + 1, a.y - spread + 3 * us), ImVec2(b.x + spread - 1, b.y + spread + 2 * us), IM_COL32(0, 0, 0, 22), 4 * us);
    }
    const platform::PageImage image = platform::page_image(s, pr.page, static_cast<int>(pr.rect.w), static_cast<int>(pr.rect.h));
    if (image.texture) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(image.texture)), a, b);
    else dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
    dl->AddRect(a, b, fwWithAlpha(p.border, 160), 0, 0, 1.0f);

    // Search highlights.
    for (size_t i = 0; i < s.hits.size(); ++i) {
        const SearchHit& hit = s.hits[i];
        if (hit.page != pr.page) continue;
        const bool current = static_cast<int>(i) == s.current_hit;
        for (const RectF& nr : hit.rects) {
            const RectF r = rotate_normalized(nr, s.view.rotation);
            const ImVec2 ha(a.x + r.x * pr.rect.w - 1, a.y + r.y * pr.rect.h - 1);
            const ImVec2 hb(a.x + (r.x + r.w) * pr.rect.w + 1, a.y + (r.y + r.h) * pr.rect.h + 1);
            dl->AddRectFilled(ha, hb, fwWithAlpha(current ? p.accent : p.warning, current ? 120 : 80), 2 * us);
            if (current) dl->AddRect(ha, hb, p.accent, 2 * us, 0, 1.5f * us);
        }
    }
}
} // namespace

void draw_canvas(Session& s) {
    const float us = ui_scale(s);
    ImGuiIO& io = ImGui::GetIO();
    s.view.gap = std::round(14 * us);
    s.view.margin = std::round(24 * us);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, canvas_background());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * us);
    // The vertical scrollbar is always reserved: fit-to-width would otherwise
    // oscillate between "fits, no scrollbar" and "scrollbar, no longer fits".
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs;
    const bool open = ImGui::BeginChild("Canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) { ImGui::EndChild(); return; }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 view_size(window->InnerRect.GetWidth(), window->InnerRect.GetHeight());
    s.view_width = view_size.x;
    s.view_height = view_size.y;

    // 1. Zoom for this frame.
    if (s.view.fit != FitMode::Manual) s.view.zoom = fit_zoom(s.view, view_size.x, view_size.y, s.dpi, s.view.fit);
    float scroll_y = ImGui::GetScrollY();
    float scroll_x = ImGui::GetScrollX();
    const bool zoom_changed = memory.last_zoom > 0 && std::fabs(memory.last_zoom - s.view.zoom) > 1e-6f && memory.last_generation == s.render_generation;
    if (zoom_changed && !s.scroll.pending) {
        // Keep the document point at the viewport centre where it was.
        scroll_y = scroll_after_zoom(scroll_y, view_size.y / 2, memory.last_zoom, s.view.zoom);
        scroll_x = scroll_after_zoom(scroll_x, view_size.x / 2, memory.last_zoom, s.view.zoom);
    }
    memory.last_zoom = s.view.zoom;
    memory.last_rotation = s.view.rotation;
    memory.last_generation = s.render_generation;

    const Layout layout = layout_pages(s.view, s.dpi);
    const float content_w = std::max(layout.width, view_size.x);
    const float content_h = std::max(layout.height, view_size.y);
    const ImVec2 origin_local((content_w - layout.width) / 2, 0);  // centre narrow stacks

    // 2. Pending navigation.
    if (s.scroll.pending && s.view.valid_page(s.scroll.page)) {
        const RectF& r = layout.pages[static_cast<size_t>(s.scroll.page)].rect;
        scroll_y = r.y + s.scroll.fraction * r.h - (s.scroll.center ? view_size.y / 2 : s.view.gap);
        // The first page starts at the very top rather than a gap below it.
        if (s.scroll.page == 0 && !s.scroll.center && s.scroll.fraction <= 0) scroll_y = 0;
        s.scroll.pending = false;
    }

    // 3. Mouse wheel: plain scrolls, Shift scrolls sideways, Ctrl zooms around the cursor.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            const float old_zoom = s.view.zoom;
            const float new_zoom = clamp_zoom(old_zoom * std::pow(1.15f, io.MouseWheel));
            if (new_zoom != old_zoom) {
                const ImVec2 mouse_rel(io.MousePos.x - window->InnerRect.Min.x, io.MousePos.y - window->InnerRect.Min.y);
                s.view.fit = FitMode::Manual;
                s.view.zoom = new_zoom;
                scroll_y = scroll_after_zoom(scroll_y, mouse_rel.y, old_zoom, new_zoom);
                scroll_x = scroll_after_zoom(scroll_x, mouse_rel.x, old_zoom, new_zoom);
                memory.last_zoom = new_zoom;
            }
        } else if (io.KeyShift) {
            scroll_x -= io.MouseWheel * 90 * us;
        } else {
            scroll_y -= io.MouseWheel * 90 * us;
        }
    }
    if (hovered && io.MouseWheelH != 0.0f) scroll_x += io.MouseWheelH * 90 * us;

    // 4. Keyboard navigation while the canvas has the pointer or focus and no text field is active.
    const bool keys = (hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) && !io.WantTextInput;
    if (keys) {
        const float line = 60 * us, page_step = view_size.y * 0.9f;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) scroll_y += line;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) scroll_y -= line;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { if (layout.width > view_size.x) scroll_x += line; else platform::go_to_page(s, s.view.current_page + 1); }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { if (layout.width > view_size.x) scroll_x -= line; else platform::go_to_page(s, s.view.current_page - 1); }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown) || (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyShift)) scroll_y += page_step;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) || (ImGui::IsKeyPressed(ImGuiKey_Space) && io.KeyShift)) scroll_y -= page_step;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) scroll_y = io.KeyCtrl ? 0 : scroll_to_page(layout, s.view.current_page, s.view.gap);
        if (ImGui::IsKeyPressed(ImGuiKey_End)) scroll_y = content_h;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && s.show_find) close_find(s);
    }

    // 5. The content item: sized to the stack so the scrollbars are right, and
    //    the drag/click surface for panning and links.
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("Pages", ImVec2(content_w, content_h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool item_active = ImGui::IsItemActive();
    const bool item_hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) { memory.drag_origin = io.MousePos; memory.dragging = false; }
    if (item_active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 4.0f))) {
        memory.dragging = true;
        scroll_x -= io.MouseDelta.x;
        scroll_y -= io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    scroll_x = clamp_scroll(scroll_x, content_w, view_size.x);
    scroll_y = clamp_scroll(scroll_y, content_h, view_size.y);
    ImGui::SetScrollX(scroll_x);
    ImGui::SetScrollY(scroll_y);
    s.scroll_y = scroll_y;
    s.view.current_page = page_at(layout, scroll_y, view_size.y);

    // 6. Draw the visible pages. Screen origin of document (0,0):
    const ImVec2 origin(window->InnerRect.Min.x - scroll_x + origin_local.x, window->InnerRect.Min.y - scroll_y + origin_local.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const LinkBox* hovered_link = nullptr;
    PageRect hovered_page_rect;
    for (int page : visible_pages(layout, scroll_y, view_size.y, 1)) {
        const PageRect& pr = layout.pages[static_cast<size_t>(page)];
        draw_page(s, dl, pr, origin, us);
        if (!item_hovered) continue;
        // Link hit test on the page under the pointer.
        const ImVec2 a(origin.x + pr.rect.x, origin.y + pr.rect.y);
        if (io.MousePos.x < a.x || io.MousePos.y < a.y || io.MousePos.x > a.x + pr.rect.w || io.MousePos.y > a.y + pr.rect.h) continue;
        for (const LinkBox& link : platform::page_links(s, page)) {
            const RectF r = rotate_normalized(link.rect, s.view.rotation);
            const ImVec2 la(a.x + r.x * pr.rect.w, a.y + r.y * pr.rect.h);
            const ImVec2 lb(la.x + r.w * pr.rect.w, la.y + r.h * pr.rect.h);
            if (io.MousePos.x >= la.x && io.MousePos.x <= lb.x && io.MousePos.y >= la.y && io.MousePos.y <= lb.y) {
                hovered_link = &link;
                hovered_page_rect = pr;
                dl->AddRectFilled(la, lb, fwWithAlpha(fwPaletteCurrent().link, 40), 2 * us);
                dl->AddRect(la, lb, fwWithAlpha(fwPaletteCurrent().link, 160), 2 * us);
                break;
            }
        }
    }
    if (hovered_link && !memory.dragging) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered()) {
            const std::string tip = hovered_link->page >= 0 ? "Go to page " + std::to_string(hovered_link->page + 1) : hovered_link->uri;
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    if (ImGui::IsItemDeactivated() && !memory.dragging && hovered_link && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const LinkBox link = *hovered_link;  // copy: activating may invalidate the cache
        platform::activate_link(s, link);
    }
    if (ImGui::IsItemDeactivated()) memory.dragging = false;

    // Progress hint while large pages are still rendering.
    if (platform::page_image_pending()) {
        const ImVec2 pos(window->InnerRect.Max.x - 120 * us, window->InnerRect.Max.y - 30 * us);
        dl->AddRectFilled(pos, ImVec2(pos.x + 108 * us, pos.y + 22 * us), fwWithAlpha(fwPaletteCurrent().surface, 220), 6 * us);
        dl->AddText(ImVec2(pos.x + 10 * us, pos.y + 3 * us), fwPaletteCurrent().textMuted, "Rendering...");
    }
    ImGui::EndChild();
}

} // namespace davepdf::ui
