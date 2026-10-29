# 当前状态

更新时间：2026-10-29。本页只记录当前事实、阻塞项和证据索引。
2026-09-25 至 2026-10-12 的流水记录原样归档于
[history/status-2026-09-25-to-2026-10-12.md](history/status-2026-09-25-to-2026-10-12.md)；
更早的证据见 [progress.md](progress.md)。

## async 完成协议：经寄存器拷贝的 promise（2026-10-29，cc；AsyncCompletionCheck 阻塞）

- `AsyncCompletionCheck::kPromiseIdentity`（约 640 条）：优化构建在 try 开始处把 promise 拷贝到
  另一个寄存器（`mov v1, v0`），外层完成 catch 用这个拷贝 reject（`asyncfunctionreject v1`）。
  原有检查要求 resolve/reject 的寄存器就是 promise 寄存器。
- 修复：`PromiseCopy` 证明拷贝在 catch 可能进入的每个点上都持有 promise：它是完成 catch 保护区
  的第一条指令；区内每次写它都再次从 promise 拷贝，除这些拷贝外区内没有任何指令读它；promise
  本身在区内不被改写；普通控制流只从区的起点进入。于是这些拷贝属于协议，与 resolve 一起从规范化
  后的函数体中删除；指向被删拷贝的分支（`RebaseBranches`）和 try/handler 边界
  （`RebaseAsyncHandlers`）都移到下一条原始指令。
- 证据：
  - 新 fixture `promise_copy`（default、O0）Node 差分：resolve、内层 catch、try 之外的 reject、
    try 边界上的拷贝；植入的变异能被检出；
  - big.abc 8,216 → **8,367**（regression 构建，309 s，峰值 RSS 237 MB），零回退、零文本变化，
    新增输出全部通过语法检查；620 用例 cohort `csr120/def` 与 `csr119/def` 逐字节一致；全量 ctest
    1005 项，失败 34 项，全部属于已知失败清单。
- 后续：更一般的形式（拷贝链经过多个寄存器、寄存器被复用、resolve 与 return 之间也有拷贝）需要
  must/may 值分析；这些函数随后停在 await 协议内部插入的 spill 拷贝上，单独处理。

## phased 路由上的条件值与逻辑值（2026-10-29，cc；CallPlanError 阻塞）

- 剩余的 `CallPlanError::kReference` 中很大一部分在 phased 路由上（async 函数、带 try 的函数）：
  `o.set("k", t.p ? "a" : "b")`、`o.def("n", f() || x)`、`o.set("k", t ?? "x")`。O0 构建能通过，
  优化构建不能。phased 选择发现（`phased_selections.cc`）有两个缺口：
  1. 分支本身就是携带 phi 拷贝、直接进入 join 的裸边时，`arm()` 立即拒绝；
  2. phased 图保留寄存器拷贝，phi 输入是生产者结果的已验证别名（`lda.str "a"; sta v4`），
     而比较只对字面量分支解析别名。
- 统一修复（与普通路由的裸边规则相同）：分支的 tail 改为可选，裸边分支只转发已经可用的值
  （与原有"惰性分支"共用 `inert`）；路径绑定对裸边要求分支边直接进入 join；所有分支都按已验证
  的别名解析 phi 输入。
- 文本变化：phased 路由上原先输出为 if/else 加 phi 变量的条件值，现在折叠成 `?:`、`||`、`??`。
  big.abc 中 708 条已完成记录的文本因此变化，逐条自动分类：每一处差异都是删除 if/else 赋值，
  全部通过语法检查；抽查（如 `X = ObservedV2(X) || X`、`getShared() ?? new LocalStorage()`）
  语义一致。620 用例 cohort 中 7 条文本变化，逐条对照原始源码确认等价。
- 证据：
  - 新 fixture `phased_selections`（default、O0）Node 差分：getter 顺序、receiver 绑定、
    被测值只求值一次、只求值被选中的分支、try 内抛出、await 之后的使用；植入的变异能被检出；
  - `guarded_reference_transports_source_test` 原先把优化构建固定为调用引用拒绝；现在它发布，并走与
    O0 相同的 13 种模式 Node 差分与变异检查（比较次数 26 → 52）；
  - big.abc 8,147 → **8,216**（regression 构建，297 s，峰值 RSS 230 MB），零回退；文本变化 708 条
    即上述折叠；620 用例 cohort `csr119/def` 与 `csr118/def` 完成数一致、7 条文本变化如上；全量
    ctest 1004 项，失败 35 项：34 项已知，另一项正是 `guarded_reference_transports_source_test`
    的旧固定，已改为差分。

## 优化构建中的单层 for...of（2026-10-29，cc；CatchRegionError 阻塞）

- `CatchRegionError::kNormalEntry`（966 条）：优化构建里连最简单的 `for (const x of xs) f(x)` 也被
  拒绝。es2abc 的优化器会改写 O0 中 iterator close 的几个部分：
  1. 删掉 close 自身 try 的死 catch 存储（`sta vN`）。handler 的声明长度变为 0，起点同时是
     close 正常完成后共享的 rethrow（`lda exc; throw`），普通边于是跳进了 handler。
  2. 删掉死的 `done = true` 存储；或者寄存器分配把它写到另一个寄存器，再经过拷贝让抛出路径的
     handler 读到。
  3. 在循环体里加入寄存器 spill 拷贝（`mov v6, v3`），把 close 状态带进 handler 寄存器。
- 统一修复（全部是对已有证明的推广，没有新增形状特例）：
  - `ExceptionRegions`：任何普通弧都会到达的 handler 起点也视为 join，即空 catch `[b, b)`，
    与原有"从入口正常可达"的规则一致（原规则看不到 handler 内部的普通弧）。`original_flow`
    的复核同步接受空 handler 区间，以及只作为非活跃区域出现的零长度保护区间。
  - `close_entries`：guard 的 `true` 存储改为可选。找不到时取最近的支配分支：它测试一个寄存器
    支撑的布尔值，并在该值为真时绕过读取；如果分支与读取之间有字面量 `true` 存储（可以写到别的
    寄存器），就记为 guard 的 write。状态证明不再要求同一个寄存器，而是沿 SSA 拷贝与 phi 反向
    追踪：遇到的每个布尔字面量要么是支配首次 advance 的 false 初始化，要么是协议拥有的 close
    存储（区分"经累加器存入的字面量"与"寄存器之间的拷贝"）。存储之后状态位于被写入的寄存器，
    所以"存储与读取之间不得改写状态"按该寄存器检查（原寄存器可能已被复用）。`CompletionGate` 与
    `DerivedClose` 的 write 改为可选，并携带状态寄存器。
  - `for_of_evaluation`：协议区域的 catch 值即使没有使用，也由该协议认领（否则会成为找不到
    catch 作用域的 catch 参数）。在没有 completion port 图时，先按原规则建计划；只有原规则以
    correspondence 拒绝时，才重试并吸收循环体中的纯寄存器拷贝（其结果已被反向活性证明不可观察）。
    这样已有计划逐字节不变。
- 仍然拒绝：嵌套 for-of（外层状态被跨块 spill，并有跳到外层 try 末尾的边）、数组解构
  `for (const [k, v] of m)`（模式内部还有一个迭代协议）、外层 try 包住的 for-of、无条件 `break`
  （走到 completion port 的激活检查），合计约 300 条。`exception_throw_domain_source_test` 与
  `isa_recovery_variants_test` 中这些仍被拒绝的优化构建 fixture 改为固定新的失败原因链（原先停在
  catch 区域阶段，现在走得更远）。
- 证据：
  - 新 fixture `optimized_for_of`（default、O0）Node 差分：值、`break`、`return`、continue 形式、
    抛出时的 iterator close（自定义可迭代对象记录 `next`/`return` 的调用顺序）、不可迭代对象的
    错误类型；植入的变异能被检出；
  - big.abc 7,971 → **8,147**（regression 构建，290 s，峰值 RSS 241 MB），零回退、零文本变化；
  - 620 用例 cohort `csr118/def` 与 `csr117/def` 逐字节一致；全量 ctest 1003 项，失败 34 项，
    全部属于已知失败清单。

## await 协议：continuation 不重新加载结果，throw-mode 常量来自寄存器（2026-10-29，cc；AwaitRegionError 阻塞）

- `AwaitRegionError::kUnsupportedShape`（约 1,030 条）：`FindAwaitRegions` 要求 resume-mode 分支
  之后的那条指令是 `lda result`。es2abc 在结果寄存器能被直接消费时会省掉这次重新加载（`x += await a`、
  `f(await a, 2)`、`(await a) + b`），continuation 就成了下一条普通指令。
- 修复：continuation 在所有下游消费者中本来就是一条保留的普通指令（`retained`，并且是这个 await
  的私有入口），结果寄存器已经由 result-store 与 rejected-load 两步确定。因此去掉"continuation 必须
  是 `lda result`"这一形状要求，其余协议检查不变。
- continuation 之后 VM 累加器里仍是模式比较的结果，所以 continuation 只允许覆盖累加器，不能读取
  （`lda result` 这种原有形式满足这一点）。
- throw-mode 常量：优化构建把常量 `1` 提升进寄存器，协议里用 `lda vX; eq` 比较（凡是函数里出现
  字面量 1 的都会这样）。`FindAwaitRegions` 现在接受这种形式，前提是能证明 vX 在每条路径上都持有
  整数 1：在普通边与 resume 边（挂起会保留寄存器）上做前向必然分析，只跟踪候选寄存器（最多 63 个）
  与累加器；handler 入口和未知写入一律视为不确定。每个块的状态只会丢位，所以至多重新入队 65 次。
  经过未跟踪寄存器的拷贝链会被保守拒绝。
- 证据：
  - 新 fixture `await_continuation`（default、O0）Node 差分，包括被拒绝的 promise 与 throw-mode
    寄存器形式（`f(1, await a)`、`if (await a)` 等）；植入的变异能被检出；
  - 新单元测试 `await_throw_mode_test`：字面量形式、被证明的寄存器形式，以及反例（别的常量、
    未知寄存器、某条路径覆盖了寄存器、读取累加器的 continuation）；
  - `AwaitRegionError` 从 1,031 降到前 8 名之外。
- 本条与下一条（成员调用参数）一起验证：big.abc 7,291 → **7,971**（regression 构建，176 s，
  峰值 RSS 221 MB），已完成记录零回退；文本变化 8 条，正是上述极性错误的改正；620 用例 cohort
  `csr116/def` 与 `csr115/def` 逐字节一致；全量 ctest 1002 项，失败 34 项，全部属于已知失败清单。

## 成员调用参数中的条件值与逻辑值：裸边与仅含寄存器移动的分支（2026-10-29，cc；CallPlanError 阻塞）

- `CallPlanError::kReference`（1,323 条，其中 1,114 条是 `kCrossRegionValue`）：形如
  `o.m("k", a ?? "")`、`o.m(c ? 1 : 2)`、`o.m(a || b)` 的调用里，方法引用 `o.m` 与调用之间
  隔着一个条件区域。方法引用不能被快照（会丢失 `this`），所以必须把这个菱形折叠成实参表达式。
  现有的选择表达式机制（`FindSelectionExpressions`）已经覆盖这种情况，但要求每个分支都有
  自己的块。优化构建里不满足这一点：一个分支只是从分支指令直达 join 的裸边，或者只含一条被删除
  的寄存器移动；phi 输入是形参、更早的值或被提到函数开头的常量。
- 统一修复：
  - `SelectionRegion::evaluate` 改为可选（`keep` 本来就是可选）。条件路径遍历把裸边视为一个
    没有块、也没有操作的分支，`closed()` 只接受已经可用的输入（保留快照，或常量 leaf）。
  - 裸边分支没有块可以容纳保留的赋值语句，所以只在合并值唯一地作为未满足的成员 Reference 的
    实参时才接受。`return a ?? "x"` 等其他消费者保持原来的 if/else 形式，文本不变。
  - tree flow 在决策的条件块上消费出边时，跳过该区域自己选择的 keep/evaluate 边（随后由
    `LogicalEdges` 选择）。
  - **修复一个已有的语义错误**：`ProjectSelectionLogicals` 对没有 decision 链的条件区域固定按
    "consequent 在分支的 taken 边上"计算真值域，而发现阶段只在正向测试（`jnez` 类）时把 consequent
    放在 taken 边上；`jeqz` 类测试的 consequent 在 fallthrough 边上。于是 `x || ""` 被发布成
    `x && ""`。现在按分支谓词确定 consequent 所在的边。已提交基线里有 8 条记录发布了这种错误
    输出（如 lottie `AnimationManager`、`LayerInterface`），这次全部改正，共 53 行 `&&`→`||`。
  - 消费者只有 function_tree、tree_flow、logical_evaluation 中的几处，现在都对缺失的
    `evaluate` 加了保护；phased 选择总是有 evaluate。
- 证据：
  - 新 fixture `selection_arguments`（default、O0）Node 差分：getter 的求值顺序、`this` 绑定、
    只求值被选中的分支、实参调用先于 `??`；植入的 5 个变异都能被检出。两种构建都输出
    `$o.m($a ? 1 : 2)`、`$a || $b`、`$a ?? "high"`；
  - `joint_selection_source_test` 与 `nullish_call_values_source_test` 原先把优化构建固定为拒绝；
    现在 6 个优化构建用例改为发布，并走完整的 Node 差分与变异检查（`(x && y) || z` 在优化构建下
    暴露了上面的极性错误）；仍有 4 个精确的引用拒绝；
  - big.abc 新增 288 个输出语法检查全部通过；`kCrossRegionValue` 的引用拒绝从 1,114 降到 747。

## 默认参数：调用入口的环境存储与提升的字面量（2026-10-29，cc；DefaultBodyError 阻塞）

- `DefaultBodyError::kProof`（1,399 条）分为三个通用缺口：
  1. 箭头函数捕获 `this`/`new.target`/函数自身时，es2abc 在默认值测试之前把这些调用入口存进
     新环境。三处默认值证明（字面量 `DefaultValueProof`、cell 默认值 `CellDefaultProof`、
     cell 参数默认值）只接受形参与 receiver 的存储，callee 与 `new.target` 被拒绝（566 + 495 条）。
     统一规则：已证明的调用入口都不可变，只拒绝未证明的 VM 槽（`UnknownEntry`）。
  2. 优化构建会把常量默认值（数值、字符串、布尔、null）提到 undefined 测试之前，缺失分支只剩
     寄存器移动。`FindDefaultCandidates` 现在接受这种形式：头块中位于比较之前、唯一使用是合并
     phi 的字面量拷贝或 `ValueString`。证明侧只把这个初始化器的所在块从 arm 放宽为 header（要求
     merged 且位于比较之前），其余检查不变。环境前缀计数允许前面有被提升的字面量。
  3. 余下 272 条是调用表达式作为默认值（`p = f()` 等），需要参数表达式树支持调用，留作后续工作。
- 证据：
  - 新 fixture `entry_cell_defaults`（default、O0）Node 差分，含 `length`、`new.target`、
    `this` 捕获与提升的字符串、数值、布尔默认值，植入的变异能被检出；
  - big.abc 6,863 → **7,291**（regression 构建，147.6 s，峰值 RSS 200 MB），已完成记录零回退。
    16 条已完成记录的文本有变化，都是原先在函数体里用 if/else 处理的提升默认值，现在恢复成签名
    默认值（如 `constructor($arg0 = "MyApp", $arg1 = 65280)`），`length` 不变；
  - 620 用例 cohort `csr115/def` 与 `csr114/def` 逐字节一致；
  - 全量 ctest 999 项，失败 34 项，全部属于已知失败清单。
- 下一个最大阻塞是 `CallPlanError::kReference`（1,323 条）：方法引用 `o.m` 与调用之间隔着条件区域
  （`o.m(a ?? "x")`、`o.m(c ? 1 : 2)`、`o.m(a || b)`）。值树路由把条件值写成 if/else 加 phi 临时
  变量，而方法引用不能被快照，需要把只含表达式分支的菱形在使用处折叠成条件表达式或逻辑表达式。

## phased creator 的类成员安装（访问器等）（2026-10-29，cc；用户指定的优先阻塞 3）

- `CaptureEvaluationError::kCreation`（1,298 条）：phased creator（带 try 的函数、ArkTS
  phased 模块入口）中的类访问器（`get`/`set`、静态访问器）由 `definemethod` +
  `definegettersetterbyvalue` 在 literal buffer 之外安装。`ClassCreator::Definitions()`
  对 phased 返回空集（"no exception-port proof"），方法因此被当作普通子函数绑定后拒绝。
- 统一修复（与静态初始化器采用同一规则）：
  - 抽出共享的 `CallerExits`：op 只抛给 caller，或抛给原样转发捕获值的 handler
    （环境清理）。`FindClassInitializerCalls` 与 `FindClassDefinitions` 都使用它。
  - `FindClassDefinitions` 支持 phased 图：producer 取拷贝根；方法值的"恰好被定义消费
    一次"按拷贝传递计数；访问器缺失一侧的 `undefined` 允许经纯拷贝物化；定义与方法
    创建必须满足 caller 出口条件。
  - `ProjectClassDefinitions` 把纯拷贝视为透明（拷贝不算观察者，它的使用才算）；
    拷贝本身仍是求值 alias。
  - `PhasedClassSites::definitions` 经 `ClassCreator::Definitions()` 传给投影、phased
    求值计划与 `ResolveDefinedMemberContext`（occurrence 按原始 PC 查找）。
    `ClassCreator::FindSite` 在 phased 下按原始 PC 查找成员创建。`RootPoint` 以
    `BindExceptionCreation` 绑定 phased 成员方法。
  - 位于本地 try 内的类仍拒绝。
- 证据：
  - 新 fixture `phased_accessors`（default、O0）Node 差分（含描述符检查），植入的
    变异能被检出；
  - big.abc（regression 构建）6,457 → **6,863**（`tests/corpus/big-abc-baseline.json`，
    133.5 s，峰值 RSS 173 MB），已完成记录零回退、零文本变化，语法错误为 0；
  - 620 用例 cohort `csr114/def` 与 `csr113/def` 逐字节一致；
  - 全量 ctest 998 项，失败 34 项，全部属于已知失败清单。

## 子函数继承的模块初始化状态：提升前奏与 phased creator（2026-10-29，cc）

- 子函数对 local 模块绑定的无 guard 读取，只有在 slot 属于"创建时已初始化"集合时才认证。
  两个通用缺口，导致 `SemanticProofError::kInternalValue`：
  1. **提升前奏**：es2abc 把所有函数声明连成一段直线前奏
     （`definefunc; stmodulevar; definefunc; ...`）。先创建的函数读取后定义的函数时，
     slot 不在创建时集合里；但在第一条可能执行用户代码的指令之前，这个函数根本不可能
     被调用。
  2. **phased creator**：phased 父函数（包括 ArkTS 的 phased 模块入口）只把自身的
     entry 集合传给子函数，从不计算创建时的状态；例如 TS 的 `var`（前奏里
     `ldundefined; stmodulevar`）对模块里的类与函数始终不可见。
- 统一修复：
  - `ExceptionCreationModuleStates` 在 ExceptionFlow 上用同一个初始化求解器（所有
    弧，store 在 Commit 生效）计算每个 creation Before 点的状态，起点为 creator 自身的
    entry 集合。读取 phased 函数时计算一次，`FilePhasedFunction::InitializedModulesBefore`
    据此回答。family reader 与 `VerifyFamilyWrites` 的 inherited 边改为使用创建时集合。
  - 两个求解入口都按同一规则扩展前奏：从函数创建往后的连续段里只允许函数创建、
    module store、纯 scalar 移动与 no-op，且都不会抛向本地 handler；其中的 store 计入
    该创建的集合。
  - 修正 phased 入口 `EntryDeclarations` 的支配校验：phased 图改用
    `VerifyPhasedValueFlow` 的支配树（原来调用了只适用于 normal 图的 `VerifyValueFlow`）。
- 证据：
  - 新 fixture `module_creation_states`（phased 与 normal 两份源码，default 与 O0）Node
    差分，包括在后续赋值之前于模块求值期间调用，植入的变异能被检出；
  - `module_initialization_test` 的 BeforeStore 按前奏规则更新，并新增前奏被打断的负例；
  - big.abc 5,405 → **6,457**（+1,052；regression 构建，含 phased 前奏规则），零回退、零文本变化，
    语法错误为 0；cohort `csr113/def` 零变化；全量 CTest 997 项，失败的 34 项都在已知清单内。

## 自引用的类：后代与 phased creator（2026-10-29，cc；用户指定的优先阻塞 2）

- es2abc 为自引用的类（成员或静态成员写类名）建立类绑定的词法槽：先 class 创建，
  再 `stlexvar`，再调用静态初始化器，整段由环境清理 handler 包住。原先只有 record
  根（normal 模块入口）支持这种形态：
  - phased creator 因缺少 `ClassSelfOrder` 而拒绝（`ClassInitializerError::kFlow`，2,229 条）；
  - 后代 creator（普通函数里的类、phased 入口的类）的类体会捕获该槽，一律以
    `kClassDeferredDeclaration` 拒绝。
- 统一修复（复用已有证明，不新建机制）：
  - `ClassSelfOrder::Build` 拆成按 `ClassCreator` 的核心与 normal 专有的
    LocalAccessPlan 前缀；phased creator 只发布 store 证书。receiver、callee 与
    store 的值都按 `CopyRoot` 比较。`FindClassSelfStores` 对 phased 图使用
    `VerifyPhasedCellPlan`。
  - `family_file` 为 phased creator 构建 self order（`PhasedSelfOrder`），经
    `ClassCreator::SelfOrder()` 传给投影与 phased 求值计划。
  - 类体 root capture 中的 deferred 声明，像普通子函数一样交给创建它的 family 成员
    （`MarkDeferredCells`），由该成员声明（与 record 根的做法相同）。因此删去后代
    路径的 `kClassDeferredDeclaration` 拒绝。
- 证据：
  - 新 fixture `self_classes`（函数与 phased 入口两份源码，default 与 O0）Node 差分，
    植入的变异能被检出；
  - `class_deferred` 的 `loop` 与 `creator_classes` 的 `deferred` 由拒绝变为完整，分别
    经 3 种模式的 oracle 和运行结果验证，测试预期随之更新；
  - big.abc 4,553 → **5,405**（+852），零回退、零文本变化，语法错误为 0；
  - cohort `csr112/def` 零变化；全量 CTest 996 项，失败的 34 项都在已知清单内。
- 剩余：静态初始化器自身读取类（`static y = A.x`）或捕获外层变量时，需要 class-inner
  初始化器的 phased 路线（`SemanticProofError::kInternalValue`）。

## 模块顶层的 `this` 与 `new.target`（2026-10-29，cc；用户指定的优先阻塞）

- big.abc 中约 2,948 条在打印阶段失败（`TreePrintError::kUnsupportedContext`）。ArkTS
  页面模块的入口把自身的 `this`（a2）和 `new.target`（a1）存进词法槽，供箭头函数
  使用；printer 在模块体中对这两者一律拒绝。
- 运行时依据：ES module 入口由 `SourceTextModule::Evaluate` 以
  `NewRuntimeCallInfo(func, undefined, undefined, …)` 调用（js_module_source_text.cpp:339），
  `this` 与 new.target 都是 undefined；模块代码是 strict，标准语义下顶层 `this` 同样是
  undefined。因此：
  - 模块体打印 `this`：精确等价；
  - 模块体中的 new.target 打印为 `(void 0)`：精确值（模块顶层写 `new.target` 是语法
    错误）。
  - script/CommonJS 体的 `this` 绑定不同，仍然拒绝。
- 当前 pinned es2abc 不允许在模块顶层箭头里写 `new.target`，所以 new.target 形式由
  unit test（`module_family_print_test`）覆盖；`this` 由新 fixture `module_this`
  （default、O0，含顶层 try）的 Node 差分覆盖，植入的变异能被检出。
- big.abc：1,608 → **4,553**（+2,945），零回退、零文本变化，语法错误为 0；cohort `csr111/def` 零变化；全量 CTest 995 项，失败的 34 项都在已知清单内。

## guard 前带寄存器拷贝的模块读取：创建时已初始化的前提（2026-10-29，cc）

- 子函数从父函数继承"创建时已初始化"的 local 模块 slot。这类 load 原本以 `kInitialized`
  直接认证，只排除"紧邻 guard"的情形。但 es2abc 常生成
  `ldlocalmodulevar; sta vN; throw.undefinedifholewithname`：中间的拷贝让 guard 不再
  紧邻，load 被当作已初始化，guard 变成孤立 op，在求值阶段以
  `EvaluationShapeError::kMissingShape` 拒绝（big.abc 3,517 条）。
- 统一 helper `GuardedAfterCopies`（`hole_guard.h`）跳过纯的 acc→寄存器拷贝，与 normal
  路线、exception 路线的 module guard 配对规则一致。三处共用：normal 子函数
  （`function_input`）、phased 子函数（`phased_function_file`）、phased 入口
  （`ProveEntryModuleLoads`）。把 load 判为有 guard 只会撤销"已初始化"前提，然后由
  guard 配对证明或拒绝，因此判断偏宽也是安全的。
- 基线脚本同时统计打印阶段拒绝的 `source_causes`（之前显示为 "none"）。
- 证据：
  - fixture `module_guard_copies`（default、O0）Node 差分；committed 构建拒绝其 default 版本；
  - `isa_recovery` 的 `module-sendable-local-default` 由拒绝变为完整，Node oracle 通过；
  - cohort `csr109/def` 零变化；全量 CTest 994 项，除上述 fixture 外没有新失败；
  - big.abc 1,535 → **1,608**（+73），零回退、零文本变化，语法错误为 0。新的主要原因之一
    是打印阶段的 `TreePrintError::kUnsupportedContext`（2,913 条，箭头函数捕获模块级
    `this`）。

## 函数提升后写入模块绑定（2026-10-29，cc）

- 被提升的函数（`export function setX(v) { x = v; }`）在 `let x` 初始化之前就已创建，
  所以写入的 slot 不在创建时的已初始化集合里，原先一律以
  `ModuleAliasError::kInitialization` 拒绝。big.abc 中有 2,489 条记录卡在这里。
- es2abc 在写入前会先显式检查 TDZ：`ldlocalmodulevar` + `throw.undefinedifholewithname`，
  然后才 `stmodulevar`。统一规则：若一次写入在所有路径上紧接着的前驱效果
  （`EffectOrder::before`）是同一 local slot 的 Hole guard，就是对已初始化绑定的普通
  赋值。slot 一旦初始化就不会再变回 Hole，guard 与 store 之间又没有其他效果，因此
  原生的 `x = v`（在赋值时检查 TDZ）与之等价。每个方法只建一次效果序。
- 证据：
  - fixture `guarded_module_writes`（default、O0）Node 差分：初始化之前调用触发 TDZ，
    之后的赋值都正确，植入的变异能被检出；
  - cohort `csr108/def` 零变化；
  - 同一提交还把 phased 模块入口的 family 构建接入 module scope：`BuildFileFamilyTrees`
    接受 `GlobalScope`，后代类读取模块变量时需要它。原先 2,917 条
    `kClassPreparationInput` 就源于缺少它；`descendant_derived` 增加 `captured` 源码覆盖；
  - big.abc 1,527 → **1,535**，零回退、零文本变化；
  - 全量 CTest：993 项，失败的 34 项都在已知清单内。

## 后代与 phased creator 中的派生类（2026-10-29，cc）

- `descendant_classes` 原先对后代 creator 中的派生类（`extends`）一律拒绝
  （`kClassHeritage`），phased 模块入口里的类也走这条路径。family tree 本来就对每个
  成员运行 `ProjectDerivedConstructor`，TreeFlow 与 RecoveryFlow 也照常校验，因此
  删除这一刀切的拒绝即可。证明责任仍在原有的派生构造证明与校验器里。
- `class extends Base {}`（参数作 heritage，隐式构造）现在停在隐式构造的 entry ABI
  （`FunctionAbiError::kEntry`），`descendant_class_refusals` 的预期随之更新。
- 新发现的独立缺口（未修复）：模块入口有真实 try 时，若模块级类被子函数捕获，会报
  `FamilyEnclosureError::kMissingContext`；这一点 committed 构建同样存在。
- 证据：
  - fixture `descendant_derived`（后代 creator 与 phased 入口两份源码，default 与 O0）
    Node 差分各 9 个用例（原型链、字段、方法、base 构造抛异常），植入的变异能被检出；
  - cohort `csr107/def` 零变化；
  - big.abc 完成 1,522 → **1,527**，零回退、零文本变化，语法错误为 0；
  - 全量 CTest：992 项，失败的 34 项都在已知清单内。

## 待用户决定：箭头函数内的 `super()`（2026-10-29，cc 调查）

- `SUPERCALLARROWRANGE`（及其 wide 形式）是 Unmeasured。运行时
  （interpreter-inl.cpp:3629，`GetNewTarget(sp)`）取的是箭头函数自身帧中的
  new.target，普通调用时为 `undefined`。因此会走 `RuntimeSuperCall`
  （runtime_stubs-inl.h:2779），以 `Construct(superFunc, newTarget = undefined)`
  构造，实例的原型是 `B.prototype`。标准 JS 则使用外层构造函数的 new.target（`D`）。
- 反编译成 `() => super(...)` 在 Ark 上可以按原指令往返编译，但在标准引擎上可观察到
  原型不同。这属于新的 source premise，需要用户批准后才能实现；在此之前继续拒绝
  （`DerivedProjectionError::kProtocol`）。

## 对象字面量的 computed key 与 index key（2026-10-29，cc）

- `{[k]: v}` 由 es2abc 编译为 `DEFINEFIELDBYVALUE`：先求 v，在 define 时才把 k 转为
  属性键；源码语义则是先转换 k、再求 v。数字键（`{0: x}` 在 computed key 之后）
  使用 `DEFINEFIELDBYINDEX`。原先只有带 name setting 的 computed 写入
  （premise 管控）能进入字面量，因此 `{[k]: 1}` 都被拒绝。
- 统一规则：`IsComputedLiteralDefinition` 涵盖 name-setting store 与 define-field
  两种形式。define-field 只有在 key 与 value 都在分配之前求出（或是常量）时才作为
  字面量成员，这样两种求值顺序之间没有任何效果，等价性是精确的，不需要新 premise。
  `{[k]: g()}` 仍然拒绝（kDefinition）。`TreeOperands` 与 `TreeStructure`
  按相同规则独立校验。
- 新 fixture `long_forms/callable_names`（`--enable-callable-name`，API 24）
  覆盖 `CALLTHIS0..3WITHNAME` 与 `CALLTHISRANGEWITHNAME`。
- 证据：
  - fixture `computed_literals`（default、O0）Node 差分 24 个用例（含会记录日志或
    抛异常的 toString key、symbol），植入的变异能被检出；
  - `isa_object_proto` 的 `['__proto__']` 拒绝原因从 kMissingShape 推进到
    kLiteralRhsAvailability；
  - cohort `csr106/def` 零变化；big.abc 1,522 条，零状态变化、零文本变化；
  - 全量 CTest：991 项，失败的 34 项都在已知清单内。
- ISA 台账：**227 verified / 16 unverified / 61 unmeasured / 28 non-executable**
  （新增 `CALLTHISRANGEWITHNAME`、`CALLRUNTIME_DEFINEFIELDBYINDEX`）。

## phased creator 的静态字段（第一步，2026-10-29，cc）

- 方案改为只读的 `CopyRoot` 查询（B）。A 会编辑 phased 图，破坏 import 对应校验。
- phased creator（含 phased 模块入口）现在可以投影没有自引用的类静态字段：
  - creation 与 call 只能抛给 caller，或抛给只做原样转发的 cleanup handler；
  - 识别、投影与求值都按拷贝根比较身份。
  详见 [phased-class-members.md](phased-class-members.md)。
- 证据：
  - fixture `phased_static_fields`（default、O0）Node 差分通过；
  - cohort `csr105/def` 零变化；big.abc 1,522 条，无回退；
  - 相关 181 项 CTest 中，失败的都在已知清单内。
- 剩余：big.abc 中 4,342 条是自引用的类，需要把 class-self 证明接入 phased creator。

## ISA：长跳转、debugger 与宽命名词法环境（2026-10-29，cc）

- 新 fixture `long_forms`：同一份 pinned es2abc 构建三份源码。
  - `debugger`（`--debug-info` 时保留 `DEBUGGER`）；
  - 300 个被捕获的 `let`（`--debug-info` 时生成 `WIDE_NEWLEXENVWITHNAME` 和宽 LD/STLEXVAR）；
  - 超过 32 KiB 的循环体（`JMP_IMM32`/`JEQZ_IMM32`）。
- Node 差分 harness `long_forms.mjs` 用记录型 Proxy 比较返回值，以及属性读取、调用
  （含 ToPrimitive）的顺序。改动参数或常量的变异都能被检出。
- 台账重新生成：**225 verified / 18 unverified / 61 unmeasured / 28 non-executable**
  （之前 222/21/61/28）。`isa-coverage.md` 换为本次生成的版本（原表停留在 10-22）。
- 观察：同类循环体到 1,400 条语句时，记录在 enclosure 阶段触发 memory_limit（RSS 仅
  64 MB）。这是预算限制而不是语义问题，单独跟进。
- 剩余 unmeasured 中，非 deprecated 部分需要真正的功能：私有字段（5 个 opcode）、
  `for await`、箭头函数内的 super 调用、computed class field、sendable env、
  hot-patch 变量。deprecated 变体只出现在旧 profile 的语料中。

## try 内带寄存器拷贝的 import 读取；嵌套 catch 的 handler 范围（2026-10-29，cc）

- **根因**：big.abc 中 4,419 条 `ExceptionInputError::kInternalValue` 抽样全部是
  同一形态。es2abc 对 import 读取生成 `ldexternalmodulevar`（以及 lazy、local 形式），
  随后 `sta vN`，再执行 `throw.undefinedifholewithname`。normal 路线早已接受 load 与
  guard 之间只隔纯寄存器拷贝的配对（`CopyOnlyModuleGuardInterval`），但 exception
  路线的 `ProveExceptionModules` 要求两者严格相邻，因此 try 内的每个 import 读取都会被拒。
- **统一修复**：exception 路线采用与 normal 路线相同的规则，不新增机制。
  - `ProveExceptionModules` 允许 load 与 guard 之间隔着不会抛出的 acc→寄存器拷贝，
    每一步都是单入口的 fallthrough。fact 中记录 guard 指令，phased lowering 不再假设
    guard 就是 `site+1`。
  - **本地 catch 的可观察性**：guard 抛出时，拷贝目标寄存器里是 Hole，而源码中的赋值
    根本没有执行。所以只有当拷贝值在 guard 的异常弧上不是任何 phi 的输入（pruned SSA
    下即为 dead）时才认证。实现上先对 phi 输入建一次有序索引，复杂度 O(P log P)。
    caller 出口则会丢弃整个帧，不受此限制。
  - 求值阶段：`VerifyModulePhaseOrder` 与 `VerifyModuleCatchCopies` 沿
    `GuardChain`（read → 拷贝 → guard 的单边 phase 块链）检查，链上的任何 normal 边
    都不允许放置 phi 拷贝。`PureCopies` 从 `CopyOnlyModuleGuardInterval` 中拆出，
    两条路线共用。
- **嵌套 catch 的 handler 范围**（修正上一条的推断规则）：handler 起点自身代码里
  开始的 try 是嵌套在该 handler 中的，它的 catch 只能经异常弧到达，现在也计入外层
  handler 的范围。原规则会把 `catch { try {…} catch {…} }` 判为 kCrossingScopes，
  导致 lottie `markerParser` 由完整退为拒绝。上一条记录的 1,460 测于这条规则的
  最终修改之前，没有覆盖这一回退。
- **证据**：
  - `exception_module_copies_test`：caller 出口与 dead 拷贝获得认证，handler 读取拷贝
    寄存器时拒绝；
  - `handler_extent_test` 新增嵌套 catch 用例，推断结果等于记录的范围；
  - 新 fixture `guarded_imports`（default 与 O0）：Node 差分 4 个场景，覆盖 call、
    别名、算术与 new，植入的变异能被检出；
  - cohort `csr103/def` 对 `csr101/def`：620/1007，零状态变化，零文本变化；
  - 相关 CTest（module/guard/exception/phased 共 209 项）中，失败的都在已知清单内。
- **big.abc**（commit 前当前树）：完成数 1,460 → **1,522**，部分 83 → 1，拒绝
  12,396；语法错误为 0；耗时 54.8 s，峰值 RSS 171 MB。两次都完整的记录零文本变化，
  没有完整 → 拒绝的记录。
  - 35 条部分输出变为拒绝：它们被省略的成员原本卡在 input gap，这是唯一可省略的
    原因类型；现在越过 input 后，停在之后的真实缺口（kNormalEntry、控制流
    多出口等）。这些原因在其他 558 条记录里本来就是整条拒绝。省略策略保持不变，
    没有扩大为"任意失败都可省略"。
- **剩余阻塞**：
  - `CreationContextError::kKind`（4,342），即 phased creator 中类的
    static_initializer；
  - FamilyCreationCheck:17 / :13（2,233 / 1,480）；
  - 未加 guard 的 local module 读取（749）；
  - kNormalEntry（558）。

## 模块入口 try/catch 与 code_size 0 handler（2026-10-28，cc）

big.abc 的拒绝原因中有 88% 来自同一根因：模块入口 `func_main_0` 含有真实的顶层
try/catch。按设计 [phased-module-entry.md](phased-module-entry.md) 分两部分交付，
都是扩展现有 owner，没有新增并行机制。

- **模块入口走 phased 路线**
  - **选路**：`OnlyEnvironmentCleanupHandlers` 对 handler 做结构分类，由环境清理
    owner 负责。全部是环境清理 handler 时继续走 record 路线，输出字节不变；
    否则以 `EntryTreeContext` 作为 family root，像普通 phased creator 一样构建。
  - **模块绑定的声明与访问**：
    - 入口自有的 local 绑定由 entry 声明（`local` 加 mutable `let`）；子成员
      仍只能引用这些绑定，`VerifyModuleAliasTree` 的 entry 模式按此独立校验。
    - 首次写入的判定与 record 路线一致：根语句序列中每个 slot 的首个 store 作为
      声明，其余 store 必须被它支配。
    - local load 的初始化事实由新增的 `ProveEntryModuleLoads` 在 ExceptionFlow 上
      证明：store 在 Commit 生效，异常弧从 Before 节点出发，因此 catch 及其后的
      汇合点看到的是抛出前的状态。`VerifyFamilyWrites` 会独立重算并要求完全一致。
  - **记录装配**：模块名、导出别名等元数据前移，两条路线共用。phased 分支复核
    inventory（每个 local slot 恰好声明一次）、globals 与 destination。
- **code_size 0 的 catch handler**：新编译器常把 handler 的 code_size 记为 0，而
  解释器本来就只使用 handler 起点。handler 的范围改为：从 handler 起点出发、只沿
  非异常弧可达、且不进入正常可达代码（汇合点）的指令，范围终止于其中最后一条。
  - 以 throw 或 return 结束的 handler 就此终止，其后的兄弟 handler 不会被并入；
    第一版按"下一个汇合点"截断，会把兄弟 handler 并入，已由 fixture 的
    kCrossingScopes 暴露并修正。
  - 起点本身就是汇合点时，即为空 catch `[b, b)`。
  - 零长度的受保护范围不保护任何指令，因此只能作为 inactive region 接受；
    若它仍是活动 region，则保留 kHandlerShape 拒绝。
- **测试**：五个原先钉住 kHandlerShape 拒绝的测试，现在让 default 构建跑完整
  oracle：
  - `inactive_regions`、`capture_scope`、`native_member_updates` 的
    catches-default，以及 `exception_throw_domain` 的 handler：完整 oracle，
    其中 `capture_scope` 与 `exception_throw_domain` 还包含变异检出。
  - 其余三个优化后的 for-of 布局，以及 `guarded_reference_transports`，现在停在
    下一个真实缺口：kNormalEntry 与 CallReferenceError。
- **证据**：
  - 新 fixture `module_entry_try`：Node 差分 4 个场景，植入的变异能被检出；
  - `handler_extent_test`：推断结果与记录的范围一致，覆盖空 catch 和延伸到代码末尾两种情形；
  - source-regression 65/65；
  - cohort `csr100/def` 对 `csr98/def`：620/1007，零状态变化，零文本变化；
  - big.abc 完成数 1,176 → 1,460，语法错误为 0；耗时 50 s，峰值 RSS 160 MB。
- **剩余阻塞**（big.abc 新的主要原因）：
  - ExceptionInputError::kInternalValue（4,419）；
  - phased creator 中带静态字段的 class，其 static_initializer 报 `CreationContextError::kKind`（3,212）；
  - FamilyCreationCheck:13（1,222）；
  - 模块顶层 `this` 被箭头函数捕获（打印阶段 kUnsupportedContext，record 路线同样拒绝）；
  - catch 内带选择的成员调用（CallReferenceError::kCrossRegionValue）。

## 可读性：尾部 continue 与 return void 0（2026-10-28，cc）

- `ReadableStatements` 新增 `omit_continues`：自身循环体尾部位置的 `continue`
  省略。沿块与未投影的 if 分支下探，不进入 try 或 switch。尾部 `continue` 与
  落到循环体末尾等价，都会执行 update 或 test 后进入下一轮。省略后 if 分支变空时，
  沿用已有的 kInvert/kDropElse 改写。
- `ElidedUndefinedReturn`：`return void 0;` 打印为 `return;`。async generator
  不适用，因为 `return expr` 会执行 Await(expr)，比 `return;` 多一个 job；
  `tail_completions` 的 job 顺序测试捕获了这一差异，现已排除。printer、位置测试与
  `instruction_positions` 共用这一条规则。
- cohort：24 个文件共 88 处 `continue;` 被移除，8 处 if 反转（取反准确）；115 个文件
  共 469 处 `return void 0;` 变为 `return;`；零状态变化。

## 测试耗时治理：整应用差分按内容缓存（2026-10-28，cc）

- **测量**：全量 CTest 983 项，共 4,755 s。其中 16 项超过 60 s，合计占 75%。
  shopping 导航图的 4 个 shard 单项约 9 min，均为 RUN_SERIAL，合计占 45%。
  该图的 26,580 个用例中有 95.6% 是 fault 注入；每个用例都要分别对 reference
  与 emitted 完整重放约 29.7k 个事件。
- **A 结果缓存**：新增 `tests/integration/result_cache.mjs`，navigation_graph 与
  community_graph 两个 harness 都改为经它运行，并用于这 9 个整应用测试。
  - 缓存键为 SHA-256，覆盖 emitted 源码、paired reference、用例参数、
    `tests/integration/*.mjs` 的全部内容以及 Node 版本，任何一项变化都会失效。
  - 每次运行仍执行导出与完整性断言，缓存只省掉对未变输入的运行期差分重放，
    且只记录通过的结论。
  - 需要强制重算时设置 `ABCD_TEST_CACHE=off`。
  - 实测：9 项冷启动约 22 min（并行），热启动 4.9 s。
- **C 标签**：上述 9 项打 `corpus-graph` 标签。快速评审用 `-LE corpus-graph`；
  提交前仍跑全量，未变的输入直接命中缓存。
- **B 已撤回**：取消 shopping shard 的 RUN_SERIAL 后，4 个 shard 并行占满 10 核，
  minigame 与 community 的 worker 因此超出 60 s 上限（单独重跑均通过）。
  RUN_SERIAL 保留，原因已写入 CMake 注释。
- **未做 D**：缩减 fault 值域的笛卡尔积会改变测试设计，需要用户另行决定。

## 第 620 条：在现有 owner 内统一规则，不新增机制（2026-10-28，cc）

- **结果**：o32 service（58223）由 partial 变为完整，cohort 达到 **620/1007**；
  零回退，零状态或 omission 变化。
- **改动**（9 个生产文件，+65/−20，没有新增 owner、证书、retry 或 Error 枚举）：
  1. **异常拷贝的唯一解释**：`exception_copies.h` 新增 `ProtocolConsumedCopy` 和
     `StagedCopyCount`。selection 发现与 corridor 的 `HasCatchCopies`/`SameCatch`
     原先读原始 bundle 计数，把已被 for-of 协议消费的拷贝误当作可执行暂存（这是
     pc 2208 的根因）。另外 8 处内联的重复判定一并改为调用这两个函数。
  2. **受保护分支的同一条 Commit 规则**：builder 的 `EmitDecisionBlock` 与
     `EmitBlock` 共用 `BranchCommitAdmissible`（允许 kRoot 或 kLogicalValue）；
     TreeFlow 的决策路径用同一条规则复核。修复前，受保护区域内带值的 `&&`
     嵌在 `||` 决策里时，只有 decision 路径拒绝。
  3. **不可抛出的 selection-arm 根**：`SameCatch` 补上"消费者不可抛出、结果只流向
     phi"的情形。此时消费者自身就是执行根；若抛出点无需暂存，且两者按序位于同一
     无拷贝 fallthrough corridor，则分派不变。纯中间节点仍沿用"证据来自最终执行根"
     的规则。只放宽到 phi 根，是 cohort 验证后的收窄：不加 phi 根限制时，
     有 5 条记录回退。
  4. **TreeFlow 映射一致**：校验 condition blocks 时改经 `SourceBlock`
     映射投影控制单元，与 builder 一致（原来误用控制节点的原始 block 号）。
- **证据**：
  - source-regression 65/65；
  - 新 fixture `protected_conditions`：5 个导出，215 个 Node 差分用例；
    植入的重排变体能被检出；
  - cohort `csr96/def` 对 `csr94/def`：619→620，零回退，零状态或 omission 变化；
  - 7 条完整记录文本变化，逐条核对为语义等价：消除临时变量，
    读取与调用的次数和顺序都不变；`p ? a : p` 与 `p && a` 等值。
- **对照审计原则**：Codex 未合入的方案为同一问题新建 owner 加 retry，约千行；
  本次只修正现有 owner 之间解释不一致的地方。

## 统一方案审计：停用 Codex，cc 独立推进（2026-10-28）

- 用户决定停止 Codex，由 cc 独立完成剩余工作，并把"不过度设计、方案统一贯通、
  不针对样本一对一处理"列为常设原则。审计结论与执行计划见
  [unified-design-audit.md](unified-design-audit.md)。
- 样本特例检查：`src/`、`include/`、`tools/` 中没有样本名、方法号、记录号，
  也没有 pc 字面量准入。
- 过度设计：存在 5 条由类型化拒绝触发的重试路线。其中 3 条（completion ports、
  iterator continuation、single-pass）是同一个迭代器激活对象的不同属性，
  计划聚合为 `IteratorActivation`。异常拷贝的解释同样计划统一。
- Codex 未完成的 projected-snapshots WIP 不合入，已归档于
  `build/review/codex/snap-wip/`。第 620 条将在现有的 `exception_copy_emission`
  owner 内修复，不新增机制。
- 当前 cohort：619/1007，o32 service 为 partial（只缺 m60551）。

## Certified single-pass iterator scopes (2026-10-28)

- **Review fix: monotone partial publication.** The initial B comparison checked
  only complete sources and missed service's partial-to-refused downgrade. That
  was a regression; the earlier complete-only acceptance claim is withdrawn. The phased
  owner now retains the actual earlier typed, independently omittable refusal.
  A later member-local tree refusal can publish that same omission with a
  separate `later_failure` cause/location/reference chain. Resource/work failures
  remain terminal. Immediate and split preparation costs are charged once.
  Prepared owner IDs remain reserved and independently checked; no failed tree, fabricated body or
  receipt is published. Service's 40,760-byte partial source is byte-identical
  to `csr93/def`, omitting only m60551, with the later original2208/working1512
  failure retained as diagnostic evidence. New comparator checks every record
  state, published partial text and omitted-member sets; its red check catches
  the pre-fix downgrade. Final validation receipts are under
  `build/single-pass/partial-fix/`.
- Added a logical `ForOfScope` alternative for iterator activations without a
  reachable repeat. Natural `LoopForest`/`LoopId`, physical CFG/SSA and exception
  ownership are unchanged. Legacy missing-natural-loop/borrowed-header witnesses
  select the bounded retry; a bare `kAdvance`/`kEntry` never does. Acquisition,
  value Commit, exhaustion, body exit cuts, protection and logical iterator
  parents are independently reconstructed. No new source premise was added.
- Control owns lexical placement; native `ForOfStmt`/break, capture lifecycle,
  evaluation, operands/origins, tree flow and OriginalFlow/RecoveryFlow share the
  same view. Recovery checks actual native flow for unreachable implicit repeat;
  existing completion ports retain close order, priority and zero extra closes.
- The new pinned O0 fixture passes 6,246 Node comparisons, and the promoted
  completion-ports single-iteration case adds 598 comparisons. Mutation and
  register/copy/block invariance checks pass. Fixed-K scaling at
  100/200/201/400/800 padding covers the 200/201 boundary with linear retained
  proof storage and bounded N log N charged work.
- Service m60551 passes both original452/1307 scopes and reaches a separate
  member Reference failure at original2208/working1512 (callee read2132):
  `CallPlanError4 / CallReferenceError1 / TemporaryReason4`. This catch-argument
  ownership gap is outside this increment. Partial publication is retained using
  the earlier omission witness, and the later refusal is additional evidence.
  The original single-pass mechanism is not widened to admit the call.
- Nested unconditional returns that fail the older close-entry continuation
  owner remain an exact negative, before placement activation. Unsupported
  `for await` and unrelated multi-exit user tries keep their typed refusals.
  Three stale integration expectations are updated to native completion plus
  differential or exact-refusal oracles; capture/mutation/raw close checks stay.
- Review-fix required targets and the full Debug build pass. Source-regression
  passes **65/65** (103.48 s), the related/changed-expectation sweep **42/42**
  (193.16 s), including class owner/omission mutation checks and the refreshed
  service partial/CLI oracle. Direct single-pass and completion-port Node tests
  pass **6,246 / 7,176** comparisons; geometric scaling passes (19.46 s).
- The new all-status comparison covers **40 inputs / 1,007 records** against
  read-only `csr93/def`: **619 complete / 1 partial / 387 no-module** before and
  after, zero status changes/downgrades, zero omitted-member-set changes, zero
  complete or partial text changes. It checks **3,606,050 complete bytes plus
  40,760 partial bytes**. The sole diagnostic change is service's additional
  later failure. CLI peak RSS is **47,054,848 bytes**; input hashes are checked
  before and after. The earlier complete-only comparison was insufficient and
  is not used as evidence of monotone publication.
- ISA ledger remains **222 verified / 21 unverified / 61 unmeasured / 28
  non-executable** over **1,169 inputs / 55 producers**, with zero needs-decision
  or unsupported-premise-free classifications. Full Debug CTest completes
  **949 passed / 34 known failures out of 983** in **3,724.10 s**: zero newly
  failing or newly passing existing tests, no missing/skipped tests. The raw exit8 is the unchanged34-test baseline
  failure set, verified independently in `ctest-comparison.json`. These final
  receipts replace the prior incomplete/overlaid validation.
- Next call blocker is design-only: raw protocol-consumed copy counts are still
  treated as executable catch staging. Original dispatch is the same enclosing
  iterator handler2443 for read2132 and call2208, both inside the user catch.
  The minimal sync/async for-of + catch logger + later break reproducers fail
  with the same exact reference chain. Proposed shared effective snapshots and
  rechecked protocol coverage are in
  [projected-call-snapshots.md](projected-call-snapshots.md); no admission was
  implemented and no new premise is proposed.
- Iterator-only OriginalFlow evidence is now cold, prepaid when present, and
  value-copy isolated. This fixes the observed nested-800 class stage-budget
  regression without raising its64MiB limit or deleting a proof. The unchanged
  class scale and fixture-support tests pass in the final two-worker full suite.
  Exploratory overlapping-build validation was invalidated, not counted. Final
  receipts and complete file/line inventory are in
  `build/single-pass/partial-fix/`.


Design: [single-pass-iterators.md](single-pass-iterators.md).

## Certified iterator exhaustion continuation (2026-10-28)

- Added source-control-owned `IteratorContinuationProof`, activated only after
  the real typed orphan-expression census and a sealed close-boundary proof of
  a misplaced exhaustion continuation. The exhausted edge's lexical follow is
  constrained outside the native loop. Physical CFG/SSA, close ownership,
  exception ports and the strict census stay unchanged. No source premise was
  added; acyclic/headerless native iterator placement remains deferred.
- One owned phased retry rebuilds control, evaluation and syntax together. ABI,
  captures, operands/origins, tree flow and recovery use that same view. Fresh
  control/operand/origin checks derive the certificate independently; immutable
  original close/copy/protection facts and actual native completion actions are
  rechecked by recovery. Failed retries retain the typed census witness and
  `ForOfPlacementError` cause. Original and retry peak storage/work are charged.
- The pinned O0 fixture passes **3,997 Node comparisons** across seven exports:
  effectful exhaustion, conditional return subtrees, captures, throwing
  getters/coercions and close-priority/identity observations. Certificate
  mutations and coherent register/copy/block variants cover the refusal and
  invariance obligations. The 100/200/201/400/800 growth test passes, with fixed
  protocol count; it is not a dense-product scaling claim.
- Required build targets and the complete Debug build pass; the final
  source-regression label passes **65/65** in **222.54 s**; the related sweep
  passes **33/33** in **192.82 s**. ISA totals stay **222 verified / 21
  unverified / 61 unmeasured / 28 non-executable**, over **1,165** inputs and
  **54** oracle producers. Full Debug CTest finishes **943 pass / 34 known
  failures out of 977** in **3,171.03 s**: zero newly failing, zero changed
  existing statuses, six new passing tests, no skipped or missing tests.
  Final receipts are in `build/iter-continuation/acceptance.md`.
- All **40** no-flag cohort inputs compare against read-only `csr92/def`:
  **618 → 619 / 1007**, zero regressions and zero text changes over **3,601,791**
  existing complete-source bytes. The only new completion is **o32/42397
  DataDriver**, including m42798. Other typed failures are identical. CLI peak
  RSS is **49,004,544 bytes**. Input hashes were checked before and after.
- Service remains partial at m60551's acyclic inner iterator, protocol location
  **original452 / working428**, with CompletionPortError9 /
  ForOfIterationError3 and the unchanged original313 multi-exit witness.
  Increment B is not included. The stale `for_in_evaluation` DataDriver refusal
  assertion now requires exact complete CLI/source correspondence; its raw
  bypass/validation edge oracle and all other assertions remain.
- Design: [iterator-continuation.md](iterator-continuation.md). Raw build/test,
  cohort and ledger evidence lives under `build/iter-continuation/`.

## Verified iterator completion ports (2026-10-27)

- Added a legacy-first `completion_ports` fact owner and independent verifier.
  The actual typed `kMultipleExits` region/two-exit witness is replayed and
  retained. Original cut edges, pending SSA payloads/copies/phis, protection
  contexts, activation gates, distinct bypass/validation resumes and complete
  transition coverage compose the existing close proofs. Other protocols'
  wrappers are never inferred from overlapping intervals. No source premise,
  for-await support or new array-pattern form was added.
- Persistent closed-iterator contexts and memoized raw/native routes prove
  inside-out order and zero extra closes on closed-handler re-entry. Internally
  caught errors become pending only at the proven priority selection. One native
  return stays at its initiating action. Close/ownership/control, evaluation,
  operands/origins, tree flow and recovery independently check the evidence.
  A shared instruction-range index avoids scanning all members per protected
  range. Live iterable and return outputs remain ordinary source observations.
- The pinned O0 fixture passes **6,578 Node comparisons** over eleven exports,
  plus three exact typed negatives. Certificate mutations cover activation,
  original edges, payloads, guards, protection, routes, range/port coverage and
  limits. Register/copy/block invariance and 200/201/geometric growth pass.
  Unrelated multi-exit user tries retain refusal. Omission diagnostics retain
  trivial records through typed indexes into separately budgeted cause owners.
- The original goal methods advance, but neither whole record is complete.
  DataDriver m42717/m42744 pass; **m42798 getSuiteStress** next hits the legacy
  AST ownership census (orphan expression59, original **pc542**; published
  FunctionTreeError9 / TreeSemanticError0 / TreeFlowError1 / TreeObligation10).
  Service m60551 passes the original313 completion-port gap, then keeps a partial
  omission for an **acyclic inner iterator at original452 / working428**:
  CompletionPortError::kTarget9 / ForOfIterationError::kAdvance3. The old region8
  witness, continuations340/363 and ordinary user try [434,2118) remain intact.
  General AST ownership and acyclic-loop source placement are separate follow-ups;
  no special-case implementation was added for either.
- Final required make targets and the full Debug build pass; source-regression
  passes **65/65** in **120.57 s**, and the related sweep passes **26/26**. The
  direct fixture and scaling reruns pass. All **40** no-flag inputs
  compare against read-only `csr91/def`: **618 → 618 / 1007**, zero regressions,
  zero text changes over **3,601,791** existing complete-source bytes, and no newly
  complete records. Only the two target failure diagnostics change. CLI peak RSS
  was **50,298,880 bytes** in the last binary sweep
  (`cohort-accepted/comparison.json`). Final ISA ledger totals remain
  **222 verified / 21 unverified / 61 unmeasured / 28 non-executable**, with
  1,163 measured inputs and 53 oracle producers.
- The guarded-catch combined negative now passes close ownership and reaches
  TreeFlowError::kPlacement4. Its exact four-cause refusal expectation was
  updated; all other oracles remain. Two integration expectations now include
  the precise ForOfIterationError3 cause; the for-in CLI integration pins the
  new m42798 leaf and separately retains the old raw bypass/validation targets.
  All three exact reruns pass. Charged work for 100/200/201/400/800
  added instructions is **72730274 / 92786122 / 92986674 / 142953642 /
  229728906**, within the N log N bound (fixed port incidence count).
- Full Debug CTest completed **971** cases in **3174.56 s**: initially **38**
  failures (the known 34, three stale diagnostic assertions, and an empty-
  diagnostic community worker failure). The three exact assertion refreshes
  pass; community ability passes an unchanged isolated rerun in **206.86 s**.
  The reconciled result is **937 pass / 34 persistent**, zero newly failing
  and zero newly passing existing tests. No community harness assertion or
  worker limit was relaxed. All four new registrations pass. Raw sweep and
  rerun receipts, not a claim that the initial full command passed, are kept in
  `ctest-full.xml`, `ctest-expectation-rerun.xml`, `ctest-community-rerun.xml`
  and `ctest-comparison.json`.
- Design, costs and typed gaps: [completion-ports.md](completion-ports.md).
  Logs and own cohort output: `build/completion-ports/`; `build/review`, samples
  and input ABCs are untouched. No commit or push.

## Certified environment renewal: body reset (2026-10-27)

- Added the sparse environment-token/correspondence owner and an independent
  verifier. Physical allocation IDs stay distinct. Slot snapshots, original
  phi edges, actual parent tokens, copy corridors, initialization Commits,
  captures and exceptional coverage are retained in one certificate. The
  ordinary environment agreement solver and ancestry index are unchanged.
- The new route activates only on typed `kEnvironment` / `kAmbiguousJoin`;
  the lower cause and original/working join witness remain in failures.
  Body-reset cells become a block-scoped `let` at the initializer inside the
  loop body. Cell-state SSA proves initialization exactly once per generation,
  including loops that would revisit the store without renewal. Header-copy
  projection deliberately retains typed `kSourceScope` and is follow-up work.
- The existing resource-exhaustion exclusion accounts for allocation after
  departure; raw observable-resource analysis refuses. Exception/local/capture
  input, phased import, working environments, cells, family writes, evaluation,
  operands/origins, tree flow and recovery independently replay the evidence.
  Newly admitted cleanup/finally catches reuse the existing guarded catch-payload
  proof; no new finally rule, source premise or ignored throwing anchor is added.
- The real O0 fixture has **330 Node comparisons**: multiple cells, old closure
  writes, initialization callbacks and shared/primitive values, nested scopes,
  zero iterations, continue/break/throw/finally, await and rejection. Header-copy
  is an exact typed negative. Owned body/certificate mutations cover slots,
  saved values, interference, phi edges, parents, generations, Commits, coverage
  and limits, plus copy/block/renumbering invariance. Fresh-entry fixture output
  remains **926 bytes, byte-identical** to the baseline engine.
- Required make targets passed; source-regression passed **65/65**. All **16**
  related ISA Python tests passed. New ISA/unit/scale tests are separately
  registered, following existing ISA label practice. Complete IDF avoids
  repeated per-cell liveness walks; 100/200/201/400/800-cell charged work is
  **313868 / 634917 / 645280 / 1418395 / 3112657**, within the N log N bound.
- All **40** inputs, no language flag, match read-only `csr90/def` for every
  previously complete record: **617 → 618 / 1007**, zero regressions, zero text
  changes over **3,596,767** prior complete-source bytes, and zero changed typed
  failures elsewhere. Only **o14/16446 TaskpoolAsyncLevel** becomes complete.
  Measured CLI peak RSS across these runs is **50,364,416 bytes**. Service
  m60551 (original 313 / working 310) and DataDriver m42717 (633) retain their
  nested iterator-close `kMultipleExits` blockers; that work is deferred.
- Full authoritative Debug CTest swept **967** tests in **3105.52 s**.
  Its initial 35 failures contain the same 34 B-state failures plus the stale
  concurrent-metadata expectation below. After that exact rerun, the final
  result is **933 pass / 34 persistent**, zero newly failing and zero newly
  passing existing tests. All three new tests pass. Exact names and comparison:
  `build/env-renewal/ctest-comparison.{json,md}` and adjacent full/rerun logs.
  The ISA ledger passed over **1159** ABC evidence inputs with unchanged
  **222 SupportedVerified / 21 SupportedUnverified / 61 Unmeasured /
  28 NonExecutable**; evidence is in `build/env-renewal/isa/`.
  A Node 25 Acorn stack overflow in the loop-transition harness was corrected
  with the neighboring test's explicit 4096-KiB stack allowance; all **108**
  emitted sources were verified identical to the baseline and assertions stay
  intact. The concurrent metadata test now requires Taskpool completion and
  runs all five records through its existing default/alias recompilation and
  module/kind/sendable/request oracles; its exact rerun passes.
- Change inventory: **27 production files, +1949/-53 lines**;
  **16 test/fixture files, +654/-3 text lines**, including four new ABC binaries;
  CMake **+11/-2**. Complete paths and per-file counts:
  `build/env-renewal/changes.json`. Unrelated `sample.tar.gz` is untouched.
- Evidence: `build/env-renewal/{cohort-final/comparison.json,related-results.json,
  scaling-final.log,fresh-comparison.json}` and
  [environment-renewal.md](environment-renewal.md). Samples, input ABCs and
  `build/review` were not modified. cc review rerun: 65/65, cohort csr91/def vs csr90/def 617→618 (o14 TaskpoolAsyncLevel), zero regressions/text changes.

## Sole source engine, increment B (2026-10-27)

- Retired the eight non-Source identities and `WithLanguageConstructors`.
  One explicit typed registry owns SourceV1–V10 writing, parsing and identity
  validation; published numeric IDs 8–17 and labels are unchanged. Retired
  labels/IDs refuse with `RouteReceiptError::kIdentity`, with negative tests.
- Removed constant constructor/capture/forwarding/lazy-import/array-spread and
  callable-text selectors. Every semantic owner, capture/store, effect,
  exception, operand, origin, flow and recovery proof remains in place.
  `RealmBindingOrigin` retains explicit-host provenance and the existing wire
  labels/key. `ResourceFailureScope::kObservable` and pinned arithmetic,
  call-result, pending-exception and private-value analysis inputs remain live.
- Source tests now select retained identities; raw numeric refusals use the
  analysis API. Historical SourceV5 still tests the pre-native-arguments frame
  snapshot ABI. Partial-expression tests retain their pinned refusal/partial checks below
  the source target; the now-complete V10 projections also have Node coercion
  oracles. Class gaps use genuine private-value operands instead of retired
  numeric-policy refusals. Source-regression membership stays at 65.
- Full authoritative make passed: `cmake --build build/regression -j 8`.
  `ctest --test-dir build/regression -L '^source-regression$' -j 2
  --output-on-failure` passed **65/65**. Historical ID/label, retired-receipt,
  raw numeric/capture/ABI, native naming and coercion oracles passed. All **218**
  existing Verify/Prove symbol names remain; production removes **633** lines
  and adds **160** (net **473 fewer**). No semantic proof/verifier was removed.
- All 40 exploration inputs, no language flag, match read-only `csr89/def`:
  **1007 records / 617 complete**, identical status/typed causes and **3,596,767**
  complete-source bytes, zero differences. Evidence and binary fingerprint:
  `build/contract-b/cohort-final/comparison.json`. No review tooling, review
  output, samples or input ABCs were changed; no new source premise.
- `python3 tests/isa/coverage_matrix.py` passed with unchanged totals:
  **222 SupportedVerified / 21 SupportedUnverified / 61 Unmeasured /
  28 NonExecutable**. Binary fingerprint and full log:
  `build/contract-b/coverage.log` (same binary as the cohort comparison).
- Full CTest swept **964** tests, including all serial graph tests. The final
  exhaustive rerun of its initial failures leaves **930 pass / 34 persistent
  A-state failures**, exactly the same names: **zero newly failing, zero newly
  passing**. Initial B fixture failures were corrected by moving retired-policy
  negatives to raw analysis, retaining meaningful identity/capture counterexamples,
  and adding native coercion/NaN/reentrant-TDZ oracles; all were rerun. Exact
  names, status comparison, commands and JUnit evidence:
  `build/contract-b/ctest-comparison.md` and adjacent JSON/XML/log files.
  `tree_structure_test` remains in the pre-existing set; its retired opt-out
  assertion was migrated without concealing that status. The 34 old failures
  and the same 390 incomplete/refused cohort records remain outside this cleanup.
- Reviewed by cc (gates rerun: 65/65, cohort `csr90/def` vs `csr89/def` 617→617
  with zero text changes, ledger unchanged). Changed-file inventory:
  `build/contract-b/changed-files.txt`. Raw arithmetic/call-result/pending-exception
  and private-value proofs retain their original analysis meanings; published
  retired contracts are rejected rather than upgraded.

## Sole CLI source target, increment A (2026-10-27)

- User decision 2026-10-26: retain `--language-source` as the sole target.
  `kSourceContract` now selects existing `kLanguageSourceV10`; no language flag
  and `--language-source` are aliases. Manifests/receipts retain the exact
  `language_source_v10` label. This adds no premise. Source-output selectors
  and raw analysis semantics remain separate.
- Both source CLIs reject arithmetic/numeric/constructor selectors before input
  access or output staging. `ExportError::kArguments` is appended (13), with the
  existing `usage:` prefix. Shared typed removed-option diagnostics also guard
  corpus inspection, scan and capability arguments. Inspection/publication and
  inherited-default call sites were audited; there is one shared default.
- Migrated CLI harnesses to V10, removed obsolete contract-off rows, and retained
  semantic oracles, exact remaining refusals and explicit pinned analysis/ABI
  fixtures. Contract mutations now use an actually different retained identity.
  The consolidated CLI test checks alias identity, permutations/duplicates,
  conflicting views, removed routes, exact errors and filesystem non-effects.
  Source-regression membership stays at 65.
- Full rebuild exposed a stale synthetic phased-construction fixture: it passed
  a normal-parent class without the required phase binding. The fixture now
  encodes its real handler and creator occurrence, then uses shared creator
  preparation/binding and the parent capture tree. All production verifiers are
  unchanged; the existing construction Node oracles pass.
- ISA recovery/evidence uses one canonical `language_source_v10` key and checks
  manifest identity independently of argv. Retired evidence is rejected, never
  relabelled. Deprecated recovery snapshots were measured anew without changing
  ABCs. The default ledger cache is keyed by production binary fingerprint;
  explicit output directories retain strict cache identity checks.
- Acceptance: `cmake --build build/regression --target source_regression
  abc_structure abc_semantic isa_coverage_probe -j 8` passed; the full make tree
  was also built. `ctest --test-dir build/regression -L '^source-regression$'
  -j 2 --output-on-failure` passed **65/65**. CLI/importer/probe and corrected
  construction checks passed. Full CTest before/after swept **964** tests;
  final status after migration reruns is **930 pass / 34 persistent pre-existing
  failures**, with no newly failing test. The faithful isolated baseline has
  **929 pass / 35 failures**, including a community ability worker termination;
  that test passes under V10. Registration replacement is arithmetic CLI →
  consolidated source CLI. Full names, status changes and runner corrections:
  `build/contract-a/ctest-comparison.md`; machine-readable comparison alongside.
- All 40 exploration inputs were emitted in both invocation forms and compared
  to read-only `build/review/csr88/ns`: **1007 records / 617 complete**, zero
  status/cause differences, zero complete-source byte changes, and identical
  default/alias manifests. Direct-run evidence:
  `build/contract-a/cohort-final/comparison.json`. No cohort tool or review
  output was modified; dated corpus evaluations were preserved without append.
- `python3 tests/isa/coverage_matrix.py` passed: **222 SupportedVerified /
  21 SupportedUnverified / 61 Unmeasured / 28 NonExecutable**, unchanged.
  Current binary-bound evidence is under `build/isa-language-source-v10/`;
  command logs and the complete changed-file list are in `build/contract-a/`.
- Increment B remains pending: retired engine variants/policy branches and the
  dense label registry are deliberately retained in A. Preserve typed historical
  Source V1–V10 identities, explicit realm-binding provenance and live runtime
  proof inputs when performing B. Iterator-close and renewal work remains parked.
  No commit or push was performed for this increment.

## Guarded catch payloads with current-activation evidence (2026-10-26)

- Legacy-first third input route: ordinary input validation and the old guarded
  retry stay unchanged. The new throw-domain owner validates current SSA
  definitions/uses, copied identities, every phi predecessor and all incoming
  Catch sources. A sparse occurrence DAG proves non-Hole facts through guarded
  edges, phi inputs and catch/rethrow transport, including loops. Only certified
  throw-to-Catch dependencies are filtered; unrelated payload uses keep Hole.
  No source premise or default iterator/concurrent policy changed.
- Immutable shared evidence survives import and recovery. Exception-input,
  imported-phase, evaluation, operand, tree-flow and recovery verifiers recheck
  it independently. Guard/phi/activation/coverage failures retain typed
  `analysis::ExceptionThrowDomainError` causes. PMR buffers and actual shared
  control blocks are prepaid; the proof arena receives its byte grant after
  its control block is charged. Sparse indexed work targets O(M log M).
- Real O0 `guarded_catch_payload` fixture: **216 Node comparisons**, plus a
  combined array-pattern/for-of/await/outer-catch typed control negative. Unit
  coverage includes guarded phi arms, catch/copy/rethrow, current loop values,
  direct-edge/split-block phi equivalence, an unguarded Hole throw, changed phi
  predecessor, wrong guard direction, replaced loop payload, missing coverage,
  exact/minus-one budgets and
  200/201/400/800 widths. Typed causes are checked at the family failure boundary.
- Acceptance: authoritative make-tree build (`source_regression abc_structure
  isa_coverage_probe -j 8`) succeeds; `ctest -L '^source-regression$' -j 2`:
  **65/65**; the new Python test plus `phased_defaults`, `normalized_protocols`,
  `array_patterns` and `phased_member_calls`: **5/5 OK**. Focused throw-domain,
  failure-routing and phased import/flow checks: **5/5**. The final build and
  all checks were repeated after the allocation-order and edge-query corrections;
  all pass.
- Direct `abc_structure` byte comparison of all **40** inputs under both flag
  sets against read-only `csr87`: **617/617 ns**, **549/549 no**, **0 regressions,
  0 text changes**, **1,166** previously complete source files byte-identical.
  No cohort tools were run. Outputs and evidence are under
  `/private/tmp/abcd-guarded-compare-euc08j9g` (report:
  `/private/tmp/guarded-compare-report.json`).
- Whole-record o32/58223 service remains **partial (status4)**. m60551 no longer
  refuses `ExceptionInputError::kInternalValue` at original2193/working1497.
  Its next omitted-member cause is `PhasedMethodError::kControl` (6) →
  `ExceptionControlError::kMultipleExits` (4), **region8, body original313 /
  working310, handler338 / working335**. Original exits **336→340** and
  **327→363** (working333→337 and324→360). Iterator-close ownership/control
  remains outside this increment; Taskpool renewal and DataDriver nesting are
  still subsequent work, and the default-flags iterator-policy issue is parked.
- Details: [exception-throw-domain.md](exception-throw-domain.md). No commit/push.

## Phased literal defaults and typed protocol-rebase causes (2026-10-26)

- `PhasedFileFailure` retains `ProtocolRebaseError` semantic causes (`kInput`,
  `kAnchor`, `kCollision`, `kDispatch`) through the route/manifest/CLI chain.
  File-level resource/work mappings follow the existing quota convention.
  A refused relocation is checked through CLI JSON in `async_failure_test`.
- `PhasedParameterDefaults` owns the original phase graph/body and the ordered
  strict-undefined decisions, provided/missing edges, initialized SSA identities,
  initializer operations and original capture Before/Commit/storage evidence.
  Only a legacy length refusal enters projection; existing rest routes stay
  separate. The residual is reimported before capture planning, and
  `ProjectDefaults` rechecks it. The normal-parent default path and the ABI
  `.length` equality are unchanged. No new source-language premise.
- Primitive and string literal defaults move to native parameter syntax only
  with original phase-SSA use closure (including catch snapshots), private-cell
  identity and nonthrowing/order proofs. ABI, operands/origins, exception flow,
  RecoveryFlow parameter ports and family writes recheck the certificate.
  Generator entry additionally proves a closed, acyclic private prologue and
  callee identity. No arbitrary catch/finally or effectful initializer admission.
  Proof/index work targets O((N+E+U+P) log(N+P)); new arenas, sparse maps, shared
  owners and comparison/copy work are prepaid. Design: [family-defaults.md](family-defaults.md).
- Authoritative build passes:
  `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`.
  `ctest --test-dir build/regression -L '^source-regression$' -j 2 --output-on-failure`:
  **65/65 pass**, 100.58 s. `phased_defaults_test.py`, `normalized_protocols_test.py`
  and `array_patterns_test.py` are **OK**. The new fixture has **357 positive
  Node comparisons**, plus typed effectful-default and unchanged phased-rest
  negatives. Mutation/budget/CLI tests pass **6/6**; the selected generator-entry
  regressions pass **14/14**. The 200/201/400/800 proof work counts are
  694240 / 697646 / 1847792 / 3896438 for 1625 / 1633 / 4339 / 9139 instructions.
- Direct `abc_structure` comparison of all 40 inputs per flag set against
  read-only `csr86`: **616/616 ns** and **549/549 no** previously complete files
  remain byte-identical, **zero regressions/text changes**. No cohort tool ran.
  Whole record **o32/15324 is complete with --language-source** (status 0):
  m16026 emits `static async hypiumInitWorkers(abilityDelegator, scriptURL, workerNum = 8, params)`.
  With `--language-numeric --language-constructors`, the next refusal is
  **m16135 handleWorkerTestResult, pc 405**, `PhasedMethodError::kControl` (6) /
  `ExceptionControlError::kMultipleExits` (4): region 0 body 405/handler 430,
  exits 428 -> 432 and 419 -> 441. That control gap remains outside this increment.
- Evidence and exact commands: [build/phased-defaults/acceptance.md](../../../build/phased-defaults/acceptance.md).
  Input ABCs, `samples/` and `build/review/` were not modified; no commit/push.

## Normalization composition: await/yield coordinates after array-pattern folding (2026-10-26)

- Soundness fix, applied **unconditionally**, with no legacy-first gate:
  `protocol_rebase.{h,cc}` composes surviving `AwaitSite.pc`/`mode_pc` and
  `YieldSite.pc`/optional `mode_pc` through the fold's original-provenance map.
  These are working **byte offsets**, not instruction indices. Original resume,
  rejection, continuation and yield RETURN-route evidence stays original.
  Surviving original anchors cannot be absorbed by a fold. The old
  `array_patterns.awaited` fixture folded only after its sole await, so neither
  await anchor moved; it did not exercise this ordering.
- Array folding publishes its input fold ranges. The producer and independent
  verifier check injective coverage, opcode/operand/branch identity, anchor
  provenance, collisions, intact retained protocol intervals and selected catch
  correspondence. Removed anchors never map into replacement instructions.
  Invalid composition now retains typed `ProtocolRebaseError` causes (see the
  follow-up above); resource/work limits retain their file-stage convention. No opcode or source premise was added.
  The new shared mapping/dispatch pass targets O((I+T+H+A+Y) log(I+T+H)) with
  prepaid mapping, claims, certificate vectors and dispatch scratch.
- All streams enter the shared composition before phased import: ordinary and
  concurrent async, synchronous and asynchronous generators. The fixture has
  **187 Node differential cases** (55 async, 132 generator) for patterns before,
  between and after suspension, multiple folds, catch/rejection, iterator
  effects/errors, NEXT/THROW/RETURN and object/error identity. Unit mutations
  cover removed/split anchors, corrupted input/output modes, anchor collision,
  original RETURN routes and changed handlers, plus exact/exhausted byte/work
  budgets and 200/201/400/800 sizes.
- Existing owner limitations remain typed negative fixtures: async-generator
  array-pattern examples fail `FunctionEmitError::kKind` before folding;
  concurrent async remaps its awaits, then fails `ConcurrentError::kCorrespondence`
  in its pre-fold one-to-one publication check. These are not stale-certificate
  admissions or claims of new source support. Design: [array-patterns.md](array-patterns.md).
- Authoritative build passes:
  `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`.
  `ctest --test-dir build/regression -L '^source-regression$' -j 2 --output-on-failure`:
  **65/65 pass**, 101.60 s. Both `python3 tests/isa/array_patterns_test.py` and
  `python3 tests/isa/normalized_protocols_test.py` are **OK**. The new unit passes.
- Related command:
  `ctest --test-dir build/regression -R '(async|generator|await|protocol_rebase|array_pattern|normalized_protocols)' -j 2 --output-on-failure`:
  **39/42 pass**, 18.99 s. Three pre-existing failures reproduce with committed
  HEAD versions of both changed existing translation units, linked before the
  unchanged archive (no stash/worktree/source replacement):
  `file_generator_return_source_test` and `file_generator_return_loop_source_test`
  hit draft `TreePrintError::kResourceLimit` at loop_closure_file_test.cc:1311;
  `async_semantics_test` aborts with `std::bad_variant_access`. The untouched older
  build reproduces them too. No expectations or budgets were weakened.
- Direct `abc_structure` comparisons against read-only `csr85` outputs across
  all 40 inputs per flag set: **616/616 language-source** and **549/549
  language-numeric + language-constructors** previously complete records remain
  byte-identical; **zero text changes**. An initial plain-default invocation did
  not match the `no` baseline's numeric/constructor contract; that comparison
  was stopped and rerun with its actual flags. No cohort tool was run.
- Whole-record **o32/58223 service is partial** (`source_status: 4`), not complete.
  The stale-await refusal at m60551 original1015/working711 is gone. The next
  omitted member is m60551 (`asyncRunSpecs`), original **2193**, working **1497**,
  `ExceptionInputError::kInternalValue` (1), exception node1226/value48, at
  `ADD2_IMM8_V8` (v19). This separate value-domain proof gap remains refused.
- Evidence: `build/normalized-protocols/` contains build/test logs, the direct
  byte comparison and service receipt, baseline source/object replay and its
  failure logs. No input ABC, paired source, `samples/` or `build/review/` changed;
  no commit/push. The pre-existing untracked `sample.tar.gz` remains untouched.

## Shared uniform exception staging — increment 1 (2026-10-26)

- Added `ExceptionCopyEmission` with `StageGroup`/`StageCoverage`: original
  exceptional bundles remain authoritative; each physical prefix assignment
  owns its syntax once, with complete per-edge/operation/copy coverage. Uniform
  snapshots require retained operands available after their establishing Commit,
  the same handler/slots, and a legal entry before the whole emitted expression.
  Selection discovery carries pending obligations; evaluation, composition,
  forest/tree emission, operands, flow, origins and recovery recheck them.
- Recovery consumes the existing checked module-read/guard and initialized-cell
  GetValue correspondences. Full-completion dominance plus bidirectional
  universal boundary histories prove one stage per expression evaluation,
  including loops; a shared write index rejects slot interference. Shared slots
  remain physical through copy/storage cleanup because catch-entry bypass and
  inline comma staging are deferred. The final fallback revisits conservative
  raw guard-use counts only with the common sole canonical-use proof.
- Both existing schedules retain first refusal. The uniform trial runs only
  for an unmet native Reference with nonempty staging, admitting the retained
  refusal and trial arenas together and reserving downstream work. Known proof
  work is charged; failed subpasses without full cost reports consume their
  reserved allowance. New indexes target O((N+E+U+K) log(N+K)), linear storage,
  prepaid allocation and typed refusals. No source-language premise was added.
- Promoted `staged.js`: **52** Node cases for failure timing, getters/coercions,
  nullish arms, call failures and array/object/function/primitive results. Kept
  the original **114** cases, plus a typed unproved capture/guard refusal.
  Certificate/physical-tree/recovery mutations cover source, slot, placement,
  coverage, entry and handler; boundary tests reject one-time staging for a
  repeated expression. Exact/exhausted budgets and 200/201/400/800 scales pass.
- Whole-record **CameraService is complete**: `source_status: 0`,
  `source_complete: true`, no `source_failure`; entity 13268 pc 138 is admitted.
  **OhReport** is byte-identical to o32 (12,483 bytes, SHA-256
  `cfd968198580026d80c5ae584fecd040cea5d7d4f2a94e6b4eacf5e216f3cb47`);
  **EditPage** is byte-identical to o36 (9,294 bytes, SHA-256
  `46f67c1226d6e78a9cb665230a3edd5d1bd2ca68c886c366ea64e6398437869a`).
- `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`:
  passes. `ctest --test-dir build/regression -L source-regression -j 2 --output-on-failure`:
  **65/65** pass (100.53 s). `python3 tests/isa/phased_member_calls_test.py`:
  **OK**, **166** differential cases plus typed refusal. Related sweep:
  **14/14** pass (17.86 s); `exception_copies_test`: **1/1** pass.
  Commands, file list and logs: `build/phased-member-calls/shared-staging/acceptance.md`.
  Design: [call-references.md §5.6](call-references.md#56-uniform-exception-snapshots-for-composed-phased-expressions-2026-10-26).
- Remaining limits: nonuniform/unavailable snapshots and unproved guard or
  suspension compositions keep typed refusals; no general catch/finally work.
  No commit/push, cohort tools, paired-source engine input, or modifications to
  input ABCs, `samples/` or `build/review/`.

## Phased prefix scheduling stability — review fix (2026-10-26)

- Took option B: run the complete original phased pipeline with predicate-only
  prefix discovery first. Preserve that schedule when all member References
  close; retry once with selection enclosures only for an unmet Reference.
  The census shares CallPlan binding classification, excluding protocol calls
  and undefined-receiver apply calls from member obligations. Typed `PhasePrefixDomain` selects the independently recomputed equations in
  builder, refresh, binding and fresh verification. The retry charges original
  work plus the Reference census and releases original arenas before rebuilding.
  No names, paths, printed text, layout tests or new source premise participate.
  Existing complexity and typed resource/refusal obligations remain binding.
- The regression unit uses a retained getter shared by a predicate and arm in
  an already-complete value call. It now keeps its original prefix/snapshots;
  legacy member-call compositions keep their domain too. Forged domain changes
  refuse; exact/exhausted budgets and 200/201/400/800 scaling cover preserved
  schedules and the expanded repair. The 114-case Node differential still passes.
- Fresh o32 whole-record emission restores **OhReport.mjs byte-for-byte** against
  the read-only `build/review/csr82/ns/o32` baseline: **12,483 bytes**, SHA-256
  `cfd968198580026d80c5ae584fecd040cea5d7d4f2a94e6b4eacf5e216f3cb47`. `cmp` exits 0.
  Output: `/private/tmp/abcd-phased-ownership-review-2mz3in37/final-o32`.
  The review diff/baseline were read only; no cohort tools ran.
- `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe phase_composition_test -j 8`:
  passes. `ctest --test-dir build/regression -L source-regression -j 2 --output-on-failure`:
  **65/65** pass. `python3 tests/isa/phased_member_calls_test.py`: **OK**.
  Additional related ISA/composition sweep: **14/14** pass.
  Fresh CameraService failure is structurally identical: entity **13268**,
  pc **138**, CallPlanError **4** / CallReferenceError **1** / TemporaryReason **4**;
  the nonempty catch-staging limit is unchanged. Commands and evidence:
  `build/phased-member-calls/review-stability/`. No commit/push; input ABCs,
  `samples/` and `build/review/` were not modified.

## Phased member-call argument composition (2026-10-26)

- Extended the shared phased predicate-prefix index with original selection
  enclosures: condition/arm roots and exact merged-value/phi-edge occurrences.
  A retained getter used by the predicate and a selected arm can now belong to
  one argument-local prefix. Existing effect order, canonical-use scope,
  composition, receiver/call, exception and final-tree/recovery proofs still
  decide admission. Builder, refresh, original-site binding and fresh selection
  verification reconstruct the index. Storage is prepaid; added indexing is
  linear before existing sorts, within O((N+E+U+K) log(N+K)). No source premise
  was added. Design and staging limit:
  [call-references.md §5.5](call-references.md#55-complete-phased-argument-subtrees-2026-10-26).
- Added pinned O0 `phased_member_calls` ABCs, append-only ISA producer, and a
  **114-case** Node differential: the original concatenation reproducer,
  templates, before/after await, both nullish arms, optional calls inside member
  arguments, independent conditional subtrees, getter/argument/coercion/call
  failures, callee replacement, receiver identity and an independently throwing
  callee-base getter. A separate live-result fixture preserves the typed catch-staging refusal. Composition units cover
  equivalent register copies, escaped inputs, unrelated effects, omitted-prefix
  and per-throw-handler mutations, exact/exhausted budgets and retained-input
  wrapper scaling at 200/201/400/800.
- Authoritative build passes:
  `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`.
  `ctest --test-dir build/regression -L source-regression -j 2 --output-on-failure`:
  **65/65** pass. `python3 tests/isa/phased_member_calls_test.py`: **OK** (114 Node
  cases plus the typed refusal). The prior 11 ISA source tests plus
  `isa_wide_calls_source_test` and the new producer pass: **13/13**; fresh
  `phase_composition_test` also passes (**14/14** combined sweep).
  Commands, logs and current record status: `build/phased-member-calls/`.
- **CameraService remains incomplete**, method entity **13268**, pc **138**:
  `CallPlanError::kReference` (4), `CallReferenceError::kNotInline` (1),
  `TemporaryReason::kCrossRegionValue` (4). Fresh whole-record emission to
  `/private/tmp/abcd-phased-member-final-92e73ds6/record` confirms this; EditPage
  remains complete. The method has a separate nonempty catch-copy obligation:
  bytecode register v4 holds the current result and remains live after catch.
  Every logger-span throw stages that value into the same private catch slot
  (current graph: source 33, catch phi 19, slot 0). At the length getter pc 119,
  selection discovery refuses the staged arm before composition can run.
  Equal snapshots are insufficient placement evidence: catch staging remains
  statements at original Before ports, and inline emission still rejects
  `kCatchStaging`. An additional exception-copy placement/coverage certificate
  and emitter/operand/flow/recovery work are required; this increment preserves
  the refusal rather than discarding those obligations. No cohort tools ran;
  input ABCs, `samples/` and `build/review/` were not modified. No commit/push.

## Classes in phased creators, increment 1 (2026-10-26)

- Added a borrowed normal/phased `ClassCreator`, one phased class inventory and
  original-PC index, and a Before/environment/Commit/exception occurrence proof.
  Classes use the existing creator preparation/descendant/binding pipeline and
  exception-aware capture planner. Phased function bindings remain separate.
  Forest/operand/finish/family-write checks require the real binding and fresh
  occurrence; scope independence alone no longer admits a phased class. Fresh
  exception-tree verification preserves execution point, Commit availability and
  the creation exception edge. No source-language premise was added. Design:
  [phased-classes.md](phased-classes.md).
- Added the pinned O0 `phased_classes` fixture, append-only ISA producer and
  **28-case** Node differential: both reproducers, captured parameters/mutable
  cells/catch locals, escaping methods, async fulfillment/rejection before and
  after await, generators, fresh identity and initialized module function reads.
  Site/PC/environment/Before/Commit/original-coordinate and missing-binding
  mutations refuse with typed errors, including independent family-write checks.
  Inventory work at 200/201/400/800 sites: **5215/5241/16315/33115**, over
  **6025/6055/20876/42476** actual value inputs (compiler register copies differ).
  Exact-work and exhausted-work/byte checks pass; these are inventory measurements.
- Authoritative build passes:
  `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`.
  `ctest --test-dir build/regression -L source-regression --output-on-failure`:
  **65/65** pass. `python3 tests/isa/phased_classes_test.py`: **OK** (28 cases).
  Related ISA source sweep: **11/11** pass. Class/capture checks including the
  new proof test: **6/6** pass. The existing deferred-loop regression now reaches
  typed `kClassDeferredDeclaration` (16), preserving its refusal and location
  assertions. Normal `local_classes` and `inherited_modules` source bytes match
  the pre-change CLI baseline exactly. Commands/logs: `build/phased-classes/`.
- Descendant heritage retains `kClassHeritage` (17); no member installation or
  initializer is fused across exception ports. Computed/static forms outside the
  shared path and deferred/unsupported capture obligations remain typed refusals.
  Node is independent source evidence; no Ark/device, full-process RSS or cohort
  run was performed. No input ABC, `samples/` or `build/review/` was modified.
  Changes remain uncommitted at the user's request.

- Review correction (2026-10-26): removed discarded `({});` from the new phased
  class output. The allocation was an unused private Hole token for absent
  heritage; NEWLEXENV already had the checked environment disposition. The
  shared Hole protocol now records non-native phi storage, freshly rechecked
  by evaluation verification. Heritage-only pure transport uses anchors with
  no token; real comparisons/rethrows/phi transport keep their source token.
  Added the ISA text assertion and typed proof mutation; no premise was added.
  Rebuilt the authoritative targets and reran source-regression **65/65**, the
  **28-case** Python/Node fixture, the **11/11** ISA source sweep, **2/2** proof
  units and **3/3** exception/completion Node tests. Normal local_classes and
  inherited_modules source bytes still match the saved baseline. Logs and
  read-only CameraService diagnosis/proposal: `build/phased-classes/review-fix/`.
  CameraService method 13268 (`getSupportedCamerasFn`) still refuses at pc 138:
  saved `Logger.info` from pc 44 remains a cross-region root across the optional
  access and concatenations in its message argument; the member-reference call
  proof requires inline ownership. A small O0 JS reproducer fails identically;
  controls without that combined phased expression succeed. No implementation
  of this follow-up, cohort tool run, commit or push was performed. The user's
  cohort review reports EditPage complete in both flag sets with zero prior
  complete-record text changes or regressions; that evidence was not rerun here.

- Review evidence: cohorts 614 -> 615 / 547 -> 548 (o36 `EditPage.mjs`
  complete in both flag sets), zero regressions and zero text changes
  (`build/review/csr82`); source-regression 65/65 at preset parallelism.
  o36 `CameraService` now stops later, at a phased call whose member callee
  read crosses an optional-chain template argument (CallReferenceError).

## Range derived constructor control, increment 1 (2026-10-26)

- Added a shared range certificate for every super/return/check/binding site,
  receiver-state CFG dataflow and SSA identity/return proofs. Branch-local super,
  early object/function/primitive returns, undefined completion, repeated super,
  loops and pre-super this reads use native constructor semantics. Throwing
  checks retain physical flow phases and all-site verification. No source
  premise was added. Spread/forwarding/implicit admission paths are unchanged;
  only existing transparent-rethrow normalization admits handlers.
- Added `tests/fixtures/isa/derived_control/` with pinned O0 compiler command and
  source/ABC/tool hashes, a Python ISA producer and 57-case Node differential.
  Registered its CTest beside array_patterns and appended its coverage producer.
  Existing derived analysis tests also cover shared guards, mode mutations,
  missing receiver checks and zero work/byte budgets. Accounted general-proof
  work at 200/201/400/800 sites: **110416/112169/232061/486455**; the doubling
  assertions pass. Existing decoded initialization cases: **720**.
- Authoritative build passes:
  `cmake --build build/regression --target source_regression abc_structure isa_coverage_probe -j 8`.
  Required `ctest --test-dir build/regression -L source-regression --output-on-failure`
  passes **65/65**. `python3 tests/isa/derived_control_test.py` passes (57 Node
  outcomes); the derived/derived_control/super_stores/construct_spread/
  inherited_modules/local_classes/instance_fields/wide_ops ISA source sweep
  passes **8/8**. Final targeted `derived_constructor_test` passes.
- Additional derived/forwarding/capture/heritage/class-initializer sweep:
  **10/15** pass. The five derived_cells/literal_receiver failures reproduce
  **5/5** on a fresh source snapshot/build of starting HEAD `f14c96d8`, with the
  same family diagnostic/tree cause and assertion at line 412. They remain open
  pre-existing failures, not passing evidence. Full commands/results and logs
  are retained under `build/derived-control/`.
- Existing `derived/basic-O0.abc` generated source is byte-identical to its
  pre-change output, including implicit/spread subclasses. Forwarding `fields`
  and `pure` outputs are byte-identical to a fresh starting-HEAD CLI build;
  forwarding unit/CLI checks pass. This is local fixture evidence only. No cohort
  tools, full-corpus RSS run or Ark/device execution was part of the
  implementation round. Review evidence: cohorts 613 -> 614 (o38 `driver.mjs`
  complete; its derived constructor prints correct native `super`/`return null`
  with residual `this;` reads and an unfolded `null !== (void 0)` selection, a
  readability follow-up), 547 unchanged, zero regressions and zero text
  changes (`build/review/csr80`). General catch/finally, spread
  control-flow expansion and unproved receiver/return forms remain out of scope.

## Array patterns, increment 1b: native source (2026-10-26)

- `ValueArrayPattern` lowers like any ordered user-code operation: shape
  `kInput` (step/close read the private record through the new
  `InputEvaluation::kPatternRecord`, protocol purpose, `PatternRecord` domain
  only), tree node `ArrayPatternExpr`, callback effects, operand verification in
  `tree_operands` under `ArrayPatternAcquisition` (V10 premise of 2026-10-23).
  The verified tree keeps `r = Acquire(e); [t =] Step(r)...; Close(r)` as
  ordinary statements with temporaries.
- `PlanArrayPatternSurfaces` (printer-only, in `PrepareSourceVariables`) certifies
  each run that is contiguous in one block, untouched by omission, motion,
  replacement, loop-header or snapshot projections, with all step/close
  operands reading the acquire's record. It prints `[t, , u] = e;` or, when
  every bound target is a `let`/`const` initializer with distinct storage, one
  `const`/`let` pattern declaration; folded steps/close keep boundary origins.
  An uncertified `ArrayPatternExpr` refuses to print.
- Evidence: fixture `tests/fixtures/isa/array_patterns` (holes, trailing hole,
  reassignment, dead inner pattern, try, loop, await) with a 39-case Node
  differential of the logged iterator protocol (cached next, per-element steps,
  return only when not done, none after a throwing step, throwing return,
  non-iterables); all seven destructurings of o38's `onRemoteMessageRequest`
  decompile (that record stays blocked by its derived constructor: conditional
  `super` plus `return null`). Cohorts unchanged (613 / 547, no text changes,
  `build/review/csr79`); ISA ledger unchanged (`isa-cc22`). The dev-tree sweep
  shows 34 failing tests that fail identically at 28dc7123 (pre-existing; next
  task); no new failures.
- Still refused (unfolded by the normalizer): empty patterns, captured
  (`STLEXVAR`) targets in the source layer, patterns nested in a for-of region,
  defaults, rest and nesting.

## Array patterns, increment 1a: analysis layer (2026-10-25)

- `NormalizeArrayPatterns` (source layer, run in the phased file reader after
  generator/async normalization) matches the exact pinned O0 es2panda
  array-pattern protocol for holes and single register/STLEXVAR targets, checks
  closure (no outside branch/handler into the interior) and that removed
  protocol registers are dead at every exit, removes the protocol and its try
  regions, rebases offsets and emits `PatternSite` certificates. The lifter turns
  them into `ArrayPatternOperation` (acquire/step/close) with a dedicated
  `ValueKinds::PatternRecord` internal record, imported as `ValueArrayPattern`.
  Design: [array-patterns.md](array-patterns.md).
- Not yet source: evaluation/tree/verification of the pattern statement is the
  next increment; such methods now refuse at evaluation planning
  (EvaluationShapeError) instead of the earlier region/control refusals.
- Evidence: cohorts unchanged (613 / 547), no text changes (`build/review/csr78`);
  misc-evidence array-pattern refusal cause and the register value-flow kind
  enumeration updated.

## Sendable local and lazy module reads (2026-10-25)

- `LDSENDABLELOCALMODULEVAR` (narrow/wide) lifts as a local, and
  `LDLAZYSENDABLEMODULEVAR` (narrow/wide) as a lazy external, module read in
  the sendable domain (v7 `LdSendableLocalModuleVar` /
  `LdLazySendableExternalModuleVar` resolve the executing sendable function's
  module record). FunctionInput now requires owner/load domain agreement for
  local loads too (the sendable-local proof), so the module enclosure accepts
  local bindings in sendable trees. Lazy and external sendable reads still need
  their hole-guard pair; a sendable local read may carry the ordinary
  initialized-local proof instead.
- Evidence: recovery fixtures module-sendable-lazy / module-sendable-local
  (260 imports/locals: narrow and wide), O0 and default lazy complete with the
  isa_recovery Node differential; default local refuses typed
  (EvaluationError / LiteralError on the 260-element literal). Ledger (`build/review/isa-cc20`): SupportedVerified 218 → **222**, Unmeasured 65 → 61. Cohorts unchanged (613 / 547), no text changes (`build/review/csr77`).

## Intrinsic TypeError throws; generator-normalized ledger rows (2026-10-24)

- THROW_CONSTASSIGNMENT and THROW_NOTEXISTS lift as
  `ThrowKind::kIntrinsicTypeError` (v7 `RuntimeThrowConstAssignment` /
  `ThrowThrowNotExists` create a fresh realm TypeError, no user code; the
  const-assignment name register only feeds the unpreserved message). They
  print like the pattern-coercibility throw, `throw ({} = null)`, a native
  intrinsic TypeError without a global lookup.
- ISA probe (test-only): in a generator/async-generator method that the
  production `NormalizeGenerator` accepts, an instruction refused by raw
  lifting and removed by that normalization is reported with producer
  `abcd::source::GeneratorNormalization` (rows also carry `roles`).
- Evidence: fixture const_assignment (Node 4); throw evidence const fixtures now take a Node differential. Ledger (`build/review/isa-cc19`): SupportedVerified 211 → **218** (generator/async-generator creation, state and settlement, THROW_NOTEXISTS, THROW_CONSTASSIGNMENT), Unmeasured 72 → 65. Cohorts unchanged (613 / 547),
  no text changes (`build/review/csr76`).

## Native instance fields; gap regions (2026-10-24)

- Stored instance initializers (`instance_initializer` + private slot +
  constructor-entry `callruntime.callinit`) become native class fields for
  base classes; design in [instance-fields.md](../instance-fields.md). Derived
  classes, capturing initializers and private names stay refused.
- Exception regions: a try region lying wholly between an open region's
  protected body and its handler (array-destructuring normal close) nests in
  the next enclosing region instead of failing `kCrossingScopes`.
- Evidence: fixture instance_fields (Node 8; dropped-field mutant fails).
  Cohorts 612→613 / 547 (o32 ArgumentMatchers), no regressions, no text
  changes (`build/review/csr74`).

## Object-literal methods (2026-10-24)

- `CREATEOBJECTWITHBUFFER` method entries (`key + METHOD* + METHOD_AFFILIATE`)
  are indexed creation occurrences with a key ordinal, bound at the object
  instruction (normal and phased parents), read as family children and
  printed as native shorthand `[async ][*]key(params) { … }` (v7
  `CreateJSFunctionInLiteral`: no-prototype non-constructor; `CloneObjectLiteral`
  sets env and home object). Bindings are keyed by (operation, ordinal); family
  writes require one child edge per method entry. Accessor placeholders and
  async generator methods stay refused. Design: [object-buffer-source.md](../object-buffer-source.md).
- Evidence: fixture object_methods (Node 9 cases: captures through try,
  receivers, key order, name/length, non-constructability, async/generator).
  Cohorts 611→612 / 546→547 (o12 query_auth complete), no regressions, no
  text changes (`build/review/csr71`).

## Deferred declarations with several stores (2026-10-24)

- A deferred cell's declaring store in another block than its allocation was
  admitted only as the cell's single store. It is now also admitted when it
  dominates every other access of the cell (`let timer = null; function
  done() { … timer … } … timer = later;`), one dominance query per access.
- Evidence: fixture deferred_stores (Node 4; baseline refuses). Cohorts
  unchanged (611/546); o32/58223 moves from refusal to partial (status 4):
  only member m60551 asyncRunSpecs (an array-destructuring for-of nested in
  a for-of) is omitted. Sweep: only known failures.

## Captured for-of/for-in loop variables (2026-10-24)

- es2abc stores the iteration value into a fresh per-iteration environment
  when a closure captures the loop variable. The lexical-store verifier now
  accepts a stored value whose use is a verified protocol input (for-of
  iteration input / for-in enumeration input, indexed once by use) whose
  expression is the store's right-hand side. Output: `for ($k of xs) { let
  $local = $k; … }`, a fresh binding per iteration.
- for-in head scope: a non-throwing environment pop does not separate the
  acquisition from `next` in the order proof; forwarded handler blocks admit
  typed `scope_pop` operations in for-in recovery.
- Evidence: o32/46008 hypium configService completes (611/620 source;
  previous unchanged 546/620); no other text changes. Fixture
  captured_loop_bindings (Node: deferred closures per iteration); baseline
  refuses it. for_in_plan_test's `captured` case moves from refusal to a
  proved plan (its context-free evaluation/emission checks skip cell graphs).
  o32/58223 returns to its root m58418 (deferred cell for child m59227).
  Remaining: a captured `let` loop variable reassigned in the body
  (variable projection) and for (let i…) per-iteration copies.

## for-in keys as method-call receivers (2026-10-24)

- A for-in key value (`kNextResult`, incl. copies) is the loop variable for its
  iteration: the call plan's stable-receiver check and the tree operand
  receiver check accept it, as they already do for for-of bindings
  (`for (k in o) k.run(c)`).
- Evidence: for_in_rhs gains receiver cases (Node 12). Cohorts unchanged
  (610/546), no text changes; o32/58223 advances to m61403, a for-in whose
  loop variable is captured per iteration (same family as for-of 46008).
  property_receiver_test (abcd_analysis unit) aborts identically at the
  session-start commit 76f52498; recorded as pre-existing.

## for-in with a protected right-hand side (2026-10-23)

- es2abc protects the for-in RHS (property read/call) and the acquisition by
  one transparent rethrow region. The emission view now admits other throwing
  operations in that forwarded region; recovery maps each to a forwarded
  exception whose target is the forward's destination catch (or the caller),
  and requires the operation's own source action to throw exactly there with
  no finalizer.
- Evidence: o32/21089 hypium core completes under both flag sets (610/620
  source, 546/620 previous); no other text changes. Fixture for_in_rhs (Node
  12: RHS getter/call throws, break, order); baseline refuses it.
  for_in_evaluation_cli moves 21089 to the completion list; async_failure
  follows 58223 to m58646's later call plan. A for-in whose advance is itself
  inside a user try still refuses. Sweep: only known failures.

## Async `arguments` prelude; refined shared-terminal follows (2026-10-23)

- Async prelude: GETUNMAPPEDARGS before ASYNCFUNCTIONENTER is admitted like a
  hoisted function creation (effects only kArgumentsRead|kAllocation; Ark
  snapshots before the promise, the spec inside FunctionDeclarationInstantiation
  after it; only allocation failure differs, excluded by the contract).
- Control planning: a refined shared terminal is skipped as an early abrupt
  arm only when the sibling arm does not itself fall into it (postdominance);
  otherwise it is the join (`if (a) { if (b || c) return; } if (d) {…}`). The
  refined pass only runs after a first-pass conflict.
- Evidence: fixtures async_arguments (Node 4) and control_shapes
  guardThenBranch (Node 224 total). Cohorts unchanged (609/545); o32/58223 now
  reaches m58646's for-in catch state (shared with o32/21089). Sweep: only
  known failures.

## for-of bodies with nested user try statements (2026-10-23)

- Close-entry loop discovery follows exception arcs into live Catch nodes; a
  Catch is a forest root only when nothing raises into it (a user catch that
  rejoins the loop no longer makes it irreducible).
- Protocol forwarding leaves a non-forwarding try that lies wholly inside a
  protocol loop body as an ordinary user try (merged wrapper intervals,
  O((W+R) log W)); both the seal and the control projection pass the
  loop-body wrapper regions.
- The for-of iteration proof's loop forest gives ordinary try wrappers
  fan-out arcs to their body and handler (back edges mapped to topology arc
  ids); counts and activation states propagate from a wrapper into those
  entries instead of seeding them as fresh roots.
- Evidence: fixture for_of_user_try (catch with effects, continue, rethrow,
  replacement throw, async awaits): Node 60 cases incl. iterator return.
  Cohorts unchanged (609/545); o32/58223 now reaches m59447 (async completion
  check). Still refused: break from a catch and try/finally in a for-of body.

## Statement units never enter a condition prefix (2026-10-23)

- `ControlVertex::statement` marks try wrappers, try exits and protocol close
  units. Regional decision discovery refuses a prefix whose follow chain from
  the evaluated node to its predicate contains such a unit (e.g. the finally
  pending-throw check of `if (c) { try {…} finally {…} }` sharing the `if`'s
  join); planning then stays ordinary. Layout is otherwise unchanged.
- Evidence: o24/37520 Camera_js CameraService (releaseCamera) completes under
  both flag sets (609/620 source, 545/620 previous); no other text changes.
  control_shapes gains finally-in-if cases (Node 192). evaluation_input_cli
  expectation moves to completion. Sweep 504: only known failures.

## Split loop-exit corridors; local-class capture requirements (2026-10-23)

- Loop exit corridors: es2abc's shared `jmp` to a for-update block is split
  per incoming edge. When a loop's retained continuation is itself a private
  one-arc corridor into the node the absorbed exits now reach, absorb it too
  so all exits converge (only loops that would otherwise refuse with
  `ControlError::kLoopExits` are affected).
- Descendant classes: the blanket `kClassCaptureRequirements` gate is lifted
  once `BindCreatorClasses` has bound every member against the creator's
  actual storage and no body needs a deferred declaration. Unsatisfiable
  demands (e.g. a grandparent binding the intermediate function never
  captures) still fail binding as kClassCaptureRequirements; late `let`
  declarations still refuse as kClassDeferredDeclaration.
- Evidence: o1/3011 CardCalc completes under both flag sets (608/620 source,
  544/620 previous); no other text changes. Fixtures loop_corridors (Node 329
  cases) and local_classes (live captures, shared mutation, module binding);
  creator_classes_cli_test now publishes `member` with a Node differential.
  Sweep 500: only known failures.

## Control planning: exitless try wrappers and empty decision arms (2026-10-23)

- `ControlTermination::kExitless`: a try wrapper with no exit port (body and
  handler only return/throw/diverge) cannot fall through to an enclosing arm's
  continuation. Planning treats it like kNormal (no layout change); the region
  verifier no longer refuses it inside a bounded arm (`if (a && b) return;
  try {…}`).
- The planner falls back from a short-circuit decision to an ordinary
  conditional when the continuation join equals the shared target (an empty
  shared arm, e.g. `else if (c) { if (d) return; }`), and, in projection views
  only, when the join is the private terminal fallback while the shared arm,
  which is the lexical follow, is not all-abrupt (a try's normal exit port).
- Evidence: o15/37098 KikaInput KeyboardController completes (607/620 source;
  previous unchanged 543/620); o36/11209's m12194 completes and the record
  now stops at a class creation in a phased method (kPhasedSite, shared with
  o36/19453). No other text changes. Fixture control_shapes: Node 128 cases
  (baseline refuses). Sweep 484: only known failures.

## Phased creators hand on initialized module slots (2026-10-23)

- Children of a phased creator inherit the creator's own creation-time
  initialized module slots (no parent-local stores), so unguarded local module
  reads in their closures are proved. See module-reads.md "Phased creators
  hand on their entry set".
- Evidence: o8/10284 KeyboardController completes under both flag sets
  (cohorts 605→606/620 source, 542→543/620 previous; zero regressions, no
  other text changes). Fixture inherited_modules Node differential 7 traces;
  class_omissions_cli / evaluation_failure_cli expectations move from partial
  to complete. Sweep 458: only known failures.

## for-of body scope cleanup and sequential loops (2026-10-23)

- for-of bodies with a captured block binding (a `poplexenv; throw` cleanup
  try forwarding into the close handler) and sequential for-of loops in one
  method now complete. Body proof admits trys wholly inside the wrapper body;
  forwarding admits scope pops and ends in protocol-owned handlers whose entry
  coverage is certified; recovery resolves such forwards to the throw-close
  entry and keeps typed exclusions (resource, number identity, guards). The
  blanket multi-acquisition gate is now true nesting via derived loop bodies.
  Details: for-of-remaining-close.md "Body scope cleanup and sequential loops".
- Evidence: fixture tests/fixtures/isa/for_of_scopes (O0) Node differential
  274 cases (`isa_for_of_scopes_source_test`); for-of tests 47/47;
  source-regression 65/65; broad sweep 261 tests with only known failures
  plus async_failure_cli_test, whose o32/58223 expectation moves forward
  (m61706 completes under --language-source; next stop m61057 PC192).
  Cohorts unchanged (605/620, 542/620), zero text changes.
- Remaining gates on the o32 cluster: 15324 FunctionAbiError 7 (m16026);
  42397 true nesting; 46008 captured loop binding (declaration-form for-of);
  58223 m61057 async for-of exits; break out of a scoped body
  (kEnvironment); user try in a for-of body.

## Readable slice 4: loop headers (2026-10-23)

- Loop header pieces were each wrapped in their own `(` `)` / `(…, true)`
  because they may compose inside conditional arms. A piece that fills a whole
  for/while slot (a full Expression context) now prints without that outer
  grouping (LoopHeader/LoopHeaderCursor `top`). The last item of a comma list
  prints at assignment level; nested compositions (the `&& (` exits and
  conditional arms) keep their parentheses. The prefix initializer statement
  `( … );` no longer double-wraps its items (LoopInitializerCursor `grouped`).
  The for-init slot keeps item grouping (Expression[~In]).
- Effect: `(($phi = 0));` → `($phi = 0);`, `for (…; ($i < n); …, (++$i))` →
  `for (…; $i < n; …, ++$i)`. 66 / 43 complete files change (source /
  previous flags). grouping_bytecode.py --changed-only: 66/66 and 43/43
  bytecode-identical at default and O0, with Node 66/66 and 43/43. Completion
  unchanged (605/620, 542/620). source-regression 65/65; the broad sweep
  failures are only the known list.

## Readable yield spelling (2026-10-23)

- YieldExpr printed its own parentheses, `(yield x)`, in every context. It now
  prints `yield x` / `yield* x` with an assignment-value operand. Its
  assignment-level precedence (TreeExpressionPrecedence) lets the enclosing
  operand context add grouping where the grammar needs it (operand of a binary
  operator, conditional test, and so on). Generator fixtures still pass (42 +
  21 Node cases). file_async_generator_yield counts `\byield` instead of
  `(yield`. No complete cohort record changes (605/620, 542/620).

## Async generators: entry resume slot liveness (2026-10-23)

- NormalizeGenerator refused any user touch of the async generator's entry
  resume result/mode registers. It now refuses only when they are observed:
  InitialSlotsObserved runs a forward two-bit may-hold-entry-value analysis
  over normal successors (a suspend resumes at the next instruction) and user
  catch handlers. Masks grow monotonically, so each instruction is processed
  at most three times; the mask and worklist are charged before allocation.
  A read before an overwrite still refuses.
- Simple async generators now complete at O0 (`yield await x`, sequential
  yields): `tests/fixtures/isa/async_generators` with
  `tests/integration/async_generators.mjs` (21 cases: next/throw/return plans,
  sent values, for-await). Yields in loops still refuse at
  AwaitCatchError::kObservedRegister: the async return-at-yield route reuses a
  resume mode register as its resolve value, which is the documented
  unsupported injected-return case. The ledger is unchanged (210 verified),
  because CREATEASYNCGENERATOROBJ and the async generator settle instructions
  have no per-instruction lifter. Cohorts unchanged.

## Synchronous generators complete (O0); coroutine ledger rows (2026-10-23)

- generator_completion: this SDK opens the generator's completion catch at
  PC 0, before the frame copies, not at CREATEGENERATOROBJ. NormalizeGenerator
  now accepts a catch start anywhere in the non-throwing prefix (fresh
  environment, frame copies and fresh-cell copies, with allocation failure
  excluded). Simple generators, generators with sent values in loops, and
  `yield*` delegation now complete at O0. Default-optimized generator lowering
  (hoisted constants, register mode compare, sta/lda rethrow) and try/finally
  generators still refuse.
- New `tests/fixtures/isa/generators` (O0) with `tests/integration/generators.mjs`
  (42 cases: next/throw/return plans, sent values, yield* to traced iterators
  with and without return/throw, arrays). throw-evidence `delegate-o0` now
  completes and passes a 9-case differential for THROW_NOTEXISTS. The
  isa_coverage_matrix_test fixture moves to the generator ABC, which still has
  a raw refusal (CREATEGENERATOROBJ).
- Probe (test-only): coroutine protocol instructions refused as
  kUnsupportedInstruction under a supported profile report their typed
  LiftCoroutineInstruction result; profile refusals are never overridden.
  Pinned specimen expectations are refreshed for exactly those rows: 4 current
  and 16 deprecated (supported profiles only).
- Ledger (`build/review/isa-cc20`): SupportedVerified 206 → **210**
  (RESUMEGENERATOR, GETRESUMEMODE, SUSPENDGENERATOR, ASYNCFUNCTIONAWAITUNCAUGHT),
  Unmeasured 77 → 73. Cohorts unchanged (605/620, 542/620).

## Async preludes, deferred cells, cleanup handler moves (2026-10-23)

- Async prelude (async_completion.cc): a hoisted function declaration
  (DEFINEFUNC, CreationKind::kFunction, home kCurrentFunction) before
  ASYNCFUNCTIONENTER is admitted when allocation failures are excluded (same
  contract as the NEWLEXENV prelude). Its effects must be within heap, module
  and environment reads plus allocation. The spec runs
  FunctionDeclarationInstantiation after NewPromiseCapability; creation runs no
  user code and no call is admitted before the promise, so only OOM timing can
  differ.
- Completion catch: an optimizer reload `sta c; lda c;` before
  ASYNCFUNCTIONREJECT is accepted (acc unchanged), and a code_size-0
  completion catch (es2abc) is accepted as extending to the end of code.
- Deferred initialization (exception_locals.cc): a captured cell with several
  direct stores now has a deferred initialization when one store dominates all
  the others (that store is the declaration; the rest are assignments). The
  existing conditions are unchanged: Language closure writes (no reset to
  Hole) and allocation dominance.
- Environment cleanup (environment_cleanup.cc): a handler may contain
  `sta vR`/`lda vR` moves around POPLEXENV (each `lda` reads a register saved
  in the same handler; the THROW leaves the method). A code_size-0 handler
  extends linearly to its first THROW. The moves are reported as a new
  CleanupInstructionKind::kMove ("move" in positions and instruction sources).
- Effects: o32/58223 (hypium service.mjs) passes the async prelude and the
  `done`/`timer` capture, and now stops at a different method's
  ExceptionControlError::kMultipleExits. The class-field sample passes cleanup
  and stops at the stored instance initializer (CreationContextError::kKind).
  Default-optimized async fixtures expose further gaps (promise identity,
  zero-length user handlers, await regions); these were not pursued, because
  cohort builds do not use that shape. Cohorts unchanged (605/620, 542/620).

## CALLRUNTIME_CALLINIT lifted as CALLTHIS0 (2026-10-23)

- interpreter-inl.cpp:1499 implements CALLINIT as "same as callthis0":
  accumulator callee, explicit this register, no arguments, result in acc.
  named_invocation lifts it on the same ReceiverCall path; there is no new
  semantics. Records with class field initializers still refuse in the
  initializer projection and environment cleanup (the instance initializer
  stored in a lexical slot and called by CALLINIT; the default build's
  `sta; poplexenv; lda; throw` cleanup handler with code_size 0). That
  projection is the next class-field step. Cohorts unchanged. The
  current-variant lift expectation is refreshed, and isa_coverage_test's
  unsupported sample is now CREATEGENERATOROBJ.

## Transparent sta/lda rethrow handlers; derived fixture (2026-10-23)

- `EraseTransparentRethrowRegions` admits a handler sequence of
  `sta vR`/`lda vR` moves followed by THROW, where each `lda` reads a register
  stored from acc in the same sequence. acc still holds the caught exception
  and the registers are dead because THROW leaves the method; the original
  "no region covers a handler" condition is unchanged. Multi-instruction
  sequences are admitted only in the trailing chain of handler sequences that
  end the method, which the cleanup removes whole. Mid-method sequences (such
  as the for-in acquisition handler) keep their regions as before, because the
  protocol recognizers depend on them. Without that restriction three o32
  records regressed; with it, both cohorts are unchanged (605/620 source,
  542/620 previous; zero regressions and zero text changes). This is the
  optimizer's form of PandaGen::AddCatchBlockForImplicitSuperCallChecks.
- Effects: default-optimized implicit derived constructors and explicit
  `super(...)` constructors complete. Recovery fixtures super-computed and
  super-named, and throw-evidence super and delete_super default builds,
  complete and pass their checks. The delete_super check keeps asserting
  that the SDK's delete-this lowering differs from the source. The wide-call
  arrow-super refusal is now DerivedProjectionError::kProtocol at both
  optimization levels. A zero-length catch handler extent (es2abc code_size 0)
  was explored and is not needed for these cases; it is not committed.
- New test-only `tests/fixtures/isa/derived` (Base, implicit and explicit
  derived constructors, `extends Array`, empty bases; default/O0) with
  `tests/integration/derived.mjs` (63 cases: `new` and Reflect.construct with a
  subclass new.target, prototypes, lengths, names, calls without new).
- Ledger (`build/review/isa-cc16`): SupportedVerified 203 → **206**,
  SupportedUnverified 24 → 21.

## Copy-separated module read guards (2026-10-23)

- Default-optimized es2abc can emit `ldlocalmodulevar k; sta vN;
  throw.undefinedifholewithname "x"`, where `sta` moves before the guard.
  Module-read facts now find the HoleGuard after accumulator-to-register copies
  in the same block (`ModuleReadFact::guard`). `CopyOnlyModuleGuardInterval`
  admits only pure, non-throwing ValueCopy ops between the read and its guard,
  with contiguous orders. module_guards, module_evaluation (ownership/order)
  and the evaluation verifier's ordered walk accept that interval. Phased owners
  keep exact adjacency. The copies observe nothing and cannot throw, so the
  guard still checks the read's value at the read's position; this is linear
  in the gap.
- Effects: the default-optimized SDK recovery fixtures module-external,
  module-lazy, module-local and module-sendable now complete and pass their
  Node differentials. super-computed and super-named default builds, and the
  implicit derived constructor (`class D extends B {}`), now stop at the
  zero-length catchall handler (CatchRegionError::kHandlerShape), a separate
  gap. Expectation updates: public_tdz default now matches the O0 refusal, and
  guarded_captures `branch` refuses at FunctionTree instead of EvaluationShape.
  module_evaluation_test's guard-identity mutation still yields kOwnership.
- Cohorts unchanged (605/620 source, 542/620 previous flags; zero regressions
  and zero text changes).

## ISA coverage15 — wide-encoding occurrence oracle (2026-10-23)

- Test-only. `tests/fixtures/isa/wide_ops`: TYPEOF_IMM16 (more than 256 IC
  slots, default/O0), JNEZ_IMM32 (a 2000-statement `if`, default) and
  JEQZ_IMM32 (a 2000-statement `else`, default), with a Proxy-traced Node
  differential (`tests/integration/wide_ops.mjs`, including throwing reads).
- JMP_IMM32 is emitted only by the O0 if/else build, which exceeds the existing
  per-function memory budget. Function memory grows linearly (about 31 KB per
  statement: 25/38/69 MB RSS at 500/1000/2000 statements), so this is a budget
  limit, not super-linear growth. It stays unverified.
- Ledger (`build/review/isa-cc15`): SupportedVerified 200 → **203**,
  SupportedUnverified 27 → 24.

## ISA coverage14 — operator/call occurrence oracle (2026-10-23)

- Test-only. New `tests/fixtures/isa/operators` (default/O0 SDK ABCs, both
  complete under V10) and `tests/integration/operators.mjs`: 524 Node cases per
  ABC over `% <= > >= << >> >>> & |`, unary `+ ! - ~ typeof`, truthiness,
  `in`/`instanceof` (including Symbol.hasInstance and non-callable RHS),
  NaN/Infinity, spread calls, 6-argument range and this-range calls, named
  object-literal functions and `debugger`, with traced valueOf/toString order,
  BigInt mixing and Symbol coercion errors. Registered as an oracle producer.
- Ledger (`build/review/isa-cc14`): SupportedVerified 182 → **200**,
  SupportedUnverified 45 → 27; Unmeasured 77 unchanged. Notes: es2abc drops
  `debugger` in these builds (DEBUGGER stays unverified), and conditions use
  `callruntime.istrue`, not plain ISTRUE. Class field initializers
  (CALLINIT) and `super(...xs)` at default optimization (PhasedMethodError 5)
  still refuse and were left out of the fixture.

## ISA coverage13 — namespace imports (GETMODULENAMESPACE) (2026-10-23)

- Implemented by Claude after the switch to Claude-independent development.
  GETMODULENAMESPACE imm8 and WIDE (es2abc uses WIDE from index 0x80) lift as
  an external module read whose slot is the module-request index
  (`request_namespace`). Source resolution maps that request to its unique
  namespace import entry and the canonical external binding slot
  (`ModuleReadBinding::binding_slot`; the original read is retained for
  correspondence). New `ModuleReadProof::kNamespace` covers the unguarded,
  never-Hole read; the value rule is Language. V10 `ModuleNamespaces()` is
  required. Phased owners refuse.
- Output: `import * as ns from "m";` plus the entry's captured lexical copy
  `let $localN = ns;` at its original initialization point. A readability
  alias (using `ns` directly) is a later printer step.
- Fixtures: `tests/fixtures/isa/module_namespace` (basic and wide, default and
  O0, SDK es2abc). Node traces cover value reads, a method call, live binding
  updates, keys, identity, toStringTag, default, strict write failure and
  extensibility; they match the original in a separate process per side.
  Scale: 200/201/400/800 namespace imports. A single function capturing 800
  lexical bindings hits the pre-existing printer storage limit (also on plain
  captured lets at baseline), so the scale source splits the reads across
  functions of 100.
- The misc_evidence namespace fixtures now complete (expectations updated),
  and the current-variant lift expectations for both opcodes are refreshed.
- Ledger (`build/review/isa-cc13`): SupportedVerified 175 → **182**,
  SupportedUnverified 50 → 45, Unmeasured 79 → 77; no backward moves. Cohorts
  vs 36b395f9 unchanged: 605/620 and 542/620, zero regressions and zero text
  changes. source-regression 65/65; sweep failures are only the known list.

## ISA coverage12 — V10 contract; computed function names; dynamic import (2026-10-23)

- `LanguageSourceV10` = V9 plus four separately versioned user-approved
  predicates (2026-10-23): array-destructuring acquisition, dynamic import,
  computed function names, and module namespace. V1–V9 keep their meanings.
  Accepted differences and citations are in [language-source.md](language-source.md);
  the design note is [isa-v10-recovery.md](isa-v10-recovery.md).
- Consumed so far: STOWNBYVALUEWITHNAMESET (both widths) → `{[k]: function(){}}`
  with LiteralPlan key/value ownership and independent verification, and
  DYNAMICIMPORT → `import(x)` with typed semantics and original-event
  correspondence. Module namespace and array-pattern recovery are not yet
  implemented (the predicates exist; refusals stay closed).
- `isa_coverage_test` used DYNAMICIMPORT as its "unsupported" sample; it now
  uses CALLRUNTIME_CALLINIT, which is still unsupported.
- Ledger (rerun into `build/review/isa-cc12`): SupportedVerified 171 → **175**,
  SupportedUnverified 51 → 50, Unmeasured 82 → 79, NonExecutable 28; no
  backward moves. Both cohorts vs 602a4a1e: **605/620** source and **542/620**
  previous flags, with zero regressions and zero completed-text changes.
  source-regression 65/65. The touched sweep failures are the known
  family_source_cli, tree_structure, try_tree, class_tree_source and
  try_tree_source.

## ISA coverage11 — global hoisting; private constructor spreads (2026-10-22)

- Consumed the user-approved V9 extension for entry STGLOBALVAR through [[Set]]
  and a pre-existing global accessor. Removed only the temporary accessor
  publication gate; original declaration/order/owner/input/exception checks
  remain. All eight previously gated SDK var/function ABCs complete. All 18
  script fixtures now complete under V9; previous flags retain metadata-only
  refusal. The predicate/version is unchanged. The enum value is retained for
  old diagnostic compatibility; no synthetic const throw was added.
- NEWOBJAPPLY both widths now consume the existing independently verified
  private dense array/cursor proof as a native constructor argument carrier.
  Only direct fresh function/class identities qualify; unknown host constructors
  still refuse with kConstructorIdentity. Indexed extraction cannot observe the
  private own data properties, so this does not widen the V5 APPLY premise.
  Inner spreads use V1. Evaluation, CallPlan, TreeFlow and recovery recheck their
  existing original event/consumer/catch correspondence. Array elisions refuse;
  syntax checking uses the ownership DFS and existing role storage, avoiding
  per-parent subtree scans. No index allocation occurs without an actual array
  initialization proof. Previously successful output is unchanged.
- Four complete SDK constructor ABCs cover both widths, mixed spreads and wide
  cursor stores; two negative ABCs retain exact constructor-identity causes.
  Node traces include custom iterator acquisition/next/value, sparse/string
  sources, throws in each iterator stage and a throwing constructor, plus
  constructor name/length and result arguments. C++ mutations recheck missing /
  forged constructor/array bindings, owners, cursor equations and elisions;
  copies/block splits and 200/201/400/800 exact byte/work admission are tested.
  At 800 spread+element steps: **2,755,918 bytes / 1,743,850 CallPlan work**;
  split/copy **5,167,024 bytes / 3,572,184 work**. No budget was raised.
- The misc list is **not all recovered**. Twelve new SDK default/O0 emitter
  probes are independently disassembled, decoded and refusal-tested.
  TOPROPERTYKEY is emitted for computed class fields/methods. LDSYMBOL has a
  dynamic PandaGen case but no found caller; ordinary Symbol source uses a
  global lookup. CLOSEITERATOR is not emitted by the observed dynamic SDK array
  pattern, which expands close explicitly. Its arbitrary acquisition difference
  is outside V2's for-of-only approval. Namespace imports need request/type and
  lexical initialization correspondence (native/CJS returns raw exports);
  arbitrary dynamic import defers ToString to a job; numeric name-setting fast
  paths differ from native string names. These stay refused and supply no
  successful oracle. Computed class-key and complete array-pattern certificates
  remain unimplemented; no NonExecutable claim is made for them.
- Ledger **164/53/87/28 → 171/51/82/28**
  (Verified/Unverified/Unmeasured/NonExecutable), **1,092 ABCs / 1,574,098
  instructions**, zero probe failures and no backward verified-profile moves.
  Seven profile-6 promotions: LDGLOBALVAR, STGLOBALVAR, NEWOBJAPPLY both widths,
  STOWNBYVALUE both widths, CREATEARRAYWITHBUFFER_IMM16. The two new/extended
  oracle producers attest **108 executed Node cases in 20 artifacts**; Ark
  runtime stays null. The 2,184 sequential production recovery RSS reports peak
  at **119,963,648 bytes**, below the 1,000,000,000-byte process limit.
- Original ABC / emitted source round trips at matching default/O0 modes:
  **7 exact / 15 differ / 0 unverified** after the existing lexer export-alias
  adaptation. Initial unadapted constructor compilation had four string-export
  parser failures, retained separately. Differences remain reported: prior
  script completion/inert O0 loads and pool offsets; existing inferred-function-
  name private wrappers; inert fresh-class prototype reads/IC renumbering;
  independent function installation ordinal names/registers. No normalization
  or grouping-token equivalence claim for full source recovery. Every emitted
  positive fixture passes Node syntax and its intended differential assertions.
- Final full build passes; source-regression **65/65**, all scale **21/21**,
  ISA **28/28**, touched sweep **258/264**. The six failures are documented
  pre-existing family_source_cli, tree_structure, try_tree, literal_construct,
  class_tree_source and try_tree_source; literal_construct also reproduces at
  wtbase (`tests/integration/literal_construct.cc:143`). No existing source
  spelling assertion was weakened; only historical refusal-to-completion
  expectations changed, with differentials.
- OLD **85452c0a**: fresh 40-input comparisons remain **605/620** source and
  **542/620** previous flags, with zero regressions, new cohort completions,
  completed text changes or contract-label changes. Fixture completions are
  separate from these unchanged cohort totals.
- Docs/evidence: [isa-misc-constructors.md](isa-misc-constructors.md),
  [language-source.md](language-source.md), `build/review/isa-r11/`,
  `build/review/isa-r11-work/`. Generated Markdown is refreshed; JSON remains
  under build. No commit, push or stash; `sample.tar.gz` is untouched.

## ISA coverage10 — V9 script entry; var/function host boundary retained (2026-10-22)

- Added independent `GlobalScripts()` in **LanguageSourceV9**; CLI, route
  receipts/round-trip tests and ISA oracle expectations select V9. Historical
  contracts retain their meanings. The required
  `grep -rn language_source_v8 tests tools src` is empty; the historical V8
  serialization name remains in the public contract definition.
- New `script_tree` route and `ScriptScopeProof` own actual same-file script
  entry, declarations and global accesses independently of module enclosure.
  Raw global writes now lift through the production effect/operand adapters;
  ValueGlobalWrite preserves original input/name, effects and exception exit.
  The verifier freshly checks original graph facts and TreeSemantics. Printer
  preflight requires the seal and actual method ownership. Private root locals,
  unsupported activation/phase and unowned var accesses retain typed refusals.
- **Partial, not full global-script completion:** eight lexical/class/closure/
  error SDK ABCs and two sloppy-write ABCs complete; eight var/function ABCs
  remain at **ScriptScopeError::kHostGlobalAccessor (10)**. Pinned STGLOBALVAR
  invokes an existing global setter during entry instantiation; native var /
  function declaration instantiation does not. The two approved differences
  do not expressly include it. The additional decision was requested; publication
  stays closed for those declarations until supplied. No refusal counts as
  support. Existing historical script probes retain exact current refusal
  boundaries. Standard-side hostile-accessor behavior is tested without claiming
  Ark VM execution.
- All 18 new ABCs retain metadata-only status with previous flags. Eight
  positive V9 oracle artifacts attest **16 executed Node cases**. Script/module
  compilation mode and differential runner dependencies are now hashed and
  independently checked; the event signature distinguishes global operation
  kinds. The two approved script differences are explicit exclusions, and the
  sloppy difference fixture emits no successful Node oracle.
- Strict original-ABC/es2abc comparisons at matching default/O0 modes: **4 exact /
  6 differ / 0 unverified** across the ten completed fixtures. Every difference
  is retained and explained: inert O0 self-register copies/value-call zero loads,
  explicit undefined script completion/directive loads, and physical pool
  offsets. No register/branch/handler normalization or false equality pass.
  All ten emitted fixture files pass `node --check` and their intended Node
  differential assertions. SDK 200/201/400/800 script stress fixtures complete;
  C++ mutations cover owner/entry/name/value/kind, missing commits and exception
  correspondence. Exact derivation admission at 800: **63,209 bytes / 24,603
  work**, with one-less limits refused.
- Ledger **159/54/91/28 → 164/53/87/28**
  (Verified/Unverified/Unmeasured/NonExecutable), **1,074 ABCs / 1,572,511
  instructions**, zero probe failures and no backward variant/verified-profile
  move. The 2,148 sequential production recovery reports peak at
  **119,947,264 bytes RSS** (below the 1,000,000,000-byte process limit). New Verified profile-6 rows: TRYSTGLOBALBYNAME both widths,
  STTOGLOBALRECORD, STCONSTTOGLOBALRECORD and STOBJBYNAME_IMM16. LDGLOBALVAR /
  STGLOBALVAR stay unverified at the pending var-declaration boundary.
- OLD **22b9a31a**: fresh 40-input comparisons are **605/620 → 605/620** source
  and **542/620 → 542/620** previous flags, with **zero regressions, new
  completions or completed text changes**. Only V9 manifest labels change.
  There are zero executable script entries in the exploration cohort (387
  no-module records are metadata without func_main_0). Across the measured
  fixture manifests, 34 records select script entry: 12 complete / 22 refused.
- Full build passes; source-regression **65/65**, all scale **19/19**, ISA
  **25/25**, touched sweep **192/197**. Its five failures are the documented
  pre-existing family_source_cli, tree_structure, try_tree, class_tree_source,
  try_tree_source. Final focused legacy/source/route/oracle tests pass; no
  existing printed-spelling assertion was weakened or changed (only version
  labels and historical script status assertions). New oracle mutation suite
  passes **13/13** Python cases. Source expectations were searched for affected
  contract labels; no previous completed source spelling changed.
- Evidence: `build/review/isa-r10/`, `build/review/isa-r10-work/`,
  [isa-global-scripts.md](isa-global-scripts.md),
  [language-source.md](language-source.md). Generated matrix JSON stays under
  build; the Markdown report is refreshed. No commit, push or stash;
  user-owned `sample.tar.gz` untouched.

## ISA coverage9 — private prototype literals; globals/throws remain partial (2026-10-22)

- Both SETOBJECTWITHPROTO widths now retain a typed literal-mutation effect,
  prototype input, target/result identity, Commit and exception exit. Existing
  LiteralPlan ownership/order/catch proofs are reused; TreeFlow checks the
  original kind and RecoveryFlow reconstructs a distinct prototype action.
  Tree structure rejects duplicate native prototype setters. The printer emits
  `__proto__: value` only from this effect; ordinary data keys keep their prior
  computed spelling. No contract or successful-plan admission was loosened.
- Four complete SDK records at default/O0 verify both widths and field order,
  primitive/null/object/function prototypes, nested/fresh identity and throws.
  Four negative records keep exact nonprivate-target / computed-DefineField
  refusals. SDK scaling covers 200/201/400/800 allocations; component tests also
  check exact work/byte boundaries, copies, block splits, missing/reordered
  commits and forged property effects. At 800 fields the literal plan retains
  **3,600 bytes**, with **114,674 work / 296,802-byte admission** normally, or
  **461,234 work / 1,403,736-byte admission** with split copy corridors.
- Ledger **156/55/93/28 → 159/54/91/28**
  (Verified/Unverified/Unmeasured/NonExecutable), **1,056 ABCs** and
  **1,565,773 instructions**, no probe failure and no backward verified-profile
  move. New Verified rows are both SETOBJECTWITHPROTO widths and
  CREATEOBJECTWITHBUFFER_IMM16, all in SDK profile **6**. The two new oracle
  producers attest **76 Node cases**; Ark runtime remains unmeasured.
- Partial scope is explicit: script globals are **not recovered**. Ten real
  non-module probes show that native global lexical activation/TDZ and sloppy
  unresolved writes are not established by the current contract. Their current
  route emits metadata only, not completed source. The throw family is also
  **not complete**: const binding/throw correspondence, register-named guards,
  yield* throw-resume and invalid-super protocols remain refused. SDK delete
  super lowers to delete-this; the test observes this discrepancy and does not
  create a successful oracle. See the cited evidence note below.
- Strict `grouping_bytecode.py` on four prototype records plus the valid O0
  super-check fixture: **2 exact / 3 differ / 0 unverified** separately at
  default and O0; all **5** emitted files pass Node syntax. All callable bodies
  in the two prototype differences are exact; only independent function
  installation order and physical literal/string placement change. The super
  fixture uses the existing private heritage inlining and object-result super
  return projection; its additional undefined comparison is inert. Differences
  remain reported, never normalized into passes. The grouping-only token
  checker correctly rejects these full source-recovery comparisons (5/5);
  no token-equivalence pass is claimed.
- OLD **dac3bfb5**: both fresh 40-input comparisons remain **605/620** source /
  **542/620** previous flags, with **zero** regressions, new completions or text
  changes in previously complete records. No contract-label change.
- Full build passes; final fixed source suite **65/65**, all scale **17/17**,
  ISA **23/23**, and touched sweep **191/203**. Its 12 failures are the listed
  pre-existing family_source_cli, tree_structure, try_tree, class_tree_source,
  unguarded derived_cells/literal_closure/derived_literal_receiver,
  captured/forwarded/reversed-initialization receiver variants, and
  try_tree_source failures. The affected-spelling grep also selected **40**
  tests through their actual fixture commands: **37 pass**, with only known
  tree_structure, phased_literal and phased_arm_selection_source failures.
  No existing printed-spelling assertion changed.
- Evidence: `build/review/isa-r9/`, `build/review/isa-r9-work/`; design and exact
  remaining boundaries:
  [isa-globals-prototype-throws.md](isa-globals-prototype-throws.md).
  No commit, push or stash. User-owned `sample.tar.gz` remains untouched.

## ISA coverage8 — wide ranges and strict super stores (2026-10-22)

- `LanguageSourceV8` inherits V7 and adds only the user-approved
  `WideRangeCalls()` predicate. WIDE_CALLRANGE uses the ISA/C++/AOT start register;
  its asm-stub byte-offset bug remains an explicitly accepted difference.
  The six related layouts were checked separately; no other path disagrees.
  Ordinary, receiver/named, constructor and derived-super calls reuse existing
  lazy range operands, reference/order/exception and recovery checks.
- Four super-store widths now retain strict super lookup in the existing write
  operations. TreeFlow independently checks class home/receiver identity,
  primitive computed keys and a transitive inert-RHS certificate. Recovery
  reconstructs the same reference phases. Effectful RHS evaluation keeps
  `kSuperStoreOrder`: Ark resolves the super reference later than native JS.
- End-to-end fixtures: **84** complete wide-call records at
  **200/201/256/300/400/800** arguments, including 800-substitution templates,
  and **4** complete instance/static super-store records. **292** executed Node
  cases; all **88** emitted records pass Node syntax. The 800-argument stress
  constructs are separate records; no aggregate family budget was increased.
- Ledger: **156 SupportedVerified / 55 SupportedUnverified / 93 Unmeasured /
  28 NonExecutable**, from **149 / 55 / 100 / 28**. Seven variants moved from
  Unmeasured to Verified in SDK profile **6**; no variant or previously verified
  profile moved backward. The two oracle producers passed and recorded
  occurrence evidence. Ark runtime stays null. Strict event mapping retains
  its mismatches/resource refusals; Node evidence is not an Ark execution pass.
- `grouping_bytecode.py`, both default and O0: **72** exact whole-record matches,
  **16** differences, **0** unverified, per mode. Twelve derived-constructor
  records add es2abc's return-expression undefined check when compiling native
  `return super(...)`; it is unreachable because super commits an object.
  Four super-store records have identical callable bodies and metadata meanings,
  with only physical entity/string offsets shifted. No registers, branches,
  handlers or metadata were normalized to call these passes. See the evidence
  section of [isa-wide-range-calls.md](isa-wide-range-calls.md).
- OLD **ccbad2ad**: both 40-input cohorts export without failures and remain
  **605/620** source / **542/620** previous flags; **zero** regressions, new
  completions or text changes in complete records. Only the source-contract
  label changes to V8. Full build passed. Fixed source suite **65/65**; all
  scale tests **16/16**; ISA **20/20**; focused units **5/5**; touched sweep
  **155/160**, with only the documented `family_source_cli`, `tree_structure`,
  `try_tree`, `class_tree_source`, `try_tree_source` failures. No spelling
  expectations moved; contract-label assertions and raw-lift goldens updated.
- Remaining scope: arrow-super is lifted with its explicit accumulator
  constructor but remains unpublished (default CatchRegionError::kHandlerShape;
  O0 DerivedProjectionError::kProtocol). It needs joint lexical constructor,
  newTarget and initialized-this-cell correspondence with its parent. Object
  literal super methods remain at ObjectBufferError::kShape;
  class home-object evidence is not reused as proof of an object home.
  Unknown computed keys and effectful super RHSs remain typed refusals.
- Evidence: `build/review/isa-r8/`, `build/review/isa-r8-work/`; design:
  [wide range calls](isa-wide-range-calls.md),
  [super stores](isa-super-stores.md). No commit, push or stash.

## ISA coverage7 — tagged templates published (2026-10-22)

GETTEMPLATEOBJECT imm8/imm16 now uses a fresh/private interleaved
raw/cooked/carrier certificate, a typed source template object, and independent
TreeFlow/operand/recovery correspondence. CALLRUNTIME_DEFINEFIELDBYVALUE reuses
the typed own-definition family with an explicit DefineField attribute.
The ordinary call proof preserves the saved tag, receiver, substitution order,
exception exits and result Commit. TV/TRV agreement is independently checked;
raw text is preserved, including invalid escapes with cooked Undefined.
The existing V7 predicate gates publication; no new premise or literal-plan
allocation admission is introduced. Details and pinned citations:
[isa-template-construction.md](isa-template-construction.md).

- Baseline: `da924a32`. Final fresh 40-input comparisons: **605/620 source,
  542/620 previous flags**, no new/lost completions and no text changes to
  previously complete records. No application-source input enters the engine.
- Eight new ABCs complete: SDK default/O0 and forced imm16, plus both widths in
  profiles 4/6. Node syntax/differentials pass: **172 cases**, excluding only
  V7 descriptor enumerability and repeated-site identity. Original ABCs are
  independently decoded by the engine and ark_disasm.
- Ledger: **143/58/103/28 → 149/55/100/28**
  (Verified/Unverified/Unmeasured/NonExecutable), 928 files, 1,358,413
  instructions, no probe failure or backward verified-profile move.
  New transactions supply 1,863 Node attestations, 1,074 exact event round-trip
  matches and 789 null matches; Ark runtime is null.
- Strict grouping_bytecode: **6 pass / 2 differ / 0 unverified**, separately
  at default and O0. Every callable body in the two differing SDK modules is
  exact; only their module-entry function installation order differs. These
  differences remain reported and are not promoted to exact round-trip passes.
- Full build passes. Source-regression final **65/65**; all scale **14/14**
  (including class-self depth800); ISA **18/18**; targeted **5/5**.
  Required touched sweep **219/231**; its 12 failures are the documented
  family_source_cli, tree_structure, try_tree, literal_construct, mixed_literal,
  phased_literal, class_tree_source, literal_closure(+unguarded),
  derived_literal_receiver(+unguarded), and try_tree_source failures.
  The first concurrent regression run had a RichText worker timeout; isolated
  OLD and NEW checks and the full final preset pass. No test assertion was
  weakened. The independent unknown-instruction receiver test now uses
  DYNAMICIMPORT, since GETTEMPLATEOBJECT is modeled.
- Exact construction budgets at 800 quasis: **438,217 bytes / 113,093 work**;
  800 substitutions (801 quasis) use **438,763 bytes / 113,234 work**; one less
  refuses. 200/201/400/800 string and phased-graph boundaries
  are checked. A single SDK tag with 200+ substitutions remains refused at
  WIDE_CALLRANGE's separate pinned execution-path disagreement; the private
  certificate supports that size, and SDK site-count/string scaling completes.
- Evidence: `build/review/isa-r7/`, `build/review/isa-r7-work/` (final-source,
  final-previous, roundtrip, test logs). Updated registry measurements retain
  original fixture hashes. No commit, push or stash.

## ISA coverage6 — RegExp complete; tagged templates unfinished (2026-10-22)

**Item 1 is implemented and measured; item 2 is not implemented.** No
GETTEMPLATEOBJECT production gate is lifted and no template oracle is imported.
The remaining event/certificate boundary is recorded in
[isa-template-construction.md](isa-template-construction.md). This is an
implementation gap, not a new source-language premise.

Both RegExp widths now retain pool pattern identity and typed raw flags through
lifting, SSA, allocation/exception effects and RegExpExpr. Runtime flags are
`dgimsuy` (no `v`), with the runtime bit layout rather than the frontend enum.
An explicit-stack, budgeted Pattern validator refuses invalid or unproven syntax
before native publication. Empty patterns and slash escaping preserve the pinned
source getter. Raw line terminators/lone surrogates retain a precise source-
representation refusal; backreferences/named groups/property escapes stay
unproven. See [isa-regexp.md](isa-regexp.md) for citations and limits.

Eight registered ABCs complete: both widths in profiles 4/6, two intrinsic loop
fixtures, and pinned SDK default/O0 fixtures. They pass Node syntax and 1,342
Node differential cases, including independent identity/lastIndex and a replaced
global RegExp for intrinsic fixtures. Mutated pattern/flags/tokens, malformed
operands and receipt round trips pass. Exact 200/201/400/800 width/depth budgets
pass; at 800 they use 4,026/8,026 admitted bytes and 1,609/4,009 work respectively.

Final ledger `build/review/isa-r6`: **143 Verified / 58 Unverified / 103
Unmeasured / 28 NonExecutable**, 920 ABCs, zero probe failures, no backward
variant/profile moves. Both RegExp variants promote under profiles 4 and 6.
JEQZ imm16, NewObjRange imm16 and TryLdGlobalByName imm16 additionally promote
under profile 6. The oracle has 1,674 Node occurrence rows, 775 matched
recompiled events and 899 null round-trip entries; Ark runtime stays null.

Strict grouping_bytecode per optimization mode: 4 exact / 4 different / 0
unverified fixture records. Factory pairs match exactly. The four loop files
retain explicit differences: private variable/register allocation, O0 snapshots,
number-only induction ToNumeric removal, and SDK module-entry declaration order.
These differences are explained in the note and remain in the reports. The SDK
lowers native regexp syntax to a mutable global RegExp lookup/construction;
original intrinsic-to-SDK event mismatches are never counted as matches.

Verification against **087cf100**:
- Full build passes; new RegExp tests 3/3; source-regression **65/65**;
  **all scale 13/13**; ISA/RegExp **19/19**.
- Touched-area sweep **190/202**. All 12 failures are on the supplied known list:
  family_source_cli, tree_structure, try_tree, literal_construct, mixed_literal,
  phased_literal, class_tree_source, literal_closure_source and its unguarded
  peer, derived_literal_receiver_source and its unguarded peer, try_tree_source.
- Both 40-input cohorts remain **605/620 source** and **542/620 previous**,
  with zero regressions, new completions or text changes to previously complete
  records. No contract label or existing spelling expectation changed.
- Logs, compiler/disassembler hashes, raw diffs and result summaries:
  `build/review/isa-r6-work/regexp/`. Generated JSON remains under build/;
  the generated Markdown ledger is refreshed. No commit/push/stash.

## ISA coverage5 — object rest; requested round unfinished (2026-10-22)

**Only item 1 is implemented.** Item 2 was investigated but has no production
change: the SDK interleaves raw/cooked private-array definitions, then defines
a two-element carrier before GETTEMPLATEOBJECT. Its native template projection
still needs the complete construction/event certificate. Item 3 (RegExp) has
not been started. Neither gate is lifted and neither family gains evidence.
No new source-language premise is assumed.

The existing V7 ObjectRests premise now documents the approved Proxy unwrap in
addition to strict Set versus CreateDataProperty. Raw normal/WIDE register
windows, SSA effects and the intrinsic PatternNonCoercible TypeError now lift.
That TypeError is a pending runtime exception, never a scalar throw of Undefined;
exception/forwarding/completion consumers retain that distinction.

The new ObjectRestPlan derives nullish exclusion from real edge dominance, ordered
Gets and exact exclusion values, use ownership and real exception dispatch. Its
independent verifier re-derives the relation. The retained source surface prints
a native assignment pattern into existing private snapshot bindings, with rest
last. Recovery checks original definitions and effects; the printer checks target
scope, shadowing and capture identity. A computed String snapshot stays at its
original event, and `[(0, snapshot)]` avoids the frontend identifier-spelling bug.
Incorrect keys, numeric/object exclusion domains, effectful duplicate key
evaluation, a missing guard/exception edge or an early observer stay refused.
See [isa-object-rest.md](isa-object-rest.md).

Ten registered ABC records complete (SDK default/O0 plus both historical
profiles), including a real 260-key WIDE fixture. There are **182 registered Node
cases**, plus default/O0 dynamic recompile/re-certification differentials and
200/201/400/800-key source scales. C++ mutations cover guard/key/catch identity,
missing exceptions, omitted/reordered/duplicate certificate members and exact
resource boundaries. At 800 fields, the exact derivation budget is **164,172
bytes / 41,438 work**; one less refuses. Both normal and Before/Commit graphs
are exercised.

Ledger: **134/62/108/28 -> 138/61/105/28** (Verified/Unverified/Unmeasured/
NonExecutable), 912 ABCs and zero probe failures. Both excluded-key widths and
PatternNonCoercible move Unmeasured -> Verified; CreateEmptyArray imm16 moves
Unverified -> Verified. No backward variant/profile movement. Evidence contains
7,289 new Node occurrences, 15 strict event matches and 7,274 unmatched null
round-trip entries; no Ark runtime result is claimed. Generated report:
`build/review/isa-r5/{matrix.md,matrix.json}`.

Strict grouping_bytecode compilation of all ten fixture outputs at default and
O0 reports **0 exact / 10 different / 0 unverified in each mode**, all syntax
checks pass. Differences are retained coercibility guards, the native pattern's
additional inert guard, the intrinsic empty-pattern TypeError arm, conservative
key/result temporaries, and module/creation-binding metadata. They are **not**
normalized into passes. The dynamic source oracle's es2abc identifier-key bug
is recorded separately; emitted default/O0 ABCs independently re-certify and
pass the differential with the actual key value. Detailed raw comparisons are
in `build/review/isa-r5-work/rest/strict-all/`.

Against wtbase **f060cb25**, cohorts remain **605/620 source / 542/620 previous**;
no regressions, new completions or source-text changes. Full build and
`git diff --check` pass. Final test results are recorded in
`build/review/isa-r5-work/rest/{acceptance.json,*.xml}`: source-regression
**65/65**, all scales **12/12**, ISA **16/16**, touched sweep **208/215**. The
seven failures are the documented pre-existing family_source_cli, tree_structure,
try_tree, phased_literal, class_tree_source, phased_arm_selection_source and
try_tree_source. New object-rest tests pass. No existing printed-spelling
expectation changes are needed: all previously complete source is identical.
No commit/push/stash.

## ISA coverage4 — private object spread (partial, 2026-10-22)

Against **fdf1152b**: **134 Verified / 62 Unverified / 108 Unmeasured / 28
NonExecutable**, from **132 / 62 / 110 / 28**. Only current and deprecated
COPYDATAPROPERTIES move, independently in profiles 12.0.6 and 24.0.0; no
variant/profile moves backwards. The ledger measures 902 ABCs with no probe
failures. See [the round note](isa-coverage-round4.md).

V7 object spread now consumes the existing private LiteralPlan through raw/value
IR, normal-result identity, source syntax, TreeFlow and joint recovery. Copying
retains a distinct effect/Commit/exception action; it is not a pure alias.
Discarded spreads retain callbacks. Unknown/escaping targets and previous
contracts keep typed refusals. No new source-language premise is introduced.

Six complete fixture records (two SDK default/O0, four constructed), **118 Node
cases**, syntax **6/6**, fresh/protocol/premise mutations and 200/201/400/800
copy/block-split exact budget tests. The new evidence producer imports **230 Node
occurrences**, **120** matched round-trip occurrences and **110** null round-trip
entries. Ark runtime remains null. Approved accessor/descriptor differences are
explicit provenance exclusions, not runtime passes.

`grouping_bytecode.py` in each compiler mode reports **4 exact / 2 different /
0 unverified**. All **48/48 per-method comparisons** across both modes are exact;
the two SDK records differ only in literal-table ordering/offsets. No instruction,
register, branch or handler normalization is used. Recovery changes declarations
and names, so the grouping-only token checker correctly rejects all six pairs;
these are not claimed as grouping passes.

Final full build passed. Fixed source regression **65/65** (rerun after fixture
setup), all scale **11/11**, ISA **15/15**, new spread targets **3/3**. Required
area sweep **209/221**: the 12 failures are the documented family CLI,
tree/try, class-tree, literal construction/closure/receiver and phased/mixed
literal failures. An extra all-source sweep was stopped at its unrelated slow
navigation-graph shards after the required area sweep completed; it is not
counted as a completed suite. Both cohort
complete counts stay **605/620** and **542/620**; all complete text and all 80
manifests are byte-identical. Peak decompiler RSS across 80 runs is **42,434,560
bytes** (Darwin wait4). Artifacts: `build/review/isa-r4` and
`build/review/isa-r4-work`.

Remaining: tagged-template construction/recovery, object rest, the super-store,
RegExp/proto/throw and remaining lift-ok families, and generator expansion. The
object-rest runtime additionally unwraps Proxy targets (`runtime_stubs-inl.h:
2642–2648`), which is outside the approved Set/CreateDataProperty difference;
arbitrary Proxy sources stay refused. No claim of full round-4 completion.

## ISA coverage3 — V7 and owner-bound frame/property evidence (partial, 2026-10-22)

Against **cbc9389b**: ledger **117/62/125/28 → 132/62/110/28** (Verified /
Unverified / Unmeasured / NonExecutable), 15 promotions, no backwards movement
at variant or profile level, 896 measured ABCs and all 332 variants observed.
The four user-approved predicates are independently versioned in V7; V1–V6 keep
their meanings. Only new.target recovery is consumed in this partial round.
Tagged-template / object-spread / object-rest construction certificates remain
unfinished. The [round note](isa-coverage-round3.md) lists all moved rows and
remaining gates; [language-source.md](language-source.md) records the exact
2026-10-22 approvals and pinned citations.

Eight implicit-this property variants now have typed raw/value semantics and
independent owner/key/strict-store/event verification through existing member
Get/Set/recovery. Explicit new-target instructions and ABI entry values have
separate owner-bound origins; unused explicit instructions retain an invocation
frame-read event so V7 cannot be bypassed. Arrow/module contexts keep typed
refusals. Existing LDTHIS/LDFUNCTION and four other lift-ok forms gain complete
constructed fixtures. No production fixture/name dispatch or new premise.

32 complete fixture records (30 constructed, two SDK default/O0), **244 Node
cases**, node --check **32/32**, premise/owner/key mutations and 200/201/400/800
exact byte/work boundaries. SDK disassembly and engine decoding both pass.
New oracle import: **241 Node occurrences, 31 strict round-trip event matches**;
210 unmatched events retain null, Ark runtime stays null. Strict source-reference
versus recovered compilation, both modes: **30 exact / 2 different / 0 unverified**.
Only the LDFUNCTION oracle's module read versus the recovered immutable self
read differs; the latter directly matches the original frame function. All
instructions/registers/handlers remain unnormalized. The grouping-only token
checker rejects all 32 general recovery pairs, not counted as semantic passes.

Both fresh 40-input cohorts versus wtbase cbc9389b: **605/620 source, 542/620
previous**, no new completions, regressions or complete-source text changes.
All 80 manifests are otherwise identical after removing only source_contract
labels; source labels change V6→V7 and previous flags stay unchanged. Full build
passes; source-regression **65/65**, all scale **11/11**, ISA **14/14**, focused
**5/5** plus property-receiver **1/1**. Final sweep **166/171**: only documented
family_source_cli, tree_structure, try_tree, class_tree_source, try_tree_source.
The old property-receiver unsupported witness was LDNEWTARGET: it is now a
positive alias-preserving test; an actual unsupported GETTEMPLATEOBJECT encoding
retains the negative barrier test. No behavioral assertion was removed.

At 800, new-target tree construction admits **1,127,155 bytes / 162,643 work**;
implicit-this reads admit **1,240,855 / 177,053**, split blocks **1,511,300 /
214,776**; each immediate smaller boundary refuses. Full recovery kernel RSS
across 80 sequential cohort runs peaks at **40,583,168 bytes** (Darwin wait4).
`grep -rn language_source_v6 tests tools src` is empty; the historical serializer
in include/ intentionally preserves V6. No commit, push or stash; unrelated
sample.tar.gz untouched. Generated large JSON remains under build/review/isa-r3/.

## ISA coverage2 — super reads, BigInt and wide oracles (partial, 2026-10-21)

Against ef018a26: **95/72/137/28 → 117/62/125/28** (Verified / Unverified /
Unmeasured / NonExecutable). **22 variants promoted**, none backward at variant
or profile level; all 332 observed over 864 inputs. Nine of the 25 requested
lift-ok encodings now verify; sixteen remain gated. Production adds typed
computed-super reads with independently checked home/receiver/primitive-key
order, and typed BigInt pool literals with linear prepaid syntax validation.
No contract change, no LDNEWTARGET code, no ordinary member-update admission for
super. The [round note](isa-coverage-round2.md) contains the exact moved list,
pinned citations, restrictions and unfinished families. Generated table:
[isa-coverage.md](isa-coverage.md); JSON is untracked under build/review/isa-r2/.

20 SDK default/O0 fixtures: **13 complete / 7 measured optimized refusals**;
**107 Node cases**, seven observed source/dependency mutants. Only complete
exports supply the strict occurrence writer. Lazy imports are tested with
already-initialized dependencies and that timing exclusion is explicit. Ark
runtime remains null. Strict SDK recompile comparisons in each optimization
mode: **7 exact records / 6 different / 0 unverified**; **38 exact method bodies /
5 different**. All differences are retained and explained (private lexical env,
derived-return checks, pure constant-key loads, equivalent iterator control,
and pool offsets); none is silently accepted by normalization. The generic
parenthesis token checker is inapplicable to recovered renaming/control and its
13 rejects are reported, not semantic passes.

Fresh 40-input cohorts: **605/620 source, 542/620 previous**, zero new
completions, regressions or complete-text changes. Full build passes;
source-regression **65/65**; all scale **11/11**; ISA **13/13** after updating the
explicit object-lookup raw payload in historical snapshots; focused **6/6** and
final super/pool **2/2**. Touched sweep **189/201**: only already-listed
family_source_cli, tree_structure, try_tree, literal_construct, mixed_literal,
phased_literal, class_tree_source, literal_closure/unguarded_literal_closure,
derived_literal_receiver/unguarded_derived_literal_receiver, try_tree_source.
Full recovery kernel peak RSS over both cohorts: **41,107,456 bytes**; raw
probe peak **42,352,640 bytes**. Exact budgets and block variants cover
200/201/400/800. No previous printed expectations weakened; complete text is
unchanged. No commit/push/stash; unrelated sample.tar.gz untouched.

**Decision candidates, not assumptions:** template arrays have non-enumerable
indexes and raw-array-identity caching; object spread copies descriptors;
object rest uses strict Set instead of CreateDataProperty; LDNEWTARGET differs
between fatal/no-op interpreters and AOT. See cited questions in the round note.
This does not complete the remaining this/super-store, RegExp, prototype,
throw-source, or remaining sixteen lift-ok recovery items.

## ISA coverage1 — oracle importer and indexed accesses (partial,2026-10-21)

Against a964be2a, measured ledger moves **148/156/0/28 →137/72/95/28**
(Unmeasured / SupportedUnverified / SupportedVerified / NonExecutable). No
backward movement. All332 variants remain observed.95 display rows now have
actual occurrence oracles in profile24;12 also verify in12.0.6. Other profiles
keep their own state. The generated table is [isa-coverage.md](isa-coverage.md);
full changes/profile identities are under build/review/isa-r1/.

The test-only writer/importer hooks11 existing/new differential suites and
checks immutable artifacts, exact ABC/profile/record/method/PC, current raw lift,
complete recovery and non-skipped CTest success. Node and SDK event-family
round-trip evidence stay separate; Ark runtime remains null. Multi-record tests
explicitly name tested records. Resources are bounded; exhaustion never proves
an oracle. Production adds allocation-free typed indexed Get and strict Set
lifting (current narrow/imm16/wide plus deprecated register-base Get), reusing
existing property/source/recovery checks. Own definitions remain distinct.

**The requested whole round is not complete:**4/29 lift-ok targets verify
(XOR,EXP,wide new-range,wide super-range);25 remain Unmeasured. Seven newly
lifted index encodings also verify. Other requested this/super, literal/cache,
spread/rest and throw families are not implemented. LDNEWTARGET has fatal/NOP/
AOT-new-target execution-path disagreement and keeps its typed refusal. No new
premise, no contract bump, no publication or ownership gate weakened.

Fresh cohorts: **605/620 source,542/620 previous**, no new completions,
regressions or byte changes in previously complete source. Full build passes;
source regression65/65; all scale11/11; ISA11/11; touched sweep211/216, only
known family_source_cli/tree_structure/try_tree/class_tree_source/try_tree_source
failures.20 new complete fixture exports pass syntax/274 Node cases and14
observed index mutants. Strict SDK comparison is18 exact/2 different/0
unverified in each optimization mode; the2 differences are existing
`return super(...)` lowering, retained and explained, never called exact passes.
See [isa-occurrence-oracles.md](isa-occurrence-oracles.md) for evidence, budgets,
round-trip mismatches, remaining variants and the string-export adaptation.
No commit/push/stash. The unrelated sample.tar.gz remains untouched.

## Member-update scale regression — partial round (2026-10-21)

Priority regression fixed against `7003dce3`: **all11 scale tests pass**, including
class_self_initialization_scale_test / nested_members800, without increasing any
budget. The decisive regression was MemberMutationExpr increasing every ExprNode
from48 to56 bytes. A typed increment/compound-with-RHS alternative restores48;
a static assertion protects the arena width. Discovery now prepays by actual
Get/Set candidates and takes no heap allocation for a forced-unmet reference
without a matching pair. Empty/per-family evaluation plans retain a nullable
immutable shared owner; verification still independently recomputes the proof.

Full build passes; source-regression **65/65**; requested exact-budget tests
**5/5**; non-scale sweep **208/213** (only the five documented failures).
Member-update fixtures: **5,852 Node differentials / 8 observed mutants**;
strict SDK old/new comparison **3/3 each at default and O0**, no differences or
unverified fixture exports. Both cohorts remain **605/620 source**, **542/620
previous flags**, with zero text/status/manifest-record drift.

**The stored-instance-initializer work is unfinished:** o32/51260 still refuses
CreationContextError::kKind at m51746 / creation51442 PC40. A fresh constructor
probe also exposes unsupported CALLINIT at m51713 PC12. No creation-kind gate
was weakened, no new field output was published, and Hypium cleanup was not
attempted. See [member-update-storage.md](member-update-storage.md) for the
before/after800 cost table, evidence and remaining cross-method recovery work.

Correction to the previous checkpoint: reproducing nested_members800 on a
baseline did not make it an accepted pre-existing failure. The cc bisect to
24b01ff0 is acknowledged; the regression is now fixed and included in every
round's scale acceptance. Scratch: build/review/scale-stored-init/.

## Class initializer composition — partial checkpoint (2026-10-21)

Against `8f50e7e8`, **o25/40458 CustomScanViewModel completes** in both flag
sets: **605/620 --language-source**, **542/620 previous flags**. No regressions
and no byte changes to previously complete source. Only o25/40458 and
o32/15324 manifests change. No contract bump or ISA admission.

The combined class projection now consumes the independently proven member
installation corridor before static initialization. A shared creator projection
avoids per-class CFG rescans; layout/printer carry original installation IDs and
creator/evaluation verifiers recompute them. A separate independently checked
private-receiver certificate permits an uncaptured static initializer's RHS
calls while its fresh class receiver remains unobserved; closed-field successes
keep their first path, and failed-attempt costs are charged before fallback.

**This round's original three-record goal is not complete.** Hypium passes
ClassSelfOrder and stops in m15918, Try1 PC283/H308, at MultipleExits; the close
probe identifies per-iteration cleanup/dispatch coverage at PC84→H259→H265.
ArgumentMatchers still refuses the stored instance-initializer role at
m51746 / creation51442 PC40 (CreationContextError::kKind). Its CALLINIT and
native per-construction field projection remain unimplemented. Parked42397 and
46008 were not changed.

Full build passes; source-regression **65/65**; new tests **3/3**; requested
sweep **215/221**. Five failures are on the documented list. The additional
class_self_initialization_scale nested_members800 failure is reproduced on
both old/new binaries with the same FamilyEnclosureError::kResourceLimit.
The five other failures were also reproduced with baseline binaries. New tests
cover mutations/copies/block splits and 200/201/400/800 exact budgets; real
compiler fixtures have **56 passing engine-output Node differentials**.
Target syntax checks pass. SDK recompilation of the complete target has all33
methods; O0 **18/33 bodies exact**, including static_initializer; default0/33.
Non-identical bodies and refused fixture variants are explicitly separated
from exact comparisons, with raw diffs retained.

See [class-initializer-composition.md](class-initializer-composition.md) for
bytecode diagnosis, semantics, per-method SDK differences, remaining work and
scratch evidence under `build/review/class-initializers/`. No commit/push/stash.

## Native arguments V6; remaining nested/sequential close gates (2026-10-21)

- Baseline wtbase **80b4030a**. **A completes:** o32/52417 (`stub`, m52655) and
  o32/53454 (nested `f`, m53795). **B remains incomplete:** 42397 and46008 retain
  their original kMultipleExits first causes. No commit/push/stash performed.
- The user-approved 2026-10-21 `NativeArguments()` predicate is versioned as
  **LanguageSourceV6**. CLI source output selects V6; V1–V5 and previous flags
  are unchanged. The accepted difference is only the own non-configurable
  throwing `caller` accessor and its reflective consequences. No APPLY premise
  was widened: the published use is ordinary `action.apply(this, snapshot)`.
- A prepaid native-entry certificate proves own-frame/non-arrow ownership,
  a unique unreentered prefix before observable events, original parameter
  correspondence through copies, and one snapshot identity per invocation.
  Projected initialized default formals are not incoming arguments. Independent
  semantic/structure checks re-derive the proof and reject shadowing. The
  existing snapshot's original event/conditional inputs/exception exits remain;
  only a typed native-entry source form replaces the exact factory spelling.
- Private-state liveness now follows normal-Commit scalar copies until kill or
  observation; live returns/calls/branches still refuse. The dead753 restore in
  m42717 is no longer a live-out. All four nested copies pass that obligation,
  but Try4/read705 and throw-body dispatch705/480 still need correlated
  closed-state/payload/destination propagation. In m47128,717 still needs the
  fresh-iteration scope exit at POP874 → rethrow875 → H880. Neither unproved
  route is admitted as inert forwarding. See [o32-cluster.md](o32-cluster.md).
- Both 40-input cohorts: **604/620 source** (was602), **541/620 previous**;
  zero regressions and zero text changes in previous complete records. After
  version-label normalization the only changed manifest records are52417/53454.
  The helper's1007 denominator includes387 skipped structural records.
- Full build succeeds. Source regression **65/65**; targeted native/contract/
  boundary/round-trip checks **8/8**; expectation-census extras **8/8**. Requested expanded sweep **247/255**, with
  eight known failures: family_source_cli, tree_structure, try_tree,
  class_tree_source, try_tree_source, snapshot_syntax(+source), async_semantics.
  Six reproduce via wtbase CTest; the other two reproduce with the same current
  test commands pointed at wtbase binaries. No new failure is classified away.
- Node: **38** constructed plus **24** packaged-original differential cases;
  both new records pass node --check. Tests cover ordinary escape, apply,
  lengths/indexes, repeated identity, parameter mutation, named descendant
  capture, receiver, throws and separate invocations. Only caller observation
  is excluded, explicitly. Wrong owner/input/certificate/shadow/prefix/reentry,
  private-copy live-out mutations and200/201/400/800 exact budgets pass.
- SDK round-trip retains **6 GETUNMAPPEDARGS at both levels** and both emitted
  ABCs re-import complete. Exact method equality is **7/8 default**, **4/8 O0**;
  no register/branch/handler normalization. Diffs preserve the renamed capture
  guard-name string/pool relocation, O0 scalar copies/registers and an unused
  formal store. Full explanation: [argument-snapshots.md](argument-snapshots.md).
- Evidence: `build/review/native-arguments/` (cohorts, comparison and manifest
  census, build/source-regression/sweep/targeted logs, original-record Node
  traces, nested/sequential probes, SDK hashes and raw roundtrip3 diffs).

## o32 Group B: async loop completion; nested/sequential still gated (2026-10-21)

- Baseline wtbase **7c4c6912**, HEAD **07c6c665**. **Partial round: o32/23058
  completes; o32/42397 and46008 do not.** Group A code remains untouched.
- ExceptionFlow retains charged owner-proven AwaitSites. A generic raw-event
  semantic owner replays the same native-await effects in completion_relation
  and close_boundary; unused/malformed sites refuse. RawEventGraph remains
  independent of AwaitRegion and the Round78 fast/query order is unchanged.
- Verified success-edge iteration bindings have a prepaid ValueId alias index,
  separate from private protocol values. Their lifetime is iteration-scoped;
  ordinary phase-corridor fusion cannot promote it to invocation scope. Operand
  checking resolves the actual loop symbol, and recovery independently rebuilds
  the index from original commits. No fabricated expressions or initialized cells.
- The record publishes all six async event methods. Its source is checked by
  Node and compared with the packaged original in360 differential cases.
  New/existing fixture scripts add60/865/756 cases covering async rejection,
  synchronous throws, iterator close priority/receiver and microtasks. Mutation,
  layout/register,200/201/400/800 and exact-budget tests pass.
- SDK default: whole fixture and2/2 methods byte-identical. O0:1/2 methods exact;
  scalar declaration/transport/register/backedge differences retained in diffs,
  with the recompiled close/evaluation proof passing. Default standalone async
  re-import still has its existing kAwait limitation; exact assembly equality
  is reported separately. Compiler/disassembler hashes/options are pinned.
- Full build passed; serial fixed suite **65/65**; focused tests **13/13**;
  publication refresh **4/4**; required sweep **227/233**, only known
  family_source_cli, tree_structure, try_tree, class_tree_source,
  try_tree_source and async_semantics failures. Obsolete refusal assertions in
  async_failure, for_of_multiple and for_of_user_try now require publication
  plus differentials. No printer change.
- Both40-input cohorts vs wtbase: **602/620** source (was601), **541/620**
  previous flags; zero regressions and zero prior-complete text changes.
  Manifest-record changes: only source o32/23058. OS peak CLI RSS **44,269,568**
  bytes across80 runs; four-engine+wrapper bound **246,546,432** bytes.
- Remaining B work is implementation, not a new premise: nested failures need
  correlated forwarding through already-closed inner/outer handlers; sequential
  cleanup needs environment-pop/fresh per-iteration source-scope correspondence.
  Experimental incomplete admissions are kept out of production. See
  [for-of-remaining-close.md](for-of-remaining-close.md) and
  `build/review/o32-groupb/`. No commit, push or stash.


## o32 groups 2 + 3: native arguments premise boundary (2026-10-21)

- HEAD **07c6c665**, wtbase **7c4c6912**. No production change. The single,
  entry-time, strict non-arrow proposal addresses argument values and identity,
  but the pinned runtime additionally installs an own non-configurable throwing
  `caller` accessor. Standard strict arguments has no own `caller`; a fresh
  Node probe confirms the descriptor/own-key/read difference.
- Verified interpreter, runtime factory, accessor initialization and compiled
  stub source. Freshly decoded target methods 52655/53795 (snapshot PC6), and
  compiled/disassembled minimal `return arguments` / `this.params = arguments`
  fixtures with the SDK. Native source recompiles to GETUNMAPPEDARGS, which
  supports Ark round-trip fidelity, not standard-JS descriptor equivalence.
- Stopped at the user's explicit new-premise boundary. No fabricated host
  factory, no native-arguments admission, no work on group B. No new build,
  suite/cohort run or completion is claimed; the reviewed baseline remains
  **601/620** source / **541/620** previous flags. Details and evidence commands:
  [argument-snapshots.md](argument-snapshots.md#entry-time-native-arguments-checkpoint-2026-10-21),
  `build/review/o32-groups23/`. No commit/push/stash.

## o32 cluster: enumeration / retained-selection composition (2026-10-21)

- OLD/wtbase **f497690f**. Fresh bytecode-only census covers the eleven listed
  records (the prompt's count of ten omits one). Completed **38853 DeepTypeUtils**
  and **40109 assertDeepEquals**; the other nine first refusals are unchanged.
  Full census, risk/yield ranking, original/recovered method comparison and
  remaining obligations: [o32-cluster.md](o32-cluster.md).
- Compose the existing for-in and sealed for-of proofs: supply the immutable
  protocol input to every independent check and borrow a transparent forwarding
  route only when its original dispatch/payload/destination and topology agree.
  A typed owner prevents duplicate ownership of the already-projected handler.
  TreeFlow and both recovery routes recheck the original entries.
- Consume already-proven normal retained selections in conditional decision
  prefixes. To finish the same record's later `isEqualSet`, index actual native
  loop enclosures for inner break/continue and retain a typed Number-only
  ToNumeric identity with conditional exception/provenance correspondence.
  No fabricated source action/ExprId, no new premise, no printer changes.
  Prepaid indexes and existing sparse propagation retain O((V+E) log V) work.
- Cohorts: previous **541 → 541/620**, source **599 → 601/620**, **zero**
  regressions and **zero** changes to previously complete text. Both new files
  pass `node --check`. Four real SDK fixtures plus the existing context fixture
  pass **338** Node differential cases (values, exceptions, receivers, effect
  order and close priority). Negative mutations, copy/register/block-split
  variants and 200/201/400/800 exact byte/work boundaries pass.
- SDK source/recovered fixture comparison at default and O0: all **16** compiler
  and disassembler invocations succeed after the same identifier-export-alias
  adaptation is applied independently to each side. Strict record comparison
  reports **0 equal /4 different /0 unverified per mode**, not equality passes;
  per-method census is **4 equal /6 different per mode**. Raw differences in
  private locals/phi copies, scope names/guards, loop form and inert branch
  routing are explained in the design note. No bytecode normalization or
  acceptance allowance was added; no Ark-runtime execution is claimed.
- Full build passes. Serial source-regression **65/65**; new/extended targets
  **11/11**; touched-area/tree/flow/recovery/CLI/receipt/manifest sweep **208/216**.
  All eight failures reproduce against OLD: family_source_cli, logical_regions
  (+source), tree_structure, try_tree, class_tree_source,
  phased_arm_selection_source and try_tree_source. Only the now-complete
  iteration-context refusal expectation changes, replaced by a differential.
  OS peak RSS per CLI stays below **42.7 MB** across both 40-input runs; the
  conservative sum of four largest CLI peaks is below **156 MB**, plus below
  **70 MB** for the four measurement wrappers. Evidence:
  `build/review/o32-cluster/`. No commit/push/stash.

## Readable output slice 3: certified statement cleanup (2026-10-21)

- OLD **40ed813b**. Added a printer-only `ReadableStatements` certificate and
  independent recomputation after the existing binding/nullish/loop plans.
  It consumes OriginalFlow guarded-read/initialized-guard evidence plus the
  whole RHS effect summary. No engine proof, contract or publication gate is
  loosened; policy remains **PrinterPolicy::kReadableV1**.
- Source cohort: **142** adjacent bare-read/store sites in **46** complete files;
  **106** redundant reads removed in **43** files, **36** conservative residual /
  composed occurrences retained. Effectful/throwing RHS and lazy module reads
  remain guarded. Empty-branch cleanup removes **29** empty elses and inverts
  **201** empty consequents. The native switch loses **7** break labels and its
  one unused switch label. No discriminant snapshot qualifies in the cohort.
- Complete sets unchanged: source **599/620**, previous **541/620**; **0**
  regressions or new completions. Text changes are intentional: source **107**
  complete files, previous **83**. Acorn/token checks pass **107/107** and
  **83/83**; Node syntax checks pass all changed files (one Ark lazy-import
  file uses a token/offset-preserving JS shadow, never for SDK input).
- SDK, default: source **12 identical / 95 approved / 0 differ / 0 unverified**;
  previous **12 / 71 / 0 / 0**. O0: source **0 / 107 / 0 / 0**, previous
  **0 / 83 / 0 / 0**. Each approved difference exactly matches a third SDK
  compilation of OLD with only independently AST-derived statement edits.
  Registers, branches, handlers and metadata are never normalized. Reports
  list each changed method and its approved rewrite classes.
- Paired samples also preserve **80/80** complete records: **2** changed files,
  **3** inverted empty arms; token/Node checks **2/2** and SDK **0 identical /
  2 approved / 0 differ / 0 unverified** at both optimizations.
- Full build succeeds. Source-regression **65/65**; new/extended readable unit,
  token and SDK-witness tests **3/3**. Requested sweep **147/159**, with **12**
  known pre-existing failures reproduced against OLD (ten via wtbase CTest,
  family/class CLI by their original commands). The only changed existing
  spelling expectation is `binding_initializer.mjs`: assert the inverted
  predicate and the single-return arm before deleting that return in its
  behavioral mutant. No differential/mutation assertion is weakened.
- The resource tests cover **200/201/400/800** exact byte/work boundaries and
  one-less refusal. Guard/catch/reference/scope identities stay intact; omitted
  text keeps source-map boundaries and counting/real sinks share the plan.
- Constructor/callee/selection snapshot removal is **deferred**, with the
  required joint invocation projection documented in
  [readable-statements.md](readable-statements.md). TimeUtils's retained DayInfo
  and phi assignments are unchanged. Evidence and raw SDK diffs:
  `build/review/readable3/`. No commit/push/stash performed.

## Staged call-field forests; TimeUtils complete (2026-10-21)

- Baseline/OLD **2a0ca2a8**. Added `LiteralFields` derivation and independent
  verification over the existing private LiteralPlan, selected-arm/constructor
  forests and checked binding reads. Prefix snapshots and native conditional
  assignments execute inside their original field, after the saved member
  callee and before its single invocation. Allocation exclusion, non-escape,
  original phi/Commit identity and catch-copy/order checks remain mandatory.
  No new premise or sample-specific admission.
- **o5/28303 m28799 completes**. PC642's object was only the first of three
  staged arguments; all three now verify through tree/operand/recovery. Fixed
  selected-arm anchor mapping through `SourceBlock` for projected for-of
  control, and construct retained conditional assignments at their proven join
  before lowering the containing object. `tree_failure_cli_test` now checks
  actual CLI/manifest completion and exact published-source identity.
- **o36/11209** passes the captured-this activation gap in getVideoProfile
  m12561 (store PC12, closure creation PC187, m11413 read PC29). The shared
  cyclic dominance index now includes completed stores even when the creator
  itself has no local load. Its next refusal is m12194 `initCamera`,
  `PhasedMethodError::kControl > ExceptionControlError::kCrossEntry` (3), at
  the independent compound-decision shared-arm/continuation check. This
  remains typed; no control ownership check is relaxed.
- Final 40-input cohorts: previous flags **541/620**, source **599/620**
  (**+1**, TimeUtils). Zero regressions and zero text changes to all previously
  complete records. Only o5 and o36 manifest rows change. New output passes
  `node --check`.
- Full build passes; **65/65** source-regression; **5/5** new tests; touched
  sweep **285/305**, with the same 20 pre-existing failures reproduced on OLD
  (18 via CTest, two by their OLD-binary commands). Node differentials cover
  **440** field/for-of scenarios and **24** captured-loop scenarios, with eight
  behavioral mutations. A separate early-TDZ closure fixture remains refused
  at the existing expression-ownership boundary, not counted as published.
- Real SDK round-trips: three fixtures, default/O0, **18 method pairs**;
  six identical entry methods and twelve unnormalized source-normal-form
  differences (private snapshot/phi storage, binding names, Boolean branch and
  protocol register/label layout). Calls/receivers, field definitions, guards
  and catch/close traces are independently checked; no normalization is counted
  as bytecode equality. Compiler/disassembler hashes and all differences are
  retained in `build/review/staged-fields/roundtrip-verified/`.
  Strict `grouping_bytecode.py` also ran: 3 differing records per optimization
  level, no unverified compilation, 3/3 Node syntax checks. This is a documented
  non-equality result for original/recovered normal forms, not a grouping pass.
- Exact 200/201/400/800 budgets and mutations pass. Actual unit RSS 50.2 MB;
  four-child-plus-parent cohort upper bounds 175.3 MB / 164.5 MB. Evidence,
  original/recovered excerpts and the o36 follow-up design:
  [staged-call-field-forests.md](staged-call-field-forests.md).


## Guarded captured PutValue; o26/4515 complete (2026-10-21)

- Baseline/OLD worktree **168ad183**. Added `guarded_stores` derivation and
  independent verification: original incoming environment/slot, stored SSA
  input, successful guard Commit dominance, an effect-free interval and the
  original catch destination. The verifier rebuilds guard/environment/effect
  facts from immutable inputs. Only certified stores carry SourceAccess under
  the existing SourceTdz contract; uncovered writes still join as Language.
  No new premise or initialized capture is assumed. The native `let` remains
  at its original first-initialization point, with original TDZ reads retained.
- **o26/4515 completes** under language-source. Its first refusal was m4696
  PC254/working251, CaptureCellError::kInput > CaptureError::kScope >
  ScopeError::kInitialization. Once those writes verify, m8331 exposes a
  guarded shared-decision prefix; its tree consumer now schedules the native
  read at the original guard, with independent TreeFlow correspondence.
- **o5/28303 remains precisely at m28799 PC642**, call241/property102,
  kNotInline > kRoot > kCrossRegionValue. The saved callee is op102 PC262/v526,
  before staged allocation104 PC268. The missing argument proof covers all four
  field forests: retained guarded receiver113/114, call130, constructor174 with
  two selection arguments, and call231/selection233 -> definition237. An
  incomplete last-selection-only experiment was removed. No production
  allocation/reference restriction is loosened. This is still a premise-free
  implementation gap, not a new user decision; the full ordered forest remains
  future work. The exact IR and next obligations are recorded in
  [guarded-call-references.md](guarded-call-references.md).
- Both 40-input comparisons against OLD: **541/620 -> 541/620** previous flags,
  **597/620 -> 598/620** language-source; **0 regressions / 0 already-complete
  text changes**. Previous manifests are byte-equivalent as JSON objects; only
  source o26/4515 changes (status/source/contract and entry/method position
  maps). Four-child-plus-parent conservative RSS bounds are **168,017,920
  bytes** previous / **174,899,200 bytes** source. New output passes Node syntax.
- Full build passes; serial source-regression **65/65**; focused tests **6/6**.
  Requested sweep **241/255**. All 14 failures reproduce against wtbase:
  tree_structure, try_tree, try_tree_source, unguarded_derived_cells,
  unguarded_literal_closure, unguarded_derived_literal_receiver,
  captured_receiver, unguarded_captured_receiver, unguarded_forwarded_receiver,
  reversed_initialization_captured_receiver, and the two unguarded reversed
  initialization receiver tests; plus family_source_cli and class_tree_source
  (not registered there, reproduced by their commands with baseline binaries).
  The initial sweep also found two old refusal expectations, decision_guard
  and phased_guarded_captures; they now assert verified completion with guard
  mutations / a Node differential, retaining the previous-contract refusal.
- New real es2abc fixture: early TDZ reader/writer, later initialized writes,
  nested closures and a catch around the store. **4 Node scenarios / 3 observed
  mutations**, plus an explicitly equivalent guard-removal control (native
  PutValue itself still checks TDZ). The published `onAudioInterrupt` callback
  separately matches its evaluation-only original source in **42 Node
  scenarios**. The engine consumes ABC only. Copies/block splits, wrong slot,
  depth/input/handler, missing/duplicate guard membership, an intervening effect
  or store, and exact byte/work limits are tested at **200/201/400/800**.
  The isolated 800-store builder plus fresh verifier costs **7,789,723 bytes /
  3,112,454 work**; the method shares its prepaid indexes and stays O(N log N).
- Recompiled fixture via grouping_bytecode.py: **0 exact / 2 differ / 0
  unverified** at default/O0, all compilation and disassembly successful.
  Differences are not normalized: retained TDZ reads add native assignment
  guards; existing anonymous-function name preservation, decision temporaries,
  register/IC allocation and literal/scope metadata also differ. See the
  [fixture README](../../../tests/integration/fixtures/guarded_stores/README.md)
  for the complete accounting and pinned compiler hashes. The full new o26
  record also recompiles at both settings; its callback retains seven stores,
  two catch regions and the async protocol counts, while the three isPlaying
  guards become six (original guards plus native PutValue). This is semantic
  evidence, **not strict bytecode/token equality**. Scratch commands, raw
  disassemblies, diffs, budgets and logs: `build/review/guarded-finish/`.
- No commit, push or stash. Unrelated `sample.tar.gz` left untouched.

## Guarded-call original-event composition (2026-10-21)

- Baseline/wtbase **24b01ff0**. Fresh ABC-only evidence confirms that
  o5/28303 m28799's PC56/58 pair is the captured TimeUtils **TDZ/Hole guard**;
  Use257 actually observes the retained PC86 call result as the PC610 indexed
  read's base. It is not optional chaining and that call is not replayed.
- Implemented the local composition gap: original throwing-event summaries and
  selection-join boundaries survive nonthrowing transports. Old composition
  runs first; only unmet member References seed the new pass. Every original
  throw keeps its dispatch/pre-throw state; catch staging, unique-use, order,
  saved callee/receiver, allocation exclusions and selection ownership remain
  independently checked in evaluation/TreeFlow/recovery. No new premise.
- **PC620 now verifies**. The exact next refusal is **m28799 PC642**, call241 /
  property102: FunctionTreeError::kExpression > ExpressionError::kUnsupportedOperation
  > CallPlanError::kReference > CallReferenceError::kNotInline >
  EvaluationKind::kRoot > TemporaryReason::kCrossRegionValue. The outer saved
  member consumes a staged object containing the earlier result; no
  GuardedCallError leaf remains. tree_failure.mjs pins this next obligation.
- **o26/4515 remains unchanged**: m4696 originalPC254/working251,
  CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization.
  This is incoming lexical slot2 (`isPlaying`), not a STMODULEVAR/export reorder.
  The store has an original Hole guard (read242/guard244; other stores273/611
  also have guarded reads). Forwarding still requires Language initialization
  at closure creation, before module-entry store618. A separate all-writes
  guarded-store/PutValue correspondence is needed; no callback timing or
  initialized capture is assumed. Design and exact source/storage evidence:
  [guarded-call-references.md](guarded-call-references.md).
- Both 40-input comparisons: **541/620 -> 541/620** previous flags and
  **597/620 -> 597/620** language-source; **0 regressions, 0 complete-text drift**.
  Only o5/28303's source-contract manifest changes; previous manifests are all
  identical. No newly complete exploration record. Conservative four-child +
  parent RSS bounds: **178,257,920 bytes** source / **166,805,504 bytes** previous.
- Full build passes. Targeted tests **6/6**. Requested sweep **235/247**;
  remaining 12 are the documented family_source_cli, tree_structure, try_tree,
  class_tree_source, try_tree_source, unguarded_derived_cells,
  unguarded_literal_closure, unguarded_derived_literal_receiver and four
  unguarded captured/forwarded/reversed receiver tests. Ten reproduce in wtbase;
  the other two corpus-dependent tests are not registered in that worktree and
  are documented pre-existing failures. Serial source-regression **65/65**. An earlier run overlapped a rebuild and hit 10 stale producer-hash
  checks; that run was discarded. The final stable build was rerun without
  rebuilding, and all 65 checks passed.
- Unit tests: copies/block splits, original throw destination mutations,
  foreign effects, guard/success/receiver/ownership mutations; 200/201/400/800
  exact byte/work and one-less refusals. New pure-guard 800 cost:
  **26,830,042 bytes / 13,799,480 work** (whole planner including verifier).
  `phorphan` now completes with the existing Node/mutation oracle, rather than
  weakening a refusal assertion. The new O0 fixture gives **26 Node scenario
  comparisons / 8 observed mutations**, syntax checks, plus **2 explicitly
  refused default-optimizer handler layouts** (CatchRegionError::kHandlerShape).
- grouping_bytecode.py was run at default and O0. Strict adapted full-record
  comparison: **0 exact / 2 differ / 0 unverified**; all methods compile. The
  changed expression method `run` is instruction/handler-identical in both
  modes. `pure` differs only in the Hole diagnostic name; entry adds a guard
  on its already-initialized recovered `pure` binding (plus default register
  transports). Literal/string byte offsets/emission order also change, with
  identical resolved literal/module identities. No normalization or false
  token-equivalence claim: the grouping token check rejects renamed/reordered
  declarations, as expected. Details and compiler hashes are in the fixture
  README. Scratch: `build/review/guarded-late/`. No commit/push/stash.


## Native member-update Reference consumed (2026-10-21)

- Baseline/wtbase **78278c38**. **o36/36985 m37638 originalPC378/working352
  completes under both contracts**. Get130 and Set134 consume the same retained
  load127 value through a verified member Reference. Native prefix/postfix and
  compound forms retain independent Get/ToNumeric/RHS/arithmetic/Set/result
  ports; no copied capture, extra member lookup or fabricated ExprId. The source
  target and operator are freshly checked in TreeFlow and on recovery projections.
- Only unmet member-call References seed this projection. Successful old plans,
  symbol updates, ordinary statement forms, literal exclusions and logical
  assignment admission stay unchanged. Strict store semantics are rechecked;
  no new premise. Original operand-role validation remains mandatory inside
  the certified intrinsic event set. Shared use/owner/effect indexes are prepaid,
  iterative, **O((V+E) log V)**; 200/201/400/800 exact byte/work boundaries and
  one-less refusals pass (800: **9,136,327 bytes / 1,759,389 work** for verification).
- Cohorts: **540 -> 541/620** previous flags, **596 -> 597/620** source;
  **0 regressions and 0 previously-complete text drift**. Only o36/36985's
  manifest row changes. Both new records pass node --check. Latest conservative
  four-child+parent RSS bounds: **166,363,136 bytes** source,
  **160,284,672 bytes** previous (below 1,000,000,000).
- Full build; serial source-regression **65/65**; targeted update/property/call
  tests **8/8**. Requested sweep **180/185**: only the documented pre-existing
  family_source_cli, tree_structure, try_tree, class_tree_source and
  try_tree_source failures. No assertions weakened. An initial overlapped-build
  archive-link failure was discarded; the complete build and suites were rerun
  without overlapping build commands.
- New es2abc default/O0 fixtures publish thirteen ordinary functions per mode
  plus two O0 catch factories. **5,852 Node differential cases**, **8 observed
  mutants**; the actual o36 callback separately matches its paired evaluation
  source on **140 async value/exception/effect/microtask cases**. Mutants cover
  target/result/role/order/consumption/ports/catch correspondence; copies,
  register permutations, block splits and exact resource exhaustion pass.
- The optimized catch fixture keeps **kRegion > kLayout > kHandlerShape**,
  independently reproduced on 78278c38. Catch-region admission is not widened.
  Recompile comparisons use the unmodified grouping_bytecode.py: **8 exact /
  48 differences / 0 unverified** across 28 wrappers and two compiler modes;
  all 28 Node syntax checks pass. Captured-let initialization/guards, extra O0
  copies and unchanged old scratch lowerings explain the inspected differences;
  these are not counted as bytecode-equality or grouping-token passes.
- Baseline census: **9 scratch update sites in 7 complete source records**;
  **8 in 6 previous-contract records**. Another 249/248 direct binary-store
  spellings are potential, unproved native candidates. Combined syntax census
  touches 26/24 records; all remain unchanged.
- Design/runtime citations, target source comparison and detailed bytecode
  categories: [member-update-constructor-events.md](member-update-constructor-events.md#native-retained-member-references-2026-10-21-baseline-78278c38).
  Evidence: `build/review/member-native/` (`build-final`, `regression`,
  `sweep-accepted`, `unit-result`, `cohort-*-diff`, `target-differential`,
  `roundtrip-accepted` and `census-*`). No commit, push or stash; unrelated
  sample.tar.gz preserved.


## Constructor-arm events; member update remains open (2026-10-20)

- Baseline/wtbase **2e33dc77**. **o7/28433 m28692 now completes under both
  contracts**. The new constructor event view joins the existing private literal
  forests to the constructor invocation/result and independently reconstructs
  original inputs, unique observations, commits and exception/handler identities.
  It is consumed only when old conditional coverage fails for an unmet member
  Reference. Existing schedule, TreeFlow and recovery checks remain mandatory;
  no printer change, allocation-exclusion change or new premise.
- **Partial delivery:** o36/36985 m37638 originalPC378/working352 remains at
  `kNotInline > kRoot > kCrossRegionValue`. Its base load127 is shared by
  Get130/Set134. Native member updates require a joint retained Reference and
  Get/conversion/RHS/Set/result ports, not an inline exemption. The current
  symbol-only UpdateExpr and single-property-action recovery path are unchanged.
  Exact IR, runtime citations and remaining integration:
  [member-update-constructor-events.md](member-update-constructor-events.md).
- Cohorts: previous flags **539 -> 540/620**, source **595 -> 596/620**;
  **0 regressions; 0 previously-complete text drift**. Only o7/28433's manifest
  row changes in each. Both newly emitted records pass node --check. RSS repeats
  have identical manifests; conservative four-child+parent upper bounds are
  **165,036,032 / 172,998,656 bytes**.
- Full build; **65/65** serial source-regression; **13/13** final targeted.
  Requested sweep **260/273**. All 13 failures reproduce with freshly built
  2e33dc77 baseline targets: family_source_cli, tree_structure, try_tree,
  literal_construct, mixed_literal, phased_literal, class_tree_source,
  literal_closure_source(+unguarded), derived_literal_receiver_source(+unguarded),
  phased_arm_selection_source and try_tree_source. No assertions weakened.
- New real es2abc default/O0 fixtures: seven methods publish per input;
  **4,688** positive Node scenarios and **10** observed mutations. Three
  constructor methods have **12/12 exact** default/O0 recompile comparisons,
  with no register/branch/handler/metadata normalization. The unchanged ordinary
  update lowerings have **6 matches / 10 differences**, explicitly reported;
  they are not counted as native member-update support. Grouping-only token
  checks flag original/recovered parameter names and are not passes.
- View/verifier mutations cover arm/owner, inputs, role, consumer, order, missing
  or duplicated events, Commit and exception/handler changes; early escape
  retains refusal. Exact byte/work boundaries at **200/201/400/800**; one less
  refuses. Width800 costs **9,293,274 bytes / 1,143,257 work** for view+verifier.
- Evidence: `build/review/member-updates/` (cohorts/comparisons, manifest changes,
  RSS, full build, source-regression, targeted-final, sweep, baseline-failure
  results, fixture compiler commands and bytecode reports). No commit/push/stash;
  unrelated sample.tar.gz preserved.


## Shared-phi nullish call References (2026-10-20)

- Baseline/wtbase **f1eb2418**. **o39/12680 m13322 completes under both
  contracts**. The new view proves the complete same-phi null/undefined
  selection by four-domain partitions, retaining every original child/test/
  phi edge. Joint-use and eager-ancestry verification compose the two saved
  member callees without a second lookup. TreeFlow/OriginalFlow/recovery
  independently rederive the view and original event correspondence. No new
  premise, printer change, phased admission, or allocation exclusion change.
- Both 40-input cohorts finish: previous flags **538 -> 539/620**, source
  **594 -> 595/620**. **0 regressions; 0 previously-complete text drift**.
  The only changed manifest row in each is **o39/12680**. Both newly emitted
  records pass `node --check`. Conservative 4-child RSS upper bounds plus
  parent are **165,658,624 bytes** / **175,620,096 bytes**, below 1 GB.
- **o36/36985 m37638 PC378** still requires a member-update Reference
  certificate for the shared captured base (Get + Set + numeric result).
  **o7/28433 m28692 PC166** still requires the constructor-result wrapper
  around verified literal forests. Existing typed refusals are unchanged;
  **o5/28303** was not pursued. Exact IR and the next proof boundaries:
  [cross-region-call-values.md](cross-region-call-values.md).
- Extended joint-selection unit cases cover direct/nested references, copies,
  register/block variants, proof/edge/domain/fallback/effect/operand mutations,
  and **200/201/400/800 exact work+byte boundaries**. At 800 nested calls the
  exact allowance is **90,682,592 bytes / 21,949,101 work**; at width800 it is
  **35,744,950 / 8,257,111**. One less refuses with the typed limit.
- Real SDK fixtures: **5 O0 methods publish**, **5 optimized-input encodings
  retain their typed Reference refusal**. New Node differential: **17,280
  scenarios**, all **10 mutations observed**; existing Boolean differential:
  **21,000 scenarios**. All **10/10** default/O0 per-method disassemblies match
  exactly, no instruction/control/handler/register normalization. The separate
  grouping-only token checker correctly flags parameter renaming (5/5); it is
  not counted as a grouping pass. Compiler identity/commands are pinned in the
  fixture manifests and raw comparison report.
- Full build passed; **65/65 serial source-regression**, **7/7 targeted
  tests**. Requested sweep **208/214**; the six failures are pre-existing:
  `family_source_cli_test`, `tree_structure_test`, `try_tree_test`,
  `class_tree_source_test`, `try_tree_source_test`,
  `phased_arm_selection_source_test`. All six were additionally reproduced by
  the exact same commands against freshly linked **f1eb2418** baseline
  binaries (`baseline-failures.json` and individual baseline logs). Only the
  three optional/nullish fixture expectations moved refusal -> publication,
  backed by the new differential; no negative expectation was weakened.
- Scratch: `build/review/remaining-calls/` (final cohorts `verified-source` /
  `verified-previous`, RSS-measured identical manifests in `final-source` /
  `final-previous`, `manifest-changes.json`, `unit-final.log`, `bytecode/`,
  `full-build-final.log`, `source-regression-final.log`, `targeted-final.log`,
  `sweep-final.log`).
  No commit/push/stash performed; unrelated `sample.tar.gz` preserved.


## Native switch fall-through (2026-10-20)

- Baseline/wtbase **f5c62475**. **o18/57687 completes under --language-source**;
  m58255's effectful close case is owned once, entered by original fall-through
  edges e31/e33. Native `SwitchStmt`/case syntax, fresh value/edge-derived control
  verification, TreeFlow and every recovery projection preserve the cached
  discriminant, lazy case tests, default position, breaks, returns and loop
  continue. Empty labels have required retention. No new premise; old successful
  if-ladder plans are unchanged. Details and exact original/emitted callback:
  [native-switch.md](native-switch.md).
- Both 40-input cohorts: **538/620 unchanged** (previous flags),
  **593/620 -> 594/620** (source); **0 regressions, 0 complete-source text drift**.
  Only source-contract record o18/57687 changes its manifest. Its previous-flag
  capture refusal in m57863 PC123 is unchanged.
- Full build passed; **65/65 source-regression**, **3/3 new tests**;
  requested sweep **186/192**, six known failures independently reproduced with
  rebuilt wtbase targets. Real es2abc fixture: **3,150 Node scenarios per flag
  set**, plus **260 paired target scenarios**, mutations and 200/201/400/800
  width/depth exact budgets. New output passes `node --check`.
- Per-method es2abc round-trip: all seven pairs compile at default/O0;
  strict assembly equality is **4/7 default, 0/7 O0**. Remaining Boolean lowering,
  private storage/register transport and jump-to-follow differences are listed
  individually in the design note; no broad normalization or equality claim.
- Scratch evidence: `build/review/native-switch/`; conservative four-child plus
  parent peak RSS upper bounds are below 174MB. Unrestricted test-only census:
  41/51 previously complete records (68/89 sites, previous/source) would pass
  native publication; production does not convert them. Phased/nested unproved
  ownership, shared lexical scope and effectful loop-continuation corridors
  keep typed refusals. No commit, push or stash performed.

## Switch ownership checkpoint: shared optional-break corridor (2026-10-20)

- Baseline **ca4fc005** (HEAD and built wtbase). This is a **partial round**:
  **o19/18254 completes under --language-source**; effectful native switch
  fall-through in o18/57687 remains unimplemented, and o32/40109 m40670 retains
  `DecisionPrefixError::kPrefixArms` at op183 PC476. The source tree currently
  lacks `SwitchStmt` and case/fall-through recovery ports; no effectful body is
  duplicated or ignored. See [switch-case-ownership.md](switch-case-ownership.md)
  for the original-edge diagnosis and remaining integration boundary.
- `ControlForward` records an original incoming edge into a verified epsilon
  corridor: no live input/result/phi transfer/effect/exception and unchanged
  natural-loop activation. An independent fresh derivation checks the view,
  then the unchanged region verifier proves all original edges and unique
  ownership. Memoized corridor closure handles block splits in linear work.
  The normal plan exports only real EdgeIds. TreeFlow records epsilon ports
  without invented expressions/statements; original and recovery flows keep
  the original graph. Successful old plans take precisely their old path.
- Cohorts vs wtbase: previous flags **538/620 unchanged**, source contract
  **592→593/620**, zero regressions and zero prior-complete text drift. Only
  o19/18254's manifest failure/status changes; previous flags retain its existing
  m19967 capture-scope refusal. The whole newly published record passes Node
  syntax. Repeat final cohort runs and OS RSS are under
  `build/review/switch-ca4fc005/`.
- New fixture: counted loop + multiple switch cases + optional calls, **800
  independent Node observations per flag set**, four observed mutants. Actual
  o19 callback: **960 original/emitted scenarios**, including changing reason
  and enum-property getters, callback/log effects, devices and pause failures.
  Paired source is never engine input. Mutation/unit tests retain the refusal
  for effectful shared bodies, definitions, throwing operations and phi
  transfers; owner/port/target forgeries reject. Exact 200/201/400/800 byte/work
  limits, original-ID permutation and split corridors pass.
- Full build and focused final tests **5/5** pass. Fixed source regression
  **65/65** passes; requested sweep **194/200**. Its six failures are unchanged:
  `family_source_cli_test`, `tree_structure_test`, `try_tree_test`,
  `class_tree_source_test`, `try_tree_source_test`,
  `file_generator_return_loop_source_test`. Four reproduce with wtbase CTest;
  the two not registered there reproduce with current scripts and wtbase
  executables. No assertion is weakened. The old empty-overlap unit graph now
  asserts verified epsilon ownership, with an effectful version retaining its
  original refusal (and an independent fixture differential).
- Final 800-unit exact admission: **9,466,343 bytes / 3,750,042 work units**;
  minimum-minus-one refuses. Both final cohort runs report all 40 exporters
  successful. Conservative four-child-plus-coordinator OS RSS is at most
  **169,738,240 bytes** (largest individual child 41,844,736), below 1 GB.
- SDK `grouping_bytecode.py` default/O0: module entry exact, both fixture
  functions differ by native-switch vs recovered-if layout, strict comparison
  orientation, feedback slots, O0 register/copy allocation, and the numeric
  counter's redundant conversion. **0 exact / 1 different record per mode**;
  this is not claimed as bytecode equality. No allowance/normalization was
  added. Node and independent original-edge flow supply the semantic evidence.
  Compiler identity/commands/hashes and full differences are documented.
- No commit, push or stash. The pre-existing untracked sample archive is untouched.


## Control ownership: shared abrupt tails (2026-10-20)

- Baseline **aa545dd4** (HEAD and wtbase). Fresh diagnosis of all four duplicate
  ownership records is in [control-shared-terminals.md](control-shared-terminals.md).
  `ControlOwnershipError(0)` is duplicate traversal, not duplicated bytecode.
- `control_plan.cc` retains every successful existing layout. On an incomplete
  ownership census, dominator/ReachMeet and real-postdominator facts can prove a
  shared single-completion abrupt suffix and refine follows once. The disputed
  continuation stays ordinary; decision prefixes still require all-path entry.
  No block/edge/effect is duplicated, no verifier is loosened, no new premise.
  Independent control verification, TreeFlow and recovery stay mandatory.
- **o38/13920 SendCommandComponent completes in both flag sets.** m14655's
  shared early returns no longer absorb the final continuation. o32/40109 m40670
  passes control and next refuses at `DecisionPrefixError::kPrefixArms`, op183
  PC476. o18/57687 (real switch fall-through) and o19/18254 (shared switch / optional
  normal join) retain their exact original refusal. Those are separate work.
- Both 40-input cohorts vs wtbase: **537→538/620** previous flags,
  **591→592/620** source contract, **0 regressions, 0 prior-complete text changes**.
  All new outputs pass Node syntax. No printer change. `tree_failure_cli_test`
  additionally asserts the new completion and CLI/manifest correspondence.
- Real es2abc fixture: three functions (including protected dispatch), **1,440
  Node scenarios per flag set**, five observed mutants. The real application's
  recovered callback is independently compared with its original source in
  **4,176 scenarios per flag set**, including changing getters, receiver identity,
  throws and early returns. Source never enters the engine. Unit tests cover
  normal/projected graphs, internal diamonds, linear splitting, ID permutation,
  ownership/entry/port mutations and exact 200/201/400/800 work+byte boundaries.
  At 800: 3,209 vertices, 731,872 work units, 2,025,543 prepaid bytes.
- Full build passed; fixed source-regression **65/65**; focused final tests **4/4**.
  Final requested sweep **156/162**. Six baseline failures remain:
  `family_source_cli_test`, `tree_structure_test`, `try_tree_test`,
  `class_tree_source_test`, `try_tree_source_test`,
  `file_generator_return_loop_source_test`. Four were reproduced by wtbase CTest;
  the two not registered in that build were reproduced with the same current
  scripts and wtbase executables. No expectations were weakened.
- SDK default/O0 recompile comparisons: `classify` and module entry are exact;
  dispatch and protected dispatch differ by source if-ladder inversion/layout
  and consequent registers, labels, feedback slots and addresses. Strict
  `grouping_bytecode.py` therefore reports **1 differing record per mode**, not a
  bytecode-identity pass. No normalization/allowance was added. The full diff,
  unchanged handler coverage and semantic validation boundary are documented.
  Repeat full-cohort OS RSS measurements bound four concurrent child peaks plus
  the coordinator at **164,216,832 bytes** (largest child 40,321,024), below 1 GB.
  Evidence is under `build/review/control-ownership/`; nothing committed/pushed.


## Readable output: await/class/optional envelopes and negated equality (2026-10-20)

- HEAD **3b93b16c**, OLD/wtbase **92e4bfcb** as authorized. Both 40-input
  cohorts retain their complete sets: **537/620** previous flags,
  **591/620** source contract; all 1,240 statuses and first causes are unchanged.
- [readable-grouping.md](readable-grouping.md): real print contexts now decide
  await, certified named-class and optional/nullish projection grouping.
  Required assignment/member, await/member, postfix chain termination, unknown
  contexts, indirect-call wrappers and braces are preserved. Under ReadableV1,
  proven initializer replacements carry NOT only to `!=`/`!==`, emitting their
  exact equality complement; relational operators are excluded. Shared index
  work is linear, prepaid and covered at 200/201/400/800 exact byte/work limits.
- Source contract: **115 changed emitted files** (114 complete +1 partial),
  **413** removed pairs, **16** equality rewrites. Previous flags: **98** files
  (95 complete +3 partial), **299** pairs, **12** rewrites. Acorn/token and Node
  syntax checks pass every changed file. One ArkTS file uses a type-only Node
  shadow with annotations checked identical; the compiler receives real types.
- SDK default and O0 each: source **108 exact +7 witnessed /0 differ/0 unverified**;
  previous **94 exact +4 witnessed /0 differ/0 unverified**. Strict is the
  default. The new opt-in `--negated-equality` acceptance recompiles OLD with
  only the independently AST-proven operator rewrites and requires NEW to match
  that entire disassembly exactly. No register/branch/handler normalization.
- Grammar census: source await-assignment grouping **70->0**, statement-await
  **268->0**, class envelopes **2->0**, optional `+` envelopes **20->0**,
  negated inequalities **16->0**. Raw `= (await` **72->2**: the two survivors
  are required member bases. All 6 required await/member and 164 assignment/member
  groups remain. Detailed previous-flags counts and examples are in the design.
- Full build passed; final fixed suite **65/65**; focused checks **18/18**;
  bytecode-tool unit checks **3/3**; receiver-spelling refresh **4/4**.
  Broad sweep: **483 selected**, raw **450 pass/33 fail**. After the explicit
  spelling and isolated timing reruns: **456 pass/27 baseline-reproduced failures**. All
  application graph shards pass; no new semantic failure remains.
  The concurrent exploratory run's fixture ENOENT is superseded by the clean
  fixed rerun. Definition-elision's loaded VM-timeout mismatch passes its
  isolated fixture+Node rerun **2/2**. The community-ability graph also passes its final isolated full matrix
  (**104 cases/330 faults/66 swaps/22 mutations/2 equivalents**), after two
  worker-timeout attempts retained in the logs. Paired OLD/NEW checks add
  **80/80 unchanged complete records**, 7 changed files and 30 removed pairs;
  token/Node **8/8**, SDK default/O0 **7 exact/0 differ/0 unverified** each.
  No timeout, case inventory, mutation or oracle was weakened.
- Existing stale spelling checks were updated for await/generator/backup,
  capture scope, precedence/Boolean contexts, conditional snapshots, VoIP,
  card-item, navigation and reversed receivers. Every mutation still changes
  valid source and is behaviorally detected. Snapshot-position failure also
  reproduces on wtbase at `snapshot_syntax_test.cc:109`.
- Evidence: `build/review/readable2-3b93/` (fresh cohorts, changed-file SDK/token
  reports, grammar census, exact commands/hashes, build/CTest logs). Fresh 80-job,
  four-worker resource run: max engine RSS **44,023,808 bytes**, conservative
  engine+runner bound **197,705,728 bytes**. No commit/push/stash; unrelated
  `sample.tar.gz` is untouched.

## Remaining o32 closes: nested entry boundary — partial (2026-10-20)

- Baseline/wtbase **92e4bfcb**. This round is **not complete**: no new method or
  record publishes. Source cohort **591/620**, previous flags **537/620**;
  both40-input comparisons have zero regressions, zero complete-text changes,
  and **zero manifest changes**. The three first refusals remain
  `PhasedMethodError::kControl > ExceptionControlError::kMultipleExits`:
  m42717@633, m47128@398, m23306@149 (working125).
- [for-of-remaining-close.md](for-of-remaining-close.md): derive an original
  enclosing-close entry boundary, rather than walking through its effectful
  implementation. m42717 now derives all four close copies and verifies their
  separate priorities. Inner exit **Commit685 -> Before687** and outer entry
  carry the same **Return(V232, caller)**. Fresh backward guard verification
  rechecks this correspondence. Structurally unreachable handler rethrows are
  excluded from arrivals using original exception dominance, not a PC rule.
- No nested ownership admission was added. Remaining m42717 witnesses:
  inner private-state live-out **PC753**, outer return/inner-wrapper role
  **Try4**, inner-body dispatch **PC705**, outer-body dispatch **PC480**.
  Shared completion storage and CURRENT inner-error -> outer-Throw dispatch
  still need joint ownership. m47128's second close remains body-dispatch
  **PC717**, plus unimplemented captured per-iteration binding; m23306 remains
  raw async correspondence **original99/working96**. These are kept typed.
- Added real es2abc O0/default fixture with compiler/command/hash registry,
  original-edge/correlated-value mutations, omitted-close rejection,
  200/201/400/800 padding, register renumbering, and exact bytes/work limits.
  O0 entry/priority checks pass; default fixture has the earlier ExceptionPlan
  layout gap@450. Neither is counted as engine-output Node/round-trip success.
- Full build passed; fixed regression **65/65**; focused **4/4**;
  requested sweep **236/244**. The seven user-listed failures remain;
  `async_semantics_test` also reproduces the same bad_variant_access on a
  freshly built **92e4bfcb** wtbase target (previously documented below).
  Existing published for-of Node differentials and SDK comparison tests pass.
  No assertion was weakened into a claimed publication.
- Evidence: `build/review/for-of-remaining/` (cohorts, comparisons, probes,
  exact budgets, build/CTest logs, baseline-async reproduction, summary.json).
  Fresh80-job/four-worker resource run: max engine RSS **40,337,408 bytes**;
  conservative concurrent engine+runner bound **182,992,896 bytes**.
  No commit/push/stash performed; unrelated sample.tar.gz preserved.

## For-of close under an enclosing user try (2026-10-20)

- Baseline/wtbase **3e475a3b**. **o37/20175 m20512** and **o38/25106 m25655**
  now complete; their inner `kMultipleExits` refusals are gone. Source contract
  cohort **589→591/620**; previous flags **537/620 unchanged**. Both40-input
  comparisons have zero regressions and zero text changes to previously complete
  records; only the two source-contract RpcTool manifests change. No new premise.
- [for-of-user-try.md](for-of-user-try.md): separate verified ordinary enclosure
  identities from owned close members. Build a sealed close statement first,
  then install ordinary try/catch wrappers with the shared scope/corridor and
  try/loop composer. Completion ports are region/arm/target keyed; every original
  incoming edge remains. No fabricated graph, ExceptionPlan or ExprId. Independent
  control, TreeFlow and recovery checks rebuild ownership, scopes, catch targets,
  payloads, unique binding commits and exception routes.
- Transparent getter rethrows can terminate at the retained user catch after
  independent same-payload route verification. Guarded-read action regions and
  conditional TDZ exclusions keep their original dependencies and catch context.
  Private state, unverified activation/enclosure, ownership, scope, catch-state
  and incomplete nested/finalizer/async obligations still refuse. Buffers are
  admitted first; sparse port propagation bounds admitted N log N work.
- Real SDK fixtures have source/ABC/command/hash registries at default/O0.
  Engine-output Node: **865** main/member/multiple-close/catch traces; real
  RpcTool queries **648** paired-source evaluation cases; refreshed throw-domain
  fixtures add **36** cases, captured-local cells/skip add **108** plus effective
  mutations and call isolation. Paired source never enters the engine. Published
  full records pass `node --check`; full methods next to originals are retained
  in `build/review/for-of-enclosing/query-o{37,38}/comparison.md`.
- SDK comparisons use grouping_bytecode.py's strict assembly reader, with all
  register/branch/handler/metadata diffs preserved. The synchronous fixtures have
  **8 method comparisons:4 identical entries,4 differing scan bodies**; both O0
  recompilations verify the full protocol/evaluation proof. Default recompiles
  retain the existing handler-shape/layout gaps (331/129). All four real-query
  default/O0 recompilations succeed but differ from original bytecode; control
  restructuring, key binding, materialized reads, registers/IC slots and dead
  Throw-result checks are documented. Scsi's eight named calls are reproduced
  with matching API24/callable-name options; no error-message erasure is assumed.
  Compilation and Node are not claimed as Ark execution or bytecode equality.
- Full build passed; serial source-regression **65/65**; focused new/refreshed
  tests **21/21**, including mutations, register/block variants,
  200/201/400/800 and exact byte/work budgets. Sweep **219/226**: only supplied
  pre-existing family_source_cli, tree_structure, try_tree, class_tree_source,
  try_tree_source and the two file_generator_return source failures remain.
  Maximum measured engine RSS **42,713,088 bytes**; four-worker runner/child
  aggregate upper bound **189,759,488 bytes**. Logs and full diffs:
  `build/review/for-of-enclosing/acceptance-*`, `accepted-*-comparison.txt`,
  `resource-parallel/report.json`, `sdk-*` and `query-o*`.
- Async raw resume/rejection and finally completion derivation stay typed
  refusals. Re-probed DataDriver m42717 still needs CURRENT inner→outer completion
  propagation at692; it was not widened by this enclosure composition.

## Multiple for-of abrupt-close copies (2026-10-20, partial delivery)

- Baseline **1ea8d4e6**, also the independently built wtbase. Diagnosis and
  pinned spec/frontend references: [for-of-multiple-exits.md](for-of-multiple-exits.md).
  `kMultipleExits` is **4**; o15/37098 is unrelated `kCrossEntry` **3** and
  has no synchronous iterator acquisition in 273 decoded instructions.
- Compose complete guard/reference/outcome/priority close forests before the
  body-wrapper split. Shared protocol/original-edge/member indexes keep abrupt
  close copies separate from ordinary body operations. Follow private state
  only through certified copies; ordinary observations, user tries and missing
  exits still refuse. The independent verifier rederives all sets and the
  original exhaustion/canonical-boundary identities. Topology keeps the empty
  exhaustion corridor for native break, so register/block splits preserve one
  lexical loop exit. Every buffer is charged; sparse work remains bounded by
  the existing input-sized N log N grant. No new premise (V2 acquisition and
  the approved V4 external-exception-no-Hole predicate remain required).
- Extend the common evaluation, exceptional phi staging, operand/TreeFlow and
  recovery handoffs. Protocol catch/Hole/guard/phi state gets no source symbol.
  The new catch projection is derived from the verified inline-close split;
  old successful plans retain their original staging/root view. No generic
  internal-value, ownership, catch-state, scope or source check is relaxed.
- Real es2abc `multi` fixture moves from **kMultipleExits -> complete native
  for-of**. It exercises break/Return/Throw/same-loop continue; engine-output
  Node differential **270/270 traces**, including missing/null/primitive return,
  getter/call failures, original Throw identity/priority, receiver and zero
  iterations. Six fixtures have O0/default ABCs with hashes and compiler
  identity/options. Labelled/async/nested/user-catch/live-out forms remain
  explicit negative gates, not source-semantic passes. Unit tests include
  recovery/port/staging/split mutations, 200/201/400/800 graph padding,
  register/block-split equivalents and exact byte/work boundaries.
- SDK comparison uses grouping_bytecode.py's strict reader, no register/branch/
  handler normalization: **4 compiler runs / 2 pairs / 4 methods compared**;
  the module entry is identical in both modes (**2/4 methods**). The scan method
  differs by continue predicate inversion/redundant jump; O0 also exposes the
  recovered assignment binding's outside-loop initialization and temporary
  allocation. Both retain matching real acquisition, return-read, object-check
  and receiver-call counts. Recompiled O0 verifies the full standalone
  protocol; default remains **ExceptionPlanError::kLayout > CatchRegionError
  at PC112**. This is a reported round-trip analysis gap, not an equality/seal
  pass. Full diffs and exact options are retained in the SDK report.
- **No corpus target completes.** RpcTool m20512/m25655 now pass entries/ports/
  private state; their remaining ownership failure is the effectful outer
  ordinary try (Try4 PC236/H759 and PC246/H801). configService m47128 still
  needs cleanup forwarding at717 plus captured per-iteration binding and
  independent multi-acquisition composition. DataDriver m42717 still needs
  inner CURRENT-completion -> outer input at692. Async m23306 still needs the
  raw await/rejection adapter at original99/working96. All five retain their
  original first kMultipleExits refusals, including the RpcTool omissions.
- Full build passed; serial source-regression **65/65**; new tests **6/6**;
  focused close/body/ownership tests **5/5** plus those new tests (**11/11** together). Requested
  sweep **192/198**: supplied pre-existing family_source_cli, tree_structure,
  try_tree, class_tree_source and try_tree_source; additionally
  file_generator_return_loop_source fails at loop_closure_file_test.cc:1311
  identically on a freshly built **1ea8d4e6** wtbase. No new failure remains.
  Three former component-gap assertions now require the complete independent
  earlier proof and the exact later gate; source rejection and all existing
  behavioral/mutation assertions remain checked. tree_failure.mjs is unchanged.
- Both 40-input cohorts: **537/620 previous, 589/620 source**, **0 regressions,
  0 new completions, 0 text changes**. All manifests and all emitted source,
  including partial source, are byte-identical to baseline. Resource rerun
  max engine RSS **41,910,272 bytes**; conservative four-worker + runner bound
  **186,400,768 bytes**, below 1GB. Evidence under `build/review/for-of-multiple/`:
  acceptance build, source-regression/new-tests/sweep-verified logs, old/verified
  cohorts and comparisons, per-method verified probes, resource/report.json,
  sdk-verified/report.json and strict per-method diffs. No commit/push/stash.

## RpcTool query: edge-qualified explicit-throw domains (2026-10-20)

- Baseline **2186fbff** (wtbase verified at that commit). Diagnose both original
  query ABCs; the failing value is the iterator-close **Hole/error sentinel
  phi**, incorrectly propagated through a conditional explicit throw into the
  user's catch parameter. It is not an await/capture lifetime or missing
  source-binding problem. Exact SSA/PC traces, pinned equality semantics and
  the proof are in [exception-throw-domain.md](exception-throw-domain.md).
- Add a shared prepaid original exception-CFG edge-dominance/SCC and scalar-copy
  index. Derive and independently rederive a non-Hole constraint only for an
  explicit throw dominated by an unequal-to-Hole edge of the same dynamic SSA
  identity. Filter that dispatch contribution, never the global SSA value or
  another catch source. Keep local raw Hole throws, other internal kinds,
  comparator exceptions, TDZ guards and original effect/Commit order. Cyclic
  definitions keep the conservative refusal. Complexity O((N+E+V+U) log(N+V));
  all buffers admitted before allocation. No new source/runtime premise.
- Only failed ordinary input proofs invoke the fallback. Preserve an existing
  omitted-member gap's provenance when it progresses to located kMultipleExits;
  this does not admit unrelated omissions or supply executable unknown bodies.
  Both RpcTool classes remain **partial status4**, with their recovered methods
  unchanged. Query remains omitted: **m20512 TryId0 PC640/H665** and **m25655
  TryId0 PC682/H707**, now **PhasedMethodError::kControl >
  ExceptionControlError::kMultipleExits**. No native-for-of ownership loosening.
  o36/11209 is not attempted because neither target completes.
- Full build passed; serial source-regression **65/65**; focused new/omission
  tests **4/4**. Width/depth 200/201/400/800 pass exact discovery/verifier
  byte/work boundaries and mutations (payload/edge/branch/evidence, alternate
  entry, dropped/inverted guard, raw Hole, other internals, stale activation).
  At width800 discovery uses **4,758,692 bytes / 1,756,691 work**.
- Pinned es2abc O0/default fixtures: Node compares **2 engine-output traces /
  10 behavior scenarios**, detects **6 mutations**, and checks **2 partial
  class good() methods** while asserting query is absent. Twelve distinct
  contract/fixture/optimization cases retain exact typed refusals, not passes.
  The raw close fixture verifies the new domain facts in the unit test. Both
  strict SDK recompilations of the published handler fixture match the source:
  **2/2 complete assemblies, 4/4 methods identical**, including handlers/module
  metadata, with no normalization. SHA/commands and the identical AST export
  alias adaptation are recorded by the reusable round-trip tool.
- Both measured 40-input cohorts remain **537/620 previous**, **589/620 source**,
  with **0 regressions, 0 new completions, 0 text changes** in complete records.
  Only o37/20175 and o38/25106 manifests/omission-location comments change in
  either contract; partial class bodies are otherwise byte-identical. CLI /
  manifest evidence and text parity **4/4**, target partial `node --check`
  **4/4**. Max measured engine RSS **45,006,848 bytes**; conservative four-worker
  engine bound **180,027,392**, plus runner peak **20,103,168**, below 1GB.
- Requested sweep **178/192**, with exactly the supplied **14 pre-existing**
  failures: family_source_cli, tree_structure, try_tree, derived_cells,
  class_tree_source, derived_cells_source, unguarded_derived_cells_source,
  try_tree_source and the six captured_receiver variants. No new failure and
  no assertion weakened. Source/SDK fixture metadata and both JSON paths are
  checked by the new tests; no corpus source was supplied to the engine.
  Evidence under `build/review/rpctool/`: full build, serial65/new-tests/sweep
  logs, raw SSA probes, immutable OLD/verified cohort outputs, resource reports,
  manifest-change census and SDK full diffs. No commit, push or stash.

## Exception-aware captured-cell scope (2026-10-20)

- Baseline **adec8da6**, including the independently built wtbase engine.
  Diagnose all four ScopeError::kInitialization records from original ABC.
  The largest common group is **o37/20175 + o38/25106**: an async callback
  captures the pending result of `let data = await bindDeviceDriver(...)`
  before its first store. o26 is a late Language **write** requirement;
  o36 is an already initialized receiver cell after an unrelated loop. Those
  two distinct proofs are unchanged. Exact PCs, slots, environment/capture
  chains and independent source lines are in [capture-scope.md](capture-scope.md).
- Add a prepaid original exception-CFG unique-store/allocation-Commit index,
  freshly rederived by the verifier once per phased owner. Working dominance
  independently requires the real Commit and retains exceptional paths.
  A native deferred declaration uses the store's private descendant scope only
  when it contains every access/demand and introduces no additional loop entry
  relative to the original environment activation. Builder and TreeFlow both
  reconstruct this scope; common name, capture, exception and recovery proofs
  still run. No synthesized initialized capture, unguarded Language exemption,
  new source premise, printer change or plan-local cross-owner CellId join.
- Both RpcTool **bind methods now recover** and preserve the callback's native
  TDZ before success and after rejection. Neither whole record completes:
  o37 now has an omitted **query m20512 PC804**, and o38 **query m25655 PC846**, both
  **ExceptionInputError::kInternalValue**. Do not chase those distinct member
  obligations in this slice. Only these two manifests change under either
  flag set: refusal status3 becomes partial-class status4, with method/entry
  spans and the exact omitted-member causes. CLI/manifest parity **8/8**.
- Full build passed. Serial source-regression **65/65**; targeted tests **8/8**
  including all three new unit/CLI/SDK tests. Mutations cover wrong slot,
  environment/allocation, store, certificate interval, capture family/scope,
  reset, catch route, an old allocation crossing a loop and native TDZ/reader
  behavior. Width and split/copy scale 200/201/400/800 pass exact byte/work
  boundaries for original discovery, capture plans, working deferral and native
  scope proof. At800 the native scope admission boundary is **716,914 bytes /
  342,575 work**, or **2,131,314 / 1,048,195** with added copy blocks.
- Real pinned es2abc fixtures: three O0 source-contract outputs and two O0
  previous-contract outputs publish. Node compares **5 full traces / 25 call
  scenarios** plus retained readers, with **13 detected behavioral mutations**;
  `node --check` also passes the two new partial record outputs under both
  contracts. Seven fixture/contract cases assert precise remaining refusals
  (default-optimized catch-store/catch-region limitations, and the previous
  contract's async callback array-return EvaluationError::kOwnership).
- SDK round trip: **6/6** emitted compilations succeed at O0/default;
  **18 methods compared**, with **6 identical entry methods / 12 differing
  function-callback methods**, and no register/branch/handler normalization.
  The differences are parameter-to-private-cell body initialization, redundant
  proven-nonthrowing guards, alpha-renamed guard strings, and equivalent
  inverted branch layout/cache operands. They are retained and explained in
  the design note, not counted as bytecode equality. Existing Acorn AST export
  alias adaptation is applied to both sides; compiler SHA-256 and commands are
  recorded. No Ark runtime execution is claimed.
- Both complete 40-input cohorts have **zero regressions / zero text drift**:
  previous flags **537/620**, source contract **589/620**, unchanged complete
  sets. Largest final child RSS **41,762,816 bytes**; the four-worker bound is
  **167,051,264 bytes**. Evidence, all changed manifest fields, recovered bind
  methods, raw instructions, source comparisons and exact budgets are under
  `build/review/capture-scope/`.
- Required broad sweep: **178/194**, with the **16 supplied pre-existing
  failures**: family_source_cli, tree_structure, try_tree, derived_cells,
  class_tree_source, derived_cells_source, literal_closure_source,
  unguarded_derived_cells_source, unguarded_literal_closure_source, the six
  captured-receiver variants, and try_tree_source. No existing expectation
  changes were necessary; tree_failure does not pin the two RpcTool records.
  No commit, push or stash was performed.

## Joint selection ownership for member-call arguments (2026-10-20)

- Baseline **1f4999b8** (wtbase binary verified at that commit). Add an
  unmet-reference-only `JointSelectionUse` certificate for an original phi's
  pure predicate occurrences and unchanged keep edge. Fresh truth equations,
  original edge identities and independent forward event intervals must all
  verify; no new source-language premise, opcode-sequence admission, printer
  change or blanket phi-use exemption. Shared prepaid indexes, explicit stacks
  and path compression preserve O((V+E) log V). The exact IR and remaining
  nullish obligation are in [cross-region-call-values.md](cross-region-call-values.md).
- **o18/22901 m24302 PC212** and **o18/38531 m39900 PC212** now complete.
  Their callback prints
  `Button.enabled(!!$local0.ca && !!$local0.cert && !!$local0.privateKey)`.
  Paired source line186 in each file independently agrees. Source was never an
  engine input. **o39/12680 m13322 PC415 remains refused**: its nullish root
  owns a partial child sharing phi20, so there is no complete retained-value
  truth-event projection. This requires shared-phi fallback/keep-edge
  correspondence, not a premise. o7 constructor arms were not attempted.
- Full build passed; source-regression **65/65** serial; new unit/Node tests
  **2/2**. Unit tests cover copied/renumbered/split variants, extra consumers,
  changed keeps/owners, missing/duplicated records, observable barriers and
  independent tree/recovery operand mutations. Many-call and one-call width
  scales 200/201/400/800 pass exact byte/work boundaries; at800 the admitted
  boundaries are 47,145,912 bytes / 11,878,221 work (calls) and
  18,869,302 bytes / 4,581,544 work (width).
- Node differential **21,000 scenarios**, preserving undefined/NaN/-0/object
  identity, function ABI, getter/throw order, receiver and saved-callee identity;
  receiver/operator mutations are observed. Three O0 fixtures publish; eleven
  other input/compiler cases assert their precise remaining reference refusal
  and are not counted as successful recovery. Recompiling the three published
  fixtures with the pinned es2abc at O0 and default gives **6/6 identical full
  disassemblies, 12/12 identical methods**, using grouping_bytecode.py helpers,
  without register/branch/handler/metadata normalization. New record outputs
  pass `node --check`.
- Both complete 40-input cohorts have **zero regressions and zero text drift**:
  previous flags **535 -> 537/620**, source contract **587 -> 589/620**. Only
  the two o18 records change status/diagnostics. HardKeyUtils remains complete
  and byte-identical. Largest child RSS is 43,204,608 bytes; the conservative
  four-worker bound is 172,818,432 bytes. Evidence is under
  `build/review/joint-selection/` (verified cohorts, comparison logs, exact
  budgets, SDK commands/hashes/disassembly, test logs).
- Required broad sweep: **248/268**, with 20 documented pre-existing failures.
  `phased_arm_selection_source_test` was independently reproduced on wtbase
  **1f4999b8** (`0 !== 2` at its existing assertion). The initial sweep also
  exposed one intentionally stale `selection_consumers_cli_test` expectation:
  its member fixture is exactly the newly supported Boolean conjunction. It
  now requires completion under both contracts, 22 additional differential
  scenarios and four detected receiver/operator mutations, preserving all
  earlier 42 differential executions and eight mutations. The final full
  sweep passes that test; `evaluation_failure_cli_test` likewise requires both
  new records complete and identical across manifest/CLI publication.
  No commit, push or stash was performed.

## Cross-region call values: joint normal selection intervals (2026-10-20)

- Baseline **041abdba**. Diagnosed all seven current member-reference refusals
  from original decoding, Value IR and fresh evaluation placements; exact
  IDs/regions, pool-resolved member names and remaining proof groups are in
  [cross-region-call-values.md](cross-region-call-values.md). These are saved
  member callees crossing argument-control regions, not one generic escaped
  argument-value problem. No new source-language premise is used.
- Add an unmet-reference-only fallback joining **already-verified normal
  selections** and their eager operand forests into one ordered interval.
  `LogicalReferenceRoute` preserves original phi/branch/use identities. Shared
  canonical-use indexes, path-compressed consumer roots, CSR membership and
  sole-entry corridor ranks avoid layout/OpId-order admission and repeated CFG
  walks. All buffers and retained certificate storage are prepaid. The reverse
  builder and independent forward verifier preserve single consumption,
  saved callee/receiver, conditional execution and caller-visible exceptions;
  existing CallPlan, TreeFlow, OriginalFlow and recovery checks remain required.
- **o21/16320 m17082 PC171 completes** (`onKelvinMonitor`): the two `mon.value()`
  calls stay distinct, each with its own retained snapshot, inside the original
  `hilog.info` argument. The other six targets' chains/PCs are unchanged. Joint
  predicate/keep-edge ownership (the o18 pair and the extra dependency in o39),
  constructor-wrapped literal arms, numeric-update result composition and the
  retained guarded-read case remain refused. No check was removed or retried.
- Fresh 40-input/module cohorts against built wtbase at **041abdba**:
  previous flags **534 -> 535 / 620**, V5 **586 -> 587 / 620**, **0 regressions,
  0 text changes** to previously complete records. Only o21/16320 changes its
  record manifest. All 40 engine runs exit 0 in each cohort; the export wrapper
  also counts non-module entries (1007 total), independently censused down to
  the 620 module records. Both new record outputs pass `node --check`.
- Full build passed. Final `source-regression -j 1`: **65/65**. New unit/real-ABC
  integration tests: **2/2**; existing logical/literal/snapshot foundations:
  **6/6**. Requested call/reference/optional/guard/temporary/tree/flow/recovery/
  CLI/receipt/manifest sweep: **181/195**, no newly introduced failures. Nine
  selected failures match the supplied known list. The five additional selected
  failures were reproduced after rebuilding baseline `derived_cells_test`:
  `unguarded_literal_closure_source_test`,
  `unguarded_derived_literal_receiver_source_test`,
  `unguarded_forwarded_receiver_source_test`,
  `unguarded_reversed_forwarded_receiver_source_test`, and
  `unguarded_reversed_initialization_forwarded_receiver_source_test`.
  Full names and baseline repros are retained in the scratch sweep logs;
  no failing test was weakened. `tree_failure.mjs` passes unchanged.
- Real DevEco O0/default ABC fixtures pass **3456 engine-output/original Node
  scenarios**, including throwing arms, getters/coercions, receiver/callee
  replacement, non-callable timing, ABI and optional nullish behavior. A wrong
  value-call mutation is observed; the retained joint-use negative fixture
  keeps `kNotInline`. Unit mutations cover route/call/consumer/arm identity,
  omitted/duplicated consumption and unowned effects. Register/copy/block-split
  variants pass. 200/201/400/800 width and call-count tests pass exact byte/work
  boundaries (800 calls: **68,695,690 admitted bytes / 20,153,586 work**;
  width800: **30,971,359 / 8,912,631**).
- SDK comparison uses `grouping_bytecode.py`'s unmodified strict assembly helper,
  pinned compiler SHA-256 `4d921b123d05...`. Default: **4/4 methods and whole
  metadata disassembly identical**. O0: **2/4 identical**, two optional methods
  have explained explicit private-snapshot initialization/stores and consequent
  temporary-register shifts; raw diffs are preserved, not normalized to passes.
  Source export-alias adaptation is documented and semantics-neutral.
- Scratch evidence: `build/review/cross-region-calls/` (`accepted-*`, `cmp-accepted-*`,
  `verification.json`, `new-tests-verified.log`, `sweep.log`, `baseline-extra-*`,
  `bytecode-verified/`). Final cohort largest child RSS **42,991,616 bytes**;
  four concurrent engine children conservatively bound at **171,966,464 bytes**.
  No commit, push or stash.

## Member class-inner bindings: o0/4660 published (2026-10-20)

- The real member-family pipeline now consumes the fresh sealed class-self
  occurrence/storage proof before child capture verification. Methods,
  accessors, static methods and nested/forwarding member closures get a typed
  `kClassInner` binding in a real class-inner scope. Original capture transport
  remains checked separately; source name/cell analysis no longer aliases it
  through the creator's post-initialization cache. Per-owner views share one
  root seal, use prepaid owner/exception indexes and have O((V+E) log V) total
  admitted work. Independent structure/flow/operand/guard/ABI/capture/recovery
  checks remain required.
- Fresh scope derivation and its independent verifier prove class creation's
  next must-reach ordered effect is the internal self Commit, before escape or
  invocation. Pinned runtime class creation installs function templates without
  invoking their bodies. Existing installation/cleanup/publication ports and
  immutable-storage checks remain intact. Native spelling is checked against
  the real class header and lexical scopes, including forwarding captures.
- **o0/4660 completes**, previously `kRecoveryGate / kFamilyEnclosure` in
  m4878 (read op3 PC6, guard op4 PC8). `reentry`, `accessors`, `completed_self`,
  and new nested/throwing member-closure fixtures publish. Two initializer-owned
  closure fixtures (`copies`, `throwing`) retain the more precise located
  `kRecoveryGate / kInitializerFamily` (projection10, m492 PC10 / m502 PC22).
  Their static-element emitter gap is not claimed as resolved; no engine-output
  differential exists for refused output. Published fixtures have35 ordinary
  Node differentials, including inherited setters, receiver/inner identity,
  re-entry and throws; all published outputs pass `node --check`.
- SDK target recompilation passes at default and O0 (24/24 methods matched in
  each). Exact bodies are0/24 and3/24 respectively; getInstance at O0 differs
  only by the dead zero load for `void 0`. Creator environment/debug names,
  regenerated function/literal identifiers, retained private materialization,
  prior predicate/control/register/async recovery account for the other exact
  differences. Full diffs and method-role explanations are retained, without
  normalization or an equality claim. Eleven fixture pairs compile in each
  mode: default41/45 and O0 30/45 method bodies identical. All22 O0 original/
  emitted raw scope probes run:20 pass and2 emitted probes retain the isolated
  unit adapter's `EvaluationError::kOwnership` gap. The default optimized
  cleanup probes remain unverified (`CleanupError::kHandler`). Production
  exporter + Node checks, raw scope/port checks and exact-bytecode comparisons
  are separate evidence layers.
- Full build passed; final serial source-regression **65/65**, focused class
  suite **18/18**, and broad sweep **227/241**, with exactly the14 documented
  pre-existing failures. Member scope/guard/owner/write/name/seal mutations,
  200/201/400/800 width and nested depth and exact/one-below budgets pass.
  Final logs are under `build/review/class-inner-members/`. Complete
  comparisons against wtbase **c5397500**: previous flags
  **533→534/620**, source **585→586/620**, no regressions, no complete-text
  changes. Only o0's manifest changes in each flag set. Peak measured engine
  RSS42,483,712 bytes on the final runs; the sum of the four largest child
  peaks is151,764,992 bytes (40-input harness has four workers). No commit,
  push or stash performed.
- Design and exact comparison index:
  [class-self-initialization-scope.md](class-self-initialization-scope.md#member-class-inner-consumption-2026-10-20).

## Completed-class availability: consumed continuation, member gate retained (2026-10-20)

- Added typed `CompletedClass` operand ports for verified classes without a
  class-valued public store. Fresh scope derivation and its independent verifier
  retain original class/UseId/operation identities and successful static-Commit
  dominance. Recovery rechecks the current source read, consuming action and
  exception edge. The saved member Reference supplies its evaluated base once
  for both the property input and the suppressed original call receiver.
  Public-store certificates keep their prior inventory; no export of a method
  result is passed off as a class publication. New indexes are shared/prepaid,
  with typed work/resource failures and O((V+E) log V) admitted analysis.
- o0/4660 advances from `kRecoveryGate / kClassInitializationPorts`
  (m5040 PC12) to **`kRecoveryGate / kFamilyEnclosure` (m4878 op4 PC8)**.
  This is getInstance's guarded self-cell read (op3 PC6). Its owner is in the
  certificate, but the emitted member has no class-inner scope projection yet.
  Resolving it through the creator's later cache would break re-entrant reads
  during static initialization, so it remains refused. **No o0 text is
  published; target SDK recompilation/Node acceptance is not claimed.**
- Two private completed-class fixtures publish and pass Node syntax plus
  original-vs-engine-output differentials (identity/receiver and throwing call).
  The requested `export class S ... return S.inst` fixture asserts the member
  gate; the early-exposure fixture asserts a typed syntax/name gate. Scope and
  recovery mutations cover lost/wrong ports/value/use, early cache/read/action,
  and changed exception routing. 200/201/400/800 widths pass exact work/byte
  limits and one-below resource refusal. The negative exported-self fixture's
  unit mode checks certificates; its complete production gate is asserted by
  the exporter test, since the synthetic enclosure adapter has no module loads.
- Full build passed. Serial source-regression **65/65**; focused class tests
  **16/16**; call-reference/spread/guarded-call checks **8/8**;
  requested broader sweep **225/239**, with exactly the 14 documented
  pre-existing failures and no new failure. The initial new test-adapter failure
  and the corrected final runs are both retained under
  `build/review/completed-class/`.
- Fresh OLD/NEW 40-input cohorts against wtbase **5a1dbcaf**: **533/620**
  previous flags and **585/620** source, unchanged complete sets, zero process
  failures, zero text drift. The only changed file in either export is
  `o0/manifest.json` (only row 4660). Final measured engine RSS peaks are below
  44 MB; the conservative sum of the four largest per-input peaks stays below
  160 MB for each flag set.
- SDK comparisons reuse grouping_bytecode.py's pinned compiler hash/options
  and retain unnormalized disassembly diffs. New fixtures: default **8/8**
  method bodies identical; O0 **6/8**, with the two creator differences being
  private materialization copies/register placement. Physical table/string
  offsets differ. All four O0 original/emitted raw port probes pass; the four
  default optimized cleanup probes remain explicitly unverified (kHandler),
  not counted as proof. No commit, push or stash.
- Diagnosis, obligations and remaining per-member projection:
  [class-self-initialization-scope.md](class-self-initialization-scope.md#completed-class-continuation-2026-10-20).

## Generic parameter-initializer expressions: o20 publishes (2026-10-20)

- Added a sealed conditional expression-default proof and independent raw,
  syntax and recovery verifiers. Original argument/undefined-test/initializer/
  conditional commit identities and each caller exception remain separate.
  Live module/capture reads and their HoleGuards remain binding reads, including
  getter order and TDZ. Later parameters and body-local dependencies refuse.
  Native signature length is verified against ABC metadata; no ABI check is
  weakened and no new source premise is introduced.
- Existing literal/captured/cell-default projections retain priority and text.
  The new path uses shared region/phi indexes and explicit stacks, with prepaid
  retained input/certificate/names/flow buffers and typed resource/work failures.
  200/201/400/800 raw proof costs are linear; exact limits and one-below failure,
  block splitting, wrong ownership/order/commit/guard/catch and recovery mutations
  are tested. Unsupported initializer event protocols remain typed refusals.
- **o20/8166 IndicatorBarAttribute now publishes** with constructor defaults
  `SizeMode.Normal, 0, 0, -1, 1, VerticalAlign.Center`, `.length === 1`.
  The deferred initializer's module aliases are independently rebound through
  the actual record enclosure, preserving their real slots/names/TDZ domains.
  o0/4660 is unchanged at `kClassInitializationPorts` (m5040 PC12): getInstance
  needs a distinct completed-class availability port, not a fabricated export.
- Fresh OLD/NEW 40-input cohorts against wtbase **024e759c**: **532→533/620**
  previous flags and **584→585/620** source; zero regressions and zero text drift
  for every previously complete record. Only o20/8166's manifest row changes.
  Node syntax passes for its new output. Six generic fixtures also publish and
  pass independent original-vs-engine-output Node behavior tests; later-parameter
  defaults refuse. The static-self constructor fixture checks native length,
  construction, getter order and explicit provided arguments.
- Pinned SDK/compiler identity, exact commands and full disassembly diffs are
  retained. Twelve generic fixture recompilations: **6 identical / 6 different**
  full disassemblies; **24/30 identical method bodies**. Differences are physical
  pool offsets, private copy moves and the initialized creator capture's inert
  source-let guard. o20: all **15/15** methods are paired by source member role
  or original enclosing definefunc relation; **11/15 O0 bodies identical**,
  including m8638, all eight accessors and both callbacks. Remaining bodies and
  default-optimized output are recorded as different, with semantic explanations
  in the design note; no register/branch/exception normalization is used.
- Full build passes. Final serial source-regression **65/65**; focused tests
  **4/4**; requested broad sweep **221/235**. Its 14 failures exactly match
  the supplied pre-existing set (family_source_cli, tree_structure, try_tree
  and source, derived_cells and source/unguarded, class_tree_source and the
  six captured_receiver variants). An earlier run correctly rejected producer
  hashes changed by concurrent relinking; it is invalid acceptance evidence.
  The final runs are quiescent, with regenerated receipts and unchanged producer
  hashes after the test-registration-only CMake update. No check was weakened.
- Final 80 engine processes all exit zero. Observed peak RSS is **42,385,408
  bytes** previous flags / **41,598,976 bytes** source. Summing the four largest
  engine peaks gives conservative concurrent-engine bounds **145,080,320 /
  151,814,144 bytes**; each engine includes its internal workers.
  Full logs, exact SDK diffs, manifest changes and machine-readable acceptance
  are retained under `build/review/parameter-expressions/`. Design:
  [parameter-initializer-expressions.md](parameter-initializer-expressions.md).
  No commit, push or stash.

## Class-self increment 6: effectful member defaults diagnosed; gates retained (2026-10-20)

- **No new completion.** o20/8166 remains `kRecoveryGate / kMemberAbi`,
  constructor m8638, declared `.length = 1` / recovered `.length = 7`.
  Its seven visible parameters are one required parameter followed by defaults
  `SizeMode.Normal`, `0`, `0`, `-1`, `1`, `VerticalAlign.Center`.
  The first fallback is a live guarded **module** read plus property access,
  not a direct captured-cell default. No rest/destructuring/arguments-tail gap
  was found. The final default performs a global lookup and property access.
- Production raw-IR probing identifies the first discovery refusal: the arm has
  module-read/guard/property-read events and exceeds the constant/negation arm
  representation in `default_candidates.cc`. Later constant defaults are
  intentionally not projected past that uncertified boundary. Increasing the
  limit would not suffice: signature construction/verification and ABI admit
  only literals or proven capture references, and parameter recovery lacks a
  certificate for these effectful expression events and their caller exits.
  The design checkpoint records the generic parameter-expression extension
  required across those layers. No ABI or ownership check was relaxed.
- Added a real pinned-es2abc fixture and production-graph/source regression
  asserting the honest **1/7** member-ABI gate; forged property-as-constant
  initializers are rejected even with falsified pure effects. All changes are
  tests/build registration/documentation; production behavior is unchanged.
- o0/4660 remains `kRecoveryGate / kClassInitializationPorts` at m5040 PC12.
  Its creator m4850 exports the result of `getInstance()`, not the class; a
  distinct completed-class availability correspondence is still needed. No
  publication port is fabricated from that export. Neither target publishes,
  so there is no target engine-output Node differential or target bytecode
  equivalence result in this round. `tree_failure.mjs` remains correct.
- Both fresh 40-input cohorts against wtbase **024e759c** have no process
  failures: **532/620** previous flags and **584/620** source. Source and
  manifests are all byte-identical (3,653 / 3,809 files including 40 manifests).
  There are no regressions, new completions, or diagnostic changes.
- Full build passes. Serial source-regression **65/65**; focused new/boundary
  tests **3/3**; class-self fixtures **12/12** within the requested sweep. The
  sweep is **219/233**, with exactly the 14 documented pre-existing failures
  (no additions or omissions). Existing fixture Node differentials and
  round-trip tests pass; the new fixture source also passes `node --check`.
  `git diff --check` passes. No commit, push or stash.
  Evidence: `build/review/class-self6/`; details and next proof obligations in
  [class-self-initialization-scope.md](class-self-initialization-scope.md).

## Class-self increment 5: initialization ports consumed; constructor ABI gate (2026-10-20)

- **o20/8166 is still incomplete.** Its m8556 initializer now passes the common
  scope/semantic path with the real outer captures. Finishing the class next
  refuses constructor **m8638**: original `.length = 1`, recovered signature
  `.length = 7`. This is a located `ClassSelfError::kRecoveryGate /
  Projection::kMemberAbi` with both lengths, not a relaxed ABI check. Its native
  default-parameter recovery is the next obligation. o0/4660 remains at m5040
  PC12's initialization/completed-class-availability gate. No target source is
  published and no target recompilation equivalence is claimed.
- A sealed, independently re-derived `ClassInnerScopeProof` now admits the real
  immutable inner scope through structure, TreeFlow, cell and recovery checks.
  OriginalFlow/RecoveryFlow retain distinct internal initialization, static
  Before/Commit/exception and public publication ports. The allocation's intrinsic
  success stays before static initialization; it cannot use the whole ClassExpr
  completion. Every recovery proposal rechecks the original roles, cleanup,
  exception destination, physical public writes and Commit dominance.
- Private source caches remain separate materializations: actual original stored
  values and private units are checked, and any retained cache definition must
  follow static Commit. A shared operation/definition index avoids per-class CFG
  scans. Public read timing uses shared dominance/effect and binding indexes.
  Buffers are admitted before allocation, with exact byte/work and one-below
  tests at **200/201/400/800**. No new source premise is introduced.
- Four real fixtures publish: self, outer, branches and constructor_throw. Their
  actual engine output passes Node syntax and **16** ordinary-call/descriptor/
  exception-trace differentials, including inherited prototype setters. Native
  own fields and ordinary static Set remain distinct. Reentry/accessors/copies/
  closure-bearing throwing cases keep `kFamilyEnclosure`; outer_tdz keeps
  `kCaptureEnclosure`. Member/descendant inner-name projection is still missing,
  so no post-class creator cache is used to fake static re-entry semantics.
- SDK es2abc SHA-256 `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`;
  matching records, `--module --merge-abc --extension=js`, default and O0.
  Four-fixture exact full-disassembly comparison is **1 identical / 7 different**;
  per-method **18 identical / 6 different**. No normalization is applied.
  Differences are private result aliases/register allocation, physical pool
  offsets and the outer binding's recovered HoleGuard diagnostic name. All
  constructor/initializer bodies otherwise retain their operations and cleanup;
  the design checkpoint explains each difference. O0 is also checked against
  the actual fixture ABC methods and independently re-verifies all **8/8** raw
  scope/cleanup/port proofs. Default optimized cleanup proofs remain **8
  unverified**, with existing `CleanupError::kHandler` (1); they are not passes.
- Full build passes; serial source-regression **65/65**, focused suite **12/12**.
  Requested broad sweep is **209/223**; its 14 failures exactly match the
  documented pre-existing set (family_source_cli, tree_structure, try_tree and
  source, derived_cells and source/unguarded, class_tree_source, six captured
  receiver variants). No new test failure or failing expectation was hidden.
  Fresh OLD/NEW cohorts against **1a7da988** retain **532/620 / 584/620**, no
  regressions, identical complete sets and all **3,613 / 3,769** non-manifest
  files byte-identical. Only o20/8166's diagnostic changes in either flag set.
  Evidence: `build/review/class-self5/`; checkpoint:
  [class-self-initialization-scope.md](class-self-initialization-scope.md).
  Measured process peak RSS across all 80 final cohort invocations is
  **42,598,400 bytes** (previous flags) / **42,106,880 bytes** (source), sampled
  with Darwin RUSAGE_CHILDREN in a fresh wrapper per engine process; every
  engine exits zero. Detached printing now rejects ordinary initializer captures
  without their creator enclosure and cannot choose an anonymous class for
  certified inner references. No commit, push or stash.

## Class-self increment 4: actual creator captures bound; publication still gated (2026-10-20)

- **Partial delivery: o20/8166 does not publish.** Its two ordinary initializer
  requirements now bind through m8392's actual CaptureTree, with real environment/
  storage identity, final creator names and unchanged TDZ domains. Aliases are
  lexical references, never copied values or fabricated initialized captures.
  Independent re-verification repeats the original capture binding and checks
  occurrence, storage, names, scopes, separate ports and cleanup correspondence.
- The sealed raw preparation survives until the existing two-phase creator
  scheduling supplies the actual source enclosure. Certified private class
  installation effects coexist with the original self store; no unrelated
  mixed-class shape is admitted. The real class task allocates the initializer
  syntax owner. Other successful preparations and Closed refusals are unchanged.
- o20 advances from `kRecoveryGate / kCaptureEnclosure` at **m8556 PC13** to
  `kRecoveryGate / kClassInitializationPorts` at **m8392 op100 PC261**, with
  `bound_outer: 2`. The common initializer verifier still rejects class-inner
  scope; joint OriginalFlow/RecoveryFlow for the private self Commit, initializer
  Before/Commit/exception and public Commit/TDZ is not implemented. It is not
  sound to identify these with the whole ClassExpr Commit. No gate is lifted.
  o0/4660 and its completed-class availability/getInstance obligation are unchanged.
- Eight real es2abc fixtures assert their exact publication refusal. `outer`
  now binds and reaches the ports gate; new `outer_tdz` keeps capture enclosure
  because native declaration-at-store fulfilment is unproved. Mutations cover
  wrong target/slot/domain/name, shadowing, cleanup and ports. Bound evidence
  round-trips through receipts and both JSON paths. Self-only and outer-capture
  widths **200/201/400/800** check exact work/minimum-byte admission and one-below
  refusal. Independent enclosure byte boundaries are **2,450,621 / 2,462,314 /
  4,830,181 / 9,589,301**, work **1,093,601 / 1,100,998 / 2,174,665 / 4,339,193**.
- Fresh OLD/NEW cohorts against **55aab71f** retain **532/620 / 584/620**, identical
  complete sets and every non-manifest byte (3,613 / 3,769 files). Only o20/8166's
  manifest changes; abc_semantic reports the same gate. No newly published class
  exists, so no engine-output Node or es2abc/ark_disasm equivalence is claimed.
  Design checkpoint: [class-self-initialization-scope.md](class-self-initialization-scope.md).
  Scratch evidence: `build/review/class-self4/`. No commit/push/stash.
- Full build passes. Final isolated serial source-regression is **65/65**,
  focused tests **10/10**, requested sweep **207/221**. All 14 sweep failures
  reproduce with rebuilt **55aab71f** baseline executables: family_source_cli,
  tree_structure, try_tree (+source), derived_cells (+source/unguarded),
  class_tree_source and the six captured-receiver variants. No failing semantic
  expectation was relaxed. The added bound-capture receipt test supplies its real
  working revision, and scale invocations use separate scratch directories;
  final acceptance runs execute sequentially to avoid shared-fixture races.

## Class-self increment 3: consumed initializer scope, explicit recovery gates (2026-10-20)

- Consumes `ClassSelfScopeProof` through the common FunctionTree builder on the
  certified failure path. Self reads now become immutable `kClassInner` symbols
  in a real class-inner source scope; original guard provenance stays attached.
  Ordinary outer cells remain typed capture requirements with their real
  addresses/domains. They are not fictional function captures or initialized
  aliases. Existing successful Closed paths stay unchanged.
- Before creator freeze, root-family demands merge into the real creator storage;
  prepared full owner ranges supply actual syntax owners. ClassSelfOrder binds
  the original internal self store, and ClassTree keeps separate internal/public
  ports plus original initializer dispatch/cleanup. Independent verification
  recomputes scope/storage, owner ranges, evaluation/bindings, lexical resolution,
  ports, cleanup and exactly-once read/guard correspondence. Ordinary printing
  and recovery reject these drafts; none is a ConstructedFunction.
- **m8556 PC13 now passes the internal-value leak:** two real class-inner tree
  references, ten allocated owners. o20/8166's first cause is
  `ClassSelfError::kRecoveryGate / Projection::kCaptureEnclosure`, because the two
  ordinary outer requirements still need the actual creator CaptureTree binding
  (including final spellings and TDZ domains). Demand forwarding alone does not
  establish it. Native class initialization/public-binding/cleanup event ports
  must then pass joint OriginalFlow/recovery on every projection before printing.
- o0/4660 advances to `kRecoveryGate / kClassInitializationPorts` at m5040 PC12:
  one initializer-tree self reference, 23 owners, five certified raw family reads.
  Additional member-family enclosure and the separate completed-class
  availability/getInstance result remain unresolved. No class-valued publication
  is fabricated from the getInstance export. **Neither record publishes yet.**
- Seven real es2abc fixtures assert the gate through the production record
  exporter as well as draft construction: outer capture enclosure for `outer`,
  class-initialization recovery ports for self/throwing/reentry/branches/copies/
  accessors. Mutations cover wrong scope/storage, mutable self, lost guard origin,
  a forged public port, owner collision and missing cleanup; receipt and both JSON
  paths round-trip the new gate. Exact 200/201/400/800 draft admission boundaries
  are **2,347,215 / 2,358,103 / 4,606,735 / 9,125,775 bytes**, work
  **1,445,501 / 1,452,838 / 2,864,716 / 5,708,978**; one-below refuses.
- Fresh OLD/NEW runs at baseline **9d77c21b** retain **532/620 previous flags /
  584/620 language-source**, identical complete sets and every non-manifest byte
  (3,613 / 3,769 files). Only o0/4660 and o20/8166 manifest causes/evidence change;
  abc_semantic agrees. No new engine-output Node differential or es2abc-equivalence
  result is claimed while publication is closed.
- Full build passes; focused tests **9/9**, serial source-regression **65/65**.
  The requested sweep is **87/95**: its eight failures all reproduce at
  **9d77c21b** (family_source_cli, class_tree_source and the six documented
  captured-receiver source variants). No expectations were weakened. The new
  exporter test uses a fresh temporary output directory per run, respecting the
  CLI's refusal to overwrite an existing export directory. Design/remaining gates:
  [class-self-initialization-scope.md](class-self-initialization-scope.md).
  Scratch logs, exact comparisons, fixture exports, budgets and baseline failure
  reproductions: `build/review/class-self3/`. No commit/push/stash.

## Class-self increment 2 continuation: verified scope view only (2026-10-20)

- **Partial delivery; the requested recovery gate is not reached.** Added
  `ClassSelfScopeProof` and independent fresh-input verification. The view gives
  occurrence/storage-bound `ClassInnerBinding` references with retained guards,
  full relative owner ranges, separate self/outer requirements, internal versus
  explicit public-store ports, original initializer dispatch and cleanup entries.
  Plan-local CellIds never establish cross-owner correspondence. Unlisted/stale
  owners cannot resolve a reference. It is not yet a TreeCapture/ClassExpr scope.
- Both real methods verify the view: m4850 has **23 owners / 5 references /
  2 inner + 2 outer requirements / 0 direct class-valued public stores**;
  m8392 has **10 / 2 / 1 + 2 / 1**. m4850's export receives getInstance's result;
  its retained class result still needs a distinct completed-class availability
  port. Both targets retain **kInternalValue > kMixed**, at m5040 PC12 and
  m8556 PC13. No recovery gate, source admission or publication is claimed.
- The accessor-before-initializer order is justified under a private must-reach
  installation corridor, fresh home identity and resolved constant keys. The
  pinned runtime defines accessor descriptors without invoking their functions;
  the frontend emits class-name initialization, missing properties, then static
  initialization. New `accessors.js/.abc` exercises the combination. Existing
  ClassTree mixed-definition/initializer and ClassSelfOrder/Closed restrictions
  remain intact until source preparation consumes the evidence. Details and
  runtime/frontend citations: [class-self-initialization-scope.md](class-self-initialization-scope.md).
- All new buffers are prepaid; shared per-input effect/constant/prototype indexes,
  sorted occurrence/address joins and indexed reference lookup avoid per-read CFG
  walks. Exact 200/201/400/800 scope-view admission boundaries are **99,150 /
  99,468 / 183,230 / 351,390 bytes**, with work **60,194 / 60,474 / 116,482 /
  229,858**. One-below memory/work and verifier-work refusals pass. Mutation
  coverage includes unlisted owners, wrong storage, weakened outer identity,
  missing requirements/installations, forged commit ports, a creator read moved
  before completion, broken cleanup and exhausted budgets.
- Full build passes; serial source-regression **65/65**; focused tests **8/8**.
  Requested class/static/initializer/family/capture/receipt/manifest sweep:
  **86/94**. Its eight failures all reproduce at **19b5b561**: family_source_cli,
  class_tree_source, and the six documented captured-receiver source variants.
  No expectations changed. Seven real es2abc fixtures cover the raw certificate
  and scope view; they still assert the Closed refusal, **not** kRecoveryGate.
  No new engine-output differential is claimed while publication stays closed.
- Fresh OLD/NEW exports using `eval_cohort.py` and `cmp_cohort.py` retain
  **532/620 previous flags / 584/620 source**, identical complete sets and all
  non-manifest bytes (3,653 / 3,809 files). Only **o0/4660 and o20/8166** gain
  nested `scope_view` diagnostics on both JSON paths and abc_semantic; every first
  cause stays unchanged. Receipt/JSON positive and negative round trips pass.
  Scratch evidence: `build/review/class-self2/`, including exact whole-directory
  comparison, target probes, disassembly, budgets and baseline reproductions.
- Remaining increment 2: consume the view in real initializer-family binding,
  merge requirements into creator storage before freeze, allocate actual syntax
  owner ranges, prove class-inner spelling/enclosure, and project the member/
  initializer/cleanup events through ClassSelfOrder and ClassTree. Only then may
  a truthful pre-publication recovery gate replace kMixed. No commit/push/stash.

## Class-self initialization: raw proof delivered; scope preparation unfinished (2026-10-20)

- **Partial delivery of the requested increments 1–2: increment 1 only.** Added
  occurrence/storage-bound `ClassSelfInitializationProof`, independent re-derivation,
  complete member/descendant capture and writer coverage, retained guards and
  exception exits, original initializer dispatch and cleanup pop/rethrow evidence.
  The shared creator-occurrence type is extracted without changing its identity.
  `ResolveClassInitializerContext` reuses the Closed reader's occurrence checks;
  the existing Closed reader, ClassSelfOrder and all successful plans are retained.
- Failure-only diagnostics verify both targets: **o0/4660 m4850 -> m5040**
  (18 family roots, 5 self reads, 4 incoming-cell records, 1 cleanup) and
  **o20/8166 m8392 -> m8556** (10 roots, 2 self reads, 3 incoming-cell records,
  1 cleanup). Both JSON paths and abc_semantic expose the raw certificate status.
  First causes remain `SemanticProofError::kInternalValue > InternalValueReason::kMixed`
  at **m5040 PC12 / m8556 PC13**. **No recovery gate or publication is claimed.**
- Increment 2 still needs real class-inner capture targets/scope, non-self demand
  forwarding before creator freeze, full owner ranges, and internal-vs-public
  commit/unwind ports through the source tree. m8392's eight accessor methods
  at PCs74–235 coexist with its PC261 initializer; current ClassTree still rejects
  combined definitions/initializer placement. Raw family preparation is write
  evidence only and does not waive that check. The exact boundary is recorded in
  [class-self-initialization-scope.md](class-self-initialization-scope.md).
- Full build passes; serial source-regression **65/65**; new tests **8/8**.
  Six real es2abc fixtures cover self construction, static-block outer access,
  read-only reentry, throwing initialization, branches and copies. Mutations cover
  storage/occurrence, guard/exception, ordering, omitted/extra roots and descendants,
  plus a valid decoded-body self-write. Original ABC files are unchanged.
  Exact 200/201/400/800 byte boundaries are **92,796 / 93,030 / 160,076 / 294,636**;
  work boundaries **56,513 / 56,782 / 110,601 / 219,577**; one-below refusals pass.
  Engine-output differential/recompile acceptance remains gated with increment 2/3.
- Requested CLI/class/static/initializer/family/capture sweep: **150/158**.
  All eight failures reproduce with rebuilt wtbase **1d8326e8**:
  `family_source_cli_test`, `class_tree_source_test`, `captured_receiver_source_test`,
  `unguarded_captured_receiver_source_test`, `reversed_captured_receiver_source_test`,
  `unguarded_reversed_captured_receiver_source_test`,
  `reversed_initialization_captured_receiver_source_test`,
  `unguarded_reversed_initialization_captured_receiver_source_test`.
  No expectations were weakened or changed.
- Both fresh 40-input cohorts match wtbase in complete sets/statuses and all
  non-manifest bytes: **532/620 previous flags (3,653 files)** and **584/620 source
  (3,809 files)**. Only the two target manifests gain raw diagnostic fields;
  all first causes remain unchanged. CLI peak RSS over all 40 inputs:
  **41,631,744 bytes previous / 41,697,280 bytes source**. No cap/contract/ISA
  ledger change. Logs, exact comparisons and baseline reproductions:
  `build/review/class-self/`. No commit, push or stash.

## Next eligible track: class-self scope design only (2026-10-20)

- At HEAD / built wtbase **1d8326e8**, no additional small, premise-free
  implementation was established outside the parked/previously ruled-out set.
  Chose the largest eligible shared L track: **o0/4660 and o20/8166**, class-self
  initialization scope, upper bound two. The five-line choice, fresh candidate
  evidence, concrete joint proof/types, three gated increments and planned tests
  are in [class-self-initialization-scope.md](class-self-initialization-scope.md).
  No production code, tests, fixtures, contract, ISA ledger or admission changed.
- Fresh probes: m5186's loop0/header block7 has distinct exits39 and33 (39 then
  reaches33); the missing obligation is a common exit frontier preserving original
  edges/copies, not permission to replace an exit ID. m16784 joins CapturedDepth0
  with allocated EnvironmentId0 at original PC275 / working PC272; equal numeric
  IDs do not prove storage identity. m37881 reaches the normal try-exit/pending
  follow check at control_verify.cc:147. m58418's PC18 closure prelude still
  precedes the Value IR/environment inputs required by the rest certificate.
  These are not adapters to existing sealed facts. Parked paths remain untouched.
- The class-self design retains Round76's **self store before initializer call**
  evidence. It adds no claim that the outer publication binding is initialized.
  Proposed proof joins readonly transitive storage, guard, native class-inner
  name/scope, initializer-family captures, internal-vs-public commits and cleanup
  unwind; no permissive Closed-reader switch or fake FunctionSelf/capture owner.
  Existing refusal remains until independent tree/original/recovery checks close.
- Fresh 40-input exports match wtbase byte-for-byte: **3,653 files, 532/620**
  previous flags; **3,809 files, 584/620** source. Thus **36** source records
  remain incomplete (35 refused, one partial), rather than the request's 37.
  Complete/incomplete sets, first causes, text, positions and manifests unchanged.
- Full build passes; existing serial source-regression **65/65**. Existing
  CLI/corpus/failure/receipt/manifest sweep **78/79**; only
  `family_source_cli_test` fails (owner/origin arena at CMake line77), reproduced
  using rebuilt **1d8326e8** abc_semantic. No expectations were changed.
  New proof, mutation, budget and Node tests are **planned, not implemented or
  claimed passing**. Scratch probes, exact output comparison and baseline evidence
  are in `build/review/next-choice/`. No commit, push or stash.

## Decision predicate literal commits (2026-10-20, uncommitted)

- Chose the smallest fresh nonparked gap: **o2/10579 m11217**, an independently
  verified literal's PropertyId commit inside a compound selection predicate.
  The five-line choice, quoted ABC-only IR, rejected alternative and proof are
  in [decision-literal-commits.md](decision-literal-commits.md).
- TreeFlow's decision operand walk omitted the kInitializer/PropertyId case;
  op46 PC117 fell through to kPlacement despite the existing literal proof.
  It now checks the exact retained initializer and original commit origin.
  Unique placement, allocation membership, key/value, order, exceptions and
  recovery remain independently checked. No builder/evaluation admission,
  printer, source contract or parked protocol change; no new buffer or scan.
- **o2/10579 completes under both flag sets**. Fresh cohorts vs rebuilt wtbase
  **80515095**: **531 -> 532/620** previous, **583 -> 584/620** source.
  Only that record's manifest changes; three new source/position files per mode.
  All existing source, positions and other files stay byte-identical. Both new
  outputs pass `node --check`; no parked record's diagnostics change.
- Full build passes; serial source-regression **65/65**; new tests **2/2**;
  focused tree/decision/logical/literal/recovery/guard tests **13/13**.
  Real es2abc fixture + engine-output Node differential: **288 scenarios / 12
  observed mutations**, covering short-circuiting, getters/throws, receiver,
  callee replacement, nested literals and an inherited setter that must not run.
  Unit mutations reject missing/double/wrong/reordered property correspondence
  and wrong execution edge/disposition; copies and block-split prefixes pass.
- Exact TreeFlow budgets at 200/201/400/800 predicates are bytes
  **1983817/1993697/3959817/7911817**, work
  **879495/884187/1762965/3538277**. One below refuses. Full source cohort maximum
  reported CLI peak RSS **39,436,288 bytes**; no cap changed.
- Sweep **145/154**. All nine failures reproduce on freshly rebuilt **80515095**:
  `family_source_cli_test`, `literal_construct_test`, `mixed_literal_test`,
  `phased_literal_test`, `literal_closure_source_test`,
  `unguarded_literal_closure_source_test`, `derived_literal_receiver_source_test`,
  `unguarded_derived_literal_receiver_source_test`, `decision_snapshots_source_test`.
  No stale expectations were changed. Logs, per-test baseline commands, complete
  file comparison, red/green tests and RSS report: `build/review/next-slice/`.
  `git diff --check` passes. No commit, push or stash.

## Guarded reference phi arms (2026-10-20, uncommitted)

- ABC-only diagnosis distinguishes the two targets; full quoted IR and proof are
  in [guarded-call-references.md](guarded-call-references.md). o5/28303's Use257
  reads op33's retained result v82, not the guarded TimeUtils read. Its earlier
  read/guard ancestor is propagated into the diagnostic. A throwing selection
  feeding a nonthrowing copy still lacks a transitive exception summary; that
  phased path and its module-atom exclusion are unchanged and parked.
- Implemented the smaller o5/49599 gap: normal evaluation already owns verified
  cell-guard dependencies, while phi-use/tail indexing counted them as foreign.
  The additional view is seeded only by an original unmet member Reference and
  otherwise rejected phi inputs. Fresh cell/storage identities, unique guard,
  raw predecessor-event flow, guard dominance before the exact phi edge, caller
  exception correspondence and the existing all-path inert tail are re-proved.
  No foreign event/copy/divergence is crossed. The independent evaluation
  verifier reconstructs the view before unchanged selection/TreeFlow/recovery
  checks. Guards, source TDZ ownership and catch-state refusals remain; no new
  premise or initialized-cell fact. No printer or phased-composition change.
- **o5/49599 m50922 PC297 completes under both flag sets**. o5/28303 m28799
  remains unchanged: source PC620/op231 `kNotInline > kRoot >
  kCrossRegionValue > GuardedCallError::kUse`, read20 PC56/guard21 PC58/Use257;
  previous flags keep the original kMultipleExits at PC1146.
- Full build passes; serial source-regression **65/65**; new tests **2/2**;
  focused guard/phi/module/phase tests **9/9**. Real es2abc fixtures yield
  **48 Node differential scenarios / 15 observed mutations**, covering captured
  let/const arguments and callees, early TDZ, initialized imported receiver,
  getter/throw/order/receiver/callee replacement and live cell writes. The early
  capture fixture keeps its existing ScopeError under previous flags; it is not
  counted as a differential pass there. See the fixture README for compiler
  identity/commands/hashes and the unclaimed cyclic-module boundary.
- 200/201/400/800 exact build bytes:
  **3517953/3535517/7030753/14056353**; exact work:
  **1531892/1539700/3083728/6209397**. One byte/work unit below refuses.
  Mutation oracles also reject wrong/missing consumption, misplaced/dropped
  guard ownership, wrong exception correspondence, duplicate guard evidence and
  foreign events. Canonical copies and block-split phi transport are exercised.
- Both 40-input cohorts against rebuilt/current **0d9a1707**: **530 -> 531/620**
  previous flags, **582 -> 583/620** source. Only o5/49599 changes in manifests;
  all existing files except o5's manifest are byte-identical, including every
  previously complete source and position map. Three newly published files per
  mode; both new sources pass `node --check`. Source cohort maximum CLI peak RSS
  is **39,845,888 bytes**; no cap changed.
- Required sweep **136/149**. All thirteen failures reproduce with rebuilt
  baseline **0d9a1707** executables: `family_source_cli_test` (owner/origin arena),
  `unguarded_derived_cells_source_test`, `unguarded_literal_closure_source_test`,
  `unguarded_derived_literal_receiver_source_test`, `captured_receiver_source_test`,
  `unguarded_captured_receiver_source_test`, `unguarded_forwarded_receiver_source_test`,
  `reversed_captured_receiver_source_test`, `unguarded_reversed_captured_receiver_source_test`,
  `unguarded_reversed_forwarded_receiver_source_test`,
  `reversed_initialization_captured_receiver_source_test`,
  `unguarded_reversed_initialization_captured_receiver_source_test`, and
  `unguarded_reversed_initialization_forwarded_receiver_source_test`.
  No stale expectations or behavioral checks changed. Logs, per-test baseline
  evidence and cohort comparison are in `build/review/guarded-references/`.
  `git diff --check` passes. No commit, push or stash.

## Deprecated ISA evidence: all 47 variants (2026-10-20, uncommitted)

- Evidence-only slice based on **41f6b2a1** (HEAD and rebuilt wtbase).
  No production code, semantic policy, opcode admission or RuntimeEvidence
  changes. All 47 have real C++/deprecated dispatch and assembly implementations;
  46 have baseline handlers. AOT metadata/lowering gaps are recorded separately;
  none has an all-path NonExecutable proof. Full per-variant file:line/hash
  citations and known ID32/class-literal path differences are in
  [isa-deprecated-evidence.md](isa-deprecated-evidence.md).
- The test-only two-pass ABC writer gains an explicit, closed profile choice;
  its default 12.0.6 output is preserved. **282 ABCs** (47 x six profiles) are
  checked against SDK `ark_disasm`, production `abc_decode` and the per-op
  coverage probe. Profiles 9/11.0.2 decode and retain UnsupportedProfile;
  each modern profile has **11 raw successes / 36 UnsupportedInstruction**
  refusals. Raw roles/effects/commit/exception, record status and available causes are retained.
- New CTest `isa_deprecated_evidence_test` has four Python cases: full catalog/
  profile fixtures, real corpus decoder, stale/missing/width/prefix/support
  mutations, exact 200/201/400/800 byte boundaries, raw-lift scaling and profile
  layout. Together with `isa_coverage_test`: **2/2**. Independent review reran
  the four cases successfully, with no important findings. No VM execution,
  historical frontend emission, high-ID path equivalence or full class/
  generator/module protocol execution is claimed; oracle columns stay empty.
- Regenerated matrix: **712 ABCs / 1,312,780 instructions**, zero probe failures.
  Fresh before -> after: **145 -> 155 SupportedUnverified**, **159 -> 149
  Unmeasured**, **28 NonExecutable unchanged**; other classes including
  SupportedVerified remain zero. Ten deprecated scalar/environment variants
  satisfy the existing limited raw-lift + complete-record rule; the other
  37 remain Unmeasured. All 47 still have zero exploration-cohort occurrences
  and zero current first-refusal impact. JSON/JSONL stays untracked in build/.
- Full build and serial source-regression **65/65** pass. Both 40-input cohorts
  remain **530/620** previous flags and **582/620** source. **All 3,647 / 3,803
  files are byte-identical** to rebuilt wtbase, including manifests, partial
  output and position maps. Evidence is in `build/review/isa-deprecated/`.
- Required `isa|cli|corpus|failure|_receipt|manifest|decode` sweep: **89/90**
  (all **8 ISA tests pass**). Its only failure is the documented
  `family_source_cli_test` owner/origin-arena assertion, reproduced directly
  with rebuilt **41f6b2a1** `abc_semantic`. No stale expectations or behavioral
  assertions were changed. Builder byte boundaries for 200/201/400/800 repeated
  instructions are **57536/57798/109936/214736**; one byte below each refuses.
  `git diff --check` passes. No commit, push or stash.

## Cell lifetimes over verified for-of (2026-10-20, uncommitted)

- Fixed o5/28303 m28799's `CellLifetimeError::kControl`: the lifetime checker
  now threads and independently re-verifies ProtocolOwned ownership plus the
  existing ForOfEmissionView. Native done edges retain the original LoopId
  scope instead of adding a lexical arm. Close units retain certified
  completion ports, without a source scope or transparent-handler treatment.
  Optional block mappings require every cell allocation/access to have a real
  visited scope; skipped blocks never default to function scope. Original
  environment/activation, scope-escape, catch and tree-declaration checks remain.
  No printer, async or close/recovery admission change. See
  [for-of-cell-lifetimes.md](for-of-cell-lifetimes.md).
- Target advances, does not complete: m28799 **op231 PC620**, now
  `FunctionTreeError::kExpression > ExpressionError::kUnsupportedOperation >
  CallPlanError::kReference > CallReferenceError::kNotInline >
  EvaluationKind::kRoot > TemporaryReason::kCrossRegionValue >
  GuardedCallError::kUse` (read op20 PC56, guard op21 PC58, Use257).
  Parked. Previous flags keep `kMultipleExits` at PC1146, TryId0/H1171.
- Real es2abc callback fixture: **34** engine/original Node cases for captured
  accumulator/helper identity/isolation, iterator effects and throw-driven
  close failures; source passes Node syntax and an accumulation mutation is
  observed. Direct body-cell recovery, explicit break/return close ownership,
  and explicit body-throw evaluation shapes remain typed negative fixtures;
  they are not claimed as successful engine differentials.
- Exact 200/201/400/800 graph byte boundaries:
  **519944/521012/733544/1164840**; work:
  **5181629/5186561/6168349/8141377**. Proof mutations, register/copy/block
  variants, ordinary control, cells and phased initialization tests pass.
- Both 40-input cohorts against **85d86079**: **530/620** previous flags,
  **582/620** source unchanged; zero status regressions or source/entry/method
  position drift in complete records. Only source o5/28303's diagnostic moves;
  previous-flag manifests are unchanged. Baseline HEAD and executable freshness
  checked against the baseline build-log start.
- Full build, focused **8/8** checks and serial source-regression **65/65**
  passed. The ordinary composition test also resolves the projected control
  unit to its real block and requires an explicit scope; it cannot pass by
  comparing two absent mappings. The TimeUtils CLI/manifest test now asserts
  PC620 and the exact guarded-reference cause/evidence, replacing the stale
  unlocated lifetime/binding expectation. Requested sweep **156/166**; all ten
  failures reproduce with baseline **85d86079** executables rebuilt:
  `family_source_cli_test` (owner/origin arena assertion), `derived_cells_test`,
  `derived_cells_source_test`, `unguarded_derived_cells_source_test`,
  `captured_receiver_source_test`, `unguarded_captured_receiver_source_test`,
  `reversed_captured_receiver_source_test`,
  `unguarded_reversed_captured_receiver_source_test`,
  `reversed_initialization_captured_receiver_source_test`, and
  `unguarded_reversed_initialization_captured_receiver_source_test`.
  Baseline family CLI was run directly because its paired-root CTest is not
  registered. Fresh review found no important correctness/accounting issue.
  Build, accepted test logs, comparisons and baseline evidence are under
  `build/review/for-of-cells/`. `git diff --check` passes. No commit, push or stash.

## Private declaration prefix before native rest (2026-10-20, uncommitted)

- o32/53454 m54264 `findProperties`: PC0 environment allocation, PC14 helper
  creation, PC23 fresh-cell store, PC25 COPYRESTARGS(1). The order proof now
  distinguishes private declaration instantiation from observable work, using
  the real creator's verified environment/storage identity and a closed CFG
  cut. Calls, early escape/read, outer-cell stores, argument-window mutation,
  scalar argument observation and repeated rest allocation remain refused.
  Residual order/captures, fresh arrays, .length, ABI and recovery are unchanged;
  async-prelude admission is untouched. The independent verifier re-derives
  typed declaration events and charges its comparison work. Syntax failures
  retain RestOwnershipError and the available PC.
- Target advances, does not complete: --language-source now stops at
  **FamilyEnclosureError::kStructure** (no published location; scratch probe:
  **m53795 `f`, TreeStructureError::kBinding**). Previous flags stop at
  **ArrayInitializationError::kContract**, m54291 `getProperty`, no PC.
  These downstream refusals are parked. See [rest-declarations.md](rest-declarations.md).
- Real es2abc positive fixture: **48** Node differential scenarios for private
  helpers and captured ordinary parameters, freshness/isolation, argument and
  body getters/throws, missing/undefined arguments and length 1. Three emitted
  signatures recompile with COPYRESTARGS(1) and two formal slots. A rest-to-
  ordinary-parameter mutation is detected. The exact captured-rest helper
  fixture remains a separately asserted **kArgumentUse** refusal for its raw
  scalar-to-cell staging; it is not counted as a successful source differential.
  Pinned compiler identity/hash/options and adaptation are in the fixture.
- Unit proof/verifier mutations and exact 200/201/400/800 byte/work boundaries
  pass; a register/copy/block-split variant passes. The checker borrows charged
  topology; its 200/201/400/800 declaration cases use **9485/9532/18885/37685**
  bytes and **10618/10671/21218/42418** planner work. Verification additionally
  charges one comparison per declaration event. All buffers are prepaid.
- Both 40-input cohorts versus **f1a0cb7e**: **530/620** previous flags and
  **582/620** source unchanged, no regression or text/position-map drift in
  previously complete records. Only o32/53454's diagnostic changes per flag set.
- Full build passed; stable-binary serial `ctest --preset source-regression -j 1`
  **65/65**; focused rest/unit/Node/recompile/CLI tests **4/4**. The existing
  for-in CLI expectation now asserts the exact downstream refusal per contract
  and retains CLI/manifest equality. Requested sweep **124/133**; all nine
  failures reproduce at **f1a0cb7e**: `family_source_cli_test` (owner/origin
  arena assertion), `snapshot_syntax_test`, `snapshot_syntax_source_test`,
  `captured_receiver_source_test`, `unguarded_captured_receiver_source_test`,
  `reversed_captured_receiver_source_test`,
  `unguarded_reversed_captured_receiver_source_test`,
  `reversed_initialization_captured_receiver_source_test`, and
  `unguarded_reversed_initialization_captured_receiver_source_test`.
  Baseline snapshot tests were rerun after building their producer; the baseline
  family CLI was run directly because its paired-root CTest is not registered.
  Final build, accepted test logs, cohort comparisons and baseline reproductions
  are under `build/review/rest-prefix/`. `git diff --check` passes. No commit,
  push or stash.

## Cross-plan lexical-cell stores (2026-10-20, uncommitted)

- Fixed o18/56008 m56581 PC108: the discovery plan's Cell2 and the final
  demanded plan's Cell3 both denote incoming depth0/slot8. The old store check
  reused Cell2 against the final flow and incorrectly expected Symbol5 rather
  than the verified Symbol6. The join now uses original operation and resolved
  environment/allocation storage identity, with independent stored-input,
  target, source-write, Commit and exception correspondence checks. Owner,
  Language-domain, store and TDZ obligations remain. No source naming or
  printer change. See [capture-binding.md](capture-binding.md).
- The target completes under --language-source. Both 40-input cohorts against
  **d233bd54**: **530 -> 530/620** previous flags, **581 -> 582/620** source.
  No status regressions or text/entry-position/method-position drift in any
  previously complete record; only source o18/56008's manifest changes.
  Previous flags keep the original capture-initialization refusal
  (m56553 PC99), which this slice does not change. New output passes Node syntax.
- The pinned es2abc fixture reproduces the old FamilyWriteError::kStore and
  passes **42** original-source / engine-output Node scenarios: hoisted
  writers, inserted/permuted descendant demands, live updates, throwing RHS,
  early TDZ (constructor and trace), and per-call cell isolation. A wrong-store
  behavioral mutation is observed. Unit mutations reject wrong depth/slot,
  forged references, RHS/operand/address substitutions, missing writes,
  forged Commit and missing exception exits. Exact byte/work limits pass at
  **200/201/400/800** stores and demands. For one retained store with those
  demand counts the checker uses **395 bytes** and **290/291/490/890** work;
  it borrows the charged final binding table and scans it once.
- Full build passed; serial `ctest --preset source-regression -j 1`
  **65/65**; focused lexical-store/class-context/Node tests **3/3**. Requested
  CLI/corpus/failure/receipt/manifest/ISA/capture/lexical/family/cell/scope
  sweep **136/146**. All ten failures reproduce with **d233bd54**:
  `family_source_cli_test` (owner/origin arena assertion), `derived_cells_test`,
  `derived_cells_source_test`, `unguarded_derived_cells_source_test`,
  `captured_receiver_source_test`, `unguarded_captured_receiver_source_test`,
  `reversed_captured_receiver_source_test`,
  `unguarded_reversed_captured_receiver_source_test`,
  `reversed_initialization_captured_receiver_source_test`, and
  `unguarded_reversed_initialization_captured_receiver_source_test`.
  The baseline has no paired-root CTest registration for the family CLI, so
  its unchanged CMake test was also run directly with the baseline executable
  and paired sample. No expectations were relaxed. `git diff --check` passes.
- Fresh OLD/NEW cohort exports, comparisons, baseline reproductions, build and
  test logs are under `build/review/lexical-cell/`. No commit or push.

## Staged indexed-array commits across selections (2026-10-19, uncommitted)

- Diagnosed o8/42040 m42490: allocation op17 PC47 creates the private empty
  array; the selected string phi v17 is committed by op48 PC126
  `STOWNBYINDEX`, then the array is stored into an outer object and passed to
  the saved member call. The pinned interpreter uses own-property definition,
  not indexed Set. See [literal-initialization.md](literal-initialization.md).
- Added a prepaid indexed-commit table using the shared array cursor equation.
  Literal verification independently reconstructs each original stored use,
  definition order, Language domain, private identity and activation proof.
  Existing normal selection/aggregate coverage now admits certified array
  storage. Every staged indexed commit must acquire that independently checked
  complete native array projection; no indexed assignment fallback is allowed.
  Tree operands and existing original/recovery ports retain successful commits
  and exception exits. No printer change, new premise or shape-specific path.
- All three targets complete under --language-source: **o8/42040 m42490,
  o15/20252 m20697, o28/41167 m41622**. Under previous flags o8 and o28 complete;
  o15 retains metadata-level `ModuleEnclosureError::kContract` for lazy imports
  (no method/PC). Both 40-input cohorts versus **abf950a8** have no regressions
  and no text or position-map drift in any previously complete record:
  **528 -> 530/620** previous flags; **578 -> 581/620** --language-source.
  Only those two/three manifests change, respectively.
- New/refreshed tests **5/5**. The pinned es2abc fixture asserts its own indexed
  definitions and caller exception exits, then passes **234** original-source /
  engine-output Node scenarios: short-circuit/getter/throw order, receiver,
  callee replacement, descriptor flags, fresh identities and hostile inherited
  indexed setters. Replacing a literal definition with indexed Set is detected.
  Synthetic tests cover wrong-edge commits, early observation, cursor/domain/
  coverage/projection mutations, copies/register/block variants, fresh loop
  activations and unfinished re-entry. Exact byte/work limits pass at
  **200/201/400/800**; measured work/storage grow linearly.
- Full build passed; fixed source regression suite **65/65** serial. Required
  CLI/corpus/failure/receipt/manifest/ISA/call-spread/staged/literal/logical/array
  sweep **149/157**. Its eight failures are the previously documented
  `family_source_cli_test`, `literal_construct_test`, `mixed_literal_test`,
  `phased_literal_test`, `literal_closure_source_test`,
  `unguarded_literal_closure_source_test`, `derived_literal_receiver_source_test`
  and `unguarded_derived_literal_receiver_source_test`; none of their checks
  were weakened. All eight reproduce on the rebuilt **abf950a8** baseline at
  the same assertions (`baseline-failures.json` and per-test logs).
- All new .mjs outputs pass `node --check`. o15 is existing lazy-import ArkTS
  syntax: its body passes Node syntax checking with only `import lazy` adapted
  to `import`; pinned es2abc accepts the full lazy form after the documented
  string-export alias adaptation (`as "KeyMenu"` -> `as KeyMenu`). Neither is
  a claim about Node executing lazy imports. Peak reported CLI RSS is
  **42,549,248 bytes** (previous flags) / **40,517,632 bytes** (--language-source);
  even four simultaneous CLI peaks are below **171 MB**, plus the small driver.
  Evidence: `build/review/staged-indexed/`.

## Retained predicate snapshot with a private literal argument (2026-10-19, uncommitted)

- Diagnosed m27565 PC903: op302 PC812 is an already-materialized condition
  input. Its projected effect successor is the saved member op313 PC839,
  rather than branch319. The previous prefix proof correctly refused moving
  that call across the Reference and outer allocation, but had no way to keep
  its existing storage while composing the independently proved literal arms.
- Added a bounded prefix split only for original certified literal-arm phi
  occurrences seeded by unmet References. Every retained leading root needs
  a language snapshot and successful Commit availability at the condition;
  its order, temporary and original exception exit stay ordinary. The movable
  suffix retains the original order/catch checks. Independent selection and
  existing TreeFlow/recovery verification remain required. No printer change,
  allocation-exclusion removal, new premise or joint keep-edge admission.
- **o24/27331 completes**, including m27565 op337 PC903. Both 40-input cohorts
  versus **6514565c** change only that record: **527 -> 528/620** previous
  flags, **577 -> 578/620** --language-source. All previously complete text is
  byte-identical, with no status regression. Both new corpus outputs pass
  `node --check`.
- New pinned es2abc fixture exports three complete functions and passes
  **216** original-source/engine-output Node scenarios, including producer
  before the body's try/catch, once-only evaluation, callee/receiver identity,
  argument getters/throws and mutation after the snapshot. Synthetic variants
  and mutations pass, with exact byte/work boundaries at **200/201/400/800**
  for the arm, enclosing reference and independent selection verifiers.
- Full build passed; `ctest --preset source-regression -j 1` **65/65**;
  new tests **2/2**; targeted **13/14**, with only the documented
  `phased_literal_test` kArmSelection mutation at line707. Required sweep
  (including nested_literal/literal_arm/logical) **115/116**, with only the
  documented `family_source_cli_test` owner/origin-arena failure. No assertions
  or printer expectations were weakened.
  See [literal-event-view.md](literal-event-view.md); scratch diagnostics,
  baseline comparisons and test logs: `build/review/literal903/`.

## Nested literal selection in a member-call argument (2026-10-19, uncommitted)

- Extended the original LiteralEventView/arm proof to phased inputs, then added
  `LiteralReferenceView` for a selected field inside an enclosing verified
  private literal. Only unmet member References seed discovery. Original arm
  phi uses, definition/Commit identities, private ownership, effect order and
  catch correspondence are independently rederived before forest membership
  may replace ordinary phase equations. Ordinary allocation exclusion stays;
  no joint predicate/keep-edge admission, printer change, or new premise.
- o24/27331 m27565's saved `showToast` Reference (op107 PC305), enclosing object
  and selected `message` definition now compose into call op131 PC369. The
  record remains incomplete at **m27565 op337 PC903**, saved property op313:
  `FunctionTreeError::kExpression > ExpressionError::kCall >
  CallPlanError::kReference > CallReferenceError::kNotInline >
  EvaluationKind::kRoot > TemporaryReason::kCrossRegionValue`. This later
  obligation is parked; o7/28433 remains excluded.
- Full build passed. The two new tests pass; the real pinned es2abc fixture
  exports both functions as complete source and passes `node --check` plus
  **120** original-source/engine-output Node scenarios (selected getters and
  throws, catch, field order/descriptors, receiver and callee replacement).
  The synthetic phased test includes copy/block-split/register-renaming,
  wrong-arm/consumer, duplicate consumption, escaping inner value, reordered
  outer definitions, wrong catch and exact byte/work refusals. Both indexes
  pass **200/201/400/800** widths with measured linear growth; at 800 the arm
  index uses 1,104,989 charged bytes and the enclosing index 1,327,203 bytes.
- Final verification: full build, fixed source suite **65/65** serial, new
  tests **2/2**, targeted **11/12**. The targeted failure is the documented
  `phased_literal_test` kArmSelection mutation (`changed` at line707), also
  reproduced on the baseline test executable (line662). Required sweep
  **112/113**; its only failure is the documented `family_source_cli_test`
  owner/origin-arena failure. No assertions or printer spellings were changed.
  Verification logs and comparisons: `build/review/literal-nesting/`.
- Both 40-input cohorts versus **3168de42** retain **527/620** and **577/620**
  complete. No source, position map, or record metadata file changes; the only
  file change in each cohort is `o24/manifest.json`, solely the target's first
  refusal location PC369 -> PC903. No new corpus output needs `node --check`.

## Readable-output test expectation refresh (2026-10-19, uncommitted)

- Test-only follow-up: updated stale assignment/object/function/arrow/default
  grouping expectations and mutation selectors. The three requested tests
  (member chain, record imports, widget globals) pass. Found and repaired the
  same issue in Index, Content, window/breakpoint/card/tabbar/viewmodel records,
  module entry initialization, deferred captures and phased nested literals.
  Every repaired mutation still changes parseable source and is behaviorally
  detected; no production code or behavioral assertions were weakened.
- Conservative printed-token source scan selected **688 tests**, run with
  `-j 6`: initially **637 passed / 51 failed**. Final repaired/related batch
  **20/20**; fixed source suite **65/65** serial; logical/literal-arm **8/8**;
  required sweep **103/104**, only the documented `family_source_cli_test`.
- **37 broad-run failures remain** after the repaired batch: documented
  snapshot/literal/receiver/try/class/generator failures, a historical community
  ability worker failure, plus low-level failures independently reproduced
  using a fresh archive build of pre-grouping **d4076bd2** (register_uses,
  metadata_index, derived_cells, expression_precedence and decision_snapshots).
  The latter already has an invalid mutation before grouping; it was left
  unchanged. No claim is made that the community worker's empty error identifies
  a timeout. The ordinary community graph test passed.
- Full test lists, per-failure evidence and logs:
  `build/review/readable-tests/report.md`, `tests.txt`, `remaining-tests.txt`,
  `{selected,repaired,fixed,logical,sweep}.log`, and `baseline-*.log`.
  Worktree checkout was sandbox-blocked; a read-only git archive was used.
  Existing wtbase stays at **41c20e3c**. No commit, push or stash.

## Conditional literal arm integration (2026-10-19, uncommitted)

- Baseline `41c20e3c`. The exact m65897 ValueIR shows **normal**, not phased,
  flow: op8/v25 is the saved `Image.create` read, refused because it crosses the
  argument selection before op27/PC64. The allocation is already a verified
  phi-input literal. See [literal-event-view.md](literal-event-view.md) for the
  ValueIds/UseIds/PCs and the original `MemberReferenceInline` failure.
- Added `LiteralArmIndex` over the accepted LiteralEventView: original entry/phi
  edges, unique external observation, committed final value, dominated all-path
  forest coverage, acyclic single execution and original exception exits.
  Shared prepaid indexes and memoized disjoint arm walks retain O((V+E) log V).
  The verifier reconstructs independently. Only an unmet member Reference seeds
  the new normal selection path; the existing order/receiver/evaluation/tree/
  recovery proofs remain authoritative. Phased allocation exclusion unchanged;
  no joint predicate/keep-edge admission, new premise or printer changes.
- **o24/63384 now completes**, including m65897 and the sibling fallback in the
  same record. Both 40-input cohorts: **527/620 previous flags**, **577/620
  language-source**, up one each. All 526/576 previously complete outputs are
  byte-identical; only record o24/63384's manifest entry changes (3 -> 0).
  Both new outputs pass `node --check`. No other target was pursued.
- Full build passed; fixed `ctest --preset source-regression -j 1`: **65/65**.
  New tests **3/3** (event view, arm view, real es2abc/Node fixture), including
  200/201/400/800 widths, exact byte/work budgets and arm/value/exit/catch/order/
  consumption mutations. Fixture was incomplete at baseline (method480 PC69,
  `kNotInline > kRoot > kCrossRegionValue`); now complete, with **140 Node
  differential scenarios** against original source and compiler/hash metadata.
- Required sweep **103/104**: pre-existing `family_source_cli_test` owner/origin
  arena failure. Broader logical target selection **3/5**: `logical_regions_test`
  and its Node wrapper fail the same stale `value.items(({probe:` formatting
  assertion, independently reproduced in baseline 41c20e3c (old line1170).
  These expectations were not weakened or changed in this slice.
- Evidence: `build/review/literal-integration/{full-build,fixed,new-tests,
  targeted,sweep}.log`, `comparison.json`, `value-ir.txt`, fixture baseline and
  cohort logs. Native process RSS measurement via `/usr/bin/time -l` was blocked
  by the sandbox's `sysctl kern.clockrate`; no new RSS claim is made.
  Staged/multiblock initializer discovery and phased-arm composition remain
  separate work; o24/27331 and excluded o7/28433 remain unchanged.

## Conditional literal event view: partial foundation (2026-10-19, uncommitted)

- Baseline `41c20e3c`. Added an **unhooked event census**, not completed
  conditional selection composition. `LiteralEventView` retains original
  allocation/definition/nested operand identities, consumers, Commit phases
  and exception exits; its verifier recomputes the census from the original
  graph and independently verified LiteralPlan. Explicit-stack forest indexing
  is linear and uses prepaid TreeCheckBudget storage.
- The allocation exclusion, selection admission, member-reference checks and
  all production placements remain unchanged. **m65897 is not complete**:
  o24/63384 still has `kNotInline > kRoot > kCrossRegionValue` at PC64;
  o24/27331 m27565 still has the same chain at PC369. o7/28433 was not touched.
  Conditional arm coverage, selection/composition integration, independent
  TreeFlow/recovery consumption and the real es2abc/Node fixture are outstanding.
  See [literal-event-view.md](literal-event-view.md); this is below the requested
  m65897 acceptance boundary and must not be counted as completion.
- Full build passed. New `literal_event_view_test` passed (existing bytecode/SSA
  fixtures, nested/empty-array/exception cases, mutations, 200/201/400/800 and
  exact byte/work budgets). Fixed source suite **65/65**. Targeted tests **2/3**:
  `phased_literal_test` fails its existing `kArmSelection` mutation's `changed`
  assertion, reproduced using the baseline test executable (old line662,
  new line707). Required sweep **103/104**: the previously documented
  `family_source_cli_test` owner/origin-arena failure remains unchanged.
- Regenerated both OLD and NEW 40-input cohorts: **526/576 complete** unchanged;
  every manifest and every emitted source file byte-identical, not merely the
  previously complete subset. No new complete output requires node --check.
  Evidence: `build/review/literal-event-view/{build,fixed,targeted,sweep}.log`
  and `comparison.json`. No new engine-output Node differential is claimed.

## Readable grouping slice 1c: residual contexts (2026-10-19, uncommitted)

- Baseline `6bdd4ae9`. Test-only syntax census covers every emitted JS/ArkTS
  grouping pair, queries production grouping decisions, and retains `kUnknown`
  for unmapped contexts. Source census **5,861 -> 3,985**; previous flags
  **4,991 -> 3,354**. Occurrences/top-15/examples are under
  `build/review/paren1c/`; see [readable-grouping.md](readable-grouping.md).
- Bare `void 0` is limited to explicit Expression/AssignmentValue positions;
  parameter cursors now supply the latter. Synthetic name-preserving object
  selections omit their envelope only in already-proven ungrouped positions,
  without self-binding IIFEs. Computed keys, selection, names, scopes, `(0, ...)`
  and all brace rules are unchanged. Unknown/operator/postfix contexts stay
  conservative; all 188 assignment/member-base groups remain.
- Complete sets/statuses **526/576/80** unchanged. Strict es2abc/disasm at both
  default and O0: **526/0/0**, **567/0/9**, **80/0/0**
  (pass/differ/unverified), no allowance. Complete-JS token removals:
  **1,624 / 1,786 / 220**; token checks **526/0/0**, **563/0/13**, **80/0/0**.
  All-JS Node checks including partial outputs **527/564/80**, zero failures.
- Full build passed; fixed suite **65/65**; grouping/brace **10/10**;
  required sweep **101/102**, only pre-existing `family_source_cli_test`.
  ProgressBar's mutation selector and the native-name wrapper-presence selector
  follow new spellings; behavioral/name assertions are unchanged. Tests include
  200/201/400/800 widths/depths and exact budgets. No commit/push/stash.

## Readable grouping slice 3: braces (2026-10-19, uncommitted)

- Baseline `edc9821e`. A prepaid `BracePlan` in `SourceVariables` uses original
  scope/symbol/target facts plus final binding/nullish/loop print projections.
  Sole-if alternate wrappers become `else if`; empty-scope, unlabelled bare
  statement-list blocks can flatten. Required bodies, lexical/function/class
  declarations, captured/catch scopes, labels, directive-sensitive strings and
  projected content stay braced. Child spans/textless anchors use the same
  counting/real token actions. No expression or value-call policy change.
- A constructed binding differential caught projected lexical declarations
  (`BindingInitializers::statements`) absent from the original scope census.
  The eligibility check now keeps those blocks; the unchanged behavioral
  oracle and a focused plan regression validate this boundary.
- Complete sets/statuses unchanged: **526/620**, **576/620**, paired **80/80**.
  Previous flags remove **96** else wrappers; language-source removes **106**;
  paired removes none. No real cohort bare block qualifies; the standalone
  tree/Node tests exercise flattening and completion preservation.
- Strict es2abc/ark_disasm, both default and O0 (no allowance): previous flags
  **526 pass / 0 differ / 0 unverified**; source **567 / 0 / 9**; paired
  **80 / 0 / 0**. Nine unchanged compiler failures remain unverified. Token
  checks: **526/0/0**, **563/0/13**, **80/0/0** (pass/fail/unverified); the 13
  ArkTS files are unchanged. All-JS Node syntax: **527/564/80**, zero failures.
- Full build passed; fixed `source-regression -j 1` **65/65**, brace/grouping
  **9/9**, required sweep **98/99**. Only the documented pre-existing
  `family_source_cli_test` owner/origin-arena failure remains in the sweep.
  Exact byte/work and width/depth cases cover
  **200/201/400/800**. See [readable-grouping.md](readable-grouping.md) for rules,
  ten examples and compiler hashes. Reports/logs:
  `build/review/braces/{finalcheck,finalchecks,checkpair,final-output-check.json}`.
  The corrected executable's sources are byte-identical to the compiler-checked
  outputs. No commit, push or stash.

## Activation-local staged literals (2026-10-19, uncommitted)

- Baseline: `66ed407c`. `VerifyStaged` retains the existing DAG path unchanged.
  Only cyclic methods use the new shared full-flow dominator/DFS-feedback index.
  Allocation and successful definition commits form a dominance-ordered chain;
  unfinished-region feedback and definition re-entry refuse. Exiting an
  allocation-dominated region may abandon an unobservable object, but cannot
  return to its definitions without a fresh allocation. Original exception
  edges, no-escape uses, successful-commit availability and tree/recovery checks
  remain binding. No symbols, names or publication layout are changed.
- The fallback uses linear prepaid storage/indexing plus the existing dominator
  bound O((V+E) log V), with constant-time interval queries per construction;
  there is no per-literal CFG scan. Build and verification independently
  recompute the input proof. No new source-language premise.
- Target outcomes under V5: o5/28303 m28799 passes staged acyclicity and reaches
  `FunctionTreeError::kBinding > TreeObligation::kBindings` (no owned PC).
  o24/37520 passes that obligation in m38333 and reaches m38116
  `FunctionTreeError::kControl > TreeObligation::kDecisionPrefix`,
  `DecisionPrefixError::kPhaseUnit`, control1396 / synthetic Try8 (no PC).
  Neither record completes; downstream obligations were not changed.
- Both 40-input comparisons against retained `66ed407c`-equivalent outputs:
  **526/620** previous flags and **576/620** V5, zero status regressions and zero
  complete-record text/extension drift. Diagnostic changes are only o24/37520
  under previous flags and o5/28303 plus o24/37520 under V5. Maximum CLI RSS:
  **39,485,440** / **39,452,672** bytes respectively.
- Full build passed (`cmake --build build/regression -j 6 -- -k`). Serial fixed
  preset **65/65**; staged/literal targeted **6/6**, including the two new tests
  (**2/2** on the final fixtures). The serial CLI/corpus/failure/receipt/manifest/
  ISA/call-spread sweep is **89/90**; only the previously documented
  `family_source_cli_test` fails. `evaluation_input_cli_test` and
  `tree_failure_cli_test` now assert the actual next leaves above, preserving
  CLI/manifest equality and real synthetic/absent-location evidence.
- Real es2abc fixtures and engine-output Node differential cover conditional
  initialization inside loops, surrounding/nested loops, fresh identities,
  property order, zero iterations and throws after an earlier field commit.
  The unfinished catch-cycle fixture stays a typed negative. Unit mutations
  reject unfinished re-entry, repeated definitions without reallocation and
  partial-object observation; 200/201/400/800 and exact byte/work budgets pass.
  Emitted positive fixture output passes `node --check`; no new cohort output
  was published. Compiler/source/ABC hashes and commands are in the fixture.
- The existing ISA generator refreshed the checked-in report: **417 ABCs /
  1,309,699 instructions**, no probe failures; **145 SupportedUnverified /
  159 Unmeasured / 28 NonExecutable**. The historical APPLY unsupported rows
  are gone. The remaining raw unsupported first refusal is Sendable creation
  under previous flags; none under V5. JSON stays untracked under
  `build/review/triage/staged-accepted-isa/`. This is refreshed measurement of
  prior APPLY support, not new ISA semantics or a SupportedVerified claim.
- Acceptance logs, per-record comparisons and scratch diagnostics are under
  `build/review/triage/staged-accepted-*`. Temporary instrumentation was removed.

## Nested APPLY snapshot accounting — uncommitted follow-up to 8922134a

- Evidence: o7/17736 m18306 op138 PC379 has the correct result consumer Use160
  in op141 PC387 (`indexOf`). Its own inputs are Use153 -> op125 PC345 (saved
  `Math.min`), Use154 -> op122 PC337 (`Math` receiver), and Use155 -> op127 PC351
  (private argument array). The refusal is actually VerifyPredicateSnapshots'
  interval-end check, not a consumer mismatch: child interval ends at 7 while
  serial is 8. The extra event is op134 PC369, the private array length read,
  already proved/classified kArrayCursor. The witness reports the current APPLY
  op and its valid consumer; it does not identify a different expected owner.
- Local fix: the shared snapshot Events function treats independently proved
  kArrayCursor bookkeeping like other projected initializer events. No ownership,
  interval, order or exception condition was removed. ArrayInitialization and
  evaluation verification still independently reject forged cursor classification.
  No new allocation/index or asymptotic work is added.
- Real es2abc fixtures add `a.indexOf(f(...xs))` with a reused predicate result
  and `g(h(...xs), y)`. Engine-output Node comparisons check calls, getters,
  iterator order, receiver identity and child throws; adapted es2abc round-trip
  checks continue to pass (the documented export-syntax adaptation is unchanged).
  `g(...h(...xs))` is a separate negative fixture retaining the existing
  ArrayInitializationError::kSharedOperand boundary, not a newly admitted protocol.
- Baseline comparison against retained 8922134a outputs: previous flags
  **526/620**, source **576/620** (575 before). The sole status/diagnostic change
  is o7/17736 becoming complete under V5; no complete-record text or extension
  drift. Its new output passes node --check. All 40 inputs completed in each run;
  measured V5 CLI peak RSS maximum **39,534,592 bytes**.
- Acceptance: full build passed; fixed preset serial **65/65**; call-spread
  **3/3**; combined targeted run **5/5**; CLI/corpus/failure/receipt/manifest/ISA
  sweep **86/87**, only the previously registered family_source_cli_test
  owner/origin-arena failure. All diagnostic instrumentation was removed.

## Narrow APPLY call spread — V5 (2026-10-19)

- V5 adds the separately approved `CallSpread()==kPrivateArgumentsV1`; V1–V4
  retain their meanings. Raw APPLY carries an argument-array form, including
  receiver, ordered effects, exceptional completion and normal result commit.
  Existing private array construction, cursor, callee/reference and tree/recovery
  proofs gate publication; the legacy fixed-argument matchers exclude APPLY.
- Both cohort runs compared against the retained ISA-3 recovery baseline:
  previous flags **526/620**, V5 **575/620** (was 573). No complete-record status
  regression or source/extension drift. Only the three target diagnostic chains
  changed (old flags now give typed kPremise); V5 newly completes o31/10300 and
  o31/28731. m29198 PC100 and PC154 both print native spread calls.
- o7/17736 remains incomplete: m18306 op138 original/working PC379,
  EvaluationError::kOwnership > kOperandOwnership, actual consumer Use160,
  actual owner op141 PC387 (the surrounding indexOf call). No ownership check
  was weakened. Normal block-split private constructions also retain typed
  CallSpreadError::kOrder at the existing scheduler boundary.
- Full build passed. Stable `ctest --preset source-regression -j 1`: **65/65**;
  new tests **3/3**; targeted tests **8/8**; CLI/corpus/failure/receipt/manifest/ISA
  sweep **86/87**, only the recorded family_source_cli_test owner/origin failure.
  The constructed APPLY lift expectation was refreshed from the production
  probe; it now lifts instead of claiming unsupported. Earlier fixed-suite
  failures caused by relinking producer binaries during execution were discarded;
  the stable rerun passed without changing those tests.
- Serial V5 cohort CLI reports: all 40 ABCs measured; maximum process peak RSS
  **39,419,904 bytes** (o4), below the 1 GB cap.
- Node differential passes the real engine-exported fixture (five functions,
  six APPLY sites), including order, throws, receiver/callee rewrite, sparse,
  empty and string inputs. New corpus outputs pass node --check. The pinned
  es2abc recompile preserves per-method createarray/STARRAYSPREAD/APPLY sequences
  after an identifier-export syntax adaptation. Unmodified full-file recompile
  fails on the existing string-named export aliases; this is not a fully passing
  unmodified round-trip. See the fixture README for commands and SHA-256.
- The modified Array.prototype iterator difference is explicitly demonstrated
  as the approved V5 difference, not counted as an Ark execution pass. No Ark VM
  run is claimed. The requested matrix is not exhaustive: cross-block publication
  and full exception-edge mutation coverage remain review/follow-up boundaries.

## 更新规则

- 每次提交连贯改动后更新本页：交付内容、构建与验证证据、剩余门槛。历史记录写入 `history/`。
- 不按文件数、提交数或 complete 数量宣布完成；通用性以冻结留出集为准，见
  [审计共识](2026-10-12-audit-consensus.md) §5。
- 全语料批跑、RSS 基准和全套测试仍按用户要求暂停；固定 65 项和定向验证可以运行。

## ISA increment 3：current Unmeasured 指令证据

- 基线 `7d97d2a6`；按用户允许的边界完成 **127 个 current** 变体的证据表，
  **47 个 deprecated 留待后续**，未推定它们不能执行。完整逐变体 handler/编译器
  引用、O0/default 实测、raw lift 和语料影响见
  [current ISA evidence](isa-current-evidence.md) 与 `tests/isa/current_variants.json`。
- 125 个 C++ handler 已实现，NOP 为显式 no-op；LDNEWTARGET 在 C++ interpreter
  FATAL、assembly 路径 stub，但 baseline/AOT 有实现。因此 **没有新增 NonExecutable**
  拒绝，也没有生产语义/准入改动。仅修复 test-only coverage_probe 对 prefixed
  secondary-opcode 0 的 prefix 报告（按真实 decoded size/operand widths 计算）。
- 127 个 constructed ABC 全部 engine + SDK ark_disasm 交叉解码，raw lift
  **43 ok / 84 UnsupportedInstruction**；builder 新增预先计费的空 tagged literal
  pool/index，保留旧默认序列化。它们不是可执行协议样本，不计 Node/Ark oracle。
- 24 种源码/模式、**48 次 es2abc O0/default** 编译与重新编译 hash 对比通过；
  **82 个变体 observed，45 个 not observed**。后者不是 impossible；patch 的
  additional-var/symbol-table 工作流仍是 emitter probe 缺口。编译器、命令和所有
  hash 见 [fixture README](../../../tests/fixtures/isa/current/README.md) / manifest。
- 完整构建通过；固定集 **65/65**；新测试的三组检查通过（全量 cross-decode/lift、
  48 命令重编译、200/201/400/800 与精确 builder budget）。最终 sweep **86/87**，
  唯一 `family_source_cli_test` 为既有 owner/origin arena 失败，未弱化断言。
- ledger **413 ABC / 1,309,154 指令**，无 probe failure：**144 SupportedUnverified /
  160 Unmeasured / 28 NonExecutable**；14 项提升来自新增 complete-fixture 证据，
  不是新增实现。oracle columns 仍为空，不能把重新编译输入当作 decompile round-trip。
- 两种探索集逐字相同（包括 manifest/partial）：旧 flags **526/620**、V4
  **573/620**；3,635 / 3,776 文件无差异。报告 `/private/tmp/isa3-cohort-comparison.json`。
  未 commit/push。下一步优先 APPLY 私有参数数组联合证明（3 个 first-refusal record），
  再补原有 primitive/width 的语义 oracle，随后 this/super/对象协议；复杂 async/
  generator 和 patch/frame track 分开。无额外 premise 授权被假定。

## ISA increment 2c：常量字符串键的原生 name-setting

- 基线 `67083c01`。仅对 STOWNBYNAMEWITHNAMESET 两种宽度解除已证明的 private
  literal 门：新建且唯一消费的匿名 function/arrow/class、pool key 身份、构造区间
  内效果/异常顺序及不逃逸均复验。DataProperty 保留 name-setting 属性；独立
  tree operands / family enclosure / structure 检查身份与匿名性，打印保持匿名，
  使用原生 NamedEvaluation，不生成显式函数名或普通赋值。
- class RHS 复用原 class/prototype/Hole 证明及同一 expression lowering；只对新
  naming obligation 种下该证明，不重排已有 literal。带 static initializer 的
  class、共享/已命名的值、非 private/staged store 和 computed-key stores 继续拒绝。
  computed numeric-key `.name` 差异没有获得新前提，也未开放。
- 完整构建通过；固定集 **65/65**；7 项定向测试 **8/8**。新增 literal proof/
  mutation、200/201/400/800 与精确 byte/work 边界、真实 es2abc 两宽度 fixture，
  两种契约的 engine-output Node 差分（name/descriptor、prototype setter、sibling
  getter 顺序及异常）通过。node --check 通过，编译器命令/hash 已登记。
- `cli|corpus|failure|_receipt|manifest|isa` sweep **85/86**；唯一失败
  `family_source_cli_test`（node escaped its owner or origin arena）与
  `/private/tmp/isa2b-sweep.log` 的基线失败相同；未修改既有断言。
- 全 ledger 238 ABC / 942,564 指令：**130 SupportedUnverified / 174 Unmeasured /
  28 NonExecutable**。named 两宽度仅在实际有 complete fixture 的 profile 6
  提升；computed 两宽度仍 Unmeasured。Node 证据独立记录，不伪造 occurrence oracle。
- 两种探索集所有文件（含 manifest/partial）逐字相同：旧 flags **526/620**，
  V4 **573/620**；3,635 / 3,776 文件无差异。既有 paired/fixture 记录也无状态
  变化，仅新增 named_properties fixture 完成。比较记录：
  `/private/tmp/isa2c-cohort-comparison.json`；ledger `/private/tmp/isa2c-coverage`。
- 参见 [语义与证明边界](isa-properties-runtime-evidence.md) 和
  [fixture README](../../../tests/fixtures/isa/named_properties/README.md)。未 commit/push。

## ISA increment 2b：原始 name-setting 语义（未完成源码恢复）

- 基线 `163b91aa`；四个 WITHNAMESET 变体增加类型化 `PropertyNameSetting`，复用
  own definition，保留 acc、descriptor/name mutation 的可抛出效果与 Value IR 身份。
  接收者字段捷径不丢弃该属性；evaluation builder/verifier 以有位置的
  `LiteralError::kNameSettingProof` 拒绝尚未证明的源码投影。
- **本轮请求尚未完成**：computed object-key 事件、fresh anonymous function/arrow/class
  的 property-driven naming、独立 tree/family/recovery 证明和 engine-output Node
  差分仍缺失。没有将 raw lifting 或拒绝门当作源码支持。numeric computed key 的
  pinned fast-path `.name` 差异已记录在 [runtime evidence](isa-properties-runtime-evidence.md)。
- 完整构建通过；固定集 **65/65**；name_setting / evaluation_input / property evidence /
  constructed branches / coverage matrix **5/5**。新测试覆盖所有四个格式、操作数变异、
  独立 import verifier 拒绝删除 naming 属性、200/201/400/800 和精确 byte/work 边界。
- 指定 CLI/corpus/failure/receipt/manifest/ISA sweep **85/86**；唯一失败为已登记的
  `family_source_cli_test`：`node escaped its owner or origin arena`，与基线相同。
- 台账重跑 237 ABC / 941,774 occurrences，分类无变化（128 SupportedUnverified、
  176 Unmeasured、28 NonExecutable）。两个探索 flag 集与保存的证据基线逐文件
  byte-identical（3,635 / 3,776，含 manifests）；526/620、573/620 无状态变化。
- 构建与测试日志：`/private/tmp/isa2b-full-build-final.log`、`isa2b-fixed.log`、
  `isa2b-targeted-final.log`、`isa2b-sweep.log`；台账 `/private/tmp/isa2b-coverage`。
  未 commit/push/stash；用户输入 ABC 未修改。

## ISA increment 2：属性变体证据切片（未完成实现）

- 基线 `5e057b6f`；**没有生产代码改动，没有新增属性指令 lifting 或源码支持**。
  [逐变体证据](isa-properties-runtime-evidence.md) 确认 21 个 handler 全部实现；
  不能沿用上一轮 NonExecutable 分类。真实 O0/default 编译均覆盖 6 个 super
  变体和 4 个 name-setting 变体；8 个 frame-this 与 3 个 indexed store 在探针中
  未出现（不宣称穷尽不可生成）。21 个独立构造 ABC 加两个编译 ABC 均通过双 decoder。
- 发现 indexed Set 的 numeric Proxy key 与源码 string key 差异；frame-this 不能
  换成可写 ABI 寄存器；name-setting 是 define 后的第二个有序 heap effect；computed
  super/write 需要扩展原有 home-object/receiver/coercion 对应证明。尚未实现这些证明，
  没有假定新 premise，也没有把普通写入/定义互换。engine-output Node、语义变异和
  新 lifting 的预算测试仍未完成。
- `isa_property_evidence_test` 含 3 组：全部编码/handler 证据、hash/重编译/SDK 与
  engine decode/current refusal、200/201/400/800 和精确 builder budget。注册表生成器
  保留其他 family 的 fixture。覆盖台账 237 ABC / 941,774 occurrences / 189 observed
  variants，分类仍为 128 SupportedUnverified / 176 Unmeasured / 28 NonExecutable。
- 完整构建通过；固定集串行 **65/65**；property evidence / constructed branches /
  coverage matrix **3/3**；指定 CLI/corpus/failure/receipt/manifest/ISA sweep **85/86**，
  唯一失败为既有 `family_source_cli_test`：`node escaped its owner or origin arena`。
- `/private/tmp/isa2-coverage` 与保存的 `/private/tmp/isa1b-coverage-final` 逐文件比较：
  old flags 3,635 文件、V4 3,776 文件，含 manifest **全部 byte-identical**；526/620、
  573/620 不变。比较回执 `/private/tmp/isa2-cohort-compare.json`。未 commit/push/stash。

## ISA increment 1b：构造 ABC 与不可执行分支拒绝（待 cc 审查）

- 基线 `845c786a`。新增 `SemanticError::kNonExecutableInstruction`（7，原值不变），
  用固定大小的 opcode / `RuntimeEvidence::kPinnedExtendedBranches`（0）见证保留
  28 个扩展分支的 pinned-runtime 拒绝。有效分支不再错误折叠为 malformed；
  未解析目标仍是 malformed。JEQZ/JNEZ 及资源分类未变。见证贯通 raw probe、
  `abc_semantic`、route receipt 和 record-entry JSON，并校验反序列化。
- 测试专用两遍 writer 在 `tests/support/abc/builder.py`：单一 12.0.6 ECMAScript
  module/method、ASCII 字符串池、类型化实体/标签、相对跳转、catch-all、region/index、
  Adler-32；预先计费，checked uint/ULEB/branch/label，最大 128 MiB，不供生产调用。
  28 个独立拒绝 ABC 经 engine decoder 和 SDK ark_disasm 交叉验证。两个
  JEQZ/JNEZ 控制 ABC 与 es2abc 等价源码均由引擎导出，再作 Node getter/异常轨迹差分。
  不执行这 28 个不可执行指令，不将 disassembly 或拒绝称作语义支持。
- [覆盖台账](isa-coverage.md)：214 ABC / 934,957 occurrences，167 个变体出现，
  28 NonExecutable、128 SupportedUnverified、176 Unmeasured，其余三类为 0。
  NonExecutable 必须有被 hash 固定的引用、构造文件、raw/record 拒绝和通过的测试；
  只给实测 profile 分类。JSON 生成物在 `build/isa-coverage/coverage.json`。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 通过；固定串行 **65/65**；
  定向 non_executable / constructed branches / coverage matrix / branch condition /
  record_failure_json / source_route **6/6**。构造 suite 内含 5 组测试，覆盖
  200/201/400/800 和精确预算、错误标签/范围/编码字段、JSON 变异、citation/hash 变异。
- CLI/corpus/failure/receipt/manifest/isa 扫描 **84/85**，唯一失败是既有
  `family_source_cli_test`（node escaped its owner or origin arena）。修复了
  `isa_pin_test` 的测试准备：继承父配置选定的 Ruby 3.2.3，复制缺失的 access/ISA
  catalog 模板；pin/mutation 断言没有放宽，现通过。
- 最终扫描 `/private/tmp/isa1b-coverage-final` 对已保存的 increment-0 输出
  `/private/tmp/isa-coverage-full`：旧 flags 3,635 文件、V4 3,776 文件（含 manifests）
  全部逐字节相同，**526/620、573/620**，0 状态变化、0 文本变化。
  对比回执 `/tmp/isa1b-cohort-compare.json`；cc 仍可独立对当前基线复验。
- 日志：`/tmp/isa1b-build-acceptance.log`、`/tmp/isa1b-fixed.log`、
  `/tmp/isa1b-targeted-final.log`、`/tmp/isa1b-sweep-acceptance.log`。
  未 commit/push/stash；用户的 `sample.tar.gz` 和 `tests/samples/big.abc` 未改动。

## Round 76：静态初始化器 self-binding（证据阶段停止，待 cc 审查）

- 基线 `a19178ed`。仅新增[证据记录](round-76-static-self.md)，没有修改生产代码或测试，
  没有放宽 guard、capture、静态初始化时序、命名或准入；未 commit/push/stash。
- 实际 ABC 顺序与任务中的待核实假设不同：o0/4660 creator m4850 为
  class13 → inner self-store28 → initializer m5040 创建40/调用51；o20/8166 creator
  m8392 为 class39 → inner self-store54 → m8556 创建250/调用261。外层发布均在之后。
  FindClassSelfStores 证明私有 parent 区间与支配；conditional 读取探针将 self Guarded
  义务重绑定为已初始化 local，均不需要 deferred。m8556 还捕获两个普通外层函数。
- 现有 ClassSelfOrder 仍要求空 captures 和 ClosedClassFields；两目标 conditional
  探针分别在 kOwner/kUnsupported 拒绝。initializer tree 用 IncomingCells::kReject，
  static-field destination 禁止 capture/self。现有 class naming 防遮蔽原绑定，不能将
  cell 重绑定为 native inner name。需要内层名字/外层 cell、重入、所有写入及静态
  初始化事件的跨层对应证明，按本轮明确停止规则未擅自实现。无新前提申请。
- 两个首因不变：SemanticProofError::kInternalValue > InternalValueReason=6，
  m5040@12、m8556@13。完整构建通过；固定集串行 **65/65**；class/self/initializer/
  capture 定向 **14/15**，仅已登记的 unguarded_captured_receiver_source_test 失败；
  CLI/corpus/failure/receipt/manifest 扫描 **72/73**，仅已登记的 family_source_cli_test。
- 重跑两组探索对 explore75/explore75s：旧 flags **521/620**、source **567/620**；
  **0 manifest 变化、0 状态变化、0 完整或不完整源码/扩展名漂移**。无新 complete，
  新输出 Node 检查不适用。没有实施新投影，未把临时 probe 当作 Node 差分或规模验收。
- 原始指令、probe、构建/测试日志和逐记录比较在 `/private/tmp/abcd-round76/`。
  未跑全套测试、留出集或 RSS 基准；独立只读复核支持停止结论，仍待 cc 审查。

## ISA 覆盖增量 0：实测台账（待 cc 审查）

- 生产代码未改；复用现有 `LiftSemanticInstruction`，逐指令测量不在首个拒绝后停止。
  test-only probe、流式 JSONL 汇总、空 oracle registry 和一致性测试已加入；详见
  [332 变体矩阵](isa-coverage.md)及[机器数据](../../../tests/isa/coverage.json)。
- 扫描 40 个探索 ABC（620 module records）、5 个配对 ABC、137 个 tests fixture，
  共 934,840 条指令、139 个实际出现的变体。128 个 SupportedUnverified，
  204 个 Unmeasured；未导入指令级 oracle，SupportedVerified 为 0。
- V4 三个 unsupported 首拒绝均实际解码为 APPLY：o7/17736 m18306@379、
  o31/10300 m10718@56、o31/28731 m29198@100。旧 flags 另有 SendableTest 的契约差异。
- 完整构建通过；新增测试加 catalog 3/3；固定集串行 65/65；指定 sweep 78/79，
  唯一失败为已登记的 family_source_cli_test（owner/origin arena）。没有更新旧测试期望。
- 探索输出对现存 Round83 after-old/after-source 比较：3,635 / 3,776 个文件
  （包括 manifest）逐字节相同，无缺失；仍为 526/620、573/620。
  原始及关联 JSONL、生产回执在 `/private/tmp/isa-coverage-full/`；构建/测试日志为
  `/tmp/isa-{full-build-final,new-tests-final,fixed,sweep}.log`。未 commit/push/stash。

## ISA 覆盖增量 0：实测台账（cc 审查通过）

- 用户目标（2026-10-19）：覆盖 isa.yaml 全部指令的反编译，以构造样本或用例验证。设计获批：profile+变体为键的矩阵、逐 occurrence 证据、经生产 lifter 逐指令测量、方法完成不等于逐指令语义验证、oracle 分层、拒绝不算支持、deprecated 不推定不可执行、六类分类含 Unmeasured。增量 0：`tests/support/isa/coverage_probe.cc`（直接调用现有 LiftSemanticInstruction，无生产改动）、`tests/isa/coverage_matrix.py`、`isa_coverage_test`。扫描 182 个 ABC、934,840 条指令：332 个变体中 139 个有 occurrence；128 个 SupportedUnverified、204 个 Unmeasured，其余各类为 0；47 个 deprecated 均未出现。V4 当前三条 unsupported 首拒绝全部是 APPLY_IMM8_V8_V8。矩阵见 isa-coverage.md（完整 JSON 为生成物，放在不纳入版本控制的 build/isa-coverage/）。
- cc 独立审查（基线 c112ecae）：构建通过；固定集串行 65/65；两组输出逐字节相同（526/620、573/620）；isa_coverage/isa_catalog 测试通过。`isa_pin_test` 失败原因是嵌套 cmake 配置找不到 pinned Ruby（未传 Ruby_EXECUTABLE），属既有环境问题，与本轮无关。

## Round 83：DeepTypeUtils m39263 的定位诊断（cc 审查通过）

- 拒绝定位为 `EvaluationError::kOwnership > kProtocolContext > kMissingInput`（op60、PC145、Value49）：方法同时含 for-in 与 for-of，for-in 分支重验证 evaluation 时没有传入 ForOfEvaluationInput。临时补齐后又暴露 for-in emission view 仍假定透明 handler 有物理 try，以及 for-in acquisition 的重抛 handler（op11/12/13）被 ForInStmt 与 ForOfStmt 重复认领；需要统一透明转发入口的协议所有者，跨 evaluation、树与 recovery，本轮只交付诊断与拒绝证据测试。
- cc 独立审查（基线 66118ddf）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 输出逐字节相同；`--language-source` 只有 o32 的诊断变化；526/620、573/620 不变。

## Round 82：ProtocolOwned 投影下的 phased selection 绑定（cc 审查通过）

- m39073 op85/PC209 的 kPrefixArms 是 round 63 已支持的 phased retained selection（`typeof key === "symbol" || !new RegExp(...).test(key)` 的布尔物化），但 ProtocolOwned 投影压缩了控制节点，FindPhasedSelections 把控制块当成原始 phase 块而丢失了已证明的选择。现在选择绑定显式使用 ExceptionControl 的 PhaseControlUnit 映射（header、arms、join、condition paths），TreeFlow 独立重建；没有新增准入。DeepTypeUtils 推进到 m39263 的 `kSemantics > EvaluationError::kOwnership > kPlan`。
- cc 独立审查（基线 31687603）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 输出逐字节相同；`--language-source` 只有 o32 的 DeepTypeUtils 首拒绝变化；526/620、573/620 不变；扫描只有既有失败（family_source_cli_test、decision_snapshots_source_test）。

## Round 81：透明转发 try 与 for-of 协议控制共存（cc 审查通过）

- 复用 AnalyzeExceptionForwarding，新增类型化 `kForwardedTry`：逐入口证明相同 payload、相同最终目的地、路径终止且与 close 子图不重叠才接受；seal、topology、TreeFlow 与 recovery 分别复核；只有获证的 handler 不发射源码 try，其他普通 try 继续拒绝。结果：m23563、m23590、m39263、m60524 完整通过（record 仍被其他方法阻断）；m61486/m61706 转为嵌套 kNested；DeepTypeUtils 推进到 m39073 的 `kDecisionPrefix > kPrefixArms`（op85、PC209）。ComponentFactory 重编译输出的 m425 现在通过完整 ownership seal，证明此前是表示缺口。
- cc 独立审查（基线 1c12589b）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 输出逐字节相同；`--language-source` 只有 o32 记录 38853 的首拒绝变化；526/620、573/620 不变；扫描只有既有的 family_source_cli_test 失败。

## Round 80：for-of 全量诊断（cc 审查通过）

- 扫描 40 个输入、15,673 个方法，33 个含同步 acquisition 的方法、49 个 acquisition、12 个 record（for-of-round80.md）。分组：9 个 evaluation 已通过（record 被其他方法或后续分析阻断）、9 个 async 原始/规范化图对应、6 个“透明转发的普通 try 与协议控制共存”（含 DeepTypeUtils m39263 与 ComponentFactory 重编译输出：seal 与 topology 都拒绝任何未拥有的活跃普通 try，需把转发异常对应贯通 topology/TreeFlow/recovery）、3 个 close ownership、3 个输入值域、2 个嵌套 completion、1 个 driver 布局。没有可独立闭合的局部修复。
- cc 独立审查（基线 ce65e686）：构建通过；固定集串行 65/65；两组输出与基线逐字节相同（526/620、573/620）。

## Round 79 III：原生 for-of 首次发布（cc 审查通过）

- OriginalFlow/RecoveryFlow 加入 for-of 事件端口（acquire、cached-next read、next call、object check、done/value、成功边绑定提交、effectful close 端口，close outcome 保持 kind/payload/target 相关）；内部 iterator/next/result/close 值不获得源码变量或伪造 ExprId；初始联合验证通过才解除 kForOfRecovery，每次 recovery 投影重新验证。ComponentFactory（o20/6952）在 `--language-source` 下完成：`for ($key4 of this.tabsInfo.keys()) { $value1.push($key4); continue; }`；cc 对照原源码 `for (let keysElement of keys) { array.push(keysElement); }`：无闭包捕获循环变量，函数级 let 的赋值形式等价，尾部 continue 属可读性问题。新增 `for_of_source_cli_test`（引擎输出的 Node 差分）。重编译：小型 fixture 往返通过完整证明；ComponentFactory 实际输出重编译后 close 摘要义务通过，但完整 ProtocolOwnership seal 报 kOwnership（未扩展 B 的准入）。
- cc 独立审查（基线为 9269e30c 构建）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 输出逐字节相同（526/620）；`--language-source` 572 → **573/620**，唯一差异是 ComponentFactory 的新源码与 manifest；扫描只有既有的 family_source_cli_test 失败。

## Round 79 II-b：for-of 生产草稿与 TreeFlow（cc 审查通过）

- 投影 `ForOfEmissionView` 保留原 LoopId、header、成功/耗尽边与 completion 端口（不用透明转发）；生产 builder 在 acquisition 处生成未发布的 ForOfStmt 草稿，复用原声明与退出映射；TreeFlow 独立重建视图并检查 RHS、事件归属、success-only 绑定、耗尽边与 close 端口。m7171 的门禁由 kForOfCorrespondence 移到 `kForOfRecovery`（Op38、PC93）。显式 break/return 的新 fixture 在 close 证书处被 kRegionCoverage（TryId4、PC133）拒绝，保留为严格负例。
- cc 独立审查（会话重启后 scratchpad 被清空，审查工具迁到不纳入版本控制的 `build/review/`，以 a0b11c6a 构建基线二进制）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 两组输出逐字节相同，`--language-source` 只有 o20 记录 6952 的门禁变化（TreeObligation 16 → 17）；526/620、572/620 不变；扫描只有既有的 family_source_cli_test 失败。

## Round 79 II-a：ForOfStmt 语法、结构与打印（cc 审查通过）

- `ForOfStmt` 持有 iterable、原声明的赋值目标、body 与 label（per-iteration let 仍拒绝）；遍历、作用域/目标检查、嵌套 break/continue 标签；打印 `for (target of rhs) body`（逗号 RHS 加括号，`async` 目标加括号）；RecoveryFlow 仍明确拒绝该节点，生产 builder 不变，m7171 仍为 kForOfCorrespondence。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变。

## Round 79 增量 C（部分 4）：for-of 生产 evaluation 接线（cc 审查通过）

- PhasedBody 持有 sealed ownership 与匹配的 MethodBody，经借用 view 传入两条 phased 调用链的公共 evaluation 构造；在 temporary/phi 规划前接入 protocol disposition（内部值不成为符号或临时变量，成功边原始值提交作为绑定来源，body use 走普通规划）；独立 verifier 重算全部证据。tree 新增 `kForOfCorrespondence` 门禁。结果：m7171（ComponentFactory）到达树门禁；其余 3 个暴露原先被遮蔽的真实下一层：TimeUtils 为 literal staged acyclic，DeepTypeUtils 的另一方法 m39263 也有 for-of close（kMultipleExits），assertDeepEquals 的另一方法 m40670 为 control duplicate。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；扫描通过（只有既有的 family_source_cli_test 失败）。

## Round 79 增量 C（部分 3）：动态引用证明与 ForOfEvents（cc 审查通过）

- UseId 级动态引用证据：沿 SSA copy/phi 的原始入边回溯到 acquisition 或 cached-next 的正常 Commit，跨越新 acquisition 的旧定义拒绝；独立 verifier 反向重算。`ForOfEvents`：acquire、next-read/call、object-check、done-read/decision、value-read 的类型化事件表，保留 Before/Commit、原始节点、异常出口、成功边限定的原始值提交与 B 的 completion 端口。4 个目标 standalone 均通过；公共 EvaluationPlan 接线、内部值 disposition 与树门禁待续，生产叶子不变。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变。

## Round 79 增量 C（部分 2）：ForOfIterationProof（cc 审查通过）

- 在 B 的投影控制图上证明每轮恰好一次 advance、done/value 顺序、回边与 continue；key 提交保留真实 SSA 定义、存储身份与受影响 phi 的输入，值身份从本次 value-read 落地。4 个目标的 prover/verifier 均通过，生产首因不变。待续：callee/receiver 跨激活的动态身份、源码绑定/TDZ/live-out、完整事件表与公共 planner 接线。新 live-out fixture 在 B 处被严格拒绝（kLiveOut、kBodyDispatch）。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变。

## Round 79 增量 C（部分）：ForOfIdentityIndex（cc 审查通过）

- 类型化的 for-of 身份/成员/用法索引：复核 B 的控制对应，使用共享 SCC 身份与异常 CFG 支配关系，检查 iterator、cached next、advance result 的封闭使用及 ObjectCheck/done/value 顺序；明确不是 evaluation 准入证书。4 个目标的索引与重算一致，生产叶子不变（kProtocolEvaluationPending）。每轮 advance 次数、success-only 绑定、事件/端口对应与公共 evaluation 接线待续。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变。

## Round 79 增量 B：ProtocolOwned 控制拓扑投影（cc 审查通过）

- 只有整方法所有 close 义务独立通过才取得 `VerifiedProtocolRegionOwnership`（V4，确定性入口）；区域角色 OrdinaryTry/Inactive/ProtocolOwned；循环主体保留原始 phase 身份，close 子图以入口限定的 ProtocolCloseUnit 与 completion 端口表示；合成节点用 ProtocolClose 来源类型，不伪造 PC；VerifyExceptionControl 复验原始 close 证明、覆盖、端口、角色与边对应。4 个目标（m28799、m7171、m39073、m40502）越过 kMultipleExits，停在专用门禁 `kEvaluation > kProtocolOperand > kProtocolEvaluationPending`；m47128 整体走原路径。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；扫描 83/84，只有既有的 family_source_cli_test 失败。

## Round 79 A3i：body-protection 的角色拆分；close 证书在 4 个方法上完整成立（cc 审查通过）

- 按 Round 57 设计（循环骨架保留原控制身份）拆分 typed 证书：`kCloseCode`（内层 return-protection，检查成员覆盖）与 `kLoopBody`（外层 wrapper 只拥有 dispatch 边与 handler/close 子图，正文节点、binding transport、回边保留原始身份）；正文从同一 advance-result 的 value 读取与原始 CFG 推导，独立复核。V4 下 close ownership 全部 verified：o5 m28799、o20 m7171、o32 m39073、o32 m40502，以及 m47128 第一处 close；m47128 第二处为 `kBodyDispatch @717`（环境恢复 handler 转发），m42717 嵌套与 m23306 async 入口保持门禁。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试全部通过。

## Round 79 A3h（部分）：close 区域边界与私有状态（cc 审查通过）

- 6 个 `kExternalEntry` 见证均为原始 CFG 中不可达的 ObjectCheck 正常 Commit 边，经可达性证明排除（不是放宽）。`close_boundary`：从 completion relation 导出携带 completion/outcome 的端口，检查全部入边/出边与私有状态 live-out（acc 与寄存器，cell-backed 状态仍拒绝），用户 try 必须核对真实 dispatch/Catch 角色。5 个方法的剩余缺口均为 `kRegionCoverage`：外层 body-protection 范围包含 close 成员集之外的普通循环正文。fixture 中发现一处真实的私有状态 live-out（PC253），该路径保持拒绝。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试全部通过。

## Round 79 A3g（部分）：事件覆盖与真实方法 probe（cc 审查通过）

- 事件覆盖事实（对所有到达状态取合取）与独立复核；用其细化成员分类。7 个真实方法在 V4 下：5 个优先级 verified，ownership 首个缺口都是成员边界上的 `kExternalEntry` 见证（还需完整的区域边界证明来判定是否为合法端口）；m42717 嵌套入口推导停在 `kContinuation`（PC692）；async m23306 停在 `kExternalEntry`（原始 PC99/工作 PC96）。私有状态 live-out、用户 try 排除与嵌套传播待续。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试全部通过。

## Round 79 A3f：外部异常不携带 Hole 的前提（V4，cc 审查通过）

- 用户 2026-10-16 批准独立的运行时前提“外部异常不携带内部 Hole”（Round 79 A3e 已从 pinned runtime 证明 callee 的 `ldhole; throw` 可以跨帧送达 catch）。新增 `kLanguageSourceV4` 与 `ExternalExceptionNoHole()`，`--language-source` 选择 V4；只适用于调用/getter/builtin/native/rejection/resume 来源，本方法自身的显式 throw 仍逐项证明。`catch_payload_domain` 计算逐 catch 的 payload 域事实，只凭该事实细化 close 哨兵的 Hole 分支。m423 Break/Return 的优先级在 V4 下 verified，在默认与 V1–V3 下仍为 missing；ownership 仍为 missing。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变（`--language-source` 的契约标签变为 V4）；扫描 79/80，只有既有的 family_source_cli_test 失败。

## Round 79 A3d：不抛出的比较与哨兵路径（cc 审查通过）

- 路径限定的 `NonThrowingComparison`：由 SSA 值流证明操作数为 undefined/null 时，EQ/STRICTEQ/NOTEQ/STRICTNOTEQ 不抛出（依据 pinned 解释器 FastEqual），裁剪其异常边；Hole 判断是保存 close error 的哨兵，正常路径两侧均为 Hole 时裁剪虚假分支。m423 Break/Return 的剩余见证后移到 PC139/230：Catch payload 是否可能为 Hole 尚无证明（SetException 不排除 Hole），未引入假定。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试全部通过。

## Round 79 A3c：按关闭结果索引的 completion（cc 审查通过）

- `close_outcome_relation`：传播状态同时保存入口、SSA 环境、completion 与 outcome（成功对象、缺失/nullish、getter 抛出、call 抛出、primitive 结果），getter/call 异常保留各自原始出口身份；builder 与 verifier 用相反工作列表顺序独立计算；去掉 outcome 后与 A2 may 关系完全一致。m423 Break/Return 入口五类 outcome 全部出现，优先级仍为 missing：nullish 比较（PC114/205）保留保守的异常边（`kOtherException`），Hole sentinel 路径也保留。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 5/5。

## Round 79 A3b（部分）：close 摘要与 return 引用（cc 审查通过）

- `close_completion`：重新派生入口，记录原始成员与边，核对 return 引用（base/callee/receiver）与 Throw payload；Throw 入口的优先级 verified（m423 6/6、m451 1/1）。缺口：completion_relation 不记录关闭结果（成功、getter 失败、call 失败、primitive 结果），相同的 Break/Return 出口元组无法区分正常成功与吞掉 close error，因此非 Throw 入口的优先级保持 missing；ownership、嵌套关联待续。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 4/4。

## Round 79 A3a：从原始代码推导 close 入口（cc 审查通过）

- `close_entries`：从 acquisition、cached-next、done 分支、私有 closed-state 与原始 continuation 推导 close 入口及其 PendingCompletion（Break 目标来自真实 exhaustion 边，Return payload 为原始 SSA，Throw 为每条入边的当前异常），再送入 A2，API 不接受调用方给定的 completion；独立 verifier 以反向支配查询发现 guard。真实 fixture：m423 Break/Return/Throw、m451 Throw 的入口全部推导正确。仍不授予 ownership 与准入。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 6/6。

## Round 79 A2：相关联的 completion 关系（cc 审查通过）

- `completion_relation`：在原始 ExceptionFlow/SSA 与 RawEventGraph 上传播完整的 PendingCompletion{kind,payload,target} 元组（不拆成独立集合），处理正常 Commit、异常路径、Catch、同时 phi 与嵌套的当前 completion；预付持久化 SSA 环境，预算耗尽时拒绝整个查询；独立 verifier 重算。真实 fixture：Break close 7 个出口元组、Return close 7 个、Throw close 2 个。保守的 may 关系；尚未接入生产，不授予拓扑所有权。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变；相关测试 5/5。

## Round 79 A1：共享原始事件图（cc 审查通过）

- for-of 轨道恢复（Round 79 设计：PendingCompletion{kind,payload,target} 相关追踪，五个门禁增量 A/B/C/II/III，最终上界 6）。增量 A 一次过大，拆片；A1 只做纯重构：把 await_catch_flow 私有的原始事件图（节点、寄存器效果、真实 dispatch、后继、预付 arena）提取为 `raw_event_graph`，接口不依赖 AwaitRegion，coroutine 边作为可选扩展。新增 `raw_event_graph_test`。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；526/620、572/620 不变，0 状态变化，0 文本变化；await/raw_event 测试 3/3。

## Round 78：await catch 输入的原始 CFG 活跃性证明（cc 审查通过）

- await_catch_flow：原始 Before/Commit/Catch/Resume/CallerExit 事件图、预付稀疏闭包、独立验证器；只有正常 Commit 能覆盖旧定义；读优先于写；资源耗尽返回拒绝；只补足快速证明的 kUnprovedPaths。o32 越过原拒绝，转为 effectful close 的 kMultipleExits（for-of 暂停轨道）。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 524 → **526/620**，`--language-source` 570 → **572/620**（o9 RdbDataPersistence、o39 WebSaveImage）；0 回退，0 文本漂移；扫描 75/76，只有既有的 family_source_cli_test 失败。

## Round 77 增量 3：captured default 的 recovery 与发布（cc 审查通过）

- recovery 加入实参入口 → undefined 分支 → provided/missing 提交 → initialized-parameter 定义；只有 missing 路径执行签名表达式，不伪造 ExprId；parameter_recovery 独立重建。kCellParameterRecovery 门禁只对通过验证的方法解除。DevEco es2abc 往返核对 length 2、条件初始化、外层与局部 cell 分离、闭包目标与 HoleGuard。
- cc 独立审查（增量 3）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521 → **524/620**，`--language-source` 567 → **570/620**（o10 Index、o31 FlowItemNode、o31 SwiperView；签名形如 `(elmtId, isInitialRender, data = __data__) => {...}`）；0 回退，0 文本漂移；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 77 增量 2：captured default 的签名与家族贯通（cc 审查通过）

- 联合残余体投影（签名拥有迁移的 read/guard/写回与异常出口，`InitializedParameterCommit` 记录残余初始 store 消费的形参）；SignatureCell 按原始 read site 查找外层存储；签名读取携带 Guarded 义务并入既有格；DefaultSignatureVerify 独立复核。首因前移为 `DefaultSignatureError::kCellParameterRecovery`；recovery 的条件参数事件链留给增量 3。
- cc 独立审查（增量 2）：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 77 增量 1：CellParameterDefaultProof（cc 审查通过）

- 密封的联合证明：按参数/cell 数据流发现，独立 verifier 重建索引，核对两条参数选择路径、两个不同存储、guarded 外层读取与异常出口、私有初始化区、body 入口状态（参数测试必须支配 body 汇合点）。3 个目标均 verified，发布不变（仍为 kLength），证明状态作为诊断发布。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 74/75，只有既有的 family_source_cli_test 失败。

## Round 75：Evaluation 输入失败证据（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、567/620 不变，0 状态变化，0 文本变化；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `4e3e9be9`；保留 HoleProtocolError / CellEvaluationError / LiteralError 和
  missing-shape 叶子及固定大小的 check/op/use/value 证据，builder、verifier、两条 JSON
  路径和 `abc_semantic --record-source` 贯通。原枚举、资源分类、准入和预算不变。
- o8/42040 m42490、o28/41167 m41622、source 下 o15/20252 m20697 均为
  `LiteralError::kDefinition > kLiteralStagedStorage`，allocation op17，原始/工作 PC47。
  跨选择的数组初始化需要保留 indexed [[Define]] 的事件及树/恢复对应；现有暂存路径
  只证明 buffered object 的已有属性赋值，不是消费登记的一处局部遗漏。
- o24/37520 m38333 为 `LiteralError::kFlow > kLiteralStagedAcyclic`：当前检查整个 CFG
  无环，故只有方法位置，没有虚构 PC。需要构造区间的 activation/escape/order 证明。
  按本轮停止条件未实施准入修复。设计与逐目标证据见
  [literal-initialization.md](literal-initialization.md#round-75-evaluation-input-refusal-evidence-2026-10-15)。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增与直接相关 **5/5**。
  evaluation/literal/cell/hole/tree-failure/source-route/logical-coverage 定向 **47/54**，
  失败仅已登记的七项 literal 测试，未弱化断言。新测试覆盖两条 JSON、真实 CLI、
  PC0/缺失/规范化位置、资源分类、伪造证明、copy/register/block 变体、200/201/400/800
  和精确 byte/work 边界。
- CLI/corpus/failure/receipt/manifest 扫描最终 **72/73**，仅已登记的
  family_source_cli_test 失败。首轮另发现 guarded_captures 的 branch 旧期望缺少新增的
  `EvaluationShapeError::kMissingShape`；补全精确原因链后重跑整个扫描通过该项，
  拒绝状态、禁止发布源码和 TDZ 变异断言均保留。
- 对 explore74c/explore74cs：旧 flags **521/620**、source **567/620**；**0 状态变化、
  0 完整或不完整源码/扩展名漂移**。manifest 只有上述目标的 3/4 条原因证据变化；
  o15 旧 flags 仍为 ModuleEnclosureError=10。无新 complete，新增输出的 Node 检查不适用。
- 日志与逐记录比较：`/private/tmp/abcd-round75/`。未运行全套测试、留出集或 RSS 基准；
  未 commit/push/stash。没有新增源语言前提。

## Round 74 增量 III/IV：Sendable 模块域与 shared 发布（cc 审查通过）
- cc 独立审查（增量 III/IV）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 566 → **567/620**（LruCache.ets：首行 `"use shared"`、经真实 ArkTSUtils import 拼写的 GENERIC 字段类型、构造器首条 `"use sendable"`、async 成员内闭包捕获自身的 this/key/value）；0 回退，0 文本漂移；扫描 76/77，只有既有的 family_source_cli_test 失败。

- 基线 `646fb819`；工作区实现，未 commit/push/stash。III 和 IV 均已接入。
- raw/Value/树保留独立 `ModuleDomain`；imm8/wide Sendable external read 保持可抛出、
  正常 acc 提交和 hole guard，没有伪造 initialized。创建上下文、guard、enclosure 与
  recovery 复验域及 import 身份。成员内普通箭头保留其自身捕获环境，外层根捕获仍拒绝。
- `SendableTypeSpelling` 从真实 import binding 与原构造器 [[Set]]/new 路径导出 GENERIC32
  的可重编译拼写（含直接 import 的零深度路径），独立重算；没有新增 import/初始化，
  不声称恢复唯一原始名义类型。未知或歧义拼写仍 `kTypeSpelling`。
- `SharedModuleProof` 独立证明 shared kind/模块表，未冒充 OrdinarySynchronous；完整
  入口恢复仍检查 class/construct/put/export 效果。模块首行 directive 来自该证明；部分
  shared 输出不能发布。新记录 `.ets`/arkts，所有既有输出扩展名不变。
- 最终完整构建成功；固定集串行 **65/65**。新增三项测试 **3/3**，覆盖真实 raw/SSA/tree
  模块域、guard/异常与提交、类型/绑定/owner 变异、shared 导出身份、真实编译往返及
  Node 普通调用差分、200/201/400/800 与精确 byte/work 边界。
- 定向正则 `module|sendable|shared|async|capture|concurrent|source_route|class_tree|recovery`：
  **105/113**；失败仅已登记的 class_tree_source_test、六项 captured_receiver 变体和
  async_semantics_test。`cli|corpus|failure|_receipt|manifest` 扫描 **71/72**，仅已登记的
  family_source_cli_test 失败；更新 LruCache 旧期望后的 tree_failure_cli_test 通过。
- 对 explore74b/explore74bs：旧 flags **521/620**，source **566→567/620**；唯一 manifest
  变化是 o14/24892 LruCache complete，**0 回退、0 既有源码/扩展名/位置文件漂移**。
- 实际 DevEco es2abc `--module --merge-abc --extension=ts --opt-level=0` 往返匹配：
  shared=true、非 CJS/TLA、request/import/export、Sendable class/length0、lock_/32、books_/32、
  instanceCount2、三 async 成员/回调、无额外 sendable env、两次 guarded Sendable slot0 read、
  两次 [[Set]]/四次 put/导出 slot0 顺序及效果事件的 handler 分派。初始化捕获的冗余
  HoleGuard 原始数量不要求相等；其可用性仍由 cell/recovery 证明。Node 不验证共享堆、
  字段类型检查、真实锁与跨线程行为。
- 仍保留静态初始化器、继承、未证明 GENERIC、Sendable local/lazy load、普通内层箭头直接
  读取 Sendable import 的继承域等门禁。ModuleCapturePrefix 没有放宽。
- 更新过期测试：sendable_metadata 的 V3 policy/目标结果，以及 tree_failure 中 LruCache
  的 source 契约 complete 结果；旧契约拒绝继续严格断言。没有修改既有失败测试。
- 日志、逐 record 比较与目标往返报告：`/private/tmp/abcd-round74iii/`。未运行全套测试、
  全语料、留出集或 RSS 基准。独立只读生产审查未发现阻断，不能替代 cc 最终审查。

## Round 74 增量 II：闭合 Sendable 实例类（cc 审查通过）
- cc 独立审查（增量 II）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 565 → **566/620**（Sendable.ets：`taskId: number;` + 构造器首条 `"use sendable"` + 两次原始 [[Set]]）；0 回退，0 文本漂移；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `201e667f`。未 commit/push/stash。V3 的 `SendableOutput()` 现在为
  `kArkDirectiveV1`，V1/V2/其他契约和 `SharedModuleOutput()` 保持原义。
  普通类路径复用原 ClassTree/family，创建种类贯通原始/值/树对应；构造器属性
  绑定实际创建点和池身份。字段为无 initializer 的 primitive/union 存储描述，
  原 [[Set]] 保留；只有实际新发布的 Sendable record 改用 `.ets` / arkts。
- 文件级 Sendable literal 索引按实体共享，创建者复用既有不可变输入，不逐 creation
  重解码整方法；字段 key 实体排序去重，所有缓存、共享控制块和字符串先计费。
  字段解码的 mask/split/key 拒绝缓存为所属类的类型化失败；资源/work 仍归索引预算。
  无可用 TS 模块名拼写返回记录级 kBinding，不伪造指令 PC。静态初始化器、外部
  capture/module 环境、heritage、未恢复成员/前缀、NONE/TYPE_REF/GENERIC 继续门禁。
- 最终完整 `cmake --build build/regression -j 6 -- -k` 成功；新增两项及 Sendable
  相关共 **5/5**。覆盖真实生产准备/完成、错误 creator/pool/kind/mask/member/directive、
  部分恢复/环境/capture、malformed mask 的同文件隔离、模块拼写拒绝、63 种 primitive
  union、200/201/400/800 和精确 byte/work 边界、实际源码发射/重编译与 Node 普通调用。
- 最终固定集串行 **65/65**。期间一次 rich_text worker 未正常返回（约 67 秒），
  单独复跑 **1/1**（13.43 秒），随后完整固定集 **65/65**，未改代码/断言。
  相关定向 **28/30**，仅既有 `tree_structure_test` abort 和 `class_tree_source_test`
  匿名 class 正则期望；CLI/corpus/failure/receipt/manifest 扫查 **70/71**，仅既有
  `family_source_cli_test` owner/origin arena 失败，均未弱化。
- 最终对 explore74/explore74s：旧 flags **521/620** 不变，source **565→566/620**，
  仅 SendableTest（o14/28934）complete；**0 回退、0 既有文本/扩展名/位置文件漂移**。
  原合同仍在 m29066@9 报 SemanticError::kUnsupportedInstruction。
- 真实 `.ets` 按原 record 名用 DevEco `--module --merge-abc --extension=ts --opt-level=0`
  重编译：shared=false；definesendableclass；constructor serialized kind NONE、sendable=false、
  length=1、无 heritage；getTaskId；taskId/NUMBER(1)/instanceCount1；整 record 无额外
  sendable-env；两次有序 taskId [[Set]]；export slot0 与模块表匹配。编译器 hash、
  fixture 命令与 oracle 边界见 [fixture README](../../../tests/integration/fixtures/sendable_source/README.md)。
  Node 不验证共享堆、类型检查或跨线程行为；这些仍是 runtime gap。
- 本轮日志/比较/往返报告位于 `/private/tmp/abcd-round74ii/`，没有跑全套、全语料、
  RSS 基准或留出集。没有改动用户的 sample.tar.gz。

## Round 74 增量 I：Sendable/shared 元数据（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、565/620 不变，0 状态变化，0 文本变化；扫描 71/72，只有既有的 family_source_cli_test 失败。

- 基线 `544e0854`。本片只交付增量 I，**未开始增量 II**；没有 commit/push/stash。
  raw `ClassCreation` 与 `ValueClassCreation` 保留 ordinary/sendable kind；按实际创建种类
  选择 literal 格式，独立保留成员缓冲、字段直接引用、字段顺序、mask 和实例/静态分界。
  实例字段使用独立的 `SendableFieldPlacement::kInstance`，不会混作 prototype 成员。
- 原始 occurrence 的区域池、constructor/member owner、字段 key 都被复核；元数据验证器
  从不可变输入重算并返回类型化失败。shared 的 absent/false/true 属性与真实 owner/module
  表一起保留，仍不构成 `OrdinarySynchronous`。两个 V3 输出政策仍 disabled，语义和类准备
  门禁保持；原首因和资源分类不变。两条 JSON 路径、receipt 与 abc_semantic 增加固定大小证据。
- 完整 `cmake --build build/regression -j 6 -- -k` **成功**；新增三项及修改的原始类/value
  测试 **5/5**；固定集 `ctest --preset source-regression -j 1` **65/65**；定向 **24/25**，
  仅已登记的 `class_tree_source_test`（匿名 class 正则期望）；CLI/corpus/failure/receipt/manifest
  扫查 **69/70**，仅已登记的 `family_source_cli_test`。未弱化这两项断言。
- 对 explore73c/explore73cs：**521/620、565/620**，0 状态变化、0 既有源码或扩展名变化；
  已 complete 的结构产物也逐字节相同。每组只有 o14/28934、o14/24892 的 manifest 新增诊断，
  原有字段和首因不变。前者确认 fields29289=taskId/NUMBER1；后者 shared/module25407，
  独立 metadata probe 确认 fields25544=lock_/GENERIC32、books_/GENERIC32。
- 真实 DevEco fixtures 已编译并记录 compiler/fixture SHA-256；包含 metadata/owner/pool/field
  变异、JSON 所有原因类型、PC0/缺失 PC、200/201/400/800 和真实 byte/work 准入边界。
  这不是引擎输出 Node 差分或目标重编译：这些仍是增量 II 的未完成验收，不能当作通过。
- 证据：`/private/tmp/abcd-round74/{final-build.log,new-tests.log,acceptance.json,cohort-comparison.json}`。
  设计与边界见 [Sendable/shared](sendable-shared.md)。

## Round 73 增量 III：async concurrent（cc 审查通过）
- cc 独立审查（增量 III）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 564 → **565/620**（GetRecentList：`async function(key, value) { "use concurrent"; await lruCache.put(key, value); }`）；0 回退，0 文本漂移；扫描 73/74，只有既有的 family_source_cli_test 失败。

- 基线 `b2d0be5e`，未 commit/push/stash。原始 typed AsyncEnter 选择 async owner；闭合后缀的反向值流证明 resolve/reject → notify → return，通知与返回均使用成功 settlement 的 Promise acc。复制、跳转及分块不依赖相邻模板；每条通知/返回都被唯一端口覆盖。复用原 async owner 证明状态、await/resume、隐式拒绝和用户 handler。不可变原始证据及共享控制块按实际分配预扣，TreeFlow/recovery 重放证明并核对原始/工作来源。
- o14/10719 GetRecentList 完整：m11357 updateBooks 输出 async function，`"use concurrent";` 为首条语句。实际输出直接用 DevEco es2abc 重编译，保持 concurrent kind、sendable=false、async 协议、存在的请求注解 [1]、完整模块表；成功和隐式拒绝各一条 Promise 通知端口。没有改写往返输入或插入假模块读取。
- 对 explore73b/explore73bs：**521/620、565/620**（source +1），0 回退、0 既有源码字节变化。旧 flags 的 manifest 无变化；source 只 o14/10719 改变。目标 CLI/manifest 源码一致，通过 node --check。
- 最终完整 keep-going 构建成功；固定集串行 **65/65**；新增 **2/2**，并发相关 **9/9**。async/concurrent/control-composition/recovery/tree-structure/source-route 定向 **26/28**，仅已登记的 tree_structure_test 和 async_semantics_test 失败；CLI/corpus/failure/receipt/manifest 扫描 **68/69**，仅已登记的 family_source_cli_test owner/origin arena 断言失败。没有弱化这三项断言。
- 真实六函数 fixture 对实际导出执行 Node 普通调用差分：解析值、拒绝类型、微任务轨迹、前后抛出、返回 Promise/thenable、catch-around-await；直接重编译复核方法属性/请求/模块表/两类完成端口。通知变异、200/201/400/800 规模及 bytes/work 边界通过。finally-around-await 夹具保留既有 `AsyncCompletionCheck::kCatchInput > AwaitCatchError` 拒绝，仅验证其原始通知端口，未声称 engine-output 差分通过。taskpool/跨线程仍未验证。
- o14/16446 未修复：m16784 的 PhasedMethodError::kEnvironment 不变；独立下层分析定位 EnvironmentFlowError::kAmbiguousJoin，原始 PC275 / 工作 PC272。H275 有来自不同环境身份的异常入口：PC59 分配后、PC243 pop / PC244 再分配的路径汇合，现有环境格不合并这些身份。
- 证据入口 `/private/tmp/abcd-r73-iii/acceptance.md`；详见 [language-source.md](language-source.md) 增量 III 与 `tests/integration/fixtures/concurrent_async/README.md`。无留出集、全语料、全套测试或 RSS 基准运行。

## Round 73 增量 II：同步 concurrent 完成投影（cc 审查通过）
- cc 独立审查（增量 II）：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 521/620 不变，`--language-source` 561 → **564/620**（TaskpoolCancel、Mock、Worker）；0 回退，0 文本漂移；cli/corpus/failure/concurrent 扫描 71/72，只有既有的 family_source_cli_test 失败。cc 核对 TaskpoolCancel：`"use concurrent";` 是函数体首条语句，函数表达式形式保留。

- 基线 `7eee8402`；未 commit/push/stash。普通 Function 与 verified directive 分离；原始 notify 保留 acc 和调用帧 owner，有限格完成证明、专用事件端口及 TreeFlow/OriginalFlow/recovery 重算贯通。实际输出引用逐项核对请求注解；普通 outer cell（含转发）仍拒绝，ModuleCapturePrefix 的 concurrent 排除未改。
- 三个 record 完整：o14/18164（cancel/delayed）、25688（query）、31477（parallelTask）。collectFrame m16922 的通知证明通过，但 16446 首拒绝推进至另一方法 m16784 的 `PhasedMethodError::kEnvironment`（创建点 m16700@123；没有虚构子 PC）。10719 的 async m11357 明确保留 `kAsync` 门禁，原始/工作 PC10。
- pinned parser 不接受 quoted export aliases。仅实际发射 verified concurrent 的新 record 改用 IdentifierName；当前 formatter 需字符串拼写的 import/re-export/export 名返回 `kModuleSyntax`，Unicode 夹具验证这一覆盖边界。未改写往返编译输入，未改变无 concurrent 输出的格式。
- 最终完整构建成功；固定集串行 **65/65**；相关定向 **14/14**（并发 **8/8**）；CLI/corpus/failure/receipt/manifest 扫描 **67/68**，仅已登记的 `family_source_cli_test` owner/origin 断言失败，未弱化。新完整输出通过 Node syntax check。三个真实 record 的直接 es2abc 往返及六方法真实夹具通过 kind/sendable/注解/请求顺序/模块表与通知完成核对；Node 只验证普通调用值/异常类型/轨迹，不验证 taskpool/跨线程。
- 相对 explore73/explore73s，结果 **521/620、564/620**，0 回退、0 既有完整文本漂移。旧 flags 的 manifest 无变化；source 仅 o14 的 10719/16446/18164/25688/31477 五个候选变化。无留出集、全语料、RSS 基准或全套测试运行。
- 同步 increment 的边界、编译器身份/命令和测试 oracle 见 [language-source.md](language-source.md) 增量 II 及 `tests/integration/fixtures/concurrent_functions/README.md`。日志、逐字段比较、目标表和完整改动清单在 `/private/tmp/abcd-r73-ii/`；入口 `acceptance.md`。async、concurrent 内 class-body 引用普查，以及一个通知对应多个物理 Return 的端口仍严格拒绝。

## Round 73 增量 I：V3 输出政策与 concurrent 元数据证据（cc 审查通过）

- 用户 2026-10-15 批准保留语义的 ArkTS 形式（concurrent/Sendable/共享模块），apply 展开暂不批准。新增 `kLanguageSourceV3` 与三个独立谓词，只有 ConcurrentOutput 启用。方法级并发模块请求注解解析，区分缺失与空注解；目标细化为 `ConcurrentError::kPreparationGate`。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；521/620、561/620 不变，0 状态变化，0 文本变化（`--language-source` 下契约标签由 V2 变为 V3）；cli/corpus/failure/concurrent 扫描 69/70，只有既有的 family_source_cli_test 失败。

## Round 71：直接 SSA 字符串默认参数（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 520 → **521/620**，`--language-source` 560 → **561/620**（Logger：`constructor(prefix = "", domain = 65280)`）；0 回退，0 文本漂移；cli/corpus/failure 扫描 66/67，只有既有的 family_source_cli_test 失败。

- 基线 `8d8febf9`。直接默认值候选保留原始字符串 pool 身份；独立证明复核常量、结果/phi 使用和参数顺序，再经已有 `SignatureString`、原方法常量池和签名验证贯通。`.length` 等式、参数环境边界、打印器及捕获默认值准入未改。见 [实现和边界](family-defaults.md)。
- 对 `explore69/explore69s`：**520 → 521/620、560 → 561/620**。每组只有 o13/3609 Logger 变化，构造器为 `constructor(prefix = "", domain = 65280)`，从 `FunctionAbiError::kLength` 到 complete；两份新输出均通过 `node --check`。0 回退，0 已 complete/partial 文本漂移，其他 record 的 manifest 条目不变。证据 `/private/tmp/r71/cohort.log`、`/private/tmp/r71/comparison/`。
- 完整构建通过；新增 **2/2**；固定集串行 **65/65**；默认参数/签名/常量池定向 **18/18**；CLI/corpus/failure/receipt/manifest 扫描 **64/65**，仅已登记的 `family_source_cli_test`（owner/origin arena）。未改既有测试期望。命令及日志索引 `/private/tmp/r71/acceptance.json`。
- IR/签名单测覆盖错误 pool、数值伪装为字符串、错误参数/顺序/生产者、错误解析文本、漏默认值及 length 不等；200/201/400/800 检查线性工作量及 byte/work 精确边界。真实 DevEco/es2abc fixture 两契约 **88 次 Node 差分、8 个被检出的变异**，覆盖函数/构造器/箭头、连续默认值、required 前缀、missing/undefined、Unicode 和显式对象转换的顺序/抛出。fixture 编译命令及哈希随 README 保存。
- Round 70 的三个 cell-backed 捕获默认值保持拒绝；无 SSA 合流的已消除字符串默认值、一般效果表达式默认值不在本增量。没有新语言前提。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 69：async 归一化诊断（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；520/620、560/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 63/64，只有既有的 family_source_cli_test 失败。

- 基线 `40f0d0c1`。新增固定大小 `AsyncCompletionFailure` / `AsyncCompletionCheck`，保留既有错误枚举和资源分类；原始/工作 PC 分开，真实 PC0 与缺失位置分开。下层 AwaitRegion/AwaitCatch 原因、handler/register 与路径未证明证据进入 receipt、manifest 两条 JSON 路径和 `abc_semantic`。见 [四目标、根因和停止边界](round-69-async-normalization.md)。
- 四例拆成 3 个 catch-input 未证明路径（m7950@3459 H3537/v25、m23306@99 H131/v13、m22136@75 H348/v11），以及 1 个 promise 创建前闭包实例化（m58418@18）。前三例不等于已证明实际泄漏；需要原始异常 CFG 上的输入定义/提交证明。后一例需要 private instantiation/escape 证明，不能扩展 prelude 操作白名单。按本轮停止条件仅交付步骤 1；没有新 complete，没有 Node 语义修复声明。


- 最终完整构建通过；固定集串行 **65/65**；新增及直接相关 **4/4**；`async|await|coroutine|control_composition|source_route|tree_failure|family_creation_failure` 定向 **25/26**（仅已登记的 async_semantics_test，bad_variant_access）；`cli|corpus|failure|_receipt|manifest` 扫描 **63/64**（仅已登记的 family_source_cli_test，owner/origin arena）。没有更新或弱化既有测试期望。命令、返回码和完整日志见 `/private/tmp/r69/acceptance.json`。
- 对 explore68/explore68s 的最终逐 record 比较：**520/620、560/620** 不变，0 状态变化，0 complete/partial 文本漂移（含 o32/49868）。每组仅四目标的诊断条目变化，其余 record 条目相同。`/private/tmp/r69/final-cohort.log` 和 `/private/tmp/r69/final-cohort/` 保留证据。没有新输出需要 node --check。
- 新测试覆盖实际 PC0/无位置预算失败、try/handler/branch 原始与工作坐标、两条 JSON 路径、所有 36 个 check 的 envelope round-trip、真实四 ABC × 两契约 CLI 一致性。200/201/400/800 的精确 byte 边界为 29896/30044/59496/118696，低一字节保持资源拒绝。只读复核发现的缺失位置误报 PC0 已修正并测试。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 68：正常 class-creation 路径的 Guarded activation（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 516 → **520/620**，`--language-source` 556 → **560/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 62/63，只有既有的 family_source_cli_test 失败。

- 基线 `95ef0813`。新增 Guarded-only 原始支配证据：allocation 支配 creation 和唯一 direct store；Language 写入前提、原作用域/激活、初始化 store 来源仍独立验证。工作 IR 的 Mark/VerifyDeferredCells 共享一次预算内支配计算，FamilyTree 持有 deferred 表的真实 charge。未放宽 SourceAccess/Language/Defined、未知闭包写入或 exceptional-parent deferral；未改声明/控制布局或打印。逐例证据与算法边界见 [Round 68](round-68-capture-activation.md)。
- explore67c/explore67cs 对比：**516 → 520/620、556 → 560/620**；每组恰好四个目标变化，o3/55292、o10/52872、o29/12767、o21/12185 全部 complete。0 状态回退，0 已 complete/partial 文本漂移（含 o32/49868），8 份新输出通过 `node --check`。证据 `/private/tmp/r68/cohort/`、`/private/tmp/r68/cohort.log`。
- 最终完整 `cmake --build build/regression -j 6 -- -k` 通过；固定集串行 **65/65**；新增/直接相关 **6/6**；`capture|deferred|guarded|class|local_initialization|cyclic_cells|scope` 定向 **59/72**（13 项均在既有失败清单）；`cli|corpus|failure|_receipt|manifest` 串行扫描 **61/62**（仅既有 family_source_cli_test）。完整日志 `/private/tmp/r68/acceptance.json`，最终补充边界与新测试 `/private/tmp/r68/boundary-tests.log`、`/private/tmp/r68/final-new.log`，最终构建 `/private/tmp/r68/final-build.log`。
- 新单测覆盖原始/工作 CFG 的非支配 allocation、reset、多 store、伪造 store、严格需求拒绝、copy/拆块、200/201/400/800 和原始局部证明/捕获计划/Mark/Verify 的精确 byte/work 边界。真实 es2abc fixture 两契约 **24 次 Node 差分、8 个变异**；分别检出提前声明丢 TDZ，以及保留早读 ReferenceError 但错误共享循环后续值。消息/stack 不作跨 VM oracle。
- 唯一过期期望更新：guarded_captures 的 branch 仍拒绝且无 source，从 ScopeError=3 推进为完整 `EvaluationError=2 > EvaluationObligation=0` 链；其余负例断言未弱化。独立只读代码复核未发现阻断，补齐了工作支配与预算/activation 反例。不支持的循环环境复制 join 仍按原类型化原因拒绝；无新语言前提。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67：闭合普通后代类生产贯通（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 513 → **516/620**，`--language-source` 552 → **556/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 63/64，只有既有的 family_source_cli_test 失败。输出形如 `(class SocketInfo {...}).prototype;`：es2abc 定义类后会读取 prototype，而类值在该函数中未被使用，所以这是对字节码的忠实投影；可读性另议。

- 基线 `640dd19f`。本次窄切片接通无 creator-cell 需求、无 heritage、无静态初始化器的普通后代类；复用 record-root 的 CreatorClasses / PreparedFamily，真实 occurrence、Storage、CaptureTree、creation OpId 贯通准备与完成。冻结前收集需求；非空/延迟需求保留独立类型化拒绝，不移动声明。完整 owner 范围、嵌套类自由名预留、完成后实际 owner 覆盖、真实 creator 的 FamilyWrites 屏障均独立复验。见 [实现、边界和逐目标表](round-67-closed-descendants.md)。
- 对 explore67b/explore67bs：原 flags **513 → 516/620**，source 契约 **552 → 556/620**；**0 状态回退、0 已 complete/partial 文本漂移**，包括 o32/49868。两组 manifest record 条目仅四个 o18 目标变化。52728、59564、60829 两组均 complete；63861 仅 source 契约 complete，原 flags 的下一叶为 m64229/PC62 的 `CaptureCellError=3 > CaptureError=1 > ScopeError=3`（ordinal2、depth0/slot7）。7 份新增 complete 输出均通过 `node --check`。证据 `/private/tmp/r67c-delivery-cohort/` 和 `/private/tmp/r67c-delivery-cohort.log`。
- 最终完整 keep-going 构建通过；固定集串行 **65/65**；新增/扩展 **8/8**；`class|capture|deferred|guarded|family|creation|recovery` 定向 **72/86**（14 项均为下方既有失败，未改断言）；`cli|corpus|failure|_receipt|manifest` 扫描 **60/61**（仅既有 family_source_cli_test）。验收命令及日志 `/private/tmp/r67c-delivery-acceptance.json`。真实 es2abc fixture 的两契约 Node 差分覆盖 construction、[[Set]]/父 setter、throw、函数/方法 creator、多次 loop 创建、copy/branch 变体和自由全局 `$value0` 与 creator 临时名冲突；[[Define]]/删除 throw 变异检出。CLI/manifest 的 capture/deferred/heritage/nested 拒绝原因与位置一致。
- 200/201/400/800 宽度与普通函数深度的生产发现/准备及精确 byte/work 边界通过。独立整 record 探针：width200/201/400 complete，width800 仍达既有 FamilyEnclosureError::kResourceLimit；depth800 导出 complete，但生成的嵌套表达式耗尽 Node 默认 parser stack，`node --stack_size=8192 --check` 通过（原始函数声明 fixture 默认 stack 可通过）。不把准备阶段证明当作无限宽度/深度的端到端保证，未提高任何 cap。
- 保留范围：capturing/deferred/heritage/static initializer、requirements-only 类成员继续创建类、phased creator；更深层类任务图和声明 fulfilment 尚待后续。无新源语言前提。只读复核未发现阻断；补齐其指出的全局名冲突测试。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67 Increment II 基础 WIP（cc 审查通过；生产贯通待续）
- cc 独立审查（增量 II 部分）：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 60/61，只有既有的 family_source_cli_test 失败。

- 基线 `97ca420a`。新增 requirements-only 类成员准备状态及禁止 Finish 的类型化门禁；按 occurrence 汇总类成员家族需求，交给既有 bottom-up 格处理；CellPlan 支持在真实 ClassCreation 环境解析需求；实际 creator Storage 贯通类根绑定、独立复核及 ClassRootWriteInput。纯转发 creator 的真实 es2abc 单测证明：缺少 demanded storage 拒绝，补齐后绑定成功。见 [实现边界和剩余依赖](round-67-class-requirements.md)。
- **未完成 II**：尚无统一的嵌套类准备任务图；生产入口仍保留 Increment I 的 gate。未接通冻结前声明决策、嵌套类全 owner 范围与 ReferenceNames、真实 enclosure 下的完成及 FamilyWrites 非根屏障验证；III 未启动。没有新增 complete，没有 Node 端到端声明。
- 完整构建通过；固定集串行 **65/65**；新/扩展测试 **5/5**；定向 **65/79**（14 项均为既有失败）；CLI/corpus/failure/receipt/manifest 扫描 **58/59**（仅既有 family_source_cli_test）。新增测试含两契约下 18 个真实 ABC 案例、200/201/400/800 宽度和成员内转发深度、需求/规划/绑定的精确 byte/work 边界及错 occurrence/creator/op/storage 反例。
- 对 explore67/explore67s：**513/620、552/620** 不变，80 份 manifest 逐字节相同，已有 complete/partial 源码逐字节相同（含 o32/49868）。四目标均仍为 FamilyCreationCheck=14。证据 `/private/tmp/r67ii-final-cohort/`，验收日志 `/private/tmp/r67ii-acceptance.json`。未运行留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 67 Increment I：普通后代类准备发现（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 59/60，只有既有的 family_source_cli_test 失败。

- 基线 `220cd6da`。从 record_tree 提取共用的布局发现、成员准备与完成协调；保持 record-root 顺序、命名和 owner 分配。普通后代在现有显式 family DFS 中保留真实类义务，按 occurrence/creator/pool/revision 独立验证后返回 `kClassPreparationGate`，不发布类体。后代完整 owner 范围仍须等成员家族计数；不以直接成员数伪造范围。见 [设计与验证记录](round-67-creator-classes.md)。
- 对 explore66/explore66s：**513/620、552/620** 不变，0 状态变化、0 complete/partial 文本漂移，含 o32/49868。完整 manifest 只有四个 o18 目标各两组 flags 的 `source_failure` 改为 gate 14；其余字段不变。证据 `/private/tmp/r67-final-cohort/`。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增/扩展 **4/4**；class/capture/deferred/guarded/family/creation/recovery 定向 **65/79**（14 项均为下方既有失败）；CLI/corpus/failure/receipt/manifest 扫描 **58/59**，仅既有 family_source_cli_test。真实 es2abc 200/201/400/800 宽度/深度、精确 byte/work 边界、错 pool/creator、重复或遗漏 occurrence、owner 冲突变异通过。未运行留出集、全套测试、RSS 基准；未 commit/push/stash。
- Increment II/III 尚未实施：类成员家族需求汇总、真实 creator storage/声明 fulfilment、ReferenceNames、enclosure 与 FamilyWrites 屏障贯通；phased creator 不变。没有新增源语言前提，也不声称后代类已可执行或通过 Node 差分。

## Round 66：FamilyFile 创建拒绝定位（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；513/620、552/620 不变，0 状态变化，0 文本变化；cli/corpus/failure 扫描 58/59，只有既有的 family_source_cli_test 失败。

- 基线 `bd4029c7`。五例在两组 flags 下相同：o18 四例为普通后代函数内的 ClassCreation 进入函数专用 BindCreation，实际叶子为 `FamilyFileError=1 > FamilyCreationCheck=9 (FunctionBinding) > CreationError=0`；o36/19453 为 phased 家族根的 ClassCreation 不在函数创建索引中，叶子为 `FamilyFileError=1 > FamilyCreationCheck=8 (PhasedSite)`。m60156 原始 CFG 的 PC128→174、PC192→229 跳转绕过先前 return，PC232 的类创建可达。逐例表与停止依据见 [round-66-family-creations.md](round-66-family-creations.md)。
- 所有 13 个 kCreation 检查有固定大小的类型化证据：实际创建 method / Op / instruction / 原始与工作 PC，家族 root / occurrence，方法作用域内 constructor / literal pool 身份，以及后代的独立 incoming creation 来源。普通 BindCreation 的真实底层错误不再折叠。route/manifest、record-entry JSON 和 abc_semantic 共用证据；失败枚举值和资源分类不变。无新生产索引、无新缓冲、失败时 O(1) 读取已有事实。
- 按本轮停止条件只完成步骤 1：现有 ClassTree 准备/组合及 FamilyWrites 验证只接入 normal record 根，后代类需要跨家族准备、捕获/声明与类屏障对应；phased 类还需异常相位对应。没有删除类、忽略创建或放宽局部准入。
- 对 explore65/explore65s：**513/620、552/620** 均不变，所有 record 状态不变，已有 complete/partial 文本逐字节不变；每组恰好五个目标仅 `source_failure` 字段变化。输出 `/private/tmp/r66-cohort/`，构建、CFG、CLI、测试与比较日志 `/private/tmp/r66-evidence/`。
- 完整 keep-going 构建通过；固定集串行 **65/65**；新增 **2/2**；family/creation/class_tree/class_omissions/source_route/tree_failure 定向 **26/28**，仅既有 `family_source_cli_test`、`class_tree_source_test` 失败。CLI/corpus/failure/receipt/manifest 扫查 **57/58**，仅既有 `family_source_cli_test` 失败。`class_deferred_cli_test` 的 loop 反例仍严格拒绝；将折叠叶子期望改为实际 `FamilyFileError=1 > FamilyCreationCheck=8`，并加强创建/后代来源断言，原 TDZ/Node/变异断言未改。新单测包含 13 种检查、位置/错误枚举变异、两条 JSON 通路、资源分类和 200/201/400/800 固定大小诊断往返；未声称新增恢复算法的精确预算或 Node 语义差分。未运行留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 65：成员调用参数中的保留值 fallback（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 510 → **513/620**，`--language-source` 549 → **552/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 57/58，只有既有的 family_source_cli_test 失败。cc 核对 ComputedProperty 输出为装饰器降级的标准形态 `Object.defineProperty(proto, "sum", decorate(...) || descriptor)`。

- 基线 `3cb1e433`。o21 四例均为 normal：callee 在 B5、调用在 B8；子调用已是 predicate snapshot，phi 在真分支保留该结果、假分支读取此前保存的 fallback。raw logical region 已发现，但 logical 路线要求 arm 内 `kPhiInput`，conditional 路线又让给 logical owner，形成发现缺口。不是 Round 46 暂缓的联合 consumer 准入。逐方法 Op/PC/phi 证据见 [round-65-retained-call-selections.md](round-65-retained-call-selections.md)。
- 只为未满足成员 Reference 的实参树保留现有 conditional/snapshot 证明，复用反向区间构造、正向验证及 TreeFlow/OriginalFlow/recovery。未改 consumer eligibility、catch、声明、命名或打印；无新前提。两个新索引预先计费，线性构建，总复杂度不超过既有 O(N log N)。
- 两套 cohort 对 explore64/explore64s：**510 → 513/620、549 → 552/620**，0 回退、0 已 complete 文本漂移。每套仅四个目标变化：15203、18179、23526 complete；16320 的入口问题消除，转到 m17082@171 的 CallReferenceError::kNotInline（call Op70/property Op8，Root/CrossRegionValue）。六份新增 complete 输出均通过 `node --check`。
- 完整 keep-going 构建成功；固定集串行 **65/65**；新增 **2/2**（104 次 Node 差分、12 个变异检出）；`retained_call_selection|call|reference|logical|selection|evaluation|recovery|original_flow` 定向 **68/69**，仅既有 phased_arm_selection_source_test；`cli|corpus|failure|_receipt|manifest` 扫查 **55/56**，仅既有 family_source_cli_test。没有修改既有失败或过期期望。
- IR 测试含 consumer/phi/树事件变异、copy/寄存器/直线前缀拆块、200/201/400/800 精确 work/byte 边界和独立 tree/recovery 验证。两个兄弟选择、join 到 consumer 的跨块限制仍开放，不把它们称为语言约束。证据在 `/private/tmp/r65-evidence/`；双 cohort 输出在 `/private/tmp/r65-first-cohort/`。未跑留出集、全套测试或 RSS 基准；未 commit/push/stash。

## Round 64：内部值诊断与循环 cell 初始化（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 509 → **510/620**，`--language-source` 547 → **549/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 55/56，只有既有的 family_source_cli_test 失败。`async_semantics_test` 在会话起点 22e7423a 上同样 rc 134，cc 将其补入既有失败清单。

- 基线 `3a8b010e`。六例拆为 3 个循环局部 cell 初始化门禁、2 个静态初始化器 self-capture 未获语言值证明、1 个无 guard 外部模块读取。新增固定大小的 `InternalValueEvidence`，保留真实 SSA producer / observer / read ordinal 或 cell / allocation / store；未知值域不误报成确定 Hole。两条 JSON 路径、route receipt、CLI 和 omitted members 均贯通。
- 只修最大局部组：对原先未能闭合的循环读取，用 allocation → store → read 的支配证据证明本次激活已初始化；所有写入仍需 Language 验证，未知闭包写入保持严格。工作 IR 的独立 cell 初始化验证器重算对应关系。未改 guard、catch、控制布局或声明放置规则，无新源语言前提。
- 双 cohort 对 explore63/explore63s：**509 → 510/620**、**547 → 549/620**，0 状态回退，0 原已 complete 文本漂移。CustomAnimationTab 两套 flags 从 partial 到 complete；OperationNodeTree 在 source flags 下 complete。assertDeepEquals 进至 m40502 @409 的 for-of 多出口 close-region 拒绝。另 o24/37520 的首拒绝前移到 m38333 的 UnsupportedOperation，状态不变。六例表和 partial 类补全的文本差异说明见 [round-64-internal-values.md](round-64-internal-values.md)。
- 完整构建通过；固定集串行 **65/65**；新增及诊断通路 **6/6**（Node 100 次差分、6 个变异）；定向 **64/78**。13 项在既有清单，另 async_semantics_test 已在干净 `3a8b010e` 临时副本重建复现相同 bad_variant_access/134，未改断言。CLI/corpus/failure/receipt/manifest 扫查 **54/55**，仅既有 family_source_cli_test 失败。新完成的三份 flags/record 输出均通过 node --check。未跑留出集、全套测试或 RSS 基准；未提交。

## Round 63：遗漏成员诊断与 phased ValueBranch 前缀（cc 审查通过）

- A：`source_omitted_members` 发布每个遗漏类成员的类型化首拒绝与位置（两个 partial class 的根因都是 `SemanticProofError::kInternalValue`）。B：3 个 phased Root 实为 Commit 上的 ValueBranch，已有 selection 证明；把向 phi 临时变量的条件赋值按原顺序放进 decision RHS 的逗号表达式，TreeFlow 独立核对。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 506 → **509/620**，`--language-source` 544 → **547/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 57/58，只有既有的 family_source_cli_test 失败。

## Round 62：DecisionPrefix（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 501 → **506/620**，`--language-source` 539 → **544/620**；0 回退，0 文本漂移；cli/corpus/failure 扫描 53/54，只有既有的 family_source_cli_test 失败。cc 对照原源码核对 KeyboardController：`isShiftKeyHold` 与原文几乎一致；调用处 `!(A && B) || C` 与原 `A && B && !C` 按德摩根律等价，分支互换，短路求值顺序相同。

- 基线 `fc7b648f`；证据先行重数两套 explore61：各 **12** 个首拒绝，集合相同。`DecisionPrefixError` 保留实际失败检查，`prefix_event` 与控制位置分开，原始/工作 PC 来自 origin，Try 单元保留 synthetic 身份；贯通两条 JSON 路径及 abc_semantic。逐 record 表见 [round-62-decision-prefix.md](round-62-decision-prefix.md)。
- 证据分为 **8 个 normal 的已验证 LogicalValue 前缀、3 个 phased root 分支、1 个 Try 单元**。本轮只修第一组：树发射消费现有 selection 的原始 phi/arms/join，TreeFlow 独立重算并验证，选择的边不再作为空直线边重复消费。没有新识别模板、联合 predicate/keep-edge 准入、catch/控制/declaration 改动、新前提或 recovery 豁免；未改变 for-of 暂停边界。
- 最终双 cohort 对 explore61/explore61s：**501 → 506/620、539 → 544/620**；**0 状态回退、0 已完成文本漂移**。每组只变动这 12 个目标：5 complete、2 明确 INCOMPLETE class、1 后续 LoopExits、4 保持拒绝并增补证据。新增完成输出两套共 10 份全部通过 `node --check`。partial class 不算完成；目前该路径的 manifest 缺少被省略成员的具体叶子/位置，已如实记录。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；`decision|logical|selection|evaluation|tree_failure|recovery|original_flow` 定向 **59/61**，仅既有 `phased_arm_selection_source_test`、`decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 串行扫描 **51/52**，仅既有 `family_source_cli_test` 失败（owner/origin arena 断言）。没有改这些失败断言。
- 新增 `decision_selection_prefix_test`、`decision_selection_prefix_cli_test`，加 `tree_failure_test` 扩展均通过：copy/寄存器/拆块、statement/expression 两种前缀、错误 join/selection/arm/缺失与重复事件变异、recovery 的已验证输入与 revision 检查、200/201/400/800 精确 byte/work 边界。最小预算 **13,569,949 / 13,635,762 / 27,125,765 / 54,237,397 bytes**，work **4,820,569 / 4,844,794 / 9,744,722 / 19,697,078**。真实 es2abc fixture **1,344 次 Node 差分、10 个变异检出**，README 登记命令、sha256 与 oracle 边界。
- `evaluation_failure.mjs` 的两项过期期望改成真实结果：DocsSample KeyboardController 为 partial class，Solutions KeyboardController 为 complete，均比对 CLI/manifest 源码；`tree_failure.mjs` 新增真实 phased prefix 的精确拒绝位置检查。
- 日志：`/private/tmp/r62-full-build-final.log`、`r62-fixed.log`、`r62-targeted.log`、`r62-sweep.log`、`r62-final-cohort.log`、`r62-comparison.json`；最终导出 `/private/tmp/r62-final-cohort/explore62{,s}`。未 commit/push/stash，未运行留出集、全套测试或 RSS 基准。

## Round 61：LogicalCoverage（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 500 → **501/620**，`--language-source` 537 → **539/620**（assertClose、GridLayout）；0 回退，0 文本漂移；cli/corpus/failure 扫描 51/52，只有既有的 family_source_cli_test 失败。

- 基线 `94e298d8`，证据先行：o32/28894 m29074 Op65/PC174 → Op101/PC258（Use111），o7/13863 m14290 Op201/PC508 → Op245/PC633（Use276），均为 `kConsumedEvent`：已登记 `kLogicalValue`，但消费目标属于 LiteralPlan 的 `kInitializer`，普通 Domain 不遍历其输入。不是 Round 46 的 predicate/keep-edge 联合义务。临时插桩已全部撤回。
- 固定大小 `LogicalCoverageEvidence` 区分 ValueMap / RouteMember / ForwardedRegistration / ConsumedEvent，保留实际位置、consumer、登记与覆盖状态，贯通 route/record-entry/JSON。逆序构建只有在所有选择的真实输入消费均通过区间证明后才提交 route 及成员；不完整候选保留原 phi/临时表示。正序验证器仍独立重算；无 TreeFlow、控制、声明、literal 准入或资源上限改动。详见 [selection-consumers.md](selection-consumers.md#atomic-route-coverage-round-61)。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；新测试加 JSON 通路 **3/3**；`logical|evaluation|selection|decision` 定向 **52/54**，仅既有 `phased_arm_selection_source_test`、`decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 扫查 **50/51**，仅既有 `family_source_cli_test` 失败。未改这些既有失败断言。
- 新增 `logical_coverage_test`、`logical_coverage_cli_test`：四类 mismatch 变异、部分 route 保留/终止、copy/寄存器/拆块、200/201/400/800 和精确 byte/work 边界；真实 es2abc fixture **112** 次 Node 差分、**10** 个变异检出。规模测试最小 scratch 预算为 3,988,312 / 4,008,235 / 7,972,912 / 15,942,112 bytes，work 为 1,777,364 / 1,786,243 / 3,566,587 / 7,158,200。
- 两组 exploration 对 explore60/explore60s：原 flags **500 → 501/620**，`--language-source` **537 → 539/620**；**0 状态回退、0 已完成文本漂移**，仅两个目标的 complete 增益产生诊断变化。assertClose o32/28894 在两组 flags 下 complete；GridLayout o7/13863 只在 source 下 complete，旧 flags 的 `DerivedProjectionError=1`（m14399，无 PC）保持不变。新完整输出全部 `node --check` 通过。
- `evaluation_failure.mjs` 的两个旧拒绝期望改为严格 complete 和 manifest/abc_semantic 完整源码一致性；其他四个后续拒绝期望不变。
- 证据：`/private/tmp/r61-evidence-o32/manifest.json`、`r61-evidence-o7/manifest.json`（修复前的类型化证据）；`r61-build-final.log`、`r61-fixed.log`、`r61-new-tests-final.log`、`r61-targeted.log`、`r61-sweep.log`、`r61-cohort.log`；导出在 `/private/tmp/r61-cohort/`。未运行留出集、全套测试或 RSS 基准；for-of 继续暂停。

## Round 60：DecisionEdgeCopies（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 498 → **500/620**，`--language-source` 535 → **537/620**（ScanSize、Filter；Filter 含三个原生 for-in，`node --check` 通过）；0 回退，0 文本漂移；cli/corpus/failure 扫描 50/51，只有既有的 family_source_cli_test 失败。

- 基线 `326fda68`。证据先行：o20/10622 m11730 PC139 e10 为 `v24 ← v23`（Use148）；o25/33281 m33743 PC421 e12 为 `v28 ← v27`（Use319）；o32/43537 m44496 PC252 e203 为 `v26 ← v35`（Use114）。均是最后谓词直接到 join 的 taken 边、单步无 cycle 临时量；真正的首拒绝位于 `OrientBranches`，早于 `EmitDecision`/`RequireNoCopies`。临时诊断已移除。
- 在原标量谓词的条件执行边上复用已验证的 phi copy schedule，表达为 `predicate ? (yes copies, true) : (no copies, false)`；无 copy 的 decision 不变。TreeFlow 独立检查条件臂、极性、顺序、唯一消费、末尾 Boolean；TreeEdgeFlow 共用语句 copy 的身份/临时量/定义检查，原有 OriginalFlow/recovery 验证边限定定义。未改变控制计划、声明、catch staging、打印器或资源上限。见 [loop-discovery.md](loop-discovery.md#decision-edge-copies-round-60)。
- 完整构建 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**。decision/edge-copy/phi-copy/control/logical/evaluation/recovery 定向 **40/41**，仅既有 `decision_snapshots_source_test` 失败；`cli|corpus|failure|_receipt|manifest` 扫查 **49/50**，仅既有 `family_source_cli_test` 失败。
- 新增 `decision_edge_copies_test` 与 `decision_edge_copies_cli_test` **2/2**：taken/fallthrough、copy/拆块、swap cycle schedule、错边/hoist/drop/duplicate 变异、200/201/400/800、精确 byte/work 边界；真实 es2abc fixture **96** 次 Node 差分、**6** 个源文本变异检出。200/201/400/800 的构建 scratch 最小预算为 8,148,463 / 8,188,154 / 16,283,271 / 32,552,887 bytes，work 为 1,674,167 / 1,684,143 / 3,393,649 / 6,811,969。
- 严格更新两处过期期望：`for_in_evaluation.mjs` 将 Filter 的旧 DecisionEdgeCopies 拒绝改为 complete、三条 native for-in 及 CLI/manifest 源码一致性；`phased_shared_decision_refusal_test` 的 catch-staging 例仍拒绝，首错从 copy 门禁前移后的 `kControl` 改为实际 `kOwnership / kCatchStaging`，另组仍严格为 `kOwnership / kExpressionOwnership`。没有放宽 staging。
- 对 explore59/explore59s：**500/620、537/620**（各 +2），**0 回退、0 已完成文本漂移**。每组仅三个目标诊断变化：ScanSize o25/33281 与 Filter o32/43537 complete；CustomAnimationTab o20/10622 → m12066 block3/control3 的 `FunctionTreeError=0 > TreeObligation=3`（DecisionPrefix，无可发布 PC）。新增完成文件两套 flags 均通过 `node --check`。
- 证据：`/private/tmp/r60-o{20,25,32}.diag`、`r60-full-build-final.log`、`r60-fixed.log`、`r60-targeted-final.log`、`r60-sweep.log`、`r60-cohort.log`；导出在 `/private/tmp/r60-cohort/`。未 commit/push/stash，未运行留出集、全套测试、RSS 基准；for-of 继续暂停。

## Round 59：SnapshotCandidate 一致性（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 回退，0 文本漂移；`snapshot_syntax_test` 在 a0302edb 上同样 rc 134（既有）。cc 另修正 `tree_failure.mjs` 中 o5/28303 的过期期望（Round 53–55 后首错为 m28799 TryId 0 的 kMultipleExits，body 1146/handler 1171），并对所有 cli/corpus/failure 类测试做了全量扫描：49 项中只有既有的 family_source_cli_test 失败。

- 基线 `a0302edb`；for-of 按用户决定暂停。两个目标 o8/10284 m12516、o28/10155 m11832 的真实失格分支一致：op24/PC59 的 Use34 消费者 op33/PC82 在构建时为 Root，logical-left 规划后为 LogicalLeft，最终候选报 `kConsumerPlacement`。临时追踪已移除，固定大小类型化候选证据保留并贯通两条 JSON 路径。
- snapshot 在 logical-left 规划后、logical route 提交前按同一候选证明确认；失格者恢复原位置的物化定义，并沿已有有界依赖遍历保留输入保存赋值。没有 verifier catch/retry、准入放宽、控制或声明位置改动。见 [expression-snapshots.md](expression-snapshots.md#snapshot-candidate-commit-boundary-round-59)。
- 完整 `cmake --build build/regression -j 6 -- -k` 成功；固定集 `ctest --preset source-regression -j 1` **65/65**；snapshot/logical/evaluation/selection 加 tree_failure/source_route 定向 **47/51**。四项既有失败：snapshot_syntax_test 与 snapshot_syntax_source_test（同一底层 :109 位置断言，Round 36 日志已有）、phased_arm_selection_source_test、decision_snapshots_source_test。未改其断言。
- 新增 snapshot_candidate_test 与 snapshot_candidates_cli_test **2/2**：copy/寄存器/拆块、错误/重复/遗漏消费、200/201/400/800 和精确 scratch/work 边界；真实 es2abc fixture **36** 次 Node 差分、**6** 个检出变异。tree_failure_test 验证所有新 sub-reason、PC0/缺失 PC 与两条 JSON 路径。evaluation_failure_cli_test 严格固定两个目标的新首错，并把已在 explore57/s complete 的 o23/4775 过期 print 期望更新为源码一致性检查。
- 对 explore57/explore57s：**498/620、535/620** 不变，**0 状态回退、0 文本漂移**。每组仅两个目标首错变化：o8/10284 → m11969 block6/control6；o28/10155 → m11285 block6/control6，均为 `FunctionTreeError=0 > TreeObligation=3`（DecisionPrefix，无 PC）；未越界修复。
- 证据：`/private/tmp/r59-evidence-o8.json`、`r59-evidence-o28.json` 及同名 `.trace`；`r59-build-final.log`、`r59-fixed.log`、`r59-targeted.log`、`r59-cohort.log`；导出在 `/private/tmp/r59-cohort/`。未 commit/push/stash；未运行留出集、全套测试或 RSS 基准。

## Round 57 增量 A（部分）：迭代器键解析与候选扫描（cc 审查通过）

- 设计（Round 57）获批：控制构造前用 IteratorProtocolScan 证明区域所有权；ProtocolCloseUnit 按完成种类分端口，循环骨架保留原控制身份；区域显式标记为 OrdinaryTry/Inactive/ProtocolOwned；关闭优先级从展开代码的值流证明；分 A/B/C/II/III 五个门禁增量。
- 本次交付：类型化 `IteratorKey`（不运行 TreeConstants 与名称规划）；`iterator_protocol_scan` 产出候选事实（acquisition 身份、唯一候选 next 定义、return 读取派发的区域与外层 handler）；`VerifyIteratorCandidates` 独立核对候选，不产生所有权。尚未完成：动态激活/advance 证明、close 完成值流、VerifiedProtocolRegionOwnership、PhasedBody 接入与诊断输出。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；新测试 1/1。

## Round 55：kMultipleExits 位置证据（cc 审查通过）

- 固定大小的区域/出口证据（TryId、body/handler 原始与工作 PC、原始 EdgeId 与 continuation），贯通 route/record-entry/JSON；仅在拒绝时做一次计入 work 预算的线性扫描。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化。cc 的“外层循环体保护区”假设被证据否定：首个冲突是 IteratorClose 内保护 `return` 读取/调用的 TryId 0，两个正常出口进入完成选择代码。下一步由 IteratorCloseEvidence 拥有内层关闭保护区、保存的异常及完成优先级。

## Round 54：不可达 handler 的 inactive 区域投影（cc 审查通过）

- 从原始 ExceptionFlow 重算正常与异常可达性；只有 handler 整段不可达、且没有可达事件派往它时，才生成类型化 inactive 记录（保留 TryId、范围与 protected 指令分类计数）；inactive 区域不分配 try/catch，原始索引保留；recovery 独立重算完整 inactive 集合。kRegion 拒绝补上 region 与 body/handler 位置。新增单元与真实 es2abc（O0）fixture；默认优化产物的 handler 长度为 0，保持带位置的 `kHandlerShape` 拒绝。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；定向 5/5。诊断变化仅 6 条：5 个 for-of 方法改为 `kControl > kMultipleExits`，driver 的 kRegion 补全为 `kLayout > kCrossingScopes`（region 4，PC 777/803）。

## Round 53 增量 I（部分）：标准迭代获取契约与原始语义（cc 审查通过）

- 用户 2026-10-14 批准第五项源语言前提“标准迭代获取”：新增 `kLanguageSourceV2` 与独立谓词 `IterationAcquisition()`，`--language-source` 选择 V2；V1 的语义、枚举值与序列化名称保留。接受的差异：@@iterator 不可调用或返回非对象时原生 for…of 更早抛 TypeError。不在此前提内、须逐循环证明的：next 缓存、done/value 顺序、关闭优先级（language-source.md）。
- 原始语义：同步 GETITERATOR imm8/imm16 → `AcquireIterator`/`ValueIteratorAcquire`（可能执行用户代码与抛出，成功后提交，不伪造 Object 结果域）；`throw.ifnotobject` → `ObjectCheck`/`ValueObjectCheck`。新增 `iteration_semantics_test`。
- 新阻塞：5 个方法在求值前停在 `PhasedMethodError::kControl > ExceptionControlError::kRegion`，原因是保留了 handler 不可达、没有 CatchBinding 的区域（ExceptionPlan 允许，ExceptionTopology 拒绝），需要有可达性证明的 inactive-region 投影。两个 RpcTool 停在其他子函数的捕获初始化。
- cc 修正 `language_source_corpus.mjs`：Round 44 起 FindAndListenAudioOutputDevice 在旧 flags 下也完成（该过期期望当时未被定向集覆盖）。
- cc 独立审查：构建通过；固定集 65/65（rich-text 首跑 worker 超时，重跑通过）；配对样本逐字节相同；498/620、535/620 不变，0 状态变化，0 文本变化；定向 15/15。

## Round 52 increment III：OriginalFlow/recovery 联合验证与发布（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；原 flags 497 → **498/620**，`--language-source` 534 → **535/620**；0 回退，0 文本漂移；for-in 测试 11/11。cc 核对 assertContain 的首个原生 `for ($key10 in actualValue)` 输出语义一致；可读性（空 then、尾部 `continue`）另议。

- 基线 `5b361344`，工作区未提交。OriginalEnumeration / FlowEnumeration 分离 acquire、next 的 Before/Commit、done、success-only KeyWrite/KeyCommit；内部 enumerator/next-result 不伪造源码变量或 ExprId。初始 draft 和每次 BuildRecoveryFlow 都复验原始事件、输入身份、边与提交。真实 key 读取必须由成功 KeyCommit 支配；写入 TDZ 用 reset-aware 声明事实证明。
- ForwardedException 与 PhysicalCatch 分开；逐入口重新核对同 payload、无观察前缀和最终目的地，反向 Kahn 检查全部路径终止。没有删除 handler 一致性义务，也未简化其他 catch。只对全部证明通过的方法移除 kForInRecovery 发布门禁；follow/decision 与原 LoopId 不变。
- 完整 `cmake --build build/regression -j 6 -- -k` **成功**；最终固定集 `ctest --preset source-regression -j 1` **65/65**；定向 **40/41**，唯一失败为此前已登记的 `tree_structure_test`（abort，未修改断言）。for-in **11/11**。新增真实 es2abc fixture 经引擎导出后做普通对象 Node 差分，另有原始 Commit/exception/identity/done、key 提前/耗尽写入、continue、handler payload、声明遗漏变异及 200/201/400/800 精确预算。
- 对 `explore52b/explore52bs`：**498/620、535/620**（各 +1），**0 状态回退、0 文本漂移**；仅四个目标 record 的首拒绝诊断变化。assertContain 新完整且两套 `node --check` 均通过。六个目标循环均通过生产 draft + OriginalFlow/recovery；DataDriver 首拒绝仍为 m42717@153 unsupported，Filter 转到 m44496@252 decision edge copies，core 转到 m21280@599 kCatchState，MockKit 转到 m54264 kParameters（无 PC）。逐循环表见 [property-enumeration.md](property-enumeration.md#round-52-increment-iii-originalrecovery-event-correspondence)。
- 边界：局部 key 在 break 后 live-out 且耗尽边有非空 phi copies 仍以 kExit 拒绝，独立 key_break.abc 固定该边界；属性 key 的 zero/last/break live-out 已有差分。捕获 m524、handled advance、非空 staging 仍拒绝。Proxy/原型或枚举属性变化/Symbol 的 Ark 运行时验证缺口未关闭；不新增源语言前提。
- 证据：`/private/tmp/r52iii-build-delivery.log`、`r52iii-fixed-delivery.log`、`r52iii-targeted-delivery.log`、`r52iii-targeted-list.txt`、`r52iii-target-loops-delivery.log`、`r52iii-delivery-cohort.log`、`r52iii-node-check.log`；导出 `/private/tmp/r52iii-delivery-cohort/`。未 commit/push/stash；未跑留出集、全套测试或 RSS 基准。

## Round 52 increment II：生产 draft 与 TreeFlow（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 9/9。审查重点：preheader 私有 phi 初始化先于 native 语句（依赖 acquisition→first-next 证明），耗尽 copies 仅在唯一正常退出时后置，其余以 kExit 拒绝。

- 基线 `8cca4728`。新增 `ForInEmissionView`，只解释已验证 ControlPlan，不改节点、follow/decision 或 LoopId。生产 builder 发射赋值式 ForInStmt；枚举器、内部 next-result、成功边 key commit 分开，原始声明与普通 body/exit 保留。
- 异常对应按原始入口记录 PhysicalCatch / ForwardedExceptionRoute；逐入口普查后才消费转发路径，混合共享入口和非空 catch staging 保守拒绝。TreeFlow 从不可变输入独立重建 view、协议事件、scope、copy 与 exit 对应。
- 生产 builder 返回 unpublished draft；TreeFlow 成功后，公开入口以 `FunctionTreeError::kSemantics(9) > TreeObligation::kForInRecovery(15)` 拒绝。**没有 ConstructedFunction 包装或源码发布**；OriginalFlow/recovery 联合验证留到 III。
- 完整 `cmake --build build/regression -j 6 -- -k` 成功；固定集串行 **65/65**；39 项定向 **38/39**，唯一失败为已登记的 `tree_structure_test`。for-in 相关 **9/9**，含新增生产 draft/真实 fixture、view/异常/作用域/跳转变异、共享 key live-out、200/201/400/800 与精确预算边界。
- 两套对 `explore52/explore52s`：**497/620、534/620** 不变，**0 状态变化、0 文本漂移**；仅 core/assertContain/Filter/MockKit 四个目标诊断改为 recovery 门禁。六个循环的方法级 draft + TreeFlow 均通过；DataDriver 首拒绝仍是 m42717@153 的 getiterator；Filter 两循环共享首个 acquisition @28 的公开门禁。
- 证据：`/private/tmp/r52ii-build-full.log`、`r52ii-fixed.log`、`r52ii-targeted.log`、`r52ii-target-drafts.log`、`r52ii-final-cohort.log`；最终导出 `/private/tmp/r52ii-final-cohort/`。设计、限制见 [property-enumeration.md](property-enumeration.md#round-52-increment-ii-native-draft-and-treeflow-unpublished)。未 commit/push/stash，未运行留出集、全套测试或 RSS 基准；不宣称 engine-output Node 差分。

## Round 52 increment I：生产求值接入（cc 审查通过）
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 7/7。

- 基线 `4674e981`。两个 phased 调用点通过共享 evaluation constructor 构造并验证 `ForInPlan` / `ForInEvents`；协议成员/use 索引区分 enumerator、next-result、done 和真实 key 用法。内部 copy/phi SCC 保留身份和原始输入记录，不分配源码符号；acquire/next 保持有序可抛出事件。normal-flow 源码准入未扩展。
- FunctionTree 在表达式/符号构造前独立复验，并以 `FunctionTreeError::kSemantics > TreeObligation::kForInCorrespondence` 在首个 acquisition 拒绝。尚未接入 TreeFlow/OriginalFlow/recovery 的 native-for-in 联合对应，不发布半验证树。
- 完整 keep-going 构建通过；固定集串行 **65/65**；37 项定向 **36/37**，唯一失败为既有 `tree_structure_test`。新增 `for_in_evaluation_test`、`for_in_evaluation_cli_test` 均通过，含 SCC/phi、协议 use/事件变异、200/201/400/800 与精确预算边界、两条 JSON 路径及 abc_semantic/manifest 一致性。
- 对 `explore51e/explore51es`：**497/620、534/620** 不变，**0 状态变化、0 文本变化**；仅 o32 的 core/assertContain/Filter/MockKit 四个 record 更新为新树拒绝。六个目标循环均通过方法级求值验证；DataDriver 仍先拒绝其他方法 m42717@153 的 getiterator，Filter 两循环共同由 m44469@28 的首个 acquisition 门禁表示。
- 设计和范围见 [property-enumeration.md](property-enumeration.md)。证据：`/private/tmp/r52-build-delivery.log`、`r52-fixed-delivery.log`、`r52-targeted-delivery.log`、`r52-target-loops.log`、`r52-delivery-cohort.log`；新导出在 `/private/tmp/r52-delivery-cohort/`。未 commit/push/stash；II/III 尚未实施。

## Round 51d：for-in 协议事件证据（cc 审查通过；生产接入待续）

- `for_in_events` 分开记录 acquisition、advance 的正常 Commit 与 not-done 边上的 key commit；验证器重新核对原始 phase、结果定义、异常出口与 done 两侧边。新增 `for_in_events_test`（伪造 Commit、替换结果、错配异常出口、反转 done 边、耗尽边写 key、遗漏/重复事件，200/201/400/800 规模与精确预算）。尚无生产消费者；生产接线草稿因联合验证未完成而撤回。
- cc 独立审查：构建通过；固定集串行 65/65；配对样本逐字节相同；497/620、534/620 不变，0 状态变化，0 文本变化；for-in 测试 5/5。

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
- 审查修订后定向 **8/8**；22 次源码 oracle、2 次 ABC handler 严格期望和 6 个变异全部通过。
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
  直接相关 7 项 **8/8**。phase/exception/source 等定向串行批次 **293/324**：
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
| 既有失败清单（2026-10-13 核实） | 以下 30 个测试在会话起点 22e7423a 上同样失败，不在定向集中（全套测试暂停），不属于本会话的回退：file_generator_return(_loop)_source_test；literal_construct、mixed_literal、phased_literal、literal_closure_source、unguarded_literal_closure_source、derived_literal_receiver_source、unguarded_derived_literal_receiver_source；boolean_context_source_test、captured_receiver_source_test、class_tree_source_test、decision_snapshots_source_test、derived_cells_source_test、family_source_cli_test、forwarded_receiver_source_test、phased_arm_selection_source_test、reversed_captured_receiver_source_test、reversed_forwarded_receiver_source_test、reversed_initialization_captured_receiver_source_test、reversed_initialization_forwarded_receiver_source_test、snapshot_syntax_source_test、try_tree_source_test、try_tree_test（2026-10-14 补登，bc0b1dee 上复现）、tree_structure_test（2026-10-14 补登，22e7423a 与 c329fd2d 上同样 rc 134）、snapshot_syntax_test（2026-10-14 补登，a0302edb 上同样 rc 134）、async_semantics_test（2026-10-14 补登，22e7423a 上同样 rc 134）、unguarded_captured_receiver_source_test、unguarded_derived_cells_source_test、unguarded_forwarded_receiver_source_test、unguarded_reversed_captured_receiver_source_test、unguarded_reversed_forwarded_receiver_source_test、unguarded_reversed_initialization_captured_receiver_source_test、unguarded_reversed_initialization_forwarded_receiver_source_test |
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

### Readable grouping slice 1 — unfinished WIP (2026-10-19)

- Added standalone occurrence-indexed `GroupingPlan` / re-deriving verifier,
  conservative syntax classification and prepaid iterative summaries. Added
  mutation and 200/201/400/800 exact-budget tests, plus a pinned Acorn grouping-token
  checker that preserves literal spelling, comments and line terminators.
- **Not wired into TreePrinter.** Existing parentheses, `(0, …)` wrappers, blocks,
  output policy/cache keys and source behavior remain unchanged. Projected use
  enumeration (declaration/boolean/nullish/loop/name envelopes), full grammar
  coverage and the es2abc/ark_disasm acceptance driver are still required.
- Full build and fixed source regression 65/65 passed for the foundation. This is
  component evidence only, not completion of the user's readability request;
  no cleaned output or compiler-equivalence acceptance is claimed.
- Fresh cohort checks of this non-integrated foundation: previous flags
  **526/620**, language-source **576/620**, zero complete-status or complete-text
  changes against the retained d4076bd2 cohort outputs. Grouping removals: **0**.
- Requested CLI/corpus/failure/receipt/manifest/ISA/call-spread/staged/position sweep
  plus the new grouping tests: **100/101**; `family_source_cli_test` remains the
  previously recorded failure. `grouping_plan_test` and `grouping_tokens_test`
  pass. Compiler equality, paired-sample acceptance and full Node differentials
  have not been run; these remain required before enabling output changes.

### Readable grouping slice 1a — print-time subset (2026-10-19)

- Supersedes the integration boundary above: no projected-occurrence adapter.
  `TreePrinter` classifies actual operand contexts after its print projections.
  Sequence/update/conditional/assignment emitters share `ReadableGrouping`;
  arguments, array elements/spreads and value arms use AssignmentExpression.
  Unknown contexts stay grouped. Prefix-update sign adjacency and update member
  bases remain parenthesized. Every `(0, …)` wrapper and every block is retained.
- Added production-printer/counting-sink/span tests and a five-form ordinary-call
  Node differential; standalone plan mutations and 200/201/400/800 exact budgets
  remain passing. Updated the progress-bar mutation's literal spelling only:
  `(this.options = ({controller: this.controller}))` becomes
  `(this.options = {controller: this.controller})`; the alias mutation still must
  be detected. No behavioral assertion was removed.
- Baseline d4076bd2 vs current: identical complete sets **526/620** previous flags,
  **576/620** language-source, **80/80** paired complete outputs. Acorn grouping-only
  checks and Node syntax checks pass **526 + 563 + 80** JavaScript files, with
  **13 ArkTS files explicitly unverified**. Removed pairs: **4,670 / 5,158 / 1,177**.
- Both default optimization and O0 compiler/disassembler checks independently:
  **526/0/0**, **567/0/9**, **80/0/0** pass/differ/unverified. No normalization of
  registers, branches, exceptions or metadata. Nine ArkTS files fail on both sides
  at pre-existing quoted export aliases. They are not passes. Tool hashes,
  commands, ten actual examples, text counts and remaining conservative contexts
  are in [readable-grouping.md](readable-grouping.md).
- Final grammar guards were followed by regenerated outputs: all **527 / 577 / 80**
  source files are byte-identical to the compiler-checked current copies.
  Evidence is under `build/review/paren/`, including `final-recheck.json`.
- Final full build passes; `ctest --preset source-regression -j 1` **65/65**;
  grouping tests **4/4** (including the printer Node differential). All generated
  JavaScript passes `node --check`: **527 / 564 / 80**. These include the extra
  partial JavaScript file in each cohort; the 13 ArkTS files are not Node checks.
- Final requested CLI/corpus/failure/receipt/manifest/ISA/call-spread/staged/
  position/grouping sweep: **102/103**. Sole failure is the documented pre-existing
  `family_source_cli_test` (`node escaped its owner or origin arena`). Also updated
  `assignment_arguments.mjs`'s spelling assertion from `f((target...))` to
  `f(target...)`; its value/order/receiver differential and both mutations pass.
  `git diff --check` passes. No commit or push performed.

### Readable grouping slice 1b (2026-10-19)

- Extended print-time contexts to statement expressions, function/arrow/class
  values, object member bases, declaration and return/throw values, condition
  operands and iteration/spread RHSs. A fixed-size statement-start flag follows
  the final leftmost print path; head syntax and lexical lookahead keep necessary
  groups. Function open/close actions share their decision. Name-inference
  envelopes, `(0, …)`, directives, optional boundaries and blocks are unchanged.
- Added 32 function-family context/name cases with a Node differential; object
  statement/value and reserved-name tests; retained independent grouping verifier
  mutations and 200/201/400/800 exact budgets. Text-dependent mutation sites were
  updated for removed parentheses; their original semantic assertions remain.
- OLD is a retained build of the clean **b24c0a92** main checkout, made before
  edits: the sandbox denied the requested worktree Git index write. Baseline
  identity/hash/build evidence and all runs are in `build/review/paren1b/`.
- Complete sets remain **526/620**, **576/620**, and **80** paired records. Both
  compiler modes independently: **526/0/0**, **567/0/9**, **80/0/0**
  pass/differ/unverified. Same nine pre-existing ArkTS export-alias failures,
  no bytecode differences. Token checks pass all **526 / 563 / 80** JavaScript
  pairs (13 ArkTS unverified). Removed pairs: **11,399 / 12,928 / 1,301**.
- Every new JavaScript output passes Node syntax: **527 / 564 / 80**. Final
  regenerated source and manifest record entries are byte-identical to the
  compiler-checked copies. Full context boundaries, ten examples and counts are
  recorded in [readable-grouping.md](readable-grouping.md).
- Final verification: full build passed; `source-regression -j 1` **65/65**;
  grouping/family/name targets **9/9**; updated integration targets **16/16**;
  requested CLI/corpus/failure/receipt/manifest/position/grouping/call-spread/staged
  sweep **96/97**, only the documented pre-existing `family_source_cli_test`
  owner/origin-arena assertion. Formatting-dependent mutation patterns and the
  partial-family return assertion were updated; semantic checks remain strict.
  `git diff --check` passed. No commit or push.

### Readable grouping slice 2 — identifier value-call wrappers (2026-10-19)

- Remove `(0, name)` only at value calls with a verified strict tree, supported
  declarative/module/global scope, a final identifier spelling other than eval,
  and no literal/replacement projection. Preserve member receivers, optional
  calls, constructor/tag contexts, typeof and inferred-name suppression.
- OLD is cc's 1d62926c executable. Complete sets remain **526 / 576 / 80**.
  Default compilation: **526/0/0**, **567/0/9**, **80/0/0**
  pass/differ/unverified. O0 pass/allowed/differ/unverified:
  **284/242/0/0**, **296/270/0/10**, **69/11/0/0**.
- Opt-in acceptance allows only zero loads immediately discarded by identical
  register/binding loads; all other assembly is exact. Strict remains default.
  Allowed deleted instructions **704 / 979 / 21**. Token and Node checks pass
  all **526 / 563 / 80** JavaScript files; 13 ArkTS token checks remain unverified.
  Nine pre-existing compile failures remain; O0 o15/45013 is additionally
  unverified without a token witness. No unapproved bytecode difference.
- Full evidence, wrapper counts and ten examples are in
  [readable-grouping.md](readable-grouping.md) and `build/review/paren2/`.
- Final verification: full build passed; fixed suite **65/65**; grouping **8/8**;
  required sweep **98/99**, only pre-existing `family_source_cli_test`.
  Broader receiver/reference checks **15/29**: the 14 registered receiver-family
  failures occur before printing; function-name and call-reference checks pass.
  Callee-reassignment and indirect-eval-alias Node differentials pass.
  Syntax including partial files **527/564/80**, zero failures. Final executable
  output matches compiler-checked text; `git diff --check` passes. No commit/push.
