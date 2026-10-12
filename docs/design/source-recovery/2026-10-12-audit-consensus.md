# 2026-10-12 项目审计共识与修复计划

审计方：Claude（Opus 5.5）与 Codex（gpt-6-astra，xhigh），两轮独立审计加交叉核实。
基线提交为 `f0028f54`。本文记录双方一致认定的问题、证据、修复顺序和应停止的做法；
实施进度见 [status.md](status.md)。

## 1. 目标

用户的目标是做一个**通用的鸿蒙动态 ABC 反编译引擎**，不按样本逐个定制。具体是：

- 固定五份配对 ABC 降为回归集，不再是完成条件。
- 是否通用，看冻结留出集上的结果：complete 率、失败的叶子原因分布、语义正确性证据。
- complete 率只是覆盖指标，不能单独作为语义正确性的验收。

## 2. 已核实的事实（基线 `f0028f54`）

| 编号 | 事实 | 证据 |
|---|---|---|
| E1 | 留出探针：随机 40 个 Harmony 应用 ABC（大小 50–400 KB，种子 20261012 抽 12 个 + 种子 7 抽 28 个），参数 `--value-source --language-numeric --language-constructors`。12.0.2/12.0.6/13.0.1 共 21 个文件，模块 record 81/299 complete（27.1%），其余 218 个全部是 `unproved/tree_stage`。24.0.0.0 共 18 个文件，**312 个模块 record**（492 个 record 中有 180 个非模块）0 complete，全部被版本门禁拒绝。CertificateFramework 整个文件失败，错误是 `ExportError::kRecord`（7），叶子原因不明。固定五份为 106/106。 | 探针产物；双方独立复算一致 |
| E2 | 24.0.0.0 是锁定参考 `isa.yaml` 的当前版本（API24 起使用）。锁定的 ets_runtime 中与文件版本有关的行为只有两处：`IsNewVersion`（以 0.0.0.2 为界）和 `ContainsLiteralArrayInHeader`（以 12.0.6 为界）。解释器没有按版本分派 opcode。约 32 个文件里散落着手写的版本白名单。9 和 1102 在解码层已准入，拒绝发生在语义层。 | `isa.yaml:109-120`、`file.h:546`、`js_pandafile.cpp:42`、`semantic_ir.cc:23` |
| E3 | 失败原因在传递中被压缩：`Project()` 只保留 (class, stage, pc)；上游也有提前压缩，例如 `BuildModuleExports` 的失败统一变成 `kExport`。 | `family_route_failure.cc:25`、`record_tree.cc:59` |
| E4 | 产品入口有三条源码路线：`--source` 走 legacy 的 `WriteRecordProgram`，`--shared-source` 走 `BuildEntrySource→SourceAst→PrintSourceAst`，`--value-source` 走 `BuildRecordTree`。 | `tools/abc_structure.cc` |
| E5 | 生产代码仍按局部形态做准入：固定七指令序列、相邻 Boolean 测试、单 phi 形状、连续五个临时槽的 async yield 序列。 | `record_module_choice.cc:15`、`logical_regions.cc:149/699`、`generator_async_yield.cc:15-46`、`async_completion.cc:113` |
| E6 | 结构化能力有上限：只支持单正常出口循环，不可归约图和跨入口区域直接拒绝。 | `control_plan.h:74-78`、`loop_control.cc:108`、`loop_forest.cc:213` |
| E7 | 打印器还在启动变量恢复，晚于签发 `TreeVerified` 的时点。 | `tree_printer.cc:1777`、`verified_function.cc:490` |
| E8 | macOS 上：`abc_scan` 和 `corpus_inspection` 无条件设置 `RLIMIT_AS`，并把 `ru_maxrss` 乘以 1024；`abc_structure` 跳过了限制设置。没有任何 CLI 建立 1 GB 的整进程合同。 | 各工具源码；Darwin 上的探针程序 |
| E9 | 默认的异步/并行/懒加载方法任务系统不存在。 | `method-tasks.md:4` |
| E10 | 验收围绕固定应用展开：`b50fe2c2..f0028f54` 之间 `src/include/tools` 没有任何差异，`tests/integration` 增加约 1541 行。MultiCommunity 仍登记为 holdout，但它已经被反复使用过。 | git 历史、`source-cohorts.json:99` |

## 3. 修复顺序（共识清单）

| 顺序 | 范围 | 验收标准 |
|---|---|---|
| S0 | 记录本共识；reset 实验另存为分支 `wip/community-reset-experiment` 后从工作区撤回；重建留出制度 | 回归集与留出集角色明确；旧测试的去向逐项记账 |
| S1 | **按用户指令删除 legacy/shared 路线**，只保留 value-tree 路线及其使用的 phased/family 组件；把新路线仍在用的类型抽到新接口 | 所有保留的生产入口只进入 value-tree；旧符号和专属目标移除；固定五份产物逐字节对照；定向回归通过 |
| S2 | 端到端错误链与能力报告：稳定的错误域、枚举值和 record/method/PC 上下文；流式读取 manifest 做聚合 | 不同叶子原因可以区分；模块、非模块、文件失败、profile 拒绝的分母准确 |
| S3 | record 失败隔离：结构导出、路径规划、模块上下文、源码恢复中能隔离的失败只影响该 record | 一个坏 record 不拖垮其他 record；不发布半成品；致命错误的分类保持不变 |
| S4 | 集中、分层的 profile 能力表；依据 E2 为 24.0 开放现代动态语义；9/1102 保持“语义未准入” | 24.0 有真实编译产物的正反 fixture；不再整体因 profile 被拒绝 |
| S5 | 共享 CLI 资源模块：说明限制类型、限额值、是否设置成功，并正确换算 RSS 单位 | 各平台单位一致、行为一致；1 GB 合同仍列为未完成项 |
| S6 | 精简 status，补齐交接文档，最终回归核对 | 实施内容、验证结果、覆盖损失、未完成项都有对应证据 |

S0–S6 之后的主线（本轮不实施）依次是：

1. **C5**：把变量恢复、声明放置、名称规划移到打印之前，打印器只负责语法。
2. **C3**：可归约控制流的一般结构化，评估 Beyond Relooper（支配树、逆后序、带标签块）；不可归约部分用受限的 node splitting。异常、完成、挂起边先定接口契约。
3. **C2**：选择表达式和 guard 的候选改由 def-use、控制依赖、效果顺序发现，不再列举编译形态。
4. **C4**：generator/async 协议改由数据流和控制流证明，固定 lowering 只作为受限的快速路径。
5. **C8**：有界方法调度器，在 S5 资源接口确定后可以并行推进。

## 4. 应停止的做法

- 给识别器增加固定指令序列、寄存器布局、邻接关系或特定图形状的分支。
- 把逐个补齐 Community、Push 等应用的平台模拟当作主干的下一步。
- 用 complete 数量、函数进入数、变异数量代替能力范围和语义证据。
- 在打印阶段新增常量传播、存储合并、声明移动或协议恢复。
- 把已经反复查看过的应用继续当作留出样本。
- 用 profile 拒绝顶替模块分类，或把整文件失败从统计中漏掉。
- 继续向状态页追加长篇流水账。

## 5. 留出制度

- **回归集**：固定五份配对 ABC。
- **探索集**：本轮用于缺口分析的 40 个 Harmony 应用。它们已经暴露过，修复时可以使用，但不能再算作未见样本。
- **冻结留出集**：从其余应用中按应用家族、内容 hash、profile 分层抽取，先冻结再评估。任何留出样本一旦被用于修复，就自动转入探索集。
- 全语料批跑、RSS 基准和全套测试仍按用户要求暂停。上面的验收要求不代表授权恢复这些运行。

## 6. 执行结果（2026-10-12）

S0–S4 已实施，S5 完成 CLI 部分。Codex 第七轮审查指出 6 项阻断问题：

- 透明重抛的前提不足。
- 分配失败观测不是粘滞信号。
- 嵌套 literal 遍历未计入预算。
- 能力报告的分母口径有误。
- 原因链在多处贯通不完整。
- 文档存在夸大。

这些问题均已在后续提交中修复。第八、九轮复审又提出 7 项问题，同样已修复：输出写入失败与错误码折叠、粘滞的输入读失败信号、literal 存活期预算、结构失败的统计口径、按 record 的源码路径规划。第十轮复审结论为 APPROVE，详见 [status.md](status.md)。

- **探索集（40 个应用，按元数据统一分类口径）**：模块 record 完整恢复从 81/611（`f0028f54`，
  另有 1 个整文件失败）提升到 430/620（整文件失败 0）。两次的分母因整文件失败的恢复而略有不同。
- **冻结留出集（60 个应用）**：共登记两次评估，均未用于调优。最近一次为 386/505（76.4%），
  整文件失败 0。

后续能力工作按 `abc_capability` 报告的帕累托顺序推进。
