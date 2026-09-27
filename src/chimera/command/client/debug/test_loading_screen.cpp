// SPDX-License-Identifier: GPL-3.0-only

#include "../../../loading_screen/loading_screen.hpp"
#include "../../../output/output.hpp"

namespace Chimera {
    bool test_loading_screen_command(int, const char **) {
        if(!play_loading_screen_demo()) {
            console_error("The loading screen is not available. Make sure halo.loading_screen is not set to 0 in chimera.ini.");
            return false;
        }
        return true;
    }
}
