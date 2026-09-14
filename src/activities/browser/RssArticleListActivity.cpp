#include "RssArticleListActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/RssArticleEpubWriter.h"
#include "util/RssFilename.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;

constexpr const char* FEED_TEMP_PATH = "/.crosspoint/rss_feed.tmp";
constexpr const char* IMAGE_TEMP_PATH = "/.crosspoint/rss_image.tmp";
// A single article's hero image, not a whole gallery -- 2MB comfortably
// covers a real-world article photo while bounding worst-case SD/network
// time for a pathological URL.
constexpr size_t MAX_IMAGE_BYTES = 2 * 1024 * 1024;

// Matches OpdsBookBrowserActivity's own DOWNLOAD_PROGRESS_MIN_UPDATE_MS in
// spirit: caps how often a LOADING screen's progress bar redraws, since
// each redraw is itself a real e-ink refresh that would otherwise add up
// over a long feed fetch or a feed with many articles. Shorter than OPDS's
// own 5000ms: both of this activity's progress phases are typically faster
// overall than a multi-MB book download, so a longer floor would often
// mean the bar never visibly moves at all before the phase finishes.
constexpr unsigned long PROGRESS_MIN_UPDATE_MS = 1000;

// Downloads imageUrl (if it looks like an absolute http(s) URL -- relative
// URLs would need the feed's base URL to resolve and are out of scope for
// now) to a shared temp path and sniffs its format via magic bytes, since
// the reader's own image pipeline only decodes JPEG and PNG (see
// ImageDecoderFactory) and a URL's own extension (if any) can't be trusted.
// Returns the temp path on success (isPng set accordingly) or "" on any
// failure -- low heap, a failed/oversized download, or an unsupported/
// non-image response (a common failure mode: an HTML error page served
// with a 200 status). All failures are non-fatal to the caller, which
// falls back to a text-only article.
std::string downloadArticleImage(const std::string& imageUrl, bool& isPng) {
  if (imageUrl.rfind("http://", 0) != 0 && imageUrl.rfind("https://", 0) != 0) {
    return "";
  }

  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_DBG("RSS", "Skipping article image: low heap");
    return "";
  }

  bool cancelFlag = false;
  const auto progress = [&cancelFlag](const size_t downloaded, const size_t) {
    if (downloaded > MAX_IMAGE_BYTES) cancelFlag = true;
  };
  const auto result = HttpDownloader::downloadToFile(imageUrl, IMAGE_TEMP_PATH, progress, &cancelFlag);
  if (result != HttpDownloader::OK) {
    Storage.remove(IMAGE_TEMP_PATH);
    return "";
  }

  HalFile file = Storage.open(IMAGE_TEMP_PATH);
  if (!file) {
    Storage.remove(IMAGE_TEMP_PATH);
    return "";
  }
  uint8_t header[8] = {0};
  const int bytesRead = file.read(header, sizeof(header));
  file.close();

  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (bytesRead >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
    isPng = false;
  } else if (bytesRead == 8 && std::equal(std::begin(PNG_SIGNATURE), std::end(PNG_SIGNATURE), header)) {
    isPng = true;
  } else {
    Storage.remove(IMAGE_TEMP_PATH);
    return "";
  }

  return IMAGE_TEMP_PATH;
}

// Resolves where this feed's combined EPUB gets written: the feed's own
// folder override, or SETTINGS.rssDownloadFolder (default "/RSS") when
// unset, plus a "<feed name> - <date> - <time>.epub" filename. mkdir/exists
// fallback mirrors OpdsBookBrowserActivity::downloadBook()'s own handling of
// a bad configured folder. Re-opening the same feed within the same
// wall-clock second reproduces an identical path, which HalStorage's
// openFileForWrite() (O_TRUNC) simply overwrites -- no separate dedup logic
// needed. The read is shifted to the same local time the on-screen clock
// shows (SETTINGS.clockUtcOffsetQ) and is best-effort: if it fails (no RTC,
// or never synced), Rtc::DateTime's own defaults (2000-01-01 00:00:00) are
// used, which still produces a valid, if less informative, deterministic
// filename.
std::string resolveFeedEpubPath(const RssFeed& feed) {
  const char* folder = feed.folder.empty() ? SETTINGS.rssDownloadFolder : feed.folder.c_str();
  std::string dir = folder;
  if (!dir.empty() && !Storage.exists(dir.c_str()) && !Storage.mkdir(dir.c_str())) {
    LOG_ERR("RSS", "mkdir failed for %s, using SD root", dir.c_str());
    dir.clear();
  }

  Rtc::DateTime dt;
  halClock.nowLocal(dt, SETTINGS.clockUtcOffsetQ);

  std::string path = dir;
  if (!path.empty()) path += '/';
  path += rssFeedFilename(feed.name.empty() ? feed.url : feed.name, dt.year, dt.month, dt.day, dt.hour, dt.minute,
                          dt.second);
  return path;
}
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
  statusDetail.clear();
  loadingProgress = 0;
  loadingTotal = 0;

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

void RssArticleListActivity::activateSelected() {
  if (articles.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(articles.size())) return;

  // Every article in the feed goes into the one combined book regardless of
  // which row was tapped (see the class comment) -- so which row triggered
  // this only matters for which chapter the user probably wants to land on
  // first, not for what gets built. Rebuilt unconditionally rather than
  // reused across taps: simpler than tracking whether the existing file is
  // still current, and this activity only reaches here when the user is
  // actively about to read, so the cost is never paid for nothing.
  state = BrowserState::LOADING;
  statusMessage = tr(STR_RSS_PREPARING_ARTICLES);
  statusDetail.clear();
  loadingProgress = 0;
  loadingTotal = articles.size();
  requestUpdate(true);

  const std::string feedEpubPath = resolveFeedEpubPath(feed);

  RssArticleEpubWriter::FeedBuilder builder;
  if (!builder.begin(feedEpubPath, feed.name)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_RSS_ARTICLE_OPEN_FAILED);
    loadingTotal = 0;
    requestUpdate();
    return;
  }

  unsigned long lastProgressUpdateMs = millis();
  for (size_t i = 0; i < articles.size(); i++) {
    const auto& article = articles[i];
    // A second status line (statusDetail), not appended onto statusMessage:
    // "Downloading image for: <title>" routinely doesn't fit on one line on
    // this hardware, and a title is exactly the kind of text that shouldn't
    // be silently truncated.
    statusDetail = article.title.empty() ? article.link : article.title;

    std::string imagePath;
    bool imageIsPng = false;
    if (!article.imageUrl.empty()) {
      statusMessage = tr(STR_RSS_DOWNLOADING_IMAGE);
      requestUpdate(true);
      imagePath = downloadArticleImage(article.imageUrl, imageIsPng);
    }

    statusMessage = tr(STR_RSS_ADDING_ARTICLE);

    const bool added = builder.addArticle(article, imagePath, imageIsPng);
    if (!imagePath.empty()) Storage.remove(imagePath.c_str());

    if (!added) {
      // Only a genuine archive-write failure reaches here (a missing/bad
      // image degrades that one chapter to text-only instead, inside
      // addArticle()) -- the archive itself is no longer trustworthy once
      // this happens, so stop rather than keep downloading images for
      // chapters that can no longer be written anyway.
      state = BrowserState::ERROR;
      errorMessage = tr(STR_RSS_ARTICLE_OPEN_FAILED);
      loadingTotal = 0;
      requestUpdate();
      return;
    }

    loadingProgress = i + 1;
    const unsigned long now = millis();
    if (loadingProgress == loadingTotal || now - lastProgressUpdateMs >= PROGRESS_MIN_UPDATE_MS) {
      lastProgressUpdateMs = now;
      requestUpdate(true);
    }

    // This loop can run long (a network fetch per article); make sure it
    // never trips the task watchdog on a feed with many articles.
    resetTaskWatchdogIfSubscribed();
  }

  loadingTotal = 0;

  if (!builder.finish()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_RSS_ARTICLE_OPEN_FAILED);
    requestUpdate();
    return;
  }

  // Each feedEpubPath is normally unique per open (feed name + folder +
  // current date/time), so the reader's spine/TOC/CSS metadata cache (keyed
  // purely by a hash of the path) is naturally fresh -- except on the rare
  // re-open within the same wall-clock second, which reproduces the exact
  // same path/content and would otherwise resurface a stale cache the same
  // way a fixed scratch path always did. clearBookCache() covers that case
  // unconditionally; it's a cheap no-op (Storage.exists() check only) when
  // there's nothing to clear. It also drops the saved reading position for
  // this path, which is what we want: every regeneration should start fresh
  // at chapter 1, not resume into a chapter list that no longer matches.
  clearBookCache(feedEpubPath);

  // Replaces the whole activity stack, same as opening any other book --
  // see the class comment on why that's the right behavior here. Always
  // opens at the book's own saved position (chapter 1, the first time), not
  // necessarily the tapped row's chapter -- see the class comment.
  activityManager.goToReader(feedEpubPath);
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
        // fetchArticles() sets its own state/statusMessage/progress and
        // renders immediately -- no need to pre-set a generic one here.
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

  if (state == BrowserState::LOADING && loadingTotal > 0) {
    // Either fetchArticles()'s feed-download progress or
    // activateSelected()'s book-assembly progress: a live status message
    // (+ an optional second line, e.g. an article title -- see
    // statusDetail's own comment) + a bar, laid out the same way as
    // OpdsBookBrowserActivity's own download screen. Whichever set
    // loadingTotal last owns this screen until it clears it back to 0.
    const int16_t lh = screen.target().lineHeight(centered.font);
    const int16_t gap = screen.theme().spaceMd;
    const int16_t barH = 16;
    const int16_t textLines = statusDetail.empty() ? 1 : 2;
    const int16_t blockH = static_cast<int16_t>(lh * textLines + barH + gap);
    const fui::Rect body = screen.body();
    if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));
    screen.target().text(screen.takeTop(lh, gap), statusMessage.c_str(), centered);
    if (!statusDetail.empty()) {
      screen.target().text(screen.takeTop(lh, gap), statusDetail.c_str(), centered);
    }

    const fui::Rect bar = screen.takeTop(barH, gap).inset(fui::Insets{0, 50, 0, 50});
    fui::ProgressBarProps progress;
    progress.value = static_cast<int32_t>(loadingProgress);
    progress.max = static_cast<int32_t>(loadingTotal);
    progress.border = fui::Paint::solid(fui::Color::Black);
    progress.borderWidth = 1;
    fui::progressBar(screen.frame(), bar, progress);
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
  state = BrowserState::LOADING;
  statusDetail.clear();
  loadingProgress = 0;
  loadingTotal = 0;

  if (feed.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_RSS_NO_FEED_URL);
    requestUpdate();
    return;
  }

  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());

  char connectingBuf[200];
  snprintf(connectingBuf, sizeof(connectingBuf), tr(STR_RSS_CONNECTING_FORMAT),
           feed.name.empty() ? feed.url.c_str() : feed.name.c_str());
  statusMessage = connectingBuf;
  requestUpdate(true);

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

  // Downloaded to a temp file (rather than streamed straight into the
  // parser, as an earlier phase did) specifically so the download has a
  // real (downloaded, total) to show a progress bar from --
  // HttpDownloader's plain streaming fetchUrl() only ever hands over chunk
  // sizes, with no Content-Length visibility.
  statusMessage = tr(STR_RSS_DOWNLOADING_FEED);
  unsigned long lastProgressUpdateMs = millis();
  const auto progress = [this, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
    loadingProgress = downloaded;
    loadingTotal = total;
    const unsigned long now = millis();
    if (now - lastProgressUpdateMs >= PROGRESS_MIN_UPDATE_MS) {
      lastProgressUpdateMs = now;
      requestUpdate(true);
    }
  };
  const auto result = HttpDownloader::downloadToFile(feed.url, FEED_TEMP_PATH, progress);

  if (result != HttpDownloader::OK) {
    Storage.remove(FEED_TEMP_PATH);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    loadingTotal = 0;
    requestUpdate();
    return;
  }

  statusMessage = tr(STR_RSS_PARSING_ARTICLES);
  loadingTotal = 0;  // article count, not byte count, from here -- no bar for this quick step
  requestUpdate(true);

  RssParser parser;
  const bool streamed = Storage.readFileToStream(FEED_TEMP_PATH, parser, 2048);
  parser.flush();
  Storage.remove(FEED_TEMP_PATH);

  if (!streamed || !parser) {
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
    // fetchArticles() sets its own state/statusMessage/progress and
    // renders immediately -- no need to pre-set a generic one here.
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
    // fetchArticles() sets its own state/statusMessage/progress and
    // renders immediately -- no need to pre-set a generic one here.
    fetchArticles();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
