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
| newobjapply | 2 | 尚缺 aggregate 参数展开证明，不能直接假设 iterator 行为 |
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
