#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }
  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(const uint8_t* buffer, size_t count) { return write(static_cast<const void*>(buffer), count); }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }
  explicit operator bool() const { return file_ != nullptr; }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool mkdir(const char*, bool = true) { return true; }
  bool remove(const char* path) { return std::remove(path) == 0; }
};

#define Storage HalStorage::getInstance()
