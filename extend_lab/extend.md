总体思路

- 以“基础优先级调度”为骨架，叠加“多级反馈队列（MLFQ）”的时间片控制与动态降级，保留“优先级老化（aging）”的动态升级，形成“高优先级短片、用满片降级、等待升级”的闭环。
- 调度选择以“有效优先级”为准，具有同级轮转（ rr_cursor ）避免偏向；软抢占通过安全点检查（ preempt_check ）实现时间片用尽后的快速让出。
核心数据结构

- struct proc （ kernel/proc.h ）
  - priority ：基础优先级，范围 PRIORITY_MIN..PRIORITY_MAX （0..10），默认 PRIORITY_DEFAULT=5 。
  - ticks ：累积运行的时钟节拍数（CPU 时间）。
  - wait_time ：在 RUNNABLE 状态下的等待时间，用于老化升级。
  - slice_ticks ：MLFQ 当前时间片内已用节拍数。
  - need_resched ：软抢占标志，时间片用尽（被降级）后置位。
- 全局调度辅助
  - rr_cursor ：同级选择的轮转起点索引。
  - last_sched_pid / sched_pick_counter ：调度日志节流（仅在 pid 变化或每 5 次选择打印）。
关键函数分层次说明

- 配置常量（ kernel/proc.h ）
  
  - PRIORITY_MIN/PRIORITY_MAX/PRIORITY_DEFAULT ：优先级边界与默认。
  - AGING_INTERVAL ：每等待多少 tick 升一级（封顶至 PRIORITY_MAX ）。
- mlfq_slice_for(int prio) （ kernel/proc.c ）
  
  - 作用：根据当前优先级计算时间片长度，实现“级别越高、时间片越短”的交互友好策略。
  - 示例实现思想： slice = 22 - 2 * prio （ prio=10 → slice=2 ； prio=0 → slice=22 ），可按需微调。
- alloc_process() / free_process() （ kernel/proc.c ）
  
  - 初始化 priority=PRIORITY_DEFAULT ， ticks=0 ， wait_time=0 ， slice_ticks=0 ， need_resched=0 。
  - 释放时清理并移除 PID 映射，保证调度表干净。
- effective_priority(const struct proc *p) （ kernel/proc.c ）
  
  - 计算“有效优先级”（通常等于基础 priority ，如未来扩展可叠加 I/O 加权或实时策略）。
  - scheduler() 用它作为“比较分数”决定选中进程。
- scheduler() （ kernel/proc.c ）
  
  - 遍历所有 RUNNABLE 进程，取 effective_priority 最大者为 best ；同分时按 rr_cursor 保证轮转。
  - 选中后设置 best->state=RUNNING ，保存 best_idx 更新 rr_cursor=(best_idx+1)%NPROC 。
  - 日志（精简版）：仅在 pid 变化或每 5 次选择打印
    - 格式： sched_pick pid=... prio=... eff=... slice=used/len ticks=...
  - 切换： swtch(&c->context, &best->context) ，返回后清空 c->proc 。
- proc_on_tick() （ kernel/proc.c ）
  
  - 每次时钟中断：
    - 对 RUNNING 进程： ticks++ ， slice_ticks++ 。
      - 若 slice_ticks >= mlfq_slice_for(priority) ：
        - 若 priority > PRIORITY_MIN ： priority-- （MLFQ 降级）。
        - slice_ticks=0 ， need_resched=1 （软抢占标志）。
        - 打印： mlfq_demote pid=... -> prio=... 。
    - 对 RUNNABLE 进程： wait_time++ 。
      - 若 wait_time >= AGING_INTERVAL ：
        - 若 priority < PRIORITY_MAX ： priority++ （老化升级）。
        - wait_time=0 。
        - 打印： aging_promote pid=... -> prio=... 。
  - 作用：形成“CPU 密集型下沉 + 等待者上升”的动态平衡。
- yield() （ kernel/proc.c ）
  
  - 自愿让出 CPU。
  - 重置当前进程的 slice_ticks=0 ， need_resched=0 ，并把状态置为 RUNNABLE ，回到 scheduler() 。
  - 作用：交互型进程频繁 yield 不会被当作“用满片”降级。
- preempt_check() （ kernel/proc.c ）
  
  - 软抢占的安全点检查（例如在 burn_cycles() 中被调用）。
  - 若当前进程 need_resched=1 ：
    - 将其置为 RUNNABLE ，清除 need_resched ，跳转到 scheduler() 。
  - 作用：时间片用尽后无需显式 yield ，在忙等点也能尽快让出。
- sleep(void *chan, struct spinlock *lk) / wakeup(void *chan) （ kernel/proc.c ）
  
  - sleep ：把当前进程置为 SLEEPING ， chan 标识睡眠原因；等待时不计 wait_time 的老化（通常仅 RUNNABLE 累计等待）。
  - wakeup ：唤醒所有在 chan 上的进程为 RUNNABLE 。
- setpriority(int pid, int value) / getpriority(int pid) （ kernel/proc.c + kernel/interrupts.c + user/user.h ）
  
  - 用户态接口： nice pid prio 调用 setpriority ； getpriority(pid) 获取当前优先级。
  - 设置时会约束在合法区间，重置 slice_ticks=0 避免立即被“片用尽判定”。
- proc_dump_detailed() （ kernel/proc.c ）
  
  - 以行格式打印： PID PRIORITY STATE TICKS 。
  - 用户态接口：新增 SYS_procdump ， user/procdump() 封装， user/ps.c 程序直接调用以显示全表。
软抢占接入点

- burn_cycles() （ kernel/start.c ）
  - 在忙等循环中插入 preempt_check() ，当进程在 proc_on_tick() 被降级且 need_resched=1 ，会在此处快速让出。
  - 效果：CPU 密集的任务（不显式 yield ）也能被时间片耗尽后迅速切走，演示 MLFQ 的抢占特性。
定性测试与演示函数

- 内核测试（ kernel/start.c ）
  
  - 任务 A/B/C ：通过 burn_cycles() + 偶尔 yield() 分别模拟 CPU 密集与交互型。
  - test_sched_T1/T2/T3 ：通过不同组合观察“降级、升级、轮转”的效果：
    - T1：两到三个任务竞争，同级轮转与时间片缩短（高优先级短片）可观察。
    - T2：加入交互型（频繁 yield ），其不降级或较少降级；CPU 密集型逐步下沉。
    - T3：长时间运行，低优先级的进程在等待后通过 aging 拉回高队列，体现防饥饿。
  - 典型日志串联（示意）：
    - mlfq_demote pid=1 -> prio=9
    - sched_pick pid=4 prio=10 eff=10 slice=0/2 ticks=106
    - aging_promote pid=1 -> prio=10
    - mlfq_demote pid=4 -> prio=9
    - sched_pick pid=5 prio=10 eff=10 slice=0/2 ticks=114
    - …重复出现的“用满片降级”、“等待升级”、“同级轮转”，说明 MLFQ 工作正常。
  - 日志节流： sched_pick 仅在 pid 改变或每 5 次选择打印，减少噪声，同时保留 mlfq_demote 与 aging_promote 事件行。
- 用户态命令
  
  - nice.c ： nice pid priority ，用于动态设置任意进程优先级（示例： nice 5 8 、 nice 6 2 ）。
  - ps.c ：调用 procdump() 打印 PID PRIORITY STATE TICKS ，用于观察当前调度状态（示例输出中的 5 8 RUNNING 50 、 6 2 SLEEPING 10 ）。
一个完整的执行过程示例

- 初始： pid=5 （交互型）， pid=6 （I/O 或睡眠）， pid=4 （CPU 密集）。
- pid=4 在 RUNNING ，时钟中断使其 slice_ticks 累加到 slice_len ， proc_on_tick ： priority-- ， need_resched=1 ，打印 mlfq_demote 。
- 安全点（ burn_cycles ）触发 preempt_check ，当前进程让出并进入 scheduler 。
- scheduler 在高优先级队列选择 pid=5 （交互型），打印一次 sched_pick （精简版）。
- pid=5 因频繁 yield ， slice_ticks 不会达到 slice_len ，保持高优先级不降级。
- pid=6 在 RUNNABLE 等待时长累积到 AGING_INTERVAL ，被提升优先级，打印 aging_promote ，随后在高队列参与选择。
- 重复循环，观察到 CPU 密集型下沉、等待者上升、同级轮转。