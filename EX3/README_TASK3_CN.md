# 任务三：基于 Linux 消息队列的线程间通信（msgqueue_threads）

## 1. 任务目标
使用 Linux POSIX 消息队列机制，实现两个线程之间的通信。

在基础功能之上，本实验实现了增强版创新：
1. 消息优先级
2. 双队列请求-响应模型
3. 超时接收机制
4. 通信统计报告

对应源码文件：msgqueue_threads.c

---

## 2. 原理说明
POSIX 消息队列是内核对象，通信双方通过 `mq_send` 和 `mq_receive/mq_timedreceive` 按“消息”维度传递数据。

与管道（字节流）相比，消息队列具备：
1. 消息边界天然保留
2. 支持消息优先级
3. 支持阻塞/非阻塞及超时接收

本实验使用两个消息队列：
1. 请求队列（request queue）：发送线程 -> 接收线程
2. 响应队列（response queue）：接收线程 -> 发送线程

构成完整的请求-响应通信流程。

---

## 3. 程序结构与流程

### 3.1 线程划分
1. sender 线程
- 构造并发送请求消息（带优先级）
- 从响应队列接收 ACK
- 计算并打印 RTT（往返时延）

2. receiver 线程
- 使用 `mq_timedreceive` 从请求队列带超时读取
- 收到请求后生成响应并回发
- 收到退出控制消息后结束

### 3.2 消息格式
消息使用简易文本协议：

`id|timestamp_ns|payload`

示例：
`3|1234567890123|task_3`

响应示例：
`3|1234567890123|ack_task_3`

### 3.3 优先级策略
发送线程为不同请求设置不同优先级（0/1/2），用于演示高优先级消息先被接收。

### 3.4 超时机制
receiver 使用 `mq_timedreceive`，若 1 秒内未收到请求，记录一次 timeout 并打印提示。

### 3.5 统计指标
程序结束时输出：
1. requests sent
2. requests received
3. responses sent
4. responses received
5. receive timeouts
6. average RTT

---

## 4. 关键接口
1. `mq_open`：创建/打开消息队列
2. `mq_send`：发送消息（可指定优先级）
3. `mq_receive`：接收消息
4. `mq_timedreceive`：超时接收
5. `mq_close`：关闭队列描述符
6. `mq_unlink`：删除消息队列对象
7. `pthread_create / pthread_join`：线程创建与回收

---

## 5. 编译与运行
在项目目录执行：

```bash
cd /home/david/Desktop/OS_Experiment/EX3
make msgqueue_threads
./msgqueue_threads
```

---

## 6. 示例输出（节选）
不同机器下顺序和耗时会略有差异，典型输出如下：

```text
receiver timeout: no request within 1s
sender -> request id=1 prio=0 payload=task_1
receiver <- request id=1 prio=0 payload=task_1
receiver -> response id=1 prio=0 payload=ack_task_1
...
sender <- response id=3 prio=2 payload=ack_task_3 rtt=604.48 ms
...
=== statistics ===
requests sent:      5
requests received:  5
responses sent:     5
responses received: 5
receive timeouts:   1
average RTT:        604.43 ms
```

---

## 7. 实验亮点（创新点）
1. 从单向发送扩展为双向请求-响应模型，更接近真实系统。
2. 引入优先级消息，展示消息队列区别于普通队列/管道的能力。
3. 使用超时接收避免永久阻塞，提高程序健壮性。
4. 增加延迟统计与汇总报告，体现工程化思维。

---

## 8. 注意事项
1. 队列名称需以 `/` 开头（POSIX 要求）。
2. 程序结束要 `mq_close + mq_unlink`，防止资源残留影响下次运行。
3. `mq_msgsize` 必须不小于实际发送消息长度。

---

## 9. 验收总结（可直接口述）
本实验使用 POSIX 消息队列实现了两个线程间通信，并在基础版上扩展了优先级调度、双向请求-响应、超时接收和统计分析，完整展示了 Linux 消息队列在并发通信场景中的工程化应用。

---

## 10. 详细验收细节

### 10.1 验收前检查清单
1. 可独立编译：`make msgqueue_threads`。
2. 运行命令：`./msgqueue_threads`。
3. 执行结束后消息队列对象被 unlink，不影响下次运行。

### 10.2 现场验收步骤（建议顺序）
1. 运行程序，先展示超时接收日志（`receiver timeout`）。
2. 展示 sender 连续发送请求，带优先级字段（`prio=0/1/2`）。
3. 展示 receiver 收到请求并返回响应（`ack_xxx`）。
4. 展示 sender 收到响应和 RTT。
5. 展示最终统计区块（sent/received/timeout/average RTT）。

### 10.3 预期验收现象
1. 请求与响应总数一致（例如 5 对 5）。
2. 高优先级消息在接收顺序上体现优先行为。
3. 程序在无消息时不会卡死，能出现 timeout 提示并继续运行。
4. 程序结束后打印统计结果，且平均 RTT 为合理非负值。

### 10.4 通过判据（建议口述）
1. 双队列模型清晰：请求队列与响应队列职责分离。
2. 使用 `mq_timedreceive` 实现超时控制，而非永久阻塞。
3. 统计结果与实际日志数量一致。

### 10.5 常见扣分点
1. 只做单向队列收发，未实现请求-响应。
2. 没有优先级或优先级字段形同虚设。
3. 无超时处理，线程可能永久阻塞。
4. 忘记 `mq_unlink`，二次运行可能因残留对象异常。
