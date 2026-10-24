// Console tests for the window-free application logic.
#include "app/recent_tree.h"
#include "app/sample_pdf.h"
#include "app/settings.h"
#include "app/viewer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace davepdf;

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)
static bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

static ViewState three_pages() {
    ViewState v;
    v.pages = {{612, 792}, {842, 595}, {612, 792}};
    v.zoom = 1.0f;
    v.fit = FitMode::Manual;
    v.gap = 10;
    v.margin = 20;
    return v;
}

static void test_zoom() {
    CHECK(near(clamp_zoom(0.0f), 1.0f));
    CHECK(near(clamp_zoom(100.0f), kMaxZoom));
    CHECK(near(clamp_zoom(0.01f), kMinZoom));
    CHECK(near(zoom_step(1.0f, true), 1.1f));
    CHECK(near(zoom_step(1.0f, false), 0.9f));
    CHECK(near(zoom_step(0.999f, true), 1.1f));  // tolerance: a fit zoom just under 1 steps past 1.0
    CHECK(near(zoom_step(16.0f, true), kMaxZoom));
    CHECK(near(zoom_step(0.2f, false), kMinZoom));
    float z = 0.25f;
    for (int i = 0; i < 40; ++i) { const float next = zoom_step(z, true); CHECK(next >= z); z = next; }
    CHECK(near(z, kMaxZoom));
}

static void test_layout() {
    ViewState v = three_pages();
    Layout l = layout_pages(v, 1.0f);
    CHECK(l.pages.size() == 3);
    // 612pt at 96dpi == 816px; the landscape page is 1123px wide and sets the column width.
    CHECK(near(l.pages[0].rect.w, 816));
    CHECK(near(l.pages[1].rect.w, 1123));
    CHECK(near(l.width, 1123 + 40));
    CHECK(near(l.pages[0].rect.y, 20));
    CHECK(near(l.pages[1].rect.y, 20 + 1056 + 10));
    CHECK(near(l.pages[0].rect.x, (l.width - 816) / 2, 0.5f));  // centred, snapped to a whole pixel
    CHECK(near(l.height, l.pages[2].rect.y + l.pages[2].rect.h + 20));
    // Rotation swaps page dimensions.
    v.rotate(1);
    CHECK(v.rotation == 90);
    l = layout_pages(v, 1.0f);
    CHECK(near(l.pages[0].rect.w, 1056));
    CHECK(near(l.pages[0].rect.h, 816));
    v.rotate(-1);
    CHECK(v.rotation == 0);
    v.rotate(-1);
    CHECK(v.rotation == 270);
    // Display scale multiplies pixel sizes but not the margins.
    v.rotation = 0;
    l = layout_pages(v, 2.0f);
    CHECK(near(l.pages[0].rect.w, 1632));
    CHECK(near(l.pages[0].rect.y, 20));
}

static void test_fit() {
    ViewState v = three_pages();
    // Fit width uses the widest page (1123px at 100%): a 1163px viewport minus margins fits it exactly.
    CHECK(near(fit_zoom(v, 1163, 800, 1.0f, FitMode::Width), 1.0f));
    CHECK(near(fit_zoom(v, 1163 / 2.0f + 20, 800, 1.0f, FitMode::Width), 0.5f, 0.01f));
    // Fit page uses the current page; page 0 is 816x1056 so height limits it.
    v.current_page = 0;
    CHECK(near(fit_zoom(v, 2000, 1056 + 40, 1.0f, FitMode::Page), 1.0f));
    CHECK(near(fit_zoom(v, 816 + 40, 5000, 1.0f, FitMode::Page), 1.0f));
    CHECK(near(fit_zoom(v, 2000, 1056 + 40, 1.0f, FitMode::Manual), v.zoom));
    ViewState empty;
    CHECK(near(fit_zoom(empty, 100, 100, 1.0f, FitMode::Width), empty.zoom));
}

static void test_scrolling() {
    ViewState v = three_pages();
    const Layout l = layout_pages(v, 1.0f);
    CHECK(page_at(l, 0, 500) == 0);
    CHECK(page_at(l, l.pages[1].rect.y + 10, 500) == 1);
    // Mostly showing page 2 even though page 1 is still visible at the top.
    CHECK(page_at(l, l.pages[1].rect.y - 100, 500) == 1);
    CHECK(page_at(l, l.height, 500) == 2);
    CHECK(near(scroll_to_page(l, 1, 10), l.pages[1].rect.y - 10));
    CHECK(near(scroll_to_page(l, 0, 10), 10));
    CHECK(near(scroll_to_page(l, 9, 10), 0));
    const auto visible = visible_pages(l, 0, 500, 1);
    CHECK(visible.size() == 2 && visible[0] == 0 && visible[1] == 1);
    const auto all = visible_pages(l, 0, 1e6f, 0);
    CHECK(all.size() == 3);
    CHECK(near(clamp_scroll(-5, 1000, 400), 0));
    CHECK(near(clamp_scroll(5000, 1000, 400), 600));
    CHECK(near(clamp_scroll(100, 300, 400), 0));
    // Doubling the zoom with the pointer 100px into a view scrolled 300px keeps that point still.
    CHECK(near(scroll_after_zoom(300, 100, 1.0f, 2.0f), 700));
    CHECK(near(scroll_after_zoom(700, 100, 2.0f, 1.0f), 300));
}

static void test_rotation_of_boxes() {
    const RectF r{0.1f, 0.2f, 0.3f, 0.1f};
    const RectF r90 = rotate_normalized(r, 90);
    CHECK(near(r90.x, 0.7f) && near(r90.y, 0.1f) && near(r90.w, 0.1f) && near(r90.h, 0.3f));
    const RectF r180 = rotate_normalized(r, 180);
    CHECK(near(r180.x, 0.6f) && near(r180.y, 0.7f) && near(r180.w, 0.3f) && near(r180.h, 0.1f));
    const RectF r270 = rotate_normalized(r, 270);
    CHECK(near(r270.x, 0.2f) && near(r270.y, 0.6f) && near(r270.w, 0.1f) && near(r270.h, 0.3f));
    // Four quarter turns are the identity.
    RectF q = r;
    for (int i = 0; i < 4; ++i) q = rotate_normalized(q, 90);
    CHECK(near(q.x, r.x) && near(q.y, r.y) && near(q.w, r.w) && near(q.h, r.h));
}

static void test_settings() {
    Settings s;
    s.theme = 2; s.scale = 1.25f; s.show_sidebar = false; s.sidebar_tab = 1; s.show_properties = true;
    s.invert_pages = true; s.fit = 2;
    s.recent = {"C:/docs/a.pdf", "D:/b c.pdf"};
    const Settings back = parse_settings(serialize_settings(s));
    CHECK(back.theme == 2);
    CHECK(back.invert_pages && back.fit == 2);
    s.layout_version = 2;
    CHECK(parse_settings(serialize_settings(s)).layout_version == 2);
    CHECK(parse_settings("theme=0\n").layout_version == 0);  // older saves have no version
    CHECK(parse_settings("fit=7\n").fit == 1);
    CHECK(near(back.scale, 1.25f));
    CHECK(!back.show_sidebar && back.sidebar_tab == 1 && back.show_properties);
    CHECK(back.recent.size() == 2 && back.recent[1] == "D:/b c.pdf");
    const Settings garbage = parse_settings("theme=abc\r\nscale=-4\nnonsense\n=\nrecent=\n");
    CHECK(garbage.theme == 0 && near(garbage.scale, 1.0f) && garbage.recent.empty());
    std::vector<std::string> recent;
    for (int i = 0; i < 15; ++i) add_recent(recent, "f" + std::to_string(i));
    CHECK(recent.size() == kMaxRecentFiles && recent.front() == "f14");
    add_recent(recent, "f10");
    CHECK(recent.front() == "f10" && recent.size() == kMaxRecentFiles);
    remove_recent(recent, "f10");
    CHECK(recent.size() == kMaxRecentFiles - 1 && recent.front() == "f14");
    add_recent(recent, "");
    CHECK(recent.size() == kMaxRecentFiles - 1);
}

static void test_recent_tree() {
    const std::vector<std::string> recent = {
        "C:\\Users\\dave\\Documents\\b.pdf",
        "C:\\Users\\dave\\Documents\\a.pdf",
        "D:\\scans\\deep\\er\\x.pdf",
        "C:\\Users\\dave\\Desktop\\c.pdf",
        "\\\\server\\share\\s.pdf",
    };
    const RecentNode root = build_recent_tree(recent);
    // Top level: drives and the UNC server, sorted by label (a backslash sorts after letters).
    CHECK(root.children.size() == 3);
    if (root.children.size() != 3) return;
    CHECK(root.children[0].label == "C:\\Users\\dave");  // single-child chain collapsed
    CHECK(root.children[1].label == "D:\\scans\\deep\\er");
    CHECK(root.children[2].label == "\\\\server\\share");
    const RecentNode& dave = root.children[0];
    CHECK(dave.path == "C:\\Users\\dave");
    CHECK(dave.children.size() == 2);
    if (dave.children.size() != 2) return;
    CHECK(dave.children[0].label == "Desktop" && dave.children[1].label == "Documents");
    const RecentNode& docs = dave.children[1];
    // Files keep recent order (b before a) and carry their index.
    CHECK(docs.children.size() == 2);
    if (docs.children.size() != 2) return;
    CHECK(docs.children[0].label == "b.pdf" && docs.children[0].recent_index == 0 && docs.children[1].recent_index == 1);
    CHECK(docs.children[0].is_file && docs.children[0].path == recent[0]);
    CHECK(root.children[1].children.size() == 1 && root.children[1].children[0].label == "x.pdf");
    CHECK(root.children[2].children.size() == 1 && root.children[2].children[0].label == "s.pdf");
    CHECK(build_recent_tree({}).children.empty());
    CHECK(build_recent_tree({"loose.pdf"}).children.size() == 1 && build_recent_tree({"loose.pdf"}).children[0].is_file);
    CHECK(recent_file_name("C:/x/y.pdf") == "y.pdf" && recent_directory("C:\\x\\y.pdf") == "C:\\x");
}

static void test_sample_pdf() {
    const std::string pdf = make_sample_pdf();
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.size() > 2000);
    CHECK(pdf.find("%%EOF") != std::string::npos);
    // The cross-reference offset must land on the xref keyword.
    const auto start = pdf.rfind("startxref");
    CHECK(start != std::string::npos);
    const size_t offset = static_cast<size_t>(std::atoll(pdf.c_str() + start + 9));
    CHECK(pdf.compare(offset, 4, "xref") == 0);
    // Every object offset in the table points at "N 0 obj".
    const auto table = pdf.find("xref\n0 ");
    const int count = std::atoi(pdf.c_str() + table + 7);
    CHECK(count > 10);
    size_t pos = pdf.find('\n', table + 5) + 1;
    pos = pdf.find('\n', pos) + 1;  // skip the free entry
    bool offsets_ok = true;
    for (int i = 1; i < count; ++i) {
        const size_t object_offset = static_cast<size_t>(std::atoll(pdf.c_str() + pos));
        const std::string expected = std::to_string(i) + " 0 obj";
        if (pdf.compare(object_offset, expected.size(), expected) != 0) offsets_ok = false;
        pos += 20;
    }
    CHECK(offsets_ok);
    CHECK(pdf.find(kSampleTitle) != std::string::npos);
    CHECK(make_sample_pdf() == pdf);  // deterministic
}

int main() {
    test_zoom();
    test_layout();
    test_fit();
    test_scrolling();
    test_rotation_of_boxes();
    test_settings();
    test_recent_tree();
    test_sample_pdf();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("app logic tests passed\n");
    return 0;
}
