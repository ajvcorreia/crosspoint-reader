#pragma once

#include <RssParser.h>

#include <string>
#include <vector>

#include "RssFeedStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

/**
 * Fetches an RSS/Atom feed over Wi-Fi, parses it, and lists its articles.
 *
 * Mirrors OpdsBookBrowserActivity's state machine (CHECK_WIFI ->
 * WIFI_SELECTION / LOADING -> BROWSING / ERROR) instead of deriving from
 * UiListActivity: UiListActivity is documented as being for a single list
 * screen only ("state machines... should NOT derive from this"), and this
 * activity needs non-list screens (checking Wi-Fi, loading, error) before
 * the list exists. The Phase 3 stub this replaces was built on
 * UiListActivity on the assumption that later phases could fill in real
 * behavior without restructuring; that assumption didn't survive contact
 * with an actual network fetch, so this is a full rewrite rather than a
 * fill-in.
 *
 * Unlike OpdsBookBrowserActivity (the root of its own navigation, launched
 * via replaceActivity), this activity is pushed on top of
 * RssFeedBrowserActivity via startActivityForResult(), so Back calls
 * finish() to pop back to the feed list rather than exiting to Home. That
 * also means onExit() does not reboot the device the way OpdsBookBrowserActivity's
 * does: OPDS's silentRestart() lands on Home, which fits it being the root,
 * but doing that here would blow past the feed list the user expects to
 * return to. Wi-Fi is simply disconnected; any resulting heap fragmentation
 * is a Phase 8 (polish) concern if it proves to matter in practice.
 *
 * Selecting any article assembles a single EPUB containing every article in
 * the feed as its own chapter (RssArticleEpubWriter::FeedBuilder, backed by
 * the from-scratch STORED-only ZipWriter -- ZipFile only reads), each with
 * its hero image embedded if it has one, and opens it via the normal
 * reader -- the same way any other book is opened. This is a single fixed
 * scratch path, not a library book, overwritten every time any article in
 * this feed is opened (so a fresh fetchArticles() always produces a fully
 * current book; nothing tracks which articles changed since last time).
 * Reading forward through the feed, and jumping to any specific article,
 * are then just the reader's own ordinary chapter navigation (turning the
 * page past a chapter's last page, or its Contents/TOC panel) -- unlike an
 * earlier one-file-per-article design this replaced, nothing here needs
 * the reader to know anything RSS-specific to make that work.
 *
 * Assembling the whole feed (including downloading every article's image)
 * happens synchronously in activateSelected() before the book is opened,
 * covered by a LOADING screen with a progress bar and a live "here's what's
 * happening right now" status line -- for a feed with many articles this
 * can take a while, so the point of that screen is making the wait
 * legible, not eliminating it.
 */
class RssArticleListActivity final : public Activity, private UiAppHost {
 public:
  RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, ERROR };

  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LOADING;
  std::vector<RssArticle> articles;
  // Row buffer, rebuilt whenever `articles` changes (fetchArticles()) so
  // buildBrowsingScreen() reuses it on every repaint instead of rebuilding a
  // ListItem vector per render.
  std::vector<freeink::ui::ListItem> rowItems;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  // Optional second status line, shown below statusMessage on the LOADING
  // screen when set (e.g. an article title, which doesn't fit on the same
  // line as the "Downloading image for:" label ahead of it). Cleared
  // whenever a status line doesn't need a companion.
  std::string statusDetail;

  // Progress for whichever LOADING screen is currently active (feed
  // download in fetchArticles(), or assembling the book in
  // activateSelected()) -- loadingTotal == 0 means "no bar" (e.g. the
  // brief Wi-Fi-check screen, or a download whose server didn't report a
  // Content-Length). The two phases never overlap, so one pair of members
  // serves both; each phase's units differ (bytes downloaded vs. articles
  // processed) but the display only ever cares about progress/total.
  size_t loadingProgress = 0;
  size_t loadingTotal = 0;

  // Copied at construction, same rationale as OpdsBookBrowserActivity's own
  // OpdsServer member: safe even if the store changes while this is open.
  RssFeed feed;

  // Viewport memory (top/visibleRows) for the article list; `selected` is
  // mirrored from selectorIndex at build/move time.
  freeink::ui::ListNav listNav;

  static void rootScreen(UiScreen& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void screenHeader(UiScreen& screen);
  void buildBrowsingScreen(UiScreen& screen);
  void buildStatusScreen(UiScreen& screen);
  void rebuildRowItems();
  void activateSelected();

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchArticles();
  bool preventAutoSleep() override { return true; }
};
