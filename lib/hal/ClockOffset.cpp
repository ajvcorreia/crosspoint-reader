#include "ClockOffset.h"

namespace {
bool isLeapYear(const int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(const int y, const int m) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m - 1];
}
}  // namespace

void applyClockOffsetMinutes(int& year, int& month, int& day, int& hour, int& minute, const int offsetMinutes) {
  int totalMinutes = hour * 60 + minute + offsetMinutes;

  // The offset magnitude is bounded to +-14:00, so at most one day boundary
  // is crossed in either direction -- these loops run 0 or 1 times.
  while (totalMinutes < 0) {
    totalMinutes += 24 * 60;
    day--;
    if (day < 1) {
      month--;
      if (month < 1) {
        month = 12;
        year--;
      }
      day = daysInMonth(year, month);
    }
  }
  while (totalMinutes >= 24 * 60) {
    totalMinutes -= 24 * 60;
    day++;
    if (day > daysInMonth(year, month)) {
      day = 1;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }
  }

  hour = totalMinutes / 60;
  minute = totalMinutes % 60;
}
