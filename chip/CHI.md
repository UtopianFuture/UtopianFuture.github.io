# CHI Architecture

## B Specification

### B5 Interconnect Protocol Flows

#### B5.4 Atomix transaction flows

##### B5.4.1 Atomic transactions with data return

![image-20260420173837064](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260420173837064.png)

> 1. RN-F0 sends an Atomic transaction to HN-F.
> 2. After receiving the Atomic request, HN-F:
> • Sends DBIDResp to RN-F0 to obtain the Atomic transaction data.
> • Sends SnpUnique Snoop request to other RN-F nodes after determining that snoops are required.
> • HN-F is permitted, but not required, to send a speculative ReadNoSnp to SN-F.
> 3. RN-F2 has the cache line in UD state and responds by sending data and invalidating its own cached copy.
> • The response is SnpRespData_I_PD.
> • This data is marked as (InitialData) in Figure B5.18 to be distinguishable from the data sent by the Requester
> and the data written to SN-F after the atomic operation is executed.
> • HN-F also receives a second Snoop response, SnpResp_I, from RN-F1.
> 4. After receiving all Snoop responses, HN-F sends CompData_I to the Requester.
> • The data sent with Comp is the initial copy of the data.
> • This data must not be cached in a coherent state at RN-F0.
> 5. In response to the DBIDResp sent previously, HN-F receives the NonCopyBackWriteData_I response from
> the Requester.
> • This data is marked as (TxnData) in Figure B5.18 to be distinguishable from the data sent by RN-F2 in
> response to the Snoop request from HN-F.
> 6. Once HN-F receives the NonCopyBackWriteData_I response from the Requester, and the Snoop response
> with data from RN-F2, the atomic operation is executed.
> • The resulting value after atomic operation execution, marked as (NewData) in Figure B5.18, is written to
> SN-F.
> 7. In this example, the read data received due to the speculative read is discarded by HN-F.

步骤 1：**RN-F0 向 HN-F 发送原子事务**

→ 发起原子操作的 CPU 喊：“我要对这个地址做原子操作！”，请求发给一致性管家 HN-F。

步骤 2：**HN-F 收到原子请求后，做 3 件事**

1. 发 **DBIDResp** 给 RN-F0：**要原子操作的操作数**（比如 “加几”“比较值是多少”）。
2. 判断需要一致性窥探，发 **SnpUnique** 给其他 RN-F：**逼其他 CPU 交出这个地址的最新数据，并失效自己的缓存**（保证没人再持有有效副本）。
3. **可选优化**：给 SN-F 发预读请求（speculative ReadNoSnp），提前读内存备用，**不是必须做**。

步骤 3：**其他 CPU 回复窥探请求**

- **RN-F2**：手里有这个缓存行，且是**UD 状态（独占脏，最新数据在它这）**，所以回复 **SnpRespData_I_PD**：**交出最新数据 + 自己失效缓存**。这个数据就是**InitialData（旧原始值）**。
- **RN-F1**：手里没数据，只回复 **SnpResp_I**：单纯失效缓存，不交数据。

步骤 4：**HN-F 收完所有窥探响应，给发起方回复 CompData_I**

1. 把 **InitialData（旧值）** 发给 RN-F0。
2. **硬性要求**：RN-F0 **绝对不能缓存这个旧值**（避免破坏一致性）。

步骤 5：**RN-F0 回复 HN-F 的 DBIDResp**

→ 给 HN-F 发 **NonCopyBackWriteData_I**，把**原子操作的操作数**发过去，这个数据就是**TxnData**。

步骤 6：**HN-F 凑齐数据，执行原子操作**

1. 凑齐两个必备数据：

   - InitialData（旧值，来自 RN-F2）
   - TxnData（操作数，来自 RN-F0）

   

2. **HN-F 硬件直接执行原子计算**（比如 旧值 + 操作数 = 新值）。

3. 把计算结果 **NewData** 写入 **SN-F（内存）**。

步骤 7：**丢弃预读的内存数据**

→ 之前 HN-F 预读内存得到的数据，因为已经有了 RN-F2 的**最新缓存旧值**，所以直接丢掉，不用。

总结下来就是：**由 HN-F 全权负责一致性窥探 + 硬件执行原子计算 + 写回内存，CPU 只发请求和操作数，全程总线级原子、无软件参与、无竞态**。

1. **原子操作不在 CPU 执行，*在 HN-F 执行***，保证多核一致性；
2. 先清空其他 CPU 缓存，确保用**最新旧值**计算；
3. 内存只接收最终新值，**不接收原子请求**；
4. 预读内存是优化，有最新缓存数据就丢弃；

如果 SOC 内没有集成 CMN，那么靠**SCU（簇内）+CCI（跨簇）+Monitor（无相干）**，覆盖手机、嵌入式、车规等绝大多数 SOC

原子原理完全一样：**先拿最新值→硬件原子计算→写回**，只是执行硬件从 HN‑F 换成 SCU / 内存控制器

### B6 Exclusive accesses

| Term                        | Description of Exclusive terms                               |
| --------------------------- | ------------------------------------------------------------ |
| Exclusive Load              | The action of an LP executing an appropriate program instruction, such<br/>as LDREX, requires:<br/>– Obtaining the data from the location to which the LP wants to<br/>perform an exclusive sequence.<br/>– Indicating that the LP is starting an exclusive sequence. |
| Exclusive Load transaction  | A transaction issued on the interface to obtain data for an Exclusive<br/>Load, if the data is not available in the cache at the LP. Not every<br/>Exclusive Load requires an Exclusive Load transaction. |
| Exclusive Store             | The action of an LP executing an appropriate program instruction, such<br/>as STREX, requires:<br/>– Determining if the exclusive sequence has passed or failed.<br/>– If appropriate, updating the data at the location.<br/>Can pass or fail and this result is known to the executing processor.<br/>When an Exclusive Store passes, the data value at the address location is<br/>updated. When an Exclusive Store fails, this indicates that the data value<br/>at the address location has not been updated, and the exclusive sequence<br/>**must be restarted**. |
| Exclusive Store transaction | A transaction issued on the interface that could be required to complete<br/>an Exclusive Store. Not every Exclusive Store requires an Exclusive<br/>Store transaction. An Exclusive Store transaction can pass or fail and<br/>this result is made known to the LP using the transaction response. |

