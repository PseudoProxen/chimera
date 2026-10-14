// SPDX-License-Identifier: GPL-3.0-only

#include <windows.h>
#include <timeapi.h>

#include "timer_resolution.hpp"

namespace Chimera {
    // Not in older headers. These are the values of PROCESS_POWER_THROTTLING_STATE and its flags.
    struct PowerThrottlingState {
        ULONG version;
        ULONG control_mask;
        ULONG state_mask;
    };
    static constexpr int PROCESS_POWER_THROTTLING_CLASS = 4;
    static constexpr ULONG POWER_THROTTLING_EXECUTION_SPEED = 0x1;
    static constexpr ULONG POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 0x4;

    // The call and flags don't exist before Windows 8 / Windows 11 (or on Wine), so look it up at runtime
    static void disable_power_throttling() noexcept {
        using SetProcessInformationFunction = BOOL (WINAPI *)(HANDLE, int, LPVOID, DWORD);

        auto *kernel32 = GetModuleHandleW(L"kernel32.dll");
        if(!kernel32) {
            return;
        }
        auto *set_process_information = reinterpret_cast<SetProcessInformationFunction>(reinterpret_cast<void *>(GetProcAddress(kernel32, "SetProcessInformation")));
        if(!set_process_information) {
            return;
        }

        PowerThrottlingState state = { 1, POWER_THROTTLING_EXECUTION_SPEED | POWER_THROTTLING_IGNORE_TIMER_RESOLUTION, 0 };
        if(!set_process_information(GetCurrentProcess(), PROCESS_POWER_THROTTLING_CLASS, &state, sizeof(state))) {
            // Windows 10 doesn't know the timer resolution flag
            state.control_mask = POWER_THROTTLING_EXECUTION_SPEED;
            set_process_information(GetCurrentProcess(), PROCESS_POWER_THROTTLING_CLASS, &state, sizeof(state));
        }
    }

    void set_up_timer_resolution_fix() noexcept {
        // This is released by Windows when the process exits
        timeBeginPeriod(1);
        disable_power_throttling();
    }
}
