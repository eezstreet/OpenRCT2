/*****************************************************************************
 * Copyright (c) 2019 eezstreet
 *
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"

struct rct_drawpixelinfo;

namespace OpenRCT2::Drawing
{
    interface ISnowDrawer;
}

// clang-format off
static constexpr const uint8_t SnowPattern[] =
{
    32, 32, 50, 50, 0, 14, 0, 16, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0,
    255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0
};
// clang-format on

void DrawSnow(rct_drawpixelinfo* dpi, OpenRCT2::Drawing::ISnowDrawer* rainDrawer);
