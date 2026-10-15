# 源语言契约：LanguageSourceV1、V2 与 V3

`SourceContract::kLanguageSourceV1`（序列化名 `language_source_v1`）保留原有四项前提。
2026-10-14 新增 `kLanguageSourceV2`（`language_source_v2`）；V2 继承 V1，并加入 §5 的独立标准迭代获取前提。
2026-10-15 新增 `kLanguageSourceV3`（`language_source_v3`），CLI 的 `--language-source` 现选择 V3；
V3 继承 V2，并增加文末列出的独立 Ark 输出政策。该选项必须与 `--value-source` 同用，并且不能与 `--language-arithmetic`、`--language-numeric`、
`--language-constructors` 组合。它包含 `LanguageNumericConstructorsV1` 的全部前提（numeric 算术、
原生 super 语义），并另外加入下面四项源语言前提。默认契约 V3 以及其他已有 opt-in 都不变。

**用户批准：2026-10-13**（会话中逐项列出影响面后，用户批准了全部四项）。代码中每项前提都有
各自的谓词，不得直接比较枚举值。新增或改变任何一项都需要新版本，并再次获得批准。

| 前提 | 谓词 | 影响（探索集，2026-10-13） | 状态 |
|---|---|---|---|
| 捕获访问（读取与写入）按源语言 TDZ 解释 | `CaptureReads() == kSourceTdz` | 诊断涉及 24 个 record（不是当前新增完成数） | 无自身 handler 的访问及 Round A 转发已实现；Round B 待做 |
| 原帧转发显式投影为 rest/spread；实例属性赋值保持 `[[Set]]` | `ConstructorForwarding() == kSourceRestSpread` | 16 个 record（`DerivedProjectionError`） | normal 路径显式 rest/spread 投影已实现；验证见 §2 |
| 惰性导入输出 ArkTS `import lazy`，保持首次访问初始化与 live binding | `LazyImports() == kArkTsLazyImport` | 12 个 record（既有诊断，非新增完成数） | normal/phased 受保护读取已实现，见 §3 |
| 数组展开 `starrayspread` 按源语言 `[...x]` 输出 | `ArraySpreads() == kSourceIterator` | 8 个 record（历史诊断） | Round 33 私有构造投影；验收见 §4 |

## 1. 捕获访问的源语言 TDZ（CaptureReads）

**范围变更（同日，经用户批准）。** 最初只批准了“读取”。Codex 第二十五轮（R2）指出，当时的实现
把写入也放宽了。改为只放宽读取后，实测语料收益为 0：这类页面里的提升函数都同时写入之后才声明的
绑定，例如 setter 中的 `globalLogUpdate = f`。用户随后批准把前提扩展到写入，差异同属提前调用一类：
声明执行之前调用 setter，运行时写入成功（随后被声明初始化覆盖），源码则抛 `ReferenceError`。

**现象。** 模块入口（以及函数体）会提前创建顶层函数声明：先执行 `definefunc`，再执行
`stlexvar`。这些函数读取一个之后才首次赋值的 `let`/`const` 绑定。es2panda 按**源码中的先后顺序**
判断是否需要 TDZ 检查：引用在声明之后出现时，就不生成 `throw.undefinedifholewithname`。因此子函数
里是一次不带守卫的捕获读取（Language 域）。而在函数对象创建与首次 store 之间，可能夹着用户调用，
固定运行时契约无法证明读取一定发生在初始化之后，于是父函数的 `Rebase` 报
`ScopeError::kInitialization`。

**前提。** 捕获读取或赋值按源程序中对该绑定的标识符访问解释。在**该次动态绑定的初始化完成之前**
执行捕获读取或赋值，输出遵守原生 TDZ：运行时可能读到 hole 或写入成功，输出则抛 `ReferenceError`。
已批准的差异包括同步异常、async 拒绝，以及 generator 恢复执行时的异常；不把某个静态 store 的存在
当作所有激活都已初始化的证据，也不承诺保留运行时的 hole 值或提前写入行为。

**实现。** 子函数侧为每个满足条件的访问记录义务 `CaptureDomain::kSourceAccess`：没有 hole 检查
的 load，或任意 store。当前实现以方法没有自身 handler 为保守条件（Codex 第二十五轮 R1）：
normal 路径要求 `body.tries.empty()`；phased 路径在 async/generator 规范化之后检查同一条件。
这样，提前访问引发的 TDZ 抛出只会离开子函数；父函数中对它的调用本来就按“可能抛出”建模，
所以不需要为这次访问新增子函数内部的 catch 快照。读取的值仍是 Language 值。规范化后没有 handler 的
phased 子函数也可记录 `kSourceAccess`；仍有 handler 的方法保持 `kLanguage`，包括其中不在 try 范围内的
访问。这是尚未实现逐访问异常投影的限制，并非语言不变量。父函数一侧：
- `NormalScopeFacts::Rebase` 在该前提下，对 `kSourceAccess` 需求采用与 Guarded 相同的延迟声明规则：
  分配之后执行的第一次写，就是该绑定的首次 store，并且满足同块在创建之后、或者是唯一的 store。
  owner 必须恰好在该 store 处声明这个绑定。
- `capture_cells.cc` 的 `Deferred()` 在该前提下接受 `kSourceAccess` 绑定。其余延迟机制（`CaptureBinding.deferred`、
  `VerifyDeferredCells`、树中声明位置的独立验证）原样复用，见 `guarded-captures.md`。
- 父 cell 在创建时已有 store（例如 `var` 先存入 `undefined`）的情形不受影响。异常路径的
  `ExceptionScopeFacts` 把 `kSourceAccess` 按 Language 处理，保持严格。
- 路由回执的契约名称表改为由 `SourceContractName` 按枚举生成，保证每个能写出的契约都能读回
  （Codex 第二十五轮 R3）；`source_route_test` 对全部枚举值做往返验证。

**Round A：家族转发。** `JoinCaptureRequirement` 统一合并自身访问、后代和签名 entry 需求：
Guarded < SourceAccess < Language。Defined 是附加的严格义务，不能降为 Language 或 TDZ；不能表达其
加强关系的 owner 返回类型化拒绝。normal 与 phased 中间函数都使用已验证 working environment 的
`FindDemandCell` 映射，映射失败返回 `CaptureCellError::kCell`，不丢掉后代需求。entry 需求保持 Language；
零自身访问也转发完整义务。`CaptureStorage.uses` 是家族义务，原始逐指令访问证明不变。
Owner 只允许原样保留、Guarded→SourceAccess/Language、SourceAccess→Language。
汇总使用每 cell 一项的预先计费表和二分查找，沿家族反向 preorder 单次传播，不做每变量图遍历。

**测试。** `language_source_cli_test` 保留 `hoisted` 与 `earlywrite` 的 Node 源码差分（后者源码与输出
都抛 `ReferenceError`）。旧 `tryread` 停在 `PhasedMethodError::kImport`，不能证明 handler 门禁，已由
`family_cells_test` 替换：成功导入的 phased 孙函数在自身 handler 下读/写同一 cell，义务仍是 Language，
跨无 handler 的 phased 中间函数转发后，normal 父函数必须以
`CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization` 拒绝延迟声明。
单测还覆盖格组合、原始证明保留、纯转发、多创建点、签名 entry 及预算边界。
`language_source_corpus_test`（仅当探索样本存在时注册）按 cc 审查后采纳的实测结果断言：旧 flags 下
五页均以 `CaptureCellError=3 > CaptureError=1 > ScopeError=3` 拒绝；`--language-source` 下四页 complete，
仅 `InputDeviceRoutingSwitching` 保持相同原因链的拒绝。逐页预期为：

| 页面 | numeric + constructors | language-source |
|---|---|---|
| FindAndListenAudioInputDevice | 指定 ScopeError 链拒绝 | complete |
| FindAndListenAudioOutputDevice | 指定 ScopeError 链拒绝 | complete |
| InputDeviceRoutingSwitching | 指定 ScopeError 链拒绝 | 指定 ScopeError 链拒绝 |
| ListenDeviceByAudioSession | 指定 ScopeError 链拒绝 | complete |
| OutputDeviceChangePause | 指定 ScopeError 链拒绝 | complete |

async method 16458 属于 `InputDeviceRoutingSwitching`（record 14751）；规范化后仍有一个用户 try，
捕获需求仍为 Language，属于 Round B 的范围。本次重新导出确认，`--language-source` 下实际原因链是
`CaptureCellError::kInput (3) > CaptureError::kScope (1) > ScopeError::kInitialization (3)`，
回执为 `unproved/tree_stage`、pc 43。其余四页的变化来自本轮修复：phased 中间函数的纯转发 Guarded 需求原先因为未映射
而默认成为 Language，现在保留 Guarded。以 record 5467 的 method 6319 为例，后代对 slot 3 的
Guarded 需求得以转发，方法自身无 handler 的访问保持 SourceAccess。没有放宽用户 handler 门禁。
在 `/private/tmp` 中仅回退两项转发规则的诊断二进制重新得到五页全拒绝；生产文件未因诊断而改写。

**本轮验证（2026-10-13，未提交）。** `cmake --build build/regression -j 6 -- -k` 成功；新增
`family_cells_test` 1/1、固定集 `ctest --preset source-regression` 65/65。首轮指定六项定向测试 5/6，
当时唯一失败为 corpus 的旧预期，本次已按 cc 确认的四页 complete、一页拒绝修正。
补充复测 `ctest --test-dir build/regression -R '^language_source_(cli|corpus)_test$' --output-on-failure`
为 2/2 通过。
单独恢复“跳过 phased 映射”或“丢失 Guarded→SourceAccess 加强”
的临时变异均被新单测检出。没有运行全语料、全套测试或 RSS 基准；complete 是导出回执，不能代替
真实页面的平台执行差分。

**Round B 待做。** 在异常 CFG、SSA 和 catch 快照之前，将契约引入的捕获 TDZ 检查投影为类型化
可抛出事件，再让表达式/树/phase 组合的异常验证消费该证据。实现后才放宽仍有用户 handler 的访问，
再把尚被拒绝页面的 `--language-source` 预期翻转为 complete。Round A 不增加异常事件、不改 catch
快照或 finally/async/generator 的异常语义，也不扩展已有延迟声明的结构准入。

## 2. 原帧转发的显式构造器投影

**前提（用户批准：2026-10-13）。** 在经证明保持本次调用参数列表、构造器身份和
new.target 的派生构造器中，允许用私有 rest 参数及原生 `super(...rest)` 表示
invocation-frame forwarding。新增数组迭代过程引起的可观察差异属于批准范围；
其余正文仍须保持求值顺序、异常、receiver 和完成语义。实例属性赋值保留 `[[Set]]`。
不能改为原生类字段声明的 `[[DefineOwnProperty]]`：父类可能有 setter 或只读属性。
rest 数组分配的资源耗尽差异沿用本契约已有的资源前提，不增加其他可观察差异。

**实现边界。** `ConstructorForwarding()` 在 `kLanguageSourceV1` 及继承它的 V2/V3 启用，独立于旧的
`Constructors()` 前提。纯转发且已接受的隐式构造器保持原输出。显式投影依次通过
共同帧/协议证明、物理参数与源码 rest 签名分离、树构建、独立复验及普通语法打印。
第一增量不支持构造器自身 handler、arguments/new.target 的正文观察、需保留的形参或
默认参数、动态重复转发，以及未转发的正常返回。前后语句通过通用效果、CFG/SSA 和
树对应检查，不按字段名、opcode 序列或块形状准入。这里的 handler 指既有透明重抛规范化之后
仍保留的 handler；本轮不扩展重抛擦除。转发前后可保留普通语句、互斥路径上的转发，以及不经过
转发的循环；每条正常返回路径须恰好成功转发一次并返回该 receiver。异常退出不需要到达转发。
暂时改写再恢复物理参数/new.target 槽仍保守拒绝，属于首版帧保存证明的覆盖限制。

**实现。** `constructor_forwarding` 在原始寄存器 SSA 上传播入口身份与参数/new.target 观察位；
在 normal CFG 上传播 Before/After/Mixed 与帧改写位。单个状态有常数次增长，稀疏工作队列与
邻接表预先计费；不按参数逐个遍历 CFG。活跃工作图的转发操作、构造器身份、receiver guard、
返回与原始指令对应均重新检查。拒绝原因在 `DerivedProjectionError` 中分别记录 contract、frame、
frame changed、handlers、arguments、new.target、parameters、repeated、missing、return。

`ParameterForm::kInvocationRest` 与 `InvocationRestBinding` 将物理参数布局同源码签名分开：
原始 `ArgumentWindow`、extra_arguments 和 SSA 不改，不伪造 `ValueRestArguments`。源码只合成
一个由常规分配器命名的私有 rest 绑定；`SuperCallExpr` 仍保留 `kInvocationFrame` 来源，
其唯一新增实参是该绑定的 spread。`tree_operands` 重新计算投影，`tree_structure` 校验契约与
形状，`function_abi` 禁止原参数残留观察、rest 写入/逃逸和额外引用。字段写入仍走现有
`ValueNamedWrite`→`AssignmentExpr` 的 [[Set]] 对应。打印器只输出已经验证的普通语法。

**测试。** `source_route_test` 穷尽全部契约。`binding_plan_test` 覆盖 0/1/200/201/4096 个物理槽
对应一个源码 rest 绑定及残留参数观察拒绝。`constructor_forwarding_test` 用提交的 es2abc fixture
解析真实类 owner，并先断言一项 SuperForward 和两项 [[Set]]；再构造协议、寄存器复制、块拆分、
互斥转发和循环变体，覆盖各项拒绝、伪造 proof/AST/owner、精确预算及 200/201/400/800 规模。
`constructor_forwarding_cli_test` 对 `fields.abc` 运行 480 个 Node 差分场景及六种变异检测，
旧的六种 CLI flags 组合保持拒绝；纯转发 `pure.abc` 的新旧输出逐字相同。fixture 来源、生成命令、
哈希和测试范围见 [README](../../../tests/integration/fixtures/constructor_forwarding/README.md)。
Node oracle 明确使用赋值而非原生类字段，不声称进行了 Ark VM 差分或平台验收。
探索集比较由 cc 执行；本轮不运行全语料或留出集，不推断新增 record 数量。

**Round 29 验证（2026-10-13，工作区未提交）。** 按 D1→D7 顺序推进，每阶段构建并运行相关测试。
最终 `cmake --build build/regression -j 6 -- -k` 成功；定向命令
`ctest --test-dir build/regression -R 'constructor|derived_projection|source_route_test|language_source_cli_test|binding_plan_test' --output-on-failure`
串行 19/19；固定集 `ctest --preset source-regression` 65/65。两个新增测试已包含在定向清单中，
单独运行也为 2/2。D6 的打印单测首次未提供全局对象读取所需的输出作用域，补齐测试的
`GlobalScope` 后通过，没有放宽生产门禁。未运行全语料、RSS、留出集或探索集；
旧 flags 的 467/620 和 `--language-source` 的不回退对照仍由 cc 完成。

## 3. 惰性导入（Round 31 用户裁决）

输出 ArkTS `import lazy`，不投影成普通静态 import。保持首次访问时初始化依赖、
模块请求顺序及 live binding；读取仍可能执行模块代码并抛出，不假设已初始化。
默认及其他契约不开放此源码准入，sendable 变体不在范围内。
Node 测试机械删除 `lazy` 后只能验证值与 live binding，不能证明惰性初始化时机。

**Round 31 实现。** `RecordModuleContext` 读取 request-phase 表，按请求索引记录 eager/lazy，
并校验数量和读取边界；缺表按锁定运行时的 eager 默认处理。imm8 和 wide lazy load 共用
`ModuleLoading::kLazy`，保留执行模块代码、读写堆和异常效果。异常路径先执行 Before，成功后
才提交累加器；不把 lazy 标成 initialized，也不因模块绑定已知而擦除它的效果。
normal/phased 的 guard 对应、slot→request→binding、加载方式及契约在绑定和树验证中复验。
没有受保护读取证明的 lazy load 仍拒绝。共享或 TLA owner、namespace lazy、lazy re-export
及无绑定 lazy 请求是首版输出覆盖限制；sendable 指令继续拒绝。

声明按原请求顺序输出 `import lazy`，读取使用 live 标识符。带 lazy 请求的 SourceV1 record
源码使用 `.ets`，manifest 增加 `source_language: "arkts"`；普通 record 保持 `.mjs`，结构
record.json 的格式不变。该标记不表示 Node 可执行。请求表畸形保留 `ModuleError::kRequestPhases`，
契约、绑定、读取证明、模块种类分别保留类型化原因，phased 的模块失败也保留叶子原因。

**测试。** `module_info_test`、`module_load_test`、`module_reads_test` 和 `lazy_module_test`
覆盖 phase 表边界、imm8/wide、效果及成功提交、normal/phased/handler、200/201/400/800 规模，
以及加载方式伪造、guard 删除、树标签伪造。`lazy_import_cli_test` 验证真实 es2abc 输出、
旧契约拒绝、元数据变异和 live binding 差分。来源、命令、哈希及 Node oracle 的局限见
[fixture README](../../../tests/integration/fixtures/lazy_import/README.md)。

**Round 31 验证（2026-10-13，工作区未提交）。** 各阶段相关单测先行通过；最终
`cmake --build build/regression -j 6 -- -k` 成功，
`ctest --test-dir build/regression -R 'module|language_source|source_route|lazy_' --output-on-failure -j 1`
为 51/51，`ctest --preset source-regression -j 1 --output-on-failure` 为 65/65。
新增元数据变异最初发现 phased 模块错误折叠，保留 `PhasedModuleFailure` 叶子原因后通过，
没有降低原因链断言。未运行探索集、留出集、全套测试或 RSS 基准；语料对照由 cc 执行。

## 4. 数组展开（LanguageSourceV1，Round 33–34）

用户批准输出标准 `[...x]`，并接受锁定运行时与标准展开的全部已列差异：数组
`.values`/直接迭代器到 `Symbol.iterator`、每步读取 next 到保存 next 方法、迭代结果
自有且可枚举的 value 描述符复制到普通取值、字符串专用遍历及 [[Set]] 到标准
迭代和自有数据属性定义，以及迭代期间 length 变化的行为。包括这些差异引起的
getter/setter 调用与异常。该前提不授权把任意已有数组修改改为新数组，或丢弃
可观察索引结果；仅 `ArraySpreads() == kSourceIterator` 开放源码投影。

**cursor 范围裁决（2026-10-13，Round 33）。** 锁定 ets_runtime
`RuntimeStArraySpread` 通用迭代分支使用 `int tmp = indexHandle->GetInt(); tmp + 1`
推进（`runtime_stubs-inl.h:600–601`）。cc 转达的裁决：超过 2^31 个元素的构造会先遇到
内存资源耗尽，属于 V2 起 `ResourceFailures() == kExcluded` 的既有排除范围。
本实现据此不另证 iterable 的整数上界；它不是一份证明 C++ 有符号溢出按模回绕的结论。
构造中的已知覆盖、倒退、错误起点或独立可见 cursor 仍拒绝。

**D5。** 原始 `ArraySpread` / `ValueArraySpread` 的三个输入为 destination、index、source；
正常结果为下一写入 cursor，抛出前可有部分堆写入，异常 SSA 仅在成功 Commit 定义结果。
空迭代返回原 index，结果域由 index 的域与 Number 合并；未知 index 不被伪造为 Number。
新增 `ComputedOwnWrite` 适配混排元素的普通/wide `stownbyvalue`，保留原始效果与 acc。

`LiteralPlan::arrays` 保存类型化步骤：元素、spread、私有 length 读取、cursor 推进。
`array_initialization` 使用公共 SSA copy/phi identity、支配可用性和 all-path effect order，
一次建立 allocation/use/cursor 索引；执行锚点证明每个步骤与本次 allocation 的次数和次序。
cursor 表达式是“某个 spread 的正常结果加固定元素数”，length 只在同一私有目标上重建
当前 cursor。没有相邻 opcode、固定寄存器或同块准入。常量池密集前缀单独复核。
全部候选缓冲在分配前计费；复杂度为公共分析成本加线性索引/步骤检查，至多 O(n log n)，
n 包含图、uses 和输出前缀元素。资源计费不是实测 RSS 证明。

**D6。** 新增 `ArrayExpr` 和带原始 origin 的 `ArrayElement`（Value / Spread / Elision）。
当前构造器发射 Value / Spread；不把未证明的空位伪装成 undefined。无 spread 的数组继续
走旧表示，打印文本不改。spread 在 recovery flow 中有自己的有序、可抛出 action，
不等同于 iterable 子表达式。cursor 计算由验证后的投影消去，并保留零宽来源边界。
独立 evaluation/tree 检查重算图证明和常量证据，检查输入、精确元素序列、origin、
原始作用域及 catch；phased 融合不能单独吸收这些初始化成员。

**Round 34：allocation 之后的 RHS。** 构造证明复用 `DescribeEvaluation` 的 eager
表达式能力、公共 copy/phi identity 与 `BuildEffectOrder` 的全路径顺序事实。
每个晚求值节点记录唯一的语法 consumer；module read 的既有 guard 证明保留为同一读取
的第二个事件。一次 use census 排除共享、逃逸和 catch/phi 观察；显式 receiver 经现有
成员引用身份检查，不作为第二次求值。迭代式后序遍历按源码操作数顺序逐一对照原图
must-reach 前驱/后继：前一元素提交 → RHS 全部事件 → 本元素提交。纯 RHS 节点也有执行
锚点。无归属效果、遗漏、重复或重排均不能穿过此方程，没有新增块形状/相邻指令准入。

晚 RHS 使用既有 `kNestedLiteral` 与 Kahn 构建队列；字面量打印器没有新语义判断。
独立数组验证器重算归属和事件方程；树操作数验证核对原始 use，tree flow 核对实际
catch 作用域，recovery flow 保留调用/getter/spread 各自的动作。后续 phased 融合不能
重新吸收数组成员或已登记的 RHS 边。phased 死拷贝仅在公共 liveness 验证通过后才不算
独立观察；catch 可见的拷贝不能这样消除。

**本增量边界。** 已有目标、提前逃逸、handler 观察部分数组、独立使用 cursor、
无法证明的写入/执行次序、错误 cursor 均拒绝。尚不能作为唯一 eager 子树组合的
选择/悬挂协议、共享 RHS，以及需 catch staging 的组合继续拒绝；不把这些实现缺口
描述成语言限制。错误枚举保留原有数值，新增 `kOperand`（10）、`kSharedOperand`（11）、
`kCatchStaging`（12）；`kOrder` 仍表示全路径事件顺序不成立，`kCatch` 表示抛出点
dispatch 不一致。最终树还独立复验实际 catch 作用域。

**D7 测试。** `array_initialization_test` 覆盖构造/树验证、copy、逐操作拆块、
200/201/400/800、目标/cursor/步骤/元数据/契约伪造、spread 删除和顺序变异、精确预算边界；
`array_phase_test` 检查异常 SSA、catch 不可观察性和错误 catch 位置。
`array_spread_test` 保留原始语义测试并增加普通/wide 混排写入检查。
`array_spread_cli_test` 用实际包含 starrayspread 的 es2abc fixture 做 Node 源语言差分、
事件轨迹与变异检测；覆盖范围和已批准差异见
[fixture README](../../../tests/integration/fixtures/array_spread/README.md)。
这不是与 Ark VM 的差分执行，也没有尝试探索集、留出集、全套测试或 RSS 基准。

七项既有 literal 失败保持原断言：`literal_construct_test`、`mixed_literal_test`、
`phased_literal_test`，以及 literal closure/derived receiver 的 guarded/unguarded 四项。
Round 32 已在隔离基线复现，cc 另确认会话起点 22e7423a 也失败。本轮最终构建、固定集
和串行定向集的实际结果随交付报告记录，不把原始 IR 单测代替端到端验收。

**Round 33 最终自检。** `cmake --build build/regression -j 6 -- -k` 成功；
`ctest --preset source-regression -j 1 --output-on-failure` 为 65/65。
`ctest --test-dir build/regression -R 'literal|array|object|language_source|source_route' -j 1 --output-on-failure`
为 48/55；上述七项仍失败，失败位置和四项 family/tree 诊断与 Round 32 保存的基线日志相同。
新增三个 CTest 目标和扩展的 `array_spread_test` 均通过。未执行探索集、留出集、全套 CTest 或 RSS。

**Round 34 定向诊断（基线 ecdf20cc）。** 仅导出用户指定的两个 ABC 文件，未运行探索集
或留出集。下列 record 的首个拒绝均来自“allocation 后的下一个效果必须是数组步骤”
旧方程，均为 normal 路径：

| record | method | allocation PC | 首个 RHS 效果 | spread PC |
|---|---|---:|---|---:|
| 34854 MonthViewItem | 35352 | 16 | pc 27 module read，随后 hole guard、属性读取和调用 | 58 |
| 41894 WeekViewItem | 42310 | 12 | pc 23 module read，随后 hole guard、属性读取和调用 | 49 |
| 14276 MusicVsGamePage | 14565、14621 | 12 | pc 23 captured lexical load，随后 pc 29 属性读取 | 33 |
| 18376 MusicVsMusicPage | 18666、18722 | 12 | pc 23 captured lexical load，随后 pc 29 属性读取 | 33 |
| 23033 MusicVsVideoPage | 23323、23379 | 12 | pc 23 captured lexical load，随后 pc 29 属性读取 | 33 |

临时诊断已撤回；实现后在 `--language-source` 下上述五个 record 均 complete。
这是定向导出结果，不是全探索集统计或配对应用执行验收。
新增 IR 检查覆盖晚 RHS 调用/异常、混排、copy/拆块、200/201/400/800、顺序/归属
变异、精确预算，以及正常/错误 catch 位置和必须 staging 的拒绝；Node fixture 增加
RHS 调用、getter、receiver、抛出、顺序及重复读取变异。结果以本轮交付日志为准。

**Round 34 最终自检。** `cmake --build build/regression -j 6 -- -k` 成功。
`ctest --preset source-regression -j 1 --output-on-failure` 为 65/65。
`ctest --test-dir build/regression -R 'literal|array|object|language_source' -j 1 --output-on-failure`
在修改前后均为 46/53：上述七项失败名单、断言位置和 family/tree 诊断相同。
扩展的 `array_initialization_test`、`array_phase_test`、`array_spread_cli_test` 全部通过。
未提交；未运行探索集、留出集、全套 CTest 或 RSS 基准。


## 5. 标准迭代获取（LanguageSourceV2，2026-10-14）

**用户批准的精确差异。** 在完整的同步 for-of 协议已经被证明时，允许输出原生
`for (... of ...)`。当 `@@iterator` 不可调用，或调用后返回非对象时，原生 for-of
较早抛出 `TypeError`；原 ABC 随后对 `next` 的读取、调用及其效果不再发生。这是
独立前提 `IterationAcquisition() == kStandardV1`，由 `kLanguageSourceV2` 及继承它的 V3 启用。
V1 的四项前提、各旧契约的枚举值和序列化名称不变；契约名称仍从枚举生成并做往返验证。

**不包含。** 每个循环仍须独立证明 next 缓存、done/value 的顺序、IteratorClose
覆盖及异常优先级、绑定与动态激活。此前 `[...x]` 的授权不能代替这些证明，此前提也
不授权 `apply` 投影为 `f(...args)`。异步迭代、捕获的逐迭代绑定、解构不是首批目标。

**当前工作区的实现边界。** 已加入谓词与同步 GETITERATOR imm8/imm16 的原始语义，
以及 `throw.ifnotobject` 的独立对象检查。获取仍返回运行时实际的语言值，不能凭契约
改成 Object 域；只有成功路径提交 acc。对象检查不写 acc，并保留独立异常出口。
`next`、`done`、`value`、`return` 仍是普通属性/调用事件。没有新增原始 CloseIterator
提升；既有拒绝不变。

**尚未完成增量 I。** 尚无 ForOfPlan/Events、IteratorCloseEvidence、共同求值入口接入、
独立协议验证或 `kForOfCorrespondence` 门禁，因此不能宣称已验证 for-of 或已有源码准入。
加入原始语义后，TimeUtils、ComponentFactory、DeepTypeUtils、configService 和 DataDriver
在进入共同求值入口前，遇到 `ExceptionControlError::kRegion`：保留的 try 没有可达的
Catch 节点/绑定，而 ExceptionTopology 要求每个保留 region 都有 CatchBinding。需要先明确
不可达异常区域的投影和独立来源对应，再接协议求值证明；不能用空 ControlPlan 绕过。
该阻碍不需要新增源语言前提，也不能放宽 effectful close 为透明转发。

原始语义单测 `iteration_semantics_test` 验证窄/宽编码、成功 Commit、独立异常出口、
伪造 Commit/删除异常出口变异，以及 200/201/400/800 的 importer 精确内存/工作边界。
这些是 raw IR 证据，不是协议层、IteratorClose 或 Node 差分验收。

**本次 raw/契约增量自检。** 完整 `cmake --build build/regression -j 6 -- -k` 成功；
新增 raw 测试的最终单目标重建成功；固定集串行 65/65，定向 21/21（含既有 for-in 11 项）。
DataDriver 的旧 CLI 断言随实际首拒绝从 unsupported@153 改为 kControl > kRegion，
严格检查 method 42717、没有虚构 PC，并补查 manifest 与 abc_semantic 一致。
对 `explore52c/explore52cs` 的逐 record 对比仍为 498/620、535/620，状态与 complete 源码
字节均无变化，assertContain 保持完整；仅指定 8 个候选 record 的首拒绝变化。
日志与比较 JSON 在 `/private/tmp/round53-{full-build,targeted-final,fixed,cohort}.log` 和
`/private/tmp/round53-cohort/comparison.json`。这些结果只验证当前 raw/契约增量；
不能代替尚未实现的 acquisition-semantics 门禁、close/completion 协议及独立求值验证。


## 2026-10-15：LanguageSourceV3 的 Ark 输出政策（Round 73）

用户批准 concurrent 函数、Sendable 类和 shared 模块的**保留语义输出**：分别使用
`"use concurrent"` 函数体 directive（或由 SDK 转换的 `@Concurrent`）、`@Sendable`/
构造器 `"use sendable"`、模块 `"use shared"`。这不是擦除授权；输出必须能由锁定的
动态前端重新编译为相同 kind/opcode 和元数据。Node 无法验证跨线程和共享语义。
`apply -> f(...args)` 没有获准，仍须走完整实参数组构造的无前提证明。

`--language-source` 从本增量起选择 `kLanguageSourceV3`，序列化为
`language_source_v3`。V1、V2 保持其原有含义；V3 继承它们已批准的独立前提，新增三个
穷尽枚举的输出政策谓词：

| 谓词 | V1/V2 及其他契约 | V3 当前值 | 发布状态 |
|---|---|---|---|
| `ConcurrentOutput()` | `kNotEnabled` | `kArkDirectiveV1` | 同步和 async 完成投影已接入；编译往返见增量 II/III 验收 |
| `SendableOutput()` | `kNotEnabled` | `kArkDirectiveV1` | 增量 II/III：primitive/union 与真实 import 支撑的 GENERIC 存储描述；独立证明和实际编译往返 |
| `SharedModuleOutput()` | `kNotEnabled` | `kArkDirectiveV1` | 增量 IV：独立 shared kind/模块表/入口恢复证明；实际编译往返 |

Round 73 增量 I 不修改 callable、创建时间、名字或任何打印动作。`CreationContextError::kKind`
保持外层枚举；附加 `ConcurrentFailure` 区分输出政策未启用、准备门禁、所属 record、模块表、
请求注解和资源失败。证据保存子方法、真实 creator occurrence/创建 PC、注解地址、是否存在、
元素数与出错元素；不存在子函数失败 PC 时不借用父创建 PC。固定大小错误对象不携带任意长列表。

`ReadConcurrentRequests` 独立读取方法注解，验证所属 record 与其请求表的映射；U32 元素类型
来自 OH7 `emitter.cpp::GenConcurrentModuleRequestsAnnotation`。保留编码顺序和重复请求。
缺失注解与存在的空注解不同：OH7 `MethodLiteral::GetConcurrentRequestedModules` 前者返回
`nullopt`，后者返回空集合，`SourceTextModule::ModuleEvaluation` 对应“遍历全部请求”与
“全部跳过”。未知元素名/类型、越界请求均拒绝，不猜测或丢弃。

读取器两次流式遍历注解，用第一次计数在分配前预留结果容量；名称边界比较不分配字符串。
时间为 O(A log R + E)，A 为注解引用数，R 为索引区域数，E 为展开后的元素数（也是输出维度），
空间 O(E)。模块表存活时，其容量从后续请求读取 allowance 中扣除。没有新增无界缓存。
record 属性读入沿用已有 reader；调用前按每字段四次文件长度上界加区域查找预扣工作量，
覆盖名称验证、转换和几何扩容复制，重复引用长名称及缺失模块也不能跳过扣款。
乘法前通过除法检查预算，拒绝时尚未解码名称。该保守扣款不改变既有 reader 的语义。

新增验证包括缺失/空注解、错误所属 record、请求越界、错误类型/名称、顺序和重复项，
200/201/400/800 元素以及精确 bytes/work 边界；固定大小错误的两条 JSON 路径往返；
真实 ABC 六个方法的请求和五个 record 的 CLI/manifest 一致性。同期元数据验收不是编译往返
或 taskpool 运行验收：后续增量必须证明通知是实际完成事件、同步/async 协议和导入使用，
再通过引擎输出→es2abc 的 kind/请求注解/相关 literal 身份比较，才能解除发布门禁。

**增量 I 自检（未提交）。** 完整 `cmake --build build/regression -j 6 -- -k` 成功；
`ctest --preset source-regression -j 1` 为 65/65；并发/契约/元数据及 CLI 定向 17/17
（含新增四项）；`cli|corpus|failure|_receipt|manifest` 串行扫描 66/67，唯一失败为
既有 `family_source_cli_test` 的 owner/origin arena 断言，未修改该断言。
预算补充回归先在旧实现 rc134，预扣修复后通过。相对 `explore71/explore71s`，
最终二进制的两组结果仍为 521/620、561/620，状态变化 0、源码字节变化 0。
旧 flags 仅 o14 的 10719/16446/18164/31477 新增并发原因；source flags 另含 25688。
source 的 40 个 manifest 顶层及 562 个 record 契约标签由 V2 更新为 V3；无其他字段变化。
完整命令、逐字段差异和六个方法的请求表在 `/private/tmp/abcd-r73/acceptance.json`、
`old-comparison.json`、`source-comparison.json`、`method-requests.txt`。

### Round 73 增量 II：同步 concurrent 完成投影

同步 concurrent 的 callable 仍为普通 Function；`ConcurrentDirective` 是独立的已验证输出
属性，仅 V3 启用，打印为函数体的第一条 `"use concurrent";`。创建表达式、名字推断和
创建时机不变。本增量当时遇到 AsyncEnter 以 `ConcurrentError::kAsync` 和真实子方法 PC 拒绝（后续增量 III 见下）；
没有把 concurrent 函数当作 async，也没有扩大 V1/V2 的输出政策。

`callruntime.notifyconcurrentresult` 的原始语义读取 acc 与当前调用帧的 function，不改写
acc、不提交新的结果。依据 OH7 interpreter-inl.cpp 的对应 handler、runtime_stubs-inl.h
及 functionBuilder.cpp，保留为不可擦除的有序宿主完成事件。异常抛出路径无需通知；
每条成功返回路径须恰有一次属于本函数的通知，值与返回值经 SSA/copy/phi 身份一致，
且通知之后不能有可抛出/有效果操作或不终止的后缀。该证明基于有限格数据流和 DAG 检查，
不是相邻指令模板。当前端口表示保守拒绝一个通知分流到不同物理 Return 的情况；后续若要
支持，须增加路径相关端口，不能靠近邻条件或静默复制通知。

共同求值入口在 temporary/phi 消费前接入该证明。通知保留专用 evaluation kind、Anchor
及通知/返回两个事件端口，不能走纯 anchor、初始化 guard 或普通表达式。TreeFlow 从
原始图重算；OriginalFlow 保留原始 typed operations，recovery 对每次投影重新推导，
在最终存活的 Return 完成上保留通知。待返回值先求值并保存，enclosing finally 先执行；
finally 改写/抛弃 pending return 时不能继续该通知。

捕获边界检查消费包含后代转发需求的真实 CellPlan：IncomingCell 不能跨越 concurrent
边界，即使 concurrent 自身没有访问。参数、自己的局部 cell、全局以及 external import
绑定不受此禁止；local module binding 和 module store 仍拒绝。ModuleCapturePrefix 对
concurrent 创建的排除保持不变，不能把普通 outer cell 冒充 import。

指令属性携带已验证的请求序列和 import-slot→request 映射。输出阶段的独立引用证明仅消费
最终 variable projection 实际发射的 typed ModuleSymbol，不分析名字/源码文本；逐项核对
拥有者、slot、请求身份、顺序和次数，且要求耗尽。普通子函数继承最近 concurrent 作用域，
与前端 binder 一致。缺失注解仍拒绝，存在的空注解须没有引用；不插入 fake reads。
目前 concurrent 内的 class body 跨独立 class printer 的引用普查尚未贯通，保守给出
`kRequestUses`，而非漏记内部使用。该边界不影响本轮五个同步方法。

证明与索引为 O((V+E) log V) 以内，缓冲在分配前收费；通知工作队列为有限格有界队列，
请求检查为流式计数。资源/工作外层枚举不变，细节原因和原始/工作 PC 走两条 JSON 路径。
原始指令、完成证明、捕获边界和 JSON 的单测含伪造/删除/重复/错误 owner/值、实际
200/201/400/800 大小与精确 bytes/work 边界；真实 fixture 记录在
`tests/integration/fixtures/concurrent_functions/README.md`。Node 只验证普通调用值、异常
类型和轨迹；跨线程语义仍是明确的 Ark runtime 验证缺口。

**动态前端语法边界。** pinned es2panda 的 named-export parser 不接受带引号的导出别名。
实际发射了 verified concurrent directive 的新 record，以 IdentifierName 输出合法的导出名，
例如 `export { query as query }`；当前 formatter 需引用字符串拼写的 import/re-export/
export 名以 `kModuleSyntax` 拒绝（含尚未贯通 Unicode 标识符拼写的覆盖缺口，并非语言限制）。同步输出 scope
从 verified directive 取证，并跨 class/family printer 传递；这仅决定目标语法拼写，不能授予
恢复准入。没有发射 concurrent 的 record 保持原有别名拼写。往返测试直接编译真实输出，
没有机械改写输入文件；导出名、local alias 和模块表保持原值。

### Round 73 增量 III：async concurrent 的 Promise 完成端口

并发 kind 不蕴含同步性：动态前端先选择 CONCURRENT_FUNCTION，再选择 async kind。
创建上下文仅用原始 typed AsyncEnter 选择 async owner；能否输出仍由完整 async 协议证明
决定。输出保持原创建形式和名字，使用 `async function`，directive 仍是函数体第一条语句。
V1/V2、其他 flags 的 concurrent 输出政策不变。

`concurrent_async` 在原始普通 CFG 上反向求完成后缀摘要，以两个标量槽需求追踪通知和
返回的输入，透过复制进行替换；后继必须同意同一物理通知/返回端口。settlement 的正常
结果须同时满足两项需求。后缀不能包含其他效果、可抛出操作、外部入口、catch 入口或
不终止路径；所有通知及返回都必须被覆盖。每条 resolve/reject 路径恰好通知一次，且通知
属于当前 frame.function。这是值流与控制证明，不以相邻 opcode、寄存器号或同块为准。

Pinned interpreter-inl.cpp:6659–6698 对 settlement 先检查 abrupt completion，再将结果
提交到 acc；runtime_stubs-inl.h:396 的 RuntimeAsyncFunctionResolveOrReject 从同一个
JSAsyncFuncObject 取 Promise 并返回该 Promise。asyncFunctionBuilder.cpp:24–32、:54–65
在 resolve/reject 后通知并返回。故通知值是 **Promise**，不是源码 return 的解析值。
证明后只折叠这些封闭后缀，再复用既有 async owner：它继续证明状态对象唯一性、Promise
寄存器、await/resume、隐式 catch/reject、用户 handler 和来源映射，不准入新的 wrapper。

verified directive 保存经计费的不可变原始方法与 frame/profile。TreeFlow 和每次 recovery
投影从该输入重算完成后缀和 async owner，并核对全部原始/工作来源及返回位置。归一化图
不制造一个“通知解析值”的虚假表达式；通知端口归属 native async 的 settlement 完成边界，
隐式拒绝也不能伪造为源码 ReturnStmt。同步通知的显式事件验证路径保持不变。

缓冲在分配前计费；反向摘要及成员归属为 O(V+E)，PC/范围重映射为 O(N log N)。请求
注解仍由真实 ModuleSymbol 引用流核对，不插入 fake reads。普通调用 Node 差分与原始/重编译
ABC 的 kind、sendable、async protocol、请求及两种通知完成端口检查分别提供证据；它们
不替代 taskpool/跨线程的 Ark runtime 验证。

夹具与 oracle 边界见 `tests/integration/fixtures/concurrent_async/README.md`。成功、await
拒绝、前后抛出、返回 Promise 以及 catch-around-await 可走实际导出；finally-around-await
仍受已有 catch-input 活性证明约束，保留类型化拒绝。本增量不放宽该门禁或 m16784 的
环境合并证明。

### Round 74 增量 I：Sendable/shared 元数据与证据

增量 I 当时只建立元数据和诊断，不开放源码准入；两个 V3 谓词当时均为 `kNotEnabled`。
创建指令给出 `ClassKind`，据此选择 ordinary/sendable literal 格式；字段缓冲保留
字段顺序、运行时类型 mask 与实例/静态分界。GENERIC/NONE 不是可猜测的名义类型。
共享属性保留 absent/false/true，和真实所属 record 的模块表绑定，不能冒充
`OrdinarySynchronous`。固定大小失败证据在 receipt、两条 manifest 路径及
`abc_semantic --record-source` 中一致，原首因和资源分类保持不变。

后续闭合类增量才会采用无 initializer 的 TS 字段存储描述、构造器首条
`"use sendable"` 和原有 [[Set]] 写入；需要类型注解的 `.ets` 用 `--extension=ts`
重编译，并标记 `source_language: arkts`。GENERIC 仍须有真实 binding 支撑的可验证
拼写，否则 `kTypeSpelling`。详见 [Sendable/shared 设计](sendable-shared.md)。


### Round 74 增量 II：闭合 Sendable 类

`SendableOutput(V3)` 现在启用 `kArkDirectiveV1`，其他契约保持 disabled；
`SharedModuleOutput` 仍 disabled。按原始创建种类选择 literal 格式，类准备复用
ClassTree/family。创建者使用已解码的不可变输入，文件级 literal 索引按实体共享
Sendable 字段/成员缓冲；字段名按字符串实体排序去重，避免别名长字符串重复读取。
索引是 O(N log N)，保存 O(N) 输入/唯一文本；输出的重复文本另按实际输出计费。
新增索引、共享控制块、字符串和所有 arena 都在分配前计费。

完整证据只允许无 heritage、无外部 capture/模块环境依赖、无待恢复 static initializer
或 defined member、无成员遗漏的闭合类；静态存储的初始化器对应仍为类型化门禁。每个构造器的 directive 属性绑定实际创建点、
constructor/literal 池和方法身份；发射器仅从该属性输出首条 `"use sendable"`。
字段保留顺序、实例/静态位置和 mask。number/string/boolean/bigint/null/undefined
及其 union 输出为**没有 initializer** 的 TS 存储描述，原构造器 [[Set]] 不变。
NONE、TYPE_REF、GENERIC(32) 没有获证的拼写，继续 `kTypeSpelling`；带环境/捕获/
heritage 或其他缺口的类继续类型化拒绝。没有把 shared 类当作普通类。

树、类准备结束及来源对应独立复核原始 kind、canonical literal、构造器属性和闭合
条件。字段描述是同一创建事件的存储元数据，不是额外的 [[Define]] 语句。
新发布且实际含此语法的 record 才使用 `.ets` / `source_language: arkts`；已有记录
不因契约选择而改扩展名。用 DevEco `--module --merge-abc --extension=ts --opt-level=0`
重编译输出，保持原 record 名；禁止 `--use-define-semantic`。

测试比较实际输出重编译后的 class kind、字段缓冲、成员、constructor length、
sendable env、模块/导出映射及有序 [[Set]]，并做 directive/mask/写入变异检测。
Node 只比较擦除存储描述后的普通调用值、异常类型和轨迹；共享堆、字段类型检查和
跨线程行为仍是 runtime 验证缺口。fixture 来源与命令见
[closed Sendable fixture](../../../tests/integration/fixtures/sendable_source/README.md)。

字段解码的 mask/split/key 失败缓存为所属类的拒绝，不能让新字段证明的失败影响
同文件无关记录；资源/work 失败仍归索引所有者。需要字符串 import/export 名而
锁定 TS 前端没有获证拼写时，返回记录级 `kBinding`，不发布无法重编译的 `.ets`，
且没有伪造指令 PC。


### Round 74 增量 III/IV：Sendable 模块读取、成员环境与 shared 发布

`ModuleDomain::{kOrdinary,kSendable}` 独立于 eager/lazy loading。imm8/wide 的
Sendable external load 保留函数对象→模块→import slot→binding→request 身份、原始
可能抛出事件和正常完成后的 acc 提交。所有实际 Sendable 读取必须有原始命名 hole guard；
不能借外部模块已初始化的前提消除它。normal/phased 输入、模块 guard、树操作数、模块
enclosure 和 recovery 分别复验域及来源。lazy-sendable/local-sendable 未获此支持。

Sendable 构造器/成员根使用原创建上下文的模块域；成员内普通箭头仍是普通闭包，可以捕获
该成员自己的 this/key/value。外层 lexical cell 跨 Sendable 根的捕获仍拒绝；
ModuleCapturePrefix 的普通同步模块条件没有删除。静态初始化器保留独立的环境模式和准备
门禁。本增量未支持在普通内层箭头中直接读取 Sendable import 的继承域证明。

GENERIC(32) 没有唯一原始类型。`SendableTypeSpelling` 只为已验证构造器中的
`this.field = new importBinding.member.path(...)` 选择一个真实 import 支撑的可重编译
类型拼写。该路径与原赋值/constructor ExprId、binding、字段 ordinal 绑定；独立验证器
重算，不增加 import、运行时读取或初始化，也不替换原 [[Set]]。未证明的类型、歧义赋值、
NONE/TYPE_REF、静态字段继续类型化拒绝；并非将 GENERIC 擦除为 any/Object。

shared record 由 `SharedModuleProof` 独立验证所属文件/record/模块表、shared=true、
非 CommonJS/非 TLA/非 JSON 和当前 eager 请求范围；不调用 OrdinarySynchronous 冒充普通
模块。完整 record tree/recovery 仍核对入口效果与 export identity；shared 部分恢复不能
发布。模块级 `"use shared";` 来自该证明，作为首行输出并计入 import 前缀字节/位置。
新发布记录为 `.ets` / `source_language: arkts`，原 flags 的门禁不变。

真实 es2abc 往返和普通调用 Node oracle 见
[fixture](../../../tests/integration/fixtures/sendable_modules/README.md)。Node 对 shared
heap、锁、跨线程和运行时字段类型检查没有验证能力；这些保留为 runtime-verification gap。
