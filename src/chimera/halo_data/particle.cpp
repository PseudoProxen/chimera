// SPDX-License-Identifier: GPL-3.0-only

#include "../signature/signature.hpp"
#include "../chimera.hpp"

#include "particle.hpp"

namespace Chimera {
    ParticleTable &ParticleTable::get_particle_table() noexcept {
        // Don't hold onto the table itself since Chimera moves it after startup.
        static auto **particle_table = *reinterpret_cast<ParticleTable ***>(get_chimera().get_signature("particle_table_sig").data() + 2);
        return **particle_table;
    }
}
