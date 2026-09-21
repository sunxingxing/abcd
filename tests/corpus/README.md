# 批量 ABC 样本与分类记录

本目录存放测试语料清单和分类证据，不包含生产引擎代码，不复制外部大体积 ABC。

## 语料来源

| 集合 | 实际路径 | 当前用途 |
|---|---|---|
| 源码对照 | `/home/rabbit/sample/abc_with_code` | 5 个 ABC 及转换后 TS，用于有对照的语义验证 |
| 批量样本 | `/home/rabbit/sample/abc` | 895 个 ABC，共 1939093000 字节，用于批量覆盖、健壮性与性能验证 |

路径使用单数 `sample`。清点时包含批量目录内的 `abc/` 和 `framework/` 子目录，
未发现需要跟随的符号链接；当前已完成类型识别，结果见下文，但尚未进行反编译验证。
用户说明批量集合包含静态 ABC，当前不支持静态字节码反编译。

## 持久记录文件

首次执行验证时创建并维护 `tests/corpus/abc-index.json`，对每个文件记录：

| 字段 | 含义 |
|---|---|
| schema_version | 分类清单格式版本 |
| corpus_id、relative_path | 语料集合身份与相对路径，可映射到当前机器根目录 |
| size、sha256 | 被分类文件的大小和内容指纹，不能只用路径作为缓存键 |
| kind | 动态、静态、未确定或损坏；未检测不能伪写为动态 |
| abc_version | 已有足够字节且可解释时的头版本数值数组，否则为空 |
| detector_revision | 识别规则版本及适用的格式基线 |
| evidence | 支撑判断的格式/语言标记、规则 ID、必要偏移及观测值 |
| checked_at | 最近确认时间，不作为分类正确性的依据 |

已生成真实分类清单：两个集合共 900 个文件、894 个唯一内容，402 个动态、498 个静态，
未知和损坏均为 0。记录包含 SHA-256、数字版本和证据；不是空壳或猜测清单。
`checked_at` 使用 UTC Unix 秒；`profile` 仅表示头部布局识别，不代表反编译支持认证。
文件存在但还未扫描、识别结果未知、数据损坏分别表达，不能合并为一个“静态”类别。

内部采用 `AbcKind`、`SkipReason` 和结构化证据；JSON 标签只用于序列化。
分类与“当前是否支持这个动态版本”分开保存。动态版本不支持应记录 UnsupportedProfile，
解析失败应记录具体错误，不能写成 Static。确认 Static 时，批量运行记录
`SkipReason::StaticAbc`，后续同指纹、同识别规则的运行直接跳过反编译。

## 复用与失效

验证流程先建立清单索引，再逐文件查询，避免 895 个路径之间的反复线性匹配。
同内容文件可以共享分类证据，仍保留各自路径，报告同时提供文件数和唯一内容数。
首次扫描以有界并发流式计算 SHA-256，不一次性把整个语料读入内存。

后续运行默认流式校验内容指纹并复用分类，重新读取用于 hash 不等于重新识别字节码。
若语料采用已验证的不可变快照，可复用快照指纹；仅凭文件名、mtime 或大小相同
不足以保证内容未变化。同路径被替换、指纹不同、识别规则发生相关变化时重新判断。
记录使用具体 detector revision，不能把每次引擎构建号改变都当成分类规则变化。

首次识别需依据可靠的格式与语言证据，不能因为文件位于 `framework/`、名字像系统库、
后缀为 `.abc` 或某个动态 reader 打不开，就断言它是静态格式。
证据不足的结果保留为未确定；新增证据或识别规则后再重试。

批次协调者合并结果并原子更新清单；并发验证会话需要文件锁或等效的合并约束，
避免互相覆盖记录。取消或崩溃不得留下看似完成的分类结果。
实现以原子 rename 作为提交点；提交前失败保留旧文件，提交后目录 fsync 失败报告
`kCommittedNotDurable`，表示替换已发生但持久性未确认，不宣称旧文件仍被保留。
格式未知时已完成的内容指纹仍可保存，但分类证据必须保持真实状态。

## 报告与验证要求

每次批量报告分别列出：发现文件、唯一内容、动态已验证、动态未支持、静态跳过、
未确定、损坏、失败及取消。静态跳过数不算作动态反编译成功数，不隐藏于总体通过率。
无源码的动态样本可以证明某些解析/结构不变量通过，不能因此宣称行为等价。

需要验证：同指纹静态结果不重复识别；同路径内容替换会失效；规则变化会重判；
未知/损坏不被误跳过；重复内容共享证据；并发更新和中断恢复不丢记录。

已验证的具体范围和实测结果见
[分类验证记录](../../docs/validation/2026-09-22-corpus-ingestion.md)。第二次完整扫描
对 900 个文件全部复用分类，分类器调用为 0；静态类型仍不计入动态反编译成功数。

## Record Index Evidence

`record-index.json` is the generated per-file metadata-index validation report,
not another classification cache and not evidence of successful source recovery.
Its validation section pins the implementation and classification-manifest hash.
Static entries are skipped from cached classifications after content rehashing.
See the [validation record](../../docs/validation/2026-09-22-record-index.md)
for reproduction and limitations.

## Method and Normal Flow Evidence

`normal-flow.json` contains the generated per-file method-encoding and normal
control-flow report, pinned to an implementation and manifest digest. All
5,486,254 dynamic methods passed these stages. Static exclusions are not source
decompilations, and normal-flow coverage is not behavioral-equivalence evidence.
See [normal-flow validation](../../docs/validation/2026-09-22-normal-flow.md).

`exception-dispatch.json` extends validation through the pinned dynamic-runtime
handler selection map and checks its boundaries against normal blocks. It pins
the implementation and manifest digest independently of the earlier report.
Protected instruction counts are conditional routing coverage, not may-throw
or source-equivalence evidence. See
[exception validation](../../docs/validation/2026-09-22-exception-dispatch.md).
