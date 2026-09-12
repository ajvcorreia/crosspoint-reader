#include "RssArticleListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "components/UITheme.h"

namespace fui = freeink::ui;

RssArticleListActivity::RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed)
    : UiListActivity("RssArticleList", renderer, mappedInput), feed(std::move(feed)) {}

const char* RssArticleListActivity::headerTitle() const {
  return feed.name.empty() ? feed.url.c_str() : feed.name.c_str();
}

void RssArticleListActivity::buildScreen(UiScreen& screen) {
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

  // Phase 3 stub: fetching and parsing land in a later phase. This proves
  // the feed-list -> article-list navigation works end-to-end.
  screen.centeredText(tr(STR_RSS_NO_ARTICLES), screen.theme().bodyText);
}
