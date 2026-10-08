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

规则214补充：FlowReference.owner 表示当前执行语句，site 保留原始来源
身份。流图构造上下文传播owner；替换与快照转发使用目的上下文，普通
子表达式、条件和短路分支不丢失owner/异常上下文。RetainedExtents 与
Coalesce通过当前owner查询语法投影后的词法目的；后者不再在快照后
使用原ExprId的归属/作用域。因此规则213所述快照前限制不再是范围
分析的数据前提；调度顺序仍保留其各项初始化/值传播依赖。
语法目的索引 O(statements)，引用owner O(references)，无逐变量遍历。
该字段是当前发生位置，不代表scope退出、capture cell或恢复环境模型
已经完成；其他仍直接消费原始表达式范围的迁移继续单独证明。

规则213补充：声明恢复和别名根选择共用当前保留引用范围。范围扫描
消费 RecoveryFlow.references 与已解析的词法目的，O(bindings+references)，
不再把已经删除的旧引用作为非逃逸绑定的范围约束；未建模签名观察
仍从原始记录保留。引用即使控制不可达也参与，因为输出中的词法名字
仍需有效。两个消费者在快照移动之前运行，不能把原始表达式所有权
无条件推广成移动后的位置。捕获绑定继续明确使用 LegacyConfined；
完整生命期和移动后统一 occurrence 所有权仍是后续迁移义务。
这份分析不修改计划，不跨提交缓存；后序pass消费当前流图重新计算。

规则212补充：非逃逸绑定的候选位置来自当前 RecoveryFlow 定义，
不再读取原始首次赋值。候选使用共同 discarded ownership、归一化
表达式根及已有声明语法容器；声明覆盖整个赋值语句或已支持的 for
声明位置，不把任意嵌套写入拆成新语句，这是明确的表示能力边界。
按支配树前序选择最早候选，再要求它支配所有保留读写；因为每个
候选写入也是引用，任何能支配全部引用的候选必在其他候选之前，
无需逐候选遍历图。PutValue正常完成和词法范围仍独立证明。
DeclarationSite 是提交后的投影状态，与原始 BindingUses 分离；
快照环境、RootLifetime、存储合并与声明重置均消费实际位置。
移除原始 initializer_container 字段；legacy_initializer 只供尚未
迁移的捕获检查使用。额外索引为 O(statements+bindings)，候选和引用
各扫描一次，支配计算复用既有 LT；无编号/同块/邻接准入。
普通声明与 for 声明打印统一消费 BindingInitializer.assignment；原始
ExpressionStmt 只用于沿已提交替换链映射源码范围，不能重新选择赋值。
嵌套死写入的独立执行捕获了旧打印器重复写入/TDZ错误，模型验证和
源码语法检查不能替代这层对照。打印器没有新增效果或准入决策。
完整绑定生命期、捕获模型和全局固定点仍未完成。

规则211补充：非逃逸绑定的声明恢复已退出首次语法观察匹配。
重复写入先归一化，无效果丢弃观察及死值定义再经独立 pass 清理，
避免把无用旧写入
过早变成必须保留的词法初始化；Declarations 对当前 RecoveryFlow
计算共同支配关系，要求候选写入正常完成支配所有保留读取和写入。
RHS、异常路径和早于初始化的写入均纳入同一参考集合；PutValue 的
正常完成消费 ExpressionFacts.normal_stores，而不是“可删除私有写入”
的 FlowWriteEffect。值写入保留为初始化，不能混用删除效果的义务。
额外扫描为 O(bindings+definitions+references)，支配计算复用现有 LT
实现；无逐变量图遍历或固定图形状准入。词法范围仍由 Confined 证明。
捕获单元的跨函数观察尚未完整进入该流，沿用原首次观察及单元作用域
检查，不扩展它的接受域。初始候选位置仍来自声明扫描，完整生命期、
更一般声明位置选择及全局固定点仍未完成。

规则210补充：初始声明定位从 Run 的直接写入迁为独立 Declarations
pass。先收集拥有自身数据的 DeclarationEdit，再统一提交声明/原占位
省略；首次提交推进投影 revision，失效初始化、表达式、流与活跃性。
无方案时不推进 revision。它仍在值传播和存储合并之前执行，保留
现有捕获单元与词法范围证明；InitializerAvailable 的首次观察检查
尚未被共同完成关系替代，本次只闭合 pass 提交职责，不宣称该检查
已退出或实现了全局固定点。额外临时空间与提交工作均为 O(bindings)。

规则209补充：BindingReadCoverage 的声明结果保存当前覆盖定义的 StmtId，
Aliases 的稳定来源与作用域查询直接消费这一身份。删除“当前定义必须
等于原始扫描 use.definition”的重复匹配，避免把原始声明定位当作当前
定义证明。移除 BindingUses::definition 字段；初始声明候选只登记已有
可迁移空声明的绑定。未删除生命期、逃逸或可观察写入约束，
Confined/RootLifetime 的完整共同生命期迁移仍开放。查询与提交边界不变，无新 pass 或额外扫描轮次。

规则208补充：初始声明定位只产生 let；独立 DeclarationKinds pass 在
存储/循环声明/死定义/表达式清理之后消费当前初始化定义与写入引用，
为非逃逸私有临时量选择 const。它只提交声明种类，不删除初始化、
更换存储或移动作用域；实际提交统一失效分析，最终效果摘要随后发布。
BindingUses::writes 及 Observe 的原始写入计数已移除。源码 let、
捕获单元和共用一个 let 关键字的原生 for 声明组保持既有表示，整组
const 恢复不在本次独立声明种类迁移内。完整 declaration placement/
activation 模型及全局固定点仍待完成。

规则207补充：Aliases 已消费共同 CoveredWrite，BindingUses::store、
旧 stores 覆盖映射和 Elide 删除函数退出生产决策路径。共同结果记录
当前 discarded ownership 与归一化表达式根；只有确实拥有整个根的语句
才进入整句删除提案，其他 occurrence 仍可按写入→RHS 保留求值，不以
整句形状作为别名准入。Observable PutValue 保留为来源存储，不能因为
值覆盖成立就删除其完成。词法名字/捕获/activation 继续独立证明，
原始声明生命周期 census 与全局固定点仍未迁移完成。

规则206补充：Constants 从共同读取覆盖结果取得当前 CoveredWrite
（表达式身份、定义种类、写入效果），退出原始首个 use.store 定位。
普通赋值删除须证明 FlowWriteEffect::kInert，词法初始化按独立种类处理；
读取覆盖不能替代写入完成证明。常量替换仅将选中写入投影为其原 RHS，
随后无效果语句清理决定是否省略语句，不再调用旧 Elide 的整语句删除。
前置 StoredResults/InertOperands/InertStatements 先移除无效旧观察，
每次提交仍统一失效分析。Aliases 仍使用旧首个 store 的覆盖映射和删除
运输，声明恢复/activation 与全局固定点尚未完成；不宣称全部消费者迁移。

规则205补充：常量传播也消费当前写入计数，原始写入次数不再承担值传播
稳定性证明。StoredResults 先于 Constants，之后保留 Constants →
StoredResults → Aliases 的依赖，因为常量投影可产生新的同值写入。
每次实际提交统一失效当前分析，后继读取覆盖重新取得当前图；没有固定
重复轮数或局部 self-copy 匹配。BindingUses::writes 仅余初始 const/let
拼写选择，已注明由简化后的定义/activation 分析替代。首个候选 store
定位与完整生命期迁移仍未完成，不能把写入计数迁移当作整个架构闭环。

规则204补充：别名稳定性改为消费当前 RecoveryFlow 的写入引用计数，
不再用原始 BindingUses::writes 判定零次/唯一写入。冗余同值存储清理
先于别名恢复；其提交失效 flow/liveness/expression facts，后继通过现有
CurrentFlow/CurrentLiveness 取得当前投影。别名提交后仍需结果存储清理，
因为别名替换可产生新的同值写入。此处是有方向的两项依赖，不是全局固定点。
计数按绑定建表，O(bindings + references) 工作和 O(bindings) 额外存储。
原始声明/候选写入定位与生命周期判断仍待共同模型迁移，不能把迁移计数
称为完整 def-use 或 SourceVariables 迁移。活跃 undefined 初始化保留；
RHS 副作用/异常由既有独立存储消除证明负责，别名计数不授权删除它们。

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
| Coalesce::CompletedStore（规则184已退出） | 删除empty声明不会丢live undefined | PlanStorageInitializations消费共享live事实；死值省略，活跃值在原点写回，共同completion dominance证明目标初始化已完成 |
| InitializedRead/normal_stores | TDZ/GetBindingValue/PutValue成功 | FlowBinding初始化与完成边；优化器不得重算 |
| completion_order/native_ready/declaration_controls（Coalesce路径规则185已退出） | 初始化先于改名引用 | FlowInitializations和当前FlowReference证明完成关系；其他消费者及activation证明仍待迁移 |
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

## Storage projection and dependent cleanup (rules183)

CurrentFlow now consumes the committed storage map as well as immutable aliases.
The flow adapter uses the same alias-then-storage identity as printed reads:
assignments, update reads, successful update commits, copied values and ordinary
reads all denote the retained slot. Declaration omissions and native initializer
plans are supplied from the same current projection. Reusing pre-storage facts
would allow a write through the removed name to be deleted even though a later
read through the retained name observes it; tests explicitly distinguish this.

Storage merging, stored-result deletion and inert-statement deletion report actual
changes. The coordinator accumulates those changes with loop declaration changes,
then reruns demand/definition/binding cleanup after those producers have finished.
An unchanged projection skips the extra analysis. A changed projection builds a
fresh mapped flow; no old liveness, demand, effects or write facts cross the commit.
Final loop entry counts and published evaluation summaries are produced afterwards.
This is a dependency-driven second stage, not an arbitrary fixed round count or
a claim that the complete storage/declaration pipeline reaches a global fixed point.

Shared initialization, preserving live undefined at its original occurrence during
storage sharing, scope activation and CompletedStore retirement remain required.
The new stage deliberately relies on the existing valid storage plan and does not
expand its admission conditions. This mapping was a prerequisite for safely
consuming common facts after storage changes, including future initialization work.

## Live undefined across storage sharing (rules184)

Coalesce no longer calls CompletedStore. It proposes noninterfering storage pairs,
then a separate PlanStorageInitializations decides what happens to their empty
declaration values. Shared all-read liveness identifies live undefined definitions;
these become explicit stores at the original statement, while dead values can be
omitted. The interference analysis already includes those initialization writes:
a retained value clobbered by the reset prevents sharing, without a new shape rule.

RecoveryFlow records the currently selected declaration per binding, with native
declaration plans taking precedence over original empty placeholders. The existing
Lengauer–Tarjan implementation is reused over ALL completion edges to prove that
the retained declaration completed before each preserved write. A binary fanout
adapter preserves all paths between real events, including exceptions, resume and
conservative cleanup; it does not discard high-outdegree paths. Source name and
activation availability still use ResolveTreeReferences and the grandfathered
RootLifetime proof, now also for the explicit statement occurrence.

The output plan has a distinct undefined_stores action. The printer emits the
planned `binding = void 0` at that statement and makes no liveness decision. The
rebuilt flow represents this as UndefinedStore, separate from lexical Undefined
initialization, so later cleanup cannot mistake a store for a new declaration.
Storage and initialization proposals commit together after all obligations pass.

Tests cover a live initial undefined read after the retained binding's old value
dies, repeated initialization inside a loop, an interfering retained old value,
and a target whose native declaration is still in TDZ. Independent execution and
missing-reset mutants distinguish preservation from merely suppressing the old
CompletedStore guard. Dominance is checked by independently removing each vertex
and recomputing reachability on 512 small completion graphs.

CompletedStore remains in immutable copy/constant recovery, outside Coalesce.
InitializedRead, native readiness and RootLifetime have not been replaced by a
complete shared initialization/activation analysis. Initializer motion, closure
cell reconstruction and whole-pipeline convergence remain open; this is a real
retirement of one decision path, not completion of the entire architecture.

## Shared reference initialization order (rules185)

RecoveryFlow emits typed binding references at actual read and successful-write
events. Update read/write halves are distinct, preserved undefined writes retain
statement identity, and forwarded expressions use their current flow position.
Removed expressions do not emit references. Coalesce now consumes this list instead
of scanning the original arena and guessing reads/targets from syntax.

AnalyzeFlowInitializations indexes selected lexical declarations, parameter entry
and catch initialization, and owns one completion-dominance result for the current
flow. Ordinary assignments, updates and preserved undefined stores do not create
lexical initialization. Coalesce reference checking and PlanStorageInitializations
borrow this same immutable result; the latter no longer constructs a second
initialization index or dominance analysis. No facts survive the mapping commit.

The Coalesce checks based on declaration_controls, definition_region and
completion_order/native_ready are removed. Each actual reference instead asks
whether its retained binding's initialization completed on every reaching path.
Unreachable retained syntax has no execution-order obligation, but still goes
through lexical name/lifetime checking. RootLifetime remains an explicit legacy
activation proof; completion dominance does not substitute for cell identity.

Source tests distinguish an eliminated early embedded write from an observed early
write before the retained native declaration. The former no longer blocks sharing
because of a stale arena occurrence; the latter must retain separate storage to
avoid TDZ. Independent execution and wrong-sharing mutants check both. The 512
graph remove-vertex oracle also checks conditional initialization completion and
exact/short budgets. InitializedRead/effect production and non-Coalesce census
consumers still need migration, as do canonical IR/lifetime integration and global
pass convergence; this retires specific production branches, not all old analyses.


## One immutable expression-fact pass (rules186)

AnalyzeExpressions replaces both AnalyzeStoredResults and AnalyzeEvaluations.
One semantic postorder over the unchanged projection produces result-binding,
normal-store and evaluation facts together. Replaced occurrences forward the
replacement facts and do not reintroduce effects from original children. This
analysis is const and returns owned arrays; it never publishes or mutates the
recovery plan. CurrentFlow consumes both relevant arrays from this one result,
and final publication moves both arrays from one result after all mutations.

Stored-result elimination and inert-statement elimination each obtain fresh local
facts, produce a const proposal, then commit that batch. PlanInertStatements no
longer changes omitted flags while scanning statements. No borrowed facts escape
these calls and no analysis cache crosses a projection change. Existing dead
value/binding and storage/initialization proposal boundaries remain independent.
The production order and final dependent cleanup are unchanged.

Tests combine nested redundant writes and agreeing read choices with required
calls, coercions, suspension, TDZ reads and arena permutations. The final effect
summary is checked independently of whether the outer store was folded; generated
source still undergoes independent execution. This removes duplicate expression
fact production and one mixed proposal/application path, not the legacy
InitializedRead census, global convergence or the planned revision coordinator.


## Constant propagation consumes shared read coverage (rules187)

Constants no longer calls PrivateCopy/CompletedStore. Its current RecoveryFlow and
all-read FlowLiveness feed AnalyzeFlowReadCoverage: for each binding, collect the
static definitions observed by reads and reject entry values without a reaching
definition. A proposed literal store must be the only observed definition; live
undefined initialization and other writes therefore block substitution through the
same facts. No original region, first-observation, same-block or branch-shape test
is used for this decision. Unreachable competing origins can conservatively block
coverage; this is a precision limit, not a language invariant.

The result summarizes static definitions, not dynamic execution identity. Constants
can use it because a primitive literal is the same value on each execution; copies
propagate only from those proved literals. Mutable/captured identities, effectful
producers and general snapshot forwarding do not gain this permission. The current
single-write candidate/lifetime representation remains explicit, and the analysis
does not authorize erasing observable initializers or binding cells.

Literal and copy propagation now fills a local proposal table and sparse worklist.
The recovery plan is unchanged until all proposals are resolved; a final batch
publishes literals and elides the corresponding private stores/empty declarations.
Later alias analysis consumes the newly committed projection. A branch that exits
before common uses can now fold a literal store using the same definition facts;
a branch that merely skips the store still preserves its undefined value.

The 175,616 small-graph independent forward oracle additionally checks coverage;
source execution covers abrupt branch completion, retained undefined/exception
paths, loops, copies, arena remapping and geometric sizes. CompletedStore remains
in Aliases and ForwardSnapshots; InitializedRead and canonical activation/IR
integration remain open. This is one actual migrated decision path, not retirement
of all legacy copy or initialization rules.


## Alias copies consume the same definition coverage (rules188)

AnalyzeBindingReadCoverage is the shared source-planning adapter for Constants
and Aliases. It associates common definition coverage with explicit store and
declaration identities; it does not infer them from statement position. Each
consumer obtains fresh facts for its current projection. Constants commits before
Aliases analyzes, so literal-elided reads/declarations cannot leave stale facts.

Aliases no longer calls CompletedStore/PrivateCopy, and declaration roots no
longer use initialized_first as their availability proof. A retained source must
have a single unescaped definition covering all its reads, including copy RHSs;
each removed copy must in turn cover every destination read. The existing
single-write representation and private-storage eligibility remain. A source
re-execution reaching a destination read without another copy would imply a path
from that source definition to the read bypassing the copy; joining it to the
entry path to the source contradicts the copy's coverage. Thus copy propagation
retains dynamic value identity without replaying allocations/calls. Missing entry
values, undefined initialization and competing writes prevent that proof.

Implicit parameter/catch/self initialization and mapped-arguments exclusions keep
their language contracts. Confined/RootLifetime and batched name resolution remain
independent legacy lifetime/name proofs; escaping cells are not scalarized. The
analysis neither moves a producer nor deletes its effects. Original declaration
values, NamedEvaluation and binding identity remain owned by their retained root.

A copy branch with an abrupt alternative now folds through a common later use;
a branch that can skip the copy and reach the use keeps its initial undefined.
Independent execution covers both outcomes, arbitrary primitive/object values,
throwing callbacks, copy chains and arena/symbol/scope permutations. Existing
loop/zero-iteration, capture, shadowing, await/yield and object-identity cases
remain acceptance gates. CompletedStore survives only in ForwardSnapshots;
InitializedRead, canonical activation, revisions and global convergence remain
open, as does migration of alias reference enumeration to the shared event list.


## Snapshot coverage and dependency order (rules189)

ForwardSnapshots now consumes AnalyzeBindingReadCoverage and the current flow's
typed read references. It no longer rediscovers binding reads in the original
expression census. CompletedStore and PrivateCopy have no remaining consumers
and are removed, together with unused declaration_controls, completion_order and
store_region records. Value availability comes from the common definition model;
single-use ownership comes from actual current read occurrences.

The coordinator runs demand/definition/binding cleanup before snapshot analysis.
A discarded initial read must disappear from that projection before coverage can
prove its later store. Snapshot proposals read one unchanged projection and are
applied in a separate batch. ForwardSnapshots reports actual changes; only such a
commit invalidates flow and liveness before Coalesce. Forwarding preserves each
RHS occurrence and removes its own single read/store/declaration, so it creates
no additional dead definition in another binding. Demand cleanup runs again only
for later storage/declaration changes. This is an explicit producer/consumer
dependency, not a fixed arbitrary pass count or a claim of global convergence.
Effects/store summaries are still published last.

An inert initial `saved;` before an effectful single-use assignment no longer
prevents restoring a direct return of the RHS. An observed initial value retains
its binding. Source tests exercise both with transparent scopes, arena reversal,
geometric sizes and independent receiver/effect/throw/identity checks; duplicate
execution mutants must still fail.

Motion itself still has the documented legacy effect-epoch/control-region and
lexical-environment proofs. They are not expanded and have not been replaced by
common effect/activation analysis. InitializedRead, canonical IR/activation,
canonical revision coordination, alias reference enumeration and whole-pipeline
convergence remain open. Removing CompletedStore does not retire these separate
obligations.


The local RunPass boundary now owns invalidation of all cached flow-dependent
facts. Constants, Aliases, dead definitions, snapshots, storage coalescing,
redundant stored results, inert statements and loop declaration projection each
report actual changes. On a change, RunPass destroys both flow and all-read
liveness after the pass returns; the next consumer obtains fresh facts. No-op
passes share const references to the same graph/liveness. Coverage borrows its
read-reference span only inside the pass, never across the commit boundary.
Every pass must finish analysis/proposals before writing; cache reuse within a
partially applied pass is forbidden. This remains a local scheduling contract,
not type-level immutability of every legacy plan member or a global pass graph.

A first version rebuilt analysis for every consumer and repeated demand cleanup
after forwarding. The existing record/class gates caught cumulative scratch
budget exhaustion. Removing the redundant cleanup alone did not fix it. Sharing
unchanged flow/liveness through the central commit boundary fixes that regression
without changing resource quotas, accounting, sample handling or recovery rules.
The dead-initial-read positive case also requires invalidation: stale pre-cleanup
reads would prevent forwarding. Existing storage projection tests continue to
exercise invalidation after storage identity changes.

A deliberate development mutant disabled RunPass invalidation. The existing
alias/snapshot composition (form 11, zero wrappers) rejected its stale references
at the forwarding assertion. The mutant was restored before the final build;
its log is /tmp/abcd-rule189-stale-mutant.log. This complements the independent
execution mutants for repeated effects and the positive/negative initial-read
cases; an unchanged real record alone would not prove the invalidation contract.


## Alias reference obligations use current flow (rules190)

Aliases now enumerates the shared coverage flow's typed read references for its
name/lifetime obligations. Each query retains the event's binding identity and
expression occurrence; it no longer scans original SymbolExpr syntax or recovers
the source identity from that syntax after name resolution. Name lookup still
uses the occurrence's lexical scope and RootLifetime still owns the documented
activation gap. Copy-chain blocking propagates through the same indexed queue.
This changes the owner of reference facts, not the single-definition requirement
or the independent name/lifetime proof. No new expression/operator admission is
added. Current flow/coverage/reference spans remain inside the same RunPass
analysis/proposal phase, before the alias batch is committed.

Independent source cases compose copies with typeof, both with ordinary scope
and an inner source-name shadow. Arena/symbol/scope remapping and geometric copy
chains exercise the same production path. Existing implicit/mapped arguments,
conditional/loop copies, captures, suspension and finally cases remain gates.
The remaining copy-RHS index maps a known elided write to its source occurrence;
it is not a second enumeration of reads or a value-availability proof.

The next initialization migration has a concrete dependency boundary:
CurrentFlow currently calls AnalyzeExpressions, which calls InitializedRead.
InitializedRead cannot start querying CurrentFlow without creating recursion.
A shared initialization producer must first separate semantic completion/TDZ
facts from effect-summary refinement; another local predicate or an unchecked
recursive cache lookup would not satisfy the common-model contract.


## Initialization facts before effect refinement (rules191, 2026-10-08)

InitializedRead and initialized_first/definition-region/read-region metadata are
removed. Expression completion summaries consume occurrence-indexed initialization
facts from the shared flow. The preparation owner first computes conservative
binding effects, builds the completion graph, and proves initialization through
AnalyzeFlowInitializations. It then recomputes expression summaries and refines
the same graph's binding effects. There is no AnalyzeExpressions/CurrentFlow
recursion and no semantic mutation during analysis. Function-self bindings now
have explicit body-entry definition events, alongside parameters and catch.

RefineRecoveryFlowEffects only removes proved-impossible throw edges and updates
write/discarded-effect annotations; events, definitions and reference identities
remain fixed. Positive completion-dominance proofs from the conservative graph
stay sound after edge removal. Negative results remain conservative, not evidence
of a real throwing path. Update read/coercion/write events remain separate and
coercion throws are retained. Demand and all-read liveness run after refinement.

RunPass now uses typed pass identities with preservation contracts declared once
at the dispatcher. Every changed pass discards event-indexed flow, dominance,
liveness and expression summaries. Dead/redundant write removal and inert-statement
omission preserve initialization of retained occurrences; they do not remove a
retained lexical initialization. Storage coalescing proves the retained root
initialized at every remapped reference, including preserved undefined writes,
before committing. These passes may retain positive ExprId initialization facts;
expression value/effect summaries are recomputed against the current storage map.
Constants, aliases, expression motion and loop-declaration projection invalidate
those facts too. Scheduling sites cannot independently select preservation.

The graph remains body-oriented: default-parameter programs are not represented.
Such absent occurrences receive no body-entry proof and retain their evaluation.
This is an explicit precision gap, not a new signature/function rejection. Full
activation/capture/unwind modeling and original IR correspondence remain open.
Native declaration selection and snapshot motion retain their documented legacy
lifetime/order obligations; common completion facts do not replace those proofs.

New source cases distinguish an early TDZ read/write from a later discarded read
after declaration. The early write still evaluates its RHS before failing; only
the later inert read disappears. Independent execution and deletion mutants cover
branch outcomes, injected throws, arena reversal and geometric statement counts.
The graph tests check effect-refinement identity, coercion throws, preservation
of positive initialization proofs, exact/short budgets, and fresh-versus-reused
graph equivalence after storage/declaration changes.

Initial implementations exhausted existing cumulative scratch quotas in the
record/class gates. The final design refines one graph, feeds CSR adjacency to
the existing LT algorithm without synthetic events, moves published graph buffers
instead of copying them, and reuses prepaid capacity after pass invalidation.
All semantic contents are rebuilt. Allocation growth/overlap stays charged and
no allowance is increased or refunded. These changes remove duplicate work and
storage; they do not establish a whole-process RSS claim.

## Revisioned commit boundary (rules192, 2026-10-08)

The variable-recovery passes no longer return manually maintained changed flags.
RunPass observes a preparation-local ProjectionRevision. Before the first semantic
plan write, BeginCommit advances that revision, applies the central preservation
contract, and invalidates dependent analyses. Later writes in the same batch do
not advance it again. A no-op retains both revision and cached analyses. The
scheduler uses the revision difference to request dependent cleanup.

A pass has separate analysis and commit phases. CurrentFlow, CurrentLiveness,
AnalyzeExpressions and ComputeExpressions reject analysis requests during commit,
before inspecting a cache. This is an internal ownership invariant, independent
of sample/opcode/block layout. Proposals and lexical obligations are computed
before the first write; applying the delta cannot ask for facts about a partially
modified projection. Loop declaration syntax metadata now participates in the
same change boundary. Derived output prefix counts remain publication metadata.

The boundary is integrated into constants, aliases, snapshot forwarding, dead
definitions, storage merging, stored-result elimination, inert statements and
loop declaration projection. It retires after-the-fact invalidation and per-pass
changed bookkeeping. Plan fields are still ordinary private C++ members: compile-
time enforcement of every future write, externally persistent analysis handles,
and a global fixed-point pass dependency graph are not claimed. Original IR,
activation, motion and declaration-proof migrations remain required.


## Discarded operands belong to recovery passes (rules193, 2026-10-08)

ElideInertOperands replaces a comma expression by its right value when the
shared completion summary proves its left inert. It computes a batch against
one projection, then commits through the revision/invalidation boundary. It
retains positive initialization facts: discarded inert evaluation neither writes
nor throws nor changes activation. GetValue and suppressed NamedEvaluation are
part of the replacement contract, not inferred from emitted text.

The pass runs after stored-result elimination and before dependent demand,
snapshot or post-storage/declaration cleanup. Final demand can expose newly
inert operands, so a final normalization precedes publication. These are explicit
dependencies in the existing pipeline, not arbitrary repeated rounds or a claim
of global convergence. Proposal storage is bounded by unprojected sequences;
traversals are linear in expression count per scheduled invocation.

Previously the printer alone removed these operands, while liveness still saw
their reads. A private state write feeding only discarded comma operands could
therefore survive as an unnecessary assignment. Now those references leave the
same projected RecoveryFlow used by dead-definition analysis. Required RHS
calls remain at their original position when the resulting dead write is elided.

The printer's effect-based sequence decision is removed. It consumes the shared
replacement and only maps the removed left subtree's original ranges to the
normalized value span. The existing value-result path retains receiver, typeof
GetValue and function/class naming behavior. Class-name ownership also follows
the same replacement. Original expression identities remain available for source
mapping; this does not recreate discarded execution.

The new regression combines discarded reads with later dead-write removal,
nested comma operands, reversed arenas and 0/200/201/400/800 scale. A coercing
unary operand is the adverse semantic case: Number conversion, object hooks and
BigInt/Symbol failures must remain. Independent execution compares traces,
returns and throws; mutants remove required RHS work or coercion. Existing
sequence/TDZ/receiver/naming/suspension tests remain acceptance evidence.


## Snapshot observations consume flow events (rules194, 2026-10-08)

RecoveryFlow events now publish their intrinsic ExpressionEvaluation. The
semantic adapter assigns it while building operations and refines it after
initialization proofs, including events outside discarded statement regions.
Binding reads and successful writes retain their separate identity and completion;
an inert GetBindingValue still observes state and is not interchangeable with
an absence of reads. Update read, coercion and successful write stay distinct.

Snapshot recovery collects expression observations from current flow reads,
expression-write definitions and required intrinsic events. The legacy completion
order assigns epochs to this set. The original syntax/statement-omission/literal
query is deleted, as are its expression-to-statement table and current-statement
field. Eliminated comma operands cannot create ghost observations after their
reads have left the common graph. There is no second original-expression
admission filter for the retained observation set.

This replaces the observation producer, not the whole motion proof. Control
prefixes, region identity, lexical-environment signatures and source lifetime
obligations remain documented transitional checks. Flow scope events do not yet
model all abrupt activation/unwind, so this change does not authorize removing
those obligations or moving across unrelated handlers/loop activations.

The new composition cases put discarded parameter reads or required member calls
inside nested return operands after a snapshot store. Only the discarded-read
case forwards; member effects and exceptions keep their order. Existing argument
value observations, catch reads, shadowing, async/generator and receiver tests
remain. Coverage includes reversed statement arenas, transparent block splitting,
0/1/200/201/400/800 nesting, independent execution and duplicate-evaluation mutants.
Flow tests assert that initialized reads can be intrinsically inert while still
being reads, coercion remains required, and reused graphs preserve effect facts.


## Universal observation order replaces linear motion counters (rules195)

Snapshot motion now consumes AnalyzeFlowMotion. From the successful source
write's event, every completion path must encounter the same destination read
before any other semantic observation or terminal sink. The solver stops at
reads, definitions and required intrinsic effects. It includes every completion
edge and cannot turn may-reach into must-execute: a transparent cycle stays
unresolved, even if it also has an exit to the destination.

The old expression effect epochs and control-statement prefix counts are removed,
including ObservationSite, control_events_ and their construction. Definition and
read FlowPoints come from the same current graph used by coverage. Candidate
queries do not walk paths, depend on arena order or match a statement sequence.
Shared semantics now mark debugger as a required event; no source-level special
case is introduced in the motion consumer.

The remaining expression region identity and lexical-environment/lifetime proof
are explicitly transitional obligations for handler/activation correspondence.
The new order proof does not license changing the handler of a moved throwing
producer. Region equality is still a conservative implementation restriction,
not a language invariant; retiring it needs explicit shared handler/unwind facts.
Global pass convergence and original IR correspondence also remain open.

A new source case crosses a transparent control split and join, which the old
control prefix check rejected. Both outcomes converge before the same read;
required conditions/effects, bypassing returns, debugger, catch and loop cases
remain adverse checks. Reversed arenas and transparent block variants use the
same production path. The graph-level exhaustive oracle and geometric tests are
documented in algorithms.md; no sample/operation-layout branch was added.


## Snapshot region identity replaced by shared obligations (rules196)

Forward next-observation alone does not prove equal execution counts: a read
inside a loop can execute repeatedly after one source write. Snapshot recovery
now requires both next[write]=read and previous[read]=write. The latter uses
invocation-reachable predecessor histories and an explicit entry boundary;
unreachable structural loop exits cannot fabricate predecessors for an actual
read. The bidirectional solver shares one implementation and bounded scratch.

FlowReference now carries an optional FlowExceptionContext containing the actual
throw destination and pending finalizer route. The semantic builder attaches
this context to reads and writes even when initialization refinement proves the
reference itself nonthrowing. The moved producer may throw or resume abruptly,
so recovery compares these shared contexts separately from observation pairing.
They are graph-local semantic identities, not region numbers or PC layouts.
Refinement and buffer reuse retain the reference context.

The expression_regions_ table, region counters, OpenRegion and guarded/parent-
region tracking in both syntax walks are removed. No same-region admission
remains in snapshot recovery. Lexical environment/name/lifetime and resource-
observable creation obligations are still separate. This does not claim complete
closure activation/default-parameter/finally reconstruction or original-IR
correspondence, and does not establish global pass convergence.

New tests distinguish a loop that immediately returns its snapshot from a loop
that observes it in multiple iterations. Only the former can move a once-only
producer inside. Removing backward pairing deliberately fails the repeated-loop
case; removing exception-context equality deliberately fails the existing
outside-catch case. Independent execution also checks duplicate evaluation.
These are separate semantic counterexamples, not new production shape branches.


## Loop declaration projection has a read-only proposal boundary (rules197)

ProjectLoopDeclarations previously selected a loop's grammar suffix and updated
the shared projection before examining later loops. It made no flow queries
after committing, but still read and wrote the same plan during decision making.
PlanLoopDeclarations now receives const LoopSyntax/BindingInitializers and the
existing declaration identities, and returns typed start/demotion/declarator
edits. It cannot mutate the projection. The caller completes all planning and
budget accounting, enters BeginCommit once, and applies only these edits.

Disjoint initializer ownership makes the edits independent of loop arena and
application order. A retained expression prefix remains at its occurrence; the
representable suffix becomes for-let. No liveness, initialization, name lookup
or lifetime is re-inferred by this grammar pass. Prefix counts are derived only
from the final projection, removing the unused mid-pass recomputation.

The isolated batch tests cover multiple loops, reversed ownership order,
application order, idempotence, exact/short budgets and 200/201/geometric growth.
Existing generated-source tests retain nested loops, catch/finally, await/yield,
copy/storage cleanup and initialization fault comparisons. This closes this
pass's partial-plan decision path, not global dependency fixed-point scheduling.


## Copy propagation consumes normal-result identity (rules198)

Aliases no longer admits only an original SymbolExpr RHS. It consumes the
shared expression normal-result binding fact already composed through sequence,
assignment, choice and logical results. Reads identify their binding even when
it is readonly; mutability remains a separate prerequisite for eliminating a
PutValue. The existing root/definition coverage, mapped-arguments, escape,
lexical visibility and activation obligations still apply. Unknown results
remain unknown; no producer list or sample condition replaces these proofs.

Removing a copy storage and dropping its RHS are separate decisions. A required
RHS remains at the original occurrence via assignment-to-RHS projection, with
its evaluation, exceptions and observed value preserved. Only a RHS proved inert
by shared effect/completion analysis permits removal of the whole native
initializer statement. Both decisions are proposed before BeginCommit; no
expression facts or flow references are consumed after that boundary.

For lexical retention, an eliminated inert statement can contain several
references. The current FlowEvent discarded owner identifies those references;
we no longer recognize only the original direct RHS ExprId. A sparse adjacency
list records every destination-to-required-source dependency and a bounded
worklist propagates retained storage. The previous one-source slot is removed.
This handles composition without repeated whole-pass sweeps. It is not a global
fixed-point scheduler, and later effect simplifications can still expose further
copy opportunities; that precision gap must not be filled with local patterns.

Tests cover effectful sequence/choice/logical copies, immutable const sources,
changed source values, observed initial undefined, catch reads before successful
write, and disagreeing branch results. They compare generated code independently
with side effects, identity and injected exceptions, including reversed arenas
and 0/1/200/201/400/800 composition sizes.

A combined scope test keeps a conditional copy with two upstream sources whose
original names resolve to TDZ shadows at the retained occurrence. Both source
storages must survive; independently dropping either introduces a ReferenceError.
This checks the multiple retention edges rather than only separate name cases.


## Value use is a shared flow fact (rules199)

RecoveryFlow now records FlowValueUse for each current expression occurrence:
observed, discarded or unmodeled. The semantic builder transports parent use
through replacement, conditional arms and logical RHS; branch predicates and
actual operation operands observe their values. Sequence discards its left
result and transports parent use to its right. Expression statements discard
their result; return and declaration values remain observed. Signature roots
not modeled by this body graph stay unmodeled, never implicitly discarded.

ElideInertOperands consumes this fact plus the shared completion/effect summary
to remove an inert right operand when a sequence's result is discarded. This
cleans up required RHS evaluation left after compound copy elimination without
dropping calls or changing observed results. Its complete proposal stores both
source and replacement identities before BeginCommit; the printer only consumes
the plan. Flow acquisition is lazy when a right-elision candidate exists.

The flow-owned value-use buffer is reset for every reconstruction and retained
by effect refinement. It shares normal cache invalidation; consumers do not
infer value use from their own parent syntax walks. This extends the common
semantic adapter, not the original IR correspondence or whole-pass convergence
claim. Tests include discarded and returned sequences, conditional arms, both
logical operand roles, coercion/throw effects, reversed arenas and geometric
composition. Inner comma-left values are discarded even when the outer result
is observed; no fixed nesting-depth rule is used.

Source-range transport also follows the selected replacement. The printer's
former left-only sequence-origin action is replaced by an operand-origin action
carrying the omitted ExprId. It only transports ranges after emission; it does
not decide value demand or effects. Existing initializer range assertions caught
the missing right-side mapping, and new discarded-result tests now check every
original expression's nonempty in-bounds range too.
