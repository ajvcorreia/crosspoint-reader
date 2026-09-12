#include "RssFeedBrowserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>

#include "MappedInputManager.h"
#include "RssArticleListActivity.h"
#include "RssFeedStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Case-insensitive ordering for the feed list (per spec: "sorted
// alphabetically by name"). Cold path -- runs once per onEnter(), never in a
// render loop.
bool caseInsensitiveLess(const std::string& a, const std::string& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    const auto ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
    const auto cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return ca < cb;
  }
  return a.size() < b.size();
}
}  // namespace

RssFeedBrowserActivity::RssFeedBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("RssFeedBrowser", renderer, mappedInput) {}

void RssFeedBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  RSS_STORE.loadFromFile();
  sortedFeeds = RSS_STORE.getFeeds();
  std::sort(sortedFeeds.begin(), sortedFeeds.end(),
           [](const RssFeed& a, const RssFeed& b) { return caseInsensitiveLess(a.name, b.name); });

  rowItems_.clear();
  rowItems_.reserve(sortedFeeds.size());
  for (size_t i = 0; i < sortedFeeds.size(); i++) {
    fui::ListItem item;
    item.label = sortedFeeds[i].name.empty() ? sortedFeeds[i].url.c_str() : sortedFeeds[i].name.c_str();
    if (!sortedFeeds[i].name.empty()) item.subtitle = sortedFeeds[i].url.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }

  nav.selected = 0;
}

const char* RssFeedBrowserActivity::headerTitle() const { return tr(STR_RSS_READER); }

void RssFeedBrowserActivity::onBackButton() { activityManager.goHome(HomeMenuItem::RSS_READER); }

void RssFeedBrowserActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens the article list; a lingering flash would gray an
  // unrelated row.
  app.clearTapFlash();
  if (index < 0 || static_cast<size_t>(index) >= sortedFeeds.size()) return;
  startActivityForResult(
      std::make_unique<RssArticleListActivity>(renderer, mappedInput, sortedFeeds[static_cast<size_t>(index)]),
      [](const ActivityResult&) {});
}

void RssFeedBrowserActivity::buildScreen(UiScreen& screen) {
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

  if (sortedFeeds.empty()) {
    screen.centeredText(tr(STR_NO_FEEDS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
