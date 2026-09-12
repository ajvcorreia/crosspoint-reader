#pragma once

#include <vector>

#include "RssFeedStore.h"
#include "activities/UiListActivity.h"

/**
 * Home-menu entry point for the RSS reader: lists configured feeds, sorted
 * alphabetically by name, and pushes RssArticleListActivity for whichever
 * one is selected. Read-only -- feed add/edit/delete lives in
 * RssFeedListActivity (Settings -> System -> RSS Feeds), not here.
 */
class RssFeedBrowserActivity final : public UiListActivity {
 public:
  explicit RssFeedBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(sortedFeeds.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  // Reached via ActivityManager::goToRssFeedBrowser(), which replaces the
  // whole stack (same as goToBrowser() does for OPDS) -- so, like
  // OpdsServerListActivity's picker mode, there is no previous activity left
  // to pop back to and this must explicitly return to the home menu.
  void onBackButton() override;

  // Snapshotted on enter. Feeds are only added/edited/removed from the
  // settings screen, which never shares a stack with this activity.
  std::vector<RssFeed> sortedFeeds;
  std::vector<freeink::ui::ListItem> rowItems_;
};
