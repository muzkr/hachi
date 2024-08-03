
.thumb
.syntax unified

.text
.global run_prog_flash
.type run_prog_flash,%function

#include "hardware/regs/addressmap.h"
#include "hardware/regs/m0plus.h"

run_prog_flash:

// push {lr}
// sub sp, sp, #4
ldr r1, =(PPB_BASE + M0PLUS_VTOR_OFFSET)
str r0, [r1]
ldmia r0, {r0, r1}
msr msp, r0
bx r1
// add sp, sp, #4
// pop {pc} 
