# 原调用帧转发与隐式派生构造器

## 语义依据

参考版本由 docs/reference/openharmony-v7.0-release.lock.json 固定。
ets_runtime 的 slow_runtime_stub.cpp::SuperCallForwardAllArgs 取得实际父
构造器，stubs/runtime_stubs-inl.h::RuntimeSuperCallForwardAllArgs 检查
IsConstructor，从原 frame 复制参数并调用 Construct；解释器提供原
newTarget 和实际参数数量。这条指令不取得 Array iterator，允许 runtime
支持的 bound/Proxy constructor。它与 super spread 的独立类型契约不同。

ECMAScript 隐式派生构造器使用原调用参数列表构造当前父类，可承载这种
整函数等价性。显式 super(...args) 会增加可观察的 iterator 访问，因此
不可作为原 frame 转发的默认打印方案。默认构造器的 source length 为0，
不能把 VM 预留的物理参数槽数当作源码形参数量。

## 显式语言构造模式（规则129）

用户允许`--value-source --language-constructors`，按原生super语义恢复普通
SuperSpread，可独立组合语言数值selector。父类可随调用改变，不需要终生
JSFunction假设；这不是原frame转发的隐式构造器证明。默认模式仍保留
SuperSpread的品牌/数组语义差异，apply也保持独立义务。实现、独立对照及
未验收边界见[语言构造语义](language-constructors.md)。

## 共同证明与表示

SuperArgumentSource 区分 explicit 和 invocation-frame 输入。共同 frame
fact 保留实际参数数量/尾部的所有权；EvaluationShape::NeedsArgumentFrame
驱动构建与独立验证的 census。constructor 保留 SSA protocol input，
正常参数快照和异常输入分析复用同一所有权接口。

ProveImplicitConstructor 使用原 CFG、共同 DerivedValueStates 和规格生成的
寄存器访问窗口。稀疏 copy/phi 传播证明 constructor 是 CallableEntry，
this guard 消费实际 undefined 或 super receiver；CFG传播证明每条正常
路径恰好转发一次并返回其receiver。原参数/newTarget 在转发前不得被未证明
的写入改变。其他操作必须无可观察效果且不能抛出；转发本身的构造副作用、
异常和 parent/newTarget 行为由语言原生构造语义承担。

正常路径可包含纯操作、不同复制寄存器和多个互斥转发站点。没有 prologue、
邻接、同块或固定站点数匹配。循环、异常相位和额外效果尚无整函数等价证明；
前置 frame 写入后恢复也尚未由共同版本化读证明。这些是公开的覆盖缺口，
不能增加 opcode序列匹配来绕过。

FunctionTree 的 ConstructorForm 保留 explicit/implicit 语义。原表达式、
语句、参数和来源仍保留供验证；tree operands 核对实际 FileFunction/identity
并重新证明。ABI保留内部 frame事实并验证隐式 source length0。class printer
省略显式 constructor，输出来源注释，FunctionFragment::kImplicitConstructor
明确区分没有显式函数正文的源表示；原来源范围映射到该注释。额外 captures/
modules 的表示目前未贯通；不能默默丢弃。

## 算法与验证边界

### 显式派生构造器的共同发布（规则86）

PreparedFamily 的 callable 检查不再要求显式 DerivedConstructor 的
receiver_guards 非空。BuildFileFamilyTrees 对真实 ClassContext 下的同 owner
构造器已调用 ProjectDerivedConstructor：原初始化 CFG/SSA、当前输入/IR、
receiver 和所有实际 guard 的覆盖与输入均在该通路检查。完整树随后经过
语义、流、ABI、来源、捕获和整族写入验证。发布层消费这条既有构建通路，
不以 guard 的存在或数量增加第二重恢复门槛，也不新增证明包装/接口。

空 guard 列表并不提供捕获 receiver 事实。没有同文件 descendant preservation
时，仅寄存器初始化可能成立；可写或混合 cell 不能成为 immutable receiver。
子函数实际 initialized guard 的省略仍须初始化、创建时支配和完整传递捕获
证据。不满足时拒绝该省略/完整源码，不将 guard 缺失推断为 this 已初始化。
当前 frame/parent brand、spread 展开、异常与多 super site 的独立缺口未改变。

序列化文件测试覆盖三个 profile 的60组完整构造/字面量/分支捕获、成员调用、
传递捕获与
1/200/201/402复制变化，以及21组写入/混合来源/早期创建/未初始化 guard
负例。空列表伪造 guard、陈旧 cell 事实、修改当前 store/来源等负例继续
新鲜复验；不能用缓存 revision 相等代替协议证明。新增独立原生对照覆盖
7686组无 guard 构造、父返回/Proxy、getter/descriptor、成员receiver、传递
闭包和抛出值。
这些是生成完整 constructor fragment 的验证，不证明任意导入父类、整份
真实 record 或阶段一所有输入已经完成。

本次仅删除常数时间的重复门槛，复用既有共同构建成本；无新依赖、算法或
许可证事项。没有恢复暂停的RSS、性能、全套或扩展语料测试，当前证据及
固定五输入产物见 status.md。

### 隐式转发的复杂度与既有验证

CFG 使用 Kahn 拓扑调度，其来源是标准有向图拓扑排序；值状态使用既有稀疏
单调数据流。V为值数、U为使用数、B/E为块/边数、I为指令及其规格访问窗口
数量：工作 O(V+U+B+E+I)，内存 O(V+U+B+E)，受 SemanticBudget 限制；没有
逐变量CFG遍历或成对匹配。无新增外部依赖/许可证需求。未重新测量全语料
峰值RSS或基准，不能用局部预算计数宣称满足整个进程1G上限。

forward_derived_projection_test 经共同输入、值、projection、evaluation、
FunctionTree 和普通 ABI 验证原ABC与变形/反语义fixtures。forward_constructor
_source_test 只向引擎提供ABC，独立TS进入Node对照：185组构造参数/身份/
newTarget/Proxy/bound/iterator/返回/异常场景及70组async方法场景。
两份新模块的语法及固定五输入的无回退比较已验证；完整 instance fields、
宿主平台和阶段一所有模块的独立语义/风格验收仍开放。
