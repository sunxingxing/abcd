# 原始 completion 关系（Round 79 / A2）

这是独立分析，没有生产接入、topology ownership 或源码准入。
`AnalyzeCompletionRelation` 和 `VerifyCompletionRelation` 消费同一方法的不可变
原始 body、ExceptionFlow、ExceptionSsa，以 A1 的 RawEventGraph 构图。
本增量只处理同步输入。

## 边界与结果

入口是原始异常图的边及一个显式 PendingCompletion 上下文：
`{Normal | Break | Continue | Return | Throw, payload, target}`。
同一入口边可有多个上下文；整个元组随上下文身份传递，不分别合并三个字段。
Kind/target 的语义来源仍是调用方的分析前提；A2 **不证明**它们是合法的 close
入口。后续候选/ownership 证明必须独立建立这些前提。

Payload 使用原始 SSA value、带类型的 entry payload 或原始 exception-exit
value；空 Return payload 表示 undefined。Target 是原始 ExceptionNodeId 或方法
caller 边界。可选 binding 与 SSA payload 必须在入口源边可用：用原始异常 CFG
上的 LT 支配树复核；不能提前绑定目标 phi 或新 Catch 定义。

出口是原始边的 cut。普通 cut 保留 pending 上下文；异常 cut 取真实的显式
throw payload 或该操作的 exception-exit value，目标为真实 Catch/caller；
return port 从原始 return 输入取值。内部 Catch 保存异常到 SSA，不自行改写
pending；后续展开代码决定最终选择哪个 payload。因此内部 close 错误可以被
保存但不压过原始 Throw；也可以在 Return 路径上成为逃逸的当前 Throw。

SSA copy 和 phi 通过原始 predecessor + successor ordinal 传递；同一节点的
phi 从同一旧环境读取，随后一起提交。动态异常定义只在 Catch 提交，正常定义
只在 Commit 提交。不同动态执行的同一 SSA site 仍是同一个静态 origin；这些
标签不是跨激活的值相等证明。

结果是保守的 **may 关系**。只计算已知原始标量常量的抽象 truth/equality；未知
谓词保留两条边，所有原始可抛出边保留，包括资源异常。不执行 getter、coercion
或任何用户代码。未知 effect、缺失 caller cut、无效身份或耗尽使整个查询拒绝，
不会返回部分关系。无出口环可以得到空 leaving；这不是终止、无效果或控制删除
证明。没有假定 native IteratorClose 的优先级，也没有借用源语言迭代前提。

## 有界实现与验证

使用显式工作队列、按完整上下文索引的状态集和持久化 SSA 环境。环境为 32 位
SSA ID trie：一次更新最多复制 33 个节点；ordered interning 让相等环境具有相同
身份，循环可以达到固定点。所有节点、map、队列及增长重叠都走预付 PMR arena。
输出容器先计费再分配。额外 LT solver 在剩余独占 grant 内预付；保守保留它的
完整 peak，不提前返还仍存活 CSR/tree/scratch 的费用。

稀疏状态最多为输入项数的 8 倍，并可由调用方降低。实际 transfer、trie 访问与
有序表比较计入 work；另有按输入 `N log N` 缩放的 work grant。稠密上下文或 phi
传播超过任一 grant 返回 ResourceLimit/WorkLimit，从不解释为 dead。对于准入的
工作，时间不超过 O(N log N)，空间受既有字节 grant 约束；N 包括原始节点/边、
SSA 定义/phi arms 和边界上下文。没有修改 record/process cap。

Verifier 从原始输入重建图与索引，按相反工作队列顺序重算整个关系，核对全部
arrival/leaving 元组，不读取 producer 的环境、状态或 coverage。它共享原始标量
transfer 定义，不是另一套独立语义实现；fixture 的完整出口 oracle 另外覆盖这层。
Telemetry 不是证据，verifier 返回自己的 cost。

## 验收边界

- 合成图：Return/Throw 经 phi 的关联、同时 phi swap 的循环、Break/Continue
  目标、内部 close Catch 的异常值流、嵌套当前 completion、entry payload binding、
  register/copy/block-split 等价变体、无出口循环、非法目标 phi binding。
- 变异：交换 payload/target、删除 arrival、丢失出口；独立重算必须拒绝。
- 200/201/400/800：宽入口、深 SSA copy、稠密上下文整体拒绝、精确 bytes/work/state
  边界，不提高任何 cap。
- 真实 es2abc `iterator_protocol/main.abc`：m423 的 break（87..158）与 return
  （178..249）、m451 的原始 throw（76..134）。完整预期分别为 7/7/2 个原始
  may 出口元组。7 项中包括未证实 Hole 比较的保守额外路径；不能称作这些
  IteratorClose 已获完整证书。fixture 的编译身份与 hash 见其 README。

后续 A3 才把此关系与实际 close-entry/guard/return/ownership 义务结合。
本增量没有接入 PhasedBody，不改变任何输出或首因。
