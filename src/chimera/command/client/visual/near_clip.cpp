// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../fix/z_fighting.hpp"
#include "../../../output/output.hpp"

namespace Chimera {
    bool near_clip_command(int argc, const char **argv) {
        static float setting = 0.0625F;
        if(argc == 1) {
            setting = std::stof(*argv);
            if(setting < 0.0625F) {
                setting = 0.0625F;
            }
            else if(setting > 0.25F) {
                setting = 0.25F;
            }
            setting = set_near_clip(setting);
        }
        console_output("%f", setting);
        return true;
    }
}
