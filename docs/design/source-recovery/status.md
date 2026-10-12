# 当前状态

更新时间：2026-10-12。本页只记录当前事实、阻塞项和证据索引。
2026-09-25 至 2026-10-12 的流水记录原样归档在
[history/status-2026-09-25-to-2026-10-12.md](history/status-2026-09-25-to-2026-10-12.md)，
更早的证据见 [progress.md](progress.md)。

## 更新规则

- 每次提交连贯改动后更新本页：交付了什么、构建与验证证据、剩余门槛。历史写入 `history/`。
- 不按文件数、提交数或 complete 数量宣布完成。通用性以冻结留出集为准，见
  [审计共识](2026-10-12-audit-consensus.md) §5。
- 全语料批跑、RSS 基准、全套测试仍按用户要求暂停。固定 65 项和定向验证可以运行。

## 架构事实

- **只有一条源码路线**：基于 pass 的 value-tree 路线。2026-10-12 按用户指令删除了 legacy
  （`WriteRecordProgram`）和 shared（`BuildEntrySource→SourceAst→PrintSourceAst`）两套
  writer，以及只为它们服务的分析模块。删除范围依据符号级可达性：对 13 个生产 CLI 用链接器
  `-dead_strip -map` 求存活符号的并集，迭代到不动点。
- 入口链路：`abc_structure --value-source` → `BuildRecordSource` → `BuildRecordTree` →
  FunctionTree 函数族 → `TreePrinter`。双路线概念 `RoutePolicy`、`AdmitLegacyWriter`、
  `SourceRoute::kLegacy`、`legacy_writer_used` 已删除。路线回执 schema 升为 4，旧 schema
  明确拒绝；manifest 不再含 `legacy_writer_used`。
- CLI：`abc_structure` 只接受“仅结构”和 `--value-source` 两种模式，`--source` 与
  `--shared-source` 视为用法错误。`abc_semantic` 删除了 `--all-records`、
  `--exception-flow/-template`、`--record-value-slice`、`--class-source`，所有源码形式都走
  value-tree。`abc_source_probe` 已删除。
- 资源：所有 CLI 共用 `tools/process_limit.h`。Linux 上设置 `RLIMIT_AS` 为 1,000,000,000 字节；
  macOS 不设置，并如实报告 `process_limit.established=false`。`ru_maxrss` 按平台换算单位。
  1 GB 整进程合同仍未完成，见下方门槛。
- 规模：生产代码（`src`、`include`、`tools`）净删约 2.8 万行。`src/source` 从 543 个文件、
  83,859 行降到 380 个文件、62,575 行。

## 本轮验证证据（主检出 `build/regression`，macOS）

| 项目 | 结果 |
|---|---|
| 固定五份 ABC 的 value-tree 产物，删除前后两套二进制对照，含默认合同与 `--language-numeric --language-constructors` 合同 | 源码逐字节一致；manifest 唯一差异是删除了 `legacy_writer_used` |
| 全量构建 `make -k all` | 0 错误 |
| 固定 65 项（`ctest --preset source-regression`） | **65/65，91.77s** |
| 定向验证：65 项加全部改动过的测试，共 212 项 | 除下行的原有失败外全部通过 |
| 原有失败：在 `f0028f54` 基线上以相同原因失败，不是本次引入 | pasteboard_breakpoint、backup_class_cells、index_record、navigation_graph、minigame_navigation_graph、module_entry_initialization、phased_arguments、file_generator_return、file_generator_return_loop、phased_method、hotitem_retained_selection、value_tree_pasteboard/call/immersive_slice、historical_record_source，共 15 项（测试名均带 `_source_test` 或 `_slice_test` 后缀） |
| `window_navigation_graph_source_test` | 单独运行通过（228s）；并行时其 60 秒子进程超时，属于资源争用 |
| `index_cli_test` | 在 macOS 上首次通过：Linux 保留 prlimit 断言，Darwin 断言“限制未建立” |

测试的删除、裁剪与迁移已经用户授权，逐项记录在
[旧路线测试台账](2026-10-12-legacy-test-ledger.md)，其中列出了随旧路线一起删除的覆盖。

## 已知残留（新路线内部）

- `EnvironmentEscape::Build` 只有声明、没有定义。`lexical_value_index` 依赖的 escape
  对象只能由它构造，所以这条分支在生产中不可达，应在 C5 收敛时清除。
- 部分保留文件内仍可能有内联后不再调用的旧辅助函数。文件级可达性已到不动点，函数级未逐一清理。
- `SourceForm::kClassConstructor` 已无 CLI 入口，留待 C5 统一清理。

## 剩余门槛（按共识顺序）

1. **S2**：端到端错误链与能力报告。目前回执只保留 class、stage、pc，无法按叶子原因做帕累托分析。
2. **S3**：record 级失败隔离。目前单个 record 的错误仍会让整个文件 `kRecord` 失败，例如 CertificateFramework。
3. **S4**：集中的 profile 能力表，开放 24.0。目前 24.0 在留出集上 0/312。
4. **S5 余项**：1 GB 整进程预算与准入，macOS 上的观测与背压。
5. **C5 → C3 → C2 → C4 → C8**：打印前语义上移 → 一般结构化 → 选择表达式通用化 →
   协议迁移 → 有界方法调度器。
6. 冻结新的留出集（探索集为本轮使用的 40 个 Harmony 应用），并以它作为通用性主指标。

阶段一（固定五份）的未完项见 [Claude 交接](2026-10-12-claude-handoff.md) §4–6。按审计
共识，固定五份只作为回归集，不再是完成条件。
