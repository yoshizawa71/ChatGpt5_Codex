       
       
       
       
 .bss
.global io26_debounce_counter
io26_debounce_counter:
    .long 0
.global io26_debounce_max_count
io26_debounce_max_count:
    .long 0
.global ext_sensor_status
ext_sensor_status:
    .long 0
.global ext_sensor_activated
ext_sensor_activated:
    .long 0
.global system_stable
system_stable:
    .long 0
.global sleep_transition_counter
sleep_transition_counter:
    .long 0
.global inactivity
inactivity:
 .long 0
 .global next_edge
next_edge:
 .long 0
 .global debounce_counter
debounce_counter:
 .long 0
 .global debounce_max_count
debounce_max_count:
 .long 0
 .global edge_count
edge_count:
 .long 0
 .global edge_count_to_wake_up
edge_count_to_wake_up:
 .long 0
 .global io_number
io_number:
 .long 0
.global ext_sensor_status_next
ext_sensor_status_next:
 .long 0
.global io_ext_sensor
io_ext_sensor:
 .long 0
.bss
    .set io26, 7
 .text
 .global entry
entry:
    move r3, sleep_transition_counter
    ld r3, r3, 0
    add r3, r3, 0
    jump check_transition, ov
    move r3, system_stable
    ld r3, r3, 0
    add r3, r3, 0
    jump skip_counting, eq
 move r3, io_number
 ld r3, r3, 0
 REG_RD ((((0x3ff48400 + 0x24)) - 0x3ff48000) / 4), ((14) + (16) - 1), (14)
    move r1, r0
 and r1, r0, 1<<7
    jump pin_check, eq
 rsh r0, r0, r3
    and r0, r0, 1
    move r3, next_edge
    ld r3, r3, 0
    sub r3, r0, r3
    jump changed, eq
    move r3, debounce_max_count
    move r2, debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    halt
read_io_high:
 REG_RD ((((0x3ff48400 + 0x24)) - 0x3ff48000) / 4), ((14 + 16) + (2) - 1), (14 + 16)
 sub r3, r3, 16
 rsh r0, r0, r3
read_done:
    and r0, r0, 1
    move r3, next_edge
    ld r3, r3, 0
    sub r3, r0, r3
    jump no_change, eq
    jump changed
no_change:
    move r3, debounce_max_count
    move r2, debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    halt
 check_transition:
    move r3, sleep_transition_counter
    ld r2, r3, 0
    sub r2, r2, 1
    st r2, r3, 0
    jump skip_counting, ov
    halt
 skip_counting:
    halt
 .global changed
changed:
 move r3, debounce_counter
 ld r2, r3, 0
 add r2, r2, 0
 jump edge_detected, eq
 sub r2, r2, 1
 st r2, r3, 0
 halt
 .global edge_detected
edge_detected:
 move r3, debounce_max_count
 move r2, debounce_counter
 ld r3, r3, 0
 st r3, r2, 0
 move r3, next_edge
 ld r2, r3, 0
 add r2, r2, 1
 and r2, r2, 1
 st r2, r3, 0
 move r3, edge_count
 ld r2, r3, 0
 add r2, r2, 1
 st r2, r3, 0
 halt
pin_check:
    rsh r0, r1, io26
    and r0, r0, 1
    move r3, ext_sensor_status
    ld r3, r3, 0
    sub r3, r0, r3
    jump pin_check_reset, eq
    jump io26_debounce_check
pin_check_reset:
    move r3, io26_debounce_max_count
    move r2, io26_debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    halt
io26_debounce_check:
rsh r0, r1, io26
    and r0, r0, 1
    move r3, ext_sensor_status
    ld r3, r3, 0
    sub r3, r0, r3
    jump pin_check, eq
    move r3, io26_debounce_counter
    ld r2, r3, 0
    sub r2, r2, 1
    st r2, r3, 0
    jump ext_sensor_status_changed, eq
    halt
ext_sensor_status_changed:
move r3, io26_debounce_max_count
    move r2, io26_debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    move r3, ext_sensor_status
    rsh r0, r1, io26
    and r0, r0, 1
    st r0, r3, 0
    move r3, ext_sensor_activated
    ld r2, r3, 0
    add r2, r2, 1
    st r2, r3, 0
    wake
    halt
