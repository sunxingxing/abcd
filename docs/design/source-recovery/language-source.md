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
| 派生类字段初始化按源语言 `[[Set]]`/define 解释 | 待定 | 16 个 record（`DerivedProjectionError`） | 未实现 |
| 惰性导入 `callruntime.ldlazymodulevar` 按普通 import 绑定输出 | 待定 | 11 个 record | 未实现 |
| 数组展开 `starrayspread` 按源语言 `[...x]` 输出 | 待定 | 7 个 record | 未实现 |

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

## 2–4. 其余三项

批准范围已登记，实施时在此处补充各自的现象、前提、实现与测试。
