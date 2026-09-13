#include "ZipWriter.h"

#include <Logging.h>

#include <array>
#include <utility>

namespace {
constexpr uint32_t LOCAL_FILE_HEADER_SIG = 0x04034b50;
constexpr uint32_t CENTRAL_DIR_HEADER_SIG = 0x02014b50;
constexpr uint32_t END_OF_CENTRAL_DIR_SIG = 0x06054b50;
constexpr uint16_t ZIP_VERSION = 20;      // 2.0 -- the minimum a STORED-only archive needs
constexpr uint16_t METHOD_STORED = 0;
// DOS date for 1980-01-01 (the format's epoch); this writer has no use for
// real timestamps, so every entry gets this fixed placeholder.
constexpr uint16_t DOS_DATE_EPOCH = 0x0021;

// Standard CRC-32 (IEEE 802.3 / zip) lookup table, built at compile time so
// there is no lazy-init/threading concern and no dependency on a
// compression library just for this one checksum.
constexpr std::array<uint32_t, 256> makeCrc32Table() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}
constexpr std::array<uint32_t, 256> kCrc32Table = makeCrc32Table();

// Folds one more chunk into a running CRC (seed with 0xFFFFFFFF, XOR the
// final result with 0xFFFFFFFF once all chunks are in) -- lets
// addEntryFromFile() compute a whole file's CRC across several small reads
// instead of needing it all in memory at once, unlike the single-shot
// crc32() below.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc = kCrc32Table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc;
}

uint32_t crc32(const uint8_t* data, const size_t len) { return crc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu; }

bool writeU16(HalFile& file, const uint16_t v) { return file.write(reinterpret_cast<const uint8_t*>(&v), 2) == 2; }
bool writeU32(HalFile& file, const uint32_t v) { return file.write(reinterpret_cast<const uint8_t*>(&v), 4) == 4; }
bool writeBytes(HalFile& file, const std::string& s) {
  return s.empty() || file.write(reinterpret_cast<const uint8_t*>(s.data()), s.size()) == s.size();
}

// Shared by addEntry() and addEntryFromFile(): every local file header is
// identical apart from the name/crc/size fields.
bool writeLocalHeader(HalFile& file, const std::string& name, const uint32_t crc, const uint32_t size32) {
  bool ok = true;
  ok &= writeU32(file, LOCAL_FILE_HEADER_SIG);
  ok &= writeU16(file, ZIP_VERSION);
  ok &= writeU16(file, 0);  // general-purpose flags
  ok &= writeU16(file, METHOD_STORED);
  ok &= writeU16(file, 0);  // mod time
  ok &= writeU16(file, DOS_DATE_EPOCH);
  ok &= writeU32(file, crc);
  ok &= writeU32(file, size32);  // compressed size == uncompressed size for STORED
  ok &= writeU32(file, size32);
  ok &= writeU16(file, static_cast<uint16_t>(name.size()));
  ok &= writeU16(file, 0);  // extra field length -- required to be 0 for EPUB's "mimetype" entry
  ok &= writeBytes(file, name);
  return ok;
}

constexpr size_t STREAM_CHUNK_SIZE = 2048;
}  // namespace

ZipWriter::ZipWriter(std::string filePath) : filePath(std::move(filePath)) {}

ZipWriter::~ZipWriter() {
  // Safety net for a caller that never reached close() (e.g. an early
  // return on error): avoid leaking an open file handle. The archive is
  // left without a central directory in that case, which is fine -- the
  // caller is expected to have already discarded/removed the file.
  if (file) file.close();
}

bool ZipWriter::open() {
  if (!Storage.openFileForWrite("RSS", filePath, file)) {
    LOG_ERR("RSS", "ZipWriter: failed to open %s for writing", filePath.c_str());
    error = true;
    return false;
  }
  return true;
}

bool ZipWriter::addEntry(const std::string& name, const uint8_t* data, const size_t len) {
  if (error || !file) return false;

  const auto offset = static_cast<uint32_t>(file.position());
  const uint32_t crc = crc32(data, len);
  const auto size32 = static_cast<uint32_t>(len);

  bool ok = writeLocalHeader(file, name, crc, size32);
  ok &= (len == 0 || file.write(data, len) == len);

  if (!ok) {
    LOG_ERR("RSS", "ZipWriter: failed writing entry %s", name.c_str());
    error = true;
    return false;
  }

  entries.push_back({name, crc, size32, offset});
  return true;
}

bool ZipWriter::addEntryFromFile(const std::string& name, const std::string& sourceFilePath) {
  if (error || !file) return false;

  HalFile source = Storage.open(sourceFilePath.c_str());
  if (!source) {
    // Unlike every other failure in this class, an unreadable SOURCE does
    // NOT poison the writer: it says nothing about whether the archive
    // being built is still healthy, only that this one entry can't be
    // added. Letting the caller treat this as "skip this entry" rather
    // than "the whole archive is now unusable" matters once one archive
    // holds many independently-sourced entries (RssArticleEpubWriter's
    // FeedBuilder embeds one downloaded image per chapter; a since-removed
    // or failed temp file for chapter 3 shouldn't cost chapters 4 onward).
    LOG_ERR("RSS", "ZipWriter: failed to open source %s", sourceFilePath.c_str());
    return false;
  }

  // Pass 1: compute the CRC-32 and size a chunk at a time, without ever
  // holding the whole source file in memory -- see the header comment.
  uint8_t buffer[STREAM_CHUNK_SIZE];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t size32 = 0;
  int bytesRead;
  while ((bytesRead = source.read(buffer, sizeof(buffer))) > 0) {
    crc = crc32Update(crc, buffer, static_cast<size_t>(bytesRead));
    size32 += static_cast<uint32_t>(bytesRead);
  }
  crc ^= 0xFFFFFFFFu;

  if (!source.seek(0)) {
    LOG_ERR("RSS", "ZipWriter: failed to rewind source %s", sourceFilePath.c_str());
    source.close();
    error = true;
    return false;
  }

  const auto offset = static_cast<uint32_t>(file.position());
  bool ok = writeLocalHeader(file, name, crc, size32);

  // Pass 2: stream the actual bytes straight through to the archive.
  while (ok && (bytesRead = source.read(buffer, sizeof(buffer))) > 0) {
    ok = (file.write(buffer, static_cast<size_t>(bytesRead)) == static_cast<size_t>(bytesRead));
  }
  source.close();

  if (!ok) {
    LOG_ERR("RSS", "ZipWriter: failed streaming entry %s from %s", name.c_str(), sourceFilePath.c_str());
    error = true;
    return false;
  }

  entries.push_back({name, crc, size32, offset});
  return true;
}

bool ZipWriter::close() {
  if (error || !file) {
    file.close();
    return false;
  }

  const auto centralDirOffset = static_cast<uint32_t>(file.position());
  bool ok = true;
  for (const auto& e : entries) {
    ok &= writeU32(file, CENTRAL_DIR_HEADER_SIG);
    ok &= writeU16(file, ZIP_VERSION);  // version made by
    ok &= writeU16(file, ZIP_VERSION);  // version needed
    ok &= writeU16(file, 0);            // general-purpose flags
    ok &= writeU16(file, METHOD_STORED);
    ok &= writeU16(file, 0);  // mod time
    ok &= writeU16(file, DOS_DATE_EPOCH);
    ok &= writeU32(file, e.crc32);
    ok &= writeU32(file, e.size);
    ok &= writeU32(file, e.size);
    ok &= writeU16(file, static_cast<uint16_t>(e.name.size()));
    ok &= writeU16(file, 0);  // extra field length
    ok &= writeU16(file, 0);  // comment length
    ok &= writeU16(file, 0);  // disk number start
    ok &= writeU16(file, 0);  // internal file attributes
    ok &= writeU32(file, 0);  // external file attributes
    ok &= writeU32(file, e.localHeaderOffset);
    ok &= writeBytes(file, e.name);
  }
  const auto centralDirSize = static_cast<uint32_t>(file.position()) - centralDirOffset;

  ok &= writeU32(file, END_OF_CENTRAL_DIR_SIG);
  ok &= writeU16(file, 0);  // disk number
  ok &= writeU16(file, 0);  // disk with the start of the central directory
  ok &= writeU16(file, static_cast<uint16_t>(entries.size()));  // entries on this disk
  ok &= writeU16(file, static_cast<uint16_t>(entries.size()));  // total entries
  ok &= writeU32(file, centralDirSize);
  ok &= writeU32(file, centralDirOffset);
  ok &= writeU16(file, 0);  // comment length

  if (!ok) {
    LOG_ERR("RSS", "ZipWriter: failed writing central directory for %s", filePath.c_str());
  }
  file.close();
  return ok;
}
