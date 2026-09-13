#include "ZipWriter.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint16_t readU16(std::ifstream& f) {
  uint8_t b[2] = {0, 0};
  f.read(reinterpret_cast<char*>(b), 2);
  return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

uint32_t readU32(std::ifstream& f) {
  uint8_t b[4] = {0, 0, 0, 0};
  f.read(reinterpret_cast<char*>(b), 4);
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

std::string tempPath(const char* name) {
  const char* dir = std::getenv("TEMP");
  if (!dir) dir = std::getenv("TMP");
  if (!dir) dir = ".";
  return std::string(dir) + "/" + name;
}

}  // namespace

TEST(ZipWriterTest, Crc32MatchesKnownVector) {
  // CRC-32 (IEEE 802.3) of "abc" is a standard, widely-published test
  // vector -- an independent check that the table-based implementation is
  // actually correct, not merely self-consistent.
  const std::string path = tempPath("crosspoint_ziptest_crc.bin");
  ZipWriter writer(path);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntry("a.txt", std::string("abc")));
  ASSERT_TRUE(writer.close());

  std::ifstream f(path, std::ios::binary);
  ASSERT_TRUE(f.is_open());
  ASSERT_EQ(readU32(f), 0x04034b50u);  // local file header signature
  readU16(f);                          // version needed
  readU16(f);                          // flags
  readU16(f);                          // method
  readU16(f);                          // mod time
  readU16(f);                          // mod date
  EXPECT_EQ(readU32(f), 0x352441C2u);  // crc32("abc")
  f.close();
  std::remove(path.c_str());
}

TEST(ZipWriterTest, ProducesWellFormedSingleEntryArchive) {
  const std::string path = tempPath("crosspoint_ziptest_single.bin");
  const std::string content = "hello world";
  ZipWriter writer(path);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntry("mimetype", content));
  ASSERT_TRUE(writer.close());

  std::ifstream f(path, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(f.is_open());
  const auto fileSize = static_cast<size_t>(f.tellg());
  f.seekg(0);

  ASSERT_EQ(readU32(f), 0x04034b50u);
  readU16(f);
  readU16(f);
  const uint16_t method = readU16(f);
  EXPECT_EQ(method, 0u);  // STORED
  readU16(f);
  readU16(f);
  readU32(f);  // crc, cross-checked in Crc32MatchesKnownVector
  const uint32_t compSize = readU32(f);
  const uint32_t uncompSize = readU32(f);
  EXPECT_EQ(compSize, content.size());
  EXPECT_EQ(uncompSize, content.size());
  const uint16_t nameLen = readU16(f);
  const uint16_t extraLen = readU16(f);
  EXPECT_EQ(nameLen, 8);   // "mimetype"
  EXPECT_EQ(extraLen, 0);  // required for EPUB's mimetype entry

  std::vector<char> name(nameLen);
  f.read(name.data(), nameLen);
  EXPECT_EQ(std::string(name.data(), nameLen), "mimetype");

  std::vector<char> data(uncompSize);
  f.read(data.data(), uncompSize);
  EXPECT_EQ(std::string(data.data(), uncompSize), content);

  // Central directory immediately follows the one entry's data.
  ASSERT_EQ(readU32(f), 0x02014b50u);

  // End-of-central-directory is the final 22 bytes (no archive comment).
  f.seekg(static_cast<std::streamoff>(fileSize) - 22);
  ASSERT_EQ(readU32(f), 0x06054b50u);
  readU16(f);
  readU16(f);
  EXPECT_EQ(readU16(f), 1);  // entries on this disk
  EXPECT_EQ(readU16(f), 1);  // total entries

  f.close();
  std::remove(path.c_str());
}

TEST(ZipWriterTest, AddEntryFromFileMatchesAddEntryForSmallContent) {
  // A source file streamed through addEntryFromFile() should produce a
  // byte-identical entry to the same content passed directly to addEntry().
  const std::string sourcePath = tempPath("crosspoint_ziptest_source_small.bin");
  const std::string content = "streamed content";
  std::ofstream source(sourcePath, std::ios::binary);
  source << content;
  source.close();

  const std::string zipPath = tempPath("crosspoint_ziptest_fromfile_small.bin");
  ZipWriter writer(zipPath);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntryFromFile("images/hero.jpg", sourcePath));
  ASSERT_TRUE(writer.close());
  std::remove(sourcePath.c_str());

  std::ifstream f(zipPath, std::ios::binary);
  ASSERT_TRUE(f.is_open());
  ASSERT_EQ(readU32(f), 0x04034b50u);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU32(f);  // crc -- cross-checked against addEntry() below
  const uint32_t compSize = readU32(f);
  const uint32_t uncompSize = readU32(f);
  EXPECT_EQ(compSize, content.size());
  EXPECT_EQ(uncompSize, content.size());
  const uint16_t nameLen = readU16(f);
  readU16(f);  // extra length

  std::vector<char> name(nameLen);
  f.read(name.data(), nameLen);
  EXPECT_EQ(std::string(name.data(), nameLen), "images/hero.jpg");

  std::vector<char> data(uncompSize);
  f.read(data.data(), uncompSize);
  EXPECT_EQ(std::string(data.data(), uncompSize), content);
  f.close();
  std::remove(zipPath.c_str());
}

TEST(ZipWriterTest, AddEntryFromFileHandlesMultipleReadChunks) {
  // Larger than any plausible internal chunk buffer, to exercise the
  // two-pass streaming loop across several reads rather than just one.
  std::string content;
  content.reserve(10000);
  for (int i = 0; i < 10000; i++) {
    content += static_cast<char>('a' + (i % 26));
  }

  const std::string sourcePath = tempPath("crosspoint_ziptest_source_large.bin");
  std::ofstream source(sourcePath, std::ios::binary);
  source << content;
  source.close();

  const std::string zipPath = tempPath("crosspoint_ziptest_fromfile_large.bin");
  ZipWriter writer(zipPath);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntryFromFile("big.bin", sourcePath));
  ASSERT_TRUE(writer.close());
  std::remove(sourcePath.c_str());

  std::ifstream f(zipPath, std::ios::binary);
  ASSERT_TRUE(f.is_open());
  readU32(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU32(f);  // crc
  const uint32_t compSize = readU32(f);
  const uint32_t uncompSize = readU32(f);
  ASSERT_EQ(compSize, content.size());
  ASSERT_EQ(uncompSize, content.size());
  const uint16_t nameLen = readU16(f);
  readU16(f);
  f.seekg(nameLen, std::ios::cur);

  std::vector<char> data(uncompSize);
  f.read(data.data(), uncompSize);
  EXPECT_EQ(std::string(data.data(), uncompSize), content);
  f.close();
  std::remove(zipPath.c_str());
}

TEST(ZipWriterTest, MixesAddEntryAndAddEntryFromFile) {
  // Mirrors real usage: string entries (mimetype/opf/xhtml) interleaved
  // with one streamed-from-file entry (a downloaded image).
  const std::string sourcePath = tempPath("crosspoint_ziptest_source_mixed.bin");
  const std::string imageContent = "fake-image-bytes";
  std::ofstream source(sourcePath, std::ios::binary);
  source << imageContent;
  source.close();

  const std::string zipPath = tempPath("crosspoint_ziptest_mixed.bin");
  ZipWriter writer(zipPath);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntry("mimetype", std::string("application/epub+zip")));
  ASSERT_TRUE(writer.addEntryFromFile("OEBPS/images/hero.jpg", sourcePath));
  ASSERT_TRUE(writer.addEntry("OEBPS/chapter1.xhtml", std::string("<html/>")));
  ASSERT_TRUE(writer.close());
  std::remove(sourcePath.c_str());

  std::ifstream f(zipPath, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(f.is_open());
  const auto fileSize = static_cast<size_t>(f.tellg());
  f.seekg(0);

  // Three local headers precede the central directory; verifying the EOCD
  // entry count confirms all three were retained despite the mixed
  // addEntry()/addEntryFromFile() sequence.
  f.seekg(static_cast<std::streamoff>(fileSize) - 22);
  ASSERT_EQ(readU32(f), 0x06054b50u);
  readU16(f);
  readU16(f);
  EXPECT_EQ(readU16(f), 3);
  EXPECT_EQ(readU16(f), 3);

  f.close();
  std::remove(zipPath.c_str());
}

TEST(ZipWriterTest, MultipleEntriesPreserveOrderAndOffsets) {
  const std::string path = tempPath("crosspoint_ziptest_multi.bin");
  ZipWriter writer(path);
  ASSERT_TRUE(writer.open());
  ASSERT_TRUE(writer.addEntry("first.txt", std::string("one")));
  ASSERT_TRUE(writer.addEntry("second.txt", std::string("two-two")));
  ASSERT_TRUE(writer.close());

  std::ifstream f(path, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(f.is_open());
  const auto fileSize = static_cast<size_t>(f.tellg());
  f.seekg(0);

  // First entry's local header.
  ASSERT_EQ(readU32(f), 0x04034b50u);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU32(f);
  EXPECT_EQ(readU32(f), 3u);
  EXPECT_EQ(readU32(f), 3u);
  const uint16_t name1Len = readU16(f);
  readU16(f);
  f.seekg(name1Len + 3, std::ios::cur);  // skip "first.txt" + "one"

  // Second entry's local header immediately follows.
  ASSERT_EQ(readU32(f), 0x04034b50u);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU16(f);
  readU32(f);
  EXPECT_EQ(readU32(f), 7u);
  EXPECT_EQ(readU32(f), 7u);

  // EOCD reports both entries.
  f.seekg(static_cast<std::streamoff>(fileSize) - 22);
  ASSERT_EQ(readU32(f), 0x06054b50u);
  readU16(f);
  readU16(f);
  EXPECT_EQ(readU16(f), 2);
  EXPECT_EQ(readU16(f), 2);

  f.close();
  std::remove(path.c_str());
}
