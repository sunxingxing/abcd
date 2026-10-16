# 当前状态

更新时间：2026-10-15。本页只记录当前事实、阻塞项和证据索引。
2026-09-25 至 2026-10-12 的流水记录原样归档于
[history/status-2026-09-25-to-2026-10-12.md](history/status-2026-09-25-to-2026-10-12.md)；
更早的证据见 [progress.md](progress.md)。

## 更新规则

- 每次提交连贯改动后更新本页：交付内容、构建与验证证据、剩余门槛。历史记录写入 `history/`。
- 不按文件数、提交数或 complete 数量宣布完成；通用性以冻结留出集为准，见
  [审计共识](2026-10-12-audit-consensus.md) §5。
- 全语料批跑、RSS 基准和全套测试仍按用户要求暂停；固定 65 项和定向验证可以运行。

## Round 76：静态初始化器 self-binding（证据阶段停止，待 cc 审查）

- 基线 `a19178ed`。仅新增[证据记录](round-76-static-self.md)，没有修改生产代码或测试，
  没有放宽 guard、capture、静态初始化时序、命名或准入；未 commit/push/stash。
- 实际 ABC 顺序与任务中的待核实假设不同：o0/4660 creator m4850 为
  class13 → inner self-store28 → initializer m5040 创建40/调用51；o20/8166 creator
  m8392 为 class39 → inner self-store54 → m8556 创建250/调用261。外层发布均在之后。
  FindClassSelfStores 证明私有 parent 区间与支配；conditional 读取探针将 self Guarded
  义务重绑定为已初始化 local，均不需要 deferred。m8556 还捕获两个普通外层函数。
- 现有 ClassSelfOrder 仍要求空 captures 和 ClosedClassFields；两目标 conditional
  探针分别在 kOwner/kUnsupported 拒绝。initializer tree 用 IncomingCells::kReject，
  static-field destination 禁止 capture/self。现有 class naming 防遮蔽原绑定，不能将
  cell 重绑定为 native inner name。需要内层名字/外层 cell、重入、所有写入及静态
  初始化事件的跨层对应证明，按本轮明确停止规则未擅自实现。无新前提申请。
- 两个首因不变：SemanticProofError::kInternalValue > InternalValueReason=6，
  m5040@12、m8556@13。完整构建通过；固定集串行 **65/65**；class/self/initializer/
  capture 定向 **14/15**，仅已登记的 unguarded_captured_receiver_source_test 失败；
  CLI/corpus/failure/receipt/manifest 扫描 **72/73**，仅已登记的 family_source_cli_test。
- 重跑两组探索对 explore75/explore75s：旧 flags **521/620**、source **567/620**；
  **0 manifest 变化、0 状态变化、0 完整或不完整源码/扩展名漂移**。无新 complete，
  新输出 Node 检查不适用。没有实施新投影，未把临时 probe 当作 Node 差分或规模验收。
- 原始指令、probe、构建/测试日志和逐记录比较在 `/private/tmp/abcd-round76/`。
  未跑全套测试、留出集或 RSS 基准；独立只读复核支持停止结论，仍待 cc 审查。

## Round 79 A3f：外部异常不携带 Hole 的前提（V4，cc 审查通过）

- 用户 2026-10-16 批准独立的运行时前提“外部异常不携带内部 Hole”（Round 79 A3e 已从 pinned runtime 证明 callee 的 `ldhole; throw` 可以跨帧送达 catch）。新增 `kLanguageSourceV4` 与 `ExternalExceptionNoHole()`，`--language-source` 选择 V4；只适用于调用/getter/builtin/native/rejection/resume 来源，本方法自身的显式 throw 仍逐项证明。`catch_payload_domain` 计算逐 catch 的 payload 域事实，只凭该事实细化 close 哨兵的 Hole 分支。m423 Break/Return 的优先级在 V4 下 verified，在默认与 V1–V3 下仍为 missing；ownership 仍为 missing。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变（`--language-source` 的契约标签变为 V4）；扫描 79/80，只有既有的 family_source_cli_test 失败。

## Round 79 A3d：不抛出的比较与哨兵路径（cc 审查通过）

- 路径限定的 `NonThrowingComparison`：由 SSA 值流证明操作数为 undefined/null 时，EQ/STRICTEQ/NOTEQ/STRICTNOTEQ 不抛出（依据 pinned 解释器 FastEqual），裁剪其异常边；Hole 判断是保存 close error 的哨兵，正常路径两侧均为 Hole 时裁剪虚假分支。m423 Break/Return 的剩余见证后移到 PC139/230：Catch payload 是否可能为 Hole 尚无证明（SetException 不排除 Hole），未引入假定。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试全部通过。

## Round 79 A3c：按关闭结果索引的 completion（cc 审查通过）

- `close_outcome_relation`：传播状态同时保存入口、SSA 环境、completion 与 outcome（成功对象、缺失/nullish、getter 抛出、call 抛出、primitive 结果），getter/call 异常保留各自原始出口身份；builder 与 verifier 用相反工作列表顺序独立计算；去掉 outcome 后与 A2 may 关系完全一致。m423 Break/Return 入口五类 outcome 全部出现，优先级仍为 missing：nullish 比较（PC114/205）保留保守的异常边（`kOtherException`），Hole sentinel 路径也保留。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 5/5。

## Round 79 A3b（部分）：close 摘要与 return 引用（cc 审查通过）

- `close_completion`：重新派生入口，记录原始成员与边，核对 return 引用（base/callee/receiver）与 Throw payload；Throw 入口的优先级 verified（m423 6/6、m451 1/1）。缺口：completion_relation 不记录关闭结果（成功、getter 失败、call 失败、primitive 结果），相同的 Break/Return 出口元组无法区分正常成功与吞掉 close error，因此非 Throw 入口的优先级保持 missing；ownership、嵌套关联待续。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 4/4。

## Round 79 A3a：从原始代码推导 close 入口（cc 审查通过）

- `close_entries`：从 acquisition、cached-next、done 分支、私有 closed-state 与原始 continuation 推导 close 入口及其 PendingCompletion（Break 目标来自真实 exhaustion 边，Return payload 为原始 SSA，Throw 为每条入边的当前异常），再送入 A2，API 不接受调用方给定的 completion；独立 verifier 以反向支配查询发现 guard。真实 fixture：m423 Break/Return/Throw、m451 Throw 的入口全部推导正确。仍不授予 ownership 与准入。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 6/6。

## Round 79 A2：相关联的 completion 关系（cc 审查通过）

- `completion_relation`：在原始 ExceptionFlow/SSA 与 RawEventGraph 上传播完整的 PendingCompletion{kind,payload,target} 元组（不拆成独立集合），处理正常 Commit、异常路径、Catch、同时 phi 与嵌套的当前 completion；预付持久化 SSA 环境，预算耗尽时拒绝整个查询；独立 verifier 重算。真实 fixture：Break close 7 个出口元组、Return close 7 个、Throw close 2 个。保守的 may 关系；尚未接入生产，不授予拓扑所有权。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 5/5。

## Round 79 A1：共享原始事件图（cc 审查通过）

- for-of 轨道恢复（Round 79 设计：PendingCompletion{kind,payload,target} 相关追踪，五个门禁增量 A/B/C/II/III，最终上界 6）。增量 A 一次过大，拆片；A1 只做纯重构：把 await_catch_flow 私有的原始事件图（节点、寄存器效果、真实 dispatch、后继、预付 arena）提取为 `raw_event_graph`，接口不依赖 AwaitRegion，coroutine 边作为可选扩展。新增 `raw_event_graph_test`。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变，0 状态变化，0 文本变化；await/raw_event 测试 3/3。

## Round 78：await catch 输入的原始 CFG 活跃性证明（cc 审查通过）

- await_catch_flow：原始 Before/Commit/Catch/Resume/CallerExit 事件图、预付稀疏闭包、独立验证器；只有正常 Commit 能覆盖旧定义；读优先于写；资源耗尽返回拒绝；只补足快速证明的 kUnprovedPaths。o32 越过原拒绝，转为 effectful close 的 kMultipleExits（for-of 暂停轨道）。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 524 → **526/620**，`--language-source` 570 → **572/620**（o9 RdbDataPersistence、o39 WebSaveImage）；0 回退，0 文本漂移；扫描 75/76，只有既有的 family_source_cli_test 失败。

## Round 77 增量 3：captured default 的 recovery 与发布（cc 审查通过）

- recovery 加入实参入口 → undefined 分支 → provided/missing 提交 → initialized-parameter 定义；只有 missing 路径执行签名表达式，不伪造 ExprId；parameter_recovery 独立重建。kCellParameterRecovery 门禁只对通过验证的方法解除。DevEco es2abc 往返核对 length 2、条件初始化、外层与局部 cell 分离、闭包目标与 HoleGuard。
- cc 独立审查（增量 3）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521 → **524/620**，`--language-source` 567 → **570/620**（o10 Index、o31 FlowItemNode、o31 SwiperView；签名形如 `(elmtId, isInitialRender, data = __data__) => {...}`）；0 回退，0 文本漂移；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 77 增量 2：captured default 的签名与家族贯通（cc 审查通过）

- 联合残余体投影（签名拥有迁移的 read/guard/写回与异常出口，`InitializedParameterCommit` 记录残余初始 store 消费的形参）；SignatureCell 按原始 read site 查找外层存储；签名读取携带 Guarded 义务并入既有格；DefaultSignatureVerify 独立复核。首因前移为 `DefaultSignatureError::kCellParameterRecovery`；recovery 的条件参数事件链留给增量 3。
- cc 独立审查（增量 2）：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 77 增量 1：CellParameterDefaultProof（cc 审查通过）

- 密封的联合证明：按参数/cell 数据流发现，独立 verifier 重建索引，核对两条参数选择路径、两个不同存储、guarded 外层读取与异常出口、私有初始化区、body 入口状态（参数测试必须支配 body 汇合点）。3 个目标均 verified，发布不变（仍为 kLength），证明状态作为诊断发布。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 75：Evaluation 输入失败证据（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `4e3e9be9`；保留 HoleProtocolError / CellEvaluationError / LiteralError 和
  missing-shape 叶子及固定大小的 check/op/use/value 证据，builder、verifier、两条 JSON
  路径和 `abc_semantic --record-source` 贯通。原枚举、资源分类、准入和预算不变。
- o8/42040 m42490、o28/41167 m41622、source 下 o15/20252 m20697 均为
  `LiteralError::kDefinition > kLiteralStagedStorage`，allocation op17，原始/工作 PC47。
  跨选择的数组初始化需要保留 indexed [[Define]] 的事件及树/恢复对应；现有暂存路径
  只证明 buffered object 的已有属性赋值，不是消费登记的一处局部遗漏。
- o24/37520 m38333 为 `LiteralError::kFlow > kLiteralStagedAcyclic`：当前检查整个 CFG
  无环，故只有方法位置，没有虚构 PC。需要构造区间的 activation/escape/order 证明。
  按本轮停止条件未实施准入修复。设计与逐目标证据见
  [literal-initialization.md](literal-initialization.md#round-75-evaluation-input-refusal-evidence-2026-10-15)。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增与直接相关 **5/5**。
  evaluation/literal/cell/hole/tree-failure/source-route/logical-coverage 定向 **47/54**，
  失败仅已登记的七项 literal 测试，未弱化断言。新测试覆盖两条 JSON、真实 CLI、
  PC0/缺失/规范化位置、资源分类、伪造证明、copy/register/block 变体、200/201/400/800
  和精确 byte/work 边界。
- CLI/corpus/failure/receipt/manifest 扫描最终 **72/73**，仅已登记的
  family_source_cli_test 失败。首轮另发现 guarded_captures 的 branch 旧期望缺少新增的
  `EvaluationShapeError::kMissingShape`；补全精确原因链后重跑整个扫描通过该项，
  拒绝状态、禁止发布源码和 TDZ 变异断言均保留。
- 对 explore74c/explore74cs：旧 flags **521/620**、source **567/620**；**0 状态变化、
  0 完整或不完整源码/扩展名漂移**。manifest 只有上述目标的 3/4 条原因证据变化；
  o15 旧 flags 仍为 ModuleEnclosureError=10。无新 complete，新增输出的 Node 检查不适用。
- 日志与逐记录比较：`/private/tmp/abcd-round75/`。未运行全套测试、留出集或 RSS 基准；
  未 commit/push/stash。没有新增源语言前提。

## Round 74 增量 III/IV：Sendable 模块域与 shared 发布（cc 审查通过）
- cc 独立审查（增量 III/IV）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 566 → **567/620**（LruCache.ets：首行 `"use shared"`、经真实 ArkTSUtils import 拼写的 GENERIC 字段类型、构造器首条 `"use sendable"`、async 成员内闭包捕获自身的 this/key/value）；0 回退，0 文本漂移；扫描 76/77，只有既有的 family_source_cli_test 失败。

- 基线 `646fb819`；工作区实现，未 commit/push/stash。III 和 IV 均已接入。
- raw/Value/树保留独立 `ModuleDomain`；imm8/wide Sendable external read 保持可抛出、
  正常 acc 提交和 hole guard，没有伪造 initialized。创建上下文、guard、enclosure 与
  recovery 复验域及 import 身份。成员内普通箭头保留其自身捕获环境，外层根捕获仍拒绝。
- `SendableTypeSpelling` 从真实 import binding 与原构造器 [[Set]]/new 路径导出 GENERIC32
  的可重编译拼写（含直接 import 的零深度路径），独立重算；没有新增 import/初始化，
  不声称恢复唯一原始名义类型。未知或歧义拼写仍 `kTypeSpelling`。
- `SharedModuleProof` 独立证明 shared kind/模块表，未冒充 OrdinarySynchronous；完整
  入口恢复仍检查 class/construct/put/export 效果。模块首行 directive 来自该证明；部分
  shared 输出不能发布。新记录 `.ets`/arkts，所有既有输出扩展名不变。
- 最终完整构建成功；固定集串行 **65/65**。新增三项测试 **3/3**，覆盖真实 raw/SSA/tree
  模块域、guard/异常与提交、类型/绑定/owner 变异、shared 导出身份、真实编译往返及
  Node 普通调用差分、200/201/400/800 与精确 byte/work 边界。
- 定向正则 `module|sendable|shared|async|capture|concurrent|source_route|class_tree|recovery`：
  **105/113**；失败仅已登记的 class_tree_source_test、六项 captured_receiver 变体和
  async_semantics_test。`cli|corpus|failure|_receipt|manifest` 扫描 **71/72**，仅已登记的
  family_source_cli_test 失败；更新 LruCache 旧期望后的 tree_failure_cli_test 通过。
- 对 explore74b/explore74bs：旧 flags **521/620**，source **566→567/620**；唯一 manifest
  变化是 o14/24892 LruCache complete，**0 回退、0 既有源码/扩展名/位置文件漂移**。
- 实际 DevEco es2abc `--module --merge-abc --extension=ts --opt-level=0` 往返匹配：
  shared=true、非 CJS/TLA、request/import/export、Sendable class/length0、lock_/32、books_/32、
  instanceCount2、三 async 成员/回调、无额外 sendable env、两次 guarded Sendable slot0 read、
  两次 [[Set]]/四次 put/导出 slot0 顺序及效果事件的 handler 分派。初始化捕获的冗余
  HoleGuard 原始数量不要求相等；其可用性仍由 cell/recovery 证明。Node 不验证共享堆、
  字段类型检查、真实锁与跨线程行为。
- 仍保留静态初始化器、继承、未证明 GENERIC、Sendable local/lazy load、普通内层箭头直接
  读取 Sendable import 的继承域等门禁。ModuleCapturePrefix 没有放宽。
- 更新过期测试：sendable_metadata 的 V3 policy/目标结果，以及 tree_failure 中 LruCache
  的 source 契约 complete 结果；旧契约拒绝继续严格断言。没有修改既有失败测试。
- 日志、逐 record 比较与目标往返报告：`/private/tmp/abcd-round74iii/`。未运行全套测试、
  全语料、留出集或 RSS 基准。独立只读生产审查未发现阻断，不能替代 cc 最终审查。

## Round 74 增量 II：闭合 Sendable 实例类（cc 审查通过）
- cc 独立审查（增量 II）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 565 → **566/620**（Sendable.ets：`taskId: number;` + 构造器首条 `"use sendable"` + 两次原始 [[Set]]）；0 回退，0 文本漂移；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `201e667f`。未 commit/push/stash。V3 的 `SendableOutput()` 现在为
  `kArkDirectiveV1`，V1/V2/其他契约和 `SharedModuleOutput()` 保持原义。
  普通类路径复用原 ClassTree/family，创建种类贯通原始/值/树对应；构造器属性
  绑定实际创建点和池身份。字段为无 initializer 的 primitive/union 存储描述，
  原 [[Set]] 保留；只有实际新发布的 Sendable record 改用 `.ets` / arkts。
- 文件级 Sendable literal 索引按实体共享，创建者复用既有不可变输入，不逐 creation
  重解码整方法；字段 key 实体排序去重，所有缓存、共享控制块和字符串先计费。
  字段解码的 mask/split/key 拒绝缓存为所属类的类型化失败；资源/work 仍归索引预算。
  无可用 TS 模块名拼写返回记录级 kBinding，不伪造指令 PC。静态初始化器、外部
  capture/module 环境、heritage、未恢复成员/前缀、NONE/TYPE_REF/GENERIC 继续门禁。
- 最终完整 `cmake --build build/regression -j 6 -- -k` 成功；新增两项及 Sendable
  相关共 **5/5**。覆盖真实生产准备/完成、错误 creator/pool/kind/mask/member/directive、
  部分恢复/环境/capture、malformed mask 的同文件隔离、模块拼写拒绝、63 种 primitive
  union、200/201/400/800 和精确 byte/work 边界、实际源码发射/重编译与 Node 普通调用。
- 最终固定集串行 **65/65**。期间一次 rich_text worker 未正常返回（约 67 秒），
  单独复跑 **1/1**（13.43 秒），随后完整固定集 **65/65**，未改代码/断言。
  相关定向 **28/30**，仅既有 `tree_structure_test` abort 和 `class_tree_source_test`
  匿名 class 正则期望；CLI/corpus/failure/receipt/manifest 扫查 **70/71**，仅既有
  `family_source_cli_test` owner/origin arena 失败，均未弱化。
- 最终对 explore74/explore74s：旧 flags **521/620** 不变，source **565→566/620**，
  仅 SendableTest（o14/28934）complete；**0 回退、0 既有文本/扩展名/位置文件漂移**。
  原合同仍在 m29066@9 报 SemanticError::kUnsupportedInstruction。
- 真实 `.ets` 按原 record 名用 DevEco `--module --merge-abc --extension=ts --opt-level=0`
  重编译：shared=false；definesendableclass；constructor serialized kind NONE、sendable=false、
  length=1、无 heritage；getTaskId；taskId/NUMBER(1)/instanceCount1；整 record 无额外
  sendable-env；两次有序 taskId [[Set]]；export slot0 与模块表匹配。编译器 hash、
  fixture 命令与 oracle 边界见 [fixture README](../../../tests/integration/fixtures/sendable_source/README.md)。
  Node 不验证共享堆、类型检查或跨线程行为；这些仍是 runtime gap。
- 本轮日志/比较/往返报告位于 `/private/tmp/abcd-round74ii/`，没有跑全套、全语料、
  RSS 基准或留出集。没有改动用户的 sample.tar.gz。

## Round 74 增量 I：Sendable/shared 元数据（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、565/620 不变，0 状态变化，0 文本变化；扫描 71/72，只有既有的 family_source_cli_test 失败。

- 基线 `544e0854`。本片只交付增量 I，**未开始增量 II**；没有 commit/push/stash。
  raw `ClassCreation` 与 `ValueClassCreation` 保留 ordinary/sendable kind；按实际创建种类
  选择 literal 格式，独立保留成员缓冲、字段直接引用、字段顺序、mask 和实例/静态分界。
  实例字段使用独立的 `SendableFieldPlacement::kInstance`，不会混作 prototype 成员。
- 原始 occurrence 的区域池、constructor/member owner、字段 key 都被复核；元数据验证器
  从不可变输入重算并返回类型化失败。shared 的 absent/false/true 属性与真实 owner/module
  表一起保留，仍不构成 `OrdinarySynchronous`。两个 V3 输出政策仍 disabled，语义和类准备
  门禁保持；原首因和资源分类不变。两条 JSON 路径、receipt 与 abc_semantic 增加固定大小证据。
- 完整 `cmake --build build/regression -j 6 -- -k` **成功**；新增三项及修改的原始类/value
  测试 **5/5**；固定集 `ctest --preset source-regression -j 1` **65/65**；定向 **24/25**，
  仅已登记的 `class_tree_source_test`（匿名 class 正则期望）；CLI/corpus/failure/receipt/manifest
  扫查 **69/70**，仅已登记的 `family_source_cli_test`。未弱化这两项断言。
- 对 explore73c/explore73cs：**521/620、565/620**，0 状态变化、0 既有源码或扩展名变化；
  已 complete 的结构产物也逐字节相同。每组只有 o14/28934、o14/24892 的 manifest 新增诊断，
  原有字段和首因不变。前者确认 fields29289=taskId/NUMBER1；后者 shared/module25407，
  独立 metadata probe 确认 fields25544=lock_/GENERIC32、books_/GENERIC32。
- 真实 DevEco fixtures 已编译并记录 compiler/fixture SHA-256；包含 metadata/owner/pool/field
  变异、JSON 所有原因类型、PC0/缺失 PC、200/201/400/800 和真实 byte/work 准入边界。
  这不是引擎输出 Node 差分或目标重编译：这些仍是增量 II 的未完成验收，不能当作通过。
- 证据：`/private/tmp/abcd-round74/{final-build.log,new-tests.log,acceptance.json,cohort-comparison.json}`。
  设计与边界见 [Sendable/shared](sendable-shared.md)。

## Round 73 增量 III：async concurrent（cc 审查通过）
- cc 独立审查（增量 III）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 564 → **565/620**（GetRecentList：`async function(key, value) { "use concurrent"; await lruCache.put(key, value); }`）；0 回退，0 文本漂移；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `b2d0be5e`，未 commit/push/stash。原始 typed AsyncEnter 选择 async owner；闭合后缀的反向值流证明 resolve/reject → notify → return，通知与返回均使用成功 settlement 的 Promise acc。复制、跳转及分块不依赖相邻模板；每条通知/返回都被唯一端口覆盖。复用原 async owner 证明状态、await/resume、隐式拒绝和用户 handler。不可变原始证据及共享控制块按实际分配预扣，TreeFlow/recovery 重放证明并核对原始/工作来源。
- o14/10719 GetRecentList 完整：m11357 updateBooks 输出 async function，`"use concurrent";` 为首条语句。实际输出直接用 DevEco es2abc 重编译，保持 concurrent kind、sendable=false、async 协议、存在的请求注解 [1]、完整模块表；成功和隐式拒绝各一条 Promise 通知端口。没有改写往返输入或插入假模块读取。
- 对 explore73b/explore73bs：**521/620、565/620**（source +1），0 回退、0 既有源码字节变化。旧 flags 的 manifest 无变化；source 只 o14/10719 改变。目标 CLI/manifest 源码一致，通过 node --check。
- 最终完整 keep-going 构建成功；固定集串行 **65/65**；新增 **2/2**，并发相关 **9/9**。async/concurrent/control-composition/recovery/tree-structure/source-route 定向 **26/28**，仅已登记的 tree_structure_test 和 async_semantics_test 失败；CLI/corpus/failure/receipt/manifest 扫描 **68/69**，仅已登记的 family_source_cli_test owner/origin arena 断言失败。没有弱化这三项断言。
- 真实六函数 fixture 对实际导出执行 Node 普通调用差分：解析值、拒绝类型、微任务轨迹、前后抛出、返回 Promise/thenable、catch-around-await；直接重编译复核方法属性/请求/模块表/两类完成端口。通知变异、200/201/400/800 规模及 bytes/work 边界通过。finally-around-await 夹具保留既有 `AsyncCompletionCheck::kCatchInput > AwaitCatchError` 拒绝，仅验证其原始通知端口，未声称 engine-output 差分通过。taskpool/跨线程仍未验证。
- o14/16446 未修复：m16784 的 PhasedMethodError::kEnvironment 不变；独立下层分析定位 EnvironmentFlowError::kAmbiguousJoin，原始 PC275 / 工作 PC272。H275 有来自不同环境身份的异常入口：PC59 分配后、PC243 pop / PC244 再分配的路径汇合，现有环境格不合并这些身份。
- 证据入口 `/private/tmp/abcd-r73-iii/acceptance.md`；详见 [language-source.md](language-source.md) 增量 III 与 `tests/integration/fixtures/concurrent_async/README.md`。无留出集、全语料、全套测试或 RSS 基准运行。

## Round 73 增量 II：同步 concurrent 完成投影（cc 审查通过）
- cc 独立审查（增量 II）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 561 → **564/620**（TaskpoolCancel、Mock、Worker）；0 回退，0 文本漂移；cli/corpus/failure/concurrent 扫描 71/72，只有既有的 family_source_cli_test 失败。cc 核对 TaskpoolCancel：`"use concurrent";` 是函数体首条语句，函数表达式形式保留。

- 基线 `7eee8402`；未 commit/push/stash。普通 Function 与 verified directive 分离；原始 notify 保留 acc 和调用帧 owner，有限格完成证明、专用事件端口及 TreeFlow/OriginalFlow/recovery 重算贯通。实际输出引用逐项核对请求注解；普通 outer cell（含转发）仍拒绝，ModuleCapturePrefix 的 concurrent 排除未改。
- 三个 record 完整：o14/18164（cancel/delayed）、25688（query）、31477（parallelTask）。collectFrame m16922 的通知证明通过，但 16446 首拒绝推进至另一方法 m16784 的 `PhasedMethodError::kEnvironment`（创建点 m16700@123；没有虚构子 PC）。10719 的 async m11357 明确保留 `kAsync` 门禁，原始/工作 PC10。
- pinned parser 不接受 quoted export aliases。仅实际发射 verified concurrent 的新 record 改用 IdentifierName；当前 formatter 需字符串拼写的 import/re-export/export 名返回 `kModuleSyntax`，Unicode 夹具验证这一覆盖边界。未改写往返编译输入，未改变无 concurrent 输出的格式。
- 最终完整构建成功；固定集串行 **65/65**；相关定向 **14/14**（并发 **7/7**）；CLI/corpus/failure/receipt/manifest 扫描 **67/68**，仅已登记的 `family_source_cli_test` owner/origin 断言失败，未弱化。新完整输出通过 Node syntax check。三个真实 record 的直接 es2abc 往返及六方法真实夹具通过 kind/sendable/注解/请求顺序/模块表与通知完成核对；Node 只验证普通调用值/异常类型/轨迹，不验证 taskpool/跨线程。
- 相对 explore73/explore73s，结果 **521/620、564/620**，0 回退、0 既有完整文本漂移。旧 flags 的 manifest 无变化；source 仅 o14 的 10719/16446/18164/25688/31477 五个候选变化。无留出集、全语料、RSS 基准或全套测试运行。
- 同步 increment 的边界、编译器身份/命令和测试 oracle 见 [language-source.md](language-source.md) 增量 II 及 `tests/integration/fixtures/concurrent_functions/README.md`。日志、逐字段比较、目标表和完整改动清单在 `/private/tmp/abcd-r73-ii/`；入口 `acceptance.md`。async、concurrent 内 class-body 引用普查，以及一个通知对应多个物理 Return 的端口仍严格拒绝。

## Round 73 增量 I：V3 输出政策与 concurrent 元数据证据（cc 审查通过）

- 用户 2026-10-15 批准保留语义的 ArkTS 形式（concurrent/Sendable/共享模块），apply 展开暂不批准。新增 `kLanguageSourceV3` 与三个独立谓词，只有 ConcurrentOutput 启用。方法级并发模块请求注解解析，区分缺失与空注解；目标细化为 `ConcurrentError::kPreparationGate`。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、561/620 不变，0 状态变化，0 文本变化（`--language-source` 下契约标签由 V2 变为 V3）；cli/corpus/failure/concurrent 扫描 69/70，只有既有的 family_source_cli_test 失败。

## Round 71：直接 SSA 字符串默认参数（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 520 → **521/620**，`--language-source` 560 → **561/620**（Logger：`constructor(prefix = "", domain = 65280)`）；0 回退，0 文本漂移；cli/corpus/failure 扫描 66/67，只有既有的 family_source_cli_test 失败。

- 基线 `8d8febf9`。直接默认值候选保留原始字符串 pool 身份；独立证明复核常量、结果/phi 使用和参数顺序，再经已有 `SignatureString`、原方法常量池和签名验证贯通。`.length` 等式、参数环境边界、打印器及捕获默认值准入未改。见 [实现和边界](family-defaults.md)。
- 对 `explore69/explore69s`：**520 → 521/620、560 → 561/620**。每组只有 o13/3609 Logger 变化，构造器为 `constructor(prefix = "", domain = 65280)`，从 `FunctionAbiError::kLength` 到 complete；两份新输出均通过 `node --check`。0 回退，0 已 complete/partial 文本漂移，其他 record 的 manifest 条目不变。证据 `/private/tmp/r71/cohort.log`、`/private/tmp/r71/comparison/`。
- 完整构建通过；新增 **2/2**；固定集串行 **65/65**；默认参数/签名/常量池定向 **18/18**；CLI/corpus/failure/receipt/manifest 扫描 **64/65**，仅已登记的 `family_source_cli_test`（owner/origin arena）。未改既有测试期望。命令及日志索引 `/private/tmp/r71/acceptance.json`。
- IR/签名单测覆盖错误 pool、数值伪装为字符串、错误参数/顺序/生产者、错误解析文本、漏默认值及 length 不等；200/201/400/800 检查线性工作量及 byte/work 精确边界。真实 DevEco/es2abc fixture 两契约 **88 次 Node 差分、8 个被检出的变异**，覆盖函数/构造器/箭头、连续默认值、required 前缀、missing/undefined、Unicode 和显式对象转换的顺序/抛出。fixture 编译命令及哈希随 README 保存。
- Round 70 的三个 cell-backed 捕获默认值保持拒绝；无 SSA 合流的已消除字符串默认值、一般效果表达式默认值不在本增量。没有新语言前提。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 69：async 归一化诊断（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；520/620、560/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 63/64，只有既有的 family_source_cli_test 失败。

- 基线 `40f0d0c1`。新增固定大小 `AsyncCompletionFailure` / `AsyncCompletionCheck`，保留既有错误枚举和资源分类；原始/工作 PC 分开，真实 PC0 与缺失位置分开。下层 AwaitRegion/AwaitCatch 原因、handler/register 与路径未证明证据进入 receipt、manifest 两条 JSON 路径和 `abc_semantic`。见 [四目标、根因和停止边界](round-69-async-normalization.md)。
- 四例拆成 3 个 catch-input 未证明路径（m7950@3459 H3537/v25、m23306@99 H131/v13、m22136@75 H348/v11），以及 1 个 promise 创建前闭包实例化（m58418@18）。前三例不等于已证明实际泄漏；需要原始异常 CFG 上的输入定义/提交证明。后一例需要 private instantiation/escape 证明，不能扩展 prelude 操作白名单。按本轮停止条件仅交付步骤 1；没有新 complete，没有 Node 语义修复声明。


- 最终完整构建通过；固定集串行 **65/65**；新增及直接相关 **4/4**；`async|await|coroutine|control_composition|source_route|tree_failure|family_creation_failure` 定向 **25/26**（仅已登记的 async_semantics_test，bad_variant_access）；`cli|corpus|failure|_receipt|manifest` 扫描 **63/64**（仅已登记的 family_source_cli_test，owner/origin arena）。没有更新或弱化既有测试期望。命令、返回码和完整日志见 `/private/tmp/r69/acceptance.json`。
- 对 explore68/explore68s 的最终逐 record 比较：**520/620、560/620** 不变，0 状态变化，0 complete/partial 文本漂移（含 o32/49868）。每组仅四目标的诊断条目变化，其余 record 条目相同。`/private/tmp/r69/final-cohort.log` 和 `/private/tmp/r69/final-cohort/` 保留证据。没有新输出需要 node --check。
- 新测试覆盖实际 PC0/无位置预算失败、try/handler/branch 原始与工作坐标、两条 JSON 路径、所有 36 个 check 的 envelope round-trip、真实四 ABC × 两契约 CLI 一致性。200/201/400/800 的精确 byte 边界为 29896/30044/59496/118696，低一字节保持资源拒绝。只读复核发现的缺失位置误报 PC0 已修正并测试。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 68：正常 class-creation 路径的 Guarded activation（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 516 → **520/620**，`--language-source` 556 → **560/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 62/63，只有既有的 family_source_cli_test 失败。

- 基线 `95ef0813`。新增 Guarded-only 原始支配证据：allocation 支配 creation 和唯一 direct store；Language 写入前提、原作用域/激活、初始化 store 来源仍独立验证。工作 IR 的 Mark/VerifyDeferredCells 共享一次预算内支配计算，FamilyTree 持有 deferred 表的真实 charge。未放宽 SourceAccess/Language/Defined、未知闭包写入或 exceptional-parent deferral；未改声明/控制布局或打印。逐例证据与算法边界见 [Round 68](round-68-capture-activation.md)。
- explore67c/explore67cs 对比：**516 → 520/620、556 → 560/620**；每组恰好四个目标变化，o3/55292、o10/52872、o29/12767、o21/12185 全部 complete。0 状态回退，0 已 complete/partial 文本漂移（含 o32/49868），8 份新输出通过 `node --check`。证据 `/private/tmp/r68/cohort/`、`/private/tmp/r68/cohort.log`。
- 最终完整 `cmake --build build/regression -j 6 -- -k` 通过；固定集串行 **65/65**；新增/直接相关 **6/6**；`capture|deferred|guarded|class|local_initialization|cyclic_cells|scope` 定向 **59/72**（13 项均在既有失败清单）；`cli|corpus|failure|_receipt|manifest` 串行扫描 **61/62**（仅既有 family_source_cli_test）。完整日志 `/private/tmp/r68/acceptance.json`，最终补充边界与新测试 `/private/tmp/r68/boundary-tests.log`、`/private/tmp/r68/final-new.log`，最终构建 `/private/tmp/r68/final-build.log`。
- 新单测覆盖原始/工作 CFG 的非支配 allocation、reset、多 store、伪造 store、严格需求拒绝、copy/拆块、200/201/400/800 和原始局部证明/捕获计划/Mark/Verify 的精确 byte/work 边界。真实 es2abc fixture 两契约 **24 次 Node 差分、8 个变异**；分别检出提前声明丢 TDZ，以及保留早读 ReferenceError 但错误共享循环后续值。消息/stack 不作跨 VM oracle。
- 唯一过期期望更新：guarded_captures 的 branch 仍拒绝且无 source，从 ScopeError=3 推进为完整 `EvaluationError=2 > EvaluationObligation=0` 链；其余负例断言未弱化。独立只读代码复核未发现阻断，补齐了工作支配与预算/activation 反例。不支持的循环环境复制 join 仍按原类型化原因拒绝；无新语言前提。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67：闭合普通后代类生产贯通（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 513 → **516/620**，`--language-source` 552 → **556/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 63/64，只有既有的 family_source_cli_test 失败。输出形如 `(class SocketInfo {...}).prototype;`：es2abc 定义类后会读取 prototype，而类值在该函数中未被使用，所以这是对字节码的忠实投影；可读性另议。

- 基线 `640dd19f`。本次窄切片接通无 creator-cell 需求、无 heritage、无静态初始化器的普通后代类；复用 record-root 的 CreatorClasses / PreparedFamily，真实 occurrence、Storage、CaptureTree、creation OpId 贯通准备与完成。冻结前收集需求；非空/延迟需求保留独立类型化拒绝，不移动声明。完整 owner 范围、嵌套类自由名预留、完成后实际 owner 覆盖、真实 creator 的 FamilyWrites 屏障均独立复验。见 [实现、边界和逐目标表](round-67-closed-descendants.md)。
- 对 explore67b/explore67bs：原 flags **513 → 516/620**，source 契约 **552 → 556/620**；**0 状态回退、0 已 complete/partial 文本漂移**，包括 o32/49868。两组 manifest record 条目仅四个 o18 目标变化。52728、59564、60829 两组均 complete；63861 仅 source 契约 complete，原 flags 的下一叶为 m64229/PC62 的 `CaptureCellError=3 > CaptureError=1 > ScopeError=3`（ordinal2、depth0/slot7）。7 份新增 complete 输出均通过 `node --check`。证据 `/private/tmp/r67c-delivery-cohort/` 和 `/private/tmp/r67c-delivery-cohort.log`。
- 最终完整 keep-going 构建通过；固定集串行 **65/65**；新增/扩展 **8/8**；`class|capture|deferred|guarded|family|creation|recovery` 定向 **72/86**（14 项均为下方既有失败，未改断言）；`cli|corpus|failure|_receipt|manifest` 扫描 **60/61**（仅既有 family_source_cli_test）。验收命令及日志 `/private/tmp/r67c-delivery-acceptance.json`。真实 es2abc fixture 的两契约 Node 差分覆盖 construction、[[Set]]/父 setter、throw、函数/方法 creator、多次 loop 创建、copy/branch 变体和自由全局 `$value0` 与 creator 临时名冲突；[[Define]]/删除 throw 变异检出。CLI/manifest 的 capture/deferred/heritage/nested 拒绝原因与位置一致。
- 200/201/400/800 宽度与普通函数深度的生产发现/准备及精确 byte/work 边界通过。独立整 record 探针：width200/201/400 complete，width800 仍达既有 FamilyEnclosureError::kResourceLimit；depth800 导出 complete，但生成的嵌套表达式耗尽 Node 默认 parser stack，`node --stack_size=8192 --check` 通过（原始函数声明 fixture 默认 stack 可通过）。不把准备阶段证明当作无限宽度/深度的端到端保证，未提高任何 cap。
- 保留范围：capturing/deferred/heritage/static initializer、requirements-only 类成员继续创建类、phased creator；更深层类任务图和声明 fulfilment 尚待后续。无新源语言前提。只读复核未发现阻断；补齐其指出的全局名冲突测试。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67 Increment II 基础 WIP（cc 审查通过；生产贯通待续）
- cc 独立审查（增量 II 部分）：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 60/61，只有既有的 family_source_cli_test 失败。

- 基线 `97ca420a`。新增 requirements-only 类成员准备状态及禁止 Finish 的类型化门禁；按 occurrence 汇总类成员家族需求，交给既有 bottom-up 格处理；CellPlan 支持在真实 ClassCreation 环境解析需求；实际 creator Storage 贯通类根绑定、独立复核及 ClassRootWriteInput。纯转发 creator 的真实 es2abc 单测证明：缺少 demanded storage 拒绝，补齐后绑定成功。见 [实现边界和剩余依赖](round-67-class-requirements.md)。
- **未完成 II**：尚无统一的嵌套类准备任务图；生产入口仍保留 Increment I 的 gate。未接通冻结前声明决策、嵌套类全 owner 范围与 ReferenceNames、真实 enclosure 下的完成及 FamilyWrites 非根屏障验证；III 未启动。没有新增 complete，没有 Node 端到端声明。
- 完整构建通过；固定集串行 **65/65**；新/扩展测试 **5/5**；定向 **65/79**（14 项均为既有失败）；CLI/corpus/failure/receipt/manifest 扫描 **58/59**（仅既有 family_source_cli_test）。新增测试含两契约下 18 个真实 ABC 案例、200/201/400/800 宽度和成员内转发深度、需求/规划/绑定的精确 byte/work 边界及错 occurrence/creator/op/storage 反例。
- 对 explore67/explore67s：**513/620、552/620** 不变，80 份 manifest 逐字节相同，已有 complete/partial 源码逐字节相同（含 o32/49868）。四目标均仍为 FamilyCreationCheck=14。证据 `/private/tmp/r67ii-final-cohort/`，验收日志 `/private/tmp/r67ii-acceptance.json`。未运行留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67 Increment I：普通后代类准备发现（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 59/60，只有既有的 family_source_cli_test 失败。

- 基线 `220cd6da`。从 record_tree 提取共用的布局发现、成员准备与完成协调；保持 record-root 顺序、命名和 owner 分配。普通后代在现有显式 family DFS 中保留真实类义务，按 occurrence/creator/pool/revision 独立验证后返回 `kClassPreparationGate`，不发布类体。后代完整 owner 范围仍须等成员家族计数；不以直接成员数伪造范围。见 [设计与验证记录](round-67-creator-classes.md)。
- 对 explore66/explore66s：**513/620、552/620** 不变，0 状态变化、0 complete/partial 文本漂移，含 o32/49868。完整 manifest 只有四个 o18 目标各两组 flags 的 `source_failure` 改为 gate 14；其余字段不变。证据 `/private/tmp/r67-final-cohort/`。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增/扩展 **4/4**；class/capture/deferred/guarded/family/creation/recovery 定向 **65/79**（14 项均为下方既有失败）；CLI/corpus/failure/receipt/manifest 扫描 **58/59**，仅既有 family_source_cli_test。真实 es2abc 200/201/400/800 宽度/深度、精确 byte/work 边界、错 pool/creator、重复或遗漏 occurrence、owner 冲突变异通过。未运行留出集、全套测试、RSS 基准；未 commit/push/stash。
- Increment II/III 尚未实施：类成员家族需求汇总、真实 creator storage/声明 fulfilment、ReferenceNames、enclosure 与 FamilyWrites 屏障贯通；phased creator 不变。没有新增源语言前提，也不声称后代类已可执行或通过 Node 差分。

## Round 66：FamilyFile 创建拒绝定位（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 58/59，只有既有的 family_source_cli_test 失败。

- 基线 `bd4029c7`。五例在两组 flags 下相同：o18 四例为普通后代函数内的 ClassCreation 进入函数专用 BindCreation，实际叶子为 `FamilyFileError=1 > FamilyCreationCheck=9 (FunctionBinding) > CreationError=0`；o36/19453 为 phased 家族根的 ClassCreation 不在函数创建索引中，叶子为 `FamilyFileError=1 > FamilyCreationCheck=8 (PhasedSite)`。m60156 原始 CFG 的 PC128→174、PC192→229 跳转绕过先前 return，PC232 的类创建可达。逐例表与停止依据见 [round-66-family-creations.md](round-66-family-creations.md)。
- 所有 13 个 kCreation 检查有固定大小的类型化证据：实际创建 method / Op / instruction / 原始与工作 PC，家族 root / occurrence，方法作用域内 constructor / literal pool 身份，以及后代的独立 incoming creation 来源。普通 BindCreation 的真实底层错误不再折叠。route/manifest、record-entry JSON 和 abc_semantic 共用证据；失败枚举值和资源分类不变。无新生产索引、无新缓冲、失败时 O(1) 读取已有事实。
- 按本轮停止条件只完成步骤 1：现有 ClassTree 准备/组合及 FamilyWrites 验证只接入 normal record 根，后代类需要跨家族准备、捕获/声明与类屏障对应；phased 类还需异常相位对应。没有删除类、忽略创建或放宽局部准入。
- 对 explore65/explore65s：**513/620、552/620** 均不变，所有 record 状态不变，已有 complete/partial 文本逐字节不变；每组恰好五个目标仅 `source_failure` 字段变化。输出 `/private/tmp/r66-cohort/`，构建、CFG、CLI、测试与比较日志 `/private/tmp/r66-evidence/`。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增 **2/2**；family/creation/class_tree/class_omissions/source_route/tree_failure 定向 **26/28**，仅既有 `family_source_cli_test`、`class_tree_source_test` 失败。CLI/corpus/failure/receipt/manifest 扫查 **57/58**，仅既有 `family_source_cli_test` 失败。`class_deferred_cli_test` 的 loop 反例仍严格拒绝；将折叠叶子期望改为实际 `FamilyFileError=1 > FamilyCreationCheck=8`，并加强创建/后代来源断言，原 TDZ/Node/变异断言未改。新单测包含 13 种检查、位置/错误枚举变异、两条 JSON 通路、资源分类和 200/201/400/800 固定大小诊断往返；未声称新增恢复算法的精确预算或 Node 语义差分。未运行留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 65：成员调用参数中的保留值 fallback（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 510 → **513/620**，`--language-source` 549 → **552/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 57/58，只有既有的 family_source_cli_test 失败。cc 核对 ComputedProperty 输出为装饰器降级的标准形态 `Object.defineProperty(proto, "sum", decorate(...) || descriptor)`。

- 基线 `3cb1e433`。o21 四例均为 normal：callee 在 B5、调用在 B8；子调用已是 predicate snapshot，phi 在真分支保留该结果、假分支读取此前保存的 fallback。raw logical region 已发现，但 logical 路线要求 arm 内 `kPhiInput`，conditional 路线又让给 logical owner，形成发现缺口。不是 Round 46 暂缓的联合 consumer 准入。逐方法 Op/PC/phi 证据见 [round-65-retained-call-selections.md](round-65-retained-call-selections.md)。
- 只为未满足成员 Reference 的实参树保留现有 conditional/snapshot 证明，复用反向区间构造、正向验证及 TreeFlow/OriginalFlow/recovery。未改 consumer eligibility、catch、声明、命名或打印；无新前提。两个新索引预先计费，线性构建，总复杂度不超过既有 O(N log N)。
- 两套 cohort 对 explore64/explore64s：**510 → 513/620、549 → 552/620**，0 回退、0 已 complete 文本漂移。每套仅四个目标变化：15203、18179、23526 complete；16320 的入口问题消除，转到 m17082@171 的 CallReferenceError::kNotInline（call Op70/property Op8，Root/CrossRegionValue）。六份新增 complete 输出均通过 `node --check`。
- 完整 keep-going 构建成功；固定集串行 **65/65**；新增 **2/2**（104 次 Node 差分、12 个变异检出）；`retained_call_selection|call|reference|logical|selection|evaluation|recovery|original_flow` 定向 **68/69**，仅既有 phased_arm_selection_source_test；`cli|corpus|failure|_receipt|manifest` 扫查 **55/56**，仅既有 family_source_cli_test。没有修改既有失败或过期期望。
- IR 测试含 consumer/phi/树事件变异、copy/寄存器/直线前缀拆块、200/201/400/800 精确 work/byte 边界和独立 tree/recovery 验证。两个兄弟选择、join 到 consumer 的跨块限制仍开放，不把它们称为语言约束。证据在 `/private/tmp/r65-evidence/`；双 cohort 输出在 `/private/tmp/r65-first-cohort/`。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 64：内部值诊断与循环 cell 初始化（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 509 → **510/620**，`--language-source` 547 → **549/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 55/56，只有既有的 family_source_cli_test 失败。`async_semantics_test` 在会话起点 22e7423a 上同样 rc 134，cc 将其补入既有失败清单。

- 基线 `3a8b010e`。六例拆为 3 个循环局部 cell 初始化门禁、2 个静态初始化器 self-capture 未获语言值证明、1 个无 guard 外部模块读取。新增固定大小的 `InternalValueEvidence`，保留真实 SSA producer / observer / read ordinal 或 cell / allocation / store；未知值域不误报成确定 Hole。两条 JSON 路径、route receipt、CLI 和 omitted members 均贯通。
- 只修最大局部组：对原先未能闭合的循环读取，用 allocation → store → read 的支配证据证明本次激活已初始化；所有写入仍需 Language 验证，未知闭包写入保持严格。工作 IR 的独立 cell 初始化验证器重算对应关系。未改 guard、catch、控制布局或声明放置规则，无新源语言前提。
- 双 cohort 对 explore63/explore63s：**509 → 510/620**、**547 → 549/620**，0 状态回退，0 原已 complete 文本漂移。CustomAnimationTab 两套 flags 从 partial 到 complete；OperationNodeTree 在 source flags 下 complete。assertDeepEquals 进至 m40502 @409 的 for-of 多出口 close-region 拒绝。另 o24/37520 的首拒绝前移到 m38333 的 UnsupportedOperation，状态不变。六例表和 partial 类补全的文本差异说明见 [round-64-internal-values.md](round-64-internal-values.md)。
- 完整构建通过；固定集串行 **65/65**；新增及诊断通路 **6/6**（Node 100 次差分、6 个变异）；定向 **64/78**。13 项在既有清单，另 async_semantics_test 已在干净 `3a8b010e` 临时副本重建复现相同 bad_variant_access/134，未改断言。CLI/corpus/failure/receipt/manifest 扫查 **54/55**，仅既有 family_source_cli_test 失败。新完成的三份 flags/record 输出均通过 node --check。未跑留出集、全套测试或 RSS 基准；未提交。

## Round 63：遗漏成员诊断与 phased ValueBranch 前缀（cc 审查通过）

- A：`source_omitted_members` 发布每个遗漏类成员的类型化首拒绝与位置（两个 partial class 的根因都是 `SemanticProofError::kInternalValue`）。B：3 个 phased Root 实为 Commit 上的 ValueBranch，已有 selection 证明；把向 phi 临时变量的条件赋值按原顺序放进 decision RHS 的逗号表达式，TreeFlow 独立核对。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 506 → **509/620**，`--language-source` 544 → **547/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 57/58，只有既有的 family_source_cli_test 失败。

## Round 62：DecisionPrefix（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 501 → **506/620**，`--language-source` 539 → **544/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 53/54，只有既有的 family_source_cli_test 失败。cc 对照原源码核对 KeyboardController：`isShiftKeyHold` 与原文几乎一致；调用处 `!(A && B) || C` 与原 `A && B && !C` 按德摩根律等价，分支互换，短路求值顺序相同。

- 基线 `fc7b648f`；证据先行重数两套 explore61：各 **12** 个首拒绝，集合相同。`DecisionPrefixError` 保留实际失败检查，`prefix_event` 与控制位置分开，原始/工作 PC 来自 origin，Try 单元保留 synthetic 身份；贯通两条 JSON 路径及 abc_semantic。逐 record 表见 [round-62-decision-prefix.md](round-62-decision-prefix.md)。
- 证据分为 **8 个 normal 的已验证 LogicalValue 前缀、3 个 phased root 分支、1 个 Try 单元**。本轮只修第一组：树发射消费现有 selection 的原始 phi/arms/join，TreeFlow 独立重算并验证，选择的边不再作为空直线边重复消费。没有新识别模板、联合 predicate/keep-edge 准入、catch/控制/declaration 改动、新前提或 recovery 豁免；未改变 for-of 暂停边界。
- 最终双 cohort 对 explore61/explore61s：**501 → 506/620、539 → 544/620**；**0 状态回退、0 已完成文本漂移**。每组只变动这 12 个目标：5 complete、2 明确 INCOMPLETE class、1 后续 LoopExits、4 保持拒绝并增补证据。新增完成输出两套共 10 份全部通过 `node --check`。partial class 不算完成；目前该路径的 manifest 缺少被省略成员的具体叶子/位置，已如实记录。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；`decision|logical|selection|evaluation|tree_failure|recovery|original_flow` 定向 **59/61**，仅既有 `phased_arm_selection_source_test`、`decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 串行扫描 **51/52**，仅既有 `family_source_cli_test` 失败（owner/origin arena 断言）。没有改这些失败断言。
- 新增 `decision_selection_prefix_test`、`decision_selection_prefix_cli_test`，加 `tree_failure_test` 扩展均通过：copy/寄存器/拆块、statement/expression 两种前缀、错误 join/selection/arm/缺失与重复事件变异、recovery 的已验证输入与 revision 检查、200/201/400/800 精确 byte/work 边界。最小预算 **13,569,949 / 13,635,762 / 27,125,765 / 54,237,397 bytes**，work **4,820,569 / 4,844,794 / 9,744,722 / 19,697,078**。真实 es2abc fixture **1,344 次 Node 差分、10 个变异检出**，README 登记命令、sha256 与 oracle 边界。
- `evaluation_failure.mjs` 的两项过期期望改成真实结果：DocsSample KeyboardController 为 partial class，Solutions KeyboardController 为 complete，均比对 CLI/manifest 源码；`tree_failure.mjs` 新增真实 phased prefix 的精确拒绝位置检查。
- 日志：`/private/tmp/r62-full-build-final.log`、`r62-fixed.log`、`r62-targeted.log`、`r62-sweep.log`、`r62-final-cohort.log`、`r62-comparison.json`；最终导出 `/private/tmp/r62-final-cohort/explore62{,s}`。未 commit/push/stash，未运行留出集、全套测试或 RSS 基准。

## Round 61：LogicalCoverage（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 500 → **501/620**，`--language-source` 537 → **539/620**（assertClose、GridLayout）；0 回退，0 文本漂移；cli/corpus/failure 扫描 51/52，只有既有的 family_source_cli_test 失败。

- 基线 `94e298d8`，证据先行：o32/28894 m29074 Op65/PC174 → Op101/PC258（Use111），o7/13863 m14290 Op201/PC508 → Op245/PC633（Use276），均为 `kConsumedEvent`：已登记 `kLogicalValue`，但消费目标属于 LiteralPlan 的 `kInitializer`，普通 Domain 不遍历其输入。不是 Round 46 的 predicate/keep-edge 联合义务。临时插桩已全部撤回。
- 固定大小 `LogicalCoverageEvidence` 区分 ValueMap / RouteMember / ForwardedRegistration / ConsumedEvent，保留实际位置、consumer、登记与覆盖状态，贯通 route/record-entry/JSON。逆序构建只有在所有选择的真实输入消费均通过区间证明后才提交 route 及成员；不完整候选保留原 phi/临时表示。正序验证器仍独立重算；无 TreeFlow、控制、声明、literal 准入或资源上限改动。详见 [selection-consumers.md](selection-consumers.md#atomic-route-coverage-round-61)。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；新测试加 JSON 通路 **3/3**；`logical|evaluation|selection|decision` 定向 **52/54**，仅既有 `phased_arm_selection_source_test`、`decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 扫查 **50/51**，仅既有 `family_source_cli_test` 失败。未改这些既有失败断言。
- 新增 `logical_coverage_test`、`logical_coverage_cli_test`：四类 mismatch 变异、部分 route 保留/终止、copy/寄存器/拆块、200/201/400/800 和精确 byte/work 边界；真实 es2abc fixture **112** 次 Node 差分、**10** 个变异检出。规模测试最小 scratch 预算为 3,988,312 / 4,008,235 / 7,972,912 / 15,942,112 bytes，work 为 1,777,364 / 1,786,243 / 3,566,587 / 7,158,200。
- 两组 exploration 对 explore60/explore60s：原 flags **500 → 501/620**，`--language-source` **537 → 539/620**；**0 状态回退、0 已完成文本漂移**，仅两个目标的 complete 增益产生诊断变化。assertClose o32/28894 在两组 flags 下 complete；GridLayout o7/13863 只在 source 下 complete，旧 flags 的 `DerivedProjectionError=1`（m14399，无 PC）保持不变。新完整输出全部 `node --check` 通过。
- `evaluation_failure.mjs` 的两个旧拒绝期望改为严格 complete 和 manifest/abc_semantic 完整源码一致性；其他四个后续拒绝期望不变。
- 证据：`/private/tmp/r61-evidence-o32/manifest.json`、`r61-evidence-o7/manifest.json`（修复前的类型化证据）；`r61-build-final.log`、`r61-fixed.log`、`r61-new-tests-final.log`、`r61-targeted.log`、`r61-sweep.log`、`r61-cohort.log`；导出在 `/private/tmp/r61-cohort/`。未运行留出集、全套测试或 RSS 基准；for-of 继续暂停。

## Round 60：DecisionEdgeCopies（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 498 → **500/620**，`--language-source` 535 → **537/620**（ScanSize、Filter；Filter 含三个原生 for-in，`node --check` 通过）；0 回退，0 文本漂移；cli/corpus/failure 扫描 50/51，只有既有的 family_source_cli_test 失败。

- 基线 `326fda68`。证据先行：o20/10622 m11730 PC139 e10 为 `v24 ← v23`（Use148）；o25/33281 m33743 PC421 e12 为 `v28 ← v27`（Use319）；o32/43537 m44496 PC252 e203 为 `v26 ← v35`（Use114）。均是最后谓词直接到 join 的 taken 边、单步无 cycle 临时量；真正的首拒绝位于 `OrientBranches`，早于 `EmitDecision`/`RequireNoCopies`。临时诊断已移除。
- 在原标量谓词的条件执行边上复用已验证的 phi copy schedule，表达为 `predicate ? (yes copies, true) : (no copies, false)`；无 copy 的 decision 不变。TreeFlow 独立检查条件臂、极性、顺序、唯一消费、末尾 Boolean；TreeEdgeFlow 共用语句 copy 的身份/临时量/定义检查，原有 OriginalFlow/recovery 验证边限定定义。未改变控制计划、声明、catch staging、打印器或资源上限。见 [loop-discovery.md](loop-discovery.md#decision-edge-copies-round-60)。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**。decision/edge-copy/phi-copy/control/logical/evaluation/recovery 定向 **40/41**，仅既有 `decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 扫查 **49/50**，仅既有 `family_source_cli_test` 失败。
- 新增 `decision_edge_copies_test` 与 `decision_edge_copies_cli_test` **2/2**：taken/fallthrough、copy/拆块、swap cycle schedule、错边/hoist/drop/duplicate 变异、200/201/400/800、精确 byte/work 边界；真实 es2abc fixture **96** 次 Node 差分、**6** 个源文本变异检出。200/201/400/800 的构建 scratch 最小预算为 8,148,463 / 8,188,154 / 16,283,271 / 32,552,887 bytes，work 为 1,674,167 / 1,684,143 / 3,393,649 / 6,811,969。
- 严格更新两处过期期望：`for_in_evaluation.mjs` 将 Filter 的旧 DecisionEdgeCopies 拒绝改为 complete、三条 native for-in 及 CLI/manifest 源码一致性；`phased_shared_decision_refusal_test` 的 catch-staging 例仍拒绝，首错从 copy 门禁前移后的 `kControl` 改为实际 `kOwnership / kCatchStaging`，另组仍严格为 `kOwnership / kExpressionOwnership`。没有放宽 staging。
- 对 explore59/explore59s：**500/620、537/620**（各 +2），**0 回退、0 已完成文本漂移**。每组仅三个目标诊断变化：ScanSize o25/33281 与 Filter o32/43537 complete；CustomAnimationTab o20/10622 → m12066 block3/control3 的 `FunctionTreeError=0 > TreeObligation=3`（DecisionPrefix，无可发布 PC）。新增完成文件两套 flags 均通过 `node --check`。
- 证据：`/private/tmp/r60-o{20,25,32}.diag`、`r60-full-build-final.log`、`r60-fixed.log`、`r60-targeted-final.log`、`r60-sweep.log`、`r60-cohort.log`；导出在 `/private/tmp/r60-cohort/`。未 commit/push/stash，未运行留出集、全套测试、RSS 基准；for-of 继续暂停。

## Round 59：SnapshotCandidate 一致性（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 回退，0 文本漂移；`snapshot_syntax_test` 在 a0302edb 上同样 rc 134（既有）。cc 另修正 `tree_failure.mjs` 中 o5/28303 的过期期望（Round 53–55 后首错为 m28799 TryId 0 的 kMultipleExits，body 1146/handler 1171），并对所有 cli/corpus/failure 类测试做了全量扫描：49 项中只有既有的 family_source_cli_test 失败。

- 基线 `a0302edb`；for-of 按用户决定暂停。两个目标 o8/10284 m12516、o28/10155 m11832 的真实失格分支一致：op24/PC59 的 Use34 消费者 op33/PC82 在构建时为 Root，logical-left 规划后为 LogicalLeft，最终候选报 `kConsumerPlacement`。临时追踪已移除，固定大小类型化候选证据保留并贯通两条 JSON 路径。
- snapshot 在 logical-left 规划后、logical route 提交前按同一候选证明确认；失格者恢复原位置的物化定义，并沿已有有界依赖遍历保留输入保存赋值。没有 verifier catch/retry、准入放宽、控制或声明位置改动。见 [expression-snapshots.md](expression-snapshots.md#snapshot-candidate-commit-boundary-round-59)。
- 完整 `cmake --build build/regression -j 6 -- -k` 成功；固定集 `ctest --preset source-regression -j 1` **65/65**；snapshot/logical/evaluation/selection 加 tree_failure/source_route 定向 **47/51**。四项既有失败：snapshot_syntax_test 与 snapshot_syntax_source_test（同一底层 :109 位置断言，Round 36 日志已有）、phased_arm_selection_source_test、decision_snapshots_source_test。未改其断言。
- 新增 snapshot_candidate_test 与 snapshot_candidates_cli_test **2/2**：copy/寄存器/拆块、错误/重复/遗漏消费、200/201/400/800 和精确 scratch/work 边界；真实 es2abc fixture **36** 次 Node 差分、**6** 个检出变异。tree_failure_test 验证所有新 sub-reason、PC0/缺失 PC 与两条 JSON 路径。evaluation_failure_cli_test 严格固定两个目标的新首错，并把已在 explore57/s complete 的 o23/4775 过期 print 期望更新为源码一致性检查。
- 对 explore57/explore57s：**498/620、535/620** 不变，**0 状态回退、0 文本漂移**。每组仅两个目标首错变化：o8/10284 → m11969 block6/control6；o28/10155 → m11285 block6/control6，均为 `FunctionTreeError=0 > TreeObligation=3`（DecisionPrefix，无 PC）；未越界修复。
- 证据：`/private/tmp/r59-evidence-o8.json`、`r59-evidence-o28.json` 及同名 `.trace`；`r59-build-final.log`、`r59-fixed.log`、`r59-targeted.log`、`r59-cohort.log`；导出在 `/private/tmp/r59-cohort/`。未 commit/push/stash；未运行留出集、全套测试或 RSS 基准。

## Round 57 增量 A（部分）：迭代器键解析与候选扫描（cc 审查通过）

- 设计（Round 57）获批：控制构造前用 IteratorProtocolScan 证明区域所有权；ProtocolCloseUnit 按完成种类分端口，循环骨架保留原控制身份；区域显式标记为 OrdinaryTry/Inactive/ProtocolOwned；关闭优先级从展开代码的值流证明；分 A/B/C/II/III 五个门禁增量。
- 本次交付：类型化 `IteratorKey`（不运行 TreeConstants 与名称规划）；`iterator_protocol_scan` 产出候选事实（acquisition 身份、唯一候选 next 定义、return 读取派发的区域与外层 handler）；`VerifyIteratorCandidates` 独立核对候选，不产生所有权。尚未完成：动态激活/advance 证明、close 完成值流、VerifiedProtocolRegionOwnership、PhasedBody 接入与诊断输出。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；新测试 1/1。

## Round 55：kMultipleExits 位置证据（cc 审查通过）

- 固定大小的区域/出口证据（TryId、body/handler 原始与工作 PC、原始 EdgeId 与 continuation），贯通 route/record-entry/JSON；仅在拒绝时做一次计入 work 预算的线性扫描。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化。cc 的“外层循环体保护区”假设被证据否定：首个冲突是 IteratorClose 内保护 `return` 读取/调用的 TryId 0，两个正常出口进入完成选择代码。下一步由 IteratorCloseEvidence 拥有内层关闭保护区、保存的异常及完成优先级。

## Round 54：不可达 handler 的 inactive 区域投影（cc 审查通过）

- 从原始 ExceptionFlow 重算正常与异常可达性；只有 handler 整段不可达、且没有可达事件派往它时，才生成类型化 inactive 记录（保留 TryId、范围与 protected 指令分类计数）；inactive 区域不分配 try/catch，原始索引保留；recovery 独立重算完整 inactive 集合。kRegion 拒绝补上 region 与 body/handler 位置。新增单元与真实 es2abc（O0）fixture；默认优化产物的 handler 长度为 0，保持带位置的 `kHandlerShape` 拒绝。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；定向 5/5。诊断变化仅 6 条：5 个 for-of 方法改为 `kControl > kMultipleExits`，driver 的 kRegion 补全为 `kLayout > kCrossingScopes`（region 4，PC 777/803）。

## Round 53 增量 I（部分）：标准迭代获取契约与原始语义（cc 审查通过）

- 用户 2026-10-14 批准第五项源语言前提“标准迭代获取”：新增 `kLanguageSourceV2` 与独立谓词 `IterationAcquisition()`，`--language-source` 选择 V2；V1 的语义、枚举值与序列化名称保留。接受的差异：@@iterator 不可调用或返回非对象时原生 for…of 更早抛 TypeError。不在此前提内、须逐循环证明的：next 缓存、done/value 顺序、关闭优先级（language-source.md）。
- 原始语义：同步 GETITERATOR imm8/imm16 → `AcquireIterator`/`ValueIteratorAcquire`（可能执行用户代码与抛出，成功后提交，不伪造 Object 结果域）；`throw.ifnotobject` → `ObjectCheck`/`ValueObjectCheck`。新增 `iteration_semantics_test`。
- 新阻塞：5 个方法在求值前停在 `PhasedMethodError::kControl > ExceptionControlError::kRegion`，原因是保留了 handler 不可达、没有 CatchBinding 的区域（ExceptionPlan 允许，ExceptionTopology 拒绝），需要有可达性证明的 inactive-region 投影。两个 RpcTool 停在其他子函数的捕获初始化。
- cc 修正 `language_source_corpus.mjs`：Round 44 起 FindAndListenAudioOutputDevice 在旧 flags 下也完成（该过期期望当时未被定向集覆盖）。
- cc 独立审查：构建通过；固定集 65/65（rich-text 首跑 worker 超时，重跑通过）；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；定向 15/15。

## Round 52 increment III：OriginalFlow/recovery 联合验证与发布（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 497 → **498/620**，`--language-source` 534 → **535/620**；0 回退，0 文本漂移；for-in 测试 11/11。cc 核对 assertContain 的首个原生 `for ($key10 in actualValue)` 输出语义一致；可读性（空 then、尾部 `continue`）另议。

- 基线 `5b361344`，工作区未提交。OriginalEnumeration / FlowEnumeration 分离 acquire、next 的 Before/Commit、done、success-only KeyWrite/KeyCommit；内部 enumerator/next-result 不伪造源码变量或 ExprId。初始 draft 和每次 BuildRecoveryFlow 都复验原始事件、输入身份、边与提交。真实 key 读取必须由成功 KeyCommit 支配；写入 TDZ 用 reset-aware 声明事实证明。
- ForwardedException 与 PhysicalCatch 分开；逐入口重新核对同 payload、无观察前缀和最终目的地，反向 Kahn 检查全部路径终止。没有删除 handler 一致性义务，也未简化其他 catch。只对全部证明通过的方法移除 kForInRecovery 发布门禁；follow/decision 与原 LoopId 不变。
- 完整 `cmake --build build/regression -j 6 -- -k` **成功**；最终固定集 `ctest --preset source-regression -j 1` **65/65**；定向 **40/41**，唯一失败为此前已登记的 `tree_structure_test`（abort，未修改断言）。for-in **11/11**。新增真实 es2abc fixture 经引擎导出后做普通对象 Node 差分，另有原始 Commit/exception/identity/done、key 提前/耗尽写入、continue、handler payload、声明遗漏变异及 200/201/400/800 精确预算。
- 对 `explore52b/explore52bs`：**498/620、535/620**（各 +1），**0 状态回退、0 文本漂移**；仅四个目标 record 的首拒绝诊断变化。assertContain 新完整且两套 `node --check` 均通过。六个目标循环均通过生产 draft + OriginalFlow/recovery；DataDriver 首拒绝仍为 m42717@153 unsupported，Filter 转到 m44496@252 decision edge copies，core 转到 m21280@599 kCatchState，MockKit 转到 m54264 kParameters（无 PC）。逐循环表见 [property-enumeration.md](property-enumeration.md#round-52-increment-iii-originalrecovery-event-correspondence)。
- 边界：局部 key 在 break 后 live-out 且耗尽边有非空 phi copies 仍以 kExit 拒绝，独立 key_break.abc 固定该边界；属性 key 的 zero/last/break live-out 已有差分。捕获 m524、handled advance、非空 staging 仍拒绝。Proxy/原型或枚举属性变化/Symbol 的 Ark 运行时验证缺口未关闭；不新增源语言前提。
- 证据：`/private/tmp/r52iii-build-delivery.log`、`r52iii-fixed-delivery.log`、`r52iii-targeted-delivery.log`、`r52iii-targeted-list.txt`、`r52iii-target-loops-delivery.log`、`r52iii-delivery-cohort.log`、`r52iii-node-check.log`；导出 `/private/tmp/r52iii-delivery-cohort/`。未 commit/push/stash；未跑留出集、全套测试或 RSS 基准。

## Round 52 increment II：生产 draft 与 TreeFlow（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 9/9。审查重点：preheader 私有 phi 初始化先于 native 语句（依赖 acquisition→first-next 证明），耗尽 copies 仅在唯一正常退出时后置，其余以 kExit 拒绝。

- 基线 `8cca4728`。新增 `ForInEmissionView`，只解释已验证 ControlPlan，不改节点、follow/decision 或 LoopId。生产 builder 发射赋值式 ForInStmt；枚举器、内部 next-result、成功边 key commit 分开，原始声明与普通 body/exit 保留。
- 异常对应按原始入口记录 PhysicalCatch / ForwardedExceptionRoute；逐入口普查后才消费转发路径，混合共享入口和非空 catch staging 保守拒绝。TreeFlow 从不可变输入独立重建 view、协议事件、scope、copy 与 exit 对应。
- 生产 builder 返回 unpublished draft；TreeFlow 成功后，公开入口以 `FunctionTreeError::kSemantics(9) > TreeObligation::kForInRecovery(15)` 拒绝。**没有 ConstructedFunction 包装或源码发布**；OriginalFlow/recovery 联合验证留到 III。
- 完整 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；39 项定向 **38/39**，唯一失败为已登记的 `tree_structure_test`。for-in 相关 **9/9**，含新增生产 draft/真实 fixture、view/异常/作用域/跳转变异、共享 key live-out、200/201/400/800 与精确预算边界。
- 两套对 `explore52/explore52s`：**497/620、534/620** 不变，**0 状态变化、0 文本漂移**；仅 core/assertContain/Filter/MockKit 四个目标诊断改为 recovery 门禁。六个循环的方法级 draft + TreeFlow 均通过；DataDriver 首拒绝仍是 m42717@153 的 getiterator；Filter 两循环共享首个 acquisition @28 的公开门禁。
- 证据：`/private/tmp/r52ii-build-full.log`、`r52ii-fixed.log`、`r52ii-targeted.log`、`r52ii-target-drafts.log`、`r52ii-final-cohort.log`；最终导出 `/private/tmp/r52ii-final-cohort/`。设计、限制见 [property-enumeration.md](property-enumeration.md#round-52-increment-ii-native-draft-and-treeflow-unpublished)。未 commit/push/stash，未运行留出集、全套测试或 RSS 基准；不宣称 engine-output Node 差分。

## Round 52 increment I：生产求值接入（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 7/7。

- 基线 `4674e981`。两个 phased 调用点通过共享 evaluation constructor 构造并验证 `ForInPlan` / `ForInEvents`；协议成员/use 索引区分 enumerator、next-result、done 和真实 key 用法。内部 copy/phi SCC 保留身份和原始输入记录，不分配源码符号；acquire/next 保持有序可抛出事件。normal-flow 源码准入未扩展。
- FunctionTree 在表达式/符号构造前独立复验，并以 `FunctionTreeError::kSemantics > TreeObligation::kForInCorrespondence` 在首个 acquisition 拒绝。尚未接入 TreeFlow/OriginalFlow/recovery 的 native-for-in 联合对应，不发布半验证树。
- 完整 keep-going 构建通过；固定集串行 **65/65**；37 项定向 **36/37**，唯一失败为既有 `tree_structure_test`。新增 `for_in_evaluation_test`、`for_in_evaluation_cli_test` 均通过，含 SCC/phi、协议 use/事件变异、200/201/400/800 与精确预算边界、两条 JSON 路径及 abc_semantic/manifest 一致性。
- 对 `explore51e/explore51es`：**497/620、534/620** 不变，**0 状态变化、0 文本变化**；仅 o32 的 core/assertContain/Filter/MockKit 四个 record 更新为新树拒绝。六个目标循环均通过方法级求值验证；DataDriver 仍先拒绝其他方法 m42717@153 的 getiterator，Filter 两循环共同由 m44469@28 的首个 acquisition 门禁表示。
- 设计和范围见 [property-enumeration.md](property-enumeration.md)。证据：`/private/tmp/r52-build-delivery.log`、`r52-fixed-delivery.log`、`r52-targeted-delivery.log`、`r52-target-loops.log`、`r52-delivery-cohort.log`；新导出在 `/private/tmp/r52-delivery-cohort/`。未 commit/push/stash；II/III 尚未实施。

## Round 51d：for-in 协议事件证据（cc 审查通过；生产接入待续）

- `for_in_events` 分开记录 acquisition、advance 的正常 Commit 与 not-done 边上的 key commit；验证器重新核对原始 phase、结果定义、异常出口与 done 两侧边。新增 `for_in_events_test`（伪造 Commit、替换结果、错配异常出口、反转 done 边、耗尽边写 key、遗漏/重复事件，200/201/400/800 规模与精确预算）。尚无生产消费者；生产接线草稿因联合验证未完成而撤回。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 5/5。

## Round 51c：ForInStmt 语法、打印与恢复骨架（cc 审查通过；生产接入待续）

- `ForInStmt` 分别持有 RHS、key、body，不引入源码枚举器；接入遍历、结构检查、break/continue 标签与 recovery 事件骨架（acquisition 一次、continue 回到 advance、exhaustion 不写 key）。打印赋值式 `for (key in object)`，保留原声明；`kPerIterationLet` 仍明确拒绝。尚未接入生产准入：advance 结果与源码 key 在 done 成功边上的提交对应、TreeFlow/OriginalFlow/recovery 的共同验证、捕获 key 的激活证明（m524：PC10 RHS 环境、PC39 逐迭代环境、PC47 初始化 key）。
- 新增 `for_in_tree_test`、`for_in_tree.mjs`（手工构造树的打印结果，不是 ABC 恢复差分）。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 4/4。

## Round 51b：for-in 协议证明与异常转发摘要（cc 审查通过；D/E 待续）

- 步骤 B/C：`ForInPlan` 经 SCC copy/phi 身份、自然循环和异常图支配，证明 acquisition 身份、闭合使用、每轮单次 advance、done 等价、key 使用与 acquisition 顺序，失败带固定大小的类型化 witness；`exception_forwarding` 是通用的异常转发摘要（所有正常路径无可观察效果地抛出同一 SSA 值到同一目的地），只由 ForInPlan 消费。新增 `for_in_plan_test`、`for_in_fixture_test`（真实 es2abc fixture）。
- 6 个循环都通过证明与证书复核，handler 都取得转发证据；尚未接入 ForInStmt 发射，公开首个拒绝不变。捕获 key 的 fixture 暂停在 `kAcquisitionOrder`（待 D 的激活证明）；handled advance 暂停在 `kCatchState`。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；新测试 2/2。

## Round 51a：for-in 原始语义（cc 审查通过；B–E 待续）

- 只做设计中的步骤 A：提升 `PropertyIterator`/`NextPropertyName` 为有序、可能抛出、成功才提交的语义操作；新增独立的 `PropertyEnumerator` 内部值域与 `ValueEnumeration`，枚举器不能作为语言值或 Hole 输出，源码投影仍然拒绝。详见 property-enumeration.md（含 Node 无法验证的 Proxy/原型变动/Symbol-key 运行时缺口）。
- 5 个候选越过 unsupported-instruction：4 个停在 `EvaluationError::kUnsupportedOperation > kPlan`（待协议投影）；DataDriver 暴露另一方法 m42717@153 的 GETITERATOR。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 均不变，0 状态变化，0 文本变化；新测试 2/2。

## Round 50：严格属性操作（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 495 → **497/620**，`--language-source` 532 → **534/620**；0 回退，0 文本漂移。cc 核对 `super.changeIndex(index, useAnimation)` 与 `LEVEL = ({["0"]: 1 << 24, ...})` 的输出形态。

- 基线 `bdab7660`。新增严格 delete、私有对象初始化中的 indexed DefineField、
  保留 home object/current-this 身份的 super named read；normal/phased 共用类型化语义。
  无新增源语言前提；for-in/apply/iterator/sendable 仍不在本轮范围。
- 完整 keep-going 构建成功；固定集串行 **65/65**；新增/扩展 **9/9**；
  相关定向 **87/94**，失败为本页已登记的 7 个 literal 既有失败，未改断言。
- 对 `explore48/explore48s`：原 flags **497/620**、source flags **534/620**，各 +2；
  两套 **0 状态回退、0 文本漂移**。Constant 与 CustomAniamtionTabController 完成；
  core/MockKit 越过 delete 后分别停在 m21553/PC47、m54047/PC230 的 GETPROPITERATOR。
- 实现、测试、边界及日志见 [Round 50 results](round-50-results.md) 和
  [strict property operations](strict-property-operations.md)。未 commit/push/stash。

## Round 48：TreePrint 存活期账本（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 488 → **495/620**，`--language-source` 522 → **532/620**；0 回退，0 文本漂移；
  10 个新完成输出 `node --check` 全部通过，文件字节数与 Round 47 计数 sink 预测完全一致。o4 的 RSS 增长（约 12 → 31 MB）来自原本就计费的 flow_kinds 依赖数组：旧版在分配前被拒，现在能执行到。
  该数组源于 block × binding 的稠密事实格，属于既有 O(n²) 内的实现，另列为后续优化项，不阻塞本轮。

- 基线 `057aa5cb`。精确树身份与仍持有的 owner charge 证明借用；未知输入继续计费。
  verifier/enclosure 的缓冲销毁后才返还 scratch，保留仍存活的 scopes/input。
  同一打印动作先计数，再预付精确 B 字节输出并直接转移所有权；取消未使用最大输出的 5 倍预留。
  未提高 record/process/output/work 上限，未改打印动作、名字或声明。
- 完整 keep-going 构建成功；固定集串行 **65/65**；定向 **17/18**，仅已登记的
  `tree_structure_test` 失败。新增 ledger 测试及原 print-failure/CLI 三项 **3/3**。
- 对 `explore47/explore47s`：**495/620、532/620**（+7/+10），均 **0 回退、0 文本漂移**。
  10 个 source flags 目标全部 complete；旧 flags 三个捕获初始化拒绝保持原原因/位置。
- **不能宣称全部验收通过**：o4/7635 的生产 CLI RSS 从约 12 MB 增至 31 MB，原因是现在
  能执行原有 flow-kind 分析的 17,866,872 字节依赖分配；旧计数诊断同样约 31 MB。
  该解释不代替用户的生产 RSS 门槛。本轮未擅自扩展到 flow solver 优化，等待 cc 审查。
- 具体文件、测试、10 个目标的前后 CLI RSS 和证据日志见
  [Round 48 results](round-48-results.md)。未 commit/push/stash；未运行留出集、全套测试或全语料 RSS。

## Round 47：TreePrint 资源证据（cc 审查通过）

- 基线 `5f1f71de`，仅诊断；预算、输出预留、打印动作、错误枚举和资源分类不变。
  固定大小的 `TreePrintFailure` 保留下层真实收费、site、method/owner 和存在的来源位置；
  route/record-entry/manifest/`abc_semantic --record-source` 贯通。
- 新增只供测试与 CLI 的 `--record-print-budget`，用真实打印动作计数且不保存输出。
  10 个目标在 source flags 下均为 scratch 拒绝；旧 flags 下 7 个到达打印，另 3 个仍先因
  捕获初始化拒绝。计数源码为 10,708–40,091 字节；最大输出预留过大及借用/已销毁空间
  重复计费均有证据。本轮未修正，未提高 128 MiB / 1 GB 上限。
- 完整 keep-going 构建成功；固定集串行 **65/65**；新测试 **2/2**。额外定向组 **11/12**，
  `tree_structure_test` 的空函数 `$exit0` 文本断言失败；使用 HEAD 旧打印器、仅适配失败
  返回类型的对照也在同一断言失败。未修改断言，未将此称为完整旧检出构建或登记的 30 项之一。
- 对 `explore46/explore46s`：**488/620、522/620**，两套均 **0 状态变化、0 源码字节变化**；
  包括所有已发布 incomplete 源码。没有运行留出集、全套测试或 RSS；未 commit/push/stash。
- 完整收费/计数/树规模表、线性规模检查、验证日志及 Round 48 建议见
  [TreePrint budget evidence](tree-print-budgets.md)。账本 peak/retained 不等于 RSS。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；两组探索集 488/620、522/620，0 状态变化，0 文本变化；
  新测试 2/2。`tree_structure_test` 在会话起点 22e7423a 和 c329fd2d 的完整旧检出构建上同样 rc 134，属既有失败，已补入既有失败清单。

## Round 46：选择路线终点资格（cc 审查通过）

- 基线 `a83a8319`。5 个 `EvaluationObligation::kLogicalInput`“缺少 consumer”的根因是：融合路线延伸到出口证据不完整的下一个选择，路线失败被升级为整个 evaluation 的 ownership 拒绝。`selection_consumer` 给出类型化 consumer 结果与物化原因（UseId/keep-edge 证据）。路线只延伸到出口证据完整的选择，否则终止，保留原有 phi/临时值/控制表示；不捕获失败后重试；验证器独立重算路线边界。
- step 4（predicate + keep-edge 联合 consumer 准入）已撤回：它对所有顶层选择生效，导致已完成的 o8/65146 HardKeyUtils 回退（m65316 PC1871 TreeFlow kPlacement）。延后理由见 selection-consumers.md，将来须与树 truth-event 所有权一起设计。
- 结果：StyleUtils、isPromiseLike 完成（cc 核对 isPromiseLike 输出为 `return !!obj && isFunction_(obj.then)`）；o18 两个 TLS 页面转为 `CallReferenceError::kNotInline`（PC212）；o23 转为 TreePrint 资源限制。新增 `selection_consumer_test`、`selection_consumers.mjs`（42 次 Node 差分、8 种变异）。
- cc 独立审查：原 flags 486 → **488/620**，`--language-source` 520 → **522/620**；0 回退，0 文本漂移（HardKeyUtils 逐字节恢复）；固定集串行 65/65；配对样本逐字节相同；定向测试 5/5。

## Round 45：decision prefix 消费初始化 guard（cc 审查通过）

- 基线 `fa9dfbd9`。先贯通固定大小的 `EvaluationFailure` 义务与真实位置，保留最终
  失败尝试、资源分类和两条 JSON 路径；再让普通块和 decision prefix 共用 disposition。
  7 个 T 目标均经实际诊断确认是 `kCellGuard`，只生成边界 anchor；真实 throwing
  `kGuardedCellRead` 仍拒绝，未改声明、控制边界或 catch staging。
- 完整 keep-going 构建成功；串行固定集 **65/65**。最终探索比较为 **486/620**、
  **520/620**，对 `explore44/explore44s` 均为 **0 回退、0 文本漂移**。
- 审查前定向 **44/56**：10 个已登记的既有失败，另两项是过期的 ExternalRoot 期望和
  不适用的 finally-throw 源码 oracle。cc 核实 ABC 的内联 finally 仍在保护区间内：
  m473 PC170 → H247，PC325 → H337，PC377 → caller；重复 finally 轨迹忠实于字节码，
  **不是引擎缺陷**。现仅该模式改用完整的 ABC handler 轨迹期望，其他模式继续源代码差分；
  o5/28303 更新为 m28799 PC677 的真实 unsupported-instruction 原因链。生产代码不变。
- 审查修订后定向 **7/7**；22 次源码 oracle、2 次 ABC handler 严格期望和 6 个变异全部通过。
  本次固定集重跑 **64/65**，仅 rich-text worker 调用失败（stderr 空）；不改代码单独重跑
  **1/1 通过**，耗时 13.76 秒。此前的完整 65/65 仍是前一轮运行结果，不混记为本次全绿。
- 新 IR/规模/预算/JSON/CLI 定位测试通过。7 个 T 与 8 个 E 的逐项结果、完整定向名单
  及日志见 [Round 45 证据](round-45-results.md)。未提交、未运行留出集或 RSS。
- cc 独立审查：完整构建通过；固定集串行 **65/65**；配对样本逐字节相同；原 flags 486/620 不变，
  `--language-source` 517 → **520/620**（新完成 o19 AudioInputDeviceManagement、
  AudioInputDeviceSwitcher、AudioOutputDeviceSwitcher）；两组 0 回退、0 文本漂移；新增与修订的定向测试 7/7。

## Round 44：checked capture 调用参数（已提交 fa9dfbd9）

- 基线 `6f1138a7`。仅从未满足的成员引用义务启动组合；checked read 保留
  read 的值身份和 guard 的异常身份，仍为 ordered/MayThrow，不能降为 kCellGuard。
  原始 guarded 证明、家族需求格、声明/activation 与无 guard 的访问准入不变。
- 独立验证覆盖完整原子事件及唯一外部消费，再发布 composition mask；tree/recovery
  保留 read Before、guard success Commit 和异常映射。新增 catch staging 仍拒绝，
  `GuardedCallError` 携带 call/read/guard/use 与原始/工作 PC，经两条失败输出路径保留。
- 完整 keep-going 构建成功。`ctest --preset source-regression` 首次 64/65，rich-text
  worker 调用失败（stderr 为空；该调用配置了 60 秒 timeout）；独立重跑通过，随后
  `ctest --preset source-regression -j 1` **65/65**。定向 59 项 **50/59**，9 个失败
  全在下方既有清单中；新增 guarded_call / guarded_calls_cli 均通过。receipt、两条
  manifest 路径与原因链测试通过。日志在 `/private/tmp/r44-*.log`。
- 对 `explore43/explore43s` 的最终逐 record 比较：旧 flags **486/620**（+1，
  o16/8916 FindAndListenAudioOutputDevice），source **517/620**；两组 **0 回退、
  0 已完成文本漂移**。结果目录 `/private/tmp/r44-final-cohort/`。
- 三个主候选 o19/7617、22760、35025 都越过原 CALL 拒绝，下一层分别为 stop
  方法 7958、23270、35440 的 `kOwnership > kExpressionOwnership`，原始/工作
  PC 88/85。旧 flags 旁侧 o32/49116 则到达 method 49302 的
  `kControl > kDecisionPrefix`，PC 347/342。未把解除首错计为 record 完成。
- 设计与验证边界见 [guarded-captures.md §11](guarded-captures.md#11-checked-capture-arguments-round-44)。
  未运行留出集、全套测试或 RSS 基准；没有提交。

## Round 42：拒绝义务与位置（工作区完成，未提交）

- 基线 `1c8dee55`。只增加诊断：FunctionTree 的义务、实际下层 verifier 原因，
  可选强类型位置及原始/工作 PC；DeferredCell 的真实 creator、child occurrence、
  allocation/store 与 ExternalRoot/PhasedParent/Placement/Binding 子原因。
- `PhasedMethodError::kInput` 保留 ExceptionFlow 的实际失败 alternative；LruCache
  的空链已定位为 `RecordModuleKindError::kShared`。两个 SemanticProof PC 属于
  initializer 方法 5040/8556，分别为真实 PC 12/13，旧外层方法归属已修正。
- route、record-entry 和 `abc_semantic --record-source` 共享 JSON witness；缺失 PC
  不再发布成 0。机制和边界见 [failure-evidence.md](failure-evidence.md)。
- `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；相关
  定向组 **27/27**；诊断组 **5/5**（与定向组重叠，非额外五项）。新测试覆盖全部
  义务、零/缺失/归一化/synthetic 位置、资源分类、两条 JSON 路径、三个 phased
  input alternative，以及六个真实 ABC 拒绝的 CLI/manifest 一致性。
- 探索对 `explore40/explore40s`：旧 flags **479/620**、source **511/620**；全部
  record 的 status/domain/外层 error/class/reason 无变化，**0** 状态回退或增益、
  **0** 文本漂移。没有运行留出集、全套测试或 RSS 基准。
- 首拒绝完整分布与逐 record 新位置见 [round-42-refusals.md](round-42-refusals.md)。
  没有把相同的义务枚举当作已证明同一根因；整体验证仍可合法地没有局部 PC。

## Round 37：调用实参中的赋值表达式（待 cc 审查，未提交）

- 基于 `c7e4fc1f`。`CallPlanFailure` 区分非属性 callee 与属性未内联，保留 call/property
  OpId、placement/temporary、失败方法及原始/工作 PC；route 和 record-program 两种
  失败出口均带诊断，资源分类不变。
- 新增独立 assignment occurrence 证据，以同一 SSA 值的一次 store / 一次 argument
  观察证明成功写入后的值身份；原 IR 的 store 无结果语义不变。normal/phased 的
  效果顺序、reference/key/RHS、catch 和独立树验证贯通，打印复用 AssignmentExpr。
  设计、逐 record 诊断和边界见 [call-references.md §6](call-references.md#6-round-37赋值表达式作为调用实参2026-10-13)。
- o13 的 MultitonAbility、SingletonAbility、SpecifiedAbility、SpecifiedFirstAbility、
  SpecifiedSecondAbility 均 complete，Node 语法检查通过。o36 ModeComponent 的赋值
  投影成立，但共享目标读取仍需快照，callee 区间不能接续；仍以 `kNotInline` 拒绝。
  仅单独导出所需 ABC；没有运行探索集或留出集。
- 构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；
  call/member/assignment/property 加求值、phase 与诊断定向组串行 **42/42**。
  新 IR 测试覆盖伪造值身份/placement/顺序、catch 快照、copy、规模及预算边界；
  四个完整 fixture 在两种 flags 下与 Node 源码 oracle 的轨迹一致，另有 computed
  行内 key + literal RHS 的保守拒绝 fixture。命令及 SHA256 见 fixture README。
- 剩余边界：额外 RHS 观察、immediate operand、computed key 的不明转换顺序、
  需要新 catch staging 的移动均不放宽；现有 taken-jump 走廊限制也未扩展。
  可选投影失败至多退回一次；分割的工作额度保守计费，cc 应审查其预算影响。
- 下方 30 个既有失败的期望未更改；涉及旧 CallPlanError variant 的测试仅机械迁移
  为 CallPlanFailure 访问。全套测试和 RSS 基准继续暂停。

## Round 36：phased 可达性投影（待 cc 审查，未提交）

- 基于 `03b1681b`，从不可变 ExceptionFlow 的 normal/exception 后继证明可达性；
  保留死 Before/Commit/Catch 的身份与来源，去除其工作图边、操作效果和活跃定义。
  独立导入验证、异常区域/控制和来源对应已贯通。不是 return/NOP 模板。
- `kImport` 保留 `ValueImportError` 叶子原因；回执区分失败方法、原始/工作 PC 与父创建位置。
  资源/工作预算分类保持不变。设计、边界与逐 record 结果见
  [phased-reachability.md](phased-reachability.md)。
- 构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；
  直接相关 7 项 **7/7**。phase/exception/source 等定向串行批次 **293/324**：
  指定的 7 个 literal 和 2 个 generator 失败前后不变；额外 21 项在 `/private/tmp`
  从精确基线 `03b1681b` 构建的工具上逐项复现。community graph 两次 worker 失败，
  第三次带进程错误诊断的串行复跑通过；前两次错误未报告退出原因，不断言为超时。
- 授权的 11 个 record 全部越过旧 import 门禁：4 complete（Node 语法检查通过），
  7 个暴露下一层类型化原因。没有运行探索集、留出集、全套测试或 RSS 基准。
- 仍有保守边界：完全不可达的 handler 可导入，但对应空 try 包装的消除尚未实现，
  exception control 可继续拒绝。新 Node fixture 覆盖正常/异常/finally 轨迹；
  编译器对 return-in-try + throwing-finally 的差异已在 fixture README 单独记录。

## 架构事实

- **只有一条源码路线**：基于 pass 的 value-tree 路线。2026-10-12 按用户指令删除了旧方案，分两步：
  1. **文件级**：删除 legacy（`WriteRecordProgram`）与 shared（`BuildEntrySource→SourceAst→PrintSourceAst`）
     两套 writer，以及只为它们服务的分析模块。依据是对 13 个生产 CLI 做链接器 `-dead_strip -map`
     符号级可达性分析，迭代至不动点。
  2. **声明与分支级**：按 Codex 第三轮审查继续删除残留，包括 `EnvironmentEscape` 及
     `RegisterValueFlow::BuildWithLexicals` 词法分支、spread-field 证明声明、`ModuleExports` 的
     旧 writer 与初始化状态、`GlobalScope` 的模块槽字段、`RecordModuleInitialization`，以及
     `ReadRecordModule` 和 `CreationExpansion` 指针。
- **入口**：`abc_structure --value-source` → `BuildRecordSource` → `BuildRecordTree` →
  FunctionTree 函数族 → `TreePrinter`。双路线概念已全部删除。
- **输出格式**：路线回执 schema 先升为 4，第七轮审查修复后升为 5。manifest schema 升为 2：失败记录改用显式的
  `source_failure_domain`（`record_program` 或 `route_refusal`），不再输出随 variant 布局
  变化的 `shared_failure_variant`。
- **CLI 选项**：`abc_structure` 只接受“仅结构”和 `--value-source` 两种模式，`--source` 与
  `--shared-source` 视为用法错误。`abc_semantic` 的所有源码形式都走 value-tree；
  `--all-records`、`--exception-flow/-template`、`--record-value-slice`、`--class-source`
  已删除。`abc_source_probe` 已删除。
- **资源**：所有 CLI（`abc_scan`、`abc_structure`、`abc_semantic`、各阶段验证器）共用
  `tools/process_limit.h`。
  - Linux 把 `RLIMIT_AS` 软限额设为“继承值与 1,000,000,000 字节中的较小者”，这是地址空间
    限制，不是 RSS 限制。
  - macOS 不设限，报告 `process_limit.established=false`。
  - 限额设置失败时输出结构化诊断：`{"error":"process_limit",...}`。
  - `abc_structure`、`abc_scan` 和各阶段验证器在报告里写入资源字段。`abc_semantic` 保持
    各视图原有的输出通道和退出码，并附上 `process_limit`、`address_space_limit_bytes`、
    `peak_rss_bytes`：
    - 单对象视图的 stdout 结果：并入该 JSON 对象。退出码可以非 0，例如源码片段视图。
    - stderr 错误诊断：并入错误对象。
    - 流式的 `--instructions`：在末尾追加一行资源报告。
    - 参数校验发生在限额建立之前，所以参数错误不带这些字段。
    - 结果捕获失败或结果不是 JSON 时，报告输出错误（`kOutput`），不会发布部分结果。
  - `ru_maxrss` 按平台换算单位（Linux 为 KiB，macOS 为字节）。
  - 1 GB 整进程合同仍未完成。
- **失败遥测（S2）**：`RouteFailure` 与 `RecordProgramFailure` 都带有有界的类型化原因链
  `FailureCauseChain`，按“外层 → 叶子”排列，每项为 `{domain, value}`。domain 是编译器推导的
  枚举类型名，只作为序列化标签。
  - manifest 中写为 `causes`/`source_causes`，manifest 头部还记录 `abc_version`；致命的 record
    失败会在 stderr 输出它的原因链。路由回执的 schema 升为 5，原因链可以往返序列化。
  - 原因链满 8 项时保留前 7 个外层原因，最后一格始终是真实叶子，并标记 `truncated`。
  - `tools/abc_capability` 流式汇总一批导出，并按叶子原因输出帕累托排序。
    - 模块分类独立于恢复结果：根据每个 record 的结构元数据判定，元数据缺失时计为
      `module_unknown`。
    - 没有 `source_status` 的 record 也计入 `module_unknown`，并分别计数：结构失败计为
      `structure_failed`（其类型化叶子进入帕累托），其余计为 `unknown_status`。不会默认为“无模块”。
    - 拒绝非 value-source 视图的 manifest（计为 `view_mismatch`），整文件失败单独计数。
- **record 失败隔离（S3）**：进程级故障只依据带外信号判定，不依据各阶段如何折叠错误码。
  有三种带外信号，都会在以下位置复查：每个 record 前后（包括提前结束的分支）、每个整文件
  索引之后、导出末尾。
  - **真实分配失败**：由 `std::new_handler` 观测（`tools/process_limit.h`），是粘滞信号。
  - **输入读失败**：`corpus::AbcFile::ReadFailed()`，也是粘滞信号。所有输入读取都经过
    `ReadAt`，短读或读错误都会置位；越界属于格式错误，不置位。
  - **输入变更**：`CheckUnchanged`。
  
  只要出现其中任何一种，导出就失败，不论该 record 的结果是什么类型。错误码本身不决定致命性：
  各阶段会把无关错误折叠进 `kRead`/`kWrite`（例如名称字符串格式错误折叠为 `kRead`），所以
  record 级只剩入口捕获的 `kAllocationFailure` 一种按错误码判为致命；模块上下文失败也按
  record 隔离。
  
  真实输出文件的 I/O 失败在写文件处判定，同样致命，包括以下几种：
  - 结构写入器目标缓冲的短写：单独记录，优先于同一 record 之后出现的结构错误；输出配额
    耗尽不会触及目标流。
  - 关闭时才暴露的刷新失败。
  - 撤回部分产物时删除失败。
  
  没有出现进程级故障时，失败按 record 隔离：
  - 资源类失败（`kResourceLimit`、`kMemoryLimit` 类 route refusal）视为该 record 自身预算耗尽，
    与 `kRecordBudget`、`kOutputLimit` 一样只隔离该 record。
  - 整文件索引（方法角色、函数/类创建点）失败，或源码路径规划超出预算时，每个 record 记为
    带原因链的 `kFileIndex` 失败。
  - 源码路径按 record 规划（`structure::PlanRecordSourcePathSet`）。身份证据不可读、编码超限、
    大小写折叠后冲突，都只影响相关 record：冲突组的每个成员都拒绝，以免由顺序决定胜者。这些
    record 记为 `kSourcePath`，其余 record 照常恢复源码。
  - 单个 record 的归档路径失败或结构写入失败，在 manifest 中记为类型化的 `structure_failure`，
    不发布该 record 的文件。
- **版本能力（S4）**：`include/abcd/abc/profile_capabilities.h` 是集中的能力表，列为
  container_decoding、pinned_runtime_semantics、literal_arrays_in_header，并附锁定运行时的出处。
  语义层门禁统一查询该表。
  - 24.0.0.0 已开放：锁定解释器只按 opcode 分派，相同 opcode 在 13.0.1 与 24.0 语义一致。
  - 9 和 1102 仍可解码，但语义恢复未准入（类型化 `kProfile`）。
- **本轮新增的通用语义规则**（均与样本无关，附出处，见源码注释）：
  - **透明重抛区域**：同时满足两个条件时，整体擦除方法内的 try 区域
    （`analysis/transparent_rethrow.h`），出处是 es2panda 的 `AddCatchBlockForImplicitSuperCallChecks`：
    - 所有处理器的入口都是 `THROW`；
    - 没有任何 try 区域覆盖处理器 PC，因此重抛必然离开方法。自循环、互相重抛的情形都保留不擦。
  - **`CALLTHIS*WITHNAME` 按 `CALLTHIS*` 提升**：锁定解释器只在不可调用的 `TypeError` 文本中
    使用该名字。
  - **方法角色索引遍历嵌套 literal array**：24.0 会生成嵌套形式。遍历在入队时去重，环和共享
    子数组只访问一次。当前 literal 的条目容量在其存活期间一直计入预算，新节点和队列扩容也先
    计入预算，所以峰值是这些项的总和，受预算约束。
- **规模**：生产代码（`src`、`include`、`tools`）净删约 2.9 万行；`src/source` 从 543 个文件、
  83,859 行降至约 380 个文件、约 6.2 万行。

## 本轮验证证据（主检出 `build/regression`，macOS）

| 项目 | 结果 |
|---|---|
| 固定五份的 value-tree 产物，`f0028f54` 与 `7b2491b7` 两套二进制对照，覆盖默认合同和 numeric+constructors 合同 | 源码逐字节一致；manifest 只差在删除的 `legacy_writer_used` 字段。之后 manifest 升为 schema 2，失败记录的字段也随之改变 |
| 全量构建 `make -k all` | 0 错误 |
| Codex 复审（gpt-6-astra，xhigh） | 第七到第九轮共提出 13 项阻断问题，均已修复；第十轮（`9e79196f`）结论为 **APPROVE**。其两条非阻断建议已采纳：延迟短写的测试，以及其余 record 源码逐字节不变、目标叶子为 `kBadString` 的断言 |
| 固定 65 项（`ctest --preset source-regression`，第九轮审查修复后） | **65/65，85.12s** |
| 固定五份，numeric+constructors 合同 | 106/106 完整，与之前一致 |
| 审查修复相关的 16 项定向测试（含新增的 `record_failure_json_test`） | 16/16 通过；`historical_record_source_test` 由原有失败转为通过 |
| 第九轮审查修复后的定向回归（218 项，`-j3`，window 与 community 单独运行） | 204 通过，失败的只有下行 14 项原有失败；window、community 单独运行通过 |
| `record_isolation_cli_test`（新增）：把 Immersive 中一个 record 的 scope 名称字符串改成非法 MUTF-8，并重算校验和 | 导出成功；只有该 record 失败，带 `MetadataError` 原因链，其余 record 结果不变。修复前，整个导出会因 `kRead` 中止 |
| 相对 `f0028f54` 改动过的全部测试，加本轮新增测试 | 除下行所列原有失败外均通过，详见 [测试台账](2026-10-12-legacy-test-ledger.md) |
| 构建核对更正（Codex 第十四轮 R1） | 此前用 `cmake --build … -k` 核对“全量构建无错误”，但 `-k` 并没有传给 make，构建在第一个错误处就停了。`5836d200` 与 `6130717d` 的错误类型封装因此漏改了 11 个测试和诊断翻译单元，以及排除构建的 `exception_candidates`。这些已修复，并按正确方式重新核对：`cmake --build build/regression -j 6 -- -k` 无错误，排除构建的目标也逐个单独构建。另外，排除构建的 `iterator_candidates` 在 HEAD 上同样无法构建（引用已删除的 `abcd/analysis/first_for_in.h`），属于原有问题，待清理 |
| 长期失败测试的清理（2026-10-13） | 原有 14 项与新发现的 4 项逐一排查，其中 16 项是测试期望过时，已按现行表示更新，语义 oracle 与变异检测均保留：零宽省略边界、`let` 声明初始化、冗余括号、`for` 形式的循环、改为恢复的 getter 情形、名字预留。位置 JSON 新增 `"extent": "boundary"`，暴露打印器的省略边界判定。删除了孤立的 `iterator_candidates`（已登记台账）。仅剩 `file_generator_return(_loop)_source_test` 两项是真实的引擎缺口（generator yield/return 选择的 draft 失败） |
| 当前原有失败 | 仅 `file_generator_return_source_test`、`file_generator_return_loop_source_test`。固定 65 项全部通过；定向 226 项中只有这 2 项失败 |
| generator 两项失败的定位（2026-10-13） | 属于 algorithms.md「402 return-selection fixture 耗尽累计 64MiB 准入」的已登记缺口，不是语义错误。`--yield-return-select-many-json` 在 count=200 时 family 阶段已占 46,964,410 字节（5247 个表达式），打印剩余约 18 MB 时 `PrepareSourceVariables` 的 binding 规划失败。`--yield-return-loop-override-json` 在 count=402（9705 个表达式）时打印剩余 42.8 MB，而 count=200（4857 个表达式）的规划峰值为 23.1 MB。flow 规模是线性的（事件容量 16K→32K），峰值约等于当前与试验两份完整 `RecoveryProjection`，再加上当前 revision 的 liveness、demand、constants 缓存。已验证无效并撤回的方案：`FlowBuilder::Append` 扩容后退还旧缓冲；`RecoveryScratch` 在释放时退还额度。两者对峰值的影响都小于 0.5%。可行方向是在构建试验投影前按 RequiredFacts 淘汰当前派生缓存，或降低单个 flow 的常数。这属于暂停中的计费重设计，未实施 |
| phased 成员调用的两处语义修正（2026-10-13） | phased 的 `CanFuse` 按共享求值形状判定“封闭纯值”（无输入、无效果、不抛出），让它越过类型列表（第一版把 `ValueGlobalObject` 标为 `kPure`，被 Codex 第十九轮 R1 指出会让 `{g: globalThis}` 的 literal 初始化回退，已撤回，并新增回归 fixture `realmlit`）。选择调用接受 `kPhiInput` 消费者作为执行根。见 call-references.md §5.3。新增 6 个 phased 完整 fixture 和 `realmlit`（Node 日志逐条相等，并做变异检测），以及 1 个拒绝 fixture `phconcat`。Codex 第二十轮 APPROVE（876a6094）。非阻断待办：补 IR 级测试，覆盖 unknown／initialized 的 `FrameThis`、带分配效果的 `RestArguments`、伪造 inline 结果被 verifier 拒绝、phi-input 消费者＋选择＋catch staging 的组合；`realmlit` 补 phased 数组。`window_navigation_graph_source_test` 曾在一次串行批跑中失败一次，单独重跑和整组重跑都通过，判定为负载下的偶发失败。固定集 65/65；定向集只剩两项 generator 已知失败；探索集仍为 461/620，逐条记录的状态和源码文本都没有变化（语料中剩余 17 个 `CallPlanError::kReference` 都属于 §5.2 的 Binary 串接形态）。旁路发现：catch `code_size=0` 是合法输入，但目前报 `kHandlerShape`，尚未修复 |
| 跨已提交选择的 phased 组合（2026-10-13） | 共享有序事件证明 `phase_composition.{h,cc}`，按 Codex 第二十一轮共识实施（见 call-references.md §5.4）：已提交选择是外层的原子事件，沿实际正常路径反向逐项匹配，并对被移动的抛出点做 catch 普查；builder 整棵提交或整棵撤销，选择证明除 execution 外必须不变；verifier 独立重证登记的组合。新增 `phase_composition_test`（IR 反例与伪造计划），`call_references_cli_test` 新增 8 个完整类和 2 个拒绝类。固定集 65/65；定向集只剩两项 generator 已知失败；重型组 9/9。探索集 461 → **467/620**，`CallPlanError::kReference` 从 17 降到 11；新完成 6 个 record（o12 的 operations/*），没有任何回退，其他记录的源码文本也没有变化。第二版（Codex 第二十二轮 R1–R3）：改为自底向上逐边证明，线性复杂度；所有成员边重验 `Fusable`；工作区预先计费。未登记的根会沿已有边撤回整棵树中的新边。仍为 467/620，相对第一版没有状态变化；唯一的文本差异是 o32 中临时变量编号整体减一。第三版（第二十三轮）补上失败根的清理闭包，`Next` 改为索引加记忆化；**Codex 第二十四轮 APPROVE（2e601f79）**。非阻断待办：把入口缓存的对照检查固化为 IR 单测；按共识拆分选择验证器；支持嵌套选择、literal、模块与对象操作数 |
| LanguageSourceV1 契约与捕获读取的源语言 TDZ（2026-10-13） | 用户于 2026-10-13 批准四项源语言前提（见 language-source.md）：捕获读取 TDZ、派生类字段、惰性导入、数组展开。新契约 `kLanguageSourceV1`（`--language-source`）在 numeric 加 constructors 的基础上逐项加入这些前提，默认契约和已有 opt-in 都不变。第一项已实现：父函数的 `Rebase` 在该前提下对 Language 需求采用延迟声明，`Deferred()` 接受该前提下的 Language 绑定。新增 `language_source_cli_test`：加 `--language-source` 时完整恢复且 Node 结果相等，不加时按原因链拒绝。探索集：原 flags 仍为 467/620，状态和文本都无变化；`--language-source` 为 **478/620**（+11），没有回退。目标组 24 个 record 中：11 个完成；4 个仍为 Guarded 或已有 store 不支配的情形；6 个暴露出 phased 导入缺口（`PhasedMethodError::kImport`）。另记：导出函数在数组字面量中被调用时报 `EvaluationError::kUnsupportedOperation`，与本契约无关，属于既有缺口。`window_navigation_graph_source_test` 在负载下偶发失败，单独重跑通过 |
| 源语言 TDZ：R1–R3 修订与 Round A（2026-10-13） | 第一次按新工作模式交付：cc 负责方向与审查，Codex 负责实现。根据 Codex 第二十五轮 R1–R3：新增 `CaptureDomain::kSourceAccess`，只用于不在子函数自身 handler 覆盖范围内的访问；经用户批准，前提从读取扩展到写入；契约名称表由枚举生成，往返测试覆盖全部枚举值。Round A（Codex 第二十六轮方案，第二十七轮实现）：phased 中间函数也通过 working environment 映射后代需求，映射失败时报类型化错误；新增类型化格 `JoinCaptureRequirement`（Guarded < SourceAccess < Language，Defined 独立且严格）；新增 `family_cells_test`。cc 审查结论：原 flags 下 467/620，与基线逐 record 一致，文本无变化；`--language-source` 下 **474/620**（+7），没有回退；固定集 65/65。定向测试中 navigation_graph、push_graph、window_navigation 在 -j6 下失败，串行重跑全部通过，属于负载敏感。真实页面 5 个中有 4 个完成；`InputDeviceRoutingSwitching` 含有带用户 try 的 async 访问，留给 Round B（类型化 TDZ 抛出事件投影） |
| 派生构造器帧转发的显式投影（2026-10-13） | LanguageSourceV1 的第二项前提。方案由 Codex 第二十八轮提出，第二十九轮实现，cc 审查。新增 `ConstructorForwarding()` 谓词；新增 `constructor_forwarding.{h,cc}`：在原 SSA 上做帧身份格传播，在 normal CFG 上用四态数据流证明每条正常返回路径恰好转发一次，并证明转发前帧未被改写，失败原因都是类型化的。物理参数与源码参数类型化分离，合成私有 rest 绑定，输出 `constructor(...rest) { super(...rest); ... }`，字段写入保持 [[Set]]。纯转发构造器仍为 implicit，新旧契约下文本逐字相同。新增 `constructor_forwarding_test`（IR，含伪造与规模测试）和 `constructor_forwarding_cli_test`（480 个 Node 差分场景，检出 6 种变异）。cc 审查结论：原 flags 下 467/620，逐 record 不变；`--language-source` 下 **481/620**（+7），没有回退，也没有文本漂移；固定集 65/65。16 个派生 record 中 7 个完成，其余 9 个停在后续缺口（CallPlan 5、SemanticError 2、EvaluationError 1、仍为投影拒绝 1）。community_ability_graph 和 window_navigation_graph 失败的原因是 worker 超时（ETIMEDOUT/SIGTERM），当时机器上运行着 DevEco 和两个模拟器。cc 用 Round A 之前的构建（1fabf894）做了对照：Immersive 与 MultiCommunity 的输出逐字节相同，与本改动无关 |
| 惰性导入输出为 ArkTS `import lazy`（2026-10-13） | LanguageSourceV1 的第三项前提。用户裁决：保留首次访问才初始化的时机，输出 ArkTS 的 `import lazy`，不改成普通静态 import。方案由 Codex 第三十轮提出，第三十一轮实现，cc 审查。新增谓词 `LazyImports()` 与 `ArraySpreads()`（后者只有谓词，尚未实现）。解析 request-phase 表，得到类型化的 eager/lazy 信息，畸形表报类型化错误；lazy 读取保留为可能执行模块代码、可能抛出的事件，且 acc 只在成功时提交；slot→request→binding、加载方式和契约都由独立验证复核。含 lazy 请求的 record 输出为 `.ets`，manifest 标记 `source_language: "arkts"`。旧契约下 lazy 请求一律拒绝。新增 `lazy_module_test` 与 `lazy_import_cli_test`（Node 差分会把 `lazy` 机械删除后执行，只验证值与 live binding；README 写明了这一局限）。cc 审查结论：原 flags 下 467/620，逐 record 不变；`--language-source` 下 **492/620**（+11），没有回退，也没有文本漂移；固定集 65/65。Immersive 与 MultiCommunity 的输出与上一轮逐字节相同。12 个 lazy record 中 11 个完成，1 个转为 EvaluationError=2 |
| 数组展开：原始语义增量（2026-10-13） | LanguageSourceV1 第四项的第一步（Codex 第三十二轮）。新增类型化的 `ArraySpread` / `ValueArraySpread`：三个输入角色为 destination、index、source；结果是成功后的 cursor；建模抛出前的效果，acc 只在 Commit 时写入；不能按纯操作删除。源码投影仍然拒绝，私有数组构造证明与 `[...x]` 树表示留待下一轮。cursor 的 int32 回绕需要超过 2^31（约二十一亿）个元素，已由既有的资源前提（`ResourceFailures()==kExcluded`）覆盖，不需要证明上界。cc 审查结论：两种 flags 下探索集逐 record 状态与文本均无变化（467 / 492）；固定集 65/65。另外发现 7 个与 literal 相关的测试失败（literal_construct、mixed_literal、phased_literal、literal_closure_source、unguarded_literal_closure_source、derived_literal_receiver_source、unguarded_derived_literal_receiver_source），它们不在定向集中；在会话起点 22e7423a 上同样失败，属于既有失败，已列入待办 |
| 数组展开：私有数组构造证明与 `[...x]` 投影（2026-10-13） | Codex 第三十三轮，cc 审查。新增 `array_initialization.{h,cc}`：用公共 SSA identity、支配可用性和全路径效果顺序，证明目标是私有数组，以及 cursor（直接得到，或由 length 重建）、混排元素的 `stownbyvalue`、执行次数和顺序。新增 `ArrayExpr` / `ArrayElement`；spread 是独立的有序可抛出 action；无 spread 的数组保持原表示。新增 `array_initialization_test`、`array_phase_test`、`array_spread.mjs`（Node 源语言差分，已登记批准的差异）。cc 审查结论：两种 flags 下探索集都没有回退，也没有文本变化（467 / 492）；Immersive 与 MultiCommunity 输出逐字节相同；固定集 65/65（一次负载下的偶发失败经重跑通过）。语料暂无新增完成：真实的 spread record 都在 allocation 之后才求值 RHS 子树，第一增量对此以 `kOrder` 保守拒绝，下一轮实现组合。7 个既有 literal 测试失败的情况不变 |
| 数组构造：allocation 之后求值的 RHS 组合（2026-10-13） | Codex 第三十四轮，cc 审查。逐 record 诊断：5 个 spread record 在 allocation 与 spread 之间都有模块读取或捕获 cell 读取，以及 getter/调用子树。新增晚求值 RHS 的唯一消费者与 guard 证书：复用求值形状、SSA identity、可用性和 `BuildEffectOrder`，检查“前一提交 → RHS 事件 → 当前提交”的精确顺序，并由独立验证重算证书；后续融合不会重新吸收这些子树。新增类型化拒绝原因 kOperand、kSharedOperand、kCatchStaging。cc 审查结论：原 flags 下 467/620，逐 record 不变；`--language-source` 下 **497/620**（+5，正是这 5 个 record），没有回退，也没有文本漂移；固定集 65/65；Immersive 与 MultiCommunity 输出逐字节相同；7 个既有 literal 失败不变 |
| phased 导入的通用可达性投影（2026-10-13） | 方案由 Codex 第三十五轮诊断并排序，第三十六轮实现，cc 审查。从 ExceptionFlow 入口遍历 normal 与 exception 后继，分别标记 Before、Commit、Catch 的可达性；不可达阶段只保留来源记录（`UnreachableInstruction`），不导入效果、读取、定义或异常出口；独立验证从不可变输入重算可达性；`inactive_commits` 推广为 `inactive_phases`，exception plan、control、topology 只消费活跃边。kImport 现在保留叶子原因（ValueImportFailure 的 kind 与 pc）、失败方法，以及原始 PC 和工作 PC，创建位置另行记录。新增 `phased_reachability_cli_test`（Node 差分）。cc 审查结论：**原 flags 下 471/620（+4），`--language-source` 下 501/620（+4）**，都没有回退，也没有文本漂移；固定集 65/65；配对样本输出逐字节相同。11 个 kImport record 中 4 个完成，7 个暴露出下一层原因（控制、捕获初始化、发射）|
| 赋值表达式作为调用实参（2026-10-13） | Codex 第三十七轮，cc 审查。第一步补定位证据：`CallPlanFailure` 区分 kNotProperty 与 kNotInline，并携带调用与属性的 OpId、原始 PC 和工作 PC、placement 与 temporary reason，经回执和 manifest 可见。6 个候选 record 的首个拒绝点都是 kNotInline；5 个 Ability record 的根因是同一 SSA 值先被属性写入再作为实参，而原始 store 没有结果。新增 `assignment_values`：按 alias 解析后的 SSA identity 建立 store→argument 的映射，在 normal 与 phased 路径上共同验证 target/key、RHS、写入和参数结果的顺序，写入保持 [[Set]]，复用 `AssignmentExpr`。ModeComponent 的投影已成立，但仍因共享目标读取需要快照而拒绝。新增 `assignment_values_test` 与 `assignment_arguments.mjs`（Node 差分）。cc 审查结论：原 flags 下 471/620，不变；`--language-source` 下 **506/620**（+5），没有回退，也没有文本漂移；固定集 65/65；配对样本输出逐字节相同 |
| phased 受保护捕获读取（2026-10-14） | Codex 第三十八轮，cc 审查。12 个 record 的首个拒绝点相同，都是 `CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization`：父函数先创建闭包，后初始化 cell，而子函数的读取带有原始 named hole-check。缺口在于 phased 输入缺少逐访问的 guarded 证明，下游又把 guard 当作不会抛出的 initialized 检查。新增 `capture_occurrence`，失败回执携带 ordinal、cell、请求的 domain、子方法原始/工作 PC。新增 `analysis/guarded_reads`：用 SSA identity、支配关系和有序事件证明 load 与 guard 的身份、guard 前没有可观察事件、成功值使用及异常出口对应，不依赖邻接模板。仅对有证据且绑定到 deferred cell 的 occurrence 降为 Guarded；新增 `kGuardedCellRead` 事件类别，与非抛出的 `kCellGuard` 分开，不允许 phase composition 吸收，recovery 独立重算证据。需求格、未保护访问和异常父函数准入不变。错误消息边界：es2abc 把原生 `let` 的 TDZ 读取编译为同一个 hole-check，Node 差分只比较异常构造器和轨迹（guarded-captures.md §9）。新增 `guarded_reads_test`、`phased_guarded_reads_test`、`phased_guarded_captures_cli_test`（真实 es2abc fixture）。12 个候选全部越过原拒绝点，其中 OneWayTlsWorker、TlsServerWorker、FunctionUtil 完成；其余 9 个暴露后续原因（7 个 `PhasedMethodError::kControl`，1 个 `FamilyWriteError::kStore`，1 个 `ControlError::kCrossEntry`）。o16 InputDeviceRoutingSwitching 也完成：guarded 读取走新证明，未保护读取走原有初始化证明，没有开放 Round B。cc 核实了 FindAndListenAudioInputDevice 的早期读取都带有 hole-check，所以原 flags 下完成是可靠的；`language_source_corpus.mjs` 已改为新的分界，只有 OutputDeviceChangePause 仍依赖 TDZ 前提。cc 审查结论：原 flags 下 471 → **478/620**；`--language-source` 下 506 → **510/620**；两者都没有回退，也没有文本漂移；固定集 65/65；配对样本逐字节相同；定向集中的其余失败都在既有失败清单内（已在 c329fd2d 上复现） |
| 私有终止区域与类型化控制失败证据（2026-10-14） | Codex 第三十九轮，cc 审查（三轮迭代）。`PhasedMethodError::kControl` 不再折叠原因：新增 `ControlFailure`/`ExceptionControlFailure`/`PhasedControlFailure`，链路为 `kControl → ExceptionControlError → ControlError → ControlOwnershipError`，携带重复节点、进入弧、两侧区域边界、方法与原始/工作 PC（合成节点保留类型与 ID）；`abc_semantic --record-source` 不再丢弃类型化原因。Class A（7 个 `start` 方法：内部带分支的提前返回区域未被识别，共享尾部被重复认领）：建立独立的入口支配树、`AllAbrupt`/`ReachMeet` 摘要，单入口、全部路径 abrupt 且 ReachMeet 为自身的区域头才算私有终止。cc 两次拒绝文本漂移：第一次统一应用改变了 follow 的选择（37 个 record 漂移，o32/49868 的捕获声明被拆开）；第二次的双终止 if/else 被展开（6 个）。最终规则：沿用原 follow；只有原 follow 为虚拟出口，且另一臂的 ReachMeet 越出分支支配（存在可证明的共享 continuation）时才补 follow；两臂各自终止的 if/else 保留虚拟出口。验证器另外检查正常完成端口不在仍会执行的 lexical continuation 之前。新增 `control_terminal_test`、`terminal_regions.mjs`（真实 es2abc fixture，两组 flags 各 360 个 Node 场景）。7 个 `start` 越过原拒绝点，但都停在各自 `stop` 方法（节点 180/弧 182，区域 210→147）；它与 renderer.stop 一样属于 Class B（decision continuation 越过外围 stop），留待下一轮。cc 审查结论：原 flags 478/620、`--language-source` 510/620，都没有变化，0 回退，0 文本漂移；固定集 65/65；配对样本逐字节相同。`try_tree_test` 在 bc0b1dee 上同样 abort（rc 134），补入既有失败清单 |
| decision continuation 与外围 stop 相容（2026-10-14） | Codex 第四十轮，cc 审查。Class B：`Decisions()` 在共同祖先为虚拟出口时把私有 return 臂选为 decision join，共享臂因此越过外围 stop，尾部被重复认领。两组 flags 下同根因共 11 个 record（normal 3：o1 CardCalc/Index、o5 YearView；phased 8：o19/o26 的 7 个 `stop` 与 renderer.stop）。o38 SendCommandComponent 与 o18 handleMessage 是另外的 duplicate，单列。修复：decision 发现与最终 join 分开，在 `Regions()` 已知 enclosing stop T、提交归属前一次性确定。新增真实 normal postdominator 树，与 lexical follow 树、entry dominance 树分开（loop-discovery.md 记录三棵树的分工）。只有当 T 是有界区域的真实 stop、J₀ == R、Private(R)、T 后支配 S、R 不支配 T，且 prefix/作用域/边对应成立时，才以 T 替换 J₀；否则保留 J₀，或以新增 `DecisionContinuationError::{kPostdominator,kPrefix}` 拒绝。独立验证器从原始边复核。新增 `control_decision_test`、`decision_continuations.mjs`（真实 es2abc，两组 flags 各 2016 个 Node 场景，7 种变异全部检出）。11 个原拒绝全部消失：YearView 完成（cc 对照原源码核对 onAnimationStart 的语义一致；`||` 链以 phi 赋值形式输出，可读性另议）；其余 10 个暴露下一层（3 个 FunctionTreeError::kOwnership，2 个 kControl，3 个 CallReferenceError::kNotInline，1 个另一方法的 duplicate，1 个捕获初始化）。cc 审查结论：原 flags 478 → **479/620**，`--language-source` 510 → **511/620**；0 回退，0 文本漂移；固定集 65/65；配对样本逐字节相同；control 定向测试 9/9 |
| 拒绝义务与位置贯通（2026-10-14） | Codex 第四十二轮（只加诊断），cc 审查。`tree_failure.h` 定义固定大小的义务与位置证据：FunctionTreeFailure 保留原有枚举，新增 decision copies/prefix、catch scope/staging、expression ownership、deferred declaration 等义务，并保留底层 verifier 原因；原始/工作 PC 分开发布，缺失 PC 不再输出为 0，synthetic 节点保留类型与 ID。`deferred_failure.h` 记录真实 creator、child occurrence、cell、domain、allocation、initializing store，以及 ExternalRoot/PhasedParent/Placement/Binding 子原因。PhasedMethod kInput 保留 ExceptionFlowResult 的真实失败；LruCache 的空链补为 `RecordModuleKindError::kShared`；两处 SemanticProof PC 实为方法归属错误（o0 m5040、o20 m8556），已修正。两条发布路径与 `abc_semantic --record-source` 一致。重分类结果（round-42-refusals.md，`--language-source`）：kControl 拆为 DecisionPrefix 9 与 DecisionEdgeCopies 2；kOwnership 3 都是 ExpressionOwnership；kDeferredCell 10 都是 ExternalRoot；kInput 拆为 iterator 4、属性枚举 3、delete 1。新增 `tree_failure_test`、`tree_failure_cli_test`。cc 审查结论：479/620、511/620 都不变，0 状态变化，0 文本变化；固定集 65/65；配对样本逐字节相同 |
| 外部类根延迟需求回传 creator（2026-10-14） | Codex 第四十三轮，cc 审查。10 个 `FamilyTreeError::kDeferredCell > kExternalRoot` 属于同一机制：类成员通过捕获槽读取本次类定义的名字（Guarded，d0/s0）。creator 先建树、后准备类成员，需求无法回传。两阶段家族准备（`prepared_family`）：先完成默认参数投影、需求汇总和 root capture 重绑，按原 constructor/member 顺序收集外部需求，与普通子函数需求在 creator 建树前合并、一次 `MarkDeferredCells`，然后再建成员树。存储身份与请求身份分开。`family_tree.cc` 的 ExternalRoot 拒绝改为履约验证（identity/revision、cell、实际 `let` 声明、scope、store→初始化表达式），没有 allow-flag；root 的 TDZ 状态经 `root_capture.targets` 从已验证声明映射。不改激活边界，静态初始化时机、内外类绑定合并、异常父函数延期也不变。3 个跨块 CAPTURE（o3/o10/o29）不在范围内，manifest 逐字段不变。新增 `class_deferred_test`、`class_deferred.mjs`（真实 es2abc，Node 差分覆盖 self 引用、多创建点、初始化前/中读取、抛出后逃逸闭包、heritage 抛出、重入）。完成 6 个（o8/o15/o28 的 StyleConfiguration 与 Log）；其余 4 个暴露下一层（TimeUtils 是 phased 不支持的指令；三个 KeyboardController 是 kOwnership）。cc 对照原源码核对 Log 的语义：静态方法经 `$local2` 读取类绑定，TDZ 保持一致。可读性待改进：一个类以 `$value0`/`$local2`/`Log` 三个名字出现，合并需证明外层绑定不被重写。cc 审查结论：原 flags 479 → **485/620**，`--language-source` 511 → **517/620**；0 回退，0 文本漂移；固定集 65/65；配对样本逐字节相同 |
| checked capture read 组合进成员调用实参（2026-10-14） | Codex 第四十四轮，cc 审查。Round 38 把已验证的 read+HoleGuard 投影保留为独立 root，导致成员调用实参中出现 checked read 时引用无法内联（`CallReferenceError::kNotInline > kRoot > kCrossRegionValue`）。新增 `checked_read_event.h`，以 guard 作为原子读取事件的异常身份；`phase_corridor`/`phase_evaluation`/`phase_composition` 统一消费该视图，事件有序且 MayThrow。`ComposeCheckedCalls` 只从未满足的成员引用义务启动，已满足的调用与独立 checked read 保持原计划（零漂移规则）；证明期间不分配符号、名字或 owner。evaluation verifier 只接受两种互斥状态（独立 root，或有完整组合证据且外部消费唯一的 inline）；guard 始终是 `kGuardedCellRead`；`VerifyPhaseCompositions` 先复核成员、消费、catch 条件，再生成掩码。不支持的 catch staging 报 `kCatchState`。可消费已验证 inline 的模块读取 pair，但没有新增准入。新增 `guarded_call_test`、`guarded_calls_cli_test`（真实 es2abc，14 个 Node 场景，每组 5 种变异）。结果：3 个目标的 CALL 拒绝消失，但都停在各自 `stop` 的 `kExpressionOwnership`（88/85，op 33 对 owner op 31）；旧 flags 下 o16 FindAndListenAudioOutputDevice 完成，输出与 `--language-source` 逐字节相同；o32/49116 暴露 DecisionPrefix（m49302 347/342）。cc 审查结论：原 flags 485 → **486/620**，`--language-source` **517/620** 不变；0 回退，0 文本漂移；固定集 65/65；配对样本逐字节相同 |
| 既有失败清单（2026-10-13 核实） | 以下 30 个测试在会话起点 22e7423a 上同样失败，不在定向集中（全套测试暂停），不属于本会话的回退：file_generator_return(_loop)_source_test；literal_construct、mixed_literal、phased_literal、literal_closure_source、unguarded_literal_closure_source、derived_literal_receiver_source、unguarded_derived_literal_receiver_source；boolean_context_source_test、captured_receiver_source_test、class_tree_source_test、decision_snapshots_source_test、derived_cells_source_test、family_source_cli_test、forwarded_receiver_source_test、phased_arm_selection_source_test、reversed_captured_receiver_source_test、reversed_forwarded_receiver_source_test、reversed_initialization_captured_receiver_source_test、reversed_initialization_forwarded_receiver_source_test、snapshot_syntax_source_test、try_tree_source_test、try_tree_test（2026-10-14 补登，bc0b1dee 上复现）、tree_structure_test（2026-10-14 补登，22e7423a 与 c329fd2d 上同样 rc 134）、snapshot_syntax_test（2026-10-14 补登，a0302edb 上同样 rc 134）、async_semantics_test（2026-10-14 补登，22e7423a 上同样 rc 134）、unguarded_captured_receiver_source_test、unguarded_derived_cells_source_test、unguarded_forwarded_receiver_source_test、unguarded_reversed_captured_receiver_source_test、unguarded_reversed_forwarded_receiver_source_test、unguarded_reversed_initialization_captured_receiver_source_test、unguarded_reversed_initialization_forwarded_receiver_source_test |
| `window_navigation_graph_source_test`、`community_ability_graph_source_test` | 单独运行均通过（231s、205s）；与其他测试并行运行时，其 60 秒 worker 子进程会超时，属于资源争用 |
| `index_cli_test` | macOS 上首次通过（Darwin 断言限额未建立；Linux 的 prlimit 断言保留） |
| 探索集：40 个应用，`--value-source --language-numeric --language-constructors`，按元数据统一分类口径，模块 record 完整恢复 | `f0028f54`：81/611，整文件失败 1。S2–S4 后：430/620。受保护捕获的三个增量之后：**448/620**，整文件失败 **0**，无逐 record 回退 |
| **冻结留出集**：60 个从未接触过的应用（每个版本 15 个），共登记三次评估，均未用于调优 | 第二次：386/505（76.4%）。第三次（`28a8d453`，受保护捕获之后）：**395/505（78.2%）**，整文件失败 0，无逐 record 回退；新增的 9 个完整 record 全部通过 `node --check`。头号叶子 `ScopeError::kInitialization` 从 48 个降到 35 个 |

## 已知限制

- 文件级可达性已到不动点，声明级残留按 Codex 审查清单清理完毕；但保留文件内部是否还有
  内联后不再被调用的旧辅助函数，没有逐函数审计。
- 原因链的叶子粒度取决于各阶段错误枚举的粒度。例如 `FamilyTreeError::kContext` 被 30 多处
  检查共用，要更细的帕累托分析，需要把这类笼统的枚举值拆开。
- 透明重抛规则只在方法内所有区域都透明时生效；部分透明的方法仍走 phased 路径。
- 冻结留出集已登记两次评估。今后若用其中任何输入指导修复，该输入须转入探索集，并另选新的留出。
- `FailureDomain` 依赖编译器的 `__PRETTY_FUNCTION__` 格式；改为显式、版本化的序列化域是后续 S2 工作。
- 分配失败观测目前是进程级全局标志，接入并发（C8）前需要按任务归属重新设计，并补上
  nothrow new 的用例。

## 剩余门槛（按共识顺序）

S0–S4 已实施，Codex 第十轮复审已 APPROVE。下列各项按审计共识的顺序推进；后续能力工作按 `abc_capability` 报告的帕累托
顺序排优先级，不按样本逐个处理。

1. **当前缺口（帕累托顺序）**：冻结留出集中，头号叶子是 `CaptureCellError::kInput`（48 个
   record）。之后依次是 `FunctionEmitError::kKind`、`DerivedProjectionError` 和 `TreePrintError`
   （打印器的上下文与预算）。
   - `kInput` 已完成诊断，并与 Codex 达成设计共识，见 [受保护捕获](guarded-captures.md)。
     探索集中的 77 次拒绝全部是 `ScopeError::kInitialization`：类内自名绑定，或提升函数捕获之后
     才初始化的 `let`/`const`，子函数读取都带 TDZ guard。
   - 第 1 步已实施：原因链改为 `CaptureCellError::kInput → CaptureError::kScope → ScopeError`，
     帕累托现在能看到这一层，准入不变。
   - 第 2–3 步的第一个增量已实施：同块延迟 TDZ 声明。子函数的受保护读取记为 `kGuarded`；
     父函数在第一次 store 处输出 `let x = v`，并由 tree_flow、operands、lexical_stores 独立复核。
     新增 `guarded_captures_cli_test`，用 es2abc fixture 在 Node 下做差分，并用变异检测确认
     测试能发现提前声明。
   - Codex 第十一轮复审（`5836d200`、`98d806ec`）：**APPROVE**。
   - 第十二轮发现一项阻断问题 R1：中间函数自身带 guard 的读取会掩盖后代的 Language 义务。
     `31db00d2` 已修复，第十三轮复审为 **APPROVE**。
   - 后续增量：受保护义务的转发、唯一写入在后续块（声明语句必须直接位于分配所在的块语句中）。
   - 探索集完整 record 430 → **448/620**，无回退。`kInitialization` 从 64 个 record 降到 27 个。
     剩余类别为：类根路径、作用域收窄、未受保护的读取，详见
     [受保护捕获](guarded-captures.md) §7。
   - 第二位缺口 `FunctionTreeError::kExpression`（23 个）的原因链已贯通到
     `CallPlanError::kReference`：成员调用的 callee 读取早于参数中的选择区域。
     normal 路径的融合域已沿内联消费链延伸，探索集 448 → **455**，详见
     [成员调用引用](call-references.md)。
   - 第三位缺口 `FunctionEmitError::kKind`：15 次来自 async 前缀只允许 `mov` 的检查。第一版按
     相邻 opcode 放宽，被 Codex 第十五轮驳回（被删除的 acc 定义缺少 def-use 证明，且属于局部模式），
     已撤回。现在改为按类型化语义准入前缀，并在 CFG 上用数据流证明被删除的 acc 定义不可观察。
     探索集 455 → **461**，详见 `docs/design/async-lexical-prelude.md`。Codex 第十六轮要求把
     直线判定改成数据流，第十七轮（`8527050d`）复审为 **APPROVE**。
   - 剩余 17 个 `CallPlanError::kReference` 都出现在带异常处理器的方法里，例如链式的
     `.then(f).catch(g)`：callee 读取与调用被可抛出操作分到不同的 phase 块。对应
     [成员调用引用](call-references.md) 第 2 步尚未实施的 phased 共同引用证据。
2. **S5 余项**：1 GB 整进程预算与准入；macOS 上的观测与背压。`abc_semantic` 的资源字段改为
   在 JSON 序列化边界直接写入。
3. **C5 → C3 → C2 → C4 → C8**：语义上移到打印之前 → 一般结构化 → 选择表达式通用化 →
   协议迁移 → 有界方法调度器。
4. **9/1102 历史版本**：要给出对照锁定运行时的 lowering 证据后，才能在能力表中开放。

阶段一（固定五份）的未完项见 [Claude 交接](2026-10-12-claude-handoff.md) §4–6。按审计共识，
固定五份只作为回归集，不再是完成条件。
