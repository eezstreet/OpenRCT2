/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef _DATE_H_
#define _DATE_H_

#include "../common.h"

constexpr int32_t MAX_YEAR = 8192;

enum
{
    MONTH_JANUARY,
    MONTH_FEBRUARY,
    MONTH_MARCH,
    MONTH_APRIL,
    MONTH_MAY,
    MONTH_JUNE,
    MONTH_JULY,
    MONTH_AUGUST,
    MONTH_SEPTEMBER,
    MONTH_OCTOBER,
    MONTH_NOVEMBER,
    MONTH_DECEMBER,

    MONTH_COUNT
};

enum
{
    DATE_FORMAT_DAY_MONTH_YEAR,
    DATE_FORMAT_MONTH_DAY_YEAR,
    DATE_FORMAT_YEAR_MONTH_DAY,
    DATE_FORMAT_YEAR_DAY_MONTH
};

struct openrct_timeofday
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
};

extern const int16_t days_in_month[MONTH_COUNT];
extern const rct_string_id DateFormatStringIds[];
extern const rct_string_id DateFormatStringFormatIds[];

extern uint16_t gDateMonthTicks;
extern uint16_t gDateMonthsElapsed;

extern openrct_timeofday gRealTimeOfDay;

// gets the number of months in a business year
int32_t date_get_months_in_year();

// converts number of months into real date months (eg, month 0 on vanilla RCT = March, so return 2)
int32_t date_get_month(int32_t months);

// returns the real year based on the number of months passed
int32_t date_get_year(int32_t months);

int32_t date_get_total_months(int32_t month, int32_t year);

// variant of date_get_total_months with starting and ending month explicitly stated
int32_t date_get_total_months(int32_t starting_month, int32_t current_month, int32_t year);
void date_reset();
void date_update();
void date_set(int32_t year, int32_t month, int32_t day);
void date_update_real_time_of_day();
bool date_is_day_start(int32_t monthTicks);
bool date_is_week_start(int32_t monthTicks);
bool date_is_fortnight_start(int32_t monthTicks);
bool date_is_month_start(int32_t monthTicks);

#endif
