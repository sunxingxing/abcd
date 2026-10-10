# 统一变量恢复：共同语义流与分离的恢复计划

2026-10-07用户明确设计；这是当前架构任务，不是已完成报告。
规则172/f656e763是迁移基线。范围为普通函数临时变量与循环携带值，
不推倒重写解码、IR、结构恢复或验证链，不扩大语料/恢复暂停事项。

## 规则254：当前计划与试算共享事实提供者

`PrepareRecoveryProjection` 对传入的完整表达式投射执行共同闭环：
保守 `AnalyzeRecoveryExpressions` -> `BuildRecoveryFlow` ->
所有 completion 边上的 `AnalyzeFlowInitializations` -> 当前 occurrence
初始化与效果 refinement。正常值域、成功写回、primitive identity、
词法依赖与 flow 来自同一候选。当前 pass 的 `CurrentFlow` 以及快照
移动试算均实际消费该入口；试算不接收当前计划的 ExpressionFacts。

原始值域只描述原 occurrence 的 normal result；replacement 取实际
producer 的 facts，transport 取实际 primitive。表达式事实保持纯分析，
不写 BindingInitializers。投射依赖按迭代三色 DFS 的完成顺序计算：
source child edges 加 replacement edges，transport 切断旧 RHS 依赖；
不得依赖 expression arena 或原 canonical finish 列表排列。循环替换
无法表示有限表达式，返回 typed ownership error，不是形状准入限制。
该分析 O(X + D)，其中 X 是表达式数，D 是子节点及替换依赖数；
复用现有 language child adapter，无新依赖或第二张语义表。
共同 bootstrap 在每个阶段扣除全部同时存活的保守占用；conservative
expression facts 和 initialization/dominance scratch 在各阶段边界释放。
报告 admitted peak，不把已释放 scratch 累加成后续打印的持续占用；
work 仍累计全部阶段。ProjectionFacts 记录自己产生的缓存 admission；
commit 销毁缓存及 occurrence proof 后释放其占用，peak 和 work 不回退。
旧 flow buffer 复用暂退，避免把继续持有的容量当作已释放；未来若复用
必须明确接入独立容量所有权。规则276起，pass 的 normal-result 查询
也只消费共同 completed bootstrap；保守中间表达式只存在于提供者内部，
不在同一个 revision 下先发布再替换。整个共同产品仍按需建立一次。
固定 record 和800组变量暴露该成本回归并验证修复，预算未
放宽。该局部资源证明不能代替整个进程 RSS 或全 pass 成本验收。

每次 commit 使 flow、表达式事实及 occurrence 初始化证明一并失效。
旧 `RecoverySchedule::TransportInitializations`、`preserves_initialization`
标记以及 Initializers 手工转运路径已删除；下一投射重新证明初始化。
调度阶段与 demand/snapshot consumer checkpoint 保留各自职责，不能把
重新分析等同于多轮 pass 收敛。版本守卫禁止读取提交中的不完整批次。

单元反例使用 synthetic analysis inputs，不冒充完整原始 IR 的证明。
未知 producer 替代 Number read 时，不沿用旧 Number 域；删除声明
候选不沿用旧 initialized proof；反转 arena/根列表、200/201/400/800、
exact/one-short resource/work、循环投射与 raw fallback 都验证共同入口。
整个旧有有限源码执行集继续检查生产输出；全 suite/扩展 corpus/RSS/
benchmark/DFX/agents 暂停不变。

此子步关闭的是试算消费当前 ExpressionFacts 的决策路径，未关闭全部
试算交付：原始 operation/region/protocol 多对多映射、普通 local/catch
activation identity、完整候选声明/生命期 delta、最终值事实及依赖驱动
pass 重跑/成本仍须完成。因此五个主干交付均保持开放，继续原始模型。

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

规则252接入了这个入口的第一段实际消费：最终 TreeFlow 共享拥有
OriginalFlow 的原始定义、使用链、CFG、exception exits/phases/origins 和
normal/before_throw/exceptional operation 事实。OperandCheck 的同一次核对
发布 raw/resolved operand、UseId（原展开不存在单一 use 时明确缺失）与
ExprId 的关系；同一个表达式的多个 normal value 域保守 join，正常结果不
冒充 throwing completion 的值。控制操作的 predicate/return operand 映射
与 result 区分，缺失 operation-expression 不伪造一个源码节点。

SourceVariables 及各实际生产准备入口传递同一原始输入，exact revision
ExpressionFacts 基于它生成当前 normal domains；literal/replacement/transport
消费实际 producer 的域。源码 flow 关联原始 input ranges 与当前 normal
continuations，并消费这些共同 domains。只有一个穷尽语言 effects 表；
缺域的 IntrinsicEvaluation 是该表的保守调用，不保留平行语义分支。
正常域不能给声明初始化、值相等、binding/cell identity 或 motion permission
提供替代证明；所有 child GetValue、normal write 与 throw/resume 义务独立。
原始 VM flags 不是规范化源码事件的逐一 effects 证书，不能直接覆盖语言表。

原始输入必须属于同一 unchanged checked tree；维度/identity/revision 检查
不是重新验证可变 AST 的证书。规范 AST 变换必须重新验证并替换输入；
源码计划的值保持投射则在 exact revision 下重新分析实际 producer。
共享拥有原始输入，分析期借用，避免每次重建 source flow 复制原始 graph。
直接/partial tree 的原始输入缺失保持显式，不能伪装成原始对应。
原始 operation/reference/phase 的完整多对多投射仍开放；Task1 和整个
五项交付主干均不能因这段值域/使用点贯通而关闭。

规则253将普通 public property Reference 加入同一源码事件适配器：typed
acquisition/receiver/key/access、normal continuation、throw/finalizer context、
current owner 与原始 base/key operand ranges。取得引用保存输入值，赋值 RHS
在 PutValue 之前；receiver、key 转换与 Get/Set 的效果、异常按2025 source
语义分开。正常结果不在 receiver/key 失败路径上提交。原始 ranges 是来源，
不能直接把整个 VM before_throw flags 当作每个 native phase 的精确效果。
移动、活跃性、需求与试算共同消费此图；初始化后的效果 refinement 保留
独立阶段，不用整个赋值的 normal-store 总结抹去 key/receiver 异常。原 generic
children→single-effect 的 member/target 决策路径退出此范围。
已记录2024/2025差异和当前 native runtime 对照，见
[source reference phases](../../reference/source-property-references.md)。
完整原始 operation/region/protocol/phase 多对多对应及五项主干仍开放。

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

规则215补充：共同表达式事实包含 LexicalDependency。当前替换转发
所选摘要，普通组合合并子摘要；未建模的隐式名字/闭包环境需要保持
enclosing environment，显式binding依赖用当前流图的读写引用表示。
跨环境快照候选批量解析目的作用域，另证已恢复声明的RootLifetime；
两者分别证明名字身份和实际词法生命期，不因环境不同直接拒绝所有
表达式，也不忽略环境捕获。整语句候选保证每个当前引用最多归属一项
运输提案，O(statements+bindings+references)附加索引和扫描；解析复用
共同scope索引。所有查询在提交前完成，不消费部分更新的计划。
V1可观察资源合同仍保留原环境边界，因为环境激活的资源事件尚未
进入共同效果图；不能从显式名字不变推出该资源顺序等价。隐式依赖
精化和完整绑定生命期仍开放，不扩展原资源/异常合同。

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


## Projection-owned analysis lifetime (2026-10-08, rules215 unchanged)

ExpressionFacts, RecoveryFlow, FlowLiveness and event-indexed FlowInitializations
now live in one private ProjectionFacts aggregate tagged with ProjectionRevision.
CurrentFacts checks the analysis/commit phase and revision before any cache is
read. BeginCommit retires the complete aggregate, preserving only reusable flow
capacity; it no longer names four independent caches to invalidate. A future
projection analysis belongs in this aggregate, not beside it. No-op passes retain
the snapshot and lazy construction remains unchanged.

The positive ExprId initialization facts remain separate because their transport
is an explicit per-pass semantic contract. This change does not grant additional
preservation to any pass. Bootstrap positive initialization facts remain valid after effect refinement
removes impossible exceptional edges. CurrentInitializations owns lazy construction
for later consumers; Coalesce no longer constructs or publishes that analysis itself.
Rule216 below separates bootstrap dominance from the final graph cache.

This removes per-cache reset bookkeeping and the storage pass's analysis-cache
ownership. It does not change scheduling or claim arbitrary pass reordering is
valid: declaration recovery, motion, coalescing and final declaration spelling
still consume the ordered prerequisites documented above. Raw references cannot
outlive the analysis phase; this private aggregate is not a persistent checked
handle API. Global fixed-point scheduling and full binding lifecycle remain open.
No new graph walk, input admission condition, or source recovery rule is added.


## Bootstrap proofs versus current dominance (rules216, 2026-10-08)

CurrentFlow uses conservative initialization dominance only to derive positive
ExprId initialization facts for effect refinement. That temporary dominance
result is now local and never installed as ProjectionFacts.initializations.
CurrentInitializations lazily computes dominance on the refined current flow.
An impossible exception edge can bypass an initialization in the provisional
graph: removing it preserves positive proofs but can change a negative answer
to positive. Same event numbering and projection revision are insufficient to
claim the provisional dominance describes the final graph exactly.

The production change adds no convergence loop or local recovery condition.
A storage consumer that needs initialization order pays one fresh LT dominance
analysis of the current graph; consumers that do not need it do not compute it.
The old bootstrap result is destroyed before later passes, rather than retained
beside the exact graph result. Existing allocation/work accounting applies.
The positive ExprId transport contract across commits is unchanged. Further
iterated effect precision and full binding lifecycle remain separate gaps.


## Explicit free-name references (rules217, 2026-10-08)

RecoveryFlow.names records GlobalNameExpr observations with typed NameId,
FlowPoint, original ExprId and current statement owner. Construction follows the
same replacement and operand context as binding references; effect refinement
keeps these occurrences, and buffer reuse clears prior identities. They are
lexical name observations, not local bindings with definitions or storage.

IntrinsicLexicalDependency now treats an explicit GlobalNameExpr as a modeled
reference. ForwardSnapshots groups free-name queries by current producer owner
and checks the actual destination scope before commit. ResolveTreeNames reuses
the same indexed scope traversal and active spelling model as ResolveTreeReferences.
A local declaration shadows a free name even before initialization; an unrelated
binding does not. Getter/read effects, exception route and exactly-once motion
are still proved independently. Name availability alone cannot elide evaluation.

The additional producer/name scan is O(S+G); scope resolution uses indexed scope
adjacency and one traversal for the name batch, O(B log B+S+G log B), plus spelling
comparison costs. There is no per-name ancestor or per-candidate expression walk.
Closures, classes and implicit helper names retain their enclosing-environment
summary until their dependencies enter this shared model. V1 observable activation
resources keep the existing motion boundary. Full binding lifecycle remains open.


## Callable ownership versus block environments (rules218, 2026-10-08)

LexicalDependency distinguishes kCallableOwner from kEnclosingEnvironment.
ThisExpr carries FunctionOwnerId, which VerifyTreeStructure already checks against
the function owner and invocation form. Local projection and snapshot forwarding
operate inside that one checked FunctionTree; child callables have separate trees.
Ordinary block bindings therefore cannot change a ThisExpr receiver identity.
The common expression postorder propagates callable ownership through operands,
while an enclosing-environment dependency still dominates the composed summary.

ForwardSnapshots consumes this existing owner proof and retires the unconditional
local-environment identity gate for receiver reads. This does not claim this is
initialized: the existing evaluation facts preserve derived-constructor and arrow
exceptions, and the shared motion/handler proofs preserve their ordering. No
receiver expression is replaced with a parameter or moved across a callable.
Observable activation resource requirements and unmodeled closure/class/helper
dependencies remain. No new scan, graph walk, opcode or shape condition is added.


## Storage consumes early copies; late copies consume cleaned storage (rules219, 2026-10-08)

The ordered schedule now has an explicit post-storage alias consumer:

`Storage → StoredResults → InertOperands → InertStatements → Aliases →
StoredResults → InertOperands → InertStatements → LoopDeclarations`.

The earlier Aliases stage still prepares storage affinities. Coalescing can turn
multiple writes into identity copies; their independent removal can expose a
single immutable definition. The later Aliases stage consumes fresh retained
coverage and normal-result facts for that definition. It does not merge storage,
remove effects, change observable source/captured bindings, or infer declaration
syntax. It uses the existing analysis/proposal/commit boundary: BeginCommit
invalidates the entire revision-tagged ProjectionFacts aggregate and positive
initialization transport for alias substitution. Each subsequent consumer obtains
facts for the committed projection, including fresh flow and initialization.

BindingInitializers::Storage and FlowBuilder::Resolve compose the same bounded
mapping: early direct value root, direct storage slot, then the late direct value
root of that slot. The alias solver publishes terminal roots from its sparse
worklist; the storage solver publishes disjoint direct slots. No coalescing stage
runs after late aliases. This stage contract gives O(1) identity lookup without
per-reference alias/storage chain walks. Adding another storage phase would
require revisiting this contract; arbitrary alternating mappings are not supported.

Aliases now resolves source spelling and narrowed declaration lifetime from
FlowReference.owner, the current execution statement. Original ExprId remains
source evidence, not a lexical location after snapshot forwarding. The original
expression_scopes_ array and RootLifetime(SymbolId, ExprId) overload are removed
from production. Coverage, spelling and lifetime all consume occurrences from the
same current flow. The owner presence check asserts an internal occurrence
invariant for eligible body-private reads; it is not a same-block input limit.
Full original-IR correspondence and binding lifecycle analysis remain open.

New tests compose storage merging with late immutable copies, and forwarding into
a scope that shadows the parameter root before its initialization. The former
eliminates both temporary bindings; the latter retains the necessary slot because
substituting the parameter spelling would enter the shadowing binding's TDZ.
Independent execution checks callback observations/throws and object identity;
wrong value deletion and TDZ substitution are detected. Variants reverse the
statement arena and add 0/1/200/201/400/800 empty blocks; existing loop, exception,
capture, suspension and live undefined cases continue to run. One additional
producer/consumer stage has constant sweep count and uses the existing budgeted,
indexed analyses; it is not global fixed-point convergence or a per-combination
production branch. Transitive coalescing and finer lifecycle precision remain open.


The existing long-copy-chain test now checks whether its last immutable alias
selects the original value root, rather than whether any alias exists. Under a
shadowing scope, coalescing may retain another visible copy slot and the late
phase may safely propagate into that slot while still rejecting the original
spelling. This is a semantic assertion change, with no corresponding production
condition. The full existing independent source behavior comparison is retained.


## Complete interference and group contraction (rules220, 2026-10-08)

FindBindingInterference now publishes every compiler-temporary conflict derived
from current shared live-in facts and successful definitions, including pairs
without a copy affinity. Affinities remain proposals; their interference flags
are derived from the same sorted conflict graph. Recording only affinity-pair
conflicts is insufficient for transitive coalescing: two accepted edges can join
endpoints whose values must remain distinct. The shared two-binding exhaustive
oracle checks complete conflicts even on graphs without any affinity.

PlanStorageCoalescing independently contracts nonadjacent affinity groups. It
maintains symmetric indexed conflict adjacency and a DSU balanced by group size.
A group has no internal interference; each neighbor rewrite removes the old root
from both endpoints and adds the surviving root. Printed representative selection
is separate from DSU balancing. The result is a direct slot for every removed
member, with no chained maps or extra storage passes. Coalesce no longer owns a
paired bitmap or admits only disjoint pairs.

Spelling preference comes from the shared TreeScopeCheck spelling index, not an
optimizer-specific comparison or a named sample. A binding with a unique spelling
is preferred; native declarations and earlier current initialization dominator
positions follow. This is a quality preference only: duplicate spellings remain
eligible and can merge. Actual name resolution, current reference ownership,
completion-before-remap and narrowed source declaration lifetime still decide
which members can commit. A group can retain any subset independently, because
all of its members were mutually noninterfering before source restrictions. Live
empty initialization is separately expressed by PlanStorageInitializations.

The existing schedule is unchanged: snapshot commit precedes fresh storage facts,
and identity/inert cleanup precedes the late alias stage. Group contraction does
not delete stores, drop RHS effects, propagate immutable values or recover source
syntax. BeginCommit still invalidates one ProjectionFacts aggregate before later
consumers. Conservative finally, escaping cells and original-IR correspondence
remain independent precision gaps; no fixed-point scheduling is introduced.

The small dense reference planner scans cross-group member pairs and rewrites
all labels, independently of DSU/indexed adjacency. 20,480 four-binding graph and
preference cases include eligibility, native ordering, reversed initialization
ranks, duplicate spelling and conflicts absent from affinity edges. Long source
chains retain each callback, ++ conversion and successful writeback. Their
non-affinity first/last conflict preserves two slots; without that observation
one slot suffices. Symbol and statement arena reversal plus 1/2/200/201/
400/800 chains exercise recovery and geometric work growth. Wrong full contraction
and dropped update are detected by independent execution. Existing shadowing
chains also require at most one retained copy, rather than allowing the new group
representative to increase mechanical declarations.


The actual PasteBoard nested type loop removes one private success-state carrier
and its update-program copy. A new paired callback test generates the ABC-only
record before reading the TS reference, then compares only that async callback.
Mixed records/types, getter reads, type-array proxies, length coercion, record and
system promise rejection and every-effect-point exceptions preserve traces and
completion. Deleting the success write or its record-data await is detected.
This advances one real callback's semantic/style evidence; the whole record and
platform execution are not accepted by that test. Final coverage is recorded in
status.md; paused corpus/RSS measurements are not resumed.


## Native initialized values share recovery passes (rules221)

A lexical DeclarationStmt initializer is a value definition with its RHS, copy
identity, successful completion, statement write site, current owner and exception
route. Previously the adapter omitted its copy/reference facts and consumers
assumed every covered definition had an AssignmentExpr. The common coverage,
liveness, initialization dominance and motion analyses now cover both sites;
constant/alias and snapshot consumers use the shared RHS value. Private storage
eligibility includes already initialized compiler declarations; source bindings
and captured cells keep their original semantic identity.

DeclarationValue is an explicit committed statement projection. Removing a
binding retains RHS GetValue at its original occurrence; coalescing instead
retains an assignment to the proved initialized writable root. The subsequent
StoredResults pass independently removes identity writes, and InertStatements
independently removes inert discarded values. RHS calls, coercions, throws,
allocations and identities cannot disappear just because storage disappears.
A retained assignment observes its RHS value even though its enclosing statement
discards its result. Only store elision changes that value-use fact; subsequent
comma cleanup therefore cannot drop an assigned constant. Removed initializer
origins map to explicit empty boundaries, while forwarded RHS origins stay at
their real destination. The printer and name preflight consume the same projection; actual destinations
use existing common NamedEvaluation envelopes and resource contracts.

The schedule remains StoredResults/demand cleanup -> declaration placement ->
constants -> aliases -> demand cleanup -> snapshots -> storage -> independent
identity/inert cleanup -> late aliases -> loop declarations -> final cleanup and
declaration kinds. Every new projection goes through BeginCommit, invalidating
the whole revision-tagged ProjectionFacts before another pass can analyze it.
No analysis is read after a batch enters commit. No additional fixed-count
optimization sweep or local graph/layout admission was added.

Storage merging independently proves completion of both the original binding
and retained slot at every remapped reference. Otherwise remapping an original
TDZ read/write could make it succeed merely because the root was initialized.
Const storage is excluded through existing MutableBinding semantics, while its
immutable values can still propagate after common coverage. Already initialized
declarations are excluded only from the grammar action that would initialize
that same binding a second time; this is not a recovery admission restriction.

The original adapter, partial binding lifetime and capture/unwind model remain
migration gaps. Dead-definition removal still preserves original lexical
initialization; arbitrary declaration relocation, global pass convergence and
full finally/capture recovery are not claimed. Tests compare two independently
built owned syntax encodings, branch/loop combinations, throwing RHS/state,
TDZ reads/writes, source const/local identity, object mutation, and reversed
expression/statement/symbol arenas. Test fixture shapes do not define production
semantics; independent JS execution and adverse mutants supply behavior evidence.


## Independent binding retention and versioned demand (rules222)

The evaluation region of a native lexical initializer has a typed initializer
binding owner. Its private successful initialization is distinct from RHS effects
and RHS value demand. Value-demand analysis may prove the value dead, but cannot
remove the lexical completion. DeadDefinitions retains initializer regions and
only commits its own value-store/evaluation edits. The separate Bindings pass
then consumes the committed projection and shared FlowDemand, before declaration
placement, snapshots or storage coalescing. Final dependent cleanup follows the
same order. The former PlanBindingElisions proposal-on-pending-delta path is gone.

FlowDemand is now part of revision-tagged ProjectionFacts, alongside expression,
initialization, flow and liveness facts. BeginCommit destroys the aggregate before
any plan mutation; analysis cannot run during a commit. Each pass owns its proposal
and copies no event-indexed facts across that boundary. Unchanged inputs reuse
immutable facts. Removing an unused private binding preserves positive occurrence
initialization proofs for all remaining references: none refers to that binding,
and retained RHS GetValue stays at the original site and handler. This contract
is explicit in the typed pass invalidation switch.

Binding retention roots observable/captured/source identities and all surviving
reads or non-initialization writes, including unreachable printed syntax. An
inert unused initializer's read instead creates an owner-binding -> read-binding
dependency. A surviving owner retains those names even if its initializer value
is dead. Sparse adjacency and a monotone worklist close those dependencies once
per binding/edge, so deleting a provider cannot leave a dangling name. This is
separate from value propagation and storage interference. It does not inspect
names, instruction sequences, producer types, block identity or adjacency.

An unused private native binding disappears, retaining any required RHS GetValue;
an entirely inert unused initializer disappears with it. The same rule covers
original DeclarationStmt and a committed lexical initializer projection. Reads
that can throw through TDZ remain required and retain their binding. Source and
captured identities and resource-observable naming contracts retain the original
representation. A still-live binding conservatively keeps its dead initializer
value and the dependencies that printing it requires; independently pruning that
value while preserving initialization completion is a future precision task.
Rules224 below close that dead-initial-value projection gap for private bindings.
Arbitrary lifetime movement, full abrupt scope unwind/capture identity, original
IR correspondence and global pass convergence remain open.

Independent dense transitive closure checks all 65,536 three-binding graph/root/
effect/layout combinations, including cycles. 200/201/400/800 chains verify work
and exact/short typed budgets. Source tests compose unused calls, live-binding
initialization dependencies, pre-initialization self reads, overwrites and loop
writes with two initializer encodings and reversed arenas. Independent execution
and intentionally removed calls/providers/TDZ effects check the semantic contract;
final selected regression and actual record evidence are recorded in status.md.


## Statement and expression writes share dead-definition projection (rules223)

FlowDefinition::WriteSite exposes a typed FlowReferenceSite from the semantic
adapter's expression/statement positions. Flow references and dead-definition
proposals consume this common site; the dead-write commit no longer extracts an
AssignmentExpr from the original tree. Each owned proposal carries its original
RHS value, or absence for a valueless shared-slot write. No source syntax or opcode
sequence is re-matched after another pass has committed storage projection.

After coalescing, a native initialized declaration can become DeclarationValue
with a successful assignment to an already initialized slot. That write may be
dead even though its RHS call is required. Previously only ExprId writes entered
PlanDefinitionElisions, so this statement assignment retained unnecessary storage.
The shared demand/effect rule now covers assignment and UndefinedStore regardless
of site representation. Native lexical initialization, updates and observable
writes retain their independent obligations. Required RHS GetValue stays at the
same occurrence, in the same handler; a dead undefined slot write has no RHS and
can disappear. Binding retention remains a separate pass over the resulting
committed flow. There is no new sweep, layout admission or naming heuristic.

Fixtures compare split stores and native declarations, arena reversal, long
transparent block programs, repeated calls and a handler that observes the old
slot if a later call/report throws. Dropping the first call and prematurely
sharing an exception-live old value are detected independently. Observation
normalizes the identities of two distinct object tokens instead of treating
empty objects as interchangeable under deep equality. Existing dense demand
oracles retain their independent result checks, and Cartesian site/kind/effect/
demand tests protect lexical completion and update obligations.

This closes dead assignment projection after shared native initialization, not
pruning the initializer value of a still-live lexical binding. That precision
gap is closed by rules224 below; full binding lifetime/capture/unwind, original-IR correspondence and global
pass convergence remain explicit. Final selected verification and actual record
scope are recorded in status.md.

## Dead initial values retain evaluation and lexical completion (rules224)

The independent InitialValues pass consumes the current RecoveryFlow and cached
FlowDemand after DeadDefinitions and before Bindings. A private inert successful
initializer whose value has no demand becomes a DeclarationCompletion: optional
discarded RHS evaluation, followed on its normal path by Initialize(undefined).
The RHS keeps its original position, execution count, GetValue and exception
route. Throws do not initialize the binding. Removing its value does not remove
the lexical completion, binding identity or TDZ. Required self reads retain their
original initialization boundary; an entirely inert dead value needs no RHS.

DeclarationCompletion distinguishes lexical initialization from AssignUndefined
to an already initialized shared slot. RecoveryFlow publishes both explicitly;
DiscardedEvaluation owns the independent RHS occurrence and FlowBinding publishes
the selected declaration and initialization kind. A supplied native declaration
remains authoritative even after its value is pruned; selecting the old empty
placeholder by definition arena order would give incorrect completion dominance.
Declaration placement now consumes that shared kind, retiring the original-tree
DeclarationStmt.initializer tests in its candidate/placement paths. All existing
coverage, dominance, scope, lifetime and exposure obligations remain.

Binding removal, declaration movement and coalescing consume the same projected
completion. Each can remove the completion while retaining the necessary RHS as
DeclarationValue without a target. A live undefined value mapped to shared storage
retains its successful AssignUndefined, after the RHS. Dead-slot-write removal
also transports that independent RHS. No pass infers it again from the original
assignment/declaration syntax. Source/captured identities and the resource-visible
naming contract retain their existing representation and documented limits.
Storage completion proposals enumerate the supplied completion occurrences before
commit, including recovered declarations away from their original placeholders;
all use the same live-undefined and destination-initialization result.
The independent lexical resolution/lifetime obligation also enumerates the
current undefined definitions, including recovered occurrences, rather than
checking only each binding's original declaration slot.
Value/copy and snapshot proposals also own the current initialization identity.
When retiring that binding they preserve any independent outlined RHS at the
initialization occurrence. Removing a declaration is never authority to delete
an earlier DeclarationValue evaluation left by another pass. A composed
dead-initializer -> declaration-placement -> return-snapshot test detects this
cross-pass omission by restoring the former unconditional declaration deletion;
the independent execution comparison observes the lost first call.

Every changed pass enters BeginCommit, invalidating the entire revision-tagged
ProjectionFacts before applying its owned proposal. InitialValues may transport
positive initialization proofs because the completion remains at the same point
after the same required RHS. Analysis is forbidden during commit. The dependent
order is DeadDefinitions -> InitialValues -> Bindings -> declaration/value users;
the final storage/alias cleanup follows that order too. LoopDeclarations now runs
after final value/binding cleanup, so its grammar consumes the surviving bindings
and evaluations. It explicitly clears a stale declarator suffix and preserves
outlined prefix effects. The former post-loop cleanup group is removed. This is
an explicit producer/consumer schedule, not extra fixed-count sweeps or a claim
of whole-pipeline convergence or arbitrary pass reordering.

The printer only renders supplied completions/values, declaration grammar and
precedence, including a discarded RHS before successful undefined initialization.
It performs no new effect, demand, or binding analysis. Origin ranges exclude the
retained RHS from removed assignment/declaration origins. Tests compare split and
native initializers, reversed arenas, composed loops/branches/exceptions, dead
initial values with required calls/comma effects, and self-TDZ reads. Independent
execution detects dropped RHS calls and early initialization; common model tests
check declaration identity, before/after completion and RHS retention after each
independent removal. 200/201/400/800 exercise costs and stale grammar clearing.
Full lifetime/finally/capture, original-IR correspondence and global convergence
remain gaps; final verification and actual record scope are in status.md.


## Revision-owned recovered source regions (rules225)

RecoveryFlow now owns SourceRegion, current occurrence positions and a typed
SourceBindingScope: Original, Projected or Unmodeled. Syntax preparation builds
its lexical ancestry interval index once from the checked source ownership
census; each flow revision publishes active recovered declaration regions from
the current plan. Region IDs are explicit projection-table identities, never PC,
register or block-layout admission. Consumers use the raw current reference
owner; publication normalizes it to the printed lexical owner once.

BindingInScope answers supplemental containment for a recovered declaration.
Original means no additional narrowing, and still requires independent ordinary
name resolution. Projected requires a modeled occurrence within its region;
Unmodeled cannot prove containment. Native declaration publication without
scope facts explicitly produces Unmodeled. This does not prove initialization
completion, per-iteration activation/captured cell identity or abrupt scope unwind.
Those obligations remain separate; LegacyConfined is retained without expansion.

RootLifetime is deleted. Snapshot destination, storage reference substitution,
undefined completion remapping and alias propagation all consume the shared query
instead of consulting declaration_sites, current statement plan and intervals
independently. Scope facts belong to the revision-tagged ProjectionFacts flow;
BeginCommit invalidates them with demand, initialization and liveness before any
write. Reusing flow capacity clears retired regions/occurrences. No optimization
runs analysis during commit, and no pass may transport these regions as an
initialization proof. There is no new pass or sweep, and the existing dependency
order remains: dead definitions -> initial values -> bindings; updated storage
and value cleanup precede final loop declaration syntax.

An independent parent-walk oracle checks the interval query for every pair in
24 five-node tree shapes, forward/reversed identities, and 200/201/400/800 query
sets. Model tests check missing scope facts, inside/outside occurrences, invalid
region ownership, exact/short budgets and reused-storage clearing. Existing
source execution comparisons exercise loop/header/body/update, nested scope,
exception and snapshot/storage compositions. This is retirement of supplemental
local scope decisions, not completion of the binding lifetime model, original IR
correspondence, arbitrary pass reordering or global pass convergence.


## Primitive completion values in the shared expression facts (rules226)

The existing const postorder ComputeExpressions now publishes a primitive
normal-result literal alongside stored-binding identity, required evaluation and
lexical dependency. Facts refer only to original TreeLiteral nodes. Assignment
and sequence forward normal values; conditional joins equal primitive values or
selects a proved primitive condition; logical selection uses ToBoolean/nullish.
The same selected completion supplies stored-binding identity for later copies.
Calls, updates, coercing operators and suspension retain unknown primitive results;
this is value propagation, not arithmetic folding or executable-edge pruning.

SameLiteral preserves the literal representation kind and exact numeric bits,
including signed zero and NaN payload. Equal UTF-16 text values are interned once
from immutable input, independent of TextId arena identity; no normalization or
repeated long-string comparisons at joins. Cross-representation numeric equality
is not inferred without profile-specific equivalence. This precision boundary
never becomes an additional admission condition on already proved value facts.

Constants no longer examines the original RHS for TreeLiteral/SymbolExpr shape.
It consumes these common literal/binding result facts and the existing shared read
coverage. Its sparse copy dependency queue carries a known constant through
compound normal results. RemoveCoveredWrite still preserves the full original
RHS and independent declaration completion; later effect/binding passes own
removal. An observable member setter, condition call, self-read TDZ or catch
observation cannot be deleted just because a successful result is constant.

Facts remain inside the revision-owned ExpressionFacts/ProjectionFacts. Constants
commits a complete proposal then invalidates facts before Aliases consumes its
literal predicate substitution. A true predicate exposes a parameter copy;
a false predicate preserves the selected call's dynamic return. This closes a
cross-pass precision gap through a new shared fact, without another pass, fixed
round count or a whole-graph convergence sweep. Final loop grammar still follows
storage/value/binding cleanup. Complete lifetime/unwind/capture, original IR
correspondence and global dependency convergence remain separate gaps.

Tests retain both native and empty-declaration/store encodings, reverse symbol,
expression, statement and text identities, and 0/1/200/201/400/800 inputs. New
compound primitive/binding cases cover selected and joined values, distinct arms,
parameter/member assignment, short circuit, equal text in distinct entries, NaN,
signed zero, TDZ and live undefined in catch. Independent execution compares
values, receiver, effects and exceptions; initializer removal/early-write mutants
must differ. Budget tests cover exact and short byte/work limits. Status.md owns
measured runs and the fresh fixed-record export; no full corpus/RSS claim follows.


## Definition value joins and separate pass responsibilities (rules227)

FlowConstants is a revision-owned analysis of the same RecoveryFlow and
FlowLiveness as the existing consumers. The source semantic adapter supplies a
typed equation per successful definition: primitive value, copied binding value,
or unknown. Initializer/assignment completion consumes the shared expression
facts; lexical undefined is itself a definition. Update is an unknown numeric
computation, not a transparent copy of its normal-result binding identity.
Primitive equality is shared with expression joins and preserves representation,
number bits and canonical UTF-16 identity.

The analysis proves a storage-value invariant across all retained successful
noninitial writes. Only lexical undefined initialization proved dead by common
liveness is excluded. Other retained writes participate even if their ordinary
read demand is empty: prefix update can supply a consumed normal result without
another GetValue of the updated binding. Observable/exposed cells and live entry
values remain unknown. This deliberately conservative invariant does not prune
executable edges or infer per-read SSA constants. An unrelated retained dead
assignment can reduce precision; independently scheduled dead-definition cleanup
precedes the analysis rather than being embedded in it.

Sparse copy dependencies are condensed by iterative Kosaraju. Components join
primitive seeds and propagate unknown/conflicting values in topological order.
A copy cycle needs a grounded primitive; a seed downstream cannot erase an
ungrounded or unknown dependency. The analysis never examines control-flow
shape, block count, source names, instruction sequences or printed expressions.
Known abstract undefined with no original literal anchor remains a value fact;
the current read plan cannot print a new literal node. This is a documented
representation gap, not a language restriction. Equal anchored inputs can
provide an existing original literal without replaying evaluation.

Constants now commits only owned read-substitution proposals. Its single-write
and single-definition coverage admission, private copy queue, selected-write
removal and declaration retirement have exited this decision path. Aliases still
uses single-definition coverage for dynamic-value identity; that is a different
obligation and is not removed by constant-value agreement. DeadDefinitions,
InitialValues and Bindings independently decide writes, required evaluations,
lexical completion and binding retention from fresh facts. The pass sequence
runs early identity/effect/dead-definition cleanup and initialization/binding
recovery, then Constants. A Constants commit schedules identity/effect and
demand/initialization/binding cleanup before Aliases. Subsequent alias/identity/
effect commits schedule fresh cleanup; if none committed, the already-clean
projection does not repeat the same analysis. Without a Constants commit, the
existing cleanup before snapshots is retained. Snapshots/storage and final cleanup still
precede loop/declaration grammar. These dependencies use RunPass revision changes,
not manually reported pass booleans. No new pass kind, fixed-round sweep or
arbitrary reordering is introduced. The fixed record exposed the redundant
same-projection analysis through budget exhaustion; dependency scheduling removes
that repeated allocation without changing the budget or weakening proofs.

FlowConstants belongs to the whole ProjectionFacts aggregate. BeginCommit clears
that aggregate before the first mutation, advances the revision and forbids
analysis during the commit phase. The pass holds IDs in an owned proposal before
committing; later consumers request new facts. Changes are derived from revision
movement, not manually reported flags. This extends existing invalidation and
transaction discipline to the new analysis instead of requiring another manual
reset at each call site. Full dependency-driven convergence, original IR/SSA
correspondence and complete lifetime/unwind/capture reconstruction remain open.

The small independent oracle walks backwards origins and separately requires
each contributing origin to be grounded; it uses neither SCCs nor component
ordering. All three-binding edge/seed combinations are checked, with reversed
identities. End-to-end fixtures add equal/different branch definitions, zero or
multiple loop iterations, self copies, a grounded mutual-copy cycle and a prefix
update counterexample. Native declaration and empty-declaration/store encodings,
arena reversal, semantic execution, 200/201/geometric sizes and exact/short
budgets remain required. Status.md records actual verification and record scope.


## Source primitive identity independent of AST occurrence (rules228)

The rules227 inability to materialize known lexical undefined without an original
TreeLiteral is retired. BindingInitializers now owns one immutable pool of typed
TreeLiteral values; PrimitiveId denotes a value in that owner's pool, not an
ExprId. Initialization's implicit undefined and input primitive literals enter
this pool once before pass execution. The adapter preserves number kind/bits and
canonical UTF-16 identities. The expression-to-primitive index is private to
preparation; the published plan owns values without changing the canonical AST.

ExpressionFacts, FlowPrimitiveValue and the committed read plan use this same
identity domain. CurrentConstants supplies the implicit value ID for undefined
initialization/store definitions. Existing sparse definition joins and independent
write/initialization/binding cleanup remain unchanged. A read can materialize the
proved value even if input contains no literal expression. Conditional/logical
result selection consumes its primitive truth/nullish facts through the same
postorder analysis, so later Aliases can use a selected parameter value while
preserving the original condition/effects. No undefined syntax pattern, loop
shape, producer list or fixed block condition selects recovery.

The value pool is immutable across projection revisions. Fact caches still belong
to ProjectionFacts and are invalidated before the first commit. A Constants
proposal compares semantic primitive values rather than occurrence/ID equality;
choosing another equal pool entry cannot falsely report a semantic change.
The established revision-driven pass dependencies remain intact. Pool creation
is input adaptation before passes, not a side effect of expression analysis.

Printing consumes the committed value and normal operator grouping. It performs
no analysis, appends no AST nodes and preserves each read's original source
range. Class-name analysis and RecoveryFlow treat that read as the same committed
value substitution and do not invent a binding reference. RHS evaluation,
exception order, TDZ/availability, dynamic update results, observable/captured
cells and source-local identity retain their independent obligations. Primitive
pool publication does not license removing initialization before an early read.

Tests add zero-literal-input declarations, copy/loop cycles, sequence effects,
nullish/conditional parameter selection, early TDZ and unknown branch writes,
with both declaration/copy encodings and arena reversal. The existing source
range, effects, numeric/text and 200/201/geometric budget tests remain required.
A compiler warning also exposed four test-only ranges obtained through references
to temporary variant results; the test now keeps the owning variant alive before
iterating, so the negative graph cases are valid C++20 evidence. This does not
complete original IR correspondence, all lifetime/capture/unwind reconstruction,
per-read value precision or global dependency-driven convergence.


## Normal-value dependency closure and immutable descendant reuse (rules229)

Rules228 postorder facts cannot close a value dependency discovered only after a
producer binding becomes constant. For example, `let current = false; let scratch =
current ? seed() : 42; report(scratch); return scratch;` retained scratch until a
later analysis despite having a normal result of 42. The copy-only Kosaraju solver
and the assumption that normal expression values are already settled are retired.
No extra whole-function pass round or conditional-expression admission was added.

The source adapter publishes immutable typed value equations once before passes:
primitive identity, binding read, forwarded normal result, conditional selection
and logical selection. Assignment/sequence forward normal values; updates and
unmodeled computations stay unknown. Current projection transports replacements,
primitive substitutions and resolved storage through that same meaning. Postorder
completion facts consume the typed equations rather than rematching selection
syntax. Primitive truth/nullish operations have one shared implementation.

CurrentConstants borrows the immutable base and unchanged replacement plan, and
owns only a B-sized snapshot of current binding values. The synchronous borrowed
view cannot escape analysis or observe a commit. ProjectionFacts still owns the
result by revision; BeginCommit invalidates the aggregate before changing any
plan state, and commit-phase analysis is forbidden. Constants changes reads only;
independent dead-definition/initialization/binding cleanup runs before Aliases,
and final loop/declaration grammar follows storage and effect cleanup. Producer
and consumer dependencies and change detection remain revision-driven.

A bounded sparse worklist solves expression and successful-binding equations
jointly. Unresolved, one typed primitive, and unknown form a flat lattice; changes
notify indexed dependent equations, each with constant arity. Missing grounding
becomes unknown and propagates before publication. Terminal values borrow their
immutable payload; binding reads share the binding node directly. Neither an
unknown operand nor an unseeded cycle disappears when its mutable node is omitted.
The result is still one stable value per binding, not per-read SSA/SCCP. Executable
edges, effects, writes, initialization, lifetime and exception paths are unchanged
by this solver. A known normal result is never permission to delete its RHS.

Fixed-record validation exposed repeated recovery, not a semantic failure:
printed class-name analysis prepared descendants then discarded their plans;
member emission reran the same passes. ClassNames now owns those already prepared
SourceVariables, sorted by exact tree identity. Class member printers borrow them
and the common naming result synchronously from their enclosing printing owner.
Supplied root-family plans remain owned by the caller. No mutable/global cache or
cross-revision reuse is introduced. Tree identity and unchanged external-context
identity scope the borrow; unrelated/equal copied trees cannot reuse a plan.
Global/module/capture destination checks and structural checks still run. Static
field emission's separate preparation remains a documented duplication gap.

The original default budgets remain intact. Initial broad expression-state
allocation and repeated base/projection arrays were compressed before the
owner-reuse change; none of these changes discard equations or choose a fallback
by resource failure, input count, sample, layout, or operation sequence. The old
"prepare again for every emitted class family" path is absent when the enclosing
owner already supplies that exact immutable plan.

Tests cover conditional/logical/effect compositions, an unknown predicate,
consumed update, missing grounding, native/store encodings, reversed arenas and
0/1/200/201/400/800 chains. An independent finite table checks 864 combinations
through both direct equations and borrowed projections, alongside the existing
32,768 copy-graph oracle. Owner tests check moved preparation, exact-tree reuse,
equal copied-tree rejection and canonical analysis without source preparation.
Status.md records actual source execution/mutant and fixed-record evidence.
Original IR/SSA correspondence, full activation/capture/finally, per-read precision
and global dependency-driven pass convergence remain open.


## Declared pass contracts and revision-owned cleanup (rules230)

RecoverySchedule now owns the typed pass phase, projection revision, semantic
stage and demand-cleanup checkpoint. The previous private phase/revision switch
in Initializers has retired. PassContract declares cached fact inputs and
initialization preservation in one exhaustive typed table. RequiredFacts closes
provider dependencies: constants need liveness; liveness/demand/initialization
need flow; flow and expressions share the conservative-effect -> initialization
-> refined-effect bootstrap. Providers remain lazy, so declaring a dependency
does not eagerly allocate unused analyses. Every cached fact entry point checks
the active pass's declared dependency closure and exact projection revision.

The first commit increments the revision once and invalidates the whole
ProjectionFacts aggregate before any plan write. Further writes belong to the
same proposal batch; analysis remains forbidden until Finish. An undeclared
fact request, stale revision, nested pass or stage violation is an ownership
error. Initialization positives outside ProjectionFacts retain only the existing
explicitly proved per-pass transport obligations; this change does not strengthen
any availability/TDZ proof or authorize read substitution.

The constants_changed/value_cleanup_needed/cleanup_needed control paths have
retired. After normalized value reads/stores/effects, alias producer coverage
consumes a completed demand-cleanup checkpoint. The schedule publishes that
checkpoint only after DeadDefinitions -> InitialValues -> Bindings have all
finished in order. A caller cannot mark it clean directly, skip an owner or
interleave another pass inside that group. Any subsequent producer commit makes
the checkpoint dirty; a no-op leaves it current. Snapshot motion alone transports
a previously current checkpoint: it removes its own single producer/read and
retains RHS completion, without making another producer newly dead. It cannot
create a checkpoint when one was already dirty.

Value recovery precedes snapshot/storage recovery, which precedes final loop
and declaration grammar. Values-stage aliases, entering storage and entering
grammar require the current demand checkpoint. Final inert-operand removal is
followed by the same checkpoint barrier before grammar. Individual passes remain
separate proposal/commit owners; a group is a sequencing contract, not a combined
semantic optimizer. The existing ordered pipeline still chooses intra-stage
pass order. No arbitrary reordering, global pass fixed point, dynamic dependency
scheduler or per-read SSA precision is claimed. The checkpoint means that the
ordered cleanup group ran at this projection, not that all optimizations converged.

Tests exercise stale/undeclared facts, commit-phase reads, nested passes, omitted
cleanup owners, dirty stage transitions, reverse stages, checkpoint transport and
200/201/400/800 repetitions. The existing generated-source tests supply the
semantic/layout/exception/capture/loop evidence. A deliberately incorrect
undeclared-fact admission is rejected by the negative test. The initial new API
test failed to build because the schedule did not yet exist; that is interface
RED evidence, not a behavior comparison. Final selected verification and fixed
record evidence are recorded in status.md.

Next precision work remains per-read normal values over shared live incoming
facts, with initialization and completed-write obligations. This turn deliberately
finished the scheduling prerequisite first; no new per-read substitution has
been implemented. Original IR/SSA correspondence, full activation/capture/finally
and global convergence remain open; the milestone and stage-one Goal are active.

## Completed private read values over shared incoming facts (rules231)

The source adapter now distinguishes a binding-wide invariant from the normal
primitive value at an actual read. AnalyzeFlowConstants first freezes the
independent all-retained-definition/entry proof, then refines unresolved completed
private reads over shared live incoming facts. These are two analysis domains
within one immutable projection, not two optimization passes. A weaker contextual
unknown from conservative unreachable predecessors cannot erase a stronger
binding invariant. No executable edge is removed to obtain a value.

The contextual domain joins each predecessor's successful definition or incoming
value for that binding. It includes throw, resume and cleanup edges. Source read
nodes join every actual occurrence of the ExprId. Expression result equations,
successful definitions and incoming values close on the same sparse monotone
worklist. Entry, missing inputs, ungrounded cycles, unknown computations and
updates remain unknown. Initialization availability comes from the existing
shared flow bootstrap; TDZ and observable/captured/source-local cells do not gain
permission from this analysis. It is source-projection precision, not original
IR/SSA correspondence or complete activation/capture reconstruction.

Constants owns binding and per-read primitive proposals before BeginCommit.
The common schedule invalidates all revision facts once; the independent demand,
initial-value and binding cleanup owners run on fresh facts before aliases and
storage. Read replacement does not itself remove a write, declaration or RHS
evaluation. Previously required initial undefined storage can disappear only
after its actual observations have become terminal values and cleanup proves it
dead. Remaining unknown reads still require independent alias/interference,
receiver, scope and completion proofs. No new admission is based on same blocks,
adjacency, opcode sequences, sample identities or compiler graph shapes.

BindingInitializers owns ExprId-indexed primitive substitutions. Shared flow,
expression facts and name preparation consume them; the printer emits them
before any remaining binding alias and preserves the read's grammar/source range.
It computes no independent semantic optimization. A known early read therefore
survives late alias recovery for the same binding's unknown value.

The old binding-wide-only precision limit retires for completed private primitive
reads. Arbitrary object/reference identity propagation, full lifetime/unwind,
original IR/SSA correspondence and global pass convergence remain open. Tests
cover sequential differing writes, branch joins, loop-carried mixed values,
failed RHS writes, initialized undefined, TDZ, source-local cells, updates and
known reads composed with later aliases. An independent finite-set oracle checks
32,768 small graphs, including cycles and conservative completion edges. Layout,
native/store encoding, 200/201 and geometric growth tests remain; status.md records
the actual selected regression and fixed-record evidence.

Declaration grammar still preserves an already modeled lexical initialization
when a later unknown value needs multiple uses. Removing its early value read
does not alone justify relocating the binding's initialization/lifetime. Single
remaining reads may let independent snapshot/binding cleanup remove the whole
slot, but this does not establish general initializer motion. That representation
precision gap needs a common initialization/lifetime proposal; it must not be
expanded with a local source-form or same-block admission.

## Initialization placement consumes normalized values (rules232)

The rules231 multi-use unknown-value gap was traced to the sole Declarations
pass running before value propagation. Existing successful-definition dominance
over all completion edges and retained lexical extent already prove placement
in this bounded domain. Declarations now runs once after normal-value substitution
and complete independent demand/initial-value/binding cleanup, before aliases and
storage. The former early call is removed, not retained with a late retry.
RecoverySchedule requires the current cleanup checkpoint; a placement commit
invalidates facts and dirties cleanup before aliases. A no-op preserves it.

Placement still carries initialization completion in the current source model.
An attempted placement after all storage passes lost useful producer/initialization
coverage and source quality; that order was retired. Complete value/storage/cell
separation remains a migration task, not a reason to weaken existing proof.

An intermediate order exposed a distinct representative-choice problem: native
spelling preference could choose a newly narrowed loop slot for a group also
observed outside the loop. Coalescing now consumes the shared projected region
and actual reference positions. The source adapter aggregates a reference extent
once per binding; DSU contraction joins those extents and missing-position facts
once per union. Between the two retained candidates, prefer one whose known
region covers the group's references over one whose region cannot. Neither
candidate is admitted or rejected here. When both cover (or both fail), existing
unique-name/native/completion-rank/ID preferences remain. Preferring wide regions
unconditionally was tested and retired: it regressed real record loop spelling.

Reference occurrences and projected scopes come from common RecoveryFlow, not
another AST scope census. Every proposed mapping still independently proves
completed initialization, name resolution, source containment and initial-value
preservation. Unknown locations cannot establish narrowed coverage. Complete
feasible representative search among all original group members remains an
explicit precision gap; the bounded choice considers the two retained candidates.

Required old-initializer RHS effects stay at their original occurrence through
DiscardDeclaration. Early reads replaced with proved primitives no longer demand
that storage. Remaining TDZ, loop/branch/catch state, source-local/captured cells
retain existing obligations. No new same-block, opcode/producer list, adjacency,
sample identity or fixed graph admission is introduced.

Tests cover early undefined followed by one unknown producer and many uses,
required initial effects and branch/loop/catch/source-local/TDZ negatives. Native/
store, reversed arenas, 0/1/200/201/400/800 and exact/short budgets remain. Scope
preference stars reverse IDs and check 200/201/geometric sizes; a both-covered
negative keeps native narrow spelling; repeated-loop
source recovery retains its merging assertion and independent execution. The
loop reference harness now distinguishes an omitted argument from explicit
undefined, so callbacks actually run. Adverse mutations target the state producer,
not an equivalent inert declarator. Actual verification belongs in status.md.
Original IR/SSA, complete activation/capture/finally and global pass convergence
remain open.

## Representative choice consumes completed groups (rules233)

Rules232's two-retained-candidate limit is retired. Interference contraction now
determines membership and joins reference extents only. After all contractions,
one scan ranks every original member against its final group. A candidate whose
scope was unnecessary early can therefore become the final representative when
later members widen the observations. DSU balancing no longer discards source
candidates. This is two phases of one immutable storage proposal, not two passes
mutating the recovery plan. Greedy conflict-constrained membership remains.

Selection also consumes the existing current FlowInitializations. Its shared
all-completion dominator intervals describe completed initialization and reachable
references; lexical SourceRegion remains a separate coordinate domain. The
source adapter aggregates both extents in one reference scan. Prefer a member
whose lexical scope and completed initialization cover the final group; equal
capability retains unique spelling/native declaration/initialization rank/ID.
Unreachable syntax adds no execution-order demand, but retains lexical obligations.
Missing source locations cannot establish narrowed scope coverage.

Storage still requests current liveness and initialization through RecoverySchedule.
All grouping, ranking, reference/name/lifetime and initial-value proposals finish
before commit; a commit invalidates the revision's facts before plan writes. No
analysis is consumed during commit. Selection is a preference, not a new admission:
the existing per-mapping root/member completion, name resolution, source containment,
cell identity and independent initial-value preservation proofs remain authoritative.
Original declaration RHS evaluations and exception positions are unchanged.

The old union-time winner decision exits the production path. This closes candidate
loss for the modeled lexical/initialization capability, not exhaustive feasible
choice across shadowing, dynamic cells or initial-value preservation. Those remain
common-analysis precision work; do not add retry patterns or syntax admissions.

Tests enumerate 45,056 tiny group cases using an independent dense membership and
all-candidate oracle. Discarded-candidate cases reverse IDs/affinities at
3/200/201/400/800, cover projected/unprojected scopes and exact/short budgets.
Native original initializers and effectful producers now compose with copies,
updates and writeback in actual generated source. Independent execution checks
Number/BigInt/coercion failures and producer exceptions; adverse update deletions
remain detectable. Existing completion, TDZ, shadowing, capture and loop negatives
remain. Selected current verification belongs in status.md. Original IR/SSA,
complete activation/capture/finally and global pass convergence remain open.

## Occurrence snapshot transport and proposal dependence (rules234)

Snapshot recovery now represents source occurrence removal as typed
ExpressionTransport, separately from the read-to-RHS value replacement. The
common adapter proves the assignment result discarded; declaration initialization
has no separately observed assignment value. Removing the source evaluation
retains enclosing values and sibling effects. Whole-expression-statement equality
and discarded-expression-statement ownership no longer decide snapshot admission.
Shared retained definitions/reads, inert successful PutValue, bidirectional motion,
exception identity and initialization/name/lifetime obligations remain mandatory.

All proposals finish on the current immutable revision before any commit. One
trial projection uses the same BuildRecoveryFlow adapter as current analysis.
Actual expression owners determine destination scopes; optional typed transport
roots track dependence through nested read replacements. Explicit name/reference
queries apply only to transported RHSs. Closure and implicit-helper dependencies retain
the existing enclosing-environment obligation until they join that reference
model. A rejected outer hypothetical environment rejects its dependent component.
Independent proposals sharing a statement remain independent. This provenance is transformation
dependence, not a new canonical SSA identity or a statement-layout admission.
No incremental partial-rejection retry is introduced.

Commit consumes owned IDs only, invalidates the projection before writes, and
removes declaration/completion actions only for their owning elided binding. A
nested snapshot cannot delete another binding's native initialization. The next
Storage consumer requests fresh common facts; independent store/operand/statement
normalization and definition/initial-value/binding cleanup keep their existing
ordered owners. No new optimization round or manual analysis refresh is needed.
The printer and printed name walkers consume the transport projection; moved RHS
source ranges belong to the destination occurrence, not its omitted source wrapper.

Source tests compose prefixes, nested return expressions, observed assignment
results, independent and dependent snapshots, nested native initialization,
shadow/TDZ names, loops, exceptions and existing suspension cases. Reversed arenas
and 0/1/200/201/400/800 sizes remain; independent execution and mutations reject
dropped/reordered effects, observed-value loss, invalid partial forwarding and
missing declarations. Scope/resource precision still retains entire dependent
components when a proposal fails. Direct creation/resource-environment limitations
remain explicit existing gaps. Original IR/SSA, complete activation/capture/finally
and global pass convergence remain open; selected evidence belongs in status.md.

### Original lexical regions share storage facts (2026-10-08, rules235)

The syntax adapter now publishes a ScopeId-to-SourceRegionId index during its
existing single lexical traversal. OriginalBindingScope carries an optional region;
ProjectedBindingScope continues to carry the recovered declaration region. Shared
BindingRegion and BindingInScope queries consume both forms. Original bindings
without supplied regions retain the legacy identity contract; unmodeled projected
bindings cannot authorize access. Name resolution, completed initialization and
activation/captured-cell identity remain distinct obligations.

Storage candidate ranking consumes this common region for every original member,
including canonical declarations. It no longer treats all original scopes as
unnarrowed while only recovered declarations carry containment facts. A unique
inner spelling cannot outrank an available outer slot when it cannot cover the
completed group's actual references. The independent post-proposal reference-name,
root/member completion, lexical containment and initial-value/effect checks remain.
No new admission, optimizer-local syntax census or candidate retry is introduced.

Canonical source regions are immutable syntax facts; projected declaration regions
are selected from the current versioned plan when building Flow. Every commit
invalidates ProjectionFacts before mutation. Storage therefore cannot consume a
pre-declaration or pre-snapshot region selection. Proposal generation/validation
stays read-only and commit consumes owned IDs. Existing ordered normalization,
dead-definition/initial-value/binding cleanup, values/storage/grammar stage barriers
remain unchanged; the printer consumes the final plan.

The new source case combines inner mutation/copy, observed assignment, sibling
shadow and an outside return with both initializations dominating all references.
Independent execution and update/scope mutants test effects and visibility;
reversed statement/region arenas, 200/201 and geometric inputs test layout and cost.
Independent ancestry checks exercise bounded original and projected scopes equally,
including missing occurrences, invalid region IDs and graph reuse. This adds lexical
containment precision only. Exhaustive name/cell/value representative feasibility,
original IR/SSA, full activation/capture/finally and global convergence remain open.

### Shared spelling facts and feasible storage names (2026-10-08, rules236)

AnalyzeTreeSpellings uses the same spelling-group and scope-activation model as
ResolveTreeReferences. It publishes nearest enclosing same-spelling bindings and
unique-spelling preferences once for the checked canonical scope tree. The source
adapter maps shadowing scopes to shared lexical regions and publishes them through
RecoveryFlow. Coalesce no longer builds a separate TreeScopeCheck to count duplicate
spellings; it consumes common FlowBinding preferences and SourceBindingShadow facts.
Canonical removed-binding spelling remains conservative, matching the existing
final reference resolver. Canonical shadow intervals do not prove rewritten
native-for visibility; declaration relocation/retirement precision remains open
under independent final names and projected-lifetime checks.

After all interference contractions, current reference positions are sorted by
final storage group. Each candidate's direct shadow region queries that group's
actual positions by binary search. The nearest shadow covers its whole subtree,
so deeper shadows need no pairwise ancestor expansion. A hole inside a group's
min/max extent does not block a candidate when no actual observation lies inside.
Missing region facts mark a candidate's preference unproved. This neither rejects
an interference group nor substitutes for final name/lifetime/completion/cell and
initial-value/effect proofs; unavailable candidates may still be proposed and
independently rejected when no fully covered candidate exists.

The same immutable projection owns names, original/projected regions and actual
current references. Proposal validation precedes commit, first commit invalidates
ProjectionFacts, and the mutation loop consumes owned IDs only. No normalization
round or whole-function fixed-point sweep is added. Sparse analysis fixed points
already operate over shared facts; general pass dependency convergence still needs
an incremental cost model rather than one full rebuild per eliminated node.

Source regressions combine observed assignments, updates/copies, an active inner
shadow, a harmless sibling shadow, outside return and reversed arenas. Independent
execution and wrong-name/update mutants exercise value/effect fidelity. Independent
ancestry plus tiny brute-force candidate oracles compare names without production
indexes; actual-use holes, reordered groups/references, 200/201/geometric and exact/
short budgets verify precision and cost. Full original IR/SSA, activation/capture/
finally, rewritten binding visibility and exhaustive cell/value feasibility remain
open. Verification scope and results belong in status.md.

### Current binding retention and shared name projection (2026-10-09, rules237)

AnalyzeFlowBindingNames is a pure analysis of one current RecoveryFlow. It publishes
typed retained/elided name presence and projects canonical same-spelling parent
facts. Parameters, source locals, catch/self/capture/module bindings and observable
compiler temporaries remain retained. Every current definition or reference retains
its binding, including implicit undefined initialization and unreachable syntax.
Only a private compiler temporary without these facts is elided from name activation.
This is a source-name fact, not value liveness, TDZ, cell identity or authorization
to delete initialization. Existing independent lifetime/effect/completion proofs stay.

Canonical spelling facts now include the shadowing SymbolId. A parent-before-child
forest traversal memoizes each nearest retained ancestor once, skips retired inner
bindings and reconnects retained inner shadows across retired parents. Legacy shadow
facts without inner identity remain conservative; identity is never guessed. The
current graph's mask is consumed by storage selection and all current alias/storage/
snapshot name queries through the shared scope resolver. Structural checking and
canonical spelling publication retain the full original binding model. The previous
all-original-symbol activation exits current source-name decisions.

Each graph rebuild derives these facts afresh, including reuse of graph storage.
They belong to ProjectionFacts and become inaccessible when BeginCommit invalidates
the projection. Canonical facts stay immutable; analysis does not mutate replacement
plans. Existing Values/Storage/Grammar order, cleanup checkpoints and owned proposal
commit contracts remain. No new whole-function optimization round is introduced.

Independent ancestor oracles cover retained masks, reads/undefined definitions/
observable exposure/source locals/captures, renumbering and reordered facts. Native
source tests combine early observed writes, a later initializer, retired or retained
inner shadows, updates and sibling scopes; execution and wrong-name/update mutants
protect behavior. Chain/star 200/201/geometric facts and exact/short budgets cover
cost; stale graph storage and malformed provenance are tested.

Full native-for rewritten name visibility, scope relocation and prospective retirement
of multiple bindings in a storage proposal remain precision gaps. Canonical uniqueness
stays a style preference. General incremental pass convergence, original IR/SSA
correspondence and full activation/capture/finally remain open. Status.md contains
actual selected verification; the milestone and stage-one Goal remain active.

### Current source namespace (2026-10-09, rules238)

Canonical spelling analysis now publishes typed SourceSpellingId mappings for
bindings and NameIds, including distinct NameIds with equal UTF-16 text. Text
matching stays at this validated boundary. Each current RecoveryFlow derives
retained-name activation from its original/projected lexical regions. Sorted
nested intervals publish disjoint active-name ranges and current direct shadows.
Canonical shadows no longer stand in for a narrowed binding's whole original
scope. All alias/storage/initialization-write and snapshot symbol/free-name queries
consume this common index; optimizer-local original statement_scopes_ lookup and
ResolveTreeReferences/Names calls exit those decisions. Structural validation
continues to use the canonical tree.

EnclosingBindingScope explicitly models self/module/capture/parameter environments
that enclose the function body. Canonical parent identities preserve their order
when several layers share the whole-body envelope. Missing local regions remain
unmodeled; absence never implies an enclosing environment. Ambiguous co-located
declarations and unknown facts return typed precision gaps. Names neither establish
TDZ/initialization completion nor authorize merging cells; independent lifetime,
completion, initial-value, interference and effect obligations remain.

SourceNameIndex belongs to the same immutable projection as current definitions
and references. Rebuild/reuse regenerates it; first commit invalidates the aggregate.
Queries are read-only and return owned results. Existing pass order and cleanup
contracts remain, with no whole-function retry/normalization round. Legacy direct
component flows without spelling identities keep conservative shadow facts and
return an unmodeled name query instead of guessing text or scope.

Independent tiny ancestry/mask/projection oracles compare symbol and free-name
queries. Enclosing-layer precedence, missing positions, half-open endpoints,
unknown/ambiguous/crossing provenance, stale reused index, 200/201/chain/star
geometric and exact/short budgets cover analysis boundaries. Source tests combine
relocated native declarations and native-for loop-carried bindings with outside
reads (legal merge) or inside reads (retained slots), reversed arenas and independent
execution/wrong-merge/update mutants. They assert actual native-for syntax.

This closes current known lexical-region name resolution, not a complete namespace
reconstruction for arbitrary binding rewrites. Prospective simultaneous storage
retirement/renaming, full activation/capture/finally and original IR/SSA integration
remain open, as does incremental global pass convergence. Status.md records actual
selected acceptance; the unified-variable milestone and stage-one Goal stay active.

### Prospective copy-retirement name closure (2026-10-09, rules239)

Aliases now submits its complete eligible copy-retirement mask to the pure common
AnalyzeSourceNameRetirement analysis. It supplies normalized fixed retained roots,
current semantic read owners, and conditional read ownership for inert statement
RHS values. Independent lexical-containment failures enter as retention demands.
Value coverage, cell identity, activation/TDZ, successful write completion and RHS
effects remain independently proved; namespace analysis cannot authorize their
removal. An observable or embedded RHS stays an unconditional read.

The baseline removes every proposed copy binding at once. Shared SourceNameIndex
records nearest baseline retained same-spelling parents even for inactive entries,
and coincident local-region peer identity. Queries use an explicit immutable index
without copying or mutating RecoveryFlow. Retention activation invalidates queries
in that binding's lexical interval and baseline-root partition. A bounded worklist
also propagates latent RHS demands when their destination becomes retained. Each
binding activates once and each failing query installs at most one demand edge.
Unknown scopes and coincident declaration conflicts invalidate the affected spelling
as typed precision gaps. They do not invent a compiler-shape admission condition.

The optimizer-local pre-proposal name rejection and blocked-copy dependency graph
exit Aliases. The returned presence delta is owned before BeginCommit invalidates
ProjectionFacts; commit reads no shared analysis. Existing typed pass contracts,
Values/Storage/Grammar stages, and ordered DeadDefinitions/InitialValues/Bindings
checkpoints remain. This is one monotone semantic analysis inside a pass, not an
extra whole-function optimization round or a shared mutable-plan fixed point.

Tiny independent lexical-ancestry/mask/projection/copy-demand oracles compare the
least retention closure, including cycles without seeds, free names, coincident
regions and reversed identities. Source execution checks simultaneous disappearance,
retained local TDZ, shadow activation, and a snapshot safely sharing retained
scratch storage. Existing storage recovery may improve that snapshot before
Aliases; tests assert the proved result rather than forbid pass interaction.
200/201/geometric work, exact/short budgets and invalid roots/IDs cover boundaries.

This closes prospective simultaneous copy-binding retirement under the stated
fixed-root contract. Simultaneous storage-group retirement/renaming or scope
relocation still needs its own proposal model; full activation/capture/finally,
original IR/SSA correspondence and global incremental pass convergence remain open.
The unified-variable milestone and stage-one Goal remain active.

### Joint storage-retirement namespace (2026-10-09, rules240)

Coalesce now consumes AnalyzeSourceNameRetirement for the complete interference-safe
storage proposal, using direct fixed retained representatives. Every mapped read,
write, update half and preserved live-undefined write supplies its actual current
owner. These reads are unconditional: changing storage never authorizes dropping
an RHS. Completion/TDZ and lexical-lifetime failures enter as independent retention
demands, alongside PlanStorageInitializations failures. The common closure handles
retained members casting shadows across other groups in the same proposal.

Representative ranking first compares common completion/lifetime feasibility over
the final group's observations. Current namespace visibility then remains a
preference, followed by spelling/native/order style. It cannot outrank a feasible
candidate just because another group currently shadows it; that shadow may retire
in the joint proposal. Contraction still owns only interference, ranks all original
members and rejects no member during union. Final occurrence proofs remain.

PlanStorageInitializations runs once against the unchanged flow/liveness/completion
facts and fixed roots. Its actions are member-local: rejecting one mapping does not
invalidate another member's original live-undefined/root-completion proof. Common
name/lifetime closure may retain any subset; commit consumes only accepted members'
owned actions. Coalesce's old pre-proposal ResolveNames batches, local blocked-mask
filtering and separate original-namespace preserved-write rejection exit. Analysis
finishes before first-commit invalidation; no shared fact is read during commit.

Source positives compose two writable groups: the inner same-name binding retires
to another slot, allowing the outer temporary to share its available root. Source
locals and actual interference keep that shadow and preserve the outer temporary.
Independent source execution, wrong-merge/old-value/update mutants, actual declaration
counts, reversed arenas and block composition verify output. Independent tiny
representative-prerequisite and initialization-subset/throw/resume oracles verify
proof order and subset stability; existing closure/flow/scaling/budget tests remain.

This closes joint storage-binding retirement for fixed roots without moving their
scopes or inventing new names. Arbitrary renaming/relocation, choosing new roots
after subset rejection, full activation/capture/finally, original IR/SSA correspondence
and global incremental pass convergence remain gaps. Existing Values/Storage/Grammar
and demand-cleanup contracts remain; the milestone/stage-one Goal is still active.


### Revision-owned snapshot consumer checkpoint (2026-10-09, rules241)

The first snapshot pass still precedes storage contraction, so removing a proven
single-use definition can reduce later interference. Storage/alias normalization
and ordered demand cleanup can then remove names or observations which blocked
that attempt. RecoverySchedule records the revision at which Snapshots completed.
At the stable storage boundary it requests one fresh attempt if later commits
changed the projection. No-op producers do not trigger it; a snapshot's own commit
does not schedule itself. This checkpoint records consumer work, not reusable facts.
First-commit aggregate invalidation and RequireCurrent still reject all old analysis.

The boundary query requires Storage stage, no active pass, an idle complete cleanup
group and a current demand checkpoint. Partial DeadDefinitions/InitialValues groups
are rejected even if an older unchanged checkpoint was current. The refresh uses
the same common read coverage, bidirectional motion, exception identity, completion,
lexical name and lifetime proofs. It builds and validates owned proposals before
commit; independent stored-result/inert/demand owners run afterwards. Grammar starts
only after the resulting projection is clean. The printer introduces no optimization.

Motion pairing is computed lazily once when an existing eligible occurrence needs
it. Lexical environment indexes are built only for actual motion proposals. Empty
refreshes avoid those allocations without weakening any semantic prerequisite.
A real storage-shadow retirement positive now emits a direct condition expression;
source locals and simultaneous live values retain their shadow and snapshot to
preserve TDZ. Independent execution, wrong-forward/update/merge mutants, reversed
arenas and 200/201/geometric composition exercise the output and scheduling contract.

This is a bounded dependency repair at one explicit stage boundary, not global
fixed-point convergence. Cleanup can expose further opportunities, and arbitrary
pass cycles still require a common incremental dependency/worklist design with a
measured complexity bound. Original IR/SSA correspondence, full activation/capture/
finally, arbitrary namespace relocation and the overall milestone remain open.


### Stable normal values across definitions (2026-10-09, rules242)

The shared flat value solver now joins both typed primitives and opaque immutable
body-entry binding identities. The source adapter establishes mapped-argument,
capture and unmodeled-signature premises once; the same mapped-argument premise
serves existing aliases. Retained definitions/writes independently defeat stable
entry grounding. Ordinary observable cells stay unknown. A grounded copy cycle
can preserve one value, but conflicting identities, unknown leaves, live initial
undefined and unseeded cycles cannot invent a value. Choice/logical/sequence normal
values consume the same equations; effects and executable edges remain independent.
Contextual primitive refinement never manufactures an all-definition binding fact.

An independent BindingValues pass substitutes only private reads. It validates
current lexical containment and common name resolution for every destination,
then commits an owned read_bindings delta after first-commit invalidation. Storage
and successful writes keep their own identities. The shared flow, expression facts,
printed-name analysis and printer all consume this read projection; no AST replay
or symbol merge supplies its meaning. DeadDefinitions/InitialValues/Bindings alone
remove writes/initializers/names, preserving each required RHS and completion.

The source pipeline now has Values -> Storage -> ReadValues -> Grammar stages.
Stable binding substitution runs after storage/aliases/snapshot destinations settle:
early substitution erased a useful outer spelling and blocked a later snapshot
under parameter-name shadowing. That existing style regression retains its original
expectations. ReadValues cannot schedule aliases, contraction or snapshot motion
backwards; completed value substitution and ordered clean demand are required before
Grammar. Normalization owners remain independently legal in ReadValues. This closes
an actual order dependency without a whole-function optimization retry.

Repeated copies in branches/loops and effectful RHS composition now recover direct
values without a singleton-definition admission. An initial undefined observation
first becomes its independently proved literal; later stable copies can then retire
without losing that observation. Different roots, written parameters, source locals,
lexical shadows and mapped arguments retain their necessary representation. Tiny
independent origin-closure and expression-join oracles, source execution/mutants,
reversed layouts and 200/201/geometric/exact-short budgets test these contracts.

Grounding currently covers proved immutable formal/self entry values. New roots for
arbitrary local SSA values, catch activation/capture cells, per-occurrence symbolic
values, final source namespace allocation and incremental global convergence remain
precision gaps. All-definition agreement is a value fact, never deletion authority.
Original IR/SSA/full activation/finally and the overall milestone remain open.


### Compose reads after storage identity (2026-10-09, rules243)

A completed ReadValues proposal is indexed by current flow bindings, which have
already consumed early aliases, coalesced storage and final aliases. Looking up a
read substitution by the original symbol loses a proved slot value for other
members of that storage group. Independent dead-definition cleanup then correctly
keeps the slot demanded by those unprojected reads, leaving mechanical variables.

BindingProjection now owns both Storage and Read interpretation. Storage preserves
the existing published direct-root stage composition. Read first obtains that
final slot, then consumes its read-only value. BindingInitializers and RecoveryFlow
share this implementation; expression facts, printed-name analysis and printing
already consume BindingInitializers. The two duplicated storage interpreters and
original-symbol-first read lookup leave the production path. No graph/AST reanalysis
or recursive binding chase supplies meaning at print time.

ReadValues still validates all current references, including remapped members, at
their actual destination scopes/names before publishing an owned delta. Writes and
update read/write halves use Storage, never the read-value substitution. Initial
undefined, successful assignment and effects remain independently demanded. The
existing first-commit invalidation and Values -> Storage -> ReadValues -> Grammar
contract prevent a later storage mutation from making the slot-indexed proposal
stale. Value analysis, interference, lifetime and deletion prerequisites are intact.

Repeated branch copies now compose with prior storage sharing and disappear when
all reads have one stable value. Written-entry and parameter-shadow negatives retain
one appropriate slot; representative spelling may vary with a legal layout, while
semantics and the number of necessary variables remain invariant. Effectful copies
keep their original calls. Reverse arenas, block splitting, 200/201/geometric cases,
independent source execution/mutants and common flow assignment/update contracts
exercise the whole composition. This closes a projection-consumer gap; arbitrary
local symbolic roots and incremental global convergence remain open.

The current source event adapter does not publish every composed normal-result
copy as an interference affinity. Effectful first-group definitions plus direct
second-group copies exercise the existing sharing contract here. Extending affinity
precision must consume common normal-value equations; it must not add Sequence or
other producer-shape matches to storage recovery.


### Normal completion values supply copy affinities (2026-10-09, rules244)

The existing shared ExpressionFacts binding result means the expression's normal
completion equals the current value of that binding. This is distinct from the
flat solver's all-definition immutable entry value, and from an earlier snapshot.
Sequence, conditional/logical selection, successful writable assignment and prefix
update already produce these facts once; postfix snapshots and differing branches
remain unknown. Consumers must not infer another result from producer syntax.

CurrentFlow now supplies that exact-revision result vector to the source event
adapter. Successful assignment and declaration definitions consume the proved
binding as their copy value. Interference can propose the usual affinity and
exclude only the equal current source at that commit; all other live conflicts,
initialization, exception, scope, capture and name obligations remain independent.
The ordinary production path no longer builds/scans the direct-SymbolExpr copy
index or guesses affinity from that shape. An explicitly supplied unknown result
stays unknown, without a second direct-read answer.

The vector is borrowed only while building; RecoveryFlow publishes owned typed IDs.
CurrentFacts checks its projection revision and aggregate first-commit invalidation
retires expression/flow/liveness together. Bootstrap initialization changes effect
precision, not these normal-result identities. Hypothetical snapshot-motion flows
omit the current vector until their own normal-value analysis exists; their legacy
coarse direct-read fallback follows the actual proposed replacements and does not
claim composed-value precision. This is a documented capability gap, not an output
admission rule or a language invariant. No new analysis cache/pass round is added.

Independent source cases recover shared mutable storage through effectful sequences,
choice/logical composition, prefix results, retained nested assignments and lexical
initialization. Live distinct values, mixed branch results, postfix snapshots and
catch observations before successful writes keep separate slots. A dead nested
assignment may be removed by earlier cleanup, so its normal-result copy also
legitimately disappears; tests observe the assignment's value when testing that
retained-copy obligation. A postfix counterexample observes both bindings before
another update can coincidentally make their values agree. Tests use independent
execution/mutants, reversed arenas, block splitting and exact/short budgets.

Original IR/SSA and arbitrary local symbolic entry values, complete activation/
capture/finally and common global convergence remain open. Trial value precision
must be extended through the same model, without Sequence or other shape matches.

Retirement consumes the current post-cleanup references. In the scoped-copy
regression, agreeing arms allow storage sharing; independent cleanup then removes
the inert discarded choice and its condition reference. Its upstream private copy
can legally alias the parameter despite an original inner shadow. A retained value
reference still requires its distinct unshadowed binding. A mixed-arm control keeps
the condition and both upstream copies; independent truthy/falsy execution and TDZ
mutants verify these obligations. The old all-alias-none assertion described the
original syntax rather than the final semantic projection; no production admission
was tightened to retain it. Each consumer obtains fresh revision-owned facts after
cleanup, without conflating storage sharing with effect or binding removal.


### Occurrence-specific symbolic read values (2026-10-09, rules245)

The common sparse normal-value solver already tracks values arriving at completed
private reads. It now publishes symbolic entry identities in a distinct binding_reads
result, alongside primitive reads. The independent all-definition binding invariant
is still frozen and restored separately. A read returning the entry value after one
successful assignment does not claim the binding held it before that assignment.
Unseeded/conflicting/written/mapped/captured roots and uninitialized reads stay unknown.

Late BindingValues owns both invariant and occurrence proposals, after final storage
and snapshot destinations. Each proposal independently checks actual retained owners,
common lexical scope and name resolution before first commit. An occurrence query
cannot hide a failed invariant query. ReadSources is expression-indexed; one
BindingProjection supplies flow, expression equations, class-name analysis and source
printing, while Storage and both update halves keep their original write identity.
Definitions, initial values, effects and bindings are removed only by their separate
current-fact cleanup owners. No new optimization round or backward stage mutation.

Invocation reachability is published once by common FlowLiveness on every completion
edge, at the same immutable projection as its sparse live facts. Contextual values
consume reachable reads/facts/predecessors; conservative all-read liveness and
all-definition stability retain unreachable syntax obligations. This prevents a
structural normal exit with no invocation path from poisoning the next read after
an abrupt completion. No catch/return/loop shape inference is involved. Current
snapshot motion borrows the same reachability; standalone legacy motion computes
its own, and manually supplied legacy liveness without the mask remains conservative.

Tests combine mixed prior values, live undefined initialization, multiple catch
states and failed effectful RHSs with written entry, TDZ shadow, source-local and
mapped-argument negatives. Final values and prior observations are independently
executed, including failures on second/third calls. A source-only or reference-only
forced merge can be equivalent after value substitution or before snapshot motion;
coalescing mutants exercise both and require an actual wrong behavior. Other output
mutants remain. Two 32768-case finite-set graph oracles independently compute entry
reachability and incoming primitive/symbolic value sets; none uses the production
lattice. Shared/standalone motion, composed storage/read/occurrence maps, malformed
boundaries and exact/short budgets cover fact ownership. Reversed statement arenas
and 0/1/200/201/400/800 composition check output/scaling. The missed unused scratch
declaration was a fixture setup failure, not feature RED; the rebuilt actual previous
consumer subsequently passed structure/printing and failed the intended return check.

Arbitrary local/catch/capture symbolic roots, trial projection value precision,
original IR/SSA/full activation/finally and common global convergence remain open.

### Revision-owned initialization evidence (2026-10-09, rules246)

The occurrence initialization mask was the remaining transported analysis outside
ProjectionFacts. It now owns its ProjectionRevision as InitializedOccurrences;
every expression/value consumer obtains it through InitializedReads, which requires
the exact current revision and an analysis phase. Immutable original CFG/SSA
revisions cannot justify reuse after source projection edits.

First commit transfers this owned proof through RecoverySchedule's
TransportInitializations(source revision). Only the active commit phase and that
pass's input revision are valid. The existing semantic preservation contract either
returns the new revision or revokes the proof. No-op passes leave it unchanged;
further writes in one batch create no additional revision. The bare preservation
boolean API no longer decides production reuse. All other projection facts still
invalidate together; transported evidence does not preserve flow/effect/name facts.

Independent phase/version/transport counterexamples cover every pass category,
non-preserving edits, stale/future/output versions, no-op and batch commits, and
0/1/200/201/400/800 consecutive uses. Existing source execution tests exercise
initialization, TDZ, effects, storage and declaration cleanup on this path. This is
an internal analysis ownership contract, never a recovery admission based on input
shape. It adds constant work per access/commit and constant cache metadata, with no
new graph, traversal, payload copy or eager analysis. Original IR/SSA integration,
local/catch/capture roots, trial precision, full activation/finally and global
convergence remain open; the overall milestone is incomplete.


### Available current-cell copies (2026-10-09, rules247)

FlowCopies computes classical available-copy gen/kill facts on the current shared
RecoveryFlow, sparse FlowLiveness and invocation reachability. Normal-completion
copy metadata generates equality between a private stored value and an entry cell's
current value. A successful write to the source kills that equality; an unknown
write to the destination also kills it. A failing RHS bypasses the successful
write event, so exception paths retain the earlier state. Every reachable incoming
path must agree. This equality is separate from FlowConstants' immutable roots and
never becomes an all-definition invariant or a storage mapping.

The stable_entry adapter evidence excludes mapped arguments, external captures and
unmodeled signature mutation. Body writes are permitted in this analysis and are
explicit kill events. Arbitrary local/catch roots remain a capability gap: lexical
scope alone cannot prove activation identity across loop reentry and TDZ. Completed
initialization and binding-value equation facts identify substitutable reads;
update reference halves keep Storage and are not independently rewritten.

BindingValues declares its new kCopies dependency and obtains both immutable and
current-copy results at one exact projection revision. It merges their sorted
occurrence results into owned proposals, separately proves actual scope/name
resolution, then commits. First commit invalidates the aggregate including copies;
no fact is read after that boundary. Late ReadValues remains after alias/storage/
snapshot work, followed by independent current-fact demand/initial-value/binding
cleanup and grammar restoration. No backward phase, new round or printer analysis
is introduced. Whole-function immutability is no longer the only decision path for
these mutable-entry reads; its independent invariant proof remains intact.

Eleven generated-source combinations cover writes before/after reads, conditional
kills, refreshed copies, loops, prefix-current/postfix-old results, failed RHS,
mapped arguments and shadowed names. Reverse arenas and 0/1/200/201/400/800 sizes
exercise layout and cost. Independent concrete path-state exploration checks
221184 small graph cases without the production lattice or dependency solver;
unknown source/destination write references, exact/short budgets and malformed
boundaries add counterexamples. Output execution and forced wrong merges/read
removal verify observable behavior. See status.md for actual selected validation.
Original IR/SSA correspondence, local/catch/capture activation, trial precision,
full finally and common global convergence remain open.


### Transitive current-copy facts (2026-10-09, rules248)

Normal-completion copy definitions now seed source-value queries in common all-read
FlowLiveness, immediately before successful target commit. These are proof queries,
not executed GetValue references: FlowReference, ownership, initialization, effects
and observable FlowDemand roots are unchanged. Conservative interference may consume
the expanded facts; dead-definition/effect removal continues to use independent
demand. One shared sparse worklist closes real reads and proof queries together.

FlowCopies includes queried private intermediates in its existing sparse fact
selection. A completed copy inherits the source's available entry-root equality at
that point; unknown or exposed intermediates remain unknown. The stored relation
continues to denote equality with the entry cell's current value. Later writes to
an intermediate do not rewrite another cell's already saved value; later entry-root
writes still kill the inherited relation. All incoming paths agree, failed RHS
paths bypass target definitions, and ungrounded cycles remain unknown. This is a
normal semantic dependency, never a direct/sequence/branch/loop producer match.
Local/catch/captured cells are not promoted to activation roots.

ReadValues retains the same exact-revision ownership and owned-proposal boundary.
No extra pass round, backward scheduling, printer inference or copy-depth cutoff
was added. The previous direct-entry-only generation exits decisions for private
copy chains. Existing scope/name/initialization/lifetime and independent cleanup
remain. Trial-flow normal-result precision and original IR/SSA/full activation/
finally/global convergence remain gaps; whole milestone remains incomplete.

Eight source combinations exercise chain insertion, intermediate updates, root
writes, normal sequence results and RHS exceptions, mixed joins, refreshed loops,
mapped arguments and postfix conversion. Reversed statement arenas and geometric
sizes remain. Independent concrete two-cell state enumeration covers 524288 graph
cases; 200/201/400/800 chains additionally split edges and reverse event arenas.
Exposed intermediates, unknown writes, malformed query boundaries and exact/short
budgets cover adverse cases. A query-only graph must produce all-read facts without
making any definition observable. Actual output execution/read deletion/incorrect
entry substitution complement wrong sharing in the independent original program:
when current read substitution legitimately retires an intermediate binding,
renaming an output slot to that absent name is not a valid coalescing counterexample.
See status.md for actual final selected evidence. At the rules248 boundary, read
substitution exposed pure empty controls after independent binding cleanup. This
source-style gap is addressed by the shared rules249 statement observations below.
Statement completion/effect normalization must be owned by common semantic
facts rather than a new empty-branch shape rule.

### Shared statement observations (2026-10-09, rules249)

RecoveryFlow's language adapter publishes the retained statement ownership forest
and conservative completion/progress obligations once. Every actual event carries
its current statement owner, including transported expressions and catch definitions.
AnalyzeFlowStatements consumes current effects, successful definitions, suspension,
abrupt completion, conservative cleanup and possible repetition; a reverse ownership
fold aggregates all descendants. It does not match empty-if/block/opcode producers.
All retained syntax participates, even unreachable declarations: private inert write
effects do not authorize deleting a live definition, initialization or binding lifetime.
Those obligations stay with independent demand/initial-value/binding cleanup.

The InertStatements pass now consumes this common kStatements fact rather than
locally guessing expression/declaration-value syntax. ProjectionFacts owns it at the
exact projection revision, the contract closes flow/expression prerequisites, and an
owned omission proposal precedes first commit/invalidation. After final BindingValues
and demand cleanup, inert operands/statements normalize before grammar; no repeated
whole-function round or backward storage/alias/motion schedule is introduced. Source
emission consumes omissions and maps the entire removed subtree/anchors to a boundary;
it performs no new semantic optimization.

This is a conservative may-observation analysis: possible loop repetition remains
observable even with an empty body; return/jump/throw, retained definitions and suspend
must stay. Finally carries an explicit conservative cleanup obligation. Exact loop
termination, handled abrupt completion and full finally replacement are capability
precision gaps, not compiler-layout prerequisites. Source-side adapter and original
IR/SSA correspondence, activation identity and overall unified-variable milestone
remain incomplete. See status.md for current verification evidence.


### Graph-proved normal completion (2026-10-09, rules250)

The source adapter publishes each retained statement's actual normal continuation
and optional repetition point, and distinguishes jump from return/throw/cleanup.
FlowCycles computes all cyclic points once with iterative Kosaraju on the current
completion graph. Every edge participates, including exceptional/resume/conservative
routes. A modeled repetition point outside all SCC cycles discharges that possible
repetition obligation; missing evidence remains conservative. This proves graph
traversal only: calls/coercions/definitions/suspend and unmodeled cleanup still remain.
Nested cycles are propagated through the shared statement observation fold.

Local jumps use their actual target point. An indexed activation traversal of the
statement ownership forest finds the nearest ancestor normal continuation matching
that target. Difference accumulation marks the intervening regions abrupt, but
leaves the consuming region/ancestors normally completed. There is no break/continue
producer or empty-loop pattern, repeated ancestor walk, target-set union or graph
walk per source region. Return/throw and all resume/cleanup completion edges retain
their independent obligations, even without a redundant AST marker. This is
conservative about handled throws/fullfinally and nested regions in outer SCCs.

Cycles and statement observations share exact ProjectionFacts ownership; kStatements
closes kCycles/flow/expression prerequisites. CurrentCycles is lazy when a repetition
point exists. Complete owned omission proposals precede first invalidation. Existing
pass stage/order and independent definition/initial-value/binding deletion remain;
no new pass round or backward storage/alias/motion is introduced. The printer's
prepared subtree omission is reused. The test range oracle now includes ancestor
omissions rather than requiring every removed child to have its own plan edit.

Independent dense reachability and ancestor-chain references, reversed/split graphs,
source compositions and actual output execution/negative transformations verify
this substep. VM deadlines on deliberate infinite programs are bounded execution
observations, not themselves termination proofs. See status.md for exact evidence.
Original IR/SSA, source activation roots, full finally, numerical loop termination,
trial precision and shared global convergence remain open; overall milestone is
not complete. Lexical visibility alone cannot ground a fresh local activation.


### Entry evaluation and residual control (2026-10-09, rules251)

The language adapter publishes the mandatory entry expression of a statement and
marks the events belonging to that evaluation. It runs once per statement execution
at the original occurrence, before the owned control region. Operand/replacement
contexts inherit this provenance, including normal definitions, getters, conversion,
call receiver, throw and suspension; statement descendants start their own ownership.
This is source-language semantics, not empty-branch or opcode-layout recognition.

FlowStatements keeps its complete observations unchanged and independently folds
residual observations. Only the queried statement's own entry evaluation is excluded:
a child's full observations still belong to its parent. Lexical completion, definitions
outside that evaluation, return/jump, conservative cleanup and cyclic progress retain
their obligations. Entry throw/resume remains in the original retained expression;
ordinary return and unknown cleanup cannot be discharged as expression evaluation.

StatementValues is a separate late pass. It consumes exact current kStatements and
its declared dependency closure after BindingValues and independent demand cleanup.
It owns every statement/value proposal before the first commit invalidates facts.
The projected expression stays in its original scope/handler/iteration and becomes
discarded; independent demand, operand and inert-statement cleanup follows, then
grammar. No storage/alias/motion stage is reopened. Initialization evidence transport
retains only unchanged remaining read occurrences, never other cached facts.

The committed statement_values recipe is consumed by both RecoveryFlow and ordinary/
loop-header printing. Removed child origins become boundaries; the entry expression
keeps its retained range. Printing introduces no semantic analysis. This currently
represents one adapter-owned mandatory entry evaluation, not synthesis of branch
conditional effects or hoisting from multiple descendants. Full finally, original
IR/SSA correspondence, activation identity and global convergence remain gaps.
Independent entry-forest references, statement/expression arena reversal, actual
source execution and adverse receiver/suspension transforms verify this substep;
see status.md for authoritative final selected evidence. Overall milestone is open.


### Projected normal domains through successful definitions (rules255)

The previous direct raw ExprId domain lookup is retired from recovery decisions.
A raw SSA operand fact describes that original operand, not any future value read
from its source slot after a trial changes a producer. OriginalFlow retains it as
provenance; it is not a reusable projected-domain certificate. Verified original
parameter-entry domains enter the shared source parameter definitions through the
checked parameter mapping, with exposure/stable-entry obligations unchanged.

PrepareRecoveryProjection conservatively constructs source effects and completion
edges, computes occurrence initialization independently, then computes all-read
liveness and FlowNormalKinds over that same graph. Sparse incoming binding facts
join predecessor state or a successful definition's current RHS domain. A throwing
RHS does not traverse that definition. Assignment and sequence forward values;
conditional/logical equations join normal alternatives; source operators use one
exhaustive normal-result table. This does not duplicate raw opcode semantics.
Number/BigInt uncertainty, observable cells, unknown sources and ungrounded cycles
stay conservative. Static definition identity is never dynamic value equality.

The finite ValueKinds lattice only widens. Expression/fact dependency lists and a
bounded worklist close the equations; there is no dense read-definition product or
walk per binding. Refinement consumes the resulting domains and the independently
completed initialization proof, preserving graph/event identities. Current and trial
plans call the same provider. External normal-kind spans are rejected at bootstrap,
so another plan's facts cannot be smuggled into a trial. Sequential liveness/domain
scratch is destroyed before later phases, with simultaneous live storage and total
work admitted against the unchanged budget. No pass order or new global round is
introduced; all commit invalidation remains as in rules254.

Independent small-graph state rescans, reverse arenas/events, 200/201/geometric
forests and emitted success/exception/loop programs verify this path. Source-adapter
fixtures explicitly do not claim original ABC certificates; decoded original
fixtures remain independent end-to-end evidence. Full original operation/protocol
correspondence, ordinary cell activation, complete lifetime deltas and dependency
convergence remain open, as do all five backbone delivery loops.


### Canonical ordinary binding activations (rules257)

RecoveryFlow now distinguishes completion-only synthetic inputs, enclosing units,
canonical scope allocation sites and unmodeled local lifetime. The adapter publishes
ScopeBindingLifetime from the actual Block EnterScope/LeaveScope events. A ScopeId
identifies a static allocation site; every visit creates a fresh dynamic unit. It
never proves equal captured cells or equal values on different visits. Local/catch
bindings without an owned activation remain unmodeled rather than being classified
as external incoming units. SourceBindingScope continues to describe lexical name
containment independently of this canonical unit lifetime.

CatchClause initializes its parameter after entering its handler environment and
before executing its statements (ECMA-262 2025, CatchClause Evaluation). The previous
catch definition preceding EnterScope is retired; every handler follows the same
adapter rule. Parameters/self retain their independent invocation-entry evidence.
The initialization provider checks canonical allocation-before-initialization before
publishing completion dominance. Synthetic completion-only inputs retain the older
order-only contract; they do not acquire an activation or cell-identity certificate.

A common constant-time ResetsBinding query supplies the gen/kill boundary for all-read
liveness, observable demand, reaching definitions, all-read coverage, constants,
available copies and normal domains. Allocation and normal departure stop value
facts from the previous unit. Liveness retains an unavailable-value boundary fact
without walking across it; coverage and all-definition invariants include this
unknown source, preventing a vacuous all-read proof. This is value demand, not
permission to remove observable undefined initialization, effects or environments.
Current/trial builders publish the same canonical facts and existing revision-owned
cache invalidation remains in force. No pass rounds/order or new local optimizer
admission conditions are introduced.

This substep does not complete ordinary lifetime recovery. Abrupt scope unwinding,
source-projected/narrowed declarations, native for per-iteration environments and
captured cell transport need explicit current lifetime deltas. Canonical allocation
metadata alone cannot authorize these transformations. Available-copy local roots
remain unproved; existing lexical/name/capture safeguards are preserved. Complex
finally and suspension precision remain gaps. All five backbone delivery loops and
the unified-variable milestone remain open; status.md owns measured evidence.


### Shared abrupt activation transfers (rules258)

The current source adapter publishes typed before/after lexical contexts for every
completion event, using the projected statement owner for relocated expressions.
Canonical block allocation/departure has explicit contexts; omitted blocks do not
enter their former environment. A single immutable FlowScopes forest index answers
unit presence and edge transfers. Crossing a known inactive context ends the unit,
including break, continue, return and throw paths that bypass normal LeaveScope.
Static lexical containment is not dynamic cell equality across repeated visits.

TransferBinding is the common gen/kill entry consumed by liveness/demand, reaching
definitions, coverage, constants, copies and normal domains. Definite departure
stops earlier-unit values. Unmodeled cleanup instead keeps may-demand and reaching
definitions, while marking all-read coverage unavailable and substitutable domains
unknown. Treating unknown as definite departure would unsoundly delete a live write;
the independent counterexample and production mutation exercise that distinction.
The old point-only transfer paths are retired in these clients. Explicit allocation
resets remain part of the same semantic transfer, not optimizer admission rules.

Initialization completion now also requires the canonical unit to be active at the
read. Allocation/initialization dominance alone cannot certify a departed unit.
The initialization product retains shared immutable scope facts rather than borrowing
a destroyed graph or copying its index. Legacy completion/value-only inputs retain
their order/demand contract without acquiring a binding-unit identity certificate.
Current and candidate graphs use the same adapter and revision invalidation; pass
order, rounds and budgets are unchanged.

This closes the canonical abrupt-departure substep, not backbone deliveries2/3.
Projected declaration narrowing, native-for fresh environments, closure transport
and precise finally completion contexts still need complete lifetime deltas. Global
cleanup dispatch is explicitly unmodeled, preserving demand but limiting substitution
precision. Original IR/protocol correspondence, dependency-driven pass convergence
and unified-variable final acceptance remain open. No appearance optimization or
paused corpus/resource/agent work resumes. Measured evidence belongs to status.md.


### Current projected allocation correspondence (rules259)

SourceScopeProjection now carries region-indexed current allocation provenance.
The existing shared lexical census publishes canonical block allocation sites once;
source position intervals continue to prove name containment independently. The
flow adapter resolves a ProjectedBindingScope through this correspondence, replacing
its original SymbolId scope unit. A narrowed declaration therefore lives in the
current block, including repeated entry and abrupt departure handled by the common
TransferBinding model. No region/statement ID cast, name, sequence or shape inference
is used to recover the allocation.

Missing correspondence and omitted allocation blocks remain UnmodeledBindingLifetime.
Native-loop regions explicitly publish LegacyLoopAllocation: the preexisting placement,
confinement and capture contract retains completion/value analysis, with no canonical
unit or dynamic cell identity certificate. This preserves the existing fast path
without enlarging it; copied iteration environments must replace this order-only
contract. Neither alternative inherits an unrelated original outer unit. The shared
initialization, may-demand/definition and substitution consumers retain their existing
independent contracts. Canonical original bindings keep their original owned unit;
current and trial plans use the same region map and revision invalidation.

This retires original-unit fallback specifically for projected declarations.
It does not finish all lifetime deltas: native-for allocation, per-iteration copied
bindings and closure transport require explicit completion-aware unit events; precise
finally contexts also remain open. Existing capture/name/confinement safeguards are
not removed. All five backbone deliveries, pass convergence and milestone acceptance
remain open, with no appearance optimization or paused work resumed.


### Current loop entry environment projection (rules260)

PlanLoopDeclarations now publishes the complete current entry ownership product
separately from its commit delta. Even an unchanged grammar plan has entry facts.
Its suffix is selected from retained declarations and the actual projection's
omission span; an omitted declarator alone creates no lexical declaration group.
The final grammar pass and current/trial semantic preparation consume this same
provider. kLoopEntries is an explicit flow/expression provider prerequisite; the
grammar pass cannot request unrelated flow analysis during its commit. No new
optimization round or scheduling stage is introduced.

ProjectLoopEntryFlow adapts the grammar product without modifying the recovery
plan. Outlined prefix assignments cease to be source declaration initializers,
retain their original binding environment, and preserve any undefined assignment
completion. Prefix reads and descendant occurrences use canonical enclosing
positions from the existing lexical census. Missing positions remain unmodeled;
prospective loop ownership is no fallback. Native declarators keep their current
loop ownership. Replacements/motion use their actual statement owner in the shared
flow, so initialization, name resolution and all downstream value clients consume
the adapted projection rather than independently classifying initializer roots.

This retires prospective-loop ownership and InitializeBinding for outlined entry
prefixes in the shared semantic preparation path. It does not allocate an actual
loop environment or clone per-iteration cells: native declarations still have the
explicit LegacyLoopAllocation completion/value contract. Initializer/condition/body/
update environment phases, captured-cell identities and precise finally contexts
remain required model extensions. The old placement/confinement/capture safeguards
remain unchanged. All five backbone deliveries and final milestone acceptance are
open. Tests and measured verification belong to status.md.

Declaration placement additionally plans the entire initializer proposal through
this same provider before entering commit. A candidate confined to an outlined
expression prefix is not published as a native loop declaration. Otherwise later
copy/dead-effect removal could expand the grammar suffix and relocate that binding
after an alias had already proved its old environment. Prefix assignments keep
their original declaration; final grammar cannot promote a previously rejected
prefix initializer into a new environment. This is the existing expression-versus-
ForDeclaration grammar obligation applied atomically, not a new loop-shape gate.
No additional placement pass or optimization round is used. The proposal's owned
prefix mask is computed before commit; no analysis is queried during publication.

The source-level counterexample exposed a real cross-pass error in the initial
adapter-only implementation: after a copied slot disappeared, a factory value
became a for-let declarator yet its post-loop read retained that name. Independent
execution detected the ReferenceError. The corrected proposal keeps the factory
value in the enclosing environment, allowing safe copy propagation without later
lifetime expansion. Original factory/return/fault comparisons are unchanged.


### Rules261: actual native header allocation and member ownership

ECMA-262 2025 14.7.4.2 creates every lexical BoundName before evaluating
the first RHS; 14.7.4.3/.4 separately creates copied iteration environments.
Reference: https://tc39.es/ecma262/2025/multipage/ecmascript-language-statements-and-declarations.html#sec-runtime-semantics-forloopevaluation
This change implements the first obligation in actual current/trial preparation.
It does not implement copied iteration units or captured-cell equality.

LoopDeclarations publishes complete environments and actual retained binding
members, independent of its mutation delta. Omitted roots are not allocation
entries or members; no-op grammar products retain both facts. ProjectLoopEntryFlow
publishes owned binding-indexed NativeLoopAllocation records, separate from its
lexical region map. A region peer is not a BoundName and cannot gain allocation
by lexical containment. The preliminary region-wide publication was rejected by
a new counterexample and removed before final validation.

FlowBuilder applies shared BindingProjection once to actual members, indexes
entry owners, and publishes kEnterLoopEnvironment before any native RHS. Every
member records that common allocation point; unrelated bindings retain their own
allocation capability. Native entry generation kills the previous incoming value
through ResetsBinding/TransferBinding, consumed by demand, liveness, definitions,
coverage, constants, copies and normal domains. AnalyzeFlowInitializations requires
allocation to dominate its selected declaration completion; the same trial builder
rejects a completion preceding a proposed allocation. No old certificate is borrowed.

The old native initial-allocation absence is retired for modeled header members.
LegacyLoopAllocation remains only an explicit scalar/completion capability for
iteration phases/other unmodeled loop bindings. Neither static entry identity nor
scalar preservation proves cell identity across clones. Initial initializer/test
closure cells, normal/continue copies before increment, body scope relocation and
precise finally/capture transport still require actual common completion programs.
No operation/layout/sample admission or pass round/order/budget change is added.
All five backbone deliveries remain OPEN; status.md owns bounded final evidence.


### Rules262: common source loop completion phases

Current and trial BuildProjectedFlow now pass the same immutable LoopSyntax
program used by printing to FlowBuilder. The constructor consumes it synchronously;
RecoveryFlow owns indexed condition/body/increment/exit points and never retains
the borrowed syntax pointer. PrepareRecoveryProjection's bootstrap/refinement
rebuilds preserve this input. Raw legacy synthetic graphs explicitly lack source
phase capability; their behavior does not certify native iteration units.

The program's condition and increment operands execute in the actual enclosing
scope, outside the body activation. Expression-program blocks allocate no source
block unit. The original continuing arm is excluded from the condition program
and scheduled exactly once inside the actual body. A body normal completion or
owned continue reaches the increment boundary; an increment-program continue
completes that clause and reaches the next condition. Owned break reaches exit;
return/throw retain the shared conservative cleanup model. Body activation and
leaving, actual statement ownership, effect/exception paths and lexical gen/kill
therefore agree with the represented source execution positions. Header transfers
retain a typed control obligation in FlowStatement: the graph's kJump route
represents predicate/clause completion, not a printed break/continue statement.

Required grammar positions are independent of computation effects. The shared
FlowStatementRetention marks callable/loop bodies, ordinary conditional arms and
try/catch/finally positions, including the retained continuing arm. Common inert
elision consumes that role; its old tree.body exception leaves the decision path.
Required empty syntax does not invent observable effects or force retention of
an otherwise inert ancestor. Clause expression-program positions stay optional.
The initial phase-only implementation omitted an empty native body while printing
still required its container; the existing projected-origin check rejected that
plan. The correction preserves the original range and behavior assertions.

This retires the raw-body-around-all-phases graph path for modeled source loops.
No consumer infers movement from an opcode sequence, adjacency, graph shape or
sample. No new syntax admission or optimization round/order is added. Existing
LoopSyntax's bound/labeled-block and expressibility coverage gaps remain explicit;
this change neither expands those local paths nor treats them as language laws.

These phase points are prerequisites for copied iteration environments, not
substitutes for them. ECMA-262 2025 14.7.4.3/.4 creates a copied environment before
the first condition and after normal/continue completion before increment:
https://tc39.es/ecma262/2025/multipage/ecmascript-language-statements-and-declarations.html#sec-forbodyevaluation
The explicit old-value read, copied-value definition and newly allocated cell
identity have not been implemented here. Initial/captured cells, precise finally
transport, complete original IR correspondence, trial lifetime deltas, dependency
convergence and final acceptance still need the common model. All five backbone
deliveries remain OPEN; status.md records bounded verification.

### Rules263: explicit native-loop copied scalar inputs

The same current/trial/bootstrap/refinement flow producer now consumes actual
native BoundNames and publishes two scalar transport programs: before the first
condition and after body normal/continue completion, before increment. Header
clause continuation still reaches the condition directly; break/return/throw do
not enter the next transport. Missing source LoopSyntax retains the documented
legacy phase gap instead of fabricating copies from raw body layout.

Each program obtains all old binding values before defining the new scalar
values. `FlowCapturedValue{point,binding}` identifies the retained read occurrence;
`kIterationCopy` distinguishes this definition from a source assignment or lexical
declarator. It has no fabricated ExprId, declaration site or original SSA write.
This is the scalar plane of ECMA-262 2025 14.7.4.3/.4, not the allocation/cell plane:
https://tc39.es/ecma262/2025/multipage/ecmascript-language-statements-and-declarations.html#sec-createperiterationenvironment
No externally observable computation occurs within this implicit scalar transport;
cell allocation and resource-failure modeling remain separate existing contracts.

Liveness and observable demand activate an old-value input when its destination
definition becomes live; merely building an implicit copy cannot retain dead
values. Reaching definitions consume the resulting shared facts. Contextual
constant and normal-domain equations use the captured read point, including when
a definite reset lies between that read and commit. A later conflicting write
must not change the retained input. Ordinary normal-result current-binding copy
queries remain a different contract; available-copy analysis cannot replace an
earlier captured value with a current mutable parameter root. No additional
same-block, adjacency, operation list, sample or shape condition is introduced.

Member indexes are built once from the actual adapter product, not inferred from
region peers. Both programs are owned by the returned graph, including their phase
boundaries; no borrowed syntax, scratch input or previous revision survives.
The flow dependency already includes the shared loop-entry provider. No new pass
round, order, hand-maintained analysis refresh or mutation from an analysis query
is added.

The implementation intentionally retains CompletionOnlyLifetime for native
iteration bindings. It does NOT allocate fresh per-iteration units, prove captured
cell equality, or replace declaration dominance with reset-aware must-initialize
analysis. Ordinary/catch lifecycle precision, full original correspondence,
complete trial lifetime deltas, dependency-driven convergence and milestone final
acceptance remain OPEN. Next migration must add actual unit allocation and its
initialization proof together; inserting a reset alone would discard the old
input or wrongly certify a missing initialization.


### Rules264: scalar unit epochs and shared must-initialize

Actual native BoundNames with modeled source phase programs now have a typed
LoopBindingLifetime. First and repeat transport capture every old scalar input,
switch CURRENT units at kEnterIterationEnvironment, then initialize the new unit.
An owned kLeaveLoopEnvironment ends a normal or owned-break exit. The initial
header allocation is still a separate event before its declarator evaluations.
Static loop/ScopeId sites identify allocation origins, not equal dynamic cells.
The native forest preserves active parent units during nested-loop entry/reset.

This is an abstraction of scalar epochs, not heap allocation ordering or closure
cell reconstruction. ECMA-262 2025 14.7.4.4 allocates the unexposed environment
before reading the previous values, initializes each binding and publishes the
new LexicalEnvironment after completing those implicit operations. Plain lexical
reads and initialization here execute no user code before publication:
https://tc39.es/ecma262/2025/multipage/ecmascript-language-statements-and-declarations.html#sec-createperiterationenvironment
No captured-cell equality or resource-failure capability follows from scalar
transport. Legacy loops without an actual modeled source program retain their
explicit CompletionOnlyLifetime precision gap.

FlowScopes owns canonical and native forests and their contexts. EnclosingScope
means its ancestors remain active while descendants are unknown, as needed by
conservative cleanup; it does not make all nested units active. Native known
ancestors likewise do not certify unrelated pending units ended. Missing source
entry/movement contexts are UnmodeledScope. Only explicit Incoming/leave facts
certify inactivity; unknown transport preserves may definitions/demand and
invalidates replacements. The first old initializer's live demand is tested.

AnalyzeFlowInitializations is a read-only shared analysis in flow_initializations.cc.
Parameter/catch/self, actual declaration initialization and successful iteration
copies generate initialized states. Scope/loop entry, replacement and leaving
kill them; ordinary assignments/updates are not initialization. All normal,
exception, resume and conservative-cleanup edges participate in intersection
must equations. After-event roots include actual references/reads/definitions
and captured inputs; additional candidate positions are explicit query inputs.
Unreachable syntax is vacuously completed but still requires source name checks.
A missing reachable query certificate remains conservative.

FlowInitializations::Completed no longer tests declaration dominance or privately
rechecks scope presence. Declarations remain placement anchors only. A common
sufficient certificate checks that the dominating gen's subtree has no kill of
THIS unit. Absence of that certificate enters the same full equations, never a
second recovery admission. Basic-block contraction and indexed local actions
avoid repeated instruction walks. Heavy-path context differences plus an offline
Fenwick sweep batch all unit/gen certificates; a reachable-gen index handles
bottom states without backward expansion. These are analysis algorithms, not
same-block or shape requirements. Budget exhaustion remains typed.

Storage sharing requests representative states at actual reference/definition
points from the same immutable graph, then its initialization and retirement
consumers use those results. This is query demand, not a plan revision, analysis
query mutation or call-site invalidation workaround. Current/trial preparation
uses the same forest/flow producer; pass ordering, rounds and budgets are unchanged.

Independent dense initialization oracles, inactive/active/unknown scope oracles,
200/201/geometric and exact/one-short cases, eight production mutants and the
frozen source/record checks provide component evidence (see status.md). The nested
native model deliberately supplies the outer condition program; it does not
certify legacy syntax discovery. Complete original IR correspondence, precise
ordinary/catch/reentry identity, full trial lifetime deltas, dependency convergence
and final milestone acceptance remain OPEN. All five backbone deliveries remain
OPEN; agents/fullsuite/expanded corpus/RSS/benchmarks/DFX remain paused.

### Rules265: one complete synchronous declaration candidate

BindingPlanView borrows read-only spans from one unchanged plan. Owned overlays
replace spans before analysis; expression input has no second replacements or
transports authority. Grammar, normal-domain dependencies and conservative /
initialized / refined flow consume the same view. The legacy flow adapter's
omissions, initializer annotations, residual values, undefined stores, declaration
values and completions are overwritten from that view as well as value facets.
Source placement sites are explicit inputs to the common flow producer, including
trial sites. Native entry adaptation overlays this candidate before bootstrap,
not an implicit current plan. No borrowed span escapes synchronous construction.

Declarations stages a complete batch: proposed placement, initialization, old
declaration retirement and effect-preserving completion removal. Trial and commit
share RetireDeclaration; even native old declarations retire in that transaction
rather than relying on a later grammar pass to change semantics. The exact trial
flow's shared reset-aware initialization facts must complete ordinary moved reads;
common name resolution proves their binding identity and preserves every formerly
resolved retained binding/free-name occurrence. Existing first-completion and
confinement obligations remain independent prerequisites, not a replacement for
trial analysis. Escaping cells retain the documented legacy same-activation proof;
this component does not certify closure-unit reconstruction.

One batch is analyzed before any plan write. If its common obligations cannot be
proved, the whole batch retains the original representation: dropping members
from a verified namespace would require revalidating that actual subset. No new
local opcode/layout/adjacency/selection-count admission is added. Grammar prefix
restrictions are existing representation gaps, unchanged here. Trial scratch is
released only after every owner is destroyed; peak admission and cumulative work
survive. It does not enter the current revision cache or change pass order.

Remaining Task3 gaps include snapshot retirement/subset trials and other candidate
ownership/lifetime deltas; ordinary/catch/reentry cell precision, raw IR/SSA
correspondence, dependency-driven convergence and milestone acceptance remain
OPEN. This removes mixed current/candidate facet authority for the migrated
producer and completes declaration trial wiring, not the five backbone deliveries.

Rules265 verification exposed a genuine cost boundary: the 800-pair storage
source case retained 34,608,942 bytes of current analysis cache, including unused derived products while the full trial
needed another flow and exact initialization proof. It exhausted the unchanged
64MiB admission. RetainFacts now runs at every pass boundary from that pass's
closed RequiredFacts contract, retaining the coupled flow/expression bootstrap
and destroying unneeded derived products before releasing their admission.
It changes neither projection revision nor proofs, introduces no local
resource/shape fallback or rerun, and lazily rebuilds evicted facts at the same
revision if later needed. This shared cache ownership policy replaces retaining
unconsumed analyses accidentally until the next commit. Convergence, selective
mutation invalidation and minimal rerun closure are still not certified.


## Snapshot candidate transaction and restoration closure (rules266)

Task3 now sends the complete snapshot transaction through the shared producer.
`PlanSnapshotProjection` owns statement initializers, declaration values and
completions, omissions, replacement/transport and forwarded-origin facets.
Declaration retirement uses the same `RetireDeclaration` as declaration placement;
required outlined RHS evaluation remains at its original occurrence and handlers.
`Publish` moves those exact validated arrays into the committed plan. The old
ForwardSnapshots mutation loop and its partial three-facet trial are removed.
Unchanged aliases/storage/literals/read identities are borrowed from one immutable
parent view; no trial consults a different plan for a changed facet.

`PlanSnapshotRetention` owns moved-read/name queries and nested transport group
edges. Current source regions describe the bindings that can be restored; draft
owners describe actual moved occurrences. It delegates restoration to the existing
indexed name-retirement closure: rejected moves can restore shadows and retain
other affected moves; unrelated components remain eligible. Remaining references
to proposed storage impose explicit conditional def-use edges, so restoring the
transport also restores the storage its surviving read requires. This analysis does not select value
availability, effects, execution count, exceptions or captured cell identity.
Those prerequisites remain separate inputs from the existing common flow/motion
and the documented transitional environment census. No block/adjacency, sample,
opcode sequence, arena ordering or fixed selector count is an admission condition.

The proof phases are full proposal, indexed closure and actual selected transaction.
The final transaction is rebuilt by the same producer and its actual moved names
are checked before publication. Failure of the closure's exact-name invariant is a
typed model error, never permission to commit an optimistic subset. There is no
per-variable full-graph retry, no round cap and no syntactic repair. Scratch graphs,
views and analyses are destroyed before their admission is credited; replacing
plan arrays credits their old storage only after destruction. Work and peak
admission survive release. No semantic/resource budget is increased.

Delta construction is O(S+E+C), with statement/expression arenas and candidate
moves as dimensions. Name selection inherits the indexed solver's sorting and
sparse restoration work; group edges are O(C). Two distinct trial phases do not
introduce a binding-count graph multiplier. Tests exercise retained outlined RHS,
original/native declarations, actual publication, reversed IDs, symbol/free-name
restoration, independent versus nested components, and typed exact/one-short
budgets. Small namespace graphs have hand-derived expected selections. The new
end-to-end shadow combination also remains protected by motion prerequisites;
it is not the sole evidence for restoration closure.

This closes the known partial snapshot delta and unchecked subset path. Other
candidate consumers, complete raw IR correspondence, ordinary/catch/reentry unit
identity, dependency convergence and milestone acceptance still require delivery.
All five backbone deliveries remain OPEN; final bounded evidence is in status.md.


### Rules267: inline original results on the common completion graph

RecoveryFlow now publishes an OpId-indexed OriginalResult evaluation interval:
the checked source expression, its actual current entry and its normal continuation.
Omitted/unmapped raw results have no interval. This records correspondence, not
dynamic value equality, a completed store, or permission to move an effect.
The current source producer remains the authority for source-specific effects;
raw normal/before_throw flags cannot simply override language semantics.

AnalyzeFlowNormalKinds consumes the raw result ValueDefinition domain through
that interval and the existing sparse operand/completion invalidation graph.
Each interval adds one completion dependency from current entry to result;
changed replacements/transports seed their actual boundary even when the old
intrinsic event is gone. This covers path-dependent original ranges without a
per-result graph walk. The joined original normal_kinds array also contains
operand hints and is deliberately not used as an inline certificate.

Inline result precision and successful source-store precision are independent
inputs to the same equations. Losing one does not invalidate the other by fiat;
missing both, changed operands/path, unknown environments or absent source
boundaries retain current language domains. Original domains only tighten a
current domain when they are a subset, and the finite lattice still widens to
convergence. Shared expression/effect refinement consumes the resulting domains
in the existing producer order; no consumer edits a plan or retains old facts.

Tests include copied/reallocated SSA, split blocks, reversed source arenas with
real re-verification, producer/path edits, missing interval/store certificates,
and exact/one-short allowances. Generated sources are independently executed;
removing inline consumption or the completion dependency must fail the model
checks. Missing expression boundaries have their own RED→GREEN counterexample:
value-only clients keep solving source values, conservatively lose raw path
precision, and accept transported values. A third production mutant removes this
fallback and must fail. Raw effect phase/exception action correspondence remains unfinished;
all five backbone deliveries remain OPEN.


### Rules268: pending exception values and scoped current definitions

The verified TreeFlow catch table now transfers raw Catch ValueId/handler and
source body ownership into immutable OriginalFlow. Current catch activation is
a definition after scope allocation, separate from its staging writes. The
original domain is only a subset refinement while the shared operand/completion
cone remains unchanged; raw before_throw/normal effects still cannot override
language effects. Resolve-to-self preserves original unit ownership, not a
same-block or adjacency requirement.

The current graph publishes explicit throw payloads after operand completion.
A sparse join at each actual catch entry combines those values; implicit throws,
resume failures and unmodeled cleanup paths contribute Language, never the normal
result of the operation that failed. Catch activation and ordinary successful
stores use the same value dependencies and transfer/lifetime facts. Shared entry
indexes avoid scanning all throwing edges once per catch definition.

Scoped exposed bindings can carry known current values while their writes remain
observable. Required evaluations conservatively invalidate those values on normal
and exceptional paths; the current coarse effect class can also include evaluations
that do not mutate a captured unit. This is a precision gap pending common typed
mutation effects, not a language invariant. A successful explicit store generates
its own new value. Source allocation,
exit and reentry transfers still delimit units. Tracking is not write elimination,
copy identity, closure reconstruction or permission to move a computation.

The common preparation order now proves initialization, refines impossible read/
write TDZ completions, and only then constructs liveness and normal-value facts.
That intermediate expression analysis is pure, private to this exact projection,
and dies before downstream scratch admission. Final effect refinement remains
separate. This is bounded producer ordering, not a claim that the global pass
schedule reaches its full dependency-driven fixed point.

Decoded exception fixtures cover literal/Boolean/unknown throws, implicit numeric
conversion failures, success-only stores, copies, block splitting and register
reallocation. A complete transported-value trial recomputes the pending Boolean
instead of borrowing a raw numeric Catch range. Missing raw catch annotations
still allow current source joins to prove numeric values. Exact/one-short budget
checks and 200/201/400/800 growth stay at existing allowances. A scoped-state
counterexample distinguishes initialized reads from external mutation barriers.
All five backbone deliveries remain OPEN, including complete raw phase/action
correspondence, ordinary unit identity, other trials, global convergence and final
milestone acceptance. Other optimizations and paused verification remain paused.


### Rules269: completion-specific effects from the common language table

The existing exhaustive language table now returns SourceEffects: possible normal
and before-throw LanguageEffect masks and exceptional completion. Required/Inert
is derived from that same table, not a second operator rule. Current flow assigns
those phases at actual intrinsic, property receiver/key/access and update-read
positions; success-only declarative definitions separately publish lexical writes.
The shared bootstrap refines these facts for the exact current candidate.

AnalyzeFlowNormalKinds now invalidates exposed units from the actual edge's
UserCode mask. Normal/resume-next use normal effects; throw/resume-throw use
before-throw effects. Unknown graph clients retain the conservative Required
fallback, and unmodeled cleanup remains conservative. Observable writes and
allocation remain Required for existing retention obligations while their absence
of a callback no longer destroys a separate numeric definition. No motion or
elision permission follows from absence of UserCode. This retires the production
Required-equals-external-mutation decision for modeled source events.

This is the source side of the raw/source effect correspondence, not permission
to copy VM normal/before_throw flags onto normalized source. Raw operation,
protocol and phase/action mapping is still incomplete. Normalization can compose
several operations or introduce source guards/definitions; those events keep
the language adapter as their semantic owner. Object/class/helper protocols and
imprecise operand domains conservatively retain callback effects until their
shared semantics can prove otherwise; no local opcode whitelist is introduced.

The effect split follows ECMA-262 2025 edition 16 abstract operations:
[RequireObjectCoercible](https://262.ecma-international.org/16.0/#sec-requireobjectcoercible)
and [ToObject](https://262.ecma-international.org/16.0/#sec-toobject) can fail/allocate
without invoking a receiver hook, whereas
[ToPropertyKey](https://262.ecma-international.org/16.0/#sec-topropertykey)
may invoke ToPrimitive. This is an inference from their algorithms. Current
Unknown key/call/coercion effects remain conservative. The edition and fetched
source identity are recorded in ignored semantic-provenance.json; no new dependency
or runtime input was added, and the locked Ark and language profiles stay unchanged.

Tests distinguish pure observable commits, normal versus exceptional callbacks,
receiver/key/access phases, unknown fallback, resource modes and numeric coercion.
A real current SourceVariables/PrintFunctionTree path independently executes the
new source-adapter case; its synthetic original envelope is explicitly not a
raw-IR or closure certificate. Reverse arena order, exact/one-short allowances and
200/201/400/800 ordinary scoped definitions verify cost/candidate properties.
Three production mutants restore the coarse kill, use normal effects on throw,
or omit key callbacks; each must fail. All five backbone deliveries stay OPEN.

Rules269 final acceptance exposed a source-print admission regression at 800
bindings: the complete effect payload grew per-event padding to 136 bytes. The
current ARM64 layout now groups narrow fields before the size_t links and keeps
the typed construction prefix; it returns to 128 bytes with all normal,
before-throw and exception facts present. The default source printing case
passes at its original 64MiB allowance. No semantic narrowing or budget increase
was used. Frozen first-attempt failure/probe and repaired targeted checks are
retained; current final verification is recorded in status.md.


### Rules270: available copies of current ordinary scoped units

The existing available-copy solver now names the current value of a modeled
observable lexical unit, including local and catch activation, as well as the
already supported closed entry roots. Every generated and propagated relation
consumes `TransferBinding(root, edge)`, actual root writes, and
`MayInvokeUserCode(edge.completion)`. The carrier's own transfer remains a
separate obligation. Leaving/reallocating a root, or unknown cleanup, invalidates
an older saved value even if its carrier remains live and its ScopeId is reused.
Normal and exceptional masks come from the common source effects table.

The current `FlowBindingExposure` fact also conservatively treats ordinary
observable writes as potential external mutation. Thus even an actually closed
ordinary local retains its snapshot across an unknown callback. This is a model
precision gap, not a language invariant or a new call-shape admission rule.
Shared cell access/capture facts must eventually distinguish external mutation
from write observability; all value/copy/trial clients must consume that producer,
not add local callback exceptions. Scope transfer remains a separate obligation.

Private carriers inherit an available root through the shared source query at
successful copy commit. They do not become arbitrary immutable value roots;
that is a documented domain precision gap, not a language admission invariant.
Missing scoped lifetime/context likewise supplies no unit identity certificate.
No local/catch kind whitelist, name, sample, adjacency or block-shape admission
was added. The old entry-only root decision and symbol-only dependency kill have
been replaced by this edge transfer in the production `CurrentCopies` consumer.

This analysis is pure and revision-owned. `BindingValues` consumes its read
proposals after common initialization/name proofs; it changes neither writes nor
storage nor declaration lifetime. Demand cleanup and dead definitions remain
separate passes, with fresh common facts under the existing schedule. No printer
analysis or combined copy/store/delete decision was introduced.

Model evidence includes 40 local/catch phase/unit/write counterexamples and
16,000 independent finite path graphs with forks, cycles, writes, reallocation,
callbacks and reversed point/symbol IDs. The reference solver tracks concrete
current/saved equality without the production transfer, copy lattice or sparse
solver. Actual SourceAdapter output includes 96 functions over eight semantic
families, reversed arenas and 0/1/200/201/400/800 inert block variants: 5,184
independent executions, safe direct local/catch returns and preserved live
write/exit/reentry snapshots. Four generated-source snapshot mutants differ in
behavior. Exact and one-short lower-API budgets are checked without increases.
Four production mutants (no scoped roots/unit transfer/write kill, and wrong
normal mask on throw) build successfully and fail the model; restored model passes.

This advances the ordinary unit consumer, not the complete ordinary-lifetime
closure: captured cells, all raw phase/protocol correspondence, every trial
client, dependency-driven pass convergence and full milestone acceptance remain
OPEN. All five backbone deliveries stay OPEN; frozen 31/63/fixed-ABC verification
and all previously paused scopes remain unchanged.

Rules270 frozen final: build31 exit0 (194.11s), fixed fresh ABC exit0 (3.76s),
exact63 tests exit0 (976.59s), exact names/no duplicates. All six frozen code/test
hashes, input hash and selection remain unchanged. The 19 records still contain
10 complete modules/0 partial/9 no-module; all ten parse. ProgressBar alone changes:
its module-level uiContext copy and unused private declaration disappear through
the common copy/demand consumers, with no optional-chain rule change. Its paired
record gate passes88 cases/570 injected faults/114 import swaps/15 mutants/2
equivalent transformations, with platform execution explicitly false. The final
source consumer includes1277 outputs/199508 comparisons/156 mutants and the
16000 new graph oracle cases. One existing literal_plan kNoop switch warning is
retained in the final build evidence; no error. No backbone closure is claimed.

### Rules271: canonical external cell access, independent of write ownership

SourceCells is produced once for the unchanged Initializers owner; current and
trial adapters borrow it and publish only copied scalar access facts. Standalone
source projections use the same producer, never optimizer-specific syntax rules.
Closed/External/Unmodeled describe whether outside user code can change a binding.
Canonical function/class capture edges, live module aliases and sloppy simple
non-arrow mapped parameters determine access. Incomplete class/control capability
conservatively widens otherwise closed facts; known external facts remain external.
Value calls print as indirect calls, so they cannot acquire direct eval; SourceTree
has no direct-eval/with syntax. Adding such syntax would require updating this
producer, not creating exceptions in copy/type consumers.

Observable writes, private storage, successful definition domains, raw entry-domain
certificates and lexical allocation identity remain separate contracts. A closed
source parameter cannot acquire an entry-domain certificate from access alone.
Storage projections aggregate canonical member access; read-value aliases cannot
make a cell closed. CurrentCopies and normal-kind completion transfers consume
MayBeChangedByUserCode together with the actual completion's UserCode effect.
Root/carrier lifetime transfer, real writes, initialization, name resolution and
all independent removal/storage/declaration passes are unchanged.

Retired from the actual source path: observable/private exposure as callback
mutation proxy, and repeated class/function capture inference in Initializers.
The absent-access fallback is confined to legacy hand-authored graph contracts;
source adapters always publish an explicit fact. Captures remain conservative:
reachability is not read-only proof or full captured-unit reconstruction. Other
raw IR/trial/lifetime/pass convergence gaps and all five backbone closures remain
OPEN. SourceCells requires the tree and prepared class bodies to stay immutable
for its lifetime; recovery-plan mutations only project those canonical facts.
The existing tree verifier constrains member calls to MemberExpr, while value
calls retain the printer's indirect-call envelope; names never select this fact.

Evidence before the frozen final:108 emitted scoped sources/5832 executions,
1289 aggregate outputs/200156 comparisons/157 execution mutants; four production
mutants build and fail. Owner mismatch, mapped/simple/arrow boundaries, missing
class/control, live module aliases, storage reachability union, distinct declaration
initialization, normal/throw phase masks and exact/short budgets are tested.
200/201/400/800 census work3628/3650/7240/14442 and bytes729/738/1465/2929.
Frozen31 build0 (196.78s), fixed fresh ABC0 (5.37s), exact63/63 tests0 (971.04s).
JUnit names exact/no duplicates; all12 frozen code/test hashes, input and selection
unchanged.19 records still10 complete/0 partial/9 no-module; all10 parse and are
byte-identical to270. Final1289/200156/157 execution evidence and census/owner/
budget/phase model checks pass. Existing literal_plan kNoop warning retained; no
error. Evidence:build/stage-one/cell-access-verification; output:cell-access-final-
rule271-verified. No closure, device execution or whole-corpus claim.

Rules271 remaining unit-domain gap confirmed in current consumers: available-copy
roots can use modeled ordinary and loop units, while normal-kind tracked observable
units still use ScopeBindingLifetime only. Access separation does not close this
difference. Extend the shared unit transfer/capability model and migrate consumers
together; do not add a native-for syntax exception or claim complete loop types.

### Rules272: current-unit capability is not an entry-domain certificate

RecoveryFlow owns the constant-time BindingUnit query for enclosing, ordinary
scoped and native loop units. Copy/type consumers retire their separate lifetime
variant admission lists. Point/edge transfer remains the sole dynamic identity
owner; missing allocation forests are Unmodeled, never preserved scoped units.
The pure query uses the immutable current/trial graph and adds no cached state.

A current enclosing cell does not certify raw signature domains. Parameter
normal-kind generation independently requires the entry-domain adapter proof and
raw correspondence; otherwise it invalidates the raw dependency cone. Actual
UserCode completions and canonical external access still invalidate current
values. A saved scalar carried into a new unit retains its scalar domain, not
identity of the previous cell. Declaration placement, storage, removal and grammar
remain separate owners. Occurrence copy and independent carrier removal must not
be mistaken for an all-definition binding invariant, including mapped parameters.

Forty explicit graph/scalar cases, current-unit/missing metadata contracts, raw
entry negatives, 24 source programs/2688 executions/8 adverse source mutants and
three production mutants passed. Existing1289/200156/158 source evidence passed
in the targeted checks. Source variants include closed numeric loops and callbacks
mutating a captured binding, conversion failure, arena reversal and200/201/400/800
inert-scope growth. Synthetic source-adapter envelopes are not raw IR certificates.
Frozen31 build0 (198.13s), fixed fresh ABC0 (3.77s); first full63 run62/63 (1018.77s),
then the affected recovery_flow_test rebuilt and passed1/1 (22.58s) after supplying its
known-scope fixture indexes. Production and the original8 frozen hashes stayed
unchanged; the final9 hashes/input/selection are verified. Consolidated current
names63/no duplicates/all pass; first failure records remain and no single full-run
exit0 is claimed. All five backbone closures remain OPEN; full raw phases,
update-domain precision, captured-unit reconstruction, remaining trials, dependency
convergence and milestone acceptance continue before other optimizations.

The fixed manifest retains19 records/10 complete/0 partial/9 no-module and the
language_numeric_constructors_v1 contract; all10 sources parse. Only ProgressBar
changes: a private captured-unit snapshot is replaced by the available current
read, with its old read completion preserved. Optional-chain rules are unchanged.
The paired gate passes88 cases/570 faults/114 swaps/15 mutants/2 transformations,
with platformExecution=false. One preexisting literal_plan kNoop warning remains.
Evidence:build/stage-one/unit-capability-verification/consolidated-result.json;
fresh output:build/stage-one/unit-capability-final-rule272-verified.

### Rules273: completion provenance belongs to the adapted graph

The current/trial flow adapter publishes Preserved, Changed or Unmodeled
provenance at execution boundaries. These states describe whether conditional raw
operand/path certificates remain available; Changed is not a semantic rejection
of the proposed source. Source effects, successful writes and binding units retain
their independent meaning. In particular, a raw operation may map only to an
operand expression, so its raw effect flags must not overwrite source effects.

Expression edits invalidate their actual boundary and replacement destination.
Statement deletion retains a linked, changed boundary even for a non-result
operation. Declaration projections and native loop phase/environment adaptation
publish the same fact. Forwarded-value and loop-declaration syntax facets still
travel through the transitional adapter explicitly; they cannot silently inherit
an old raw completion certificate. Current and trial preparation use one adapter.

AnalyzeFlowNormalKinds no longer reads projection facets to rediscover execution
edits. Its existing sparse dependency graph consumes event provenance and current
expression boundaries, including Unmodeled inputs. Canonical value equations cannot
revive a raw path proof after a non-value action was removed. This is protection
of conditional raw certificates, not permission to mix arbitrary stale value
analyses with a newer plan. Revision-owned pass products remain mandatory. The new fact is owned by
RecoveryFlow inside ProjectionFacts: BeginCommit destroys the aggregate before
plan writes, and CurrentFlow rebuilds through the same current/trial provider.
It introduces no separate cache or caller-managed invalidation order.

The direct regression removes a verified non-result action, then supplies the
current flow with canonical value equations: old code incorrectly restored the
raw finite range. The current graph withdraws that range while preserving the
source Number domain. Tests distinguish unchanged/current/unknown provenance,
statement and predicate edits, block splits, SSA and source-arena permutations,
200/201/400/800 copies, and exact/one-short budgets. Independent execution uses
engine output; debugger observations are retained in emitted canonical source.

Full raw phase/protocol/action correspondence is still incomplete. This change
retires one duplicated completion-validity decision, not the original-results
mapping or the transitional projection adapter. All five backbone closures remain
OPEN; other optimizations remain paused.

Rules273 frozen verification:31 targets build0 (102.04s), fixed fresh ABC0 (3.98s),
exact63 tests0 (1007.73s), complete names/no duplicates. Seven frozen code/test
hashes, selection and input SHA stayed unchanged. Ten sources parse and are byte
identical to272;19 records remain10 complete/0 partial/9 no-module with the same
language contract. The paired ProgressBar gate passes88/570/114/15/2 and reports
platformExecution=false. Two production mutants fail as intended and were
restored. Evidence:build/stage-one/flow-provenance-verification/final-result.json.
All five backbone closures remain OPEN; this is a verified migration substep.

### Rules274: captured numeric inputs and success-only definitions

Update actions now publish their pre-conversion read through the existing
FlowCapturedValue input. The common sparse normal-kind graph applies a numeric
category transfer separately to the expression result and the successful store.
It never samples the current binding at commit: a conversion hook may have changed
that cell. Throwing edges bypass the store and retain the state actually reached
before failure. The same captured-input contract already serves iteration copies;
demand and initialization consume the input without interpreting an AST pattern.

The transformation is not a copy proof. Number, String and Boolean/null inputs
produce Number on normal numeric completion; unknown inputs may produce BigInt
and remain Language in the current lattice. Finite/range refinements are widened,
so postfix result precision cannot be lent to the written value. Prefix/postfix
and increment/decrement share this category property while keeping their existing
native operation/value semantics. The ordinary unary numeric equation uses the
same category transfer. An unresolved captured-input cycle remains unknown.

The normal-kind consumer's unconditional kUpdate-to-Language branch is retired
when the shared input is present. Missing input still has explicit conservative
behavior. No transformation pass, admission predicate, fixed layout or opcode
sequence was added. Conversion effects, observable write retention and successful
binding initialization remain independent; precise update effect refinement is
not inferred from the result domain. All products remain revision-owned and use
the common current/trial provider.

Tests exercise captured value versus later cell writes, failed conversion and
missing input, reversed point IDs, prefix/postfix, both directions, catch reads,
Number/BigInt/NaN/Symbol and user coercion hooks. Engine-produced source tests also
combine updates with loop units and mutating closures, reverse source arenas and
200/201/400/800 scope growth. A trial removing the numeric producer must withdraw Number and restore Required
coercion without borrowing the committed projection. Reentrant coercion can
invoke a captured setter and then return BigInt; independent execution checks
the resulting value and effect order. Source-adapter tests with an OriginalFlow
envelope are not raw-IR correspondence certificates. Full raw phase/protocol mapping and
the other backbone closures remain OPEN.

Rules274 frozen verification:31 targets build0 (100.05s), fixed fresh ABC0 (3.77s),
exact63 tests0 (999.32s), complete names/no duplicates. Seven frozen code/test
hashes, selection and input SHA stayed unchanged. Ten sources parse and remain
byte identical to273;19 records retain10 complete/0 partial/9 no-module with the
same explicit language contract. New/current source evidence includes48 loop/
capture programs6720 comparisons16 mutants and32 update/catch programs2176
comparisons96 mutants. Two production mutants were caught and restored. All
five backbone closures remain OPEN. Evidence:build/stage-one/captured-numeric-verification.

## 规则275：初始化与逐边转移共享单元存在事实

RecoveryFlow::BindingPresence 在同一当前/候选图上组合静态单元能力与
occurrence 前后作用域事实。初始化的局部 gen/kill 和跨块 must 求解、
逐边值/需求转移共同消费它；缺失普通 scope 索引不能因为 gen 与 use
落在同一个分析块而被当作已初始化。批量支配证书也消费 BindingUnit，
不再仅用 Unmodeled lifetime 标签判断能力。CompletionOnly 图保留其
显式非词法契约，未知单元不等于确实离开；需求保守保留。

分配、重复进入、成功定义和异常绕过仍由独立的事件/边表示。本次不
改变稀疏块压缩、重路径和 Fenwick 批量证书算法；Scope/Loop 的 reset
索引只是求解加速，不能成为恢复准入。当前/试算均走既有共同提供者。
完整普通单元重构及五项主干交付仍未关闭。

## 规则276：同一 revision 只发布一个完整共同产品

CurrentProjection 一次拥有表达式、flow、初始化读取事实，所有 pass
查询返回这个完整 RecoveryProjection 的 const 子视图。共同 bootstrap
结束以前不发布产品；BeginCommit 在修改计划前销毁整个 owner。最终
输出阶段才能移交打印所需的向量，分析阶段不能更新已发布表达式事实。

退出 AnalyzeExpressions 单独生成保守结果、随后 CurrentFlow 覆盖其
精度的路径；也退出分立 InitializedOccurrences 的重复 revision/所有权。
这解决同一 pass 遍历候选期间因调用 flow 查询而改变事实的问题。保守
表达式分析仍是共同提供者内部的纯步骤，不是另一种可缓存的 pass 结果。

这是依赖调度的前置契约：revision 内分析精度固定，后续才能可靠定义
pass 已完成与需要重跑的条件。全局依赖驱动收敛和五项主干交付仍开放。

## 规则277：调度器拥有消费者与阶段的前置依赖

PassContract 显式声明 ValueCleanup、ReadValueCleanup 前置职责；
RecoveryBarrier 表示需求清理、存储、快照刷新、读取值和语法边界。
Prerequisite 只读取当前阶段/检查点，返回下一位职责 owner，不执行
分析或写计划。执行器先完成该 pass 的独立分析与提交，再重新查询
前置依赖，最后开始消费者。直接跳过依赖的 Start/Arrive 仍拒绝。

退出 Initializers 中 EnsureDemandCleanup 和手写三步清理实现；
声明/别名/语句值消费者的依赖由契约触发，阶段切换也由同一解析入口
完成。有意安排的清理阶段使用显式 barrier；这与某次读取能否替换、
初始化能否删除等语义义务不同，不能用调度完成状态替代分析证明。

依赖遍历只推进固定的职责图：尚未结束的清理依次完成 initial values、
bindings；缺失的 snapshot/binding-values 尝试执行一次，随后完成其
触发的清理。完成清理组才在最新revision发布检查点，自己的提交不会
形成反复入队。未知/错误阶段在执行依赖前拒绝。全局pass机会的收敛、
更细失效范围及其成本仍未完成；本次不把检查点当作全局不动点。


### Retired mixed raw domains (2026-10-10)

OriginalFlow no longer computes or stores an ExprId-indexed join of raw operation
results and operand domains. Production already obtains conditional result proofs
from original definitions and current completion intervals; the old table was an
unused alternative semantic channel. OriginalInput retains operand/resolved/use
identity, and source projections continue to compute their own normal domains.
Operand identity and bounds checks remain at the preparation boundary.

Tests now select raw finite inputs through occurrence ranges and ValueId domains,
not the retired join. Synthetic trial tests retain numeric input correspondence
while requiring projected reaching values to determine the answer. This removes
one obsolete fact producer; it does not deliver full operation/phase/exception
correspondence. No recovery rule or language contract changes in this cleanup.


### Rules278: revision-specific stable pass attempts

RecoverySchedule records a stable revision only when a pass finishes without
Commit. A modifying pass has not proved a fixed point of its new output; its next
request still executes. Any later projection revision invalidates all recorded
no-ops by comparison, and stage transitions clear them because prerequisites and
legal responsibilities change. This is conservative dependency invalidation.

RunPassOnce always performs Start, cache retention/release and Finish. Only the
analysis/transform body is skipped for an already stable attempt. In particular,
cleanup owners still advance the ordered group and publish its demand checkpoint;
a cached no-op must not leave the prerequisite driver stuck. No semantic fact is
borrowed from an old projection, and no plan write occurs during skipped work.

Storage is one optional revision per typed pass, independent of input size. A
request costs constant scheduling work; repeated requests on one unchanged
revision compute once. A modifying attempt never skips the next request. The
existing work budget still charges every request. This eliminates duplicate
unchanged attempts but does not yet discover or schedule all newly enabled
opportunities; fine-grained change dependencies/global convergence remain OPEN.
