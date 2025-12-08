文件系统实现总结（kernel）

**总体设计**
- 文件系统以“块”为基本单位，常量定义在 `kernel/fs.h:6-9`（块大小、超级块号、日志区范围）。
- 元数据采用 `MYFS` 结构：`superblock` 描述整体布局与容量，`inode` 描述单个文件的寻址与属性（`kernel/fs.h:24-42`、`kernel/fs.h:44-60`）。
- 读写路径通过块缓存层统一进入设备：缓存命中/替换、写回与统计均由 `bcache` 管理（`kernel/bcache.c`）。
- 元数据一致性采用简化的“写前日志”（write-ahead logging），接口与流程在 `kernel/log.h` 与 `kernel/log.c` 中实现。
- 目录与路径解析在 `kernel/dir.c` 中实现，基于变长目录项格式与线性扫描的简化方案。

**关键数据结构**
- `superblock`：记录魔数、版本、容量、各区域起始与大小、根 inode 号等（`kernel/fs.h:24-42`）。挂载时读取于 `kernel/dir.c:8-17`。
- `inode`：支持 12 个直接块、1 级与 2 级间接块、内联数据等字段（`kernel/fs.h:44-60`）。当前项目尚未实现全功能的 inode 缓存与分配器。
- `dirent`：变长目录项，字段为 `(ino, type, name_len, name[])`（`kernel/fs.h:62-67`），便于减少空洞，提高目录密度。

**块缓存与设备 I/O**
- 初始化与资源：`bcache_init` 建立固定大小缓冲池、哈希桶与 LRU 双向链（`kernel/bcache.c:96-118`）。
- 访问流程：
  - 命中路径：`get_block` 哈希查找命中后提升至 MRU，更新计数（`kernel/bcache.c:131-144`）。
  - 替换路径：选择 LRU 尾部可替换块，必要时写回旧块，再加载新块并放入 MRU（`kernel/bcache.c:146-196`）。
  - 释放与老化：`put_block` 降至 LRU 尾部，引用计数递减（`kernel/bcache.c:198-210`）。
  - 同步与刷新：`sync_block` 与 `flush_all_blocks` 负责写回（`kernel/bcache.c:212-244`）。
- 统计信息：`buffer_cache_hits/misses`、`disk_read_count/write_count`（`kernel/bcache.c:17-20`）。
- 设备桩：`block_read/block_write` 当前为桩实现，未接入真实块设备（`kernel/bcache.c:23-34`）。这使得数据读入为零填充、写入直接成功，适合逻辑验证但不具备持久化。

**写前日志（WAL）**
- 状态与头部：`log_state` 管理日志区起始、大小、事务计数、记录的目标块集合；`log_header` 存于日志区首块，记录 n 与目标块号数组（`kernel/log.h:14-31`）。
- 初始化与恢复：`log_init` 从超级块参数初始化并调用 `recover_log` 进行幂等恢复（`kernel/log.c:95-107`、`kernel/log.c:189-222`）。
- 事务接口：
  - `begin_transaction`/`end_transaction` 控制事务生命周期与串行提交（`kernel/log.c:109-164`）。
  - `log_block_write` 记录即将写入的目标块号，去重并受日志容量约束（`kernel/log.c:166-187`）。
- 提交流程：
  - 将“家块”的最新内容复制到日志区数据块（`write_log_data`，`kernel/log.c:34-61`）。
  - 写入日志头以标记提交（`write_log_header`，`kernel/log.c:11-25`）。
  - 将日志区数据安装回目标块（`install_trans`，`kernel/log.c:64-93`）。
  - 清空日志头并重置内部集合（`kernel/log.c:27-31`、`kernel/log.c:133-137`）。
- 测试用例：`test_crash_recovery` 人工构造日志头与数据，验证恢复的幂等性（`kernel/start.c:375-495`）。

**目录与路径解析**
- 读取超级块与根 inode：`read_superblock`、`load_inode_by_inum`、`get_root_inode`（`kernel/dir.c:8-17`、`kernel/dir.c:19-36`、`kernel/dir.c:38-56`）。
- 目录项扫描：`dirent_next` 从块内指定偏移读取下一项并推进（`kernel/dir.c:61-85`）。
- 查找与修改：
  - `dir_lookup` 在线性扫描直接块中匹配名称，返回匹配项位置（当前占位，尚未返回 inode 指针）（`kernel/dir.c:87-113`）。
  - `dir_link` 在目录末尾追加一项（简化：仅直接块，未集成块分配）（`kernel/dir.c:115-149`）。
  - `dir_unlink` 通过零化 ino 标记删除（`kernel/dir.c:151-178`）。
- 路径解析：`path_walk` 逐段从根下降，忽略 `.`/`..` 的语义细节（`kernel/dir.c:180-204`）；`path_parent` 返回父目录及最后一段名称（`kernel/dir.c:206-235`）。
- 调试接口：`debug_filesystem_state` 输出超级块与缓存统计；`debug_disk_io` 输出 I/O 计数（`kernel/dir.c:237-250`、`kernel/dir.c:259-263`）。

**文件接口（用于内核测试）**
- 提供最小的 `open/write/read/close/unlink` 包装，当前以一个内存文件 `fake_file_t` 满足测试用例：
  - `open` 支持 `O_CREATE` 与只读/读写打开逻辑（`kernel/fs.c:17-34`）。
  - `write` 维护当前偏移并更新大小（`kernel/fs.c:36-47`）。
  - `read` 按剩余长度读取（`kernel/fs.c:49-58`）。
  - `close`/`unlink` 重置状态与删除（`kernel/fs.c:60-76`）。
- 该层尚未与 `MYFS` 的 `inode`/目录/日志打通，主要用于接口验证与集成测试演示。

**测试与集成**
- 启动时初始化块缓存并以“内核线程”运行综合测试（`kernel/start.c:562-567`）。
- 测试内容包括：
  - 文件系统状态与 I/O 计数输出：`debug_filesystem_state`、`debug_disk_io`（`kernel/start.c:535-536`）。
  - 接口完整性：创建/写入/读取/删除内存文件（`test_filesystem_integrity`，`kernel/start.c:322-343`）。
  - 并发访问：多任务同时读写不同块，验证锁与计数（`test_concurrent_access`，`kernel/start.c:362-373`）。
  - 崩溃恢复：日志头与数据回放验证（`test_crash_recovery`，`kernel/start.c:375-495`）。
  - 性能模拟：顺序/小块写入的周期统计（`test_filesystem_performance`，`kernel/start.c:497-527`）。

**当前局限与下一步**
- 设备 I/O 为桩实现，缺乏真实持久化与错误路径；需实现块设备驱动并替换 `block_read/block_write`（`kernel/bcache.c:23-34`）。
- 未实现块与 inode 分配器、位图维护与 icache；`dir_link` 等依赖分配器的功能暂不可用。
- `dir_lookup` 仍为占位（未返回实际 inode 指针），路径解析功能有限，未处理 `.`/`..`/符号链接等细节。
- 文件接口层未与 `MYFS` 打通；需设计 `fd→inode` 映射、页缓存与写回策略、与日志的元数据事务边界。
  - 实现块设备驱动与格式化工具，完成超级块与基本布局写入。
  - 完成位图分配器与 inode/数据块生命周期管理；引入 icache。
  - 将目录/文件操作改为真正的 inode 读写，并用 WAL 包裹元数据更新。
  - 扩展目录查找为哈希/有序结构以提速；完善并发控制与缓存策略。
