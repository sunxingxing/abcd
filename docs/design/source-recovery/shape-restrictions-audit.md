# 按用户要求进行的恢复条件审计

2026-10-03，基线 87176c10。此为本次明确请求的审计，不恢复定期审视。
目标是查出把编译器局部形状当作恢复准入条件的实现，并规定后续通用替换方式。
本次覆盖 src/include/tools 的样本身份文本搜索，并人工检查调用引用、条件表达式、
谓词快照、rest/派生构造器和异常类集成的条件。不是所有生产分支已逐行审计。

## 结论

此次文本搜索没有发现以 pasteboard、WindowUtil、Immersive、PushMessage、
EntryAbility、Widget、hilog、JSON 或 sample 路径进行生产语义分派的实现。
仅有 fixture/sample 的说明性注释。文本搜索只能支持这一有限结论，不能证明
没有隐蔽特例。以下确实存在的是以局部布局或子集形状限制恢复能力的实现。
它们不能被称为语言语义要求，也不能继续靠追加形状匹配扩充。

## 需要通用替换的条件

| 优先级 | 位置 | 当前限制 | 应替换的通用机制 |
|---|---|---|---|
| P1 | src/source/call_plan.cc::VerifyCalls | member callee/call 同块或已有 cross-inline 证书；property 必须被原局部 forest 内联 | Reference 保留 base/key/callee 的真实定义与使用；通用依赖和效果顺序安排该引用与所有参数；CFG 块划分只作分析载体 |
| P1 | src/source/phase_selection_calls.cc::Check/Ancestors | 一个调用恰有一个 phi selection，最终 root 恰有一个 selection；前缀只穿单入/单出 corridor | 为整个调用建立有序参数/引用依赖图，一次索引、联合安排多个条件参数，显式处理 phi 和异常快照；不增加 selection 个数的枚举分支 |
| P1 | src/source/predicate_snapshots.cc::Candidates | 只接受两种 property read，最早使用必须是 strict comparison 并直接内联到 branch | 按值定义、使用普查、支配和不可重放效果建立通用 snapshot；条件语法只决定表示方式，不决定哪些 producer 能保留身份 |
| P1 | src/analysis/rest_arguments.cc::ProveRestPrologue | 前 N 条恰为规定顺序的 ABI MOV，再在 copied/copy+1 固定位置取 rest 和 STA；拒绝 try | 按 typed ArgumentFrame、SSA origin、def-use 与 CFG 证明参数搬运和 rest 来源；寄存器重排、插入纯操作、分块不改变语义 |
| P1 | src/analysis/spread_field_constructor.cc / derived_constructor.cc | 直线构造器、终结 return、规定 prologue 起点及有限状态序列 | 在通用 CFG/SSA 上分析 uninitialized-this、super result、绑定和 guard 的状态转换；证明全路径义务，不用某个线性指令模板定义默认构造器 |
| P2 | src/source/logical_regions.cc | 特定 branch/ToBoolean 邻接、两入 phi 和限定 arm 布局作为结构候选 | 通用控制依赖/区域恢复先形成表达式区域；布尔/选择语义从 typed predicate 和 phi 得出，局部模式仅作为可删除的快路径 |
| P2 | src/source/class_tree.cc::CheckClassParentScope | phased class 必须无 parent cells/modules、无 initializer/defined-member projection | 复用完整作用域/捕获/模块/异常对应接口；这些是尚未贯通的接口，不是 class 语义禁止条件 |

这些条件不等于已经查证的样本名称特例。它们的共同问题是把实现能力边界与
语义正确性混在一起，使等价的不同布局需要追加新匹配。修复不能简单删除
if/Require 后放行；应删除其“恢复准入依据”角色，以共同分析证明真正的义务。

## 必须保留的语义义务

- 原 callee、receiver 和 computed key 的身份；getter 与参数的实际求值顺序；
  不重读 getter、不改变 receiver、不引入可变 .call/.apply lookup。
- 单次/条件执行和共享值身份；一个定义多处使用不能通过重复副作用恢复。
- 原作用域、创建 occurrence、文件/profile/revision、捕获和模块初始化的来源。
- 正常/异常路径上的副作用与快照、TDZ/Hole、this/super 生命周期。
- 固定运行时与目标语言的实际差异，例如 Proxy 构造、数值/tag 边界；没有证明
  时保持明确缺口，不借布局巧合或 debug 类型当作运行时保证。
- 有界资源和真实复杂度。避免二次扫描应靠联合索引/调度，不靠“仅一个参数”的
  硬门槛冒充完整算法；所需输出本身的成本也必须计费。

## 本次实际动作与下一交付

已撤回尚未提交的 predicate snapshot ValueInvoke 白名单扩展。它虽不是样本
名称分派，也能复用现有顺序验证，但只是扩大局部 producer 列表，未解决上述
通用 snapshot/reference 设计缺口，不作为本次请求下的最终修复。

AGENTS.md 已加入通用恢复规则：禁止以样本身份和局部形状追加恢复分支，明确
区分语义不变量与临时实现边界。后续首项实施是共同调用 Reference/有序求值
模型，覆盖 normal/phased、多参数选择、getter、复制/分块变化和异常快照。
需要以原始定义/使用和 CFG 验证，不能用生成文本、样本名称或局部 opcode 模板
反推答案。结构等价变换的回归应与真实失败 case 并行验证。

当前 pasteboard record4767 仍未恢复，不宣称这个问题已修复。阶段一和 object
creator 全组目标保持原范围，完整语义与源码风格验收仍未达到。

## 规则 66 的首项替换

CallPlan 不再检查 property/call 的同块关系或手工枚举 cross-flow 例外。已有
共同 EvaluationPlan 的新鲜独立验证先证明可用性、定义/使用、单次所有权及效果
顺序；CallPlan 再检查原 property result、callee use、base/receiver 身份及使用
关系。并未通过删除引用身份或顺序义务来放行。

predicate snapshot 的 producer 类型白名单已删除。候选能力来自共同
EvaluationShape：可形成表达式且输入语义为 eager；正向事件区间重新核对
producer 子树和所有干预效果。于是调用结果、其他表达式与 getter 复用同一个
快照机制，而不需为每种 opcode/样本加入匹配分支。尚有 earliest-use/strict
comparison/branch 的局部候选范围，未宣称完整替换所有 snapshot/CFG 边界。

真实 pasteboard record4767 和 MultiCommunity record40957 均从 failure 进入
incomplete；原 callee-before-JSON.stringify 的调用已恢复。测试覆盖返回对象/
primitive/nullish、getter/方法改写、receiver、抛出值及连续1/2/200/201表达式。
七类审计事项仍有开放项，后续不能再靠枚举局部模板扩大接受范围。

## 规则 67：ValueTree rest 来源证明

共同语法解析不再调用固定 MOV/COPYRESTARGS/STA ProveRestPrologue 模板。
新 ProveRestOwnership 从当前已验证 ValueProgram、原 body 和 owner frame 出发：
定位独立 rest allocation；反向 CFG reachability 检查前驱效果和重复执行；原
instruction correspondence 检查隐式 physical argument 输入没有被提前覆盖；
SSA def-use worklist 检查原始最后声明参数只能做透明 transport，不能变成
native rest array 的可观察标量使用。GETUNMAPPEDARGS 仍是独立快照。

重排 ABI copy、NOP、改变 rest 暂存局部位置都不需要新生产匹配分支。文件内
真实 method/family 经过共同签名、AST、ABI 和 printer，48 complete families /
24 rejected protocol cases 的原生对照通过。200/201 NOP 与资源/工作预算测试
验证这个来源机制。

当前接口处理 normal ValueIR。旧 legacy semantic writer、FindRestLocal 和
spread/forwarding constructor 专用分析仍含旧模板；此项未宣称已删完。
完整 CFG 的异常 rest 和派生构造器状态分析继续沿用本审计的通用替换要求。

## 规则 68：已有存储值的共同语义分类

选择表达式的 retained-input 判断不再漏掉更早汇合的 phi。共同
HasRetainedEvaluationValue 按 ValueDefinition 的穷尽类型及 EvaluationPlan 的
实际存储表示，识别参数/receiver、phi、catch binding 和 operation temporary；
调用 receiver 也复用该接口。原始值可用性与 phi-copy 验证仍须先成立，查询
本身不证明 dominance、不移动定义，也不允许重放 producer。

同一 arm 内尚需执行的 producer 仍走原 arm-owned evaluation 检查，不能靠
“已有临时变量”隐藏该 arm 的效果。此为共同表示分类的遗漏修复，未增加新的
opcode、寄存器、PC 或 selection 数量匹配。normal 的先前 phi→后续选择已
贯通，1440 native /1584 paired方法场景通过；异常相位的选择/reference组合
仍有缺口，record5374未恢复。七类审计事项并未全部关闭。
