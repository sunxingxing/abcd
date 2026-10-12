# 当前状态

更新时间：2026-10-12。本页只记录当前事实、阻塞项和证据索引。
2026-09-25 至 2026-10-12 的流水记录原样归档于
[history/status-2026-09-25-to-2026-10-12.md](history/status-2026-09-25-to-2026-10-12.md)；
更早的证据见 [progress.md](progress.md)。

## 更新规则

- 每次提交连贯改动后更新本页：交付内容、构建与验证证据、剩余门槛。历史记录写入 `history/`。
- 不按文件数、提交数或 complete 数量宣布完成；通用性以冻结留出集为准，见
  [审计共识](2026-10-12-audit-consensus.md) §5。
- 全语料批跑、RSS 基准和全套测试仍按用户要求暂停；固定 65 项和定向验证可以运行。

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
| 原有失败之外新发现：`exception_plan_test` | 在 `3a9d7b88` 上单独构建同样失败（line 378，打印位置断言），早于 TDZ 工作；此前不在定向列表中，待单独排查 |
| 原有失败（在 `f0028f54` 基线上以相同原因失败） | pasteboard_breakpoint、backup_class_cells、index_record、navigation_graph、minigame_navigation_graph、module_entry_initialization、phased_arguments、file_generator_return(_loop)、phased_method、hotitem_retained_selection、value_tree_{pasteboard,call,immersive}_slice |
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
2. **S5 余项**：1 GB 整进程预算与准入；macOS 上的观测与背压。`abc_semantic` 的资源字段改为
   在 JSON 序列化边界直接写入。
3. **C5 → C3 → C2 → C4 → C8**：语义上移到打印之前 → 一般结构化 → 选择表达式通用化 →
   协议迁移 → 有界方法调度器。
4. **9/1102 历史版本**：要给出对照锁定运行时的 lowering 证据后，才能在能力表中开放。

阶段一（固定五份）的未完项见 [Claude 交接](2026-10-12-claude-handoff.md) §4–6。按审计共识，
固定五份只作为回归集，不再是完成条件。
