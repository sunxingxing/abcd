# LanguageSourceV1：按源语言语义输出的四项前提

`SourceContract::kLanguageSourceV1`（序列化名 `language_source_v1`）由 CLI 的 `--language-source`
选择，必须与 `--value-source` 同用，并且不能与 `--language-arithmetic`、`--language-numeric`、
`--language-constructors` 组合。它包含 `LanguageNumericConstructorsV1` 的全部前提（numeric 算术、
原生 super 语义），并另外加入下面四项源语言前提。默认契约 V3 以及其他已有 opt-in 都不变。

**用户批准：2026-10-13**（会话中逐项列出影响面后，用户批准了全部四项）。代码中每项前提都有
各自的谓词，不得直接比较枚举值。新增或改变任何一项都需要新版本，并再次获得批准。

| 前提 | 谓词 | 影响（探索集，2026-10-13） | 状态 |
|---|---|---|---|
| 捕获访问（读取与写入）按源语言 TDZ 解释 | `CaptureReads() == kSourceTdz` | 诊断涉及 24 个 record（不是当前新增完成数） | 无自身 handler 的访问及 Round A 转发已实现；Round B 待做 |
| 原帧转发显式投影为 rest/spread；实例属性赋值保持 `[[Set]]` | `ConstructorForwarding() == kSourceRestSpread` | 16 个 record（`DerivedProjectionError`） | normal 路径显式 rest/spread 投影已实现；验证见 §2 |
| 惰性导入输出 ArkTS `import lazy`，保持首次访问初始化与 live binding | `LazyImports() == kArkTsLazyImport` | 12 个 record（既有诊断，非新增完成数） | normal/phased 受保护读取已实现，见 §3 |
| 数组展开 `starrayspread` 按源语言 `[...x]` 输出 | `ArraySpreads() == kSourceIterator` | 8 个 record | 未实现 |

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

**实现边界。** `ConstructorForwarding()` 仅在 `kLanguageSourceV1` 启用，独立于旧的
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

## 4. 数组展开（已批准，待下一轮实现）

用户批准输出标准 `[...x]`，并接受锁定运行时与标准展开的全部已列差异：数组
`.values`/直接迭代器到 `Symbol.iterator`、每步读取 next 到保存 next 方法、迭代结果
自有且可枚举的 value 描述符复制到普通取值、字符串专用遍历及 [[Set]] 到标准
迭代和自有数据属性定义，以及迭代期间 length 变化的行为。包括这些差异引起的
getter/setter 调用与异常。本轮只登记谓词，不实现展开恢复；该前提不授权把任意
已有数组修改改为新数组，或丢弃可观察索引结果。
