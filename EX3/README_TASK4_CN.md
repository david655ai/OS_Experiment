# 任务四：基于 Linux 共享内存的进程间通信（shm_processes）

## 1. 任务目标
使用 Linux 共享内存机制实现两个进程间通信。

在基础单向通信之上，本实验实现了增强版创新：
1. 双向请求-响应模型
2. 双信号量握手同步
3. 超时等待（避免永久阻塞）
4. 统计报告（RTT 与超时）
5. 统一资源清理

对应源码文件：shm_processes.c

---

## 2. 原理说明
共享内存（Shared Memory）是高性能 IPC 方式。两个进程将同一块内存映射到各自地址空间后，可直接读写同一数据区。

特点：
1. 通信速度快（减少数据拷贝）
2. 需要额外同步机制保证时序一致性

因此本实验配合 POSIX 命名信号量使用：
1. `req_sem`：通知“请求已写入”
2. `resp_sem`：通知“响应已写入”

这样可以保证：
1. 父进程写请求后子进程再读
2. 子进程写响应后父进程再读

---

## 3. 程序结构与流程

### 3.1 共享数据结构
共享内存区维护一个结构体，包含：
1. 请求编号 `req_id`
2. 发送时间戳 `req_ts_ns`
3. 退出标志 `shutdown`
4. 请求负载 `req_payload`
5. 响应负载 `resp_payload`

### 3.2 父进程流程
1. 写入请求（id + payload + timestamp）
2. `sem_post(req_sem)` 通知子进程处理
3. `sem_timedwait(resp_sem)` 等待响应
4. 计算 RTT，统计成功/超时
5. 发送若干请求后置 `shutdown=1` 通知子进程退出

### 3.3 子进程流程
1. `sem_timedwait(req_sem)` 等待请求
2. 读取共享区请求并处理
3. 写入响应到共享区
4. `sem_post(resp_sem)` 通知父进程
5. 收到 `shutdown` 标志后退出

---

## 4. 创新点说明
1. **双向通信**：由单向“父写子读”升级为请求-响应。
2. **超时机制**：使用 `sem_timedwait`，防止进程永久阻塞。
3. **性能统计**：输出平均 RTT，便于评估共享内存通信效率。
4. **工程化清理**：统一清理函数处理 `sem_close/sem_unlink/shm_unlink/munmap`。

---

## 5. 关键接口
1. `shm_open`：创建共享内存对象
2. `ftruncate`：设置共享内存大小
3. `mmap`：映射共享内存到进程地址空间
4. `sem_open`：创建命名信号量
5. `sem_post` / `sem_timedwait`：同步握手
6. `waitpid`：父进程回收子进程
7. `munmap` / `shm_unlink` / `sem_unlink`：资源释放

---

## 6. 编译与运行
在项目目录执行：

```bash
cd /home/david/Desktop/OS_Experiment/EX3
make shm_processes
./shm_processes
```

---

## 7. 示例输出（节选）
不同机器下耗时会有差异，典型输出如下：

```text
parent -> request id=1 payload=task_1
child <- request id=1 payload=task_1
parent <- response id=1 payload=ack_task_1_by_child rtt=0.53 ms
...
child processed total: 5

=== statistics ===
requests sent:      5
responses received: 5
response timeouts:  0
average RTT:        0.32 ms
```

---

## 8. 注意事项
1. 共享内存只解决“数据可见”，不解决“访问顺序”，必须配合同步。
2. 命名信号量和共享内存对象运行后要 unlink，避免资源残留。
3. 发生异常时也应尽量执行统一清理逻辑。

---

## 9. 验收总结（可直接口述）
本实验使用 POSIX 共享内存与命名信号量实现了两个进程间的双向请求-响应通信，并通过超时控制与统计报告提升了程序健壮性与工程化水平，完整体现了共享内存 IPC 的高性能与同步控制要点。

---

## 10. 详细验收细节

### 10.1 验收前检查清单
1. 可独立编译：`make shm_processes`。
2. 可直接运行：`./shm_processes`。
3. 运行前后无共享内存/信号量残留（可重复运行验证）。

### 10.2 现场验收步骤（建议顺序）
1. 启动程序，观察父进程发送请求日志（`parent -> request`）。
2. 观察子进程读取请求并写回响应（`child <- request`）。
3. 观察父进程收到响应并打印 RTT（`parent <- response ... rtt=...`）。
4. 观察子进程处理总数（`child processed total`）。
5. 观察统计区块（请求数、响应数、超时数、平均 RTT）。

### 10.3 预期验收现象
1. 请求和响应一一对应，不丢失。
2. 父子进程通信顺序稳定，无明显竞态输出异常。
3. 超时机制可正常工作（正常场景可为 0 次超时）。
4. 程序结束后可再次运行，不出现“对象已存在”类错误。

### 10.4 通过判据（建议口述）
1. 双向共享内存请求-响应流程完整。
2. 同步机制明确：`req_sem` 与 `resp_sem` 握手控制时序。
3. 使用 `sem_timedwait` 体现超时健壮性设计。
4. 清理逻辑完备：`sem_close/sem_unlink/shm_unlink/munmap`。

### 10.5 常见扣分点
1. 仅共享内存读写，没有同步，出现竞态或脏读。
2. 仍是单向通信，未体现请求-响应。
3. 无超时控制，异常时永久阻塞。
4. 忘记 unlink，导致后续运行失败或资源泄露。
