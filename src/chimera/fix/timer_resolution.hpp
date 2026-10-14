// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_TIMER_RESOLUTION_HPP
#define CHIMERA_TIMER_RESOLUTION_HPP

namespace Chimera {
    /**
     * Ask Windows for a 1 ms timer resolution, and stop it from throttling the process when it is in the background.
     *
     * By default Sleep() and timers only wake on the system clock (15.6 ms), so something like chimera_throttle_fps
     * could not hold a frame rate that needs a shorter wait. Windows 11 can also move a background process to
     * efficiency cores and stop honoring its timer resolution, which hurts when playing tabbed out or when hosting.
     */
    void set_up_timer_resolution_fix() noexcept;
}

#endif
