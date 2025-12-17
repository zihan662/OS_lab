# Lab1 实现步骤与思考

## 1. 实验目标
构建一个最小的 RISC-V 操作系统内核，能够启动并在串口（UART）打印 "Hello,os"。

## 2. 实现步骤

### 步骤 1: 定义内存布局 (Linker Script)
- **文件**: `kernel/kernel.ld`
- **内容**:
  - 指定架构为 `riscv`。
  - 设置入口点符号为 `_entry`。
  - 定义代码段（`.text`）起始地址为 `0x80200000`。
    - *注*: QEMU `virt` 机器的 RAM 起始于 `0x80000000`。OpenSBI（作为 Bios）通常运行在低地址，并将内核跳转到 `0x80200000`。
  - 定义数据段（`.data`）和 BSS 段（`.bss`）。
  - 在 `.bss` 段后预留 4KB 的空间作为内核栈。

### 步骤 2: 汇编入口 (Entry Point)
- **文件**: `kernel/entry.S`
- **内容**:
  - 定义全局标签 `_entry`。
  - **设置栈指针**: 将 `sp` 寄存器指向 `stack_top`（由链接脚本定义的栈顶符号）。这是 C 代码运行的前提。
  - **清零 BSS**: 遍历 `_bss_start` 到 `_bss_end`，将内存置零。
  - **跳转**: 使用 `call start` 跳转到 C 语言入口函数。
  - **自旋**: 如果 `start` 返回，进入死循环。

### 步骤 3: UART 驱动
- **文件**: `kernel/uart.c`, `kernel/memlayout.h`
- **内容**:
  - **地址定义**: 在 `memlayout.h` 中定义 `UART0` 基地址为 `0x10000000`。
  - **初始化 (`uartinit`)**:
    - 关闭中断。
    - 设置波特率（通常为 38400）。
    - 设置数据格式为 8 位，无校验位。
  - **输出字符 (`uart_putc`)**:
    - 轮询 LSR 寄存器的 `TX_IDLE` 位，等待发送缓冲区为空。
    - 向 THR 寄存器写入字符。
  - **输出字符串 (`uart_puts`)**: 循环调用 `uart_putc`。

### 步骤 4: 内核主函数 (Kernel Start)
- **文件**: `kernel/start.c`
- **内容**:
  - 引入 `uart` 相关函数声明。
  - 定义 `start` 函数：
    - 调用 `uartinit()` 初始化串口。
    - 调用 `uart_puts("Hello,os")` 打印目标字符串。
    - 进入死循环 `for(;;)`，防止程序跑飞。

### 步骤 5: 构建与运行 (Makefile)
- **文件**: `Makefile`
- **内容**:
  - 设置交叉编译器 `riscv64-unknown-elf-`。
  - 编译规则：将 `.S` 和 `.c` 文件编译为 `.o`。
  - 链接规则：使用 linker script 将 `.o` 文件链接为 `kernel.elf`。
  - 运行规则 (`make qemu`)：使用 `qemu-system-riscv64` 加载 `kernel.elf` 并运行。

## 3. 相关思考

### 为什么从 0x80200000 开始？
RISC-V QEMU `virt` 机器的物理内存从 `0x80000000` 开始。通常，`0x80000000` 到 `0x80200000` 的区域被 OpenSBI（Supervisor Binary Interface）使用，它负责硬件底层的初始化。OpenSBI 初始化完成后，会跳转到 `0x80200000` 执行操作系统内核。因此，我们在链接脚本中将起始地址设为 `0x80200000`。

### 栈的重要性
在汇编代码 `entry.S` 中，最关键的一步是设置 `sp` (Stack Pointer)。C 语言的函数调用机制（保存返回地址、参数传递、局部变量分配）完全依赖于栈。如果在调用 `start()` 之前没有正确设置 `sp`，程序将会崩溃。

### 内存映射 I/O (MMIO)
本实验通过直接读写特定的内存地址（`0x10000000`）来控制硬件（UART）。这是 RISC-V 架构与外设交互的标准方式。我们在 C 语言中通过指针强制转换（如 `(volatile unsigned char *)`）来访问这些物理地址，`volatile` 关键字告诉编译器不要优化这些读写操作，因为它们对应的是硬件寄存器而非普通内存。
