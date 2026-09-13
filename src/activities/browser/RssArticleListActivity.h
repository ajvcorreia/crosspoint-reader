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
 * Selecting an article assembles a minimal EPUB from its content
 * (RssArticleEpubWriter, backed by the from-scratch STORED-only ZipWriter --
 * ZipFile only reads) and opens it via the normal reader, the same way any
 * other book is opened. Opening it goes through ActivityManager::goToReader(),
 * which replaces the whole activity stack -- so Back from the reader lands
 * on Home, exactly like opening any book from the file browser, not back on
 * this list.
 *
 * These EPUBs are scratch output, not library books, but they are written
 * as a shared, feed-ordered folder (one file per article, filenames prefixed
 * with a zero-padded index) rather than to a single fixed path: that is what
 * lets the reader's existing end-of-book "Continue with..." suggestion menu
 * (EndOfBookOptions / NextBookFinder, which finds sibling files in the same
 * folder that sort after the current one) chain forward through the feed
 * with no reader-side changes at all. The folder is cleared and repopulated
 * on every successful fetchArticles(), so a stale previous feed's articles
 * never leak into the current one's chain. Tapping an article (re)writes it
 * plus a short lookahead (EndOfBookOptions::MAX_SUGGESTIONS articles ahead)
 * so those sibling files already exist by the time the reader looks for them.
 *
 * Only the explicitly tapped article's hero image (article.imageUrl) is
 * downloaded and embedded -- lookahead articles stay text-only, since eagerly
 * fetching images for several articles the user hasn't asked to read yet
 * would make every tap noticeably slower for no benefit if they never
 * continue that far. That means an article reached via "Continue with..."
 * (rather than tapped directly from this list) currently opens without its
 * image; a known, deliberate limit of combining eager-lookahead chaining
 * with per-article image embedding, not a bug.
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
  // Path for articles[index]'s generated EPUB inside the shared, feed-ordered
  // scratch folder -- see the class comment.
  std::string articleEpubPath(size_t index) const;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchArticles();
  bool preventAutoSleep() override { return true; }
};
