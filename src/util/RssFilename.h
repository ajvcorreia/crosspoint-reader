#pragma once
#include <string>

// Composes and sanitizes the on-disk filename (including the ".epub"
// extension) for a feed's combined EPUB: "<feed name> - <date> - <time>.epub".
// Re-fetching the same feed within the same wall-clock second reproduces an
// identical filename, which overwrites that exact file (HalStorage opens for
// write with O_TRUNC) -- no separate dedup logic needed. Pure: no I/O, no
// globals.
std::string rssFeedFilename(const std::string& feedName, int year, int month, int day, int hour, int minute,
                            int second);

// Normalizes a user-typed RSS download folder: trims surrounding whitespace,
// returns "" for an empty path or a bare "/" (both mean SD root), and
// otherwise guarantees exactly one leading '/' and no trailing '/'. Cold path
// (runs once per edit). Pure: no I/O, no globals.
std::string normalizeRssFolder(std::string path);
