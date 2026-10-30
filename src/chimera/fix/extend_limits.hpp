// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_EXTEND_LIMITS_HPP
#define CHIMERA_EXTEND_LIMITS_HPP

#define BSP_POLY_LIMIT 0xFFFE
#define PARTICLE_LIMIT 4096

namespace Chimera {
    /**
     * Extend Halo's limits
     */
    void set_up_extend_limits() noexcept;

    /**
     * Extend the particle limit. The game must have allocated its particle table before this is called.
     */
    void set_up_extend_particle_limit() noexcept;
}

#endif
