#include "RssArticleEpubWriter.h"

#include <gtest/gtest.h>

using RssArticleEpubWriter::internal::htmlToParagraphs;
using RssArticleEpubWriter::internal::xmlEscape;

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
