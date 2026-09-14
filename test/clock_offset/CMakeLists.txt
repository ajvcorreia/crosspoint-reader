add_executable(ClockOffsetTest
  ClockOffsetTest.cpp
  ${REPO_ROOT}/lib/hal/ClockOffset.cpp
)

target_include_directories(ClockOffsetTest PRIVATE
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(ClockOffsetTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ClockOffsetTest)
