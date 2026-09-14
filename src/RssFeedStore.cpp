#include "RssFeedStore.h"

#include <Logging.h>

#include <algorithm>

#include "util/RssFilename.h"

void RssFeedStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["feeds"].to<JsonArray>();
  for (const auto& feed : feeds) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = feed.name;
    obj["url"] = feed.url;
    obj["folder"] = feed.folder;
  }
}

bool RssFeedStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'feeds' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  feeds.clear();
  JsonArrayConst arr = doc["feeds"].as<JsonArrayConst>();
  feeds.reserve(std::min(arr.size(), MAX_FEEDS));

  for (JsonObjectConst obj : arr) {
    if (feeds.size() >= RssFeedStore::MAX_FEEDS) break;
    RssFeed feed;
    feed.name = obj["name"] | "";
    feed.url = obj["url"] | "";
    // Normalized on load too: tolerates a hand-edited or pre-normalization file.
    feed.folder = normalizeRssFolder(obj["folder"] | "");
    feeds.push_back(std::move(feed));
  }

  LOG_DBG("RSS", "Loaded %zu RSS feeds from file", feeds.size());
  return true;
}

bool RssFeedStore::addFeed(const RssFeed& feed) {
  if (feeds.size() >= MAX_FEEDS) {
    LOG_DBG("RSS", "Cannot add more feeds, limit of %zu reached", MAX_FEEDS);
    return false;
  }

  feeds.push_back(feed);
  LOG_DBG("RSS", "Added feed: %s", feed.name.c_str());
  return saveToFile();
}

bool RssFeedStore::updateFeed(size_t index, const RssFeed& feed) {
  if (index >= feeds.size()) {
    return false;
  }

  feeds[index] = feed;
  LOG_DBG("RSS", "Updated feed: %s", feed.name.c_str());
  return saveToFile();
}

bool RssFeedStore::removeFeed(size_t index) {
  if (index >= feeds.size()) {
    return false;
  }

  LOG_DBG("RSS", "Removed feed: %s", feeds[index].name.c_str());
  feeds.erase(feeds.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const RssFeed* RssFeedStore::getFeed(size_t index) const {
  if (index >= feeds.size()) {
    return nullptr;
  }
  return &feeds[index];
}
