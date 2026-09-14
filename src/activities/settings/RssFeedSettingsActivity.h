#pragma once

#include "RssFeedStore.h"
#include "activities/UiListActivity.h"

/**
 * Edit screen for a single RSS feed.
 * Shows Feed Name, Feed URL, and Download folder fields, plus a Delete option.
 * Used for both adding new feeds and editing existing ones.
 */
class RssFeedSettingsActivity final : public UiListActivity {
 public:
  /**
   * @param feedIndex Index into RssFeedStore, or -1 for a new feed
   */
  explicit RssFeedSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int feedIndex = -1);

  void onEnter() override;

 private:
  int feedIndex;
  RssFeed editFeed;
  bool isNewFeed = false;
  bool showSaveError = false;

  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveFeed();

  // Row storage: at most 4 rows (Name/URL/Folder + Delete, see BASE_ITEMS in
  // the .cpp), so a fixed-capacity array avoids any heap allocation for the
  // row list. Labels are set once in the constructor (they never change);
  // buildScreen() only refreshes the value pointers, which already point at
  // editFeed's own fields (no new strings built).
  static constexpr int MAX_MENU_ITEMS = 4;
  freeink::ui::ListItem fieldRowItems[MAX_MENU_ITEMS]{};
};
