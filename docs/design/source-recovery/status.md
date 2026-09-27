# 项目进展与设计审查

更新时间：2026-09-27。设计基线见 [实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)。
本页是当前状态入口；[progress.md](progress.md) 保留历史证据，不覆盖本页结论。

## 更新规则

- 每个完整子任务提交同步更新本页：实际交付、构建证据、验证状态、缺口、下一步。
- 用户已取消定期设计审视，不再按提交次数或阶段切换安排审查；遇到具体架构风险时说明。
- 工作期间持续简短汇报；无运行中的会话时不承诺后台定时刷新。
- 不按文件数、提交数或接口数量宣布完成；必须以原计划的端到端验收为准。

## 当前阶段

执行目标已按用户要求调整为两阶段：

1. **优先覆盖 pinned `isa.yaml` 全部 groups 的通用反编译**：按文件中的 19 个 group
   顺序推进全部 332 个编码格式的解析、语义 IR、AST、源码输出和测试。LD/ST 方案
   纳入其中，跨作用域、词法环境、lambda、generator、async/async generator、多应用
   和历史 profile 要求不变。完整顺序见 [全 ISA 优先级](isa-priority.md)。
2. **随后完成原 T00–T12 剩余目标**：保留全部原验收要求，不缩减范围、不以第一阶段
   的局部成功替代原结构 gate 或整体完成。

各指令必需的 T07/T08/T09/T10/T11 能力在第一阶段按依赖提前实现，复用同一主干；
不等第二阶段才处理异常、捕获、receiver 或协程，也不创建平行专项路线。
独立开发，不使用 CC 或子代理；不恢复定期审视、RSS/基准或调度器开发。
源码证据规则版本为 44，默认源码契约为 V3。原 T07 仍未完成，但不再作为独立于全 ISA 覆盖的首要执行目标。
T00–T12 整体未完成；实例字段、全局变量、完整签名与函数体整合的原始 gate 未完成。
用户批准访问恢复方案并要求测试覆盖，现恢复相关单元/集成及受控生成 JS 执行。
全量测试、基准及 RSS 未恢复；下表均不代表阶段验收通过。

| 任务 | 实际状态 | 主要未完成项 |
|---|---|---|
| T00–T02 | 身份/路线、值 IR、复制与死值简化已有实现和生产构建记录 | 跨 profile、反例、预算执行验收 |
| T03、T06、T04 | 真实方法片段已走共享源码树；参数/默认值、普通调用已有实现 | 完整函数族、签名迁移及跨应用行为验收 |
| T05 | 正常边复制、自然循环、MethodEntry 初始化已接入树和独立检查 | 真实循环方法源码验收、执行验证 |
| T07 | 字面量、cell、逐创建点捕获、模块读取配对、共享语法解析已实现；新增真实文件函数族输入读取 | 全部子函数到 AST/源码的整体贯通，捕获写入与作用域闭合 |
| T08 | 新主干 Receiver/ClassTree 尚未完成 | 构造器并入通用 callable、类与实例字段整合 |
| T09–T10 | 新主干异常/async 尚未完成 | 异常相位、completion、恢复 owner；P3 子函数依赖这些能力 |
| T11 | 模块读取和别名已有前置构件，不是完整 record 支持 | 初始化/TDZ/循环依赖、模块及全局写入、整份 record 发布 |
| T12 | 语料与分类基础已有历史记录 | 全样本新路线审计、跨版本/留出集、进程 1G 实测 |

## 本次证据

- T09 异常 phi 已接入同一套并行复制与求值算法：新增显式 phased 入口，保留
  Before/Commit/Catch 可用性检查，不把异常图降格为普通图。EvaluationPlan 持有
  ExceptionCopies，独立验证缺失/伪造计划；normal 入口及普通 BindingPlan 仍拒绝它。
  抛出前仅写调用私有 CatchSlot，只有进入 Catch 才更新正式 phi 位置；正常返回
  不修改这些 phi。各抛出边为同一 catch 复用按 phi 身份分配的槽，后一次暂存覆盖
  前一次的槽值；绑定契约禁止与参数、cell、phi 或 cycle temporary 共用位置。普通边和方法
  入口仍使用既有复制调度。共享算法不跨相位融合 getter/调用等求值。
  三个 profile 的解码测试贯通异常 SSA→值 IR→区域计划→复制→求值；同一 catch
  从两个 getter 进入时，分别携带旧局部值与更新后的局部值。另覆盖 0/1/2/200/201/402
  catch phi、入口/普通/Catch 出边混合、正常返回前不写 phi、暂存更新、错误槽/落点/
  提交/修订、正常结果泄漏到 catch，以及构建和校验的精确 work 与资源边界。
  21/21 定向回归通过：exception_copies/plan、phi_copies、evaluation_plan、
  function_tree、control_plan、phased_value_flow/import、throw_value、constant_group、
  三个既有应用源码切片、creation/class_tree_context、source_route、function_input、
  async_semantics、exception_arguments/dispatch 和 argument_snapshot_source。
  abc_semantic 构建通过；新增阶段分别先运行失败再通过。未运行全量、RSS 或基准。
  **没有新增异常 JS 输出覆盖**：当前新证据到求值计划为止，生成 JS 测试是既有路径
  回归。下一步必须把 CatchBinding/CatchSlot 绑定和两阶段赋值接入共享 AST/控制树，
  验证实际 try/catch 执行和 PC 位置；不得直接把抽象 exceptional EdgeCopies 放在
  抛出前执行。规则仍为 44，group 1、finally、async owner 及全 ISA 目标均未完成。

- T09 新增通用值 IR 的 ExceptionPlan 与独立对照验证：重新核对原始相位导入、
  保护区/handler 范围及优先级，按原 TryId 关联 Before 入口和 CatchDefinition。
  普通边、抛出边、Catch→handler 边采用不同类型；catch 载荷不能冒充函数体局部值。
  计划只描述异常区域和边，不改变 CFG/SSA，不清空 phases，不授予源码发布能力。
  构建与校验共用有界预算；一次索引 phase/definition，避免逐 handler 扫值表。
  嵌套测试暴露并修正了原区域校验对“同一入口开始的多层 try”的误拒绝；入口祖先
  按外到内顺序预计算，不在每条边上遍历祖先。普通跳入 catch 仍明确拒绝。
  新测试先在未实现计划处运行失败，再覆盖三个 profile、旧局部值、异常再抛出、
  200/201/402 前导指令与嵌套区域、错误载荷/入口/范围顺序/边分类/修订/PC 及精确预算。
  14/14 定向回归通过：exception_plan、throw_value、phased_value_import/flow、
  control_plan、function_tree、evaluation_plan、async_semantics、exception_arguments、
  exception_dispatch、constant_group 及 pasteboard/Immersive/call 三个现有源码切片；
  abc_semantic 构建通过。未运行全量测试、RSS 或基准。
  **尚未增加 try/catch 源码输出覆盖**，规则仍为 44；下一步将本计划接入共同的
  求值/绑定/控制树：异常边输入在抛出前暂存，正式 phi 写入在 Catch 提交，catch 参数进入真实 handler
  scope，再接共享 TryCatch AST、printer 与位置验证。finally/async owner 仍未完成，
  不以区域计划测试替代 group 1 或全 ISA 的端到端验收。

- 显式 THROW_PREF_NONE 已端到端接入普通无 handler 主干：ValueThrow/kThrownValue
  保留原值，ThrowStmt 进入通用 AST、独立结构/语义验证、printer 和 PC→语句位置映射。
  文件来源的三个 profile 新增 39 个生成函数，覆盖常量/严格 this 抛出、回调副作用、
  较早异常优先、undefined/NaN/-0/bigint/symbol/Proxy 等原值身份与无隐式转换。
  源码规则升到 44，V3 契约不变；未新增专用 writer。
  异常主干同步接入显式 throw，保留其结构性不可达、无值/无效果 Commit；不制造
  正常落空边，Catch/向调用方传播及异常 payload 仍独立核验。200/201/402 前导指令、
  内部 Hole、错误完成/载荷、AST 被换成 return/表达式、非法表达式 ID 均有测试。
  20/20 定向回归通过：throw_value、phased_value_import、phased_value_flow、
  value_flow/value_import/value_simplify、function_tree、evaluation_plan、control_plan、
  source_route、constant_group、async_semantics、exception_arguments、function_input、
  creation_tree_context、class_tree_context、argument_snapshot_source 及三个应用源码切片。
  abc_semantic 构建通过；新增导入与文件源码正例均先运行失败，再通过。
  未恢复全量测试、RSS 或基准，1G 上限未在本轮实测。
  **尚未完成 try/catch/finally、通用不可达指令保留、async owner 与 group 1**；
  本项是依赖提前实现，也不代表 group 8 全部格式已覆盖。

- 已新增真实异常 SSA 到通用值 IR 的 ImportPhasedValues 和独立 VerifyImportedPhases。
  对照原始 PC、相位身份、输入 ABI、操作/效果、Commit/Catch 定义、SSA 每次读取及
  phi 入边；异常出口保留选中的 TryId/HandlerId/handler PC/Catch 块，区别向调用方
  传播。普通导入拒绝该 handler 元数据，普通改写不能篡改它；未新增专项源码 writer。
  输入是同一未变更 flow/SSA/env/value/proof/frame 的借用集合；预算覆盖输出图与
  验证 scratch，输入保留存储由调用方另行准入。顺序读取 SSA phi 输入避免逐 phi 扫图。
  初始快照正例在返回“不支持”的占位实现处运行失败，随后通过；不是只增加接口。
  13/13 定向回归通过：phased_value_import、phased_value_flow、value_flow、
  value_simplify、value_import、exception_arguments、async_semantics、control_plan、
  evaluation_plan、function_tree，以及 pasteboard/Immersive/call 三个既有源码切片
  执行测试；abc_semantic 构建通过。新增用例含三个 profile 的 0/1/200/201/402 参数、
  handler 汇合 phi、词法环境前导、catch 读取旧局部值、元数据反例及精确 work 边界。
  未运行全量、RSS 或基准；未声称 1G 上限已实测。
  **仍不算异常源码贯通**：通用不可达相位、归一化恢复 owner、环境/捕获与
  源码作用域对照、异常区域 AST/completion 和源文件发布仍待完成，group 1 未完成。

- T09 相位可用性已纳入同一个 ValueProgram：Before 保存操作与效果，正常结果
  显式指向 Commit，CatchDefinition 独立定义异常值，保留异常边及不同于指令 PC
  的相位身份。共享支配关系/稀疏 phi 检查阻止正常结果泄漏到抛出路径；普通导入
  对照、简化、改写、求值和控制规划明确拒绝相位图，不静默丢弃相位。
  新测试覆盖 Catch/正常结果隔离、异常边 phi、正常跳入 handler 的汇合、畸形
  Commit/边/handler、精确预算及 200/201/402 阶段对直线和循环图。
  最新验证 12/12：phased_value_flow、value_flow、value_simplify、value_import、
  exception_arguments、async_semantics、control_plan、evaluation_plan、function_tree，
  以及 pasteboard/Immersive/call 三个既有生成源码切片执行测试；abc_semantic 构建通过。
  初始新测试在缺失相位 API 处编译失败，并非运行期红测；未运行全量/RSS 验证。
  **这不是 T09 或 group 1 完成**：初始独立导入与对照见上；通用不可达相位、
  环境/捕获衔接、异常区域 AST 和 async owner 仍未接通，不另建源码 writer。

- 已补齐异常图中的 GETUNMAPPEDARGS 帧衔接：BuildExceptionFlow 接收并保留布局，
  复用普通语义提升的隐式参数窗口；异常源码调用方传入真实 FrameLayout，但不
  自动伪造调用归属。VerifyExceptionInputs 可接收显式 ArgumentFrameFact 并在
  成功结果中保留，验证窗口/效果、Before 读取顺序与槽、对应 Commit 的正常结果；
  缺失事实（包括零参数）或事实与布局不符拒绝，内部值不能借事实变成 JS 值。
  正常与异常路径复用同一快照效果检查，O(N+U+S log V) 工作、O(1) 额外验证存储。
  新增 0/1/200/201/402 参数、正常结果与 Catch 异常值分离、InstructionSsa 不把
  Catch 映射为普通指令定义、词法环境成功 pop／异常保留、畸形布局与效果、错误
  Commit、内部槽值和精确 work 边界测试。新正例先在原异常提升处失败。
  8/8 定向回归通过：exception_arguments、argument_snapshot、async_semantics、
  function_input、creation_tree_context、class_tree_context、source_route、
  argument_snapshot_source（原 51 组源码执行）；相关构建与 abc_semantic 成功。
  未运行全语料、RSS、基准或 Ark VM 差分。源码规则仍为 43；这不是异常 AST、
  完整 async 或 group 1 完成。下一步仍为 T09 相位化值 IR 导入，再接统一异常 AST；
  真实创建 owner 必须在源码边界独立闭合，不能把布局匹配当作 owner 身份证明。
- 用户已明确同意恢复后的源码文字不同，同时要求执行行为、作用域、参数、返回值
  和副作用保持正确。本轮落实 [源码契约 V3](../source-contract-v3.md)，只增加文字
  差异许可，不改变 V2 的其他前提，不豁免 ABI、捕获、异常、宿主快照工厂或求值
  顺序检查。V3 成为引擎默认，规则升至 43；收据和位置产物记录版本。
  V3 的箭头实参载体不再需要外加文字许可；V2 仍须显式许可，撤销后输出仍拒绝。
  新正例先在旧默认门禁失败，随后 6/6 定向回归通过：creation_tree_context、
  class_tree_context、source_route、tree_globals、family_enclosure 和
  argument_snapshot_source（51 个普通/箭头/类方法源码变体执行及 PC 映射）。
  三版本收据往返、未知版本拒绝和 V2 其他策略不变均覆盖；相关目标与 abc_semantic
  构建成功。没有改动 CFG、scope 或函数体变换，也未运行全语料、RSS 或 Ark VM 差分。
  下方“文字前提待确认”是历史记录，现已由本项取代。
  group 1 与完整 async 仍未完成；继续 T09/T10 时已定位异常图未传 FrameLayout、
  无法组合 GETUNMAPPEDARGS 的缺口，须连同零参数快照的 owner 门禁一起接入，
  不能仅因没有显式寄存器读取就授予源码权限。本轮先落实用户新确认，尚未修改该接口。
- group 1 的 async 前置切片：AsyncEnter / AsyncSettle 已进入共享语义 IR，
  复用 coroutine 操作数解析、正常 SSA 与 Before/Commit/Catch 异常 SSA。
  保留原 PC、状态与结算值、正常结果提交以及抛出前的分配/堆/用户代码效果；
  不移除原字节码，不新增 writer。验证 enter 失败逃逸、resolve 异常保留状态
  寄存器、Catch 定义异常 acc，普通跳转进入 handler 则绕过 Catch 并产生正确 phi。
  新增现代编码、畸形操作数、历史编码拒绝、预算边界及 200/201/402 分配点测试。
  原始 async 操作仍被 typed value/source 门禁拒绝，尚无 owner 证明、async AST
  或可执行源码交付；规则维持 42，group 1 未完成。后续依赖与验收见
  [async phases](async-phases.md)，继续 T09 相位值导入、T10 owner 和共享 AST。
  新正例先在旧语义提升处失败；7/7 定向回归通过（async_semantics、constant_object、
  value_operands、value_import、source_route、function_input、constant_group），
  对应构建与 abc_semantic 成功。扩展测试中修正了零预算在 CFG 层先拒绝的预期，
  以及旧 value_operands 测试仍将已支持 LDGLOBAL 视为 unsupported 的过期断言；
  未放宽生产门禁。未运行完整 async 源码、Ark VM 差分、全语料、RSS 或基准。
  用户对源码文本反射问题表示未理解，已用变量改名导致 toString 文本变化解释；
  仍未取得接受前提的确认，相关默认发布策略不变。
- 规则 42 新增箭头自身调用载体：保留 kArrow 身份，以类型化 InvocationForm
  选择带原名、不可构造的简洁方法表达式；共享 AST/结构/ABI/printer 接入，不新增
  body writer。动态 receiver、LDTHIS、new.target 的既有拒绝不放宽，捕获仍使用
  原 owner 的 cell/symbol；LDFUNCTION 自引用复用私有创建绑定。
  新路径默认不发布：除宿主快照工厂，还须显式 CallableTextPolicy::kRecovered，
  缺失或撤销时 kCallableText 拒绝，位置产物记录承载形式与文本策略。原因是
  Function.prototype.toString 可观察重建源码文本；已向用户确认整个引擎的等价
  范围，当前尚未收到答复，未暗中扩展 V2 或增加 CLI 开关。该问题亦影响其他
  重建源码，方法承载实现不证明整个引擎的源码反射等价。
  51 个文件级合成源码变体执行通过，包括箭头分支/循环/自引用、bind 后不可构造、
  缺失/撤销文本策略拒绝及 PC 映射；伪造 callable/承载形式、名称和 new.target
  输入亦拒绝。新正例先在旧 AST 门禁失败；本轮 17/17 定向回归通过，相关目标与
  `abc_semantic` 构建成功。未做 Ark VM 差分、全语料、RSS 或基准；group 1 未完成。
- 规则 41 补齐值默认参数到函数族的衔接：复用 DefaultBody/DefaultValueProof，
  在环境重建与闭包边重绑定之前投影，不增加类方法专用 writer。原默认分支迁入
  签名后成为剩余 IR 的 Nop，原始 PC 仍由签名证明持有；保留剩余指令索引，
  不复制原始值对应关系。默认值合流后的值可作为快照条件输入，原始入参逃逸、
  缺调用帧事实、错误角色/槽数、其他协议消费者仍拒绝。已有类方法默认值正例
  原先在函数族阶段失败，现接通；同时修正元数据与编码头不一致反例的预期，
  该反例实际在解码门禁以 kBody 拒绝，而非走到参数门禁。
  新增普通/整数默认值/负数计算默认值三组类方法快照源码执行，合计 23 个文件级
  合成变体；验证方法 name/length/不可构造性、遗漏与显式 undefined、非 undefined
  值、额外尾部及宿主绑定要求。混合默认值、rest、箭头和协程仍未整体接通。
  `class_tree_context_test`、`default_candidates_test`、`argument_snapshot_source_test`
  及创建/环境/求值/ABI/路线相关 17/17 定向回归通过；`abc_semantic` 与对应测试
  目标构建成功。未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照首个宿主契约源码切片贯通：ArgumentSnapshotExpr 经共享 forest、结构/
  流/操作数检查、ABI 及 printer 输出。严格普通函数的原生 arguments 只作为
  未暴露的实际数量/额外尾部载体，声明槽仍按实际数量条件读取；返回值必须由
  显式 HostArgumentSnapshotBinding 工厂按 v7 契约新建，不能直接返回 arguments
  或数组。绑定名称、类型化 ABI 与契约版本写入位置产物；缺绑定、遮蔽、V1
  资源契约、错 owner、错槽对应均拒绝。箭头仍拒绝，未虚构其自身调用载体。
  文件级普通函数包含读、改写参数、重复快照、分支汇合和有限循环；源码执行与 PC 映射
  测试使用独立测试工厂。实际应用宿主适配、多应用与更多 callable/默认值/rest
  组合仍待验证，未将 GETUNMAPPEDARGS 或 group 1 标为整体完成。
  源码运行覆盖 20 个文件级合成变体（0/1/2/200/201 声明参数），每个再测试不同实际
  实参数量；包含分支 false/0 与 undefined/null/BigInt 的区别、构造调用、对象身份、
  属性描述符/顺序、公共数组迭代器改写及原始 PC→生成范围。封存后方法 receipt
  为 Complete，但明确依赖输出中的宿主工厂契约，不等于整份 record 或全组完成。
  新增循环覆盖 2/200/201 声明参数：两次迭代分别保存快照到参数，退出后再次读取；
  执行验证各轮槽值、嵌套对象身份与跨调用隔离，实际零参数也必须调用工厂三次。
  复用既有 CFG/phi/循环 AST，无生产代码或规则版本改动，不扩大 callable 支持声明。
  本轮循环相关 8/8 定向回归通过（上述快照/创建/求值测试及 control_plan、phi_copies、
  function_tree、tree_structure）。其中既有 control/phi 4096 规模夹具最初触发
  kWorkLimit，已改为显式有界 O(n log n) 测试配额，并保留精确 work / work-1
  成功与拒绝检查；未放宽生产默认限制，也未启动性能基准或 RSS 测量。
  新正例先在旧 AST 门禁失败；`argument_snapshot_source_test`、
  `creation_tree_context_test`、`argument_snapshot_test`、`argument_evaluation_test`、
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `tree_globals_test`、`call_plan_test`、`evaluation_plan_test`、
  `default_candidates_test`、`function_input_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 15/15 通过；相关目标和
  `abc_semantic` 构建成功。未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照进入共享表达式规划：新增类型化 ConditionalArgument 输入模式，保留
  协议读取/原序号/语言值域及当前调用前提，不将条件读取改成必定求值。
  槽值的生产表达式禁止内联到快照里，包括携带 getter 的纯复制节点；独立调度
  检查器验证该边界，并保留快照自身的有序分配事件。CFG、作用域和 phi 路线不变。
  普通/arrow 文件夹具推进到求值计划，AST 仍拒绝；实际 count/tail 的源码绑定、
  精确对象工厂和统一 printer 尚待实现，本次仍不计完整指令源码覆盖。
  新正例先在旧求值计划门禁失败，再通过实现；覆盖三 profile、0/1/200/201/65536、
  getter 后写参数、重复快照、分支 phi、循环、伪造内联、丢失归属、错效果/序号/
  读取目的/内部值域和预算。`argument_evaluation_test`、`creation_tree_context_test`、
  `evaluation_plan_test`、`argument_snapshot_test`、`value_simplify_test`、
  `default_candidates_test`、`function_input_test`、`constant_group_test`、
  `value_created_family_test`、`source_route_test`、`function_tree_test`、
  `call_plan_test` 共 12/12 通过；相关目标及 `abc_semantic` 重建成功。
  未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照进入共享值 IR：ValueArgumentSnapshot 保留声明槽数量及条件协议读取，
  ValueProgram 保留已核对的当前调用 ArgumentFrameFact；不是将声明数量当实际数量。
  导入重新核对原指令/效果/SSA；对应检查验证槽数量、归属、PC、读序号和异常出口。
  简化器复制并保留归属，改写检查拒绝丢失/变更该事实；快照即使效果字段被清空也
  不可作为纯操作删除。文件级普通/arrow 输入已到达值 IR，求值计划仍显式拒绝，
  运行时 count/tail、精确对象工厂、AST 和源码绑定仍未完成，不计指令源码覆盖。
  新测试覆盖三 profile、0/1/200/201/65536 参数、重复分配与参数槽改写，
  错归属/槽数量/读取目的/序号/异常效果、预算耗尽及简化保留。
  文件级正例先在旧值导入处失败；新增改写检查又捕获 Clone 遗漏归属，补齐后
  `argument_snapshot_test`、`creation_tree_context_test`、`function_input_test`、
  `snapshot_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test`、`value_simplify_test`、
  `default_candidates_test` 共 10/10 通过；相关目标及 `abc_semantic` 构建成功。
  未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参输入已绑定精确 callable owner：FunctionInput 在核对文件身份、目标和帧后
  提供 ArgumentFrameFact，并保留原创建点 stamp；同目标不同创建点不可互换。
  独立检查原 opcode/PC、规范帧、范围、正常/异常效果及 SSA 读写对应关系，
  随后继续逐值域验证，Hole 不因潜在缺省实参而放行。源码规则版本 37。
  分析层覆盖三 profile、0/1/200/201/65536、改写/分支/循环、错 IR/SSA 和预算；
  文件级普通/arrow 覆盖 0/1/200/201 参数、缺 owner、错身份/目标/帧和不同创建点。
  证明保留实际数量/尾部属于当前调用的前提，不将声明数量变成实际数量，
  不把箭头函数绑定到外层 arguments。ValueOperation、运行时 count/tail 绑定、
  AST 与源码仍待实现；带 owner 的文件级输入目前在值 IR 阶段拒绝，不计完整输出。
  同时修复该文件级夹具的旧 API 预算参数、写死的创建 PC 和角色冲突取值错误。
  新文件级 owner 正例先在旧 kArgumentOwner 门禁失败，再通过实现；
  `creation_tree_context_test`、`function_input_test`、`argument_snapshot_test`、
  `snapshot_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 8/8 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参快照的共享 OperandView 已接通：常量大小的窗口视图按 32-bit 序号
  描述每个当前参数槽，标记为 Argument 协议依赖，不授权无条件 JS 求值。
  独立校验读范围、acc 写入、allocation 和异常前效果；错槽、错数量、
  缺范围、额外固定读、错写入及删改效果均拒绝。相同 ABC 经 IR/SSA 后再核对
  操作数对应关系；覆盖 0/1/200/201/65536，以及 28-bit 最大窗口的 O(1) 访问。
  实际数量/尾部所有者、ValueOperation、AST 尚未接通，源码仍以 kArgumentOwner
  拒绝。未改变源码发布范围，规则版本仍为 36，不计为该指令端到端完成。
  `snapshot_operands_test` 先因不支持快照失败，实现后与 `argument_snapshot_test`、
  `call_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 7/7 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- GETUNMAPPEDARGS 已接入共同语义提升和正常 CFG/SSA：ArgumentSnapshot 保留
  当前声明槽的隐式 may-read、每次执行的 allocation 及异常前效果，acc 仅在
  正常返回定义。FunctionInput 传入真实 FrameLayout；无帧信息、错位/不匹配
  布局继续拒绝。32-bit RegisterRange 与 16-bit CallRange 分离，保留资源预算。
  源码验证以 kArgumentOwner 显式拒绝，尚未提供实际数量/尾部所有者证明、
  值 IR adapter 和 AST；零声明参数也不能越过该边界。规则版本 36。
  新测试覆盖三个 profile、0/1/200/201/65536 槽、当前槽覆写、连续快照、
  分支合流和循环回边，以及巨大窗口的预算拒绝；不是生成源码完成证据。
  `argument_snapshot_test`、`argument_window_test`、`call_operands_test`、
  `register_ssa_test`、`register_uses_test`、`register_effect_test`、
  `constructor_call_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 11/11 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参窗口新增 ArgumentWindow：复用 BuildFrameLayout 验证隐藏 ABI 槽顺序、
  28-bit 计数和参数边界，以 ActualArgumentCount 区分当前槽前缀与额外实参尾部。
  计算 O(1)，不按声明或实际实参数量分配；声明数量不作为实际数量的默认值。
  rest 证明已消费该校验，错置 first_parameter 的反例先失败后修复。
  测试覆盖全部调用标志组合、零参数、200/201/65536、最大运行时字段、
  数量溢出及各隐藏槽错置。规则版本 35；仍待 callable owner、语义 IR/SSA
  快照依赖和源码接入，不计为 GETUNMAPPEDARGS 完成。
  `argument_window_test`、`rest_arguments_test`、`frame_layout_test`、
  `source_route_test`、`constant_group_test`、`value_created_family_test` 共 6/6
  通过；对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参快照前置：共享 SSA、值 IR、操作数对应关系及 guarded-read 查询的读取
  序号扩为 32 位，不再把编码范围的 16-bit 宽度错误套到整个输入列表。
  65535 项范围加 callee/receiver 的边界用例先因旧上限拒绝，新增用例验证
  每个 occurrence 保持顺序、重复槽保持同值但不同 occurrence、末项不截断。
  编码 CallRange 仍为 16 位，参数窗口的独立范围模型仍待接入；不计为
  GETUNMAPPEDARGS 完成，也不改变 CFG、作用域或副作用顺序。规则版本 34。
  `register_ssa_test`、`register_uses_test`、`call_operands_test`、
  `guarded_reads_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 8/8 通过；相关目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- GETUNMAPPEDARGS 的运行时核对发现 rest 签名证明边界缺口：该指令没有显式
  寄存器操作数，但按实际实参数量复制当前物理实参槽，会隐式读取原 rest 槽。
  ProveRestPrologue 原本会错误放行这种后续读取，新增反例先失败，现已拒绝。
  未把指令错误提升为零输入常量，也未直接输出 native arguments/rest 数组。
  [实参快照设计](argument-snapshots.md) 记录新对象身份、caller/callee 描述符、
  帧数量/尾部输入、SSA 当前槽依赖、16-bit occurrence 边界和 callable 约束。
  规则版本 33；本项是接通该指令的前置修正，不计为 GETUNMAPPEDARGS 源码完成。
  `rest_arguments_test`、`source_route_test`、`constant_group_test`、
  `value_created_family_test` 共 4/4 通过；相关目标及 `abc_semantic` 构建成功。
  未运行 RSS/基准/全语料或 Ark VM 差分。
- LDSYMBOL 已贯通 ValueSymbolConstructor -> SymbolConstructorExpr -> 统一
  printer，并通过显式 HostSymbolBinding 记录原始构造器的不可变宿主绑定。
  V1/V2 均不自动假定公开 Symbol 属性稳定；缺能力、名字遮蔽或与 realm
  绑定同名时拒绝。普通全局读取也不能被新绑定捕获；换目的地重新验证。
  PrintedFunction 和位置 JSON 保存绑定要求及契约，PC 映射到真实标识符区间。
  三个 profile 的普通/arrow、直接/回调共 12 个文件级变体，在公开 Symbol
  被替换和抛异常 getter 两种环境下执行；保留身份、Symbol 调用、不可构造性、
  回调异常与重入。两个窄/宽闭包变体验证词法捕获、key 异常和 setter 重入；
  六个缺能力反例继续拒绝。作用域单测覆盖 V1/V2、非法名、外部/参数/模块名
  冲突、祖先/兄弟块、能力冲突和 200/201/4096 work 上界；没有测量 RSS/延迟。
  规则版本 32；下面规则 31 的源码拒绝状态是历史前置。本项不代表整个第一组
  或 record 发布完成，V1 命名创建及协程/derived owner 仍受独立前提限制。
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `evaluation_plan_test`、`tree_globals_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`computed_store_test`、`value_created_family_test` 共 10/10 通过；
  对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDSYMBOL 新增 SymbolConstructorLoad -> ValueSymbolConstructor，保留 pinned
  GlobalEnv::SymbolFunction 身份，不将其误写成 Symbol/globalThis.Symbol 查找。
  无寄存器输入，只定义 acc，结果 Defined、无普通异常或可观察效果。
  三个 profile 新增导入、值域、畸形操作数及 IR 身份替换测试；测试先失败于
  语义提升，再实现。源码端仍拒绝，六个文件级直接/回调反例防止误发 Complete。
  规则版本 31；这是可信 intrinsic 绑定/AST 的前置，不计为 LDSYMBOL 源码完成。
  未扩大 V2 的 globalThis 前提，后续步骤见
  [Symbol Constructor Dependency](constant-group.md#symbol-constructor-dependency)。
  `value_import_test`、`evaluation_plan_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 5/5 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDGLOBAL 已接通 ValueGlobalObject -> RealmObjectExpr -> 统一 printer：
  在实际函数族目的地解析稳定 globalThis 或显式宿主绑定，复核祖先作用域与
  外部绑定后才发布。每次换目的地重查，缺上下文或遮蔽在写入前拒绝。
  PrintedFunction 保存实际 policy/name，位置 JSON 序列化该前提与契约版本；
  独立语义检查拒绝替换为普通全局名称或 this。规则版本 30，未改变 CFG/SSA/cell。
  三个 profile、普通/arrow、直接/回调、稳定/宿主绑定共 24 个文件级变体
  生成并执行 JS，检查身份、异常、重入、PC 区间及 JSON 前提；两个窄/宽
  闭包样本验证 realm 捕获、错误 receiver、key 异常及 setter 重入。
  V1 缺宿主/显式宿主、非法名字、局部/外部遮蔽与祖先/兄弟 scope 由单测覆盖；
  V1 完整命名创建仍受原资源异常契约限制，不冒充执行正例。以下规则 29 的
  源码拒绝状态为历史前置。本项不代表整个第一组或 record 发布完成。
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `evaluation_plan_test`、`tree_globals_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`computed_store_test`、`value_created_family_test` 共 10/10 通过；
  对应目标和 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDGLOBAL 的新主干入口已补齐：既有 GlobalObjectLoad 导入独立
  ValueGlobalObject，无寄存器输入、只定义 acc、结果 Defined；不复用普通
  ValueGlobalRead 或 globalThis 字符串语义。依据 pinned interpreter 的
  GetGlobalObject 初始化及 LDGLOBAL handler，未引入新的宿主前提。
  三个 profile 新增导入、值域、零操作数、错误 IR 身份替换与畸形编码测试；
  测试先失败于值 IR 导入，再实现。源码求值/AST 检查暂时明确拒绝，六个
  文件级直接/回调反例防止误发 Complete。该项是 realm AST/绑定证明的前置，
  `value_import_test`、`evaluation_plan_test`、`constant_group_test`、
  `source_route_test` 共 4/4 通过，对应目标与 `abc_semantic` 构建成功。
  规则版本 29；未运行 RSS/基准/全语料或 Ark VM 差分。
  不计为 LDGLOBAL 端到端完成；后续执行步骤见
  [Realm Object Dependency](constant-group.md#realm-object-dependency)。
- LDFUNCTION 扩展到普通函数/arrow 的创建期私有不可变绑定：类型化 SelfForm
  区分命名函数表达式与 CreationBinding，共用现有 IR、作用域验证和 printer。
  空/非法 runtime name、参数/全局名字冲突、含子函数的 owner 使用私有 const；
  创建后才允许调用，不读取可变 function.name，不改变 CFG/SSA/cell 归属。
  选择器每个 owner 扫描一次参数、捕获、模块和表达式，计入字符比较与预算；
  新增分配沿用既有资源耗尽排除契约，未增加宿主假设。规则版本 28。
  三个 profile 的 60 个名称/callable/直接或回调变体生成并执行 JS；另有 12 个
  同名全局 getter 变体、2 个子函数全局名字与父函数同名的窄/宽编码函数族，
  验证异常顺序、身份、重入、name 描述符、length、可构造性及 PC 映射。
  参数同名与非法 form/evidence/const/contract 由 AST 单测覆盖，不冒充文件级证据。
  `function_tree_test`、`binding_plan_test`、`tree_structure_test`、`tree_globals_test`、
  `family_enclosure_test`、`evaluation_plan_test`、`constant_group_test`、
  `computed_store_test`、`source_route_test`、`value_created_family_test` 共 10/10 通过；
  对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
  缺失名字元数据、上游边界外 Unicode、method/async/derived 身份恢复仍未完成；
  本项取代下方规则 27 时空/非法名字、普通 arrow 与命名冲突仍拒绝的历史状态。
- LDFUNCTION 普通命名函数已端到端贯通：BindingPlan 为实际 owner 分配一个
  FunctionSelfBinding，位于参数作用域之外、捕获/模块作用域之内；表达式森林
  降为 SymbolExpr，结构/flow/operand/ABI 独立核验，统一 printer 输出命名函数
  表达式。没有函数体 self let，没有更改 CFG/基本块或环境 cell 的归属。
  ABI 分开统计 frame_function_reads/self_references，明确报告 FrameIdentity；
  创建点继续核对 runtime name，原始 PC 映射到精确 self 标识符区间。规则版本 27。
  三个 profile 的 12 个直接/回调/覆盖 ABI 副本变体执行通过；另有 4 个名称边界
  正例与 6 个非法/空名或 raw arrow 拒绝用例。运行检查外部变量重绑定、修改
  function.name、construct 返回、回调异常及重入；两种宽度的闭包写入样本验证
  父函数身份捕获、多 activation、错误 this/显式参数、key 异常与 setter 重入。
  AST mutation 拒绝参数替换/遮蔽、缺失或可写 self、错误 scope、非法名字及
  不支持的 callable。新增端到端测试先失败于 PrepareCreated，再实现并通过。
  `function_tree_test`、`binding_plan_test`、`tree_structure_test`、`tree_globals_test`、
  `family_enclosure_test`、`evaluation_plan_test`、`constant_group_test`、
  `computed_store_test`、`source_route_test`、`value_created_family_test` 共 10/10 通过；
  对应目标与 `abc_semantic` 构建成功。旧 self 拒绝断言已改为精确非法绑定诊断。
  未运行 RSS/基准/全语料或 Ark VM 差分；空/非法名、现有标识符边界外的 Unicode、
  参数/全局命名冲突及 arrow/method/async/derived 的完整身份恢复仍未完成。
- LDFUNCTION 已接入公共 FrameFunctionLoad -> ValueFrameFunction：按 pinned
  GetFunction 读取独立 frame.function，无寄存器输入，只写 acc，正常结果为
  Defined 语言值；不把可写 ABI function 副本或函数名称当作运行时身份。
  三个 profile 验证无 ABI slot、覆盖副本、malformed 操作数及 IR 身份篡改；
  先观察到缺少语义提升的失败，再实现并通过。该前置提交中 self scope 尚未接线，
  源码求值与 AST 明确拒绝该操作，六个文件级直接/回调变体验证不误发 Complete。
  `value_import_test`、`constant_group_test`、`evaluation_plan_test`、
  `source_route_test` 共 4/4 通过；对应目标及 `abc_semantic` 构建成功。
  规则版本 26；未运行 RSS/基准/全语料或 Ark VM 差分。本项不计为 LDFUNCTION
  源码完成；后续 self scope、创建点身份、名称/遮蔽、统一 printer 和端到端执行
  验收顺序已写入 [第一组设计](constant-group.md#current-function-identity)。
- LDTHIS 已对初始化的普通 callable owner 贯通 FrameThisLoad、值域证明、
  ValueFrameThis、ThisExpr、独立语义/ABI 检查与统一 printer；不依赖可写 ABI
  receiver 副本。FrameReceiver 前提随输入证明保存，发布时复核 owner；无 owner、
  raw arrow、derived、async 或存在 SuperRange 的方法不自动获取该前提。
  三个历史 profile 共 12 个直接/回调/覆盖 ABI 副本变体实际生成并执行 JS，
  检查 call/construct、primitive/null/undefined/Proxy receiver、异常身份及 PC 映射。
  另有 3 个 raw arrow 拒绝反例和 narrow/wide 父 receiver 词法捕获用例，覆盖
  多次 activation、错误显式参数、不同子调用 receiver、抛异常 key 和 setter 重入。
  ABI 单独记录 frame_receiver_reads；源码证据规则升至 25，旧 receipt 失效。
  `source_route_test`、`constant_group_test`、`computed_store_test`、
  `value_import_test`、`function_tree_test`、`function_input_test`、
  `value_operands_test`、`value_simplify_test`、`evaluation_plan_test` 共 9/9 通过；
  对应目标与 `abc_semantic` 构建成功。未运行 RSS/基准/全语料，未作 Ark VM 差分；
  第一组整体未完成，derived/arrow/async receiver 相位证明仍待实现。
- receiver owner 前置接线完成：PreparedFamily 在根函数输入验证前传入真实
  FunctionContext；子函数在读取前先解析实际 creation occurrence，随后走相同入口。
  FunctionInput 保存不可变 creator/PC/class property/callable/strictness/frame
  标识，输入前拒绝错误 identity/target/frame，构建源码树时再次核对实际 owner。
  此前置提交保留 ownerless 诊断入口，不自动增强 receiver 值域；源码接线见上项。
  同目标两个创建点不能串用的正反例、父子 owner 对应及现有闭包输出均通过；
  `function_input_test`、`constant_group_test`、`computed_store_test` 共 3/3 通过。
  fixture、输入测试及 `abc_semantic` 构建成功；补齐旧输入测试缺少的 algorithm 包含。
- LDTHIS 新增独立 FrameThisLoad 语义操作：读取 frame.thisObj，不读取/别名化
  可写 ABI receiver 寄存器；保留 acc 写入、无普通语言效果和 Unknown 值域。
  三个历史 profile 验证 ABI 副本覆盖不能证明 frame receiver 初始化，缺少 owner
  证明时返回它仍以 InternalValue 拒绝；malformed 操作数也有反例。
  `value_import_test`、`constant_group_test` 2/2 通过，`abc_semantic` 和 fixture
  构建成功。该提交仅完成语义前置，不计入 LDTHIS 源码完成量；后续 owner 输入
  与值 IR/ThisExpr 接线见上两项。
  具体集成顺序已写入 [第一组证据](constant-group.md)，不新增构造器专项 writer。
- DEBUGGER 已贯穿公共 SemanticOperation、ValueOperation、求值计划、独立
  DebuggerStmt、语义/来源校验与统一 printer。它保留 acc、无结果，有独立有序
  观察效果，不允许按 NOP 或纯操作删除；没有增加专项 body writer。
  三个历史 profile 的 36 个函数变体新增两次 debugger，Node inspector 实际验证
  暂停/回调/暂停及异常跳过第二次暂停；原始 PC 对应精确的 `debugger;` 区间。
  malformed 操作数、伪造 AST 语句、getter 跨观察点移动均有回归。
  `abc_semantic` 与相关测试目标构建成功；value_operands、function_tree、
  evaluation_plan、value_simplify、value_import、constant_group、computed_store
  共 7/7 通过。不是 Ark VM 调试协议/寄存器视图等价证明，第一组仍未全部完成。
- 第一组普通常量端到端证据已补齐：LDNAN/LDINFINITY/LDUNDEFINED/LDNULL/
  LDTRUE/LDFALSE，三个历史 profile、直接返回/跨回调保留两种形态，共 36 个
  文件方法 fixture，经 PreparedFamily/FamilyEmission 到源码执行；检查 PC 0
  生成区间、名字遮蔽、参数/receiver、调用次数、异常身份和原值返回。
  另有 6 个 LDHOLE 返回/传给宿主的拒绝反例。未修改生产语义，未扩大接受域。
  `constant_group_fixture` 构建成功，`constant_group_test`、`constant_object_test`、
  `isa_catalog_test`、`computed_store_test` 共 4/4 通过。不是 Ark VM 差分或真实
  应用验收，第一组尚未完成；边界与剩余项见 [第一组证据](constant-group.md)。
- 全 ISA 计划已启动：新增类型化 `IsaGroup`/`IsaSpec`，固定 YAML 的 19 groups、
  332 格式均进入不可变清单；显式生成边界拒绝未知/缺失/顺序变化的 group，
  运行时 Opcode 二分查找、不按字符串分派、不分配内存。清单不授予源码发布能力。
  新增全格式解码、acc 标志、全部截断长度、归组数量和未知 opcode 测试；
  独立 YAML 副本的未知 group 生成反例通过。第一组原有 18 条专项语义测试
  现与清单交叉检查，防止漏项或重复项冒充覆盖。
  `isa_catalog_test`、`constant_object_test` 构建成功；两项新 catalog 测试、
  两项 access catalog 测试、`constant_object_test`、`instruction_test` 共 6/6 通过。
  下一步是第一组逐指令的通用 IR/AST/源码证据与缺口，不是宣布 332 格式已反编译。
- computed Set 集成测试增加窄/宽格式的捕获箭头函数：父函数创建词法环境、
  子函数读取并更新捕获 cell 后写计算属性，共用 PreparedFamily/FamilyEmission。
  独立源码 oracle 比较多次调用、独立激活、key 抛出和 setter 重入的事件顺序、
  异常身份及最终值；检查子函数原 PC 14 的生成区间、箭头 prototype 和 arity。
  修正 fixture 提前 unlink 导致 ctime 改变的问题，保留生产文件身份检查。
  `computed_store_fixture` 构建成功，`computed_store_test` 1/1 通过；
  不代表循环环境、兄弟闭包、generator/async 或完整异常路径已覆盖。
- L0 新增 pinned ISA 访问清单：146 个编码格式、21 个 typed family，包含 wide、
  deprecated、runtime 前缀；同时纳入 MOV/FLDAI、环境建立及 named definition 依赖。
  生成阶段校验显式边界映射的遗漏、重复和失效项，运行时不按指令名称字符串分派。
  清单及查找不分配内存，查找 O(log N)。这是编码归属，不是 lift/AST 支持证明；
  profile/实际 route 的分阶段证据接线仍待完成，不把 146 项写成已支持反编译。
  独立 ISA 副本加入未知 LD 时生成失败，原输入未改动；细节见模板说明。
  `access_catalog_test`、`access_catalog_guard_test`、`instruction_test`：3/3 通过；
  `abcd_analysis` 与两个 C++ 测试目标构建成功。未运行性能或 RSS 测试。
- 已开始执行 [访问恢复方案](access-recovery.md)，本次为 L1 computed Set，不是整个
  LD/ST 家族完成。`STOBJBYVALUE` 窄/宽格式进入同一 `ValuePropertyWrite`，保留三个
  输入、原始 PC、效果和 acc 不变语义；共用 AssignmentExpr/ComputedKey 与 printer。
  求值 shape 增加 typed ReferenceBoundary，限制 RHS 跨引用/key 转换的内联；独立
  verifier 复核边界、三个操作数及 strict 语义。未改 CFG、未增加专用 writer。
  受控字节码 fixture 经 PreparedFamily/FamilyEmission 生成可执行 JS，检查原 PC 4
  的源码区间；Node oracle 覆盖 RHS/key/setter 顺序、Symbol、null、三类抛出、只读
  Proxy 和 setter 同步重入。它不是 pinned Ark VM 差分证据，不验证 VM 错误消息一致性。
  恢复测试时修正了此前模块存储规格中的 variant 比较编译错误及错误的常量传播假定。
  L0 全量 typed 覆盖目录、模块发布、环境/捕获组合、generator/async 仍未完成；
  RHS 保留的快照是当前语义约束，不以删尽临时变量作为完成标准。
  `value_import_test`、`value_operands_test`、`value_simplify_test`、
  `evaluation_plan_test`、`function_tree_test`、`computed_store_test`：6/6 通过。
  生产 `abc_semantic` 构建退出 0；既有 pasteboard 7449/Immersive 21082 草稿仍为
  1434/160 字节且 incomplete。未做 Ark VM 差分、全量回归或 RSS 验证。
- 通用值 IR 新增 `ValueModuleWrite`，窄/宽 STMODULEVAR 共用 typed 模型，
  保留模块槽、单个 stored-value 操作数、原始 PC、正常/抛出前模块写效果及异常出口。
  不产生虚假的 accumulator 结果；复制/死值简化禁止删除模块存储，求值计划保持顺序。
  源码 AST 尚未支持模块写，构树和独立对应检查均明确拒绝，不误输出普通局部赋值。
  生产双目标构建退出 0。ABC-only 入口 7283 当前在 PC 101 拒绝，已越过 PC 11
  模块存储；7449 四成员和 Immersive 21082 单成员草稿仍生成，7477 record 片段及
  PC 194/11 初始化来源仍保留。窄/宽边界、accumulator 保持、输入篡改和简化保留
  规格已写但未编译、未运行。实际 record 绑定、声明/更新 AST 与 TDZ 证明仍待接入，
  本项不是 T07/T11 完成，也未解除 7449 的模块发布 guard。
- 模块存储输出现保留类型化初始化状态：TDZ、位置未知、原始首次存储 PC。
  仅成功写出首次声明后提交位置，后续更新不覆盖，不把 PC 0 当作缺失。
  两条 record writer 均传入实际指令位置；`BuildRecordSource` 成功时将模块表
  移入源码产物的不可变 owner，并由 record-slice 流式输出，失败不发布。
  ABC-only record 6897/entry 7283 的 slot 0/1 初始化分别为 PC 194/11，
  独立读取原指令确认均为 STMODULEVAR；7477、7586 片段区间未变。
  生产双目标构建退出 0；新增规格未运行。这是声明来源，不是模块读取等价证明，
  7449 的发布 guard 保留，实际名字绑定、TDZ 异常和 live-write 义务仍需闭合。
- 函数族作用域检查新增模块引用：模块 scope 中的 alias 不再能掩盖父函数局部变量
  或其 TDZ；仍按实际创建位置区分祖先与兄弟 block。跨成员核对 typed 模块目标与
  名字的一致性，使用排序索引和 Euler 区间，无逐引用祖先扫描，不改变 CFG。
  这只是作用域证明；destination 对缺失模块执行上下文显式拒绝，发布 guard 保留。
  生产双目标构建退出 0；ABC-only 7449 四成员草稿仍为 1434 字节，Immersive
  21082 草稿仍为 160 字节，7477 record 片段仍能生成。新增规格未编译、未运行。
  已核对干净的 pinned v7 runtime 模块读取路径。7089 的本地导出 `PageA` 与
  record 实际 `$local0` 绑定、原存储点处的 `let` 初始化需要连接，不能照抄 metadata
  名字。具体下一步见 [模块别名作用域与发布边界](family-enclosure.md)。
- 普通函数创建点已接入真实 record 装配：`FunctionCreationSelection` 按 caller/PC
  选择，同一调用不能同时选择类方法；输出必须保留对应函数族片段，否则整次拒绝。
  适配层复核实际创建操作数、借用 record 的完整绑定上下文，不新增 body writer，
  不允许选中路径失败后回退；类型化拒绝保留到 record 返回值。
  ABC-only 7117、7477 分别在 [975,1018)、[1139,1202) 输出，实际创建点为
  7089/PC 55、7089/PC 102；7586 类片段仍为 [2307,2464)。所有 record 仍明确不完整。
  7449 的真实阻点已由调试确认：成员 1（7089）的 `FamilyTreeError::kModules`。
  仅提供 globals 不能解决；模块 alias 的 TDZ/循环初始化/作用域发布义务仍未闭合，
  不能把诊断 metadata alias 当成执行证明。新增适配层/record 规格未编译、未运行。
  生产双目标构建退出 0；未运行测试、生成 JS、基准或 RSS。
- 普通/箭头函数的 `--value-source` 不再在准备后固定拒绝输出，现经过
  `PreparedFamily::PrepareCreated` 与同一个 `FamilyEmission`。结果显式要求
  strict enclosing destination；缺失 global/capture 证明仍拒绝，不插入伪造上下文。
  ABC-only 7117、7477 已输出函数表达式片段，各保留 5 条指令归属、record=6897；
  7449 仍拒绝且无源码，根收据保持 Decoded，族内失败单独报告，不虚报阶段。
  普通表达式与 record 片段共用逐成员位置/收据序列化。CLI 回归规格已写、未运行。
  生产 `abc_semantic` / `abc_structure` 构建退出 0；测试、JS 执行和 RSS 仍暂停。
  这不代表完整 P3、多应用运行验收或 record/module 完成。
- selected-method 生产入口现已切换到 `PreparedFamily` / `FamilyEmission`，
  核对实际创建点的目标、种类、length、placement 和名称；拒绝后不回退旧函数体。
  函数创建索引每次选择只构建一次并计费；原始 record 身份随逐成员收据保留。
  类型化失败投影保留嵌套资源/工作限制和最深原始指令 PC，不解析错误文本。
  生产双目标构建退出 0。ABC-only 7559 的 [2638,2924) 已实际走函数族 emitter，
  record=6897，单成员、43 条指令归属；7586 为 [2307,2464)、21 条归属，
  输出字节及收据与独立入口一致。独立 7559 入口缺上下文而拒绝，不能作为对照。
  Immersive 21082 仍在外围 record 装配 PC 159 拒绝；不冒充整份 record 成功。
  新增失败映射与更新后的 record 规格未编译、未运行；整份 record 仍不完整。
- record 的 `FunctionSegment` 现可持有单函数或完整函数族，共享原有搬运、捕获
  位置重定位、内存计费及保留检查。整族只移动一层外部区间，内部 AST/收据/位置不拆散。
  record-slice 序列化已支持逐成员收据和原始指令位置表。
  此前该运输层提交尚未切换 selected-method；现已由上项生产入口接入。
  新旧头文件接合暴露的类型重名已修正为 `ReceiverEntryBinding`，未改变 receiver 语义。
  双 payload 的重放、捕获前缀、预算与 200/201 规格已更新，仍未编译、未运行。
  生产双目标构建退出 0；ABC-only 7559 仍为 [2638,2924)、规则 24 的单函数
  record 片段，Immersive 21082 类成员草稿仍为 160 字节、不可执行、不完整。
- 新增[拥有型函数族输出](family-emission.md)：准备接口保存逐 occurrence 的
  TreeVerified/Incomplete 收据；`FamilyEmission` 在实际输出、目标语法/作用域和
  全指令位置检查成功后才产生逐方法 Printed/Complete 收据，并持有树与位置表。
  类成员和函数表达式目的位置采用枚举，不允许互换。证据规则升为 24。
  这是代码实现，尚未运行新路径测试或生成 JS；CLI 草稿和整份 record 不自动升级。
  记录装配整合、异常/async、构造器与原始结构 gate 仍未完成。
  生产 `abc_semantic`/`abc_structure` 构建退出 0。ABC-only 7559 的既有方法片段
  仍为 [2638,2924)、Printed/Complete，规则 24；7449 的四成员草稿仍为
  1434 字节、不可执行、不完整。这不构成新 `FamilyEmission` 的执行验收。
- 类方法与普通函数已共用 `PreparedFamily::Prepare`，仅创建上下文不同。
  共享 printer 支持实际类方法根节点，保留 prototype/static、名称和参数范围；
  后代仍是普通/箭头函数表达式，不生成另一套 body writer。
  ABC-only Immersive 21082 现输出 `setInitiallyProvidedValue(params)` 类成员草稿，
  21 条原始指令有归属；pasteboard 7449 仍为四成员、1434 字节函数表达式草稿。
  生产构建退出 0；两份产物仍不可执行、不完整。新增类路径规格未编译、未运行。
  构造器、类整体、异常及 async 的未完成条件没有放宽。
- 新增[原始指令源码归属表](instruction-sources.md)：整族构建逐条原始指令核对
  AST 节点、锚点、默认参数签名或已证明纯消除的归属；拒绝缺失、重复及 PC/版本错配。
  线性索引随不可变准备结果保留，草稿 JSON 流式输出 `instruction_sources`。
  纯消除使用 `range:null`，不伪造生成位置。ABC-only 的 7449 四成员覆盖表长度
  为 36/66/5/5，根签名 6 项，草稿仍 1434 字节；Immersive 21082 仍可构建。
  两者仍不完整、不可执行、退出 1。生产构建退出 0；新增规格未编译、未运行。
- 新增 [不可变函数族准备](prepared-family.md)：`PreparedFamily::PrepareCreated`
  从实际 ABC 创建点重新贯通完整 family 输入、默认值、AST、ABI、捕获/写入与
  外部作用域检查，再保存 `unique_ptr<const Storage>`；不接受任意树或统计作为证明。
  `PrintDraft` 在输出前重新检查严格模式与目标作用域，调用现有共享 printer。
  仍输出草稿，不授予 executable/complete；CLI、module 初始化及整份 record 发布未升级。
  简单/cell/嵌套写入、移动所有权、输入文件释放后输出与预算规格已写但未编译、未运行。
  生产构建退出 0；测试、JS 执行和 RSS 保持暂停。
- 新增 `VerifyFamilyDestination`：复用函数族逐创建点 scope 检查，同时核对调用方
  证明完整的外部 `GlobalScope`。缺少 context 只允许没有 global read 的函数族；
  外部 record/import/parameter/class/self/realm 绑定不能遮蔽后代全局名。
  有序外部索引借用并按比较长度计费，不按子函数复制或扫描整个 record。
  新接口尚未接入不可变发布 owner，诊断构建仍只做内部 scope 检查，不扩大覆盖率。
  外部绑定类别、缺失上下文、目标作用域变化与预算规格已写，未编译、未运行。
  本次生产 `abc_semantic`/`abc_structure` 构建退出 0；不据此声称运行验收通过。
- 单函数与文件函数族现共享箭头函数 receiver ABI 拒绝规则：动态 receiver entry
  或 owner-local `ThisExpr` 不能直接生成词法 `this`；外层 this 仍须显式捕获绑定。
  文件函数族保留各成员实际 `FunctionAbi`，不再检查后丢弃；它仍是可变构建产品，
  不是可转移的发布证据。新增普通/箭头 receiver 与 ABI 保留规格未编译、未运行。
  生产 `abc_semantic`/`abc_structure` 构建退出 0；ABC-only 的 pasteboard 7449
  四成员草稿、Immersive 21082 单成员条件树均仍可构建，退出 1，
  `source_complete=false`、`executable=false`。未执行生成 JS 或 RSS 测量。
- 整族写入域检查已接入：逐个原始 lexical store 核对 SSA Language 值域及残余
  AST cell 写入位置；仅允许已有默认参数证明迁移的 store 不再出现在函数体。
  所有创建屏障都必须由实际子函数或原始值对象证据覆盖，并重新核对捕获边。
  7449 统计为 3 个原始 store、1 个签名迁移、3 条函数创建边、2 个原始值对象；
  该样本没有捕获 store，不能代表捕获写入的运行验收。新增“子函数写父 cell”
  文件级 fixture 与缺失写入/子函数、预算规格均未编译、未运行。
- 文件函数族构建现新增内部 scope 遮蔽检查。结构遍历记录每个表达式的 scope，
  子树在真实 FunctionExpr 创建点接入父 scope，而不是把父函数全部局部变量
  都视为可见。Euler 区间与名字区间合并保留兄弟 block 边界，并计入 TDZ 绑定。
  它不改变 CFG/basic block，不补造外部 record/module 环境，也不证明捕获写入。
  本次 ABC-only 检查：7449 四成员草稿仍为 1434 字节；Immersive 21082 的
  单成员条件 AST 仍可构建。scope/深层祖先/200/201/预算规格未编译、未运行。
- 新增 `--family-source-draft`，保留 `--conditional-family` 不输出源码的契约。
  7449 的四成员 AST 已通过同一个迭代 printer 输出嵌套函数草稿；事件栈携带
  owner 和缩进深度，各成员保留表达式/语句/参数/属性/锚点的整份草稿绝对 UTF-8 区间。
  草稿不是发布收据，`source_complete=false`、`executable=false`；外层
  strict/global/module 绑定及传递前提仍未闭合。仍有 phi/效果快照临时变量。
  CLI 现通过 `draft_positions` 逐 owner 流式导出完整的现有 origin DAG、共享
  part 表、节点区间、零宽锚点、操作归属及签名迁移表。与单函数路径使用同一个
  序列化实现，不展开传递 PC 集合，也不从 JS 反推来源。源码缓冲有独立 1 MiB
  诊断上限。这些表是草稿的来源信息，不证明全指令覆盖或可执行语义等价。
  当前 ABC-only 输出共 1434 字节，四成员范围为 [0,1433)、[325,1368)、
  [1025,1079)、[1255,1329)。G2 的 7559 record 片段仍为 [2638,2924)、
  Printed/complete、规则 23；只检查了产物，未执行生成 JS。
  四成员 origin 数为 [42,84,5,5]；根签名中的 PC [16,20,21,24,26,31]
  均关联到实际生成的 `$arg0 = null` 参数范围 [29,41)。新增 CLI 映射规格未运行。
- 运行时名字已从实际创建上下文传入共享 AST。7449 四成员分别保留
  `PageABuilder`、空名、空名、`paramsLambda`；统一 printer 用计算属性的
  NamedEvaluation 命名，不新增 self 绑定、不依赖可变全局 intrinsic。
  该包装使用已批准 V2 的资源耗尽差异排除；V1 在输出前拒绝。更自然的具名表达式
  仍需证明不改变名称解析；当前没有将包装美化冒充作用域证明。
- `cmake --build build --target abc_semantic abc_structure -j 2`：退出 0。
- 新增 `--conditional-family`：只读取真实 ABC 的函数创建族、逐创建点绑定与捕获，
  不输出源码、不授予 executable/complete 收据；诊断模式退出 1 是预期行为。
- pasteboard `modules.abc` 方法 7366：`family_prepared=false`。原 PC 24 拒绝已定位并修正：
  捕获屏障包含缓冲对象，不等于函数边。共享对象解析确认 PC 24、180 只含原始值，
  保留对象证据及原始屏障，随后进入创建 PC 219 的子函数 7421；在其 PC 0 的异常
  区域输入处拒绝。完整函数族仍未成功，下一层为既有 T09/T10 依赖。
- Immersive `modules.abc` 方法 21082：输入及条件 AST 准备成功，单成员、21 个操作、
  10 个表达式、无子函数；pasteboard 7559 条件 AST 为单成员、22 个表达式。
  两者 `source_complete=false`、`executable=false`，不是闭包或跨应用源码成功证据。
- 上次 G2 方法 7559 的 record 内片段为 Printed/complete、区间 [2638,2924)、规则 23；
  **仅方法片段，整个 record 仍不完整**。本次未执行生成 JS。
- 新增测试规格未编译、未运行。内存预算代码不能代替整个进程的 1G RSS 证明。
- PageABuilder 7449 的真实函数族有四成员（7449、7089、7117、7477），
  三条创建边。此次接入发现函数创建的一律固定策略阻断了合法成员调用的求值组合；
  现允许顺序保持且不跨环境变化的内联，并已通过 cell 默认值证明和共享残余投影
  解决 length=0/1 不匹配。四成员条件 AST 已全部构建，表达式数为 [22,51,1,1]。
  实施约束见 [函数族默认参数迁移](family-defaults.md)，尚未发布完整函数族源码。
- 已接入入口 cell 默认值候选发现，7449 报告 `cell_default_candidates=1`。
  独立 `CellDefaultProof` 已接入，7449 报告 `cell_defaults_proved=true`。
  原 `kLength` 检查保留，现通过真实默认签名迁移满足；投影后的捕获已全族重新绑定。
  回归规格未运行，conditional AST 不等同 executable 收据。

## 设计对照审查

本次审查覆盖共享语法解析、模块/捕获别名和文件函数族读取的近期工作。

1. **方向一致**：沿用 typed IR、输入身份/修订、真实创建 PC、作用域和效果检查；
   没有引入专用 body writer、生成文本反推语义或配对源码输入。
2. **存在集成滞后风险**：前置构件增长，但真实完整函数族尚未贯通；不能继续以
   “又增加一个表或接口”代替 T07 的端到端交付。该项保持开放，下一轮必须复查。
3. **CFG/scope 边界仍须验证**：按创建 occurrence 区分 owner，不能按 method 合并
   可变状态；接入 AST 时必须重新验证工作修订、捕获映射、默认参数投影与效果顺序。
4. **接受域未扩大**：未新增宿主假设；不解除 Proxy-super 拒绝；完整 P3 的
   异常/async 依赖不得通过省略子函数、伪造函数体或旧路径补正文绕过。
5. **验证缺口保留**：当前没有 Validated 能力，也没有全样本完成或内存达标结论。

## 下一步交付顺序

1. 已修正缓冲对象与函数创建混淆；含方法/访问器的对象继续类型化拒绝，
   不能以无子函数论处。新增 0/1/2/200/201 重复池对象和方法值负例规格尚未运行。
2. 7449 四成员已串入共享 AST，后续在此基础上闭合 callable、外层作用域和发布义务；
   继续保留跨应用扩展及未支持模块/默认值形式，不跳过失败子函数。
3. 嵌套源码草稿、运行时名字、owner 事件区间与 origin DAG 已贯通导出；下一步
   闭合外层环境与不可变发布 owner。整族写入值域检查已接入，但不能据此标量化
   可变捕获或消除调用后的重新读取；继续处理有独立证明的冗余变量搬运。
4. 扩展跨应用证据；完整 P3 的 T09/T10 依赖明确保留，T07 不因简化样本通过而宣告完成。

定期设计审视已按用户要求取消；以上审查保留为历史记录，不安排下一次定期审查。
