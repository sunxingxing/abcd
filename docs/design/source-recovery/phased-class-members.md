# phased creator 中的类成员投影（2026-10-29，设计稿；原型已撤回）

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

## 原型结论（2026-10-29，已撤回，未提交代码）

原型依次打通了以下环节：creation 模式按 kind 选择、`FindClassInitializerCalls` 在
phased 图上改用 `VerifyPhasedValueFlow`、family tree 把调用传给 phased 求值计划。
随后发现另外三个真实前提，原方案低估了工作量：

1. **寄存器拷贝**：phased 图有意保留 `ValueCopy`（异常拷贝模型依赖寄存器槽）。
   normal 路线由 `SimplifyValues` 折叠这些拷贝，而类分析（识别、投影的"调用前无
   观察"检查、求值、类树）都按无拷贝图写成。原型给识别器加了按拷贝根比较，识别
   得以通过，但投影又把类值的拷贝当作调用前的观察而拒绝。逐处打补丁会把拷贝语义
   散布到每个类分析里，不符合统一方案。
2. **自引用静态字段**：`static y = A.x` 会让 es2abc 生成 `newlexenv` 加环境清理
   handler（`sta; poplexenv; lda; throw`）。这样的 handler 在异常端口上等同于
   caller 出口，但 class 本身走的是 record 路线的 class-self（`ClassInitializationPorts`）
   机制，需要一并接入。
3. **实例字段**：`FindClassPrototypeReads` 只支持 normal 图。

## 可选方向（需要先定设计，再实现）

- **A. phased 拷贝折叠**：对异常弧上没有被任何 phi 读取的拷贝，把它的使用直接改为
  指向源值（与 guarded import 的 dead-copy 判定同一个条件），之后类分析无需修改。
  这需要同时整理审计中列出的"异常拷贝模型合并"，影响面最大，收益也最大（会顺带
  简化其他 phased 识别器）。
- **B. 拷贝根视图**：提供一个只读的 copy-root 查询，让类分析统一通过它比较身份。
  改动局部，但要在每个类分析里逐一接入。

推荐 A：它是同一条规则的推广，没有为类分析单独开口。
