// Console tests for the PDFium wrapper, using the generated sample document.
#include "app/sample_pdf.h"
#include "platform/pdf_engine.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

using namespace davepdf;
using namespace davepdf::platform;

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)
static bool near(float a, float b, float eps = 0.5f) { return std::fabs(a - b) <= eps; }
static bool in_unit(const RectF& r) { return r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= 1.001f && r.y + r.h <= 1.001f; }

int main() {
    std::string error;
    CHECK(pdf_init(error));
    PdfDocument doc;
    CHECK(!doc.is_open());
    CHECK(doc.page_count() == 0);

    // Garbage is rejected with a message and leaves the document closed.
    CHECK(!doc.load_memory("this is not a pdf", "", error));
    CHECK(!error.empty());
    CHECK(!doc.is_open() && !doc.needs_password());

    CHECK(doc.load_memory(make_sample_pdf(), "", error));
    CHECK(error.empty());
    CHECK(doc.is_open());
    CHECK(doc.page_count() == kSamplePageCount);
    const auto sizes = doc.page_sizes();
    CHECK(sizes.size() == 3);
    CHECK(near(sizes[0].width, 612) && near(sizes[0].height, 792));
    CHECK(near(sizes[1].width, 842) && near(sizes[1].height, 595));

    // Rendering: the header band is red at the top, the body is white.
    std::vector<unsigned char> pixels;
    CHECK(doc.render(0, 153, 198, 0, pixels, error));
    CHECK(pixels.size() == 153u * 198u * 4u);
    const auto pixel = [&](int x, int y) { const size_t i = (static_cast<size_t>(y) * 153 + static_cast<size_t>(x)) * 4; return std::tuple<int, int, int>(pixels[i + 2], pixels[i + 1], pixels[i]); };  // BGRA -> r,g,b
    {
        const auto [r, g, b] = pixel(76, 8);
        CHECK(r > 150 && g < 100 && b < 100);
        const auto [r2, g2, b2] = pixel(140, 190);
        CHECK(r2 > 240 && g2 > 240 && b2 > 240);
    }
    // Rotation by 90 degrees puts the band on the right edge.
    CHECK(doc.render(0, 198, 153, 90, pixels, error));
    {
        const size_t i = (static_cast<size_t>(76) * 198 + 190) * 4;
        CHECK(pixels[i + 2] > 150 && pixels[i + 1] < 100);
    }
    CHECK(!doc.render(99, 10, 10, 0, pixels, error));
    CHECK(!doc.render(0, 0, 10, 0, pixels, error));

    // Metadata and outline.
    const DocumentInfo info = doc.metadata();
    CHECK(info.title == kSampleTitle);
    CHECK(info.author == "davepdf");
    CHECK(info.version == "1.4");
    CHECK(info.created == "2026-10-24 09:00");
    CHECK(!info.encrypted);
    const auto outline = doc.outline();
    CHECK(outline.size() == 3);
    if (outline.size() == 3) {
        CHECK(outline[0].title == "Welcome" && outline[0].page == 0 && outline[0].depth == 0);
        CHECK(outline[2].title == "The last page" && outline[2].page == 2);
    }

    // Text layer.
    std::string text;
    CHECK(doc.page_text(0, text, error));
    CHECK(text.find("sample page 1 of 3") != std::string::npos);
    CHECK(text.find(kSampleTitle) != std::string::npos);

    // Search: the title appears once per page; case folding is the default.
    std::vector<SearchHit> hits;
    for (int page = 0; page < 3; ++page) CHECK(doc.search(page, "DAVEPDF SAMPLE", false, hits));
    CHECK(hits.size() == 3);
    bool boxes_ok = !hits.empty();
    for (const auto& hit : hits) for (const auto& r : hit.rects) if (!in_unit(r)) boxes_ok = false;
    CHECK(boxes_ok);
    if (!hits.empty()) {
        // The title sits in the header band near the top left.
        CHECK(hits[0].page == 0 && hits[0].rects[0].y < 0.1f && hits[0].rects[0].x < 0.1f);
    }
    hits.clear();
    CHECK(doc.search(0, "DAVEPDF SAMPLE", true, hits));
    CHECK(hits.empty());
    CHECK(doc.search(2, "landscape", false, hits));
    CHECK(hits.size() == 1);
    hits.clear();
    CHECK(doc.search(0, "", false, hits) && hits.empty());

    // Links on page 1: an internal jump to the last page and an external URI.
    const auto links = doc.links(0);
    CHECK(links.size() == 2);
    bool found_goto = false, found_uri = false;
    for (const auto& link : links) {
        if (link.page == 2) found_goto = in_unit(link.rect);
        if (link.uri == "https://example.com/") found_uri = in_unit(link.rect);
    }
    CHECK(found_goto && found_uri);
    CHECK(doc.links(1).empty());

    // Close and reopen from a file path.
    doc.close();
    CHECK(!doc.is_open());
    const auto path = std::filesystem::temp_directory_path() / "davepdf-pdf-test.pdf";
    { FILE* f = std::fopen(path.string().c_str(), "wb"); CHECK(f != nullptr); if (f) { const std::string bytes = make_sample_pdf(); std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); } }
    CHECK(doc.load_file(path, "", error));
    CHECK(doc.page_count() == 3);
    doc.close();
    std::filesystem::remove(path);
    CHECK(!doc.load_file(std::filesystem::path("C:/definitely/missing/file.pdf"), "", error));
    CHECK(!error.empty());

    pdf_shutdown();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("pdf engine tests passed (PDFium %s)\n", pdfium_version());
    return 0;
}
