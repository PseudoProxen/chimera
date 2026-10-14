;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text

;# Takes the place of the instruction that begins the loop where the server sleeps until the next frame. The time since
;# the last frame was already worked out before this point, so all that is left to do is wait for the next frame and
;# carry on after the loop.
.globl _server_main_loop_sleep_asm
_server_main_loop_sleep_asm:
    call _server_main_loop_wait
    jmp dword ptr [_server_main_loop_resume_address]
