#include "platform/desktop.h"
#include "platform/host.h"
#include "platform/pdf_engine.h"
#include "platform/render_worker.h"
#include "app/sample_pdf.h"
#include "ui/workspace.h"
#include "ui/fwTheme.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#ifdef DAVEPDF_TESTING
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_exporters.h"
void register_davepdf_tests(ImGuiTestEngine*, davepdf::Session&);
void capture_davepdf_test_frame(SDL_Renderer*);
#endif

namespace davepdf::platform {
namespace {
using Clock = std::chrono::steady_clock;

SDL_Window* main_window = nullptr;
SDL_Renderer* renderer = nullptr;

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}
std::filesystem::path from_utf8(const std::string& text) { return std::filesystem::path(std::u8string(text.begin(), text.end())); }

// Rendered pages, keyed by size and rotation. Rendering happens on the
// RenderWorker thread; the frame loop only uploads finished pixels.
struct CacheEntry {
    SDL_Texture* texture = nullptr;
    int page = 0, width = 0, height = 0, rotation = 0;
    unsigned generation = 0;
    unsigned long long last_used = 0;
    size_t bytes = 0;
};
constexpr size_t kCacheBudget = 640ull * 1024 * 1024;
constexpr int kMaxTexture = 8192;
// How long a zoom or rotation must hold still before pages re-render at the
// new size. Shorter feels laggy under continuous wheel input; longer leaves
// the stretched preview on screen noticeably.
constexpr double kSettleSeconds = 0.12;
// Texture uploads per frame are capped so a burst of finished renders cannot
// stall a frame the way rendering used to.
constexpr size_t kUploadBudgetBytes = 48ull * 1024 * 1024;
// Pages wider than this get a quick half-size pass first when nothing cached
// can stand in for them.
constexpr int kPreviewThreshold = 600;
// Layout schema: 1 = Pages/Outline/Properties, 2 = + Search results, Recent.
constexpr int kLayoutVersion = 2;

struct App {
    Session session;
    PdfDocument doc;
    std::unique_ptr<RenderWorker> worker;
    std::vector<CacheEntry> cache;
    std::vector<RenderResult> inbox;   // finished renders awaiting upload
    size_t cache_bytes = 0;
    std::unordered_map<int, std::vector<LinkBox>> links;
    unsigned long long frame_index = 0;
    bool frame_pending = false;
    int find_start_page = 0;

    bool testing = false;
    int smoke_frames = 0;
    std::string shot_path;
    int frames = 0, result = 0;
    Clock::time_point start;
    std::filesystem::path settings_dir;
    std::string ini_path;
    Settings saved;
#ifdef DAVEPDF_TESTING
    ImGuiTestEngine* engine = nullptr;
#endif

    bool init(int argc, char** argv);
    bool frame();
    void persist(bool force);
    int shutdown();
    void clear_cache();
    void evict();
    void upload_results();
    void find_pump();
    void open_path(const std::filesystem::path& path);
    void open_bytes(std::string bytes, const std::filesystem::path& path, const std::string& label);
};
App* g_app = nullptr;

bool load_fonts(const std::filesystem::path& base) {
    auto& atlas = *ImGui::GetIO().Fonts;
    ui::Fonts fonts;
    // Loaded through streams so a portable folder may sit on a Unicode path.
    const auto add = [&](const char* name, bool merge) -> ImFont* {
        std::ifstream stream(base / "assets/fonts" / name, std::ios::binary | std::ios::ate);
        if (!stream) return nullptr;
        const auto length = stream.tellg();
        if (length <= 0 || length > 16 * 1024 * 1024) return nullptr;
        void* data = IM_ALLOC(static_cast<size_t>(length));
        stream.seekg(0);
        stream.read(static_cast<char*>(data), length);
        if (!stream) { IM_FREE(data); return nullptr; }
        ImFontConfig config;
        config.MergeMode = merge;
        config.PixelSnapH = true;
        static const ImWchar icon_ranges[] = {0xe000, 0xf8ff, 0};
        if (merge) { config.GlyphMinAdvanceX = 16.0f; config.GlyphOffset.y = 2.0f; }
        return atlas.AddFontFromMemoryTTF(data, static_cast<int>(length), 16.0f, &config, merge ? icon_ranges : nullptr);
    };
    fonts.regular = add("OpenSans-Regular.ttf", false);
    if (!fonts.regular || !add("MaterialIcons-Regular.ttf", true)) return false;
    fonts.bold = add("OpenSans-SemiBold.ttf", false);
    if (!fonts.bold || !add("MaterialIcons-Regular.ttf", true)) return false;
    fonts.italic = add("OpenSans-Italic.ttf", false);
    if (!fonts.italic) return false;
    // A standalone icon face for large glyphs (welcome screen, about box).
    fonts.icons = add("MaterialIcons-Regular.ttf", false);
    if (!fonts.icons) return false;
    ImGui::GetIO().FontDefault = fonts.regular;
    ui::set_fonts(fonts);
    return true;
}

void App::clear_cache() {
    if (worker) worker->cancel_all();
    inbox.clear();
    for (auto& entry : cache) SDL_DestroyTexture(entry.texture);
    cache.clear();
    cache_bytes = 0;
    links.clear();
}

void App::upload_results() {
    if (worker) {
        auto fresh = worker->collect();
        for (auto& r : fresh) inbox.push_back(std::move(r));
    }
    size_t uploaded = 0;
    while (!inbox.empty() && uploaded < kUploadBudgetBytes) {
        RenderResult r = std::move(inbox.front());
        inbox.erase(inbox.begin());
        const RenderJob& j = r.job;
        // A render that outlived its document, rotation or night-mode setting is dropped.
        if (j.generation != session.render_generation || j.rotation != session.view.rotation || j.invert != session.invert_pages) continue;
        session.last_render_ms = static_cast<float>(r.milliseconds);
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, j.width, j.height);
        if (!texture) continue;
        SDL_UpdateTexture(texture, nullptr, r.bgra.data(), j.width * 4);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        uploaded += r.bgra.size();
        // Replace an identical entry rather than keeping two.
        for (size_t i = 0; i < cache.size(); ++i) {
            const auto& e = cache[i];
            if (e.page == j.page && e.width == j.width && e.height == j.height && e.rotation == j.rotation && e.generation == j.generation) {
                SDL_DestroyTexture(e.texture); cache_bytes -= e.bytes; cache[i] = cache.back(); cache.pop_back(); break;
            }
        }
        cache.push_back({texture, j.page, j.width, j.height, j.rotation, j.generation, frame_index, r.bgra.size()});
        cache_bytes += r.bgra.size();
    }
}

void App::evict() {
    const auto& view = session.view;
    // Renderings of another rotation or document can never be shown again.
    for (size_t i = 0; i < cache.size();) {
        auto& e = cache[i];
        if (e.rotation != view.rotation || e.generation != session.render_generation) {
            SDL_DestroyTexture(e.texture); cache_bytes -= e.bytes; cache[i] = cache.back(); cache.pop_back();
        } else ++i;
    }
    while (cache_bytes > kCacheBudget) {
        size_t oldest = cache.size();
        for (size_t i = 0; i < cache.size(); ++i)
            if (cache[i].last_used != frame_index && (oldest == cache.size() || cache[i].last_used < cache[oldest].last_used)) oldest = i;
        if (oldest == cache.size()) break;  // everything is on screen right now
        SDL_DestroyTexture(cache[oldest].texture);
        cache_bytes -= cache[oldest].bytes;
        cache[oldest] = cache.back();
        cache.pop_back();
    }
}

void App::open_bytes(std::string bytes, const std::filesystem::path& path, const std::string& label) {
    Session& s = session;
    std::string error;
    const auto size = bytes.size();
    if (worker) worker->cancel_all();
    // Waits for a render in flight to finish before the document changes under it.
    std::lock_guard<std::mutex> lock(doc.mutex());
    if (!doc.load_memory(std::move(bytes), s.password, error)) {
        if (doc.needs_password()) {
            // Keep the path so the password dialog can retry the same file.
            s.pending_path = utf8(path);
            s.show_password = true;
            if (!s.password.empty()) s.status = "The password was not accepted.";
            s.password.clear();
            return;
        }
        s.error = label + ": " + error;
        return;
    }
    // The fit mode (and a manual zoom) carries over from the previous
    // document; the canvas recomputes fitted zooms every frame.
    s.view.pages = doc.page_sizes();
    s.view.current_page = 0;
    s.view.rotation = 0;
    s.info = doc.metadata();
    s.info.path = utf8(path);
    s.info.file_name = path.empty() ? label : utf8(path.filename());
    s.info.bytes = size;
    s.outline = doc.outline();
    s.has_document = true;
    s.hits.clear();
    s.current_hit = -1;
    s.find_progress = 0;
    s.find_dirty = !s.find_text.empty();
    s.selection.clear();
    s.selecting = false;
    s.password.clear();
    s.show_password = false;
    s.pending_path.clear();
    s.scroll_y = 0;
    s.request_scroll(0);
    s.goto_input = 1;
    ++s.render_generation;
    clear_cache();
    if (!path.empty()) add_recent(s.recent, s.info.path);
    s.status = "Opened " + s.info.file_name + " (" + std::to_string(s.view.page_count()) + (s.view.page_count() == 1 ? " page)" : " pages)");
}

void App::open_path(const std::filesystem::path& given) {
    std::error_code ec;
    // Command-line and dropped paths are relative to wherever they came from,
    // not to the executable; absolute() resolves against the working directory.
    const auto path = std::filesystem::absolute(given, ec);
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        remove_recent(session.recent, utf8(path));
        session.error = "The file could not be opened:\n" + utf8(path);
        return;
    }
    const auto length = stream.tellg();
    if (length < 0 || length > (1ll << 31)) { session.error = "The file is too large to open (2 GB limit)."; return; }
    std::string bytes(static_cast<size_t>(length), '\0');
    stream.seekg(0);
    if (length > 0) stream.read(bytes.data(), length);
    if (!stream) { session.error = "The file could not be read:\n" + utf8(path); return; }
    open_bytes(std::move(bytes), path, utf8(path.filename()));
}

void App::find_pump() {
    Session& s = session;
    if (!s.has_document || !doc.is_open()) { s.hits.clear(); s.current_hit = -1; return; }
    if (s.find_dirty) {
        s.find_dirty = false;
        s.hits.clear();
        s.current_hit = -1;
        s.find_progress = 0;
        find_start_page = std::clamp(s.view.current_page, 0, std::max(0, s.view.page_count() - 1));
    }
    const int count = s.view.page_count();
    if (s.find_text.empty()) { s.find_progress = count; s.hits.clear(); s.current_hit = -1; return; }
    const auto jump = [&](const SearchHit& hit) {
        const RectF r = rotate_normalized(hit.rects.front(), s.view.rotation);
        s.request_scroll(hit.page, r.y + r.h / 2, true);
    };
    // Scanning starts at the page being read, so the first result is the one
    // a reader expects, and continues a few milliseconds per frame.
    // A render in progress owns the engine; scanning simply resumes next frame.
    std::unique_lock<std::mutex> lock(doc.mutex(), std::try_to_lock);
    const auto started = Clock::now();
    while (lock.owns_lock() && s.find_progress < count && std::chrono::duration<double, std::milli>(Clock::now() - started).count() < 4.0) {
        const int page = (find_start_page + s.find_progress) % count;
        const size_t before = s.hits.size();
        doc.search(page, s.find_text, s.find_match_case, s.hits);
        ++s.find_progress;
        if (s.current_hit < 0 && s.hits.size() > before) { s.current_hit = static_cast<int>(before); jump(s.hits[before]); }
    }
    if (lock.owns_lock()) lock.unlock();
    if (s.find_step_requested) {
        // A step typed before the scan has found anything waits for the scan
        // (which may be a frame late when the render thread holds the engine)
        // rather than being lost.
        if (!s.hits.empty()) {
            s.find_step_requested = false;
            const int n = static_cast<int>(s.hits.size());
            s.current_hit = ((s.current_hit + s.find_step_direction) % n + n) % n;
            jump(s.hits[static_cast<size_t>(s.current_hit)]);
        } else if (s.find_progress >= count) {
            s.find_step_requested = false;
        }
    }
    if (s.find_select_hit >= 0) {
        if (s.find_select_hit < static_cast<int>(s.hits.size())) { s.current_hit = s.find_select_hit; jump(s.hits[static_cast<size_t>(s.current_hit)]); }
        s.find_select_hit = -1;
    }
}

bool App::init(int argc, char** argv) {
    g_app = this;
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return false; }
    std::string open_at_start;
    bool open_sample = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--test") testing = true;
        else if (arg == "--smoke") smoke_frames = 8;
        else if (arg == "--sample") open_sample = true;
        else if (arg == "--shot" && i + 1 < argc) shot_path = argv[++i];
        else if (arg.rfind("--", 0) != 0) open_at_start = arg;
    }
    const auto base = executable_directory();
    std::string error;
    if (!pdf_init(error)) { std::fprintf(stderr, "%s\n", error.c_str()); return false; }
    worker = std::make_unique<RenderWorker>(doc);
    settings_dir = host::settings_directory();
    if (!testing) {
        std::ifstream file(settings_dir / "settings.txt");
        std::stringstream text;
        text << file.rdbuf();
        const Settings loaded = parse_settings(text.str());
        session.theme = std::clamp(loaded.theme, 0, fwThemeCount() - 1);
        session.scale = fwClampUiScale(loaded.scale);
        session.show_sidebar = loaded.show_sidebar;
        session.sidebar_tab = std::clamp(loaded.sidebar_tab, 0, 2);
        session.show_properties = loaded.show_properties;
        session.invert_pages = loaded.invert_pages;
        session.view.fit = static_cast<FitMode>(loaded.fit);
        session.recent = loaded.recent;
        session.adopt_new_panels = loaded.layout_version < kLayoutVersion;
        saved = loaded;
    }
    session.dpi = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (session.dpi <= 0) session.dpi = 1;
    main_window = SDL_CreateWindow("davepdf", static_cast<int>(1400 * session.dpi), static_cast<int>(900 * session.dpi),
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!main_window) { SDL_Quit(); return false; }
    host::decorate_window(main_window);
    SDL_SetWindowMinimumSize(main_window, 800, 560);
    SDL_SetWindowPosition(main_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    renderer = SDL_CreateRenderer(main_window, nullptr);
    if (!renderer) { SDL_DestroyWindow(main_window); SDL_Quit(); return false; }
    SDL_SetRenderVSync(renderer, testing ? 0 : 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Keyboard navigation stays off: arrows, Space and Home/End scroll the page.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Enter in the find field steps to the next hit and must not drop focus.
    io.ConfigInputTextEnterKeepActive = true;
    io.IniFilename = nullptr;
    ini_path = utf8(settings_dir / "layout.ini");
    if (!testing) ImGui::LoadIniSettingsFromDisk(ini_path.c_str());
    if (!load_fonts(base)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "davepdf assets missing", "Keep the assets folder beside davepdf.exe.", main_window);
        std::fprintf(stderr, "davepdf: fonts are missing from %s\n", utf8(base / "assets/fonts").c_str());
        ImGui::DestroyContext(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(main_window); SDL_Quit();
        return false;
    }
    fwApplyAppearanceNow(session.theme, session.scale, session.dpi);
    ImGui_ImplSDL3_InitForSDLRenderer(main_window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    if (open_sample) request_action(session, Action::OpenSample);
    if (!open_at_start.empty()) request_action(session, Action::OpenPath, open_at_start);
    SDL_ShowWindow(main_window);
#ifdef DAVEPDF_TESTING
    engine = ImGuiTestEngine_CreateContext();
    auto& test_io = ImGuiTestEngine_GetIO(engine);
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigLogToTTY = true;
    test_io.ConfigBreakOnError = false;
    static std::string report;
    report = utf8(base / "ui-test-results.xml");
    test_io.ExportResultsFilename = report.c_str();
    test_io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    register_davepdf_tests(engine, session);
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests);
#endif
    start = Clock::now();
    return true;
}

void App::persist(bool force) {
    if (testing) return;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantSaveIniSettings || force) {
            ImGui::SaveIniSettingsToDisk(ini_path.c_str());
            io.WantSaveIniSettings = false;
        }
    }
    Settings now;
    now.theme = session.theme;
    now.scale = session.scale;
    now.show_sidebar = session.show_sidebar;
    now.sidebar_tab = session.sidebar_tab;
    now.show_properties = session.show_properties;
    now.invert_pages = session.invert_pages;
    now.fit = static_cast<int>(session.view.fit);
    now.layout_version = kLayoutVersion;
    now.recent = session.recent;
    const std::string text = serialize_settings(now);
    if (force || text != serialize_settings(saved)) {
        std::ofstream file(settings_dir / "settings.txt", std::ios::binary);
        file << text;
        saved = now;
    }
}

bool App::frame() {
    ImGuiIO& io = ImGui::GetIO();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(main_window))) session.quit = true;
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) request_action(session, Action::OpenPath, event.drop.data);
        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) session.dpi = SDL_GetWindowDisplayScale(main_window);
    }
    ++frame_index;
    frame_pending = false;
    upload_results();
    if (session.selection_dirty && !session.selection.empty()) set_selection(session, session.selection.page, session.selection.anchor, session.selection.focus);
    fwPumpAppearance(session.scale, session.dpi);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ui::draw_workspace(session);
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    SDL_SetRenderDrawColorFloat(renderer, bg.x, bg.y, bg.z, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
#ifdef DAVEPDF_TESTING
    capture_davepdf_test_frame(renderer);
#endif
    if (!shot_path.empty() && frames == 24) {
        if (SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr)) { SDL_SaveBMP(surface, shot_path.c_str()); SDL_DestroySurface(surface); }
        session.quit = true;
    }
    SDL_RenderPresent(renderer);
    find_pump();
    evict();
    if (worker) {
        worker->prune(frame_index, session.render_generation, session.view.rotation);
        session.render_queue = static_cast<int>(worker->pending());
        if (session.render_queue > 0 || !inbox.empty()) frame_pending = true;
    }
    const std::string title = session.has_document ? session.info.file_name + " - davepdf" : "davepdf";
    SDL_SetWindowTitle(main_window, title.c_str());
    ++frames;
    if (smoke_frames && frames >= smoke_frames) return false;
#ifdef DAVEPDF_TESTING
    ImGuiTestEngine_PostSwap(engine);
    if (frames > 5 && ImGuiTestEngine_IsTestQueueEmpty(engine)) {
        int total = 0, succeeded = 0;
        ImGuiTestEngine_GetResult(engine, total, succeeded);
        result = total > 0 && total == succeeded ? 0 : 1;
        std::printf("davepdf UI tests: %d/%d passed\n", succeeded, total);
        return false;
    }
#endif
    if (testing && Clock::now() - start > std::chrono::seconds(160)) { result = 2; return false; }
    persist(false);
    // Nothing animates while idle, so a minimised or untouched window need not spin.
    if (!testing && !frame_pending && (SDL_GetWindowFlags(main_window) & SDL_WINDOW_MINIMIZED)) SDL_Delay(30);
    return !session.quit;
}

int App::shutdown() {
#ifdef DAVEPDF_TESTING
    ImGuiTestEngine_Stop(engine);
#endif
    persist(true);
    clear_cache();
    worker.reset();  // joins the render thread before the document goes away
    doc.close();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
#ifdef DAVEPDF_TESTING
    ImGuiTestEngine_DestroyContext(engine);
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(main_window);
    main_window = nullptr;
    pdf_shutdown();
    SDL_Quit();
    g_app = nullptr;
    return result;
}

} // namespace

void request_action(Session& s, Action action, const std::string& path) {
    App& app = *g_app;
    switch (action) {
    case Action::Quit: s.quit = true; break;
    case Action::Close: close_document(s); break;
    case Action::OpenSample: app.open_bytes(make_sample_pdf(), {}, "Sample document"); break;
    case Action::OpenPath: if (!path.empty()) app.open_path(from_utf8(path)); break;
    case Action::Open: {
        std::filesystem::path file;
        std::string error;
        if (host::choose_pdf(main_window, file, error)) app.open_path(file);
        else if (!error.empty()) s.error = error;
        break;
    }
    case Action::None: break;
    }
    s.pending = Action::None;
}

void close_document(Session& s) {
    App& app = *g_app;
    app.clear_cache();
    {
        std::lock_guard<std::mutex> lock(app.doc.mutex());
        app.doc.close();
    }
    s.has_document = false;
    s.view.pages.clear();
    s.view.current_page = 0;
    s.outline.clear();
    s.info = {};
    s.hits.clear();
    s.current_hit = -1;
    s.selection.clear();
    s.selecting = false;
    s.show_find = false;
    s.status = "Closed";
    ++s.render_generation;
}

PageImage page_image(Session& s, int page, int width, int height, bool thumbnail) {
    App& app = *g_app;
    if (!s.has_document || !app.worker || !s.view.valid_page(page) || width < 1 || height < 1) return {};
    width = std::min(width, kMaxTexture);
    height = std::min(height, kMaxTexture);
    for (auto& e : app.cache) {
        if (e.page == page && e.width == width && e.height == height && e.rotation == s.view.rotation && e.generation == s.render_generation) {
            e.last_used = app.frame_index;
            return {reinterpret_cast<unsigned long long>(e.texture), width, height, true};
        }
    }
    // The nearest rendering of the same page stands in while the exact size is produced.
    CacheEntry* best = nullptr;
    for (auto& e : app.cache)
        if (e.page == page && e.rotation == s.view.rotation && e.generation == s.render_generation && (!best || std::abs(e.width - width) < std::abs(best->width - width))) best = &e;
    const bool settling = ImGui::GetTime() - s.view_changed_at < kSettleSeconds;
    if (!(settling && best)) {
        // Pages near the one being read come first; thumbnails after all of them.
        const int distance = std::abs(page - s.view.current_page);
        RenderJob job;
        job.page = page; job.width = width; job.height = height; job.rotation = s.view.rotation;
        job.generation = s.render_generation; job.invert = s.invert_pages;
        job.priority = (thumbnail ? 100 : 0) + distance;
        job.requested = app.frame_index;
        if (!best && !thumbnail && width > kPreviewThreshold) {
            // Nothing to show yet: a half-size pass lands in a quarter of the time.
            RenderJob preview = job;
            preview.width = std::max(1, width / 2);
            preview.height = std::max(1, height / 2);
            preview.priority -= 1;
            app.worker->submit(preview);
        }
        app.worker->submit(job);
    }
    app.frame_pending = true;
    if (!best) return {};
    best->last_used = app.frame_index;
    return {reinterpret_cast<unsigned long long>(best->texture), best->width, best->height, false};
}

bool page_image_pending() { return g_app && g_app->frame_pending; }

const std::vector<LinkBox>& page_links(Session& s, int page) {
    static const std::vector<LinkBox> none;
    App& app = *g_app;
    if (!s.has_document || !s.view.valid_page(page)) return none;
    auto it = app.links.find(page);
    if (it == app.links.end()) {
        std::unique_lock<std::mutex> lock(app.doc.mutex(), std::try_to_lock);
        if (!lock.owns_lock() || !app.doc.is_open()) return none;  // not cached: asked again next frame
        it = app.links.emplace(page, app.doc.links(page)).first;
    }
    return it->second;
}

void activate_link(Session& s, const LinkBox& link) {
    if (link.page >= 0) { go_to_page(s, link.page); return; }
    std::string error;
    if (!host::open_external(link.uri, error)) s.error = error;
    else s.status = "Opened " + link.uri;
}

void copy_page_text(Session& s, int page) {
    App& app = *g_app;
    std::string text, error;
    std::lock_guard<std::mutex> lock(app.doc.mutex());
    if (!app.doc.is_open() || !app.doc.page_text(page, text, error)) { s.error = error.empty() ? "No document is open." : error; return; }
    if (text.empty()) { s.status = "Page " + std::to_string(page + 1) + " has no text layer"; return; }
    if (!SDL_SetClipboardText(text.c_str())) { s.error = SDL_GetError(); return; }
    s.status = "Copied the text of page " + std::to_string(page + 1);
}

int text_index_at(Session& s, int page, float nx, float ny, bool wait) {
    App& app = *g_app;
    if (!s.has_document || !s.view.valid_page(page)) return -1;
    std::unique_lock<std::mutex> lock(app.doc.mutex(), std::defer_lock);
    if (wait) lock.lock(); else if (!lock.try_lock()) return -1;  // hover simply re-asks next frame
    if (!app.doc.is_open()) return -1;
    return app.doc.char_index_at(page, nx, ny);
}

void set_selection(Session& s, int page, int anchor, int focus) {
    App& app = *g_app;
    if (!s.has_document || !s.view.valid_page(page) || anchor < 0 || focus < 0) { s.selection.clear(); s.selection_dirty = false; return; }
    const bool same = s.selection.page == page && s.selection.anchor == anchor && s.selection.focus == focus && !s.selection_dirty;
    s.selection.page = page;
    s.selection.anchor = anchor;
    s.selection.focus = focus;
    if (same) return;
    std::unique_lock<std::mutex> lock(app.doc.mutex(), std::try_to_lock);
    if (!lock.owns_lock() || !app.doc.is_open()) { s.selection_dirty = true; return; }  // boxes follow next frame
    s.selection.rects = app.doc.text_rects(page, s.selection.first(), s.selection.count());
    s.selection_dirty = false;
}

void copy_selection_or_page(Session& s) {
    App& app = *g_app;
    if (s.selection.empty()) { copy_page_text(s, s.view.current_page); return; }
    std::string text;
    {
        std::lock_guard<std::mutex> lock(app.doc.mutex());
        text = app.doc.text_range(s.selection.page, s.selection.first(), s.selection.count());
    }
    if (text.empty()) { s.status = "The selection has no text"; return; }
    if (!SDL_SetClipboardText(text.c_str())) { s.error = SDL_GetError(); return; }
    s.status = "Copied " + std::to_string(s.selection.count()) + (s.selection.count() == 1 ? " character" : " characters");
}

void set_invert_pages(Session& s, bool invert) {
    if (s.invert_pages == invert) return;
    s.invert_pages = invert;
    // Cached textures hold the other polarity; a new generation retires them.
    ++s.render_generation;
}

bool recent_exists(const std::string& utf8_path) {
    struct Probe { bool exists; Clock::time_point checked; };
    static std::unordered_map<std::string, Probe> probes;
    const auto now = Clock::now();
    auto it = probes.find(utf8_path);
    if (it != probes.end() && now - it->second.checked < std::chrono::seconds(5)) return it->second.exists;
    std::error_code ec;
    const bool exists = std::filesystem::is_regular_file(from_utf8(utf8_path), ec);
    probes[utf8_path] = {exists, now};
    return exists;
}

void reveal_recent(Session& s, const std::string& utf8_path) {
    std::string error;
    if (!host::reveal_in_folder(utf8_path, error)) s.error = error;
}

void go_to_page(Session& s, int page) {
    if (!s.has_document) return;
    page = std::clamp(page, 0, s.view.page_count() - 1);
    s.request_scroll(page);
    s.view.current_page = page;
    s.goto_input = page + 1;
}

int run(int argc, char** argv) {
    SDL_SetMainReady();
    App app;
    if (!app.init(argc, argv)) return 1;
    while (app.frame()) {}
    return app.shutdown();
}

} // namespace davepdf::platform
