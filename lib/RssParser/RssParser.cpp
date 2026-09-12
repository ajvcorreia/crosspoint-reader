#include "RssParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cctype>
#include <cstring>

namespace {
constexpr size_t ENTRY_STORAGE_CAPACITY = 64;
constexpr size_t MAX_ARTICLES = ENTRY_STORAGE_CAPACITY - 2;
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_IMAGE_URL_CHARS = 768;
constexpr size_t MAX_PUBLISHED_AT_CHARS = 64;
constexpr size_t MAX_CONTENT_CHARS = 8192;
}  // namespace

RssParser::RssParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("RSS", "Couldn't allocate memory for parser");
    return;
  }
  articles.reserve(ENTRY_STORAGE_CAPACITY);
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

RssParser::~RssParser() { destroyXmlParser(parser); }

size_t RssParser::write(uint8_t c) { return write(&c, 1); }

size_t RssParser::write(const uint8_t* xmlData, const size_t length) {
  if (errorOccured) return length;

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("RSS", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return length;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("RSS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return length;
    }
    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void RssParser::flush() {
  if (errorOccured || !parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool RssParser::parse(const uint8_t* data, const size_t len) {
  write(data, len);
  flush();
  return !error();
}

void RssParser::clear() {
  articles.clear();
  feedTitle.clear();
  currentArticle = RssArticle{};
  currentText.clear();
  inItem = false;
  collectCurrentArticle = false;
  inTitle = inLink = inAuthor = inPublishedAt = inContent = false;
  contentIsHighPriority = false;
  feedTruncated = false;
}

const char* RssParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void RssParser::assignBounded(std::string& target, const char* value, const size_t maxLen) {
  if (!value) {
    target.clear();
    return;
  }
  target.assign(value, strnlen(value, maxLen));
}

void RssParser::appendBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  if (target.size() >= maxLen) return;
  const size_t remaining = maxLen - target.size();
  target.append(value, len < remaining ? len : remaining);
}

void RssParser::trimInPlace(std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  s = s.substr(start, end - start);
}

std::string RssParser::extractFirstImgSrc(const std::string& html) {
  size_t pos = 0;
  while (true) {
    const size_t imgPos = html.find("<img", pos);
    if (imgPos == std::string::npos) return "";
    const size_t tagEnd = html.find('>', imgPos);
    const size_t searchEnd = (tagEnd == std::string::npos) ? html.size() : tagEnd;

    const size_t srcPos = html.find("src=", imgPos);
    if (srcPos == std::string::npos || srcPos >= searchEnd) {
      pos = imgPos + 4;
      continue;
    }
    const size_t quotePos = srcPos + 4;
    if (quotePos >= html.size()) return "";
    const char quote = html[quotePos];
    if (quote != '"' && quote != '\'') {
      pos = imgPos + 4;
      continue;
    }
    const size_t valueStart = quotePos + 1;
    const size_t valueEnd = html.find(quote, valueStart);
    if (valueEnd == std::string::npos) return "";
    return html.substr(valueStart, valueEnd - valueStart);
  }
}

void RssParser::finishArticle() {
  if (!collectCurrentArticle) return;
  // Require at least a title and a link -- same minimum-viability bar
  // OpdsParser applies to its entries.
  if (currentArticle.title.empty() || currentArticle.link.empty()) return;
  if (currentArticle.imageUrl.empty()) {
    currentArticle.imageUrl = extractFirstImgSrc(currentArticle.contentHtml);
  }
  articles.push_back(std::move(currentArticle));
}

void XMLCALL RssParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<RssParser*>(userData);

  if (xmlLocalNameEquals(name, "item") || xmlLocalNameEquals(name, "entry")) {
    self->inItem = true;
    self->collectCurrentArticle = self->articles.size() < MAX_ARTICLES;
    self->feedTruncated = self->feedTruncated || !self->collectCurrentArticle;
    self->currentArticle = RssArticle{};
    self->currentText.clear();
    self->inTitle = self->inLink = self->inAuthor = self->inPublishedAt = self->inContent = false;
    self->contentIsHighPriority = false;
    return;
  }

  if (xmlLocalNameEquals(name, "link")) {
    const char* href = findAttribute(atts, "href");
    if (href) {
      // Atom-style: self-closing, href attribute. Atom feeds also carry
      // rel="enclosure"/"self" links; only "alternate" (or an absent rel,
      // which defaults to alternate per the Atom spec) is the article link.
      const char* rel = findAttribute(atts, "rel");
      if (self->inItem && self->collectCurrentArticle && self->currentArticle.link.empty() &&
          (!rel || strcmp(rel, "alternate") == 0)) {
        assignBounded(self->currentArticle.link, href, MAX_HREF_CHARS);
      }
    } else if (self->inItem && self->collectCurrentArticle) {
      // RSS-style: <link>text</link>, no href attribute.
      self->inLink = true;
      self->currentText.clear();
    }
    return;
  }

  if (!self->inItem) {
    // Outside any item/entry: only the feed's own title is of interest.
    if (xmlLocalNameEquals(name, "title")) {
      self->inTitle = true;
      self->currentText.clear();
    }
    return;
  }

  if (!self->collectCurrentArticle) return;

  if (xmlLocalNameEquals(name, "title")) {
    self->inTitle = true;
    self->currentText.clear();
  } else if (xmlLocalNameEquals(name, "author") || xmlLocalNameEquals(name, "creator")) {
    // Covers RSS's bare <author>text</author>, Atom's
    // <author><name>text</name></author> wrapper (character data is
    // collected across the whole span regardless of the intervening <name>,
    // see characterData()), and the Dublin Core <dc:creator> extension
    // common in blog-style RSS feeds.
    if (self->currentArticle.author.empty()) {
      self->inAuthor = true;
      self->currentText.clear();
    }
  } else if (xmlLocalNameEquals(name, "pubDate") || xmlLocalNameEquals(name, "updated") ||
             xmlLocalNameEquals(name, "published")) {
    if (self->currentArticle.publishedAt.empty()) {
      self->inPublishedAt = true;
      self->currentText.clear();
    }
  } else if (xmlLocalNameEquals(name, "enclosure")) {
    if (self->currentArticle.imageUrl.empty()) {
      const char* url = findAttribute(atts, "url");
      const char* type = findAttribute(atts, "type");
      if (url && type && strncmp(type, "image/", 6) == 0) {
        assignBounded(self->currentArticle.imageUrl, url, MAX_IMAGE_URL_CHARS);
      }
    }
  } else if (xmlLocalNameEquals(name, "content")) {
    const char* url = findAttribute(atts, "url");
    if (url) {
      // media:content (or any content-named element carrying a url
      // attribute) -- an image reference, not body text.
      if (self->currentArticle.imageUrl.empty()) {
        const char* medium = findAttribute(atts, "medium");
        const char* type = findAttribute(atts, "type");
        const bool looksLikeImage =
            (medium && strcmp(medium, "image") == 0) || (type && strncmp(type, "image/", 6) == 0);
        if (looksLikeImage) {
          assignBounded(self->currentArticle.imageUrl, url, MAX_IMAGE_URL_CHARS);
        }
      }
    } else {
      // Atom's own body-content tag.
      self->inContent = true;
      self->currentText.clear();
    }
  } else if (xmlLocalNameEquals(name, "encoded")) {
    // content:encoded (RSS extension) -- full HTML body, highest priority.
    self->inContent = true;
    self->currentText.clear();
  } else if (xmlLocalNameEquals(name, "description") || xmlLocalNameEquals(name, "summary")) {
    if (!self->contentIsHighPriority) {
      self->inContent = true;
      self->currentText.clear();
    }
  }
}

void XMLCALL RssParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<RssParser*>(userData);

  if (xmlLocalNameEquals(name, "item") || xmlLocalNameEquals(name, "entry")) {
    self->finishArticle();
    self->inItem = false;
    self->collectCurrentArticle = false;
    return;
  }

  if (!self->inItem) {
    if (xmlLocalNameEquals(name, "title") && self->inTitle) {
      self->feedTitle = self->currentText;
      self->inTitle = false;
    }
    return;
  }

  if (!self->collectCurrentArticle) return;

  if (xmlLocalNameEquals(name, "title")) {
    if (self->inTitle) self->currentArticle.title = self->currentText;
    self->inTitle = false;
  } else if (xmlLocalNameEquals(name, "link")) {
    if (self->inLink) {
      assignBounded(self->currentArticle.link, self->currentText.c_str(), MAX_HREF_CHARS);
      self->inLink = false;
    }
  } else if (xmlLocalNameEquals(name, "author") || xmlLocalNameEquals(name, "creator")) {
    if (self->inAuthor) {
      trimInPlace(self->currentText);
      self->currentArticle.author = self->currentText;
      self->inAuthor = false;
    }
  } else if (xmlLocalNameEquals(name, "pubDate") || xmlLocalNameEquals(name, "updated") ||
             xmlLocalNameEquals(name, "published")) {
    if (self->inPublishedAt) {
      trimInPlace(self->currentText);
      self->currentArticle.publishedAt = self->currentText;
      self->inPublishedAt = false;
    }
  } else if (xmlLocalNameEquals(name, "content") || xmlLocalNameEquals(name, "encoded")) {
    if (self->inContent) {
      self->currentArticle.contentHtml = self->currentText;
      self->contentIsHighPriority = true;
      self->inContent = false;
    }
  } else if (xmlLocalNameEquals(name, "description") || xmlLocalNameEquals(name, "summary")) {
    if (self->inContent && !self->contentIsHighPriority) {
      self->currentArticle.contentHtml = self->currentText;
      self->inContent = false;
    }
  }
}

void XMLCALL RssParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<RssParser*>(userData);
  if (self->inTitle) {
    appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
  } else if (self->inLink) {
    appendBounded(self->currentText, s, len, MAX_HREF_CHARS);
  } else if (self->inAuthor) {
    appendBounded(self->currentText, s, len, MAX_AUTHOR_CHARS);
  } else if (self->inPublishedAt) {
    appendBounded(self->currentText, s, len, MAX_PUBLISHED_AT_CHARS);
  } else if (self->inContent) {
    appendBounded(self->currentText, s, len, MAX_CONTENT_CHARS);
  }
}
