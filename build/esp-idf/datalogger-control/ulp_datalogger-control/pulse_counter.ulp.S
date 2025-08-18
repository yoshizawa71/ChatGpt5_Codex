       
       
       
.global pulse_status
pulse_status:
 .long 0
.global pulse_status_next
pulse_status_next:
 .long 0
.global pulsed
pulsed:
 .long 0
.global err_halt
err_halt:
 .long 0
.bss
    .set io36, 0
    .set io26, 7
 .text
 .global entrance
entrance:
pin_check:
 REG_RD ((((0x3ff48400 + 0x24)) - 0x3ff48000) / 4), ((14) + (16) - 1), (14)
 move r1, r0
 move r3, pulse_status
    rsh r0, r1, io36
 and r0, r0, 1
 st r0, r3, 0
    rsh r0, r1, io36
 and r0, r0, 1
 move r3, pulse_status_next
 ld r3, r3, 0
 add r3, r0, r3
    and r3, r3, 1
 jump pulse_status_changed, eq
 jump pin_check
pulse_status_changed:
 move r3, pulse_status_next
 ld r2, r3, 0
 add r2, r2, 1
 and r2, r2, 1
 st r2, r3, 0
 move r3, pulsed
 ld r2, r3, 0
 add r2, r2, 1
 st r2, r3, 0
    jump wake_up
jump entrance
halt
