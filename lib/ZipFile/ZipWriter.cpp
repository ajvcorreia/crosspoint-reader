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

uint32_t crc32(const uint8_t* data, const size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc = kCrc32Table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

bool writeU16(HalFile& file, const uint16_t v) { return file.write(reinterpret_cast<const uint8_t*>(&v), 2) == 2; }
bool writeU32(HalFile& file, const uint32_t v) { return file.write(reinterpret_cast<const uint8_t*>(&v), 4) == 4; }
bool writeBytes(HalFile& file, const std::string& s) {
  return s.empty() || file.write(reinterpret_cast<const uint8_t*>(s.data()), s.size()) == s.size();
}
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
  ok &= (len == 0 || file.write(data, len) == len);

  if (!ok) {
    LOG_ERR("RSS", "ZipWriter: failed writing entry %s", name.c_str());
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
