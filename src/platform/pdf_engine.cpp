#include "platform/pdf_engine.h"
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

namespace davepdf::platform {
namespace {

bool g_initialized = false;

// PDFium hands strings back as UTF-16LE with a terminating null; the rest of
// the program is UTF-8 throughout.
std::string utf16_to_utf8(const unsigned short* text, size_t units) {
    std::string out;
    out.reserve(units);
    for (size_t i = 0; i < units; ++i) {
        unsigned int c = text[i];
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < units && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::vector<unsigned short> utf8_to_utf16(const std::string& text) {
    std::vector<unsigned short> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        unsigned int c = b;
        size_t extra = 0;
        if (b >= 0xF0) { c = b & 0x07; extra = 3; }
        else if (b >= 0xE0) { c = b & 0x0F; extra = 2; }
        else if (b >= 0xC0) { c = b & 0x1F; extra = 1; }
        if (i + extra >= text.size() + (extra ? 0 : 1)) { extra = 0; c = b; }
        for (size_t k = 1; k <= extra; ++k) c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += extra + 1;
        if (c >= 0x10000) { c -= 0x10000; out.push_back(static_cast<unsigned short>(0xD800 + (c >> 10))); out.push_back(static_cast<unsigned short>(0xDC00 + (c & 0x3FF))); }
        else out.push_back(static_cast<unsigned short>(c));
    }
    out.push_back(0);
    return out;
}

// Calls a PDFium "get text" entry point twice: once for the length, once for the data.
template <class Getter>
std::string read_wide(Getter get) {
    const unsigned long bytes = get(nullptr, 0);
    if (bytes <= 2) return {};
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    get(buffer.data(), bytes);
    return utf16_to_utf8(buffer.data(), bytes / 2);
}

std::string describe_error(unsigned long code) {
    switch (code) {
    case FPDF_ERR_FILE: return "The file could not be opened.";
    case FPDF_ERR_FORMAT: return "This is not a PDF file, or the file is damaged.";
    case FPDF_ERR_PASSWORD: return "A password is required to open this document.";
    case FPDF_ERR_SECURITY: return "The document uses a security scheme this viewer does not support.";
    case FPDF_ERR_PAGE: return "A page in the document could not be read.";
    default: return "PDFium could not open the document (error " + std::to_string(code) + ").";
    }
}

// "D:20261024090000Z" -> "2026-10-24 09:00". Anything else is shown as is.
std::string pretty_date(const std::string& raw) {
    std::string s = raw;
    if (s.rfind("D:", 0) == 0) s.erase(0, 2);
    if (s.size() < 8) return raw;
    for (size_t i = 0; i < 8; ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return raw;
    std::string out = s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2);
    if (s.size() >= 12) out += " " + s.substr(8, 2) + ":" + s.substr(10, 2);
    return out;
}

// Device space here is a fixed grid over the unrotated page so results are
// resolution independent; the view scales them to whatever it draws.
constexpr int kGrid = 4096;

RectF normalize(FPDF_PAGE page, double left, double top, double right, double bottom) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    FPDF_PageToDevice(page, 0, 0, kGrid, kGrid, 0, left, top, &x1, &y1);
    FPDF_PageToDevice(page, 0, 0, kGrid, kGrid, 0, right, bottom, &x2, &y2);
    RectF r;
    r.x = static_cast<float>(std::min(x1, x2)) / kGrid;
    r.y = static_cast<float>(std::min(y1, y2)) / kGrid;
    r.w = static_cast<float>(std::abs(x2 - x1)) / kGrid;
    r.h = static_cast<float>(std::abs(y2 - y1)) / kGrid;
    return r;
}

} // namespace

bool pdf_init(std::string& error) {
    if (g_initialized) return true;
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
    g_initialized = true;
    error.clear();
    return true;
}

void pdf_shutdown() {
    if (!g_initialized) return;
    FPDF_DestroyLibrary();
    g_initialized = false;
}

const char* pdfium_version() {
#ifdef DAVEPDF_PDFIUM_VERSION
    return DAVEPDF_PDFIUM_VERSION;
#else
    return "unknown";
#endif
}

struct PdfDocument::Impl {
    std::string bytes;
    FPDF_DOCUMENT doc = nullptr;
    bool needs_password = false;
    // Pages stay loaded while they are being drawn, searched or hovered. A
    // small cap keeps memory bounded on thousand-page documents.
    std::map<int, FPDF_PAGE> pages;
    std::map<int, FPDF_TEXTPAGE> text_pages;

    void drop_pages() {
        for (auto& [index, text] : text_pages) FPDFText_ClosePage(text);
        text_pages.clear();
        for (auto& [index, page] : pages) FPDF_ClosePage(page);
        pages.clear();
    }

    FPDF_PAGE page(int index) {
        if (!doc || index < 0 || index >= FPDF_GetPageCount(doc)) return nullptr;
        auto it = pages.find(index);
        if (it != pages.end()) return it->second;
        if (pages.size() >= 16) drop_pages();
        FPDF_PAGE p = FPDF_LoadPage(doc, index);
        if (p) pages[index] = p;
        return p;
    }

    FPDF_TEXTPAGE text_page(int index) {
        auto it = text_pages.find(index);
        if (it != text_pages.end()) return it->second;
        FPDF_PAGE p = page(index);
        if (!p) return nullptr;
        FPDF_TEXTPAGE t = FPDFText_LoadPage(p);
        if (t) text_pages[index] = t;
        return t;
    }
};

PdfDocument::PdfDocument() : impl_(std::make_unique<Impl>()) {}
PdfDocument::~PdfDocument() { close(); }

bool PdfDocument::load_file(const std::filesystem::path& path, const std::string& password, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) { error = "The file could not be opened."; return false; }
    const auto length = stream.tellg();
    if (length < 0) { error = "The file could not be read."; return false; }
    std::string bytes(static_cast<size_t>(length), '\0');
    stream.seekg(0);
    if (length > 0) stream.read(bytes.data(), length);
    if (!stream) { error = "The file could not be read."; return false; }
    return load_memory(std::move(bytes), password, error);
}

bool PdfDocument::load_memory(std::string bytes, const std::string& password, std::string& error) {
    close();
    if (!g_initialized) { error = "PDFium is not initialised."; return false; }
    impl_->bytes = std::move(bytes);
    impl_->doc = FPDF_LoadMemDocument64(impl_->bytes.data(), impl_->bytes.size(), password.empty() ? nullptr : password.c_str());
    if (!impl_->doc) {
        const unsigned long code = FPDF_GetLastError();
        impl_->needs_password = code == FPDF_ERR_PASSWORD;
        error = describe_error(code);
        impl_->bytes.clear();
        return false;
    }
    impl_->needs_password = false;
    error.clear();
    return true;
}

void PdfDocument::close() {
    impl_->drop_pages();
    if (impl_->doc) FPDF_CloseDocument(impl_->doc);
    impl_->doc = nullptr;
    impl_->bytes.clear();
    impl_->bytes.shrink_to_fit();
}

bool PdfDocument::is_open() const { return impl_->doc != nullptr; }
bool PdfDocument::needs_password() const { return impl_->needs_password; }
int PdfDocument::page_count() const { return impl_->doc ? FPDF_GetPageCount(impl_->doc) : 0; }

std::vector<PageInfo> PdfDocument::page_sizes() const {
    std::vector<PageInfo> sizes;
    const int count = page_count();
    for (int i = 0; i < count; ++i) {
        FS_SIZEF size{};
        PageInfo info{612, 792};  // US Letter stands in for a page PDFium cannot measure
        if (FPDF_GetPageSizeByIndexF(impl_->doc, i, &size) && size.width > 0 && size.height > 0) info = {size.width, size.height};
        sizes.push_back(info);
    }
    return sizes;
}

bool PdfDocument::render(int index, int width, int height, int rotation, std::vector<unsigned char>& bgra, std::string& error) {
    FPDF_PAGE page = impl_->page(index);
    if (!page) { error = "The page could not be loaded."; return false; }
    if (width < 1 || height < 1) { error = "Invalid render size."; return false; }
    bgra.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0xFF);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA, bgra.data(), width * 4);
    if (!bitmap) { error = "Out of memory while rendering."; return false; }
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFFu);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, ((rotation / 90) % 4 + 4) % 4, FPDF_ANNOT);
    FPDFBitmap_Destroy(bitmap);
    return true;
}

namespace {
void walk_outline(FPDF_DOCUMENT doc, FPDF_BOOKMARK bookmark, int depth, std::vector<OutlineItem>& out) {
    for (; bookmark && out.size() < 20000; bookmark = FPDFBookmark_GetNextSibling(doc, bookmark)) {
        OutlineItem item;
        item.depth = depth;
        item.title = read_wide([&](void* buffer, unsigned long length) { return FPDFBookmark_GetTitle(bookmark, buffer, length); });
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bookmark);
        if (!dest) {
            FPDF_ACTION action = FPDFBookmark_GetAction(bookmark);
            if (action && FPDFAction_GetType(action) == PDFACTION_GOTO) dest = FPDFAction_GetDest(doc, action);
        }
        if (dest) item.page = FPDFDest_GetDestPageIndex(doc, dest);
        out.push_back(item);
        // Malformed files can nest bookmarks in a cycle; a depth cap ends it.
        if (depth < 32) walk_outline(doc, FPDFBookmark_GetFirstChild(doc, bookmark), depth + 1, out);
    }
}
} // namespace

std::vector<OutlineItem> PdfDocument::outline() const {
    std::vector<OutlineItem> items;
    if (impl_->doc) walk_outline(impl_->doc, FPDFBookmark_GetFirstChild(impl_->doc, nullptr), 0, items);
    return items;
}

DocumentInfo PdfDocument::metadata() const {
    DocumentInfo info;
    if (!impl_->doc) return info;
    const auto meta = [&](const char* tag) {
        return read_wide([&](void* buffer, unsigned long length) { return FPDF_GetMetaText(impl_->doc, tag, buffer, length); });
    };
    info.title = meta("Title");
    info.author = meta("Author");
    info.subject = meta("Subject");
    info.keywords = meta("Keywords");
    info.creator = meta("Creator");
    info.producer = meta("Producer");
    info.created = pretty_date(meta("CreationDate"));
    info.modified = pretty_date(meta("ModDate"));
    int version = 0;
    if (FPDF_GetFileVersion(impl_->doc, &version) && version > 0) info.version = std::to_string(version / 10) + "." + std::to_string(version % 10);
    info.encrypted = FPDF_GetSecurityHandlerRevision(impl_->doc) != -1;
    return info;
}

std::vector<LinkBox> PdfDocument::links(int index) {
    std::vector<LinkBox> boxes;
    FPDF_PAGE page = impl_->page(index);
    if (!page) return boxes;
    int position = 0;
    FPDF_LINK link = nullptr;
    while (FPDFLink_Enumerate(page, &position, &link) && boxes.size() < 10000) {
        FS_RECTF rect{};
        if (!FPDFLink_GetAnnotRect(link, &rect)) continue;
        LinkBox box;
        box.rect = normalize(page, rect.left, rect.top, rect.right, rect.bottom);
        FPDF_DEST dest = FPDFLink_GetDest(impl_->doc, link);
        if (!dest) {
            FPDF_ACTION action = FPDFLink_GetAction(link);
            if (action) {
                const unsigned long type = FPDFAction_GetType(action);
                if (type == PDFACTION_GOTO) dest = FPDFAction_GetDest(impl_->doc, action);
                else if (type == PDFACTION_URI) {
                    const unsigned long length = FPDFAction_GetURIPath(impl_->doc, action, nullptr, 0);
                    if (length > 1) {
                        std::string uri(length, '\0');
                        FPDFAction_GetURIPath(impl_->doc, action, uri.data(), length);
                        uri.resize(strnlen(uri.c_str(), length));
                        box.uri = uri;
                    }
                }
            }
        }
        if (dest) box.page = FPDFDest_GetDestPageIndex(impl_->doc, dest);
        if (box.page >= 0 || !box.uri.empty()) boxes.push_back(box);
    }
    return boxes;
}

bool PdfDocument::page_text(int index, std::string& utf8, std::string& error) {
    FPDF_TEXTPAGE text = impl_->text_page(index);
    if (!text) { error = "The page text could not be loaded."; return false; }
    const int count = FPDFText_CountChars(text);
    utf8.clear();
    if (count <= 0) return true;
    std::vector<unsigned short> buffer(static_cast<size_t>(count) + 1, 0);
    const int written = FPDFText_GetText(text, 0, count, buffer.data());
    utf8 = utf16_to_utf8(buffer.data(), written > 0 ? static_cast<size_t>(written) : 0);
    return true;
}

bool PdfDocument::search(int index, const std::string& needle, bool match_case, std::vector<SearchHit>& out) {
    if (needle.empty()) return true;
    FPDF_TEXTPAGE text = impl_->text_page(index);
    FPDF_PAGE page = impl_->page(index);
    if (!text || !page) return false;
    const auto wide = utf8_to_utf16(needle);
    FPDF_SCHHANDLE handle = FPDFText_FindStart(text, wide.data(), match_case ? FPDF_MATCHCASE : 0, 0);
    if (!handle) return false;
    while (FPDFText_FindNext(handle)) {
        SearchHit hit;
        hit.page = index;
        const int start = FPDFText_GetSchResultIndex(handle), count = FPDFText_GetSchCount(handle);
        const int rects = FPDFText_CountRects(text, start, count);
        for (int i = 0; i < rects; ++i) {
            double left = 0, top = 0, right = 0, bottom = 0;
            if (FPDFText_GetRect(text, i, &left, &top, &right, &bottom)) hit.rects.push_back(normalize(page, left, top, right, bottom));
        }
        if (!hit.rects.empty()) out.push_back(std::move(hit));
    }
    FPDFText_FindClose(handle);
    return true;
}

} // namespace davepdf::platform
