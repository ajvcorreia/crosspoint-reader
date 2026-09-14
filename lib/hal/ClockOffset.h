#pragma once

// Pure calendar-shift arithmetic, no hardware/Arduino dependency. Lives next
// to HalClock (its only caller, for shifting an RTC's UTC reading to a
// display timezone) but is kept standalone so the actual rollover math is
// host-testable on its own.

// Shifts (year, month, day, hour, minute) in place by offsetMinutes (signed),
// with calendar-correct day/month/year rollover across a Gregorian calendar.
// Assumes a valid input date. offsetMinutes is expected to be bounded to a
// real timezone's range (-12:00 to +14:00 -- see HalClock::formatTime's own
// clamp), which crosses at most one day boundary in either direction.
void applyClockOffsetMinutes(int& year, int& month, int& day, int& hour, int& minute, int offsetMinutes);
