#pragma once
// The one place PDFium is spoken to. Everything comes back as plain app
// structs (page sizes, pixels, outline, hits) so the rest of the program
// never includes an fpdf header and could be rebuilt on another engine.
#include "app/session.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace davepdf::platform {

// Process-wide library state. PDFium is not thread safe: every call on a
// document, from any thread, must hold that document's mutex() first. The
// methods below do not lock themselves so a caller can group several calls
// (or try_lock and skip a frame when the render thread is busy).
bool pdf_init(std::string& error);
void pdf_shutdown();
const char* pdfium_version();

class PdfDocument {
public:
    PdfDocument();
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    std::mutex& mutex() const;

    // Both loaders keep the file bytes in memory for the document's lifetime,
    // which sidesteps PDFium's narrow-path file API on Windows.
    bool load_file(const std::filesystem::path& path, const std::string& password, std::string& error);
    bool load_memory(std::string bytes, const std::string& password, std::string& error);
    void close();
    bool is_open() const;
    // True after a failed load that would succeed with the right password.
    bool needs_password() const;

    int page_count() const;
    std::vector<PageInfo> page_sizes() const;
    // Renders a page onto white at exactly width x height pixels, rotated by
    // 0/90/180/270 degrees clockwise. bgra receives width*height*4 bytes.
    bool render(int page, int width, int height, int rotation, std::vector<unsigned char>& bgra, std::string& error);
    std::vector<OutlineItem> outline() const;
    // Fills the metadata fields; path, file_name and bytes are the caller's.
    DocumentInfo metadata() const;
    std::vector<LinkBox> links(int page);
    bool page_text(int page, std::string& utf8, std::string& error);
    // Appends every occurrence on the page, in reading order.
    bool search(int page, const std::string& utf8, bool match_case, std::vector<SearchHit>& out);
    // Text selection support. Positions are normalised to the unrotated page.
    // Returns -1 when no character is within tolerance of the point.
    int char_index_at(int page, float nx, float ny);
    int char_count(int page);
    std::vector<RectF> text_rects(int page, int start, int count);
    std::string text_range(int page, int start, int count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace davepdf::platform
