#include "RssArticleListActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/EndOfBookOptions.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "util/RssArticleEpubWriter.h"
#include "util/StringUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
// Scratch output, not a library book -- see the class comment on why this is
// a shared, feed-ordered folder rather than a single fixed path.
constexpr const char* RSS_ARTICLES_DIR = "/.crosspoint/rss_articles";
}  // namespace

RssArticleListActivity::RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed)
    : Activity("RssArticleList", renderer, mappedInput),
      UiAppHost(renderer),
      buttonNavigator(),
      feed(std::move(feed)) {}

void RssArticleListActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  articles.clear();
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);

  listNav.reset();
  resetUi();
  app.on(ACTION_ROW, &RssArticleListActivity::onRowEvent, this);
  app.setScreen(&RssArticleListActivity::rootScreen, this);
  requestUpdate();

  checkAndConnectWifi();
}

void RssArticleListActivity::onExit() {
  Activity::onExit();
  articles.clear();

  // Unlike OpdsBookBrowserActivity::onExit(), no silentRestart(): this
  // activity is popped back to RssFeedBrowserActivity, not exited to Home,
  // and a reboot-to-Home would blow past that. See the class comment.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
  }
}

std::string RssArticleListActivity::articleEpubPath(const size_t index) const {
  char indexPrefix[4];
  snprintf(indexPrefix, sizeof(indexPrefix), "%02zu", index);

  const auto& article = articles[index];
  std::string path = RSS_ARTICLES_DIR;
  path += "/";
  path += indexPrefix;
  path += " - ";
  path += StringUtils::sanitizeFilename(article.title.empty() ? article.link : article.title);
  path += ".epub";
  return path;
}

void RssArticleListActivity::activateSelected() {
  if (articles.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(articles.size())) return;

  // Writes the tapped article plus a short lookahead so EndOfBookOptions'
  // folder-based "Continue with..." suggestions -- which only consider files
  // that already exist -- have real next-article files ready by the time the
  // reader reaches the end of this one. Unconditionally overwritten rather
  // than tracked: a handful of small re-writes per tap is simpler than
  // bookkeeping which indices are already current, and cheap either way.
  const auto start = static_cast<size_t>(selectorIndex);
  const size_t end = std::min(articles.size(), start + 1 + EndOfBookOptions::MAX_SUGGESTIONS);
  std::string openPath;
  for (size_t i = start; i < end; i++) {
    const std::string path = articleEpubPath(i);
    if (!RssArticleEpubWriter::write(articles[i], path)) {
      if (i == start) {
        state = BrowserState::ERROR;
        errorMessage = tr(STR_RSS_ARTICLE_OPEN_FAILED);
        requestUpdate();
        return;
      }
      break;  // lookahead write failed; the tapped article itself still opens fine
    }
    if (i == start) openPath = path;
  }

  // Replaces the whole activity stack, same as opening any other book --
  // see the class comment on why that's the right behavior here.
  activityManager.goToReader(openPath);
}

void RssArticleListActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<RssArticleListActivity*>(user);
  if (self->state != BrowserState::BROWSING) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->articles.size())) return;
  self->selectorIndex = event.value;
  // The tapped row leaves the screen either way (reader or an error screen);
  // a lingering tap flash would gray an unrelated row on the next list.
  self->app.clearTapFlash();
  self->activateSelected();
}

void RssArticleListActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) return;

  if (state == BrowserState::ERROR) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchArticles();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  // BROWSING
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Touch goes through the FreeInkApp: render() registered every tap target
  // (rows); route the snapshot and let onRowEvent dispatch.
  const auto route = routeTouch(mappedInput);
  if (route.routed) {
    // No pressed-state repaint: the render it triggers would drop a slow
    // tap's release inside the uiReady window, and it costs a second e-ink
    // refresh per tap.
    if (app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onRowEvent
    if (state != BrowserState::BROWSING) return;
  }

  if (!articles.empty()) {
    // Swipes scroll the viewport; the selection stays put (it may scroll
    // off-screen) and button navigation pulls the view back to it.
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? listNav.visibleRows : -listNav.visibleRows;
      if (listNav.scrollBy(delta, static_cast<int>(articles.size()))) requestUpdate();
      return;
    }

    const auto moveSelection = [this](const int index) {
      selectorIndex = index;
      listNav.selected = index;
      listNav.follow(static_cast<int>(articles.size()));
      requestUpdate();
    };
    buttonNavigator.onNextRelease(
        [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, articles.size())); });
    buttonNavigator.onPreviousRelease(
        [this, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, articles.size())); });
    buttonNavigator.onNextContinuous([this, &moveSelection] {
      moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, articles.size(), listNav.visibleRows));
    });
    buttonNavigator.onPreviousContinuous([this, &moveSelection] {
      moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, articles.size(), listNav.visibleRows));
    });
  }
}

void RssArticleListActivity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<RssArticleListActivity*>(user);
  if (self->state == BrowserState::BROWSING) {
    self->buildBrowsingScreen(screen);
  } else {
    self->buildStatusScreen(screen);
  }
}

// Shared chrome for every state: reserve the firmware's button-hint band and
// draw the themed header (padding, centering, and rule come from the theme).
void RssArticleListActivity::screenHeader(UiScreen& screen) {
  screen.takeBottom(static_cast<int16_t>(UITheme::getInstance().getMetrics().buttonHintsHeight));
  // Same top offset as every GUI.drawHeader caller, so the band lines up with
  // the rest of the firmware's screens.
  screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().topPadding));
  fui::HeaderProps header;
  header.title = feed.name.empty() ? feed.url.c_str() : feed.name.c_str();
  header.borderEdges = fui::EdgeBottom;
  screen.header(header);
  // Same breathing room between header and content as the legacy screens.
  screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));
}

void RssArticleListActivity::buildBrowsingScreen(UiScreen& screen) {
  screenHeader(screen);

  if (articles.empty()) {
    screen.centeredText(tr(STR_RSS_NO_ARTICLES), screen.theme().bodyText);
    return;
  }

  // rowItems is rebuilt whenever articles changes (see rebuildRowItems(),
  // called from fetchArticles()) and reused here on every repaint instead of
  // rebuilding a ListItem vector per render.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the nav chevron and the row edge
  listNav.selected = selectorIndex;
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser row height
    // instead of FreeInkUI's touch-target-sized default. Article rows carry
    // a published-date/author subtitle.
    rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listWithSubtitleRowHeight);
    props.rowHeight = rowHeight;
  }
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, static_cast<int>(articles.size()), props);
  screen.list(props);
}

void RssArticleListActivity::buildStatusScreen(UiScreen& screen) {
  screenHeader(screen);

  fui::TextStyle centered = screen.theme().bodyText;
  centered.align = fui::TextAlign::Center;
  if (state == BrowserState::ERROR) {
    const int16_t lh = screen.target().lineHeight(centered.font);
    const int16_t gap = screen.theme().spaceMd;
    const bool showTapHint = mappedInput.hasTouch();
    const int16_t blockH = static_cast<int16_t>(lh * (showTapHint ? 3 : 2) + gap * (showTapHint ? 2 : 1));
    const fui::Rect body = screen.body();
    if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));
    screen.target().text(screen.takeTop(lh, gap), tr(STR_ERROR_MSG), centered);
    screen.target().text(screen.takeTop(lh, gap), errorMessage.c_str(), centered);
    if (showTapHint) screen.target().text(screen.takeTop(lh), tr(STR_TAP_TO_RETRY), centered);
    return;
  }
  // CHECK_WIFI / LOADING (and the brief WifiSelectionActivity handoff state).
  screen.centeredText(statusMessage.c_str(), centered);
}

void RssArticleListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  MappedInputManager::Labels labels;
  switch (state) {
    case BrowserState::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderUi();
  renderer.displayBuffer();
}

void RssArticleListActivity::fetchArticles() {
  if (feed.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_RSS_NO_FEED_URL);
    requestUpdate();
    return;
  }

  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());

  // Same pre-flight floor OpdsBookBrowserActivity applies before a TLS
  // download: below this a session (or its ~17KB record buffer) fails
  // mid-stream instead of failing cleanly up front.
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("RSS", "Low heap for fetch (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  RssParser parser;
  const bool fetched = HttpDownloader::fetchUrl(feed.url, [&parser](const uint8_t* data, const size_t len) {
    parser.write(data, len);
    return true;  // RssParser has no natural "abort mid-stream" signal; never abort here
  });
  parser.flush();

  if (!fetched) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  const bool feedTruncated = parser.truncated();

  // Reset the selection before the swap: the render task reads
  // articles[selectorIndex] under only an empty() guard, and the new feed
  // can be shorter than the old selection.
  selectorIndex = 0;
  listNav.reset();
  articles = std::move(parser).getArticles();

  if (feedTruncated) {
    LOG_INF("RSS", "Feed truncated to fit memory");
  }

  // Fresh feed load: drop any per-article EPUBs left over from a previous
  // feed so they can never leak into this feed's "Continue with..." chain
  // (see the class comment). Recreated even when empty, so it's always in
  // sync with whatever this fetch produced.
  if (Storage.exists(RSS_ARTICLES_DIR)) {
    Storage.removeDir(RSS_ARTICLES_DIR);
  }
  Storage.mkdir(RSS_ARTICLES_DIR);

  state = articles.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (articles.empty()) errorMessage = tr(STR_RSS_NO_ARTICLES);
  rebuildRowItems();
  requestUpdate();
}

// Derives rowItems from articles. Called whenever articles changes
// (fetchArticles()) so buildBrowsingScreen() reuses the cached rows on every
// repaint instead of rebuilding them per render.
void RssArticleListActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(articles.size());
  for (size_t i = 0; i < articles.size(); i++) {
    const auto& article = articles[i];
    fui::ListItem item;
    item.label = article.title.empty() ? article.link.c_str() : article.title.c_str();
    // No date-formatting utility exists in this codebase yet -- publishedAt
    // is the feed's raw pubDate/updated/published string; show it as-is.
    // Pretty-formatting is a polish-phase concern, not a fetch/parse/list one.
    if (!article.publishedAt.empty()) {
      item.subtitle = article.publishedAt.c_str();
    } else if (!article.author.empty()) {
      item.subtitle = article.author.c_str();
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void RssArticleListActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchArticles();
    return;
  }
  launchWifiSelection();
}

void RssArticleListActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssArticleListActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchArticles();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
