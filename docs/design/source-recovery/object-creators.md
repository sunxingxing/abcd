# 对象创建指令

2026-10-03：按用户要求优先补齐固定 `isa.yaml` 的 `object creaters`。
该组有 20 条签名、26 个编码格式。清单完整不表示实现完整。
ISA/runtime 身份遵循 `docs/reference/openharmony-v7.0-release.lock.json`：
runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`，
ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9`。

| 指令签名族 | 格式数 | 当前路径及边界 |
|---|---:|---|
| createemptyobject | 1 | 共同对象 AST，保留每次分配身份 |
| createemptyarray | 2 | 共同数组 AST；窄/宽 cache 编码 |
| creategeneratorobj | 1 | 需真实 generator owner 和完整暂停/恢复协议证明 |
| createiterresultobj | 1 | 本次贯通普通/异常相位 ValueIR、对象 AST 和独立验证 |
| createobjectwithexcludedkeys / wide | 2 | 尚缺对象 rest 的通用恢复及属性观察证明 |
| createarraywithbuffer | 2 | 共同数组常量及初始化计划；仅接受已证明元素语义 |
| deprecated.createarraywithbuffer | 1 | 显式拒绝；历史全局池布局不能当现代区域池 |
| createobjectwithbuffer | 2 | 共同对象常量及初始化计划；未证明的方法/访问器仍拒绝 |
| deprecated.createobjectwithbuffer | 1 | 显式拒绝；须独立历史 profile/池解析 |
| createregexpwithliteral | 2 | 尚缺 pattern/flags 解析和原生 regexp AST |
| newobjapply | 2 | 共同构造接口支持实际类创建来源与未逃逸的完整数组；未知目标/数组仍缺证明 |
| newobjrange / wide | 3 | 共同 constructor operands、CallPlan 和 ConstructExpr |
| newlexenv / wide | 2 | 共同环境/cell 生命周期，环境对象不会打印为 JS 值 |
| newlexenvwithname / wide | 2 | 同上，scope literal 是调试命名证据 |
| createasyncgeneratorobj | 1 | 需 async generator owner 和请求/恢复协议证明 |
| asyncgeneratorresolve | 1 | 需同一 owner 的完成协议；不可单独改写为 Promise.resolve |

## 公共接口

复用 `SemanticOperation` / `ValueOperation` 的类型化变体及 exhaustive visitor，
`SemanticEffects`、`OperandView`、`EvaluationShape`、`ExpressionConstants`、
`ExpressionForest` 和同一个 AST/printer。normal 与 Before/Commit/Catch 图共享
操作描述，后者另核对原异常归属。没有按 group 或样本新增 body writer。

按语义区分固定字面量分配、输入构造的记录、用户 constructor 调用、词法环境及
协程 owner 协议。它们共享上述接口，但不具有可互换的输入或效果契约；不为了
减少类型数量给所有操作加一个含可选字段的父类，也不从 opcode 名字派发语义。
新增创建形式必须接入这些接口和独立验证后才能计为源码恢复。

## 迭代结果对象

`LiftIterResult` 保留两个真实寄存器输入；共同 semantic lifter/importer 使用
`IterResult` / `ValueIterResult`，保留 allocation/may-throw 及正常完成才更新 acc。
`RuntimeCreateIterResultObj` 使用 `flag.IsTrue()`；`JSIterator::CreateIterResultObject`
按 value、done 顺序建立可写、可枚举、可配置的 own data properties。
这是新对象，每次执行均有不同身份，不能作为常量消除或复用。

`IterResultConstant` 在方法常量解析中仅建立一次语言规定的两个名字，所有创建
点复用其 NameId。forest 输出现有 `ObjectExpr` / `DataProperty`，独立 TreeFlow
检查属性数量、顺序和原创建来源，TreeSemantics 另检查实际键、输入及精确 true
转换。已证明布尔的 canonical operand 直接输出；其他 Language 值使用严格等于
true，无 truthiness 替代、用户 coercion 或可变全局工厂调用。

工作与额外空间均按操作/使用数线性计费；属性 arena 在构建前按固定 schema
数量准入。测试覆盖 1/2/200/201/402 次创建、三种既有动态 profile、普通/异常
图、直接/计算/布尔 flag、原始输入对应关系和伪造 AST 拒绝。native 对照独立
核对身份、descriptor、getter 顺序、异常值、全局工厂污染及 inherited setter。

## 后续义务

优先继续 regexp、对象 rest 和 apply 构造的共同表达式恢复。regexp 必须区分
pattern 在创建处的异常与非法源码字面量导致的 parse-time 异常；对象 rest 不能
为排除键额外读取 getter；apply 不能把按索引取数组参数误写成任意 iterator spread。
保留历史格式和完整 generator 协议矩阵，既有 owner 子集不算整组完成。
阶段一 fixed-five 的完整 record 验收仍独立，不以新增操作或测试数量替代。

### 阶段一实际使用与表达式组合

2026-10-03 只读检查固定五份 ABC 的全部方法，解码失败为 0。正则创建、
excluded-keys 和 newobjapply 未出现；缓冲对象创建分别为
524 /1480 /179 /62 /2 次，所有相关 literal 都没有 method/accessor tag。
检查读取 ABC 与固定解码/池/字面量接口，不读取配对源码，不改变上述完整覆盖目标。
证据 /tmp/abcd-creator-census-summary.log、/tmp/abcd-buffer-creator-census-summary.log。

迭代结果对象虽然不是缓冲 aggregate，仍是普通、拥有输入的创建表达式。
共同 LiteralPlan 的 expression 候选与 PhaseCorridors 现均识别 ValueIterResult；
原使用普查、laminar 初始化区间、异常签名及 AST 对应关系继续独立验证。
同一个 Lower / ObjectExpr / DataProperty 输出可嵌入空/缓冲对象的 record，也可
直接作为原生成员调用参数；没有新增专用 writer 或无类型创建父类。
共享/逃逸的 record 不获重复构建许可，缺失 nested ownership 继续拒绝。

## Apply 构造

两个 cache 宽度使用同一个 `ApplyConstructorCall` / `ValueConstructApply`。
输入仍分别为寄存器中的 constructor 与 acc 中的数组，不把数组当成一个参数。
固定 `RuntimeNewObjApply` 要求真实 JSArray，读取一次 ArrayLength，再依序
GetProperty 各索引；最后 `NewObject` 使用同一个 func/newTarget。它没有调用
数组 iterator，但其 Proxy 分支未先检查 IsConstructor：非可构造 target 的 Proxy
仍可能读取并执行 construct trap，普通 JS new 则先拒绝。这由固定
`runtime_stubs.cpp::NewObject` 和 `js_proxy.cpp::ConstructInternal` 源码确认；
没有将跨引擎普通构造器对照当成该边界的验证。

`CallArgumentUses` 建立一次 canonical 使用普查，跳过已证明的 alias 和 literal
receiver 初始化；stored self-reference、其他观察、phi/catch 暂存仍计数。
CallPlan 与最终 AST 语义检查分别使用原图重新检查，只有唯一外部使用的完整
新数组能恢复。另要求 constructor 的 canonical SSA 来源为实际 ValueClassCreation，
并保留其创建 OpId；原 ClassTree 的身份、字节码、完整成员正文与捕获校验继续有效。
这证明该值始终是创建出的类构造器，不假设任意参数/宿主返回值在生命周期内
保持 JSFunction。未知 constructor/getter 结果、数组参数、共享/逃逸数组和普通
对象仍分别类型化拒绝。普通函数创建、其他已证类型及完整 Proxy 差异待继续建模。

构造 AST 共用 `ConstructArguments` 类型化接口：显式 `ArgumentRange` 或拥有
原创建子树的 `ArrayArguments`。顺序证书允许内联时，现有对象 property cursor
按元素顺序打印为 native 参数列表，不产生 spread、Reflect.construct 或动态工厂。
原 array 节点和各 initializer 的来源区间保留；资源耗尽差异遵循既有 V3 范围。

constructor 取值之前存在较晚的 getter/其他原始效果时，保留原数组快照和效果顺序，用同一个
ConstructExpr / MemberExpr / ArgumentRange 输出按索引取参。独立校验核对静态
长度、从 0 开始连续索引、准确 snapshot 绑定和数组的唯一外部使用。
数组本身的创建与元素表达式留在原语句，不为外观把 getter 提前。
原生表示分别是 `new ctor(value0, value1)` 和
`const args = [value0, value1]; observe(); new knownClass(args[0], args[1])`。
未知 getter 直接提供 constructor 的形式不被这条顺序证明自动授予构造语义。

使用普查、array-size 索引和输出容量准入均为线性遍历；较长参数列表的
MemberExpr/数字 key/快照 leaf 在生成前逐项计费。既有 range 构造复用相同
AST/printer，原 API 不被 reinterpret 为 apply。完整组验证与任意数组恢复仍开放。

已准备、没有父 cell/module 依赖且没有 initializer/defined-member 投影的类，
通过计费的 CheckClassParentScope 进入同一 phased forest 与独立操作数验证。
没有擦除 exception 图来冒充 normal 输入；其他类仍需完整 phase capture/projection
桥。测试的 class constructor 正文由文件中实际字节码解码、准备，不是 placeholder。
低层 phase fixture 另建立明确的 handler 图，类创建保留在 protected region 之前；
这不代表实际 record 的 phased class reader 和捕获 gate 已全部完成。
