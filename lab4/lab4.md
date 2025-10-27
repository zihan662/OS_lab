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
  中断设计

- 为什么先 M 模式再委托 S 模式
  - 硬件定时器（如 CLINT/mtime ）通常只允许 M 模式访问；OpenSBI 作为 M 模式固件统一管理计时与电源、时间虚拟化、安全隔离。S 模式通过 SBI 的 ecall 请求计时，M 模式在到期时设置 S 模式定时器中断（ STIP ），再把中断“注入”给 S 模式。
  - 好处是可移植性（OS 不依赖具体 SoC 寄存器），虚拟化（可给每个客体系统提供独立时间视图），安全（S 模式无法直接触发/屏蔽关键硬件）。
  - 在你的代码中， timer.c 用 sbi_set_timer() 请求下一次定时；到期后 OpenSBI 触发 S 模式的 STIE ，你在 interrupts.c 中通过 enable_interrupt(5) 打开。
- 设计支持“中断优先级”的系统
  - 利用外部中断控制器（ PLIC ）的优先级寄存器： PLIC_PRIORITY + 4*irq 设每个 IRQ 的优先级， PLIC_SPRIORITY(hart) 设阈值，只有优先级高于阈值的中断会被送达。
  - 对于 S 定时器中断（ STIE ），它不走 PLIC；可以用软件策略实现优先级，例如：
    - 在 trap_handler / dispatch_irq 前检查当前“内核优先级”状态，必要时临时屏蔽低优中断（调整 sie 位或延迟处理）。
    - 将 ISR 做到极短，复杂工作下推到“下半部”（deferred work/tasklet），让高优先级 ISR 尽快响应。
  - 你的 interrupts.c 已有 register_interrupt() （链表），可以扩展为“按优先级插入”或为每个 IRQ 维护一个最优先处理器。
性能考虑

- 中断处理的主要时间开销
  - 进入/退出陷阱的保存/恢复上下文（ kernel/trap.S ）；CSR 读写（ r_sstatus/w_sstatus 等）和流水线 flush。
  - 中断控制器/固件交互（ SBI ecall 的开销； PLIC 的 claim/complete 内存访问）。
  - ISR 自身工作量（尤其是打印、锁和内存访问）。
- 优化策略
  - 减小 ISR 工作量：只做必要的计数与重装定时器，将其他工作下推到主循环或工作队列。
  - 减少日志与 I/O：避免在 ISR 或高频路径中 printf ；你已改用 wfi 并仅在计数变化时打印，正确。
  - 使用向量模式（ stvec 的 vectored 模式）可避免统一入口的额外分发开销。
  - 调整定时器频率与批量化：降低 tick_interval 或在高频场景下合并事件、成批处理。
  - 锁优化与无锁计数：如使用原子或 per-hart 数据，减少抢占/锁开销。
高频率中断的影响

- 增加上下文切换与管线扰动，降低吞吐（CPU 时间被中断占用）。
- 产生抖动（jitter）与延迟累积，影响实时性与调度稳定性。
- 可能导致其他工作饥饿（尤其在单核），需要限流或动态调整频率。
- 缓解方法：降低中断频率、合并事件、将中断改为“通知+批处理”，或让高频源走更轻量路径。
可靠性

- 确保中断处理函数安全
  - ISR 内不做可阻塞操作，不访问可能睡眠的接口，不长时间持锁。
  - 保守地重装定时器、防止丢 tick（你的 timer_interrupt() 在每次触发后都会 sbi_set_timer(next) ，正确）。
  - 使用简单、可验证的共享状态结构（如 volatile 计数），避免竞态。
- 错误处理策略
  - 明确分类异常：你的 interrupts.c 已按 scause 分派；未知异常打印后 panic 。
  - 对可恢复错误（如设备偶发错误）采用退避重试/软重置；对不可恢复错误（非法指令、严重页故障）选择 panic 或进入安全模式。
  - 记录上下文（ scause/sepc/stval ）便于诊断；必要时做最小化转储。
扩展性

- 支持更多中断源
  - 为 PLIC 增加对应设备的 enable/priority 初始化；对每个新 irq 使用 register_interrupt(irq, handler) 。
  - 把处理器/中断绑定到特定 hart（CPU affinity），提高并行性，减少跨核争用。
- 动态路由
  - 在 interrupts.c 中维护可修改的路由表（你的链表已经支持动态增删）；可以扩展为：
    - 支持按 hart 注册不同处理函数。
    - 支持在运行时改变优先级/阈值（更新 PLIC_SPRIORITY(hart) 与对应 priority 寄存器）。
实时性

- 当前延迟特征
  - 使用 SBI 设定定时器会引入 M 模式路径开销；统一入口 stvec 会有额外的分发/栈保存成本。
  - 你的 wfi 在等待阶段能降低忙等干扰，提升响应确定性。
- 面向实时的设计要点
  - ISR 最短化 + 下半部；严格控制禁中断时长与锁持有时长。
  - 使用向量模式、核内优先级管理，让高优先级中断更快拿到 CPU。
  - 调整 tick_interval 与时钟源精度，保证期限（deadline）满足；必要时采用硬件定时器的比较寄存器直接映射（某些平台可在 S 模式访问），减少依赖固件。
  - 监测与量化延迟：在 trap_handler /ISR 采样 get_time() ，统计分位数（p95/p99），据此优化。
如果你希望，我可以继续在 interrupts.c 里加入 PLIC 优先级与阈值的动态接口，并把 trap_init() 的调试日志降噪，或者把更多测试输出改为只在关键事件时打印，进一步提升可读性与实时性表现。