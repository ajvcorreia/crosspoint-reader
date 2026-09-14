#include <gtest/gtest.h>

#include "ClockOffset.h"

namespace {

struct DateTime {
  int year, month, day, hour, minute;
  bool operator==(const DateTime& o) const {
    return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute;
  }
};

DateTime shift(DateTime dt, const int offsetMinutes) {
  applyClockOffsetMinutes(dt.year, dt.month, dt.day, dt.hour, dt.minute, offsetMinutes);
  return dt;
}

TEST(ClockOffset, ZeroOffsetIsANoOp) {
  EXPECT_EQ(shift({2026, 9, 14, 10, 30}, 0), (DateTime{2026, 9, 14, 10, 30}));
}

TEST(ClockOffset, PositiveShiftWithinDay) {
  EXPECT_EQ(shift({2026, 9, 14, 10, 0}, 60), (DateTime{2026, 9, 14, 11, 0}));
}

TEST(ClockOffset, NegativeShiftWithinDay) {
  EXPECT_EQ(shift({2026, 9, 14, 10, 0}, -60), (DateTime{2026, 9, 14, 9, 0}));
}

TEST(ClockOffset, ForwardDayRollover) {
  EXPECT_EQ(shift({2026, 9, 14, 23, 30}, 60), (DateTime{2026, 9, 15, 0, 30}));
}

TEST(ClockOffset, BackwardDayRollover) {
  EXPECT_EQ(shift({2026, 9, 14, 0, 30}, -60), (DateTime{2026, 9, 13, 23, 30}));
}

TEST(ClockOffset, ForwardMonthRollover) {
  // September has 30 days.
  EXPECT_EQ(shift({2026, 9, 30, 23, 0}, 120), (DateTime{2026, 10, 1, 1, 0}));
}

TEST(ClockOffset, BackwardMonthRollover) {
  EXPECT_EQ(shift({2026, 10, 1, 0, 30}, -60), (DateTime{2026, 9, 30, 23, 30}));
}

TEST(ClockOffset, ForwardYearRollover) {
  EXPECT_EQ(shift({2026, 12, 31, 23, 30}, 60), (DateTime{2027, 1, 1, 0, 30}));
}

TEST(ClockOffset, BackwardYearRollover) {
  EXPECT_EQ(shift({2027, 1, 1, 0, 30}, -60), (DateTime{2026, 12, 31, 23, 30}));
}

TEST(ClockOffset, LeapYearFebruaryTwentyNinth) {
  EXPECT_EQ(shift({2024, 2, 28, 23, 0}, 60), (DateTime{2024, 2, 29, 0, 0}));
}

TEST(ClockOffset, NonLeapYearFebruaryRollsToMarch) {
  EXPECT_EQ(shift({2023, 2, 28, 23, 0}, 60), (DateTime{2023, 3, 1, 0, 0}));
}

TEST(ClockOffset, MaxPositiveTimezoneOffset) {
  // +14:00, the most positive real UTC offset (formatTime's own clamp).
  EXPECT_EQ(shift({2026, 9, 14, 10, 0}, 14 * 60), (DateTime{2026, 9, 15, 0, 0}));
}

TEST(ClockOffset, MaxNegativeTimezoneOffset) {
  // -12:00, the most negative real UTC offset (formatTime's own clamp).
  EXPECT_EQ(shift({2026, 9, 14, 10, 0}, -12 * 60), (DateTime{2026, 9, 13, 22, 0}));
}

}  // namespace
