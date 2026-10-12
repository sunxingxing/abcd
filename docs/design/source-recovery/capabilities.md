# 全语料能力清单（骨架）

> **历史快照（2026-09）**：本页全文反映旧路线时期的状态，包括其中“测试暂停”、`RoutePolicy`、
> rules23 等表述，均不再代表当前事实。当前只有 value-tree 一条路线，当前事实以
> [status.md](status.md) 为准。

状态：T00 建立，随任务与 Cnn 持续更新。每行是一项通用语义能力，不是"支持某应用某方法"。
状态值仅四种：`Designed`（规格已定）/ `Implemented`（代码已提交）/ `BuildChecked`（生产构建过、
静态核对有记录）/ `Validated`（真实回归/负例/预算验证执行且通过）。**当前无任何能力处于
Validated：测试、生成 JS 执行、基准与 RSS 测量仍被用户暂停。**

分类与语料分组见 [abc-index](../../../tests/corpus/abc-index.json)、
[cohort 合同](../../../tests/corpus/source-cohorts.json)、
[切片合同](../../../tests/corpus/pasteboard-value-tree.json)。
方法级路线证据由 [acceptance](acceptance.md) 的 receipt 合同承载。
当前阶段、最新实物证据及设计偏差见 [进展看板](status.md)。

## 1. 基础设施能力

| ID | 能力 | 状态 | 说明与阻塞 |
|---|---|---|---|
| CAP-ROUTE | 路线策略/收据/义务闭合（RoutePolicy、MethodRouteReceipt、版本化边界） | BuildChecked | 当前源码 evidence rules 23（不等同 schema 版本）；收据绑定输入身份与工作修订。条件函数族诊断不授予源码收据。方法片段证据见 progress.md；回归未执行。 |
| CAP-DECODE | 容器/版本识别、指令解码、方法体解码 | BuildChecked | 既有能力；历史 normal-flow 记录曾覆盖 5,486,254 动态方法（早期证据，非新路成功数）。 |
| CAP-CLASSIFY | 静态/动态分类与持久化 | BuildChecked | 900 文件/894 唯一内容/402 动态/498 静态为已记录证据；静态一律 skip，不计成功。 |

## 2. 语义提升（analysis 层，输入证明）

每行按 SemanticOperation 家族 × profile 记录；P1/P2/P3 所需家族见切片合同的 opcode 清单。

| ID | 操作族（代表性 opcode 形式） | profile | 状态 | 说明与阻塞 |
|---|---|---|---|---|
| CAP-LIFT-SCALAR | 常量/搬运：LDUNDEFINED、LDA_V8、STA_V8、MOV_V4_V4、LDAI_IMM32、LDA_STR_ID16 | 12.0.6 | BuildChecked | 新路已有类型化导入、对应检查与方法片段证据；跨版本验收未完成。 |
| CAP-LIFT-MEMBER | 命名属性读写：LDOBJBYNAME、STOBJBYNAME、DEFINEPROPERTYBYNAME、THROW_UNDEFINEDIFHOLEWITHNAME | 12.0.6 | BuildChecked | getter/Proxy 可观察语义保留是硬合同（S03）。 |
| CAP-LIFT-GLOBAL | 全局读取：TRYLDGLOBALBYNAME、LDGLOBALVAR | 12.0.6 | BuildChecked | name/var lookup 的 IR、异常和有序效果已保留；name 读取在完整 GlobalScope 检查下可输出。G2 7559 在真实 record 内达 Printed；独立无上下文入口和 var 源码仍拒绝。模块读取/组合待 T11。 |
| CAP-LIFT-CALL | 普通/显式 receiver 调用、NEWOBJRANGE（固定与 range 操作数） | 12.0.6 | BuildChecked | 新路 typed Invoke/Construct、变长操作数与独立输入对应已接通；PageA 7613 的 CALLTHIS1 达 Printed。其他形式真实泛化及行为验证未完成。 |
| CAP-LIFT-BRANCH | 分支：JEQZ、JMP、STRICTEQ/STRICTNOTEQ | 12.0.6 | BuildChecked | 新路 T03 已有直线/if 控制计划、AST 与片段证据；执行验收未完成。 |
| CAP-LIFT-CREATE | 创建：CREATEEMPTYARRAY、CREATEOBJECTWITHBUFFER、DEFINEFUNC | 12.0.6 | BuildChecked（旧路部分） | 新路对象/闭包合同在 T07；hole/descriptor 反例 V11 待测。 |
| CAP-LIFT-LEXENV | 词法环境：LDLEXVAR | 12.0.6 | BuildChecked（旧路部分） | ScopeFacts/capture 义务在 T07。 |

其他家族（迭代器、super range、private property、await/coroutine 等）已有旧路分析实现，
进入新路时按 T04/T07–T10 补行；缺真实样本的能力须记录"合成已覆盖、真实泛化未验证"。

## 3. 值 IR 与源码树（新主干，逐阶段解锁）

| ID | 能力 | 状态 | 任务/阻塞 |
|---|---|---|---|
| CAP-VALUE-IMPORT | 只读输入 → ValueProgram typed 导入 + InputCorrespondence | BuildChecked | T01 部分实现：G1 operand/role、SSA 值、MethodEntry phi、逐操作异常侧表、使用链与对应验证；P1 达 ValuesImported。独立应用/profile、对抗图和完整输入/owner 验证仍待完成，非 T01 完成声明。 |
| CAP-VALUE-SIMPLIFY | 可维护 use 链、identity copy 传播、phi-only SCC、死纯值清理 | BuildChecked | T02 已有实现和片段证据；保留 getter/call。执行验收未完成。 |
| CAP-TREE-G1 | 直线/if 函数树：Literal/SymbolRef/This/Member/Compare/Assignment + Block/If/Return | BuildChecked | T03 已有真实方法片段，不是完整 record；行为验收未完成。 |
| CAP-BINDING | Symbol/Scope/NameEvidence/TemporaryReason、参数 entry 绑定、默认值签名迁移 | BuildChecked | T06 已有参数与默认值支持；函数族默认值投影和完整签名整合仍须校验。 |
| CAP-EVAL-CALL | EvaluationPlan、CallPlan、Call/Construct AST 与 printer | BuildChecked | T04 部分：函数值调用保持间接 eval；成员调用验证属性读取/receiver，含同块属性读取与调用之间不发生边写入的私有 phi；参数独立校验。7613 独立片段与 G2 7559 record 内嵌已贯通。super、不可内联 callee 的显式 receiver、异常区域、跨应用调用证据及执行验收未完成。 |
| CAP-PHI-LOOP | EdgePlacement、parallel copies、MethodEntry bundle、临界边批量拆分 | BuildChecked | T05：正常边复制、紧凑自然循环、MethodEntry 初始化已接入树与独立检查；条件临界边在专属分支落位。真实循环/phi 方法源码及执行验收仍未完成。 |
| CAP-SCOPE-CAPTURE | ScopeFacts、CaptureBinding 逐创建点 rebase、LiteralPlan | BuildChecked | T07 前置构件已接入共享树接口；真实文件函数族输入诊断已加入。完整子函数 AST/源码、捕获写入和端到端验收未完成。 |
| CAP-RECEIVER-CLASS | ReceiverOwner 状态机、ClassTree、构造器共用 callable | Designed | T08。 |
| CAP-EXCEPTION | PhasedValueImport、Try/Catch/Finally completion | Designed | T09。 |
| CAP-ASYNC | await owner 归一化入同一树 | Designed | T10。 |
| CAP-RECORD-MODULE | RecordTree/module live binding/原子发布 | Designed | T11。 |

## 4. 证据与反例义务

- 每项能力升到 BuildChecked 需要：生产构建命令与退出码、ABC-only 核对记录。
- 升到 Validated 需要：对应 Vxx/Sxx/Axx 正反例实际运行、跨应用真实方法证据（或明示
  "缺真实样本"）、留出集结果。这些当前全部未执行。
- 反过拟合：pasteboard 调优；Immersive/push-kit 跨应用开发；MultiCommunity 留出；
  匿名批量文件不做跨应用主张。
