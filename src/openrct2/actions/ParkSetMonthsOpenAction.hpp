/*****************************************************************************
 * Copyright (c) 2019 eezstreet
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../scenario/Scenario.h"
#include "GameAction.h"

DEFINE_GAME_ACTION(ParkSetMonthsOpenAction, GAME_COMMAND_SET_MONTHS_OPEN, GameActionResult)
{
private:
    uint8_t _start_month, _end_month;

public:
    ParkSetMonthsOpenAction() = default;
    ParkSetMonthsOpenAction(uint8_t start_month, uint8_t end_month)
        : _start_month(start_month)
        , _end_month(end_month)
    {
    }

    uint16_t GetActionFlags() const override
    {
        return GameAction::GetActionFlags() | GA_FLAGS::ALLOW_WHILE_PAUSED;
    }

    void Serialise(DataSerialiser & stream) override
    {
        GameAction::Serialise(stream);

        stream << DS_TAG(_start_month) << DS_TAG(_end_month);
    }

    GameActionResult::Ptr Query() const override
    {
        if (_start_month >= MONTH_COUNT || _end_month >= MONTH_COUNT)
        {
            return std::make_unique<GameActionResult>(GA_ERROR::INVALID_PARAMETERS, STR_INVALID_CLIMATE_ID, STR_NONE);
        }

        return std::make_unique<GameActionResult>();
    }

    GameActionResult::Ptr Execute() const override
    {
        gS7Info.starting_month = _start_month;
        gS7Info.ending_month = _end_month;

        if (gS7Info.starting_month > gS7Info.ending_month)
        {   // keep starting month before the ending month
            gS7Info.starting_month = gS7Info.ending_month;
        }
        else if (gS7Info.ending_month < gS7Info.starting_month)
        {   // keep starting month before the ending month
            gS7Info.ending_month = gS7Info.starting_month;
        }

        gfx_invalidate_screen();

        return std::make_unique<GameActionResult>();
    }
};
