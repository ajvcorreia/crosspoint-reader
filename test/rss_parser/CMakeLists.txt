add_executable(RssParserTest
  RssParserTest.cpp
  ${REPO_ROOT}/lib/RssParser/RssParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(RssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/RssParser
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(RssParserTest PRIVATE
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

target_link_libraries(RssParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RssParserTest)
