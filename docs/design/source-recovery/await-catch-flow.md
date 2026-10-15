# 原始 async catch-input 可观察性证明（Round 78）

基线 `4c6e1115`。原始 resume 协议在 rejection 前提交 result/mode，原生 await
抛出时不提交这些寄存器定义。只有这两个差异定义在用户 handler 入口之后
不可观察，才能归一化。该证明不需要新的源语言前提，不授权 IteratorClose、
catch/finally 简化或 generator/yield 的准入变化。

## 输入、图与证明

`await_catch_flow` 消费不可变原始 MethodBody、profile、frame、重新核验的
AwaitRegion，以及 async owner 已证明的 completion-handler 身份。每条原始
指令有 Before / Commit / Catch / CallerExit 身份；已认证的恢复读取另标为
Resume。正常跳入 handler 的 Before 不经过 Catch。异常分派沿 pinned
first-matching-try 规则从 Before 出发，绕过 Commit；正常寄存器写只在 Commit
生效。Catch 只改 acc，保留被查询的局部寄存器。Suspend 的恢复后继必须有
AwaitRegion 所有权，未认证 coroutine 是 unknown，不能作 no-op 或出口。

后续协议的 result/mode 读取并未被全局忽略：它们仍是实际读取，前面的真实
Commit 自然杀死旧定义。对保留 continuation 的外部入口也沿真实原始边处理。

证明是从每个 (handler-entry, register) 查询出发的稀疏闭包。每个状态在以下
边界停止：实际读取、未知效果、正常覆盖或 caller exit。其余状态必须覆盖
所有正常、异常和恢复后继。反向有限摘要传播 read / unknown / cover / terminal
的真实原始 site；只传播从无到有，不在环内不断改善 witness。纯闭环可以
保持 unobserved：这里不要求终止。读优先于写，抛出前状态不能由正常写覆盖。
cover/terminal PC 是可达边界的 witness，并非唯一覆盖点或所有路径的清单；
完整闭包才是所有路径的证书。

独立 verifier 重新提升原始语义、重建 dispatch 和协议所有权，从独立查询根
核对证书闭包：每条后继必须存在，没有多余/重复状态，所有边界由不可变输入
重新导出，最后重算摘要和查询身份。不信任 builder 的 outcome 或 consumed 标记。

## 资源与复杂度

设 N 为输入指令、try/handler、await 及显式边的总规模。稀疏状态可能形成
寄存器 x 图节点的稠密乘积，因此不声称无界的 O(N log N) 活跃性算法。
本实现有线性状态 admission（每个事件至多八个状态的总额度）与
O(N log N) 的工作额度，超出返回 typed resource/work refusal，绝不返回 dead。
这不是语义或图形状门禁；更密集的合法方法仍是已记录的资源覆盖缺口。

节点、稀疏索引、闭包、反向边、队列及其增长都经过预付 PMR 资源。返回的
证书和 query payload 在复制前计费；验证期间扣除仍存活的证书。upstream
解析的存活数组保守保留计费，没有在其释放前退账。共享诊断的实现控制块
通过检查真实 allocate_shared 分配请求的 allocator 预付，不能猜测 libc++ 大小。

## 接入和漂移规则

先运行原快速证明。成功的方法保持旧路径和旧规范化结果。只有原检查得到
kUnprovedPaths 时，重新验证原始流证书并补足该查询；原检查的 kRead 不放宽。
所有查询仍由快速扫描逐一检查，不能只检查最先失败的一项。规范化后的 body、
原始 PC 映射与异常 SSA 只在证明成功之后创建。

失败的 supplementary proof 不授权变换；原拒绝继续保留，附上原始流查询或
分析失败。每项诊断含 await、差异定义、handler、register、role 和真实的
read/unknown/cover/terminal 原始 PC，不填 working PC。
`close_projection_authorized=false` 是所有此类报告的范围声明。

## 三个方法

| 方法 | 查询 | 覆盖 | 覆盖及终止 | 原始流证书状态数 | 证明额外 peak bytes |
|---|---:|---:|---:|---:|---:|
| o9/7596 m7950 | 28 | 18 | 10 | 325 | 557485 |
| o32/23058 m23306 | 2 | 0 | 2 | 62 | 39082 |
| o39/21472 m22136 | 2 | 1 | 1 | 63 | 76427 |

这里 peak 是该分析的预算计量，不是进程 RSS。三个方法均无 read/unknown 查询。
o9 与 o39 complete；o32 保留 effectful close 的下一层拒绝：
PhasedMethodError::kControl > ExceptionControlError::kMultipleExits，m23306，
original body149 / handler174，working body125 / handler150，TryId0。

## 测试边界

单测覆盖分支分别覆盖、异常边保留写前状态、回到保留 continuation 的真实
读取、同指令读写、未知 raw resume、无读不终止环、catch payload 与其它局部、
复制变体、错误查询/定义/边界、删除/重复闭包状态、错误分支/异常边，以及
200/201/400/800 的线性增长和精确 byte 边界。多寄存器稠密传播必须资源拒绝。
JSON 测试覆盖两条 manifest 路径、receipt round-trip、PC0 与缺失 PC；
原有 enum 值、资源分类不变。便携 es2abc fixture 与 Node 的边界见
[fixture README](../../../tests/integration/fixtures/await_catch_flow/README.md)。

完整验收结果和 cohort 比较见本轮交付；探索输入不是生产算法的条件。

## 本轮实际验收（未提交）

- 最终完整 `cmake --build build/regression -j 6 -- -k` 成功。
- 固定集 `ctest --preset source-regression -j 1`：65/65。
- 新增及直接相关：5/5；包括更新后的 concurrent finally-await 完成、通知、
  重编译及 Node 微任务差分。该 finally 不抛出，不声称 finally-throw 的源码 oracle。
- `await|async|exception|phased` 定向：81/84。仅已登记的 phased_literal_test、
  phased_arm_selection_source_test、async_semantics_test 失败，没有改动其断言。
- `cli|corpus|failure|_receipt|manifest` 扫描：74/75；仅已登记的 family_source_cli_test。
- 对 explore77c/explore77cs：524→526/620、570→572/620。两组仅 o9/7596、
  o39/21472 complete；o32/23058 仅首拒绝前移。0 回退、0 已完成文本漂移，
  其余 manifest 记录无变化；四份新完成输出通过 `node --check`。
- 稀疏闭包、异常边、协议、verifier 与资源边界的独立静态审查未发现阻断；
  审查者没有构建或跑测试。没有运行留出集、全套测试或 RSS 基准。

详细比较、全部32项查询、命令结果和四份新源码路径保存在
`/private/tmp/abcd-round78-delivery/acceptance.json`。
