#include "ui/workspace.h"
#include "ui/fwTheme.h"
#include "platform/desktop.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "IconsMaterialDesign.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace davepdf::ui {
namespace {
int last_thumbnail_page = -1;

void empty_state(const char* icon, const char* text) {
    const auto& p = fwPaletteCurrent();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushFont(fonts().icons, 40.0f);
    const float icon_w = ImGui::CalcTextSize(icon).x;
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - icon_w) / 2, ImGui::GetCursorPosY() + std::max(0.0f, avail.y / 2 - 50)));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "%s", icon);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x);
    const float text_w = std::min(ImGui::CalcTextSize(text).x, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(std::max(ImGui::GetStyle().WindowPadding.x, (ImGui::GetWindowWidth() - text_w) / 2));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "%s", text);
    ImGui::PopTextWrapPos();
}

std::string human_size(unsigned long long bytes) {
    char buffer[64];
    if (bytes < 1024) std::snprintf(buffer, sizeof buffer, "%llu bytes", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buffer, sizeof buffer, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(buffer, sizeof buffer, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

void property_row(const char* label, const std::string& value) {
    if (value.empty()) return;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(fwPaletteCurrent().textMuted), "%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

// Draws outline items from `index` while they are deeper than `parent_depth`.
void outline_level(Session& s, size_t& index, int depth, float us) {
    const auto& p = fwPaletteCurrent();
    while (index < s.outline.size() && s.outline[index].depth == depth) {
        const size_t here = index;
        const OutlineItem& item = s.outline[here];
        const bool has_children = here + 1 < s.outline.size() && s.outline[here + 1].depth > depth;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (depth == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool current = item.page >= 0 && item.page == s.view.current_page;
        if (current) flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushID(static_cast<int>(here));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * us, 3 * us));
        // The title is the ID (inside a per-item PushID) so tests and the
        // navigation system can address bookmarks by name.
        const std::string label = item.title.empty() ? std::string("(untitled)") : item.title;
        const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
        ImGui::PopStyleVar();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && item.page >= 0) platform::go_to_page(s, item.page);
        if (ImGui::IsItemHovered() && item.page >= 0) ImGui::SetTooltip("Page %d", item.page + 1);
        if (item.page >= 0) {
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 34 * us));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "%d", item.page + 1);
        }
        ++index;
        if (has_children) {
            if (opened) { outline_level(s, index, depth + 1, us); ImGui::TreePop(); }
            else while (index < s.outline.size() && s.outline[index].depth > depth) ++index;
        }
        ImGui::PopID();
    }
    // Skip malformed deeper items that have no parent at this level.
    while (index < s.outline.size() && s.outline[index].depth > depth) ++index;
}
} // namespace

void draw_pages_panel(Session& s) {
    const float us = ui_scale(s);
    const auto& p = fwPaletteCurrent();
    ImGui::SetNextWindowSizeConstraints(ImVec2(140 * us, 100 * us), ImVec2(FLT_MAX, FLT_MAX));
    if (s.sidebar_focus_frames > 0 && s.sidebar_focus_tab == 0) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Pages", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar)) { ImGui::End(); return; }
    // Begin returns true only for the visible tab of a dock node, which is
    // exactly the "which sidebar tab is open" fact worth remembering.
    s.sidebar_tab = 0;
    if (!s.has_document) { empty_state(ICON_MD_PHOTO_LIBRARY, "Page thumbnails appear here once a document is open."); ImGui::End(); return; }

    const float pad = 10 * us;
    const float thumb_w = std::max(48.0f, ImGui::GetContentRegionAvail().x - 2 * pad - 4);
    const float label_h = ImGui::GetTextLineHeight() + 6 * us;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool jump = last_thumbnail_page != s.view.current_page;
    last_thumbnail_page = s.view.current_page;
    for (int page = 0; page < s.view.page_count(); ++page) {
        const PageInfo info = s.view.rotated(page);
        const float thumb_h = std::max(24.0f, std::round(thumb_w * info.height / std::max(1.0f, info.width)));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const ImVec2 size(thumb_w, thumb_h + label_h);
        const std::string id = "Page " + std::to_string(page + 1) + "###thumb" + std::to_string(page);
        const bool visible = ImGui::IsRectVisible(size);
        if (ImGui::InvisibleButton(id.c_str(), size)) platform::go_to_page(s, page);
        const bool current = page == s.view.current_page;
        if (current && jump && !visible) ImGui::SetScrollHereY(0.5f);
        if (visible) {
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (current || hovered) dl->AddRectFilled(ImVec2(top.x - 5 * us, top.y - 5 * us), ImVec2(top.x + thumb_w + 5 * us, top.y + thumb_h + label_h + 2 * us), current ? p.accentFill : p.surfaceHovered, 6 * us);
            const platform::PageImage image = platform::page_image(s, page, static_cast<int>(thumb_w), static_cast<int>(thumb_h), true);
            const ImVec2 a = top, b(top.x + thumb_w, top.y + thumb_h);
            dl->AddRectFilled(ImVec2(a.x + 2 * us, a.y + 3 * us), ImVec2(b.x + 2 * us, b.y + 3 * us), IM_COL32(0, 0, 0, 40), 2 * us);
            if (image.texture) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(image.texture)), a, b);
            else dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
            dl->AddRect(a, b, current ? p.accent : fwWithAlpha(p.border, 200), 0, 0, current ? 2.0f * us : 1.0f);
            const std::string number = std::to_string(page + 1);
            const float number_w = ImGui::CalcTextSize(number.c_str()).x;
            dl->AddText(ImVec2(top.x + (thumb_w - number_w) / 2, b.y + 4 * us), current ? p.text : p.textMuted, number.c_str());
        }
        ImGui::Dummy(ImVec2(0, 6 * us));
    }
    ImGui::End();
}

void draw_outline_panel(Session& s) {
    const float us = ui_scale(s);
    ImGui::SetNextWindowSizeConstraints(ImVec2(140 * us, 100 * us), ImVec2(FLT_MAX, FLT_MAX));
    if (s.sidebar_focus_frames > 0 && s.sidebar_focus_tab == 1) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Outline", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar)) { ImGui::End(); return; }
    s.sidebar_tab = 1;
    if (!s.has_document) empty_state(ICON_MD_TOC, "The document outline (bookmarks) appears here.");
    else if (s.outline.empty()) empty_state(ICON_MD_TOC, "This document has no outline.");
    else {
        size_t index = 0;
        outline_level(s, index, 0, us);
    }
    ImGui::End();
}

void draw_search_results_panel(Session& s) {
    const float us = ui_scale(s);
    const auto& p = fwPaletteCurrent();
    ImGui::SetNextWindowSizeConstraints(ImVec2(220 * us, 120 * us), ImVec2(FLT_MAX, FLT_MAX));
    if (s.focus_results) ImGui::SetNextWindowFocus();
    // Appearing while the user types must not take the keyboard away from the find box.
    if (!ImGui::Begin("Search results", &s.show_results, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing)) { ImGui::End(); return; }
    if (!s.has_document || s.find_text.empty()) {
        empty_state(ICON_MD_MANAGE_SEARCH, s.has_document ? "Type in the find bar (Ctrl+F) to list every match here." : "Open a document and search it to list matches here.");
        ImGui::End();
        return;
    }
    // Header: query and tally.
    ImGui::PushFont(fonts().bold, 0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.heading), "%s", s.find_text.c_str());
    ImGui::PopFont();
    int pages_with_hits = 0, last_page = -1;
    for (const auto& hit : s.hits) if (hit.page != last_page) { ++pages_with_hits; last_page = hit.page; }
    if (s.find_progress < s.view.page_count()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "%zu so far, scanning page %d of %d", s.hits.size(), s.find_progress, s.view.page_count());
    else if (s.hits.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.warning), "No matches");
    else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "%zu %s on %d %s", s.hits.size(), s.hits.size() == 1 ? "match" : "matches", pages_with_hits, pages_with_hits == 1 ? "page" : "pages");
    ImGui::Separator();

    static int last_current = -1;
    const bool follow = last_current != s.current_hit;
    last_current = s.current_hit;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float row_h = ImGui::GetTextLineHeight() * 2 + 10 * us;
    // Hits are stored in scan order (from the page being read, wrapping);
    // the list reads better in page order, so render through a sorted index.
    std::vector<size_t> order(s.hits.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return s.hits[a].page != s.hits[b].page ? s.hits[a].page < s.hits[b].page : s.hits[a].start < s.hits[b].start;
    });
    last_page = -1;
    for (size_t i : order) {
        const SearchHit& hit = s.hits[i];
        if (hit.page != last_page) {
            // Page group header. Hits are in scan order, so a page's hits are contiguous.
            last_page = hit.page;
            ImGui::Dummy(ImVec2(0, 2 * us));
            ImGui::PushFont(fonts().bold, 0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "PAGE %d", hit.page + 1);
            ImGui::PopFont();
        }
        const bool current = static_cast<int>(i) == s.current_hit;
        const std::string id = "###hit" + std::to_string(i);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable(id.c_str(), current, ImGuiSelectableFlags_None, ImVec2(0, row_h))) s.find_select_hit = static_cast<int>(i);
        if (current && follow && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
        if (!ImGui::IsItemVisible()) continue;
        // Accent bar on the current hit, then the snippet with the match coloured.
        if (current) dl->AddRectFilled(ImVec2(top.x, top.y + 3 * us), ImVec2(top.x + 3 * us, top.y + row_h - 3 * us), p.accent, 2 * us);
        const float x0 = top.x + 10 * us, y0 = top.y + 5 * us;
        const float max_x = top.x + width - 4 * us;
        ImVec4 clip(top.x, top.y, max_x, top.y + row_h);
        const char* ellipsis = "\xE2\x80\xA6";
        // Lead-in is right-aligned to the match so the match always shows.
        const float match_w = ImGui::CalcTextSize(hit.match.c_str()).x;
        const float before_w = ImGui::CalcTextSize(hit.before.c_str()).x;
        const float lead_room = std::max(0.0f, std::min(before_w, (max_x - x0) * 0.4f));
        // Walk the lead-in from its end to fit lead_room.
        const char* lead_begin = hit.before.c_str() + hit.before.size();
        while (lead_begin > hit.before.c_str() && ImGui::CalcTextSize(lead_begin - 1).x <= lead_room) --lead_begin;
        while (lead_begin < hit.before.c_str() + hit.before.size() && (static_cast<unsigned char>(*lead_begin) & 0xC0) == 0x80) ++lead_begin;  // UTF-8 boundary
        float x = x0;
        if (lead_begin > hit.before.c_str()) { dl->AddText(nullptr, 0, ImVec2(x, y0), p.textFaint, ellipsis, nullptr, 0, &clip); x += ImGui::CalcTextSize(ellipsis).x; }
        dl->AddText(nullptr, 0, ImVec2(x, y0), p.textMuted, lead_begin, nullptr, 0, &clip);
        x += ImGui::CalcTextSize(lead_begin).x;
        ImGui::PushFont(fonts().bold, 0);
        dl->AddText(nullptr, 0, ImVec2(x, y0), p.accent, hit.match.c_str(), nullptr, 0, &clip);
        ImGui::PopFont();
        x += match_w;
        dl->AddText(nullptr, 0, ImVec2(x, y0), p.textMuted, hit.after.c_str(), nullptr, 0, &clip);
        // Second line: position on the page.
        char where[64];
        const RectF r = hit.rects.empty() ? RectF{} : hit.rects.front();
        std::snprintf(where, sizeof where, "%d%% down the page", static_cast<int>(r.y * 100));
        dl->AddText(ImVec2(x0, y0 + ImGui::GetTextLineHeight() + 2 * us), p.textFaint, where);
    }
    ImGui::End();
}

void draw_properties_panel(Session& s) {
    const float us = ui_scale(s);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220 * us, 120 * us), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Properties", &s.show_properties, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar)) { ImGui::End(); return; }
    if (!s.has_document) { empty_state(ICON_MD_INFO_OUTLINE, "Document properties appear here."); ImGui::End(); return; }
    const auto& p = fwPaletteCurrent();
    const DocumentInfo& d = s.info;
    ImGui::PushFont(fonts().bold, 0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.heading), "%s", d.file_name.c_str());
    ImGui::PopFont();
    if (!d.path.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "%s", d.path.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::Spacing();
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("file", 2, flags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 86 * us);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        property_row("Pages", std::to_string(s.view.page_count()));
        property_row("Size", human_size(d.bytes));
        property_row("PDF version", d.version);
        property_row("Encrypted", d.encrypted ? "Yes" : "No");
        const PageInfo page = s.view.pages.empty() ? PageInfo{} : s.view.pages[static_cast<size_t>(s.view.current_page)];
        char dims[128];
        std::snprintf(dims, sizeof dims, "%.0f x %.0f pt\n%.1f x %.1f mm\n%.2f x %.2f in", static_cast<double>(page.width), static_cast<double>(page.height),
                      page.width * 25.4 / 72.0, page.height * 25.4 / 72.0, page.width / 72.0, page.height / 72.0);
        property_row("Current page", dims);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::PushFont(fonts().bold, 0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "METADATA");
    ImGui::PopFont();
    ImGui::Separator();
    const bool any = !(d.title.empty() && d.author.empty() && d.subject.empty() && d.keywords.empty() && d.creator.empty() && d.producer.empty() && d.created.empty() && d.modified.empty());
    if (!any) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "The document carries no metadata.");
    else if (ImGui::BeginTable("meta", 2, flags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 86 * us);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        property_row("Title", d.title);
        property_row("Author", d.author);
        property_row("Subject", d.subject);
        property_row("Keywords", d.keywords);
        property_row("Creator", d.creator);
        property_row("Producer", d.producer);
        property_row("Created", d.created);
        property_row("Modified", d.modified);
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace davepdf::ui
