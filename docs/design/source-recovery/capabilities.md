# 全语料能力清单（骨架）

状态：T00 建立，随任务与 Cnn 持续更新。每行是一项通用语义能力，不是"支持某应用某方法"。
状态值仅四种：`Designed`（规格已定）/ `Implemented`（代码已提交）/ `BuildChecked`（生产构建过、
静态核对有记录）/ `Validated`（真实回归/负例/预算验证执行且通过）。**当前无任何能力处于
Validated：测试、生成 JS 执行、基准与 RSS 测量仍被用户暂停。**

分类与语料分组见 [abc-index](../../../tests/corpus/abc-index.json)、
[cohort 合同](../../../tests/corpus/source-cohorts.json)、
[切片合同](../../../tests/corpus/pasteboard-value-tree.json)。
方法级路线证据由 [acceptance](acceptance.md) 的 receipt 合同承载。

## 1. 基础设施能力

| ID | 能力 | 状态 | 说明与阻塞 |
|---|---|---|---|
| CAP-ROUTE | 路线策略/收据/义务闭合（RoutePolicy、MethodRouteReceipt、版本化边界） | BuildChecked | T00 交付并按评审修正：schema 2 收据绑定输入身份（sha256/file_size/abc_version/profile/source_contract），work_revision typed-absent；中央 ValidateRouteReceipt 用于解析与义务闭合；CLI 选项按序无关校验，admission 先于旧分析。`--value-source` 输出类型化拒绝，无旧 writer 回退。单测与 CLI 回归已写未运行；修正版 ABC-only 复查未执行。 |
| CAP-DECODE | 容器/版本识别、指令解码、方法体解码 | BuildChecked | 既有能力；历史 normal-flow 记录曾覆盖 5,486,254 动态方法（早期证据，非新路成功数）。 |
| CAP-CLASSIFY | 静态/动态分类与持久化 | BuildChecked | 900 文件/894 唯一内容/402 动态/498 静态为已记录证据；静态一律 skip，不计成功。 |

## 2. 语义提升（analysis 层，输入证明）

每行按 SemanticOperation 家族 × profile 记录；P1/P2/P3 所需家族见切片合同的 opcode 清单。

| ID | 操作族（代表性 opcode 形式） | profile | 状态 | 说明与阻塞 |
|---|---|---|---|---|
| CAP-LIFT-SCALAR | 常量/搬运：LDUNDEFINED、LDA_V8、STA_V8、MOV_V4_V4、LDAI_IMM32、LDA_STR_ID16 | 12.0.6 | BuildChecked | 旧路已用；新路 T01 导入合同 Designed。 |
| CAP-LIFT-MEMBER | 命名属性读写：LDOBJBYNAME、STOBJBYNAME、DEFINEPROPERTYBYNAME、THROW_UNDEFINEDIFHOLEWITHNAME | 12.0.6 | BuildChecked | getter/Proxy 可观察语义保留是硬合同（S03）。 |
| CAP-LIFT-GLOBAL | 全局/模块读取：TRYLDGLOBALBYNAME、LDEXTERNALMODULEVAR | 12.0.6 | BuildChecked | 新路 G2 复用 GlobalAccess 证明；完整模块组合在 T11。 |
| CAP-LIFT-CALL | 调用：CALLTHIS0、CALLTHIS1（含 range 形式） | 12.0.6 | BuildChecked | 旧路可打印；新路 T04 typed Invoke 导入 Designed。 |
| CAP-LIFT-BRANCH | 分支：JEQZ、JMP、STRICTEQ/STRICTNOTEQ | 12.0.6 | BuildChecked | 新路 T03 最小直线/if 控制计划 Designed。 |
| CAP-LIFT-CREATE | 创建：CREATEEMPTYARRAY、CREATEOBJECTWITHBUFFER、DEFINEFUNC | 12.0.6 | BuildChecked（旧路部分） | 新路对象/闭包合同在 T07；hole/descriptor 反例 V11 待测。 |
| CAP-LIFT-LEXENV | 词法环境：LDLEXVAR | 12.0.6 | BuildChecked（旧路部分） | ScopeFacts/capture 义务在 T07。 |

其他家族（迭代器、super range、private property、await/coroutine 等）已有旧路分析实现，
进入新路时按 T04/T07–T10 补行；缺真实样本的能力须记录"合成已覆盖、真实泛化未验证"。

## 3. 值 IR 与源码树（新主干，逐阶段解锁）

| ID | 能力 | 状态 | 任务/阻塞 |
|---|---|---|---|
| CAP-VALUE-IMPORT | 只读输入 → ValueProgram typed 导入 + InputCorrespondence | Designed | T01。read-ordinal/role 映射、MethodEntry phi、may-throw 侧表。 |
| CAP-VALUE-SIMPLIFY | 可维护 use 链、identity copy 传播、phi-only SCC、死纯值清理 | Designed | T02。禁止删未用 getter/call。 |
| CAP-TREE-G1 | 直线/if 函数树：Literal/SymbolRef/This/Member/Compare/Assignment + Block/If/Return | Designed | T03。P1 首个纵向贯通。 |
| CAP-BINDING | Symbol/Scope/NameEvidence/TemporaryReason、参数 entry 绑定、默认值签名迁移 | Designed | T06。 |
| CAP-EVAL-CALL | EvaluationPlan、CallPlan（Direct/Member/Super/Construct、EvalMode） | Designed | T04。 |
| CAP-PHI-LOOP | EdgePlacement、parallel copies、MethodEntry bundle、临界边批量拆分 | Designed | T05。 |
| CAP-SCOPE-CAPTURE | ScopeFacts、CaptureBinding 逐创建点 rebase、LiteralPlan | Designed | T07。 |
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
