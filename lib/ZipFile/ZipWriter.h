#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * Minimal ZIP writer producing STORED-only (uncompressed) archives -- the
 * on-device write counterpart to ZipFile's read-only support. Not general
 * purpose: entries must be added in their final archive order in one pass
 * (no rewriting/patching beyond what addEntry()/close() already do), and
 * there is no support for ZIP64, encryption, or any compression method
 * other than STORED. Written for RssArticleEpubWriter, whose EPUBs are a
 * handful of small text entries -- STORED is sufficient and keeps this
 * writer free of any dependency on a compression library.
 *
 * All multi-byte fields in the ZIP format are little-endian; this writer
 * copies uint16_t/uint32_t values directly from memory without byte-
 * swapping, which is only correct on a little-endian target. Both of this
 * project's ESP32 targets (C3/RISC-V and S3/Xtensa) are little-endian.
 */
class ZipWriter {
 public:
  explicit ZipWriter(std::string filePath);
  ~ZipWriter();

  ZipWriter(const ZipWriter&) = delete;
  ZipWriter& operator=(const ZipWriter&) = delete;

  // Creates (overwriting) filePath and prepares to receive entries.
  bool open();

  // Appends one STORED entry. Call in the exact order entries should appear
  // in the archive (e.g. "mimetype" first, for EPUB).
  bool addEntry(const std::string& name, const uint8_t* data, size_t len);
  bool addEntry(const std::string& name, const std::string& content) {
    return addEntry(name, reinterpret_cast<const uint8_t*>(content.data()), content.size());
  }

  // Appends one STORED entry by streaming sourceFilePath's contents through
  // a small fixed-size buffer, rather than requiring the caller to hold the
  // whole file in memory first (as the addEntry() overloads above do) --
  // for entries too large to comfortably build as an in-memory std::string,
  // like a downloaded image. The ZIP local file header format requires the
  // CRC-32 and size up front, before the entry's data bytes, so this reads
  // sourceFilePath twice: once to compute them, once to copy the bytes.
  // An unreadable sourceFilePath fails only this one call, unlike every
  // other failure in this class -- it does not poison later addEntry*()
  // calls, since it says nothing about whether the archive itself is
  // still healthy (see the .cpp for why this distinction exists).
  bool addEntryFromFile(const std::string& name, const std::string& sourceFilePath);

  // Writes the central directory and end-of-central-directory records and
  // closes the file. Returns false if this or any prior addEntry()/
  // addEntryFromFile() write to the archive itself failed (not counting an
  // addEntryFromFile() that merely couldn't read its source -- see above);
  // the caller should discard/remove the (partial) file in that case.
  bool close();

 private:
  struct WrittenEntry {
    std::string name;
    uint32_t crc32;
    uint32_t size;
    uint32_t localHeaderOffset;
  };

  std::string filePath;
  HalFile file;
  std::vector<WrittenEntry> entries;
  bool error = false;
};
