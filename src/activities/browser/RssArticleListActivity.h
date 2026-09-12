#pragma once

#include "RssFeedStore.h"
#include "activities/UiListActivity.h"

/**
 * Shows the articles fetched from one RSS feed.
 *
 * Phase 3 stub: no Wi-Fi connect, no fetch, no parser -- this only proves
 * the feed-list -> article-list navigation works end-to-end. A later phase
 * fills in the real connect/fetch/parse/list behavior (onEnter() gains the
 * Wi-Fi connect + fetch, onExit() gains the disconnect, matching
 * OpdsBookBrowserActivity's own onEnter/onExit Wi-Fi lifecycle) without
 * needing to restructure this class.
 */
class RssArticleListActivity final : public UiListActivity {
 public:
  RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed);

 private:
  int listCount() const override { return 0; }
  void buildScreen(UiScreen& screen) override;
  const char* headerTitle() const override;
  // No rows in this stub (listCount() == 0), so this is never actually
  // invoked -- still required, UiListActivity declares it pure virtual.
  void activateIndex(int) override {}

  // Copied at construction, same rationale as OpdsBookBrowserActivity's own
  // OpdsServer member: safe even if the store changes while this is open.
  RssFeed feed;
};
