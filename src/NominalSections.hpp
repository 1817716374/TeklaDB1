#pragma once

#include <array>

namespace tekla::db1::detail
{
// Discrete nominal dimensions (mm), NOT a project catalog. Ordinary UPE/IPE
// only; no interpolation or assumptions about A/O/V or mass-qualified variants.
// Sources: https://orangebook.arcelormittal.com/node/253 and /node/6
// Retrieved 2026-09-27. The fallback using these dimensions has sharp corners.
struct NominalSection { double height, width, web, flange; };
inline constexpr std::array<NominalSection, 14> nominalUPE{{
    {80, 50, 4, 7},
    {100, 55, 4.5, 7.5},
    {120, 60, 5, 8},
    {140, 65, 5, 9},
    {160, 70, 5.5, 9.5},
    {180, 75, 5.5, 10.5},
    {200, 80, 6, 11},
    {220, 85, 6.5, 12},
    {240, 90, 7, 12.5},
    {270, 95, 7.5, 13.5},
    {300, 100, 9.5, 15},
    {330, 105, 11, 16},
    {360, 110, 12, 17},
    {400, 115, 13.5, 18},
}};
inline constexpr std::array<NominalSection, 18> nominalIPE{{
    {80, 46, 3.8, 5.2},
    {100, 55, 4.1, 5.7},
    {120, 64, 4.4, 6.3},
    {140, 73, 4.7, 6.9},
    {160, 82, 5, 7.4},
    {180, 91, 5.3, 8},
    {200, 100, 5.6, 8.5},
    {220, 110, 5.9, 9.2},
    {240, 120, 6.2, 9.8},
    {270, 135, 6.6, 10.2},
    {300, 150, 7.1, 10.7},
    {330, 160, 7.5, 11.5},
    {360, 170, 8, 12.7},
    {400, 180, 8.6, 13.5},
    {450, 190, 9.4, 14.6},
    {500, 200, 10.2, 16},
    {550, 210, 11.1, 17.2},
    {600, 220, 12, 19},
}};
}
