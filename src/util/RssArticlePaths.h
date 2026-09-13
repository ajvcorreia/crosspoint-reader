#pragma once

// Shared between RssArticleListActivity (which writes generated article
// EPUBs here -- see its class comment for why they live in one shared,
// feed-ordered folder) and EpubReaderActivity (which recognizes this
// folder to auto-advance straight to the next article at end-of-book,
// instead of showing the usual "Continue with..." choice).
namespace RssArticlePaths {
constexpr const char* ARTICLES_DIR = "/.crosspoint/rss_articles";
}
