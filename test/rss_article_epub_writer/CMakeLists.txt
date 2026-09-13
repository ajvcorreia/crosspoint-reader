add_executable(RssArticleEpubWriterTest
  RssArticleEpubWriterTest.cpp
  ${REPO_ROOT}/src/util/RssArticleEpubWriter.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipWriter.cpp
)

target_include_directories(RssArticleEpubWriterTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/RssParser
  ${REPO_ROOT}/lib/expat
)

# RssArticleEpubWriter.h pulls in RssParser.h purely for the RssArticle
# struct; these definitions only need to make expat.h's declarations
# parse consistently with how the real RssParser target builds it -- no
# expat .c sources are compiled here since nothing in this test ever
# instantiates RssParser itself.
target_compile_definitions(RssArticleEpubWriterTest PRIVATE
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

target_link_libraries(RssArticleEpubWriterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RssArticleEpubWriterTest)
