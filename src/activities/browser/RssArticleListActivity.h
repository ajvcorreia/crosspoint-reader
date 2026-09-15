#pragma once

#include <RssParser.h>

#include <string>
#include <vector>

#include "RssFeedStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

/**
 * Fetches an RSS/Atom feed over Wi-Fi, parses it, and immediately assembles
 * every article into one combined EPUB, then opens it via the normal reader
 * -- no article picker in between. Earlier revisions of this activity showed
 * a list of articles first and only built the book once one was tapped, but
 * every article ends up in the same combined book regardless of which row
 * was tapped (see below), so that list never actually chose anything -- it
 * was one extra screen and one extra input before the same outcome. Removed
 * outright rather than kept as an unused code path.
 *
 * Mirrors OpdsBookBrowserActivity's state machine (CHECK_WIFI ->
 * WIFI_SELECTION / LOADING -> ERROR) instead of deriving from UiListActivity:
 * UiListActivity is documented as being for a single list screen only
 * ("state machines... should NOT derive from this"), and this activity needs
 * non-list screens (checking Wi-Fi, loading, error).
 *
 * Unlike OpdsBookBrowserActivity (the root of its own navigation, launched
 * via replaceActivity), this activity is pushed on top of
 * RssFeedBrowserActivity via startActivityForResult(), so Back (before the
 * book opens) calls finish() to pop back to the feed list rather than
 * exiting to Home. That also means onExit() does not reboot the device the
 * way OpdsBookBrowserActivity's does: OPDS's silentRestart() lands on Home,
 * which fits it being the root, but doing that here would blow past the feed
 * list the user expects to return to on a failed/cancelled attempt. Wi-Fi is
 * simply disconnected; any resulting heap fragmentation is a Phase 8
 * (polish) concern if it proves to matter in practice.
 *
 * The combined EPUB (RssArticleEpubWriter::FeedBuilder, backed by the
 * from-scratch STORED-only ZipWriter -- ZipFile only reads) has one chapter
 * per article, each with its hero image embedded if it has one. It's written
 * into the feed's own download folder (RssFeed::folder, falling back to
 * SETTINGS.rssDownloadFolder) as "<feed name> - <date> - <time>.epub" (see
 * resolveFeedEpubPath() in the .cpp) -- so it's a real, persisted library
 * entry, and successive opens normally accumulate distinct snapshots rather
 * than overwrite one another; a re-open within the same wall-clock second is
 * the only case that overwrites (identical path). Reading forward through
 * the feed, and jumping to any specific article, are then just the reader's
 * own ordinary chapter navigation (turning the page past a chapter's last
 * page, or its Contents/TOC panel) -- nothing here needs the reader to know
 * anything RSS-specific to make that work.
 *
 * Fetching the feed and assembling the book (including downloading every
 * article's image) both happen synchronously, back to back, covered by a
 * LOADING screen with a progress bar and a live "here's what's happening
 * right now" status line -- for a feed with many articles this can take a
 * while, so the point of that screen is making the wait legible, not
 * eliminating it.
 */
class RssArticleListActivity final : public Activity, private UiAppHost {
 public:
  RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, ERROR };

  BrowserState state = BrowserState::LOADING;
  std::vector<RssArticle> articles;
  std::string errorMessage;
  std::string statusMessage;
  // Optional second status line, shown below statusMessage on the LOADING
  // screen when set (e.g. an article title, which doesn't fit on the same
  // line as the "Downloading image for:" label ahead of it). Cleared
  // whenever a status line doesn't need a companion.
  std::string statusDetail;

  // Progress for whichever LOADING screen is currently active (feed
  // download in fetchArticles(), or assembling the book in
  // buildAndOpenBook()) -- loadingTotal == 0 means "no bar" (e.g. the
  // brief Wi-Fi-check screen, or a download whose server didn't report a
  // Content-Length). The two phases never overlap, so one pair of members
  // serves both; each phase's units differ (bytes downloaded vs. articles
  // processed) but the display only ever cares about progress/total.
  size_t loadingProgress = 0;
  size_t loadingTotal = 0;

  // Copied at construction, same rationale as OpdsBookBrowserActivity's own
  // OpdsServer member: safe even if the store changes while this is open.
  RssFeed feed;

  static void rootScreen(UiScreen& screen, void* user);
  void screenHeader(UiScreen& screen);
  void buildStatusScreen(UiScreen& screen);
  void buildAndOpenBook();

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchArticles();
  bool preventAutoSleep() override { return true; }
};
