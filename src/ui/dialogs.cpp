#include "ui/workspace.h"
#include "ui/fwTheme.h"
#include "platform/desktop.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "IconsMaterialDesign.h"
#include <algorithm>
#include <string>

#ifndef DAVEPDF_VERSION
#define DAVEPDF_VERSION "dev"
#endif
#ifndef DAVEPDF_PDFIUM_VERSION
#define DAVEPDF_PDFIUM_VERSION "unknown"
#endif

namespace davepdf::ui {
namespace {
void center_next_window() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}
void shortcut_row(const char* keys, const char* action) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(fwPaletteCurrent().accent), "%s", keys);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(action);
}
} // namespace

void draw_dialogs(Session& s) {
    const float us = ui_scale(s);
    const auto& p = fwPaletteCurrent();
    const ImGuiWindowFlags modal_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    // About: always modal, as the brief asks.
    if (s.show_about) ImGui::OpenPopup("About davepdf");
    center_next_window();
    if (ImGui::BeginPopupModal("About davepdf", &s.show_about, modal_flags)) {
        ImGui::PushFont(fonts().icons, 48 * us);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.accent), ICON_MD_PICTURE_AS_PDF);
        ImGui::PopFont();
        ImGui::SameLine(0, 14 * us);
        ImGui::BeginGroup();
        ImGui::PushFont(fonts().bold, 26 * us);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.heading), "davepdf");
        ImGui::PopFont();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "Version " DAVEPDF_VERSION "  |  Windows x64");
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("A portable native PDF viewer.");
        ImGui::Spacing();
        if (ImGui::BeginTable("about", 2, ImGuiTableFlags_SizingFixedFit)) {
            const auto row = [&](const char* k, const char* v) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "%s", k);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(v);
            };
            row("Rendering", "PDFium " DAVEPDF_PDFIUM_VERSION " (pdfium-binaries)");
            row("Window and input", "SDL3");
            row("Interface", "Dear ImGui, docking branch");
            row("Typeface and icons", "Open Sans, Material Icons");
            row("Theme", "fwcom token themes (FreeWili)");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textFaint), "Third-party licenses are in the assets/licenses folder.");
        ImGui::Spacing();
        const float w = 110 * us;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
        if (ImGui::Button("Close", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { s.show_about = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Go to page.
    if (s.show_goto) ImGui::OpenPopup("Go to page");
    center_next_window();
    if (ImGui::BeginPopupModal("Go to page", &s.show_goto, modal_flags)) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "Enter a page number from 1 to %d.", s.view.page_count());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(120 * us);
        const bool entered = ImGui::InputInt("###GotoInput", &s.goto_input, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool valid = s.goto_input >= 1 && s.goto_input <= s.view.page_count();
        ImGui::Spacing();
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Go", ImVec2(100 * us, 0)) || (entered && valid)) { platform::go_to_page(s, s.goto_input - 1); s.show_goto = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100 * us, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { s.show_goto = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Password.
    if (s.show_password) ImGui::OpenPopup("Password required");
    center_next_window();
    if (ImGui::BeginPopupModal("Password required", &s.show_password, modal_flags)) {
        ImGui::TextUnformatted(ICON_MD_LOCK "  This document is encrypted.");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.textMuted), "Enter the password to open it.");
        if (!s.status.empty() && s.status.find("not accepted") != std::string::npos) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.danger), "%s", s.status.c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(260 * us);
        const bool entered = ImGui::InputText("###Password", &s.password, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if (ImGui::Button("Open", ImVec2(100 * us, 0)) || entered) {
            s.show_password = false;
            ImGui::CloseCurrentPopup();
            platform::request_action(s, Action::OpenPath, s.pending_path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100 * us, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { s.show_password = false; s.password.clear(); s.pending_path.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (!s.show_password && ImGui::IsPopupOpen("Password required")) {
        // Closed with the title-bar X: forget the pending file.
        s.password.clear();
        s.pending_path.clear();
    }

    // Keyboard shortcuts.
    if (s.show_shortcuts) ImGui::OpenPopup("Keyboard shortcuts");
    center_next_window();
    if (ImGui::BeginPopupModal("Keyboard shortcuts", &s.show_shortcuts, modal_flags)) {
        if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            shortcut_row("Ctrl+O", "Open a document");
            shortcut_row("Ctrl+W", "Close the document");
            shortcut_row("Ctrl+F  /  F3  /  Shift+F3", "Find, next match, previous match");
            shortcut_row("Ctrl+G", "Go to page");
            shortcut_row("Page Down / Page Up / Space", "Scroll a screen");
            shortcut_row("Home / End", "Top of the page / end of the document");
            shortcut_row("Arrow keys", "Scroll, or step pages when nothing to scroll sideways");
            shortcut_row("Ctrl++  /  Ctrl+-  /  Ctrl+wheel", "Zoom in, out, around the pointer");
            shortcut_row("Ctrl+1  /  Ctrl+2  /  Ctrl+0", "Actual size, fit width, fit page");
            shortcut_row("Ctrl+R  /  Ctrl+Shift+R", "Rotate clockwise / counter-clockwise");
            shortcut_row("Ctrl+B  /  Ctrl+I", "Toggle the sidebar / properties");
            shortcut_row("Ctrl+C", "Copy the selection, or the whole page's text");
            shortcut_row("Ctrl+Shift+N", "Invert page colours (night reading)");
            shortcut_row("Drag on text / elsewhere", "Select text / pan the page");
            shortcut_row("Escape", "Clear the selection or close find");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(110 * us, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { s.show_shortcuts = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Errors.
    if (!s.error.empty()) ImGui::OpenPopup("davepdf");
    center_next_window();
    if (ImGui::BeginPopupModal("davepdf", nullptr, modal_flags)) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.danger), ICON_MD_ERROR_OUTLINE);
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460 * us);
        ImGui::TextUnformatted(s.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(100 * us, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) { s.error.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace davepdf::ui
