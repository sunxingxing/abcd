# 全样本通用能力与持续覆盖路线

本文件落实用户最新要求：**pasteboard 只是启动点，目标是所有样本及后续新应用的
通用反编译引擎。** 本合同约束所有任务，不是 T12 才执行的附加测试建议。
当前只设计，不恢复批量测试、JS 执行或 RSS 测量。

## 1. 覆盖对象和已有证据

两个根目录都属于目标集合：

- `/home/rabbit/sample/abc_with_code`：仅取其中 ABC 作为引擎输入。
- `/home/rabbit/sample/abc`：包括子目录，作为批量真实输入；不得只取根目录或应用名文件。

本次只读路径清点确认 paired 目录有以下 5 个 ABC：

| 应用族 | 文件 | 建议角色 |
|---|---|---|
| pasteboard | `pasteboard/modules.abc` | 首个开发切片及固定回归子集 |
| Immersive | `Immersive/modules.abc` | 跨应用 ArkUI/类/生命周期压力样本 |
| push-kit-sample-code-clientdemo-arkts | `modules.abc`、`widgets.abc` | 多 ABC/不同模块上下文的开发与回归样本 |
| MultiCommunityApplication | `MultiCommunityApplication/modules.abc` | 首批独立应用留出候选，不参与初始规则调优 |

表中后两族路径均以同名应用目录为前缀；文件是否支持、方法/profile 分布须由原
manifest 与 ABC 证据确认，不能由应用名推断。用途不是只有这些应用能支持。

`tests/corpus/README.md` 的既有历史记录为 900 文件、894 唯一内容、402 动态文件、
498 静态文件，正常流阶段曾覆盖 5,486,254 个动态方法。这些是已记录的早期分类/解析
证据，**不是本次重新扫描，不是新源码通路成功数**。本设计不复制一个新静态分类缓存。
实际执行以有效内容 hash 与 detector revision 校验后的 `tests/corpus/abc-index.json`
为准；当前文件系统可增删，不能把历史数量写成生产常量。

静态 ABC 继续暂不支持，按既有证据持久 skip；未知版本、malformed、decode failure
都不是 static。全样本目标不是要求当前忽略静态边界，更不是把静态 skip 算成功。

## 2. 两类清单，职责分离

### 2.1 已有分类清单

`tests/corpus/abc-index.json` 继续负责内容身份、静态/动态分类和 detector evidence。
复用、失效、流式 hash、并发/原子持久化遵守 `tests/corpus/README.md`。
不得每次实现新优化规则就使全部静态分类失效；engine revision 与 detector revision 不同。

### 2.2 新能力与路线清单

拟增加：

- `tests/corpus/source-cohorts.json`：小型、审阅后提交的分组/留出政策，引用已有 hash/profile。
- `docs/design/source-recovery/capabilities.md`：实施 T00 创建的人工审阅能力合同表。
- `out/source-audit/<run-id>/index.json` 与分片 receipts：生成结果，不入 git，不读作引擎输入。
- `tests/corpus/pasteboard-value-tree.json` 只固定最初切片，不能充当全语料总清单。

内部 typed records 至少有：ContentId、ApplicationFamilyId（可未知）、ProfileIds、
MethodEntity、CapabilityId、SourceStage、FailureClass、Origin/PC、Route、CohortKind。
JSON 标签仅序列化边界；不使用应用字符串决定优化路径。语义 profile 不能只存头版本。

大量方法 receipts 必须分文件/分片流式写出，内存只保留索引/有界聚合计数，不能把数百万
方法的全部 IR/诊断/源码文本拼成一个大内存 JSON。没有引擎调度器也可以串行流式审计。

## 3. 能力表定义

每行是一项通用语义能力，不是“支持 PageA.setInitiallyProvidedValue”：

```text
Capability
  id: typed stable identifier
  ISA group/opcode forms: exact profile-bound identities
  semantic operations and operand/effect contracts
  accepted control shapes and owner prerequisites
  required proof kinds and invalidation rules
  ValueIR / SourceTree node coverage
  admitted profiles and separately rejected historical differences
  minimal positive/negative synthetic fixtures
  real development and held-out content/method identities
  current stage and remaining typed blockers
```

格式解析、语义提升、SSA/效果/owner、AST、源码输出分别统计，不把“能 decode”当“能反编译”。
CapabilityId 不能成为任意字符串开关；新增能力按具体 enum/variant 合同扩展。
“支持一种 opcode”还需说明 compact/wide/deprecated 等格式，以及同 opcode 在 profile
间的语义差异；未知版本不自动使用 v7 规则。不得从 filename 选择 profile。

## 4. 抽样与反过拟合门槛

1. 开发集：pasteboard 首片，然后 Immersive、push-kit 的不同能力/控制形状。
2. 留出集：按应用族而非同文件内随机方法切分，MultiCommunity 是首批候选；
   批量匿名文件按内容去重与可证明应用关联分组，关联未知要标注泄漏风险。
3. profile 分层：已知历史 profile 与 v7 参考语义分别有用例，不把同一 profile 的
   多个应用冒充历史兼容验证。真实 corpus 没该 profile 时使用已有历史 fixture 并标明缺口。
4. 风险分层：直线/分支/phi/循环、普通/箭头/constructor/async、异常、cell/capture、
   module/global、class/object/array、大小及高扇出；每项能力覆盖其适用层。
5. 同内容重复路径只能算一个独立样本；modules/widgets 同应用族不能算两个跨应用证据。

G1 可先展示 pasteboard 单方法成果，但称“可复用能力验收通过”前必须至少有另一独立
应用的适用真实方法，加上能力边界反例。没有第二真实实例时报告“合成用例已覆盖，
真实泛化待验证”，不编造样本。T00 根据有效清单选定 hash/entity，计划不猜方法号。
G2–G4 每项能力沿用该门槛，并在冻结实现后跑留出集。暂停期间只记录待验证选择。

看到留出失败后允许修复通用规则，但该样本随即转为回归样本；为下一轮保留新的独立
留出组，不反复把已经调优过的文件宣称“未见数据”。不得隐藏先前留出失败。

## 5. 统计与分母

按文件、唯一内容、record、方法分别报告；方法还要区分有可执行正文与声明/无正文实体。
入口/匿名/嵌套/异步生成方法都可追踪，不能只统计具名导出函数。

文件分类与反编译阶段分开：

```text
InputClass: Dynamic | Static | Unknown | Malformed
Attempt: NotAttempted | Attempted | Cancelled
SourceResult: ValueTreeComplete | LegacyComplete | Incomplete | Unsupported
            | InvalidInput | InternalFailure | ResourceFailure
```

每个方法实际 route/stage 沿用 acceptance.md；暂未能索引的方法所在文件保留
unresolved-method-count，不能因为没有 method denominator 而从覆盖报告消失。
范围快照在每轮开始冻结，更新要另开快照；不能从目标分母移除失败文件改善成功率。

- 全量分类率：所有发现输入都有合法身份/类别/attempt 记录，不表示反编译成功。
- 通用新路方法率：ValueTreeComplete / 目标动态方法；同时报告无法确定方法数量的文件。
- record 完整率：entry、module、类、所有必需子函数都闭合的 record / 目标动态 records。
- profile/能力矩阵：单独报告历史版本缺口，不能由热门 profile 的大分母淹没。
- 语义验证率：实际具备独立 oracle/运行对照的范围；无源码批量样本不能凭语法通过宣称
  行为等价。工程 verifier 通过和外部验证证据分别记录。

不存在固定“超过某百分比就是全部覆盖”。G5 是完整审计；G6 只有当目标动态范围的
真实源码及结构门槛均满足且无未解释拒绝才完成。明确暂不支持的静态输入另列，不计成功。
不支持的动态 profile/操作仍是 backlog，不能靠重新定义“支持范围”从总目标中消失。

## 6. 后续能力任务 Cnn 合同

T00–T12 是通用主干落地计划，不可能预先穷举未来所有 ABC 组合。后续由全样本失败
聚合产生 Cnn 任务；按缺失语义能力归组，不能每个失败函数单开修补 writer。
每个 Cnn 在开工前必须提交具体设计，格式如下：

| 字段 | 必填内容 |
|---|---|
| 需求与证据 | 所有受影响 content/profile/op 统计，至少两个应用或明确缺样本 |
| 参考 | pinned ISA 组、格式、v7 runtime 实现位置及历史差异 |
| 前置能力 | T/C 依赖、输入证书、CFG/owner 条件 |
| 语义合同 | typed operands/results、正常/异常/恢复 effects、禁止假设 |
| 贯通改动 | decode → lift → ValueIR → proofs → plans → AST → printer/mapping 的缺失项 |
| 代码范围 | 明确文件与唯一责任人，不新增专用 record/method writer |
| 正反例 | 最小 synthetic、多个真实方法、版本/边界反例、留出集 |
| 复杂度 | I/E/U/P/K 维度、预算失败点、是否引入新的组合爆炸 |
| 发布标准 | receipt、回归不降、来源闭合、语义审查、暂停验证标记 |

顺序遵循用户已指定的 pinned `isa.yaml` group 顺序；同组先补缺失格式和语义，再补
输出表达能力。先前优先的 DEFINE*/constant object loaders 已实现能力不能在新路丢失。
函数/类结构 gate 和当前 T00–T12 主干依赖可以优先处理必要基础；改变组顺序须记录
明确依赖原因，不能仅因某单一样本好改就长期插队。

阶段算法：

```text
stream all valid dynamic inputs through current stages (when validation authorized)
aggregate typed failure signatures by profile + capability + missing proof
prioritize dependency prerequisites, then pinned ISA group order
freeze one Cnn contract and cross-application cases
implement through the common pipeline, not source text special cases
verify development + retained regressions + held-out groups
publish updated capability/coverage receipts; retain failures in the denominator
```

暂停验证期间可从既有清单/已知 blocker 设计任务，但不伪造新批次结论。
失败聚合使用索引/排序流，不对数百万方法做两两模式匹配。

## 7. 对主实施计划的强制补充

- T00 建全语料身份/分组/能力表骨架并引用已有分类证据，不只冻结 pasteboard。
- T01–T03 的最小支持域按 typed operations 明确，P1 仅用于选最小能力组合。
- T04–T10 每任务必须附跨应用方法和历史 profile 影响审计；没有证据不得声称普适。
- T11 不能假定 ArkUI record 形状，普通库/模块/匿名方法也必须复用 RecordTree。
- T12 审计两个 corpus 根的全体输入及所有可索引实体，不把 19/219 设为全局上限。
- G5 后仍有动态缺口时生成 Cnn，继续朝 G6 推进；不能宣布“设计完成所以覆盖目标完成”。

当前没有启动扫描/测试，也没有重写分类文件。所有新清单和工具变更由对应任务实施。
