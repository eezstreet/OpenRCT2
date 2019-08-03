/*****************************************************************************
 * Copyright (c) 2019 eezstreet
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../world/Park.h"
#include "GameAction.h"

DEFINE_GAME_ACTION(ParkSetGenerationAlgorithmsAction, GAME_COMMAND_SET_GENERATION_ALGORITHMS, GameActionResult)
{
private:
    uint8_t _guest_generation, _advertising_generation;

public:
    ParkSetGenerationAlgorithmsAction() = default;
    ParkSetGenerationAlgorithmsAction(uint8_t guest_generation, uint8_t advertising_generation)
        : _guest_generation(guest_generation)
        , _advertising_generation(advertising_generation)
    {
    }

    uint16_t GetActionFlags() const override
    {
        return GameAction::GetActionFlags() | GA_FLAGS::ALLOW_WHILE_PAUSED;
    }

    void Serialise(DataSerialiser & stream) override
    {
        GameAction::Serialise(stream);

        stream << DS_TAG(_guest_generation) << DS_TAG(_advertising_generation);
    }

    GameActionResult::Ptr Query() const override
    {
        if (_guest_generation >= GUESTGEN_COUNT || _advertising_generation >= ADVERTISEGEN_COUNT)
        {
            return std::make_unique<GameActionResult>(GA_ERROR::INVALID_PARAMETERS, STR_INVALID_CLIMATE_ID, STR_NONE);
        }

        return std::make_unique<GameActionResult>();
    }

    GameActionResult::Ptr Execute() const override
    {
        gGuestGenerationAlgorithm = _guest_generation;
        gAdvertisementGenerationAlgorithm = _advertising_generation;

        gfx_invalidate_screen();

        return std::make_unique<GameActionResult>();
    }
};
