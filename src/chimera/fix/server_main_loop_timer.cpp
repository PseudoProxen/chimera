// SPDX-License-Identifier: GPL-3.0-only

// This is based on the main loop timer from SAPP.

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "../chimera.hpp"
#include "../config/ini.hpp"
#include "../signature/hook.hpp"
#include "../signature/signature.hpp"

#include "server_main_loop_timer.hpp"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
// Only understood by Windows 10 1803 and later
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace Chimera {
    /** Offset from the start of the signature to the code that runs once the sleep loop is over */
    static constexpr std::size_t RESUME_OFFSET = 48;

    /** Size of the instruction at the start of the sleep loop, which is what gets replaced */
    static constexpr std::size_t REPLACED_SIZE = 6;

    /** Halo runs at 30 frames per second, and timers only do whole milliseconds */
    static constexpr LONG PERIOD_MS = 33;

    /** Wait this long at most, in case the timer is somehow broken, so the server can never freeze */
    static constexpr DWORD MAX_WAIT_MS = 1000;

    /** Wakes later than this many milliseconds past the period are counted as late */
    static constexpr double LATE_MS = 2.0;

    extern "C" {
        /** Where to go once the main loop timer has had its wait */
        const void *server_main_loop_resume_address = nullptr;

        void server_main_loop_sleep_asm() noexcept;
        void server_main_loop_wait() noexcept;
    }

    static HANDLE main_loop_timer = nullptr;
    static ServerMainLoopTimerMode main_loop_timer_mode = ServerMainLoopTimerMode::NONE;

    // Only touched by the main loop thread
    static double counts_per_ms = 0.0;
    static LARGE_INTEGER last_wake = {};
    static std::size_t stat_wakes = 0;
    static double stat_sum = 0.0;
    static double stat_min = 0.0;
    static double stat_max = 0.0;
    static std::size_t stat_late = 0;

    static void reset_stats() noexcept {
        last_wake.QuadPart = 0;
        stat_wakes = 0;
        stat_sum = 0.0;
        stat_min = 0.0;
        stat_max = 0.0;
        stat_late = 0;
    }

    static void record_wake() noexcept {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        if(last_wake.QuadPart != 0) {
            double interval = static_cast<double>(now.QuadPart - last_wake.QuadPart) / counts_per_ms;
            if(stat_wakes == 0 || interval < stat_min) {
                stat_min = interval;
            }
            if(interval > stat_max) {
                stat_max = interval;
            }
            if(interval > static_cast<double>(PERIOD_MS) + LATE_MS) {
                stat_late++;
            }
            stat_sum += interval;
            stat_wakes++;
        }

        last_wake = now;
    }

    // Halo calls into this with a stack that may not be aligned the way the compiler expects
    extern "C" __attribute__((force_align_arg_pointer)) void server_main_loop_wait() noexcept {
        WaitForSingleObject(main_loop_timer, MAX_WAIT_MS);
        record_wake();
    }

    static bool create_timer() noexcept {
        using CreateWaitableTimerExFunction = HANDLE (WINAPI *)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);

        // Windows XP does not have this
        auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
        CreateWaitableTimerExFunction create_waitable_timer_ex = nullptr;
        if(kernel32) {
            create_waitable_timer_ex = reinterpret_cast<CreateWaitableTimerExFunction>(reinterpret_cast<void *>(GetProcAddress(kernel32, "CreateWaitableTimerExW")));
        }

        // A high resolution timer wakes on time regardless of the timer resolution
        if(create_waitable_timer_ex) {
            main_loop_timer = create_waitable_timer_ex(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if(main_loop_timer) {
                main_loop_timer_mode = ServerMainLoopTimerMode::HIGH_RESOLUTION;
            }
        }

        // Otherwise this only wakes as often as the timer resolution allows, which the timer resolution fix raises
        if(!main_loop_timer) {
            main_loop_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            main_loop_timer_mode = ServerMainLoopTimerMode::STANDARD;
        }

        if(!main_loop_timer) {
            main_loop_timer_mode = ServerMainLoopTimerMode::NONE;
            return false;
        }

        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        counts_per_ms = static_cast<double>(frequency.QuadPart) / 1000.0;

        // Relative and negative, so fire right away and then every PERIOD_MS
        LARGE_INTEGER due;
        due.QuadPart = -1;
        if(!SetWaitableTimer(main_loop_timer, &due, PERIOD_MS, nullptr, nullptr, FALSE)) {
            CloseHandle(main_loop_timer);
            main_loop_timer = nullptr;
            main_loop_timer_mode = ServerMainLoopTimerMode::NONE;
            return false;
        }

        reset_stats();
        return true;
    }

    void set_up_server_main_loop_timer() noexcept {
        auto &chimera = get_chimera();
        if(!chimera.feature_present("server_main_loop_timer")) {
            return;
        }
        if(!chimera.get_ini()->get_value_bool("halo.server_main_loop_timer").value_or(true)) {
            return;
        }
        if(!create_timer()) {
            return;
        }

        auto *loop = chimera.get_signature("server_main_loop_sleep_sig").data();
        server_main_loop_resume_address = loop + RESUME_OFFSET;

        // Replace the instruction that begins the sleep loop with a jmp to our wait (and a nop for the leftover byte)
        std::uint8_t patch[REPLACED_SIZE];
        auto offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(server_main_loop_sleep_asm) - reinterpret_cast<std::uintptr_t>(loop + 5));
        patch[0] = 0xE9;
        std::memcpy(patch + 1, &offset, sizeof(offset));
        patch[5] = 0x90;
        overwrite(loop, patch, sizeof(patch));
    }

    ServerMainLoopTimerStats get_server_main_loop_timer_stats(bool reset) noexcept {
        ServerMainLoopTimerStats stats;
        stats.mode = main_loop_timer_mode;
        stats.period_ms = static_cast<std::size_t>(PERIOD_MS);
        stats.wakes = stat_wakes;
        stats.average_ms = stat_wakes ? stat_sum / static_cast<double>(stat_wakes) : 0.0;
        stats.min_ms = stat_min;
        stats.max_ms = stat_max;
        stats.late = stat_late;

        if(reset) {
            reset_stats();
        }

        return stats;
    }
}
