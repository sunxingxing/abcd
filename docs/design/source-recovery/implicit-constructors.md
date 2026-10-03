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
