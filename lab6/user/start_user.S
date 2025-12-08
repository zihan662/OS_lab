.section .text
.align 4
.global _start
.extern main

_start:
    li   a0, 0
    li   a1, 0
    call main
    li   a0, 0
    li   a7, 4
    ecall
1:
    j 1b
