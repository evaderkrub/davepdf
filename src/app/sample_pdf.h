#pragma once
// A small PDF generated in memory. It gives the application something to show
// without a file at hand and gives the tests a document whose contents are
// known exactly: page sizes, text, outline, links and metadata.
#include <string>

namespace davepdf {

inline constexpr int kSamplePageCount = 3;
inline constexpr const char* kSampleTitle = "davepdf sample document";

// Returns the complete file bytes of a valid PDF 1.4 document.
std::string make_sample_pdf();

} // namespace davepdf
