/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"
#include "../drawing/Drawing.h"

enum CLIMATE
{ // should match the climate names in InteractiveConsole.cpp!
  // should match the strings as present in EditorScenarioOptions.cpp!
  // vanilla RCT climates
    CLIMATE_COOL_AND_WET,
    CLIMATE_WARM,
    CLIMATE_HOT_AND_DRY,
    CLIMATE_COLD,

    // earth-like climates
    CLIMATE_ARID_WARM,            // arid (little rainfall) and it is always warm.
    CLIMATE_ARID_COLD,            // arid (little rainfall) and cold winters, warm otherwise.
    CLIMATE_SEMI_ARID,            // semi-arid (some rainfall) and it is always warm
    CLIMATE_MEDITERRANEAN,        // warm and hot. rainy winters and dry summers
    CLIMATE_TEMPERATE,            // warm, wet summers, and cold, snowy winters
    CLIMATE_TROPICAL,             // a lot of rain, little temperature variance (always warm).
    CLIMATE_TROPICAL_SAVANNAH,    // like tropical, but it has wet and dry seasons with some temperature variance.
    CLIMATE_ISLAND,               // similar to tropical, but the rainfall is inconsistent. when it does rain, it storms for
    // a long time.
    CLIMATE_TAIGA,                // cool summers, cold winters, most of the rainfall is during the summer
    CLIMATE_TUNDRA,               // arid (little snow) and it is always cold

    CLIMATE_ARID_COLD_S,          // arid_cold but with southern hemisphere distribution
    CLIMATE_MEDITERRANEAN_S,      // mediterreanean but with southern hemisphere distribution
    CLIMATE_TEMPERATE_S,          // temperate but with southern hemisphere distribution
    CLIMATE_TROPICAL_SAVANNAH_S,  // tropical savannah but with southern hemisphere distribution
    CLIMATE_TAIGA_S,              // taiga but with southern hemisphere distribution

    // fantasy climates
    CLIMATE_HELL,                 // it is always sunny, it never rains, and it is always hot
    CLIMATE_MARS,                 // it is always sunny, it never rains, and the temperatures have normal variance

    CLIMATE_COUNT,
};

enum WEATHER
{
    WEATHER_SUNNY,
    WEATHER_PARTIALLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_RAIN,
    WEATHER_HEAVY_RAIN,
    WEATHER_THUNDER,
    WEATHER_SNOW,
};

enum WEATHER_EFFECT
{
    WEATHER_EFFECT_NONE,
    WEATHER_EFFECT_RAIN,
    WEATHER_EFFECT_STORM,
    WEATHER_EFFECT_SNOW,
};

enum RAIN_LEVEL
{
    RAIN_LEVEL_NONE,
    RAIN_LEVEL_LIGHT,
    RAIN_LEVEL_HEAVY,
};

struct WeatherState
{
    int8_t TemperatureDelta;
    int8_t EffectLevel;
    int8_t GloomLevel;
    int8_t RainLevel;
    uint32_t SpriteId;
};

struct ClimateState
{
    uint8_t Weather;
    int8_t Temperature;
    uint8_t WeatherEffect;
    uint8_t WeatherGloom;
    uint8_t RainLevel;
};

struct ClimateHeatBounds
{
    int8_t MinimumToFreeze; // anything below this will set peep target heat to 0
    int8_t MinimumToHeat; // anything above this will set peep target heat to 255
};

extern uint8_t gClimate;
extern ClimateState gClimateCurrent;
extern ClimateState gClimateNext;
extern uint16_t gClimateUpdateTimer;
extern uint16_t gClimateLightningFlash;

extern const ClimateHeatBounds gClimateHeatBounds[CLIMATE_COUNT];

int32_t climate_celsius_to_fahrenheit(int32_t celsius);
void climate_reset(int32_t climate);
void climate_update();
void climate_update_sound();
void climate_force_weather(uint8_t weather);

// returns true if we should start spawning ducks
bool climate_should_spawn_ducks();

// returns true if we should start migrating ducks away
bool climate_should_migrate_ducks();

bool climate_is_raining();
FILTER_PALETTE_ID climate_get_weather_gloom_palette_id(const ClimateState& state);
uint32_t climate_get_weather_sprite_id(const ClimateState& state);
