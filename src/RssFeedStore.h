#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct RssFeed {
  std::string name;
  std::string url;
  // Per-feed override for where the combined EPUB is written. Empty inherits
  // the global default (SETTINGS.rssDownloadFolder).
  std::string folder;
};

/**
 * Singleton class for storing RSS/Atom feed configurations on the SD card.
 * No credentials in v1 -- see OpdsServerStore for the obfuscation pattern if
 * feed authentication is added later.
 */
class RssFeedStore : public PersistableStore<RssFeedStore> {
 private:
  std::vector<RssFeed> feeds;

  static constexpr size_t MAX_FEEDS = 20;

  RssFeedStore() = default;

  friend class PersistableStore<RssFeedStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/rss.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addFeed(const RssFeed& feed);
  bool updateFeed(size_t index, const RssFeed& feed);
  bool removeFeed(size_t index);

  const std::vector<RssFeed>& getFeeds() const { return feeds; }
  const RssFeed* getFeed(size_t index) const;
  size_t getCount() const { return feeds.size(); }
  bool hasFeeds() const { return !feeds.empty(); }
};

#define RSS_STORE RssFeedStore::getInstance()
