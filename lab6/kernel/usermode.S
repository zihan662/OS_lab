.section .text
.align 4
.global enter_user_mode
# enter_user_mode(pagetable_t pt, uint64 uentry, uint64 usp)
enter_user_mode:
    # a0: pt
    # a1: uentry
    # a2: usp
    # switch to user pagetable
    srli t0, a0, 12
    li   t1, (8 << 60)        # SATP_SV39
    or   t0, t0, t1
    csrw satp, t0
    sfence.vma
    # set sepc to user entry
    mv   t0, a1
    csrw sepc, t0
    # set sstatus: clear SPP (previous mode = U), set SPIE
    csrr t1, sstatus
    li   t2, ~(1 << 8)        # ~SSTATUS_SPP
    and  t1, t1, t2
    li   t2, (1 << 5)         # SSTATUS_SPIE
    or   t1, t1, t2
    csrw sstatus, t1
    # set user stack pointer
    mv   sp, a2
    # clear a7 to avoid inheriting kernel a7 value
    mv   a7, zero
    # return to user mode
    sret

.global enter_user_after_fork
# enter_user_after_fork(pagetable_t pt, uint64 uentry, uint64 usp)
enter_user_after_fork:
    # switch to user pagetable
    srli t0, a0, 12
    li   t1, (8 << 60)
    or   t0, t0, t1
    csrw satp, t0
    sfence.vma
    # set sepc and sstatus
    mv   t0, a1
    csrw sepc, t0
    csrr t1, sstatus
    li   t2, ~(1 << 8)
    and  t1, t1, t2
    li   t2, (1 << 5)
    or   t1, t1, t2
    csrw sstatus, t1
    # set user sp
    mv   sp, a2
    # child returns 0 from fork
    li   a0, 0
    mv   a7, zero
    sret
