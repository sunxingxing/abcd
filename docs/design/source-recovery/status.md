# 当前状态

更新时间：2026-10-14。本页只记录当前事实、阻塞项和证据索引。
2026-09-25 至 2026-10-12 的流水记录原样归档于
[history/status-2026-09-25-to-2026-10-12.md](history/status-2026-09-25-to-2026-10-12.md)；
更早的证据见 [progress.md](progress.md)。

## 更新规则

- 每次提交连贯改动后更新本页：交付内容、构建与验证证据、剩余门槛。历史记录写入 `history/`。
- 不按文件数、提交数或 complete 数量宣布完成；通用性以冻结留出集为准，见
  [审计共识](2026-10-12-audit-consensus.md) §5。
- 全语料批跑、RSS 基准和全套测试仍按用户要求暂停；固定 65 项和定向验证可以运行。

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
| 既有失败清单（2026-10-13 核实） | 以下 30 个测试在会话起点 22e7423a 上同样失败，不在定向集中（全套测试暂停），不属于本会话的回退：file_generator_return(_loop)_source_test；literal_construct、mixed_literal、phased_literal、literal_closure_source、unguarded_literal_closure_source、derived_literal_receiver_source、unguarded_derived_literal_receiver_source；boolean_context_source_test、captured_receiver_source_test、class_tree_source_test、decision_snapshots_source_test、derived_cells_source_test、family_source_cli_test、forwarded_receiver_source_test、phased_arm_selection_source_test、reversed_captured_receiver_source_test、reversed_forwarded_receiver_source_test、reversed_initialization_captured_receiver_source_test、reversed_initialization_forwarded_receiver_source_test、snapshot_syntax_source_test、try_tree_source_test、try_tree_test（2026-10-14 补登，bc0b1dee 上复现）、tree_structure_test（2026-10-14 补登，22e7423a 与 c329fd2d 上同样 rc 134）、unguarded_captured_receiver_source_test、unguarded_derived_cells_source_test、unguarded_forwarded_receiver_source_test、unguarded_reversed_captured_receiver_source_test、unguarded_reversed_forwarded_receiver_source_test、unguarded_reversed_initialization_captured_receiver_source_test、unguarded_reversed_initialization_forwarded_receiver_source_test |
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
