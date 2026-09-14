add_executable(RssFilenameTest
  RssFilenameTest.cpp
  ${REPO_ROOT}/src/util/RssFilename.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(RssFilenameTest PRIVATE
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(RssFilenameTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RssFilenameTest)
