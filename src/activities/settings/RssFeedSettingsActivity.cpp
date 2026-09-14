#include "RssFeedSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/RssFilename.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Feed Name, Feed URL, Download folder.
// Existing feeds also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 3;
}  // namespace

RssFeedSettingsActivity::RssFeedSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const int feedIndex)
    : UiListActivity("RssFeedSettings", renderer, mappedInput), feedIndex(feedIndex) {
  // Labels never change (unlike the values, which track editFeed's fields
  // live), so they're set once here rather than every buildScreen() call.
  static constexpr StrId fieldNames[BASE_ITEMS] = {StrId::STR_FEED_NAME, StrId::STR_FEED_URL,
                                                    StrId::STR_RSS_DOWNLOAD_FOLDER};
  for (int i = 0; i < BASE_ITEMS; i++) {
    fieldRowItems[i].label = I18N.get(fieldNames[i]);
    fieldRowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fieldRowItems[BASE_ITEMS].label = tr(STR_DELETE_FEED);
  fieldRowItems[BASE_ITEMS].actionValue = static_cast<int16_t>(BASE_ITEMS);
}

int RssFeedSettingsActivity::getMenuItemCount() const {
  return isNewFeed ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void RssFeedSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  isNewFeed = (feedIndex < 0);
  showSaveError = false;

  if (!isNewFeed) {
    // Edit flow: copy the selected feed into local editable state.
    // Changes are persisted field-by-field through saveFeed().
    const auto* feed = RSS_STORE.getFeed(static_cast<size_t>(feedIndex));
    if (feed) {
      editFeed = *feed;
    } else {
      // Feed was deleted between navigation and entering this screen -- treat as new
      isNewFeed = true;
      feedIndex = -1;
    }
  }
}

void RssFeedSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens a keyboard or leaves the screen; a lingering flash would
  // gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
}

bool RssFeedSettingsActivity::saveFeed() {
  bool success = false;

  if (isNewFeed) {
    // Create flow: first save inserts a new feed record into the multi-feed store.
    success = RSS_STORE.addFeed(editFeed);
    if (success) {
      // After the first successful save, promote to an existing feed so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewFeed = false;
      feedIndex = static_cast<int>(RSS_STORE.getCount()) - 1;
    } else {
      LOG_ERR("RSS", "Failed to add RSS feed");
    }
  } else {
    // Edit flow: update the same feed entry in-place.
    success = RSS_STORE.updateFeed(static_cast<size_t>(feedIndex), editFeed);
    if (!success) {
      LOG_ERR("RSS", "Failed to update RSS feed at index %d", feedIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void RssFeedSettingsActivity::handleSelection() {
  // Each field edit is saved immediately so partially configured feeds
  // survive navigation and power-loss scenarios.
  if (nav.selected == 0) {
    // Feed Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editFeed.name = kb.text;
        saveFeed();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FEED_NAME),
                                                                   editFeed.name, 63, InputType::Text),
                           handler);
  } else if (nav.selected == 1) {
    // Feed URL
    const std::string prefillUrl = editFeed.url.empty() ? "https://" : editFeed.url;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editFeed.url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveFeed();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FEED_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (nav.selected == 2) {
    // Download folder. Empty defers to the global default, so the keyboard
    // prefills with whatever this feed has set rather than the fallback.
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editFeed.folder = normalizeRssFolder(kb.text);
        saveFeed();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RSS_DOWNLOAD_FOLDER),
                                                                   editFeed.folder, 63, InputType::Text),
                           handler);
  } else if (nav.selected == 3 && !isNewFeed) {
    // Delete flow is only available for existing feeds.
    if (!RSS_STORE.removeFeed(static_cast<size_t>(feedIndex))) {
      LOG_ERR("RSS", "Failed to remove RSS feed at index %d", feedIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  }
}

void RssFeedSettingsActivity::buildScreen(UiScreen& screen) {
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

  // fieldRowItems' labels/actionValue were set once in the constructor; only
  // the live value pointers (already pointing at editFeed's own fields, no
  // new strings built) need refreshing here.
  fieldRowItems[0].value = editFeed.name.empty() ? tr(STR_NOT_SET) : editFeed.name.c_str();
  fieldRowItems[1].value = editFeed.url.empty() ? tr(STR_NOT_SET) : editFeed.url.c_str();
  // Empty folder falls back to the feed list's default, so show "Default"
  // rather than "Not Set" -- nothing is missing, it is just inherited.
  fieldRowItems[2].value = editFeed.folder.empty() ? tr(STR_DEFAULT_VALUE) : editFeed.folder.c_str();

  fui::ListProps props;
  props.items = fieldRowItems;
  props.count = static_cast<uint16_t>(getMenuItemCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const char* RssFeedSettingsActivity::headerTitle() const {
  return isNewFeed ? tr(STR_ADD_FEED) : tr(STR_RSS_FEEDS);
}

void RssFeedSettingsActivity::drawFooter() {
  UiListActivity::drawFooter();
  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  }
}
