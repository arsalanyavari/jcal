/*
 * jalali.c - Tools for manipulating Jalali representation of Iranian calendar
 * and necessary conversations to Gregorian calendar.
 * Copyright (C) 2006, 2007, 2009, 2010, 2011 Ashkan Ghassemi.
 *
 * This file is part of libjalali.
 *
 * libjalali is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libjalali is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libjalali.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "jalali.h"
#include "jconfig.h"

/*
 * Assuming *factor* numbers of *lo* make one *hi*, cluster *lo*s and change
 * *hi* appropriately. In the end:
 * - new lo will be in [0, factor)
 * - new hi will be hi + lo / factor
 */
#define RECLUSTER(hi, lo, factor)                                              \
  if (lo < 0 || lo >= (factor)) {                                              \
    hi += lo / (factor);                                                       \
    lo = lo % (factor);                                                        \
    if (lo < 0) {                                                              \
      lo += (factor);                                                          \
      hi--;                                                                    \
    }                                                                          \
  }

const int cycle_patterns[] = {J_PT0, J_PT1, J_PT2, J_PT3, INT_MAX};
const int leaps[] = {J_L0, J_L1, J_L2, J_L3, INT_MAX};

const int jalali_month_len[] = {31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29};
const int accumulated_jalali_month_len[] = {0,   31,  62,  93,  124, 155,
                                            186, 216, 246, 276, 306, 336};

extern char *tzname[2];

/*
 * Jalali leap year indication function. The algorithm used here
 * is loosely based on the famous recurring 2820 years length period. This
 * period is then divided into 88 cycles, each following a 29, 33, 33, 33
 * years length pattern with the exception for the last being 37 years long.
 * In every of these 29, 33 or 37 years long periods starting with year 0,
 * leap years are multiples of four except for year 0 in each period.
 * The current 2820 year period started in the year AP 475 (AD 1096).
 * https://www.wikifunctions.org/view/en/Z11014
 */
int jalali_is_jleap(int year) {
  // handle the only leap year before 9 that the algorithm can't figure out
  if (year <= 5) {
    int mod = year % 4;
    return (mod == 0);
  }

  int mod = year % 33;
  return (mod == 1 || mod == 5 || mod == 9 || mod == 13 || mod == 17 ||
          mod == 22 || mod == 26 || mod == 30);
}

int gregorian_is_gleap(int year) {
  return (year % 4 == 0) && !(year % 100 == 0 && year % 400 != 0);
}
/*
 * Creates absolute values for day, hour, minute and seconds from time_t.
 * Values are signed integers.
 */
void jalali_create_time_from_secs(time_t t, struct ab_jtm *d) {
  d->ab_days = (t >= 0) ? (t / (time_t)J_DAY_LENGTH_IN_SECONDS)
                        : ((t - (time_t)J_DAY_LENGTH_IN_SECONDS + (time_t)1) /
                           (time_t)J_DAY_LENGTH_IN_SECONDS);

  t %= (time_t)J_DAY_LENGTH_IN_SECONDS;

  d->ab_hour = t / J_HOUR_LENGTH_IN_SECONDS;
  t %= J_HOUR_LENGTH_IN_SECONDS;
  d->ab_min = t / J_MINUTE_LENGTH_IN_SECONDS;
  d->ab_sec = t % J_MINUTE_LENGTH_IN_SECONDS;
}

/*
 * Creates a timestamp from day, hour, minute and seconds.
 * Values are signed integers.
 */
time_t jalali_create_secs_from_time(const struct ab_jtm *d) {
  return (time_t)((time_t)d->ab_days * (time_t)J_DAY_LENGTH_IN_SECONDS +
                  (time_t)d->ab_hour * (time_t)J_HOUR_LENGTH_IN_SECONDS +
                  (time_t)d->ab_min * (time_t)J_MINUTE_LENGTH_IN_SECONDS +
                  (time_t)d->ab_sec);
}

/*
 * Month and day of year calculation for a desired day of year.
 * Alters only tm_mday and tm_mon.
 * Zero on success, -1 on failure.
 */
int jalali_create_date_from_days(struct jtm *j) {
  int p = j->tm_yday;
  if (p > 365 || p < 0)
    return -1;

  p++;
  int i;

  /* Traversing all twelve months, ranging from 0 to 11 */
  for (i = 0; i < 11; i++) {
    if (p > jalali_month_len[i])
      p -= jalali_month_len[i];
    else
      break;
  }

  j->tm_mday = p;
  j->tm_mon = i;

  return 0;
}

/*
 * Calculate day of year (0-365) based on month and day.
 */
int jalali_create_days_from_date(struct jtm *j) {
  int p;
  if (j->tm_mon < 0 || j->tm_mon > 11)
    return -1;

  if (j->tm_mday < 1 || j->tm_mday > 31)
    return -1;

  p = accumulated_jalali_month_len[j->tm_mon];
  p += j->tm_mday;
  j->tm_yday = p - 1 /* zero based offset */;

  return 0;
}

/*
 * Get useful information on a desired jalali year, including:
 * 1. Leap status. -lf
 * 2. Year position in grand leap cycle, passed and remaining years. -p, -r
 * 3. Passed and remaining leap years in grand leap cycle. -pl, -rl
 * 4. Absolute passed leap years since grand leap cycle epoch (AP 475). -apl
 */
void jalali_get_jyear_info(struct jyinfo *year) {
  int y = year->y;
  year->lf = jalali_is_jleap(year->y);
  int i;
  int d = (year->y >= JALALI_LEAP_BASE) ? 1 : -1;
  int c = 0;

  for (i = JALALI_LEAP_BASE;; i += d) {
    if (jalali_is_jleap(i)) {
      c++;
    }

    if (i == year->y)
      break;
  }

  year->apl = c * d;
  year->pl = (d > 0) ? c % JALALI_TOTAL_LEAPS_IN_PERIOD
                     : JALALI_TOTAL_LEAPS_IN_PERIOD -
                           (c % JALALI_TOTAL_LEAPS_IN_PERIOD);
  year->rl = JALALI_TOTAL_LEAPS_IN_PERIOD - year->pl;

  y -= JALALI_LEAP_BASE;
  y %= JALALI_LEAP_PERIOD;
  if (y < 0)
    y += JALALI_LEAP_PERIOD;

  year->p = y;
  year->r = JALALI_LEAP_PERIOD - y - 1;

  return;
}

/*
 * Calculates date (Jalali) based on difference factor from UTC Epoch by days.
 * 0 means 1 January 1970 (11 Dey 1348).
 */
void jalali_get_date(int p, struct jtm *j) {
  int porg = p;
  time_t t;
  struct tm lt;
#if defined _WIN32 || defined __MINGW32__ || defined __CYGWIN__
  struct timezone tz;
  struct timeval tv;
#endif

  int wd = (p + J_UTC_EPOCH_WDAY) % J_WEEK_LENGTH;

  if (wd < 0) {
    j->tm_wday = wd + J_WEEK_LENGTH;
  } else {
    j->tm_wday = wd;
  }

  int y = J_UTC_EPOCH_YEAR, f = 0;
  p += J_UTC_EPOCH_DIFF;
  int d;

  while (1) {
    d = (p >= 0) ? 1 : -1;
    f = jalali_is_jleap(((d > 0) ? y : y - 1))
            ? JALALI_LEAP_YEAR_LENGTH_IN_DAYS
            : JALALI_NORMAL_YEAR_LENGTH_IN_DAYS;

    if ((0 <= p) && (p < f))
      break;

    p -= (d * f);
    y += d;
  }

  j->tm_year = y;
  j->tm_yday = p;

  jalali_create_date_from_days(j);
  tzset();
  t = porg * J_DAY_LENGTH_IN_SECONDS;
  localtime_r(&t, &lt);

#if defined _WIN32 || defined __MINGW32__ || defined __CYGWIN__
  gettimeofday(&tv, &tz);
  j->tm_gmtoff = (-tz.tz_minuteswest) * J_MINUTE_LENGTH_IN_SECONDS +
                 (tz.tz_dsttime * J_HOUR_LENGTH_IN_SECONDS);
  j->tm_zone = tzname[lt.tm_isdst];
#else
  j->tm_gmtoff = lt.tm_gmtoff;
  j->tm_zone = lt.tm_zone;
#endif

  j->tm_isdst = lt.tm_isdst;
}

/*
 * Calculates UTC epoch difference of a desired date by measure of days.
 */
int jalali_get_diff(const struct jtm *j) {
  int p = 0;

  if (j->tm_year >= J_UTC_EPOCH_YEAR) {
    for (int i = J_UTC_EPOCH_YEAR; i < j->tm_year; i++) {
      p += jalali_is_jleap(i) ? JALALI_LEAP_YEAR_LENGTH_IN_DAYS
                              : JALALI_NORMAL_YEAR_LENGTH_IN_DAYS;
    }
    if (j->tm_yday > J_UTC_EPOCH_DIFF)
      p += j->tm_yday - J_UTC_EPOCH_DIFF;
    else
      p -= J_UTC_EPOCH_DIFF - j->tm_yday;
  } else {
    for (int i = j->tm_year; i < J_UTC_EPOCH_YEAR; i++) {
      p -= jalali_is_jleap(i) ? JALALI_LEAP_YEAR_LENGTH_IN_DAYS
                              : JALALI_NORMAL_YEAR_LENGTH_IN_DAYS;
    }
    if (j->tm_yday < J_UTC_EPOCH_DIFF)
      p -= J_UTC_EPOCH_DIFF - j->tm_yday;
    else
      p += j->tm_yday - J_UTC_EPOCH_DIFF;
  }

  return p;
}

/*
 * Number of days in provided year and month
 */
int jalali_year_month_days(int year, int month) {
  int dim = jalali_month_len[month];
  if (month == 11 && jalali_is_jleap(year))
    dim += 1;
  return dim;
}

/*
 * Updates a jalali date struct fields based on tm_year, tm_mon and tm_mday
 */
void jalali_update(struct jtm *jtm) {
  int dim; // number of days in current month
  RECLUSTER(jtm->tm_min, jtm->tm_sec, J_MINUTE_LENGTH_IN_SECONDS);
  RECLUSTER(jtm->tm_hour, jtm->tm_min, J_HOUR_LENGTH_IN_MINUTES);
  RECLUSTER(jtm->tm_mday, jtm->tm_hour, J_DAY_LENGTH_IN_HOURS);

  /* start by calculating a year based on month and change month and year till
   * mday fit */
  RECLUSTER(jtm->tm_year, jtm->tm_mon, J_YEAR_LENGTH_IN_MONTHS);

  if (jtm->tm_mday < 1) {
    /* breaking months to days */
    while (jtm->tm_mday < 1) {
      if (jtm->tm_mon == 0) {
        jtm->tm_mon = 11;
        jtm->tm_year -= 1;
      } else {
        jtm->tm_mon -= 1;
      }
      jtm->tm_mday += jalali_year_month_days(jtm->tm_year, jtm->tm_mon);
    }
  } else {
    /* clustering days as months */
    while (jtm->tm_mday >
           (dim = jalali_year_month_days(jtm->tm_year, jtm->tm_mon))) {
      jtm->tm_mday -= dim;
      if (jtm->tm_mon == 11) {
        jtm->tm_mon = 0;
        jtm->tm_year += 1;
      } else {
        jtm->tm_mon += 1;
      }
    }
  }

  /* date is normalized, compute tm_wday and tm_yday */
  jalali_create_days_from_date(jtm);
  jalali_get_date(jalali_get_diff(jtm), jtm);
}

/*
 * Displays a jalali date struct fields.
 * should be used for debugging purposes only.
 */
void jalali_show_time(const struct jtm *j) {
  printf("%d/%02d/%02d (%02d:%02d:%02d) [%d]", j->tm_year, j->tm_mon + 1,
         j->tm_mday, j->tm_hour, j->tm_min, j->tm_sec, j->tm_wday);
  printf(" yday: %d, dst: %d, off: %ld, zone: %s\n", j->tm_yday, j->tm_isdst,
         j->tm_gmtoff, j->tm_zone);
}

// AI generated code >>
/* Julian Day Number (JDN) calculation for Gregorian calendar
Formula from Jean Meeus' "Astronomical Algorithms" */
int compute_jdn(int year, int month, int day) {
  /* Magic numbers explanation:
     - 4800: Year adjustment for algorithm's epoch
     - 14/12: Converts month to March-based year (0=Mar, 11=Feb)
     - 153: Days in 5 months (30.6 average) for March-based calendar
     - 32045: JDN offset for Gregorian calendar alignment */
  int a = (14 - month) / 12;  // March-based year adjustment (0 or 1)
  int y = year + 4800 - a;    // Adjusted year for algorithm
  int m = month + 12 * a - 3; // Convert month to March-based (0-11)

  return day + (153 * m + 2) / 5 // Day count from March-based months
         + 365 * y               // Add non-leap days
         + y / 4                 // Julian leap years
         - y / 100               // Gregorian century adjustment
         + y / 400               // Gregorian leap year exception
         - 32045;                // Offset to match Gregorian JDN
}
// << AI generated code

int is_valid_jalali(int year, int month, int day) {
  if (year < 1 || year > MAXIMUM_JALALI_YEAR)
    return 0;
  if (month < 1 || month > 12)
    return 0;
  if (day < 1)
    return 0;
  int days_in_month;
  if (month <= 6)
    days_in_month = 31;
  else if (month <= 11)
    days_in_month = 30;
  else
    days_in_month = jalali_is_jleap(year) ? 30 : 29;
  return day <= days_in_month;
}

int is_valid_gregorian(int year, int month, int day) {
  if (year < 1 || year > MAXIMUM_GREGORIAN_YEAR)
    return 0;
  if (month < 1 || month > 12)
    return 0;
  if (day < 1)
    return 0;
  int days_in_month;
  if (month == 2) {
    if (gregorian_is_gleap(year))
      days_in_month = 29;
    else
      days_in_month = 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    days_in_month = 30;
  } else {
    days_in_month = 31;
  }
  return day <= days_in_month;
}