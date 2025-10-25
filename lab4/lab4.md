框架概览

- 提供统一的中断接口与调度路径： trap_init 、 register_interrupt 、 enable_interrupt 、 disable_interrupt
- 使用汇编向量入口保存现场，转入 C 的 trap_handler 按 scause 分类分发
- 支持 PLIC 外部中断与 S 模式时钟中断，兼容 OpenSBI/Sstc 的实验环境
- 以链表支持共享中断，同一 IRQ 可挂多个处理函数
关键接口

- typedef void (*interrupt_handler_t)(void);
- void trap_init(void); 初始化 stvec 指向汇编向量，设定 PLIC 阈值
- void register_interrupt(int irq, interrupt_handler_t h); 注册或注销处理函数
  - 传入 h == NULL 表示注销该 irq 下所有已注册的处理函数
- void enable_interrupt(int irq); 开启特定中断并打开全局 S 模式中断使能
- void disable_interrupt(int irq); 关闭特定中断，不影响其他已开启的中断
向量表与陷入路径

- trap.S 的 trap_vector 入口：
  - 保存所有通用寄存器到栈
  - 读取 scause 、 sepc 、 stval 放入 a0 / a1 / a2
  - 调用 C 层的 trap_handler(scause, sepc, stval)
  - 恢复寄存器并 sret 返回
- trap_handler 分发策略：
  - scause 为中断时（高位为 1）：
    - code == 5 ：S 模式时钟中断，分发到 irq=5 的处理链，更新下一次 tick
    - code == 9 ：S 模式外部中断，从 PLIC claim 取设备 IRQ 号，分发并写回 claim
  - 非中断异常（例如访存异常）当前直接死循环，可后续扩展异常处理
优先级与共享

- 默认为每个外部设备 IRQ 设置 PLIC 优先级 1 ，hart 阈值设为 0 ，优先级机制可在此基础上扩展
- 共享中断通过 irq → 链表 结构实现，同一 IRQ 的多个处理函数按注册顺序依次调用
嵌套与屏蔽

- 框架默认不在陷入期间打开嵌套（trap 入口时 SIE 自动清除）
- 在 enable_interrupt() 中：
  - 针对时钟中断置位 SIE_STIE 并安排下一次计时
  - 针对外部中断置位 SIE_SEIE 并在 PLIC 侧使能
  - 最后统一打开全局 SSTATUS_SIE ，允许在“非陷入态”发生中断
- 若需要“嵌套中断”，可在 trap_handler 合适位置（例如分发前）短暂打开 SSTATUS_SIE ，但要评估可重入性与栈空间；建议先在时钟中断上验证再推广
基本时钟中断

- 使用 Sstc stimecmp CSR 实现（已在 riscv.h 提供封装）：
  - enable_interrupt(5) 会置位 SIE_STIE 并设置下一次 stimecmp （间隔 TICK_INTERVAL ，默认 1_000_000 周期）
  - trap_handler 处理 irq=5 后刷新下一次 stimecmp
- 如运行环境不支持 Sstc，可切换为 SBI set_timer 方案（保留接口不变，内部改为通过 SBI 设置下一次定时），当前实验环境的 riscv.h 已提供 Sstc 支持
使用示例

- 初始化（已接入现有流程）：
  - 在 kvminit() / kvminithart() 之后调用 trap_init() （已替换到 start.c 的 test_virtual_memory() 中）
- 注册并开启时钟中断：
  - 定义处理函数： void tick(void) { /* 做计时/调度/统计等 */ }
  - 注册： register_interrupt(5, tick);
  - 开启： enable_interrupt(5);
- 注册并开启 UART 外部中断（以 memlayout.h 的 UART0_IRQ 为例）：
  - register_interrupt(UART0_IRQ, uart_isr);
  - enable_interrupt(UART0_IRQ);
- 注销某个 IRQ 的处理函数链：
  - register_interrupt(irq, NULL); // 清除该 IRQ 的所有处理函数
  实现检查

- 保存到哪里
  - 当前设计将上下文保存到“当前内核栈”（ sp ）上。对 Lab3 目标（仅 S 态中断、内核态运行）是合理的。
  - 若未来涉及 U 态陷阱或在用户栈上触发中断，需要在进入向量时切换到“每 hart 的内核栈”，常见做法是使用 sscratch 保存内核栈指针并在向量入口切换。
- 保存哪些寄存器
  - 已保存除 x0 外的所有 31 个通用寄存器： ra/sp/gp/tp/t0-t6/s0-s11/a0-a7 ，覆盖调用者/被调用者保存约定，满足中断安全。
  - 额外读取了 scause 、 sepc 、 stval 到 a0/a1/a2 传给 C 处理函数，满足基本异常/中断分类需求。
- 快速保存与恢复
  - 使用顺序 sd / ld 和整块栈帧 addi sp, sp, -256 / +256 的方式，常数偏移、无分支，性能与可读性兼顾。
  - 已修复“恢复阶段先加载 sp ”的问题，防止后续寄存器加载以错误基址进行。
  