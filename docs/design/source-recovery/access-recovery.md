# LD/ST 与访问协议的通用恢复方案

状态：设计提案，待用户确认；不是已实施能力或验收报告。
日期：2026-09-27。
基线：[T00–T12 实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)。
本方案细化 T01/T02/T07/T08/T09/T10/T11，不替代原结构 gate，不限于 pasteboard。
所有阶段独立实施，不使用 CC 或子代理。测试代码与生产代码分离。

## 1. 目标与边界

- 在统一输入身份、语义 IR、CFG/SSA、环境/效果证明、区域 AST 和 printer 上补齐访问指令。
- 覆盖当前、wide、deprecated 和 runtime 前缀形式；每项按 container/ISA/semantic profile 分别判定。
- 普通函数、箭头/lambda、方法、构造器、generator、async、async generator 共用函数主体流水线。
- GET 不一律是读取：iterator、namespace、template、arguments、resume 是协议操作，独立建模。
- 不把 ST 一律翻译成赋值，不把 LD 一律翻译成可复制的无副作用表达式。
- 不新增样本分支、源码文本后处理或专用 body writer，不读 sourcesMap 或配对源码作为引擎输入。
- 未证明的行为用类型化原因拒绝，保留真实未完成项；拒绝测试通过不代表正向支持完成。
- 仍遵守进程 1,000,000,000 字节上限；本方案不恢复 RSS/基准任务或函数调度器开发。

## 2. 方案选择

1. 逐 opcode 增加 writer：局部快，但重复 scope/effect/异常逻辑，不能接受。
2. 所有访问统一成泛化 Load/Store + 属性字符串：抹平 binding/property/define/protocol 差异，不能接受。
3. **采用类型化访问族 + 共享证明与 AST**：少量语义族复用通用 passes，保留真实差异。

不立即重写现有所有 ValueOperation。先增量补齐缺失节点，把共用的目标、效果和证明契约提取出来；
已有节点逐项迁移，原始输入对应检查与现有路径作为差分基准。避免同一操作存在两套权威语义。

## 3. 全流程

```text
ABC + pinned profiles + file identity
  -> decode: opcode / widths / pool operands / PC
  -> typed semantic lift: target / operation / operands / completion
  -> original CFG + normal/exception/resume edges
  -> register SSA + environment flow + creation occurrence graph
  -> typed access facts + conservative alias/effect dependencies
  -> verified copies / dead values / scoped access recovery
  -> evaluation plan with reference-conversion and suspension boundaries
  -> regions + FunctionTree / ClassTree / ModuleTree
  -> independent operand / scope / phase / effect / origin checks
  -> immutable verified owner -> unified printer
  -> source + PC positions + per-method/record receipts
```

读取和写入不自行拆分 basic block。只有真实分支、异常相位和 resume owner 改变控制结构，
由现有 CFG/phase 基础设施负责；变换后重算或失效相关 revision 证书。
源码作用域不等于 basic block，不得根据临时寄存器的范围凭空生成 JS 块作用域。

## 4. 覆盖目录与类型模型

建立编译期/生成的 typed AccessSpec 表，以 Opcode + profile 为键，不能按 opcode 文本分派。
每项记录格式、target kind、语义操作、输入角色、accumulator 行为、效果及实现阶段。
覆盖目录与解码 ISA 对照，新增 opcode 或未分配 family 导致明确遗漏报告。
decode/lift/value IR/AST/behavior 五个阶段分开记录，不能以单一 supported 位代表完成。

| 语义族 | 代表形式 | 不可丢失的信息 |
|---|---|---|
| 标量搬运 | LDA/STA/MOV/常量 | 定义/使用、值域、原始 PC |
| 词法 cell | LD/STLEXVAR、环境 push/pop | 环境链、深度、slot、动态实例、TDZ |
| 模块 binding | LDLOCAL/LDEXTERNAL/STMODULEVAR | record、canonical binding、live/read-only、初始化 |
| 全局访问 | TRYLD/TRYST、LD/STGLOBALVAR、ST*GLOBALRECORD、LDGLOBAL | 名称查询/对象属性/声明初始化不能混淆 |
| 普通属性 | LD/STOBJBYNAME/BYVALUE/BYINDEX | receiver、key 类型、Get/strict Set、转换相位 |
| own/define | STOWN*、DEFINEFIELD/PROPERTY、WITHNAMESET | 描述符、原型行为、函数名字副作用 |
| receiver 特例 | LD/STTHIS*、LD/STSUPER*、private | this owner、home object、brand、receiver 可用性 |
| 模块运行协议 | namespace、lazy、sendable、patch | loader、共享/隔离语义、热更新身份 |
| 获取/恢复协议 | iterator、template、arguments、resume | 缓存身份、迭代器关闭、挂起/恢复 completion |

内部使用目标专属 variants，不采用任意字段组合：
- BindingTarget：LocalSymbol / LexicalCell / ModuleBinding / GlobalName / GlobalRecord。
- PropertyTarget：Ordinary(base,key) / Super(home,receiver,key) / Private(brand,receiver,slot)。
- PropertyKey：Name / ComputedValue / FixedIndex；FixedIndex 不直接降格为 ComputedValue。
- 操作区分 Read、Write、Initialize、Define；协议有独立节点，不塞进通用赋值操作。
- InitializationState：Uninitialized / Initialized / PathDependent，不能用 undefined 表示 TDZ。
- 所有引用使用强 ID 和 owner；上述名称是设计类型，不要求另建重复的现有 SymbolId/CellId。
- 原始 IC/pool/slot 字段保留在输入身份和来源表；非源码语义字段不得成为 JS 操作数。

## 5. 求值、引用和效果

现有 normal/before_throw/exceptional 效果继续使用。访问证明另外区分：
读取输入值、构造 Reference、检查 receiver、ToPropertyKey、Get/Set/Define、正常提交。
相位是 typed 枚举/记录，由具体 runtime profile 定义，不能假定所有指令共用同一顺序。

关键反例：原 ABC 先计算 RHS，然后 STOBJBYVALUE 才执行 key 转换。
若直接把 RHS 内联成 `obj[key] = sideEffect()`，JS 的 key 转换可能先于 RHS，改变行为。
因此 EvaluationPlan 必须检查操作内部边界，而不只是所有 operand 之后的一个效果点。
不能证明安全时保留 RHS 快照；独立 verifier 拒绝跨 key 转换的错误内联。
现有固定两个 ExprId 的 lowering scratch 需要按语义最大固定 arity 扩展或改用有界 span；
不能因此把调用参数塞入固定数组，调用继续采用独立变长方案。

- Getter/setter、Proxy、key coercion、prototype lookup 均可能执行用户代码或抛出。
- 值调用和 Reference 调用分开；属性读结果内联为调用不能偷偷补上 this。
- binding 初始化与更新不同；异常出口不能获得未成功读取的结果。
- ST 指令是否保留 acc 按实际 profile，不让 JS assignment 的返回值反向改变 VM 数据流。
- own define 不能用普通 assignment 代替；使用对象/类定义语法须满足新鲜性、未逃逸、顺序证明。
- 不依赖可修改的 Object/Reflect 全局补语义。必须调用 intrinsic 时需要显式可信宿主能力，默认不具备。

索引读取已知边界见 [property-indices.md](../../reference/property-indices.md)：
v7 的部分 uint32/Proxy 路径可能传入数字 trap key，字典高位索引也有差异。
先保留专属语义，在 receiver/path 证明成立时输出标准访问；其余保持类型化拒绝。
deprecated computed read 还有寄存器/acc slow-path 差异，不能按新格式的语义替代。
未来若提供可信适配运行时，必须是显式的新输出模式及契约，不能静默加入 helper。

## 6. Scope、闭包与数据流

复用 EnvironmentFlow、CreationBinding、CaptureCells、FamilyWrites 和 scope Euler 索引。
静态环境分配点不是动态实例：同一 PC 在循环迭代/重入中可能创建不同 cell。
cell 身份表达为 owner/分配点/slot 与实例关系，分析不枚举无限动态实例。
depth/slot 先解析为环境目标，再映射为源码 SymbolId；绝不把 slot 名直接当源码变量。

- 每个函数创建 occurrence 单独绑定 capture；同一 method 的多次创建不能共享错误环境。
- 父写子读、子写父读、兄弟 closure 读写保留共享 cell，而非捕获时值快照。
- 逐迭代环境、catch 参数、默认参数环境与 body scope 分别处理；同名变量靠 ID 区分。
- Arrow 的 this/arguments/new.target 绑定到外层 owner，普通函数保留自己的 owner。
- 使用稀疏 use-def、邻接表和有界 worklist；跨函数摘要按 SCC 求解，未知调用保守失效。
- 别名集合保守合并；不能证明不别名时，禁止读复用或写消除，仍可输出有序源码。
- 第一版不实现激进 load CSE/DSE；SSA 只消除证明为纯搬运的值，不能消除可观察的存储。
- 禁止每变量遍历整张 CFG；scope/创建边统一建索引。数据流迭代不虚称线性，超预算返回 typed failure。

## 7. 异常、generator 与 async

复用 T09 的 Before/Commit/Catch 相位；T10 扩展为协程 owner 共用层：
CallableKind 增加 generator 和 async generator，主体仍走同一个 FunctionTree builder。
ResumeKind 区分 Next/Throw/Return 以及 async 正常/拒绝恢复；不是普通 CFG 正常边的别名。

- Yield/Await 是效果和可用性边界，不能把 cell/global/property 读取跨边界移动。
- 挂起期间外部 closure、宿主回调或其他 generator 可修改共享状态；恢复后按原指令重新读取。
- snapshot 值可以跨挂起保留，live binding 不能因此变成 snapshot，反之亦然。
- `.throw()`/`.return()` 进入对应异常/finally 流，保留待完成 completion 与 finally 覆盖规则。
- `yield*` 需完整委托/IteratorClose 协议，不能降成普通 yield 循环而省略 return/throw。
- async generator 同时处理请求顺序、await/yield 与关闭语义，不因已有 async 支持就默认通过。
- AST 增加 YieldExpr、AwaitExpr、Try/Catch/Finally 与必要的 typed completion/owner 元信息。
  printer 只写语法，不识别状态机。未认证的 resume 协议不猜成 async/generator。
- 合法 JS/合规宿主、正常返回/异常不暴露 VM 标记及资源耗尽排除沿用既有版本契约，不扩大。

## 8. AST 与发布

复用 MemberExpr/AssignmentExpr/SymbolExpr/DeclarationStmt，扩展 typed Super/Private target、
有序 PropertyDefinition/ClassTree、模块声明及协程节点，不新增 RawJs/TextExpr 逃生口。
binding 的 DeclarationKind 由初始化和写证明决定，不根据 ST 名称猜 let/const。
全局对象属性不一定等同全局词法名称；LDGLOBALVAR 目前有值 IR，但现有 AST 明确拒绝。

每个访问节点必须闭合：输入身份、target owner、类型/值域、初始化、正常/异常相位、
求值顺序、外层名称解析及 origin 映射。证明失效必须重验，不接受陈旧 revision。
getter/setter/捕获的自然名字不能遮蔽真实模块 binding；模块输出保留 live 读写和初始化时点。
调用产生的子函数、类和模块依赖全闭合才发布 complete；局部成功仍标 incomplete。
合并后的多个 PC 通过 origin DAG 关联源码节点，纯搬运消除使用 typed elision，不能丢 PC。

## 9. 测试架构与判据

最新请求要求增加测试覆盖。本提案将针对新增功能的单元/集成测试列为实施必需项，
不恢复周期审视、全语料性能测试或 RSS。当前仅设计，尚未运行新增测试。

目录按现有惯例分离：
- tests/unit/analysis：格式、typed lift、值 IR、异常相位、环境数据流。
- tests/unit/source：evaluation、scope/capture、AST 对应检查、origin、错误证书拒绝。
- tests/integration：ABC -> IR -> AST -> 源码全过程，独立执行宿主与行为比较。
- tests/fixtures/access：受控输入与期望；tests/support：构造和 oracle，不进生产 include。
- tests/corpus：真实应用及静态分类；保持既有 hash/revision 缓存，不改原始 ABC。

三层 oracle 不能互相冒充：
1. 编码与 IR：手工独立预期覆盖 operand/width/PC/寄存器/效果，不能只调用同一个 lifter 做对照。
2. 语言语义：受控源码编译为 ABC，反编译再执行，比较完成值、异常、事件顺序和最终状态。
3. 运行时语义：可用 pinned Ark runtime 时比较同一输入的 Ark 行为和生成 JS；不可用则记录未验证，
   尤其 indexed/deprecated/sendable 等不能以 Node 自比较代替 runtime 证据。
对 identity 用对象关系/alias 图比较而非 JSON 值；Symbol、BigInt、NaN、-0、undefined、hole 独立表示。
异步用确定性 deferred promise/事件记录控制推进，不用固定 sleep 猜顺序；不执行未知应用副作用。

| 编号 | 必须覆盖的场景 | 核心断言 |
|---|---|---|
| A01 | 每个窄/宽/deprecated/runtime 编码、profile、不完整输入、错误 pool/寄存器 | 正确 lift 或精确拒绝，无越界、无格式混同 |
| A02 | index 0/200/201/65535/2^31/2^32-1；宽度溢出 | 不截断/符号扩展，区别 format 与 runtime 语义 |
| A03 | computed key 的 Symbol/BigInt/-0/对象转换；base/key/RHS 各自有副作用 | 顺序、次数及异常前已发生的效果一致 |
| A04 | inherited getter/setter、只读属性、primitive/null、Proxy trap/revocation | strict Set/Get/Define 不混同，无额外读/调用 |
| A05 | own define、__proto__、getter 冲突、WITHNAMESET、逃逸对象 | descriptor/原型/函数名正确，错误折叠被拒绝 |
| A06 | if/loop/backedge/phi、同名嵌套块、参数/body/default scope | SSA 可用性及词法解析正确，无跨 block 错误搬移 |
| A07 | 父子/兄弟 closure 双向修改、重复创建、递归重入 | 共享与独立 cell 实例均正确 |
| A08 | 循环逐迭代 capture、catch capture、TDZ、闭包逃逸后调用 | 生命周期/初始化正确，不捕获快照 |
| A09 | lambda/arrow vs 普通函数的 this/arguments/new.target；constructor super 前后 | owner 正确，未初始化 this 不提前读取 |
| A10 | module 循环依赖、live export 更新、只读 import、重命名/遮蔽 | 初始化时点、读写目标与 TDZ 异常正确 |
| A11 | 全局 lexical/object 同名、缺失名称、只读属性、宿主全局对象 | lookup/initialize/set 分离，契约未被隐式扩大 |
| A12 | getter/key/setter 抛任意 JS 值；catch 读旧值；nested finally | result 仅正常提交，completion 覆盖正确 |
| A13 | generator yield 前后读写、外部改 capture、多个实例交错 next | 该快照的保留、该重读的重读，实例不串扰 |
| A14 | generator throw/return、finally 内 yield、yield* 委托关闭 | 恢复 kind、finally 和 iterator 关闭一致 |
| A15 | await resolve/reject、期间外部写 cell/module/property、嵌套 async arrow | 恢复后状态正确，不跨 await 合并访问 |
| A16 | async generator 请求队列、await+yield、提前关闭、for-await | 请求/效果顺序及完成值正确 |
| A17 | getter 同步重入 closure、写后读 alias、未知调用修改状态 | 摘要保守，无错误 CSE/DSE |
| A18 | 修改 target/role/phase/owner/revision/PC 的 IR/AST 负例 | 独立 verifier 拒绝，拒绝产物不携带 complete receipt |
| A19 | 200/201、多层 scope、大量 sibling closures/操作、预算耗尽 | 有界工作/存储，精确失败，无逐引用全图扫描 |
| A20 | 各 paired 应用 + batch 留出集、历史 profile、静态分类缓存 | 样本无特判；decode/lift/AST/行为分开报告 |

必须有组合正例而非各特性孤立通过：
- loop 中创建箭头闭包，修改共享 cell，经过 computed setter 重入，catch/finally 再读取。
- generator 在两次 yield 之间读 module live binding，外部 closure 修改，随后 return 经过 finally。
- async 方法捕获 block cell，await 前后访问同一对象，拒绝路径中修改 cell，父函数观察更新。
- async generator 委托迭代期间修改 capture，消费者提前 return，验证 finally 和关闭顺序。
每例都有独立行为 oracle、实际多层通路收据及 PC 映射检查；打印 goldens 只是辅助。

## 10. 实施分解与完成标准

以下是设计级工作包；批准后补具体任务计划，不将整个系统一次性重写。

| 包 | 交付/主要位置 | 验收与依赖 |
|---|---|---|
| L0 | typed AccessSpec/覆盖目录；analysis access contract；测试清单 | 所有相关 ISA 形式有归属和分阶段状态；A01/A02，不假报源码支持 |
| L1 | value_import/operands、evaluation_plan/verify、expression_forest/tree_operands；补 computed Set | 首个副作用函数完整走 IR/AST/source；A03/A04/A18，包含 RHS 转换顺序反例 |
| L2 | fixed-index/deprecated 特殊节点与 receiver/path 证明 | 对支持域有正向源码证据，对差异域有拒绝证据；不把仅拒绝算完成 |
| L3 | binding 初始化/更新、ModuleTree/全局语义接入 | A10/A11；实际 record 名称、TDZ、live-write 闭合，解开现有 family 发布阻点 |
| L4 | environment/capture/SCC 摘要接入统一访问事实 | A06–A08/A17；父子写、逐迭代、重入组合正例，共用普通函数主体 |
| L5 | own define、this/super/private、ClassTree；T08 | A05/A09；实例字段与函数签名整合，无构造器专用正文路径 |
| L6 | phased importer、异常 region 与 AST；T09 | A12，所有访问失败相位与旧值可见性；为协程提供 completion |
| L7 | generator/async/async-generator owner、AST；T10 扩展 | A13–A16 及组合场景；未识别协议不伪输出，依赖 L4/L6 |
| L8 | namespace/iterator/template/arguments/sendable/lazy/patch | 各协议独立规范/fixture/runtime 证据，未闭合者保持明确工作项 |
| L9 | RecordTree 与跨样本验收；T11/T12 功能部分 | A18/A20；原结构 gate 分项通过，保留 RSS/性能验收待恢复 |

L1/L3 是最近优先交付，不因 L2 的特殊 runtime 差异阻塞所有普通访问。
L4–L7 是完成用户复杂场景的必要工作，不能用简单 lambda 成功替代 generator/async。
每个包先写失败测试，再实现、运行针对性验证并提交；设计确认前不开展生产实现。
预算、规范和 runtime 证据不足时记录真实缺口，不继续叠加样本专项输出冒充覆盖。

## 11. 总体验收

完成要求是每个计划支持的 profile/形式具有明确正向或约定外边界、完整 typed lift、
独立检查、合法 AST、真实源码产物及适用的行为证据。异常、closure、resume 组合必须通过。
所有 ABC 指令都有对应阶段结论，不以“未在 pasteboard 出现”省略；静态 ABC 单独分类。
L0–L9 的代码/测试通过仍不替代 T00–T12 原验收，尤其实例字段/全局/签名 gate、
完整 P3、跨应用支持与进程 1G 实测。测试暂停或 runtime 不可用的项不能标已验证。
