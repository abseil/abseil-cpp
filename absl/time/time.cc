// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The implementation of the absl::Time class, which is declared in
// //absl/time.h.
//
// The representation for an absl::Time is an absl::Duration offset from the
// epoch.  We use the traditional Unix epoch (1970-01-01 00:00:00 +0000)
// for convenience, but this is not exposed in the API and could be changed.
//
// NOTE: To keep type verbosity to a minimum, the following variable naming
// conventions are used throughout this file.
//
// cz: A cctz::time_zone
// tz: An absl::TimeZone
// cl: A cctz::time_zone::civil_lookup
// al: A cctz::time_zone::absolute_lookup
// cd: A cctz::civil_day
// cs: A cctz::civil_second
// bd: An absl::Time::Breakdown

#include "absl/time/time.h"

#include <cstring>
#include <ctime>
#include <limits>

#include "cctz/civil_time.h"
#include "cctz/time_zone.h"
namespace absl {

namespace {

inline cctz::time_point<cctz::sys_seconds> unix_epoch() {
  return std::chrono::time_point_cast<cctz::sys_seconds>(
      std::chrono::system_clock::from_time_t(0));
}

// Floors d to the next unit boundary closer to negative infinity.
inline int64_t FloorToUnit(absl::Duration d, absl::Duration unit) {
  absl::Duration rem;
  int64_t q = absl::IDivDuration(d, unit, &rem);
  return (q > 0 ||
          rem >= ZeroDuration() ||
          q == std::numeric_limits<int64_t>::min()) ? q : q - 1;
}

inline absl::Time::Breakdown InfiniteFutureBreakdown() {
  absl::Time::Breakdown bd;
  bd.year = std::numeric_limits<int64_t>::max();
  bd.month = 12;
  bd.day = 31;
  bd.hour = 23;
  bd.minute = 59;
  bd.second = 59;
  bd.subsecond = absl::InfiniteDuration();
  bd.weekday = 4;
  bd.yearday = 365;
  bd.offset = 0;
  bd.is_dst = false;
  bd.zone_abbr = "-0000";
  return bd;
}

inline Time::Breakdown InfinitePastBreakdown() {
  Time::Breakdown bd;
  bd.year = std::numeric_limits<int64_t>::min();
  bd.month = 1;
  bd.day = 1;
  bd.hour = 0;
  bd.minute = 0;
  bd.second = 0;
  bd.subsecond = -absl::InfiniteDuration();
  bd.weekday = 7;
  bd.yearday = 1;
  bd.offset = 0;
  bd.is_dst = false;
  bd.zone_abbr = "-0000";
  return bd;
}

inline absl::TimeConversion InfiniteFutureTimeConversion() {
  absl::TimeConversion tc;
  tc.pre = tc.trans = tc.post = absl::InfiniteFuture();
  tc.kind = absl::TimeConversion::UNIQUE;
  tc.normalized = true;
  return tc;
}

inline TimeConversion InfinitePastTimeConversion() {
  absl::TimeConversion tc;
  tc.pre = tc.trans = tc.post = absl::InfinitePast();
  tc.kind = absl::TimeConversion::UNIQUE;
  tc.normalized = true;
  return tc;
}

// Makes a Time from sec, overflowing to InfiniteFuture/InfinitePast as
// necessary. If sec is min/max, then consult cs+tz to check for overlow.
Time MakeTimeWithOverflow(const cctz::time_point<cctz::sys_seconds>& sec,
                          const cctz::civil_second& cs,
                          const cctz::time_zone& tz,
                          bool* normalized = nullptr) {
  const auto max = cctz::time_point<cctz::sys_seconds>::max();
  const auto min = cctz::time_point<cctz::sys_seconds>::min();
  if (sec == max) {
    const auto al = tz.lookup(max);
    if (cs > al.cs) {
      if (normalized) *normalized = true;
      return absl::InfiniteFuture();
    }
  }
  if (sec == min) {
    const auto al = tz.lookup(min);
    if (cs < al.cs) {
      if (normalized) *normalized = true;
      return absl::InfinitePast();
    }
  }
  const auto hi = (sec - unix_epoch()).count();
  return time_internal::FromUnixDuration(time_internal::MakeDuration(hi));
}

inline absl::TimeConversion::Kind MapKind(
    const cctz::time_zone::civil_lookup::civil_kind& kind) {
  switch (kind) {
    case cctz::time_zone::civil_lookup::UNIQUE:
      return absl::TimeConversion::UNIQUE;
    case cctz::time_zone::civil_lookup::SKIPPED:
      return absl::TimeConversion::SKIPPED;
    case cctz::time_zone::civil_lookup::REPEATED:
      return absl::TimeConversion::REPEATED;
  }
  return absl::TimeConversion::UNIQUE;
}

// Returns Mon=1..Sun=7.
inline int MapWeekday(const cctz::weekday& wd) {
  switch (wd) {
    case cctz::weekday::monday:
      return 1;
    case cctz::weekday::tuesday:
      return 2;
    case cctz::weekday::wednesday:
      return 3;
    case cctz::weekday::thursday:
      return 4;
    case cctz::weekday::friday:
      return 5;
    case cctz::weekday::saturday:
      return 6;
    case cctz::weekday::sunday:
      return 7;
  }
  return 1;
}

}  // namespace

absl::Time::Breakdown Time::In(absl::TimeZone tz) const {
  if (*this == absl::InfiniteFuture()) return absl::InfiniteFutureBreakdown();
  if (*this == absl::InfinitePast()) return absl::InfinitePastBreakdown();

  const auto tp =
      unix_epoch() + cctz::sys_seconds(time_internal::GetRepHi(rep_));
  const auto al = cctz::time_zone(tz).lookup(tp);
  const auto cs = al.cs;
  const auto cd = cctz::civil_day(cs);

  absl::Time::Breakdown bd;
  bd.year = cs.year();
  bd.month = cs.month();
  bd.day = cs.day();
  bd.hour = cs.hour();
  bd.minute = cs.minute();
  bd.second = cs.second();
  bd.subsecond = time_internal::MakeDuration(0, time_internal::GetRepLo(rep_));
  bd.weekday = MapWeekday(get_weekday(cd));
  bd.yearday = get_yearday(cd);
  bd.offset = al.offset;
  bd.is_dst = al.is_dst;
  bd.zone_abbr = al.abbr;
  return bd;
}

absl::Time FromTM(const struct tm& tm, absl::TimeZone tz) {
  const auto cz = cctz::time_zone(tz);
  const auto cs =
      cctz::civil_second(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
  const auto cl = cz.lookup(cs);
  const auto tp = tm.tm_isdst == 0 ? cl.post : cl.pre;
  return MakeTimeWithOverflow(tp, cs, cz);
}

struct tm ToTM(absl::Time t, absl::TimeZone tz) {
  const absl::Time::Breakdown bd = t.In(tz);
  struct tm tm;
  std::memset(&tm, 0, sizeof(tm));
  tm.tm_sec = bd.second;
  tm.tm_min = bd.minute;
  tm.tm_hour = bd.hour;
  tm.tm_mday = bd.day;
  tm.tm_mon = bd.month - 1;

  // Saturates tm.tm_year in cases of over/underflow, accounting for the fact
  // that tm.tm_year is years since 1900.
  if (bd.year < std::numeric_limits<int>::min() + 1900) {
    tm.tm_year = std::numeric_limits<int>::min();
  } else if (bd.year > std::numeric_limits<int>::max()) {
    tm.tm_year = std::numeric_limits<int>::max() - 1900;
  } else {
    tm.tm_year = static_cast<int>(bd.year - 1900);
  }

  tm.tm_wday = bd.weekday % 7;
  tm.tm_yday = bd.yearday - 1;
  tm.tm_isdst = bd.is_dst ? 1 : 0;

  return tm;
}

//
// Factory functions.
//

absl::TimeConversion ConvertDateTime(int64_t year, int mon, int day, int hour,
                                     int min, int sec, TimeZone tz) {
  // Avoids years that are too extreme for civil_second to normalize.
  if (year > 300000000000) return InfiniteFutureTimeConversion();
  if (year < -300000000000) return InfinitePastTimeConversion();
  const auto cz = cctz::time_zone(tz);
  const auto cs = cctz::civil_second(year, mon, day, hour, min, sec);
  absl::TimeConversion tc;
  tc.normalized = year != cs.year() || mon != cs.month() || day != cs.day() ||
                  hour != cs.hour() || min != cs.minute() || sec != cs.second();
  const auto cl = cz.lookup(cs);
  // Converts the civil_lookup struct to a TimeConversion.
  tc.pre = MakeTimeWithOverflow(cl.pre, cs, cz, &tc.normalized);
  tc.trans = MakeTimeWithOverflow(cl.trans, cs, cz, &tc.normalized);
  tc.post = MakeTimeWithOverflow(cl.post, cs, cz, &tc.normalized);
  tc.kind = MapKind(cl.kind);
  return tc;
}

absl::Time FromDateTime(int64_t year, int mon, int day, int hour, int min,
                        int sec, TimeZone tz) {
  if (year > 300000000000) return InfiniteFuture();
  if (year < -300000000000) return InfinitePast();
  const auto cz = cctz::time_zone(tz);
  const auto cs = cctz::civil_second(year, mon, day, hour, min, sec);
  const auto cl = cz.lookup(cs);
  return MakeTimeWithOverflow(cl.pre, cs, cz);
}

absl::Time TimeFromTimespec(timespec ts) {
  return time_internal::FromUnixDuration(absl::DurationFromTimespec(ts));
}

absl::Time TimeFromTimeval(timeval tv) {
  return time_internal::FromUnixDuration(absl::DurationFromTimeval(tv));
}

absl::Time FromUDate(double udate) {
  return time_internal::FromUnixDuration(absl::Milliseconds(udate));
}

absl::Time FromUniversal(int64_t universal) {
  return absl::UniversalEpoch() + 100 * absl::Nanoseconds(universal);
}

//
// Conversion to other time types.
//

int64_t ToUnixNanos(Time t) {
  if (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >= 0 &&
      time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >> 33 == 0) {
    return (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) *
            1000 * 1000 * 1000) +
           (time_internal::GetRepLo(time_internal::ToUnixDuration(t)) / 4);
  }
  return FloorToUnit(time_internal::ToUnixDuration(t), absl::Nanoseconds(1));
}

int64_t ToUnixMicros(Time t) {
  if (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >= 0 &&
      time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >> 43 == 0) {
    return (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) *
            1000 * 1000) +
           (time_internal::GetRepLo(time_internal::ToUnixDuration(t)) / 4000);
  }
  return FloorToUnit(time_internal::ToUnixDuration(t), absl::Microseconds(1));
}

int64_t ToUnixMillis(Time t) {
  if (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >= 0 &&
      time_internal::GetRepHi(time_internal::ToUnixDuration(t)) >> 53 == 0) {
    return (time_internal::GetRepHi(time_internal::ToUnixDuration(t)) * 1000) +
           (time_internal::GetRepLo(time_internal::ToUnixDuration(t)) /
            (4000 * 1000));
  }
  return FloorToUnit(time_internal::ToUnixDuration(t), absl::Milliseconds(1));
}

int64_t ToUnixSeconds(Time t) {
  return time_internal::GetRepHi(time_internal::ToUnixDuration(t));
}

time_t ToTimeT(Time t) { return absl::ToTimespec(t).tv_sec; }

timespec ToTimespec(Time t) {
  timespec ts;
  absl::Duration d = time_internal::ToUnixDuration(t);
  if (!time_internal::IsInfiniteDuration(d)) {
    ts.tv_sec = time_internal::GetRepHi(d);
    if (ts.tv_sec == time_internal::GetRepHi(d)) {  // no time_t narrowing
      ts.tv_nsec = time_internal::GetRepLo(d) / 4;  // floor
      return ts;
    }
  }
  if (d >= absl::ZeroDuration()) {
    ts.tv_sec = std::numeric_limits<time_t>::max();
    ts.tv_nsec = 1000 * 1000 * 1000 - 1;
  } else {
    ts.tv_sec = std::numeric_limits<time_t>::min();
    ts.tv_nsec = 0;
  }
  return ts;
}

timeval ToTimeval(Time t) {
  timeval tv;
  timespec ts = absl::ToTimespec(t);
  tv.tv_sec = ts.tv_sec;
  if (tv.tv_sec != ts.tv_sec) {  // narrowing
    if (ts.tv_sec < 0) {
      tv.tv_sec = std::numeric_limits<decltype(tv.tv_sec)>::min();
      tv.tv_usec = 0;
    } else {
      tv.tv_sec = std::numeric_limits<decltype(tv.tv_sec)>::max();
      tv.tv_usec = 1000 * 1000 - 1;
    }
    return tv;
  }
  tv.tv_usec = static_cast<int>(ts.tv_nsec / 1000);  // suseconds_t
  return tv;
}

double ToUDate(Time t) {
  return absl::FDivDuration(time_internal::ToUnixDuration(t),
                            absl::Milliseconds(1));
}

int64_t ToUniversal(absl::Time t) {
  return absl::FloorToUnit(t - absl::UniversalEpoch(), absl::Nanoseconds(100));
}

Time FromChrono(const std::chrono::system_clock::time_point& tp) {
  return time_internal::FromUnixDuration(time_internal::FromChrono(
      tp - std::chrono::system_clock::from_time_t(0)));
}

std::chrono::system_clock::time_point ToChronoTime(absl::Time t) {
  using D = std::chrono::system_clock::duration;
  auto d = time_internal::ToUnixDuration(t);
  if (d < ZeroDuration()) d = Floor(d, FromChrono(D{1}));
  return std::chrono::system_clock::from_time_t(0) +
         time_internal::ToChronoDuration<D>(d);
}

}  // namespace absl
