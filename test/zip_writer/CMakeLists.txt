add_executable(ZipWriterTest
  ZipWriterTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipWriter.cpp
)

target_include_directories(ZipWriterTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/ZipFile
)

target_link_libraries(ZipWriterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZipWriterTest)
