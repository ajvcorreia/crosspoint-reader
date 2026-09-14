#include "RssFeedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RssFeedSettingsActivity.h"
#include "RssFeedStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/RssFilename.h"

namespace fui = freeink::ui;

int RssFeedListActivity::getItemCount() const {
  // Index layout: [feeds 0..count-1], [Add Feed], [Default download folder]
  return static_cast<int>(RSS_STORE.getCount()) + 2;
}

RssFeedListActivity::RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("RssFeedList", renderer, mappedInput) {}

void RssFeedListActivity::onEnter() {
  UiListActivity::onEnter();

  // Reload from disk in case feeds were added/removed by a subactivity or the web UI
  RSS_STORE.loadFromFile();
  nav.selected = 0;
  rebuildRowItems();
}

// Rebuilds rowItems_ from RSS_STORE. Structural -- call only when the feed
// list itself reloads, not from buildScreen().
void RssFeedListActivity::rebuildRowItems() {
  rowItems_.clear();
  const int itemCount = getItemCount();
  rowItems_.reserve(itemCount);

  const auto& feeds = RSS_STORE.getFeeds();
  const auto feedCount = static_cast<int>(feeds.size());
  for (int i = 0; i < feedCount; i++) {
    fui::ListItem item;
    item.label = feeds[i].name.empty() ? feeds[i].url.c_str() : feeds[i].name.c_str();
    if (!feeds[i].name.empty()) item.subtitle = feeds[i].url.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }

  fui::ListItem addFeed;
  addFeed.label = tr(STR_ADD_FEED);
  addFeed.actionValue = static_cast<int16_t>(feedCount);
  rowItems_.push_back(addFeed);

  fui::ListItem folder;
  folder.label = tr(STR_RSS_DEFAULT_DOWNLOAD_FOLDER);
  folder.actionValue = static_cast<int16_t>(feedCount + 1);
  rowItems_.push_back(folder);  // subtitle refreshed per render in buildScreen()
}

const char* RssFeedListActivity::headerTitle() const { return tr(STR_RSS_FEEDS); }

void RssFeedListActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens the editor; a lingering flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void RssFeedListActivity::handleSelection() {
  const auto feedCount = static_cast<int>(RSS_STORE.getCount());

  // Index layout: [feeds 0..feedCount-1], [Add Feed], [Default download folder].
  if (nav.selected == feedCount + 1) {
    auto folderHandler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const std::string norm = normalizeRssFolder(kb.text);
        strncpy(SETTINGS.rssDownloadFolder, norm.c_str(), sizeof(SETTINGS.rssDownloadFolder) - 1);
        SETTINGS.rssDownloadFolder[sizeof(SETTINGS.rssDownloadFolder) - 1] = '\0';
        SETTINGS.saveToFile();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RSS_DEFAULT_DOWNLOAD_FOLDER),
                                                std::string(SETTINGS.rssDownloadFolder), 63, InputType::Text),
        folderHandler);
    return;
  }

  auto resultHandler = [this](const ActivityResult&) {
    // Reload feed list when returning from editor
    RSS_STORE.loadFromFile();
    nav.selected = 0;
    rebuildRowItems();
  };

  if (nav.selected < feedCount) {
    startActivityForResult(std::make_unique<RssFeedSettingsActivity>(renderer, mappedInput, nav.selected),
                           resultHandler);
  } else {
    startActivityForResult(std::make_unique<RssFeedSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void RssFeedListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints; derived
  // from the safe area so board bezel insets apply (same as LanguageSelect).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ (labels/actionValue, feed subtitles) was built by
  // rebuildRowItems() when the feed list last reloaded; only the folder
  // row's live subtitle needs refreshing here (pointer reassignment onto an
  // already-owned string -- no allocation).
  const auto feedCount = static_cast<int>(RSS_STORE.getCount());
  rowItems_[feedCount + 1].subtitle =
      SETTINGS.rssDownloadFolder[0] ? SETTINGS.rssDownloadFolder : tr(STR_OPDS_SD_ROOT);

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
