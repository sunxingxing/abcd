# 统一变量恢复：共同语义流与分离的恢复计划

2026-10-07用户明确设计；这是当前架构任务，不是已完成报告。
规则172/f656e763是迁移基线。范围为普通函数临时变量与循环携带值，
不推倒重写解码、IR、结构恢复或验证链，不扩大语料/恢复暂停事项。

## 唯一语义入口

生产入口位于通过 VerifyTreeFlow/CheckTreeOperands/
CheckTreeCorrespondenceOrigins 的同一次函数准备内：保留 ValueProgram、
ControlPlan、EvaluationPlan 与 TreeFlow 已证明的对应事实，构成不可变
RecoveryFlow。不能由优化器重新匹配原始 opcode、图形状或 printed text。
规则176中普通 VerifiedFunction 与 family 均保留最终语义验证产生的
TreeFlow；family移动既有证据，不重复保存或重新计算。TreeFlow 是对应证据，不是另一个待替代的 CFG；复用 snapshots、phis、
cells、copies、catch commits、原操作表达式和 origin 身份。源语法引入的
binding allocation/初始化/离开另建显式事件，不能冒充原 SSA operation。
独立 FunctionTree 测试走同一源码事件适配器，但不得伪造原 IR 证明。

核心类型为 FlowPoint、FlowDefinition、FlowReference、FlowCompletion、
FlowBinding；均为强 ID/typed variant。定义记录参数/undefined初始化/
表达式结果/赋值/phi 来源。引用区分绑定与属性，捕获保存单元身份而非值。
完成边区分 normal/throw/return/break/continue/suspend/resume；作用域事件
区分 enter/initialize/leave。++ 分成引用读取、ToNumeric、数值操作、成功
写回；读失败、转换失败都不能到达成功写回。let x; 是 undefined 定义。

图保存原 identity/value_revision/cfg_revision、ExprId/StmtId/TreeOriginId
映射，原生词法名称解析复用 TreeScopeCheck/ResolveTreeReferences。
不可把待消费的 BindingInitializers::normal_stores 或 CompletedStore
作为共同模型输入：初始化事实必须由模型自己的定义/控制分析产生。
既有 IntrinsicEvaluation 语言表可复用一次；不能在每个优化内各自解释。

## 分析与变换契约

| 组件 | 消费/产出 | 禁止副作用 |
| --- | --- | --- |
| RecoveryFlow | 已验证值/控制/效果与源码生命期事件 | 不选名字、不删声明 |
| FlowDefinitions | 到达定义、def-use、复制等值与合流事实 | 不删除效果或初始化 |
| FlowLiveness | 活跃事实和组干涉 | 不删除写入、不改绑定生命期 |
| StoragePlan | 已证无干涉的值/存储分组、绑定身份约束 | 不抹掉被合并声明的定义 |
| DefinitionElision | 死定义和冗余copy动作，保留RHS效果/异常 | 不按名称认定死值 |
| SourceVariables | 词法声明/let/const/loop位置与名字解析 | 不改变已证执行关系 |
| printer/class-name consumer | 同一immutable恢复计划、语法优先级/来源范围 | 不再做独立语义优化 |

初始undefined活跃时可以共享存储，但必须在对应原点保留初始化写入；
若这次写入会破坏另一个仍活跃的值，干涉分析拒绝共享。两种决定分开。
不能再以“首次读取必须是首次赋值”同时替代干涉和死初始化证明。
共享一份恢复计划给class-name准备与printer，禁止两边重复生成语义事实。

## Pass 顺序、状态依赖与分析失效

2026-10-07补充：多轮执行不能代替依赖管理。每个pass读取固定版本的
图和恢复计划，先生成delta再统一应用；不能在扫描中让后续节点消费
部分更新状态。协调器记录pass读取/修改的事实，以及仍被保留的分析。
源码投影变化需要独立revision：原value/cfg revision不变不代表读取、
定义、效果或绑定生命期仍未改变。失效分析必须在下一消费者之前重算。

依赖顺序为：共同模型 → 定义/初始化事实 → 值与复制传播 → 更新模型及
失效事实 → 活跃性/干涉 → 存储映射 → 映射后的定义/效果消除 → 简化稳定
后恢复声明/循环/作用域 → 冻结打印计划。后向活跃性本身只依赖图的
read/definition与完成边，可先求解，再用于裁剪前向到达定义；这是分析
求解的依赖，不允许以活跃性代替初始化证明。

只对会产生新机会的简化组迭代，比较实际delta，给出单调简化度量与
预算终止；不固定跑三轮，不让反向重写相互振荡。无变化立即结束。
资源耗尽仍返回typed错误，不能把未完成轮次声称为稳定结果。
声明恢复引入的初始化同样进入模型，不能被打印器私自省略。

当前尚无完整pass协调器：BindingInitializers::Run串行修改同一plan。
规则179移除RefreshProjection这一混合入口：ElideStoredResults仅执行
const分析、const提案、批量提交。AnalyzeEvaluations为const，返回独立
结果，不更新plan。Coalesce与OmitInertStores在消费处取得当前投影的
局部事实，因此ForwardSnapshots之后不会复用之前的效果/写入事实。
最终normal_stores/evaluations仅在Finish前发布，内部pass不再读取这两份
输出缓存。局部分析随调用结束释放，不跨修改缓存；将来若缓存，必须引入
完整投影版本及失效依赖。尚需多轮简化协调器与共同初始化模型。
本次共享活跃性在冻结的局部RecoveryFlow上先算，干涉随后只消费它；
二者计算期间不修改flow或plan，分析不跨存储映射应用继续复用。

规则178增加SourceVariables准备结果：按nullish → loop → binding顺序产生
只读消费的同一份投影，当前打印单元的class-name与printer共享它。Direct
入口先准备变量再分析类名，family在所有成员准备完毕后分析类名；借用的
结果绑定同一未修改FunctionTree对象，禁止跨修改缓存。外部嵌套class body
仍由类名消费者通过同一协调器临时准备，跨打印实例共享尚未完成。
这消除了两个消费者各自调用三套准备函数的路径，但未替代BindingInitializers
内部旧初始化分析，也没有完成版本失效/多轮pass协调器。

## 路线取舍

1. 采用现有已验证流的source overlay，保留原SSA/效果/异常身份并显式
   加入词法事件。适合当前恢复链，避免重做正确的解码/验证。
2. 只把规则172私有图移动到新文件：可作抽取步骤，但不完成里程碑；
   它仍依赖旧优化器提供normal_stores/aliases并缺少定义/生命期事实。
3. 直接在printer上继续局部合并，或重建整套IR：均不采用。前者违反
   用户职责边界，后者扩大迁移面且丢弃既有独立证明。

## 旧判断退出清单

| 当前决策 | 当前证明义务 | 替代方/退出标准 |
| --- | --- | --- |
| Coalesce::CompletedStore | 删除empty声明不会丢live undefined | DefinitionElision按定义live/use决定，Coalesce删除该条件 |
| InitializedRead/normal_stores | TDZ/GetBindingValue/PutValue成功 | FlowBinding初始化与完成边；优化器不得重算 |
| completion_order/native_ready/declaration_controls | 初始化先于改名引用 | FlowDefinitions与SourceVariables生命期证明 |
| RootLifetime/Confined/region区间 | 原生for-let不改变环境与可见性 | 统一scope索引与binding lifetime计划 |
| Constants/Aliases/PrivateCopy | immutable值传播与复制等值 | FlowDefinitions复制/到达定义；名字不参与值判断 |
| Evaluations的stored-result推断 | 同值已写回，不重复PutValue | 同一定义/正常完成事实和DefinitionElision |
| ForwardSnapshots控制/环境census | 执行次数、顺序、异常/词法环境 | 共同效果/控制/生命期事实；不能先删真实义务 |
| printer的inert/声明推断 | 当前表示与省略判断 | SourceVariables已确定动作，仅打印与来源投影 |

暂未迁移部分显式标记legacy owner与义务，不增加新的局部准入条件。
不能把重命名函数或增加wrapper算旧决策已退出。审查要列出删除的实际
分支及其共同证据使用点。完整原控制/receiver/捕获身份安全检查不盲删。

## 源码省略与发布消费者

规则177将每个表达式/属性/语句的GeneratedExtent随打印结果传递：正文
跨度或明确省略边界。范围种类来自执行既有恢复计划的打印事件，不根据
零长度范围反推省略许可；默认是必须有正文的跨度。已转发RHS在实际位置
输出时恢复跨度标记，原被删除写入仅保留边界。循环头遍历不能为了少输出
分隔符跳过剩余省略根，它们仍有原始来源身份。

FamilyEmission只校验同一不可变打印结果的身份、索引、归属与坐标种类；
删除其独立PlanNullishSyntax、DiscardedInvocationRead和ImplicitTailCompletion
判断。语义省略合法性仍由原恢复分析负责，类型标记不是绕过该证明的输入。
空模块正文是模块语法允许的表示；每个原始来源仍必须满足自身已记录的
跨度或省略边界，不能把未标记的空范围当作成功。测试端保留独立重算计划、
尾部/可选链精确坐标关系及生成源码行为对照。完整SourceVariables协调器
仍需统一printer/class-name准备，不能把这项消费者迁移当作全部完成。

## 能力边界和失败语义

复杂finally暂采用保守完成关系或保持原表示；不引入pending-completion
模板匹配。闭包单元重构与更精细暂停恢复不属于首个普通函数里程碑。
已有可用输出继续可用；表示精度不足保留真实源码存储，确实缺失语义
使用现有typed gap。资源/工作耗尽沿现有budget，不能新增覆盖平台作前置。
跨scope身份、source locals和外部可变单元不冒充private scalar。

## 验收

普通复制/多次赋值、diamond phi、循环携带、活跃undefined、异常前后写回、
shadow六域形成闭环：从共同模型生成源码，并退出被替代局部判断。
布局变化含寄存器重排、COPY插入、拆块、arena顺序，不要求输出逐字相同。
小图独立固定点oracle核对def-use/liveness/interference，禁止复用生产算法
作oracle。组合覆盖loop/branch/exception/capture/suspension；超出精度的
项明确保留，不能靠新增组合分支。合法JS错误合并/提前写回/删live定义
mutations必须被独立行为对照发现。实际PasteBoard/ProgressBar等既定
record继续验证；配对源码仅在测试端。按N/E/活跃事实L/传播P说明成本，
200/201和几何增长用定向测试，暂停的全套/RSS/benchmark不自动恢复。

完成标准不是“新增模型”：共同事实真正驱动五类恢复职责，重复决策退出，
性质与独立执行以及真实record通过，并说明仍保守的分析精度范围。

## Dead-write consumer (rules180)

`CurrentFlow` is the single source-projection analysis entry for the independent
write-elision and storage passes. `RecoveryFlow` now distinguishes a native source
declaration commit from an ordinary assignment, retains the write occurrence,
and records whether its successful PutValue is inert. The transitional adapter
supplies declaration plans, initialized writable-store facts, and nonescaping
compiler-temporary identity. These are not a new SSA/TDZ proof: the existing
initialization and escape census still owns them until the shared model replaces it.

`PlanDefinitionElisions` consumes that unchanged flow plus shared liveness and
returns write occurrences. Only dead, inert ordinary assignment definitions are
proposed. Applying the batch replaces each write by its original RHS, preserving
its evaluation, result and completion. Initialization, updates, externally visible
bindings and capture cells remain. Storage coalescing runs afterwards on a newly
built flow; no analysis spans the delta commit. Later inert-expression omission
remains separate. This is an actual production consumer, not an unused analysis.

The resource-observable legacy contract retains its representation: existing
source naming restoration after elision can require an allocation envelope, and
allocation equivalence is not proved for this new transformation. This is an
explicit capability gap, not a language invariant. Selection uses the contract,
not a local producer whitelist. Current phase-one language contracts exclude
resource exhaustion differences and use the new pass.

This substep does not yet separate live undefined initialization from storage
sharing, eliminate dead declarations, retire Coalesce's CompletedStore, or supply
full initialization/lifetime facts from the canonical CFG/SSA. The simplification
schedule is still finite and explicit, not a fixed-point coordinator.

Partial trees retain the existing no-simplification path. That terminal path also
publishes expression-indexed conservative evaluation/write summaries; empty
vectors cannot represent unknown facts to printer consumers. The partial-tree
regression combines private writes, comma evaluation and an UnrecoveredStmt.

## Demand closure (rules181)

The delete-write/delete-inert-RHS interaction no longer needs repeated graph
reconstruction for cascading dead copies. RecoveryFlow records typed discarded
regions and effects. AnalyzeFlowDemand shares the backward liveness solver's
completion traversal and definition kills; observable roots activate reads, and a
needed definition activates reads in its producing region. The monotone worklist
finds def-use dependencies on demand and closes them before one batch commit.

FlowDemand is deliberately distinct from all-read FlowLiveness: storage consumers
cannot accidentally receive a pruned demand result. No full read/definition product
is required. Materializing that product exceeded the existing 402-selection budget;
the on-demand solver addresses the cost without relaxing the budget or adding a
size/shape admission. FlowDefinitions itself remains tested for future consumers.

An unobserved copy cycle has no seed and can disappear; an observed result or
effect retains its dependency chain. Control conditions, initializations, returns,
throws and suspension stay. Subsequent storage analysis starts on a rebuilt flow.
This closes only the dead-write/inert-evaluation interaction. Shared initialization,
storage identities in the flow, declaration removal, CompletedStore retirement and
post-storage convergence remain open.

## Independent unused-binding pass (rules182)

RecoveryFlow now retains a typed binding exposure fact. The migration adapter
supplies nonescaping compiler-private identity; it does not claim to replace the
legacy capture/initialization analysis. PlanBindingElisions consumes the immutable
flow and the proposed DefinitionElisions, before either delta is applied. It
indexes removed occurrences and checks all remaining semantic reads/definitions.
An unused private undefined declaration can disappear only when no retained
reference or other definition needs that binding. A dead initial value alone is
insufficient. Explicit/native initializers, retained assignments/updates and
observable/captured identities remain. Unreachable but retained syntax also counts.

The concrete dependency order is flow -> demand closure -> definition delta ->
binding delta -> batch commit -> fresh flow/liveness/interference for storage.
The binding pass owns declaration removal; the definition pass still owns only
write/evaluation removal. No post-mutation demand or effect fact is reused, and no
additional graph rebuild is needed between these two proposal passes. The new
scan is over typed semantic occurrences, not another syntax/first-store census.

This removes unused empty declarations left by dead-write elimination, including
bindings that were never written. It does not remove effectful/native initializer
declarations, implement live-undefined storage sharing, or retire CompletedStore.
The full revision/invalidation coordinator and post-storage convergence remain
open; this bounded group provides explicit proposal dependencies and one commit.
