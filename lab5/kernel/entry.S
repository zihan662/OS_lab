.section .text
.global _entry
_entry:
    # Set up stack pointer (simplified, single fixed stack)
    # Assume stack_top is defined in linker script or C (e.g., a global array)
    la sp, stack_top   # Load address of stack_top into sp

    # 保存 OpenSBI 传入的 hartid（a0）到 tp，供内核使用
    mv tp, a0

    # Optional: Clear BSS segment (if needed; can move to C for simplicity)
    # For minimal, skip or implement simply here.
    la a0, _bss_start  # Assume symbols from linker script
    la a1, _bss_end
    bge a0, a1, bss_done
bss_loop:
    sb zero, 0(a0)
    addi a0, a0, 1
    blt a0, a1, bss_loop
bss_done:

    # Jump to C main function (e.g., in main.c)
    call start

    # Infinite loop if main returns (spin)
spin:
    j spin
