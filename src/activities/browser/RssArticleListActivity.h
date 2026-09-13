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
 * Selecting an article is still a no-op in this phase: opening one requires
 * assembling a minimal EPUB from its content (a later phase), so there is
 * nothing to open yet.
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
