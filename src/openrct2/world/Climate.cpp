/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Climate.h"

#include "../Cheats.h"
#include "../Context.h"
#include "../Game.h"
#include "../OpenRCT2.h"
#include "../audio/AudioMixer.h"
#include "../audio/audio.h"
#include "../config/Config.h"
#include "../drawing/Drawing.h"
#include "../interface/Window.h"
#include "../localisation/Date.h"
#include "../scenario/Scenario.h"
#include "../sprites.h"
#include "../util/Util.h"
#include "../windows/Intent.h"

#include <algorithm>
#include <iterator>

constexpr int32_t MAX_THUNDER_INSTANCES = 2;

enum class THUNDER_STATUS
{
    NONE,
    PLAYING,
};

struct WeatherTransition
{
    int8_t BaseTemperature;
    int8_t DistributionSize;
    int8_t Distribution[24];
};

extern const WeatherTransition* ClimateTransitions[20];
extern const WeatherState ClimateWeatherData[7];
extern const FILTER_PALETTE_ID ClimateWeatherGloomColours[4];

// Climate data
uint8_t gClimate;
ClimateState gClimateCurrent;
ClimateState gClimateNext;
uint16_t gClimateUpdateTimer;
uint16_t gClimateLightningFlash;

// Sound data
static int32_t _rainVolume = 1;
static uint32_t _lightningTimer;
static uint32_t _thunderTimer;
static void* _thunderSoundChannels[MAX_THUNDER_INSTANCES];
static THUNDER_STATUS _thunderStatus[MAX_THUNDER_INSTANCES] = { THUNDER_STATUS::NONE, THUNDER_STATUS::NONE };
static SoundId _thunderSoundId;
static int32_t _thunderVolume;
static int32_t _thunderStereoEcho = 0;

static int8_t climate_step_weather_level(int8_t currentWeatherLevel, int8_t nextWeatherLevel);
static void climate_determine_future_weather(int32_t randomDistribution);
static void climate_update_rain_sound();
static void climate_update_thunder_sound();
static void climate_update_lightning();
static void climate_update_thunder();
static void climate_play_thunder(int32_t instanceIndex, SoundId soundId, int32_t volume, int32_t pan);

int32_t climate_celsius_to_fahrenheit(int32_t celsius)
{
    return (celsius * 29) / 16 + 32;
}

/**
 * Set climate and determine start weather.
 */
void climate_reset(int32_t climate)
{
    uint8_t weather = WEATHER_PARTIALLY_CLOUDY;
    int32_t month = date_get_month(gDateMonthsElapsed);
    const WeatherTransition* transition = &ClimateTransitions[climate][month];
    const WeatherState* weatherState = &ClimateWeatherData[weather];

    gClimate = climate;
    gClimateCurrent.Weather = weather;
    gClimateCurrent.Temperature = transition->BaseTemperature + weatherState->TemperatureDelta;
    gClimateCurrent.WeatherEffect = weatherState->EffectLevel;
    gClimateCurrent.WeatherGloom = weatherState->GloomLevel;
    gClimateCurrent.RainLevel = weatherState->RainLevel;

    _lightningTimer = 0;
    _thunderTimer = 0;
    if (_rainVolume != 1)
    {
        audio_stop_rain_sound();
        _rainVolume = 1;
    }

    climate_determine_future_weather(scenario_rand());
}

/**
 * Weather & climate update iteration.
 * Gradually changes the weather parameters towards their determined next values.
 */
void climate_update()
{
    // Only do climate logic if playing (not in scenario editor or title screen)
    if (gScreenFlags & (~SCREEN_FLAGS_PLAYING))
        return;

    if (!gCheatsFreezeWeather)
    {
        if (gClimateUpdateTimer)
        {
            if (gClimateUpdateTimer == 960)
            {
                auto intent = Intent(INTENT_ACTION_UPDATE_CLIMATE);
                context_broadcast_intent(&intent);
            }
            gClimateUpdateTimer--;
        }
        else if (!(gCurrentTicks & 0x7F))
        {
            if (gClimateCurrent.Temperature == gClimateNext.Temperature)
            {
                if (gClimateCurrent.WeatherGloom == gClimateNext.WeatherGloom)
                {
                    gClimateCurrent.WeatherEffect = gClimateNext.WeatherEffect;
                    _thunderTimer = 0;
                    _lightningTimer = 0;

                    if (gClimateCurrent.RainLevel == gClimateNext.RainLevel)
                    {
                        gClimateCurrent.Weather = gClimateNext.Weather;
                        climate_determine_future_weather(scenario_rand());
                        auto intent = Intent(INTENT_ACTION_UPDATE_CLIMATE);
                        context_broadcast_intent(&intent);
                    }
                    else if (gClimateNext.RainLevel <= RAIN_LEVEL_HEAVY)
                    {
                        gClimateCurrent.RainLevel = climate_step_weather_level(
                            gClimateCurrent.RainLevel, gClimateNext.RainLevel);
                    }
                }
                else
                {
                    gClimateCurrent.WeatherGloom = climate_step_weather_level(
                        gClimateCurrent.WeatherGloom, gClimateNext.WeatherGloom);
                    gfx_invalidate_screen();
                }
            }
            else
            {
                gClimateCurrent.Temperature = climate_step_weather_level(gClimateCurrent.Temperature, gClimateNext.Temperature);
                auto intent = Intent(INTENT_ACTION_UPDATE_CLIMATE);
                context_broadcast_intent(&intent);
            }
        }
    }

    if (_thunderTimer != 0)
    {
        climate_update_lightning();
        climate_update_thunder();
    }
    else if (gClimateCurrent.WeatherEffect == WEATHER_EFFECT_STORM)
    {
        // Create new thunder and lightning
        uint32_t randomNumber = util_rand();
        if ((randomNumber & 0xFFFF) <= 0x1B4)
        {
            randomNumber >>= 16;
            _thunderTimer = 43 + (randomNumber % 64);
            _lightningTimer = randomNumber % 32;
        }
    }
}

void climate_force_weather(uint8_t weather)
{
    const auto weatherState = &ClimateWeatherData[weather];
    gClimateCurrent.Weather = weather;
    gClimateCurrent.WeatherGloom = weatherState->GloomLevel;
    gClimateCurrent.RainLevel = weatherState->RainLevel;
    gClimateCurrent.WeatherEffect = weatherState->EffectLevel;
    gClimateUpdateTimer = 1920;

    climate_update();

    // In case of change in gloom level force a complete redraw
    gfx_invalidate_screen();
}

void climate_update_sound()
{
    if (gAudioCurrentDevice == -1)
        return;
    if (gGameSoundsOff)
        return;
    if (!gConfigSound.sound_enabled)
        return;
    if (gScreenFlags & SCREEN_FLAGS_TITLE_DEMO)
        return;

    climate_update_rain_sound();
    climate_update_thunder_sound();
}

bool climate_is_raining()
{
    return gClimateCurrent.RainLevel != RAIN_LEVEL_NONE;
}

FILTER_PALETTE_ID climate_get_weather_gloom_palette_id(const ClimateState& state)
{
    auto paletteId = PALETTE_NULL;
    auto gloom = state.WeatherGloom;
    if (gloom < std::size(ClimateWeatherGloomColours))
    {
        paletteId = ClimateWeatherGloomColours[gloom];
    }
    return paletteId;
}

uint32_t climate_get_weather_sprite_id(const ClimateState& state)
{
    uint32_t spriteId = SPR_WEATHER_SUN;
    if (state.Weather < std::size(ClimateWeatherData))
    {
        spriteId = ClimateWeatherData[state.Weather].SpriteId;
    }
    return spriteId;
}

bool climate_should_spawn_ducks()
{
    int32_t month = date_get_month(gDateMonthsElapsed);
    switch (gClimate)
    {
        case CLIMATE_COOL_AND_WET:
        case CLIMATE_WARM:
        case CLIMATE_HOT_AND_DRY:
        case CLIMATE_COLD:
        case CLIMATE_TEMPERATE:
        case CLIMATE_TROPICAL_SAVANNAH_S:
            return month >= MONTH_APRIL && month < MONTH_SEPTEMBER;    // ducks use vanilla behavior in these climates
        case CLIMATE_ARID_WARM:
        case CLIMATE_ARID_COLD:
        case CLIMATE_TUNDRA:
        case CLIMATE_ARID_COLD_S:
        case CLIMATE_HELL:
        case CLIMATE_MARS:
            return false;   // ducks never spawn in these climates
        case CLIMATE_MEDITERRANEAN:
        case CLIMATE_MEDITERRANEAN_S:
            return true;    // ducks always spawn in these climates
        case CLIMATE_SEMI_ARID:
        case CLIMATE_TROPICAL:
        case CLIMATE_TROPICAL_SAVANNAH:
        case CLIMATE_ISLAND:
        case CLIMATE_TEMPERATE_S:
            return month >= MONTH_OCTOBER || month < MONTH_MARCH; // tropical behavior for ducks
        case CLIMATE_TAIGA:
            return month >= MONTH_MAY && month < MONTH_SEPTEMBER;
        case CLIMATE_TAIGA_S:
            return month < MONTH_MAY || month >= MONTH_SEPTEMBER;
    }
    return month >= MONTH_APRIL;
}

bool climate_should_migrate_ducks()
{
    int32_t month = date_get_month(gDateMonthsElapsed);
    switch (gClimate)
    {
        case CLIMATE_COOL_AND_WET:
        case CLIMATE_WARM:
        case CLIMATE_HOT_AND_DRY:
        case CLIMATE_COLD:
        case CLIMATE_TEMPERATE:
        case CLIMATE_TROPICAL_SAVANNAH_S:
            return month < MONTH_APRIL || month >= MONTH_SEPTEMBER; // ducks use vanilla behavior in these climates
        case CLIMATE_ARID_WARM:
        case CLIMATE_ARID_COLD:
        case CLIMATE_TUNDRA:
        case CLIMATE_ARID_COLD_S:
        case CLIMATE_HELL:
        case CLIMATE_MARS:
            return true; // ducks never spawn in these climates, so they should always migrate if they somehow appear
        case CLIMATE_MEDITERRANEAN:
        case CLIMATE_MEDITERRANEAN_S:
            return false; // ducks never migrate in these climates
        case CLIMATE_SEMI_ARID:
        case CLIMATE_TROPICAL:
        case CLIMATE_TROPICAL_SAVANNAH:
        case CLIMATE_ISLAND:
        case CLIMATE_TEMPERATE_S:
            return month < MONTH_OCTOBER && month >= MONTH_MARCH; // tropical behavior for ducks
        case CLIMATE_TAIGA:
            return month < MONTH_MAY || month >= MONTH_SEPTEMBER;
        case CLIMATE_TAIGA_S:
            return month >= MONTH_MAY && month < MONTH_SEPTEMBER;
    }
    return month >= MONTH_SEPTEMBER;
}

static int8_t climate_step_weather_level(int8_t currentWeatherLevel, int8_t nextWeatherLevel)
{
    if (nextWeatherLevel > currentWeatherLevel)
    {
        return currentWeatherLevel + 1;
    }
    else
    {
        return currentWeatherLevel - 1;
    }
}

/**
 * Calculates future weather development.
 * RCT2 implements this as discrete probability distributions dependant on month and climate
 * for nextWeather. The other weather parameters are then looked up depending only on the
 * next weather.
 */
static void climate_determine_future_weather(int32_t randomDistribution)
{
    int8_t month = date_get_month(gDateMonthsElapsed);

    // Generate a random variable with values 0 up to DistributionSize-1 and chose weather from the distribution table
    // accordingly
    const WeatherTransition* transition = &ClimateTransitions[gClimate][month];
    int8_t nextWeather = transition->Distribution[((randomDistribution & 0xFF) * transition->DistributionSize) >> 8];
    gClimateNext.Weather = nextWeather;

    const auto nextWeatherState = &ClimateWeatherData[nextWeather];
    gClimateNext.Temperature = transition->BaseTemperature + nextWeatherState->TemperatureDelta;
    gClimateNext.WeatherEffect = nextWeatherState->EffectLevel;
    gClimateNext.WeatherGloom = nextWeatherState->GloomLevel;
    gClimateNext.RainLevel = nextWeatherState->RainLevel;

    gClimateUpdateTimer = 1920;
}

static void climate_update_rain_sound()
{
    if (gClimateCurrent.WeatherEffect == WEATHER_EFFECT_RAIN || gClimateCurrent.WeatherEffect == WEATHER_EFFECT_STORM)
    {
        // Start playing the rain sound
        if (gRainSoundChannel == nullptr)
        {
            gRainSoundChannel = Mixer_Play_Effect(SoundId::Rain, MIXER_LOOP_INFINITE, DStoMixerVolume(-4000), 0.5f, 1, 0);
        }
        if (_rainVolume == 1)
        {
            _rainVolume = -4000;
        }
        else
        {
            // Increase rain sound
            _rainVolume = std::min(-1400, _rainVolume + 80);
            if (gRainSoundChannel != nullptr)
            {
                Mixer_Channel_Volume(gRainSoundChannel, DStoMixerVolume(_rainVolume));
            }
        }
    }
    else if (_rainVolume != 1)
    {
        // Decrease rain sound
        _rainVolume -= 80;
        if (_rainVolume > -4000)
        {
            if (gRainSoundChannel != nullptr)
            {
                Mixer_Channel_Volume(gRainSoundChannel, DStoMixerVolume(_rainVolume));
            }
        }
        else
        {
            audio_stop_rain_sound();
            _rainVolume = 1;
        }
    }
}

static void climate_update_thunder_sound()
{
    if (_thunderStereoEcho)
    {
        // Play thunder on right side
        _thunderStereoEcho = 0;
        climate_play_thunder(1, _thunderSoundId, _thunderVolume, 10000);
    }

    // Stop thunder sounds if they have finished
    for (int32_t i = 0; i < MAX_THUNDER_INSTANCES; i++)
    {
        if (_thunderStatus[i] != THUNDER_STATUS::NONE)
        {
            void* channel = _thunderSoundChannels[i];
            if (!Mixer_Channel_IsPlaying(channel))
            {
                Mixer_Stop_Channel(channel);
                _thunderStatus[i] = THUNDER_STATUS::NONE;
            }
        }
    }
}

static void climate_update_lightning()
{
    if (_lightningTimer == 0)
        return;
    if (gConfigGeneral.disable_lightning_effect)
        return;
    if (!gConfigGeneral.render_weather_effects && !gConfigGeneral.render_weather_gloom)
        return;

    _lightningTimer--;
    if (gClimateLightningFlash == 0)
    {
        if ((util_rand() & 0xFFFF) <= 0x2000)
        {
            gClimateLightningFlash = 1;
        }
    }
}

static void climate_update_thunder()
{
    _thunderTimer--;
    if (_thunderTimer == 0)
    {
        uint32_t randomNumber = util_rand();
        if (randomNumber & 0x10000)
        {
            if (_thunderStatus[0] == THUNDER_STATUS::NONE && _thunderStatus[1] == THUNDER_STATUS::NONE)
            {
                // Play thunder on left side
                _thunderSoundId = (randomNumber & 0x20000) ? SoundId::Thunder1 : SoundId::Thunder2;
                _thunderVolume = (-((int32_t)((randomNumber >> 18) & 0xFF))) * 8;
                climate_play_thunder(0, _thunderSoundId, _thunderVolume, -10000);

                // Let thunder play on right side
                _thunderStereoEcho = 1;
            }
        }
        else
        {
            if (_thunderStatus[0] == THUNDER_STATUS::NONE)
            {
                _thunderSoundId = (randomNumber & 0x20000) ? SoundId::Thunder1 : SoundId::Thunder2;
                int32_t pan = (((randomNumber >> 18) & 0xFF) - 128) * 16;
                climate_play_thunder(0, _thunderSoundId, 0, pan);
            }
        }
    }
}

static void climate_play_thunder(int32_t instanceIndex, SoundId soundId, int32_t volume, int32_t pan)
{
    _thunderSoundChannels[instanceIndex] = Mixer_Play_Effect(
        soundId, MIXER_LOOP_NONE, DStoMixerVolume(volume), DStoMixerPan(pan), 1, 0);
    if (_thunderSoundChannels[instanceIndex] != nullptr)
    {
        _thunderStatus[instanceIndex] = THUNDER_STATUS::PLAYING;
    }
}

#pragma region Climate / Weather data tables

const FILTER_PALETTE_ID ClimateWeatherGloomColours[4] = {
    PALETTE_NULL,
    PALETTE_DARKEN_1,
    PALETTE_DARKEN_2,
    PALETTE_DARKEN_3,
};

const WeatherState ClimateWeatherData[7] = {
    { 10, WEATHER_EFFECT_NONE, 0, RAIN_LEVEL_NONE, SPR_WEATHER_SUN },         // Sunny
    { 5, WEATHER_EFFECT_NONE, 0, RAIN_LEVEL_NONE, SPR_WEATHER_SUN_CLOUD },    // Partially Cloudy
    { 0, WEATHER_EFFECT_NONE, 0, RAIN_LEVEL_NONE, SPR_WEATHER_CLOUD },        // Cloudy
    { -2, WEATHER_EFFECT_RAIN, 1, RAIN_LEVEL_LIGHT, SPR_WEATHER_LIGHT_RAIN }, // Rain
    { -4, WEATHER_EFFECT_RAIN, 2, RAIN_LEVEL_HEAVY, SPR_WEATHER_HEAVY_RAIN }, // Heavy Rain
    { 2, WEATHER_EFFECT_STORM, 3, RAIN_LEVEL_HEAVY, SPR_WEATHER_STORM },      // Thunderstorm
    { -8, WEATHER_EFFECT_SNOW, 1, RAIN_LEVEL_NONE, SPR_WEATHER_SNOW },        // Snow

};

// fixme: these transitions are all wrong (we will adjust them anyway when we add new climates)
static constexpr const WeatherTransition ClimateTransitionsCoolAndWet[] = {
    { -4, 18, { 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0 } },
    { 1, 21, { 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 0, 0 } },
    { 8, 18, { 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0 } },
    { 10, 21, { 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 0, 0 } },
    { 14, 17, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 0, 0, 0, 0, 0, 0 } },
    { 17, 17, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0 } },
    { 19, 23, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 20, 23, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 4, 4, 5 } },
    { 16, 19, { 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 0, 0, 0, 0 } },
    { 13, 16, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { 4, 19, { 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 0, 0, 0, 0 } },
    { 0, 16, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
};
static constexpr const WeatherTransition ClimateTransitionsWarm[] = {
    { 3, 21, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0 } },
    { 8, 22, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 5, 0 } },
    { 12, 21, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0 } },
    { 13, 22, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 5, 0 } },
    { 16, 17, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 19, 18, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0 } },
    { 21, 22, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0 } },
    { 22, 17, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 5, 0, 0, 0, 0, 0, 0 } },
    { 19, 17, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 16, 17, { 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0, 0, 0, 0, 0 } },
    { 7, 17, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 4, 17, { 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0, 0, 0, 0, 0 } },
};
static constexpr const WeatherTransition ClimateTransitionsHotAndDry[] = {
    { 6, 15, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 8, 12, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 12, 15, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 14, 12, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 16, 11, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 19, 9, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 21, 13, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 22, 11, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 21, 12, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 16, 13, { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 11, 12, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 8, 13, { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
};
static constexpr const WeatherTransition ClimateTransitionsCold[] = {
    { -10, 18, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { -5, 21, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 4, 18, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { 5, 21, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 7, 17, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0, 0 } },
    { 9, 17, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0 } },
    { 10, 23, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 11, 23, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 9, 19, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { 6, 16, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { 0, 19, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { -8, 16, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
};

// new climate patterns
// first number = base temperature
// second number = size of distribution of weather
// the numbers that follow are the weather distribution
// 0 = sunny, 1 = partially cloudy, 2 = cloudy, 3 = rain, 4 = heavy rain, 5 = thunderstorm, 6 = snow

static constexpr const WeatherTransition ClimateTransitionsAridWarm[] = {
    { 6, 23,    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 8, 23,    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5 } },
    { 12, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 17, 23,   { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 5 } },
    { 22, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 25, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 25, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 25, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 5 } },
    { 19, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 12, 23,   { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5 } },
    { 7, 23,    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsAridCold[] = {
    { -14, 23,  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -11, 23,  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -6, 23,   { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6, 0, 0 } },
    { -1, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 3 } },
    { 3, 23,    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 7, 23,    { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
    { 11, 23,   { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
    { 10, 23,   { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
    { 6, 23,    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { -1, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 3 } },
    { -7, 23,   { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -12, 23,  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
};

static constexpr const WeatherTransition ClimateTransitionsSemiArid[] = {
    { 16, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 19, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 23, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 27, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 26, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 23, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 24, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 20, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 17, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsMediterranean[] = {
    { 9, 23,    { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 10, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 11, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 12, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 14, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 16, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 18, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 18, 23,   { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 17, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 15, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 11, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 9, 23,    { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsTemperate[] = {
    { -1, 23,   { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { 1, 23,    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6 } },
    { 4, 23,    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 3, 3 } },
    { 11, 23,   { 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 16, 23,   { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 19, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 5 } },
    { 23, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 22, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 17, 23,   { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5 } },
    { 11, 23,   { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5 } },
    { 3, 23,    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 6, 6, 6 } },
    { -1, 23,   { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6 } },
};

static constexpr const WeatherTransition ClimateTransitionsTropical[] = {
    { 27, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 27, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 29, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 30, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 30, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 29, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsTropicalSavannah[] = {
    { 23, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 23, 23,   { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4 } },
    { 23, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 22, 23,   { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 23, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 23, 23,   { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 24, 23,   { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 24, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3 } },
};

static constexpr const WeatherTransition ClimateTransitionsIsland[] = {
    { 27, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 27, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 29, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 30, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 31, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 30, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 29, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 28, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 4, 5, 5, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsTaiga[] = {
    { -26, 23,  { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { -25, 23,  { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { -18, 23,  { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { -8, 23,   { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 0, 23,    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0, 0 } },
    { 9, 23,    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0 } },
    { 12, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 10, 23,   { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 4, 23,    { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { -4, 23,   { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { -16, 23,  { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { -24, 23,  { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
};

static constexpr const WeatherTransition ClimateTransitionsTundra[] = {
    { -33, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -28, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -19, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -12, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -5, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -2, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { 0, 23,    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -3, 23,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -12, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -12, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -18, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
    { -27, 23,  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6 } },
};

// rain numbers needed first
static constexpr const WeatherTransition ClimateTransitionsAridColdS[] = {
    { 11, 23, { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
    { 10, 23, { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
    { 6, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { -1, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 3 } },
    { -7, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -12, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -14, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -11, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { -6, 23, { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6, 0, 0 } },
    { -1, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 3 } },
    { 3, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 7, 23, { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsMediterraneanS[] = {
    { 18, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 18, 23, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 17, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 15, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 11, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 9, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 9, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 10, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 11, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 12, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5 } },
    { 14, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 16, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsTemperateS[] = {
    { 23, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 22, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 5 } },
    { 17, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5 } },
    { 11, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5 } },
    { 3, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 6, 6, 6 } },
    { -1, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6 } },
    { -1, 23, { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6, 6, 6 } },
    { 1, 23, { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 6, 6 } },
    { 4, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 6, 6, 3, 3 } },
    { 11, 23, { 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 16, 23, { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 19, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 5 } },
};

static constexpr const WeatherTransition ClimateTransitionsTropicalSavannahS[] = {
    { 23, 23, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 22, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 23, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 23, 23, { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5 } },
    { 24, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5 } },
    { 24, 23, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3 } },
    { 23, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23, { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 24, 23, { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4 } },
    { 23, 23, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4 } },
};

static constexpr const WeatherTransition ClimateTransitionsTaigaS[] = {
    { 12, 23, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 10, 23, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 4, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { -4, 23, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { -16, 23, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { -24, 23, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { -26, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { -25, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { -18, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { -8, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 0, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0, 0 } },
    { 9, 23, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0 } },
};

// end rain numbers needed first

static constexpr const WeatherTransition ClimateTransitionsHell[] = {
    { 32, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { 34, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 34, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0 } },
    { 34, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0 } },
    { 39, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 0, 0, 0, 0, 0, 0 } },
    { 42, 1, { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0 } },
    { 40, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 4 } },
    { 36, 1, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5 } },
    { 35, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { 34, 1, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
    { 34, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 4, 5, 0, 0, 0, 0 } },
    { 33, 1, { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0 } },
};

static constexpr const WeatherTransition ClimateTransitionsMars[] = {
    { 3, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0 } },
    { 8, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 5, 0 } },
    { 12, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0 } },
    { 13, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 5, 0 } },
    { 16, 1, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 19, 1, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0 } },
    { 21, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0 } },
    { 22, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 5, 0, 0, 0, 0, 0, 0 } },
    { 19, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 16, 1, { 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0, 0, 0, 0, 0 } },
    { 7, 1, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0, 0 } },
    { 4, 1, { 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0, 0, 0, 0, 0, 0 } },
};

const WeatherTransition* ClimateTransitions[] = {
    ClimateTransitionsCoolAndWet,
    ClimateTransitionsWarm,
    ClimateTransitionsHotAndDry,
    ClimateTransitionsCold,
    ClimateTransitionsAridWarm,
    ClimateTransitionsAridCold,
    ClimateTransitionsSemiArid,
    ClimateTransitionsMediterranean,
    ClimateTransitionsTemperate,
    ClimateTransitionsTropical,
    ClimateTransitionsTropicalSavannah,
    ClimateTransitionsIsland,
    ClimateTransitionsTaiga,
    ClimateTransitionsAridColdS,
    ClimateTransitionsMediterraneanS,
    ClimateTransitionsTemperateS,
    ClimateTransitionsTropicalSavannahS,
    ClimateTransitionsTaiga,
    ClimateTransitionsHell,
    ClimateTransitionsMars,
};

const ClimateHeatBounds gClimateHeatBounds[CLIMATE_COUNT] = {
    { 15, 26 },  // cool and wet
    { 15, 30 },  // warm
    { 15, 26 },  // hot and dry
    { 15, 26 },  // cold
    { 15, 30 }, // arid/warm
    { 15, 26 },  // arid/cold
    { 15, 30 }, // semi-arid
    { 15, 26 }, // mediterranean
    { 10, 26 },  // temperate
    { 20, 33 }, // tropical
    { 20, 30 }, // tropical savannah
    { 20, 33 }, // island
    { 10, 25 },  // taiga
    { 10, 20 },  // tundra
    { 15, 26 },  // arid/cold
    { 15, 26 }, // mediterranean
    { 10, 26 },  // temperate
    { 20, 30 }, // tropical savannah
    { 10, 25 },  // taiga
    { 25, 30 }, // hell
    { 10, 25 },  // mars
};

#pragma endregion
