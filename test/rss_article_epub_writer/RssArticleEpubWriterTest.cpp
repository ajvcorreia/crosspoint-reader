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

// FeedBuilder produces a STORED (uncompressed) zip, so the embedded
// XML/image bytes appear verbatim in the raw file -- a plain substring
// search is enough to check what got embedded/referenced without
// re-parsing the zip format again here (ZipWriterTest.cpp already covers
// that in detail).
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

// FeedBuilder does no networking of its own (see the header comment) -- an
// image is embedded by passing an already-downloaded file's path, which
// makes the whole class exercisable here without stubbing out HTTP.

TEST(FeedBuilderTest, ProducesReadableSingleChapterBook) {
  RssArticle article;
  article.title = "Hello World";
  article.link = "https://example.com/article";
  article.author = "Jane Doe";
  article.publishedAt = "2024-01-01";
  article.contentHtml = "<p>First paragraph.</p><p>Second paragraph.</p>";

  const std::string destPath = tempPath("crosspoint_epubtest_textonly.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "My Feed"));
  ASSERT_TRUE(builder.addArticle(article));
  ASSERT_TRUE(builder.finish());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("application/epub+zip"), std::string::npos);
  EXPECT_NE(contents.find("My Feed"), std::string::npos);
  EXPECT_NE(contents.find("Hello World"), std::string::npos);
  EXPECT_NE(contents.find("First paragraph."), std::string::npos);
  EXPECT_NE(contents.find("Jane Doe"), std::string::npos);
  EXPECT_NE(contents.find("chapter1.xhtml"), std::string::npos);
  EXPECT_EQ(contents.find("<img"), std::string::npos);  // no image was provided

  std::remove(destPath.c_str());
}

TEST(FeedBuilderTest, MultipleArticlesBecomeChaptersInOrder) {
  RssArticle first;
  first.title = "First Article";
  first.contentHtml = "<p>One.</p>";
  RssArticle second;
  second.title = "Second Article";
  second.contentHtml = "<p>Two.</p>";
  RssArticle third;
  third.title = "Third Article";
  third.contentHtml = "<p>Three.</p>";

  const std::string destPath = tempPath("crosspoint_epubtest_multi.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "Multi Feed"));
  ASSERT_TRUE(builder.addArticle(first));
  ASSERT_TRUE(builder.addArticle(second));
  ASSERT_TRUE(builder.addArticle(third));
  ASSERT_TRUE(builder.finish());

  const std::string contents = readWholeFile(destPath);
  // All three chapters exist, in spine order.
  EXPECT_NE(contents.find("chapter1.xhtml"), std::string::npos);
  EXPECT_NE(contents.find("chapter2.xhtml"), std::string::npos);
  EXPECT_NE(contents.find("chapter3.xhtml"), std::string::npos);
  const auto firstPos = contents.find("First Article");
  const auto secondPos = contents.find("Second Article");
  const auto thirdPos = contents.find("Third Article");
  ASSERT_NE(firstPos, std::string::npos);
  ASSERT_NE(secondPos, std::string::npos);
  ASSERT_NE(thirdPos, std::string::npos);
  // Chapters are written to the archive in addArticle() call order, so
  // each title's first appearance in the raw bytes (inside its own
  // chapterN.xhtml <h1>) follows the previous chapter's.
  EXPECT_LT(firstPos, secondPos);
  EXPECT_LT(secondPos, thirdPos);

  std::remove(destPath.c_str());
}

TEST(FeedBuilderTest, EmbedsJpegImageForOneChapterAndReferencesIt) {
  RssArticle article;
  article.title = "Article With Image";
  article.contentHtml = "<p>Body text.</p>";

  const std::string imagePath = tempPath("crosspoint_epubtest_source_image.bin");
  const std::string fakeImageBytes = "not-a-real-jpeg-but-thats-fine-for-this-test";
  std::ofstream img(imagePath, std::ios::binary);
  img << fakeImageBytes;
  img.close();

  const std::string destPath = tempPath("crosspoint_epubtest_withimage.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "Feed"));
  ASSERT_TRUE(builder.addArticle(article, imagePath, /*imageIsPng=*/false));
  ASSERT_TRUE(builder.finish());
  std::remove(imagePath.c_str());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("images/hero1.jpg"), std::string::npos);
  EXPECT_NE(contents.find("image/jpeg"), std::string::npos);
  EXPECT_NE(contents.find("<img src=\"images/hero1.jpg\""), std::string::npos);
  EXPECT_NE(contents.find(fakeImageBytes), std::string::npos);  // bytes embedded verbatim (STORED, uncompressed)

  std::remove(destPath.c_str());
}

TEST(FeedBuilderTest, EmbedsPngImageWithCorrectMediaType) {
  RssArticle article;
  article.title = "PNG Article";

  const std::string imagePath = tempPath("crosspoint_epubtest_source_png.bin");
  std::ofstream img(imagePath, std::ios::binary);
  img << "fake-png-bytes";
  img.close();

  const std::string destPath = tempPath("crosspoint_epubtest_png.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "Feed"));
  ASSERT_TRUE(builder.addArticle(article, imagePath, /*imageIsPng=*/true));
  ASSERT_TRUE(builder.finish());
  std::remove(imagePath.c_str());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("images/hero1.png"), std::string::npos);
  EXPECT_NE(contents.find("image/png"), std::string::npos);

  std::remove(destPath.c_str());
}

TEST(FeedBuilderTest, MissingImageSourceDegradesChapterToTextOnly) {
  // Unlike the old per-article write(), a missing/unreadable image source
  // no longer fails the whole book -- it degrades just that one chapter to
  // text-only (see ZipWriter::addEntryFromFile()'s note on why a bad
  // source doesn't poison the archive the way a bad output write does).
  // This matters much more now that one archive holds many articles: a
  // later chapter's image problem must not cost earlier or later chapters.
  RssArticle article;
  article.title = "Missing Image Source";
  article.contentHtml = "<p>Still has text.</p>";

  const std::string destPath = tempPath("crosspoint_epubtest_missing_image.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "Feed"));
  EXPECT_TRUE(builder.addArticle(article, "Z:/definitely/not/a/real/path/image.jpg", false));
  ASSERT_TRUE(builder.finish());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("Still has text."), std::string::npos);
  EXPECT_EQ(contents.find("<img"), std::string::npos);

  std::remove(destPath.c_str());
}

TEST(FeedBuilderTest, OneChapterImageFailureDoesNotAffectLaterChapters) {
  RssArticle broken;
  broken.title = "Broken Image Article";
  broken.contentHtml = "<p>No image here.</p>";
  RssArticle healthy;
  healthy.title = "Healthy Image Article";
  healthy.contentHtml = "<p>Has an image.</p>";

  const std::string imagePath = tempPath("crosspoint_epubtest_source_recovery.bin");
  const std::string imageBytes = "real-enough-image-bytes";
  std::ofstream img(imagePath, std::ios::binary);
  img << imageBytes;
  img.close();

  const std::string destPath = tempPath("crosspoint_epubtest_recovery.epub");
  RssArticleEpubWriter::FeedBuilder builder;
  ASSERT_TRUE(builder.begin(destPath, "Feed"));
  ASSERT_TRUE(builder.addArticle(broken, "Z:/nonexistent/broken.jpg", false));
  ASSERT_TRUE(builder.addArticle(healthy, imagePath, false));
  ASSERT_TRUE(builder.finish());
  std::remove(imagePath.c_str());

  const std::string contents = readWholeFile(destPath);
  EXPECT_NE(contents.find("Broken Image Article"), std::string::npos);
  EXPECT_NE(contents.find("Healthy Image Article"), std::string::npos);
  EXPECT_NE(contents.find("images/hero2.jpg"), std::string::npos);
  EXPECT_NE(contents.find(imageBytes), std::string::npos);

  std::remove(destPath.c_str());
}
