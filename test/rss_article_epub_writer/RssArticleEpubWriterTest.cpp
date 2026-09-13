#include "RssArticleEpubWriter.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using RssArticleEpubWriter::internal::htmlToParagraphs;
using RssArticleEpubWriter::internal::xmlEscape;

namespace {
std::string tempPath(const char* name) {
  const char* dir = std::getenv("TEMP");
  if (!dir) dir = std::getenv("TMP");
  if (!dir) dir = ".";
  return std::string(dir) + "/" + name;
}

// write() produces a STORED (uncompressed) zip, so the embedded XML/image
// bytes appear verbatim in the raw file -- a plain substring search is
// enough to check what got embedded/referenced without re-parsing the zip
// format again here (ZipWriterTest.cpp already covers that in detail).
std::string readWholeFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

TEST(XmlEscapeTest, EscapesAmpersandLessThanGreaterThan) {
  EXPECT_EQ(xmlEscape("Tom & Jerry"), "Tom &amp; Jerry");
  EXPECT_EQ(xmlEscape("a < b > c"), "a &lt; b &gt; c");
}

TEST(XmlEscapeTest, LeavesOrdinaryTextUnchanged) {
  EXPECT_EQ(xmlEscape("Hello, world! 123"), "Hello, world! 123");
}

TEST(XmlEscapeTest, HandlesEmptyString) { EXPECT_EQ(xmlEscape(""), ""); }

TEST(HtmlToParagraphsTest, PlainTextIsOneParagraph) {
  const auto paragraphs = htmlToParagraphs("Just plain text.");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "Just plain text.");
}

TEST(HtmlToParagraphsTest, PTagsSplitIntoSeparateParagraphs) {
  const auto paragraphs = htmlToParagraphs("<p>First.</p><p>Second.</p>");
  ASSERT_EQ(paragraphs.size(), 2u);
  EXPECT_EQ(paragraphs[0], "First.");
  EXPECT_EQ(paragraphs[1], "Second.");
}

TEST(HtmlToParagraphsTest, BrTagsSplitParagraphs) {
  const auto paragraphs = htmlToParagraphs("Line one<br/>Line two<br>Line three");
  ASSERT_EQ(paragraphs.size(), 3u);
  EXPECT_EQ(paragraphs[0], "Line one");
  EXPECT_EQ(paragraphs[1], "Line two");
  EXPECT_EQ(paragraphs[2], "Line three");
}

TEST(HtmlToParagraphsTest, InlineTagsAreStrippedNotSplit) {
  const auto paragraphs = htmlToParagraphs("A <b>bold</b> and <i>italic</i> word.");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "A bold and italic word.");
}

TEST(HtmlToParagraphsTest, AnchorTagWithAttributesIsStripped) {
  const auto paragraphs = htmlToParagraphs("Read <a href=\"https://x.example/?a=1&b=2\">more here</a> please.");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "Read more here please.");
}

TEST(HtmlToParagraphsTest, NestedBlockTagsDoNotProduceEmptyParagraphs) {
  const auto paragraphs = htmlToParagraphs("<div><p>Only paragraph.</p></div>");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "Only paragraph.");
}

TEST(HtmlToParagraphsTest, LeadingTrailingWhitespaceIsTrimmedPerParagraph) {
  const auto paragraphs = htmlToParagraphs("<p>\n  padded text  \n</p>");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "padded text");
}

TEST(HtmlToParagraphsTest, EmptyInputProducesNoParagraphs) {
  EXPECT_TRUE(htmlToParagraphs("").empty());
  EXPECT_TRUE(htmlToParagraphs("<p></p><div>   </div>").empty());
}

TEST(HtmlToParagraphsTest, UnclosedTrailingTagStopsGracefully) {
  // A malformed feed with a truncated/unterminated tag at the end should
  // not lose everything that came before it.
  const auto paragraphs = htmlToParagraphs("<p>Good text.</p><div class=\"broken");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "Good text.");
}

TEST(HtmlToParagraphsTest, HeadingTagsAreTreatedAsBlockBoundaries) {
  const auto paragraphs = htmlToParagraphs("<h2>Title</h2><p>Body.</p>");
  ASSERT_EQ(paragraphs.size(), 2u);
  EXPECT_EQ(paragraphs[0], "Title");
  EXPECT_EQ(paragraphs[1], "Body.");
}

TEST(HtmlToParagraphsTest, LiteralAmpersandPassesThroughForLaterEscaping) {
  // htmlToParagraphs only strips tags; re-escaping "&" for XML output is
  // xmlEscape's job, applied separately by the caller.
  const auto paragraphs = htmlToParagraphs("Fish & chips");
  ASSERT_EQ(paragraphs.size(), 1u);
  EXPECT_EQ(paragraphs[0], "Fish & chips");
}

// write() does no networking of its own (see the header comment) -- an
// image is embedded by passing an already-downloaded file's path, which
// makes the whole function exercisable here without stubbing out HTTP.

TEST(RssArticleEpubWriterWriteTest, ProducesReadableTextOnlyEpub) {
  RssArticle article;
  article.title = "Hello World";
  article.link = "https://example.com/article";
  article.author = "Jane Doe";
  article.publishedAt = "2024-01-01";
  article.contentHtml = "<p>First paragraph.</p><p>Second paragraph.</p>";

  const std::string destPath = tempPath("crosspoint_epubtest_textonly.epub");
  ASSERT_TRUE(RssArticleEpubWriter::write(article, destPath));

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("application/epub+zip"), std::string::npos);
  EXPECT_NE(contents.find("Hello World"), std::string::npos);
  EXPECT_NE(contents.find("First paragraph."), std::string::npos);
  EXPECT_NE(contents.find("Jane Doe"), std::string::npos);
  EXPECT_EQ(contents.find("hero-image"), std::string::npos);  // no image was provided
  EXPECT_EQ(contents.find("<img"), std::string::npos);

  std::remove(destPath.c_str());
}

TEST(RssArticleEpubWriterWriteTest, EmbedsProvidedJpegImageAndReferencesIt) {
  RssArticle article;
  article.title = "Article With Image";
  article.contentHtml = "<p>Body text.</p>";

  const std::string imagePath = tempPath("crosspoint_epubtest_source_image.bin");
  const std::string fakeImageBytes = "not-a-real-jpeg-but-thats-fine-for-this-test";
  std::ofstream img(imagePath, std::ios::binary);
  img << fakeImageBytes;
  img.close();

  const std::string destPath = tempPath("crosspoint_epubtest_withimage.epub");
  ASSERT_TRUE(RssArticleEpubWriter::write(article, destPath, imagePath, /*imageIsPng=*/false));
  std::remove(imagePath.c_str());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("images/hero.jpg"), std::string::npos);
  EXPECT_NE(contents.find("image/jpeg"), std::string::npos);
  EXPECT_NE(contents.find("<img src=\"images/hero.jpg\""), std::string::npos);
  EXPECT_NE(contents.find(fakeImageBytes), std::string::npos);  // bytes embedded verbatim (STORED, uncompressed)

  std::remove(destPath.c_str());
}

TEST(RssArticleEpubWriterWriteTest, EmbedsPngImageWithCorrectMediaType) {
  RssArticle article;
  article.title = "PNG Article";

  const std::string imagePath = tempPath("crosspoint_epubtest_source_png.bin");
  std::ofstream img(imagePath, std::ios::binary);
  img << "fake-png-bytes";
  img.close();

  const std::string destPath = tempPath("crosspoint_epubtest_png.epub");
  ASSERT_TRUE(RssArticleEpubWriter::write(article, destPath, imagePath, /*imageIsPng=*/true));
  std::remove(imagePath.c_str());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("images/hero.png"), std::string::npos);
  EXPECT_NE(contents.find("image/png"), std::string::npos);

  std::remove(destPath.c_str());
}

TEST(RssArticleEpubWriterWriteTest, FailsWhenImageSourceIsMissing) {
  RssArticle article;
  article.title = "Missing Image Source";

  const std::string destPath = tempPath("crosspoint_epubtest_missing_image.epub");
  // A nonexistent source path fails the whole write() rather than silently
  // producing a corrupt or partial archive.
  EXPECT_FALSE(RssArticleEpubWriter::write(article, destPath, "/nonexistent/path/image.jpg", false));
}
