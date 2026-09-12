#pragma once
#include <Print.h>
#include <expat.h>

#include <string>
#include <vector>

/**
 * A single article parsed from an RSS 2.0 <item> or Atom <entry>.
 */
struct RssArticle {
  std::string title;
  std::string link;
  std::string author;       // optional
  std::string publishedAt;  // raw pubDate/updated/published string; formatted on render
  std::string contentHtml;  // content:encoded / Atom <content>, or description/summary as fallback
  std::string imageUrl;     // enclosure/media:content url, or first inline <img> in contentHtml
};

/**
 * Streaming parser for RSS 2.0 and Atom feeds. Modeled directly on
 * lib/OpdsParser/OpdsParser.h (Expat-based, no full-document buffering --
 * required given the OPDS OOM history that motivated that design).
 *
 * Both formats are handled by the same state machine: an RSS <item> and an
 * Atom <entry> both start/end an article; a plain-text <link> (RSS) and a
 * self-closing <link href="..."/> (Atom) both set the article link; a
 * high-priority full-content tag (content:encoded, or Atom's <content> when
 * it has no url attribute) wins over a low-priority summary tag
 * (description, or Atom's <summary>) when both are present. Namespace
 * prefixes (media:, content:, dc:) are stripped via XmlParserUtils'
 * xmlLocalNameEquals rather than tracked -- this parser, like OpdsParser,
 * creates its XML_Parser without namespace processing.
 *
 * Usage:
 *   RssParser parser;
 *   if (parser.parse(xmlData, xmlLength)) {
 *     for (const auto& article : parser.getArticles()) { ... }
 *   }
 * Or streamed (e.g. from an HTTP response body, via a Print& sink):
 *   RssParser parser;
 *   httpDownloader.streamTo(parser);  // repeated write() calls
 *   parser.flush();
 */
class RssParser final : public Print {
 public:
  RssParser();
  ~RssParser() override;

  RssParser(const RssParser&) = delete;
  RssParser& operator=(const RssParser&) = delete;

  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;
  void flush() override;

  // Convenience for a single in-memory buffer (small feeds, tests). Calls
  // write() then flush(); returns !error().
  bool parse(const uint8_t* data, size_t len);

  bool error() const { return errorOccured; }
  bool truncated() const { return feedTruncated; }
  operator bool() const { return !errorOccured; }

  const std::string& getFeedTitle() const { return feedTitle; }
  const std::vector<RssArticle>& getArticles() const& { return articles; }
  std::vector<RssArticle> getArticles() && { return std::move(articles); }

  void clear();

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  static const char* findAttribute(const XML_Char** atts, const char* name);
  static void assignBounded(std::string& target, const char* value, size_t maxLen);
  static void appendBounded(std::string& target, const char* value, size_t len, size_t maxLen);
  // Trims leading/trailing ASCII whitespace in place -- author names and
  // dates often pick up surrounding newlines from pretty-printed XML.
  static void trimInPlace(std::string& s);
  // First <img src="..."> (single- or double-quoted) found in html, or "".
  // Pure string scan: contentHtml is already-unescaped text by the time
  // Expat hands it to us (both entity-escaped and CDATA content arrive
  // through characterData already unescaped), so this never touches XML.
  static std::string extractFirstImgSrc(const std::string& html);

  XML_Parser parser = nullptr;
  std::string feedTitle;
  std::vector<RssArticle> articles;
  RssArticle currentArticle;
  std::string currentText;

  bool inItem = false;  // true between <item>/<entry> start and end
  bool collectCurrentArticle = false;

  // Which leaf text element we're currently collecting character data for.
  // Only one can be true at a time.
  bool inTitle = false;
  bool inLink = false;         // RSS-style <link>text</link> only -- Atom's
                                // <link href="..."/> is captured directly in
                                // startElement, never sets this.
  bool inAuthor = false;       // spans RSS's bare <author> and Atom's
                                // <author><name> wrapper alike -- see .cpp.
  bool inPublishedAt = false;  // <pubDate>, <updated>, or <published>
  bool inContent = false;      // whichever content tag is currently open
  bool contentIsHighPriority = false;  // true once set from content:encoded
                                        // or Atom <content>, blocking a later
                                        // lower-priority description/summary
                                        // from overwriting it

  bool errorOccured = false;
  bool feedTruncated = false;

  void finishArticle();
};
