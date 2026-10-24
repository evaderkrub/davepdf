#include "ui/workspace.h"
#include "ui/fwTheme.h"
#include "platform/desktop.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "IconsMaterialDesign.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace davepdf::ui {
namespace {
Fonts loaded_fonts;

std::string file_name_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
std::string directory_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// Icon-prefixed menu labels keep a plain-text ID (after ###) so shortcuts,
// tests and the ini file address them by their visible name.
std::string menu_label(const char* icon, const char* text) {
    return std::string(icon) + "  " + text + "###" + text;
}

std::string zoom_label(const Session& s) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%d%%", static_cast<int>(std::lround(s.view.zoom * 100)));
    return buffer;
}

// ---------------------------------------------------------------- menu bar
void menu(Session& s) {
    if (!ImGui::BeginMenuBar()) return;
    const auto& p = fwPaletteCurrent();
    ImGui::PushFont(fonts().bold, 0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.accent), ICON_MD_PICTURE_AS_PDF " davepdf");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(6 * ui_scale(s), 0));
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(menu_label(ICON_MD_FOLDER_OPEN, "Open...").c_str(), "Ctrl+O")) platform::request_action(s, Action::Open);
        if (ImGui::BeginMenu("Open recent", !s.recent.empty())) {
            for (size_t i = 0; i < s.recent.size(); ++i) {
                const std::string label = file_name_of(s.recent[i]) + "###recent" + std::to_string(i);
                if (ImGui::MenuItem(label.c_str())) platform::request_action(s, Action::OpenPath, s.recent[i]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.recent[i].c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) s.recent.clear();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(menu_label(ICON_MD_AUTO_STORIES, "Open sample document").c_str())) platform::request_action(s, Action::OpenSample);
        ImGui::Separator();
        if (ImGui::MenuItem("Close", "Ctrl+W", false, s.has_document)) platform::request_action(s, Action::Close);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) platform::request_action(s, Action::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(menu_label(ICON_MD_CONTENT_COPY, "Copy").c_str(), "Ctrl+C", false, s.has_document)) platform::copy_selection_or_page(s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies the selected text, or the whole page when nothing is selected.");
        if (ImGui::MenuItem("Copy page text", nullptr, false, s.has_document)) platform::copy_page_text(s, s.view.current_page);
        if (ImGui::MenuItem("Clear selection", "Escape", false, !s.selection.empty())) s.selection.clear();
        ImGui::Separator();
        if (ImGui::MenuItem(menu_label(ICON_MD_SEARCH, "Find...").c_str(), "Ctrl+F", false, s.has_document)) open_find(s);
        if (ImGui::MenuItem("Find next", "F3", false, !s.hits.empty())) find_step(s, 1);
        if (ImGui::MenuItem("Find previous", "Shift+F3", false, !s.hits.empty())) find_step(s, -1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(menu_label(ICON_MD_VERTICAL_SPLIT, "Sidebar").c_str(), "Ctrl+B", &s.show_sidebar);
        ImGui::MenuItem(menu_label(ICON_MD_INFO_OUTLINE, "Properties").c_str(), "Ctrl+I", &s.show_properties);
        ImGui::Separator();
        if (ImGui::MenuItem(menu_label(ICON_MD_ZOOM_IN, "Zoom in").c_str(), "Ctrl++", false, s.has_document)) zoom_step_view(s, true);
        if (ImGui::MenuItem(menu_label(ICON_MD_ZOOM_OUT, "Zoom out").c_str(), "Ctrl+-", false, s.has_document)) zoom_step_view(s, false);
        if (ImGui::BeginMenu("Zoom", s.has_document)) {
            for (int percent : {50, 75, 100, 125, 150, 200, 300, 400}) {
                const std::string label = std::to_string(percent) + "%";
                if (ImGui::MenuItem(label.c_str(), percent == 100 ? "Ctrl+1" : nullptr, s.view.fit == FitMode::Manual && std::abs(s.view.zoom * 100 - static_cast<float>(percent)) < 0.5f))
                    zoom_to(s, static_cast<float>(percent) / 100.0f);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(menu_label(ICON_MD_SWAP_HORIZ, "Fit width").c_str(), "Ctrl+2", s.view.fit == FitMode::Width, s.has_document)) set_fit(s, FitMode::Width);
        if (ImGui::MenuItem(menu_label(ICON_MD_FIT_SCREEN, "Fit page").c_str(), "Ctrl+0", s.view.fit == FitMode::Page, s.has_document)) set_fit(s, FitMode::Page);
        ImGui::Separator();
        if (ImGui::MenuItem(menu_label(ICON_MD_ROTATE_RIGHT, "Rotate clockwise").c_str(), "Ctrl+R", false, s.has_document)) rotate_view(s, 1);
        if (ImGui::MenuItem(menu_label(ICON_MD_ROTATE_LEFT, "Rotate counter-clockwise").c_str(), "Ctrl+Shift+R", false, s.has_document)) rotate_view(s, -1);
        ImGui::Separator();
        if (ImGui::MenuItem(menu_label(ICON_MD_DARK_MODE, "Invert page colours").c_str(), "Ctrl+Shift+N", s.invert_pages)) platform::set_invert_pages(s, !s.invert_pages);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Night reading: white pages become dark, the interface keeps its theme.");
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            for (int i = 0; i < fwThemeCount(); ++i) if (ImGui::MenuItem(fwThemeAt(i).szName, nullptr, s.theme == i)) { s.theme = i; fwRequestTheme(i); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("GUI scale")) {
            for (int percent : {75, 100, 125, 150, 175, 200}) {
                const std::string label = std::to_string(percent) + "%";
                if (ImGui::MenuItem(label.c_str(), nullptr, std::abs(s.scale * 100 - static_cast<float>(percent)) < 1)) s.scale = static_cast<float>(percent) / 100;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset layout")) { s.dock_initialized = false; s.reset_layout = true; s.show_sidebar = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Go")) {
        if (ImGui::MenuItem(menu_label(ICON_MD_FIRST_PAGE, "First page").c_str(), "Home", false, s.has_document)) platform::go_to_page(s, 0);
        if (ImGui::MenuItem(menu_label(ICON_MD_NAVIGATE_BEFORE, "Previous page").c_str(), "Page Up", false, s.has_document)) platform::go_to_page(s, s.view.current_page - 1);
        if (ImGui::MenuItem(menu_label(ICON_MD_NAVIGATE_NEXT, "Next page").c_str(), "Page Down", false, s.has_document)) platform::go_to_page(s, s.view.current_page + 1);
        if (ImGui::MenuItem(menu_label(ICON_MD_LAST_PAGE, "Last page").c_str(), "End", false, s.has_document)) platform::go_to_page(s, s.view.page_count() - 1);
        ImGui::Separator();
        if (ImGui::MenuItem("Go to page...", "Ctrl+G", false, s.has_document)) { s.goto_input = s.view.current_page + 1; s.show_goto = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(menu_label(ICON_MD_KEYBOARD, "Keyboard shortcuts").c_str(), "F1")) s.show_shortcuts = true;
        if (ImGui::MenuItem("About davepdf")) s.show_about = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// ----------------------------------------------------------------- toolbar
void toolbar(Session& s) {
    const float us = ui_scale(s);
    const bool doc = s.has_document;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * us, 4 * us));
    if (icon_button(ICON_MD_FOLDER_OPEN, "Open")) platform::request_action(s, Action::Open);
    ImGui::SameLine();
    if (icon_button(ICON_MD_VERTICAL_SPLIT, "Toggle sidebar", true, s.show_sidebar)) s.show_sidebar = !s.show_sidebar;
    toolbar_divider();
    if (icon_button(ICON_MD_FIRST_PAGE, "First page", doc && s.view.current_page > 0)) platform::go_to_page(s, 0);
    ImGui::SameLine();
    if (icon_button(ICON_MD_NAVIGATE_BEFORE, "Previous page", doc && s.view.current_page > 0)) platform::go_to_page(s, s.view.current_page - 1);
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(!doc);
        ImGui::SetNextItemWidth(56 * us);
        int page = doc ? s.view.current_page + 1 : 0;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * us, ImGui::GetStyle().FramePadding.y));
        if (ImGui::InputInt("###PageInput", &page, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) platform::go_to_page(s, page - 1);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Page number (Enter to jump)");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("/ %d", s.view.page_count());
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (icon_button(ICON_MD_NAVIGATE_NEXT, "Next page", doc && s.view.current_page + 1 < s.view.page_count())) platform::go_to_page(s, s.view.current_page + 1);
    ImGui::SameLine();
    if (icon_button(ICON_MD_LAST_PAGE, "Last page", doc && s.view.current_page + 1 < s.view.page_count())) platform::go_to_page(s, s.view.page_count() - 1);
    toolbar_divider();
    if (icon_button(ICON_MD_ZOOM_OUT, "Zoom out", doc && s.view.zoom > kMinZoom)) zoom_step_view(s, false);
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(!doc);
        ImGui::SetNextItemWidth(78 * us);
        const std::string label = zoom_label(s);
        if (ImGui::BeginCombo("###Zoom", label.c_str(), ImGuiComboFlags_NoArrowButton)) {
            if (ImGui::Selectable("Fit width", s.view.fit == FitMode::Width)) set_fit(s, FitMode::Width);
            if (ImGui::Selectable("Fit page", s.view.fit == FitMode::Page)) set_fit(s, FitMode::Page);
            ImGui::Separator();
            for (int percent : {50, 75, 100, 125, 150, 200, 300, 400}) {
                const std::string item = std::to_string(percent) + "%";
                if (ImGui::Selectable(item.c_str(), s.view.fit == FitMode::Manual && std::abs(s.view.zoom * 100 - static_cast<float>(percent)) < 0.5f)) zoom_to(s, static_cast<float>(percent) / 100.0f);
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom (Ctrl+wheel over the page)");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (icon_button(ICON_MD_ZOOM_IN, "Zoom in", doc && s.view.zoom < kMaxZoom)) zoom_step_view(s, true);
    ImGui::SameLine();
    if (icon_button(ICON_MD_SWAP_HORIZ, "Fit width", doc, doc && s.view.fit == FitMode::Width)) set_fit(s, FitMode::Width);
    ImGui::SameLine();
    if (icon_button(ICON_MD_FIT_SCREEN, "Fit page", doc, doc && s.view.fit == FitMode::Page)) set_fit(s, FitMode::Page);
    toolbar_divider();
    if (icon_button(ICON_MD_ROTATE_LEFT, "Rotate left", doc)) rotate_view(s, -1);
    ImGui::SameLine();
    if (icon_button(ICON_MD_ROTATE_RIGHT, "Rotate right", doc)) rotate_view(s, 1);
    toolbar_divider();
    if (icon_button(ICON_MD_SEARCH, "Find", doc, s.show_find)) { if (s.show_find) close_find(s); else open_find(s); }
    // Right-aligned group.
    const float right_group = ImGui::GetFrameHeight() * 3 + 8 * us;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - right_group));
    if (icon_button(ICON_MD_CONTENT_COPY, "Copy", doc)) platform::copy_selection_or_page(s);
    ImGui::SameLine();
    if (icon_button(ICON_MD_DARK_MODE, "Invert page colours", doc, s.invert_pages)) platform::set_invert_pages(s, !s.invert_pages);
    ImGui::SameLine();
    if (icon_button(ICON_MD_INFO_OUTLINE, "Properties", true, s.show_properties)) s.show_properties = !s.show_properties;
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------- find bar
void find_bar(Session& s) {
    if (!s.show_find) return;
    const float us = ui_scale(s);
    const auto& p = fwPaletteCurrent();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * us);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * us, 6 * us));
    const float height = ImGui::GetFrameHeight() + 12 * us;
    if (ImGui::BeginChild("FindBar", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), ICON_MD_SEARCH);
        ImGui::SameLine();
        if (s.focus_find) { ImGui::SetKeyboardFocusHere(); s.focus_find = false; }
        ImGui::SetNextItemWidth(std::max(160 * us, ImGui::GetContentRegionAvail().x - 300 * us));
        const bool entered = ImGui::InputTextWithHint("###FindText", "Find in document", &s.find_text, ImGuiInputTextFlags_EnterReturnsTrue);
        const ImGuiID field = ImGui::GetItemID();
        if (ImGui::IsItemEdited()) s.find_dirty = true;
        // Enter steps to the next hit, Shift+Enter to the previous. The field
        // owns the Enter key and validates with key repeat, so a slow frame
        // (pages rendering) could turn one press into two; asking as the owner
        // without repeat counts each press once. ConfigInputTextEnterKeepActive
        // keeps the field active so Enter can be pressed again.
        const bool pressed_once = ImGui::IsKeyPressed(ImGuiKey_Enter, ImGuiInputFlags_None, field) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, ImGuiInputFlags_None, field);
        if (entered && pressed_once) find_step(s, ImGui::GetIO().KeyShift ? -1 : 1);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (s.find_text.empty()) ImGui::TextDisabled("Type to search");
        else if (s.find_progress < s.view.page_count()) ImGui::TextDisabled("%zu found, scanning %d/%d", s.hits.size(), s.find_progress, s.view.page_count());
        else if (s.hits.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.warning), "No matches");
        else ImGui::TextDisabled("%d of %zu", s.current_hit + 1, s.hits.size());
        ImGui::SameLine();
        if (icon_button(ICON_MD_KEYBOARD_ARROW_UP, "Previous match", !s.hits.empty())) find_step(s, -1);
        ImGui::SameLine();
        if (icon_button(ICON_MD_KEYBOARD_ARROW_DOWN, "Next match", !s.hits.empty())) find_step(s, 1);
        ImGui::SameLine();
        if (icon_button(ICON_MD_FORMAT_SIZE, "Match case", true, s.find_match_case)) { s.find_match_case = !s.find_match_case; s.find_dirty = true; }
        ImGui::SameLine();
        if (icon_button(ICON_MD_CLOSE, "Close find")) close_find(s);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// -------------------------------------------------------------- status bar
void status_bar(Session& s) {
    const auto& p = fwPaletteCurrent();
    const float us = ui_scale(s);
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, p.textMuted);
    ImGui::Dummy(ImVec2(0, 1 * us));
    ImGui::Indent(4 * us);
    if (s.has_document) {
        ImGui::Text("Page %d of %d", s.view.current_page + 1, s.view.page_count());
        ImGui::SameLine(0, 18 * us);
        const char* fit = s.view.fit == FitMode::Width ? "Fit width" : s.view.fit == FitMode::Page ? "Fit page" : "Custom";
        ImGui::Text("%s  %s", zoom_label(s).c_str(), fit);
        if (s.view.rotation) { ImGui::SameLine(0, 18 * us); ImGui::Text(ICON_MD_ROTATE_RIGHT " %d" "\xC2\xB0", s.view.rotation); }
        if (!s.selection.empty()) { ImGui::SameLine(0, 18 * us); ImGui::Text("%d selected", s.selection.count()); }
        ImGui::SameLine(0, 18 * us);
        ImGui::TextUnformatted(s.info.file_name.c_str());
    } else {
        ImGui::TextUnformatted("No document open");
    }
    if (!s.status.empty()) {
        const float width = ImGui::CalcTextSize(s.status.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 24 * us, ImGui::GetWindowContentRegionMax().x - width - 8 * us));
        ImGui::PushStyleColor(ImGuiCol_Text, p.textFaint);
        ImGui::TextUnformatted(s.status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Unindent(4 * us);
    ImGui::PopStyleColor();
}

// ------------------------------------------------------------ welcome card
void welcome(Session& s) {
    const float us = ui_scale(s);
    const auto& p = fwPaletteCurrent();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float card_w = std::min(avail.x - 40 * us, 520 * us);
    const float card_h = std::min(avail.y - 40 * us, (s.recent.empty() ? 292.0f : 292.0f + std::min<float>(static_cast<float>(s.recent.size()), 5.0f) * 34.0f + 40.0f) * us);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - card_w) / 2), ImGui::GetCursorPosY() + std::max(0.0f, (avail.y - card_h) / 2)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * us);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * us, 28 * us));
    if (ImGui::BeginChild("Welcome", ImVec2(card_w, card_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        const float inner = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(fonts().icons, 64 * us);
        const float icon_w = ImGui::CalcTextSize(ICON_MD_PICTURE_AS_PDF).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inner - icon_w) / 2);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.accent), ICON_MD_PICTURE_AS_PDF);
        ImGui::PopFont();
        ImGui::PushFont(fonts().bold, 30 * us);
        const float title_w = ImGui::CalcTextSize("davepdf").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inner - title_w) / 2);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.heading), "davepdf");
        ImGui::PopFont();
        const char* tagline = "A fast, portable PDF viewer";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inner - ImGui::CalcTextSize(tagline).x) / 2);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "%s", tagline);
        ImGui::Dummy(ImVec2(0, 14 * us));
        const float button_w = 190 * us;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inner - 2 * button_w - 10 * us) / 2);
        ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, p.accentText);
        if (ImGui::Button(ICON_MD_FOLDER_OPEN "  Open PDF...", ImVec2(button_w, 0))) platform::request_action(s, Action::Open);
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0, 10 * us);
        if (ImGui::Button(ICON_MD_AUTO_STORIES "  Open sample", ImVec2(button_w, 0))) platform::request_action(s, Action::OpenSample);
        if (!s.recent.empty()) {
            ImGui::Dummy(ImVec2(0, 14 * us));
            ImGui::PushStyleColor(ImGuiCol_Text, p.textFaint);
            ImGui::PushFont(fonts().bold, 0);
            ImGui::TextUnformatted("RECENT");
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::Separator();
            for (size_t i = 0; i < s.recent.size() && i < 5; ++i) {
                const std::string name = file_name_of(s.recent[i]);
                const std::string id = "###welcome_recent" + std::to_string(i);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
                if (ImGui::Selectable((ICON_MD_DESCRIPTION "  " + name + id).c_str(), false, 0, ImVec2(0, 30 * us))) platform::request_action(s, Action::OpenPath, s.recent[i]);
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.recent[i].c_str());
                const std::string dir = directory_of(s.recent[i]);
                if (!dir.empty()) {
                    ImGui::SameLine(0, 10 * us);
                    ImGui::PushStyleColor(ImGuiCol_Text, p.textFaint);
                    ImGui::PushFont(fonts().regular, 13 * us);
                    ImGui::TextUnformatted(dir.c_str());
                    ImGui::PopFont();
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::Dummy(ImVec2(0, 10 * us));
        const char* hint = "Drop a PDF anywhere in this window to open it";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (inner - ImGui::CalcTextSize(hint).x) / 2));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "%s", hint);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// --------------------------------------------------------------- shortcuts
void shortcuts(Session& s) {
    const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, route)) platform::request_action(s, Action::Open);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, route) && s.has_document) platform::request_action(s, Action::Close);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, route)) s.show_sidebar = !s.show_sidebar;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_I, route)) s.show_properties = !s.show_properties;
    if (ImGui::Shortcut(ImGuiKey_F1, route)) s.show_shortcuts = true;
    if (!s.has_document) return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, route)) open_find(s);
    if (ImGui::Shortcut(ImGuiKey_F3, route)) find_step(s, 1);
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F3, route)) find_step(s, -1);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, route)) { s.goto_input = s.view.current_page + 1; s.show_goto = true; }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, route) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, route)) zoom_step_view(s, true);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, route) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, route)) zoom_step_view(s, false);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, route)) set_fit(s, FitMode::Page);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_1, route)) zoom_to(s, 1.0f);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_2, route)) set_fit(s, FitMode::Width);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, route)) rotate_view(s, 1);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R, route)) rotate_view(s, -1);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, route) && !ImGui::GetIO().WantTextInput) platform::copy_selection_or_page(s);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, route)) platform::set_invert_pages(s, !s.invert_pages);
}

void dock_layout(Session& s, ImGuiID dock, const ImGuiViewport* viewport) {
    if (s.dock_initialized) return;
    if (s.reset_layout) { ImGui::DockBuilderRemoveNode(dock); s.reset_layout = false; }
    if (!ImGui::DockBuilderGetNode(dock)) {
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
        ImGuiID center = dock;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Document", center);
        ImGui::DockBuilderDockWindow("Pages", left);
        ImGui::DockBuilderDockWindow("Outline", left);
        ImGui::DockBuilderDockWindow("Properties", right);
        ImGui::DockBuilderFinish(dock);
        s.sidebar_focus_tab = s.sidebar_tab;
        s.sidebar_focus_frames = 3;
    }
    s.dock_initialized = true;
}
} // namespace

void set_fonts(Fonts f) { loaded_fonts = f; }
const Fonts& fonts() { return loaded_fonts; }
float ui_scale(const Session& s) { return s.scale * s.dpi; }

bool icon_button(const char* icon, const char* label, bool enabled, bool active) {
    const auto& p = fwPaletteCurrent();
    ImGui::BeginDisabled(!enabled);
    // Flat buttons: no fill at rest, a soft surface on hover, the accent as a
    // tint when the control is in its "on" state.
    ImGui::PushStyleColor(ImGuiCol_Button, active ? p.accentFill : IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? fwWithAlpha(p.accent, 110) : p.surfaceHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? fwWithAlpha(p.accent, 150) : p.surfaceActive);
    if (active) ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
    const std::string id = std::string(icon) + "###" + label;
    const float side = ImGui::GetFrameHeight();
    const bool pressed = ImGui::Button(id.c_str(), ImVec2(side, side));
    if (active) ImGui::PopStyleColor();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", label);
    ImGui::EndDisabled();
    return pressed;
}

void toolbar_divider() {
    ImGui::SameLine(0, 8);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + h * 0.2f), ImVec2(pos.x, pos.y + h * 0.8f), fwPaletteCurrent().border, 1.0f);
    ImGui::Dummy(ImVec2(1, h));
    ImGui::SameLine(0, 8);
}

void zoom_to(Session& s, float zoom) { s.view.fit = FitMode::Manual; s.view.zoom = clamp_zoom(zoom); }
void zoom_step_view(Session& s, bool in) { zoom_to(s, zoom_step(s.view.zoom, in)); }
void set_fit(Session& s, FitMode mode) { s.view.fit = mode; }
void rotate_view(Session& s, int quarter_turns) {
    if (!s.has_document) return;
    s.view.rotate(quarter_turns);
    s.request_scroll(s.view.current_page);
}
void open_find(Session& s) { if (!s.has_document) return; s.show_find = true; s.focus_find = true; }
void close_find(Session& s) { s.show_find = false; s.find_text.clear(); s.find_dirty = true; }
void find_step(Session& s, int direction) {
    if (!s.show_find) open_find(s);
    s.find_step_requested = true;
    s.find_step_direction = direction;
}

void draw_workspace(Session& s) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(3);
    menu(s);
    shortcuts(s);
    const ImGuiID dock = ImGui::GetID("MainDock");
    dock_layout(s, dock, viewport);
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_NoWindowMenuButton);
    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * ui_scale(s), 6 * ui_scale(s)));
    if (ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse)) {
        toolbar(s);
        ImGui::Spacing();
        find_bar(s);
        const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2 + 4 * ui_scale(s);
        const float body = std::max(60.0f, ImGui::GetContentRegionAvail().y - footer);
        if (s.has_document) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("CanvasHost", ImVec2(0, body), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            draw_canvas(s);
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("WelcomeHost", ImVec2(0, body), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            welcome(s);
            ImGui::EndChild();
        }
        status_bar(s);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (s.show_sidebar) {
        draw_pages_panel(s);
        draw_outline_panel(s);
    }
    if (s.sidebar_focus_frames > 0) { --s.sidebar_focus_frames; s.sidebar_tab = s.sidebar_focus_tab; }
    if (s.show_properties) draw_properties_panel(s);
    draw_dialogs(s);
}

} // namespace davepdf::ui
