;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text

;# Load a map with a Halo function. This is the entry point for the map loading thread.
;#
;# The Halo load map function takes the map name in eax and returns in eax.
.globl _load_map_worker_asm
_load_map_worker_asm:
    mov eax, [esp + 0x4]
    call dword ptr [_load_map_function_address]
    ret

;# Replaces the Halo load map function. The map name is in eax.
.globl _load_map_override_asm
_load_map_override_asm:
    push eax
    call _load_map_override
    add esp, 0x4
    ret

;# Called just before Halo passes its behavior flags to CreateDevice. edi holds the flags.
.globl _set_d3d9_device_multithreaded_flag
_set_d3d9_device_multithreaded_flag:
    or edi, 0x4 ;# D3DCREATE_MULTITHREADED
    ret

;# Called in place of the function that draws the original loading screen background. edx is the color mask.
;#
;# The background is not drawn (it is replaced by our own), but we do keep track of the alpha so we can fade out with
;# Halo. The caller cleans up the arguments and Halo does not expect anything back, so we preserve everything and return.
.globl _loading_screen_background_render_asm
_loading_screen_background_render_asm:
    pushfd
    pushad

    push edx
    call _loading_screen_background_render_hook
    add esp, 0x4

    popad
    popfd
    ret
