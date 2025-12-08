.section .rodata
.align 4
.global initcode_start
.global initcode_end
initcode_start:
.incbin "user/test_syscalls.bin"
initcode_end:

