# LanguageSourceV1：按源语言语义输出的四项前提

`SourceContract::kLanguageSourceV1`（序列化名 `language_source_v1`）由 CLI 的 `--language-source`
选择，必须与 `--value-source` 同用，并且不能与 `--language-arithmetic`、`--language-numeric`、
`--language-constructors` 组合。它包含 `LanguageNumericConstructorsV1` 的全部前提（numeric 算术、
原生 super 语义），并另外加入下面四项源语言前提。默认契约 V3 以及其他已有 opt-in 都不变。

**用户批准：2026-10-13**（会话中逐项列出影响面后，用户批准了全部四项）。代码中每项前提都有
各自的谓词，不得直接比较枚举值。新增或改变任何一项都需要新版本，并再次获得批准。

| 前提 | 谓词 | 影响（探索集，2026-10-13） | 状态 |
|---|---|---|---|
| 捕获读取按源语言 TDZ 解释 | `CaptureReads() == kSourceTdz` | 24 个 record（`ScopeError::kInitialization`，Language 读取） | 已实现 |
| 派生类字段初始化按源语言 `[[Set]]`/define 解释 | 待定 | 16 个 record（`DerivedProjectionError`） | 未实现 |
| 惰性导入 `callruntime.ldlazymodulevar` 按普通 import 绑定输出 | 待定 | 11 个 record | 未实现 |
| 数组展开 `starrayspread` 按源语言 `[...x]` 输出 | 待定 | 7 个 record | 未实现 |

## 1. 捕获读取的源语言 TDZ（CaptureReads）

**现象。** 模块入口（以及函数体）会提前创建顶层函数声明：先执行 `definefunc`，再执行
`stlexvar`。这些函数读取一个之后才首次赋值的 `let`/`const` 绑定。es2panda 按**源码中的先后顺序**
判断是否需要 TDZ 检查：引用在声明之后出现时，就不生成 `throw.undefinedifholewithname`。因此子函数
里是一次不带守卫的捕获读取（Language 域）。而在函数对象创建与首次 store 之间，可能夹着用户调用，
固定运行时契约无法证明读取一定发生在初始化之后，于是父函数的 `Rebase` 报
`ScopeError::kInitialization`。

**前提。** 不带 hole 检查编译出的捕获读取，就是源程序中对该绑定的标识符读取。与运行时唯一的
差异是：在首次赋值之前就调用该函数时，运行时读到的是 hole，而源程序会抛 `ReferenceError`。
在合法的原程序中，这正是源语言规定的行为。

**实现。** 只放宽父函数一侧，子函数的读取仍是 Language 值，不引入新的值域：
- `NormalScopeFacts::Rebase` 在该前提下，对 Language 需求采用与 Guarded 相同的延迟声明规则：
  分配之后执行的第一次写，就是该绑定的首次 store，并且满足同块在创建之后、或者是唯一的 store。
  owner 必须恰好在该 store 处声明这个绑定。
- `capture_cells.cc` 的 `Deferred()` 在该前提下接受 Language 绑定。其余延迟机制（`CaptureBinding.deferred`、
  `VerifyDeferredCells`、树中声明位置的独立验证）原样复用，见 `guarded-captures.md`。
- 父 cell 在创建时已有 store（例如 `var` 先存入 `undefined`）的情形不受影响。异常路径的
  `ExceptionScopeFacts` 不在本增量范围内。

## 2–4. 其余三项

批准范围已登记，实施时在此处补充各自的现象、前提、实现与测试。
