#pragma once

#include <RssParser.h>
#include <ZipWriter.h>

#include <memory>
#include <string>
#include <vector>

/**
 * Assembles a single EPUB containing one chapter per RSS/Atom article, via
 * ZipWriter. Replaces an earlier one-file-per-article design (chained
 * together through the reader's folder-scan "Continue with..." mechanism):
 * a combined book gets the same "keep reading forward" behavior for free
 * from the reader's own chapter navigation (turning the page past a
 * chapter's last page moves to the next chapter; the Contents/TOC panel
 * jumps to any of them directly), with no reader-side cooperation needed at
 * all -- unlike the old design, which had to teach the shared EpubReaderActivity
 * to recognize an RSS-specific scratch folder.
 *
 * contentHtml is not assumed to be well-formed XHTML (real-world feeds
 * routinely aren't), so each chapter's body is reduced to plain paragraphs
 * by stripping every tag rather than passed through: this guarantees the
 * generated chapter is well-formed XML regardless of how messy the source
 * feed's markup is, at the cost of losing inline formatting (bold, italics,
 * links) -- still out of scope.
 *
 * This module does no networking of its own: embedding a chapter's image
 * means passing the path to an already-downloaded, already-format-validated
 * image file to addArticle(). Downloading and validating it is the caller's
 * job (RssArticleListActivity) -- keeping this module to local file assembly
 * only is what lets its pure/local-I/O-only pieces stay host-testable
 * without stubbing out HTTP.
 */
namespace RssArticleEpubWriter {

/**
 * Driven incrementally (begin/addArticle.../finish) rather than given a
 * whole article list up front, so the caller can show real per-article
 * progress (which article, which step) while it downloads each one's image
 * in between addArticle() calls.
 */
class FeedBuilder {
 public:
  FeedBuilder();
  ~FeedBuilder();

  FeedBuilder(const FeedBuilder&) = delete;
  FeedBuilder& operator=(const FeedBuilder&) = delete;

  // Creates (overwriting) destPath and prepares to receive chapters.
  // feedTitle becomes the book's dc:title (typically the RSS feed's own
  // name); an empty title falls back to a generic placeholder.
  bool begin(const std::string& destPath, const std::string& feedTitle);

  // Appends one chapter. Call once per article, in the order they should
  // appear in the book's spine/table of contents.
  //
  // imagePath: optional path to an already-downloaded image file to embed
  // as this chapter's hero image; pass "" for a text-only chapter.
  // imageIsPng: true if imagePath is a PNG, false if it's a JPEG -- the
  // only two formats the reader's own image pipeline supports (see
  // ImageDecoderFactory); ignored when imagePath is empty. The caller is
  // expected to have already sniffed the format (e.g. via magic bytes)
  // since determining it requires reading the file once, which the caller
  // already did to download it. An unreadable imagePath degrades this
  // chapter to text-only rather than failing it (or the rest of the book).
  //
  // Returns false only on a genuine archive-write failure (not an image
  // read failure, which degrades gracefully instead) -- once that happens,
  // the archive itself is no longer trustworthy, so stop calling
  // addArticle()/finish() and discard destPath.
  bool addArticle(const RssArticle& article, const std::string& imagePath = "", bool imageIsPng = false);

  // Writes content.opf/nav.xhtml (now that every chapter is known) and
  // closes the archive. Call exactly once, after all addArticle() calls.
  bool finish();

 private:
  struct ChapterMeta {
    std::string escapedTitle;
    std::string imageHref;  // "" if this chapter has no image
    bool imageIsPng = false;
  };

  std::unique_ptr<ZipWriter> zip;
  std::string destPath;
  std::string feedTitleEscaped;
  std::vector<ChapterMeta> chapters;
  bool ok = false;
};

// Exposed for host unit testing only -- not part of the public contract
// callers outside this file should rely on.
namespace internal {
std::string xmlEscape(const std::string& s);
std::vector<std::string> htmlToParagraphs(const std::string& html);
}  // namespace internal
}  // namespace RssArticleEpubWriter
