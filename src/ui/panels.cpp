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
        const std::string label = item.title.empty() ? std::string("(untitled)") : item.title;
        const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
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
    if (!ImGui::Begin("Pages", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered()) s.sidebar_tab = 0;
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
            const platform::PageImage image = platform::page_image(s, page, static_cast<int>(thumb_w), static_cast<int>(thumb_h));
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
    if (!ImGui::Begin("Outline", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered()) s.sidebar_tab = 1;
    if (!s.has_document) empty_state(ICON_MD_TOC, "The document outline (bookmarks) appears here.");
    else if (s.outline.empty()) empty_state(ICON_MD_TOC, "This document has no outline.");
    else {
        size_t index = 0;
        outline_level(s, index, 0, us);
    }
    ImGui::End();
}

void draw_properties_panel(Session& s) {
    const float us = ui_scale(s);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220 * us, 120 * us), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Properties", &s.show_properties, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
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
