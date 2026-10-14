// SPDX-License-Identifier: GPL-3.0-only

#include "../../../fix/server_main_loop_timer.hpp"
#include "../../../output/output.hpp"

namespace Chimera {
    bool timer_stats_command(int, const char **) {
        static const char *const MODES[] = { "none", "high resolution", "standard" };

        auto stats = get_server_main_loop_timer_stats(true);
        if(stats.mode == ServerMainLoopTimerMode::NONE) {
            console_output("The main loop timer is not active. Make sure halo.server_main_loop_timer is not set to 0 in chimera.ini.");
            return true;
        }

        console_output("Main loop timer: %s, period: %u ms", MODES[static_cast<std::size_t>(stats.mode)], static_cast<unsigned int>(stats.period_ms));
        if(stats.wakes) {
            console_output("Wakes: %u  Avg: %.2f ms  Min: %.2f ms  Max: %.2f ms  Late: %u", static_cast<unsigned int>(stats.wakes), stats.average_ms, stats.min_ms, stats.max_ms, static_cast<unsigned int>(stats.late));
        }
        else {
            console_output("No wakes recorded since the last reset.");
        }
        return true;
    }
}
