#include <gtest/gtest.h>

#include <string>

#include "RssParser.h"

namespace {

bool parseString(RssParser& parser, const std::string& xml) {
  return parser.parse(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
}

constexpr char kRssBasic[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <title>Example Blog</title>
  <item>
    <title>First Post</title>
    <link>https://example.com/first-post</link>
    <description>A short summary.</description>
    <pubDate>Mon, 01 Jan 2026 12:00:00 GMT</pubDate>
    <author>jane@example.com (Jane Doe)</author>
  </item>
</channel></rss>)";

constexpr char kAtomBasic[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Example Atom Feed</title>
  <entry>
    <title>Atom Entry One</title>
    <link rel="alternate" href="https://example.com/atom-one"/>
    <link rel="self" href="https://example.com/feed.xml"/>
    <summary>An Atom summary.</summary>
    <updated>2026-01-01T12:00:00Z</updated>
    <author><name>Jane Atom</name></author>
  </entry>
</feed>)";

constexpr char kContentEncodedPreferred[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
<channel><item>
  <title>Full vs Summary</title>
  <link>https://example.com/full</link>
  <description>Short summary only.</description>
  <content:encoded><![CDATA[<p>Full <strong>HTML</strong> body.</p>]]></content:encoded>
</item></channel></rss>)";

constexpr char kAtomContentPreferred[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
<entry>
  <title>Atom Full vs Summary</title>
  <link href="https://example.com/atom-full"/>
  <summary>Short Atom summary.</summary>
  <content type="html">&lt;p&gt;Full Atom body.&lt;/p&gt;</content>
</entry></feed>)";

constexpr char kCdataDescription[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><item>
  <title>CDATA Test</title>
  <link>https://example.com/cdata</link>
  <description><![CDATA[<p>Raw <em>HTML</em> via CDATA.</p>]]></description>
</item></channel></rss>)";

constexpr char kEnclosureImage[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><item>
  <title>Enclosure Image</title>
  <link>https://example.com/enclosure</link>
  <enclosure url="https://example.com/photo.jpg" type="image/jpeg" length="12345"/>
</item></channel></rss>)";

constexpr char kEnclosureNonImageIgnored[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><item>
  <title>Podcast Enclosure</title>
  <link>https://example.com/podcast</link>
  <enclosure url="https://example.com/episode.mp3" type="audio/mpeg" length="99999"/>
  <description>&lt;img src="https://example.com/fallback.png"/&gt; inline fallback</description>
</item></channel></rss>)";

constexpr char kMediaContentImage[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:media="http://search.yahoo.com/mrss/"><channel><item>
  <title>Media Content</title>
  <link>https://example.com/media</link>
  <media:content url="https://example.com/media.jpg" medium="image"/>
</item></channel></rss>)";

constexpr char kDcCreatorAuthor[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><item>
  <title>DC Creator</title>
  <link>https://example.com/dc</link>
  <dc:creator>  Dublin Core Author  </dc:creator>
</item></channel></rss>)";

constexpr char kInlineImgFallback[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><item>
  <title>Inline Image Fallback</title>
  <link>https://example.com/inline-img</link>
  <description>&lt;p&gt;Text &lt;img src='https://example.com/inline.png' alt="x"/&gt; more&lt;/p&gt;</description>
</item></channel></rss>)";

constexpr char kMultipleItemsOrderPreserved[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <item><title>Newest</title><link>https://example.com/newest</link></item>
  <item><title>Middle</title><link>https://example.com/middle</link></item>
  <item><title>Oldest</title><link>https://example.com/oldest</link></item>
</channel></rss>)";

constexpr char kMissingTitleOrLinkSkipped[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <item><title>Has Both</title><link>https://example.com/both</link></item>
  <item><title>No Link</title></item>
  <item><link>https://example.com/no-title</link></item>
</channel></rss>)";

constexpr char kMalformedXml[] = "<rss version=\"2.0\"><channel><item><title>Unclosed";

std::string makeManyItemsFeed(int count) {
  std::string xml = "<?xml version=\"1.0\"?><rss version=\"2.0\"><channel>";
  for (int i = 0; i < count; i++) {
    xml += "<item><title>Item " + std::to_string(i) + "</title><link>https://example.com/" +
           std::to_string(i) + "</link></item>";
  }
  xml += "</channel></rss>";
  return xml;
}

TEST(RssParser, RssBasicFields) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kRssBasic));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  const auto& a = parser.getArticles()[0];
  EXPECT_EQ(a.title, "First Post");
  EXPECT_EQ(a.link, "https://example.com/first-post");
  EXPECT_EQ(a.contentHtml, "A short summary.");
  EXPECT_EQ(a.publishedAt, "Mon, 01 Jan 2026 12:00:00 GMT");
  EXPECT_EQ(a.author, "jane@example.com (Jane Doe)");
}

TEST(RssParser, FeedTitleIsChannelLevelNotItemLevel) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kRssBasic));
  EXPECT_EQ(parser.getFeedTitle(), "Example Blog");
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].title, "First Post");
}

TEST(RssParser, AtomBasicFields) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kAtomBasic));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  const auto& a = parser.getArticles()[0];
  EXPECT_EQ(a.title, "Atom Entry One");
  EXPECT_EQ(a.link, "https://example.com/atom-one");
  EXPECT_EQ(a.contentHtml, "An Atom summary.");
  EXPECT_EQ(a.publishedAt, "2026-01-01T12:00:00Z");
  EXPECT_EQ(a.author, "Jane Atom");
}

TEST(RssParser, ContentEncodedPreferredOverDescription) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kContentEncodedPreferred));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].contentHtml, "<p>Full <strong>HTML</strong> body.</p>");
}

TEST(RssParser, AtomContentPreferredOverSummary) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kAtomContentPreferred));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].contentHtml, "<p>Full Atom body.</p>");
}

TEST(RssParser, CdataContentIsUnescapedRawHtml) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kCdataDescription));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].contentHtml, "<p>Raw <em>HTML</em> via CDATA.</p>");
}

TEST(RssParser, EnclosureImageAccepted) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kEnclosureImage));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].imageUrl, "https://example.com/photo.jpg");
}

TEST(RssParser, EnclosureNonImageIgnoredFallsBackToInlineImg) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kEnclosureNonImageIgnored));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].imageUrl, "https://example.com/fallback.png");
}

TEST(RssParser, MediaContentImageAccepted) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kMediaContentImage));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].imageUrl, "https://example.com/media.jpg");
}

TEST(RssParser, DcCreatorUsedAsAuthorAndTrimmed) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kDcCreatorAuthor));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].author, "Dublin Core Author");
}

TEST(RssParser, InlineImgExtractedFromContentWhenNoEnclosure) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kInlineImgFallback));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].imageUrl, "https://example.com/inline.png");
}

TEST(RssParser, MultipleItemsPreserveFeedOrder) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kMultipleItemsOrderPreserved));
  ASSERT_EQ(parser.getArticles().size(), 3u);
  EXPECT_EQ(parser.getArticles()[0].title, "Newest");
  EXPECT_EQ(parser.getArticles()[1].title, "Middle");
  EXPECT_EQ(parser.getArticles()[2].title, "Oldest");
}

TEST(RssParser, ArticlesMissingTitleOrLinkAreSkipped) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kMissingTitleOrLinkSkipped));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  EXPECT_EQ(parser.getArticles()[0].title, "Has Both");
}

TEST(RssParser, MalformedXmlReportsError) {
  RssParser parser;
  EXPECT_FALSE(parseString(parser, kMalformedXml));
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.getArticles().empty());
}

TEST(RssParser, TruncatesAtMaxArticlesAndReportsTruncated) {
  RssParser parser;
  const std::string xml = makeManyItemsFeed(100);
  ASSERT_TRUE(parseString(parser, xml));
  EXPECT_LT(parser.getArticles().size(), 100u);
  EXPECT_TRUE(parser.truncated());
}

TEST(RssParser, ClearResetsAllState) {
  RssParser parser;
  ASSERT_TRUE(parseString(parser, kRssBasic));
  ASSERT_EQ(parser.getArticles().size(), 1u);
  parser.clear();
  EXPECT_TRUE(parser.getArticles().empty());
  EXPECT_TRUE(parser.getFeedTitle().empty());
  EXPECT_FALSE(parser.truncated());
}

}  // namespace
