OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
  /* Place user text at fixed VA */
  . = 0x400000;

  .text : {
    /* Ensure _start is the very first bytes in .text */
    user/start_user.o(.text)
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  /* Minimal data sections (unused by current freestanding build) */
  .data : {
    *(.data .data.*)
  }
  .bss : {
    *(.bss .bss.*)
  }
}

