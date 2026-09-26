# Value IR、导入与证明合同

状态：拟实施规范；与 [总体入口](README.md) 和 [实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)
共同使用。以下类型为设计词汇，不宣称现有头文件已定义。实现须按仓库 Result/ID
惯例命名；改变字段责任或证明前提需要先修改本合同，不能只在代码中静默偏离。

## 1. 两层表示，不复制一套 VM

不可变输入层继续使用 `SemanticMethod`、`RegisterSsa`/`InstructionSsa`、原方法体及原证书。
工作层 `ValueProgram` 保存可变换的值、操作、控制边、使用链及来源。它不是新的
字节码解释器，不按 opcode 名分派，不把寄存器槽直接当源码变量。

工作 IR 的标量值包含语言值与尚待证明的内部域；未知不能默认为 undefined。
对象、模块、词法 cell 的“内容”不能因为某次 Load 得到 ValueId 就变成不可变存储。
Load 的结果可以是快照值，但后续另一 Load 必须保留独立操作，除非另有已实现证明。

## 2. 身份与所有权

| 身份 | 唯一性范围 | 用途 |
|---|---|---|
| InputIdentity | 内容 hash、实体 ID、profile、输入布局、规则版本 | 原始输入/缓存/输出关联 |
| InputSiteId | 输入方法内 | 原指令位置，关联原 PC 和规范化 origin |
| PhaseSiteId | 分析 owner 内 | Normal/Before/Commit/Catch/恢复位置，不等同 PC |
| OpId / ValueId / UseId | 一个工作方法内 | 稳定操作、结果值、操作数 occurrence |
| WorkBlockId / EdgeId | 一个工作图内 | 完整边身份，含种类及 successor ordinal |
| OriginId | 来源 arena 内 | 多 PC、合并、消除或合成的可追踪关系 |
| EnvId / CellId | 方法创建上下文内 | 环境分配身份与共享存储位置 |
| FactId / ProofId | 输入/工作证书 owner 内 | 前提、证明和变换义务 |
| IrRevision / CfgRevision | 一个工作方法内单调增长 | 下游缓存失效 |

ID 用不同 strong type；不能混用裸整数或跨 owner 比较。可空 ID 用 optional，
arena 空洞用显式 tombstone；不能以最大整数等魔数表示不存在。序列化可用数字。
参数数量和 ABI 槽从 FrameLayout/profile 取，不写 `num_args - 3`。

`InputIdentity` 必须包含 container/ISA/semantic 三种 profile 身份及
SourceContract、引擎规则修订；方法实体不等于方法数组下标。归一化方法还需
normalization owner/version 和原始方法身份。hash 获取复用有界文件身份接口。

阶段对象持有明确 owner 或借用 view，借用不超过输入 owner 生命周期。
禁止 SourceTree/公开结果保留指向已释放 MethodBody、SSA 或常量 reader 的指针。
来源只保留需要的紧凑事实；不要求把整个输入和每个工作快照永久常驻。

## 3. 核心数据形状

以下为合同草图，不是可直接编译的实现；容器、Result、ID 复用仓库惯例。

```text
ValueProgram
  identity: InputIdentity
  revision: IrRevision, cfg_revision: CfgRevision
  blocks: arena<WorkBlock>
  edges: arena<WorkEdge>
  ops: arena<ValueOp>
  values: arena<ValueDef>
  uses: arena<OperandUse>
  origins: OriginArena

ValueDef = EntryValue | OpResult | PhiValue
EntryValue = Parameter(index) | Receiver(owner) | CallableSelf(owner)
           | NewTarget(owner) | UnknownInput(slot)
OpResult = (OpId, ResultIndex, Availability)
PhiValue = (WorkBlockId, list<(IncomingKey, ValueId)>)
IncomingKey = MethodEntryKey | EdgeId

OperandUse = (OpId or PhiValueId, OperandRole, occurrence, ValueId)
OperandRole = Base | Key | Callee | Receiver | Argument | StoredValue
            | Left | Right | Condition | ReturnValue | ThrownValue | PhiInput
ValueOp = (typed operation variant, operand use range, result range,
           placement, effect contract, origin, lifecycle)
Placement = (WorkBlockId, order, PhaseSiteId, HandlerContextId)
Availability = NormalCommit | CatchEntry | MethodEntry
WorkEdge = (source BlockExit or PhaseExit, target or function-exit,
            EdgeKind, successor ordinal, origin)
ExceptionalExit = (OpId, PhaseSiteId, HandlerContextId, origin, before_throw effects)
EdgeKind = Normal | Exceptional | Resume
```

`OperandRole` 是操作语义，不代替求值顺序表。常量/name/module/entity operands 使用
相应类型字段而非伪造 ValueId。variant 必须穷尽处理；未知分支返回类型化不支持。
方法 entry 的未知 VM 局部不能转成声明初值；只有 ABI/初始化证明允许创建语言值。
正常 CFG 的一个 block 内可能有多个抛出点，G1 不拆块时用逐 Op 的 ExceptionalExit
侧表保存每个异常截止点，不能仅用 block+successor ordinal 表示所有异常。
正常 SSA 的 phi 前驱仍是正常 WorkEdge，不混入该侧表。T09 相位图使用 PhaseExit
作为源，不能把相位边 ID 与正常 CFG 边 ID 混用；分析 stamp 包含 graph kind。
现有 RegisterSsa 允许 phi 的 MethodEntry 输入，尤其 entry-header 循环；导入必须保留
MethodEntryKey，不能丢掉它或伪造 predecessor block。该输入由独立入口初始化放置
证明下降，不能套普通边复制。G1 的 live 非平凡 entry phi 与其他非平凡 phi 一样拒绝。

### 3.1 操作集合与首期边界

| 操作族 | 必须保存的信息 | 首期处理 |
|---|---|---|
| 常量/复制 | 精确值域、NaN/-0/string identity、来源 | 类型化导入；纯复制传播 |
| unary/binary/compare | 运算枚举、左右 operand、强制转换/抛出性质 | 有证明的操作；不是默认纯操作 |
| member read/write | base/key/value、reference 类别、执行位置 | 保留效果；命名和计算属性分开 |
| invoke/construct | callee、receiver、参数顺序、调用种类、返回域 | G2；不将普通调用替换为 `.call` |
| branch/return/throw | 条件/结果/目标、completion | G1 支持正常 branch/return；throw 按能力表 |
| global read | 全局访问种类、guard、realm 契约、可观察效果 | G2 复用已证明 GlobalAccess；不猜局部声明 |
| local/env/module/global write | CellId/绑定 ID、guard、初始化及 owner | G3；record 组合 G5，不用名称证明身份 |
| object/array/own define | 分配身份、hole/length、key、descriptor、原型与定义次序 | G3/T07；定义不能降为普通赋值 |
| create function/class | method、creation-site、home object、capture 契约 | G3，子方法失败不得发布父级完成 |

当前 `ValueObjectAllocation::literal` 用类型化可选 pool slot 区分空对象与
`CREATEOBJECTWITHBUFFER`。缓冲创建仍为每次执行的新分配，保留输入 PC、效果和
结果，不能变成可共享常量或被纯值删除。导入不等于缓冲内容已证明：属性、方法、
捕获和定义顺序仍需解析。现有空对象求值、初始化合并及 AST 校验明确拒绝有
literal 的分配，禁止把尚未解析的缓冲静默输出成 `{}`。
| super/receiver init | owner、初始化前后状态、range 协议证书 | G3，共用操作管线 |
| exception/suspend/resume | phase、completion、owner、可见状态 | G4，禁止普通 SSA 适配器接收 |

能力表覆盖全 corpus，按 `SemanticOperation` variant + profile + 必要前提记录，不能只按 ISA group
说“已覆盖”。额外 ISA group 开发仍遵循既定顺序，但不是本轮新通路的前置全量工程。

## 4. 输入导入的硬合同

### 4.1 正常导入

`ImportNormalValues` 的逻辑输入：原 MethodBody、FrameLayout、SemanticMethod、
原 SemanticProof、OwnerFacts、SourceContract、共享预算；输出已验证 ValueProgram
和 InputCorrespondence。不得依赖已折叠的旧 SourceAst 或源码文本。

步骤：

1. 校验同一 body/profile/layout，原 SSA/证书和 body 未变更；拒绝 handler/恢复 owner。
2. 一次建立输入 block/edge/value 到工作 ID 的映射，预留预算检查后的紧凑容量。
3. 逐 typed semantic operation 枚举实际语义操作数 occurrence，再绑定 SSA read。
4. 校验 read ordinal、slot、数量、range 展开、write/result 和输入证书一致。
5. 记录 normal commit 与 may-throw 异常退出，不把正常结果暴露给异常路径。
6. 导入 IncomingKey phi，包括 MethodEntry 和同一 predecessor 不同 successor 的不同边。
7. 建立 use list、原始来源及 effect obligations；运行 ImportVerifier。

`RegisterEffect` 可能保守读取不进入最终源操作数。每个原 read 必须分类为
`SemanticOperand`、`ProtocolDependency` 或 `ProvenRedundantRead` 并附依据，不能静默丢弃。
同一 ValueId 可出现多次，每个 occurrence 独立保存；UseCount 是 occurrence 数。
同 block 多个 may-throw op 分别核对 effect prefix 和 origin，不只记一个“块可抛出”。

### 4.2 操作映射例子

| 语义操作 | 绑定要求 | 常见错误 |
|---|---|---|
| 命名属性读 | base 从该操作规定的输入 slot 获取，key 为 NameId | 把 AllReads 的第一个当 base |
| 属性写 | base/key/value 分别按提升结构与 register-effect 合同绑定 | value 与 base 恰同槽时丢一个 use |
| range call | callee、receiver、展开参数逐 occurrence 映射 | 把保守 read 顺序当 JS 求值顺序 |
| strict compare | 明确 left/right，分支 polarity 独立保存 | 根据反汇编文本反向猜条件 |
| catch entry | CatchResult 独立值，来源是抛出边/handler owner | 调用 Definition() 失败后当 undefined |

每种操作提交时同时提供“原语义字段 → SSA read/write → Value operand/result”的表和
负例。不能用一个读取循环处理所有操作后声称穷尽导入语义。

### 4.3 异常导入

`ImportPhasedValues` 只消费已验证的 ExceptionFlow/ExceptionSsa 及对应环境/owner 证书。
Before 读取并产生 before-throw 效果；Commit 才定义正常结果；Catch 定义异常 acc。
同一原指令可对应多个 phase site；正常跳进 handler 代码不等于进入 Catch。
保留正确 handler 优先级、显式 throw、正常/异常 completion 及异常发生前的 vreg 状态。
禁止为减少节点先合并 Before/Commit，再丢失 handler 可见性。
首期该适配器不存在时必须返回 `kUnsupportedExceptionFlow`，不调用正常导入试运气。

## 5. 效果不是纯度标签

首期 `EffectPlan` 是图中保守有序的效果 occurrence 和控制约束，不是全程序一条列表。
每个块内记录执行次序；边记录正常/异常效果出口；合流保留 predecessor 身份。
所有 heap/global/module/env 访问、调用、guard、分配及可能强制转换的操作均先视为屏障。
未知也是屏障。操作删除判定必须同时满足：

```text
unused scalar result
AND no language/protocol effect
AND exceptional completion == Impossible
AND no control/owner/capture obligation
```

`typeof`/加法/比较的纯度取决于具体输入、guard 和操作语义，不能按 JS 运算符名字判定。
未使用调用的 result 可删除，但调用必须保留为 expression statement。
属性读即使结果未使用也保留，因为 getter/Proxy 可抛出或执行用户代码。
`kAllocation` 初期保守保留；V2 排除 OOM 不意味着可随意删除有初始化/身份效果的操作。

变换不跨条件分支、handler、suspend、cell 初始化、super 初始化或动态 scope 边界。
G2 表达式融合允许把相邻效果组合到一个语法语句，但原 occurrence 的动态执行顺序、
次数和异常截止点必须一一对应，不能仅检验最终 result 值相同。
G1 不实现 load CSE、heap alias 优化、多效果域拆分或跨块代码移动。

## 6. 可维护 def-use 与变换

### 6.1 使用链

每个 operand occurrence 有 UseId，链接 ValueId 的使用链；每个 op 有固定范围或索引表。
替换一个 use O(1)，访问某值所有 use 按实际次数计费；不要每次替换扫描所有操作。
可用索引式双链/带反向索引的紧凑列表；不能保存会因 vector 扩容失效的裸指针。
erase 标记 tombstone，阶段边界可做一次压缩且显式重映射全部 ID 侧表。

### 6.2 首期算法

```text
build canonical representatives for identity copies
resolve aliases with path compression
visit each operand occurrence; replace with canonical value
build phi-only dependency graph after copy canonicalization
compute SCCs and process the condensation DAG in dependency order
collapse an SCC only if all external inputs denote one proven available value
canonicalize affected operands in one batch
mark observable/control/required-owner operations as roots
walk def edges backwards from roots; retain required input producers
erase unmarked pure operations; append ElidedPure origin disposition
verify once at stage boundary
```

复制纯 identity，不穿过数值转换、guard、cell store/load、参数默认值判定。
phi SCC 简化必须还有外部定义，且替换值在各使用/边上可用；全自引用/未初始化 phi
不能凭空产生语言值。外部输入不唯一的 SCC 留给 G2 的去 SSA，不求更激进等价。
SCC/condensation 只对 phi 依赖图建立一次，按输入边数计费，不枚举块或值对。
利用单调减少的活跃 op/alias 以及显式 work budget 保证终止，禁止“反复扫描直到没变化”。
不能在每个入参改变时重扫高入度 phi 的全部 inputs；上述批次 SCC 路线避免该退化。
不要每换一个 use 就扫描其余 use 建二次方链；批量 canonicalize 是首选。

G1 对所有已接纳参数/寄存器值在一次工作图中处理，不只选择第一个候选寄存器。
这不等于宣称已经处理所有语义操作族或所有控制图。

## 7. 修订、事实与证明失效

原输入证书 `InputProof` 不随工作图变更。`WorkProof` 明确对应 IR/CFG/owner revisions。
事实不能仅凭旧 InstructionId 搬到新操作。最低需记录如下依赖：

| 变更 | 必须更新 | 可以保留但需满足条件 |
|---|---|---|
| 纯 copy 替换 | operands、use lists、value domains、受影响 phi、origins | 不改边时 dominance/handler topology |
| 删除纯死操作 | result/use census、源 PC disposition、owner obligations | 效果顺序中的未删节点 |
| 表达式分配/融合 | evaluation ownership、effect occurrence map、source origins | 不改 IR 的输入证书 |
| 拆边/改终结点 | CFG、phi edge keys、dominance、regions、liveness、scope placement | 原指令本身的语义事实 |
| cell/捕获合并 | 所有使用与逃逸/初始化/creation rebasing | 无证明不得合并 |
| 方法子树变化 | child closure-write/capture obligations、parent publish proof | 不相关子树 immutable facts |

原模块初始化/capture/receiver 证书作为前提保留，工作证明必须说明原操作如何仍被
执行、引用与初始化时刻如何保留。`ClosureWrites::kLanguage` 只证明值域，不能被
扩大成不变性、非 undefined、getter 无副作用或 cell 永不修改。

变换批次先收集 edits、检查预算及局部前提，再原子提交工作修订。失败不得留下半改图。
同一批次结束验证：def-use 双向一致、结果唯一、非 phi 使用被支配、phi 沿边可用、
terminator/edges 一致、结果异常可见性正确、required effects/owner obligations 不丢失。
拓扑不变的纯变换不必重建 CFG；也不能以此为由跳过值及来源验证。

## 8. 预算、复杂度与失败

以 I/B/E/U/P 为输入/图维度，另记 O 为效果 occurrences、A 为最终树节点、K 为来源边。
目标 import/索引 O(I+E+U+P)，来源/输出 O(K+A+bytes)，alias 近线性；实际 worklist
重访和 dominance/SSA 构造沿用 algorithms.md 的上界与预算，不能统称全部 O(n)。
禁止每个 ValueId/寄存器走一遍整个 CFG、全体值配对干涉、每次 origin 合并复制全集。

预算覆盖同时存活的输入、SSA/证明、工作图、区域、树、来源、输出和运行时依赖。
使用共享预算 ticket/RAII 记账，不能每个阶段都拿同一个“剩余 1G”独立分配。
旧/新路径对照顺序运行或释放前一图，不默认同时保留两个全集。
工作量计数不等于性能测试；预算账本不等于 RSS 合规证明。后者仍待授权恢复测量。

类型化失败至少区分：unsupported operation/profile/exception/resume、unproved operand、
invalid input correspondence、invalid work IR、unresolved phi、unclosed owner obligation、
work limit、memory limit。保留原 method/PC/阶段/修订，字符串只出现在呈现边界。
无能力与内部不变量损坏不同；内部损坏不能统计为正常“不支持”或继续回退发布。

## 9. 实施者交付清单

- 每个新操作的导入表、正常/异常 outcome、前提与负例。
- 真实输入到工作 ID 的关联及 revision 检查；不得保留未经 owner 管理的借用。
- 每个变换的 preconditions/edits/invalidations/postconditions/origin rule/work bound。
- use-list、phi、effect、capture/owner 验证器；没有 verifier 不接入公开新路线。
- 验收合同 V01–V10 对应实现/规格/未执行状态，不以旧路径成功补缺项。
