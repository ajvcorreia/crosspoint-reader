#pragma once

#include <RssParser.h>

#include <string>
#include <vector>

/**
 * Assembles a minimal, text-only EPUB from a single RSS/Atom article and
 * writes it to destPath (overwriting any existing file there) via ZipWriter.
 *
 * "Text-only": article.imageUrl and any <img> tags inside contentHtml are
 * ignored -- downloading/embedding images is a later phase. contentHtml
 * itself is not assumed to be well-formed XHTML (real-world feeds routinely
 * aren't), so it is reduced to plain paragraphs by stripping every tag
 * rather than passed through: this guarantees the generated chapter is
 * well-formed XML regardless of how messy the source feed's markup is, at
 * the cost of losing inline formatting (bold, italics, links) for now.
 */
namespace RssArticleEpubWriter {
bool write(const RssArticle& article, const std::string& destPath);

// Exposed for host unit testing only -- not part of the public contract
// callers outside this file should rely on.
namespace internal {
std::string xmlEscape(const std::string& s);
std::vector<std::string> htmlToParagraphs(const std::string& html);
}  // namespace internal
}  // namespace RssArticleEpubWriter
