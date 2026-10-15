# Round 69：async 归一化的拒绝证据

基线 `40f0d0c1`。本轮只完成 evidence-first；未改变恢复准入、async
协议、catch staging、声明或输出。没有新源语言前提。

## 四个首拒绝

两种 flags 的四例相同。下表 PC 均为原始 PC；归一化尚未成功，故不发布
working PC。父函数 creation PC 保留在独立字段，不能代替子方法失败位置。

| cohort / record | 方法 | 原始 PC | 类型化原因 | catch-input 证据 |
|---|---:|---:|---|---|
| o9 / 7596 RdbDataPersistence | 7950 | 3459 | FunctionEmitError::kKind → AsyncCompletionCheck::kCatchInput → AwaitCatchError::kObservedRegister | H3537，v25；kUnprovedPaths；suffix known / confined |
| o32 / 23058 event | 23306 | 99 | 同上 | H131，v13；kUnprovedPaths；suffix known，但不 confined |
| o32 / 58223 service | 58418 | 18 | FunctionEmitError::kKind → AsyncCompletionCheck::kPreludeEffects | definefunc m59227 发生在 ASYNCFUNCTIONENTER PC25 之前 |
| o39 / 21472 WebSaveImage | 22136 | 75 | FunctionEmitError::kKind → AsyncCompletionCheck::kCatchInput → AwaitCatchError::kObservedRegister | H348，v11；kUnprovedPaths；suffix known / confined |

`kObservedRegister` 是保留的历史枚举，不等于这三例已经证明存在实际泄漏。
新增 prefix 子证据区分 `kRead` 与 `kUnprovedPaths`：这里全部是后者。

## 为什么没有做局部放宽

`ProveAwaitCatchInputs` 证明 await 的 VM result/mode 暂存定义在 rejection
进入用户 handler 后不可观察。原生 await 抛出不会提交这些定义，VM 的恢复
路径会先提交再 throw；删掉这个检查会改变 catch 观察值。

现有证明以直线前缀的覆盖写入/退出，以及包含全部 branch/exception 目标且
完全不读取该寄存器的 suffix 为充分条件。m7950 的 handler 在覆盖相应
暂存前经过 getter/call 的异常边 H3688，之后还有 await；m22136 的 H348
在 v11 覆盖前有可抛出的 global read，异常到 H428。m23306 的 H131 内
有条件路径、return getter/call 的 H174 和回到 PC44 的边；suffix 不封闭。
因此不能把“后面能看到 store”当作所有路径都先覆盖的证据。

后续应单独实现**原始异常 CFG 上的输入定义活跃性/覆盖证明**，区分正常
commit、throw 前状态、选中的 handler 和恢复协议自身读写，并独立复验。
当前异常 SSA 的入口依赖归一化后的 body，不能直接用它证明归一化自己的
前提。m23306 还涉及 effectful close/completion 流；本轮不恢复已暂停的
for-of 工作，也不删除其 catch。

m58418 需要另一类证明：promise 创建前闭包实例化及其环境的私有性、逃逸
和可观察异常。现有新环境写入准入明确以“尚无闭包”为前提。不能仅将
DEFINEFUNC 加入 prelude 操作名单，或因资源差异被排除就把所有创建效果
当作不可观察。本轮遵守 evidence-first 停止条件，不扩大为这个证明工程。

## 诊断接口与验证边界

- `AsyncCompletionFailure` 保留 `FunctionEmitError` 原值，附固定大小 check、
  可选 `TreeLocation`、related original PC 和 AwaitRegion/AwaitCatch 下层原因。
- `AwaitCatchObservation` 只记录已有的 handler/register、prefix、suffix 判断，
  不增加分析扫描或准入条件。失败位置使用 optional；真实 PC0 与无位置分开。
  旧 dispatch 的 malformed offset 有歧义时不发布猜测的 PC。
- FamilyRouteFailure、route receipt、manifest 的 route / record-entry 两条
  JSON 路径，以及 `abc_semantic --record-source` 共同保留这些字段。
- async branch 迁移到新封装；generator 的旧 FunctionEmitError 路径不改。
  resource 分类保持历史 MemoryLimit，不借此改变进程致命性。

新单测覆盖全部 check 的序列化、两条 JSON 路径、真实 PC0、缺失位置、
原始/工作 try、handler 和 branch 边界、下层预算失败、200/201/400/800 的精确 byte 边界。
CLI 测试读取四个真实 ABC，两种契约分别核对原始指令、完整原因链、
handler/register 证据和 manifest/abc_semantic 一致性。它依赖本地 corpus，
不是新建的便携 es2abc fixture。

由于本轮不放宽准入，没有新增 complete，也没有声称完成 await/microtask
Node 差分或修复组的语义变异矩阵。实际构建、回归和 cohort 数字见 status.md。

Round 78 已补充原始异常 CFG 的 catch-input 证明，见 [await-catch-flow.md](await-catch-flow.md)。
本文件保留 Round 69 当时的拒绝事实；当前准入与验证边界以新文档和交付证据为准。
