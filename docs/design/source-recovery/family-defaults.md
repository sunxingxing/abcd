# 函数族默认参数迁移

## 2026-10-15：Round 71 直接 SSA 字符串默认值

直接 SSA 默认值候选现在保留可选的类型化 `PoolIndex`。它与现有数字等
常量使用同一个按参数顺序推进的发现过程；接受字符串后继续处理下一个
默认参数。没有改变默认分支/phi、入口逃逸和效果顺序证明，也不新增
opcode 邻接模板。此增量处理仍有 SSA 合流结果的字符串默认值；未扩展
无结果的已消除字符串初始化、cell-backed 捕获默认值或任意表达式默认值。

`DefaultValueProof` 独立核对原始 `ValueString` 的 pool 身份、String
值域、无输入/无效果/不抛出、结果及原始 phi 使用，并复核参数顺序。
`DefaultBody` 将证明过的字符串交给已有 `SignatureString` 通路，保留
原始操作来源。`TreeConstants` 用原方法的常量池解析实体和字符串，
pool 索引不进入可执行语法。签名验证独立核对解析后的常量、参数位置和
来源；ABI 原有的首个默认参数位置与 `.length` 等式保持不变。

所有新增持久字段由已有容器的 `sizeof` 计费；没有新索引或逐参数扫描。
发现和证明仍为 O(V+E)，常量池解析复用 O(N log N) 的排序/去重。
新测试 `string_defaults_test` 覆盖交替字符串/数值的 200/201/400/800
参数、精确 byte/work 边界，以及 pool、参数、顺序、生产者和签名变异。
实际 DevEco/es2abc fixture 与 Node 差分说明见
[string_defaults README](../../../tests/integration/fixtures/string_defaults/README.md)。

对 `explore69/explore69s` 的比较：只有 o13/3609 Logger 从拒绝变为
complete，两组为 521/620、561/620；已有 complete/partial 输出逐字节
不变。Logger 的构造器现在保留两个原生默认参数，`.length` 为 0。
Round 70 的另外三个 cell-backed 捕获默认值仍保持原拒绝。

## 2026-10-15：Round 70 length 拒绝诊断（未扩大准入）

以下是 HEAD `17b20ccd` 的 ABC-only 证据；未读取配对源码。表中的
“当前 length”是 ABI 检查前的树签名计算结果，不是已发布函数的运行结果。
创建点的 `length` 经 `FunctionContext::Length()` 取得；不是 CodeInfo
的物理参数槽数。`AbiBuilder::Build()` 按第一个默认参数的位置计算源码
length，再与创建元数据比较。这个等式没有错误，必须保留。

| 探索 record / 方法 | ABC length | 当前 length / 默认位图 | 原始默认初始化 |
| --- | ---: | --- | --- |
| o10/16048 Index / m16233 | 2 | 3 / `000` | 第三个形参在 PC16 写本地 cell；PC18 测该 cell；PC28 读外层 depth1/slot0，PC30 HoleGuard，PC38 回写本地 cell |
| o31/17804 FlowItemNode / m17994 | 2 | 3 / `000` | 第三个形参在 PC21 写本地 cell；PC23 测该 cell；PC33 读外层 depth1/slot0，PC35 HoleGuard，PC43 回写本地 cell |
| o31/20739 SwiperView / m20927 | 2 | 3 / `000` | 同上，PC21/23/33/35/43；是独立方法、独立捕获身份 |
| o13/3609 Logger / m3876 | 0 | 2 / `00` | 第一个形参缺省时 PC20 读字符串 pool84；第二个形参缺省时 PC35 取整数 65280 |

四例在两组 flags 下都以 `FunctionAbiError::kLength`（枚举值 7）拒绝。
前三例的 ABC 形参/局部调试信息分别给出 `elmtId, isInitialRender, name`
和 `elmtId, isInitialRender, data`；第三项是本地 cell 的名字，不可据此
把外层默认值 cell 合并为同一绑定。可表达的目标是第三个原生默认参数
读取**独立的外层绑定**。Logger 则是两个直接 SSA 参数的常量默认值，
不属于捕获 cell 组；这里只记录 pool 身份，不推测字符串内容。

### 为什么本轮停在证据阶段

当前两条证明不能直接拼接：

- `FindDefaultCandidates` 要求 undefined 比较对象是 `ParameterEntry`；
  前三例比较的是本地 cell 的读取。`FindCellDefaults` 可以找这种目标，
  但其初始化 RHS 和独立 `CellDefaultProof` 都仅支持常量/字符串。
- `DefaultValueProof::BuildCaptured` 已支持外层捕获默认值，但其前缀
  效果检查显式以 `!capture_` 限定本地环境分配和初始化的交换。
  不能只在候选发现器里新增 LexicalLoad/HoleGuard 分支就越过该限制。
- `DefaultBody` 将已迁移的操作及异常出口从残余体移出，保留原始来源；
  cell 路径的初始 store 保留，但输入解释为已完成默认初始化的形参。
  捕获入口 demand、`SignatureCell`、签名验证当前仅消费直接 SSA 的
  `CapturedDefaultBinding`。缺少同时覆盖外层读/guard 和本地参数 cell
  初始化的联合证书，不能伪造一个直接 SSA 的候选代替它。
- Logger 的第一条字符串默认值在直接 SSA 候选发现中未获支持
  （该分支要求单输入 literal/copy 或数值负号；`ValueString` 没有输入）；
  从第一个默认位置开始的发现因此停止，第二个数值默认值也未提交。

需要补齐的是**参数初始化与残余 body cell 的联合对应**，不是新的源语言
前提，也不是修改 `.length`。本轮按约定停止：没有修改生产代码、签名准入、
guard 或 ABI 等式，没有新增完成 record。

### 后续证明边界

1. 类型化区分原始实参入口、按序完成的源码形参、私有本地 cell、外层
   incoming cell；共享 allocation/use/cell 索引，不用邻接或固定寄存器
   识别。用原始边和数据流证明 undefined 分支及最终写入来源。
2. 对原生默认值提前到 body 环境分配之前，证明被跨过的初始化私有且
   不可观察，外层读与这些写入互不别名，guard 的异常完成仍归属于同一
   初始化事件。一般调用、早期闭包、handler 观察和 arguments 不能借此
   获得准入。后续参数仍处 TDZ；较早参数引用必须使用已初始化的身份。
3. 证书同时交给 residual 投影、家族需求重绑定和签名独立验证，分别
   重算 cell 写入、外层捕获、作用域遮蔽、原始事件归属和 length；不能
   仅向 `CellDefaultProof` 的操作白名单追加 load/guard。
4. 验收除当前三例外，覆盖跨调用重赋值、missing/undefined、左右顺序、
   较早/较晚参数、早期逃逸、TDZ、错误 cell/写入/guard 的变异，以及
   copy/块拆分和 200/201/400/800 预算边界。需要新的联合证明实现后
   才能声称这些验收已通过；本轮未实施这些行为测试。

本轮还原插桩并重建后的核对：完整构建成功，固定集串行 65/65；
`default|signature|function_abi|record_import_tree|parameter_plan|parameter_names|source_route`
定向 15/15；`cli|corpus|failure|_receipt|manifest` 扫描 63/64，唯一失败为
status.md 已登记的 `family_source_cli_test`。两组探索逐 record 对比
`explore69/explore69s`，仍为 520/620、560/620；record manifest 条目、状态
和已有源码字节均无变化。日志与命令在 `/private/tmp/r70/acceptance.json`、
`/private/tmp/r70/cohort.log`；ABI 临时观测为 `o10-abi.log`、`o31-abi.log`、
`o13-abi.log`。未新增行为测试，未跑留出集、全套测试或 RSS 基准。

下文保留早期迁移设计与当时状态，不能作为当前验收结果。

这是 T06/T07 集成缺口的实施约束，不是新增完成标准或已实现能力。
保留原计划完整签名、作用域、捕获、效果与 PC 映射验收。

## 实际阻塞

ABC-only 检查 pasteboard 7449：创建元数据 length=0，ABI 有一个形参。
原始 PC 0 分配词法环境；PC 10 保存 receiver，PC 14 保存形参；PC 16
读取该 cell，PC 21/24 比较 undefined 并分支，PC 26/31 将 null 写入 cell。
PC 56 创建子函数 7089，子函数捕获两个 cell。之后还有两个后代函数。
不能直接用现有纯 SSA `DefaultCandidate` 删除头部块：环境分配和 receiver
初始化仍有用途，形参 cell 的存储身份也必须保留。

GDB 确认当前失败是 `BuildFunctionAbi` 的 length 一致性检查：期望 0、实际 1。
它现被分类为 `FunctionAbiError::kLength`，不再混同参数写入、身份或数量错误。
不通过合成 `= undefined`、rest 包装、修改 length 或跳过 ABI 检查掩盖此缺口。

当前实现：`FindCellDefaults` 使用已验证 CellPlan 和 ParameterPlan，沿入口默认
分支链发现类型化候选；已接入 `BuildFileFamilyTrees`。真实 7449 报告一个候选。
`CellDefaultProof` 已独立检查控制边、比较极性、入口值使用范围、初始化 cell 身份和
前缀操作白名单，并保留需要迁移的原始操作清单。7449 的候选通过该 IR 检查。
cell 分支现已接入共享 DefaultBody 残余投影：保留环境和初始 cell 存储，迁移操作
保留原始来源并以残余空操作占位。全族先投影，再重建环境/cell/module guard 和
创建点捕获，最后共享构树、生成默认签名并复核语义/ABI/家族归属。
真实 7449 的四成员已全部构建条件 AST；回归规格未执行，源码发布尚未贯通。

## 实施顺序

1. 在默认值证据中区分 SSA 合流与词法 cell 初始化目标，使用 variant 与强 ID。
   通过已有环境/cell 索引追踪入口参数存储、测试读取、默认值存储与合流。
   不按名称、寄存器号、应用实体或打印后的代码匹配。
2. 独立验证默认值前缀：cell 来自本次局部分配；入口参数是被测值；分支只在
   undefined 情况执行；fallback 是已支持的语言常量。证明在默认分支合流前
   没有该 cell 的外泄、创建点捕获、调用观察、别名写入或额外读取。
   环境链、receiver 和其他参数的初始化不是可删除噪声。
3. 构造带签名证据的 residual IR：保留实际环境分配及 cell 身份，将该 cell 的
   初次存储输入解释为已完成源码默认初始化的形参；删除有证据覆盖的测试/默认
   写入并收缩对应边。禁止凭 graph revision 相同沿用原输入证书。
4. 重建环境与 cell 分析，在原创建点重新绑定整个函数族的 incoming/outgoing
   capture；重新验证模块 guard 映射。必须先完成全族投影和重绑定，再建父子 AST，
   避免父树携带投影前的 child revision。采用两遍处理而非边建树边修证明。
5. 从同一默认值证据构建原生签名、复核 residual tree 语义及 ABI。原始比较、
   分支和 fallback 的 PC 归属于签名来源；保留初始化的 PC 归属于函数体，
   不能丢弃或从生成文本推断。随后继续全族归属检查和统一 printer。

## 必须保留的边界

- 默认参数环境与函数体环境不同。仅在已证明的入口值/cell 捕获语义下迁移，
  不扩大到任意参数依赖、arguments、eval、任意表达式 fallback 或早期闭包。
- OOM/栈耗尽差异仅沿用既有版本化契约；应用异常、宿主调用和 cell TDZ 不豁免。
- 复杂度按图、使用边、cell 和创建边计量；共享索引，禁止逐参数遍历全图。
- 完整原始输入、residual IR、重建证明和输出树同时存在时必须统一预算。
- 暂停测试期间只写规格、生产构建及 ABC-only 检查；不宣称阶段验收。

## 验收规格

- 普通函数、方法和后续构造器使用同一规划；SSA 默认值既有路径不回退。
- 正例包含 captured 参数、receiver 并存、多个默认参数、200/201 规模。
- 反例覆盖默认前读/写/捕获/调用、外部 cell、错误 depth、不同 incoming 值、
  循环前缀、非 undefined 分支、额外副作用、陈旧 revision、漏失创建边。
- 实际 7449 的四成员必须全部到共享树并保持三个创建关系；完整 P3 的异常/async
  义务仍保留。另一个应用/profile 的同类输入用于泛化证据，不能仅调整这个样本。
- 源码同时保留正确签名与函数体效果，PC 映射可核查；未闭合义务不得发布 executable。

## Round 77 increment I: cell parameter and outer default joint evidence

`CellParameterDefaultProof` is original-IR evidence only. It distinguishes the
raw ParameterEntry, the initialized source-formal ordinal, the allocated local
parameter cell, and the incoming default cell. It does not mutate DefaultBody,
relax ABI length, allocate source symbols, or authorize publication.

The shared index verifies environments/cells and normal value availability,
canonicalizes copy identity once, indexes guard/branch/exception occurrences,
and builds entry dominance plus real normal postdominance. Discovery follows
cell and value identities. The independent verifier rebuilds those indexes and
checks the proposed records, the undefined edges, private prefix coverage,
exact guarded-read/write-back order, caller exception exit, and the body-entry
cell state. Restricted values cannot escape into body/phi/unrelated uses.
The parameter decision must dominate its body join as well as having that join
as its normal postdominator: an entry path bypassing initialization is rejected.
All paths in the initialization regions must converge without a back edge;
copy insertion and block splitting do not introduce new match branches.

Time is O((V+E+U) log(V+E+U)), scratch O(V+E+U+P). Region walks are disjoint,
then the entire prefix is visited once. Each cell access list is visited once;
there is no per-parameter whole-method traversal. All variable buffers and
retained proof records are charged before allocation, with old and new scratch
charges retained conservatively during independent verification.

Production runs this analysis only after the existing normal-family ABI length
refusal, using the remaining budget. The first refusal and resource class are
unchanged, even when the additional analysis itself cannot establish a proof.
`cell_parameter_default` (route) / `source_cell_parameter_default` (record-entry)
contains a bounded status, exact original/working locations and distinct storage
identities. Incoming depth is relative to entry, not lexical instruction depth.
There is no new source-language premise and no change to the capture requirement
lattice. This certificate does not yet establish native parameter scope or
fulfil the incoming binding's source TDZ obligation: both remain increment II.

Tests: cell_parameter_default_test independently checks forged certificates and
changed graphs (identity/order/guard/read/write/exception mutations), copy and
block variants, entry-ID renumbering, and 200/201/400/800 exact bytes/work budgets.
cell_parameter_defaults_cli_test uses a real es2abc fixture and checks both
contracts and manifest/abc_semantic consistency. No generated-source differential
is claimed while publication is gated. The three corpus targets retain kLength;
acceptance results and the complete manifest-change list are reported in the
Round 77 delivery rather than inferred from proof status.

Increment I acceptance (2026-10-15, uncommitted implementation): full keep-going
build passed; new tests 2/2, fixed source-regression 65/65, default/signature/ABI/
parameter/capture targeted tests 23/23. CLI/corpus/failure/receipt/manifest sweep
was 73/74; only the already registered family_source_cli_test owner/origin arena
assertion failed, unchanged. Against explore75/explore75s, completion remained
521/620 and 567/620, with zero status or text changes. The only manifest changes
in each flag set were the new diagnostic for o10/16048 (m16233), o31/17804
(m17994), and o31/20739 (m20927), all verified with one third-parameter proof;
the original FunctionAbiError::kLength cause remained. All 3,580/3,718 non-manifest
per-ABC export files were byte-identical. Full logs and comparison exports are
under /private/tmp/r77; no holdout evaluation was run.

## Round 77 increment II: signature ownership and initialized-formal commits

The additional projection is selected only when the existing direct and cell
constant projections do not cover the prefix. `DefaultBody::BuildCellParameter`
seals the joint original-IR proof, retains the original graph for independent
verification, and builds the residual before family Rebind. It neither changes
existing successful default projections nor allocates names while proving.

`SignatureCapture` records the original read site, lexical address and incoming
storage. `SignatureCell` resolves that exact site to incoming storage; it never
uses the local parameter cell merely because its slot number matches. Each
`InitializedParameterCommit` records the source-formal ordinal, initialized entry
value, original initial store and allocated destination. The residual keeps this
store, removes the conditional read/guard/write-back from execution, and retains
their origins as signature placements. Signature exception records retain the
original guard exits and caller propagation. Copy identities and split arms are
handled through def-use and reachability, not instruction adjacency. Dead pure
arm blocks are tombstones attached to the preceding residual arena; escaping
values, retained effects or lost phi inputs cause refusal, not silent erasure.

The family entry demand is Guarded. Only an occurrence matched by the sealed
signature certificate can use that access proof; other accesses and descendant
requirements still join normally, Language dominates, and Defined stays strict.
The rebuilt working storage is passed to capture rebinding. Signature checking
independently rebuilds original and residual storage, verifies the initialized
entry consumed by each body store, parameter order, distinct outer binding,
actual initializer symbol, original guard exit and removed-site ownership.
It does not assume original and residual CellIds are equal.

Time is O((V+E+U) log(V+E+U)); all indexes, worklists, original/residual arenas,
proof records and transient replacement storage are charged before allocation.
The retained original graph costs additional linear memory within the existing
record cap; it is not borrowed without an owner charge.

Publication remains refused with DefaultSignatureError::kCellParameterRecovery
(value 9), after the signature checks. RecoveryFlow currently defines ordinary
parameters unconditionally at entry; it does not yet model a conditional native
default evaluation, its throwing guard and its initialized-parameter commit.
Increment III must connect those ports to original parameter initialization and
then prove ABI length and final recovery. Passing a residual-body check cannot
stand in for that proof. No native-default source, Node output differential or
emitted-signature recompilation is claimed by increment II.

Tests extend cell_parameter_default_test with residual ownership, explicit
commits, independent signature construction/verification, wrong parameter-symbol
and placement mutations, a forged residual write and moved exception handler.
Both missing and provided arms have block-split variants. 200/201/400/800 cases
check exact bytes/work boundaries. The real es2abc fixture still captures c in
a body closure; its CLI test now expects the recovery gate instead of kLength,
while asserting the same distinct original storages and named HoleGuard.

Increment II acceptance (2026-10-15, uncommitted): full keep-going build passed,
fixed set 65/65, extended tests 2/2, default/signature/ABI/parameter/capture/family
and source-route targeted tests 22/22. The CLI/corpus/failure/receipt/manifest
sweep passed 73/74; only the registered family_source_cli_test owner/origin arena
failure remains, with its assertion unchanged. Against explore77/explore77s,
completion stays 521/620 and 567/620. Only o10/16048 m16233 and o31/17804 m17994,
o31/20739 m20927 change their first refusal to the explicit recovery gate (9),
with verified signature diagnostics under both contracts. All 3,580/3,718
non-manifest per-ABC artifacts remain byte-identical; there are no new complete
outputs. Logs, the six-row target table and comparisons are in /private/tmp/r77b.


### Round 77 increment III: conditional invocation recovery

Cell-backed captured defaults now retain their immutable original input alongside
the initialized-formal residual. `OriginalParameterDefaults` records parameter,
outer binding, actual signature expression and the sealed raw-IR certificate.
`RetainParameterDefaults` repeats signature/source verification before retaining
this evidence; recovery independently rebuilds environments, cells and parameter
facts and checks the certificate on each projection.

RecoveryFlow gives each certified default an argument entry, undefined decision,
provided commit, missing-path commit and initialized-formal definition. Only the
missing edge evaluates the real signature expression. Its guarded capture read
has the caller exception exit, outside all body handlers. The two successful
paths converge before the residual's initial cell store. No expression is
invented for the raw argument or the merged formal. Ordinary signatures keep
the existing entry graph. The flow verifier checks exact edge sets, unique
ports, actual binding/storage identity, signature expression identity, parameter
order and the continuation into the body. The raw source/residual verifier
continues to prove the initial store consumes that initialized formal.

All retained input and scratch are charged before allocation (including the
shared input's allocator control block). Indexing is linear apart from the
existing dominance/storage proofs, bounded by O(N log N). Tests cover graph-edge
mutations, 200/201/400/800 parameters and exact byte/work budgets. The real
es2abc/Node fixture covers reassignment, per-call cell isolation, provided-value
bypass, early TDZ, closure writes and length 2. See the fixture README for the
compiler export-alias syntax adaptation and the scope of round-trip acceptance.

Validation of this uncommitted increment (2026-10-15): full regression build
passed; fixed set 65/65; the two cell-parameter tests 2/2; default/signature/ABI/
parameter/capture/family/recovery-flow targeted tests 23/23. The prescribed
CLI/corpus/failure/receipt/manifest sweep passed 73/74, with only the already
registered `family_source_cli_test` (`node escaped its owner or origin arena`).
Both flags were compared with explore77b/explore77bs: 524/620 and 570/620,
respectively. Only o10/16048 m16233, o31/17804 m17994 and o31/20739 m20927
became complete; all six new outputs passed `node --check`. No existing status
regressed and no existing text changed; all 3580/3718 old non-manifest artifacts
were also byte-identical. Evidence is in `/private/tmp/r77c/`, including
`acceptance.json`, the `*-final.log` files and `final-comparison/`. No holdout,
full-suite or RSS benchmark was run. No commit/push/stash was performed.
