#include "RssFilename.h"

#include <cctype>
#include <cstdio>

#include "StringUtils.h"

std::string rssFeedFilename(const std::string& feedName, const int year, const int month, const int day,
                            const int hour, const int minute, const int second) {
  char stamp[32];
  snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d - %02d-%02d-%02d", year, month, day, hour, minute, second);
  // sanitizeFilename caps at 100 bytes and never returns empty (falls back to
  // "book"); the date/time stamp and ".epub" are appended after so neither is
  // ever truncated.
  return StringUtils::sanitizeFilename(feedName) + " - " + stamp + ".epub";
}

std::string normalizeRssFolder(std::string path) {
  auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  while (!path.empty() && isSpace(path.front())) path.erase(path.begin());
  while (!path.empty() && isSpace(path.back())) path.pop_back();
  if (path.empty()) return "";
  if (path.front() != '/') path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  if (path == "/") return "";  // a bare slash is SD root, same as empty
  return path;
}
