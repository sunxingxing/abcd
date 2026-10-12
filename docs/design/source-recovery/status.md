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
- **输出格式**：路线回执 schema 升为 4。manifest schema 升为 2：失败记录改用显式的
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
  - `abc_structure`、`abc_scan` 和各阶段验证器在报告里写入资源字段。`abc_semantic` 把
    `process_limit`、`address_space_limit_bytes`、`peak_rss_bytes` 并入它输出的最后一个 JSON
    对象：成功时在 stdout，失败时在 stderr。
  - `ru_maxrss` 按平台换算单位（Linux 为 KiB，macOS 为字节）。
  - 1 GB 整进程合同仍未完成。
- **record 失败隔离（S3 部分完成）**：只有能证明来自 record 自身预算的失败才隔离到该
  record，包括 `record_tree` 中 record 级预算检查产生的新错误 `kRecordBudget`，以及输出上限
  `kOutputLimit`。`kResourceLimit` 仍是进程级致命错误，因为底层阶段（例如
  `module_enclosure.cc:91`、`TreePrinter`）会把真实的分配失败也折叠成它，无法区分来源。
  输入读写、输入变更和入口捕获的分配失败（`kAllocationFailure`）同样致命。
- **规模**：生产代码（`src`、`include`、`tools`）净删约 2.9 万行；`src/source` 从 543 个文件、
  83,859 行降至约 380 个文件、约 6.2 万行。

## 本轮验证证据（主检出 `build/regression`，macOS）

| 项目 | 结果 |
|---|---|
| 固定五份的 value-tree 产物，`f0028f54` 与 `7b2491b7` 两套二进制对照，覆盖默认合同和 numeric+constructors 合同 | 源码逐字节一致；manifest 只差在删除的 `legacy_writer_used` 字段。之后 manifest 升为 schema 2，失败记录的字段也随之改变 |
| 全量构建 `make -k all` | 0 错误 |
| 固定 65 项（`ctest --preset source-regression`） | **65/65，96.70s** |
| 审查修复相关的 16 项定向测试（含新增的 `record_failure_json_test`） | 16/16 通过；`historical_record_source_test` 由原有失败转为通过 |
| 相对 `f0028f54` 改动过的全部测试 | 除下行所列原有失败外均通过，详见 [测试台账](2026-10-12-legacy-test-ledger.md) |
| 原有失败（在 `f0028f54` 基线上以相同原因失败） | pasteboard_breakpoint、backup_class_cells、index_record、navigation_graph、minigame_navigation_graph、module_entry_initialization、phased_arguments、file_generator_return(_loop)、phased_method、hotitem_retained_selection、value_tree_{pasteboard,call,immersive}_slice |
| `window_navigation_graph_source_test` | 单独运行通过（228s）；并行运行时其 60 秒子进程超时，属于资源争用 |
| `index_cli_test` | macOS 上首次通过（Darwin 断言限额未建立；Linux 的 prlimit 断言保留） |
| 留出探针：40 个探索集应用，均使用 `--value-source --language-numeric --language-constructors` | 12.0.x/13.0.1 的模块 record 完整恢复 81/299（不含整文件失败的 CertificateFramework）；24.0 为 **0/312**（S4 处理）；整文件失败 1 |

## 已知限制

- 文件级可达性已到不动点，声明级残留按 Codex 审查清单清理完毕；但保留文件内部是否还有
  内联后不再被调用的旧辅助函数，没有逐函数审计。
- `FatalRecordSource` 仍把所有 `RouteRefusal` 视为非致命，而函数族读取失败中的文件变更或
  资源耗尽也会被包装成 `RouteRefusal`。这是继承自基线的问题，需要 S2 保留叶子原因后，
  按叶子判定是否致命。
- CertificateFramework 仍是整文件失败：`Data.ts` record 的失败来自 `TreePrinter` 的
  `kResourceLimit`（`record_tree.cc:628`）。在区分预算与真实分配失败之前，它仍按致命处理。
  这个 record 只包含几段字节数组，说明打印器的预算核算方式可能有问题，是 S2/S3 的直接线索。

## 剩余门槛（按共识顺序）

1. **S2**：端到端错误链与能力报告。回执目前只有 class、stage、pc，无法按叶子原因做帕累托分析。
2. **S3 余项**：让底层阶段区分“预算耗尽”与“真实分配失败”，包括 `module_enclosure`、
   `TreePrinter`、函数族等；处理结构导出、路径规划和模块上下文中可隔离的失败；按叶子原因
   判定 `RouteRefusal` 是否致命。
3. **S4**：集中的 profile 能力表，开放 24.0（留出探针中为 0/312）。
4. **S5 余项**：1 GB 整进程预算与准入；macOS 上的观测与背压。
5. **C5 → C3 → C2 → C4 → C8**：语义上移到打印之前 → 一般结构化 → 选择表达式通用化 →
   协议迁移 → 有界方法调度器。
6. **留出集**：在 `tests/corpus/source-cohorts.json` 中，MultiCommunity 已改为回归集，40 个探索
   应用已按 hash 登记；冻结留出集为 `pending_selection`，需按应用家族、hash 和 profile 选取并冻结。

阶段一（固定五份）的未完项见 [Claude 交接](2026-10-12-claude-handoff.md) §4–6。按审计共识，
固定五份只作为回归集，不再是完成条件。
