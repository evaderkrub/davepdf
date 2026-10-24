// End-to-end tests driving the real SDL/ImGui application with ImGui Test Engine.
#include "app/session.h"
#include "app/sample_pdf.h"
#include "platform/desktop.h"
#include "ui/fwTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_context.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace davepdf;

static std::string screenshot_request;
static bool screenshot_saved = false;
void capture_davepdf_test_frame(SDL_Renderer* renderer) {
    if (screenshot_request.empty()) return;
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    screenshot_saved = surface && SDL_SaveBMP(surface, screenshot_request.c_str());
    SDL_DestroySurface(surface);
    screenshot_request.clear();
}
// Rendering is asynchronous; tests that look at pixels wait for the worker.
static void wait_for_renders(ImGuiTestContext* ctx, Session& s) {
    for (int i = 0; i < 300 && (s.render_queue > 0 || platform::page_image_pending()); ++i) ctx->Yield();
}
// Screenshots are optional: set DAVEPDF_TEST_SCREENSHOTS to a folder to get them.
static void capture_frame(ImGuiTestContext* ctx, const char* name) {
    const char* folder = std::getenv("DAVEPDF_TEST_SCREENSHOTS");
    if (!folder) return;
    wait_for_renders(ctx, *static_cast<Session*>(ctx->Test->UserData));
    std::filesystem::create_directories(folder);
    screenshot_request = (std::filesystem::path(folder) / (std::string(name) + ".bmp")).string();
    screenshot_saved = false;
    ctx->Yield(3);
    IM_CHECK(screenshot_saved);
}

static void reset_session(Session& s) {
    ImGui::ClearActiveID();
    ImGui::ClosePopupsOverWindow(nullptr, false);
    if (s.has_document) platform::close_document(s);
    s.scale = 1.0f;
    s.theme = 0;
    fwRequestTheme(0);
    s.show_find = false; s.show_results = false; s.find_text.clear(); s.find_match_case = false; s.find_dirty = true;
    s.show_about = s.show_goto = s.show_password = s.show_shortcuts = false;
    s.show_properties = false;
    s.show_sidebar = true;
    s.error.clear();
    s.status.clear();
    s.view.fit = FitMode::Width;
    s.view.zoom = 1.0f;
    s.view.rotation = 0;
    s.selection.clear();
    s.selecting = false;
    s.invert_pages = false;
}

static void open_sample(ImGuiTestContext* ctx, Session& s) {
    ctx->SetRef("Workspace");
    ctx->MenuClick("File/Open sample document");
    ctx->Yield(4);
    IM_CHECK(s.has_document);
    IM_CHECK_EQ(s.view.page_count(), kSamplePageCount);
}

static void wait_for_search(ImGuiTestContext* ctx, Session& s) {
    for (int i = 0; i < 60 && (s.find_dirty || s.find_progress < s.view.page_count()); ++i) ctx->Yield();
}

void register_davepdf_tests(ImGuiTestEngine* engine, Session& session) {
    ImGuiTest* test = IM_REGISTER_TEST(engine, "davepdf", "welcome_then_open_and_navigate");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        ctx->Yield(3);
        IM_CHECK(!s.has_document);
        ImGuiWindow* document = ImGui::FindWindowByName("Document");
        IM_CHECK(document != nullptr && document->WasActive);
        capture_frame(ctx, "welcome");
        open_sample(ctx, s);
        IM_CHECK_EQ(s.view.current_page, 0);
        IM_CHECK(s.info.title == kSampleTitle);
        // The render worker produces the first page and reports its timing.
        wait_for_renders(ctx, s);
        IM_CHECK(s.last_render_ms > 0.0f);
        IM_CHECK_EQ(s.render_queue, 0);
        capture_frame(ctx, "document");
        ctx->SetRef("Document");
        ctx->ItemClick("**/Next page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 1);
        ctx->ItemClick("**/Last page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 2);
        IM_CHECK(s.scroll_y > 100.0f);
        ctx->ItemClick("**/First page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 0);
        IM_CHECK(s.scroll_y < 0.5f);
        ctx->ItemInputValue("**/###PageInput", 3);
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 2);
        ctx->ItemClick("**/Previous page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 1);
        // Keyboard: End of document, Home of the current page.
        ctx->MouseMove("**/Pages");
        ctx->KeyPress(ImGuiKey_End);
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 2);
        // Close through the shortcut.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_W);
        ctx->Yield(3);
        IM_CHECK(!s.has_document);
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "zoom_fit_and_rotate");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        open_sample(ctx, s);
        ctx->SetRef("Document");
        IM_CHECK(s.view.fit == FitMode::Width);
        const float fitted = s.view.zoom;
        IM_CHECK(fitted > 0.2f && fitted < 3.0f);
        // Fit width really fits: the widest page fills the viewport minus margins.
        const Layout layout = layout_pages(s.view, s.dpi);
        IM_CHECK(std::fabs(layout.width - s.view_width) < 2.0f);
        ctx->ItemClick("**/Zoom in");
        ctx->Yield(3);
        IM_CHECK(s.view.fit == FitMode::Manual);
        IM_CHECK(s.view.zoom > fitted);
        ctx->ItemClick("**/Zoom out");
        ctx->ItemClick("**/Zoom out");
        ctx->Yield(3);
        IM_CHECK(s.view.zoom < fitted);
        ctx->ItemClick("**/Fit page");
        ctx->Yield(3);
        IM_CHECK(s.view.fit == FitMode::Page);
        const Layout page_layout = layout_pages(s.view, s.dpi);
        IM_CHECK(page_layout.pages[0].rect.h <= s.view_height);
        ctx->SetRef("Workspace");
        ctx->MenuClick("View/Zoom/100%");
        ctx->Yield(3);
        IM_CHECK(s.view.fit == FitMode::Manual);
        IM_CHECK(std::fabs(s.view.zoom - 1.0f) < 0.001f);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_2);
        ctx->Yield(3);
        IM_CHECK(s.view.fit == FitMode::Width);
        ctx->SetRef("Document");
        ctx->ItemClick("**/Rotate right");
        ctx->Yield(4);
        IM_CHECK_EQ(s.view.rotation, 90);
        const Layout rotated = layout_pages(s.view, s.dpi);
        IM_CHECK(rotated.pages[0].rect.w > rotated.pages[0].rect.h);
        capture_frame(ctx, "rotated");
        ctx->ItemClick("**/Rotate left");
        ctx->ItemClick("**/Rotate left");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.rotation, 270);
        ctx->ItemClick("**/Rotate right");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.rotation, 0);
        // Ctrl+wheel over the page zooms around the pointer and leaves fit mode.
        ctx->MouseMove("**/Pages");
        const float before = s.view.zoom;
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->MouseWheelY(2.0f);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->Yield(3);
        IM_CHECK(s.view.fit == FitMode::Manual);
        IM_CHECK(s.view.zoom > before);
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "find_text_and_step_through_hits");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        open_sample(ctx, s);
        ctx->SetRef("Workspace");
        // The find box is always on the toolbar; typing into it is enough.
        ctx->SetRef("Document");
        IM_CHECK(ctx->ItemExists("**/###FindText"));
        // Typing selects the first hit as it is found. ItemInputValue finishes
        // with Shift+Enter, which is "previous match" and wraps to the last hit.
        ctx->ItemInputValue("**/###FindText", "davepdf sample");
        wait_for_search(ctx, s);
        IM_CHECK(s.show_find && s.show_results);
        IM_CHECK(s.hits.size() == 3);
        IM_CHECK_EQ(s.hits[0].page, 0);
        IM_CHECK_EQ(s.current_hit, 2);
        IM_CHECK_EQ(s.view.current_page, 2);
        ctx->Yield(4);
        capture_frame(ctx, "find");
        ctx->ItemClick("**/Next match");  // wraps forward
        ctx->Yield(4);
        IM_CHECK_EQ(s.current_hit, 0);
        IM_CHECK_EQ(s.view.current_page, 0);
        ctx->ItemClick("**/Next match");
        ctx->Yield(4);
        IM_CHECK_EQ(s.current_hit, 1);
        IM_CHECK_EQ(s.view.current_page, 1);
        ctx->ItemClick("**/Previous match");
        ctx->Yield(4);
        IM_CHECK_EQ(s.current_hit, 0);
        IM_CHECK_EQ(s.view.current_page, 0);
        // A search started while reading page 3 lists page 3's hit first.
        ctx->ItemClick("**/Last page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 2);
        ctx->ItemInputValue("**/###FindText", "sample page");
        wait_for_search(ctx, s);
        IM_CHECK(s.hits.size() == 3);
        IM_CHECK_EQ(s.hits[0].page, 2);
        IM_CHECK_EQ(s.hits[1].page, 0);
        IM_CHECK_EQ(s.hits[2].page, 1);
        IM_CHECK_EQ(s.current_hit, 2);  // Shift+Enter from the first hit wraps to the last
        // Match case excludes the lower-case body text.
        ctx->ItemClick("**/Match case");
        ctx->ItemInputValue("**/###FindText", "DAVEPDF");
        wait_for_search(ctx, s);
        IM_CHECK(s.hits.empty());
        ctx->ItemClick("**/Match case");
        wait_for_search(ctx, s);
        IM_CHECK(s.hits.size() == 4);  // the title on every page plus "by davepdf itself" on page 1
        // The results panel lists every hit; clicking one jumps to it.
        IM_CHECK(s.show_results);
        ImGuiWindow* results = ImGui::FindWindowByName("Search results");
        IM_CHECK(results != nullptr && results->WasActive);
        ctx->WindowFocus("//Search results");
        ctx->SetRef("Search results");
        IM_CHECK(ctx->ItemExists("###hit0") && ctx->ItemExists("###hit3"));
        IM_CHECK(!s.hits[2].match.empty());
        ctx->ItemClick("###hit2");
        ctx->Yield(4);
        IM_CHECK_EQ(s.current_hit, 2);
        IM_CHECK_EQ(s.view.current_page, s.hits[2].page);
        capture_frame(ctx, "results");
        ctx->SetRef("Document");
        ctx->ItemClick("**/Close find");
        ctx->Yield(3);
        IM_CHECK(!s.show_find);
        IM_CHECK(!s.show_results);
        IM_CHECK(s.find_text.empty());
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "outline_thumbnails_and_links");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        open_sample(ctx, s);
        IM_CHECK(s.outline.size() == 3);
        ctx->WindowFocus("//Outline");
        ctx->Yield(2);
        ctx->SetRef("Outline");
        ctx->ItemClick("**/The last page");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 2);
        // The Outline click selected that tab; bring the thumbnails back.
        ctx->WindowFocus("//Pages");
        ctx->Yield(2);
        ctx->SetRef("Pages");
        ctx->ItemClick("###thumb1");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 1);
        ctx->ItemClick("###thumb0");
        ctx->Yield(3);
        IM_CHECK_EQ(s.view.current_page, 0);
        // Click the "jump to the last page" link on page 1 through the real canvas geometry.
        ImGuiTestItemInfo canvas = ctx->WindowInfo("//Document/CanvasHost/Canvas");
        IM_CHECK(canvas.Window != nullptr);
        const Layout layout = layout_pages(s.view, s.dpi);
        const auto& links = platform::page_links(s, 0);
        IM_CHECK(links.size() == 2);
        const LinkBox* jump = nullptr;
        for (const auto& link : links) if (link.page == 2) jump = &link;
        IM_CHECK(jump != nullptr);
        const RectF page_rect = layout.pages[0].rect;
        const float origin_x = canvas.Window->InnerRect.Min.x - canvas.Window->Scroll.x + std::max(0.0f, (canvas.Window->InnerRect.GetWidth() - layout.width) / 2);
        const float origin_y = canvas.Window->InnerRect.Min.y - canvas.Window->Scroll.y;
        const ImVec2 target(origin_x + page_rect.x + (jump->rect.x + jump->rect.w / 2) * page_rect.w,
                            origin_y + page_rect.y + (jump->rect.y + jump->rect.h / 2) * page_rect.h);
        ctx->MouseMoveToPos(target);
        ctx->Yield(2);
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->Yield(4);
        IM_CHECK_EQ(s.view.current_page, 2);
        // Dragging pans instead of following a link.
        ctx->SetRef("Document");
        ctx->ItemClick("**/First page");
        ctx->Yield(3);
        const float before = s.scroll_y;
        ctx->MouseMoveToPos(target);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->MouseMoveToPos(ImVec2(target.x, target.y - 120));
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(3);
        IM_CHECK(s.scroll_y > before + 60);
        IM_CHECK_EQ(s.view.current_page, 0);
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "select_text_copy_and_invert");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        open_sample(ctx, s);
        // Locate the title on page 1 through a search, then drag across it on screen.
        s.find_text = "davepdf sample";
        s.find_dirty = true;
        wait_for_search(ctx, s);
        IM_CHECK(s.hits.size() == 3 && s.hits[0].page == 0);
        const RectF title = s.hits[0].rects[0];
        ImGuiTestItemInfo canvas = ctx->WindowInfo("//Document/CanvasHost/Canvas");
        IM_CHECK(canvas.Window != nullptr);
        const Layout layout = layout_pages(s.view, s.dpi);
        const RectF page_rect = layout.pages[0].rect;
        const float origin_x = canvas.Window->InnerRect.Min.x - canvas.Window->Scroll.x + std::max(0.0f, (canvas.Window->InnerRect.GetWidth() - layout.width) / 2);
        const float origin_y = canvas.Window->InnerRect.Min.y - canvas.Window->Scroll.y;
        const ImVec2 from(origin_x + page_rect.x + (title.x + 0.01f) * page_rect.w, origin_y + page_rect.y + (title.y + title.h / 2) * page_rect.h);
        const ImVec2 to(origin_x + page_rect.x + (title.x + title.w - 0.01f) * page_rect.w, from.y);
        const float scroll_before = s.scroll_y;
        wait_for_renders(ctx, s);
        ctx->MouseMoveToPos(from);
        ctx->Yield(2);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->MouseMoveToPos(ImVec2((from.x + to.x) / 2, from.y));
        ctx->MouseMoveToPos(to);
        ctx->Yield(2);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(3);
        IM_CHECK(!s.selection.empty());
        IM_CHECK_EQ(s.selection.page, 0);
        IM_CHECK(s.selection.count() >= 8);
        IM_CHECK(!s.selection.rects.empty());
        IM_CHECK(std::fabs(s.scroll_y - scroll_before) < 0.5f);  // selecting does not pan
        capture_frame(ctx, "selection");
        SDL_SetClipboardText("");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
        ctx->Yield(3);
        char* clip = SDL_GetClipboardText();
        const std::string copied = clip ? clip : "";
        SDL_free(clip);
        IM_CHECK(copied.find("davepdf") != std::string::npos || copied.find("sample") != std::string::npos);
        IM_CHECK(copied.find("Welcome") == std::string::npos);  // not the whole page
        // Escape clears; Ctrl+C then copies the whole page.
        ctx->MouseMoveToPos(from);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(s.selection.empty());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
        ctx->Yield(3);
        clip = SDL_GetClipboardText();
        const std::string page_text = clip ? clip : "";
        SDL_free(clip);
        IM_CHECK(page_text.find("Welcome") != std::string::npos);
        // Night mode flips the page pixels and retires the cached textures.
        const unsigned generation = s.render_generation;
        ctx->SetRef("Workspace");
        ctx->MenuClick("View/Invert page colours");
        ctx->Yield(6);
        IM_CHECK(s.invert_pages);
        IM_CHECK(s.render_generation > generation);
        capture_frame(ctx, "inverted");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N);
        ctx->Yield(3);
        IM_CHECK(!s.invert_pages);
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "scale_theme_properties_and_modal_about");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        open_sample(ctx, s);
        ctx->SetRef("Workspace");
        ctx->MenuClick("View/GUI scale/125%");
        ctx->Yield(4);
        IM_CHECK_EQ(s.scale, 1.25f);
        IM_CHECK(std::fabs(ImGui::GetStyle().FontScaleMain - 1.25f) < 0.001f);
        ctx->MenuClick("View/Theme/Light");
        ctx->Yield(4);
        IM_CHECK_EQ(s.theme, fwThemeIndexByName("Light"));
        IM_CHECK_EQ(fwThemeCurrentIndex(), fwThemeIndexByName("Light"));
        capture_frame(ctx, "light-125");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_I);
        ctx->Yield(3);
        IM_CHECK(s.show_properties);
        ImGuiWindow* properties = ImGui::FindWindowByName("Properties");
        IM_CHECK(properties != nullptr && properties->WasActive);
        ctx->MenuClick("Help/About davepdf");
        ctx->Yield(3);
        ImGuiWindow* modal = ImGui::FindWindowByName("About davepdf");
        IM_CHECK(modal != nullptr && modal->WasActive);
        IM_CHECK((modal->Flags & ImGuiWindowFlags_Modal) != 0);
        IM_CHECK(ImGui::GetTopMostPopupModal() == modal);
        capture_frame(ctx, "about");
        ctx->SetRef("About davepdf");
        ctx->ItemClick("Close");
        ctx->Yield(2);
        IM_CHECK(!s.show_about);
        IM_CHECK(ImGui::GetTopMostPopupModal() == nullptr);
        ctx->SetRef("Workspace");
        ctx->MenuClick("Go/Go to page...");
        ctx->Yield(2);
        ctx->SetRef("Go to page");
        ctx->ItemInputValue("###GotoInput", 2);
        ctx->Yield(3);
        IM_CHECK(!s.show_goto);
        IM_CHECK_EQ(s.view.current_page, 1);
        ctx->SetRef("Workspace");
        ctx->MenuClick("View/GUI scale/100%");
        ctx->MenuClick("View/Theme/Wili Dark");
        ctx->Yield(3);
        IM_CHECK_EQ(s.scale, 1.0f);
        s.show_properties = false;
    };

    test = IM_REGISTER_TEST(engine, "davepdf", "open_from_disk_and_bad_files");
    test->UserData = &session;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = *static_cast<Session*>(ctx->Test->UserData);
        reset_session(s);
        const auto dir = std::filesystem::temp_directory_path() / "davepdf-ui-test";
        std::filesystem::create_directories(dir);
        const auto good = dir / "sample copy.pdf";
        const auto bad = dir / "broken.pdf";
        { std::ofstream f(good, std::ios::binary); f << make_sample_pdf(); }
        { std::ofstream f(bad, std::ios::binary); f << "%PDF-1.4 but nothing else"; }
        const auto u8 = [](const std::filesystem::path& p) { const auto v = p.u8string(); return std::string(v.begin(), v.end()); };
        platform::request_action(s, Action::OpenPath, u8(good));
        ctx->Yield(4);
        IM_CHECK(s.has_document);
        IM_CHECK_STR_EQ(s.info.file_name.c_str(), "sample copy.pdf");
        IM_CHECK(!s.recent.empty() && s.recent.front() == u8(good));
        platform::request_action(s, Action::OpenPath, u8(bad));
        ctx->Yield(4);
        IM_CHECK(!s.error.empty());
        // The failed open leaves the previous document in place and shows a modal message.
        IM_CHECK(s.has_document && s.info.file_name == "sample copy.pdf");
        ImGuiWindow* message = ImGui::FindWindowByName("davepdf");
        IM_CHECK(message != nullptr && message->WasActive && (message->Flags & ImGuiWindowFlags_Modal));
        ctx->SetRef("davepdf");
        ctx->ItemClick("OK");
        ctx->Yield(2);
        IM_CHECK(s.error.empty());
        platform::request_action(s, Action::OpenPath, u8(dir / "missing.pdf"));
        ctx->Yield(3);
        IM_CHECK(!s.error.empty());
        ctx->SetRef("davepdf");
        ctx->ItemClick("OK");
        ctx->Yield(2);
        IM_CHECK(s.error.empty());
        // Recent list: the menu opens the file again after closing.
        platform::close_document(s);
        ctx->Yield(2);
        ctx->SetRef("Workspace");
        ctx->MenuClick("File/Open recent/###recent0");
        ctx->Yield(4);
        IM_CHECK(s.has_document);
        // The Recent panel groups by folder, removes single entries and clears the list.
        const auto sub = dir / "sub";
        std::filesystem::create_directories(sub);
        const auto second = sub / "second.pdf";
        { std::ofstream f(second, std::ios::binary); f << make_sample_pdf(); }
        platform::request_action(s, Action::OpenPath, u8(second));
        ctx->Yield(4);
        IM_CHECK(s.recent.size() == 2 && s.recent[0] == u8(second));
        ctx->WindowFocus("//Recent");
        ctx->Yield(2);
        ctx->SetRef("Recent");
        IM_CHECK(ctx->ItemExists("**/###recentfile0") && ctx->ItemExists("**/###recentfile1"));
        ctx->ItemClick("**/###recentfile1");  // the older entry: opens it
        ctx->Yield(4);
        IM_CHECK(s.info.file_name == "sample copy.pdf");
        IM_CHECK(s.recent[0] == u8(good));
        ctx->ItemClick("**/remove1");  // drop second.pdf
        ctx->Yield(3);
        IM_CHECK(s.recent.size() == 1 && s.recent[0] == u8(good));
        ctx->ItemClick("Clear list");
        ctx->Yield(2);
        ctx->SetRef("Clear recent list");
        ctx->ItemClick("Clear");
        ctx->Yield(2);
        IM_CHECK(s.recent.empty());
        platform::close_document(s);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };
}
