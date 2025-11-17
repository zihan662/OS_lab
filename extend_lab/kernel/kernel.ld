OUTPUT_ARCH("riscv")
ENTRY(_entry)  /* Set entry point to _entry in entry.S */

SECTIONS
{
  /* Starting address: QEMU loads kernel here and jumps to it */
  . = 0x80200000;

  /* Code segment (.text): Place entry first, then all code */
  .text : {
    kernel/entry.o(.text)  /* Ensure _entry is at 0x80000000 */
    *(.text .text.*)       /* All other code sections */
    etext = .; /* 定义 etext 为 .text 段结束地址 */
  }

  /* Data segment: Merge rodata and data for simplicity (no separate rodata/srodata) */
  .data : {
    . = ALIGN(16);         /* Basic alignment for data */
    *(.rodata .rodata.*)   /* Constants */
    *(.data .data.*)       /* Initialized variables */
  }

  /* BSS segment: Uninitialized data, including stack */
  .bss : {
    . = ALIGN(16);         /* Basic alignment */
    _bss_start = .;        /* Start of BSS (for clearing in entry.S) */
    *(.bss .bss.*)         /* All BSS sections */
    . = ALIGN(4096);       /* Page align for stack */
    stack_bottom = .;      /* Optional: Stack bottom */
    . += 4096;             /* 4KB stack space */
    stack_top = .;         /* Stack top (used in entry.S) */
    _bss_end = .;          /* End of BSS (for clearing in entry.S) */
  }

  /* 3. Define end symbol for kernel end (optional, for memory management) */
  PROVIDE(end = .);
}
