// SPDX-License-Identifier: GPL-3.0-only

#include "extend_limits.hpp"
#include "../signature/hook.hpp"
#include "../signature/signature.hpp"
#include "../chimera.hpp"
#include "../output/output.hpp"
#include "../halo_data/particle.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#define VISIBLE_OBJECT_LIMIT 1024
#define PARTICLE_VERTEX_LIMIT 0x6000

extern "C" {
    std::uint32_t *extend_limits_polies_count = nullptr;
    std::uint32_t extend_limits_poly_limit = 0x7FFE;
    const void *extend_limits_count_1 = nullptr;
    const void *extend_limits_count_2 = nullptr;
    const void *extend_limits_count_3 = nullptr;
    const void *extend_limits_count_4 = nullptr;

    void extend_limits_cmp_1() noexcept;
    void extend_limits_cmp_2() noexcept;
    void extend_limits_cmp_3() noexcept;
    void extend_limits_cmp_4() noexcept;
}

namespace Chimera {
    static auto bsp_polies = std::make_unique<std::uint32_t []>(BSP_POLY_LIMIT);
    void set_up_extend_limits() noexcept {
        // There's a ton of stuff here.
        static const float MAXIMUM_DRAW_DISTANCE = 1026.0F * 1.34F * 1.34F * 1.34F * 1.34F * 1.34F / 1.97F;
        static auto visible_objects = std::make_unique<std::uint32_t []>(VISIBLE_OBJECT_LIMIT);

        auto &bsp_poly_1_sig = get_chimera().get_signature("bsp_poly_1_sig");
        auto &bsp_poly_2_sig = get_chimera().get_signature("bsp_poly_2_sig");
        auto &bsp_poly_3_sig = get_chimera().get_signature("bsp_poly_3_sig");
        auto &bsp_poly_4_sig = get_chimera().get_signature("bsp_poly_4_sig");
        auto &bsp_poly_5_sig = get_chimera().get_signature("bsp_poly_5_sig");
        auto &bsp_poly_6_sig = get_chimera().get_signature("bsp_poly_6_sig");
        auto &bsp_poly_7_sig = get_chimera().get_signature("bsp_poly_7_sig");
        auto &bsp_poly_8_sig = get_chimera().get_signature("bsp_poly_8_sig");
        auto &bsp_poly_9_sig = get_chimera().get_signature("bsp_poly_9_sig");
        auto &bsp_poly_10_sig = get_chimera().get_signature("bsp_poly_10_sig");
        auto &bsp_poly_11_sig = get_chimera().get_signature("bsp_poly_11_sig");
        auto &bsp_poly_12_sig = get_chimera().get_signature("bsp_poly_12_sig");
        auto &bsp_poly_13_sig = get_chimera().get_signature("bsp_poly_13_sig");

        auto &bsp_poly_count_sig = get_chimera().get_signature("bsp_poly_count_sig");
        auto &bsp_poly_limit_1_sig = get_chimera().get_signature("bsp_poly_limit_1_sig");
        auto &bsp_poly_limit_2_sig = get_chimera().get_signature("bsp_poly_limit_2_sig");
        auto &bsp_poly_limit_3_sig = get_chimera().get_signature("bsp_poly_limit_3_sig");
        auto &bsp_poly_movsx_sig = get_chimera().get_signature("bsp_poly_movsx_sig");
        auto &bsp_poly_movsx_2_sig = get_chimera().get_signature("bsp_poly_movsx_2_sig");

        auto &visible_object_list_1_sig = get_chimera().get_signature("visible_object_list_1_sig");
        auto &visible_object_list_2_sig = get_chimera().get_signature("visible_object_list_2_sig");
        auto &visible_object_list_3_sig = get_chimera().get_signature("visible_object_list_3_sig");
        auto &visible_object_ptr_sig = get_chimera().get_signature("visible_object_ptr_sig");

        auto &visible_object_limit_1_sig = get_chimera().get_signature("visible_object_limit_1_sig");
        auto &visible_object_limit_2_sig = get_chimera().get_signature("visible_object_limit_2_sig");

        auto &draw_distance_sig = get_chimera().get_signature("draw_distance_sig");

        // Chimera allocated a new BSP polygon array. We need to point to those.
        overwrite(bsp_poly_1_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_2_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_3_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_4_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_5_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_6_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_7_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_8_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_9_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_10_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_11_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_12_sig.data() + 1, bsp_polies.get());
        overwrite(bsp_poly_13_sig.data() + 1, bsp_polies.get());

        // What I'm doing is making the polygon count a 32-bit integer
        extend_limits_polies_count = *reinterpret_cast<std::uint32_t **>(bsp_poly_count_sig.data() + 3);
        overwrite(bsp_poly_count_sig.data(), static_cast<std::uint8_t>(0xFF));
        overwrite(bsp_poly_count_sig.data() + 1, static_cast<std::uint8_t>(0x05));
        overwrite(bsp_poly_count_sig.data() + 2, extend_limits_polies_count);
        overwrite(bsp_poly_count_sig.data() + 6, static_cast<std::uint8_t>(0x90));

        if(get_chimera().feature_present("client_bsp_poly_demo")) { //005076AD
            auto &bsp_poly_count_demo_sig = get_chimera().get_signature("bsp_poly_demo_sig");
            overwrite(bsp_poly_count_demo_sig.data() + 1, bsp_polies.get());
        }

        SigByte nop_bsp_limit[9] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        write_code_s(bsp_poly_limit_1_sig.data(), nop_bsp_limit);
        write_code_s(bsp_poly_limit_2_sig.data(), nop_bsp_limit);
        write_code_s(bsp_poly_limit_3_sig.data(), nop_bsp_limit);
        static Hook bsp_limit_1_hook;
        static Hook bsp_limit_2_hook;
        static Hook bsp_limit_3_hook;
        write_function_override(bsp_poly_limit_1_sig.data(), bsp_limit_1_hook, reinterpret_cast<const void *>(extend_limits_cmp_1), &extend_limits_count_1);
        write_function_override(bsp_poly_limit_2_sig.data(), bsp_limit_2_hook, reinterpret_cast<const void *>(extend_limits_cmp_2), &extend_limits_count_2);
        write_function_override(bsp_poly_limit_3_sig.data(), bsp_limit_3_hook, reinterpret_cast<const void *>(extend_limits_cmp_3), &extend_limits_count_3);

        SigByte nop_movsx[3] = {0x90, 0x90, 0x90};
        write_code_s(bsp_poly_movsx_sig.data(), nop_movsx);

        SigByte new_movsx[] =  { -1, -1, -1, 0x85, 0xFF, 0x90, -1, -1, -1, -1, -1, -1, -1, 0x8B, 0xD7, 0x90 };
        write_code_s(bsp_poly_movsx_2_sig.data(), new_movsx);

        // Going above 32767 needs everything that reads the count as a signed 16-bit integer fixed, along with a bigger index buffer since each polygon takes up 6 bytes.
        if(get_chimera().feature_present("client_bsp_poly_limit")) {
            overwrite(get_chimera().get_signature("bsp_poly_index_buffer_size_sig").data() + 1, static_cast<std::uint32_t>((BSP_POLY_LIMIT + 2) * 6));
            overwrite(get_chimera().get_signature("bsp_poly_another_limit_sig").data() + 1, static_cast<std::uint32_t>(BSP_POLY_LIMIT + 2));

            // This is the offset the polygons are written to in the index buffer, so a negative value would write before it.
            SigByte mov_ecx_edi[] = { 0x8B, 0xCF, 0x90 };
            write_code_s(get_chimera().get_signature("bsp_poly_index_offset_sig").data() + 4, mov_ecx_edi);

            // Number of polygons in a single draw
            SigByte mov_edx_esi[] = { 0x8B, 0xD6, 0x90 };
            SigByte mov_ecx_esi[] = { 0x8B, 0xCE, 0x90 };
            write_code_s(get_chimera().get_signature("bsp_poly_draw_count_1_sig").data(), mov_edx_esi);
            write_code_s(get_chimera().get_signature("bsp_poly_draw_count_2_sig").data() + 4, mov_ecx_esi);
            write_code_s(get_chimera().get_signature("bsp_poly_draw_count_3_sig").data() + 8, mov_ecx_esi);

            // movsx esi, word ptr [count] -> mov esi, dword ptr [count]
            for(auto *sig : { "bsp_poly_read_count_1_sig", "bsp_poly_read_count_2_sig" }) {
                auto *read_count = get_chimera().get_signature(sig).data();
                overwrite(read_count, static_cast<std::uint8_t>(0x8B));
                overwrite(read_count + 1, static_cast<std::uint8_t>(0x35));
                overwrite(read_count + 2, extend_limits_polies_count);
                overwrite(read_count + 6, static_cast<std::uint8_t>(0x90));
            }

            // This check stops working once the count no longer fits in a signed 16-bit integer, so it has to use the real count, too.
            auto &bsp_poly_limit_4_sig = get_chimera().get_signature("bsp_poly_limit_4_sig");
            write_code_s(bsp_poly_limit_4_sig.data(), nop_bsp_limit);
            static Hook bsp_limit_4_hook;
            write_function_override(bsp_poly_limit_4_sig.data(), bsp_limit_4_hook, reinterpret_cast<const void *>(extend_limits_cmp_4), &extend_limits_count_4);

            extend_limits_poly_limit = BSP_POLY_LIMIT;
        }

        // Bump up the visible object limit
        overwrite(visible_object_list_1_sig.data() + 3, visible_objects.get());
        overwrite(visible_object_list_2_sig.data() + 1, static_cast<std::uint32_t>(VISIBLE_OBJECT_LIMIT));
        overwrite(visible_object_list_2_sig.data() + 5 + 1, visible_objects.get());
        overwrite(visible_object_list_3_sig.data() + 3, visible_objects.get());
        overwrite(visible_object_ptr_sig.data() + 3, visible_objects.get());
        overwrite(visible_object_limit_1_sig.data() + 7, static_cast<std::uint16_t>(VISIBLE_OBJECT_LIMIT));
        overwrite(visible_object_limit_2_sig.data() + 1, static_cast<std::uint32_t>(VISIBLE_OBJECT_LIMIT));

        overwrite(draw_distance_sig.data() + 1, &MAXIMUM_DRAW_DISTANCE);

        // Particles are four vertices each and run out of dynamic vertices at 2048 per frame, at which point whole groups of them disappear. This has to be done before the device is created.
        if(get_chimera().feature_present("client_particle_limit")) {
            overwrite(get_chimera().get_signature("particle_vertex_pool_sig").data() + 4, static_cast<std::uint32_t>(PARTICLE_VERTEX_LIMIT));
        }
    }

    void set_up_extend_particle_limit() noexcept {
        auto &chimera = get_chimera();
        if(!chimera.feature_present("client_particle_limit")) {
            return;
        }

        // Particle rendering sorts a list of 8-byte entries on the stack with no bounds checking. The list is at the top of the stack frame, so we only need to make the frame bigger.
        const std::uint32_t frame_size = 0x24F4 + (PARTICLE_LIMIT - 1024) * 8;
        overwrite(chimera.get_signature("particle_render_prologue_sig").data() + 1, frame_size);
        overwrite(chimera.get_signature("particle_render_epilogue_sig").data() + 6, frame_size);

        // There's no room left in the game state, and tag data sits right after it. Use our own table instead and leave the old one where it is so that the game state layout and existing saves are unaffected.
        auto **table_address = *reinterpret_cast<ParticleTable ***>(chimera.get_signature("particle_table_sig").data() + 2);
        auto *old_table = *table_address;
        if(!old_table) {
            return;
        }

        static auto table_data = std::make_unique<std::byte []>(sizeof(ParticleTable) + PARTICLE_LIMIT * old_table->element_size);
        auto *new_table = reinterpret_cast<ParticleTable *>(table_data.get());
        auto *new_elements = table_data.get() + sizeof(ParticleTable);

        std::memcpy(new_table, old_table, sizeof(ParticleTable));
        std::memcpy(new_elements, old_table->first_element, old_table->current_size * old_table->element_size);
        new_table->max_elements = PARTICLE_LIMIT;
        new_table->first_element = reinterpret_cast<Particle *>(new_elements);

        *table_address = new_table;
    }
}
