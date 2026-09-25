#pragma once

namespace sonora
{
inline constexpr int ticksPerQuarter = 960;
inline constexpr int stepTicks = ticksPerQuarter / 4;
inline constexpr int patternTicks = 16 * ticksPerQuarter;
inline constexpr int gridSteps = patternTicks / stepTicks;
}
