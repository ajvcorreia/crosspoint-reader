#include "RssArticleEpubWriter.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>

std::string RssArticleEpubWriter::internal::xmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// Strips HTML tags from `html`, splitting the surviving text into
// paragraphs wherever a block-level tag (open or close) appeared. Inline
// tags (b, i, a, img, span, ...) are dropped along with their attributes;
// any text they wrapped flows into the surrounding paragraph. Deliberately
// lossy -- see the header comment on why contentHtml is never passed
// through as markup.
std::vector<std::string> RssArticleEpubWriter::internal::htmlToParagraphs(const std::string& html) {
  static const std::vector<std::string> blockTags = {"p", "div", "br", "li", "blockquote", "h1",
                                                       "h2", "h3", "h4", "h5", "h6"};

  std::vector<std::string> paragraphs;
  std::string current;

  const auto flush = [&]() {
    const size_t start = current.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) {
      const size_t end = current.find_last_not_of(" \t\r\n");
      paragraphs.push_back(current.substr(start, end - start + 1));
    }
    current.clear();
  };

  size_t i = 0;
  const size_t n = html.size();
  while (i < n) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string::npos) break;  // malformed trailing tag -- stop here

      size_t tagStart = i + 1;
      if (tagStart < close && html[tagStart] == '/') tagStart++;
      size_t tagEnd = tagStart;
      while (tagEnd < close && html[tagEnd] != ' ' && html[tagEnd] != '\t' && html[tagEnd] != '\n' &&
             html[tagEnd] != '/') {
        tagEnd++;
      }
      std::string tagName = html.substr(tagStart, tagEnd - tagStart);
      std::transform(tagName.begin(), tagName.end(), tagName.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      if (std::find(blockTags.begin(), blockTags.end(), tagName) != blockTags.end()) flush();

      i = close + 1;
      continue;
    }
    current += html[i];
    i++;
  }
  flush();
  return paragraphs;
}

namespace {

using RssArticleEpubWriter::internal::htmlToParagraphs;
using RssArticleEpubWriter::internal::xmlEscape;

std::string buildChapterBody(const RssArticle& article, const std::string& escapedTitle,
                             const std::string& imageHref) {
  std::string body;
  body += "<h1>" + escapedTitle + "</h1>\n";
  if (!imageHref.empty()) body += "<p><img src=\"" + imageHref + "\" alt=\"\"/></p>\n";

  std::string byline;
  if (!article.author.empty()) byline += article.author;
  if (!article.publishedAt.empty()) {
    if (!byline.empty()) byline += " -- ";
    byline += article.publishedAt;
  }
  if (!byline.empty()) body += "<p><em>" + xmlEscape(byline) + "</em></p>\n";

  for (const auto& paragraph : htmlToParagraphs(article.contentHtml)) {
    body += "<p>" + xmlEscape(paragraph) + "</p>\n";
  }

  if (!article.link.empty()) body += "<p><em>" + xmlEscape(article.link) + "</em></p>\n";

  return body;
}

std::string buildChapterXhtml(const std::string& body) {
  std::string xhtml;
  xhtml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xhtml += "<!DOCTYPE html>\n";
  xhtml += "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n";
  xhtml += "<head><title>Article</title></head>\n";
  xhtml += "<body>\n";
  xhtml += body;
  xhtml += "</body>\n</html>\n";
  return xhtml;
}

constexpr const char* CONTAINER_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
    "  <rootfiles>\n"
    "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
    "  </rootfiles>\n"
    "</container>\n";

// 1-based chapter/image numbering in filenames, since that's how they're
// presented in nav.xhtml/content.opf and a reader's Contents panel.
std::string chapterFileName(const size_t index) {
  char buf[24];
  snprintf(buf, sizeof(buf), "chapter%zu.xhtml", index + 1);
  return buf;
}

std::string imageFileName(const size_t index, const bool isPng) {
  char buf[32];
  snprintf(buf, sizeof(buf), "images/hero%zu.%s", index + 1, isPng ? "png" : "jpg");
  return buf;
}

}  // namespace

RssArticleEpubWriter::FeedBuilder::FeedBuilder() = default;
RssArticleEpubWriter::FeedBuilder::~FeedBuilder() = default;

bool RssArticleEpubWriter::FeedBuilder::begin(const std::string& destPathIn, const std::string& feedTitle) {
  destPath = destPathIn;
  // Mirrors PersistableStoreBase::writeDocToFile's own mkdir-before-write:
  // called unconditionally and its result ignored -- the directory almost
  // always exists already (OPDS/RSS/settings all create it first), and
  // mkdir on an existing directory is harmless.
  const size_t lastSlash = destPath.find_last_of('/');
  if (lastSlash != std::string::npos && lastSlash > 0) {
    Storage.mkdir(destPath.substr(0, lastSlash).c_str());
  }

  feedTitleEscaped = xmlEscape(feedTitle.empty() ? "RSS Feed" : feedTitle);
  chapters.clear();

  zip = std::make_unique<ZipWriter>(destPath);
  ok = zip->open();
  ok = ok && zip->addEntry("mimetype", "application/epub+zip");
  ok = ok && zip->addEntry("META-INF/container.xml", CONTAINER_XML);
  return ok;
}

bool RssArticleEpubWriter::FeedBuilder::addArticle(const RssArticle& article, const std::string& imagePath,
                                                   const bool imageIsPng) {
  if (!ok || !zip) return false;

  const size_t index = chapters.size();
  const std::string escapedTitle = xmlEscape(article.title.empty() ? article.link : article.title);

  std::string imageHref;
  if (!imagePath.empty()) {
    const std::string href = imageFileName(index, imageIsPng);
    if (zip->addEntryFromFile("OEBPS/" + href, imagePath)) {
      imageHref = href;
    }
    // A failed image embed (e.g. a since-removed temp file) degrades this
    // chapter to text-only rather than failing it or the rest of the book
    // -- see ZipWriter::addEntryFromFile()'s note on why an unreadable
    // source doesn't poison the archive the way a broken write does.
  }

  const std::string body = buildChapterBody(article, escapedTitle, imageHref);
  ok = zip->addEntry("OEBPS/" + chapterFileName(index), buildChapterXhtml(body));
  if (!ok) return false;

  chapters.push_back({escapedTitle, imageHref, imageIsPng});
  return true;
}

bool RssArticleEpubWriter::FeedBuilder::finish() {
  if (!ok || !zip) {
    if (zip) zip->close();
    return false;
  }

  std::string opf;
  opf += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  opf += "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n";
  opf += "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
  opf += "    <dc:identifier id=\"uid\">crosspoint-rss-feed</dc:identifier>\n";
  opf += "    <dc:title>" + feedTitleEscaped + "</dc:title>\n";
  opf += "    <dc:language>en</dc:language>\n";
  opf += "  </metadata>\n";
  opf += "  <manifest>\n";
  opf += "    <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n";
  for (size_t i = 0; i < chapters.size(); i++) {
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "chapter%zu", i + 1);
    opf += "    <item id=\"" + std::string(idBuf) + "\" href=\"" + chapterFileName(i) +
           "\" media-type=\"application/xhtml+xml\"/>\n";
    if (!chapters[i].imageHref.empty()) {
      char imgIdBuf[24];
      snprintf(imgIdBuf, sizeof(imgIdBuf), "image%zu", i + 1);
      opf += "    <item id=\"" + std::string(imgIdBuf) + "\" href=\"" + chapters[i].imageHref + "\" media-type=\"" +
             (chapters[i].imageIsPng ? "image/png" : "image/jpeg") + "\"/>\n";
    }
  }
  opf += "  </manifest>\n";
  opf += "  <spine>\n";
  for (size_t i = 0; i < chapters.size(); i++) {
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "chapter%zu", i + 1);
    opf += "    <itemref idref=\"" + std::string(idBuf) + "\"/>\n";
  }
  opf += "  </spine>\n";
  opf += "</package>\n";

  std::string nav;
  nav += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  nav += "<!DOCTYPE html>\n";
  nav += "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n";
  nav += "<head><title>Navigation</title></head>\n";
  nav += "<body>\n";
  nav += "  <nav epub:type=\"toc\">\n";
  nav += "    <h1>Contents</h1>\n";
  nav += "    <ol>\n";
  for (size_t i = 0; i < chapters.size(); i++) {
    nav += "      <li><a href=\"" + chapterFileName(i) + "\">" + chapters[i].escapedTitle + "</a></li>\n";
  }
  nav += "    </ol>\n";
  nav += "  </nav>\n";
  nav += "</body>\n";
  nav += "</html>\n";

  ok = ok && zip->addEntry("OEBPS/content.opf", opf);
  ok = ok && zip->addEntry("OEBPS/nav.xhtml", nav);
  ok = zip->close() && ok;

  if (!ok) {
    LOG_ERR("RSS", "Failed to write feed EPUB to %s", destPath.c_str());
    Storage.remove(destPath.c_str());  // don't leave a corrupt partial file behind
    return false;
  }
  return true;
}
