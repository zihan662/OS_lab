# 系统调用实现过程

## 总览
- 用户态通过 `a7` 传递系统调用号，`a0..a2` 等寄存器传参，执行 `ecall` 陷入内核。
- 陷阱入口在 `kernel/trap.S:1` 保存通用寄存器，采集 `scause/sepc/stval`，并将保存的上下文指针传给 C 语言处理函数。
- 内核在 `kernel/interrupts.c:210` 构造统一的 `syscall_frame`，调用 `syscall_dispatch` 分发至具体系统调用实现，返回值写回 `a0`，`sepc` 前进 4 字节跳过 `ecall`。
- 指针与缓冲区在 `kernel/syscall.c` 做用户态地址范围与页表权限校验，避免越权读写内核页或未映射页。

## 用户态封装
- 调用约定在 `user/user.h:9-69`：
  - `syscall0/1/2/3` 将参数放入 `a0..a2`，调用 `ecall`，返回值从 `a0` 取回。
  - 例如 `usys_write(fd, buf, n)` 使用 `syscall3` 封装（`user/user.h:94-97`）。
- 示例测试在 `user/test_syscalls.c:5-22` 等函数中依次调用封装好的用户接口。

## 陷阱入口与上下文保存
- `kernel/trap.S:1-40`：保存寄存器、读取 `scause/sepc/stval`，传参如下：
  - `a0=scause`，`a1=sepc`，`a2=stval`，`a3=sp(保存区指针)`，`a4=a7(系统调用号)`，`a5=72(sp)处保存的原始a0`。
- 跳转 `call trap_handler` 后，在返回路径恢复寄存器并根据 `SPP` 进行栈切换，最终 `sret` 回到用户态（`kernel/trap.S:68-96`）。

## 统一分发与返回
- `kernel/interrupts.c:185-233` 的 `trap_handler`：
  - 判断中断/异常类型；遇到 `ECALL_U/ECALL_S` 时，构造 `syscall_frame`（取 `a0..a6` 与 `a7`、`sepc`），调用 `syscall_dispatch`（`kernel/interrupts.c:210-229`）。
  - 分发返回后，`ctx_write64(..., CTX_OFF_A0, ret)` 将返回值写回保存区对应的 `a0`（`kernel/interrupts.c:225-227`）。
  - `w_sepc(sepc + 4)` 推进到 `ecall` 下一条指令（`kernel/interrupts.c:229`）。
- `syscall_dispatch` 在 `kernel/syscall.c:279-303`：
  - 检查系统调用号范围和权限（`syscall_check_perm`），定位 `syscall_table`（`kernel/syscall.c:259-274`），调用对应 `handler` 并返回结果。

## 指针与缓冲区校验
- 用户指针校验在 `kernel/syscall.c`：
  - 早期的粗略范围校验 `validate_user_range`（`kernel/syscall.c:21-28`）已被页表级权限检查替换：`check_user_va_perm`（`kernel/syscall.c:20-34`）。
  - 新逻辑逐页检查用户页表 `PTE_V`、`PTE_U`，并要求读/写时具备 `PTE_R/PTE_W`；范围不能越过 `TRAPFRAME`。
  - `get_user_buffer` 与 `put_user_buffer` 分别在读和写路径调用相应的权限检查（`kernel/syscall.c:56-66`、`69-79`）。

## 具体系统调用示例
- `write`（`SYS_write`）：
  - 入口 `h_write` 在 `kernel/syscall.c:123-149`，参数 `fd/a1(buf)/a2(n)`；分块复制用户缓冲区到内核临时区，路由到控制台或假文件系统，累计写入长度返回。
  - 用户态封装 `usys_write` 在 `user/user.h:94-97`。
- `read`（`SYS_read`）：
  - 入口 `h_read` 在 `kernel/syscall.c:164-183`，从设备/文件系统读取到临时区，再复制到用户缓冲区，返回读取字节数。
- `exit`（`SYS_exit`）：
  - 入口 `h_exit` 在 `kernel/syscall.c:112-115`，调用 `exit_process`（`kernel/proc.c:113-130`）将当前进程置为 `ZOMBIE` 并切换回调度器。
- `getpid`（`SYS_getpid`）：
  - 入口 `h_getpid` 在 `kernel/syscall.c:185-189`，返回当前进程的 `pid`。
- `wait`（`SYS_wait`）：
  - 入口 `h_wait` 在 `kernel/syscall.c:191-194`，调用 `wait_process`（`kernel/proc.c:231-253`）回收子进程并返回其 pid。
- `fork`（`SYS_fork`）：
  - 入口 `h_fork` 在 `kernel/syscall.c:217-249`：为子进程创建独立用户页表（`create_pagetable` + `init_user_pagetable`），复制父进程用户空间（`copy_user_space` 在 `kernel/vm.c:93-110`），设置用户返回点与栈指针，子进程入口由 `user_fork_entry` 恢复到用户态返回 0（`kernel/start.c:45-50`、`kernel/usermode.S:32-56`）。

## 返回路径
- 分发后，返回值写回 `a0`（`kernel/interrupts.c:225-227`），`sepc` 推进（`kernel/interrupts.c:229`），`trap.S` 恢复现场并 `sret`（`kernel/trap.S:68-96`）。
- 用户态随后在调用点继续执行，读取 `a0` 作为系统调用返回值。

## 异常与故障处理（与系统调用相关的健壮性）
- 非法指令：`handle_illegal_instruction`（`kernel/interrupts.c:154-158`）打印并跳过当前指令以便继续测试。
- 页错误：
  - 加载页错误 `handle_load_page_fault` 打印并诊断（`kernel/interrupts.c:130-136`）。
  - 存储页错误 `handle_store_page_fault`：若来源为用户态（`SSTATUS_SPP==0`），打印用户页表项并终止当前进程（`exit_process(-1)`），避免内核 `panic`（`kernel/interrupts.c:138-158`）；若来源为内核态，打印内核页表项并 `panic`。

## 数据路径示意
1. 用户态：`usys_write(fd, buf, n)`（`user/user.h:94-97`）→ `ecall`
2. 陷阱入口：`trap.S` 保存现场，调用 `trap_handler`（`kernel/trap.S:1-40`）
3. 分发：`trap_handler` 构造帧 → `syscall_dispatch` 按 `syscall_table` 分发（`kernel/interrupts.c:210-229`、`kernel/syscall.c:259-274`）
4. 校验与执行：`check_user_va_perm` 校验用户缓冲区 → 执行具体 `h_write`（`kernel/syscall.c:20-34`、`123-149`）
5. 返回：写回 `a0`、`sepc+4`、`sret` 回到用户态（`kernel/interrupts.c:225-229`、`kernel/trap.S:68-96`）
