# 按用户要求进行的恢复条件审计

最新替换状态及补充事项见文末规则93；历史表中的严格比较候选及literal同块条件不再是
当前准入条件。未替换项继续开放，不能将本次局部交付称为全量审计闭合。

2026-10-03，基线 87176c10。此为本次明确请求的审计，不恢复定期审视。
目标是查出把编译器局部形状当作恢复准入条件的实现，并规定后续通用替换方式。
本次覆盖 src/include/tools 的样本身份文本搜索，并人工检查调用引用、条件表达式、
谓词快照、rest/派生构造器和异常类集成的条件。不是所有生产分支已逐行审计。

## 结论

本节及下表记录 2026-10-03 的历史基线；最新核对见文末
“2026-10-04：当前引用校验与扩展审计”。已替换项不能继续当作当前缺陷报告。

此次文本搜索没有发现以 pasteboard、WindowUtil、Immersive、PushMessage、
EntryAbility、Widget、hilog、JSON 或 sample 路径进行生产语义分派的实现。
仅有 fixture/sample 的说明性注释。文本搜索只能支持这一有限结论，不能证明
没有隐蔽特例。以下确实存在的是以局部布局或子集形状限制恢复能力的实现。
它们不能被称为语言语义要求，也不能继续靠追加形状匹配扩充。

## 初次审计发现（历史基线）

下表保留首次审计的发现；不是当前仍有这些条件的声明。CallPlan同块、
predicate producer/strict-comparison、单selection及computed phi Commit
邻接门槛已分别替换，当前剩余项和关闭证据见后续规则66至88。

| 优先级 | 位置 | 当前限制 | 应替换的通用机制 |
|---|---|---|---|
| P1 | src/source/call_plan.cc::VerifyCalls | member callee/call 同块或已有 cross-inline 证书；property 必须被原局部 forest 内联 | Reference 保留 base/key/callee 的真实定义与使用；通用依赖和效果顺序安排该引用与所有参数；CFG 块划分只作分析载体 |
| P1 | src/source/phase_selection_calls.cc::Check/Ancestors | 一个调用恰有一个 phi selection，最终 root 恰有一个 selection；前缀只穿单入/单出 corridor | 为整个调用建立有序参数/引用依赖图，一次索引、联合安排多个条件参数，显式处理 phi 和异常快照；不增加 selection 个数的枚举分支 |
| P1 | src/source/predicate_snapshots.cc::Candidates | 只接受两种 property read，最早使用必须是 strict comparison 并直接内联到 branch | 按值定义、使用普查、支配和不可重放效果建立通用 snapshot；条件语法只决定表示方式，不决定哪些 producer 能保留身份 |
| P1 | src/analysis/rest_arguments.cc::ProveRestPrologue | 前 N 条恰为规定顺序的 ABI MOV，再在 copied/copy+1 固定位置取 rest 和 STA；拒绝 try | 按 typed ArgumentFrame、SSA origin、def-use 与 CFG 证明参数搬运和 rest 来源；寄存器重排、插入纯操作、分块不改变语义 |
| P1 | src/analysis/spread_field_constructor.cc / derived_constructor.cc | 直线构造器、终结 return、规定 prologue 起点及有限状态序列 | 在通用 CFG/SSA 上分析 uninitialized-this、super result、绑定和 guard 的状态转换；证明全路径义务，不用某个线性指令模板定义默认构造器 |
| P2 | src/source/logical_regions.cc | 特定 branch/ToBoolean 邻接、两入 phi 和限定 arm 布局作为结构候选 | 通用控制依赖/区域恢复先形成表达式区域；布尔/选择语义从 typed predicate 和 phi 得出，局部模式仅作为可删除的快路径 |
| P2 | src/source/class_tree.cc::CheckClassParentScope | phased class 必须无 parent cells/modules、无 initializer/defined-member projection | 复用完整作用域/捕获/模块/异常对应接口；这些是尚未贯通的接口，不是 class 语义禁止条件 |

这些条件不等于已经查证的样本名称特例。它们的共同问题是把实现能力边界与
语义正确性混在一起，使等价的不同布局需要追加新匹配。修复不能简单删除
if/Require 后放行；应删除其“恢复准入依据”角色，以共同分析证明真正的义务。

## 必须保留的语义义务

- 原 callee、receiver 和 computed key 的身份；getter 与参数的实际求值顺序；
  不重读 getter、不改变 receiver、不引入可变 .call/.apply lookup。
- 单次/条件执行和共享值身份；一个定义多处使用不能通过重复副作用恢复。
- 原作用域、创建 occurrence、文件/profile/revision、捕获和模块初始化的来源。
- 正常/异常路径上的副作用与快照、TDZ/Hole、this/super 生命周期。
- 固定运行时与目标语言的实际差异，例如 Proxy 构造、数值/tag 边界；没有证明
  时保持明确缺口，不借布局巧合或 debug 类型当作运行时保证。
- 有界资源和真实复杂度。避免二次扫描应靠联合索引/调度，不靠“仅一个参数”的
  硬门槛冒充完整算法；所需输出本身的成本也必须计费。

## 本次实际动作与下一交付

已撤回尚未提交的 predicate snapshot ValueInvoke 白名单扩展。它虽不是样本
名称分派，也能复用现有顺序验证，但只是扩大局部 producer 列表，未解决上述
通用 snapshot/reference 设计缺口，不作为本次请求下的最终修复。

AGENTS.md 已加入通用恢复规则：禁止以样本身份和局部形状追加恢复分支，明确
区分语义不变量与临时实现边界。后续首项实施是共同调用 Reference/有序求值
模型，覆盖 normal/phased、多参数选择、getter、复制/分块变化和异常快照。
需要以原始定义/使用和 CFG 验证，不能用生成文本、样本名称或局部 opcode 模板
反推答案。结构等价变换的回归应与真实失败 case 并行验证。

当前 pasteboard record4767 仍未恢复，不宣称这个问题已修复。阶段一和 object
creator 全组目标保持原范围，完整语义与源码风格验收仍未达到。

## 规则 66 的首项替换

CallPlan 不再检查 property/call 的同块关系或手工枚举 cross-flow 例外。已有
共同 EvaluationPlan 的新鲜独立验证先证明可用性、定义/使用、单次所有权及效果
顺序；CallPlan 再检查原 property result、callee use、base/receiver 身份及使用
关系。并未通过删除引用身份或顺序义务来放行。

predicate snapshot 的 producer 类型白名单已删除。候选能力来自共同
EvaluationShape：可形成表达式且输入语义为 eager；正向事件区间重新核对
producer 子树和所有干预效果。于是调用结果、其他表达式与 getter 复用同一个
快照机制，而不需为每种 opcode/样本加入匹配分支。尚有 earliest-use/strict
comparison/branch 的局部候选范围，未宣称完整替换所有 snapshot/CFG 边界。

真实 pasteboard record4767 和 MultiCommunity record40957 均从 failure 进入
incomplete；原 callee-before-JSON.stringify 的调用已恢复。测试覆盖返回对象/
primitive/nullish、getter/方法改写、receiver、抛出值及连续1/2/200/201表达式。
七类审计事项仍有开放项，后续不能再靠枚举局部模板扩大接受范围。

## 规则 67：ValueTree rest 来源证明

共同语法解析不再调用固定 MOV/COPYRESTARGS/STA ProveRestPrologue 模板。
新 ProveRestOwnership 从当前已验证 ValueProgram、原 body 和 owner frame 出发：
定位独立 rest allocation；反向 CFG reachability 检查前驱效果和重复执行；原
instruction correspondence 检查隐式 physical argument 输入没有被提前覆盖；
SSA def-use worklist 检查原始最后声明参数只能做透明 transport，不能变成
native rest array 的可观察标量使用。GETUNMAPPEDARGS 仍是独立快照。

重排 ABI copy、NOP、改变 rest 暂存局部位置都不需要新生产匹配分支。文件内
真实 method/family 经过共同签名、AST、ABI 和 printer，48 complete families /
24 rejected protocol cases 的原生对照通过。200/201 NOP 与资源/工作预算测试
验证这个来源机制。

当前接口处理 normal ValueIR。旧 legacy semantic writer、FindRestLocal 和
spread/forwarding constructor 专用分析仍含旧模板；此项未宣称已删完。
完整 CFG 的异常 rest 和派生构造器状态分析继续沿用本审计的通用替换要求。

## 规则 68：已有存储值的共同语义分类

选择表达式的 retained-input 判断不再漏掉更早汇合的 phi。共同
HasRetainedEvaluationValue 按 ValueDefinition 的穷尽类型及 EvaluationPlan 的
实际存储表示，识别参数/receiver、phi、catch binding 和 operation temporary；
调用 receiver 也复用该接口。原始值可用性与 phi-copy 验证仍须先成立，查询
本身不证明 dominance、不移动定义，也不允许重放 producer。

同一 arm 内尚需执行的 producer 仍走原 arm-owned evaluation 检查，不能靠
“已有临时变量”隐藏该 arm 的效果。此为共同表示分类的遗漏修复，未增加新的
opcode、寄存器、PC 或 selection 数量匹配。normal 的先前 phi→后续选择已
贯通，1440 native /1584 paired方法场景通过；异常相位的选择/reference组合
仍有缺口，record5374未恢复。七类审计事项并未全部关闭。

## 规则 69：异常相位的保留值与输入边归属

异常选择发现现在先解析已经验证的复制别名，再使用共同保留值分类，将
无producer且无隐藏效果的分支作为原 binding 的一次读取。首次phi/catch
materialization没有被别名消除。调用前缀的使用普查核对真实phi owner和输入
edge：当前选择输入属于同一参数表达式，外部phi及较早的其他参数不能读取
尚未执行的移动前缀。

纯、不会抛出、结果为Language且只含literal输入的共同 eager表达式，现可
跨CFG分块安排到唯一原消费者。判断来自共享DescribeEvaluation与效果/值域，
不新增producer opcode白名单；有状态输入、getter及first materialization
继续保留控制/顺序义务。单selection、有限arm/copy布局等旧边界仍开放，未
扩大其局部模式列表。record5374已从failure进入incomplete，constructor缺口
与阶段一完整验收仍保留。定向副作用/错误分支值反例及独立行为对照支持本次
接受域，不能据此宣布七类审计事项全部解决。


## 规则 70：输入失败归属于实际成员函数族

部分class草稿原先只允许成员根自身的输入域缺口，defined member没有相同
表示。该边界会因失败位于不同嵌套深度、class buffer或DEFINEMETHOD安装而
丢弃其他独立成员。现在函数族reader记录请求root及实际DFS失败创建边，
共同ClassMemberOmission按真实所属成员记录缺口；没有局部序列/深度匹配。

这不是扩大完整语义接受域。只允许原输入InternalValue证明失败，省略整个
成员族；输入损坏、资源超限和后续证明失败保持拒绝。定义成员依然逐个核对
原始身份/创建/定义位置，所有缺口使class/record保持incomplete，没有假的
callback正文或内部写入/ABI/source收据。1/2/3层、三profile和200/201回归，
完整组合的访问器反例及独立方法行为对照支持这个表示接入。15个record现
可保留其他成员，未宣称数值语义、遗漏函数体或七类既有审计事项已经完成。

## 2026-10-04：当前引用校验与扩展审计

本次按用户要求复核，基线 79324555。工作区另有未完成的 SuperForward 接入；
以下不将其作为已交付能力。人工核对调用引用、normal/phased 选择、谓词快照、
rest、派生构造器、类成员与初始化器、literal 输入和 legacy 模块/数组/super
整理；另搜索 src/include/tools 的样本身份文本和块/邻接条件。未逐行审计所有
生产分支，也未运行全套、扩展语料或 RSS。搜索命中需结合用途判断；真实名称、
ISA 解码、枚举穷尽性和操作数元数校验不是样本分派或布局特例。

### 同块校验的来源和现状

历史提交 c9c8a93b（2026-09-26，调用引用与 receiver 证明）在 VerifyCalls 中
要求 property.block == call.block 且 property.order < call.order，同时要求
property 为 callee 的内联 producer。它用块内次序限制早期表达式恢复范围，
防止 getter 重读、receiver 改变和求值乱序；这不是 VM 或语言对成员调用的
限制。历史代码本身不足以证明其因某个样本而加入，不能据此推断作者动机。

766afe0c 已删除同块条件及 CrossPhaseInline/CrossLogicalInline 的例外枚举。
当前 src/source/call_plan.cc::VerifyCalls 先独立调用 VerifyEvaluationPlan 或
VerifyPhasedEvaluationPlan，再核对原 property result、callee/base/receiver
身份和实际 consumer。callee 在分支前、参数在汇合后的关系本身不再拒绝。
仍要求 property 被当前表达式内联拥有：这是现有 MemberCall 的表示边界，
不是所有合法显式 receiver 调用的定义，不能新增更多 placement 枚举来填洞。

### 当前待替换项

| 优先级 | 当前源码位置 | 观察到的实现边界 | 必须保留的义务及共同替换方向 |
|---|---|---|---|
| P1 | src/source/call_plan.cc::VerifyCalls，约215行 | callee property 只接受当前表达式的 inline/nested-literal consumer | 保留原 callee/base/key 与 receiver；用共同 Reference 和保留值表示承载共享/已存储引用，证明一次读取和调用时序 |
| P1 | src/source/phase_selection_calls.cc::Prefix；src/source/phased_selections.cc；src/source/phi_expression_inputs.cc | 规则87已移除单选择门槛，88替换Commit邻接，89投影冗余phi/copy身份；单入候选和多份真实parallel-copy RHS共同安排仍开放 | 共同Reference联合安排同一调用的全部选择；继续贯通控制依赖/区域及真实edge-copy时序，不能扩大local opcode或tail白名单 |
| P1 | src/source/predicate_snapshots.cc::Candidates，约20/38行 | 最早同块eager occurrence可接收snapshot assignment；规则82已去除strict-comparison/branch限定 | producer与consumer白名单已删；尚需跨块snapshot所有权/支配/控制依赖与效果区间，保留一次求值和所有使用身份 |
| P1 | src/analysis/rest_arguments.cc::ProveRestPrologue；src/source/rest_binding.cc::FindRestLocal | 固定起始 ABI MOV、寄存器布局及紧邻 rest/STA；旧路径仍调用 | normal ValueTree 已用 ProveRestOwnership；剩余 legacy/构造器使用必须迁移共同 frame、SSA、参数写入和动态数量证明，不能扩充 prologue 模板 |
| P1 | src/analysis/derived_constructor.cc；spread_field_constructor.cc；range_derived_constructor.cc，约38/79行 | legacy直线状态序列及copy opcode列表仍在；共同ValueTree的回边/guard存在门槛已由规则84/86移除 | 共同CFG/SSA单次调用、receiver、guard输入与cell事实已贯通；剩余多super site、捕获/异常与导入父协议不以legacy布局定义，不能假定host/import构造器类型 |
| P1 | src/source/class_definitions.cc，约83/96行；class_initializers.cc，约58/70行 | 规则74/75已替换defined-member同块条件；class initializer owner/method/call仍受同块局部次序限制 | 已定义成员复用真实HomeObject、occurrence和normal-CFG效果顺序；初始化器及phased捕获环境仍需共同证明，不能追加局部序列 |
| P2 | src/source/logical_regions.cc::FindSelectionExpressions 等 | branch/ToBoolean 邻接、有限 arm/phi 布局 | 用通用控制依赖与区域表示产生候选，typed predicate/phi 定义语义；二元分支或操作数元数本身不是缺陷 |
| P2 | src/source/class_tree.cc::CheckClassParentScope，约20行 | phased class 不接受 initializer/defined members 或父 captures/modules | 作用域/模块/创建环境接口尚未贯通；禁止把“无捕获/模块”视为 class 语言要求；部分成员缺口不等于完整环境证明 |
| P2 | src/source/literal_plan.cc::EarlyValue，约236行 | 规则81已以共同availability替换phased phi/allocation同块；working-IR导入对应、首次OwnDefinition候选仍开放 | 保留snapshot/edge-copy版本与定义/观察顺序；剩余候选要贯通共同ownership，不追加block/producer类型匹配 |
| P2 | src/source/super_arguments.cc::ProveSuperArguments，约67行 | legacy super 每个参数必须来自调用前紧邻的 load/save 对，且只支持单 super 区域 | 直接通过真实 def-use 和有序参数依赖整理，保留原参数快照、this 初始化和效果；不扩充寄存器排列或 load/save 模式 |
| P2 | src/source/array_literals.cc::FoldArrayExpressions/FoldArrayLiterals；class_assembly.cc | legacy 数组/类整理按同块连续语句与操作列表推进 | 共同 LiteralPlan/ClassTree 是迁移方向；保留新对象身份、定义/逃逸/观测、属性描述符和捕获时机，不追加 legacy 白名单 |
| P2 | src/source/module_binding.cc，约73行 | 未证明初始化的模块 load 要求下一条指令就是 Hole guard | 需用原 module binding、Hole 状态、guard 输入的 SSA 身份及控制/异常效果证明；TDZ 行为必须保留，邻接仅是旧实现边界 |

以上是恢复能力限制，不是已证明的错误放行；同块比较也可能是在实现一个已
声明为局部范围的优化，不能只凭搜索命中全部删除。特别是 literal phi 的
edge-copy、模块 TDZ、getter、super 和 class 捕获会在粗暴放宽后改变语义。
通用证明负责正确性；表示接口负责表达证明后的结果。两者已有完整证据后，
不得再加样本/块形状等第二重准入条件。AGENTS.md 已明确该规则及变形/负例
验证要求。本次没有扩展任何生产模式，也未声称这些开放项已全部修复。

### 本次验证

现有 build 二进制执行 5/5 定向测试通过（15.11秒）：logical_regions_test、
expression_snapshot_source_test、pasteboard_partial_callback_source_test、
retained_selection_source_test、forwarded_selection_source_test。日志：
/tmp/abcd-reference-shape-audit-tests.log。通用表达式快照另直接执行270组场景；
pasteboard 配对脚本覆盖240组原 callback 场景并要求 record 保持 incomplete。
验证的是已有构建的行为，没有重新构建未完成的工作区 SuperForward 修改，
也不证明所有 phased 多选择、共享引用或阶段一完整语义已覆盖。

## 规则 71：原调用帧转发采用整个函数的语义证明

前述审计时未完成的 SuperForward 现已贯通共同 IR、frame fact、evaluation、
SourceTree、ABI 与 class printer。当前 normal 路径调用 ProveImplicitConstructor，
不调用 ProveForwardingDerivedConstructor 的直线 prologue 模板。原 CFG 上的
转发次数与终止传播、共同 SSA/copy/phi 身份和物理 frame 写入分析负责证明；
不限定原始转发站点数量、寄存器布局、同块或邻接。多个分支分别转发、汇合
返回 super phi 的 fixture 经共同 FunctionTree/ABI 通过。

该证明允许将真正等价的整个函数表示为语言原生的隐式派生构造器。源类省略
显式 constructor，保留原始 IR/树来源与 typed implicit fragment。它不是将
forward 指令局部替换成 super(...args)：后者观察 iterator，前者直接转发原
调用参数。绑定/Proxy parent、动态父构造器和 newTarget 行为通过独立对照；
不得以这种转发的 IsConstructor runtime 规则放宽另一个 super spread 协议。

以下仍是未完成的共同证明范围：循环的终止、异常相位和额外可观察效果；
调用前物理参数/newTarget 写入后恢复尚未通过版本化 frame 读证明，当前拒绝；
printer 尚未承载此形式的额外捕获/模块环境。这些不是语言禁止条件，不允许
增加局部恢复写入序列、producer白名单或新的布局例外去补。range/spread 与
legacy 构造器的既有模板仍开放，前表其余项目也未全部关闭。

15/15 定向回归32.10秒通过，含寄存器复制、0/1/200/201间隔、分支转发及
跳过/重复转发、替换constructor、修改frame、效果、错误返回和回边负例。
185组构造行为及70组异步方法 paired对照通过。固定五输入从78 complete /
26 incomplete /2 failure到80/26/0，原78份源码逐字未变；此为组件交付，
不代替阶段一与 object creators 全组验收。细节见 implicit-constructors.md。

## 规则 72：语法分组不承担恢复准入

共同打印上下文已区分 member/call/new 的语言 grammar，替换旧 Member printer
按 Symbol/This/Member 列表决定括号的写法。new 的 MemberExpression 上下文
向 member base 传播，任意深度的工厂调用均在原调用处正确分组；没有新增
graph形状或producer白名单去取得恢复许可。callee、receiver和求值顺序仍
由原共同分析决定，所有被接受的 AST 都由同一个打印器处理。

值调用的 `(0, callee)` 是语言 Reference/GetValue 与 indirect-eval 表示义务，
不能因 callee 恰好是 identifier 而删除；这种语法规则与同块/邻接准入不同。
数字成员base及new目标中的call分组也是语言grammar，不能靠删括号提升外观。
14种组合、2538组行为对照及200/201长链验证支持这次表示变更；前述12项
能力审计中的开放语义/表示缺口没有因此全部关闭。

## 2026-10-04：当前提交复核与 literal 嵌套补充

复核基线 66e48615，工作区干净。再次核对历史 c9c8a93b、删除同块条件的
766afe0c 和当前 VerifyCalls：同块限制没有回归；inline-owned callee 表示
边界仍在。src/include/tools 的 pasteboard、WindowUtil、Immersive、
PushMessage、EntryAbility、Widget、abc_with_code 文本搜索未命中；这仍只
证明所搜身份文本没有被用于生产分派，不证明不存在其他隐蔽特例。

补充一类此前表格未独立列出的实现边界，使当前清单为13类：

| 优先级 | 当前源码位置 | 实现边界 | 通用替换方向与真实义务 |
|---|---|---|---|
| P1 | src/source/literal_plan.cc::LiteralExpression/LiteralUnary，约247/265行 | literal 嵌套单独列举 unary、binary、call、construct、read、closure 等可接受表达式；共同 DescribeEvaluation 已有表达式能力描述，两处能力需分别维护 | 共同接口描述 AST lowering 的输入求值次数、eager/conditional/suspension 行为和 Reference 消费方式；结合 literal 所有权、效果/异常区间、创建环境和逃逸证明决定嵌套，不再通过追加 operation 类型扩大准入 |

不能仅将这份列表替换为 DescribeEvaluation(...).expression：该属性目前不
证明一次求值、暂停语义或安全的原生表示。FalseOrZero/TrueOrNonzeroNumber
打印会重复读取 snapshot，不能把有副作用子树直接代入；await/yield 也不能
靠表达式标签忽略暂停。binary 的 use_count == 2 是两个输入操作数的元数，
不是“只允许两个使用者”的恢复限制；Language domain、closure HomeObject、
模块 Hole guard 和全局 lookup 的语义区别仍须逐项证明。

前表 class 同块项还覆盖 ProjectClassDefinitions 的第二层检查：约64行的
claim 限定 creation/definition 同块，约93/107行限定 class/prototype 的
外部观察者与最后安装同块且在其后。只改 FindClassDefinitions 的候选发现
不能修复跨块投影。共同替换必须同时证明安装的执行条件/次数、所有观察者
看见完整类、HomeObject/捕获身份与 intervening effects；不能以 block ID 或
operation ID 的数值顺序推断 dominance/lifetime。

本次仅更新审计及状态，没有扩大生产接受域或删除真实语义义务。当前现有
build 重跑5/5定向测试通过（15.10秒）：logical_regions_test、
pasteboard_partial_callback_source_test、retained_selection_source_test、
expression_snapshot_source_test、forwarded_selection_source_test。日志
/tmp/abcd-reference-shape-current-tests.log。未重新构建、未运行全套/扩展
语料/RSS；pasteboard callback 通过不等于整个 record 已完整恢复。

## 规则 73：literal 复用共同源码求值能力

前节新增的 LiteralExpression/LiteralUnary 独立operation准入列表已替换。
现有 EvaluationShape 同时描述 SourceEvaluation：直接表达式、纯值、重复
snapshot输入、暂停、调用帧协议及无源码降低能力。literal从共同能力和
输入/结果语义发现候选，原 use/effect/catch/escape/environment 证明决定
实际归属，producer种类不再作为第二份可扩充白名单。

共同类型分派是描述指令/降低语义的边界，不是移动原列表后继续逐个给
literal授权。原来遗漏的this、当前函数身份和纯binding copy自动进入该
机制；单次使用与共享使用仍不同。重复输入谓词复用 PredicateSnapshot 和
BooleanEvaluation：原存储输入可重复读，Boolean降低可单次消费子树；不能
把getter子树复制进展开式。独立verifier重查嵌套输入角色/purpose及snapshot
义务。pure copy/string的额外效果、模块TDZ、HomeObject、异常和super协议
仍需各自真实证明。没有把“表达式”标签当作暂停或运行时等价证明。

真实DEFINEFUNC owner的编码用例经共同输入/树/ABI恢复此前拒绝的嵌套对象，
有复制/NOP、1/2/200/201/402变化及效果/共享/所有权负例。272组独立行为
对照通过；24项相关定向检查通过，细节及失败修复证据见status.md。五输入
及106份源码无回退/内容变化，阶段一依然80 complete/26 incomplete。
这关闭的是第13类独立列表事项；前表12类中尚存的布局/表示限制未因此关闭，
literal的phased phi存储版本和跨块区间也没有完整贯通。

## 规则 74：已完成类成员安装的后续观察

ProjectClassDefinitions 的 class/prototype 外部观察者同块门槛已替换。
共同 VerifyValueFlow 在当前原始图上证明SSA定义支配直接使用及phi实际
输入边。现有安装全部位于创建块，正常离开该块前必已完成全部安装；
故支配的其他块使用无需再匹配布局。创建块内仍按真实operation order
证明晚于最后安装，不能把未来定义或更早逃逸当作合法观察。phi按incoming
edge的source位置读取，不按merge块或PC大小推断时序。保留外部使用的
prototype读取与身份，不隐藏已安装方法或duplicated getter。

这不是删除所有class同块条件：FindClassDefinitions的候选范围与projection
claim仍限制creation/method/installation同块，原效果区间、HomeObject、
捕获和安装次数义务未变。跨块安装需要共同全路径效果/控制证明，此项仍
开放；也未扩大class initializer和phased接口的局部模板。禁止以新增
特殊分块、寄存器或方法名称匹配代替后续共同实现。

编码fixture覆盖后续分块、phi及循环观察和1/200/201/402安装；旧代码的
分块用例拒绝，当前通过共同evaluation独立复验。144组独立原生对照核对
类/方法身份、命名、描述符、receiver、Proxy效果/任意抛出值，配对源码
不进入引擎。共享SSA证明的实际资源费用也纳入投影预算。10项不同相关
检查通过；另两项legacy失败在优先链接原实现后仍复现，完整证据见status.md。
五输入仍80 complete/26 incomplete，106份源码内容未变，未达阶段一验收。

## 规则 75：跨块类成员安装不再以同块布局准入

FindClassDefinitions 中 creation/installation、method/installation 的同块
条件及 ProjectClassDefinitions 的同块 claim 已删除。共同 EffectOrder 从
真实 normal CFG/SSA 和 typed effect/exception/environment 语义构造效果
前驱；无效果依赖 SCC 透明传播同一前驱身份。不同路径上不同的效果即使
数量一致仍形成 opaque join。没有枚举允许的分块、opcode序列、寄存器、PC
或graph形状；pure diamond/loop 自动使用同一机制。

投影要求私有 class 的所有被移入成员效果组成从原创建出发的单一前驱链，
每个前驱支配后继，唯一尾部为最后安装。外部 class/prototype 观察仍须
晚于最后安装；phi使用按真实输入边检查。HomeObject/函数身份、捕获环境、
原方法单次消费和descriptor义务未放宽。条件跳过安装、循环重复安装、
环境/debugger干预继续拒绝；不声称证明全路径终止或完整异常图。

原 bytecode/OpId 的升序只保留为索引边界，不再决定 native 成员覆盖顺序。
DefinitionOrder 取共同 CFG reverse postorder，并在上述链/支配证明成立后
用于语法排序。物理布局反转的端到端 getter/data-method 覆盖用例检查真实
执行顺序；伪造顺序由 fresh candidate 对应检查拒绝。

这关闭了第6类中的 defined-member 同块门槛，未关闭其 class initializer、
phased scope/capture 接口和活跃 accessor-result alias 缺口。前表其他开放
项也未因这次替换消失。13项相关检查、432组observer及96组capture原生
对照通过；固定五输入与106份源码无回退，完整证据与剩余gate见status.md。

## 规则 76：名称推断是语法上下文，不是恢复准入

直接 identifier declaration/assignment 的 ClassExpr 现在复用该语言位置
自带的 NamedEvaluation。实际 binding spelling 等于原 runtime class name
时可去掉计算属性包装；不一致时保留包装，不拒绝已恢复 AST，不扩大效果
移动许可。这是生成文本/名称边界上的正确性规则，不能用类似字符串比较
反过来按函数/应用名称选择生产语义。成员属性赋值、非直接 RHS和其他目的地
不会继承这个 identifier 上下文；也不凭原 class name 引入 immutable self。

实际编码首次声明、后续赋值、名称不匹配和模块绑定重赋值的63组独立对照
通过；6个 named-class-self 合法语法 mutant 改变行为，证明名字相同不足以
证明 lexical binding 对应。它改善固定输入的源码表示，未关闭其他开放准入
限制或26份不完整模块的正文缺口。


## 规则 78：父函数初始化不再限定入口块

审计后续发现 FunctionInput 的 creation-time 初始化只收集入口块 store；
这也是旧实现范围，不是语言要求。现以已有 sparse SSA 的完整 IDF、Boolean
must meet 和支配遍历证明正常 CFG 的初始化状态，并在真实 function/class
创建处保存共享持久化集合。不同分支、汇合或循环不再需要新的形状匹配。
Language store、真实 owner/创建身份、TDZ、异常和效果义务保留；早于初始化
的创建仍缺 escape/invocation 证明。正常 parent 的结果供 normal/phased
child 共同使用，phased parent 的成功 Commit 分析仍未贯通。源码首次声明
和父函数 raw read 的独立边界没有被这项替换隐藏。详见 module-reads.md。


## 规则 79：同函数读取与当前图复用共同初始化分析

SemanticValidation 的 raw local read 已不再只认 entry-block store。共用
analysis::BuildModuleInitialization 的 all-path 候选，完整输入验证再逐项
核对所有store的Language操作数，单侧写入、零次循环和Hole/read-store循环
不能自证。ModuleReadFact::kStored 的入口store backlink变为可选来源，不是
准入条件。source ModuleGuards 用同一个算法重算 CURRENT ValueProgram 的
初始化状态，并检查实际write/use/domain和原始身份/effects；删除其stored-read
current_store.block==0 / 同块order条件。工作图移除/移动store不能靠旧输入证明
放行。phased parent、guard成功Commit与首次source声明归属仍是开放项；
没有增加branch形状、producer列表或sample身份匹配。详见module-reads.md。

## 规则 81：literal 的早期输入改用实际分配点可用性

删除 `literal_plan.cc::EarlyValue` 的 phased `phi.block == allocation.block`
和 producer 块身份分派。普通及 phased 路径保留 fresh ValueFlow 产生的支配树，
在真实 allocation 的 block/order 上复用共同可用性查询，包括成功 Commit 与
phi/catch entry；查询不提供值域、效果搬移或源存储版本权限。原来的 native
interval/unique phase corridor、no-escape 和 catch-copy 证明仍独立成立。
不再因为一个已可用的 phi 来自另一块而拒绝，也不把晚于分配的 phi 当早期叶。

原始编码的 phi materialization 之前就可输出；本次 RED 来自同一编码消除
纯复制后的等价候选 IR。候选证明和原始编码到独立源码树的验证分别记录，
不能把修改过的候选直接当原始 import 证据：当前 `VerifyImportedPhases`
对这种 working-IR 编辑的对应关系尚未贯通。该边界未添加新的本地匹配例外。
首次 own-definition 跨选择的原生表示仍开放；不得用普通 Set 替换
DefineOwnProperty。细节与验证层见 literal-initialization.md。

## 规则 82：共同 eager 使用点保存与引用复核

再次核对发现 `tree_operands.cc::Receiver` 仍有第二重
`property.block==call.block` 或 CrossPhaseInline/CrossLogicalInline 枚举门槛。
`VerifyTreeFlow` 在操作数复核前已新鲜独立验证共同 normal/phased EvaluationPlan。
该额外布局门槛现已删除；实际 property result、consumer、base/receiver、私有
存储和原始使用点对应继续校验，没有 callee 重读或 `.call/.apply` 替代。

`predicate_snapshots.cc::Candidates` 的“strict comparison 内联进 branch”候选
条件已删除。共同 DescribeEvaluation 的 eager 语义、reference/repeated-input
边界和已有完整效果区间证明决定保存位置。普通函数无需有 phi 才运行该机制。
源码森林与操作数校验携带真实 UseId，覆盖 scalar/call/module store；native
logical/conditional 使用原始谓词 occurrence，原 logical-LHS 证明查询原森林。
逻辑 keep edge 必须绑定真实已验证的左值；其他已保存值仍走普通 private-slot
校验。不能因 producer 是 snapshot 就推断任意 phi 输入属于那个逻辑左值。

这不是关闭所有表达式限制。当前 use-site interval 仍为 normal 局部证明，
跨块可继续保留原始独立保存；phased 保存需要共同 Commit/throw/catch 版本
模型。单 selection 的 phased call、inline-owned callee 表示、literal 首次
own-definition、初始化器/捕获投影与剩余 legacy 构造模板仍开放。不能继续
增加 producer 白名单、selection 数量枚举、邻接或块布局例外。具体契约见
expression-snapshots.md；验证与固定五输入证据见 status.md。

## 规则 84：构造初始化不以向后 PC 或 guard 布局准入

后续核对 `range_derived_constructor.cc::ProveInitialization` 发现三项额外
布局条件：任何 branch_target<=PC 一律拒绝、至少出现一个 super guard、
super 必须支配所有 guard。这些不是初始化不变量。range/spread 现在共享
真实 normal CFG 的 successor 可达性，只有通过 call block 的循环才拒绝
该单次调用表示；其他循环和向后物理布局不要求新的恢复分支。guard 按
其实际 SSA 输入逐项证明，未初始化输入可以在 super 前检查，no-op 不
要求 super 在前；没有 guard 时也不伪造 guard 或输入事实。

正常 return 的原始支配、super 来源、current constructor、每个初始化/
未初始化 guard 的真实值、文件/owner/current-IR 对应及完整 guard coverage
均保留。重复调用负例在新的 CFG 证明处拒绝；错误 working guard 输入和
原始错误来源继续拒绝。新增遍历 O(B+E) 时间/O(B) scratch，并计费真实
初始化和边访问；不按每项 guard 重新遍历。算法及预算见 algorithms.md。

这是构造初始化证明的替换，不能当作原生构造/整类输出许可。单 super
site 接口、多路径 invocation frame、异常控制、捕获/receiver 初始化、
parent brand、spread-array 展开及 legacy 直线模板仍有独立边界。多个
互斥 super site 尚未表示；不能按数量、opcode 序列或固定图形补充模板。
旧审计表的 rest/构造模板类事项及 phased call/reference 事项未全部关闭。

720个三profile真实解码用例、1/200/201/402/804块的CFG链、预算边界与
40个原始input/current-IR投影用例通过；实际source路径的构造/捕获/Proxy/
异常与配对回调另有独立Node对照。首轮行为mutation暴露了旧文本替换未
实际改变嵌套保存赋值的问题，已让负例保留super效果而仅破坏receiver身份。
旧writer assignments失败单独记录，没有计入通过。最新验证与五输入
状态见 status.md；未进行逐行全量生产审计、full-suite、扩展语料或RSS。

## 规则 85：语法恒等变换与恢复准入分开，补充发布层门槛

当前再次核对 CallPlan 与 tree_operands::Receiver，同块比较及
CrossPhaseInline/CrossLogicalInline 例外没有回归；共同求值独立验证与实际
callee/base/receiver 对应仍在。phase_selection_calls 的 phi_arguments 和
root_selections 各为1门槛仍在，不能视为已由这些修复关闭。

新共同 printer 用 typed Value/Truthiness/Discarded 和 polarity 组合 NOT 与
BooleanLiteral 条件分支。这种源语言布尔恒等式仅改变已恢复树的表示，不
决定哪个指令、布局或样本可以恢复；条件 GetValue、getter/throw 和 nullish
左侧精确值保留。不得将这类 AST 语法变换误用为 producer/CFG 准入模板。
独立行为与来源范围验证见 operator-expressions.md、status.md。

本次补充确认另一项发布边界：prepared_family.cc 约216行的 root callable
检查，在实际 ClassContext 下仍要求显式 DerivedConstructor 的
receiver_guards 非空。规则84的共同初始化分析已不以 guard 存在与否准入，
这里不能再用 guard 数量代替初始化/receiver 生命周期证明。暂未修改这个
独立发布接口；后续需消费原 owner、实际初始化证明、完整树/ABI、捕获和
constructor 上下文，避免直接删除后让未初始化 receiver 获得发布许可。
隐式构造路径有独立协议证明，不能据其成功宣称此显式根接口已经贯通。

当前审计仍有 inline-owned Reference、phased 多selection、initializer/
capture/模块相位、首次 own-definition 和 legacy 构造/rest 等开放项。本轮
27项相关检查通过，并重新导出固定五输入；未逐行审计所有生产条件，也未
恢复暂停的全套、扩展语料、性能或RSS验证。全部审计事项尚未关闭。

## 规则 86：显式派生根不再以 guard 存在准入

前节补充的 PreparedFamily::Prepare 发布门槛已删除。真实 ClassContext
和 root placement 保留；FamilyTrees 对实际同 owner 输入先证明构造初始化
协议，再构建/验证 SourceTree、ABI、来源及全族捕获/写入，发布层不再重复
要求 receiver_guards 非空。RED 在完整构建后命中 kCallable；无 guard 的
正例经同一通路进入完整 family emission，没有另加局部序列或布局分支。

未新增“无 guard 即 initialized”的假设。原 guard 的实际输入及覆盖、cell
初始化与 descendant preservation 仍独立证明。没有可省略的 root guard 时，
可写 cell 不一定使寄存器初始化协议失败，但不能获得 immutable receiver
事实；真实 child initialized guard 无证据时继续拒绝。不得将这两种义务
合成新的“必须只读捕获”或“必须出现某种 guard”模板。

测试真实序列化的三profile owner/constructor/child，包含复制规模、分支
捕获、字面量初始化、getter和原始/current-IR变造。夹具省略 guard 后曾
遗漏必要 accumulator receiver load，现修正输入而未增加生产模式。旧负例
要求“协议全部拒绝”的假设也已分解为实际 receiver 事实/guard 省略义务。
具体构建及失败修复证据见 status.md；当前开放的 phased 多selection、
inline-owned Reference、initializer/捕获/模块相位与legacy模板仍未关闭。


## 规则 87：联合选择参数与索引祖先 Reference

删除phase_selection_calls的phi_arguments == 1和root_selections == 1
准入，不再以选择数量避免共用callee的重复扫描。实际argument UseId决定
源码参数次序，同一call联合检查从callee Commit经各selection header/join
到实际call的分段依赖；整组失败保留原快照，不能部分移走callee。prefix
只由相应decision拥有，原phi输入/transport/branch身份分别验证。普通
branch也登记decision ownership，避免把其真实Boolean输入误判为外部使用。

多条selection记录可以对应一个成员Reference，phase_references只保存
其同一实际callee-use。Build/Verify/ExpressionForest/TreeFlow仍使用原
ValueProgram与共同ConditionalExpr/MemberCall及printer，没有样本名、
固定参数个数、局部opcode串或新增placement例外。callee与branch的物理
OpId先后门槛也由真实CFG分段路径取代，outer callee与child的次序复用
引用区间；receiver/getter、原use、正常复制、catch scope/staging仍保留。

祖先Reference按实际inline consumer关系连接。预索引正常corridor的
效果归属区间和catch一致性，路径终点进入前停止，终点的其他前驱不会
新增single-entry门槛。每个祖先关系只查询一次、存储其联合引用一次，
避免多个selection/兄弟参数重扫长前缀；详见algorithms.md。三profile
129组解码正/负例覆盖1/2/3/50/100/200/201参数、前缀getter、外层成员调用、
复制变化、反向参数、debugger屏障、伪造argument/callee/缺项及配额边界；
另有44496组生成源码行为对照和9项有效mutation。

当前开放项包括MemberCall的inline-owned表示边界、单入prefix/control
candidate、PhiExpressionProducer的computed Commit邻接与literal-only
inert tail、class initializer同块、phased scope/capture/module接口以及
legacy rest/super/array/class/module模板。初次双getter arm夹具因现有
computed arm后跟pure jump限制没有产生候选，已记录为缺口，没有追加
opcode/tail匹配。确认为有实现准入限制，不据此断言其历史作者因样本加入。
禁止继续用样本/布局匹配扩大这些路径；真实语义校验继续保留。具体新鲜
构建/测试/固定五输入证据见status.md，审计仍非全仓生产分支逐行关闭。


## 规则 88：phi 表达式以全路径效果证明替代 Commit 邻接

删除PhiExpressionProducer的computed producer必须紧邻原phi输入边及
仅primitive literal可跨inert tail的限制。IndexPhiExpressionInputs先一次
普查实际UseId观察，再从原normal copy边反向求必须到达的同一EdgeId。
普通与phased共用此索引；正常块查询producer自有literal/guard区间结束
后的尾部，相位图查询实际Commit。checked module guard和native literal
内部定义仍属于同一表达式，不能被当作额外观察或样本模板。

所有正常分支必须到达同一原phi边；返回/外来效果/环境改变/中间copy或
无限循环使证明失败。单次原观察、原throw staging和最终TreeFlow的原
protected/handler scope核对保留。typed EvaluationShape记录normal control
意义，路径分析不按opcode串、producer名单、前驱个数或图形模板准入。
索引是线性工作/空间，不逐参数或逐phi重走尾部，见algorithms.md。

216组三profile实际解码用例及88992组完整生成源码对照通过，覆盖两侧
getter、多参数、prefix/outer Reference、throw/noncallable及18项有效
mutation。另有normal图纯branch/merge和1/200/201/402块尾部，divergence、
早退、debugger、额外观察与中间phi-copy拒绝。固定五输入无complete回退，
真实VoIP方法的一处双分支getter改成ConditionalExpr，独立配对服务300组
行为对照通过；不是HarmonyOS主机运行验收，阶段一仍未完成。

本轮诊断还确认：true arm插入冗余MOV自身会新增一个真实SSA phi输入，
现有IndexPhiExpressionInputs及FindPhasedPhiSelections仍以bundle.count /
schedule.steps为1准入。此例也被初始夹具触发，不能把它的拒绝全归因于
Commit邻接；夹具移除这个独立变化后双getter路径通过。多parallel-copy
RHS需要按实际值身份/存活/复制顺序共同安排，不能删除count门槛就放行。
该缺口、单入candidate/prefix、inline-owned Reference、class initializer
同块、phased scope/capture/module接口及legacy模板仍开放。禁止用更多
样本/opcode/寄存器/PC/数量匹配修补；不得断言已完成全仓逐分支审计。


## 规则 89：冗余 phi/copy 不再充当正常边屏障

arm 中 MOV 自身会产生额外 SSA phi，原 bundle.count == 1 把实际值不变也
当成多份写入；这属于表示/分析缺口，不能解释成成员调用的语言限制。
共同 AnalyzeValueIdentities 复用迭代 Tarjan，按原 PhiDefinition/纯 ValueCopy
方程证明同一外部 ValueId 锚点，不看寄存器、PC、块布局或 producer 名单。
多个不同外部值、无外部锚点、域不相容不会折叠；catch binding 和真正的
mutable phi 首次快照仍独立。原 phased SSA、全部输入 UseId 和 catch staging
保留，PhiCopies 独立复算身份并验证原输入全集，只投影 executable schedule。

正常边屏障及唯一 expression_input 改用实际保留的 schedule；共同 canonical
use census 保留穿过身份 phi/copy 的最终观察，不把搬运计为额外求值，也不
遗漏共享 getter/receiver。BindingPlan、选择恢复、Reference corridor 和树
flow 消费同一已验证投影。删除的是冗余身份造成的 raw-count 限制；多份真正
不同值的 parallel-copy RHS 顺序仍未贯通，不能宣称单份 RHS 边界全部关闭。

旧 phased-selection-refusal 测试假定普通 conditional module-property 参数
必拒绝；现在它经过完整 draft、树语义/flow 和原 guard/staging 验证生成
原生 MemberCall，测试改为正例。独立 Node 检查 getter 替换、receiver、模块
绑定变化、throw/catch、非 callable、TDZ 与未取分支；不经 .call/.apply。
补测直接 module 值作为选择参数仍命中 kReference，保留为 phased module
参数/guard 接口缺口；没有新增生产 direct/property 分支或放宽 TDZ 义务。

435组三profile解码矩阵覆盖 base/callee arm 自复制、getter/prefix/outer
Reference、多参数及1/2/200/201/402复制变化。不同 receiver、debugger、
反向参数及变造 alias/projection 拒绝仍测；完整生成源码另有177984组行为
对照和36项有效 mutation。构建、最终模块测试及固定五输入证据见 status.md。
inline-owned Reference、single-entry candidate/prefix、class initializer
同块、phased scope/capture/module 和 legacy 模板仍未全部替换。本次沿既有
审计发现推进共同实现，不声称已经逐行关闭全仓全部恢复条件。


## 规则 90：模块读取不再按 producer 类型强制保留为 root

补测直接模块值作为conditional参数时，IndexPhiExpressionInputs已经由
共同原UseId、原边和all-path尾部证明可以表达；但builder/verifier又为
所有phased ValueModuleRead设置pinned。此前inline模块读取另有类型例外，
phi输入却被第二重准入拒绝。这是早期实现范围限制，不是TDZ/模块语言要求。

删除全局module pin及对应inline例外，共同观察/效果/原边/Reference证明
消费这一producer。没有增加direct/property或placement名单。模块绑定、
原guard输入、原Before/Commit/catch对应、catch slot来源与最终树中的guard/
read共同表达式仍独立验证。原始SSA与staging不改写，不猜initialized状态。
多观察、额外debugger效果和不同catch状态不因pin删除而获得移动权限。

三profile72组实际模块参数正例及18组额外效果拒绝通过；9252组native
行为对照包含模块绑定变化、getter替换、receiver、noncallable、caught
completion、TDZ与未取分支。phased and/or目前保留高层ConditionalExpr，
原生logical表示仍是风格缺口。固定五输入无complete回退且源码字节不变。
具体新鲜构建和28项相关验证见status.md；这关闭直接模块选择参数的pin
缺口，没有关闭模块guard邻接、scope/capture、多真实copy安排、单入口
candidate、initializer同块或legacy模板，也未做全仓逐生产分支审计。


## 规则 91：原生 logical 是共同真值/保存使用的表示投影

此次改进不增加CFG识别模式。既有SelectionRegion仍拥有原两臂、phi边及
异常/control通路；共同真值方程只证明其保留分支返回的正是被测试SSA值。
inline consumer森林、纯效果、完整predicate极性和canonical身份分别证明
可省略的Boolean计算，没有opcode串、同块、固定深度或条件数量准入。
raw VM zero/nonzero对null/undefined/NaN不同于JS truthiness，不能贸然替换。
有外部观察的Boolean结果仍materialize，不因为&&/||表示而省略其观察。

首次保存也是语义义务。首轮实现从slot取值漏掉saving use，固定五输入
抓到DetailPage complete回退。现在原UseId沿方程保留，源树消费原保存
赋值，原phi边读取其已求值结果；scope、Reference、TDZ及异常staging
没有因表示更简洁而放宽。120组三profile解码、10162组模块行为、1390组
实际输出相关方法的配对对照与固定五输入支持此交付，具体证据见status.md。

当前再次核对的开放限制如下；不能用其数量/布局条件代替语言不变量：

| 位置 | 当前实现边界 | 后续共同替换 |
|---|---|---|
| call_plan.cc:215 | MemberCall的property仍要求inline-owned或已有nested-literal接口 | 真正保存的Reference及有序参数安排，保留callee/base/key/receiver身份，不重读getter |
| phased_selections.cc:25/63/129 | 唯一实际copy RHS、arm/condition单入口corridor | 同一依赖/控制/效果模型安排全部真实RHS并证明区域归属，不枚举数量或前驱形状 |
| class_initializers.cc / class_initializer_projection.cc | 同块/order/OpId门槛与效果数量比较已由规则92替换；当前ClassTree仍只有一个initializer接口 | 多次真实初始化区段须由共同顺序/owner模型表达，不能增加count模板 |
| module_guards.cc:79 | guarded read仍要求block/order/OpId邻接 | 以原guard事实、精确输入及相位/TDZ对应证明所属读取，保留原抛出顺序 |
| class_tree.cc::CheckClassParentScope | phased parent接口仍排除initializer、defined-member、captures和modules | 贯通真实创建scope/捕获/module/异常对应，不能直接删掉父作用域义务 |
| class_self.cc::FindClassSelfStores | 同块/order和效果数量已由规则93的共同EffectOrder身份/支配/必达替换；实际唯一首次store及初始化器正文接口范围仍有限 | 多次写入与一般初始化器须由共同cell/观察/执行顺序模型表达，不能增加数量模板 |
| semantic_locals.cc / scope_facts.cc | 规则93以真实拓扑排序替换物理forward-edge准入；既有一般cell初始化仍依赖原指令排序，循环保留局部路径 | activation-sensitive初始化数据流证明每次分配到读取的全路径store；不能以PC大小或同块替代 |
| literal_class_fields.cc::Build | 已有literal-only快速路径要求单块、无边、无参数及局部类型集合 | 统一field依赖/执行顺序/this与cell观察证明；既有路径只作为明确未迁移边界 |
| literal_plan.h::IsReceiverField | 直接字段接口只消费ReceiverEntry，FrameThis结果尚未进入同一identity证明 | 根据真实ABI/callable/this初始化状态建立身份对应，保留派生TDZ与arrow lexical-this差异 |
| legacy rest/super/array/class/module整理 | 既有直线/局部序列模板未全部迁移 | typed CFG/SSA/协议状态和共同FunctionTree通路；不继续扩大模板集合 |

CallPlan同块及CrossPhaseInline/CrossLogicalInline例外没有回归，模块producer
pin、selection数量及computed Commit邻接门槛已经替换。上表的边界仍开放，
这不是全仓每个生产分支逐行审计闭合，也不能推断原作者因某个样本加入。
禁止追加样本/名字/布局匹配的规则继续有效。真实身份、effect、scope、TDZ、
异常和资源校验继续有效，不能把“避免特例匹配”理解为删除语义证明。

### 2026-10-04：规则92，class initializer删掉同块及数量式效果走廊

FindClassInitializerCalls以fresh共同SSA可用性验证实际home/callee/receiver。
ProjectClassMembers消费共同EffectOrder，原前驱身份加新增after的全路径
下一效果必达事实，取代同块、物理OpId先后及prefix效果数量比较。
纯分块/菱形和逆物理布局无需新增生产匹配；绕过call、重复call、可能
不终止及任意夹杂效果不能挪进class syntax。phi观察按其原incoming edge
检查是否由call支配，真实self cell写入保留，未替代self初始化证明。

零length、零实参、method不逃逸、未使用call结果及单initializer接口仍
是现有静态字段表示的前提/接口范围，不宣称所有ordinary method call都
是static initializer。实际body、DefineOwnProperty、receiver及scope还须
由独立共同树通路证明；多参数/多initializer应通过相应接口与分析扩展。
本次额外核查并登记class_self及literal_class_fields的局部限制，未扩大旧
fast path，也不宣称全仓每个条件已逐行迁移完成。验证及剩余gate见status。

### 2026-10-04：规则93，class self 私有初始化与捕获的跨块证明

FindClassSelfStores复用共同EffectOrder：allocation支配creation，creation
支配store，store支配initializer call；allocation→creation→store使用实际
效果前驱身份及全路径下一效果必达，不比较数量、同块或物理OpId。
所有原cell读取须由call支配。提前读/发布、夹杂效果或不终止路径仍拒绝，
不是靠删除语义检查扩大覆盖。原self store继续留在父FunctionTree。

ClassSelfOrder以真实同文件初始化器正文验证闭合私有区间，再把实际class
capture occurrence绑定进arena/revision受约束的证书。ScopeFacts消费这个
Language-only事实；Defined、未知child writes和一般cell初始化仍有各自
证明义务，不能把该证书用于别的capture或TDZ豁免。FamilyWrites从实际
已准备ClassExpr的initializer context/body登记原method barrier，与普通
定义成员共用owner索引；缺正文、错call或错body owner仍拒绝。

逆物理布局的完整用例发现ProveLocalAccesses把target block编号较小当作
真实环。现以已有OrderAcyclicBlocks的Kahn顺序区分布局与实际cycle，再
使用原支配证明。该局部迁移尚未替换一般cell的PC顺序和循环activation
限制，已列入开放项。没有生产opcode串、样本身份或新图形枚举分支。
验证与阶段一边界见status.md；不把此项交付称为全仓审计闭合。
