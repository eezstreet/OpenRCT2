/*****************************************************************************
 * Copyright (c) 2019 eezstreet
 *
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef _SCENARIO_H
#define _SCENARIO_H

#include "ScenarioVanilla.h"

#pragma pack(push, 1)
/**
 * SV7/SC7 header chunk
 * size: 0x20
 */
struct rct_scenario_header
{
    uint8_t type;                // 0x00
    uint8_t classic_flag;        // 0x01
    uint16_t num_packed_objects; // 0x02
    uint32_t version;            // 0x04
    uint32_t magic_number;       // 0x08
    uint8_t pad_0C[0x14];
};
assert_struct_size(rct_scenario_header, 0x20);

/**
 * SC7 information chunk
 * size: 0x198
 */
struct rct_scenario_info
{
    uint8_t editor_step;
    uint8_t category;        // 0x01
    uint8_t objective_type;  // 0x02
    uint8_t objective_arg_1; // 0x03
    int32_t objective_arg_2; // 0x04
    int16_t objective_arg_3; // 0x08
    uint8_t starting_month;  // new
    uint8_t ending_month;    // new
    uint8_t pad_00A[0x3C];
    char name[64];          // 0x48
    char details[256];      // 0x88
    rct_object_entry entry; // 0x188
};
assert_struct_size(rct_scenario_info, 0x198);

// This is the scenario struct for my custom RCT2 scenario variant - SC7.
struct rct_scenario_data
{
    // SC6[0]
    rct_scenario_header header;

    // SC6[1]
    rct_scenario_info info;

    // SC6[2]
    // packed objects

    // SC6[3]
    rct_object_entry objects[OBJECT_ENTRY_COUNT];

    // SC6[4]
    uint16_t elapsed_months;
    uint16_t current_day;
    uint32_t scenario_ticks;
    uint32_t scenario_srand_0;
    uint32_t scenario_srand_1;

    // SC6[5]
    RCT12TileElement tile_elements[RCT2_MAX_TILE_ELEMENTS];

    // SC6[6]
    uint32_t next_free_tile_element_pointer_index;
    RCT2Sprite sprites[RCT2_MAX_SPRITES];
    uint16_t sprite_lists_head[6];
    uint16_t sprite_lists_count[6];
    rct_string_id park_name;
    uint8_t pad_013573D6[2];
    uint32_t park_name_args;
    money32 initial_cash;
    money32 current_loan;
    uint32_t park_flags;
    money16 park_entrance_fee;
    uint16_t rct1_park_entrance_x;
    uint16_t rct1_park_entrance_y;
    uint8_t pad_013573EE[2];
    uint8_t rct1_park_entrance_z;
    uint8_t pad_013573F1;
    rct12_peep_spawn peep_spawns[RCT12_MAX_PEEP_SPAWNS];
    uint8_t guest_count_change_modifier;
    uint8_t current_research_level;
    uint8_t pad_01357400[4];
    uint32_t researched_ride_types[RCT2_MAX_RESEARCHED_RIDE_TYPE_QUADS];
    uint32_t researched_ride_entries[RCT2_MAX_RESEARCHED_RIDE_ENTRY_QUADS];
    uint32_t researched_track_types_a[128];
    uint32_t researched_track_types_b[128];

    // SC6[7]
    uint16_t guests_in_park;
    uint16_t guests_heading_for_park;

    // Ignored in scenario
    money32 expenditure_table[RCT12_EXPENDITURE_TABLE_MONTH_COUNT][RCT12_EXPENDITURE_TYPE_COUNT];

    // SC6[8]
    uint16_t last_guests_in_park;
    uint8_t pad_01357BCA[3];
    uint8_t handyman_colour;
    uint8_t mechanic_colour;
    uint8_t security_colour;

    // Ignored in scenario
    uint32_t researched_scenery_items[RCT2_MAX_RESEARCHED_SCENERY_ITEM_QUADS];

    // SC6[9]
    uint16_t park_rating;

    // Ignored in scenario
    uint8_t park_rating_history[32];
    uint8_t guests_in_park_history[32];

    // SC6[10]
    uint8_t active_research_types;
    uint8_t research_progress_stage;
    uint32_t last_researched_item_subject;
    uint8_t pad_01357CF8[1000];
    uint32_t next_research_item;
    uint16_t research_progress;
    uint8_t next_research_category;
    uint8_t next_research_expected_day;
    uint8_t next_research_expected_month;
    uint8_t guest_initial_happiness;
    uint16_t park_size;
    uint16_t guest_generation_probability;
    uint16_t total_ride_value_for_money;
    money32 maximum_loan;
    money16 guest_initial_cash;
    uint8_t guest_initial_hunger;
    uint8_t guest_initial_thirst;
    uint8_t objective_type;
    uint8_t objective_year;
    uint8_t pad_013580FA[2];
    money32 objective_currency;
    uint16_t objective_guests;
    uint8_t campaign_weeks_left[20];
    uint8_t campaign_ride_index[22];

    // Ignored in scenario
    money32 balance_history[RCT12_FINANCE_GRAPH_SIZE];

    // SC6[11]
    money32 current_expenditure;
    money32 current_profit;
    uint32_t weekly_profit_average_dividend;
    uint16_t weekly_profit_average_divisor;
    uint8_t pad_0135833A[2];

    // Ignored in scenario
    money32 weekly_profit_history[RCT12_FINANCE_GRAPH_SIZE];

    // SC6[12]
    money32 park_value;

    // Ignored in scenario
    money32 park_value_history[RCT12_FINANCE_GRAPH_SIZE];

    // SC6[13]
    money32 completed_company_value;
    uint32_t total_admissions;
    money32 income_from_admissions;
    money32 company_value;
    uint8_t peep_warning_throttle[16];
    rct12_award awards[RCT12_MAX_AWARDS];
    money16 land_price;
    money16 construction_rights_price;
    uint16_t word_01358774;
    uint8_t guest_generation_algorithm;
    uint8_t advertisement_generation_algorithm;
    uint32_t cd_key;
    uint8_t pad_0135877C[64];
    uint32_t game_version_number;
    money32 completed_company_value_record;
    uint32_t loan_hash;
    uint16_t ride_count;
    uint8_t pad_013587CA[6];
    money32 historical_profit;
    uint8_t pad_013587D4[4];
    char scenario_completed_name[32];
    money32 cash;
    uint8_t pad_013587FC[50];
    uint16_t park_rating_casualty_penalty;
    uint16_t map_size_units;
    uint16_t map_size_minus_2;
    uint16_t map_size;
    uint16_t map_max_xy;
    uint32_t same_price_throughout;
    uint16_t suggested_max_guests;
    uint16_t park_rating_warning_days;
    uint8_t last_entrance_style;
    uint8_t rct1_water_colour;
    uint8_t pad_01358842[2];
    rct_research_item research_items[MAX_RESEARCH_ITEMS];
    uint16_t map_base_z;
    char scenario_name[64];
    char scenario_description[256];
    uint8_t current_interest_rate;
    uint8_t pad_0135934B;
    uint32_t same_price_throughout_extended;
    int16_t park_entrance_x[RCT12_MAX_PARK_ENTRANCES];
    int16_t park_entrance_y[RCT12_MAX_PARK_ENTRANCES];
    int16_t park_entrance_z[RCT12_MAX_PARK_ENTRANCES];
    uint8_t park_entrance_direction[RCT12_MAX_PARK_ENTRANCES];
    char scenario_filename[256];
    uint8_t saved_expansion_pack_names[3256];
    RCT12Banner banners[RCT2_MAX_BANNERS_IN_PARK];
    char custom_strings[RCT12_MAX_USER_STRINGS][RCT12_USER_STRING_MAX_LENGTH];
    uint32_t game_ticks_1;
    rct2_ride rides[RCT12_MAX_RIDES_IN_PARK];
    uint16_t saved_age;
    uint16_t saved_view_x;
    uint16_t saved_view_y;
    uint8_t saved_view_zoom;
    uint8_t saved_view_rotation;
    rct_map_animation map_animations[RCT2_MAX_ANIMATED_OBJECTS];
    uint16_t num_map_animations;
    uint8_t pad_0138B582[2];
    rct_ride_rating_calc_data ride_ratings_calc_data;
    uint8_t pad_0138B5D0[60];
    RCT12RideMeasurement ride_measurements[8];
    uint32_t next_guest_index;
    uint16_t grass_and_scenery_tilepos;
    uint32_t patrol_areas[(RCT2_MAX_STAFF + RCT12_STAFF_TYPE_COUNT) * RCT12_PATROL_AREA_SIZE];
    uint8_t staff_modes[RCT2_MAX_STAFF + RCT12_STAFF_TYPE_COUNT];
    uint8_t pad_13CA73E;
    uint8_t pad_13CA73F;
    uint8_t byte_13CA740;
    uint8_t pad_13CA741;
    uint8_t byte_13CA742[4]; // unused
    uint8_t climate;
    uint8_t pad_013CA747;
    uint16_t climate_update_timer;
    uint8_t current_weather;
    uint8_t next_weather;
    uint8_t temperature;
    uint8_t next_temperature;
    uint8_t current_weather_effect;
    uint8_t next_weather_effect;
    uint8_t current_weather_gloom;
    uint8_t next_weather_gloom;
    uint8_t current_rain_level;
    uint8_t next_rain_level;
    rct12_news_item news_items[RCT12_MAX_NEWS_ITEMS];
    char rct1_scenario_name[62];       // Unused in RCT2
    uint16_t rct1_scenario_slot_index; // Unused in RCT2
    uint32_t rct1_scenario_flags;      // Unused in RCT2
    uint16_t wide_path_tile_loop_x;
    uint16_t wide_path_tile_loop_y;
    uint8_t pad_13CE778[434];
};
assert_struct_size(rct_scenario_data, 0x46b44a);
#pragma pack(pop)

enum
{
    S7_TYPE_SAVEDGAME,
    S7_TYPE_SCENARIO
};

enum
{
    GUESTGEN_VANILLA,       // vanilla guest generation
    GUESTGEN_SCALED,        // scale guest generation based on excitement rating (7.5), excluding flat rides and stalls which obey their normal amounts
    GUESTGEN_COUNT,
};

enum
{
    ADVERTISEGEN_VANILLA,   // vanilla advertisement generation
    ADVERTISEGEN_COUNT,
};

#define S7_VERSION 120002
#define S7_MAGIC_NUMBER 0x00031144

enum
{
    // RCT2 categories (keep order)
    SCENARIO_CATEGORY_BEGINNER,
    SCENARIO_CATEGORY_CHALLENGING,
    SCENARIO_CATEGORY_EXPERT,
    SCENARIO_CATEGORY_REAL,
    SCENARIO_CATEGORY_OTHER,

    // OpenRCT2 categories
    SCENARIO_CATEGORY_DLC,
    SCENARIO_CATEGORY_BUILD_YOUR_OWN,

    SCENARIO_CATEGORY_COUNT
};

extern const rct_string_id ScenarioCategoryStringIds[SCENARIO_CATEGORY_COUNT];

enum
{
    OBJECTIVE_NONE,
    OBJECTIVE_GUESTS_BY,
    OBJECTIVE_PARK_VALUE_BY,
    OBJECTIVE_HAVE_FUN,
    OBJECTIVE_BUILD_THE_BEST,
    OBJECTIVE_10_ROLLERCOASTERS,
    OBJECTIVE_GUESTS_AND_RATING,
    OBJECTIVE_MONTHLY_RIDE_INCOME,
    OBJECTIVE_10_ROLLERCOASTERS_LENGTH,
    OBJECTIVE_FINISH_5_ROLLERCOASTERS,
    OBJECTIVE_REPLAY_LOAN_AND_PARK_VALUE,
    OBJECTIVE_MONTHLY_FOOD_INCOME
};

extern rct_scenario_info gS7Info;

void scenario_remove_trackless_rides(rct_scenario_data* s6);
void scenario_fix_ghosts(rct_scenario_data* s6);

#endif
