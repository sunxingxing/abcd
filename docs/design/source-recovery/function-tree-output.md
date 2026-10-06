# FunctionTree 与功能优先的部分源码输出

2026-09-28 用户讨论后的设计修订；本文件是当前输出策略与投入顺序的依据。
这是设计变更，不代表相关接口已经实现。旧文档中“验证/发布义务全部闭合才允许
输出”、新增 budget/receipt 设施作为功能前置的要求，由本文替代。

## 1. 最终表示和职责

最终表示就是现有 `FunctionTree`：语句/表达式 AST，加符号、作用域、捕获和来源
关联表。沿用 C++20 variant、强 ID、连续节点数组和共享 printer；不新增“最终 IR”。
RegionPlan 表达控制组合，EvaluationPlan 决定表达式和临时变量，绑定/cell 分析
决定声明与捕获；它们共同构建 AST。CFG 保留作分析依据，不直接驱动文本打印。

目标打印入口直接消费 `const FunctionTree&` 及输出上下文。FunctionTree 不必先
经私有工厂成为“已验证且不可变的树”；不增加 verified 标记、准入 token 或新的
VerifiedFunction 包装。打印期间树不可被并发修改，这是普通所有权要求。
已有 VerifiedFunction/PreparedFamily 和严格发布接口暂保留兼容，不集中拆除；
新功能及部分输出复用它们底层的同一个 printer，不依赖它们取得输出许可。

Printer 处理节点、名字、括号、转义、标签、注释和已有输出位置；不识别字节码
模式，不补推 CFG、finally 或闭包，不调用专项 body writer。无原名可用合成名，
无独立 PC 的结构节点保留结构来源；不补造原名、类型、PC 或可执行占位行为。

## 2. 部分输出是正常能力

允许在整体尚未成熟时输出已恢复片段，不因一个函数或子函数不支持而丢弃整份
文件的其他结果。函数前标记 INCOMPLETE，并在缺口处给出简短原因及可用原始 PC。
标记表示供阅读的部分反编译，不承诺语义等价或可执行；部分输出不能计为完整支持。

最小拟议表示复用现有错误枚举并增加：

```text
RecoveryIssue = (typed reason, optional original PC range)
UnrecoveredStmt = (RecoveryIssueId)
FunctionRecovery = (Complete | Incomplete, issues)
```

`UnrecoveredStmt` 是输出注释的缺口标记，没有可执行语义，不是任意源码字符串节点。
状态/问题表可以作为 FunctionTree 的附属结果；Complete 仅表示没有已知恢复缺口，
测试是否实际通过另行报告，不等同于发布证书或已证明等价。

实现边界：

- 能确定位置、控制归属和操作数的语句照常输出。缺少操作数或绑定时，扩大缺口
  到相关语句/区域；不造 undefined、return、throw 或空实现来假装恢复。
- 未恢复区域的值、效果、控制后继未知时，不把前后片段拼成确定的顺序，也不
  将后续依赖语句当正常代码继续打印。保留可定位区域，必要时收缩到已恢复前缀。
- 未知控制关系中的独立片段如需展示，只作注明位置的注释片段，不伪造 AST 嵌套。
- 子函数不完整时，在子函数前标记，包含它的函数/record 同步保持不完整；其他
  已恢复成员继续输出。签名本身未知时也明确标出，不凭空构造合法 callable。
- 部分输出作为整体仅做阅读/结构检查，不作为整个函数/record 的等价执行验收。
  其中已完整准备的成员可在受控环境中隔离做定向行为对照；不能调用缺口成员，
  也不能把这些局部结果计为 record、构造器或平台验收。完整测试函数仍对照
  返回值、异常、效果和捕获行为。
- 损坏节点 ID、内部不变量失败、无法可靠解码的输入不冒充普通能力缺口。停止
  受影响部分，保留已有可靠结果和错误信息；不能捕获所有错误后宣称部分恢复成功。

## 3. 保留的正确性工作与暂缓的 DFX

当前仍做：值可用性、phi copy 放置、效果求值顺序、handler 范围、cell 动态实例、
finally completion、await/yield 恢复语义，以及与正在开发功能直接相关的检查和
定向行为测试。无法可靠恢复就标记缺口，不用增加证明设施来阻止其他片段输出。
打印器保留索引/节点结构、字面量转义和写入失败处理，避免崩溃及损坏输出。

暂缓新增：预算逐容器计费、work 计数、预算失败注入、全进程 RSS/基准、通用证明
封装、不可变发布 owner、receipt 链路扩展、全面 origin census/SourceMap 审计。
已有检查、预算和映射先保留并复用，不批量删除，也不以新增 DFX 配套作为功能交付
前置条件。独立通用区域验证器后移；功能必需的控制/值/效果检查随对应恢复实现。

按用户 2026-09-28 的明确要求，默认源码产出不再配置 work 计数阈值：语义、旧
方法输出和派生构造器路径使用计数器完整范围，函数展开不得分割这一默认额度。
work 统计和调用方显式传入有限额度的底层兼容接口保留，但默认生产输出不能再因
原来的 100 万/1 亿 work 配额拒绝源码；这不解除内存、输入安全或写入错误检查。

保留有界算法、取消能力和现有资源保护；不引入明显平方扫描或无界任务。进程
1,000,000,000 字节仍是长期约束，本阶段暂停补强和实测，不宣称已经达标。

## 4. 交付顺序与验收

2026-10-06 用户调整：完成当前 `?.` 调用修复后，先支持阶段一实际
`INCOMPLETE / UNRECOVERED` 中缺失的反编译功能，再做 optional chain 等
表示细节优化。部分输出仍为正常能力，但完善标记和保留片段不能替代
将不完整函数、类及 record 恢复为完整源码及其独立语义/表示验收。

先交付共同 FunctionTree/printer 的部分输出，再贯通循环/catch，随后外层跳转和
异常闭包、finally、await、generator、async generator/yield*。图视图和 RegionPlan
只建设下一项功能需要的部分，不等待通用框架或全部 DFX 完备。

每项报告已恢复行为、实际定向测试、剩余缺口；部分输出可独立验收其标记和保留
片段，但不能冒充该控制组合已经完整支持。全 ISA、字段/全局/签名/函数体整合及
整份 record 的最终目标不变。演进任务见
[C00–C09 计划](../../superpowers/plans/2026-09-28-unified-control-regions.md)。

## 原生空值语法的表示（规则 104、105、107）

PlanNullishSyntax在已经检查的FunctionTree上解释null、undefined、other
三个语言值域，证明nullish路径返回undefined、其他路径恰好执行同一个
MemberExpr。纯谓词按typed SymbolId/ThisExpr.owner关联稳定读取，支持
严格比较、NOT、AND/OR及条件组合，不按两个guard、opcode序列、寄存器、
块布局、源码名字或样本选择。效果、异常与所有权仍由原贯通分析负责；
全局getter或不同binding不能凭同样拼写获得稳定身份。

共同printer用原MemberExpr的base/key输出?.name或?.[key]。外部member
与call通过括号结束本次optional chain；value-call沿用间接调用表示，不能
因成员文本改变this/direct-eval语义。nullish路径不计算key、不读取getter；
非nullish路径仍保留原key转换、receiver和异常。依据
[ECMA-262 2024 Optional Chains](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-optional-chains)。

不改FunctionTree、CFG、operand或原instruction来源。被省略的测试与原
分支节点在已打印base.end、原生?.之前保留零宽事件，位置序列化标记
optional_access及projection root。纯条件证明永不执行的opaque arm，也
沿完整AST所有权保留其调用/聚合子节点事件；不捏造有长度的源码片段。
printer、family emission与serializer使用同一计划，错误仍保留真实分类。

规则107进一步解释AssignmentExpr的语言语义：赋值表达式返回写入同一
SymbolId的RHS值，原整个assignment occurrence可以成为native base。
按null/undefined/other各路径传播reference formation、先前读取与执行
顺序，支持严格比较两种操作数方向、NOT、AND/OR、条件与Sequence组合。
这不是匹配“assignment后两个guard”的局部语法/字节码模板；RHS可包含
任意原有call/getter/复合效果，全部随原赋值保留，不按producer种类准入。
依据[ECMA-262 2024 Assignment Evaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-assignment-operators-runtime-semantics-evaluation)。

每条可能执行路径必须保留同一个原形成表达式，不能将其提前到旧值读取
之前或丢掉另一次写入，也不能从只在部分路径执行的保存中制造必执行base。
这是当前以一个原ExprId表示native base的能力边界，不是语言禁止多个
赋值；未能表示的情况继续打印原条件，不拒绝其完整函数或虚构源语义。
独立、分离的多次形成效果与一般alias transport仍需共同表示扩展。

OptionalMemberSyntax保留原member/key并显式指定已证明的base；原Symbol
base读取与其他消除的guard继续在保存表达式的end记录零宽来源。赋值本身
及任意RHS子树保持实际非零范围，nullish路径不计算key/getter。原AST、
control、operand、instruction origin、scope和exception证明保持不变。
后续规则127贯通下述原生optional call；整条链合并及未打印子函数owner
仍未完成，不能因为不可达分支伪造family发布或整份阶段一验收。

规则127补充：同一 null/undefined/other 抽象解释现在承载原生 optional
调用。nullish 路径必须返回 undefined，非 nullish 路径保留实际 CallExpr；
成员调用的真实 MemberExpr.base 必须等于受检 reference，值调用保留
实际 stored callable 身份和间接 eval。原参数及计算 key 的 occurrence
在非 nullish 路径按原顺序打印，已有 assignment formation 只保留原
求值一次；不会从相同拼写取得 receiver，也不会把条件之外的后续调用
吸收到短路范围。依据
[ECMA-262 2024 Optional Chains](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-optional-chains)。

OptionalCallSyntax 保留原 call、member 和 base；原 AST、CFG、异常范围、
参数与来源不变。共同 printer/事件输出/序列化使用同一个计划；被省略
的 guards 和被 formation 替代的纯 base 读取在 base.end 记录零宽事件，
member、call、计算 key 与参数保留实际区间。每个候选增加常数工作，
复用既有 O(N) arena 队列及 O(T log T) 文本索引；无逐候选子树回放、
新依赖或专项 writer。更长链组合、保存的成员 callable 与 receiver 的
联合 optional 调用等表示缺口仍开放；按当前优先级后移。

规则105将同一计划扩展为typed OptionalMemberSyntax/NullishFallbackSyntax
投影。两个nullish域返回同一fallback、其他域返回原稳定值时，使用现有
LogicalExpr kNullish打印value ?? fallback；不改变原AST或控制结构。
fallback是保留的原子树，可包含call、member、sequence及另一binding的
条件。它只在nullish路径执行，原getter、调用receiver、转换及异常仍由
原树负责；外部value-call沿用间接调用，??与&&/||混合遵守原生括号规则。
依据[ECMA-262 2024 Binary Logical Operators](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-binary-logical-operators-runtime-semantics-evaluation)。

fallback结果相等依据原ExprId、typed稳定身份或literal值；UTF-16字符串
先按内容排序规范化TextId，重复literal池编号不成为准入条件。false与0、
正负零及不同字符串不合并，effectful的不同执行occurrence不凭文本相似
合并。其他binding的谓词即使两支同值也必须保留，不能丢掉TDZ异常；
最终truth已知也不能使实际执行过的其他绑定读取获得纯guard资格。

省略节点及属性继续在原value.end记录零宽事件，serializer标记
nullish_fallback和projection；family emission与printer共用同一计划。
规则107使同一ordered formation也成为??的保留value；fallback仍是原
子树。独立副作用fallback occurrence的合并与一般alias transport保持缺口。

规则106在ABC/SSA层复用ProjectSelectionLogicals恢复已证明retained值的
原生??。四域（null/undefined/其他假值/其他真值）方程解释原严格比较、
NOT和truthiness；原first UseId、snapshot assignment、phi/control/effect/
exception与来源保持。效果读取的保存位置作为LogicalExpr.left进入共同
树及printer，不通过语法匹配assignment推断单次执行。
例如原已保存getter的展开式恢复为($value = object.property) ?? fallback；
fallback仍只在nullish路径执行，其他使用仍读取同一个保存值。

fresh TreeFlow证明后，operand不再重复要求unary/copy/branch producer
名单；原保存身份和selected edge等义务照常核对。复制、纯块拆分、反向
比较和任意重复guard不用新增匹配分支。PlanNullishSyntax的其他effectful
alias、optional member/call/整链与未打印child owner缺口保持。
条件之间的statement-only前缀仍是共同FunctionTree构建缺口，debugger
反例没有完整源码或行为通过，不能用忽略语句来扩大原生逻辑表示。

## 原生函数尾部完成的表示（规则 100、103）

共同 printer 可将实际 function body 最后一个直接子语句的 bare return 或
typed undefined literal return 表示为自然结束；FunctionTree、原指令来源和
控制计划保持完整。更早/嵌套 return、变量读取、复合表达式、call、await/yield
及 finally 中的 return 不在这个语法投影范围内。

规则103的 ImplicitTailCompletion 还允许 base/derived constructor 的实际
body尾部直接返回自身 ThisExpr，通过原生构造完成自然结束。当前实例身份
不变；derived this未初始化时，原生完成的GetThisBinding仍抛出ReferenceError。
不能移除try/catch/finally中的this读取，否则可能把异常移出原保护范围。
普通function/method的receiver返回、别名、属性读取、call和sequence均保留。
语法尾部是这个安全投影的边界，不是ABC恢复的块布局准入条件；还没有为
嵌套控制中的等价完成建立通用消除证明。依据
[ECMA-262 2024 Construct](https://tc39.es/ecma262/2024/multipage/ordinary-and-exotic-objects-behaviours.html#sec-ecmascript-function-objects-construct-argumentslist-newtarget)。

async generator 的有操作数 return 隐含 Await，连 undefined literal 也必须
保留，避免改变完成通知相对其他微任务的顺序。此区别来自语言语义，不是
按样本/编译器布局设置的准入条件。见
[ReturnStatement evaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-return-statement-runtime-semantics-evaluation)。

省略的原 return/字面量或receiver在结束 block 前保留共同零宽位置，序列化标记
tail_completion；module root 沿用 module_completion，位置在 module owner
末尾。位置消费方核对实际 body、owner 及前一个 sibling 的范围，不把这些
事件当作无来源节点或捏造有长度的源码片段。静态字段和隐式构造器原投影
保持独立位置约定。

## 类成员的输入证明缺口（规则 64）

`ClassTree` 的 wire-order 成员槽现为有类型的 optional PreparedFamily。
显式 AllowPartial 下，只有原成员根的 normal SemanticProofFailure 或 phased
ExceptionInputFailure 的 kInternalValue 可产生 `ClassMemberOmission`。
真正的 ClassTreeContext 仍从同一文件、profile、角色和 literal occurrence 解析；
它不是一个代替输入证明的 FunctionTree。签名/正文都没有恢复时只输出安全的
数值身份注释，不合成空函数、return、throw 或成功 source receipt。

`DraftPrepared` 独立检查每个缺失槽与 omission 的顺序、身份、原始创建 PC、
property、site 及类型化原因。`BodiesPrepared` 对任何 omission 返回 false；
phased parent 独立性和完整 default-export 移动仍拒绝该类。原 record 草稿和
共同 printer 可保留其他完整成员，输出 INCOMPLETE class 与逐个 UNRECOVERED
注释，并将 incomplete 传到 parent/record。缺失成员不获得伪造的函数位置表。

完整 family-write 扫描继续把整个实际 class creation 作为原有 class barrier；
它只核对该 barrier 的覆盖，不因此证明缺失成员的内部写入或执行许可。
缺失的成员没有 body、ABI 或 complete receipt。失败子函数、后续 tree/family
证明、损坏解码/analysis identity、资源/工作限额和构造器/initializer 失败仍
按原失败路径处理，不能用 catch-all 错误转换制造部分成功。

Phased 输入失败现保留原 ExceptionInputFailure，而非压扁为 kValues；暂停/恢复
归一化后的 PC 映射回原指令。正常/异常分析及 file/family 准备层保留可计费的
失败工作/分配，类恢复在继续前扣除这些保守费用。ordered omission 扫描与
两倍容量增长为线性；printer 使用固定数字缓冲，不复制整图或扫描所有成员找
每个缺口。200/201 个成员测试覆盖顺序、完整模式拒绝、缺失/错配声明和损坏输入。

阶段一 WindowUtil record7618 的 8620/8539/8431 仍分别缺输入证明，在该共同
路径下保留其他成员和完整 WindowInfo 构造器/字段。setUIContext、
setSystemBarEnabled 及后者 callback 的配对对照只检验这些恢复出的完整成员；
不构造不完整类、不调用缺失方法，也不声称该 record 或 HarmonyOS API 已验收。

## 父级模块初始化进入异常函数（规则 65）

共同 `FunctionInput::IsCreatorOf` 校验同一文件身份/record、真实可达创建位置、PC
和类型化创建操作，normal 与 phased reader 复用该接口。FilePhasedFunction
只在 Conditional 模式接收 ParentModuleInput；创建前的持久模块成员集合和原
store witness 保留在拥有者中。集合只表示初始化，不保存或冻结模块值。

未带 Hole guard 的本地读取进入已存在的 ProveExceptionModules initialized
路径。ExceptionInputProof 保留拥有者的不可变 load-premise span，PhasedBody
拥有其 move-stable arena；独立 importer 重新检查这些原坐标。带 guard 的读取
仍走原成对证明，没有把后续 guard 删除。源码别名、属性 getter 和后续读取仍
是同一实时模块绑定。

共同 ModuleGuards/ModuleEvaluation 接受 Initialized 单事件，独立检查真实
Before/Commit 和 SSA result origin；PhaseCorridors 计一次原事件，catch copies
保持原位置，不为不存在的 guard 制造事件或搬移异常快照。family-write 校验在
外层类根或真实 normal-parent 创建边再次核对初始化集合、读取槽和原 store 的
Language 域。无创建证明、闭合模式、晚写入、缺失 premise、ownerless publication
和限额拒绝保留。

文件内真实 class/member 读取覆盖创建前/后的区别、1/200/201 次读取、丢失
premise 与 guard-plan 声明、工作预算。真实 WindowUtil method8539 已恢复；
配对测试分开转换 enum 与类正文以保留 ABC 实际执行的 enum 属性读取，验证
setter/receiver、enum getter、条件分支和异常。两个数值缺口仍然存在，不能以
该成员或 Node 对照宣称整个 record 或平台语义验收完成。


## Generator RETURN 的显式清理退出（规则 101）

恢复保留调用者 RETURN payload 的清理路径时，不再要求原终止语句只处于
尾部 Block/If 链。共同 FunctionTree 的 BlockStmt 可携带 LabelId，BreakStmt
可退出真实祖先 block；ContinueStmt 仍只接受循环。标签分配、语法校验和
printer 共用这些类型，不按 opcode、样本或局部图形状选择表示。

原 preserving ReturnStmt 保留在清理外的 NEXT 审核路径。清理中原终止位置
放置指向该清理 block 的 break，正常退出 finalizer 后保留原 pending RETURN；
中间 finally 的返回或抛出仍能替换它。原终止的 operand/origin 不删除，不读取
或制造调用者 payload。逆投影核对实际 break 的所有权、目标、标签及 origin，
恢复原序列交给既有 SSA/control/operand 检查；源码事件沿用原 instruction
origin，不宣称新增一条 VM 指令。

RETURN arm 从原 resume adapter 的 fallthrough 事实与共同 TreeFlow 的分支
polarity 映射，不能把 native If 的 alternate 当作固定 RETURN arm。一次
迭代 Euler 遍历为实际 AST 的 Block/If/Try/Loop 所有权建立区间；查询包含
关系不宣称循环或 try 是可透明穿过的尾部。

现有 yield/guard 包装投影仍要求同一源码序列、yield 在 guard 之前；空 RETURN
arm 后的 following-sequence 放置仍是已有实现边界。它们不是语言不变量，
本轮没有扩大匹配，也没有宣称任意协程控制布局都已恢复。402 返回选择的
默认 64MiB 累计准入仍有资源缺口；budget 平台/峰值验证继续暂缓。

## 普通流 cell 初始化与捕获路径（规则 102）

普通无环流的 LocalAccessPlan 收集每个实际 cell 的全部初始化 store，
先证明 allocation 支配 store，再以 store 的支配子树建立互不重叠的区域。
读取和创建点共用 InitializationBefore 查询；跨块执行先后来自支配关系，
仅同块使用原 instruction 顺序。多个兄弟分支可各有初始化区域，不能按
物理扫描的第一个 store 定义整个 cell 的支持域。

这些事实只证明初始化，不转发或冻结某个 store 的值。所有原始 store
仍须通过 Language 域检查。分支汇合处没有共同支配 store 时，尚需独立的
所有路径初始化证明；当前区域接口不能据两个兄弟 store 授予该事实。

未证明只读的捕获沿真实正常前驱传播，创建点查询其所有到达路径上的
暴露状态；兄弟分支或 fresh allocation 之前的捕获不是当前环境的暴露。
rank 是 DAG 分析坐标，不能替代路径或支配证明。Defined guard 也以实际
block 所有权和支配区间查询，同块才要求 guard 先于 creation。

真实循环仍保留 fresh 同块 allocation/store/read 的已有实现限制，异常
和重复激活需要对应的生命周期转移。已有 deferred/module prefix 接口
保持，未扩大其局部布局条件，也未改变数值和父级构造语义合同。

## 普通决策中的条件语句前缀（规则 108）

共同 ControlDecision 仍拥有原来的条件路径、共享分支和最终 fallback。
当普通 EvaluationPlan 将路径内操作保留为语句根时，FunctionTree 以一个
块内私有条件变量保存原谓词，使用同层的连续 if 执行后续前缀及下一谓词，
最后按保存条件进入共享分支或 fallback。条件为真后，余下前缀都不执行；
共享分支只构建一次。纯表达式路径继续使用原来的 native logical 表示。

属性读取、普通根表达式、debugger、原 copy/jump 锚点都通过共同语句构建；
不根据 debugger、样本名、固定 guard 数量、寄存器分配或块布局选择恢复。
私有变量具有独立块作用域及 BindingOrigin，不对应输入 ABI/寄存器/cell。
原始操作、控制边、求值计划及 instruction provenance 不改变。

独立 TreeFlow 从真实 AST 的声明、原谓词赋值、每个条件前缀和分支归属
重建语义；核对极性、执行顺序、保存身份与私有变量的全部访问。不得将
私有变量用于其他读写、数值更新或捕获。StatementDecision 的 AST 结构
检查证明生成代码的语义，不是按输入图形状进行准入。现有边 copy、词法
作用域及异常归属义务仍保留。异常相位中的 statement-only 前缀尚未接入
这一表示；原 phased 表达式/逗号路径继续保留，不能删除 catch staging。

语句 staging 为同层 if，避免将长条件链转成深度随输入增长的嵌套语句。
实际 decoded 用例覆盖三 profile、1/200/201/402、纯副作用前缀、debugger、
寄存器 copy、纯块拆分及反向比较；输出完整函数后再验证行为。
