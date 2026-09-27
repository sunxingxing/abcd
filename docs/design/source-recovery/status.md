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
源码证据规则版本为 25。原 T07 仍未完成，但不再作为独立于全 ISA 覆盖的首要执行目标。
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
