# phased creator 中的类成员投影（2026-10-29，设计稿，未实现）

## 问题

big.abc 中 4,342 条记录被拒，原因是 `CreationContextError::kKind`。复现只需两行：

```js
export class A { static x = 1; }
try { f(); } catch (e) {}
```

模块入口有真实 try/catch，所以走 phased 路线。class `A` 的静态字段由 es2abc 编译为
`definemethod static_initializer`（home object 为类本身，经 accumulator 传入），随后
`callthis0` 调用它。

## 根因

两层，均为通用缺口，与具体样本无关：

1. `BindPhasedCreations` 把所有存活的 function creation 都按 `kFunction` 解析，
   DEFINEMETHOD（accumulator home）因此被判为 kKind。record 路线对同一指令使用
   `kMethodBody`。改为按 creation kind 选择模式后，失败点后移到第 2 层。
2. `ClassCreator::Initializers()`、`Definitions()`、`Instances()` 在 phased creator 中
   返回空集（注释："phased member-installation fusion has no exception-port proof"），
   因此静态字段初始化器只能被当作普通子函数，在 capture evaluation 阶段被拒
   （`CaptureEvaluationError::kCreation`）。

## 方案（复用 record 路线的投影，只补异常端口条件）

- 在 phased 图上运行同一套 `FindClassInitializerCalls` 与 `FindClassDefinitions`，
  不新增第二套识别逻辑。
- **异常端口条件**：参与投影的每个 op（creation、home 读取、call、定义存储）的异常
  出口都只到 caller，没有本地 handler。这时投影中间状态随帧一起丢弃，与 normal 流
  等价，这与 `kCallerGuarded` 模块读取采用的是同一论证。
- 本地 handler 内的类（类定义位于 try 内）继续拒绝，等待单独的端口证明，不在本步扩大。
- 第 1 层的模式选择随本步一起交付。单独提交只会改变拒绝原因，不会增加完成数。

## 过度设计检查

不新增 owner，也不新增 IR 节点。只是把现有的 record 路线投影接入 phased creator，
外加一个按 op 检查异常出口的谓词。

## 验收

- fixture 差分：模块入口 try 加静态字段、实例字段、computed member；
- 负例：try 内的类仍被拒绝；
- cohort 零回退、零文本变化；big.abc 完成数上升，且无"完整 → 拒绝"。
