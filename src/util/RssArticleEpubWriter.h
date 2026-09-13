#pragma once

#include <RssParser.h>

#include <string>
#include <vector>

/**
 * Assembles a minimal EPUB from a single RSS/Atom article and writes it to
 * destPath (overwriting any existing file there) via ZipWriter.
 *
 * contentHtml is not assumed to be well-formed XHTML (real-world feeds
 * routinely aren't), so it is reduced to plain paragraphs by stripping
 * every tag rather than passed through: this guarantees the generated
 * chapter is well-formed XML regardless of how messy the source feed's
 * markup is, at the cost of losing inline formatting (bold, italics,
 * links) -- still out of scope.
 *
 * This module does no networking of its own: embedding an image means
 * passing the path to an already-downloaded, already-format-validated
 * image file via imagePath/imageIsPng (see the write() doc below).
 * Downloading and validating it is the caller's job (RssArticleListActivity)
 * -- keeping this module to local file assembly only is what lets its
 * pure/local-I/O-only pieces stay host-testable without stubbing out HTTP.
 */
namespace RssArticleEpubWriter {
// imagePath: optional path to an already-downloaded image file to embed as
// the article's hero image; pass "" for a text-only article. imageIsPng:
// true if imagePath is a PNG, false if it's a JPEG -- the only two formats
// the reader's own image pipeline supports (see ImageDecoderFactory);
// ignored when imagePath is empty. The caller is expected to have already
// sniffed the format (e.g. via magic bytes) since determining it requires
// reading the file once, which the caller already did to download it.
bool write(const RssArticle& article, const std::string& destPath, const std::string& imagePath = "",
           bool imageIsPng = false);

// Exposed for host unit testing only -- not part of the public contract
// callers outside this file should rely on.
namespace internal {
std::string xmlEscape(const std::string& s);
std::vector<std::string> htmlToParagraphs(const std::string& html);
}  // namespace internal
}  // namespace RssArticleEpubWriter
