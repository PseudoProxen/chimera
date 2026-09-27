// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_LOADING_SCREEN_HPP
#define CHIMERA_LOADING_SCREEN_HPP

namespace Chimera {
    /**
     * Set up the custom loading screen.
     *
     * This loads maps on a separate thread while the main thread keeps drawing (and pumping window messages), which
     * lets us show an animated loading screen and stops Windows from thinking that the game froze. It requires the
     * Direct3D 9 device to be created with D3DCREATE_MULTITHREADED, so this must be called before the device is created.
     *
     * This does nothing if the signatures for it are missing or if halo.loading_screen is set to 0 in chimera.ini.
     */
    void set_up_loading_screen() noexcept;

    /**
     * Set whether the loading screen is blocked (see chimera_block_loading_screen). While blocked, maps are loaded as
     * they normally would be, without the custom loading screen.
     * @param blocked true if blocked
     */
    void set_loading_screen_blocked(bool blocked) noexcept;

    /**
     * Play the loading screen animation for a few seconds without loading a map.
     * @return true if playing; false if the loading screen is not available
     */
    bool play_loading_screen_demo() noexcept;
}

#endif
