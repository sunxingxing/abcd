# 绑定恢复、控制结构与源码树

2026-09-28 修订：[FunctionTree 与部分输出设计](function-tree-output.md) 规定最终
AST 直接进入共同 printer，允许缺口标记；不要求先封装 VerifiedFunction 或冻结为
通过全部验证的树。下文语义约束继续指导已恢复代码，无法恢复的区域按新设计留缺口。

状态：拟实施合同。输入来自 [Value IR](value-ir.md)，阶段与任务见
[实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)。
本文的 SourceTree 是新的最终表示；现有 `SourceAst` 保持旧名称与旧路径，不改名冒充迁移。

## 1. 四类结构必须分开

| 结构 | 身份与职责 | 反例 |
|---|---|---|
| CFG | WorkBlockId/EdgeId，执行与异常可达 | 一个 if 不是一个基本块 |
| Value graph | ValueId，每个标量定义及使用 | 同一寄存器复用不等于同一局部变量 |
| ScopeFacts | ScopeId/CellId/OwnerId，绑定和动态身份 | env allocation site 不等于唯一运行时环境 |
| Region/SourceTree | RegionId/StmtId/ExprId，结构化语法 | 打印花括号不能凭空建立正确 TDZ |

ScopeFacts 在值变换前提供语义限制，声明位置在控制结构冻结后决定。
不从 dominance tree、PC 区间、debug 名称或花括号反向猜共享 cell 身份。

## 2. 绑定模型

### 2.1 Symbol 与 Value 不同

```text
Symbol = (SymbolId, ScopeId, BindingKind, NameEvidence, Mutability,
          InitializationRule, optional<CellIdentity>, OriginId)
BindingKind = Parameter | Local | Captured | CatchParameter | Import
            | ModuleLocal | FunctionSelf | ClassSelf | CompilerTemporary
ValueLocation = InlineExpression | SymbolValue | ConstantValue
CellIdentity = (CreationContextId, EnvAllocationId, CellSlot)
TemporaryReason = MultipleUses | EffectSnapshot | PhiJoin | ParallelCopyCycle
                | CrossRegionValue | ReferenceSnapshot | UnprovedInline
```

名字是必要文本；内部引用永远用 SymbolId。debug 名称必须有有效范围/slot 证据，
无证据时使用稳定短名及 NameEvidence::Synthesized，不声称恢复了作者局部名。
成员名字不能被当作当前 scope 的变量身份；绑定同名不意味着可合并。
每个剩余合成临时量在证据中有 reason，可供后续通用能力扩展，不写进源码的说明注释。

### 2.2 参数与默认值

参数 entry value 绑定到 ParameterSymbol。identity copy 传播后直接引用该符号，
不再生成 `$r3 = parent` 及没有其他用途的 `$r3` 声明。
参数存在写入时区分该参数的当前版本与旧值快照：

```js
const previous = value;
value = next;
consume(previous);
```

不能把 previous 的 SSA 旧值替换成后来读取的 value。合流可使用显式局部，
首期不强制把所有版本合成同一个变量。

默认参数 `p = -1` 的恢复需复用并扩展现有参数前导证明：判定只在 p===undefined
时执行、默认表达式执行顺序、同参数后续引用绑定、函数 length、rest 及参数环境。
`p = undefined` 与没有 default 并不在所有情况下相同；参数环境、arguments 映射及
可见性不能因输出更短而改变。无完整证书时保留函数体内的默认赋值，不伪造签名。
箭头/严格模式/非简单参数的 arguments 行为按 profile 和函数事实证明，不凭默认猜测。
将前导迁入签名要为原 PC 保存 SignaturePlacement 来源，不以“PC 消失”当作死代码。
sloppy mapped arguments、direct eval、with 或其他动态绑定观察不能被忽略。未取得
严格性/动态作用域证明时，不采用只对静态不可观察参数安全的 alias/scope 改写。
`.mjs` 隐含严格模式；输入 callable 若不兼容目标 strictness，须选已证明的输出上下文
或拒绝，不能仅改变文件后缀/加 wrapper 就认为严格性差异消失。

### 2.3 this、构造器与捕获

统一 FunctionTree 保存 CallableKind、strictness、ReceiverOwner、NewTargetOwner、
HomeObjectOwner、参数和 body。构造器是 CallableKind 的一种，不开新的优化/printer。

- 普通函数 this 是其调用 receiver；箭头 this/new.target 引用外层 owner。
- 派生构造器 receiver 有 Uninitialized/Initialized 状态，super 正常完成才初始化。
- super 之前不能读取 this；多路径 super、提前返回对象/原始值、重复 super 各自验证。
- 从内部 sentinel 到 this 的搬运只在 receiver 初始化证明及所有用途已分类后消除。
- 捕获 receiver 的 cell 若会被后续 child/父方法写入，不能直接改成词法 this。
- 捕获的值域为 language 并不证明捕获值恒等于 this。捕获替换需 identity 和写入证明。
- 保留 v7 super range 的限制；原有 Proxy 前提拒绝不能通过普通 `super(...args)` 掩盖。

共同派生构造器初始化分析现同时支持 range 和 spread 的单次正常调用协议。
spread 另以原始寄存器/局部 SSA 的稀疏来源传播证明 acc 是本次调用的当前构造器；
错误 newTarget、混合来源 phi、重复调用、错误 guard、回边和异常区域继续拒绝。
旧 range 接口仍只接受 range。ProjectDerivedConstructor 按实际原始协议选择分析，
并重新核对当前工作图的 callable 身份、构造器/数组用途、cache 元数据、效果与
guard 输入；该结果已接入既有函数族构建入口。原始分析可合并等同来源的 phi，
当前工作投影仅跟随已有纯 copy，未扩大工作 phi 接受域。
初始化事实不授权数组展开或 native super spread；父类运行时品牌、newTarget、
源码参数和数组展开仍须各自满足既有合同。目前 EvaluationShape 保留
ValueSuperSpread的两个真实用途与效果；ExpressionForest在读取VM-only callable
之前拒绝整个操作，不输出native spread。真实 EntryAbility 的初始化投影通过
不等于构造器源码完成。显式AllowPartial可保留已有直线、无捕获leaf的可靠前缀，
在整个未完成表达式起点标记缺口。类、record和导出清单传递incomplete状态；
完整FamilyEmission在写入前拒绝没有完整ABI/source证据的成员。

### 2.4 Cell 与 scope

复用 LocalAccessPlan、初始化证明、CreationBinding 的逐创建点 rebasing。
EnvironmentFlow 只给 topology；depth0 可能是未知外部环境。循环中同一个分配指令
可产生多份动态环境，必须保留逐迭代 cell 身份及闭包共享关系。
环境父链与源码 scope tree 不能按数量直接对应；编译器环境可以映射到函数/块，
但只有在 TDZ、分配时点、捕获和写入一致时才能把 cell 下沉为源码绑定。
每个闭包 creation occurrence 都要证明捕获已初始化或保留原先 guard/TDZ 行为。
不以“父函数最终赋了值”代替创建时/使用时的初始化证据。

### 2.5 声明放置

先为参数、imports、真实 cell 固定合法 owner，再处理纯编译器临时量。
临时量可在共同词法祖先声明，但赋值保留原执行点；声明不能创造新的可观察
TDZ/捕获身份。LCA 只是候选，不是合法性证明。使用 scope ancestry 索引查询，
不对每个变量重新走全部 CFG。没有证明的用户绑定不提升到函数头。
const 需唯一初始化、无后续写入且可合法放置；否则 let。不以“编译器只写一次”
推断原作者 const。名称分配按 scope 索引检测冲突，禁止遮蔽依赖的 intrinsic/模块绑定。

## 3. 区域、phi 与去 SSA

### 3.1 先控制，后声明

`RegionPlan` 输入工作 CFG 和完整控制事实，输出 Seq/If/Loop/Try 等候选及
`EdgePlacement`：Fallthrough、ThenExit、ElseExit、LoopBack、Break、Continue、
Return、Throw、ExceptionTransfer，另有 MethodEntry 对应的 EntryPlacement。
每条可达边恰有一个解释；同一源块的两条到同一
目标的边也不能合并身份。不可达代码的丢弃须有 Reachability disposition。

首期直线/if 模式不改变 CFG；支持提前 return 的分支，不仅 diamond。
循环阶段复用已验证 dominance/loop 信息；保留每次条件求值、continue 的目标以及
循环退出路径。不为恢复 `for` 搬动可能抛出的更新表达式。
不可约控制流或 owner 不能覆盖的边显式拒绝，不复制任意子图来“凑结构”。

### 3.2 非平凡 phi 的下降

```text
for each live phi in a supported join:
  allocate a distinct provisional PhiJoin symbol
  for each incoming key:
    append parallel copy (symbol <- incoming ValueId) to its edge or entry bundle
group bundles once by EdgeId
place copies at the edge's proven structured exit
if a normal critical edge has no unique placement:
  split that edge in one batch; rekey phi inputs; invalidate CFG-dependent plans
schedule each parallel-copy bundle using dependency counts and cycle temporaries
rebuild affected control analyses once; validate all edge placements; freeze
```

首实现不做 pairwise interference graph 或全局 coalescing。phi 使用替换为 symbol；
不同 phi 可以保守用不同 symbol，正确性优先于最少变量。每条边的 RHS 基于该边
原状态同时读取，循环交换必须产生一个必要 temp，不变成破坏性的顺序赋值。
copy planner 在最终绑定计划前创建临时 SymbolId，后者负责其声明位置/名字。
MethodEntry bundle 不能放到虚构 predecessor；入口 phi 初始化只执行一次，而 backedge
copy 每次循环执行。入口放置需证明与默认参数前导、scope/cell 初始化和捕获顺序一致，
不能固定“全部放默认参数之前/之后”。不能证明时 typed reject；T05/T06 联合验证这类输入。

批量拆边只拆原计划中需要的普通边，新增节点/边与该集合有界对应；不重复寻找新的
优化机会。拆分继承控制/handler 上下文，合成 origin 指向原边/phi；不是伪造原 PC。
如果重建后结构化仍失败，返回拒绝，而不是继续拆边直到预算耗尽。
异常边不得放普通赋值；异常状态下降由 phased handler/catch plan 处理。
G1 剩余 live 非平凡 phi 必须拒绝，不能把 phi 打印为变量却遗漏边赋值。

## 4. 表达式与求值规划

### 4.1 目标

`EvaluationPlan` 给每个 retained op 唯一动态求值归属和必要引用关系。
它回答“在哪个控制位置求值几次”，不是递归遍历 def-use 图打印表达式。
单次使用仅是候选条件；定义支配使用不能证明允许跨效果、条件或异常边界移动。

内联至少满足：同一执行条件/次数；值在使用处可用；handler/suspend/初始化上下文
一致；没有复制 observable effect；保持 JS 运算符/reference 求值顺序；输出大小有界。
G1 仅纯标量树和保序相邻效果；G2 才扩展调用与跨语句的已证明组合。

多次使用的 effectful 结果必须 materialize 一次；纯 DAG 也不能指数展开。
给节点计算展开成本并设共享预算，超过预算保留局部。成本统计只累计一次及必要边数，
不先展开大字符串再发现超限。

### 4.2 必须保留的反例

```js
if (params.pathStack !== undefined) {
  this.pathStack = params.pathStack;
}
```

两个 `params.pathStack` 是两次 getter/Proxy 读取，不能合并成一次快照。反过来，
原程序若只读一次后复用同一值，不能复制成两次表达式。

```js
const snapshot = object.value;
mutate(object);
consume(snapshot);
```

snapshot 不能内联为 `consume(object.value)`；也不能把原 if 内调用提到外面。
getter 为真/假或抛异常时的读取次数分别计算，不能只看正常最终结果。

### 4.3 调用 Reference

`CallPlan` 区分 DirectCall、PropertyCall、SuperCall、Construct，并保存 receiver、
callee 获取点、home object 和参数求值次序。`obj.m(args)` 会在求值 args 之前读取 m。
原字节码若已提前取得 callee，而中间操作可能修改 obj.m，不能在后面重新读取。

不要改写成 `callee.call(receiver, ...)` 或 `Reflect.apply(...)`，除非已另行获得可信
intrinsic 绑定及正确调用协议证明；`.call` 和 Reflect 本身可被篡改。
可融合时使用原生 member call 并证明 callee 获取及参数效果顺序一致；复杂参数若只能
通过 IIFE/箭头包裹，必须先证明 this/arguments/new.target/await/return 及作用域不变。
正常流条件参数的两臂可以是已验证的完整字面量初始化：同块LiteralPlan的最后
definition索引界定唯一未暴露对象的表达式范围，phi只排除真实own-definition
base use。独立求值及树检查仍核对初始化、选择和成员引用顺序；任何额外效果
或初始化后的观察屏障仍拒绝融合。对象语法、嵌套分配和getter留在原选择臂，
不在合流后重新读取成员，不借助可篡改的.call/apply/Reflect或包裹函数。
同一对象参数内允许普通属性与条件属性交错：按实际definition顺序维护对象效果
区间，选择只在对应属性处推进header/join。先索引对象首个选择，避免后续属性
重复扫描整个对象；局部已验证字面量的nested/initializer操作不重复计数。
仍保留构造参数、成员getter、原receiver及重复键的所有RHS效果；对象提前暴露、
共享消费或额外效果阻隔不能靠这种组合消除。
条件属性位于嵌套staged对象时，可以按唯一外部消费点组成对象归属树。只有真实
父属性的stored RHS消费可以连接父子；一次顺序索引根归属，再按原操作顺序合并
各组定义。每个allocation维持自己的效果区间、末定义、ObjectExpr和属性来源，
不合并对象身份。父层或子层的共享、提前观察及效果间隙仍须保留或拒绝融合。
字面量依赖树可能先构造后面的闭包；FunctionBinding须按原creation OpId查找，
不能用AST构造游标代替身份。保留child owner、capture映射及完整创建计数，
运行时求值顺序仍由原位置和效果检查决定。
正常流共享分支的短路decision也保留module read/guard配对：guard与read映射同一
表达式，保留在条件RHS内，不补独立语句或提前读模块。TreeFlow先核对配对的
准确ExprId，再核对decision位置；不改变相位异常路径的staging/handler要求。
嵌套条件可以保留独立的内外层phi：内层完整选择的结果仅有一个活跃使用，且
经过自身只含纯跳转的join进入父phi时，记录确切forwarded UseId并组合为子表达式。
保持原CFG、两层phi及边身份；内层边和父转交边分别检查，不把内层所有叶边当作
父phi输入。含副作用的join或共享结果仍保留原控制结构；方法读取在参数之后时
保留参数快照，不能为了成员调用外观把getter提前。
本计划不把这种包裹作为万能降级。无法忠实表达的 staged reference 明确拒绝，
保留诊断，不能因为降低临时量优先级而输出错误调用。
Call 节点另有 `EvalMode = NotEval | DirectEval | IndirectEval` 及 binding 证书，
不能把名为 eval 的普通 SymbolRef 自动当 direct eval。首个调用阶段不支持的 eval/
动态 scope 形态明确拒绝；S04 中这些负例不能误当普通 direct call 通过。

### 4.4 最终死代码清理

Value IR 阶段删除纯死值；树阶段仅删除无引用的 synthetic declarations、证明冗余的
空语句/纯 literal statement。调用/getter/store/guard 未使用不能删除。
树引用计数、declaration census 与 origin disposition 一次建立/维护，不再按变量逐次
扫描整棵树。保留 return undefined 或改变写法均需语义及映射理由，不以行数为指标。

## 5. 最终 SourceTree 数据合同

### 5.1 表示方式

函数级 arena + 强类型索引 + `std::variant`；父子关系用 ExprId/StmtId/SymbolId。
不同函数有不同 arena owner，跨函数只通过 FunctionId、CaptureBinding 和 owner facts。
树节点不存任意语义字符串，不依赖旧 SourceStatement/staged indices。
ValueId 仅允许出现在验证/来源侧表，不能成为最终表达式运行操作数。

```text
FunctionTree
  identity, CallableKind, ReceiverOwner, NewTargetOwner, HomeObjectOwner
  function_symbol?, parameter_scope, body_scope, parameters[], body: BlockStmt
  symbol_table, expr_arena, stmt_arena, origins, closed_obligations

Parameter = (binding: SymbolId or proved PatternId, default?: ExprId,
             initializer_context: ParameterScopeId, is_rest)
Expr = Literal | SymbolRef | This | NewTarget | Member | Call | Construct
     | Unary | Binary | Logical | Conditional | Assignment | Update
     | Await | FunctionExpr | ClassExpr | ObjectExpr | ArrayExpr
Stmt = Block | Declaration | Expression | If | While | DoWhile | For
     | Return | Throw | Try | Break | Continue | Label | Empty
Target = SymbolTarget | MemberTarget | proved PatternTarget
Member = (base ExprId, NamedKey or ComputedKey or proved PrivateKey)
Call = (DirectCallee or MemberCallee or SuperCallee, EvalMode, ordered arguments)
ObjectExpr = (ordered PropertyDefinition[], proved prototype behavior)
PropertyDefinition = DataProperty | Getter | Setter | Method | proved Spread
ArrayExpr = (ordered ValueElement or Hole or proved Spread, length facts)
Try = (body BlockStmt, catch?: (symbol?, BlockStmt), finally?: BlockStmt)
ClassTree = (name?, heritage?, ordered members, home-object/capture facts)
RecordTree = (path, imports[], ordered module items, exports[], function owners)
```

这是目标节点目录，不表示 G1 全部支持。各节点在其所属阶段解锁；构造器必须有
接收者证书，Await 必须有 owner 归一化证书。无法建合法节点时拒绝，不用 RawCode 节点
绕过。未知字节码不能以 `/* ... */`/空函数体伪装成功。
简单参数环境与 body 可经证明共享 scope；非简单参数/default 的 initializer 解析上下文
必须独立表达。T06 迁移前验证默认表达式每个 SymbolRef 的可见性、前后参数 TDZ、
body 局部不可见性和 arguments 关系，不能仅因原前导给参数赋值就认定可搬到签名。

对象/数组初始化由 T07 的 LiteralPlan 接入：先证明分配身份、未提前逃逸、属性定义
顺序、descriptor/prototype，再合并为字面量。`DefineOwnProperty` 与 setter-triggering
assignment 不是一种效果；任意对象上的 own define 不能伪装为 Member assignment。
无可用源码构造或可信 intrinsic 证书时明确拒绝，不偷偷生成可篡改 Object.defineProperty。
array hole 不等于 undefined element，`__proto__` key 也有特殊字面量语法风险。

### 5.2 Tree verifier

检查顺序：

1. ID/arena owner 正确、没有循环 child graph、每个 executable 节点只有一个语法 parent。
2. 所有 SymbolRef 可见且绑定唯一；声明/TDZ/初始化/capture 映射满足 ScopeFacts。
3. Target 合法；break/continue 指向活动 label/loop；return/await/super 与 CallableKind 合法。
4. live phi、UnknownInput、staged call、寄存器、未决边不能进入树。
5. 每个 retained effect occurrence 在树中唯一归属；分支/循环路径对应 RegionPlan。
6. 子函数/类树已验证，所有 creation-site capture rebasing 和 closure-write 义务闭合。
7. 参数/default/body 的迁移与 source contract 一致，origins 没有无解释缺口。
8. 节点数、深度、来源边及预计输出容量在预算内。

深树遍历用显式栈或已命名深度限制，避免恶意 ABC 耗尽反编译器宿主栈。
G1 的 verified function slice 可被旧 record 外壳嵌入，但不能继承 whole-record verified 标记。

### 5.3 过渡包装适配

T03 在 base_function 输出 header/VM 声明之前提供 typed `FunctionEmission`：完整 callable
身份、ABI 使用 census（parameter/receiver/self/new.target/arguments）、签名事实、正文
tree/生成段、method/child origins。新树的局部声明只来自 BindingPlan，不再让旧
register_uses/accumulator_used 扫描生成一组 VM let。
首片不支持 capture/template 时直接拒绝；T07 完成后由已闭合树提供捕获绑定，不把
旧 PrintedTemplate 的未闭合义务伪装成源码树。T06 扩展默认签名及签名 span 运输。
适配产物失败同样服从 RequireValueTree，不可回退旧 writer 补足 header/body。

## 6. Printer 必须纯化

当前实现增加条件捕获 AST：`TreeCapture` 用本树 `SymbolId` 别名指向
`CapturedSymbol{FunctionOwnerId, SymbolId}`；别名属于参数环境之外的
`ScopeKind::kCaptures`，种类为 `BindingKind::kCapture`。该 scope 不生成源码块，
别名不生成 `let`，读取/写入仍使用统一的 `SymbolExpr` / `SymbolTarget`。
树结构检查处理真实遮蔽；值对应检查保留 incoming cell 与标量快照的区别。
父级目标存在性、名称相等、创建位置的可见性、闭包写入与依赖闭合仍须单独验证。
条件父函数也可构建 `FunctionExpr{child, captures}`：每个创建节点拥有子函数归属与
父级符号的隐式捕获边，而不是运行时参数。作用域检查在真实创建位置解析这些符号，
操作数检查核对对应的父级词法单元。创建节点暂时固定在原有序位置，不跨环境操作内联。
独立 `VerifiedFunction` 发布与 printer 显式拒绝未闭合捕获或子函数的树；printer
在写入前拒绝创建节点。不能把条件父/子树检查通过视为完整闭包源码已经贯通。

类根使用真实 ClassExpr 锚点，constructor、buffer 与 defined member 共用
CaptureCells、source aliases 和完整 family writes。ConstructedFamily/PreparedFamily
拥有 root_capture，区别于 intra-family incoming 边；不保留父 tree/context 指针。
写入检查重新核对实际创建点初始化、父子 revisions、别名对应及全部后代原始写入，
保留模块初始化检查；它不授权 immutable snapshot。最终父树在 ClassExpr 的
活跃作用域解析每个实际 root capture，并检查后代 global/module/宿主辅助绑定
是否被外层词法声明遮蔽。未准备的 layout 仍不是完整正文。
共同 printer 传入实际 ExprId/parent syntax 到 PreparedFamily 的目的地检查，
不推断 CFG/cell，也不在每个 member 重放整个父图。缺失外部目的地的捕获类
不能经独立 PrintClassTree 输出；完整 record 仍需父级语义、写入和模块闭合。
TestAbilityPage 的配对 TS 模板插值在自定义 ToPrimitive 对象上与 ABC ADD2
存在已观测差异，不能为模板风格改写已确定的默认提示加法语义。

原生字面量的嵌套 RHS 可复用共同 GlobalNameExpr 接纳 kName 全局读取，
保持每个读取及后续属性 getter 的实际求值点。完整初始化的单次消费、未暴露、
效果顺序和异常处理器/捕获快照检查不变，kVar 仍不能伪装为名称读取。
实际 WidgetCard 的 Flex.create({direction: FlexDirection.Column}) 因此走共同
对象表达式及成员 Reference；全局/属性 getter 不能导致重新读取先前取得的
callee/receiver。配对验证仅在独立测试执行选定回调，不是平台渲染证明。

模块别名使用独立 `ModuleOwnerId`、`ModuleSymbol` 和 `TreeModuleAlias`，不复用
函数 owner，也不把 ABC slot/pool ID 放入可执行 AST 操作数。作用域链为
`module -> captures（可选）-> parameters -> body -> blocks`；模块别名属于
`ScopeKind::kModule` / `BindingKind::kModule`，没有函数内声明或初始化快照。
`ModuleAccess::kReadOnly` 禁止通过导入绑定赋值，但不意味着其值恒定；可写本地
模块绑定用 `kMutable`。统一 SymbolExpr/SymbolTarget 和作用域激活表检查实际遮蔽。
构造器之外的普通函数也使用同一结构；参数/捕获/模块三类身份不能互相替代。
模块 owner 内目标唯一性采用排序检查，不做符号两两比较；名称仍走共享预算和
命名冲突索引。已有参数/临时/单元 SymbolId 不重排，模块别名追加到符号表末尾。
当前只有条件 AST 构造/结构对应，模块操作数降低和 record 目标/TDZ 证明仍待闭合；
独立 VerifiedFunction 与 printer 显式拒绝带模块别名的树，不能借名字输出全局引用。

`PrintFunctionTree`/`PrintRecordTree` 只消费已验证 immutable tree、NameTable、输出预算。
不接受 MethodBody/SSA/常量 reader 以便偷偷补反编译；不触发子方法反编译或 capture replay。
职责：语法优先级/结合性、括号、字符串/标识符转义、缩进、token/span 输出、错误传播。
所有名字在打印前分配，printer 不决定 let/const、参数默认值、是否 inline 或作用域。

特别规则：保持 -0/NaN/Infinity 的合法表示及所需 intrinsic 绑定前提；字符串 escape
正确；对象字面量表达式语句括号；return/throw 与换行；`??` 和 `&&/||` 混用括号；
幂运算与 unary 左 operand；computed key 执行顺序；保留 direct eval 与 indirect call 区别。
未支持的语法组合拒绝在建树阶段，不由 printer 临时改结构。
生成 UTF-8 bytes offset 作为主坐标；line/column 单独明确编码单位，不能把字节列冒充
标准 source map UTF-16 列。本阶段产物是 PC 映射，不声称等同消费者标准 source map。

## 7. 来源与生成位置

`OriginArena` 以 DAG/区间共享多来源，不在每个节点复制所有祖先 PC。记录类型：
OriginalSite、Combined、SignaturePlacement、ElidedPure、ControlEdge、SyntheticBinding。
每个原 PC 具有保留/组合/纯消除/输入协议归一化/不可达等明确 disposition。
执行效果和控制来源必须有实际生成位置或合法结构边位置；不能全部塞进文件首个零宽 span。
纯 copy 被消除可只有 elimination record；不能伪造对应执行语句。

生成端以 token/节点输出事件记录 byte 范围，保持 method identity；融合表达式允许
一个节点对应多个 PC，一个原操作也可关联多个必要 token 范围，但不能因此重复执行。
零宽锚点只用于合法无文本来源，并记录种类，不充当缺失效果的证据。
参数默认值/嵌套函数各自 PC 原点保留，拼 record 时使用段偏移运输，不重扫 JS 推算。
验证 PC 边界来自输入指令表；拒绝跨方法 PC 混用和超出输出 byte 范围。

## 8. Record、类、全局与模块

RecordTree 消费真实 module/import/export 元数据及 entry 的顺序执行树。
保留 live import/export binding、初始化顺序、循环依赖与 guard；禁止把模块读取
改成顶层 const 快照，除非已有不可变性证明。命名冲突通过 SymbolId 和名称表处理。
显式源 import type 若已擦除不补；用户提供的配对源码不能进入恢复决策。

类保留 heritage 求值位置、computed keys、static/instance member 次序、home object、
constructor/capture 关系。`this.x = v` 不能无条件变成字段 `x = v`：两者可能在 setter、
属性定义语义及执行时点上不同。无证明时保留构造器赋值，字段布局作为独立事实。
动态 global 属性访问不等于 lexical let；仅在 GlobalAccess/初始化证明支持时建立声明。
record metadata 字段、寄存器和 VM cell slots 不是源字段名/全局变量。

当前结构 gate 的证据必须同时覆盖路径/imports/类归属/实例字段/全局/完整签名/函数体；
部分正文失败时 publish incomplete 状态与拒绝清单，不能仅因壳完整而标 executable。

### 已实现的类继承表达式归属（2026-10-01）

共同 ClassExpr 的 optional heritage 属于创建类的外层 FunctionTree；ClassTree
继续持有构造器和成员各自的函数族。缺省 heritage 对应内部 Hole 与基类 callable，
存在的 heritage 对应派生 callable，包括原生 extends null。独立操作数校验对照
原始创建用途，结构校验将 heritage 当作实际子节点，防止遗漏或重复拥有。
打印时外层表达式与既有类体组合；公开独立类打印仍不接受缺少 heritage 的派生类。
依据 pinned ets_runtime/ecmascript/stubs/runtime_stubs-inl.h 中
RuntimeSetClassInheritanceRelationship，原生 extends 负责父类合法性、prototype
读取异常及两条原型链；不展开成手工属性搬运。新增遍历成本为该表达式子树的线性
访问，沿用已有节点/输出计费，无新增每成员全图扫描。

### 条件求值前导区域与后缀（2026-10-02）

ControlDecision 的 kRegion 前导保留 evaluate 到 predicate 的单入口区域，
允许内部条件值汇合，不复制共享的有副作用分支。区域树独立检查原控制边，
cell scope 遍历在 predicate 前停止，由所属 decision 唯一处理该节点。

当前 FunctionTree 接入的是能够闭合为原生值表达式的前导区域：内层选择
保留自身 phi/边身份，条件前导与分支内 PhiInput 后缀通过既有迭代建树工作栈
组合。相邻选择可继续供给下一个 predicate 或对象属性，成员 callee/receiver
仍须通过统一求值区间检查；模块 read/guard 配对不变。独立 TreeFlow 和
操作数验证分别核对效果、子选择边和 logical LHS 的精确 keep occurrence。

任意包含语句效果的非线性前导尚未接入语句 decision lowering，仍明确拒绝；
控制计划能够表示一个区域不等于源码树支持该区域的所有语句组合。
