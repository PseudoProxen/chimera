// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_SERVER_MAIN_LOOP_TIMER_HPP
#define CHIMERA_SERVER_MAIN_LOOP_TIMER_HPP

#include <cstddef>

namespace Chimera {
    /** Kind of timer the main loop is waiting on */
    enum class ServerMainLoopTimerMode {
        /** The main loop timer is not being used */
        NONE,

        /** High resolution waitable timer; wakes on time without changing the timer resolution of the system */
        HIGH_RESOLUTION,

        /** Regular waitable timer; wakes on the timer resolution of the process (set to 1 ms by the timer resolution fix) */
        STANDARD
    };

    /** Information about how on time the main loop was woken up */
    struct ServerMainLoopTimerStats {
        /** Kind of timer being used */
        ServerMainLoopTimerMode mode;

        /** Time between wakes in milliseconds */
        std::size_t period_ms;

        /** Number of wakes measured */
        std::size_t wakes;

        /** Average time between wakes in milliseconds */
        double average_ms;

        /** Shortest time between wakes in milliseconds */
        double min_ms;

        /** Longest time between wakes in milliseconds */
        double max_ms;

        /** Number of wakes that were more than 2 ms later than the period */
        std::size_t late;
    };

    /**
     * Make the dedicated server wait for the next frame on a periodic timer instead of by calling Sleep() in a loop.
     *
     * Halo sleeps for 10 ms at a time until the last 12 ms of the frame, then spins on Sleep(0) until the frame is
     * over. A periodic timer does not burn a core for that last part, and wakes that are missed (e.g. when loading
     * a map) are combined into a single wake instead of piling up.
     *
     * This does nothing on anything but a dedicated server, if the signature for it cannot be found, or if
     * halo.server_main_loop_timer is set to 0 in chimera.ini.
     */
    void set_up_server_main_loop_timer() noexcept;

    /**
     * Get information about the main loop timer
     * @param  reset clear the numbers after getting them
     * @return       stats; mode is NONE if the main loop timer is not active
     */
    ServerMainLoopTimerStats get_server_main_loop_timer_stats(bool reset) noexcept;
}

#endif
