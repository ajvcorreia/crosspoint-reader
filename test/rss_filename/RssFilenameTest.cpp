#include <gtest/gtest.h>

#include <string>

#include "RssFilename.h"

namespace {

TEST(RssFilename, ComposesNameDateAndTime) {
  EXPECT_EQ(rssFeedFilename("My Feed", 2026, 9, 14, 8, 5, 3), "My Feed - 2026-09-14 - 08-05-03.epub");
}

TEST(RssFilename, IdenticalInputsProduceIdenticalFilenames) {
  // The overwrite behavior relies on this: same feed + same wall-clock
  // second must produce the exact same path.
  EXPECT_EQ(rssFeedFilename("My Feed", 2026, 9, 14, 8, 5, 3), rssFeedFilename("My Feed", 2026, 9, 14, 8, 5, 3));
}

TEST(RssFilename, DifferentTimeProducesDifferentFilename) {
  EXPECT_NE(rssFeedFilename("My Feed", 2026, 9, 14, 8, 5, 3), rssFeedFilename("My Feed", 2026, 9, 14, 8, 5, 4));
}

TEST(RssFilename, IllegalCharactersInFeedNameAreSanitized) {
  // '/' ':' '*' '?' etc. are replaced with '_' by sanitizeFilename.
  EXPECT_EQ(rssFeedFilename("A/B: C?", 2026, 1, 1, 0, 0, 0), "A_B_ C_ - 2026-01-01 - 00-00-00.epub");
}

TEST(RssFilename, EmptyFeedNameFallsBackToBook) {
  // sanitizeFilename returns "book" when nothing usable remains.
  EXPECT_EQ(rssFeedFilename("", 2026, 1, 1, 0, 0, 0), "book - 2026-01-01 - 00-00-00.epub");
}

TEST(RssFilename, LongNameIsTruncatedToByteBudgetBeforeStamp) {
  // sanitizeFilename caps the feed-name portion at 100 bytes; the date/time
  // stamp and ".epub" are appended after, so neither is ever truncated.
  const std::string longName(200, 'a');
  const std::string result = rssFeedFilename(longName, 2026, 1, 1, 0, 0, 0);
  EXPECT_EQ(result, std::string(100, 'a') + " - 2026-01-01 - 00-00-00.epub");
}

TEST(RssFolder, EmptyAndBareSlashBothMeanSdRoot) {
  // "" is the sentinel for "no folder of my own"; a bare "/" collapses onto it
  // so the two never produce different download paths.
  EXPECT_EQ(normalizeRssFolder(""), "");
  EXPECT_EQ(normalizeRssFolder("/"), "");
  EXPECT_EQ(normalizeRssFolder("///"), "");
}

TEST(RssFolder, LeadingSlashIsAdded) {
  EXPECT_EQ(normalizeRssFolder("RSS"), "/RSS");
  EXPECT_EQ(normalizeRssFolder("/RSS"), "/RSS");
}

TEST(RssFolder, TrailingSlashesAreStripped) {
  EXPECT_EQ(normalizeRssFolder("/RSS/"), "/RSS");
  EXPECT_EQ(normalizeRssFolder("RSS///"), "/RSS");
}

TEST(RssFolder, NestedPathsKeepInteriorSeparators) {
  EXPECT_EQ(normalizeRssFolder("RSS/tech/"), "/RSS/tech");
}

TEST(RssFolder, SurroundingWhitespaceIsTrimmed) {
  EXPECT_EQ(normalizeRssFolder("  /RSS  "), "/RSS");
  EXPECT_EQ(normalizeRssFolder("\tRSS\t"), "/RSS");
  EXPECT_EQ(normalizeRssFolder("   "), "");
}

TEST(RssFolder, IsIdempotent) {
  // The store round-trips already-normalized values through this on every edit.
  const std::string once = normalizeRssFolder("  RSS/tech//  ");
  EXPECT_EQ(once, "/RSS/tech");
  EXPECT_EQ(normalizeRssFolder(once), once);
}

}  // namespace
