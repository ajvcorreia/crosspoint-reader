#pragma once

#include <vector>

#include "activities/UiListActivity.h"

/**
 * Activity showing the list of configured RSS feeds.
 * Allows adding new feeds and editing/deleting existing ones.
 */
class RssFeedListActivity final : public UiListActivity {
 public:
  explicit RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return getItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row structure (labels, actionValue, feed subtitles), rebuilt only when
  // the feed list itself reloads (rebuildRowItems(), called from onEnter()
  // and after returning from the feed editor) -- not on every repaint.
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

  int getItemCount() const;
  void handleSelection();
};
