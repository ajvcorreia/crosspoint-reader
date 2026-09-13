#include "RssArticleEpubWriter.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ZipWriter.h>

#include <algorithm>
#include <cctype>
#include <vector>

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

std::string buildChapterBody(const RssArticle& article, const std::string& escapedTitle) {
  std::string body;
  body += "<h1>" + escapedTitle + "</h1>\n";

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

std::string buildContentOpf(const std::string& escapedTitle) {
  std::string opf;
  opf += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  opf += "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n";
  opf += "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
  opf += "    <dc:identifier id=\"uid\">crosspoint-rss-article</dc:identifier>\n";
  opf += "    <dc:title>" + escapedTitle + "</dc:title>\n";
  opf += "    <dc:language>en</dc:language>\n";
  opf += "  </metadata>\n";
  opf += "  <manifest>\n";
  opf += "    <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n";
  opf += "    <item id=\"chapter1\" href=\"chapter1.xhtml\" media-type=\"application/xhtml+xml\"/>\n";
  opf += "  </manifest>\n";
  opf += "  <spine>\n";
  opf += "    <itemref idref=\"chapter1\"/>\n";
  opf += "  </spine>\n";
  opf += "</package>\n";
  return opf;
}

std::string buildNavXhtml(const std::string& escapedTitle) {
  std::string nav;
  nav += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  nav += "<!DOCTYPE html>\n";
  nav += "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n";
  nav += "<head><title>Navigation</title></head>\n";
  nav += "<body>\n";
  nav += "  <nav epub:type=\"toc\">\n";
  nav += "    <h1>Contents</h1>\n";
  nav += "    <ol>\n";
  nav += "      <li><a href=\"chapter1.xhtml\">" + escapedTitle + "</a></li>\n";
  nav += "    </ol>\n";
  nav += "  </nav>\n";
  nav += "</body>\n";
  nav += "</html>\n";
  return nav;
}

std::string buildChapter1Xhtml(const std::string& body) {
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

}  // namespace

bool RssArticleEpubWriter::write(const RssArticle& article, const std::string& destPath) {
  // Mirrors PersistableStoreBase::writeDocToFile's own mkdir-before-write:
  // called unconditionally and its result ignored -- the directory almost
  // always exists already (OPDS/RSS/settings all create it first), and
  // mkdir on an existing directory is harmless.
  const size_t lastSlash = destPath.find_last_of('/');
  if (lastSlash != std::string::npos && lastSlash > 0) {
    Storage.mkdir(destPath.substr(0, lastSlash).c_str());
  }

  const std::string escapedTitle = xmlEscape(article.title.empty() ? article.link : article.title);

  ZipWriter zip(destPath);
  bool ok = zip.open();
  ok = ok && zip.addEntry("mimetype", "application/epub+zip");
  ok = ok && zip.addEntry("META-INF/container.xml", CONTAINER_XML);
  ok = ok && zip.addEntry("OEBPS/content.opf", buildContentOpf(escapedTitle));
  ok = ok && zip.addEntry("OEBPS/nav.xhtml", buildNavXhtml(escapedTitle));
  ok = ok && zip.addEntry("OEBPS/chapter1.xhtml", buildChapter1Xhtml(buildChapterBody(article, escapedTitle)));
  ok = zip.close() && ok;

  if (!ok) {
    LOG_ERR("RSS", "Failed to write article EPUB to %s", destPath.c_str());
    Storage.remove(destPath.c_str());  // don't leave a corrupt partial file behind
    return false;
  }
  return true;
}
