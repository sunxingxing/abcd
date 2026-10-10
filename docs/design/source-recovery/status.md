# 项目进展与设计审查

更新时间：2026-10-10。设计基线见 [实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)。
本页是当前状态入口；[progress.md](progress.md) 保留历史证据，不覆盖本页结论。

## 更新规则

- 每个完整子任务提交同步更新本页：实际交付、构建证据、验证状态、缺口、下一步。
- 用户已取消定期设计审视，不再按提交次数或阶段切换安排审查；遇到具体架构风险时说明。
- 工作期间持续简短汇报；无运行中的会话时不承诺后台定时刷新。
- 不按文件数、提交数或接口数量宣布完成；必须以原计划的端到端验收为准。

## 当前阶段


### 2026-10-07：当前优先里程碑——统一变量恢复架构迁移

用户最新指令优先：停止为初始化、快照、循环、自增分别扩展准入条件。
规则172存储合并仅为迁移基线，不能宣称共同语义模型已完成。
下一闭环限定普通函数临时变量与循环携带值：复用已验证 CFG/SSA、效果、
异常与 TreeFlow 对应，源声明初始化显式接入；定义—使用/复制传播、
活跃性/干涉、死定义/效果消除、源码绑定/作用域恢复分别消费共同事实。
打印器只消费计划。必须退出对应旧局部判断的决策路径，不能仅新增接口。
复杂 finally、闭包环境重构与更精细暂停恢复保留原表示、报告能力缺口。
既定五输入、结构gate及暂停事项保持，原阶段一 Goal active。

2026-10-09用户最新优先级：规则251本轮pass完成后回到主干，主干完成前
暂停其他表现层优化。以下按完整交付结果衡量；规则280终验后状态为：

1. **未关闭**：原始IR/SSA接入：原始值、效果与源码恢复模型的完整对应。
2. **未关闭**：普通绑定生命期：local/catch在作用域进入、初始化、重复进入时的单元身份。
3. **已关闭（现有共同模型能力范围内）**：试算模型精度；当前/候选完整复用
   共同投射提供者，初始化、值域、效果、完成边与证明失效联合收敛。
   原始对应和绑定身份的模型缺口仍归第1/2项，不由试算另行猜测。
4. **未关闭**：多轮Pass收敛：依赖驱动的重跑范围、终止依据与成本控制。
5. **未关闭**：统一变量里程碑终验：全部职责共享事实、被替代旧判断退出、规定端到端验收。

这五项是主干交付闭环，不以局部规则或单项测试代替；第3项的关闭依据见下，
其余四项不随之关闭。下一主干是完整原始IR/SSA语义接入，既定暂停事项保持。

### 2026-10-10：规则282，内联与存储共用原始结果身份

- 原始结果身份撤回后，内联路径已放弃原始范围，存储路径却仍能借
  OriginalResultDefinition恢复该范围。新增已验证原始SSA测试观察到RED134。
  两条路径现共同消费FindOriginalResult；源提交必须携带同一原始值及
  已验证输入。缺失证明只撤销原始cap，当前语言Number事实仍保留。
- 已退出存储消费者绕过result_expression直接读ValueId.domain的路径；
  不增加语法/opcode/布局准入。新增查询为常数时间、无分配，已有遍历计费。
  测试覆盖撤回身份、不同原始值/源输入、移除操作/值，沿既有复制、拆块、
  arena反转、候选失效和200/201/400/800组合运行。
- 最终三个相关目标构建通过，3/3定向测试通过（11.07s）。固定ABC0
  （3.23s），19 records仍10 complete/9 no-module；10份源码语法通过且
  逐字节等同281。冻结文件及输入SHA未变。证据：
  build/stage-one/stored-result-identity-verification；RED日志为
  build/stage-one/stored-result-identity-red.log。构建只有既有
  literal_plan.cc:59的kNoop警告。未重跑完整63项或暂停的全套/语料/RSS。
- 第1项仍OPEN；本次统一结果身份消费契约，不等于原始操作效果、异常阶段
  与区域全部贯通。下一步仍围绕原始语义主干，五项状态不变，Goal active。

### 2026-10-10：规则281，纠正原始结果与源码覆盖的身份混淆

- 推进第1项时，真实解码的逻辑选择暴露错误对应：原始ToBoolean与branch
  共用逻辑表达式源码位置，旧恢复层却把Boolean结果当成整个选择的结果。
  已观察函数树断言RED（退出134）。这不是要求同块或某种指令序列的理由。
- CheckTreeOperands在既有值等价验证处发布独立result_expression；覆盖位置
  expression继续承担来源/控制对应。RecoveryFlow及共同值域求解只消费
  显式结果对应，不再从覆盖位置推断结果身份。控制组合保留原验证义务，
  不新增恢复端语法/opcode名单或局部布局准入。额外空间每操作一个optional
  ExprId，由已有数组预算计入，遍历维持既有复杂度。
- 最终abc_structure/function_tree_test/recovery_projection_test构建通过。
  相关3/3 CTests通过（11.22s），新增12源码/360独立行为比较/6反例，
  覆盖三个动态profile、AND/OR、源码arena反转；既有成功结果/试算失效及
  200/201/400/800检查同次通过。扩展测试开发中曾遗漏生成器命令入口及
  函数fixture的callable设置，均已修正；不把那些失败记录计为通过。
- 冻结固定ABC退出0（3.09s），19 records仍10 complete/9 no-module，
  十份源码语法通过且逐字节等同280。输入SHA及冻结文件未变。
  证据：build/stage-one/original-result-verification；RED日志为
  build/stage-one/original-result-red.log。最终构建仅见既有literal_plan.cc:59
  的kNoop switch警告。本轮没有重跑完整63选集，不能沿用280的结果作281证明。
- **第1项仍未关闭**：此处退出了一条错误身份推断；原始操作效果、异常阶段、
  区域与源码行动的完整对应尚未实现。五项状态仍为第3项关闭、其余开放，
  Goal active。继续这项主干，不转回表现层优化或扩大暂停的验证范围。

### 2026-10-10：规则280终验通过，关闭试算精度交付

- 第五次冻结验收：31目标构建0（44.38s），固定ABC0（3.15s），
  63/63测试0（1197.60s）；实际名称精确吻合、无重复。28份冻结文件、
  测试清单及输入SHA保持不变，最终构建日志无warning/error。
  19 records为10 complete/0 partial/9 no-module；10份源码语法通过，
  逐字节等同规则279，契约仍language_numeric_constructors_v1。
  证据：build/stage-one/joint-projection-verification/final-attempt-5。
- 交付包括共同初始化/值域/效果/完成反馈、原始证明依赖失效、有限工作队列、
  稀疏值依赖与预算计费、同一修订稳定发布；声明移动试算的独立初始化求解
  已退出决策路径。独立最终图/完整逐事件对照、正反布局、200/201/400/800、
  16组新增源码/480次执行比较/16反例和7个生产突变均通过。
- 本轮关闭第3项，不关闭完整原始对应、普通绑定身份、全局Pass调度收敛或
  统一变量里程碑。存储声明锚点/额外查询仍由共同初始化分析提供，未声称
  全部职责已共享同一事实产品；复杂finally等保留共同模型的能力缺口。
  原Goal保持active，不恢复已暂停的全量语料、全套测试或RSS工作。

以下保留本轮失败定位与修复证据，不能以最终通过掩盖此前失败：

- 当前/候选共用初始化、值域、效果、完成边与原始证明失效的稀疏队列。
  原始证明通过值域交集参与求解；没有新增样本、同块、邻接或布局准入。
- 已删除前置初始化求解、提前边压缩、无原始IR独立路径和保守图上先行
  原始证明失效传播。独立最终图分析仍作对照，不作生产重试。
- 默认投射测试通过；四个生产突变均构建成功且失败，恢复后通过。
  首次31目标构建0（199.21s），固定ABC因已销毁分析峰值被逐函数累加而
  在record8383预算失败，63项尚未启动。已按返回对象生命期分离峰值与
  保留空间，未提高预算；诊断重跑固定ABC0，10份源码仍与279逐字节一致。
  相关投射、初始化及类名字检查通过；真实SSA/源码测试的一条旧约定
  （无原始IR必留无用算术）已改为共同Number语义，未知输入仍保留效果。
  该两项重跑2/2通过（14.40s）。第二次冻结31目标构建0（29.37s）、
  固定ABC0（2.07s），10份源码语法及字节比较通过；63项未通过。
- 第二次验收在binding_initializer模型/源码两项失败（537.30/537.78s），
  随即停止，不能引用此前63/63结果作为本轮证明。定位为既有Alias27的
  自读取/复制用例；聚焦检查在400变量已失败。独立800变量诊断中，值域
  准备要求完整逐事件liveness，生成1210170条事实后
  在剩余48557104字节预算耗尽。不是语义验收失败的特判理由，也不能
  提高限额。证据：build/stage-one/binding-closure-budget-debug.log。
- 已将值依赖准备改为独立FlowValueStates：共同完成图上的稀疏求值图，
  持久绑定动作索引跳过不变传输，保留合流、成功写入、每条查询完成守卫、
  外部修改与单元重置。生产提供者不再要求完整逐事件活跃性；完整活跃性
  仍服务存储分析，并作为小图独立对照。未新增样本或布局准入，预算不变。
- 稀疏/完整模型逐项语义比较通过；原失败Alias27/28在200/201/400/800、
  正反布局全部通过。证据：value-states-growth-2.log及projection-2.log。
  三个稀疏索引突变与四个原始证明突变均构建成功并被捕获，恢复后通过；
  完成守卫突变首次存活，新增不可达读取反例后被捕获，未掩盖测试缺口。
  接下来重新冻结同31目标/63项/固定ABC；此前失败记录保留，不能用聚焦
  通过替代全闭环验收。第三次构建0/69.69s、固定ABC0/3.19s，测试启动后
  为删除声明移动试算的重复初始化求解主动停止（32.05s），不计测试通过。
  该消费者改用共同提供者发布的读取事件证书。
- 第四次冻结构建0/28.20s、固定ABC0/3.04s，63项完整执行为62通过1失败
  （1380.95s）。失败是binding_interference_source_test生成器60秒超时，
  不是已观察到的语义断言失败；单独重跑仍60秒超时，不能按并发偶发忽略。
  27份冻结文件哈希不变、实际63名称精确吻合；十份源码仍语法通过且等同279。
  无限时诊断相同生成器完成1152输出，耗时68.93s。仅用于失败定位，不放宽
  既有测试限额。已按实际条件证明消费者裁剪无观察者的失效依赖，并复用
  共同scope/loop转移避免一次转移重复判定能力；稀疏索引保持同一字典序。
  投影/恢复流2项通过25.30s；超时的源码测试单独重跑通过84.00s，包含
  1289源码、200156次行为比较、158反例。并发模型/源码2/2通过84.49s
  （模型58.66s、源码84.48s），原60秒子进程限额不变。七个生产突变再次
  捕获且恢复后通过；第五次全选集冻结复验已通过（见本节顶部）。
  本段诊断时五项仍OPEN；最终只关闭第3项。

### 2026-10-10：规则279验证完成；试算精度闭环仍未关闭

- 共同初始化分析向实际 lexical GetValue 事件发布证明，包括自增的独立读取
  阶段。效果细化消费该证明及 PutValue 完成证明；异常边由各阶段自身的
  SourceEffects 决定。已删除旧 expression-shape independent gate，转换
  异常及成功后写回仍由各自阶段承担，没有新增样本或布局准入。
- 已观察初始化读取的 RED；三个生产突变（漏读证明、未证明读取获准、
  删除转换异常）均构建成功且被测试拒绝，恢复后通过。生成源码新增
  4组/120次独立行为比较/4个TDZ反例；既有32组更新运算/2176次比较/
  96个反例同时通过。
- 冻结终验：31目标构建0（97.31s）、fixed ABC0（3.77s）、63/63测试0
  （1044.71s），名称完整无重复。六份源码/测试哈希、输入SHA及清单
  不变。本次构建日志无warning/error。19 records仍10 complete/0 partial/
  9 no-module；10份源码语法通过且逐字节等同规则278。
  证据：build/stage-one/read-phase-verification。
- 独立诊断已证实提供者内部精度缺口：`let number=+input(); let saved;`
  后在try中执行`+number; saved=7`、空catch、最后读取saved，最终图已
  删除plus异常边，但发布域仍非Number；在同一最终图上独立重算可得Number。
  证据：build/stage-one/projection-closure-probe.log。该诊断未更改冻结源码。
- 下一交付按“试算精度闭环”验收：当前与候选共同提供者内求解初始化、
  值域、效果与异常边的反馈依赖，证明终止与成本，发布一致事实，删除
  被替代的手工阶段重算路径，并完成性质、反例、独立执行及既定record验证。
  不以追加一次重算或固定轮数替代依赖求解。五项主干仍全部开放。

### 2026-10-10：规则278，同一投射上的无修改 Pass 稳定点

- RecoverySchedule 仅在未发生 Commit 的 Finish 记录该 Pass 的稳定 revision。
  其他 owner 的提交使旧记录失效；修改过计划的 Pass 不会自行证明新状态
  已稳定，后续请求仍执行。阶段切换清空记录，不能跨职责阶段复用。
- RunPassOnce 保留 Start、前置依赖、缓存释放及 Finish 协议，只跳过已经
  稳定的计算主体。清理组依然推进到最新 demand checkpoint，不会因
  跳过 owner 而悬挂。声明位置/循环声明更新均先 BeginCommit，已核对。
- 独立计数测试覆盖200/201/400/800重复请求（同revision只计算一次）、
  跨owner修改、自修改、非法阶段与提交期间查询。四份生产header副本
  突变（缺稳定点、旧revision、自认证、旧stage）均build0/test-6；原版
  build0/test0。没有修改正在验证的生产源码。证据stable-pass-mutants。
- 冻结终验：31目标构建0（203.42s）、fresh fixed ABC0（3.50s）、
  63/63测试0（1039.08s），JUnit名称完整无重复。4份源码/测试哈希、
  输入SHA及清单保持。构建暴露未改动literal_plan.cc:59既有kNoop
  switch警告，本次未顺带修改。19 records仍10 complete/0 partial/
  9 no-module；10份源码语法通过，逐字节等同上一规则277输出，同一
  语言contract。证据根build/stage-one/stable-pass-verification。
- 状态按固定Pass种类保存，空间O(P)，单请求比较O(1)，每个调度请求
  仍计入预算。本次消除相同状态的重复计算；仍是保守全revision失效，
  不宣称细粒度机会重跑或全局收敛已完成。五项主干均OPEN、Goal active。
  后续核查共同投射内部精度与收敛，其他优化/既定暂停事项保持。

### 2026-10-10：原始操作数/结果混合域退出准备路径

- 删除 OriginalFlow::normal_kinds、Join 及其结果/输入写入点。该表已无
  生产消费者，仍独立保留结果域与操作数域的混合提示。原始结果继续
  从成功定义/完成区间获得条件证明；OriginalInput 保留原始值、解析值
  和 use 身份。表达式/ValueId 边界检查保留，未增加恢复准入条件。
- 测试改为直接通过输入 occurrence range/ValueId 核对原始有限域；
  试算反例保留数值输入对应，仍要求当前到达值独立决定结果域。
  删除一个按表达式分配的冗余向量；没有声称整进程 RSS 改善。
- 冻结验证：31目标构建0（88.10s，无warning/error）；相关3/3测试
  通过（11.42s）：function_tree、recovery_projection、original_flow_source。
  fresh fixed ABC0（3.92s），19 records仍10 complete/0 partial/9 no-module；
  10份源码语法通过、逐字节等同上一规则277输出，同一语言contract。
  5份源码/测试哈希、输入SHA、选择清单保持。证据根：
  build/stage-one/raw-input-retirement-verification。此次未重跑无关63项全选。
- 此项没有改变恢复语义，保留规则277。完整原始动作/阶段/异常对应、
  普通绑定单元、完整trial、依赖收敛、终验五项仍OPEN；其他优化与
  既定暂停事项保持，Goal active。

### 2026-10-10：规则277，前置依赖由调度器驱动

- PassContract 声明消费者前置职责，RecoveryBarrier 声明阶段边界。
  Prerequisite 只返回下一位owner；执行器完成其独立分析/提交后再次
  查询，随后才启动消费者。Start/Arrive 仍核对依赖和阶段，错误阶段
  在任何依赖执行前拒绝。旧EnsureDemandCleanup及手写三步清理退出。
- 依赖图按职责完成推进，没有整函数重试或固定轮数。已有清理可中途
  继续；snapshot/binding-values缺失时先执行对应owner，再完成必要
  清理。清理组在最新revision发布检查点，自身提交不导致反复入队。
- 定向3/3通过（22.92s）。32种提交/no-op组合覆盖消费者、边界、
  未完成清理、值阶段、非法阶段/提交期间查询；200/201/400/800次
  请求产生600/603/1200/2400次依赖执行。两个生产突变（遗漏依赖、
  旧检查点）均build0/test-6，恢复后build/test0。调度步骤计入原额度。
- 冻结终验：31目标构建0（96.74s）、fresh fixed ABC0（3.85s）、
  63/63测试0（1057.22s），JUnit名称完整无重复。4份代码/测试哈希、
  输入SHA及清单保持；构建无warning/error。19 records仍10 complete/
  0 partial/9 no-module，10份源码语法通过、逐字节等同规则276，同一
  语言contract。证据根build/stage-one/pass-prerequisites-verification
  （final-result.json、final-junit.xml）；fresh输出pass-prerequisites-final-rule277-verified。
- 本次关闭调用位置手工保证既有前置依赖的路径；没有宣称全局机会
  收敛或五项主干完成。细粒度失效/重跑、终止与整体成本继续，Goal
  active；其他优化和既定暂停事项保持。

### 2026-10-10：规则276，pass 分析一次发布完整共同产品

- CurrentProjection 一次发布 RecoveryProjection（expressions、flow、
  initialized_reads）。所有 pass 只读取 const 子视图；BeginCommit 在
  计划写入前销毁整个 owner，最终输出才移交打印向量。退出分立缓存与
  InitializedOccurrences，避免在同一revision内由flow查询覆盖表达式精度。
- 实际旧路径中，ElideInertOperands 可在候选遍历中触发这种覆盖；现在
  遍历前取得同一完整产品，保守中间分析仅留在共同提供者内部。没有
  新增条件匹配、恢复pass、整函数重试循环或固定轮数。
- 新24份混合逗号/TDZ源码覆盖反向arena、200/201/400/800，检查调用
  次数、顺序、抛出、返回与保留的未初始化读取；exact/one-short通过。
  首轮定向4/5通过，失败为新测试把GetValue调用包装计入待删除逗号；
  修正测试后源码对照1/1通过（8.62s）、投影1/1通过（6.34s）。保留
  首轮失败记录。生产代码未为该断言添加形状条件。
- 冻结终验：31目标构建0（94.75s）、fresh fixed ABC0（4.11s）、
  63/63测试0（1048.53s），JUnit名称完整无重复。4份代码/测试哈希、
  输入SHA及测试清单保持；构建无warning/error。24份新源码960次
  独立比较/4个语义突变通过。19 records仍10 complete/0 partial/
  9 no-module；10份源码语法通过，逐字节等同规则275，同一语言contract。
  证据根build/stage-one/joint-publication-verification（final-result.json、
  final-junit.xml）；fresh输出joint-publication-final-rule276-verified。
- 五项主干仍OPEN；本次固定revision内分析输入，是依赖驱动重跑/收敛
  的前置契约，不代表全局调度完成。其他优化和既定暂停事项保持，Goal active。

### 2026-10-10：规则275，初始化与转移共享单元存在事实

- RecoveryFlow::BindingPresence 统一静态能力与 occurrence 前后单元状态。
  初始化局部求解、跨块转移及值/需求逐边转移消费同一事实；批量支配
  证书改用 BindingUnit。退出初始化自己的 Scope/Loop presence 推断和
  仅检查 Unmodeled 标签的准入，不增加布局、样本或指令序列条件。
- 反例先RED：缺失普通scope事实时，直线gen/use被误判完成，而拆块
  依赖转移不能证明。新160组合覆盖普通/catch定义、Scope/Loop、缺失
  forest/context、反向point、拆块及异常绕过；exact/one-short通过。
  既有32768重置图、27648作用域图与200/201/400/800成本检查通过。
- 共同查询O(1)、无新增保留内存；原稀疏must、重路径/Fenwick批量证书
  算法保持。CompletionOnly契约保留；未知单元保留可能需求，但不能
  提供完成/替换证明。分配、成功初始化与异常完成仍是独立义务。
- 冻结终验：31目标构建0（100.87s）、fresh fixed ABC0（3.88s）、
  63/63测试0（1044.96s），JUnit名称完整无重复；5份代码/测试哈希、
  清单与输入SHA保持。19 records仍10 complete/0 partial/9 no-module，
  10份源码语法通过、逐字节等同规则274，同一语言contract。构建无
  warning/error。既有重复catch/local与投影源码独立执行对照均通过；
  ProgressBar配对88场景/570异常/114 swaps/15突变/2等价变化通过，
  platformExecution=false。证据根build/stage-one/initialization-unit-verification，
  final-result.json、final-junit.xml；fresh输出initialization-unit-final-rule275-verified。
- 五项主干仍OPEN，Goal active；其他优化及既定暂停事项保持。

### 2026-10-10：规则274，捕获的数值输入贯通成功定义

- 更新动作的转换前读取接入既有FlowCapturedValue；共同稀疏值图分别
  求解正常表达式结果和成功写回。转换期间当前绑定被改写，不能替换旧
  输入；失败边绕过写回，保留失败前实际状态。需求/初始化共享该输入。
- 退出kUpdate无条件未知域分支。Number/String/Boolean/null输入的
  正常数值结果为Number，未知输入可能为BigInt，仍保持Language。
  前缀/后缀、增量/减量共享类别转移，不借用旧值身份或有限范围。
  一般一元数值方程复用此转移；效果和可观察写入保留独立义务。
- 新循环用例先RED：旧模型在更新后丢失数值类型并留下多余计算。
  定向扩展2/2通过（11.65s）；48份循环/捕获源码6720次独立比较、
  16突变，32份前缀/后缀/catch源码2176次比较、96突变。包含转换钩子
  重入闭包改写绑定后返回BigInt；8个小图区分旧输入/后续写入/失败/
  缺失输入及反向point编号。候选移除数值转换后必须撤销旧Number，
  恢复Required效果；exact/one-short和200/201/400/800均通过。
- 两个生产突变（写回点重读、未知输入当Number）build0/test-6，均已
  恢复。复用现有输入字段、事件和稀疏事实，无新增图或按变量遍历，
  原资源额度不变；不新增恢复pass或样本/操作序列/同块等准入条件。
- 冻结终验：31目标构建0（100.05s）、fresh fixed ABC0（3.77s）、
  63/63测试0（999.32s），JUnit名称完整无重复。7份代码/测试哈希、
  清单及输入SHA在导出前后/测试后不变。19 records仍10 complete/
  0 partial/9 no-module，10份源码语法全部通过，逐字节等同规则273，
  contract仍language_numeric_constructors_v1。最终构建无warning/error。
  证据根build/stage-one/captured-numeric-verification（final-result.json、
  final-junit.xml）；fresh输出captured-numeric-final-rule274-verified。
- 五项主干仍OPEN、Goal active。完整raw phase/protocol对应、普通单元
  重构、其余trial、依赖收敛和统一终验继续；其余优化与暂停事项保持。

### 2026-10-10：规则273，执行图拥有原始证明的失效事实

- 当前/候选 flow 构建器在实际执行边界发布 Preserved/Changed/Unmodeled；
  替换、删除、声明投影及 native loop 阶段使用同一事实通道。被删除的
  无结果动作保留已连接的失效边界，替换目标保留其移动来源。
- normal-kind 消费者不再逐项检查恢复计划以猜测执行变化；共同稀疏依赖图
  传播来源事实。旧值方程不能复活当前图已经撤销的 raw 路径范围证明。
  失效只撤销 raw 精度，不禁止求解源码域，不授予删除效果/写入或合并单元。
  图与值方程的任意混用仍不被授权，pass revision 契约保持独立。
- 新无结果动作反例先 RED：旧实现会恢复有限数范围。当前定向3/3通过
  （10.91s）；104份生成源码/3120独立比较/104个语义突变，SSA编号、
  block split、source arena重排、200/201/400/800及exact/one-short额度
  均通过。矩阵首轮暴露测试反向arena工具不支持DebuggerStmt，补齐测试
  适配后通过，保留失败记录。两个生产突变build0/test-6，均已恢复。
- FlowEvent仍128字节，无新增图/缓存/每变量遍历；原资源额度不变。
  完整raw phase/protocol/action映射仍未完成；本次退出的是重复的
  completion-validity判断，不把原指令效果直接套到其操作数源码节点。
- 冻结终验：31目标构建0（102.04s）、fresh fixed ABC0（3.98s）、
  63/63测试0（1007.73s）；JUnit名称完整无重复。7份代码/测试哈希、
  清单及输入SHA在导出前后/测试后不变。19 records仍10 complete/
  0 partial/9 no-module，10份源码语法全部通过，逐字节等同规则272，
  contract仍language_numeric_constructors_v1。ProgressBar配对88场景/
  570异常/114 swaps/15突变/2等价变化通过，platformExecution=false。
  最终构建无warning/error；证据根build/stage-one/flow-provenance-verification，
  final-result.json与final-junit.xml；fresh输出flow-provenance-final-rule273-verified。
- 五项主干仍OPEN、Goal active；完整原始对应、普通/捕获单元重构、其余
  trial、依赖收敛与里程碑终验继续。其他优化及既定暂停事项保持。

### 2026-10-10：规则272，共同当前单元能力与独立入口域

- RecoveryFlow::BindingUnit 统一发布 Enclosing/Scope/Loop 的当前单元能力，
  CompletionOnly 与 Unmodeled 保持显式。Copy/type 消费同一查询，退出各自
  Scope/Loop 生命周期准入列表；实际身份仍由逐边 transfer、分配、退出与
  成功写入决定。缺失作用域/循环索引返回 Unmodeled，不能假定单元保持。
- 当前单元可追踪不授予 raw 参数入口类型。参数生成独立检查 entry-domain
  与原始入口对应；缺少证明时撤销 raw 依赖锥。外部可达单元在实际 UserCode
  completion 失效，已保存标量的重建携带不冒充旧 cell identity。
- mapped 参数的回调前读取、最后一次赋值后的读取可以使用当前值；回调后
  快照必须保留。测试分别验证 occurrence copy、独立绑定删除和全定义
  invariant；删除载体不生成入口/全定义证明。未加入样本、同块、邻接、
  操作序列或固定循环形状条件；声明/存储/删除仍由独立 pass 负责。
- 定向3/3通过（70.41s）：40个显式小图 scalar oracle、反向 arena、
  作用域/循环 reset/carry/未知/退出、closed/external 回调及缺失索引契约；
  新增24份循环/闭包源码、2688次独立比较与8个执行突变。既有存储消费者
  1289份源码/200156次比较/158个执行突变通过。三个生产突变 build0/
  test-6，分别退回 Scope-only、泄漏 raw 入口、缺失 scope 假定保持；已恢复。
- 当前单元查询O(1)，无新增保留存储；source 200/201/400/800、反向
  statement arena 与 exact/one-short byte/work 均通过，原额度保持。
- 冻结31目标构建0（198.13s）、fresh fixed ABC0（3.77s）。完整63项
  首轮62/63通过（1018.77s），唯一失败是旧手工图声明 Scope lifetime
  却缺失 forest。补齐已假定的作用域事实，未改生产代码；该目标重建0，
  定向1/1通过（22.58s）。保留首轮与修复记录，当前合并结果 exact names
  63/63、无重复。首轮8份代码/测试哈希及修复后的9份哈希、清单、输入
  SHA在完整测试与修复后保持；没有宣称同一次全63运行终态0。
- 19 records仍10 complete/0 partial/9 no-module，10份语法全部通过，
  contract仍language_numeric_constructors_v1。只有ProgressBar变化：当前
  捕获单元读取替代私有快照，原读取完成保留；?.规则未改。paired gate
  88场景/570异常/114 import swaps/15突变/2等价变化通过，
  platformExecution=false。既有literal_plan kNoop warning一条，无error。
  证据根build/stage-one/unit-capability-verification，汇总consolidated-result.json；
  fresh输出build/stage-one/unit-capability-final-rule272-verified。
- 五项主干仍未关闭。完整 raw phase/protocol/action 对应、普通/捕获单元
  重构、其余 trial、依赖驱动收敛与里程碑终验继续；kUpdate 正常域仍较粗。
  下一主干入口是当前 success-result 表尚未完整覆盖的效果/阶段/协议动作。
  其他优化和 agents/fullsuite/扩展语料/RSS/benchmarks/DFX 暂停，Goal active。

### 2026-10-10：规则271，共同单元访问事实替代回调失效代理

- 在未修改的源码 owner 上一次生成 SourceCells：Closed、External、
  Unmodeled 三态访问事实和按 creation 索引的捕获边。函数/类捕获、
  module live alias 与 mapped arguments 在同一生产者中适配；类 body
  或控制能力缺失保守发布 Unmodeled。当前和 trial 投影借用同一 owner，
  返回图只保存标量事实；外部 owner 的证明拒绝复用。
- Copy 与 normal-kind 的实际 UserCode completion 失效消费同一事实，
  不再以 observable write/private storage 代替外部改写能力。初始化
  恢复中重复的函数/类捕获扫描退出，改读共同 capture index。
  storage 投影聚合成员外部可达性，read-value alias 不制造 Closed 证明。
  旧代理仅留在无 source adapter 的 legacy 手工图缺省契约中。
- Closed 不授予 raw 参数入口域、immutable value、删除写入、共享存储或
  cell identity 许可。原初始化、成功写入、生命期、名字和独立 DCE/存储/
  声明 pass 继续负责各自义务。源码树及 prepared class body 在共同事实
  存活期间不变；plan revision 仅投影，不能写回分析。
- 先前 RED 确认回调后的安全 local copy 未恢复；模型/源码定向3项已
  通过（59.88s）。108份 local/catch 输出在独立参考下执行5832次，
  包括能改写局部值的闭包、两类重复进入、catch退出和失败 update；
  新增闭包快照执行突变与其他反例均检测到差异。现有综合消费者为
  1289份源码/200156次比较/157个执行突变。四个生产突变均 build0/
  test-6，分别退回 copy/type 旧代理、遗漏捕获和接受 foreign owner；
  原文件全部恢复。定向证据不冒充后续冻结验收。
- canonical census/capture 查询的200/201/400/800 work为3628/3650/
  7240/14442，bytes为729/738/1465/2929；反向 arena、exact/one-short
  byte/work 和候选改变初始化而复用访问事实均通过。所有额度保持。
- 最终冻结31目标构建0（196.78s），fresh fixed ABC0
  （5.37s），63/63项全部通过（971.04s），终态0；JUnit
  exact names63/63，无重复。12份冻结代码/测试哈希、清单及ABC SHA
  导出前/后/测试后不变。19 records仍为10 complete/0 partial/9 no-module，
  10份语法全部通过，源码逐字节等同270。最终共同模型/反例/执行证据
  与定向结果一致，综合1289/200156/157在冻结验收内通过。
  ProgressBar record88场景/570异常/114 import swaps/15突变/2等价变化
  通过；platformExecution=false，未冒充设备执行或完整阶段一验收。
  既有 literal_plan.cc:59 kNoop switch warning一条保留，无error。
  证据根 build/stage-one/cell-access-verification，fresh输出
  build/stage-one/cell-access-final-rule271-verified。
- 五项主干仍未关闭。下一工作继续完整 raw phase/protocol/action 对应、
  普通/捕获单元身份、其余 trial 和依赖收敛，最后做里程碑终验；其他
  优化和既定暂停范围保持，Goal active。
  已确认具体缺口：copy已消费 ordinary/loop 单元，normal-kind 的
  observable 跟踪仍仅 ScopeBindingLifetime；需要共同单元能力迁移，
  不能以本轮访问事实宣布完整循环类型或单元闭环。

### 2026-10-10：规则270，普通 local/catch 当前单元复制接入共同分析

- 主干第2项的实际读取恢复消费者现在支持 observable scoped root。
  同一个稀疏 available-copy solver 消费源单元与载体各自的 transfer、
  实际根写入，以及当前 completion 的 UserCode；generation 和每条
  propagation 边同样适用。旧 entry-only root 与 symbol-only kill 决策
  被替代。未新增名字、样本、操作序列、同块或邻接准入条件。
- CurrentCopies 仍为纯、revision-owned 分析；BindingValues 在共同初始化/
  名字证明后只提出读取替换。写入、存储、死定义及声明由独立 pass
  处理，清理使用新鲜事实。private carrier 继承已证明 root，不将未知
  private 值冒认为 immutable root；缺少 scoped context 保守。
  common exposure 仍保守混合 observable write 与外部可修改性；未知
  回调会保留普通闭合 local 的快照。后续必须由共同单元访问/捕获事实
  拆分该精度缺口，不在 copy/type/trial 各自加回调例外。
- 40个 local/catch 单元/效果/写入反例及16000个独立小图通过，包含
  分支、环、重分配、回调、反向 point/symbol ID。96份实际 SourceAdapter
  源码独立执行5184次，四个输出突变行为不同；安全复制输出 return
  current/error，写入、catch退出和两类重复进入仍保留 scratch。
  该 evidence 不冒充完整 raw/capture 证书。
- 针对3项均通过，80.56s；综合源码消费者现有及新增1277份源码/
  199508次比较/156个执行突变全部通过。新增独立小图随后单独通过。
  四个生产突变（删除 scoped root/单元 transfer/根写入 kill、异常取
  normal mask）均 build0/model-6，恢复后 build0/model0。
- 200/201/400/800 普通重入 family work=256394/258005/486282/955320，
  bytes=775938/777073/1527226/3029802；catch重入 work=
  269889/270556/508185/995033，bytes=777951/779086/1529239/3031815。
  反向 arena 和 exact/one-short byte/work 通过；这是组件 admission，
  非进程RSS。所有既定额度及冻结31/63/fixedABC选择保持不变。
- 最终31目标构建0（194.11s），固定 fresh ABC0（3.76s），冻结63项
  全部通过（976.59s），终态0；JUnit exact names63/63、无重复。
  6份冻结生产/测试代码哈希、测试清单及 ABC SHA 在导出前/后及
  测试后均不变。19 records：10 complete、0 partial、9 no-module，
  10模块语法全部通过。仅 ProgressBar.mjs 相对269变化：去掉临时
  $value1 及 (($value1 = uiContext)?.getHostContext()) 的搬运，输出
  uiContext?.getHostContext()；未修改可选链恢复规则。其 record 配对
  执行88场景、570异常注入、114 import swaps、15突变及2等价变化
  全部通过（platformExecution=false，不冒充设备平台执行）。
  最终源码消费者1277/199508/156及16000小图均在冻结63项内通过。
  构建保留既有 literal_plan.cc:59 kNoop switch warning 1条，无error；
  未混入无关修复。证据根 build/stage-one/scoped-copy-verification，
  fresh输出 build/stage-one/scoped-copy-final-rule270-verified。
- 五项主干均未关闭：完整 raw phase/protocol/action 对应、普通及捕获
  单元身份闭环、其他trial、依赖收敛和里程碑终验继续。其他优化及
  agents/fullsuite/扩展语料/RSS/benchmarks/DFX 保持暂停，Goal active。

### 2026-10-10：规则269，完成路径效果与可观察写入分离

- 主干第1项继续 source 侧效果对应：唯一穷尽语言表返回 typed normal/
  before_throw masks 和 exceptional completion，Required/Inert 从同一事实
  派生，未增加第二张操作表。当前 flow 在实际 intrinsic、receiver/key/
  access、update read 处发布效果；成功 declarative definition 单独发布
  lexical write。规范化源码仍以语言语义为准，不直接覆盖 raw VM flags。
- 共同正常域分析改为读取当前 completion 的 UserCode：正常/resume-next
  使用 normal，throw/resume-throw 使用 before_throw。Required guard、分配
  和其他绑定的普通写入仍保留自身效果，但不冒充外部修改。原来的粗
  Required kill 已退出建模源码事件的决策路径；未知图客户端 fallback
  和 unmodeled cleanup 保守。无 motion/elision 许可随之隐含产生。
- 纯绑定写入反例 build0/model134；修复后针对4项全部通过，22.27s，
  SourceAdapter 新生成2函数/60独立行为比较/2执行突变，写入仍可观察，
  数值转换保留、冗余一元运算消除。正常/异常两类效果的反向组合有
  独立小图反例；这些 synthetic 原始包不冒充完整 raw/closure 证书。
  receiver/key/access、资源契约和未知转换覆盖共同表语义。
- 三项生产突变退回粗 kill、在异常路径取 normal masks、删除 key 回调，
  均 build0/model-6；已恢复。N=200/201/400/800 native scoped definitions
  work=691921/695278/1419907/2910736，bytes=3233801/3237587/6464985/
  12927353；反向 arena、exact/one-short byte/work 通过，预算未放宽，
  这是组件 admission，不是进程 RSS。语义依据已核对 ECMA-262 edition16
  RequireObjectCoercible/ToObject/ToPropertyKey；原语言及 Ark profile 不变。
- 第一轮冻结31目标构建0、固定ABC0；63项62通过/1失败，800 binding
  打印返回资源错误。完整效果事实导致 event 128→136 byte；重新布局
  窄字段后恢复128 byte，无额度放宽。针对投影与失败源码2项全部通过，
  62.97s；first final 失败及 probe 均保留在证据根。
- 第二轮同一31目标构建0（47.11s）、固定ABC0（3.62s），精确63项
  全部通过（985.53s）；JUnit 名称完整且无重复。8份冻结代码哈希、
  测试清单及 ABC SHA 在导出前/后和测试后均不变；profile、64MiB
  及既定 lower-API work 额度不变。19 records：10 complete、0 partial、
  9 no-module；10模块语法全部通过，源码逐字节与268相同。新增效果
  源码2/60/2比较与突变、已有原始SSA/异常/生命期/试算及固定record
  验证全部通过。第二轮与 repair build 无 warning/error；第一轮暴露
  的既有 literal_plan.cc:59 kNoop switch warning 仍记录在首轮证据。
  证据根 build/stage-one/completion-effects-verification；最终 fresh输出
  build/stage-one/completion-effects-final-rule269-verified。
  五项主干仍未关闭：完整 raw phase/protocol/action 对应、普通单元身份、
  其他 trial、依赖收敛和里程碑终验继续。对象/class/helper 未精确建模的
  效果保守，不加白名单。其他优化及 agents/fullsuite/扩展语料/RSS/
  benchmarks/DFX 保持暂停。

### 2026-10-10：规则268，当前异常值合流与普通作用域值状态

- 主干继续第1/2/3项：同一次 verified TreeFlow 转移 raw Catch 的值、
  handler 和源码 body 所有权；当前 catch activation 在进入作用域后定义，
  与后续 staging 写入分开。原始域只在共同依赖锥不变且更精确时参与，
  未拿 raw before_throw/normal effects 覆盖语言表。
- 显式 throw 在操作数正常完成后发布 payload；catch 入口共用稀疏值
  合流节点。隐式抛异常、暂停失败和未精确建模 cleanup 均贡献 Language，
  不借用失败操作的正常结果。普通成功存储与 catch activation 共享值
  依赖、作用域 transfer 和完成事实，没有逐 catch 整图扫描。
- 普通 scoped exposed 绑定可跟踪当前值，但写入仍 Observable。Required
  求值在正常/异常路径撤销其值精度，成功显式写回另行生成新值。共同
  Prepare 顺序为初始化证明→初始化已完成的读/写效果 refinement→
  活跃性/正常域→最终效果；中间分析纯读计划，scratch 在阶段边界释放。
- 初始异常值消费反例 build0/model134。56个实际解码异常函数包含 literal/
  Boolean/未知值、隐式转换失败、成功写回、复制、拆块及寄存器重分配；
  独立执行1736次、检出32个执行突变。完整运输试算重算 Boolean payload，
  缺失 raw catch 表时当前 source join 仍可证明 Number；精确/差一预算通过。
  独立小模型区分连续初始化读取与外部修改，不冒充 raw SSA/capture 证书。
- 三项生产突变分别删除 pending payload、exposed invalidation 和初始化
  refinement，最终均 build0/model-6。第三项最初逃过 catch 测试，补连续
  GetValue 的作用域反例后检出；保留初次 model0 证据，未伪造检测结果。
  N=200/201/400/800 普通复制布局 work=403582/405552/797585/1585588，
  admitted bytes=805891/809711/1569891/3097891（draft+projection保守总量，
  不是进程RSS）；拆块/重分配增长也通过，现有 byte/work 预算未放宽。
- 最终31目标构建0，193.24s；冻结63项全部通过，950.40s，终态0，
  JUnit exact names63/63、无重复。固定 fresh 导出0，3.86s：19 record=
  10 complete/0 partial/9 no-module，10源码语法检查0，与规则267逐字节
  一致。SHA在导出前后及测试结束均不变，profile、预算和冻结选择不变；
  11个生产/测试输入哈希复核一致。重建暴露既有 literal_plan.cc:59 的
  kNoop 未穷尽 switch 警告1条，无 error；不混入本轮修改，未宣称无警告。
  新异常源码证据56/1736/32已纳入冻结 original_flow_source_test；现有
  生命周期、循环、定义/存储、快照和真实 record 对照继续通过。
- 完整日志/JUnit/detail/coverage、增长与三项生产突变位于
  build/stage-one/catch-flow-verification；固定源码位于
  catch-flow-final-rule268-stable。作者 inline 复核，未宣称独立复核。
  五项主干均未关闭；当前粗粒度 Required 撤销是保守精度缺口，完整
  raw phase/action/异常出口、普通单元身份、其他 trial、依赖收敛及终验
  继续。其他优化、agents/fullsuite/扩展语料/RSS/benchmarks/DFX 保持暂停。

### 2026-10-10：规则267，内联 SSA 结果与当前完成图贯通

- 主干第1项增加实际消费者：RecoveryFlow 建立 OpId 到当前源码表达式、
  求值入口和正常续接点的对应；AnalyzeFlowNormalKinds 读取原始结果
  ValueDefinition 的域，并在既有稀疏操作数/完成依赖图上验证其有效性。
  内联结果不必先成为临时变量存储；存储证明与内联证明独立但共享事实。
  原 normal_kinds 混有操作数 hint，仍不作为范围证书。源码效果保持由
  语言语义表产生，未用 raw normal/before_throw 覆盖源码行为。
- 候选替换/运输在实际边界失效传播，即使旧 intrinsic event 已被省略。
  路径/操作数变化、未建模环境及缺少边界对应均撤销原始路径精度，
  当前语言值域继续求解。作者复核发现只缺少表达式边界时仍保留范围；
  新反例 build0/model134，修复后运输值正常求解，未增加表示准入条件。
- 最初内联范围反例 build0/model134；52组复制、反向SSA、拆块和反向
  源码arena（真实重验证）的域/源码验证覆盖正常结果、生产者/条件改变、
  缺少两类证据及精确/差一预算。三项生产突变分别移除内联消费、完成
  依赖和表达式边界fallback，均 build0/model-6；恢复后通过。
- N=200/201/400/800 普通投影 work=4660/4667/6060/8860，bytes=
  21968/21984/25168/31568；拆块/重分配/反向arena增长也通过。原始 O
  随复制增长，源码 E 可经规范化保持不变；这是准入/work证据，不是RSS。
- 最终31目标构建0，29.25s，无 warning/error；固定 fresh 导出0，3.24s，
  19 record=10 complete/0 partial/9 no-module，10源码语法检查0，源码与
  规则266逐字节一致。SHA前后、profile和预算不变。冻结63项全部通过，
  926.00s，终态0，exact names63/63、无重复。原始结果独立执行覆盖52个
  生成函数、1560个行为比较并检出52个执行突变；现有异常/循环/生命期
  对照继续通过。首次验收在边界修复前中止143，其构建/导出通过但仅
  两项测试开始，没有套件终态；不计作最终验收。最终日志/JUnit/detail/
  coverage、增长和三项生产突变及中止证据位于
  build/stage-one/inline-result-verification；最终源码位于
  inline-result-final-rule267-stable。作者 inline 复核，未宣称独立复核。
- 五项主干仍未关闭。下一步继续 raw 效果阶段与异常出口对应，随后
  ordinary/local/catch/reentry、其他trial消费者、依赖收敛及里程碑终验。
  其他优化、agent/fullsuite/扩展语料/RSS/benchmarks/DFX 继续暂停。

### 2026-10-10：规则266，完整快照事务与候选恢复依赖

- 主干第3项的快照路径已迁移：PlanSnapshotProjection 暂存声明/初始化、
  完成点、遗漏、残留 RHS、替换/运输及来源字段；trial 与 Publish 使用
  同一份完整 delta，声明放置和快照共享 RetireDeclaration。原来的部分
  trial、invalid/blocked 选择器及提交时再推断退役的循环已退出决策路径。
- PlanSnapshotRetention 统一收集实际移动引用/自由名字及嵌套组件依赖，
  使用 indexed SourceNameRetirement。恢复一个声明会传播到受其遮蔽影响
  的候选；恢复运输产生的存储读取也建立明确的保留依赖。最终选中事务
  再经共同 producer 检查实际名字，然后发布原数组；没有逐变量整图重试、
  同块/邻接/样本/操作序列或固定候选数量准入。已有效果、次数、异常和
  暂时保留的捕获环境义务仍各自负责，不由选择器重新从语法推断。
- 保留原声明反例先 build0/model134；完整 delta、native 初始化、outlined
  必需 RHS、实际 Publish、反向 IDs 通过。手工期望的小图覆盖恢复遮蔽、
  独立/嵌套组件、符号/自由名字、未恢复存储读取和预算边界。端到端组合
  用例提前被 motion 挡住，因此没有把它算作恢复闭包证明；独立小图负责
  此项证明。五个生产突变分别缺失声明退役/RHS/恢复结果/嵌套边/存储边，
  均 build0/model-6，恢复通过。最后一项在作者复核中发现，另经新增反例
  RED→GREEN；仅停止了尚在构建的第一次自有终验进程，未启动其测试/导出。
- 200/201/400/800 恢复选择 work=15681/15758/31483/63485，
  bytes=85430/85849/169230/336830；完整 delta+trial work=
  407016/409167/827352/1681096，bytes=2380198/2383187/4759758/
  9518878。增长和精确/差一预算验证通过，64MiB 与既定 work 上限未提高；
  两阶段临时图及被替换旧数组都先销毁后归还准入，累计 work/peak 保留。
- 稳定终验：31目标构建0，45.91s；固定 pasteboard fresh 导出0，3.15s，
  19 record=10 complete/0 partial/9 no-module，10个源码语法检查0；源码与
  规则265逐字节相同。原 SHA/profile/预算不变。冻结63项测试全部通过，924.06s，终态0、
  exact names63/63、无重复，构建没有 warning/error。statement-snapshot
  独立执行覆盖660个引擎生成函数、25320个行为场景并检出78个执行突变。
  证据保存在 build/stage-one/snapshot-plan-verification（最终日志/JUnit/
  detail/coverage、增长、五个生产突变及提前终止的构建）；最终源码位于
  snapshot-plan-final-rule266-stable。
- 五个主干交付仍未关闭。此项覆盖已知快照部分 trial/未验证子集缺口；
  其他候选消费者、raw IR 效果阶段完整对应、ordinary/catch/reentry 单元、
  依赖收敛与里程碑终验继续按主干推进。其他优化、agent/fullsuite/扩展语料/
  RSS/benchmarks/DFX 保持暂停。作者 inline 复核，不宣称独立复核或全项目验收。

### 2026-10-10：规则265，统一候选视图、完整声明试算与 pass 事实生命周期

- 主干第3项的实际迁移：BindingPlanView 为一个同步不可变候选提供全部
  binding/declaration/value spans。表达式输入退出第二份 replacement/transport
  权威；grammar 退出可选 initializer/omission fallback。共同 bootstrap 的
  Apply 从同一视图取得遗漏、声明、完成点、值与残留效果，源码放置 sites
  显式传入。native 入口适配覆盖候选后再分析，避免借用当前计划的字段。
- Declarations 暂存完整批次：新声明位置/初始化、旧声明退役、保留的 RHS
  效果和完成点。trial/commit 共享 RetireDeclaration；native 旧声明在该
  事务内退役。共同 must-initialize 验证实际候选的普通移动读取，名字分析
  验证单元对应的绑定与已有可解析引用/自由名字。所有证明结束后才原子
  提交；没有逐变量整图重建、同块/邻接/样本/操作序列准入或额外 pass 轮次。
  不能证明时保留整个批次，暂不根据一个完整候选的证明发布未重新验证的
  子集。captured cell 继续保留注明的旧同 activation 义务，没有宣称重构完成。
- 反例先 RED：候选删除声明仍借用当前初始化，模型134；统一视图 GREEN。
  声明值/完成点/新初始化/残留效果、arena 反向、200/201/400/800 和精确/
  差一预算覆盖。新增长用例最初错误复用别的语句拥有的调用，共同 ownership
  检出；测试改成声明拥有独立必需调用，没有放宽生产校验。五个 Apply
  权威生产突变全部 build0/model-6，恢复 build0/model0。
- 首次冻结终验构建31目标、fresh固定导出通过，63项实际62通过/1失败，
  803.14s，名单完整且无重复。失败为 --scale 的800组变量真实触发64MiB
  resource limit。trace 显示当时当前分析缓存总准入34,608,942字节，包含
  不再被消费的派生产品；完整试算 flow 后没有足够空间运行 must 分析。
  失败日志/JUnit/导出保存在 candidate-plan-verification/first；临时 trace
  已完整撤回，没有扩大预算或将 resource failure 改成语义拒绝。
- 主干第4项的成本迁移：每个 RunPass 边界使用 RequiredFacts 闭包保留共同
  flow/expression，销毁不再需要的派生产品后释放其准入，后续按相同修订
  懒重算。不是计划修改、手工证明刷新、资源阈值准入或新优化轮次。800组
  现报告61,536,904字节；200/201/400/800 work为9459371/9510402/
  19736968/41194586，几何增长<3。缓存保留禁用突变 build0/model-6；恢复
  build0/scale0/projection0，差一预算检查仍保持。
- 修复后最终31目标构建28.0160s、fresh固定导出3.1304s、
  精确63/63 CTest 919.2095s全部通过，名单一致/无重复；构建无
  warning/error。19records/10complete/0partial/9no-module，10语法检查通过，
  与规则264源码字节相同，输入SHA前后相同，profile与预算保持。证据
  build/stage-one/candidate-plan-verification；最终fresh输出
  build/stage-one/candidate-plan-final-rule265-stable。作者 inline 复核，未恢复
  agent review、全套测试、扩展语料或RSS。首次失败与六项突变证据保留。
- **五项主干全部开放**。本次退出的是混合当前/候选字段权威、声明试算
  使用旧位置以及无消费的派生缓存长期占用。下一步完成 snapshot 的最终
  声明退役、完成点和实际保留候选子集试算，再继续普通 local/catch/reentry
  身份与原始 IR/SSA 完整对应、依赖重跑/收敛和统一变量终验。其他表现层
  优化及 agents/fullsuite/扩展语料/RSS/benchmark/DFX 保持暂停，Goal active。

### 2026-10-10：规则264，逐轮单元重置与共同 must-initialize 分析

- 主干第2/3项：实际 native BoundNames 的首轮及逐轮复制增加共同单元
  重置点；所有旧值先被捕获，再切换当前标量单元并初始化新值。正常
  循环退出有独立 leave 事件；嵌套循环森林保留父单元。这里表示标量
  epoch，不声称 VM 堆分配顺序或 captured cell 相等。旧值输入没有
  伪造 ExprId/原始 SSA 写入，当前/trial/bootstrap/refinement 共用 producer。
- AnalyzeFlowInitializations 从 dominance 文件独立出来，以共同 gen/kill、
  活跃单元及所有完成边计算 requested roots 的 must-initialize。声明
  anchor 仅用于源码放置；旧 Completed 的“声明支配读取”及私有 scope
  判断退出决策路径。存储候选明确请求代表变量在实际读取/定义位置的
  状态，消费同一不可变图；没有查询更新计划、手工刷新或新增 pass 轮次。
- 共同普通 scope/native loop 森林共享 heavy-path 索引。支配子树内无本
  单元 kill 的充分证书批量求解，其他状态进入完整 must 方程。基本块
  收缩只减少分析工作，不构成恢复准入。无可达初始化的 bottom fact
  一次建立；保留原预算与 typed resource exhaustion，没有 200 截断。
- 初步逐指令 sparse 展开在既有 800-copy/nested/finally/catch 及 late-TDZ
  场景超出原 64MiB 预算，改为基本块、索引和批量证书。finally 仅保证
  函数 body 祖先单元仍活跃，嵌套单元保持 unknown。inline 复核发现
  native 缺失上下文被当作退出会丢失首轮旧值需求，改为 unknown；确定
  退出只由共同显式事实产生。未通过预算增加或旧支配 fallback 绕过问题。
- 独立小图对照：32768 组初始化、27648 组 canonical/native 活跃/未知/
  退出组合；native 首轮原始值需求、逐轮初始化及父/子 reset 独立检查。
  新增嵌套模型输入明确供应外层 condition program，不计作旧语法 planner
  新增恢复能力。200/201/400/800、arena 反向、exact/one-short 预算均覆盖。
  八项生产突变 build0/model-6；其中忽略 cuts 的最终突变保留候选资格，
  仍由 scoped oracle 检出。恢复后 reset/native/projection 模型均退出0。
- 首次冻结验证31目标构建及fresh固定导出通过，63项CTest实际执行62通过/
  1失败：新增native工作量日志污染 --emit 的JSON stdout，旧loop源码对照
  拒绝解析。统计只在显式 --phase-scopes 模型模式输出；原行为/范围参照
  未削弱。first日志/JSON/JUnit/输出保留，未称作63通过。
- 修复后原loop源码对照11.53s通过。最终31目标构建22.7693s、
  fresh固定导出1.7411s、精确63/63项CTest731.1865s全部通过，
  名单完全一致且无重复。19records/10complete/0partial/9no-module，10语法
  检查通过，与规则263源码字节相同，输入SHA256前后相同。最终构建无
  warning/error，profile/预算保持。证据 build/stage-one/iteration-unit-verification；
  fresh输出 build/stage-one/iteration-unit-final-rule264-stable，首次失败证据保留。
- 最终原选定source目标实际输出：initializer2004份/482752独立执行对照/
  370mutants；declaration-value2568份/513600/100；loop-initializer276份/
  13976/68；loop-transition108份/2418/14；original-flow合计10280/158。
  这些Node执行对照不计作新平台/全语料执行，也不替代统一变量终验。
- **五项主干全部开放**：native 入口/源码移动走廊仍有 unknown，普通
  local/catch 与重复进入的完整身份、captured cells、原始IR/SSA完整对应、
  完整trial生命期delta、依赖驱动收敛及统一变量终验尚未交付。本组件
  通过不替代主干或阶段一验收。下一主干子步将候选声明位置及生命期
  delta 纳入统一试算输入；当前入口仍有 plan_.statements 与 declaration_sites_
  来自当前计划的精度缺口，不能以当前图的检查替代候选事实。随后继续
  完整原始IR/SSA对应、依赖重跑/收敛和终验。作者inline复核；agents/
  fullsuite/expanded corpus/RSS/benchmark/DFX 及其他表现优化保持暂停。

### 2026-10-09：规则263，实际循环复制值与共同读取依赖

- 主干第2/3项：共同flow从实际native BoundNames及已建模source loop阶段
  生成首轮和normal/continue后的标量复制；首轮在condition前，逐轮复制在
  increment前。break/return/throw不进入下一轮复制。成员按loop/binding
  一次索引，不按region peers、样本、指令序列、同块、邻接或数量准入。
- FlowCapturedValue明确保留旧值的读取位置；kIterationCopy独立表示新标量
  定义，无伪造ExprId、源码写入或原始SSA定义。共同活跃性/需求只在目标
  定义活跃时拉入旧输入，定义—使用、常量与类型分析消费同一事实。
  跨reset的输入和后续冲突写入都有反例。当前绑定复制分析不把较早捕获值
  替换为可变参数root的当前值；这一精度义务留待共同效果/写入证明。
- 当前/trial/bootstrap/refinement复用同一producer，返回图拥有输入与阶段
  记录；未新增pass轮次、顺序、手工刷新位置或分析查询修改计划的职责。
  既有原生入口分配及声明完成契约保持，源码打印消费已有计划。
- 新增120组native阶段组合、10组2/200/201/400/800声明成员及反向arena
  检查，含exact/one-short建图预算及几何增长；8组跨reset/死输入/冲突写入、
  1组可变root及2组类型输入反例。七项最终生产突变build0/model-6，
  恢复后的value/phase/type模型全部0。第一轮突变发现断开的首轮程序仍被
  静态活跃性看到，补充入口支配关系；可变root反例补充先前读取的正例与
  后续读取的负例。早期日志保留，未削弱执行/范围参照。
- 最终31目标构建91.8466s、fresh固定record3.4759s、精确63/63项定向
  CTest593.5276s通过，名单一致且无重复。固定19records/10complete/
  0partial/9no-module，10语法检查通过，源码与规则262字节相同；输入
  SHA256前后相同，profile与预算保持。最终构建无warning/error。
  证据build/stage-one/iteration-copy-verification；fresh输出
  build/stage-one/iteration-copy-final-rule263。未运行扩展语料或新平台验证。
- **五项主干仍全部开放**。本轮接通旧值读取和新标量定义，仍未分配新的
  逐轮单元、证明captured cell相等或以reset-aware must-initialize替换声明
  dominance。下一步必须把逐轮分配与初始化证明一起迁移，随后继续原始
  IR/SSA完整对应、完整trial生命期delta、依赖驱动收敛和统一变量终验。
  作者inline复核，agents/fullsuite/expanded corpus/RSS/benchmark/DFX
  保持暂停；本组件通过不替代主干或阶段一验收。

### 2026-10-09：规则262，实际循环阶段与共同语法保留事实

- 主干第2/3项：当前/trial及其bootstrap/refinement都消费打印器已有的
  同一不可变LoopSyntax。共同flow分别拥有condition/body/increment/exit
  边界；条件与更新表达式在循环体激活之外，保留的继续分支体在体内
  执行一次。normal/continue经更新边界，break/return/throw保留共同
  完成与保守cleanup路径。原“原始循环体作用域覆盖所有阶段”的建图
  路径在已建模source loop上退出。返回图不持有临时syntax指针。
- 首次最终验证发现真实范围回归：空native body被效果消除标成omitted，
  打印仍要求其容器、移动后的条件仍有源码范围。原declaration-value
  model/source的projected_ranges.h47检出；该次runner已停止，first日志
  和输出保留，未称作63项通过，也未削弱既有范围/行为参照。
- FlowStatementRetention现在独立发布函数/循环体、普通条件arm、
  try/catch/finally及保留体的必需语法位置。inert删除消费共同角色，旧
  id!=tree.body单独判断退出。必需容器不伪造效果，不强制保留无效祖先；
  表达式程序里的块仍为optional。该事实描述实际源码语法，不是新增
  样本、序列、同块、邻接、数量或固定图形状准入。pass顺序/轮次/预算
  保持，旧grammar表达能力缺口未扩展。
- 新增120组阶段模型：12种语义组合、正反statement编号、
  1/200/201/400/800尺寸；检查实际scope presence、allocation dominance、
  保留体归属/单次执行、normal/continue目的地、必需语法角色、exact及
  one-short bytes/work、几何工作量。六项最终生产突变build0/model-6，
  恢复build/model0；覆盖阶段作用域、跳过increment、重复体/更新及
  删除必需body。原失败声明model通过，两个独立source回归通过。
- 最终31目标构建48.8890s、fresh固定record2.9677s、精确63/63项
  定向CTest588.7978s全部通过，名单一致且无重复。固定19records/
  10complete/0partial/9no-module，10语法检查通过，源码与规则261
  字节相同；输入SHA256前后相同，profile/预算保持。最终构建无
  warning/error。证据build/stage-one/loop-phase-verification，fresh输出
  build/stage-one/loop-phase-final-rule262-stable；first证据单独保留。
- 最终原选定source目标实际输出：loop276份/13976独立行为对照/
  68mutants；initializer2004份/482752/370；declaration-value2568份/
  513600/100；definition-elision1181份/194324/152；original-flow合计
  10280/158。未将这些Node对照称作新增平台执行或全语料验收。
- **五项主干仍全部开放**。本轮接通实际source完成阶段，未实现逐轮
  旧值读取、新定义及新单元；也未授予captured cell相等证明。原始IR/SSA
  完整对应、普通绑定及循环单元、完整trial生命期delta、依赖驱动收敛和
  里程碑终验继续作为主干，其他表现优化保持暂停。作者inline复核，
  agents/fullsuite/expanded corpus/RSS/benchmark/DFX均未恢复。

### 2026-10-09：规则261，原生循环入口分配与实际声明成员

- 主干第2/3项：完整LoopDeclarations现在发布实际保留的声明组入口及
  BoundNames成员；no-op产品仍保留事实，被省略的入口/成员不分配环境。
  同一provider接入当前/trial准备与最终语法，pass轮次/顺序/预算未改变。
- ProjectLoopEntryFlow发布逐绑定的NativeLoopAllocation；FlowBuilder
  通过共享BindingProjection对应当前存储，在首个RHS前发布一个实际
  kEnterLoopEnvironment事件。所有实际成员共享首次分配点；分配gen/kill
  接入ResetsBinding/TransferBinding，初始化分析检查分配先于完成。
  普通canonical分配与native首次分配共享该完成顺序检查。原生声明
  初次分配缺失的旧路径在已建模成员上退出，未授予逐轮cell身份证明。
- 审查中新增同区域非成员反例：初步按区域发布会错误地给peer授予
  native分配/reset；RED413已检出。最终使用实际声明成员名单，词法
  区域包含不再决定分配身份。无样本、操作序列、邻接、同块或固定形状
  准入；当前/试算的完成证明不借用旧投影。
- 新增40组共同模型验证，覆盖先前完成的初始化读取、forward TDZ读取、
  非成员peer、分配晚于完成的负向trial、正反statement编号、拆分块及
  2/200/201/400/800成员。既有1/200/201/400/800批次扩展完整/no-op/
  proposal/current/trial成员和入口事实；exact/one-short预算与增长检查
  通过。五项最终生产突变build0/model-6，全部恢复后build/model为0。
- 最终31目标构建97.5640s、fresh固定record
  3.3859s、精确63/63 CTests586.2609s通过，
  名单一致且无重复。固定19records/10complete/0partial/9no-module，
  10语法检查通过，源码与规则260字节一致；输入SHA256前后相同，
  profile及预算保持。最终构建无warning/error。证据
  build/stage-one/native-entry-verification，fresh输出
  build/stage-one/native-entry-final-rule261；初次RED、peer RED、前后
  实现的突变证据保留，最终五项突变对应member实现。
- 最终loop source目标276份/13976独立行为对照/68mutants；initializer
  source目标2004份/482752/370；original-flow目标10280/158，均为
  原选定目标本轮实际输出，未把组件模型通过称作新增平台执行验收。
  作者完成inline复核，用户暂停agents，未执行独立agent审查。
- **五项主干仍全部开放**。首次分配顺序已接入，逐轮值复制及新单元、
  实际condition/body/increment阶段归属、capture/finally仍需共同
  completion程序；LegacyLoopAllocation只保留尚未迁移的完成/值能力。
  完整原始IR/SSA、完整trial生命期delta、依赖驱动收敛及终验未关闭。
  下一步沿该主干继续，其他表现优化及fullsuite/corpus/RSS/benchmark/
  DFX/agents保持暂停。

### 2026-10-09：规则260，循环入口归属与声明提交稳定性

- 主干第2/3项：PlanLoopDeclarations分别发布完整当前入口归属和提交
  delta；无变化的语法计划仍有完整事实。声明整批候选、当前计划、trial
  及最终语法pass使用同一provider，trial使用自己的omission。
  kLoopEntries成为显式provider依赖，不增加pass轮次/阶段/预算。
- ProjectLoopEntryFlow为实际表达式前缀恢复原绑定环境、赋值完成和
  原始作用域中的读取位置，native declarators保留循环环境。一次词法
  遍历索引root/descendants，单次线性适配；未知位置不继承预期循环
  区域。已退出前缀的“整个入口流属于for声明环境/InitializeBinding”路径。
- 首轮adapter-only实现暴露真实跨pass错误：复制被删除后，最终声明区
  向前扩展，将saved放进for-let，循环后仍读取saved。首次63项验证
  61/63，两项初始化测试失败；进一步独立执行检出ReferenceError及
  报告trace丢失。没有把错误当作旧断言过时而放行；失败源码和日志保留。
- 声明pass现在在commit前，使用同一完整grammar provider试算整批
  initializer候选；表达式前缀候选保留原声明/赋值，不发布循环initializer
  annotation。后续复制/效果删除不能重新扩大已选定绑定生命期。分析
  与提交分离，commit内不查询事实；最终语法不擅自重新选择分配环境。
- 正反布局的实际源码2份通过448次独立行为对照、2个错误值mutants；
  修复后工厂值在外层、carried在for-let，多余transport可安全消除。原
  参照保持，测试增加源码位置覆盖并替换了旧“复制必须保留”断言。
  四项模型生产突变及一项撤销稳定声明的独立执行突变均检出，全部恢复；
  后者source harness exit1，恢复build/source/model均0。
- 1/200/201/400/800循环批次覆盖完整/no-op/proposal/current/trial产品、
  未知位置、正反source region及exact/one-short bytes/work；common
  flow直接验证外层前缀解析、禁止前缀提前访问native绑定、assignment与
  initialization的区分。方法成本按S/B/R/L记录，未恢复RSS/benchmark。
- 最终31目标构建32.9153s、fresh固定record2.8184s已通过；固定
  19records/10complete/0partial/9no-module、10语法检查通过，输出源码
  与规则259相同。最终精确63/63项定向测试584.8927s通过，名单一致且无重复。
  证据build/stage-one/loop-entry-verification，输出
  build/stage-one/loop-entry-final-rule260-stable；first-*及失败源码保留。
  初始化source目标合计2004份源码/482752行为对照/370有效mutants；
  original-flow目标合计10280对照/158mutants，含既有scoped/narrowed
  80份/6200/100。最终构建无warning/error；首轮重编译显示既有
  literal_plan.cc:59 kNoop warning，该历史代码未在本轮修改。
- **五项主干仍全部开放**。原生for仍使用明确的LegacyLoopAllocation
  完成/值能力；首次及逐轮环境分配、值复制与captured cell identity没有
  完整接入，不能据此宣称普通绑定生命期闭环。下一步继续该共同模型
  迁移；完整原始IR/SSA、finally/闭包、依赖收敛及里程碑终验仍待交付。
  其他表现优化与fullsuite/corpus/RSS/benchmark/DFX/agents保持暂停。

### 2026-10-09：规则259，当前声明分配对应

- 主干第2/3项：共同词法遍历一次发布区域到当前分配的typed对应，当前/
  trial均消费。ProjectedBindingScope使用当前块分配，原Symbol.scope
  作为其单元的旧fallback退出；缺失对应或被省略的分配保持unmodeled。
  lexical包含、初始化完成、值传播和捕获身份仍是独立义务。
- 实际RED2550证明：原始外层local合法；候选把声明缩入内层后，即使
  initializer支配外部读取，也不能使用原外层单元证明读取有效。新共同
  模型允许内层合法读取、切断离开后的旧单元定义；区域/Scope编号重排
  不改变结论。八种原始/投影、已知/未知、正反编号以及exact/one-short
  bytes/work、非法边界映射规格已接入原目标。
- 首轮选定验证61/63，pasteboard_storage_callback_source_test和
  rich_text_record_source_test检出原生循环复用回退：误把已有完成/值
  契约随未建模动态单元一起降为未知。已拆出显式LegacyLoopAllocation，
  只保留原有placement/confinement/capture义务下的完成与值能力，不
  恢复原Scope单元或授予动态cell身份证明。旧循环准入不扩展；完整
  copied per-iteration环境必须继续替代该过渡契约。两项原测试未弱化。
- 修复后四项定向验证4/4、17.93s通过；三个最终生产突变分别取消当前
  单元对应、忽略当前对应、删除既有循环值能力，均build0并被反例检出；
  代码全部恢复后重跑最终31目标/精确63名单/fresh固定record验证。
- 最终31目标构建39.6555s、fresh固定record3.0140s、精确63/63
  CTests580.5131s通过，名单一致/无重复。
  固定19records/10complete/0partial/9no-module、10源码语法检查通过；
  修复后源码与规则258完全相同，原机械临时变量回退已消失。证据
  build/stage-one/projected-lifetime-verification，fresh输出
  build/stage-one/projected-lifetime-final-rule259-preserved；first-*保留
  首轮失败，首轮header重编译显示原literal_plan.cc的kNoop warning，
  最终构建日志无warning/error；该旧warning未作为此主干变更修复。
- 新增48份实际优化后的声明缩短源码，涵盖四种jump/throw/return、
  copies0/1/200/201/400/800及嵌套block：新增3720独立行为对照、60
  行为mutants通过；该目标包含原32场景后合计6200/100，original-flow
  目标全部合计10280对照/158mutants（不含其他source目标）。边界/几何
  成本增长验证通过，计入整个恢复/打印路径，不是RSS或全局收敛证明。
- **五项主干均未关闭**；下一步沿原生for实际declarator/entry/更新
  owner，把环境分配和逐轮值复制完整接入共同完成图，不追加自增形状
  判断。原始IR/SSA完整对应、capture/finally、依赖重跑及终验仍开放。
  其他表现优化与fullsuite/corpus/RSS/benchmark/DFX/agents保持暂停。

### 2026-10-09：规则258，共同异常/跳转作用域转移

- 主干第2/3项继续实施：当前与trial共同adapter为每个完成事件发布typed
  before/after词法上下文；移动表达式使用当前投影owner。单份不可变
  FlowScopes索引在完成边上区分preserved、ended和unmodeled，不复制
  每个跳转的LeaveScope，也不以静态ScopeId证明动态cell相等。
- 修正实际RED：break/throw后把外部保存值试算合并到已离开的局部单元，
  原初始化支配检查仍允许外部读取。现在共同初始化事实同时要求单元存在；
  liveness/demand、reaching definitions、coverage、constants、copies和
  normal domains共享TransferBinding。点式normal-leave传播和离开后
  仅凭初始化支配的旧判断退出这些路径，不新增局部准入或pass轮次。
- 确定离开切断旧单元值；未知cleanup保留may-demand/definitions，禁止
  值替换。未知不能当成离开而删活跃写入。初始化product共享拥有索引，
  原graph释放后仍安全；旧value/completion-only输入继续提供需求/顺序，
  不取得单元身份证明。实际兼容性崩溃已由LLDB定位并有独立反例覆盖。
- 最终31目标构建91.7936s，fresh pasteboard record3.2971s，精确63/63
  CTests580.5861s通过，名单一致/无重复；构建无warning/error。
  固定输入19records/10complete/0partial/9no-module，10源码语法检查
  通过；源码内容与规则257相同，SHA-256前后相同，profile/budgets保持。
  证据build/stage-one/scope-exit-verification，fresh输出
  build/stage-one/scope-exit-final-rule258。
- 新增32份优化后跳转/异常/返回源码，覆盖copies0/1/200/201和嵌套block：
  2480独立执行对照、40行为mutants通过。原original-flow目标合计
  6560对照/98行为mutants；此统计不包含其他source目标。独立125个
  合法scope森林/反序编号、unknown-demand、adverse-storage-trial及共享
  ownership反例通过。Q200/201/400/800索引work1199/1205/2399/4799、
  allocated bytes12812/12845/25556/51044，exact/one-short admission通过；
  仅索引成本，不是完整graph/RSS/全局收敛结论。两个生产突变各build0/
  test-6，恢复后再运行上述完整选定验证。
- **五项主干均未关闭**。下一步明确为候选声明/narrowing和native-for
  per-iteration环境的完整生命期投影；精确finally/closure transport仍
  unmodeled。全原始IR/protocol对应、依赖驱动pass收敛、旧判断完整退出
  与统一变量终验继续开放。其他表现优化、fullsuite/corpus/RSS/benchmark/
  DFX/agents保持暂停，阶段一Goal active。

### 2026-10-09：规则257，共同普通绑定激活事实

- 主干第2项继续实施：RecoveryFlow区分canonical作用域分配点、enclosing
  单元、unmodeled局部生命期和仅有完成顺序的synthetic输入。ScopeId标识
  静态分配点；每次进入产生新动态单元，不授予跨迭代值/capture身份相等。
  canonical名字包含、稳定入口及捕获/暴露义务仍独立。
- 修正实际RED：catch初始化原先位于handler EnterScope之前；现在所有
  CatchClause都在作用域进入后、body语句前初始化参数。初始化共同入口
  先证明分配支配初始化，再发布完成顺序；原错误catch顺序退出决策路径。
- allocation/normal departure的共同gen/kill由liveness/demand、reaching
  definitions、coverage、constants、copies和normal domains共享消费。
  unavailable边界事实保留，不能因旧定义被切断而虚构all-read证明；不
  顺带删除可观察undefined初始化、效果或环境，不新增优化器局部形状规则。
- 当前/trial仍使用共同adapter和修订失效机制；pass顺序、轮次、预算未改。
  **第2项未关闭**：abrupt unwind、投影/narrowing后的声明与native for
  per-iteration环境尚未完整接入。canonical站点不是动态cell等价证明。
  local copy roots仍未获证明；复杂finally/闭包/暂停精度保持明确缺口。
- 最终31目标构建96.0989s，fresh pasteboard record2.9132s，精确63/63
  CTests552.2499s通过，名单一致/无重复；构建无warning/error。
  原19records/10complete/0partial/9no-module、10语法检查、源码内容与
  规则256相同；原SHA-256前后相同、profile/budgets保持。证据位于
  build/stage-one/binding-activation-verification，fresh输出为
  build/stage-one/binding-activation-final-rule257。
- 新独立4096状态执行对照、两项reset反例、初始化提前到分配之前的反例、
  正反event arena和exact/short预算通过。B=200/201/400/800森林分别有
  4B节点、4B-1边、B活跃事实；work39643/39859/83951/177347，累计
  allocated bytes111576/112093/223168/446352。不是进程RSS或全局收敛证明。
- 新8份实际源码覆盖catch重复进入、live undefined初始化、copies0/1/
  200/201及嵌套块：40独立执行比较、16语义变异反例通过。与现有
  96/2880/4、8/620/20、18/540/18证据共4080比较/58反例；另两项
  生产变异（删除激活kill/分配前提）build0/test-6，恢复后通过。
- 所有五个主干交付、统一变量里程碑和阶段一Goal保持开放；下一步接入
  异常/跳转离开及完整current/trial生命期变化，随后依赖收敛与终验。
  fullsuite/扩展corpus/RSS/benchmark/DFX/agents仍暂停，无表现层优化。

### 2026-10-09：规则256，原始成功写入对应与共同证明失效

- 主干第1/3项继续实施：既有TreeFlow快照、phi拷贝、并行拷贝环暂存、异常
  stage和catch提交的证明路径发布typed原始结果/贡献与实际源码成功
  写入对应；表只分配一次，移交immutable OriginalFlow。共同源码定义
  借用条件provenance，优化器不从AST或操作序列重新推断对应。
- 共同normal分析区分值依赖和完成依赖。原始更精细域只有在当前RHS、
  写入和前置执行路径的证明仍成立时消费；改写分支、值或声明边界会
  传播失效。条件Boolean不会混入存储类型，宽泛原始phi域也不会
  丢弃源码已证明的精度。旧raw ExprId域提示仍仅是provenance。
- 原SSA编号不是动态值/cell身份；数值更新的旧结果与成功新写入分开，
  postfix ToNumeric内部scratch尚未映射，不把旧结果域冒充新存储域。
  不新增样本、同块、邻接、生产者白名单、固定布局或pass轮次条件。
- 首次限定验证62/63，独立小图值流因没有声明边界索引被错误拒绝。
  移除这条额外准入：缺少对应只使原始路径证明未知，当前值流仍求解；
  已提供但越界的point仍是真实ownership错误。新增实际原始对应的
  missing-boundary反例先RED后GREEN，原独立oracle保持不变。
- 当前/试算复用同一稀疏值与完成图；变化闭包单调传播一次，域按有限
  lattice求解，不按定义遍历全图、不构造read×definition关系。200/201/
  400/800 work=546917/549389/1118887/2297280，bytes=2812123/
  2815741/5624683/11249803；exact/one-short及1,024独立小图通过。
- 新18个checked原始SSA图源码用例覆盖复制、编号重排、拆块和前置
  分支限定域；540次独立执行及18个行为反例通过。既有96原始SSA
  图源码/2880比较/4反例、8共同源码adapter成功/异常/循环源码/
  620比较/20反例仍通过。它们不冒充完整ABC语料的端到端验收。
- 最终代码两次fresh生产错误修改均构建成功，移除原始严格域消费在
  check332 SIGABRT -6，移除执行上下文依赖在check344 SIGABRT -6；
  恢复后再完成最终限定验证。声明边界缺失RED在check340 -6。
- 最终31目标构建28.1674秒，精确63/63 CTests550.8872秒，名称集合
  完全相同、无重复；fresh固定pasteboard导出2.8423秒，19records/
  10complete/0partial/9no-module、10语法检查，源码相对规则255无变化，
  profile=language_numeric_constructors_v1，预算未增。输入SHA前后仍
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/original-definition-verification，首次失败保留在
  attempt-one；最终构建无warning/error。
- 完整原始effect/protocol与未映射normal根、local/catch动态激活、
  候选声明/lifetime delta、依赖收敛及终验仍开放，五项交付均未关闭。
  不继续表现层优化；full suite/扩展语料/RSS/benchmark/DFX/agents
  仍暂停。阶段一Goal active，未修改输入或memory，未push或新建worktree。

### 2026-10-09：规则255，共同成功定义与投射数值域

- 主干第1/3项继续实施：原SSA参数入口域按已校验参数对应接入共同
  source成功定义；正常写入、分支合流、循环携带值和异常路由驱动
  `FlowNormalKinds`。静态定义编号不证明跨执行的值相等或cell身份。
- 消费旧original.normal_kinds[read]的源码域决策退出；共同入口先以
  source语义保守建图，再用同一图的all-read liveness与稀疏正常值
  方程求解，最后独立精化effects。试算修改写入值后，下游读不能
  继续沿用旧Number提示。source-only无原始输入的模式仍保守。
- 有限ValueKinds格单调求解，不构造read×definition关系、不按绑定
  遍历全图；无来源循环变成未知。预算计入所有存活输出和阶段scratch，
  分析完成后释放scratch。当前计划和试算共用入口，拒绝外部提供的
  normal_kinds证明；每次commit仍整体销毁缓存。
- 新反例在旧实现上失败；恢复旧raw域消费的fresh错误修改SIGABRT。
  独立小图参考算法1,024例，正反arena、事件重排、分支/循环/异常边，
  exact/one-short和200/201/400/800几何增长已通过聚焦验证。
- 源码adapter的8个生成源码变体覆盖普通/循环/失败写入和catch读，
  620次独立执行与20个初始值删除、提前写回、转换/效果删除反例通过；明确
  不冒充原始ABC证书。实际原SSA96源码/2880执行对照仍由既有测试验证。
- 最终限定31目标构建185.4039秒通过；补充原SSA入口域/exposure/未知
  域/非法参数对应的单元反例后，在该项CTest执行前单独重建目标通过。
  本次精确63/63 CTests547.8327秒通过，名称集合完全匹配、无重复。
  fresh固定pasteboard导出3.3846秒通过，19records/10complete/0partial/
  9no-module、10语法检查，changed_sources=[]（对规则254）；原SHA-256
  前后854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e
  相同。证据在build/stage-one/normal-domain-verification。
- 构建保留已知literal_plan.cc kNoop switch警告；无新warning/error，
  本轮未扩展其他优化。以上是限定验证，未冒充全语料/RSS/完整里程碑。
- 全部五项主干仍开放：完整原始operation/
  protocol/effects对应、local/catch activation、完整候选life delta、
  依赖收敛和里程碑终验尚未完成。其他优化与既定扩大验证仍暂停。

### 2026-10-09：规则254，当前计划与试算共享投射事实入口

- `RecoveryProjection` 持有同一候选的 expression facts、共同流图和
  occurrence initialization。CurrentFlow 与快照试算实际调用相同
  `PrepareRecoveryProjection`；候选不再借当前计划的 effects/domains。
- replacement/transport 的事实按实际 producer/primitive 和投射依赖
  顺序计算，迭代 DFS 不依赖 arena/原 finish 顺序。保守初始化 bootstrap
  包含所有 completion 边，refinement 只消除已证明不可能的 throw。
- 删除手工跨 commit 转运初始化证明的实际路径、公共内部 API 和
  pass preservation flags；所有 occurrence proof 随 commit 失效。
  demand/snapshot checkpoint 仍是调度证据，不冒充可重用分析。
- 无初始化假设的 normal-result 查询继续惰性执行共同纯分析；流消费者
  与最终输出强制共同 completed bootstrap。缓存 admission 属于该
  ProjectionFacts，commit 销毁所有缓存/初始化证明后释放占用；峰值和
  工作量保留。旧 flow buffer 复用暂退，避免误释放仍持有的容量。
- 最终冻结版本31个目标构建50.9162秒通过，fresh固定 pasteboard 导出
  2.6827秒通过，精确63/63 CTests548.1334秒通过；名字集合与selection
  完全匹配。记录在build/stage-one/trial-projection-verification。
- 固定输入19records/10complete/0partial/9no-module，10语法检查通过，
  changed_sources=[]（对规则253），原输入SHA-256前后相同。
- unknown producer 替换 Number read、删除初始化声明的两项错误修改
  fresh测试均SIGABRT；恢复后最终验证通过。反转arena/根列表、循环
  replacement、raw fallback、exact/one-short预算与工作量均通过。
- 800组变量曾耗尽默认预算：阶段临时内存和已销毁缓存被累计计账。
  修复所有权/峰值计账后保持原预算通过，400→800工作量约2.075倍；
  无样本/布局准入。初次宽头重编译暴露既有literal_plan.cc kNoop
  未穷尽switch警告，本轮最终增量构建无warning/error，未扩大其他优化。
- 修正旧测试对已省略/未建模读取的inert断言，仍检查所有保留观察并
  对全部生成源码做独立执行对照。初步或失败运行不计最终验收。
- 全部五项主干仍开放。此处未证明完整原始 effects/protocol 映射、
  local/catch activation、完整候选 lifetime delta 或全 pass 收敛；
  表现层优化及既定大范围验证暂停不变。

### 2026-10-09：规则253，共同 public property 引用与完成阶段

- 主干第1项继续实施：共同源码 adapter 对所有 MemberExpr/MemberTarget 发布
  typed Reference acquisition、receiver、key、access、normal continuation 与
  throw/finalizer route/current owner；原始 base/key 的 raw/resolved UseId ranges
  通过既有索引接入。成功结果与 receiver/key/get/set 的异常出口分离，不把
  来源 ranges 冒充原始 VM flags 的完整阶段证书。
- 原 generic children→single-effect 的 member/target 路径退出；GetValue 在
  call arguments 前，PutValue 在 RHS 后，reference 保存求值时的 base/key。
  shared motion、定义需求与各现有试算 builder 消费这些事件。refinement
  复用唯一 receiver/key 域规则；整个赋值的 normal-store 总结不抹去独立
  阶段的 throw。试算域重算完整性仍属于开放的第3项，未宣称已经闭合。
- 实施中独立 Node 执行发现2024/2025规范差异：2024早转换假设不适用当前
  native source，初始RED不计有效修复证据；已纠正为2025 deferred key，
  正确的 inert acquisition 义务在修复前 fresh property check99 exit134。
  源码 profile/version 边界见 [reference phases](../../reference/source-property-references.md)。
  不扩展旧模式识别、不新增准入条件或静默改变原始 Ark effects。
- 聚焦验证8源码/336独立执行比较/6反例，正反arena、key hook/throw、
  null/undefined receiver、Proxy/getter/setter、成功/失败写入状态、原始
  operand role/use身份、精确/短预算和200/201/400/800维度成本。首轮试算
  测试对universal motion误用了normal-only预期及scaling fixture漏声明已
  修正，未增加生产条件。最终62/62项CTest494.51秒通过，exact名称集合
  为既定60项加2项property测试；原SSA96源码/2880执行比较/4反例以及
  既有独立值/复制/完成/存储/名字/初始化对照均通过。
- 最终30目标构建102.89秒无warning/error；fresh固定pasteboard导出3.26秒，
  19records、10complete/0partial/9no-module，10份源码语法检查通过，
  输出与规则252字节一致，输入SHA前后不变。原预算及
  language_numeric_constructors_v1保持。证据
  build/stage-one/property-flow-verification；不是全corpus/Ark平台/RSS终验。
- 五项主干仍全部开放；下一步继续完整原始 operation/region/protocol
  effects/completion 对应，再推进普通绑定生命期、试算与依赖收敛。
  完整套件、扩展corpus、RSS/benchmarks/DFX/agents暂停保持。

### 2026-10-09：规则252，原始 SSA 值域与使用点接入共同恢复事实

- 主干第1项的实施子闭环：在同一次 TreeSemantics 校验中保留 OriginalFlow，
  含原定义/使用/CFG边/阶段/来源及操作 normal、before_throw、exceptional、
  success-only result。操作不复制 opcode variant，不新增第二套 ISA 解码。
  OperandCheck 将已经核对的 raw/resolved operand、真实 UseId 与 ExprId 一次
  发布；结果域与多个对应值保守 join。不是按 PC、名字或布局寻找来源。
- 最终 TreeFlow 共享拥有这些不可变事实。普通函数、family、record 根与
  类静态初始化均传到 SourceVariables；默认签名/类正文的规范树变换替换
  最终对应。record family 退出重复 VerifyTreeFlow 路径，直接移动最终校验
  的 flow；class-name/打印准备按同一 tree 和 original 输入借用计划。
  直接/partial FunctionTree 仍允许缺原始输入，保守输出，不制造证明。
- exact projection 的共同 ExpressionFacts 消费正常值域；替换沿实际 producer
  传播域，primitive 按实际值给域。唯一穷尽语言表同时服务效果分析、flow
  构建与初始化后的 refinement；旧 IntrinsicEvaluation 仅为缺域调用入口。
  域可排除已知 primitive 的转换 hook/语言异常，不能证明初始化、值相等、
  可共享 cell 或删除子 GetValue/效果。原始 VM guard/protocol effects 不由
  原始 flags 直接消除；String concat 继续尊重 resource contract。
- RecoveryFlow 关联原始输入 ranges 和源码表达式 normal continuation，原始
  OpResult 的 commit 与异常出口保持独立。事实保留/输入 scatter 线性、所有
  容量与实际 shared control-block 分配计入预算；每 pass 不复制原始 graph。
  原始 effects 已保留，但到共同效果/完成事件的完整映射尚未闭合，特别是
  normalized multi-operation、绑定/属性 reference 与签名/协议投射。
- 有效 consumer mutant：保留全部原始元数据、移除实际 SSA 域读取，fresh
  function_tree_test SIGABRT check190。新的 independent source 对照覆盖未知
  hook/throw/BigInt、正常 Number、复制、SSA 重编号、block split、反向源码
  arenas、200/201/400/800、精确/短预算与维度成本；最终96源码、2880次
  独立执行比较、4个语义反例通过。首轮新增metrics脚本早于配套binary导致
  缺字段失败，该混合版本结果作废；随后冻结代码版本重跑完整有限集合。
- 最终29目标构建86.39秒，exact选定60/60项CTest491.29秒通过；包含原59项
  与新增original_flow_source_test，名称集合由combined-coverage.json核对。
  fresh固定pasteboard导出2.86秒：19records、10complete/0partial/
  9no-module，10源码语法检查通过，输出字节与规则251一致，输入SHA前后
  不变；language_numeric_constructors_v1与原预算保持。最终目标构建
  无warning/error。证据build/stage-one/original-flow-verification；本轮
  独立执行对照使用生成的JS，未宣称平台解释器或全corpus验收。
- 五项主干仍均未关闭；下一步继续原始 operation/reference/phase 到共同
  模型的完整对应，再推进普通绑定生命期、试算模型与依赖收敛。暂停其他
  表现优化；不恢复 full suite/corpus/RSS/DFX/benchmarks/agents。

### 2026-10-09：规则251，共同入口求值与剩余控制分离

- 共同adapter一次发布mandatory entry-value及其事件来源；statement分析
  同时保留完整观测和排除本语句入口求值后的residual观测。子语句效果、
  定义、绑定完成、return/jump、cleanup与可能重复仍进入父区域，不能
  用子语句自己的入口排除证明父区域无作用。入口throw/resume由原表达式
  保留；普通return与保守cleanup不被排除。无空if/body/opcode布局匹配。
- 独立StatementValues pass只消费exact kStatements及共同依赖，待最终
  BindingValues与独立需求清理完成后，将已证明inert residual的区域
  投影为原位置的discarded表达式。完整自有提案先于首次提交/事实失效；
  再独立需求、operand/statement清理，最后grammar。打印与flow消费同一
  statement_values投影；不重推语义、不移动求值、不回退storage/motion。
- 有效旧输出RED390 exit134，结构先通过。首轮3/3通过；扩展测试构造的
  成员key类型、错误替换位置、未拥有条件及catch嵌套scope问题已修正，
  未更改生产准入。之后3/3在62.46秒通过，900源码/23400执行比较/
  103反例，独立16384入口森林对照通过；补入receiver/暂停删除反例后，
  最终聚焦5/5在90.65秒通过（后续测试scope构造仅作等价可读性整理）。
- 最终原29目标构建86.04秒，exact原59/59项CTest490.45秒通过，
  combined-coverage.json核对名称集合完全一致。900源码/23400执行比较/
  105反例和16384入口森林，以及既有2048循环图、78732完成森林、28812
  完整观测森林和各值/复制/名字/初始化/存储/真实record对照均通过。
  fresh固定pasteboard导出2.69秒：19records、10complete/0partial/
  9no-module，10源码语法检查通过，输出字节与规则250一致，输入SHA前后
  不变；language_numeric_constructors_v1与原预算保持。最终目标构建
  无warning/error，早前完整聚焦构建仅既有literal_plan.cc kNoop warning。
  证据build/stage-one/statement-values-verification。
- 覆盖短路、条件、逗号转换、参数赋值、getter/receiver、catch、generator
  next/throw/return与async thenable恢复；reverse arena、200/201/400/800、
  来源/anchor和预算反例保持。分析和提案为线性遍历，无逐区域图搜索。
  多个有作用分支的合成求值程序、完整finally、activation身份、原IR/SSA
  对应与全局收敛仍开放；统一变量/结构gate/阶段一Goal未完成。
  完整套件/扩展corpus/RSS/benchmarks/DFX/agents暂停。

### 2026-10-09：规则250，共同图证明有限正常完成

- Source adapter发布normal-exit/repeat-point，并区分jump与return/throw。
  FlowCycles在当前完成图上一次迭代Kosaraju，所有异常/resume/cleanup边
  计入；无SCC周期的重复点可证明有限遍历。缺失证据、effects、定义、
  暂停和保守cleanup仍保持。不是一般数值循环终止证明。
- 共同statement分析按实际目标点索引nearest owning normal exit，差分
  累计仅将尚未消费的jump标记abrupt；所有return/throw/resume/cleanup边
  另行穷尽汇总。旧无条件repeat/abrupt jump在共同义务证明后退出，不
  匹配break/body布局。kCycles与kStatements共享exact revision、自有
  proposal/首次提交失效和既定pass顺序，无新增整轮或回退storage。
- 有效旧实现RED366 exit134，结构先通过；graph-only abrupt/resume
  反例再现旧遗漏RED2092 exit134，修复共同边观测。独立2,048循环图、
  78,732完成森林与200/201/400/800、reverse/split、预算反例已通过。
- 最终原29目标构建179.70秒，exact原59/59项CTest475.69秒通过；
  combined-coverage.json核对选定名称集合一致。语句清理780份源码/
  19980执行比较/83变异反例；既有值、复制、需求、初始化、名字、存储、
  声明、循环和真实record回归保持通过。以上独立图对照均在最终运行中
  再次通过；来源/anchor、版本/依赖/首次提交失效及预算反例通过。
  fresh固定pasteboard导出2.81秒：19records，10complete/0partial/
  9no-module，10语法检查通过，源码字节与规则249一致，输入SHA前后
  不变；原预算与language_numeric_constructors_v1契约保持。
  最终构建仅既有literal_plan.cc kNoop switch warning，无error。
  证据build/stage-one/finite-completion-verification；非终止测试的VM
  deadline只构成有界执行证据，不声称一般终止证明或平台执行验收。
- 数值终止、outer SCC内更精确子区域、handled throw/完整finally、
  activation身份、原IR/SSA与全局收敛仍开放，统一变量/结构gate/
  阶段一Goal未完成。完整套件/扩展corpus/RSS/benchmarks/DFX/agents暂停。

### 2026-10-09：规则249，共同语句效果与完成观测

- RecoveryFlow适配器一次发布retained语句ownership forest、完成/progress
  义务和每个事件当前owner。AnalyzeFlowStatements汇总效果、保留定义、
  abrupt、cleanup、repetition、suspension；所有保留语法含不可达声明参与。
  private inert write不能替代独立需求删除，空循环不能忽略不终止。
- InertStatements消费exact ProjectionFacts中的kStatements，退出原
  ExpressionStmt/declaration_values局部重判；契约声明依赖闭包，自有
  omission提案完成后才首次提交/失效。最终BindingValues与独立需求
  清理后，既定normalization阶段清理纯控制，再进入grammar。无新整轮
  pass、回退storage/alias/motion或空if/同块/邻接/producer准入条件。
  打印器只消费省略计划，把整棵被省略子树/anchors映射到来源边界。
- 有效旧实现RED353 exit134，结构验证先通过。首轮返回反例发现漏记
  abrupt完成，补入共同adapter；两项历史快照形状断言随合法纯控制
  消除更新，保留独立错误TDZ移动和实际输出更新/返回删除反例。
  最终聚焦5/5在64.55秒通过；28812独立4点森林、反向ID/事件、
  200/201/400/800 chain/star、exact/short预算和非法ownership通过。
  11种真实控制源码组合、反向语句/表达式arena、来源/anchor边界通过。
- 最终原29目标构建177.30秒，exact原59/59项CTest463.38秒通过，
  combined-coverage.json核对选定名称集合一致。语句清理720份源码/
  18360行为比较/77变异反例；存储1181/194324/152，声明2568/513600/
  100、初始化2004/482752/368、循环276/13976/68和既有真实record
  回归通过；旧值/复制/名字/版本/需求/初始化checkpoint反例保持。
  fresh固定pasteboard导出3.10秒：19records，10complete/0partial/
  9no-module，10语法检查通过，源码字节与规则248一致，输入SHA前后
  不变；原预算与language_numeric_constructors_v1契约保持。
  最终构建仅既有literal_plan.cc kNoop switch warning，无error。
  证据build/stage-one/statement-semantics-verification。
- 有限正常完成汇总是保守分析；循环终止、handled abrupt与精确finally
  仍有精度缺口。原IR/SSA对应、activation单元身份、试算精度、共同
  全局收敛与统一变量/结构gate/阶段一Goal继续开放。完整套件、扩展
  corpus、RSS/benchmarks、DFX与agents继续暂停。

### 2026-10-09：规则248，共同完成值查询贯通私有复制链

- 共同all-read FlowLiveness在正常复制定义成功提交前查询源当前值，
  与真实读取一起在同一稀疏worklist闭合。查询不是执行GetValue，
  不新增FlowReference、不改变初始化/效果；observable FlowDemand不
  以这些查询为根。干涉可保守消费扩展事实，删除写入仍由独立需求决定。
- FlowCopies把查询到的私有中间单元加入共同稀疏依赖，复制成功时继承
  源已证明的入口当前值关系。中间单元后来改写不改变其他单元保存的值；
  入口根写入仍撤销关系，合流要求所有路径一致，失败RHS绕过定义，
  暴露单元/无来源循环保持unknown。旧direct-entry-only生成退出私有链
  决策，不增加pass轮次、回退阶段、复制深度/块/邻接/producer形状条件。
  原exact-revision缓存、自有提案/首次提交失效、名字/作用域/初始化及
  独立清理契约保持；不把local/catch/capture提升为activation根。
- 有效旧实现RED684 exit134，结构先通过。首轮聚焦2/3的源码失败为
  等价变异：正常读取恢复已合法删除current，输出scratch改名current
  没有合并仍同时活跃的值。改用独立原程序验证错误共享，实际输出验证
  错误入口替换/读取删除；不要求生产保留无用声明。源码单项36.95秒，
  最终聚焦3/3在56.50秒通过。8种多级复制/中间转换/根写入/混合分支/
  逗号异常RHS/循环刷新/mapped/postfix组合、反向arena及几何规模通过。
  独立524288双单元路径状态，200/201/400/800链拆分边/反转事件arena，
  observable中间单元、query-only无observable需求、非法边界及exact/
  short预算通过；既有221184直接复制与版本/名字/值反例保持。
- 最终原29目标构建84.44秒，exact原59/59项CTest497.55秒通过；
  combined-coverage.json核对选定名称集合一致。存储1181源码/194324
  行为比较/150变异反例，声明2568/513600/100、初始化2004/482752/368、
  循环276/13976/68及既有真实record回归通过。fresh固定pasteboard
  导出2.69秒：19records、10complete/0partial/9no-module，10语法检查
  通过，源码字节与规则247一致，输入SHA前后不变；原预算及显式语言
  契约language_numeric_constructors_v1保持。最终构建无warning/error，
  早期完整聚焦构建仅原literal_plan.cc kNoop warning。
  证据build/stage-one/transitive-copy-verification。
- 共同语句完成/效果消除尚未清理读取恢复后暴露的纯空控制，是实际
  表示缺口，不能加局部分支形状匹配。原IR/SSA、activation单元身份、
  试算精度、完整finally与共同全局收敛仍开放。统一变量/结构gate/
  阶段一Goal active；全suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则247，共同可用复制恢复可写入口绑定的读取

- 新FlowCopies在共同RecoveryFlow/normal-result复制定义、稀疏活跃性、
  invocation reachability与已初始化的binding-value读取上计算gen/kill。
  证明的是读取发生时与入口单元当前值相同，独立于全定义不可变根及
  存储合并。每次成功源写入撤销关系；未知目标写入也撤销；抛异常的RHS
  未经过成功定义，其异常路径保留原状态。合流需所有可达路径一致。
  source stable_entry只表达adapter单元证据，排除mapped/capture/signature
  外部改写；local/catch跨activation身份仍未证明，不能用词法scope替代。
- Late BindingValues显式声明Copies及其Liveness/Flow/Expressions依赖，
  取得同一revision的不可变/当前复制结果，按ExprId归并成自有提案。
  实际scope/name独立证明后提交；首次提交原子失效含copies的聚合事实，
  后续死定义/初始值/绑定清理重新取当前事实，最后恢复grammar。
  旧“全函数不可变才可恢复此读取”的唯一决策退出这些可写入口场景；
  不可变根的独立证明保留。无回退阶段、额外轮次、打印器分析或样本/
  同块/邻接/操作序列准入。计量区分live facts与实际检查前驱连接，
  所有稠密事实/连接/输出均计入预算，无复制对枚举或每绑定图遍历。
- 有效功能RED619 exit134，结构验证先通过。独立未知写引用反例在
  原solver上1517 exit134后补齐通用目标kill。首个生产构建漏tree_checks
  include已修正，未用失败产物运行测试。首轮聚焦2/3的源码失败仅为
  新测试未识别既有(0,report)值调用；修正AST目标解析，无生产限制补丁。
  最终聚焦3/3在48.43秒通过：11种写入/刷新/合流/循环/异常/prefix/
  postfix/mapped/shadow源码组合，反向arena与0/1/200/201/400/800，
  独立221184小图路径状态及未知写/exact-short预算/非法边界通过。
- 最终原29目标构建81.90秒、exact原59/59项CTest478.62秒通过，
  combined-coverage.json核对名称集合一致。存储1085源码/173588行为
  比较/122变异反例，声明2568/513600/100、初始化2004/482752/368、
  循环276/13976/68；既有逐读值65536/稳定值32768、名字/作用域、
  pass所有权/依赖/清理、初始化版本运输和真实record回归通过。
  fresh固定pasteboard导出4.95秒：19records、10complete/0partial/
  9no-module，10语法检查通过，源码字节与规则246一致，输入SHA前后
  不变，原预算和language_numeric_constructors_v1保持。最终构建无
  warning/error；早期完整构建仅原literal_plan.cc kNoop warning及已修正
  的缺include错误。证据build/stage-one/available-copy-verification。
- 原IR/SSA接入、local/catch/capture activation、试算精度、完整finally
  与共同全局收敛仍开放，统一变量/结构gate/阶段一Goal active。
  全suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则246，初始化读取证明的版本所有权

- InitializedOccurrences自有投影revision与读取mask，表达式和值分析统一
  经InitializedReads验证当前版本/analysis阶段。首次提交以pass输入版本
  显式TransportInitializations，按已有语义契约运输到新版本或撤销；
  noop不改版本，同批后续写入不再运输。旧unversioned缓存/裸preservation
  boolean决策退出生产路径，其余flow/effect/name事实仍聚合失效。
- 旧实现有效RED：analysis阶段仍可取得初始化保留许可，1836退出134。
  新反例覆盖每种pass、stale/future/output版本、提交阶段禁止读取、
  noop/batch及0/1/200/201/400/800连续运输。聚焦3/3在35.53秒通过；
  最终29目标构建82.06秒、exact原59/59项CTest在472.34秒通过，
  combined-coverage.json核对选定名称一致，包含新运输协议反例与既有
  65,536逐读值/32,768稳定值、名字、清理、版本检查点及真实record回归。
  存储953源码/145,076行为比较/92组变异反例，声明2568/513600/100、
  初始化2004/482752/368、循环276/13976/68通过。fresh固定pasteboard
  导出2.66秒：19records、10complete/0partial/9no-module，10语法检查
  通过，源码字节与规则245一致，输入SHA前后不变，原预算及语言模式保留。
  最终构建无warning/error；早期完整聚焦构建只有既有literal_plan.cc
  kNoop warning。证据位于build/stage-one/initialization-revision-verification。
  没有新增输入形状准入、图分析或mask复制。
- 原IR/SSA接入、local/catch/capture根、试算精度、完整activation/finally
  与共同全局收敛仍开放，统一变量/结构gate/阶段一Goal active。
  全suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则245，按读取发生点的符号值恢复

- 共同稀疏normal-value分析发布binding_reads，独立于all-definition
  不变量。ReadValues在storage/snapshot之后按实际读取发生点恢复
  immutable entry值；两个提案分别证明scope/name，首次提交前均自有。
  Flow/表达式/class-name/printer复用BindingProjection的occurrence优先
  解释，写入/update两半仍用Storage；删除由独立清理pass消费新事实。
- FlowLiveness一次发布所有completion边的invocation reachability。
  contextual合流消费可达前驱，保留原保守liveness/global/name义务；
  catch return后无入口的正常出口不再污染后续值。当前snapshot motion
  复用同一mask，其重复遍历退出；legacy standalone与手工无mask图
  保留明确的保守兼容路径。无新的catch/块/邻接/producer准入。
- 新8种源码组合覆盖mixed prior/liveundefined/catch状态/抛异常RHS，
  written-root/TDZ shadow/source-local/mapped反例、反向statement arena、
  0/1/200/201/400/800及exact/short预算。65,536组独立reach/value小图
  对照、shared motion与16种storage/read/occurrence投影通过。首次571
  是漏声明scratch的setup失败；修正后重建真实HEAD消费实现，结构/
  打印通过后在576获得有效功能RED。错误coalescing候选可因不可变参数
  快照或移动位置变成等价；测试同时验证独立原始程序与实际输出，要求
  实际行为反例，不限制生产恢复。聚焦3/3在32.76秒通过。
  最后无提案分配优化已纳入最终验证：29构建目标86.49秒通过，原定
  59/59项CTest在469.44秒通过，combined-coverage.json核对选定名称一致。
  存储恢复953份源码、145,076次行为比较、92组语义变异反例通过；既有
  初始化、声明、循环、名字/作用域、pass所有权/依赖/清理、版本检查点
  与真实callback/rich-text/progressbar/EntryAbility回归通过。
  fresh固定pasteboard导出2.75秒：19records、10complete/0partial/9no-module，
  10份源码语法检查通过，源码字节与规则244一致，原输入SHA前后不变。
  原预算与language_numeric_constructors_v1保留；最终构建无warning/error。
  证据位于build/stage-one/read-binding-verification，未扩大验证范围。
- 任意local/catch/capture符号根、试算值精度、原IR/SSA/full activation/
  finally及共同增量全局收敛仍开放；统一变量/结构gate/阶段一Goal active。
  全suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则244，共同正常结果值贯通复制干涉分析

- CurrentFlow提供共同ExpressionFacts的exact-revision正常结果绑定，
  赋值/初始化成功定义直接消费该copy事实；它表示完成时的当前绑定值，
  不等于旧快照或全定义不可变入口值。当前生产路径不再构造/扫描
  direct-SymbolExpr复制索引；显式unknown不会再次按语法猜测。
- 原稀疏affinity/interference消费新事实，活跃冲突、初始化/异常完成、
  scope/name/capture/lifetime仍独立证明；效果与死写清理由独立pass决定。
  借用结果向量只活到build结束，flow发布自有typed IDs；CurrentFacts
  版本及first-commit聚合失效保持。试算移动图不借用当前正常值精度，
  其原直接读fallback暂作为能力缺口保留，无新增producer形状分支。
- 有效旧实现RED exit134（533）。新12种源码组合覆盖逗号/效果、条件/
  OR/AND/nullish、prefix、被观察的嵌套赋值与词法初始化；不同活跃值、
  混合分支、postfix旧值和catch前undefined反例保留必要存储。无观察
  内层写入会被早期清理合法移除，不为测试阻止；postfix反例提前观察
  区别。native夹具未归属声明的结构失败只计setup，已修正。
  反向arena、0/1/200/201/400/800、独立执行/变体、明确known/unknown、
  非法span/ID和exact/short预算覆盖；聚焦3/3在29.04秒通过。
  聚焦修正5/5在49.10秒通过；最终29目标构建19.99秒、exact原59/59
  选定回归444.14秒通过，combined-coverage.json核对名称集合一致。
  首次完整构建169.60秒后57/59，两个失败均为旧ScopedCopies全无alias
  断言：共享存储后的独立清理删除了惰性同值条件，其condition引用消失，
  upstream参数alias合法。改按实际保留引用验证，加入mixed-arm对照
  要求条件和两个TDZ-shadowed副本保留；独立truthy/falsy Cartesian
  执行和错误变体通过，无生产限制补丁。首次失败证据保留。
- 存储857源码/124340行为比较/76错误变体；定义消除588/14796/65，
  声明2568/513600/100、初始化2004/482752/368、循环276/13976/68；
  共同值/名字/retirement/初始化subset/调度反例与真实callback/rich-text/
  progressbar/EntryAbility回归通过。首轮仅既有literal_plan.cc kNoop
  switch警告，最终增量构建无新警告。
- fresh固定pasteboard最终导出1.32秒，19records、10完整/0部分/9无module，
  10语法检查通过；源码字节等于243，原language_numeric_constructors_v1
  与预算保持。输入SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  重跑曾因复用首轮目标目录被kDestination拒绝，改用新的final目录后
  完成；未计为源码验收。最终证据在build/stage-one/normal-copy-verification，
  导出在build/stage-one/normal-copy-rule244-final。
- 共同生产的复合正常值复制关系已接入；试算值精度、任意local/catch/
  capture符号值、完整activation/finally、原IR/SSA及共同增量全局收敛
  仍开放。统一变量里程碑/结构gate/阶段一Goal active；全suite/广语料/
  RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则243，读取值与存储投影按共同阶段组合

- ReadValues提案按当前flow的最终slot索引，原符号先查read_bindings
  会漏掉同组其他成员。新typed BindingProjection集中解释早alias、
  storage与晚alias，读取随后消费该slot上的稳定值；写入和update两半
  保持Storage。共同flow与源码消费者复用同一实现，旧重复解释退出。
- 每次查询O(1)、无分配；阶段所发布的direct roots是计划契约，不是
  字节码/图形状准入。现有共同scope/name、完成/effect/初始化/value
  证明及first-commit失效保持；ReadValues之后禁止alias/storage/motion
  修改，独立清理完成才进入Grammar，无额外pass轮次或整函数重试。
- 真实复制关系下，旧实现通过共享slot断言后在声明消除处RED exit134
  （497）。新4种源码组合覆盖多分支复制、写入口参数、内层同名遮蔽与
  RHS效果；反向arena、0/1/200/201/400/800、exact/short预算和独立
  执行/错误变体覆盖。共同flow八种alias/storage/read组合验证实际
  读取、赋值目标、update读写两半、原结果不变与预算反例。
  聚焦3/3在25.97秒通过；最终共同值、名字、retirement、storage、
  初始化subset及调度反例通过，原回归期望保持。
- 当前29目标构建171.29秒通过；最终59/59选定回归459.05秒通过，
  combined-coverage.json核对通过名称集合与原选择完全一致。存储
  713源码/103604行为比较/64错误变体；声明2568/513600/100、初始化
  2004/482752/368、循环276/13976/68；真实callback/rich-text/
  progressbar/EntryAbility回归通过。既有literal_plan.cc的kNoop
  switch警告保持，无本次新增编译警告。
- fresh固定pasteboard导出2.77秒，19records、10完整/0部分/9无module，
  10语法检查通过；源码字节等于242，language_numeric_constructors_v1
  与原预算保持。输入SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/binding-projection-verification。
- 共同normal-result copy尚未全部接入interference affinity，是适配
  精度缺口，不能用Sequence等producer形状分支补齐。任意local/catch/
  capture符号值、完整activation/finally、原IR/SSA及共同增量全局收敛
  仍开放；统一变量里程碑/结构gate/阶段一Goal active。全suite/广语料/
  RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则242，多定义稳定值与独立读取替换阶段

- 共同稀疏flat正常值域加入typed、不可变的函数入口绑定身份；参数/
  self的capture、签名与mapped-arguments前提由源适配一次产生，原Alias
  复用该事实并移除重复参数列表扫描。共同定义/写入分析阻止不稳定入口
  grounding；分支、循环复制可以共同证明同一个值，无单定义准入。
  unknown、不同root、活跃undefined和无种子环不能创造已知值。
- 独立BindingValues只提交自有read_bindings；共同当前flow、表达式事实、
  名字分析与打印器消费读取投影，写入继续使用Storage。每个实际读取
  目的地经共同scope/name查询；first-commit先使聚合事实失效，再提交
  自有delta。DeadDefinitions/InitialValues/Bindings独立决定写入、初始化
  与声明删除，RHS效果、正常/异常完成没有因值相同而被自动删除。
- 顺序显式为Values→Storage→ReadValues→Grammar。稳定读取替换在存储/
  alias/快照目的地确定后运行，避免提前丢掉可用外层名字、阻断后续
  snapshot恢复。原shadow快照回归期望保持；进入ReadValues后禁止
  alias/存储/快照回退，完成值pass和有序清理后才允许Grammar。
  调度完成状态不是分析缓存有效性，没有新增整函数重试。
- 有效旧实现RED exit134（458）；早先fixture声明缺失导致的结构失败
  不计作功能证据。新9种源码语义覆盖分支/循环/effect/初始undefined、
  不同root/写参数/source-local/shadow/mapped反例；反向arena及
  0/1/200/201/400/800组合。32768独立copy-origin闭包、表达式值join、
  live/dead undefined、无种子环、exact/short预算及非法入口对照通过。
  聚焦3/3在25.14秒通过；最终32768独立值对照、调度反例及原共同名字/
  retirement/storage/subset对照均通过。
- 当前29目标构建168.64秒通过；最终59/59选定回归456.22秒通过，
  combined-coverage.json核对通过名称集合与原选择完全一致。存储
  665源码/96692行为比较/60错误变体；声明2568/513600/100、初始化
  2004/482752/368、循环276/13976/68；真实callback/rich-text/
  progressbar/EntryAbility回归通过。既有literal_plan.cc的kNoop
  switch警告保持，无本次新增编译警告。
- fresh固定pasteboard导出2.72秒，19records、10完整/0部分/9无module，
  10语法检查通过；源码字节等于241，language_numeric_constructors_v1
  与原预算保持。输入SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/stable-value-verification。
- grounding目前限定已证明不可变的formal/self入口；任意local SSA值、
  catch/capture、每次读取symbolic值、完整activation/finally、原IR/SSA
  与共同增量全局收敛仍开放。统一变量里程碑、结构gate、阶段一Goal
  active；全suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则241，按版本复查存储阶段的快照消费者

- 保留存储合并前的快照规范化；存储/alias及独立清理结束后，调度器
  比较Snapshots最后完成版本与当前projection版本，发生后续提交时
  在稳定边界用新事实复查一次。producer无变更不触发，消费者自己的
  提交不形成自循环；检查点记录处理进度，不是分析缓存有效性。
- first-commit聚合事实失效、RequireCurrent与自有提案提交契约保持。
  查询必须处于Storage、无活动pass、清理组idle且demand检查点current。
  清理组中途即使旧版本仍clean也拒绝。复查仍消费共同coverage、双向
  motion、exception、完成、名字及lifetime证明；后续独立结果/效果/
  demand清理完成后才进入Grammar，打印器不重新优化。
- Motion只在现有候选真正需要证明时按需构建一次；environment索引
  只为实际提案分配。原预算保持；新增边界查询O(1)，至多增加一个
  同成本消费者尝试，无逐变量整图重建、整函数重试或输入相关轮数。
- 实际正例：内层同名临时变量被存储共享消除后，条件快照恢复为直接
  一元表达式。source-local及真实干涉反例保留内层绑定和快照，避免
  TDZ/旧值错误。旧实现RED exit134（442）；反向arena及
  0/1/200/201/400/800块组合、独立源码执行和错误条件/更新/合并变体
  验证行为。首轮聚焦3/3在22.25秒通过；另补clean/dirty partial
  cleanup、活动pass、错误stage及旧事实版本反例。
- 最终当前29目标构建35.70秒通过；fresh固定pasteboard导出2.60秒，
  19records、10完整/0部分/9无module，10语法检查通过，源码字节等于240。
  language_numeric_constructors_v1、原预算及输入SHA256保持。
  最终59/59选定回归438.36秒通过，combined-coverage.json核对通过
  名称集合与原选择完全一致。存储557源码/81140行为比较/53错误变体；
  声明2568/513600/100、初始化2004/482752/368、循环276/13976/68。
  revision/cleanup/stage检查点反例及原共同名字、retirement、存储、
  真实callback/rich-text/progressbar/EntryAbility验证通过。SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/snapshot-checkpoint-verification。
- 本次关闭存储/alias规范化后的快照阶段依赖，未声称完整pass收敛。
  cleanup仍可能暴露更多机会；共同增量依赖/worklist、任意名字移动、
  完整activation/capture/finally及原IR/SSA继续开放。统一变量里程碑、
  结构gate及阶段一Goal active；全suite/广语料/RSS/benchmarks/DFX/
  代理暂停保持。

### 2026-10-09：规则240，存储组整批名字闭包与初始化子集契约

- Coalesce把完整interference-safe、direct fixed-root存储提案接入共同
  AnalyzeSourceNameRetirement。映射后的read/write/update两半及live
  undefined新增写入均用当前语义owner查询最终namespace。存储共享不
  删除RHS，因此这些查询全部无条件；与copy的条件读取职责明确区分。
- root/original完成与TDZ、词法lifetime及PlanStorageInitializations失败
  是独立保留需求，共同队列再传播跨组遮蔽。旧Coalesce的两批变更前
  ResolveNames查询、local blocked过滤及单独旧namespace初始化写入
  拒绝退出；独立值/效果/cell/干涉/初始化义务没有弱化。
- 代表选择先比较完整组的共同完成/lifetime可行性，再比较当前名字
  遮蔽偏好及style。另一组可能删除遮蔽绑定，因此旧名字偏好不能压过
  已证明的初始化可用性。仍先构造纯干涉组，结束后考虑所有原成员；
  不在union时过滤候选，也不添加图形状准入。
- PlanStorageInitializations在同一未修改flow/liveness/completion上运行
  一次，固定root下每个动作只依赖其原live undefined与root完成事实。
  保留某个成员不会使其他成员的动作失效。闭包完成后只提交获准成员
  的自有动作；first-commit聚合失效、阶段/cleanup顺序及原预算保持。
- 两组可写变量共同消除内层shadow正例在旧实现RED（exit134，419）；
  source-local及真实interference反例仍保留内层与外层必要存储。
  三种语义、反向arena、0/1/200/201/400/800块组合、源码独立执行及
  错合并/旧值/更新变体覆盖。聚焦3/3在20.80秒通过；新增512独立
  prerequisite代表排序、64初始化subset/throw/resume对照通过。
- 增量适配扫描B绑定、R引用、D定义；共同名字求解新增
  O((B+R+D) log(B+R+D+1)+N+H)工作及O(B+R+D+N+H)内存。
  无逐成员重建、pass重跑或200阈值；缓冲和子分析仍计入原预算。
- 当前29目标构建170.22秒通过；fresh固定pasteboard导出2.81秒保持
  19records、10完整/0部分/9无module、10语法检查，源码字节等于239。
  input哈希检查保持，language_numeric_constructors_v1与预算保持。
  最终59/59选定回归440.59秒通过，combined-coverage.json核对名称
  与原选择完全一致。存储521源码/75956行为比较/46错误变体；声明
  2568/513600/100、初始化2004/482752/368、循环276/13976/68；
  原3968名字闭包、12304当前namespace、7696 retention、9360存储
  名字对照、调度反例及真实callback/rich-text/progressbar/EntryAbility
  验证通过。SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/storage-retirement-verification。
- 完成固定root、不移动作用域或创造名字的跨storage-group绑定消除。
  任意renaming/relocation、子集拒绝后新root选择、完整activation/capture/
  finally、原IR/SSA及全局增量收敛仍开放；里程碑、结构gate及阶段一
  Goal active。全量suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则239，复制绑定整批退休的名字需求闭包

- Aliases把值/覆盖/cell/效果已证明的全部复制候选交给纯分析
  AnalyzeSourceNameRetirement。共同模型在所有候选暂时消失的namespace
  中解析名字，再按必须保留的绑定单调传播遮蔽与条件RHS需求；固定且
  retained的root是规范化提案契约，不是指令布局准入。
- SourceNameIndex记录inactive绑定的最近baseline retained父身份及
  同名同词法范围peer身份。按spelling/root/位置分区，successor删除只
  访问每个失效query一次；每个binding入队一次，未知范围或声明冲突
  按spelling传播typed精度缺口。无整函数重跑、逐变量CFG/祖先遍历。
- 旧Alias内部基于变更前namespace的拒绝及blocked-copy依赖图退出；
  生命周期失败作为独立需求输入。只有inert statement RHS可随目的
  copy消失；observable/embedded RHS仍是无条件读取。名称分析不删除
  计算/初始化，也不合并cell或推断TDZ；独立证明保持。
- 提案presence为自有delta，第一次BeginCommit先使ProjectionFacts失效，
  后续提交不读共同分析。现有Values→Storage→Grammar契约、revision、
  独立pass及DeadDefinitions→InitialValues→Bindings顺序保持。
- 新正例在旧实现RED（exit134）；当前3968独立词法祖先/retirement-mask/
  projection/条件copy闭包对照通过，包含无种子环、自由名字、声明冲突、
  反向身份、exact/short预算及非法root/ID。名字激活、unknown和coincident
  缺口与latent RHS边共同传播。200/201/400/800/1600工作量为
  15554/15634/31844/65222/133576，无200准入阈值。
- 新源码覆盖同名临时绑定一起消失、source local必须保留/TDZ、保留
  遮蔽导致外层copy继续存活、快照安全共享scratch。先行storage pass
  已证明的合法共享保留，不为测试预设结果阻止pass组合。布局反转、
  0/1/200/201/400/800块拆分、独立行为及错合并/错root变体覆盖。
- B绑定、N NameId、H shadow、Q query、D需求：
  O((B+Q) log(B+Q+1)+N+H+D)工作、O(B+Q+N+H+D)内存；排序、
  访问、缓冲、子分析全部计入原预算，不扩大资源/语言契约。
- 当前29目标构建85.55秒通过；固定pasteboard导出2.56秒保持19records、
  10完整/0部分/9无module及10语法检查，源码字节等于238。输入SHA256
  前后均为854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  最终59/59选定测试439.95秒通过，combined-coverage.json核对名称与
  既定选择完全一致。存储485源码/70772行为比较/40错误变体；声明
  2568/513600/100，初始化2004/482752/368，循环276/13976/68；
  调度/ownership反例、既有7696 retention、12304当前namespace和9360
  storage-name对照及真实callback、rich-text、progressbar、EntryAbility
  验证通过。证据在build/stage-one/name-retirement-verification；当前
  构建完成前误用旧binary的观察日志明确排除，不作为验收。
- 本次完成固定root契约内的copy-binding同时退休；storage-group同时
  退休/重命名/搬作用域仍需提案模型，完整activation/capture/finally、
  原IR/SSA对应及全局增量pass收敛仍开放。统一变量里程碑、结构gate及
  阶段一Goal active；全量suite/广语料/RSS/benchmarks/DFX/代理暂停保持。

### 2026-10-09：规则238，当前源码名字范围统一解析

- AnalyzeTreeSpellings一次发布binding与NameId的typed SourceSpellingId，
  相同UTF-16文本的不同NameId归同一身份；匹配留在验证的文本边界。
  当前共同图按retained original/projected词法范围排序/扫栈，发布不相交
  的名字activation区间和当前direct shadow。声明缩窄及native-for不再
  使用整个原始scope的遮蔽范围。
- Alias/storage/初始化新增写入及snapshot的symbol/free-name查询全部
  消费共同SourceNameIndex；原statement_scopes_数组与这些优化器的
  ResolveTreeReferences/Names路径退出。结构验证保持canonical模型。
  未弱化cell/初始值/完成/效果/干涉/lifetime等独立义务。
- EnclosingBindingScope显式表示self/module/capture/parameter外层环境，
  canonical父身份保持相同body-envelope中的层次；缺少local范围不能
  推断为全函数作用域。冲突、未知范围/身份/位置返回typed分析缺口；
  crossing范围是非法词法adapter provenance，不是CFG形状准入。
- 索引属于同一immutable ProjectionFacts revision；图重建/复用均清除
  旧索引，首次commit先失效，查询只产生自有结果。原阶段/依赖/cleanup
  契约保持，无额外pass轮次或样本/序列/同块/邻接匹配。
- 缩窄声明反例RED1.19秒；初版聚焦3/3在19.71秒通过。Native-for初始
  fixture未产生for声明，按现有循环携带值构造更正测试，未改生产matcher；
  外部可合并/内部必须保留组合及实际for-let语法检查通过。显式外层
  环境检查后最终聚焦3/3在20.77秒通过。
- 最终12304独立当前名字scope/projection/mask对照、7696既有retention
  与9360候选对照通过。覆盖外层父身份、缺失local/owner、half-open边界、
  冲突/crossing/非法IDs及旧index复用；反向ID/arena/事实、200/201/400/800
  chain/star、exact/short预算、源码独立执行和错误名字合并/更新mutation。
- B绑定、N NameId、H shadow、Q query：边界新增O(N log(B+1))文本比较；
  当前范围索引O(B log(B+1)+B+H+N)，最多2B段，查询O(Q log(B+1))。
  无per-variable祖先/CFG扫描；原预算计入缓冲、比较、访问。此证据不替代
  已暂停的完整过程RSS验证。
- inline检查发现缺范围过宽推断后，首轮构建仅终止自身8个进程，未导出/
  测试；该中断日志不作为验收。最终当前29目标构建65.77秒、既定59/59
  测试439.77秒通过，combined-coverage.json核对名称与原选择完全一致。
  存储437源码/63860行为比较/35错误变体；声明2568/513600/100，初始化
  2004/482752/368、循环276/13976/68；调度器反例及既定真实callback、
  rich-text、progressbar、EntryAbility对照均通过。
- fresh固定pasteboard导出2.59秒：19records保持10完整/0部分/9无module，
  10语法检查通过，全部源码字节等于237；原预算与language_numeric_constructors_v1
  保持。输入SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/projected-names-verification。
- 已知当前词法范围解析迁移完成；任意binding改写的完整namespace、storage
  提案的同时退役/重命名、原IR/SSA、完整activation/capture/finally及增量
  pass收敛仍开放。统一变量里程碑与阶段一Goal active，所有暂停范围保持。

### 2026-10-09：规则237，当前绑定保留事实贯通名字决策

- 新纯分析AnalyzeFlowBindingNames从当前共同图发布typed名字保留事实。
  参数、源码local/catch/self/capture/module及可观察temporary保留；所有
  当前定义/引用保留名字，包括初始undefined和不可达但仍输出的语法。
  仅已无这些事实的私有temporary退出名字activation。它不是值活跃性、
  TDZ或删除初始化的证明，独立cell/lifetime/完成/效果义务继续保留。
- Canonical shadow增加内层SymbolId；稀疏父先子后遍历跳过退役temporary，
  将仍保留的内层遮蔽重接到最近保留祖先。未知内层身份的legacy事实保守
  保留。storage选择及snapshot/storage/alias最终名字查询共享当前mask；
  原来激活全部原始symbol的判断退出这些决策路径。结构校验仍用原模型。
- 图重建/复用均重新产生mask/shadow；它们归ProjectionFacts同一revision。
  首次commit先失效所有分析，提交只消费自有ID；现有阶段和cleanup屏障
  保持，无额外pass轮次或样本/序列/同块/邻接/固定图形状条件。
- 新源码反例RED1.14秒：内层temporary声明已省略但旧名字仍阻挡合并。
  初版聚焦3/3在16.93秒通过；加强独立oracle后3/3在17.44秒通过。
  最终7696独立retention/父链对照、全部小型mask名字/自由名字祖先对照、
  9360既有候选对照，覆盖reads/undefined定义/observable/local/capture，
  反向IDs/事实/arena、retained-local错误合并与更新mutation、复用旧mask、
  非法provenance/enum/尺寸及200/201/400/800、exact/short预算。
- 名字retention/projection额外O(B+H+D+R)工作、O(B+H)空间，父链只遍历
  一次，无每变量祖先/CFG扫描；所有缓冲和工作计入原预算。这是共同
  分析与几何/预算验证，不能替代已暂停的完整过程RSS验证。
- 最终29目标构建169.73秒、既定59/59测试437.05秒通过；名称集合逐项
  等于原选择。存储源码389/56948行为比较/29错误变体；声明2568/513600/100，
  初始化2004/482752/368、循环276/13976/68；共同调度器反例及既定真实
  callback/rich-text/progressbar/EntryAbility对照通过。
- fresh固定pasteboard导出2.70秒：19records，10完整/0部分/9无module，
  10语法检查通过，全部源码字节等于236；language_numeric_constructors_v1
  及原预算保持。输入SHA256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  实际证据在build/stage-one/retired-shadow-verification/combined-coverage.json。
- 完整native-for重写名字范围、保留绑定scope搬移和storage提案中多个
  绑定同时退役的预测仍有精度缺口；canonical唯一性仅style偏好。
  原IR/SSA、完整activation/capture/finally、cell/value可行代表及增量
  pass依赖收敛仍开放。统一变量里程碑/阶段一Goal active；所有既定暂停保持。

### 2026-10-08：规则236，共同名字事实与实际引用可见性

- AnalyzeTreeSpellings复用同一spelling-group/scope-activation模型，
  一次发布最近同名外层绑定与唯一性偏好。语法adapter将遮蔽scope
  映射到共同SourceRegion；RecoveryFlow验证并发布，复用时清空。
  Coalesce自建TreeScopeCheck/重新统计唯一性的路径已退出。
- 全部干涉组构造结束后，按组/位置排序当前真实引用；每个候选
  遮蔽范围做一次二分查询。范围落在引用min/max之间却无实际
  引用时仍可选择该名字。唯一性继续只是style偏好，不能证明
  可见性；没有新样本/序列/同块/邻接/固定图形状准入。
- grouping仍只决定干涉；最终name、root/member完成支配、词法
  lifetime、cell、初始值及效果证明独立保留。原始名字事实不因
  计划revision改变，实际引用从fresh共同图读取；提案先验证，
  首次commit先失效事实，提交只使用自有ID。没有新增pass轮次。
- 新源码双重同名/内层观察反例RED在1.18秒失败；初版聚焦3/3
  16.75秒通过。共同唯一性迁移和加强后3/3在16.60秒通过：独立
  祖先名字oracle、9360小型暴力候选对照、无引用遮蔽空洞、反向
  IDs/affinities/arenas、200/201/400/800及exact/short预算；新源码
  执行及错误名字/更新mutation通过。
- 名字排序O(B log B)，scope发布O(Q+B)；组内引用索引额外
  O(R log R+H log R+B)，空间O(R+H+B)，H<=B。没有per-variable
  CFG/祖先/整函数扫描；无shadow时跳过引用索引。全部计入原预算。
  这是资源计数与几何证据，不能替代已暂停的过程RSS验证。
- 最终29目标build168.74秒、固定导出2.83秒成功：19records仍
  10完整/0部分/9无module，10语法通过，全部源码字节等于235；
  原预算和language_numeric_constructors_v1保持，输入SHA未改变。
  证据在build/stage-one/name-scope-verification。
- 最终既定59/59测试在439.23秒通过，combined-coverage.json确认
  名称与原选择完全一致。存储源码365/53492比较/26错误变体；
  声明2568/513600/100、初始化2004/482752/368、循环276/13976/68；
  9360独立名字候选、共同调度器反例、原model/ABC布局变体及
  既定真实callback/rich-text/progressbar/EntryAbility对照均通过。
  固定输入SHA256前后为854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
- 原始scope的遮蔽不等于重写后native-for名字可见性；声明省略/
  搬移后的完整名字模型仍有精度缺口，由最终name与projected
  lifetime校验保护。原IR/SSA、activation/capture/finally、完整
  cell/value可行代表及增量依赖收敛仍开放。统一变量里程碑和
  阶段一Goal active，既定暂停范围保持。

### 2026-10-08：规则235，原始与恢复后绑定共享词法范围

- 在已有唯一语法扫描中发布ScopeId到SourceRegionId索引，原始
  声明和恢复后声明共同消费BindingRegion/BindingInScope。
  存储代表选择不再把所有原始绑定当作无范围限制；初始化均覆盖
  所有引用时，内层唯一名字不能压过能覆盖块外返回的外层槽。
  原始绑定无索引时保留既有identity契约，未建模投影不能授权访问。
- 此范围只证明词法包含；名字解析、root/member完成支配、动态
  cell身份、初始值活跃性与效果仍独立证明。没有新增样本/序列/
  同块/邻接/固定图形状准入，也没有优化器各自重扫语法。
- 不增加pass轮次。原始范围是不可变syntax事实，恢复后范围从
  当前revision计划选取；首次commit先失效ProjectionFacts，禁止
  提交期间读分析。存储合并只消费fresh共同图，后续归一化、
  dead-definition/initial-value/binding清理及grammar阶段边界保持。
- 新源码反例RED在1.19秒失败；共同模型实现后聚焦3/3在16.40秒
  通过。随后加强original region非法ID/缺失occurrence、exact/short
  预算、独立块外引用反例与几何work断言，并纳入最终既定选择。
- 最终29目标构建169.08秒通过，固定导出2.59秒：19records仍
  10完整/0部分/9无module，10源码语法通过，全部源码字节等于234。
  language_numeric_constructors_v1与真实原预算保持，输入SHA前后
  一致；证据在build/stage-one/canonical-scope-verification。
- 最终既定59/59测试在435.01秒通过，combined-coverage.json核验
  通过名称与原选择完全一致。存储源码341/50036行为比较/23错误
  变体；声明源码2568/513600/100，初始化2004/482752/368，循环
  276/13976/68；既有模型、调度器反例、真实callback、rich-text及
  EntryAbility完整类对照通过。嵌套作用域、初始undefined、异常、
  TDZ、反向arena、200/201与几何成本及exact/short预算均保留。
  固定输入SHA256为854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
- 更完整名字/cell/value可行代表选择、原始IR/SSA、activation/
  capture/finally与global convergence仍未完成。统一变量里程碑和
  阶段一Goal继续active；full suite/扩展corpus/RSS/DFX/agents保持暂停。

### 2026-10-08：规则234，表达式 occurrence 运输与共同提案依赖

- 快照源移除与读值替换分别表示：ExpressionTransport只移除已证明
  discarded的源赋值求值，任意前缀/兄弟效果继续原位执行。旧的
  whole-expression-statement相等、必须属于discarded表达式语句及
  按源整语句收集名字引用的判断已退出快照路径。成功写入、唯一
  保留定义/读取、双向motion、异常身份和初始化/名字/lifetime证明
  继续由共同事实负责；没有新样本/同块/邻接/固定图形状准入。
- 先在当前不可变revision生成全部提案，再用同一个RecoveryFlow
  adapter构造一次只读试投影。共同图发布真实expression owner和
  typed运输依赖root，批量核验移动RHS的名字、绑定范围和环境。
  嵌套提案依赖外层假设作用域时共同拒绝；同一return中的独立
  提案仍可单独成功。临时的按目的语句拒绝分组已删除，不再作为
  精度限制。一次O(E+S)试投影缓冲/O(E)owner/root数组和批量引用
  查询计入原预算，没有per-candidate图重建或新增pass轮次。
- 首次commit先失效版本事实；提交只消费自有ID，禁止读取分析。
  只删除该binding拥有的声明/完成动作，保留嵌套快照所在的另一
  变量初始化。后续Storage请求fresh共同图；独立store/operand/
  statement归一化、dead-definition/initial-value/binding清理仍按
  契约执行。打印与名字遍历只消费同一计划，移动RHS范围属于目的
  occurrence，不能被源wrapper的省略范围覆盖。
- 新增return前缀/嵌套赋值、已观察赋值结果、双快照、独立与嵌套
  提案作用域、loop/resource/TDZ、另一binding原生初始化组合。
  reversed arenas和0/1/200/201/400/800保持。真实RED包括旧前缀
  8.45秒、嵌套return11.11秒、依赖错误13.60秒、初始化误删13.94秒、
  独立提案过度拒绝13.91秒。最终聚焦3/3在19.20秒通过：648源码、
  24,864行为比较、76错误变体；单/嵌套root及复用图清空均核验。
- 首轮29目标build169.80秒/固定导出2.71秒成功；选定59中48通过、
  11失败，313.85秒。更新测试的旧整语句转发/range契约后受影响9
  中8通过，126.98秒；剩余class-context经LLDB定位为200/201批量
  固定64MiB不足。保持生产每保留occurrence计payload成本/零预算
  拒绝；测试批量按count乘真实payload配置，加原transport slack。
  真实导出和常规child预算均未改动。最终当前重验见下项。
- 真实固定19records仍10完整/0部分/9无module，10源码语法通过。
  EntryAbility.updateBreakpoint第二阈值和PasteBoard回调长度判断各
  移除一个嵌套临时变量；两源码与233不同，其余8逐字节一致。
  既定EntryAbility测试复用独立完整class harness：1,520行为/8构造
  赋值/7错误变体通过；rich-text5records/4views/70cases/1,030faults/
  206swaps/18mutants及既定callback对照通过，不代表平台运行验收。
- 最终当前29目标build24.12秒成功；单独class-context1/1在1.58秒
  通过，完整选定59/59在420.89秒全部通过。声明2568源码/513600
  比较/100mutants，初始化2004/482752/368，干涉329/48308/21、
  循环276/13976/68，model32,768逐读/45,056全组及原schedule/graph
  oracle通过。测试范围/range断言更新保持全部独立语义/表示检查。
  这是既定相关集合，不是full suite、paired corpus或平台执行验收。
- 最终fresh导出1.34秒，build/stage-one/expression-transport-final-rule234，
  10源码语法通过，原预算及language_numeric_constructors_v1保持；
  输入SHA-256前后均
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  build/stage-one/expression-transport-verification保留初始RED、各轮
  聚焦/失败/最终日志、final-detail及combined-coverage.json，独立
  核对最终59个名称与既定selection完全一致。作者自审已完成，
  agents继续按用户指令暂停；没有新布局准入或commit期间读分析。
- 运输组件拒绝仍保守，direct creation/resource环境等现有能力缺口
  保留并记录。原IR/SSA、完整activation/capture/finally、全可行存储
  代表及全局pass收敛未完成。统一变量里程碑/阶段一Goal继续active；
  agents、新工作树、full suite、扩展corpus、RSS/DFX/benchmarks仍暂停。

### 2026-10-08：规则233，分组完成后选择全部候选与共享初始化可用性

- 存储合并先独立完成干涉组及引用extent，再一次扫描全部原始成员
  选择代表。旧union期间只比较两个赢家、丢弃其他候选的判断已删除。
  后续引用扩大范围时，早先不占优的候选仍可成为最终代表；不修改
  greedy干涉分组，不增加样本/同块/邻接/固定图形状准入。
- 代表偏好同时复用当前FlowInitializations的全completion支配事实。
  初始化子树和可达引用区间使用CompletionRegion，词法范围继续用
  SourceRegion；一次共同reference扫描分别聚合，不能混用坐标。
  优先选择词法与初始化均覆盖最终组的候选；能力相同时保留原
  名字/native/初始化rank/ID偏好。不可达语法不产生执行需求，但仍
  保留词法证明。原RHS的执行、效果、异常位置不由代表偏好更改。
- Storage通过RecoverySchedule请求当前版本活跃性/初始化，全部
  提案生成后提交，提交前统一失效事实；不在commit期间读分析。
  每映射的root/member初始化、名字、source containment、cell身份
  和独立初值保留证明继续负责合法性。不是将这些职责塞进代表选择。
  新增最终扫描O(B alpha(B))及O(B)数组，引用扫描仍O(B+R)，union
  区间join O(1)；没有per-binding图遍历，原预算保持。
- 红测先证明丢失候选：未改生产时count3失败，build3.09秒/test1.27秒。
  全组扫描后model在5.42秒通过，build7.08秒。实际native初始声明/
  复制/自增/写回红测保留两个slot，build4.02秒/test9.19秒；初始化
  model红测build6.97秒/test1.21秒。接入共同completion事实后聚焦
  3/3在17.14秒通过，build6.88秒；之后增强producer返回原输入及
  更新删除反例，纳入最终既定59项验证。
- 独立dense组membership/逐候选逐原始引用oracle覆盖45,056小图，
  不调用生产DSU/区间聚合。ID/affinity反排、缺位置、窄/宽scope、
  不同初始化完成范围及不可达引用都有覆盖；3/200/201/400/800及
  精确/不足预算保持。源码组合含原生初始化及效果producer，继续
  独立比较Number/BigInt/对象转换失败/producer抛异常及错误变体。
- 最终当前29目标build179.41秒成功；选定59项一次全部通过，427.64秒。
  model6.72秒，45,056全组代表/32,768逐读小图及原graph/schedule反例
  通过。干涉source16.93秒：329源码/48,308比较/21错误变体；声明
  2,568/513,600/100、初始化2,004/482,752/368、循环276/13,976/68
  均通过。没有修改旧源码语义/表示断言来接受回退。build出现未改动
  literal_plan.cc:59既有kNoop switch警告，本补丁未改动该文件。
  这仍是既定相关集合，不是full suite、paired corpus或平台执行验收。
- fresh固定导出2.67秒，位置
  build/stage-one/group-roots-rule233：19records、10完整/0部分/9无module，
  10源码语法通过，与规则232源码逐字节一致；原预算与显式语言
  数值/构造contract保持，输入SHA-256前后均
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  build/stage-one/group-roots-verification的final-*及combined-coverage.json
  保存最终证据；red/green/initialization-red/completion-red/green记录
  中间过程，不作最终通过依据。
- 全组词法/初始化偏好中的候选丢失已解决；跨名字遮蔽、动态cell、
  初值保留的完整可行代表搜索仍开放。一般原IR/SSA对应、activation/
  capture/finally、非primitive值传播及全局pass收敛仍开放。统一变量
  里程碑、结构gate和阶段一Goal未完成；全量suite、广语料、RSS/
  DFX/benchmarks、agents和新worktree继续暂停。

### 2026-10-08：规则232，归一化后声明恢复与共享组词法覆盖

- 唯一的Declarations从值归一化之前移到Constants及独立需求/初值/
  绑定清理之后、Aliases/Storage之前，旧早期调用退出，不增加重试轮。
  RecoverySchedule要求当前revision的完整清理检查点；声明提交统一
  失效分析并使检查点过期，随后独立清理完成才准许覆盖传播。无修改
  保留检查点。原全completion支配与词法extent证明继续负责初始化
  放置，原RHS效果保留原位置；没有新增样本/同块/邻接/opcode序列准入。
- 新顺序暴露存储代表偏好问题：native声明可能使循环内窄slot优先，
  然后因组内循环外引用而被共同生命期证明拒绝。现在一次扫描共同
  Flow真实occurrence生成每binding引用extent；DSU union合并extent和
  缺位置事实。两个保留候选中，优先能覆盖组实际引用的候选；覆盖
  能力相同时保持原名字/native/初始化rank偏好。不是group准入，所有
  映射仍通过独立初始化、名字、source containment与初值保留证明。
  新成本O(R)扫描/O(B)数组/每union O(1)，仍计入原预算，无per-slot图遍历。
- 试验把声明移到全部storage之后，丢失既有producer/初始化覆盖及
  源码表示质量，已经退回；当前声明恢复仍承载初始化completion。
  无条件偏好宽作用域也使真实pasteboard循环退回机械变量，已经删除。
  最终只比较组实际引用覆盖，保留已有循环表示。没有弱化循环存储
  合并断言来接受回退。原IR/完整cell生命周期分离仍需后续共同模型。
- 新声明正反例含早读undefined、未知producer后多次读取、初始效果、
  分支/循环携带/失败RHS与catch状态、SourceLocal与TDZ；native/store、
  arena反排、0/1/200/201/400/800及精确/不足预算保持。新增scope组星图
  在2/200/201/400/800反排ID，检查覆盖优先及两者覆盖时保留native窄slot。
  原32,768逐读小图及复制/选择/活跃性/版本反例仍通过。
- 循环独立runner原默认参数把显式undefined替换为initial，callback
  协议断言可能被被测函数catch吞掉。最终使用尾参数实际存在性保留
  undefined，正常参考执行额外要求setup事件；错误变体删除真实state
  producer，而非删除等价的惰性声明。这是验收入口修复，不是引擎样本匹配。
- RED：初始新增声明/model两项在41.83秒正确失败，build6.12秒；初次
  GREEN三项67.63秒通过，2,568源码/513,600比较/100错误变体。初轮
  29目标build173.82秒、59回归385.89秒得51通过/8旧保留断言失败；
  修订后8项196.34秒得5通过/3失败，定位循环代表偏好与runner变体。
  全storage后置试验59项274.79秒得51通过/8失败；宽scope试验在真实
  输出回退后终止自身进程，仅留中间证据。再次导出到已存在目录被
  ExportError::kDestination拒绝，改用fresh目录；未覆盖既有证据。
- 最终当前29目标build35.52秒成功；选定59项一次全部通过，419.68秒。
  声明源码2,568/513,600/100，初始化源码2,004/482,752/368，干涉源码
  305/44,852/19，循环源码276/13,976/68均通过。增加参考producer
  实际执行断言后的循环source注册CTest在15.31秒单独通过，证据见loop-final-*。
  这仍是既定相关集合，不是full suite、paired corpus或平台执行验收。
- fresh固定导出2.36秒，build/stage-one/declaration-order-coverage-rule232：
  19records、10完整/0部分/9无module，10源码语法通过，与规则231源码
  逐字节一致；language_numeric_constructors_v1及原预算保持。输入
  SHA-256前后均854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  build/stage-one/declaration-order-verification的final-*、combined-coverage.json
  保存最终证据；initial-final-*、expectations-*、storage-order-*、
  broad-scope-partial-*与termination记录中间过程，不作最终通过依据。
- 完整group内可行代表选择仍开放，目前比较两个保留候选；一般原IR/SSA
  对应、activation/capture/finally、非primitive值传播及全局pass收敛仍
  开放。统一变量里程碑、结构gate与阶段一Goal未完成。全量suite、
  广语料、RSS/DFX/benchmarks、agents及新worktree继续暂停。

### 2026-10-08：规则231，共同完成流上的逐读取原始值

- 共同正常值求解器新增实际读取与活跃incoming事实节点，区别绑定
  invariant和各次读取的primitive正常值。先冻结独立的全定义/入口
  invariant，再对未解析的已初始化private读取闭合表达式、成功定义、
  incoming和读取方程；较弱的保守不可达前驱unknown不能抹掉较强证明。
  两个分析域共用同一不可变projection及原预算，不是两个优化pass。
  所有throw/resume/cleanup边保留，入口/缺来源/未grounded循环、未知
  运算/update和可观察cell保持unknown；TDZ使用共同bootstrap证明。
- Constants先拥有全部binding/per-read提案，再一次提交使旧事实失效。
  写入、初值、绑定删除仍由独立owner基于新事实决定，之后才有alias/
  storage/grammar。共同表达式、流图和命名消费已提交的逐读取值，
  打印器在alias前消费primitive与原优先级/范围，无独立语义推断。
  原绑定单值限制在这些读取的决策路径退出；没有新增样本、opcode
  序列、同块、邻接、producer-list、selection-count或固定形状准入。
- 新顺序写入42/43红测在32.21秒失败，证明原限制确实保留机械存储。
  首轮联合context求解使既有循环正例被保守前驱unknown弱化；独立
  invariant先冻结后精化解决该精度回退，未裁剪边或增加布局限制。
  初次聚焦3/3在46.96秒通过：2,352源码/470,400比较/89错误变体，
  再增加已知predicate闭包与早读值/晚alias组合后参与最终59项。
- 最终29目标构建173.45秒成功；初轮既定59相关回归在240.13秒得到
  51通过/8失败。8项是旧存储保留断言及对应runner：初始undefined/
  已知零读取成为terminal后，独立cleanup、copy或snapshot可以删除
  原存储。逐项核对并保留独立执行，错误变体改测错误初值替换，而非
  依赖已删除的赋值文本。没有新增生产条件。中间失败完整保留；直接
  runner首次遗漏注册的--expose-internals，invalid-direct-source.log
  仅作调用诊断，不计入验证。后续还确认单次unknown读取可删除整slot，
  多次使用则仍保留原初始化；最后一项旧“缩短声明必错”反例在已知
  早读替换后成为等价变换，改用初始读取处提前执行producer的反例。
- 最终初轮声明源码2,400份/480,000独立比较/91错误变体通过；新增
  32,768有限集合小图独立对照通过，既有复制/选择/活跃性、pass版本
  与依赖反例保持。源测试含分支、循环、失败RHS、初始undefined、TDZ、
  SourceLocal/update、native/store、arena反排与0/1/200/201/400/800。
  每域与提案内存/工作均计费，无read×definition积；精确/不足预算
  和几何工作检查保留，复杂度维度见algorithms.md。不是RSS证明。
- fresh固定pasteboard导出3.69秒，build/stage-one/read-values-rule231：
  19records、10完整/0部分/9无module，10源码语法检查通过，与规则230
  源码逐字节一致，language_numeric_constructors_v1及原预算保持。
  输入前后SHA-256均为854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  final-record-result.json/final-result.json记录此实际证据；不是新增
  真实record覆盖、paired corpus或平台验收。
- 最终既定59项分批全部通过：初轮51，加snapshot2、storage/死定义3、
  interference源码1、初始化单元1及最后源码1；不是一次新的59/59。
  初始化单元123.86秒通过，最后测试目标构建7.33秒成功、只重跑受
  runner变体修改影响的初始化源码122.85秒通过：2,004份源码、
  482,752比较、356错误变体。死定义源码586/14,488/61，存储源码
  305/44,852/19；后者注册CTest在13.48秒通过，等同正确直接调用
  13.86秒的计数证据。生产逻辑自初轮29目标后未变，只更改旧断言/
  错误变体及补充接口注释，不额外重跑无变更的通过项。
  build/stage-one/read-value-verification/combined-coverage.json列逐项
  通过日志，initial-final-*保存初轮，retention*/storage-final*/
  last-binding*/verified-binding*保存修订过程；最终初始化源码见
  initializer-final-source-{test.log,detail.log,result.json}。bounded
  fixture诊断仅辅助区分初始化移动缺口，不代替完整独立执行验收。
- 多次使用的后续unknown值仍可能保留已拥有词法completion的原声明/
  赋值；单次读取能让snapshot删除整个slot不等于一般初始化移动。
  已记录共同初始化/生命期精度缺口，不扩展局部准入。原IR/SSA对应、
  完整activation/capture/finally、非primitive逐读identity及全局pass
  收敛仍开放；统一变量里程碑/结构gate/阶段一Goal未完成。全量suite、
  广语料、RSS/DFX/benchmarks和agents继续暂停。

### 2026-10-08：规则230，pass依赖契约与版本化清理调度

- RecoverySchedule在生产Initializers中统一拥有typed pass/fact契约、
  projection revision、analysis/commit阶段和values→storage→grammar
  顺序。六个缓存事实入口核对已声明依赖闭包与当前版本；provider
  仍按需构造。Flow/Expressions共同bootstrap明确为分析供应边界，
  不是两个相互调用的优化pass。提交前销毁整个ProjectionFacts，
  一批修改只增加一次revision，提交中禁止再次分析。
- 原constants_changed/value_cleanup_needed/cleanup_needed控制路径
  退出。共同调度器仅在DeadDefinitions→InitialValues→Bindings
  三个独立pass依次完成后记录清理检查点，调用方不能手工标记完成。
  producer提交自动使其过期，无修改pass保持；快照移动仅按既有
  “移除自身单一producer/read且保留RHS、不令其他producer新增死亡”
  义务搬运已有检查点，不能生成或修复过期检查点。values阶段alias
  producer覆盖、进入storage和grammar都检查版本；最终inert operand
  修改后也经过相同屏障。初始化正事实仍按原有义务单独搬运。
- 调度契约不以样本、opcode序列、同块、邻接或固定图形状准入。
  分析/传播、存储共享、删除写入/初始化、恢复声明和打印的职责保持。
  原有阶段内顺序仍显式；检查点只表示该版本完成有序清理，不能宣称
  任意pass重排或全局固定点。无新增per-read替换，先交付该精度扩展
  需要的调度前提；下一步仍为共同活跃incoming事实上的逐读取正常值。
- 新接口红测缺失recovery_schedule.h导致构建失败，明确是interface
  RED。首轮变异实验提前执行了旧binary，invalid-mutant-tests.log
  仅保留诊断，不计入证据。随后确认完整构建的错误变体移除了声明
  依赖限制，recovery_flow_test在1016反例断言以SIGABRT拒绝；生产
  变体已恢复。反例另覆盖stale revision、commit查询、嵌套pass、
  缺失/乱序清理owner、脏阶段进入、阶段倒退、快照检查点搬运和
  200/201/400/800重复。初次源码/model 3/3在38.49秒通过；新增
  完整清理组及阶段屏障后的最终结果下列补充。
- 当前29目标构建97.33秒成功，fresh固定pasteboard导出2.61秒：
  19records、10完整/0部分/9无module，10源码语法检查通过，源码
  与规则229逐字节一致。language_numeric_constructors_v1及原预算
  保持，输入SHA-256为854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  证据在build/stage-one/pass-contract-verification，导出在
  build/stage-one/pass-contract-rule230。fresh既定59/59相关回归在
  358.14秒全部通过；声明2,136源码/427,200独立比较/79错误变体，
  初始化2,004/482,752/346，死定义586/14,488/61。布局变化、循环、
  异常、闭包、optional/nullish、partial及actual records保持通过。
  补充“commit结束后stale revision仍拒绝”的反例后仅重建模型目标
  3.34秒、重新运行4.50秒通过，生产代码不变；stale-{build,tests}.log
  与stale-result.json保存追加证据。final-{build,tests,detail}.log及
  final-result.json/final-record-result.json保存最终构建、59项和导出证据。
  输入前后SHA-256相同；不是全量suite、广语料、平台或1G RSS验收。
- 原IR/SSA对应、完整activation/capture/finally、per-read精化及全局
  pass收敛仍开放。统一变量里程碑/结构gate/阶段一Goal未完成；
  全量suite、广语料、RSS/DFX/benchmarks和agents保持暂停。

### 2026-10-08：规则229，正常值依赖闭包与后代不可变准备共享

- 旧后序事实在producer变为常量后仍不能闭合consumer的条件正常结果。
  form81红测28.20秒失败：`current=false; scratch=current?seed():42`
  仍保留scratch。共同source adapter现一次发布typed primitive/read/
  forwarded-result/conditional/logical方程，稀疏单调worklist联合求解
  绑定与表达式。复制专用Kosaraju及“表达式必须先在后序中变为已知”
  的限制退出，没有增加整函数pass轮次或组合形状匹配。未知运算、
  update、未grounded来源及可观察/入口值仍为unknown。
- 基础方程跨pass不可变，当前方程view借用已提交的replacement，仅
  快照B个binding值。绑定读取直接用binding节点，终端primitive/
  unknown不分配可变节点，但全部操作数含义保持；payload借用到
  同步分析结束，发布值拥有副本。ExpressionFacts消费同一typed
  方程与共同truth/nullish语义，未追加AST。Constants只提交读取，
  dead-definition/initialization/binding删除独立；版本整体失效、
  提交中禁分析及Constants→清理→Aliases→存储后清理→最终语法
  顺序保持。仅值依赖收敛，不宣称全局pass固定点或per-read SSA。
- 初次聚焦3/3在71.37秒通过（2,136完整源码、427,200独立比较、
  79错误变体），实际record却在8383拒绝。内部定位RecordProgram
  resource，非语义失败。先移除每revision重复方程数组、压缩终端/
  binding节点与current projection复制；仍触及原预算。保留所有
  中间诊断/失败，未增加预算、输入count阈值或资源失败回退。
- 逐pass分配比较HEAD adapter（同一新solver复制兼容入口）证明
  record的pass提交/顺序不变。浪费来自类命名为后代准备后丢弃
  SourceVariables，成员打印再执行相同pass。现ClassNames持有
  已计费的后代不可变结果，按确切tree身份索引；成员打印同步借用
  这些计划及同一外部context的命名事实。借用不跨tree/context
  变化；原global/module/capture目标和结构检查保留。该供应路径
  的重复pass退出，static fields的独立准备仍是开销缺口。
- 最终实现前的6项聚焦45.09秒全通过，构建12.35秒；包含正常
  值/声明源码、直接及借用方程、类名/访问器源码与计划move/身份
  所有权。864独立truth/selection组合分别验证直接和借用表示，
  原32,768复制小图对照保留。新增unknown predicate、成功update
  反例、效果组合及0/1/200/201/400/800依赖链，两种初始化编码、
  arena反排、精确/不足byte/work预算保持。最后外部context身份
  核对随最终59项重建/验证，以下只记录实际最终结果。
- 最终29目标构建77.92秒成功；fresh 59/59相关回归
  344.83秒全部通过，构建+导出+测试425.30秒。最后
  context身份核对及864直接/借用方程、32,768复制小图再次通过；
  声明源码2,136份/427,200独立比较/79变体，初始化2,004份/
  482,752比较/346变体，死定义586份/14,488比较/61变体。既定
  循环/异常/闭包、partial、命名/类、可选链/空值及actual record
  回归全部保持通过。日志在build/stage-one/value-dependency-
  verification，final-{build,tests,detail}.log、final-result.json。
  本次中途29目标83.50秒成功、验证主动停止的日志保留为intermediate，
  不混入最终通过证据；所有临时诊断已从生产代码移除。
- fresh固定pasteboard导出到build/stage-one/value-dependency-final-
  rule229：19records、10完整/0部分/9无module，10源码语法通过，
  与规则228源码逐字节一致，contract为language_numeric_constructors_v1。
  最终导出2.55秒；输入SHA-256前后记录在final-result.json，样本身份
  保持854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  原预算保持。不是新增真实record覆盖、paired corpus/platform或
  全过程1G RSS证明。原IR/SSA对应、完整activation/capture/finally、
  per-read精化和全局pass依赖收敛仍开放；统一变量里程碑/结构gate/
  阶段一Goal未完成。全量suite/广语料/RSS/DFX/benchmarks仍暂停。

### 2026-10-08：规则228，原始值身份与表达式发生点分离

- 退休规则227的“已知undefined无原literal节点不能替换”表示缺口。
  BindingInitializers发布typed primitive值池及PrimitiveId，准备
  阶段一次接入输入字面量和词法初始化的隐式undefined，不追加或
  改写原AST。共同ExpressionFacts、FlowPrimitiveValue、读取计划
  与打印器共享值身份；ID不再借用ExprId或要求producer节点。
  数值kind/bits、UTF-16内容身份和源读取范围保持，打印器只消费
  值及原优先级规则。流图/类命名消费者同样消费已提交的值替换。
- Constants仍只提交读取提案，按语义值比较变化，不因选择另一
  个同值发生点增加revision。池跨pass保持不可变，revision-owned
  分析整体提交前失效；已有Constants→独立清理→Aliases依赖保持。
  初始化、成功写入、异常/效果、捕获cell及SourceLocal不被值事实
  替代。TDZ和未知/不同值写入保持原义务，没有新增样本、同块、
  邻接、操作序列或固定图形状准入。
- 旧实现form74在25.98秒失败：无literal输入仍输出let current及
  两次current读取。值池接通后3/3聚焦62.28秒通过，1,944份完整
  源码、388,800独立执行比较、71错误变体。新增隐式初值复制、
  sequence效果、nullish/conditional参数选择、自/互复制循环、
  早读TDZ及unknown branch write；native/store编码、arena反排、
  0/1/200/201/400/800及精确/不足byte/work预算通过。red/green
  的build/tests及green-detail.log在build/stage-one/primitive-pool-
  verification；不是全语料语义/style验收。
- 编译器暴露规则227测试中四处经临时variant引用迭代member范围的
  生命周期问题。测试改为先持有variant再遍历，消除dangling警告；
  fresh recovery_flow_test 4.95秒通过，32,768小图独立对照以及
  活跃初值/外部绑定/入口需求/未知更新反例重新通过。未更改生产
  方程证明或弱化断言，记录见record-build.log/lifetime-tests.log。
- 既定29目标构建81.67秒成功，初轮59相关回归55通过，耗时
  424.08秒。4项失败来自旧存储保留断言及其依赖的源码runner：
  自复制、嵌套shadow只读初值、赋值结果转发后的初始读取，旧断言
  要求保留copy0/声明。更新为明确的存储消除断言，并保留update、
  SourceLocal和未知活跃写入反例；生产代码未为这些测试增加分支。
- 重跑时先验证死定义单元18.22秒/源码16.48秒通过：586份完整
  源码、14,488独立行为比较、61错误变体。随后恢复测试最后一处
  自复制声明旧断言也退休；最终初始化单元103.81秒/源码106.91秒
  通过，2,004份完整源码、482,752独立比较、346错误变体，2项
  并行总106.92秒，fresh构建3.48秒。既定59项分批全部取得通过
  证据（初轮55+死定义2+最后2），不是一次新的59/59或全量suite。
  生产实现自初轮29目标构建后保持不变；只重跑受修改断言影响的
  测试。combined-coverage.json记录逐项覆盖，initial-final-*、
  expectation-*、second-final-*保存中间失败，最终final-{build,
  tests}.log、final-result.json、LastTest.log保存通过结果。
- fresh pasteboard导出至build/stage-one/primitive-pool-rule228，
  19 records，10完整/0部分/9无module，10源码语法通过，与规则227
  源码逐字节相同；原预算保持。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  导出2.59秒，record-{export.log,result.json,source.diff}保存在本次
  验证目录。无新增actual record coverage/platform证据。完整绑定
  生命期/capture/finally、原IR/SSA对应、per-read值精化及全局依赖
  收敛仍开放。全量suite、广语料、RSS/DFX/benchmarks继续暂停，
  阶段一Goal保持active。

### 2026-10-08：规则227，多定义值合流与按提交版本调度清理

- 新FlowConstants消费同一RecoveryFlow、活跃性和表达式正常结果，
  以primitive/binding/unknown方程表示成功定义。迭代Kosaraju处理
  稀疏复制依赖，合流所有保留写入的值；同值分支、自复制和有常量
  来源的互复制循环可以消除机械临时。不同值、未知/无来源复制、
  入口活跃值、可观察绑定保持未知。更新是未知的新数值计算，
  不能把++x的stored binding误作旧值的自复制。仅共同活跃性证明
  无读取的词法undefined初始定义可以排除；其他保留写入均参与，
  即使普通GetValue无需求，其正常结果仍可能被消费。
- Constants的单写入计数/唯一覆盖定义、本地复制队列以及选中写入
  删除/声明退役退出决策路径。它只提交拥有的读取替换提案；独立
  DeadDefinitions/InitialValues/Bindings决定写入、RHS效果、
  正常/异常完成和绑定保留。动态值Aliases的唯一覆盖义务保持。
  新分析随revision-owned ProjectionFacts整体在提交前失效；
  提交中禁止分析，后序consumer请求新事实，未新增手工reset。
- 明确Constants→清理→Aliases依赖。常量提交后先独立清理；
  后续别名/身份/效果有提交才重跑清理。RunPass版本变化产生
  change信息，已清理而未变化的版本不重复相同分析。最终循环/
  声明语法仍位于存储和值/绑定清理之后，没有新增pass种类、
  固定轮次或任意重排。不是全局依赖驱动收敛的完成声明。
- 旧实现form68同值分支仍保留current/scratch，聚焦失败21.79秒。
  初始新实现2/2源码/单元52.09秒通过；组合9项中8项通过、独立
  oracle发现自身把无来源依赖忽略，补齐每个来源的grounding后
  32,768小图对照4.76秒通过，未弱化生产算法。
- 首次固定导出在17函数family目标验证耗尽原预算。停止中间59项
  验证，定位并移除同版本重复需求清理；另将复制边容量从D收紧为
  实际C、依赖/component身份使用共同32位强ID。预算不变，无
  样本/大小/资源失败回退或丢弃语义。原实现/中间/最终的累计
  分配诊断保存在本次目录，所有临时诊断已移除生产代码。
- 干净实现3/3聚焦57.34秒通过：1,776份完整源码、355,200独立
  执行比较、64错误变体；32,768方程小图独立对照。涵盖同值/
  异值分支、零次/多次循环、自/互复制、前缀更新反例，保留原有
  异常、TDZ、catch活跃undefined、setter及复合表达式组合。
  两种初始化编码、arena反排、200/201/400/800及精确/不足byte
  与work预算通过。dependency-check-{build,tests,detail}.log
  位于build/stage-one/value-join-verification。
- 最终29目标构建成功，59/59相关回归629.99秒全部通过，构建+
  测试655.85秒。selection.json、final-result.json、final-{build,
  tests}.log及LastTest.log保存在本次验证目录。共同值小图和声明
  源码计数再次通过；既有statement-snapshot 540份/20,760行为/
  60变体、stored-result 528份/18,192行为/14变体通过。变量、
  循环、异常、命名、类、可选链/空值组合、partial与真实record
  回归保持通过。Finish原空白格式恢复后另保留post-format构建
  记录，无语义变更或额外重复行为测试。
- 固定pasteboard导出至build/stage-one/value-join-rule227：
  19 records，10完整/0部分/9无module，10源码语法通过，与规则226
  源码逐字节相同。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  导出3.10秒；record-{export.log,result.json,source.diff}在本次
  验证目录。无新增真实record覆盖；不是paired corpus/platform
  验收。完整生命期/capture/finally、原IR/SSA对应、per-read值精化、
  全局依赖收敛及无原literal节点的undefined表示仍开放。全量suite、
  广语料、RSS/DFX及benchmarks继续暂停，阶段一Goal保持active。

### 2026-10-08：规则226，共同正常结果事实恢复复合常量与复制

- ComputeExpressions在同一只读后序入口发布primitive literal正常
  结果，与已保存binding、必要求值及词法依赖分离。赋值/sequence
  转交值；条件按共同值合流或已知primitive选择；逻辑使用ToBoolean/
  nullish。同一选择也产生binding结果。调用/更新/算术coercion/暂停
  未推断primitive常量，没有执行边裁剪或新增语义模式。
- Constants的原RHS TreeLiteral/SymbolExpr形状分支退出，消费共同
  结果与read coverage；原稀疏复制队列贯通任意复合结果的常量依赖。
  整个RHS、成员setter、异常及词法完成独立保留，后序效果/绑定pass
  再清理。数值kind/bits保持，UTF-16同值表项一次建立共同身份，
  不按TextId/arena布局匹配；每次表达式join不再扫描长字符串。
- primitive结果随整个revision-owned ProjectionFacts在提交前失效。
  Constants提交私有条件true后，Aliases从新版本选择参数复制并消除
  临时；false条件保留所选调用的动态值。值事实没有被运输成效果或
  初始化证明；既有pass顺序保持，没有新pass、固定轮次或扫全图。
- 新form16表示要求在旧实现6.21秒失败，仍保留const scratch=
  (report(input),42)。迁移后2/2聚焦32.23秒通过；扩展成员setter与
  预算后9/9组合111.01秒通过。新增catch的中间2项有一项source
  mutant断言失败：无异常路径删除空let未改变该观察。改用显式
  提前写回及异常路径反例，未放宽生产证明或正常/异常执行要求。
- 最终扩展2/2聚焦45.02秒通过：1,632份完整源码、326,400独立
  执行比较、58错误变体。涵盖正负零/NaN/Infinity、同值/异值分支、
  不同TextId同字符串、短路/所选未知值、参数/member赋值、TDZ、
  catch活跃undefined及前序常量条件影响后序复制。两种初始化编码，
  symbol/expression/statement/text反排，0/1/200/201/400/800及
  同前缀长文本池，精确/不足byte与work预算均通过。
  {red,green,compose,exception,selection}-{build,tests}.log及
  selection-detail.log保存在build/stage-one/literal-result-verification。
- 最终29目标构建成功，59/59相关回归614.66秒全部通过，构建+
  测试691.76秒。selection.json、final-result.json、final-{build,
  tests}.log及LastTest.log保存在本次验证目录。声明源码1,632份/
  326,400行为/58错误变体再次通过；既有statement-snapshot 540份/
  20,760行为/60变体与stored-result 528份/18,192行为/14变体通过。
  相关变量/循环/异常/命名/类/表达式组合/partial及真实record回归
  保持通过；这不替代全语料语义/style或平台验收。
- 固定pasteboard导出至build/stage-one/literal-result-rule226，
  19 records，10完整/0部分/9无module，10源码语法通过，与规则225
  源码逐字节相同。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  导出2.36秒；record-{export.log,result.json,source.diff}保留在本次
  验证目录。没有新增真实record覆盖，非paired corpus/platform验收。
  完整生命期/capture/finally、原IR对应及全局pass收敛仍开放；全量
  suite、广语料、RSS/DFX及benchmarks继续暂停，阶段一Goal保持active。

### 2026-10-08：规则225，恢复声明范围归入共同流图

- RecoveryFlow持有SourceRegion、当前引用位置及Original/Projected/
  Unmodeled绑定范围。共同入口从验证过的源码所有权索引发布词法
  范围；活动声明从当前投影取得。区间表达祖先关系，不是字节码布局。
  Original表示没有额外收窄，名字解析仍需通过；缺失原生声明范围事实
  显式成为Unmodeled，不能授权访问。初始化完成、异常及动态cell
  身份保持独立义务，未把该查询宣称为完整绑定生命期证明。
- 删除RootLifetime，快照目的引用、存储引用替换、undefined完成
  重映射与别名传播四个消费者改读共同BindingInScope。消费者不再
  自行读取声明位置/plan/区间构造判断；当前引用owner在发布时统一
  映射到打印owner。LegacyConfined保留且未扩展，无新样本、同块、
  邻接、操作序列或固定图准入。
- 范围事实随revision-tagged ProjectionFacts在BeginCommit前统一
  失效，禁止提交中分析；重用flow容量时清空旧范围/位置。现有独立
  分析 -> 完整提案 -> 提交边界及pass依赖顺序保持，没有新pass或
  固定轮次扫描。准备/发布增加O(S+B)预算计费，单次范围查询O(1)。
- 首批7/7聚焦66.54秒；新增模型后11/11聚焦159.83秒通过。
  独立父链oracle核对24种五节点树、正反编号与200/201/400/800
  查询集合；模型覆盖范围内外、缺失事实、无效region、精确/不足
  字节与工作预算、复用容量清空。慢oracle只在测试中枚举成对关系。
  model-{build,tests}.log保留在build/stage-one/binding-scope-verification。
- 最终29目标构建成功，59/59相关回归590.89秒全部通过，构建+
  测试676.15秒。selection.json、final-result.json、final-{build,
  tests}.log及LastTest.log保存在本次验证目录；覆盖变量/循环/快照/
  干涉、名字/类、表达式组合、partial输出与既定真实record行为。
  声明源码696份/139,200行为/19错误变体，绑定初始化源码2,004份/
  482,752行为/342错误变体均通过。这是相关回归验收，非全语料结论。
- 固定pasteboard重新导出至build/stage-one/binding-scope-rule225，
  19 records，10完整/0部分/9无module，10源码语法通过，与规则224
  源码逐字节相同。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  导出2.39秒；record-{export.log,result.json}保留在本次验证目录。
  这不替代整个paired corpus或platform验收；阶段一Goal保持active。
  完整生命期/capture/finally、原IR对应及全局pass收敛仍开放；全量
  suite、广语料、RSS/DFX及benchmarks继续暂停。

### 2026-10-08：规则224，死初值、必要RHS与绑定完成分离

- 独立InitialValues消费当前RecoveryFlow/FlowDemand，裁剪私有死初值；
  DeclarationCompletion显式保留必要RHS求值与正常完成后的词法
  Initialize(undefined)，区分已初始化slot的AssignUndefined。
  RHS抛异常时不提前初始化/写回，TDZ、次数及原异常路由保持。
  Bindings、声明移动、存储合并和死slot写入各自消费这些事实。
- 声明候选/放置的原DeclarationStmt.initializer条件退出生产路径，
  改用共同FlowBinding初始化kind。已恢复声明在初值裁剪后仍拥有
  初始化身份；存储完成提案枚举全部显式位置，涵盖原placeholder
  之外的声明，使用共同活跃undefined及目的初始化证明。
- pass依赖明确为DeadDefinitions -> InitialValues -> Bindings，
  后序声明/快照/存储消费已提交投影；最终循环语法移到值/绑定清理
  之后，清除过期declarator suffix并保留必要prefix RHS。原循环
  语法后的重复清理组移除。每次BeginCommit统一失效ProjectionFacts，
  提交阶段禁止分析；初值裁剪仅运输未改变完成位置的正向初始化证明。
  未增加名字/样本/同块/邻接/操作序列/固定图准入或固定轮次扫全图。
- 新form24在旧实现red-tests.log的8.70秒复现死初值保留；聚焦5项
  54.65秒、循环顺序7项78.17秒、模型7项78.31秒均通过。
  ownership独立模型测试4.43秒通过。新增死初值调用/comma效果/
  自读TDZ，两个初始化编码与反向arena、0/1/200/201/400/800继续
  检查独立源码行为和错误变体；模型验证独立绑定/写回删除仍保留RHS，
  已恢复声明的初始化身份与前后完成关系，循环语法显式清除过期状态。
- 自审补充全部完成位置的存储提案与词法证明；首轮59项575.20秒
  57/59通过，两项同源失败是stored-result旧断言假设保留嵌套私有
  写入。裁剪死初值后，共同copy分析使该输入复制根可被传播；最终
  效果已无写入。改为要求零冗余赋值，并新增删除report的AST错误变体，
  独立执行证明效果仍保留。修正后7/7聚焦75.71秒通过。
  672份声明源码/134,400行为/18错误变体，528份stored-result源码/
  18,192行为/14错误变体均通过；未弱化原语义，也未新增生产样本分支。
  pre-storage、effect、recovery各诊断日志保留在同一验证目录。
- 初始化裁剪 -> 声明移动 -> 返回快照组合揭示跨pass遗漏：快照删除
  原placeholder时把前序pass已保留的独立RHS也省略。统一声明退役
  仅移除完成，保留DeclarationValue；值/复制/快照提案同时持有当前
  初始化身份，覆盖原声明之外的完成位置。新增form28独立对照、
  无冗余变量要求及调用删除反例；恢复旧省略行为的snapshot-red在
  11.55秒失败，输出仅一次seed调用。临时变体已恢复，生产无组合模式。
  更新中间版本因这处修复主动停止回归（pre-snapshot，50项已通过），
  不把中断验证记作完整通过。
- 最终快照组合聚焦9/9在149.55秒通过；696份声明源码/139,200独立
  行为/19错误变体，540份statement-snapshot源码/20,760行为/60错误
  变体，528份stored-result源码/18,192行为/14错误变体均通过。
  snapshot-{build,tests,detail}.log保留完整证据。
- 最终29目标构建成功，59/59相关回归588.18秒全部通过，构建+
  测试618.76秒；selection.json、final-result.json及final-{build,
  tests,detail}.log保存在build/stage-one/initial-value-verification。
  覆盖既定变量/干涉/循环/快照、命名/类、表达式组合、partial输出与
  真实record行为；上列中间失败与中断不替代这份最终验收。
- 固定pasteboard最终ABC导出至build/stage-one/initial-value-rule224-verified，
  19 records，10完整/0部分/9无module，10源码语法通过，与规则223
  逐字节相同。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  导出2.38秒；record-{export.log,result.json}保留。这不替代整个paired
  corpus/PasteBoard/platform验收；原阶段一Goal保持active。
  完整绑定生命期/finally/capture、原IR对应和全局pass收敛仍开放；
  全量suite、广语料、RSS/DFX及benchmarks继续暂停。

### 2026-10-08：规则223，死定义统一处理语句/表达式写入

- 共同FlowDefinition::WriteSite供reference发布及死写入提案使用；
  DefinitionWrite拥有typed ExprId/StmtId位置与原RHS。旧提交从
  canonical tree提取AssignmentExpr的路径移除，消费提案后统一
  失效当前分析；无新pass、扫描轮次或布局匹配。
- 存储合并后的原生声明投影为已初始化slot赋值，其死值写入现与
  普通表达式赋值共享demand/inert证明。必要RHS GetValue保持
  原位置、次数和异常路由；死UndefinedStore没有RHS可省略。
  词法初始化完成、update转换/写回及observable写入仍保留。
  独立Bindings随后消费已提交flow；活跃词法初始化值裁剪仍开放。
- 旧实现新增form22复现合并后的current第一次seed写回冗余，
  red-tests.log在8.08秒失败；新版C++/flow/死定义聚焦4项均通过。
  新源码错误变体测试暴露了seed返回同值及两个空对象deepEqual
  不区分身份的问题，修正为独立对象身份观察；不修改生产语义或
  降低对照。identity-recheck.log独立源码测试通过8.02秒，所有
  初始诊断保存在build/stage-one/statement-write-verification。
- 两种初始化编码、正反arena、0/1/200/201/400/800组合增加死共享
  写入和handler读取异常旧值；576份引擎源码/115,200独立行为/
  15错误变体。64种site/kind/demand/effect组合保护初始化与update
  边界；原小图独立demand/live oracle和typed预算继续覆盖。
- 最终29目标构建成功，59/59定向回归578.15秒全部通过，构建+
  测试712.41秒。selection.json、final-result.json及final-{build,
  tests,detail}.log保存于同一验证目录；包含既定变量/干涉/快照/
  循环、表达式组合、函数/类命名及真实record行为。
- 固定pasteboard ABC导出至build/stage-one/statement-write-rule223，
  19 records，10完整/0部分/9无module，10源码语法全部通过，
  源码与规则222导出逐字节相同。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e，
  record-{export.log,result.json}保留；这不代替整个paired corpus/
  PasteBoard/platform验收。阶段一Goal保持active。
  原IR对应、完整生命期/finally/capture及全局pass收敛未完成；
  全量suite、广语料、RSS/DFX及benchmarks保持暂停。

### 2026-10-08：规则222，初始化值需求与绑定保留分离

- 原生词法初始化region发布typed initializer owner，私有成功完成与
  RHS效果/值需求分开。DeadDefinitions只处理死值写入及非初始化
  求值；独立Bindings在它提交后读取当前flow和FlowDemand。原
  PlanBindingElisions消费待提交delta并联合提交的路径已移除。
- FlowDemand进入同一版本的ProjectionFacts；BeginCommit统一销毁
  全部分析，提交阶段禁止查询。提案持有自己的ID/值，后序声明/
  快照/存储消费者只分析已提交投影。既定最终cleanup也使用死定义
  ->绑定保留顺序，无新的固定轮次或语法准入匹配。
- 保留读取/写入/source/capture身份作为绑定根。保留绑定的无效果
  初始化仍需输出时，其owner到RHS读取绑定的依赖经一次稀疏闭包
  传播，避免删除provider留下悬空名字。未使用原生声明可删除，但
  必要RHS GetValue保持原位置、次数和异常路由。仍活跃绑定的死
  初始化值暂不单独裁剪，记录为精度缺口；原IR对应、完整生命期/
  finally/capture及全局pass收敛继续开放。
- 新增未使用effectful初始化、活跃绑定初始化依赖、覆盖写入、TDZ
  自读及循环丢弃写入，两种初始化表示、反向arena及0/1/200/201/
  400/800组合。528份源码/105,600独立执行比较/13错误变体通过。
  小图独立dense闭包覆盖65,536种图/根/效果/布局组合；链测试
  核对200/201几何增长与exact/short typed预算。
- red-tests.log在旧实现上复现form17原生初始化留下current，6.67秒
  失败。聚焦5/5通过48.44秒，证据为
  build/stage-one/unused-initializer-verification的oracle-{build,tests}.log。
  首轮聚焦旧“未做别名/常量则声明必须存在”断言失败，改为真正的
  活跃初始化/资源义务，独立行为对照不降低；诊断保留。
- 最终29个目标构建成功，59/59定向回归577.43秒全部通过，构建+
  测试711.52秒；selection.json、final-result.json和final-{build,
  tests,detail}.log持久证据位于同一验证目录。包含既定变量/干涉/
  快照/循环/声明/optional/nullish组合、命名/类及真实record行为。
- 固定pasteboard ABC导出至build/stage-one/unused-initializer-rule222：
  19 records，10份完整源码/0部分/9无module，10份语法全部通过，
  与规则221实际导出源码逐字节一致。输入SHA-256前后均为
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  record-{export.log,result.json}持久保存；不代替完整paired corpus
  或整个PasteBoard/platform验收。阶段一Goal保持active。
  全量suite、广语料、RSS/DFX及benchmarks保持暂停。

### 2026-10-08：规则221，原生词法初始化贯通变量恢复 pass

- 原DeclarationStmt初始化现在发布共同RHS/copy/StmtId写引用/当前owner/
  异常路由，covered value与快照不再假设所有定义都是AssignmentExpr。
  初始化完成与赋值PutValue保持不同语义；常量/别名、快照和存储分析
  复用同一coverage/liveness/completion/motion事实。
- 独立DeclarationValue源码投影保留原位置RHS GetValue，或把成功
  初始化写入已证明初始化的共享slot。合并不再直接省略含值声明；后序
  StoredResults独立删除identity写回，InertStatements独立删除无效果
  丢弃值。打印器/name preflight消费同一计划。所有提交仍统一失效
  ProjectionFacts，保持既定先后依赖，无新固定轮次或局部布局匹配。
- 共享存储同时证明原绑定及目的slot在当前引用处已初始化，包含TDZ
  读与写；const可值传播但不可mutable coalesce。源码local/const与
  capture身份保持。已初始化声明不会被语法恢复再次初始化。共享赋值
  仍消费RHS值；独立identity写入消除后才能成为discarded值，防止逗号
  清理误删被写入的常量。省略初始化origin显式标记boundary，转发值
  仍映射真实目的位置。
- 新declaration_value测试独立建立两种初始化表示，覆盖复制/常量/
  effectful RHS、单次与多次快照、组收缩、异常旧值、对象身份、名字
  遮蔽、TDZ读/写、分支及循环携带值。反向expression/statement/symbol
  arena与0/1/200/201/400/800节点组合，检查work增长及typed资源失败。
  408份引擎源码/81,600独立执行对照/7错误变体通过；源码两种表示均
  消除无需保留的机械变量，多次写入组使用同一slot。共同flow补测
  原生initializer/copy/context、投影写不重复初始化、TDZ和容量复用清空。
- 初始旧实现直接let current=42/copy无法传播，red-tests.log失败1.52秒。
  测试开发中修正非法const赋值fixture、arena增长后的scope ID、loop
  alternate必须为block及干涉字段拼写；保留诊断，无生产样本特例。
  expanded3 2/2通过12.04秒；原binding/interference/snapshot/flow单元
  亦通过。首轮59项522.58秒中55项通过，4项受旧“所有表达式span
  非空”断言阻挡；统一范围检查按省略计划验boundary并保留转发范围。
  stored-result成员写入mutation也改用AST节点选择，消除对被恢复掉的
  state名字依赖；原“观察到值就必须保留全部copy/初始化”断言改为
  保留必要绑定初始化义务并用独立执行核对值，不要求冗余存储。
  未降低独立行为对照，range系列诊断保留。
- 最终29个目标构建成功，59/59定向回归587.69秒全部通过；证据为
  build/stage-one/declaration-value-verification的selection.json、
  final-result.json及final-{build,tests,detail}.log。包含2,004份变量
  源码/482,752行为/342变体，408份新增/81,600行为/7变体，305份
  存储组合/44,852行为/19变体，540份快照/20,760行为/60变体，
  586份dead-definition/14,488执行/61变体，528份stored-result/
  18,192行为/12变体，以及Home/PageA独立整record语义与style验收。
  PasteBoard异步回调仍通过6,995场景/两变体，仅该回调验收。
- fresh ABC-only导出build/stage-one/declaration-value-rule221-final：19 records，
  10 complete / 0 partial / 9 noModule；10份语法通过，相对规则220源码
  逐字节不变，输入SHA256不变，export-check.json记录结果。
  原生声明dead-definition及声明kind精度、完整生命期/原IR对应/复杂
  finally/全局pass收敛仍开放；阶段一Goal active。暂停的完整corpus/
  RSS/基准/调度器保持，不把本次通用迁移当作新的record完成验收。

### 2026-10-08：规则220，完整干涉图与通用存储组收缩

- FindBindingInterference从共同live-in/成功定义发布完整temporary干涉边，
  包含没有copy affinity的端点；affinity的冲突标记来自同一图。否则
  相邻两对都安全仍可能错误合并必须同时保留的链首与链尾值。
- 独立PlanStorageCoalescing使用组间冲突邻接、union by size/path
  compression产生direct slot map。Coalesce移除paired bitmap及每个
  symbol仅进入一对的限制；代表选择与DSU平衡独立。共同spelling索引
  提供唯一性软偏好，再考虑native声明/当前初始化dominator位置；同名
  不拒绝。所有实际引用仍独立证明初始化、scope与RootLifetime，活跃
  undefined由独立初始化计划保留。调度和提交统一失效契约保持。
- 两级长链旧版只合并一对，red-tests.log失败1.47秒；通用算法可把
  1/2/200/201/400/800链恢复为一个slot，首尾冲突保留两个。反向symbol/
  statement arena、callbacks/coercion/throw、错误全收缩/删除update
  通过独立执行。已有shadow链最多保留一个copy，无新增样本/序列准入。
- 完整干涉通过既有175,616个两绑定三点图稠密固定点oracle；新20,480
  个四绑定图/偏好组合用独立稠密跨组扫描核对收缩，覆盖非affinity冲突、
  eligibility/native/逆序初始化及同名偏好。typed资源/work边界也验证。
  相关3/3通过21.79秒：305份源码/44,852行为/19错误变体。
- 首次构建遇到Prefer的const方法调用预算Step，已修正分析计费方法
  限定，initial-build.log保留诊断；无生成代码行为失败。最终28个既定
  目标构建通过，56/56定向回归558.90秒；包含2,004份变量源码/
  482,752行为/342变体、同一305份存储组合、540份快照/20,760行为/
  60变体，以及Home/PageA实际record的独立语义/style对照。
- fresh ABC-only导出build/stage-one/group-coalescing-rule220仍为
  19 records、10 complete / 0 partial / 9 noModule，10份语法通过，
  输入SHA256不变。PasteBoard.mjs移除$phi6及循环尾搬运赋值，其他
  九份完整源码逐字节一致。来源为共同干涉/存储/identity清理，不是样本修复。
- 新pasteboard_storage_callback_source_test先ABC-only生成，再读取
  独立paired TS。record8383的PasteButton异步回调通过6,995场景/
  两mutation，覆盖多record/types、常量getter、类型数组Proxy、length
  coercion、await拒绝及每个效果点throw；success flag一个slot，两个
  loop indices。注册后1/1通过3.61秒，即最终56+1项全部通过。
  仅该回调验收；wholeRecordBehavior/platformExecution=false。
- 证据目录build/stage-one/group-coalescing-verification，selection.json、
  final-result.json、final-{build,tests,detail}.log、callback-{build,tests,
  detail}.log及export-check.json记录完整范围。算法成本、稠密图输出、
  预算计费及来源见algorithms.md；不恢复暂停的RSS/全corpus/基准/调度器。
  原IR对应、完整绑定生命期、复杂finally与全局pass收敛仍开放。
  阶段一Goal active，字段/全局/签名/body和全paired整体验收未完成。

### 2026-10-08：规则219，存储合并后的复制传播消费当前owner

- 增加明确依赖阶段：Storage → identity/inert独立清理 → Aliases →
  identity/inert独立清理 → LoopDeclarations。多写经过共用存储并去掉
  同值写回后可成为单一不可变定义，后序复制传播消费当前coverage。
  使用既有RunPass/BeginCommit边界，提交统一失效整个ProjectionFacts；
  别名提交同时清除初始化传输证明。未增加全局不动点或形状准入条件。
- Aliases的词法解析与收窄声明生命期使用FlowReference当前owner；
  移除expression_scopes_及RootLifetime(SymbolId,ExprId)旧决策路径。
  名字遮蔽含TDZ仍由共同scope解析证明，源码/capture写入、活跃undefined、
  效果与初始化义务保持独立。早期直接值根/直接slot/后期直接值根按
  阶段契约O(1)组合，后期Aliases之后不再coalesce。
- 新往返复制正例恢复为report(input); return input；快照转发后进入
  input同名TDZ scope的反例保留current。覆盖正反arena和0/1/200/201/
  400/800块组合，无样本、同块、邻接、opcode序列生产分支。
  旧版本缺少末轮alias时form21失败4.16秒；相关5/5通过32.00秒。
- 首轮56项回归在长复制链form14反向布局中触发旧“任何alias都必须
  不存在”断言，停止该轮，证据interrupted-{tests,detail}.log。检查
  表明共同算法可保留另一个可见slot并传播部分复制；测试改为验证
  是否选中被遮蔽的原始值根，无生产特例。既有变量恢复2/2通过
  197.52秒：2,004份源码/482,752独立JS行为/342错误变体。
- 最终28个目标构建通过；上述2项加剩余54/54（335.17秒）覆盖同一
  56项既定选择。包含存储281份源码/42,260行为/17错误变体，快照
  540份/20,760行为/60变体，以及Home/PageA实际record独立对照。
  selection.json/final-result.json、alias-root-{tests,detail}.log与
  final-{build,tests,detail}.log记录完整选择、结果及拆分执行。
- 证据目录build/stage-one/late-alias-verification。fresh ABC-only导出
  build/stage-one/late-alias-rule219：19 records，10 complete / 0 partial /
  9 noModule；10份语法通过，源码相对规则218逐字节一致，输入SHA256
  不变，export-check.json记录结果。新增通用正例不代表新增record完成。
- 全局pass收敛、transitive coalescing、完整绑定生命期及original IR
  correspondence仍开放；阶段一Goal active。完整corpus/RSS/基准及
  任务调度器暂停保持；本交付不替代字段、全局、签名与body整体验收。

### 2026-10-08：规则218，this调用所有权贯通局部快照移动

- 共同LexicalDependency新增kCallableOwner，复用已验证ThisExpr的
  FunctionOwnerId；表达式后序统一组合，整环境依赖优先。同一准备
  只修改一个FunctionTree，局部块不改变调用所有权，子函数是独立树。
  this退出遇到任意局部绑定就要求整环境一致的限制。未新增准入形状。
- 这不证明this初始化：普通/derived/arrow现有效果规则保持，异常与
  双向motion仍必须成立；V1激活资源、闭包/类/隐式helper等缺口保持。
- 新增this.read在无关flag和同名input局部scope下的正例，验证独立
  receiver、null/undefined/primitive、getter异常与一次执行；重复读取
  和把this换成input的错误代码必须被发现。共同树原wrong-owner与
  arrow-method反例继续运行。旧版本form42失败8.95秒，red-tests.log。
- 相关3/3通过18.68秒，528份源码/20,220行为/58变体。追加真实derived
  constructor super前对照时，input-shadow错误变体自身也报ReferenceError，
  不能作为该观察范围的反例；收窄测试后528份/20,244行为/58变体通过，
  receiver-corrected.log。这是测试反例选择修正，没有生成代码行为失败。
  再补直接this值经已有comma简化后进入快照的组合正例。
- 直接this值组合的最终独立脚本通过：540份源码/20,760行为/60变体，
  value-final.log。最新版28个既定目标构建通过，56/56最终回归通过
  539.62秒，final-{build,tests,detail}.log；包含同一540份快照对照、
  2,004份变量源码/482,752行为/342变体、存储257份/38,804行为/
  15变体，以及Home/PageA实际record独立对照。
- 记录在build/stage-one/this-owner-verification。fresh ABC-only导出
  build/stage-one/this-owner-rule218：19 records，10 complete / 0 partial /
  9 noModule，10份语法通过，源码相对规则217逐字节一致，输入SHA256
  不变，export-check.json记录结果。新增通用正例不等于新增实际record
  完成覆盖；阶段一Goal active，完整corpus/RSS/基准及调度器暂停保持。

### 2026-10-08：规则217，显式全局名字贯通快照与目的解析

- RecoveryFlow.names新增typed名字读取事实：NameId、发生点、原始
  ExprId、当前owner。跟随替换/转发上下文，细化保持，复用容量清空。
  全局名字不再被当作不可分析的整环境依赖；闭包/类/隐式helper等缺口
  继续保留，不放宽V1可观察激活资源边界。
- ForwardSnapshots按当前producer owner收集目的名字查询；同一
  TreeScopeCheck索引与scope遍历算法同时支持SymbolId和NameId查询。
  真正的局部遮蔽（含TDZ）拒绝，无关flag不阻止；效果、异常路由、
  双向执行次数证明保持，无样本、同块、邻接或opcode序列新增条件。
- 新增无关绑定下!!Math和可观察全局getter组合的正例；原!!Math
  遇到同名局部遮蔽的反例保持。重复getter读取的错误源码必须被发现。
  各form覆盖0/1/200/201/400/800节点和反向statement arena。
  旧实现form40失败8.65秒，red-tests.log；相关3/3通过20.96秒，
  504份生成源码/19,212行为/50错误变体，focused-detail.log。
  共同流补测原/移动owner、细化保持及容量复用清空名字事实。
- 最终28个既定目标构建通过，56/56定向回归通过539.42秒；证据在
  build/stage-one/free-name-flow-verification/final-{build,tests,detail}.log。
  变量恢复2,004份源码/482,752行为/342变体，存储257份源码/
  38,804行为/15变体，快照504份源码/19,212行为/50变体，包含
  Home/PageA实际record独立对照。最终共同流测试包含名字归属与清空补测。
- fresh ABC-only导出build/stage-one/free-name-flow-rule217：19 records，
  10 complete / 0 partial / 9 noModule，10份语法通过，源码相对规则216
  逐字节一致，输入SHA256不变，export-check.json记录结果。本次新增
  通用恢复正例不等于新增实际record完成覆盖；阶段一Goal active，
  完整corpus/RSS/基准及调度器保持暂停。

### 2026-10-08：规则216，存储分析消费细化后图的初始化顺序

- 定位到同一投影内部的两阶段事实混用：引导初始化支配针对保守图，
  效果细化删除不可能异常边后仍被缓存。旧肯定结论安全，但否定结论
  可能漏掉已成立的关系；本次没有发现旧输出行为错误。
- 引导FlowInitializations改为局部结果，仅负责产生效果细化所需的
  ExprId正证明。CurrentInitializations按需构造最终图分析并进入共同
  投影缓存，存储合并不再消费引导图的否定结论。无新增图形状准入，
  原初始化传递契约、pass顺序与生命周期证明保持。
- 新增共同图精度测试：真实异常仍阻挡，不可能异常被细化删除后建立
  初始化支配，0/1/200/201/400/800节点均验证。已有初始化小图独立
  oracle及存储源码行为对照一起通过：相关3/3，详见
  build/stage-one/refined-initialization-verification/focused-{tests,detail}.log。
  这是精度契约验证，不声称新测试在旧planner下失败或新增真实record覆盖。
- 相关3/3通过13.47秒，其余53/53通过523.19秒，28个既定目标构建
  通过。共同图测试后续明确区分参数读取与可抛一元转换，最终版本直接
  执行通过，refinement-final.log包含512支配图/175,616定义使用图/
  98,304需求图的独立对照及200/201几何增长。回归包含2,004份源码/
  482,752行为/342变体，存储257份/38,804行为/15变体，快照480份/
  18,300行为/48变体，以及Home/PageA实际record对照。
- 新鲜ABC-only导出build/stage-one/refined-initialization-rule216：19 records，
  10 complete / 0 partial / 9 noModule，10份语法通过；除规则头外正文
  相对规则215不变，输入SHA256不变，export-check.json记录结果。
  阶段一Goal保持active，完整corpus/RSS/基准保持暂停。

### 2026-10-08：投影统一拥有分析缓存，规则215保持

- ExpressionFacts、RecoveryFlow、FlowLiveness、事件索引的初始化分析
  合并到带ProjectionRevision的私有ProjectionFacts。CurrentFacts统一
  检查分析阶段与版本；BeginCommit整体销毁，不再逐项维护reset列表。
  no-op保留缓存，流图容量复用保持。初始化正证明仍按原pass保留契约
  单独传递，没有扩大任何pass可保留的语义事实。
- CurrentInitializations统一负责按需构造；Coalesce退出自行创建并发布
  初始化分析缓存的职责。pass先后顺序、准入条件、打印及规则版本不变。
  这不等于任意pass可换序，尚未实现全局不动点调度或持久分析句柄。
- 独立build副本故意去掉facts_.reset，存储测试form0立即返回kOwnership，
  测试断言终止（exit -6）；正式生产源没有该故障。证据保存在
  build/stage-one/projection-facts-verification/stale-{build,tests}.log。
- 28个既定目标构建通过，56/56定向回归通过530.06秒，记录在上述
  verification目录的build.log、tests.log、detail.log；变量恢复2,004份
  源码/482,752行为/342变体，存储257份/38,804行为/15变体，快照
  480份/18,300行为/48变体，包含Home/PageA实际record独立对照。
  fresh ABC-only导出build/stage-one/projection-facts-rule215：19 records，
  10 complete / 0 partial / 9 noModule，10份语法通过，源码相对规则215
  上轮最终导出逐字节一致，输入SHA256不变。export-check.json记录结果。
  完整corpus/RSS/基准保持暂停，阶段一Goal继续。

### 2026-10-08：规则215，显式引用驱动跨环境快照证明

- 共同ExpressionFacts新增词法依赖摘要，随当前替换及子表达式组合，
  与效果/正常写回事实一起失效重算。显式引用可解析；全局名字、闭包、
  类和隐式环境依赖未建模时保留原环境一致性证明。这是AST语义属性，
  不是ISA操作白名单或样本形状条件。
- ForwardSnapshots先生成完整候选，再按当前flow.reference.owner收集
  目的作用域查询，用共同ResolveTreeReferences与RootLifetime验证每个
  保留读写。仅排除将被删除的快照PutValue；其他嵌套写入也必须证明。
  效果/异常/双向执行次数证明保持，提交前不修改计划。V1可观察资源
  合同的环境激活资源事件尚未建模，继续保留原环境边界；新增对应反例。
- whole-statement候选的引用归属互不重叠；候选索引与批量查询各线性
  扫描一次，不逐变量遍历表达式或父scope。完整capture/隐式名字依赖
  尚未建模，不能声称所有跨环境移动均已支持。
- 新增目的中无关flag绑定的正例与真正写入flag被遮蔽的反例；现有input
  读取遮蔽与投影后生命期反例继续保留。旧版本form36/count0失败8.14秒，
  build/stage-one/snapshot-scope-verification/red-tests.log。
- 首轮定向4/4通过25.87秒；快照456份源码/17,388行为/46变体。新增
  自增快照移入带current遮蔽的作用域后，必须拒绝后续current/scratch
  合并；计划与独立执行均验证。补测存储257份源码/38,804行为/15变体。
  独立脚本首次漏用注册的--expose-internals而未加载解析器，补齐后通过，
  不计为引擎行为失败。V1边界补齐前的52/52中间回归通过504.46秒；
  最新代码重新构建后56/56最终通过532.19秒，final-tests.log；
  快照480份源码/18,300行为/48变体，存储257份/38,804行为/15变体，
  变量恢复2,004份/482,752行为/342变体，包含Home/PageA实际record对照。
  最后补充的快照测试在CTest启动该目标前完成重编译，480份输出确认
  最终执行包含V1与全局组合反例。另增加全局名字经双重not组合
  后被目的局部绑定遮蔽的反例，验证共同摘要传递隐式环境依赖。
- 新鲜ABC-only导出 build/stage-one/snapshot-scope-rule215-final：19 records，
  10 complete / 0 partial / 9 noModule，10份语法通过，正文相对规则214
  不变，输入SHA256不变。记录均在上述snapshot-scope-verification目录，
  阶段一Goal继续，未恢复暂停的完整corpus/RSS/基准。

### 2026-10-08：快照转发与存储合并的组合验收

- 新增普通路径与try/catch路径：snapshot = ++scratch；随后在嵌套空
  作用域回写current。断言实际forwarded_values指向原自增表达式，
  storage合并成功，最终源码同时去掉snapshot/scratch机械变量。
  保持抛异常前的current值、转换顺序和独立返回值对照。
- 在生产者与目的之间插入0/1/200/201/400/800个空块并反向语句编号；
  检查几何增长。独立错误变体删除自增，必须发现行为差异。
- 本轮没有发现新的生产语义差异，生产代码及规则版本保持214。
  现有环境一致性证明仍限制带新绑定的跨环境快照移动；本测试不证明
  那部分已支持，也不替代完整绑定生命期模型。
- 最终定向2/2通过9.10秒，245份源码、37,076行为比较、14错误变体；
  记录在 build/stage-one/forwarded-storage-verification/final-{build,tests,detail}.log。
  本轮仅改测试和状态，不重复运行未受影响的56项生产回归或固定ABC导出。
  阶段一Goal保持active。

### 2026-10-08：规则214，当前引用归属贯通快照之后的存储分析

- RecoveryFlow.references 新增当前语句owner，由构造上下文统一传播；
  原表达式site仍作来源身份，替换/转发继承目的语句，二者不再混用。
  子表达式通过复制上下文调整continuation/value-use，保留异常与归属。
- RetainedExtents 和快照之后的 Coalesce 使用当前引用owner映射到既有
  源码语法的词法目的及作用域。后者退出按原始ExprId查位置的路径；
  正常初始化、干涉、名字解析和声明生命期证明均保留。两个只读索引
  随函数准备构建，流图引用归属随每次提交后的重建更新。
- 当前归属不等于完整绑定生命期；捕获单元、异常scope退出和暂停的
  精细语义仍未补齐。不添加同块/布局/样本条件，不修改打印器语义。
- 新增原引用与转发目的、旧图、重用存储、效果细化及条件/短路/暂停
  子引用归属检查。中断前旧构造器失败1.15秒，直接相关5/5通过28.95秒；
  原/tmp日志在环境中断后已不可用，未完成的51项不计通过。
- 恢复后确认原句柄及进程不存在，重新构建并运行全部56项相关测试，
  56/56通过498.46秒；新记录保存在
  build/stage-one/rule214-verification/{build,tests,detail}.log。
  变量恢复2,004份源码/482,752行为/342变体，存储干涉221份/33,620行为/
  12变体，快照432份/16,476行为/42变体；包括Home/PageA实际record对照。
- 恢复后的新鲜ABC-only导出
  build/stage-one/current-reference-owner-rule214-resumed：19 records，
  10 complete / 0 partial / 9 noModule，10份语法通过；正文相对规则213
  不变，输入SHA256不变，证据为上述验证目录的export-check.json。
  阶段一Goal继续，完整corpus、平台执行及暂停的RSS/基准不在本轮范围。

### 2026-10-08：规则213，以保留引用替代普通绑定的原始范围统计

- Declarations 与 Aliases 共用 RetainedExtents，逐次从当前 RecoveryFlow
  引用和已有词法目的计算范围；不让已删除读取/死写入继续阻断声明或
  别名根恢复。独立的正常初始化、支配、名字解析和效果证明保持。
- 当前两个消费者均排在快照移动之前；表达式身份仍对应其已解析的
  词法目的。分析只读，提交后按既有机制失效；不保存跨pass范围缓存。
  捕获绑定保留 LegacyConfined，未建模签名观察仍保留约束。
  这是保留引用范围迁移，不是完整异常退出/捕获单元生命期模型。
- 新增作用域外无效果读取、死赋值及真实初始undefined读取反例，覆盖
  正反编号和0/1/200/201/400/800。旧版本在form22/count1失败91.28秒，
  /tmp/abcd-rule213-red-tests.log；前序清理已删除读取但仍遗留外层声明。
- 定向2/2通过196.59秒，/tmp/abcd-rule213-focused-tests.log；
  2,004份源码、482,752次独立行为比较、342个错误变体。其余54/54项
  相关回归通过330.94秒，/tmp/abcd-rule213-regression-tests.log；
  最终共56项不同定向测试通过，包含Home/PageA实际record对照。
  新鲜ABC-only导出
  build/stage-one/retained-binding-extents-rule213：19 records，
  10 complete / 0 partial / 9 noModule；10份语法检查通过，正文相对
  规则212不变，输入SHA256不变，/tmp/abcd-rule213-export-check.json。
  不代表完整corpus或平台执行通过；阶段一Goal保持active。

### 2026-10-08：规则212，从当前定义选择声明位置并分离位置状态

- DeclarationCandidates 遍历当前 RecoveryFlow 定义，使用共同 discarded
  ownership、归一化表达式根及已有声明语法容器。按支配前序选候选，
  再核验全部保留读写；候选自身也是写入引用，故无需逐候选重走图。
  正常写回与词法范围证明保持，不使用原始首次赋值或节点编号选位置。
- DeclarationSite 在声明pass提交时发布，与原始BindingUses分离。
  快照环境、RootLifetime、存储合并、常量/别名的声明重置均使用实际
  位置。移除 initializer_container，legacy_initializer 只供尚未
  迁移的捕获检查使用。任意嵌套写入拆成新语句仍未支持，整语句及
  已支持for声明位置是表示能力边界，不是CFG布局或语言不变性。
- 新增死赋值前缀、副作用前缀、拆分块，以及外层无用写入消除后
  内层赋值成为当前语句根四类组合；覆盖正反编号、0/1/200/201/400/800。
  旧实现在form18/count1仍输出空声明加赋值，失败88.50秒，证据
  /tmp/abcd-rule212-red-tests.log。没有新增生产样本或序列匹配。
- 第一批6/6定向通过228.42秒，/tmp/abcd-rule212-focused-tests.log。
  后补嵌套语句根测试时该目标已完成编译，时间戳和1,956份输出确认
  此轮只覆盖前三类新增场景；不把它当作第四类的通过证据。打印器
  随后修改，快照与死定义四项也纳入修复后的重新验证。
- 首轮重新构建52项，51项通过、变量恢复独立执行失败，474.04秒，
  /tmp/abcd-rule212-final-tests.log。嵌套场景暴露普通声明打印仍读取
  原始外层赋值，生成重复写入并触发TDZ。改为消费计划assignment，
  普通/for声明共用已提交替换链的范围映射；不修改行为对照。
- 修复后变量恢复2/2通过188.02秒，
  /tmp/abcd-rule212-repair-tests.log；1,968份源码、477,568次行为比较、
  334个错误变体。其余54/54共享打印器相关回归通过330.07秒，
  /tmp/abcd-rule212-regression-tests.log。修复后共56项不同测试通过，
  包含循环、快照、死定义、名称、可选访问及Home/PageA实际record对照；
  未运行暂停的全套、扩展corpus或RSS测试。
- 新鲜ABC-only导出 build/stage-one/declaration-sites-printer-rule212：
  19 records，10 complete / 0 partial / 9 noModule，10份源码语法检查
  通过，正文相对规则211不变，输入SHA256不变；
  /tmp/abcd-rule212-repair-export-check.json。不是完整corpus或平台执行验收。
  完整生命期、捕获观察及全局固定点仍开放，阶段一目标保持active。

### 2026-10-08：规则211，当前完成顺序替代非逃逸绑定的首次观察匹配

- 非逃逸绑定的声明恢复消费当前 RecoveryFlow 的共同支配关系，要求
  候选写入正常完成支配所有保留读取/写入。正常 PutValue 事实来自
  ExpressionFacts.normal_stores，RHS、异常路径及提前写入均纳入；
  不以私有写入可删除性代替正常完成。移除 InitializerAvailable；
  首次语法观察检查只为尚未迁移的捕获单元保留，不扩展其接受域。
- 独立无效果观察及死定义清理先于 Declarations，避免无用旧值过早
  变成必须保留的词法初始化。后续值传播、存储、效果与最终声明种类
  pass 保持独立提交/统一失效。候选位置扫描及 Confined/RootLifetime
  的完整生命期迁移仍开放，不宣称一般声明选择或全局固定点完成。
- 新增普通丢弃读取、带前置调用的序列表达式，以及观察初始undefined
  的反例和错误提前初始化变体；正反编号及0/1/200/201/400/800验证。
  旧实现在新增场景失败65.72秒，/tmp/abcd-rule211-red-tests.log。
  首轮实现误用可删除写入事实，普通Local声明表示退化；改用正常
  写入事实后定向2/2通过159.97秒。中间证据
  /tmp/abcd-rule211-focused-tests.log、/tmp/abcd-rule211-focused2-tests.log。
- 随后54项相关回归52通过、快照unit/source两项失败304.11秒，暴露
  初始声明恢复先于死定义清理的顺序问题。修复顺序后快照2/2通过
  15.97秒，原行为oracle与快照预期不改。证据
  /tmp/abcd-rule211-regression-tests.log、/tmp/abcd-rule211-order-snapshot-tests.log。
  早期DCE还允许稳定来源的复制消除、无用初始常量和循环存储消除；
  测试分别核验死声明与别名，保留原独立参考与调用/异常轨迹。
  移除源码位置测试“转发的一定是第一次赋值”的旧假设。
- 下一轮56项中52通过、死定义/重复写入unit与source四项失败483.41秒，
  /tmp/abcd-rule211-final-tests.log。死定义用例仍期待新建初始化保护死
  赋值；修正该表示预期。重复写入归一化移到早期DCE之前，先消除
  已知值的冗余PutValue，避免两项pass选择不同的等价保留写入。
  修复后四项全部通过50.78秒，/tmp/abcd-rule211-order2-tests.log、
  /tmp/abcd-rule211-order2-detail.log。原独立行为参考保持不变。
- 最终相关目标构建成功，/tmp/abcd-rule211-final2-build.log；同一生产
  版本其余52/52项通过460.08秒，连同前述四项共56项当前通过证据。
  最终日志 /tmp/abcd-rule211-final2-tests.log、
  /tmp/abcd-rule211-final2-detail.log。变量恢复1,920份源码、470,656次
  独立行为比较、318错误变换检测；Home/PageA行为及表示验收通过。
  初次失败与中间结果保留，不以旧版本通过替代最终结果。git diff --check通过。
- 最终ABC-only导出 build/stage-one/declaration-normalization-rule211：
  19 records，10 complete / 0 partial / 9 noModule；十份源码语法通过，
  正文与规则210一致，输入SHA-256不变。证据
  /tmp/abcd-rule211-final2-export.log、/tmp/abcd-rule211-final2-export-check.json。
  通用组合恢复能力有提升，但该真实输入覆盖数量没有增加。
- 完整生命期、捕获观察、候选声明位置选择、全局固定点及阶段一独立
  验收仍开放；未恢复完整套件/RSS及其他既定暂停范围。上述受控
  独立执行不代表真实平台执行。

### 2026-10-08：规则210，初始声明恢复进入统一 pass 提交边界

- 将 Run 内直接修改声明/省略计划的循环迁入独立 Declarations pass。
  先收集拥有自身数据的 DeclarationEdit，再统一提交；有修改才推进
  projection revision，初始化、表达式、流与活跃性分析统一失效。
  顺序仍在值传播/存储合并之前。没有新增准入条件或固定迭代轮数。
- 原算法的捕获单元、首次观察及词法范围约束保持。此轮消除一个绕过
  协调器的修改入口，未声称原固定顺序存在已复现的行为错误；也没有
  将职责迁移当作 InitializerAvailable/RootLifetime 共同模型替代。
  方案数组及额外提交工作 O(bindings)，无逐变量图遍历。
- 相关目标构建成功，/tmp/abcd-rule210-build.log。声明、循环、共同流、
  捕获、Home/PageA 共12/12项定向测试通过176.29秒；证据
  /tmp/abcd-rule210-tests.log、/tmp/abcd-rule210-detail.log。
  变量恢复1,884份源码、465,472次独立行为比较、306错误变换检测，
  既有反序编号及200/201/几何增长检查通过。git diff --check通过。
  本轮没有重跑其余44项，不沿用规则209结果充作本轮通过证据。
- ABC-only 导出 build/stage-one/declaration-pass-rule210：19 records，
  10 complete / 0 partial / 9 noModule；十份源码语法通过，正文与规则209
  一致，输入SHA-256不变。证据 /tmp/abcd-rule210-export.log、
  /tmp/abcd-rule210-export-check.json。真实样本覆盖数量没有提升。
- 下一步仍是用共同完成/定义使用事实替代首次观察条件及完整生命期
  迁移；全局固定点与阶段一验收开放。完整套件/RSS及其他既定暂停
  范围保持，模拟宿主测试不代表真实平台执行。

### 2026-10-08：规则209，声明覆盖消费当前定义身份

- BindingReadCoverage 保存当前覆盖声明的 StmtId；Aliases 直接以此
  查询声明的词法所有者，不再与原始扫描 use.definition 重复匹配。
  移除 BindingUses::definition 与原始声明记录赋值，初始声明候选
  只登记已有可迁移空声明的绑定。生命周期与逃逸约束仍独立保留。
- 当前图/覆盖/写入事实保持同一分析版本，未新增 pass、固定轮次或
  同块准入。原定义扫描维持 O(definitions)，声明身份表为 O(bindings)，
  没有新增逐变量图遍历。Confined/RootLifetime 和初始声明定位仍待
  完整共同生命期迁移，不以本次删除字段声称架构里程碑完成。
- 最终相关目标构建成功，/tmp/abcd-rule209-build.log；56/56项相关
  回归全部通过435.02秒，/tmp/abcd-rule209-tests.log 与
  /tmp/abcd-rule209-detail.log。变量恢复1,884份生成函数、465,472次
  独立行为比较、306个错误变换检测，既有反序编号、200/201与几何
  增长验证保持。Home/PageA行为及表示验收通过；git diff --check通过。
  本次复用已有语义测试，不将内部职责迁移描述为新增样本恢复能力。
  第一轮定向结果早于原始字段删除，仅作为中间证据。
- ABC-only 导出 build/stage-one/declaration-coverage-rule209：19 records，
  10 complete / 0 partial / 9 noModule，十份源码语法通过，正文与规则208
  一致，输入SHA-256不变。证据 /tmp/abcd-rule209-export.log 与
  /tmp/abcd-rule209-export-check.json。全阶段独立验收、完整生命周期
  与全局固定点仍开放；完整套件/RSS及既定暂停范围未恢复，模拟宿主
  对照不代表真实平台执行。

### 2026-10-08：规则208，独立最终声明种类恢复

- 初始声明定位先保留 let；DeclarationKinds 在存储、循环声明、
  死定义与表达式清理后，以当前初始化定义/写入引用及实际存储根
  决定私有非逃逸临时量的 const。只改变声明种类，不删除初始化、
  求值或改变作用域；提交统一失效分析，最后再发布表达式摘要。
- 移除 BindingUses::writes 和 Observe 的原始读写标志/计数。源码
  let 与未知捕获写入不作 const 推断；原生 for 共享 let 关键字的
  声明组由既有 LoopDeclarations 处理，未把语法组限制当语言不变性。
  新分析按引用/定义/声明各线性扫描，额外存储为 O(bindings+statements)。
- 已有 self-copy 与带副作用 self-copy 场景增加声明种类的独立断言，
  旧实现失败48.58秒，/tmp/abcd-rule208-red-tests.log。反序编号和
  0/1/200/201/400/800、工作增长及原始行为oracle保留。定向2/2通过
  141.00秒；证据 /tmp/abcd-rule208-focused-tests.log、
  /tmp/abcd-rule208-focused-detail.log。
- 相关目标构建成功，/tmp/abcd-rule208-regression-build.log；其余54/54项
  回归全部通过295.87秒。连同定向两项共56项通过，Home/PageA的独立
  行为及表示验收保持通过。最终证据 /tmp/abcd-rule208-regression-tests.log、
  /tmp/abcd-rule208-regression-detail.log。变量恢复1,884份函数、465,472个
  行为情景、306个错误变换通过；git diff --check通过。
- ABC-only 导出 build/stage-one/declaration-kinds-rule208：19 records，
  10 complete / 0 partial / 9 noModule；十份源码语法检查通过，正文
  与规则207一致，输入 SHA-256 不变。证据 /tmp/abcd-rule208-export.log、
  /tmp/abcd-rule208-export-check.json。不计作实际语料覆盖数量提升。
- 声明位置/完整生命期、全局固定点及阶段一仍开放；保持既定暂停
  范围。上述独立执行属于模拟宿主，不代表真实平台验收。

### 2026-10-08：规则207，别名传播选择当前定义并移除旧 store 字段

- Aliases 复用当前 CoveredWrite，不再从原始第一次赋值寻找 RHS。
  删除 BindingUses::store、旧 stores 覆盖映射、Scan 的首次 store
  记录及 Elide。当前定义/写入计数/覆盖保持同一不可变投影，提交后
  沿用统一失效。Observable PutValue 保留为来源根，不删除其完成。
- CoveredWrite 同时记录共享 discarded ownership 与归一化表达式根
  对应的整句归属。该信息只决定整句删除范围；嵌套赋值可保留 RHS
  消除写入，不新增同块或固定图形状准入。副作用 RHS 保留实际发生点，
  名字可见性及原生声明生命周期仍分别证明；不宣称完整作用域模型完成。
- 新增根/复制前置 self-copy、含 report 副作用的复制链，以及目标
  声明在 return 之后的 TDZ 反例。正反序编号与0/1/200/201/400/800
  及工作增长覆盖保持。旧实现失败50.82秒，/tmp/abcd-rule207-red-tests.log。
  定向2/2通过137.84秒，1,884份函数、465,472行为情景、306错误变换。
  ReferenceError 使用独立类型标记对照，未将它和注入的用户异常混同。
  证据 /tmp/abcd-rule207-focused-tests.log、/tmp/abcd-rule207-focused-detail.log。
- 相关目标构建成功，/tmp/abcd-rule207-regression-build.log；首轮54项
  53通过、1个statement_snapshot表示断言失败，293.96秒。当前定义恢复
  使 saved=(flag=value) 可以别名为flag，旧测试只统计快照内联。现分别
  核验目标SymbolId和别名/内联表示，原独立执行oracle不变。补测2/2
  通过13.68秒，/tmp/abcd-rule207-snapshot-build.log、
  /tmp/abcd-rule207-snapshot-tests.log、/tmp/abcd-rule207-snapshot-detail.log。
  连同定向两项，56个不同相关测试均有当前通过证据；git diff --check通过。
  首轮证据 /tmp/abcd-rule207-regression-tests.log、
  /tmp/abcd-rule207-regression-detail.log 保留，不当作整批通过收据。
- ABC-only 导出 build/stage-one/aliases-current-definitions-rule207：
  19 records，10 complete / 0 partial / 9 noModule；十份源码语法
  检查通过，正文与规则206一致，输入 SHA-256 不变。证据
  /tmp/abcd-rule207-export.log 与 /tmp/abcd-rule207-export-check.json。
- 原始声明/activation、全局固定点及阶段一仍开放；保持既定暂停
  范围。上述模拟宿主验证不代表真实平台执行或全语料验收。

### 2026-10-08：规则206，常量传播选择当前覆盖定义

- 共同 BindingReadCoverage 保存当前 CoveredWrite 的表达式身份、
  定义种类与写入效果；Constants 退出原始第一个 use.store 的筛选及
  producer 定位。旧 store 映射仍供 Aliases 使用，迁移边界明确保留。
- 清理无效旧写入/无效果观察先于常量覆盖分析。常量替换将当前写入
  投影成原 RHS，再由效果 pass 删除无效果语句，退出旧 Elide 整句
  删除路径。普通 PutValue 的删除独立消费 FlowWriteEffect::kInert；
  词法初始化是独立 definition kind，不新增声明顺序或同块准入。
- 新增前置 self-copy 的等价组合，以及声明在 return 后的 TDZ 反例，
  覆盖正反序编号、0/1/200/201/400/800 与工作增长。旧实现前置组合
  失败56.68秒，/tmp/abcd-rule206-red-tests.log。前置 inert 清理也允许
  旧 expression7 的复制链恢复，因此更新表示预期，保留独立执行 oracle。
  首轮旧预期失败证据 /tmp/abcd-rule206-focused-tests.log。
- 最终定向2/2通过，133.05秒；1,836份生成函数、464,128行为情景、
  298错误变换，TDZ对照分别要求抛 ReferenceError。证据
  /tmp/abcd-rule206-focused2-tests.log、/tmp/abcd-rule206-focused-detail.log。
  其余54/54项相关回归全部通过，297.57秒；连同定向两项共56项通过。
  Home、PageA整record行为与表示验收保持通过。构建与最终回归证据
  /tmp/abcd-rule206-regression-build.log、/tmp/abcd-rule206-regression-tests.log、
  /tmp/abcd-rule206-regression-detail.log。git diff --check通过。
- ABC-only 导出 build/stage-one/constants-current-definitions-rule206：
  19 records，10 complete / 0 partial / 9 noModule；十份源码语法
  检查通过，正文与规则205一致，输入 SHA-256 不变。证据
  /tmp/abcd-rule206-export.log 与 /tmp/abcd-rule206-export-check.json。
- Alias producer/声明生命期/全局固定点及阶段一仍开放；保持既定
  暂停范围。上述模拟宿主行为验证不代表真实平台执行或全语料验收。

### 2026-10-08：规则205，常量传播消费当前写入事实

- Constants 复用读取覆盖分析的当前写入计数，不再按原始
  BindingUses::writes 过滤候选或证明唯一写入。原始计数仅余初始
  const/let 选择；代码注明其不变性义务及未来 SourceVariables 的
  definition/activation 替代分析，不能再扩展到值传播决策。
- 冗余同值存储清理先于常量传播，保留常量投影后的清理及别名依赖。
  沿用统一 BeginCommit 失效与只读分析/批量提交，没有新增自赋值、
  opcode 序列、同块或样本准入；原始候选 store 定位仍是待迁移项。
- 新增普通自赋值、带 report 效果自赋值及提前读取初始 undefined 的
  三类组合，覆盖正反序编号、0/1/200/201/400/800 与工作增长。
  原始实现失败，55.50秒，/tmp/abcd-rule205-red-tests.log。
  修复后相关56/56项全部通过，421.74秒。变量恢复1,812份生成函数、
  463,456个独立行为情景、294个错误变换；Home 1,086次配对执行及
  362个表示反例情景、PageA 1,422次执行通过。最终证据
  /tmp/abcd-rule205-tests.log、/tmp/abcd-rule205-test-detail.log。
  git diff --check 通过。
- 所有相关目标构建成功，/tmp/abcd-rule205-build.log。
  ABC-only 导出 build/stage-one/constants-current-writes-rule205：
  19 records，10 complete / 0 partial / 9 noModule；十份源码语法
  检查通过，正文与规则204一致，输入 SHA-256 不变。证据
  /tmp/abcd-rule205-export.log 与 /tmp/abcd-rule205-export-check.json。
  不将共同算法等价变换覆盖计为真实语料 complete 数量提升。
- 全局固定点、候选定义定位、完整绑定生命期及阶段一仍开放；
  保持既定暂停范围，模拟宿主验证不代表真实平台执行。

### 2026-10-08：规则204，别名稳定性消费当前写入事实

- Aliases 的零次/唯一写入判断改用当前 RecoveryFlow 引用计数，删除
  原始 BindingUses::writes 的候选过滤与稳定性判断。计数统一随读取
  覆盖分析取得，O(bindings + references) 工作、O(bindings) 额外存储。
  原始声明与首个候选 store 定位仍保留，未声称全 def-use 迁移完成。
- 调度明确 Constants → StoredResults → Aliases → StoredResults：
  前一次消除不改变值的写入，使复制传播看到当前稳定来源；后一次处理
  别名替换新产生的同值写入。沿用 BeginCommit 统一失效与只读分析、
  提案后提交，不让别名 pass 顺带猜测或删除 RHS 效果，不加新形状准入。
- 新增普通 self-copy 与包含 report 副作用的 self-copy，覆盖正反序
  expression/statement/symbol/scope 编号、0/1/200/201/400/800 和工作增长。
  旧实现新测试失败，/tmp/abcd-rule204-red-tests.log。已有初始 undefined
  复制链现在能共享首个声明，因此将“部分共享”和“整个临时根消除”分开
  验证；独立源码检查明确保留 copy0 的活跃初始化。
- 构建成功，定向单元通过60.67秒；独立行为最终通过64.79秒，生成函数
  1,776、行为情景462,448、错误变换290。反例定位首次漏掉 value-call
  callee，已复用 expression_edits 的 AST 定位，没有改生产条件。
  最终行为证据 /tmp/abcd-rule204-behavior-tests.log、
  /tmp/abcd-rule204-behavior-detail.log；前置失败证据保留在
  /tmp/abcd-rule204-focused-tests.log 与 /tmp/abcd-rule204-focused2-tests.log。
- 其余相关54/54回归全部通过，292.62秒；连同定向两项共56项通过。
  Home 1,086次配对行为/362个表示反例情景及PageA 1,422次执行通过。
  构建与回归证据 /tmp/abcd-rule204-regression-build.log、
  /tmp/abcd-rule204-regression-tests.log、/tmp/abcd-rule204-regression-detail.log。
  git diff --check 通过。
- ABC-only 导出 build/stage-one/alias-current-writes-rule204：19 records，
  10 complete / 0 partial / 9 noModule；十份源码语法检查通过，正文与
  规则203一致，输入 SHA-256 不变。证据 /tmp/abcd-rule204-export.log
  与 /tmp/abcd-rule204-export-check.json。不计作实际语料覆盖率提升。
- 全局固定点、完整声明生命期、统一变量剩余迁移与阶段一仍开放；
  不恢复全量语料/RSS/DFX，以上执行验证不是平台验收。

### 2026-10-08：Home 整 record 行为与表示独立验收

- 将 PageA 的 AST 规范化、模块接口、原生表达式统计和构造参数变体工具
  提取为 tests/integration/record_style.mjs；新增 Home 表示验收，核对
  imports、类布局、继承、完整签名、普通方法和原生回调/Promise 调用。
  复杂方法的调用/构造/属性写入统计仅为表示约束，不能代替行为或顺序证明。
- Home 原始输出和两种等价表示（构造参数改名、原生 class 声明）分别
  运行完整 362 情景，共 1,086 次配对行为执行。额外插入不可达循环，
  362 情景行为保持等价，但表示检查拒绝；原有权限语义错误反例继续拒绝。
  配对源码只进入测试，生产引擎仍仅接收 ABC，没有样本准入条件。
- 初次运行缺失 Node --expose-internals，补齐 CTest 参数；随后独立行为
  对照发现测试重命名工具误改对象属性 params，已区分属性键与绑定引用。
  首次日志 /tmp/abcd-home-style-tests.log 和
  /tmp/abcd-home-style-final-tests.log 保留为失败证据。
- CMake 配置成功，最终 Home/PageA 2/2 通过，3.59 秒：
  /tmp/abcd-home-style-verified-tests.log、
  /tmp/abcd-home-style-test-detail.log。PageA 保持 1,422 次配对执行、
  两种等价表示、三个语义及两个表示反例。git diff --check 通过。
- 本次仅测试与注册调整，生产规则仍为203；上述收据为模拟宿主下的
  有界独立行为/表示验收，不是平台执行或全部语料验收。统一变量恢复、
  全局固定点和完整绑定生命期仍开放，结构 gate 与既定暂停范围不变。

### 2026-10-08：规则203，共享表达式根身份与显式 RHS 来源运输

- ExpressionFacts 在既有 postorder 计算当前 replacement 链的最终 ExprId；
  快照的整语句归属比较消费此根身份，不再比较原始语法根。这里表示实际
  保留 occurrence 的身份，不把相同值当成同一次求值；没有增加 comma
  或操作序列识别。根表 O(E) 工作/存储，避免逐候选重走替换链。
- 将 forwarded_statements 布尔数组替换为 forwarded_values：每条省略
  语句明确记录已移动的 RHS ExprId。打印器将其余省略子树来源运输到
  边界，仅跳过该 RHS，保留其新位置范围；不作新的效果或语义判断。
  指令位置及表达式范围测试相应核验明确运输身份，不跳过整棵来源树。
- 新增两侧 inert 包装与必要效果包装组合，检查原始/反序 statement
  arena、0/1/200/201/400/800、工作增长、独立执行、错误重复 producer
  与丢失/重排效果。每个新增来源必须有有效非零位置，移动 RHS 保持 span。
  必要外围效果不能通过整句删除处理，任意嵌套 occurrence 删除仍是能力缺口。
- 旧实现 form32/count0 恢复失败，/tmp/abcd-rule203-red-tests.log。
  实现后定向 2/2 通过，12.96秒，/tmp/abcd-rule203-focused-tests.log。
  新增声明位于 return 之后的 TDZ 反例另发现规则202迁移后的证明缺口：
  读取覆盖与 RHS 移动不能证明 PutValue 可删除。修复要求普通赋值消费
  FlowWriteEffect::kInert，词法初始化按独立 definition kind 处理；没有
  新增声明顺序/同块条件。反例旧实现失败，/tmp/abcd-rule203-tdz-test.log；
  修复后单元与独立执行 2/2 通过，13.15秒，
  /tmp/abcd-rule203-tdz-fixed-tests.log。发现问题后主动停止修复前回归，
  /tmp/abcd-rule203-regression-tests.log 不作为最终收据。
  最终相关回归 56/56 全部通过，410.39秒，日志
  /tmp/abcd-rule203-final-build.log、/tmp/abcd-rule203-final-tests.log 和
  /tmp/abcd-rule203-final-test-detail.log。快照 432 份生成函数、16,476 个
  行为情景、42 个错误变换；变量恢复 1,752/461,776/284，定义清理
  586/14,488/61。Home 362 个情景及 PageA 1,422 次配对执行通过。
- ABC-only 导出 build/stage-one/snapshot-roots-rule203：19 records，
  10 complete / 0 partial / 9 noModule；十份源码语法检查通过，正文与
  规则202一致，输入 SHA-256 不变。证据为 /tmp/abcd-rule203-export.log
  与 /tmp/abcd-rule203-export-check.json，不报告语料覆盖率提升或平台收据。
- 统一变量、完整声明生命期、全局固定点及阶段一仍开放；结构 gate、
  全量语料/RSS/DFX 暂停范围保持。

### 2026-10-08：规则202，快照从当前覆盖定义选择 producer

- ForwardSnapshots 直接消费 AnalyzeFlowReadCoverage 的定义结果，从
  当前 FlowDefinition 取得写入与 RHS，并通过 FlowEvent.discarded 取得
  所属语句。删除 BindingUses.root_store 及其 Scan 更新；本 pass 不再
  使用原始 use.store，也不再把覆盖证明映射回最初写入。其他 pass 的
  原始统计迁移仍未完成，不能据此宣称全局固定点已实现。
- 当前读/写计数、双向执行配对、异常上下文和环境证明保持。仍限定已有
  整赋值语句的源投影；嵌套写入需要保持周边效果的 occurrence 删除，
  该表示能力缺口明确记录，不作为语言或图形状不变量。声明与环境来源
  信息仍沿原实现运输，不增加局部序列、同块、邻接或样本条件。
- 新增早期死写入、效果型早期死写入及早期值被观察的组合；前两组允许
  从后继保留定义恢复，后一组保持可观察值。覆盖反序 statement arena、
  0/1/200/201/400/800 和工作增长。独立执行核验 receiver、值身份、
  抛异常与调用顺序；错误重复 producer、删除效果/读取及提前 producer
  的变换由对照识别。
- 旧实现 form29/count0 恢复失败，/tmp/abcd-rule202-red-tests.log。
  实现后快照单元与源码测试 2/2 通过，11.33秒，
  /tmp/abcd-rule202-focused-tests.log。最终相关回归 56/56 全部通过，
  411.64秒，构建/测试日志为 /tmp/abcd-rule202-regression-build.log、
  /tmp/abcd-rule202-regression-tests.log、/tmp/abcd-rule202-test-detail.log。
  快照 384 份生成函数、14,652 个行为情景、32 个错误变换；Home 362 个
  情景与 PageA 1,422 次配对执行通过。
- ABC-only 导出 build/stage-one/snapshot-definitions-rule202：19 records，
  10 complete / 0 partial / 9 noModule；十份源码语法检查通过，正文与
  规则201一致，输入 SHA-256 不变。证据为 /tmp/abcd-rule202-export.log
  和 /tmp/abcd-rule202-export-check.json；不报告覆盖率提升或平台执行收据。
- 定义选择 O(B+D+R) 工作、O(B) 附加存储，B/D/R 为绑定/定义/引用数；
  没有逐绑定遍历整个图。统一变量、结构 gate 和阶段一目标仍开放，
  全量语料/RSS/DFX 暂停范围保持。

### 2026-10-08：规则201，快照写入计数消费当前语义流图

- ForwardSnapshots 的候选筛选不再使用最初 Scan 的 use.writes==1。
  当前 FlowReference 的读/写计数与读取覆盖、执行次数、异常上下文
  共同决定是否移动；计数包括 typed statement-site 写入，不能只数
  ExprId 写入。前序 pass 已删除的冗余写入不再阻挡快照恢复。
- 当前流程仍先完成 stored-result/inert/dead-definition 清理，再执行
  快照分析；分析与方案生成先于 BeginCommit。此步未更改源位置运输
  和声明生命期策略，也未迁移所有 BindingUses 消费者或实现全局固定点。
  任意后继定义的候选选择仍待后续共同定义模型迁移。
- 新增纯自赋值与携带副作用的自赋值组合，覆盖反序 statement arena、
  0/1/200/201/400/800 和工作增长。纯复制消除后可恢复 producer；效果
  保留时仍由共同 motion 分析阻止跨越调用。独立执行检查值身份、receiver、
  异常和事件序列；反例验证重复 producer、删除效果、提前效果均被发现。
- 旧实现 form27/count0 的恢复断言失败，/tmp/abcd-rule201-red-test.log。
  实现后两项定向测试通过，9.07秒，/tmp/abcd-rule201-focused-tests.log。
  最终相关回归 56/56 全部通过，406.68秒，日志
  /tmp/abcd-rule201-regression-build.log、/tmp/abcd-rule201-regression-tests.log
  与 /tmp/abcd-rule201-test-detail.log。快照 348 份生成函数、13,284 个
  行为情景、24 个错误变换；变量恢复 1,752/461,776/284，定义清理
  586/14,488/61；Home 362 个情景和 PageA 1,422 次配对执行通过。
- ABC-only 导出 build/stage-one/snapshot-writes-rule201：19 records，
  10 complete / 0 partial / 9 noModule；十份源码 node --check 通过，
  正文与规则200一致，输入 SHA-256 不变。证据为
  /tmp/abcd-rule201-export.log、/tmp/abcd-rule201-export-check.json。
  此步不报告真实语料覆盖率上升，也不授予平台 executable 收据。
- 新增写入计数复用已有 reference 遍历，附加存储 O(B)、工作 O(B+R)，
  B 为绑定数、R 为当前引用数；无逐绑定图遍历或样本/同块/邻接匹配。
  统一变量、结构 gate 和阶段一目标仍开放；全量语料/RSS/DFX 暂停范围保持。

### 2026-10-08：规则200，惰性语句清理退出上游改写准入

- PlanInertStatements 删除“表达式根已经被其他 pass 替换”的前提。
  表达式语句丢弃结果，共同初始化/效果事实证明 inert 即可省略；
  声明仍由独立职责处理，TDZ、强制转换、调用与暂停仍由效果模型保护。
  OmitInertStores 更名 OmitInertStatements，不在打印器中增加语义优化。
- 惰性语句清理在死定义与快照分析前执行，提交沿用 BeginCommit 的中央
  失效；存储/循环声明恢复之后保留依赖清理。此步删除具体顺序耦合，
  不代表全局固定点完成；部分 pass 的原始 BindingUses 统计尚未迁移，
  不能直接放进不受约束的重复执行循环。
- 新用例无 compiler temporary 候选，覆盖参数、普通局部初始化、初始
  undefined、TDZ、coercion、调用、literal、conditional，并核对反序
  arena、0/1/200/201/400/800 工作增长；等价 comma 包装显式比较有无
  上游替换。独立源码执行观察返回值、异常和副作用，错误删除反例必拒绝。
- 原实现新用例失败，/tmp/abcd-rule200-red-test.log。新增 fixture 曾构造
  未拥有的表达式，结构验证拒绝；修正测试构造后定向 2/2 通过，17.30秒，
  /tmp/abcd-rule200-focused-tests2.log。相关回归发现旧来源映射断言要求
  所有表达式非空；整句省略按已有 GeneratedExtent::kBoundary 契约输出。
  测试现明确检查省略子树的准确语句边界，保留表达式仍必须非空，未放宽
  行为和来源身份要求。
- 相关回归首轮 54/56 通过，285.55秒，两项失败均为上述来源范围断言，
  日志 /tmp/abcd-rule200-regression-tests.log；构建日志为
  /tmp/abcd-rule200-regression-build.log。修正测试契约并补齐等价 comma
  用例后，重建受影响目标，四项定向复验全部通过，141.74秒，日志
  /tmp/abcd-rule200-final-build.log、/tmp/abcd-rule200-final-tests.log 与
  /tmp/abcd-rule200-final-test-detail.log。未把首轮失败改写为整批全绿。
  最终定义/效果清理 586 份源码、14,488 次独立执行、61 个反例；变量
  恢复 1,752 份源码、461,776 次对照、284 个反例。Home 的 362 个配对
  情景与 PageA 的 1,422 次配对执行在首轮通过。
- ABC-only 导出 build/stage-one/inert-statements-rule200：19 records，
  10 complete / 0 partial / 9 noModule；十份源码 node --check 通过。
  对比规则199仅 Home 正文变化，删除构造函数中已初始化临时变量的独立
  无用读取；声明、super、后续 this 写入保留。输入 SHA-256 不变，
  /tmp/abcd-rule200-export.log。此处不是全阶段覆盖提升或平台执行收据。
- 全量语料/RSS/DFX 暂停范围保持；统一变量里程碑、结构 gate 和阶段一
  仍开放，Goal active。

### 2026-10-08：PageA 整个 record 的配对表示验证

- 在既有 ABC-only PageA record6897 独立行为测试上增加测试侧表示检查：
  模块导入导出、原生类和父类、方法/默认参数、普通方法正文，以及 UI
  构造、成员调用、async/await 的表示。允许导入拆分、形参改名、void0、
  静态属性访问拼写和类表达式；不要求输出逐字等于配对源码。
- 表示检查是本 record 的有限验收，不进入生产恢复路径；渲染方法的
  调用/构造清单不证明顺序或任意语义等价，顺序、接收者、异常及暂停
  继续由独立执行对照验证。配对源码仅在生产子进程退出后读取。
- 两种等价表示（构造参数改名、原生类表达式）均重新执行完整情景矩阵。
  原输出及两种改写共 1,422 次配对执行；保留 3 个语义反例，增加/复用
  2 个表示反例。删除 await 被拒绝；插入不可达 while 的行为对照通过，
  表示检查拒绝，以证明行为和表示检查各自承担的职责。
- 基线定向 CTest 1/1 通过；新增测试初次因字符串导出名解析失败，修正
  AST 文本边界后 1/1 通过。最终验证日志为 /tmp/abcd-pagea-style-final.log；
  生产代码和规则199未改变，无需 C++ 重构建。没有运行全量测试/语料/RSS。
- 此证据限定受控 SDK 宿主下的 PageA 测试情景及明确表示检查，未授予
  平台 executable 收据。统一变量恢复、全局 pass 固定点、结构 gate 和
  阶段一 Goal 仍开放，不以单 record 通过替代整体完成。

### 2026-10-08：规则199，共同流图记录表达式值用途

- RecoveryFlow 新增 typed FlowValueUse：observed/discarded/unmodeled。
  共同语义构造器通过 replacement、条件分支和 logical RHS 传递用途；
  判定条件及实际操作数观察值，sequence 左侧丢弃值、右侧继承用途，
  表达式语句丢弃结果，return/声明初值保留结果。正文图未处理的签名
  根保持 unmodeled，不能作为值无用证明。
- ElideInertOperands 消费共同用途与效果事实，允许删除结果被丢弃的
  sequence 的 inert 右操作数，保留左侧调用/异常/顺序；observed 值、
  logical 左侧判定、强制转换与抛异常操作仍保留。完整 ExprId 替换方案
  在 BeginCommit 前形成，打印器不再次作语义判断。只有出现右侧消除
  候选才懒取得流图，沿用中央缓存失效。
- 原实现在 discarded-result form0/count0 断言失败，日志
  /tmp/abcd-rule199-red-test.log。新用例覆盖丢弃/返回、条件两臂用途、
  logical 左右角色、coercion/异常，反序 arena 和 0/1/200/201/400/800。
  初验 3/3 通过，19.81 秒，日志 /tmp/abcd-rule199-focused-tests.log。
  流图测试核验赋值 RHS 与语句结果用途不同、refinement 保持用途及
  复用缓冲重建与新建一致。存储 O(X)、附加工作 O(X)，X 为表达式数。
- 首次相关批次 52/54 通过，281.39 秒；binding_initializer 的单元及
  源码测试在原表达式来源范围非空断言失败，日志 /tmp/abcd-rule199-tests.log
  与 /tmp/abcd-rule199-initial-test-detail.log。根因是 printer 的旧
  ProjectSequenceLeft 只运输左侧来源，未覆盖新右侧消除；已改为纯
  ProjectSequenceOperand 范围运输，明确传入被消除的 ExprId，不加入
  打印器语义优化。新增用例现在也校验所有表达式范围非空且位于输出内。
- 修复后 54/54 项定向测试全部通过，397.56 秒，日志
  /tmp/abcd-rule199-repaired-tests.log 与 /tmp/abcd-rule199-test-detail.log。
  定义/表达式清理 394 份源码/9,304 次执行/49 个反例；变量/别名
  1,752/461,776/284；存储 221/33,620/12；快照 324/12,372/18；
  冗余写回 528/18,192/12。最终目标构建通过，日志
  /tmp/abcd-rule199-repaired-build.log。首次构建与失败日志保持，未将
  原批次的两项失败隐去。
- ABC-only 最终导出 build/stage-one/value-uses-rule199-verified 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则198一致，输入身份保持，日志 /tmp/abcd-rule199-verified-export.log。
- 此步扩展统一值/效果模型与实际清理通路，不代表全局 pass 固定点、
  canonical IR/完整 activation/unwind 已完成；统一变量里程碑、结构
  gate 和阶段一继续开放，Goal active。全量语料/RSS/DFX/代理等暂停
  范围保持，定向独立源码执行不等同于平台和全阶段语义/style 验收。

### 2026-10-08：规则198，复制传播消费共同正常结果身份

- Aliases 删除直接 SymbolExpr RHS 准入，改用共同 ExpressionFacts 的
  正常结果绑定身份，复用序列/赋值/分支/逻辑表达式的语义组合。只读
  绑定读取也提供身份，PutValue 消除仍独立要求可写目标；原定义覆盖、
  mapped arguments、escape、名字解析与生命期证明保持。
- 删除临时存储与删除 RHS 分别决定：共同效果分析仍判 required 的 RHS
  在原位置保留 assignment-to-RHS 投影，不能因 alias 消失而丢调用或
  异常。所有方案在 BeginCommit 前计算，提交阶段不再消费分析引用。
- 词法保留依赖改用共同 FlowEvent 的 discarded owner，覆盖一个复合
  RHS 的所有读取；旧 direct-RHS ExprId 查找和单 blocked-source 槽
  实际删除。稀疏邻接表/有界工作表传播所有依赖，新增 O(B+R) 工作与
  存储，不逐候选遍历子树、不重复全 pass 扫描。
- 旧实现先在 compound-copy form0/count0 失败，证据
  /tmp/abcd-rule198-red-test.log。新增副作用序列/分支/逻辑及 const
  来源，源变量改写、初始 undefined 观察、catch 在成功写回前读取、
  分支结果不一致的反例；反序 arena、0/1/200/201/400/800、exact/short
  预算通过。扩展初验 2/2 通过，15.99 秒，日志
  /tmp/abcd-rule198-expanded-tests.log。
- 另补两个上游复制同时被目标作用域名字遮蔽的组合：保留下游必须
  保留两条依赖，删任意一个会引入 TDZ。补充 C++ 单元退出0；源码
  322 份/7,216 次比较/31 个反例通过，日志 /tmp/abcd-rule198-scope-unit.log
  与 /tmp/abcd-rule198-scope-source.log。手动源码补测首次漏加
  --expose-internals，解析器未启动；已按 CTest 注册命令重跑成功，
  原启动失败日志 /tmp/abcd-rule198-scope-source-launch.log 保留。
- 最终相关 CTest 批次 54/54 通过，398.16 秒，日志
  /tmp/abcd-rule198-tests.log 与 /tmp/abcd-rule198-test-detail.log；源码
  对照包含补充 scope 用例，追加 scope 的 C++ 单元已另行补验退出0。
  变量/别名 1,752/461,776/284；存储 221/33,620/12；快照
  324/12,372/18；定义/复合复制 322/7,216/31；冗余写回
  528/18,192/12。构建日志 /tmp/abcd-rule198-final-build.log 及
  /tmp/abcd-rule198-scope-build.log。独立源码执行不是平台执行。
- ABC-only 导出 build/stage-one/compound-copies-rule198 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则197一致，日志 /tmp/abcd-rule198-export.log。此次增加通用表达式
  组合能力，无新增该真实输入覆盖，不替代全阶段语义/style 验收。
- 全局 pass 固定点、共同 activation/unwind 与 canonical IR 对应仍未
  完成；结构 gate、统一变量里程碑和阶段一保持开放。Goal active，
  全量语料/RSS/DFX/代理等暂停范围保持。

### 2026-10-08：规则197，循环声明规划与提交分离

- 原 ProjectLoopDeclarations 在同一遍历中判断和修改共享计划，后续循环
  继续读取已经部分更新的状态。现抽取 const PlanLoopDeclarations，只读
  当前 LoopSyntax/BindingInitializers 和已知声明身份，返回 typed 起点、
  降回表达式和原生 declarator 修改。调用者完成全部规划和预算接纳后，
  只进入一次 BeginCommit，后续只应用 delta，不再分析半更新计划。
- 保留 ForDeclaration 的表达式前缀/声明后缀语法合同，初始化/生命期/
  名字解析证明仍归原共同分析，不重新推断。删除无消费者的中途 entry
  前缀计数重算，只在所有 pass 后从最终投影生成。O(S+R+B) 工作，
  S 为 statement-indexed loop slots，R 为互斥 entry roots，B 为绑定数。
- 新增 1/200/201/400/800 个独立循环的批量测试：规划不修改输入，反向
  loop arena 与 delta 应用顺序不影响结果，已应用投影的重复规划为空，
  exact/short 资源与工作预算通过。既有源码测试保留嵌套循环、异常、
  await/yield 和存储合并/清理组合。初验 2/2 通过，23.03 秒，日志
  /tmp/abcd-rule197-focused-tests.log。
- 最终 37/37 项定向组合测试通过，243.23 秒，日志
  /tmp/abcd-rule197-tests.log 与 /tmp/abcd-rule197-test-detail.log。
  循环初始化 276 份源码/13,892 次行为对照/60 个反例；变量/别名
  1,752/461,776/284；快照 324/12,372/18；定义消除 224/2,880/11；
  冗余写回 528/18,192/12；局部初始化 192/7,596/16。独立执行对照不是
  平台执行。相关目标构建通过，日志 /tmp/abcd-rule197-focused-build.log
  与 /tmp/abcd-rule197-final-build.log。
- ABC-only 导出 build/stage-one/loop-declarations-rule197 保持
  10complete/0partial/9noModule，十份源码 node --check 通过，正文与
  规则196一致；固定输入 SHA256 保持，日志 /tmp/abcd-rule197-export.log。
  这是 pass 职责分离的交付，无新增真实 record 覆盖，不替代独立全阶段
  源码语义/style 验收。
- 全局 pass 依赖固定点调度、共同 activation/unwind、canonical IR 对应
  及结构 gate 仍开放。阶段一 Goal active；全量语料/RSS/DFX/代理等暂停
  范围保持。

### 2026-10-08：规则196，共同执行次数与异常上下文替换区域匹配

- AnalyzeFlowMotion 复用同一方向化求解器，提供 next/previous 必经观察。
  正向检查写回后必达目标读取；反向检查读取的上一个观察必为该写回，
  排除一次写回对应多次循环读取。反向只消费调用入口可达的前驱，入口
  本身作为历史锚；不可达结构退出不制造虚假执行。全部完成边参与，
  透明环保持未知；O(N+E) 时间/O(N) 空间，复用 scratch，保持原资源预算。
- FlowReference 新增 typed FlowExceptionContext，统一保存 thrown/finalizer
  身份；初始化细化把读取变成 nonthrowing 时仍保留上下文，移动一个可抛
  异常或暂停的生产者不能因此丢失处理器义务。快照消费共同引用与两向
  观察事实，先形成方案再 BeginCommit；不在提交期消费失效的图引用。
- 删除 expression_regions_、OpenRegion、region counters 与扫描 guarded
  状态，same-region 准入真正退出决策路径。词法环境/名字解析/生命期与
  资源可观察创建义务仍保留；不把区域删除表述为完整 activation/unwind
  模型已完成，不增加同块、邻接或特定循环形状分支。
- 新一次执行循环用例在旧实现 form25/count0 失败，日志
  /tmp/abcd-rule196-red-test.log；新重复读取循环必须保持快照。分别故意
  删除反向执行关系和异常上下文检查，在 form26/count0、form15/count0
  被检出，日志 /tmp/abcd-rule196-repeat-mutant-test.log 与
  /tmp/abcd-rule196-handler-mutant-test.log；最终构建已恢复正确实现。
  4,096 个小图/观察配置的前后向结果与独立枚举对照；200/201 与几何
  增长、exact/short 预算、反序 arena、异常/循环组合纳入定向验证。
- 最终 54/54 项定向测试通过，396.10 秒，日志
  /tmp/abcd-rule196-tests.log 与 /tmp/abcd-rule196-test-detail.log。
  快照 324 份源码/12,372 次行为/18 个反例；变量/别名
  1,752/461,776/284；存储 221/33,620/12；定义消除 224/2,880/11；
  冗余写回 528/18,192/12。相关目标最终构建通过，日志
  /tmp/abcd-rule196-final-build.log。独立源码行为对照不等同于平台执行，
  不以这些定向证据替代全阶段验收。
- ABC-only 导出 build/stage-one/flow-contexts-rule196 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则195一致，固定输入身份保持，日志 /tmp/abcd-rule196-export.log。
- 共同绑定 activation、默认参数/复杂 unwind、canonical IR 对应及
  全局依赖固定点仍开放。统一变量里程碑、结构 gate 和阶段一未完成，
  原 Goal active；全量语料/RSS/DFX/代理等暂停范围保持。

### 2026-10-08：规则195，共同必经观察分析替换线性移动计数

- 实现并贯通 AnalyzeFlowMotion：对当前 RecoveryFlow 的全部完成边执行
  反向全路径传播。读取、定义、必要效果及终止点为锚；透明节点仅在
  所有后继都已解析且首个锚一致时取得证明。可无限停留的透明环保持
  未知，不把 may-reach 当成 must-execute。复杂度 O(N+E) 时间/O(N) 空间，
  每个快照候选通过成功写回/目标读取 FlowPoint 常数时间查询。
- 删除快照 equal-epoch、equal-control-prefix 两项准入及 ObservationSite、
  control_events_、control_ 和构建逻辑；不再用原语句计数/线性位置证明
  移动。共同语义层显式标记 debugger 的必要效果。保留 region equality
  作为 handler/activation 对应的实现限制，并保留词法环境/生命期义务；
  尚无完整共享 unwind/context 事实，不能将此限制描述为语言规则。
- 新透明分支合流用例在旧实现 form23/count0 的转发断言失败，日志
  /tmp/abcd-rule195-red-test.log；新算法允许所有路径先到同一读取的
  无观察控制变化，实际效果、返回绕过、debugger 和旧异常/循环反例保持。
  4,096 个三节点图/观察集合配置与独立路径枚举对照通过；200/201/
  400/800/1600 链与循环、exact/short 预算通过。定向初验 3/3，11.26 秒，
  日志 /tmp/abcd-rule195-focused-tests.log。
- 最终 CTest 批次 53/53 通过，396.54 秒，日志 /tmp/abcd-rule195-tests.log
  与 /tmp/abcd-rule195-test-detail.log。快照 300 份源码/11,460 次行为/
  14 个反例；变量/别名 1,752/461,776/284；存储 221/33,620/12；定义
  消除 224/2,880/11；冗余写回 528/18,192/12。相关目标构建通过，日志
  /tmp/abcd-rule195-final-build.log。本次 CTest 过滤式漏选 capture_tree_test，
  已用同一构建的 build/capture_tree_test 单独补跑退出0，日志
  /tmp/abcd-rule195-capture-test.log；不将分次验证表述为一次54项CTest。
- ABC-only 导出 build/stage-one/flow-motion-rule195 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则194一致，固定输入身份保持，日志 /tmp/abcd-rule195-export.log。
- 剩余 handler/activation、默认参数/复杂 unwind、canonical IR 对应及
  全局依赖固定点仍开放。统一变量里程碑、结构 gate 和阶段一未完成，
  原 Goal active；全量语料/RSS/DFX/代理等暂停范围保持。

### 2026-10-08：规则194，快照观察集合消费共同流图

- RecoveryFlow 事件提供 intrinsic effect，构造与初始化细化同时维护，
  不限于 discarded statement 区域。读取和成功写回身份仍独立表示；
  初始化读取即使 effect inert，也仍是状态观察，update 读取/转换/
  成功写回及转换异常继续分离。
- ForwardSnapshots 从当前流图的读取、表达式写回和必要效果收集观察。
  原先再次查询 IntrinsicEvaluation、原语句省略和 literal 特例的条件
  实际删除；expression_statements_ 表与 current_statement_ 也删除。
  已消除逗号左值不再凭原始语法阻挡移动。执行顺序、control/region、
  environment 和 lifetime 的旧证明仍保留，未扩大任何局部形状准入。
- 新增嵌套 return 操作数组合：已丢弃 flag 读取允许转发，input.effect()
  必须保留原先求值和异常顺序；覆盖反序 arena、透明块拆分、0/1/200/
  201/400/800 规模。旧实现先在 form21/count0 的转发断言失败，证据
  /tmp/abcd-rule194-red-test.log。共同图、快照单元、独立源码执行初验
  3/3 通过，11.12 秒，日志 /tmp/abcd-rule194-focused-tests.log。
  图测试检查 inert 读取仍有 read 身份、转换 effect required，以及
  复用缓冲重建与新建图的 effect 一致。
- 最终 54 项定向全部通过，394.92 秒，日志 /tmp/abcd-rule194-tests.log
  与 /tmp/abcd-rule194-test-detail.log。快照 276 份源码/10,548 次行为/
  12 个反例；变量/别名 1,752/461,776/284；存储 221/33,620/12；定义
  消除 224/2,880/11；冗余写回 528/18,192/12。相关目标构建通过，日志
  /tmp/abcd-rule194-final-build.log。
- ABC-only 导出 build/stage-one/flow-observations-rule194 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则193一致，固定输入身份保持。日志 /tmp/abcd-rule194-export.log。
- 剩余共同 motion/activation、默认参数与复杂 unwind、canonical IR
  对应、全局依赖固定点仍开放。统一变量里程碑、结构 gate 和阶段一
  未完成；原 Goal active，全量语料/RSS/DFX/代理等暂停范围保持。

### 2026-10-08：规则193，无效操作数消除迁出打印器

- 新增独立 ElideInertOperands pass：逗号左值是否可消除由共同完成/效果
  摘要决定，先批量提案，再通过 BeginCommit 更新投影。替换保留 GetValue
  与禁止隐式 NamedEvaluation 的值类别；仅保留已证明不受无效读取消除
  影响的初始化事实。调用、转换、TDZ、暂停仍由共同效果模型保留。
- 打印器原先独立查询 evaluations 并删除 comma-left 的判断实际移除。
  现在只消费 replacements 并运输被省略左子树的来源范围。共同流图、
  活跃性、死定义、快照和 class-name ownership 看到相同的读取集合。
  写回消除后、依赖的需求/快照清理前显式调度；最终需求消除后规范化
  新暴露的无效操作数，不宣称固定次数重复已经构成全局固定点算法。
- 新用例旧实现先在 dead-store 断言失败（/tmp/abcd-rule193-red-test.log）。
  原因是打印器消除 state 读取后，分析仍保留它，使只供逗号左值使用的
  写入留下。迁移后删除无效写入，保留 produce(1) 的原始执行位置和异常。
  新增嵌套逗号、反序 arena、200/201/几何增长，以及 +state 的对象转换、
  BigInt/Symbol 错误对照；独立 mutant 删除必要 RHS 或转换必须被检出。
- 最终 54 项定向全部通过，392.73 秒，日志 /tmp/abcd-rule193-tests.log
  与 /tmp/abcd-rule193-test-detail.log。定义消除 224 份源码、2,880 次独立
  行为比较、11 个反例；变量/别名 1,752/461,776/284，存储 221/33,620/12，
  快照 252/9,636/10，冗余写回 528/18,192/12。相关目标构建通过，日志
  /tmp/abcd-rule193-final-build.log；最初 definition_elision 单元及独立
  源码执行 2/2 通过，11.32 秒，日志 /tmp/abcd-rule193-focused-tests.log。
- ABC-only 导出 build/stage-one/discarded-operands-rule193 为
  10complete/0partial/9noModule，十份源码 node --check 通过，正文与
  规则192一致，固定输入身份保持。日志 /tmp/abcd-rule193-export.log。
- 新 pass 每次按 E 个表达式线性扫描，提案空间 O(C)，C 为未投影逗号
  节点数；无每变量遍历图、局部 opcode/块形状或样本准入。旧快照 motion
  census、绑定 activation、默认参数/复杂 unwind、canonical IR 对应与
  全局依赖固定点仍开放。结构 gate、统一变量里程碑、阶段一未完成；
  Goal active，全量语料/RSS/DFX/代理等暂停事项保持。

### 2026-10-08：规则192，写前提交边界与投影版本

- 八个变量恢复 pass 不再返回手工维护的 changed。首次语义写入前统一
  BeginCommit，推进 preparation-local ProjectionRevision，并按中央保留
  契约丢弃依赖分析；同批后续写入不重复推进版本，no-op 保留缓存。
  RunPass 根据实际版本差触发后续清理，循环声明语法投影也计入修改。
- 分析/提交阶段分离。CurrentFlow、CurrentLiveness、AnalyzeExpressions、
  ComputeExpressions 在读取缓存前禁止提交阶段请求，防止分析半修改
  投影。既有提案、词法和初始化证明在首次写入前完成，缓存失效从
  pass 返回后前移到写入前；这是内部所有权义务，不是恢复形状准入。
- 故意在冗余写回首次 BeginCommit 后调用 AnalyzeExpressions，现有
  stored_result_test 检出错误；LLDB 首个抛出栈确认 RequireAnalysis
  抛 kOwnership，尚未重新分析。日志 /tmp/abcd-rule192-mutant-test.log、
  /tmp/abcd-rule192-mutant-debug.log。故意改动已撤销并重新构建全部
  定向目标，日志 /tmp/abcd-rule192-restored-build.log。
- 最终 54 项定向回归全部通过，393.07 秒，日志
  /tmp/abcd-rule192-tests.log 与 /tmp/abcd-rule192-test-detail.log。
  变量/别名 1,752 份源码/461,776 行为/284 反例；存储 221/33,620/12；
  快照 252/9,636/10；定义消除 204/2,280/7；冗余写回 528/18,192/12。
  既有布局变换、200/201/几何增长、独立执行和预算边界保持通过。
- ABC-only 导出 build/stage-one/pass-commit-rule192 保持
  10complete/0partial/9noModule；十份源码 node --check 通过，正文与
  规则191一致。日志 /tmp/abcd-rule192-export.log，固定输入身份保持。
- 计划成员尚非编译期不可变，未来新写入仍须经过共同提交边界；本轮
  不宣称外部持久分析句柄或全局固定点 pass 图已完成。原 IR 对应、
  activation/默认参数/复杂 unwind、旧 motion/声明生命期仍待迁移。
  统一变量里程碑、结构 gate、阶段一未完成，Goal active；全量语料、
  RSS/DFX/代理等暂停事项保持。规则191的扫描 CLI 环境失败未改未重跑。

### 2026-10-08：规则191，初始化事实与 pass 失效契约贯通

- 删除 InitializedRead 及 initialized_first/定义区域/读取区域元数据。
  先以保守效果建立完成流，由共同初始化支配分析产生事实，再细化表达式
  效果和同一图的异常边；避免 CurrentFlow/AnalyzeExpressions 递归依赖。
  FunctionSelf 增加显式函数体入口定义；默认参数程序尚未建模，其读取
  不借用函数体入口证明。自增读取、转换、成功写回及转换异常继续分离。
- RunPass 使用类型化 pass 身份，保留契约集中声明一次。发生修改时统一
  丢弃事件索引的图、支配、活跃性及表达式摘要；仅已证明保留初始化的
  消除/存储合并保留正向 occurrence 事实，仍按当前存储映射重算效果。
  常量、别名、快照移动、循环声明投影同时丢弃该事实。无修改 pass 共享
  只读分析；调度点不再手选失效集合。需求消除先于快照，存储与声明
  修改之后按依赖运行清理，最终发布当前表达式摘要。
- 初版反复建图/分析触及既有累计预算。通过同图细化、共享 LT 的直接 CSR
  输入、移动发布图缓冲及复用同一预算所有者已支付容量消除重复成本；
  每次重建全部语义内容，新增容量及增长重叠继续收费，未提高或退还额度。
  复用/新建等价、异常边细化、正向初始化证明保持及 exact/short 预算
  验证通过；这不构成进程 RSS 验收。
- 相关目标构建通过，日志 /tmp/abcd-rule191-final-build.log。
  56 项定向测试首次 55 通过（393.82 秒），日志
  /tmp/abcd-rule191-final-tests.log，完整输出保存在
  /tmp/abcd-rule191-test-detail.log。唯一未通过 dominators_cli_test
  首次缺 abc_scan/abc_dominators；补建依赖通过后单独重跑仍在 abc_scan
  设置 RLIMIT_AS 阶段失败，报 cannot establish process memory limit，
  未进入支配分析。日志 /tmp/abcd-rule191-cli-build.log 与
  /tmp/abcd-rule191-cli-tests.log。扫描器本轮未修改，不绕过资源门槛，
  不宣称 56 项全绿；原 54 项源码恢复回归和 dominators_test 均通过。
- 定义消除新增 TDZ 读取/赋值、RHS 抛异常及初始化后无效读取用例，覆盖
  arena 反序、200/201 与几何增长：204 份生成源码、2,280 次独立执行、
  7 个反例。变量/别名 1,752 份源码/461,776 行为/284 反例；存储
  221/33,620/12；快照 252/9,636/10；冗余写回 528/18,192/12。
- ABC-only 导出 build/stage-one/initialization-facts-rule191：
  10complete/0partial/9noModule，10 份源码 node --check 通过，正文与
  规则190一致；日志 /tmp/abcd-rule191-final-export.log。输入 SHA256
  854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
- 全局投影版本/固定点收敛、canonical IR 对应、绑定 activation/default
  parameter/复杂 unwind、旧 motion/声明生命期证明仍待迁移。统一变量
  里程碑、结构 gate 和阶段一均未完成，原 Goal active；全量语料、
  RSS/DFX、代理等暂停范围保持。

### 2026-10-07：规则190，别名名称义务消费共同引用事件

- Aliases的名称/生命期查询改用共同覆盖分析所属流图的FlowReference，
  保留类型化binding与expression身份。原始SymbolExpr引用枚举及名称
  查询后重新从语法提取源绑定的cast实际删除；覆盖、引用和替换提案
  消费同一当前投影，在RunPass提交边界之前完成。
- 单一定义/动态值身份、批量词法名称解析、RootLifetime仍分别承担义务；
  copy-RHS索引仅关联已知复制写入与源读取，不再独立决定引用集合。
  查询按R个当前引用分配有界空间，继续计入原预算，无按绑定遍历图。
- 新增typeof表达式组合及内层同名绑定的正反例，覆盖arena/symbol/scope
  重排和0/1/200/201/400/800复制规模。独立执行还故意把保留的读取改成
  内层同名绑定，区分对象和数字，检出错误替换；生产代码没有新增
  typeof、样本、同块、邻接或固定图形状准入分支。
- 相关目标构建通过，日志/tmp/abcd-rule190-build.log。最终54项相关回归
  全部通过380.67秒，日志/tmp/abcd-rule190-tests.log，详细输出
  /tmp/abcd-rule190-test-detail.log。变量/别名1,752份源码/461,776行为/
  284反例；存储221份源码/33,620行为/12反例；快照252份源码/9,636行为/
  10反例；定义消除184份源码/2,080行为/3反例；冗余写回528份源码/
  18,192行为/12反例。512支配图、175,616活跃性/定义/覆盖图、98,304需求图
  及既定布局变换、规模和exact/short预算门槛继续通过。
- ABC-only导出build/stage-one/alias-references-rule190为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则189
  一致；日志/tmp/abcd-rule190-export.log。本轮交付是共同事实迁移及组合
  验证，不新增全阶段语义/风格完成声明。
- 下一步初始化迁移须处理CurrentFlow→AnalyzeExpressions→InitializedRead
  的实际依赖；不能直接在InitializedRead里反查CurrentFlow形成递归。
  完成/TDZ事实生产须与效果摘要细化分离。canonical IR/activation、旧
  motion条件、全局版本与收敛仍开放；统一变量里程碑、结构gate和阶段一
  未完成，原Goal active及全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则189，快照接入共同覆盖并统一局部pass失效边界

- ForwardSnapshots消费共同定义覆盖及当前FlowReference读取事件，不再从
  原始表达式形状重新统计读取。CompletedStore/PrivateCopy的最后消费者
  已迁移，两个函数及无用store_region、declaration_controls、
  completion_order元数据实际删除；旧局部条件不再决定值可用性。
- 先完成死读取/定义/空绑定清理，再分析快照提案，最后批量提交。新增
  discarded initial read与observed initial read的正反例；前者可恢复
  直接return，后者保留初始undefined观察。快照保留RHS的单次求值，
  删除自身read/store/declaration，不因此产生另一绑定的死定义；后续
  存储/声明变化才重新触发需求清理，最终效果/正常写入摘要最后发布。
- RunPass统一管理局部流图/全部读取活跃性的失效。常量、别名、死定义、
  快照、存储合并、冗余写回、惰性语句和循环声明投影报告实际变化；变化
  后由同一边界销毁两项分析，无变化则借用同一份const事实。覆盖适配器
  的引用span仅在单个pass内借用，不跨提交边界。每个pass内部仍须先
  分析/提案后写入，尚未做到所有旧计划字段的类型级只读隔离或全局调度。
- 初版额外清理和重复分析使既定类/record门槛触发累计scratch预算不足；
  LLDB定位到Coalesce的初始化支配分析，单独删掉重复清理仍不够。
  通过统一失效边界复用未变化分析后修复，未扩大预算、退回局部判断或
  增加样本分支。故意关闭RunPass失效的开发mutant被别名/快照组合
  （form11，零包装）断言检出；正式构建前已恢复，日志
  /tmp/abcd-rule189-stale-mutant.log。
- 最终相关目标构建通过，日志/tmp/abcd-rule189-final-build.log。
  54项相关回归全部通过372.11秒，日志/tmp/abcd-rule189-final-tests.log，
  详细输出/tmp/abcd-rule189-test-detail.log。快照252份源码/9,636行为/
  10反例；初始化/别名1,730份源码/461,160行为/280反例；存储221份源码/
  33,620行为/12反例；定义消除184份源码/2,080行为/3反例；冗余写回
  528份源码/18,192行为/12反例。512支配图、175,616活跃性/定义/覆盖图、
  98,304需求图、200/201/400/800、布局变化及exact/short预算继续通过。
- ABC-only导出build/stage-one/snapshot-coverage-rule189为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则188
  一致；日志/tmp/abcd-rule189-final-export.log。最终既定真实record回归
  通过，不将源码完整标记扩张为全阶段语义/风格验收。
- 旧motion epoch/control region与词法环境检查仍是待迁移的效果顺序、
  执行次数和activation义务，不扩展形状准入。别名原始引用枚举、
  InitializedRead、canonical IR/完整activation、全局版本与收敛继续
  开放；统一变量里程碑、结构gate和阶段一未完成。原Goal active及全量
  语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则188，别名复制接入共同定义覆盖

- Aliases不再调用CompletedStore/PrivateCopy，声明源的可用性也不再依赖
  initialized_first。Constants与Aliases共用AnalyzeBindingReadCoverage，
  由当前流图的定义身份关联store/declaration；常量提交后别名重新分析，
  不复用提交前的流图或覆盖事实。
- 保留源定义须覆盖所有源读取（包括copy RHS），复制定义须覆盖所有
  目标读取；另一源执行若能绕过copy到达目标读取，将与copy覆盖矛盾。
  这证明替换读取保留动态值，不重放调用/分配。入口未知值、undefined
  与其他定义继续阻止证明；原单写入表示及未逃逸约束保持。
- 参数/catch/self初始化及mapped arguments遵循原语言契约，名称解析、
  Confined/RootLifetime仍承担独立的名称和activation义务。新增copy或
  提前返回后汇合读取的源码组合，原条件漏写、循环零次/多次执行、对象
  身份、捕获、shadowing与await/yield用例继续作为验收门槛。
- 相关目标构建通过，日志/tmp/abcd-rule188-build.log。最终54项相关回归
  全部通过394.10秒，日志/tmp/abcd-rule188-tests.log、详细输出
  /tmp/abcd-rule188-test-detail.log。初始化/别名恢复1,730份源码/461,160
  行为/280反例；存储合并221份源码/33,620行为/12反例、定义消除184份
  源码/2,080行为/3反例、冗余写回528份源码/18,192行为/12反例通过。
  512支配图、175,616活跃性/定义/覆盖图、98,304需求图，以及200/201/
  400/800、arena/symbol/scope重排和exact/short预算继续通过。
- ABC-only导出build/stage-one/alias-coverage-rule188为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则187
  一致；日志/tmp/abcd-rule188-export.log，不新增真实record验收声明。
- CompletedStore现在仅在ForwardSnapshots路径保留，已标明不得继续扩展。
  别名原始引用枚举、InitializedRead、完整activation/canonical IR接入、
  版本协调与全局收敛继续开放；统一变量里程碑、结构gate与阶段一未完成。
  原Goal active及全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则187，常量传播退出CompletedStore并消费共同定义覆盖

- Constants不再调用PrivateCopy/CompletedStore，原始first observation与
  store region范围不再决定常量准入。AnalyzeFlowReadCoverage消费当前
  RecoveryFlow的全部读取活跃性：目标定义是唯一被观察的来源，且没有
  活跃入口未知值，才允许覆盖读取；活跃undefined或其他定义统一阻止替换。
  不构造完整read×definition关系，不按绑定重新遍历图。
- 这项证明只描述静态定义覆盖，不等同动态执行身份。生产消费者仅传播
  每次执行均相同的原始值literal及已证明常量的复制；对象/调用/捕获值
  不因此获得移动或复制权限。未被读取的纯常量写入也可省略。
- 常量稀疏工作队列现在更新局部literal提案，全部求解后才批量发布并
  删除对应私有空声明/存储。后续Aliases消费提交后的投影；没有旧分析
  跨提交复用，也没有增加同块、邻接或分支形状准入。
- 新增“赋值分支与提前返回分支汇合后读取”的完整源码恢复：旧区域条件
  拒绝，通用定义覆盖现在可以删除临时变量。条件漏写、初始undefined、
  异常/finally及循环路径仍由现有反例覆盖。新增删掉另一分支return的
  合法JS错误变换；175,616小图独立前向定义oracle同时核对覆盖结果。
- 相关目标构建通过，日志/tmp/abcd-rule187-build.log。最终54项相关回归
  全部通过354.41秒，日志/tmp/abcd-rule187-tests.log、详细输出
  /tmp/abcd-rule187-test-detail.log。初始化恢复1,718份源码/459,144行为/
  278反例；存储合并221份源码/33,620行为/12反例、定义消除184份源码/
  2,080行为/3反例、冗余写回528份源码/18,192行为/12反例全部通过。
  512支配图、175,616活跃性/定义/覆盖图、98,304需求图，以及200/201/
  400/800、arena重排和exact/short预算继续通过。
- ABC-only导出build/stage-one/constant-coverage-rule187为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则186
  一致；日志/tmp/abcd-rule187-export.log，不新增真实record验收声明。
- CompletedStore仍在Aliases/ForwardSnapshots中；InitializedRead、统一
  activation/canonical IR接入、版本协调与全局收敛继续迁移。本轮没有
  宣称这些义务已退出。统一变量里程碑、结构gate和阶段一仍未完成，
  原Goal active及全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则186，统一表达式事实与无用语句批量提交

- AnalyzeExpressions替代AnalyzeStoredResults与AnalyzeEvaluations：在一次
  const后序遍历中产生result-binding、normal-store和evaluation事实。
  替换节点只转发当前替代表达式事实，不重新引入原有子节点的效果。
  CurrentFlow与最终发布都消费同一次分析的相关数组；分析不修改plan，
  局部结果不跨投影修改缓存。最终数组仅在全部变换后一次发布。
- OmitInertStores拆成共同事实→const PlanInertStatements提案→批量提交，
  不再在扫描语句时修改omitted。冗余写回、死定义/声明和存储合并各自的
  提案与提交边界保留，生产pass顺序及映射后的依赖清理没有改变。
  没有增加样本、序列、同块或图形状准入条件。
- 新增嵌套同值写回、条件两臂读取同一绑定的源码组合，同时独立检查
  最终效果摘要：删除外层写回不代表调用、转换、暂停或TDZ读取可删除。
  原始/反转arena、200/201/400/800及既有不安全变换反例继续覆盖。
- 相关目标构建通过，日志/tmp/abcd-rule186-build.log。最终54项相关回归
  全部通过357.78秒，日志/tmp/abcd-rule186-tests.log、详细输出
  /tmp/abcd-rule186-test-detail.log。冗余写回528份源码/18,192行为/12反例，
  存储合并221份源码/33,620行为/12反例，定义消除184份源码/2,080行为/
  3反例；512支配图、175,616活跃性图、98,304需求图及预算边界通过。
- ABC-only导出build/stage-one/expression-facts-rule186为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则185
  一致；日志/tmp/abcd-rule186-export.log，不新增真实record验收声明。
- 这是共同事实生产与提交边界的迁移；InitializedRead、绑定activation、
  完整projection revision协调器和全局收敛仍开放。增加的O(S)语句提案
  数组受现有预算计费，未宣称整体性能提升。统一变量里程碑、结构gate
  与阶段一仍未完成；原Goal active及全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则185，存储合并共享当前引用与初始化完成事实

- RecoveryFlow一次产生typed绑定读写引用，自增的读取和成功写回分别记录，
  转发表达式使用当前执行位置；被前序pass删除的表达式不再留下决策引用。
  Coalesce不再扫描原始expression arena重新猜测引用，也移除了该路径的
  declaration_controls/definition_region及completion_order/native_ready判断。
- AnalyzeFlowInitializations索引当前选定的声明、参数和catch初始化，基于
  全部completion边计算一次支配关系。普通引用检查与独立的
  PlanStorageInitializations共同借用这份不可变事实，后者不再重复建索引
  或计算支配。两份提案完成后统一提交，无分析结果跨storage修改复用。
  不可达但保留的引用没有执行顺序义务，仍接受词法名称与生命期检查。
- 新增完整源码正反例：早期嵌入写入已被删除时允许后续共享；该写入仍被
  report读取时保留独立存储，错误共享导致的TDZ由独立执行反例发现。
  512个小图的独立删点可达性oracle同时核对初始化完成关系及预算边界。
  不增加样本、同块、邻接、操作序列或固定图形状条件。
- 相关目标构建通过，日志/tmp/abcd-rule185-build.log。最终54项相关回归
  全部通过334.30秒，日志/tmp/abcd-rule185-tests.log、详细输出
  /tmp/abcd-rule185-test-detail.log。存储恢复221份源码/33,620行为/12反例，
  定义恢复184份源码/2,080行为/3反例；175,616活跃性图、98,304需求图、
  200/201/400/800、arena重排和exact/short预算继续通过。
- ABC-only导出build/stage-one/reference-initialization-rule185为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则184
  一致；日志/tmp/abcd-rule185-export.log。这不是新增真实record语义验收。
- InitializedRead/效果生产、RootLifetime/activation和常量/复制路径中的
  CompletedStore仍待迁移；canonical CFG/SSA接入、完整版本失效协调器及
  全局收敛仍开放。统一变量里程碑、结构gate与阶段一未完成；原Goal active，
  全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则184，保留活跃undefined并退出Coalesce的CompletedStore

- Coalesce中的CompletedStore调用已移除，没有改名或移到另一个准入函数。
  存储提案后的独立PlanStorageInitializations消费共同活跃性：已死的初始
  undefined可省略，仍活跃的值在原声明位置写入共享存储。共同干涉分析
  已计入这次写入，若会覆盖另一活跃值则不共享。
- RecoveryFlow记录当前选定的源码声明；显式native初始化优先于其原空声明
  占位。复用既有Lengauer–Tarjan实现的AnalyzeFlowDominance，在包含全部
  exception/resume/conservative-cleanup边的图上证明目标声明先完成。
  二叉fanout仅适配既有图存储，保留真实事件间全部路径，不是恢复准入形状。
- 源码计划新增undefined_stores；printer只在原语句输出binding = void 0，
  不自行判断初始化死活。重建图以UndefinedStore区分赋值与词法初始化。
  既有ResolveTreeReferences/RootLifetime同时覆盖这次显式写入的词法身份，
  不增加样本、首条操作、邻接或同块规则。两份提案验证后共同提交。
- 新增直线活跃undefined、循环中重复初始化、目标native声明尚未完成等
  完整源码场景；保留原有干涉反例。漏掉reset的合法JS错误变换必须被独立
  执行发现。512个completion图通过独立删点可达性oracle验证支配关系，
  包含异常/恢复边和超过两个后继；另测初始化异常绕过及exact/short预算。
- 完成相关目标重建，日志/tmp/abcd-rule184-build.log。最终54项相关回归
  全部通过343.64秒，日志/tmp/abcd-rule184-tests.log、详细输出
  /tmp/abcd-rule184-test-detail.log。存储恢复197份源码/30,164行为/11反例，
  定义恢复184份源码/2,080行为/3反例；200/201/400/800、arena重排、预算
  边界及既有175,616/98,304小图对照继续通过。
- ABC-only导出build/stage-one/preserved-initialization-rule184仍为
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则183
  一致；日志/tmp/abcd-rule184-export.log。没有新增真实record验收声明。
- CompletedStore在常量/不可变复制恢复中仍存在；InitializedRead、native
  readiness、RootLifetime及完整activation模型继续迁移。这里只证明存储
  合并这一决策路径已退出旧条件，不代表统一变量里程碑或阶段一完成。
  结构gate、Goal active及全量语料/RSS/DFX/代理暂停范围保持。

### 2026-10-07：规则183，共同流图接入存储映射与依赖驱动的后续清理

- 核对CompletedStore后续迁移时发现前提缺口：CurrentFlow尚未消费已提交
  storage map。若直接开启合并后死定义删除，通过旧绑定写入、通过保留
  绑定读取的同一存储会被误作两个身份。因此先补齐这条实际状态依赖。
- LegacyFlowProjection显式携带storage；共同adapter在值别名之后解析
  存储身份，统一普通读取、定义、copy及update读取/成功写回。写入可观察性
  同样查询实际目标。没有新增样本、opcode、同块、邻接或图形状分支。
- Coalesce、stored-result和inert-statement pass报告实际修改，协调器汇总
  它们及loop声明投影变化。生产者完成且输入改变后，重建映射后的流图，
  再运行共同需求/定义/声明清理；无修改跳过额外分析。最后更新loop计数、
  normal-store和效果摘要。无旧分析跨提交复用，也未宣称全局固定点已实现。
- 流图测试明确验证update两半共用目标，并对照未映射旧图与当前图对
  必需写入的需求结论。新增完整源码场景：共享存储的后续调用结果写回；
  独立错误变换保留调用但删除写回，必须发现返回值不同。
- 完成相关重建，日志/tmp/abcd-rule183-build.log。最终54项相关定向回归
  全部通过333.65秒，日志/tmp/abcd-rule183-tests.log；详细输出
  /tmp/abcd-rule183-test-detail.log。存储恢复161份源码/24,980行为/9反例，
  定义恢复184份源码/2,080行为/3反例，175,616/98,304小图对照，以及
  200/201/几何增长、arena重排、exact/short预算继续通过。
- ABC-only导出build/stage-one/storage-projection-rule183仍为
  10complete/0partial/9noModule；十份源码node --check通过，正文与规则182
  一致。日志/tmp/abcd-rule183-export.log；没有新增真实record覆盖声明。
- CompletedStore、共同初始化与绑定activation、活跃undefined共享、完整
  版本失效协调器及全局收敛仍未完成。本轮仅闭合使用合并后共同事实的
  必要前提；阶段一Goal、结构gate和既定暂停范围保持。

### 2026-10-07：规则182，独立声明删除提案消费同一流图与写入delta

- RecoveryFlow增加typed FlowBinding exposure，沿现有adapter接入未逃逸
  compiler-private身份。新增PlanBindingElisions，独立判断写入/语句删除
  提案之后还有哪些语义读取或定义需要绑定；仅删除没有保留引用的私有
  undefined声明。不把“初始值已死”等同于“绑定生命期可删除”。
- 固定顺序为共同流图→需求闭包→定义删除提案→声明删除提案→批量提交
  →重新建立存储分析。两份提案都在原plan未修改时完成，无旧分析跨提交
  复用，也不为声明pass额外建图。消费typed事件，不重建语法/名字census。
  去掉入口必须存在写入的筛选，未曾写入的无用私有声明也进入共同分析。
- 保留活跃undefined、更新/赋值仍需的绑定、显式/native初始化、外部或
  捕获身份；不可达但仍保留的源码引用同样要求声明。没有新增样本、同块、
  邻接或图形状准入。CompletedStore及通用初始化/作用域证明未被替代。
- 新增64组绑定保留事实组合与exact/short预算测试；源码覆盖无用声明、
  保留调用RHS的死写入、活跃undefined、自增、嵌套同名作用域和非私有local。
  定义消除套件现在184份源码/2,080执行比较/3错误变换反例；arena反转、
  200/201/400/800及几何工作量通过，既有175,616/98,304小图对照继续通过。
- 完成相关目标重建，日志/tmp/abcd-rule182-build.log。54项相关定向回归
  全部通过325.30秒，日志/tmp/abcd-rule182-tests.log，详细输出
  /tmp/abcd-rule182-test-detail.log。包括部分输出、闭包捕获、循环/异常/暂停、
  命名、语言构造模式、既定真实record及54组EntryAbility独立配对路径。
- ABC-only导出build/stage-one/unused-bindings-rule182：
  10complete/0partial/9noModule，十份源码node --check通过。相对规则181
  仅EntryAbility正文删除了先前死写入遗留的let $value2;，其余九份正文不变。
  导出日志/tmp/abcd-rule182-export.log；这不代表整个record/阶段一语义验收完成。
- 完整投影版本/失效协调器、存储后的收敛、活跃undefined共享与旧判断退出
  仍开放。阶段一Goal和结构gate未完成；全量语料/RSS/DFX及代理保持暂停。

### 2026-10-07：规则181，共享反向求解器完成死复制需求收敛

- RecoveryFlow为结果被丢弃的表达式语句记录typed区域，并一次组合其
  效果。声明初始化、可观察写入、调用/转换/异常/暂停要求保留；不根据
  邻接、同块、名字或复制序列选择。控制条件和完成转移保持原表示。
- AnalyzeFlowDemand与AnalyzeFlowLiveness共享同一反向gen/kill及异常边
  求解器。可观察效果与区域外读取作为种子；遇到必需定义时激活其产生
  区域的读取。区域至多激活一次，工作队列单调收敛，随后统一提交写入/
  无效果语句delta。死复制链与无观察循环不再依赖逐次重建流图才能删除。
- FlowDemand与全读取FlowLiveness使用不同结果类型，避免需求裁剪结果
  被误传给存储干涉。提交后的Coalesce仍重建当前分析；初始化/存储映射
  和整个pipeline的收敛尚未完成。FlowDefinitions保留为已验证的后续分析，
  本次生产路径按需发现def-use依赖，不物化完整读取—定义乘积。
- 原物化方案在402-selection nullish回归触及既有资源预算；改用共享
  稀疏反向求解后通过，不放宽预算、不增加数量阈值。完整事实集仍可能
  稠密，按节点/边/活跃事实计费；不作整体线性或进程RSS达标声明。
- 独立对照覆盖175,616活跃性/定义图和新增98,304需求图，包括多定义区域、
  外部读取、效果和循环。源码测试增至124份/1,480执行比较/2错误变换反例；
  2/200/201/400/800复制链与循环、arena反转、exact/short预算及几何工作量
  验证通过。存储结果的480份源码/16,560行为比较/12反例继续通过。
- 位置测试移除“被删除表达式一定为赋值”的假定，并跟踪被取消的转发
  目的地到其RHS；生产位置标记协议未放宽。补充失败诊断保留在测试中。
- 最终54项相关定向回归全部通过317.17秒，日志/tmp/abcd-rule181-final-tests.log；
  详细输出/tmp/abcd-rule181-test-detail.log，重建日志/tmp/abcd-rule181-final-build.log。
  包括部分输出、异常/暂停、native命名、既定真实record与54组EntryAbility配对路径。
- 最终ABC-only导出build/stage-one/sparse-demand-rule181：
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则180一致。
  这轮提供通用收敛与规模能力，没有新增真实record覆盖声明。
- 共同初始化、活跃undefined存储共享、死声明删除、CompletedStore替代和
  存储合并后的全局收敛仍开放。原结构gate和阶段一Goal未完成；全量语料/
  RSS/DFX及代理保持暂停。

### 2026-10-07：规则180，共享活跃性驱动独立死写入消除

- RecoveryFlow新增写入occurrence与成功写入效果，native源码声明提交明确
  标为Initializer。CurrentFlow统一准备两类消费者的当前投影事实；
  PlanDefinitionElisions实际接入快照转发之后、存储合并之前，仅删除共享
  活跃性证明已死、且写入不可观察的普通赋值。RHS、返回值、异常和暂停
  仍在原位置执行，初始化与存储生命周期不随之删除。合并消费重建后的图。
- 私有性来自compiler-temporary身份和未逃逸事实，不检查名字/样本/同块/
  邻接/操作序列。成功写入仍经旧初始化分析供给；未声称新TDZ证明已完成。
  旧资源可观察契约保留表示：现有命名恢复可能需要分配包装，其分配等价
  尚未证明；按契约保留，不新增局部producer名单。当前语言模式运行新pass。
- 独立小图oracle覆盖175,616图的活跃性、定义—使用、干涉和死写入集合。
  新增84份源码/840组独立执行比较/2个错误变换反例，覆盖覆盖写入、分支
  合流、catch旧值、赋值返回值、native初始化与活跃undefined；含arena反转、
  200/201/400/800及exact/short预算、旧资源契约保留测试。
- 52项相关回归中51项初次通过，1项旧语法计数断言期望暂停内外都保留写入。
  核对私有内部写入确实已死后更新断言；stored-result两项重跑通过16.29秒，
  含480份源码、16,560行为比较、12错误变换反例。日志分别为
  /tmp/abcd-rule180-tests.log、/tmp/abcd-rule180-stored-tests.log。
- 复核并修复规则179的部分输出缺陷：早退未分配最终效果摘要，逗号表达式
  打印越界。新增partial/private-write/sequence反例复现退出139；现于早退
  发布完整的保守摘要，保留已知语句及UNRECOVERED。最终7项补充回归全部
  通过11.29秒，日志/tmp/abcd-rule180-final-tests.log；构建及重链接完成。
- pasteboard导出build/stage-one/dead-definitions-rule180为
  10complete/0partial/9noModule，十份源码node --check通过；仅EntryAbility
  正文变化：删除JSON.stringify(err)日志参数中的死$value2写入，求值与??
  回退保留。新增注册entryability_load_error_source_test，ABC-only生成后独立
  读取配对TS，54组receiver/调用顺序/nullish/异常比较通过。仅为该路径证据。
- 共同初始化、活跃undefined在存储合并后的保留、死声明删除、CompletedStore
  退出及delta收敛调度仍未完成。原结构gate和阶段一Goal保持开放；没有启动
  暂停的全量语料、RSS或DFX。

### 2026-10-07：规则179，效果与成功写入事实不再跨pass修改复用

- 定位并移除旧事实消费窗口：ForwardSnapshots改写替换后，Coalesce此前
  仍读取前一次RefreshProjection的evaluations/normal_stores。现在消费者
  对当前投影生成局部只读事实，存储方案提交后不再复用它们。
- RefreshProjection退出调用路径，替换为只负责分析→提案→批量提交的
  ElideStoredResults。AnalyzeEvaluations改为const并返回独立结果；
  OmitInertStores自行取得当前效果事实，最终摘要只在Finish前发布。
  内部pass不再读取plan的两份输出缓存，不靠手工刷新保证其新鲜度。
- 快照测试增加最终替换效果与成功写入事实性质，保留arena反转、
  200/201/几何规模与独立执行验证。构建日志/tmp/abcd-rule179-build.log；
  最终17项相关定向回归全部通过，日志/tmp/abcd-rule179-tests.log。
- pasteboard重新导出到build/stage-one/current-facts-rule179：
  10complete/0partial/9noModule，十份源码node --check通过，正文与规则178
  相同。没有新增实际record覆盖声明，暂停的全量测试/RSS未启动。
- 这解决一处实际顺序依赖，不是完整协调器。仍需共同初始化、独立死定义
  消除、存储与声明职责分离、基于delta的收敛调度；CompletedStore等过渡
  义务尚未被共同模型替代。阶段一Goal与结构gate均保持未完成。

### 2026-10-07：规则178，类名与打印共享 SourceVariables 准备结果

- 新增PrepareSourceVariables，以nullish → loop → binding的明确依赖顺序
  产生一个当前树的准备结果。TreePrinter不再自行调用三套pass；只消费
  同一结果中的语法/绑定事实。Direct先准备变量后分析类名，family先准备
  全部当前成员，再进行类名分析。结果在本次准备内只读，不作跨修改缓存。
- PlanPrintedClassNames借用当前打印单元的SourceVariables，不重算已提供
  owner的计划。按真实FunctionTree对象身份排序/二分匹配，不按名字、PC
  或图形状选择。借用事实预算由拥有者支付一次，消费者只计自身工作。
- 外部嵌套class body通过同一协调器临时准备；跨打印实例共享仍待完成。
  BindingInitializers内部初始化/多轮刷新仍未迁移，不能宣称完整pass管理器。
- class-name性质测试比较独立准备与共享准备的命名决定，并断言共享路径
  工作量减少，证明没有暗中重算。覆盖复制0/1/200/201/400/800、语句arena
  反转、参数/全局来源、遮蔽与不遮蔽；新增协调器exact/short预算验证。
- 最终50项相关定向回归全部通过261.93秒，日志/tmp/abcd-rule178-tests.log；
  含类名共享性质/独立源码行为、预算边界、捕获与默认参数、既定真实record。
- 单pasteboard重新导出到build/stage-one/shared-variables-rule178，
  10complete/0partial/9noModule；十份源码node --check通过，正文与规则177
  一致。相关CLI/fixture已重建，无新增实际record覆盖声明。
- 初步5项回归通过8.28秒，新增共享准备性质测试通过3.85秒。阶段一与
  架构里程碑继续进行；暂停事项和固定输入范围保持。

### 2026-10-07：规则177，发布消费者复用省略事实

- PrintedFunction增加typed GeneratedExtent，区分必须有正文的范围与已决定
  省略的边界；来自原恢复计划的打印事件，不由零长度反推许可。转发RHS在
  实际输出点重置为正文范围，新增数组计入打印和持久占用预算。
- FamilyEmission删除独立PlanNullishSyntax、DiscardedInvocationRead与
  ImplicitTailCompletion推断，只消费同一打印产物的范围种类并校验身份/
  来源索引/归属/跨度或边界。未标记空范围仍拒绝。原恢复义务未移除。
- 规则176记录的forwarder发布失败已修复，class_tree_context_test主入口
  定向运行通过。测试端保留独立恢复计划及尾部/可选链位置关系验证。
- 新增快照范围种类一致性检查发现循环头最后有正文节点后跳到结束，漏记
  后续省略根。现继续原顺序遍历并记录边界，不新增源码或准入条件；正反
  arena、200/201/400/800以及转发RHS的实际非空范围一并检验。
- 最终50项相关定向回归全部通过263.21秒，日志/tmp/abcd-rule177-tests.log；
  包括此前失败的class_tree_context主入口、可选链/空值回退独立源码执行、
  快照范围一致性、捕获/默认参数和既定真实record。未运行全套或扩大语料。
- 规则177相关CLI/fixture重建，单pasteboard导出到
  build/stage-one/projection-extents-rule177，10complete/0partial/9noModule；
  十份源码node --check通过，正文与规则176一致，无新增record覆盖声明。
- 尚未完成共同初始化、DefinitionElision、SourceVariables协调器或阶段一
  验收；这次是移除一个重复消费者决策，暂停事项与固定输入范围保持。

### 2026-10-07：规则176，保留最终语义验证的 TreeFlow 对应证据

- TreeSemantics把已完成flow/operand/origin检查的TreeFlow移交给构造结果；
  完整普通/异常树保留证据，明确partial树不伪造完整对应关系。
  VerifiedFunction拥有最终flow；默认参数改写树后重新验证并替换旧flow。
- family不再另跑VerifyTreeFlow/VerifyExceptionTreeFlow取得同一映射；
  CheckCaptures同样消费刚完成语义检查的flow，不重复构造。实际捕获、
  operand/origin、ABI与位置检查保持。保留容量以flow验证的树+scratch+
  对应证据预算作保守上界，没有重复把同一树计入持久预算。
- 新测试比较保留与新鲜重算的表达式/语句/snapshot/phi/cell映射，核对
  revision与partial缺证据边界；普通VerifiedFunction增加flow交接断言。
- 新发现既有失败：class_tree_context_test的异常forwarder用例正文正确，
  FamilyEmission::CheckPositions拒绝StmtId9的省略范围。本次版本和
  提交前75b93a59均在相同phased_family_emission.h:105中止；基线回溯
  /tmp/abcd-flow-baseline-backtrace.log，本次定位/源码正文日志分别为
  /tmp/abcd-flow-position-backtrace.log与/tmp/abcd-retained-flow-diagnostic.log。
  不取消位置校验、不作样本特例；后续需共享源码省略事实来处理。
- 35项相关定向回归全部通过138.92秒，日志/tmp/abcd-rule176-tests.log；
  上述已确认的class_tree_context_test主入口失败单独保留，未算作通过。
- 规则176相关CLI/fixture已重建，class_tree_context_test的snapshot与
  receiver-catch定向入口通过。单pasteboard导出到
  build/stage-one/retained-flow-rule176，10complete/0partial/9noModule；
  十份源码node --check通过，正文与规则175一致，无新增覆盖声明。
- 这只完成原对应证据的所有权交接；canonical RecoveryFlow仍未接入
  原SSA/效果与绑定初始化。CompletedStore、初始化和pass失效协调器仍待
  迁移，架构里程碑/阶段一未完成，暂停事项和固定输入范围保持。

### 2026-10-07：规则175，存储结果事实与删除计划分离

- 在规则174之上继续拆分：AnalyzeStoredResults为const事实生产者，
  PlanStoredResultElisions为const计划生产者，ApplyStoredResultElisions
  是唯一批量提交点，随后求值分析消费提交后的投影。计划阶段不能通过
  修改plan影响后续候选；空结果身份不作为等值证明。
- 修复跨轮normal_stores残留：每轮新建写入事实，已替换节点没有PutValue
  事实，提交新替换时也清除对应标记。stored_result_test新增最终投影
  约束，覆盖已有全部语义形式、正反arena和200/201/几何规模。
- 共同初始化仍是前置缺口：LegacyFlowProjection跳过已省略声明，且
  normal_stores仍由InitializedRead提供；不能直接删除CompletedStore，
  也不能拿空def-use证明初始undefined已死。后续需补齐canonical生命期/
  初始化定义，再由独立DefinitionElision决定保留原点写入。
- 最终28项相关回归全部通过136.07秒，日志/tmp/abcd-rule175-tests.log；
  包含新增旧事实失效断言及独立源码行为验证。
- 最终相关CLI/fixture已重建；单pasteboard导出到
  build/stage-one/stored-facts-rule175，10complete/0partial/9noModule。
  十份源码node --check通过，正文与规则174一致；不代表新增record覆盖。
- 当前没有完整版本化pass协调器，手工刷新和旧初始化决策仍未退出。
  本次不宣称架构里程碑或阶段一完成；暂停事项与固定输入范围保持。

### 2026-10-07：规则174，共享活跃性与分离的求值 pass

- FlowLiveness统一求解incoming活跃事实和live definitions；生产干涉消费者
  删除原有后向工作队列，只消费共同事实。FlowDefinitions提供按活跃性
  裁剪的前向到达定义/def-use；当前尚无变换消费者，不宣称初始化迁移完成。
- 原Evaluations拆分为固定投影上的存储结果分析/候选收集、批量替换提交、
  随后的AnalyzeEvaluations。分析求值不再修改replacements；内外冗余写入
  的批量动作保持RHS正常结果身份，不依赖候选应用顺序。未添加样本/同块/
  邻接准入。完整projection revision/失效协调器仍待实现，手工刷新仍存在。
- 独立小图oracle同时对照活跃性、到达定义、live definitions和干涉；包含
  环、自环、不可达节点、同点读后写。预算边界和200/201/400/800/1600
  几何规模及arena反转均在recovery_flow_test内。生产仍受既有预算约束。
- 拆分后的7项定向测试通过28.63秒，日志/tmp/abcd-pass-order-tests.log。
  覆盖stored-result/loop/interference的单元和独立源码行为，以及共同图。
- 最终规则174相关28项定向回归全部通过135.15秒，日志
  /tmp/abcd-rule174-final-tests.log；完整测试套件与扩大语料仍暂停。
- 相关CLI/fixture已按规则174重建；单pasteboard重新导出到
  build/stage-one/shared-facts-rule174，10complete/0partial/9noModule。
  十份源码均通过node --check，正文与规则173逐字相同；这是保持现有输出
  的证据，不是新增覆盖或阶段一独立验收。输入SHA与既定身份一致。
- 未退出Coalesce::CompletedStore、InitializedRead等旧决策。下一步继续
  共同初始化/原SSA对应、独立DefinitionElision、SourceVariables协调器；
  存储映射不得自行删除live undefined。阶段一与架构里程碑均未完成。

### 2026-10-07：规则173，统一变量恢复的首个图消费者迁移

- 架构契约：[unified-variables.md](unified-variables.md)；分步实施：
  [2026-10-07-unified-variables.md](../../superpowers/plans/2026-10-07-unified-variables.md)。
  用户设计直接授权实施，不重新请求方案/agent审批。当前Goal仍active。
  早期source-variable-recovery.md已标为历史路径，禁止扩大其同块/邻接规则。
- f656e763为规则172已验证迁移基线。本次提取RecoveryFlow与预算化scratch：
  source-side定义/事件/双向完成边/源码点映射有独立产物。参数body-entry、
  let undefined、普通initializer/assignment、catch、update成功写回显式定义；
  ++读取与求值及写回分点，truth/nullish的分支方向、throw/return/jump、
  await/yield恢复完成有typed边。normal scope enter/leave显式，非完整生命期。
- BindingInterference不再有Statement/Expression Build、syntax visitor、
  IntrinsicEvaluation或FunctionTree参数；它只消费RecoveryFlow。真正退出
  该消费者的是重复语法解释；尚未退出Coalesce::CompletedStore、
  InitializedRead、RootLifetime、native_ready等局部决策，已在代码/契约
  明确其义务和替代方，未追加新的准入条件。
- LegacyFlowProjection明确标记过渡：现有aliases/replacements/omitted/
  evaluations/normal_stores仍来自BindingInitializers。当前构造发生在旧
  printer preparation链中，尚未实现唯一canonical入口或跨consumer共享
  的完整变量计划。不能把此适配器包装成最终共同模型。
- 核对发现family已有保留TreeFlow（function_family.h）；普通VerifiedFunction
  没有同等保留。后续直接复用family证据并补齐普通入口，不能重复保存。
  原CFG/SSA/效果对应交接、property reference/capture事件、abrupt scope
  unwind、共同def-use/初始化/死定义、源码声明计划仍待完成。
- 独立三点/两绑定固定点oracle核对全部175616图的干涉，含自环、不可达
  和copy；未复用生产稀疏算法。事件测试核对undefined/参数/赋值/update、
  conditional/and/or/nullish分支、显式throw、await/yield恢复及预算边界。
  原149源码/23252行为/8变异和200/201/400/800规模沿新消费者通过。
  新work为1313/298418/298625/622162/1305730（1/200/201/400/800）。
- 最终相关CLI/fixture重建，28项定向全部通过117.01秒，日志
  /tmp/abcd-recovery-flow-final28-tests.log。单pasteboard导出
  build/stage-one/recovery-flow-rule173，10complete/0partial/9noModule；
  十份node --check通过，全部正文与规则172逐字相同，真实rich_text/
  progressbar及record合同/语言构造回归通过。无新增实际record覆盖声明。
- 下一工作从模型到共同分析继续：先完成原证据交接和binding/引用事件，
  建立定义—使用/初始化事实，再独立处理存储共享与活跃初始化保留，实际
  移除CompletedStore等已被覆盖判断。任务1仅source adapter/首消费者
  子步骤完成，不宣布架构或阶段一完成。全套/扩大语料/RSS/benchmark/
  DFX/调度/agents继续暂停；未push、未改输入。

### 2026-10-07：规则172迁移基线，私有存储的共同干涉分析

- 从5f1c9865继续，新增 FunctionTree 后向 may-liveness/copy-affinity 图；
  可抛求值和成功写回分点，含分支/loop/catch/await/yield/保守finally。
  替换链缓存解析最终读取；不按++、opcode、链长、同块或样本选择。
  storage map贯通printer/class-self/normal-result投影；冗余inert语句
  的原来源范围跟随owned children和转发目的，避免失去来源。
- 暂存的局部义务必须迁移：Coalesce内CompletedStore证明被删empty
  declaration初始undefined已死，应由共同定义—使用/死定义分析替代；
  InitializedRead/normal_stores证明TDZ与成功PutValue，由共同绑定生命期/
  完成模型替代；RootLifetime、declaration_controls/region/native_ready
  证明改名后作用域与初始化，由共同scope/lifetime计划替代。disjoint
  matching只是当前组干涉精度限制，不能当语言约束。
- 新144树变化+5规模源码、23252独立行为对照、8可解析错误合并变异通过。
  涵盖初始undefined、旧值观察、转换失败、更新后异常、await拒绝、
  generator throw/return与finally、capture/local/shadow、arena逆序和
  0/1/200/201/400/800空block。独立多copy对1/200/201/400/800 work
  为1157/278362/278469/582106/1225674；短byte/work预算返回typed错误。
  现有循环链回归覆盖1至800复制，最终一个原生loop声明，无新长度分支。
- CMake -j4重建两个CLI和相关fixture。最终27项定向全部通过114.97秒：
  新unit/source、binding/local/stored/snapshot/loop系列、class命名/声明/
  accessor、record合同、语言构造paired、progressbar/rich_text真实record。
  不是全套或扩大语料。日志/tmp/abcd-binding-final27-tests.log。
- 最终单input导出build/stage-one/binding-coalesce-rule172-verified，
  SHA256 854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e；
  10complete/0partial/9noModule，十份node --check通过。对规则171仅
  PasteBoard正文改变，两个for直接++自己的初始化变量，移除三个私有
  中转变量；独立rich_text行为和新增AST性质通过，其余九份正文不变。
- 过程中真实失败已修复：无返回型推断/测试枚举编译错误、初始化读与
  PutValue虚假异常边、非法temporary capture夹具、NaN无限循环测试输入、
  inert省略与for声明计划次序、旧范围/声明数量断言、转发复制链缺失的
  affinity与来源零位置。未把拒绝/INCOMPLETE当行为通过。
- 这是局部迁移基线。共同模型、独立小图oracle、一般定义恢复、旧判断
  退出以及阶段一结构/语义/风格独立验收未完成。全套、扩大语料、RSS/
  benchmark、DFX、调度与agents仍暂停；没有push、改输入或恢复这些事项。

### 2026-10-07：规则171，函数/类快照与实际命名目的贯通

- 从干净c887c4fb继续。先核对结构gate与默认参数路径：一般默认表达式
  受candidate、DefaultBody、signature独立验证及parameter scope共同约束，
  不能只删除printer检查；尚无证据表明它阻塞固定五输入，未扩大这个域。
- family命名预检从未省略的statement/parameter根开始，沿共同replacement
  一次遍历实际表达式归属。replacement清除隐式命名目的，和printer的
  GetValue envelope一致；assignment/property/field内部的原生目的仍
  保留。省略的旧store不再覆盖最终name决策，不引入function self binding。
- 直接FunctionExpr/ClassExpr快照复用原单次读取、执行/异常activation、
  观察顺序和词法环境证明，不再一律保留。可能增加computed-name wrapper
  分配时，严格计入resource failure的合同仍保留原快照；这是尚未证明
  无新增分配的实现边界。class-self沿规则170投影证明，无新的样本/
  opcode/同块/邻接/名称条件。每个family member有界工作栈，语法节点/
  槽位线性访问，child owner查找沿既有排序索引，内存/work计入原budget。
- 150个新函数快照组合覆盖六callable、五目的、五种runtime name；独立
  执行验证name descriptor、length、fresh identity、constructibility、
  async拒绝及generator next/throw。原1296命名组合、mutable capture、
  1/200/201/402 owner/work预算检查保留。新增六个合法错误命名mutation，
  合计60个命名变异。V1直接name为saved的原生初始化仍成功且保留存储，
  其他需要wrapper的V1结果在输出前拒绝；没有因优化新增严格合同失败。
- 两份真实decoded class定义进入共同FunctionTree的private snapshot，
  分别用合法/需包装的类名。独立执行检查类名、length、新class/prototype
  身份、constructor、accessor值、name descriptor和不可普通调用；没有
  从配对源码生成正文。此前class naming/heritage冲突回归均保持。
- 最终相关目标重建，11/11定向测试通过43.06秒：native names、class
  names/accessors/omissions/declarations、record合同、statement snapshot，
  以及实际ProgressBar、rich-text和语言构造paired。新function fixture
  初次单项通过3.14秒。修改JS语法检查及git diff --check通过。
- fresh build/stage-one/function-snapshot-rule171与规则169输入SHA一致，
  19records仍10complete/0partial/9noModule，十份mjs解析通过且正文全部
  逐字不变。本轮是共同恢复能力扩展，不计新增实际record或完整style验收。
  证据/tmp/abcd-projected-function-{build,test-build,test}.log、
  /tmp/abcd-function-snapshot-final-{build,tests}.log及export.log。
- Goal active；下一步仍须从固定输入的实际结构/正文/style缺口推进，不能
  用这个组件通过关闭fields/globals/signatures/body或阶段一gate。full
  suite、expanded corpus、RSS/benchmark、DFX、scheduler、agents与周期
  audit保持暂停；输入未修改。


### 2026-10-07：规则170，按打印投影证明类名作用域

- 从干净0f71ae79继续。检查规则169后的命名边界，复现私有`saved = C`
  在唯一heritage读取处转发后，canonical class-name分析仍允许新增
  `class C extends C`的问题；新增self会让原外部父类读取触发TDZ。
  成功构建旧实现后，新回归在该期望处RED退出134；没有删除转发能力。
- 新PlanPrintedClassNames复用共同nullish/loop/binding投影，遍历省略
  statement之后的实际根及replacement边；alias读检查最终SymbolId，
  literal替换不再当成绑定引用。完整family、class成员及静态初始化
  都按每个owner建立投影；同一词法作用域/capture算法检查新增self。
  不能安全命名时沿原computed-property NamedEvaluation打印，保留
  runtime name且不新增遮蔽。canonical分析仍供原词法分析使用。
- 无样本/名称常量/操作码序列/同块/邻接准入。规划scratch、结果和work
  全部计入原budget；每个owner一次规划，没有逐class子树重放。新增
  遍历线性，沿用各共同planner的原索引复杂度；复制规模400→800 work
  小于3倍。未开启直接function/class快照转发，它仍缺完整命名目的证明。
- class_names_test新增48组：global快照/parameter别名、冲突/非冲突、
  正反statement arena、复制0/1/200/201/400/800；验证真实replacement/
  alias与命名结论，每组检验work/bytes少一预算失败。原dense类嵌套、
  capture/heritage/partial body检查保留。
- 完整decoded record独立验收增加同名import父类：三个profile×三种
  copy数量×两种分块，共18条新增完整record。含原有场景的最终计数
  为72 records、612行为、7个错误变异；覆盖父类Proxy、bound、null、
  无效父类、构造返回对象、原异常身份、动态替换父类、prototype及
  new.target。强加同名class-self的合法语法变异被检出。
- 首批三个相关测试通过；规模测试通过；新增record脚本第一次因参数
  替换误改另一函数断言而失败，修正后class_declarations_source_test
  通过1.88秒。最终相关目标全部重建，13/13定向测试通过41.09秒，含
  class命名/成员/partial omissions、record import/合同、native function
  names、statement snapshot，以及真实ProgressBar、rich-text和显式语言
  构造paired验收。未运行full suite或新一轮固定五输入多模式导出。
- 证据/tmp/abcd-projected-class-names-{red-build,red,build,tests,scale-build,
  scale}.log、/tmp/abcd-projected-class-record-{build,tests}.log及
  /tmp/abcd-projected-class-final-{build,tests}.log。Goal仍active；fields/
  globals/signatures/body整合、一般SSA coalescing、完整record语义/style
  gate未完成。full suite、expanded corpus、RSS/benchmark、DFX、scheduler、
  agents及周期audit继续暂停，输入未修改。


### 2026-10-07：规则169，共同语法树的有序快照转发

- 从干净b251e26a继续，规则168已提交且属实际进展。本轮检查真实三个
  loop条件快照，确认coercing比较有throw义务，normal ValueBranch没有
  对应exception dispatch；未删除phase verifier/SameCatch检查。
- 在已验证FunctionTree的BindingInitializers中复用首次完成、捕获、
  词法容器及表达式完成序，转发仅有一次实际读取的私有statement快照。
  先处理原alias/literal投影，再索引保留的读取/效果、控制/初始化事件
  和执行region。生产者与读取须有同一控制/异常/迭代activation，无
  中间状态观察，且词法环境签名相同。绑定读取也算观察，不能把会改
  状态的RHS搬过先求值的参数。无opcode/PC/register/同块/样本匹配。
- 空作用域折叠到最近有绑定的环境；原声明与可能生成的native initializer
  作用域均计入，保护TDZ遮蔽和初始化后缩窄的local。已证明消失的copy
  声明不再形成初始化屏障，交错声明/copy链与集中声明用同一流程。
  这是保守环境等价证明，未证明不相干绑定可交换时仍可能保留快照。
- 原RHS只在实际读取处出现一次，保留GetValue、receiver、typeof未解析
  名称异常、原转换/抛出/暂停顺序。直接function/class definition的
  NamedEvaluation尚缺投影后的命名目的证明，保留这些绑定；这是表示
  覆盖缺口。canonical树、phase异常模型、默认及显式语言合同不变。
- 同一replacement表串联转发；printer用child prefix counts跳过loop
  expression program中已消失的根。部分输出的未优化路径也初始化
  counts，不生成空逗号操作数。移走RHS保留实际表达式范围，避免for
  update先打印时被随后body的省略点覆盖。新增索引/两次有界完成扫描
  为O(E+S+B)时间和空间，按原budget计入；无fixed point/逐变量图遍历。
  rules168升169，无新writer、DFX或发布准入设施。
- statement_snapshot_test现有19种组合×六规模0/1/200/201/400/800×
  正反statement arena，共228份完整源码；覆盖空块拆分、复制链及交错
  声明、循环/条件、源Local、早期return、guarded读取、catch内外/旧值、
  TDZ遮蔽、native local缩窄、await/yield与typeof。400→800 census work
  小于3倍；旧实现成功构建后正例RED退出134。独立JS按这些维度执行
  8724组行为，8个有效错误变异检出；对象/原异常值显式身份比较，AST
  单独检查私有snapshot是否移除，RHS实际范围保持非空。
- 首批20项17通过，binding fixture仍要求单用allocation保留saved，
  main fixture仍要求单用return保留const，loop init仍要求每个copy有
  声明，导致3个目标的旧表示断言失败。按新合法表示更新这些断言，
  仍检查allocation每次新身份、每次求值、loop carried binding及所有
  原行为/负变异。省略assignment才允许零范围，未放宽其他表达式范围。
- 最终相关目标重建，20/20定向通过95.79秒：新snapshot、规则168、
  binding/local、loop entry/transition/syntax、native function names、
  partial callback，以及实际rich-text/ProgressBar/Home/PageA/EntryAbility。
  另language_constructors_paired_test通过3.20秒，共21个不同通过目标。
  中间针对修复的5项5/5通过45.22秒。未启动full suite或扩大语料。
- fresh ABC-only build/stage-one/statement-snapshot-rule169-final与规则168
  输入SHA一致，仍19records/219methods、10complete/0partial/9noModule，
  十份mjs均通过语法检查。七份正文变化：EntryAbility私有value/phi声明
  8→5，Home9→8，PageA4→2，PageB4→2，PasteBoard43→35，ProgressBar
  15→12，ShareDitect3→1，共少21项；另外三份正文逐字不变。三个真实
  for条件由保存/读取序列恢复为直接BinaryExpression `<`，富文本测试
  明确验证这个独立表示要求。计数不是整体语义/风格完成的替代证据。
- 证据/tmp/abcd-statement-snapshot-{red-build,red,final-build,repairs,
  final,constructors,final-export}.log；git diff --check、修改JS语法检查
  通过，输入未改。Goal active：一般SSA coalescing、loop update搬运、
  更广的命名目的/环境交换、其余record语义/style和原fields/globals/
  signature/body整合gate仍开放。full suite、expanded corpus、RSS/
  benchmark、DFX、scheduler、agents及周期audit继续暂停。


### 2026-10-07：规则168，消除已保存表达式结果的重复写入

- 从干净e0834016继续。上一轮只复核已有语言构造模式，没有推进功能；
  本轮实际检查规则167 PasteBoard输出，确认循环更新含
  `t = ((t = i), ++t)`，其外层写入重复保存右侧已经写入的同一结果。
- 在既有BindingInitializers完成序中增加normal-completion结果身份：
  可写声明式绑定的读取/赋值、prefix update、sequence右侧，以及结果
  身份一致的conditional/logical路径，共同传播“结果仍存于此绑定”。
  只有目标已保存同一结果且PutValue无外部hook时，原赋值投影到原RHS。
  RHS不移动/复制；TDZ、原异常、转换、求值次数与暂停保持。成员setter、
  postfix旧值、未知调用/await/yield结果仍保留写入；暂停后内层赋值
  可以重新建立事实。未按opcode序列、同块、样本、临时变量名或图形准入。
- 使用既有ExprId replacement与后序遍历；增加O(E)可选SymbolId存储，
  由原TreeCheckBudget计入。无逐变量图扫描/fixed point/新writer；
  canonical AST、默认合同及显式数值/构造模式不变。rules167升168。
  import/capture/self的可写性尚无此处证明，保守保留是实现覆盖缺口，
  不是语言禁止重复写入消除的规则。
- 新stored_result_test覆盖20种组合、源Local/CompilerTemporary、正反
  表达式arena及0/1/200/201/400/800效果前缀，共480份完整生成函数；
  400→800 work小于3倍。含声明前读取、member getter/setter、分支不同
  身份、postfix、暂停前后赋值。旧生产实现先成功构建，正例断言RED
  退出134；最初fixture的MemberKey拼写编译错误未计为RED。
- 独立JS程序对照16560组行为，分别检查AST赋值数量、返回/原对象身份、
  Number/BigInt/NaN/-0/Symbol/coercion、任意throw值、thenable/rejection、
  generator next/throw/return。对象显式按身份编码，不能靠空对象deepEqual
  混过。12个有效JS错误变异（漏调用、prefix变postfix、错setter值）检出。
- abc_structure/abc_semantic及相关fixture构建通过。15项定向15/15通过
  105.14秒，包括binding/local初始化、loop syntax/entry/transition、
  predicate partition和实际ProgressBar/rich-text行为。新增暂停组合后
  两项2/2通过7.97秒；加强身份/任意异常值后source项再通过4.36秒。
  实际rich-text另加AST表示断言，确认prefix更新不被冗余赋值包裹。
- fresh ABC-only build/stage-one/stored-result-rule168：与规则167输入SHA
  一致，仍19records/219methods，10complete/0partial/9noModule。
  只有PasteBoard正文变化，两个loop update分别少一次冗余赋值，另外
  九份逐字一致。循环条件快照、phi搬运仍在；本次不是SSA整体消除。
  证据/tmp/abcd-stored-result-{red-build,red,rule168-build,targeted,final,
  identity,export}.log。git diff --check与修改JS语法检查通过，输入未改。
- Goal active。继续共同SSA存储/循环条件表示、其余record独立语义/风格
  和原fields/globals/signature/body整合门槛；不以本次局部表示改善宣布
  阶段一完成。full suite、expanded corpus、RSS/benchmark、DFX、scheduler、
  agents及周期audit继续暂停。

### 2026-10-07：规则167，record 输出保留实际语义合同

- 从干净3ac417ca继续；上一轮规则166实际条件表示改进属于进展。本轮
  查实共同record输出的合同传递缺陷：BuildRecordTree按identity.contract
  构建完整family后，PrintedSource仍默认kSourceContract，导致显式语言
  模式的record manifest被标为language_and_host_v3。顶层manifest选择
  正确，不能替代artifact本身正确。这会误导后续消费者的语义验收边界。
- 输出现在直接携带实际identity.contract，完整及incomplete共用一个
  返回路径；abc_semantic的ValueTree record JSON也返回source_contract，
  采用现有typed enum边界映射。未从flags/诊断文本猜标签，没有改变默认
  数值/构造规则、函数正文、恢复准入或executable状态。rules166升167。
- record_import_tree_test新增--contracts：三profile、四种入口/函数族/
  class/capture结构、六种CLI语义组合共72个结果逐一核对typed contract。
  不涉数值/构造差异的中性fixture正文还需与该profile默认正文完全一致。
  所有既有成功返回也增加identity对应断言；旧实现构建成功后断言RED
  退出134，修复后通过。
- 实际language_constructors_paired_test除原十构造器独立行为外，新增同
  一真实record的六模式：无constructors为明确partial、有为complete；
  artifact合同、标记和JS语法逐一验证，参考TS仍只在全部ABC-only生成
  结束后读取。rich-text核对导出manifest中每份源码的record合同与顶层
  所选合同一致；partial callback与named complete两种结果也独立核对。
- record_import_tree/abc_structure/abc_semantic构建通过，首批实际8项
  8/8通过22.11秒。补跑method_cell_names发现旧mutation仅匹配独立store，
  规则164后的let initializer未命中；改用已有AST选择器同时覆盖两种
  初始化表示，保留原capture错误行为检测，相关两项2/2通过0.56秒。
  新六模式的language_constructors_paired_test另通过3.09秒。最终9个
  不同定向项有通过证据，包括默认CLI拒绝、class declarations和record
  imports；不存在的旧selector未计为已运行项。
- fresh ABC-only build/stage-one/record-contract-rule167：十份源码正文
  与规则166逐字一致，输入SHA相同，十份artifact合同均为实际选择的
  language_numeric_constructors_v1；仍10complete/0partial/9noModule。
  这是语义合同集成修复，不增加源码覆盖或表示改善数量。证据
  /tmp/abcd-record-contract-{red-build,red,build,targeted,final,callback-final,
  export}.log，git diff --check及修改JS的node --check通过，输入未改。
- Goal active。其余record独立语义/表示、一般SSA及原fields/globals/
  signature/body门槛仍未完成；full suite、expanded corpus、RSS/benchmark、
  DFX、scheduler、agents及周期audit继续暂停。未扩展来源位置映射。


### 2026-10-07：规则166，条件表达式的共同效果分区

- 从干净b37cbeb5继续；规则165实际await表示改善已提交，属于进展。
  本轮定位phase planner的贪心边界：getter先合入纯比较，branch因无
  异常dispatch不能承接整个effect subtree，导致纯比较也物化。第一遍
  现允许branch进入临时候选forest，随后标记包含effect的控制root，并
  复用原有一次有界retry分区。throwing producer留在原位置，纯比较/
  布尔后缀重新合入条件；最终forest仍禁止branch的effectful child。
- 只修改共同BuildPhaseExpressions候选及拒绝分区，不删除验证约束。
  单次def-use、availability、effect suffix、SameCatch/exception copies
  和独立VerifyPhaseExpressions保持；无源文本、样本、指令序列、数量、
  PC/register/新同块准入。原两遍调度/线性root替换与分区沿用，无fixed
  point、额外逐变量图遍历或新IR。rules165升166；numeric/constructor
  合同保持；一般跨分支effect/控制覆盖不在本次宣称范围。
- predicate_partition_test使用三profile、两register layout、六规模
  0/1/200/201/400/800 copy与纯wrapper、五种语义情况，共180真实解码/
  phased import/树语义校验/打印源码。覆盖原catch、仅getter被catch、
  中间effect、可coerce比较和multiple-use。纯后缀可合成，coercing原
  effect及多用条件仍保留；每个适用输入都显式检出一次伪造getter融合，
  无空mutation循环。400/800的planner work小于前一规模3倍。
- 独立JS reference从语言行为构造，不输入引擎。4140组返回/任意原异常/
  getter次数/顺序/valueOf与原对象身份对照；66个有效JS错误变异（遗漏/
  重复getter、错误strict coercion）检出。测试接线的JSON依赖与导入
  fixture截取已修正；有效旧HEAD构建后新正例RED退出-6，恢复生产修复
  后通过，不能把编译错误算RED。初次2/2定向21.29秒通过。
- rule166重建abc_structure/abc_semantic及相关fixture；13项定向13/13
  通过62.17秒，包括新decoded/source、phase references/copies、规则165
  suspension、真实富文本/ProgressBar/PageA、await/finally、generator/
  finally。rich-text原先残留的临时编号mutation改用既有AST选择器，原
  错误行为仍必须检出；未降低运行时比较。额外变化record另跑Home与
  EntryAbility独立回归，2/2通过3.22秒；共15个不同定向项通过。
- fresh ABC-only build/stage-one/predicate-partition-rule166仍19records/
  219methods，10complete/0partial/9noModule，十份mjs解析且无缺口；
  EntryAbility私有const $value声明7→5、Home7→6、PasteBoard32→27、
  ProgressBar12→11，共少9项。恢复if(result===value)、MIME比较和原生
  truthiness条件，throwing getter/调用保留自己的原执行点；其他六份
  正文不变。不以字符减少或complete数量替代整体表示/语义验收。
- 日志/tmp/abcd-predicate-partition-{red,unit-build,focused,behavior,
  rule166-build,targeted,remaining-records,rule166-export}.log；输入未改，
  git diff --check通过。阶段一Goal active；一般SSA存储、剩余record
  独立验收及原fields/globals/signature/body gate仍开放。full suite、
  expanded corpus、RSS/benchmark、DFX、scheduler、agents与周期audit
  继续暂停。


### 2026-10-07：规则165，暂停前操作数的共同表达式合成

- 从干净96df5688继续；上一轮HomeModel独立验收已提交，属于实际进展。
  本轮定位真实PageA/PageB/PasteBoard await调用前私有快照：共同
  PhaseCorridors::ConsumesOperands一律拒绝SourceEvaluation::kSuspending。
  依据语言求值顺序，await/yield先求operand再暂停，故允许eager operand
  消费既有effect suffix。暂停本身不进入Expression可搬移域，仍在原root。
  单次def-use、availability、effect interval与最终root SameCatch/异常
  快照校验保持；没有样本、opcode序列、PC、寄存器或新同块/邻接选择。
- 完全复用原EvaluationPlan/FunctionTree/printer与独立verifier，不增加
  IR、writer或额外图遍历，既有复杂度/预算不变。原PhaseCorridors覆盖
  边界仍存在，本次没有宣称一般跨分支/循环或跨暂停搬移支持。rules164
  升165，默认数值/构造合同保持。
- 新suspension_operands_test以await/yield、0/1/200/201/400/800空块
  split及相同环境/中间effect/handler变化/multiple-use共48个forest验证。
  正例可合成，后三类保留快照；每例再伪造移动暂停节点，独立verifier
  均拒绝。后三区域伪造融合也拒绝，400/800的work小于前一规模3倍。
  测试先修正kTypeOf枚举拼写；随后旧HEAD生产代码构建成功，正例断言
  RED退出-6，恢复新实现后通过。该单元验证forest，实际解码/源码执行
  由下面既有file/corpus回归覆盖，不把合成graph冒充ABC完整验收。
- rich-text测试增加AST独立检查三个record的原生await调用operand。
  第一轮11项9通过41.29秒；两失败均为旧mutation依赖临时变量编号。
  PageA移除await与PasteBoard错误HTML controller变异改用现有AST
  def-use选择器，保留原错误行为检测，不放宽运行时比较。修复后两项
  2/2通过12.89秒，原11项均有当前通过证据，覆盖phase references、
  async capture/arrow/finally/suspending-finally、generator/finally及
  async-generator await。另实际ProgressBar表示变化触发其独立回归，
  1/1通过7.32秒；共12个不同定向项最终通过。
- fresh ABC-only build/stage-one/suspension-operands-rule165仍19records/
  219methods，10complete/0partial/9noModule，ValueTree且无legacy；十份
  mjs均解析、无INCOMPLETE/UNRECOVERED。四份正文表示改善，const $value
  声明PageA3→2、PageB3→2、PasteBoard38→32、ProgressBar14→12，共少
  10项私有快照；如await getSystemPasteboard().setData(data)直接恢复。
  其余正文不变，不以字符减少或完整数量替代语义/表示验收。
- abc_structure/abc_semantic及新单元/phase_reference/loop_closure_file
  构建通过；证据/tmp/abcd-suspension-operands-{red,unit-build,rule165-build,
  targeted,final-paired,progressbar,rule165-export}.log。全套与更大语料
  未运行，输入未改动；git diff --check通过。Goal active，原fields/
  globals/signature/body gate、其余record和一般SSA表示仍开放；full
  suite、expanded corpus、RSS/benchmark、DFX、scheduler、agents及周期
  audit继续暂停。


### 2026-10-07：HomeModel 回调、成员调用及构造赋值独立验收

- 从干净7e61f9b4继续。上一轮仅复核已有构造模式，按无进展处理；本轮
  补齐真实record16474的回调观察缺口，未改变生产恢复或rules164。原
  HomeModel测试覆盖正常四record连接，但未覆盖延迟重复callback与资源
  getter期间的callee/live import变化。复用该测试和独立配对源码，不建
  新报告层；ABC SHA/size与manifest身份核对，生成进程结束后才读参考。
- 新homemodel_observation实际执行完整生成initTabBarList、getTabBarList
  与arrow。20组0/1/3/200/201、稀疏、同步/逆序延迟/重复回调检查；borrowed
  receiver与错误callback receiver保留lexical this。icon getter期间换绑
  HomeConstants并更换push，验证已取得callee不重读、随后调用读新callee，
  activeIcon读取新的live import。逐项保留对象/资源身份、fresh item、
  属性顺序/descriptor、方法name/arity与结果；不靠空对象deepEqual判身份。
- 延迟重复场景46个实际观察逐点注入undefined/null/0/Symbol/object，
  共230组原异常/partial item状态对照；断言指定fault确实到达。另25组
  构造检查以独立pinned PutValue顺序验证new.target prototype读取、字段
  setter、init getter/call及各点原异常；不拿Node DefineField作为该边界
  oracle。构造里的动态方法查找在这个子测试为受控边界，真实方法正文由
  前述配对执行及原四record测试覆盖。
- AST单独验证native class/method、map/arrow/push/object/array表示；复用
  已固定Acorn8.15.0的class_reference，CTest增加--expose-internals。
  三个callback/resource有效JS错误变异及一个DefineField替代assignment
  变异均检出。仅测试内用AST选择变异点；生产无样本、名称、图形或同块匹配。
- abc_structure构建通过，HomeModel、shopping十record及record import
  三项3/3通过26.51秒；最后新增constructor mutation后HomeModel1/1通过
  2.33秒。node --check与git diff --check通过。日志
  /tmp/abcd-homemodel-{build,targeted,observation-first,observation-final,
  mutation-final}.log。默认ValueTree当前生成，输入未改动。
- 这是HomeModel独立行为/表示证据扩充，不增加源码覆盖数量，不以受控
  依赖测试宣布完整平台或阶段一完成。其余record全正文/表示、一般SSA
  存储及原fields/globals/signature/body gate仍开放。Goal active；full
  suite、expanded corpus、RSS/benchmark、DFX、scheduler、agents及周期
  audit继续暂停。源码位置映射缺口已确认但不作为本轮功能前置工作。


### 2026-10-07：规则164，源级局部变量及捕获 cell 的初始化声明

- 从干净5d39ca87继续；上一轮完成四ViewModel验收，属于实际进展。
  BindingInitializers将空let的Local接入原first-completion/全部观察/
  lexical confinement证明，在原赋值发生处输出初始化声明。真实let
  保持let、名字与存储，不进入私有constant/copy删除域；没有移动RHS、
  回放效果或按样本/名字/opcode/register/PC/同块/邻接选择恢复。
- FunctionExpr与class constructor/member/defined-member/static initializer
  的实际capture边在创建处进入同一观察census。创建早于首次store或在
  RHS内的capture保留旧声明；子函数可写，不能从父函数零写推断const。
  逃逸cell还必须保留原ScopeId的activation：外层loop共享cell不移成
  每轮cell，原loop体本来每轮创建的cell可就地初始化。此检查证明实际
  环境身份，不是CFG同块条件；nonescaping Local复用原跨嵌套scope证明。
  module aliases继续保留原可观察初始化，未知外部读不能局部消除。
- 原共同printer与origin/anchor路径直接消费已有BindingInitializer，
  未新增writer或最终IR。投影观察容量按expressions与closure/class
  capture边计，不能用expression数量限制一个creation的捕获数。增加
  O(E+C)计数/预算化空间，原census整体仍按输入节点与capture边线性扫描；
  C为实际capture边，不逐变量遍历图。来源范围和资源边界保持。
- 新local_initializer_test先在旧规则163有效树的源Local初始化断言RED。
  十六语义族、0/1/200/201/400/800与正反arena共192完整生成源，通过
  7596独立JS行为比较及16有效JS错误变异。包括旧值/自读/捕获创建、
  child写入、共享与每轮loop环境、嵌套scope、finally、await job/reject、
  yield next/throw/return；原对象身份逐叶比较。另五个1/200/201/400/800
  多cell捕获的实际loop条件投影验证容量、exact budget与线性work增长。
  原三profile72实际cell解码继续通过，source Local已用let initializer。
- abc_structure/abc_semantic及相关fixture最终构建通过。新注册target首
  次需重新configure；容量计数lambda的optional类型修正后构建通过。
  首批5项4通过，binding JS旧独立store位置假设失败，改为在原生声明
  initializer上执行原副作用变异后binding/local三项3/3通过25.82秒。
  相关17项15通过75.91秒；失败为loop Local准入旧断言及class named
  cell空声明旧断言。修正后loop/local source两项2/2通过5.39秒，module
  unit/source两项2/2通过3.37秒。最终23个不同定向项均有当前通过证据，
  包括shopping十record、rich-text/ProgressBar、class captures/names、
  module live binding、loop/await capture和snapshot。误写的不存在
  class_declaration_test构建目标未当成功；实际class_declarations_source_test
  依赖record_import_tree_test，已正确构建并通过。全套未运行。
- fresh ABC-only build/stage-one/local-initializers-rule164：pasteboard仍
  19 records/219 methods，10complete/0partial/9noModule，显式numeric+
  constructors、ValueTree且无legacy。十份mjs独立解析且无缺口标记；
  七份有表示改善，声明总数140保持，空声明45→18、初始化95→122。
  mutableStyledString1、captured this/formal、ProgressBar内部模块变量
  与Home类绑定在原初始化点恢复let，不宣称所有名称/SSA或style已完成。
- 证据/tmp/abcd-local-initializer-{red,red-build,build-fixed,focused,
  final-binding,regressions,final-local,final-module,behavior,export,artifacts}.log；
  node --check与git diff --check通过。初版独立oracle深比较把两个空对象
  当相同，已由共享loop错误变异暴露并修正为身份比较；最终16变异全检出。
  下一步继续其余record独立语义/style、一般SSA存储与原fields/globals/
  signature/body gate。Goal active；full suite、expanded corpus、RSS/
  benchmark、DFX、scheduler、agents和周期audit继续暂停，输入未改动。

### 2026-10-07：四个 shopping ViewModel 与十个真实 record 的完整连接验收

- 从干净e2f0f6b5继续，上一轮属于实际进展。本轮生产/rules163保持，
  完成尚缺的独立整record验收，未添加恢复准入或样本生产分派。现有
  paired index的阶段一分母确为五ABC，实际目录没有额外ABC；只读校验
  五文件SHA/size均匹配既有dynamic/profile身份。106/0是历史显式
  数值+构造模式的导出证据，不冒充当前完整paired语义/表示通过。
- 新shopping_viewmodels_source_test先以ABC-only显式numeric+
  constructors完成Immersive导出并核对manifest SHA/ValueTree/合同/
  十record complete及无legacy，进程结束后才读取paired TS。共用
  classReference只擦除type-only无initializer字段，保留全部初始化和
  方法正文。真实Home/IconInfo/Recommend/Section四ViewModel，四Model、
  HomeConstants与Breakpoint共十record同ESM图执行；commons barrel
  为受控forwarding边界，WidthBreakpoint仍为外部受控定义，不计整个
  commons或真实平台验收。四ViewModel及本图可达的ctor/initData/map/
  new item/push/getter/slice正文实际执行，未用host替换方法正文。
- 四ViewModel的module TDZ/重复evaluate/export图、class/方法name/arity/
  descriptor、实例/模型/列表fresh与持有身份、原资源/字段完整graph和
  partial模型状态独立比较。22 ordinary/sparse与count边界场景；一个
  baseline的512实际观察逐项注入undefined/null/0/Symbol/object原异常，
  共2560 paired fault场景通过。每次保留原异常身份，错误结果不能靠
  双方提前throw算成功。-0/NaN/Infinity、201、BigInt/Symbol拒绝、count
  coercion及多实例回调/列表持有均覆盖。
- 416独立pinned PutValue检查覆盖class/foreign/两realm fallback、201
  ignored实参、constructor trap/prototype/field setter的顺序与原异常，
  prototype getter期间换绑保留已取得callee，后续构造读新导入。它们
  不拿Node原生class字段DefineField当Ark assignment的oracle。八个
  有效JS mutation检出错误callee/field/count/receiver/list与字段定义
  替代赋值。Source AST另核对原生class、完整公开方法及构造初始化；
  alpha-normalize参数/import，四份等价参数/Peer alias/let-class表示
  同时通过同一style与完整graph行为，未把逐字一致作为验收标准。
- records28887/29119/29382/29619四ViewModel的受控whole-record语义与
  高层表示由pending转accepted；旧两ViewModel 1603 boundary/1199
  connected和三个Model的2481行为/九mutation继续保留。本轮新增
  Home/Recommend真实peer连接、十record literal/export状态与四ViewModel
  表示验收；不宣称十个peer全部方法、设备或阶段一整体完成。
- abc_structure/abc_semantic构建退出0，九项相关定向9/9通过54.35秒，
  包括旧model/viewmodel/Home graph、classReference与显式构造验收。
  Alpha-normalized style补强后新注册项1/1通过23.15秒，最后四份等价
  表示的独立命令也通过；最终注册项1/1通过22.76秒，node --check与
  git diff --check通过。证据
  /tmp/abcd-shopping-viewmodels-{build,first,final-behavior,targeted,
  style-final,equivalence-final,final-test}.log。fresh保留产物
  build/stage-one/shopping-models-rule163，生产源码覆盖/完整数量不变。
- 下一步继续其他尚缺独立whole-record语义/表示的record与原实例字段/
  globals/完整signature/body结构gate；精确SSA/原变量/每轮capture、
  其余body/style仍开放。Goal active；full suite、expanded corpus、
  RSS/benchmark、DFX、scheduler、agents及周期audit继续暂停，未重复
  固定五ABC的六模式导出，输入未改动。

### 2026-10-07：规则163，共同表达式观察省略纯逗号左值

- 从干净3331b471继续，Goal active；已获准且已实现的显式数值/
  构造模式保持。BindingInitializers在原完成census记录postorder，
  以typed expression/operator穷尽语义和child完成汇总ExpressionEvaluation。
  初始化binding读取复用原声明先后/lexical与execution包含以及copy
  证明；body-entry参数/self/catch和own ordinary this按语言初始化。
  mutation不使已初始化读取成为hook；global/member/未知coercion、
  TDZ、写、分配及await/yield保持，不增加样本/名字/opcode/producer
  名单、同块/邻接、PC/register或数量匹配。
- printer把纯comma left投影到原right，保留原发生点、parent grammar
  和value-result类别；typeof unresolved global仍GetValue，原value
  call保留indirect/receiverless语义。function/class命名目的保留必要
  comma envelope，不引入新name/self；原AST/ID/origin/anchor不改。
  纯省略子树通过迭代事件映射到非零normalized result span，不新增
  transport准入或zero-range豁免。摘要/postorder新增预算化O(E)空间、
  O(E+owned slots)工作，省略子树disjoint ownership保证打印线性。
- 十九组0/1/200/201/400/800正反arena新增228完整树/源，原1478源
  保持，共1706完整生成源、457128独立原生JS行为比较和270有效JS
  mutation通过。覆盖pure operators/choice、mutable入口读取、local
  完成/TDZ、getter/coercion、unresolved/global accessor、await job/
  rejection和yield next/throw/return；原72三profile实际cell解码及
  全binding oracle保持。一般默认表达式仍被既有kUnsupportedSyntax
  拒绝，新增负例记录此表示缺口，未放宽规则。命名fixture扩展至
  1296场景，54有效mutation证明去除必要comma会改变name；class
  let/const也覆盖value projection。platformExecution:false。
- 有效旧规则162的源码style断言RED：仍有SequenceExpression实参，
  /tmp/abcd-sequence-red.log。新增default fixture曾错误要求成功，
  改为明确拒绝断言；mutation曾误选callee comma和未捕获generator
  提前完成断言，修正测试选择/异步mutation检测，生产检查未弱化。
  abc_structure/abc_semantic及十四fixture最终构建退出0；新增测试
  使用原已锁Acorn解析器，首次30项定向29通过/1因新增import缺少
  --expose-internals失败，总154.50秒。修复CMake测试启动参数后
  native names和binding两项共3/3补验通过40.38秒；其余29项最终
  生产语义代码已通过，包括loop/snapshot/module/structure/numeric/
  class/control/family、语言构造实际十record、Push/ProgressBar/富文本。
- fresh ABC-only build/stage-one/sequence-pasteboard-verified-final仍19 records/
  219 methods，10complete/0partial/9noModule、value_tree/无legacy，
  顶层contract language_numeric_constructors_v1。十mjs独立解析且无
  INCOMPLETE/UNRECOVERED，与规则162逐字节一致；总声明140/空45/
  初始化95/const87/sequence28，PasteBoard62/16/46/42/sequence13，
  三个native for let保持。本轮无新增actual record coverage或style
  数量改善；受控新树的纯复制实参已去掉机械comma。全body/style与
  阶段一验收不能用这些局部通过代替。
  最后重导出曾被已有destination保护拒绝，退出1/error10；使用新的
  输出目录复验，不覆盖此前产物、不修改输入。
- 证据/tmp/abcd-sequence-{final-build-final,final-registration-build,
  fixture-final,behavior-final,names-final,targeted,repaired,export-final,
  style-final}.log；node --check与git diff --check通过。接下来继续
  剩余普通INCOMPLETE/UNRECOVERED与结构/signature/body gate；共同
  更精确reaching definitions/SSA干涉、原变量/每轮capture、unknown
  capture/module和一般dead statement/right pruning仍开放。full suite、
  expanded corpus、RSS/benchmark、DFX、scheduler、agents及周期audit
  继续暂停，固定五ABC106/0未重跑。输入未改动。

### 2026-10-07：规则162，表达式内赋值接入共同完成与复制传播

- 从干净9db586fd继续，上一轮属于实际进展。BindingInitializers的
  同一census按shared expression child ownership的语言次序观察，
  LIFO入栈children反转保证callee/argument/RHS从左到右，RHS完成后
  才记identifier PutValue。表达式内store的ExprId进入原constant/
  copy worklists，不再以独立ExpressionStmt作为存储共享准入。
  单写/first completion、执行区间全部观察、escape和共同词法根解析
  保护可用性、身份、次数及原效果/异常/暂停。无opcode/producer名单、
  样本/名字、PC/register、同块/邻接或复制数量匹配。
- statement控制子程序、logical RHS、conditional arm划分execution
  区间；普通block按顺序执行，不创建人为屏障，原binding scope由
  共同scope解析保护。原生声明可能缩短生命周期，特别是prospective
  for let；RootLifetime用共同声明区间及读取的lexical/projection
  owner保护替换后的实际打印可用性，不能只看canonical SymbolId scope。
  独立copy语句整体省略；嵌入copy投影到原pure RHS保留表达式结果，
  RHS仍是实际读取，不能套用已删除语句RHS的conditional-retention
  豁免。原AST/ID/source kind/store/NamedEvaluation及effectful root保持。
- region/owner/replacement元数据是预算化O(S+E+B)空间；shared census
  O(S+E+owned slots+B+class captures)，原copy/name index成本保持。
  每child只访问/反转常数次，owner单次索引pass，无逐symbol CFG或
  ancestor扫描。0/1/200/201/400/800、400→800 work小于三倍及正反
  SymbolId/ScopeId/ExprId/StmtId通过，不声明physical RSS/benchmark达标。
- 新增264有效树/完整源码，含实参先后顺序、短路&&/||/??、conditional
  arm、赋值前/自读、后续写、普通嵌套block、await/yield、分配身份、
  callee receiver、嵌入RHS遮蔽和for-let生命周期。旧1226 checked/1214
  源保留，总1490 checked/1478完整生成源；435930独立Acorn/原生JS
  行为比较、252有效JS mutation通过，folded源700。原72三profile
  实际NEWLEXENV/STLEXVAR/LDLEXVAR的copy/register/split/branch变体
  均完整解码、语义检查及打印并共享Local；普通实参36用例的复制缺口
  已恢复，原36成员写入的key/Proxy setter顺序/状态/异常检查保持。
  platformExecution:false，不扩大为Ark设备SDK验收。
- 旧规则161在有效实际解码树已通过语义/打印后alias断言RED，退出134，
  /tmp/abcd-expression-binding-red.log。新增If fixture缺少canonical
  Block而被结构检查拒绝，修正fixture不放宽生产规则。初次构建计数
  接口/auto声明及目标名错误已修正并重建，无新依赖。abc_structure/
  abc_semantic及十四fixture最终构建退出0，原literal_plan.cc:59
  kNoop switch warning保持。生命周期补强前30/30通过148.89秒；最终
  生产代码30/30定向通过147.90秒，包括binding/loop/snapshot/structure/
  numeric/class/control/module/family、实际构造十record、Push、
  ProgressBar及富文本。最后||/??只新增fixture/oracle，独立1478源
  对照已通过，绑定两项CTest 2/2补测通过35.71秒；node --check及
  git diff --check通过。
- fresh ABC-only build/stage-one/expression-binding-lifetime-pasteboard-final：
  19 records/219 methods，10complete/0partial/9noModule、value_tree，
  无legacy，顶层contract language_numeric_constructors_v1。十源独立
  解析，无INCOMPLETE/UNRECOVERED；与规则161十份mjs逐字节一致。
  总声明140/空45/初始化95/const87；PasteBoard62/16/46/42、三个
  native for let保持。本轮无新增actual record coverage或声明数改善。
  数值/构造模式保持，record contract序列化既有缺口仍属暂停DFX。
- 最终证据/tmp/abcd-expression-binding-lifetime-{final-build,tests-final,
  export-final,style-final}.log及/tmp/abcd-expression-binding-logical-
  {build,behavior-final,tests-final}.log。下一步共同dead-expression/
  sequence投影：已消除存储的连续pure读可能仍输出comma；跨execution
  区间的精确reaching definitions、一般SSA干涉、未知capture/module、
  原变量/每轮capture、全body/style及原结构/signature/body gate仍开放。
  Goal active；full suite、expanded corpus、RSS/benchmark、DFX、scheduler、
  agents及周期audit继续暂停，固定五ABC106/0未重跑。输入未改动。

### 2026-10-07：规则161，原局部声明与保留首次赋值接入共同复制根

- 从干净ee18cec4继续，上一轮属于实际进展。BindingInitializers在
  同一观察census中记录DeclarationStmt完成，RHS先观察、空let在原
  位置初始化undefined。无owned写或未知capture/module逃逸且声明
  先于全部观察时，经共同词法包含证明供私有复制共享。空声明后保留的
  单写source store复用原FirstStored；Confined/FirstStored同时服务
  私有声明投影。原let/const、scope、RHS、NamedEvaluation、分配身份、
  effect/exception/await/yield完成位置保持，不推测用户const或移动
  原赋值。不增加样本/名字、opcode/producer名单、PC/register、同块/
  邻接或复制数量匹配。常量已删除的store不能作为保留存储根。
- 新增O(S+B)扫描、每binding一个optional声明ID和完成标志，现有
  sizeof(BindingUses)预算已计入；复制图/name index成本沿规则159。
  无逐root CFG/祖先遍历。0/1/200/201/400/800链及400→800 work小于
  三倍通过，正反SymbolId/ScopeId/ExprId/StmtId保持语义；没有physical
  RSS/benchmark达标声明。
- 新增16树族六规模正反arena共188有效树/完整源；三profile实际
  NEWLEXENV/STLEXVAR/LDLEXVAR及register copy/block split/条件调用共
  72解码树/完整源，其中36成员写入用例验证复制共用原Local根。全部
  经过共同graph/control/evaluation/FunctionTree、语义检查与实际打印。
  旧966 checked/954源保留，总1226 checked/1214完整生成源。独立
  Acorn/原生JS oracle累计376758行为比较、224有效JS mutation，folded
  源560；覆盖TDZ、自读、后续写、activation退出、逐轮初始化/存储、
  await拒绝/job、yield throw/return、分配身份、computed key和Proxy
  setter顺序/异常/receiver。平台执行仍为false，不扩展为设备SDK验收。
- 有效旧规则160树先通过结构/打印后alias断言RED，退出134，证据
  /tmp/abcd-source-binding-red.log。loop fixture子range错误及无scope
  证据的LocalNameSyntax被生产检查拒绝后修正fixture，不弱化检查。
  旧未capture真实Local单写反例改为合法共享，独立检查原let和原store
  保留。自然调用路径的复制嵌在实参AssignmentExpr中，当前first-store
  投影只收集直接ExpressionStmt，仍是表示缺口；成员写入的早期快照
  由原共同evaluator生成并消费，无新增field/opcode生产准入条件。
- abc_structure/abc_semantic及十四fixture最终构建退出0，既有
  literal_plan.cc:59 kNoop switch warning保留。首次30项定向29通过/
  1失败，140.29秒；唯一失败是binding源码oracle仍要求旧临时const，
  改为验证原Local let/store保留并保留独立行为对照。修复后binding两项
  CTest 2/2通过27.50秒；其余29项已验证最终生产代码，包括loop/
  snapshot/structure/numeric/class/control/module/family、实际构造十
  record、Push、ProgressBar和富文本。日志/tmp/abcd-source-binding-
  {final-build-final,behavior-final,tests-final,tests-repaired,export-store-final,
  style-final}.log；清理oracle两个不可达分支后源码CTest 1/1再通过
  14.60秒，/tmp/abcd-source-binding-tests-cleanup.log；node --check和
  git diff --check通过。
- fresh ABC-only build/stage-one/source-store-binding-pasteboard-final：
  19 records/219 methods，10complete/0partial/9noModule、value_tree，
  无legacy，顶层contract language_numeric_constructors_v1。十份完整
  源解析且无INCOMPLETE/UNRECOVERED；与规则160十份mjs逐字节相同。
  总声明140、空声明45、初始化95、const87；PasteBoard62/16/46/42，
  三个native for let保持。本轮无新增actual record coverage或声明数
  改善。数值/构造模式保持，record contract序列化既有缺口仍属暂停DFX。
- 下一步嵌套表达式赋值的共同完成/观察投影；条件store的更精细支配/
  活跃期、未知capture/module、canonical同名保守保留、一般SSA干涉、
  原变量/每轮capture、compound声明、全body/style及原结构/signature/
  body gate仍开放。Goal active；full suite、expanded corpus、RSS/
  benchmark、DFX、scheduler、agents和周期audit继续暂停，固定五ABC
  106/0未重跑。输入未修改、无新依赖。

### 2026-10-07：规则160，隐式入口绑定接入共同复制传播

- 从干净77be831e继续，上一轮属于实际进展。共同BindingInitializers
  的不可变复制图以Parameter/CatchParameter/FunctionSelf的typed
  初始化语义建立根，无本地写且无未知capture/escape才可共享。body
  进入、handler activation和函数创建分别提供初始化事实；复制目标仍
  使用原first-store/完整观察/词法包含和共同TreeScopeCheck解析证明。
  不增加样本/名字、opcode名单、PC/register、同块/邻接或复制数量条件。
- ECMA-262 2024 FunctionDeclarationInstantiation的mapped arguments
  规则通过Strictness/CallableKind/Parameter表达：非严格非箭头simple
  参数保留旧值复制；strict/arrow/default/rest没有此隐式写通道。默认
  参数中的写也计入。Local/Capture/Module零写不能提供初始化或外部
  不可变性。本轮bootstrap仅新增O(P+B)扫描、无新数组或逐root图遍历，
  后续graph/scope/retention成本沿用规则159，不声明physical RSS达标。
- 新增17树族×六规模×正反arena，三个不适用零copy各跳过正反，共
  198有效树/完整源；旧768 checked/756源码保留，总966 checked/954
  完整源码。0/1/200/201/400/800链及400→800 work小于三倍通过，正反
  同时翻转SymbolId/ScopeId/ExprId/StmtId。有效树先通过结构/打印，在
  旧实现alias断言RED（退出134，0.98秒），/tmp/abcd-entry-binding-red.log。
- 独立Acorn/原生JS oracle累计335362行为比较、166有效JS mutation，
  folded源450。覆盖参数和catch重写、self身份、inner shadow、条件
  首store、catch/finally退出旧值、await拒绝/job、yield throw/return、
  任意Number/BigInt/Symbol/object输入及value-call receiver。mapped/
  unmapped写探针在生成源/reference同位置插入arguments写，仅属测试
  层；不声称当前native arguments字节码恢复完成。
- canonical capture只接受Local/Capture lexical cell，原调用槽Parameter
  直接capture仍是表示缺口。初次form16 fixture违反此域而拒绝，改为
  有效提升Local cell及owned child修改，并由共同PrintFamilyDraft输出；
  不放宽生产结构规则让无效fixture通过。早期form11 handler子scope
  及loop range测试未识别合法省略store/RHS也已修正，失败保留。loop
  range仍要求所有未省略读取/anchor有效。standalone binding和loop通过，
  早期三项3/3通过6.78秒。
- abc_structure/abc_semantic及十四fixture最终构建退出0；既有
  literal_plan.cc:59 kNoop switch warning保留。最终30项定向30/30通过
  137.87秒，覆盖binding/loop/snapshot/structure/
  numeric/class/control/module/family、实际构造十record、Push、ProgressBar、
  富文本、native名字及loop闭包。
  最终954源oracle结果再次一致。最终日志/tmp/abcd-entry-binding-
  {final-build,behavior-final,tests-final,export-final,style}.log；node
  --check及git diff --check通过。
- fresh ABC-only build/stage-one/entry-binding-pasteboard-final：19
  records/219 methods，10complete/0partial/9noModule、value_tree，无legacy，
  顶层contract language_numeric_constructors_v1。十份完整源码解析，无
  INCOMPLETE/UNRECOVERED。相对规则159四份源码改变，共七个catch复制
  消除，总声明147→140、初始化102→95、const94→87、空声明45保持。
  PasteBoard单份总声明63→62、初始化47→46、const43→42、空声明16
  保持，三个native for let保持。新增表示改善，无新增record coverage；
  数值/构造模式不变。record contract序列化既有缺口仍属暂停DFX。
- 后续真实source const初始化/不可变证明、外部capture/module、canonical
  同名额外保留、一般SSA活跃期/干涉、原变量及每轮capture、compound
  进入声明、全部body/style和原结构/signature/body gate仍开放。阶段一
  Goal active；full suite、expanded corpus、RSS/benchmark、DFX、scheduler、
  agents及周期audit继续暂停，固定五ABC106/0未重跑；受控平台不代替
  设备SDK验收。输入未修改、无新依赖。

### 2026-10-07：规则159，不可变私有复制共用保留存储

- 干净a50c65c7继续，上一轮属于实际进展。BindingInitializers将共同
  单写/首次完成store/全部观察包含/capture逃逸证明沿任意SymbolExpr
  复制链组合，读取可共用一个保留producer的存储。原allocation/
  call/getter/await/yield位置、次数与身份不变；不合并mutable phi/cell，
  不推测或更名原变量，不按producer/opcode、样本、PC/register、同块/
  邻接或复制数量准入。原constant传播继续保留。
- 共同TreeScopeCheck新增Resolves/ResolveTreeReferences，消费原读取
  的canonical ScopeId，使用同一spelling group/activation及scope/query
  adjacency，批量证明根SymbolId可见。既有投影owner及包含关系证明
  打印后的生命周期。内层遮蔽保留存储，兄弟同名不影响合法读取；
  不新增全函数唯一名或固定深度限制。被删除copy的RHS不再是读取；
  若目标必须保留且该位置不能解析根，第二个有界worklist沿条件依赖
  保留必要上游，其他合法分支仍可共用根。原树/ID/read range、被省略
  store/RHS的原boundary零range及全部anchor保持。
- 复制/保留队列和ownership仍线性；新增共同name index成本为
  O(B log(B+1)+M log(M+1)+scopes+queries+比较文本单位)，空间
  O(B+M+scopes+queries)。不会逐symbol重走CFG或逐引用扫描祖先；
  实际输出字节另计。没有physical RSS/benchmark达标声明。
- 新增21树族×六规模×正反arena，四种不适用的零copy变体各跳过
  正反，共244有效树/完整源；旧524 checked/512源码保留，总768
  checked/756完整源码。正反同时翻转SymbolId/ScopeId/ExprId/StmtId，
  200/201/400/800复制链和深作用域、400→800 work小于三倍通过。
  新有效树先通过结构检查，在旧实现alias断言RED（退出134，0.70秒），
  证据/tmp/abcd-alias-binding-red.log；不以无效树或拒绝替代成功。
- 独立Acorn/原生JS oracle累计328132行为比较、146有效JS mutation，
  folded源350。覆盖任意输入/原异常、提前/条件/后续写、真实Local、
  sibling/inner shadow、死copy RHS不额外检查、必须保留的复制级联、
  零/多轮及退出读取、finally旧值、await拒绝/job、yield throw/return、
  value-call receiver、单次对象/数组alias mutation及不同调用新分配。
  harness初次await位置错误和隐式undefined return的mutation定位已
  修正，失败保留；800层生成源触发Node默认编译栈，沿用相邻deep-loop
  oracle的4MB stack并完成执行，不把reference stack拒绝当作通过。
- abc_structure/abc_semantic及十四fixture最终构建退出0，既有
  literal_plan.cc:59 kNoop switch warning保留。早期四项4/4通过17.10秒；
  最终30项定向30/30通过132.26秒，覆盖binding/loop/snapshot/scope/
  structure/numeric/class/control/module/family、实际构造十record、Push、
  ProgressBar、富文本、native名字及loop闭包。最终oracle/build/test/
  export/style日志为/tmp/abcd-alias-binding-{behavior-final,final-build,
  tests-final,export-final,style}.log；node --check及git diff --check通过。
- fresh ABC-only build/stage-one/alias-binding-pasteboard-final：19
  records/219 methods，10complete/0partial/9noModule、value_tree，无legacy；
  顶层contract language_numeric_constructors_v1。十份完整源解析，无
  INCOMPLETE/UNRECOVERED；源码内容与规则158十份相同，实际PasteBoard
  空声明16、初始化47、const43、总声明63、三个native for let保持。
  本轮没有新增实际record coverage或声明数减少，不扩大验证结论。
- 后续继续参数/catch/function-self隐式初始化及source/capture/module
  不可变证明，不能由本地零写推断已初始化或无外部写。canonical同名
  绑定的保守额外保留、一般SSA活跃期/干涉、真实变量及每轮capture、
  compound进入声明、全body/style与原结构/signature/body gate仍开放。
  阶段一Goal active；full suite、expanded corpus、RSS/benchmark、DFX、
  scheduler、agents及周期audit继续暂停，固定五ABC106/0未重跑；受控
  平台不代替设备SDK验收。输入未修改、无新依赖。

### 2026-10-07：规则158，共同私有原始值常量与复制链传播

- 干净af212204继续。BindingInitializers在既有首次完成store/全部观察
  包含/capture逃逸证明上建立常量格：已证明单写的CompilerTemporary
  TreeLiteral定义沿SymbolExpr复制边传播，移除对应空声明和纯store。
  原始值域是既有typed AST，不匹配opcode/producer名单、样本/名字、
  register/PC、CFG同块/邻接或复制数量；不进行算术折叠/未知alias或SSA
  合并。真实local/cell/formal、mutable/captured和未知值继续保持存储。
- SymbolId索引head/next依赖和有界queue每边一次，新增O(B+D)时间/
  O(B)空间（D≤B），全分析继续O(S+E+owned slots+B+class captures)。
  loop进入root前缀计数与有序cursor避免省略项的空逗号/空let/空前缀。
  原AST/ExprId/StmtId/SymbolId不变；实际读取保留自己的生成range，并
  按共同TreeOperandGrouping处理数字member/负数指数base。被省略
  assignment/RHS和声明在原boundary保留零range，所有anchor保留。
  按实际输出字节计费，长字符串重复展开可能增加输出；不声称源码体积
  改善或全style完成，也没有physical RSS达标声明。
- 在旧规则157上，有效树先通过结构验证/打印后于省略存储断言RED，
  /tmp/abcd-constant-binding-red.log退出134。初次build的混合auto类型
  声明错误已修正，不计作语义RED。新增16树族×六规模×正反arena，
  加10原始literal种类×正反arena；旧312 checked/300完整源码保留，
  总524 checked/512完整生成源码。200/201/400/800与work增长通过。
- 独立Acorn/原生JS oracle累计317848行为比较、124有效JS mutation，
  folded源240份。覆盖任意Number/BigInt/Symbol/object输入、长复制链、
  提前读取/自读/条件首次赋值/后续写、真实local、零/多轮退出读取、
  原异常、await拒绝与job顺序、yield throw/return、Number.prototype
  member getter/receiver和负数指数。对象/数组分配不传播：单调用内
  alias/mutation身份保持，不同调用仍新分配；没有回放call/getter。
- abc_structure/abc_semantic及十四fixture最终构建退出0，既有
  literal_plan.cc:59 kNoop switch warning保留。对象反例补强后四项
  定向4/4通过14.05秒；最终30项定向30/30通过122.88秒，覆盖共同
  binding/loop/snapshot/scope/structure/numeric/class/control/module/family、
  native函数名及实际构造十record、Push、ProgressBar、富文本和loop闭包。
  oracle /tmp/abcd-constant-binding-behavior-final.log；最终build/test/
  export/style日志为/tmp/abcd-constant-binding-{final-build,tests-final,
  export-final,style}.log。node --check、git diff --check通过。
- fresh ABC-only build/stage-one/constant-binding-pasteboard-final：19
  records/219 methods，10complete/0partial/9noModule、value_tree，无
  legacy；顶层contract language_numeric_constructors_v1。十源独立
  解析，complete无INCOMPLETE/UNRECOVERED。实际PasteBoard常量种子
  直接为false/0，空声明19→16，总声明66→63，初始化47/const43和
  三个native for let保持。phi/update搬运与未知alias仍存在。
- 原结构/signature/body gate、一般SSA活跃期/干涉与存储投影、真实
  变量归属/每轮capture、compound进入声明及全body/style仍开放。
  阶段一Goal active；full suite、expanded corpus、RSS/benchmark、DFX、
  scheduler、agents及周期audit继续暂停，固定五ABC106/0未重跑；
  受控平台/构造子集不代替设备SDK和全record body/style验收。
  输入未修改、无新依赖。

### 2026-10-07：规则157，私有循环进入绑定的原生 for let

- 从干净f72c545b继续，当前隔离branch codex/resume-generator-post-loop。
  共同BindingInitializers给有进入程序的LoopStmt记录候选词法区间，
  entry/test/update观察解析到该owner，body继续保留native子作用域。
  复用首次完成直接store、全部读写包含、capture/module escape证明，
  将未逃逸的CompilerTemporary进入绑定投影为原生for let；循环外/
  catch/finally旧值、RHS自读、先读旧值、真实local/cell/formal仍保留。
- ForDeclaration使用进入程序可声明后缀，任意数量声明保持mutable let；
  前序有序效果在原进入位置/原handler内运行一次，然后带原label打印
  for。不将前序塞入第一个RHS改变函数/class NamedEvaluation。原树/
  ExprId/StmtId/SymbolId、作用域proof及anchor保留，不合并SSA/phi/cell，
  不更名或猜原变量。无样本/名字、PC/register、同块/邻接、数量或
  opcode/producer列表匹配。compound root/非声明交错仍有语法表示缺口，
  保留完整表达式，不成为整函数拒绝或被描述成语言不变量。
- 新一份StmtId可选进入offset，共同迭代DFS/观察census和disjoint
  进入扫描仍O(statements+expressions+owned slots+symbols+class captures)。
  无逐symbol/loop图遍历；200/201/400/800、多声明链、透明块深度、正反
  arena及400→800 work小于三倍通过。没有物理RSS/benchmark达标声明。
- 增加十类native/private树族×六规模×正反arena共120树，既有156树保持，
  全部276完整生成源进入独立Acorn/JS oracle：13892行为比较与56有效JS
  mutation通过。覆盖零/多轮、任意Number/BigInt/Symbol/object身份、
  callback receiver/order、原throw、await拒绝、yield throw/return、
  退出后读取、先读旧值、真实local及nested outer labeled continue。
  新fixture起初遗漏origin所需operation记录，且未区分既有tail/discard
  零范围，已修正fixture；有效禁用loop-entry候选probe在打印后的native
  for let断言RED，恢复后通过。label用例起初给最近loop冗余label，违反
  现有canonical契约；改为nested outer transfer，未放宽生产检查。
- abc_structure/abc_semantic及十四相关fixture构建退出0；既有
  literal_plan.cc:59 kNoop switch warning保留。最终30项定向30/30通过
  109.71秒，覆盖共同binding/loop/snapshot/scope/structure/numeric/
  class/control/module/family及实际构造十record、Push、ProgressBar、
  富文本、native函数命名和loop闭包。新fixture把结构检查移到分析前，
  最终另外两项2/2通过7.85秒；原invalid label主运行29/30保留为历史。
  受控平台/构造子集不代替设备SDK或全record body/style验收。
  原结构/signature/body gate、一般SSA干涉/存储投影、真实变量归属、
  用户每轮capture、compound进入声明及全body/style仍开放。阶段一
  Goal active；full suite、expanded corpus、RSS/benchmark、DFX、scheduler、
  agents及周期audit继续暂停，固定五ABC106/0未重跑。
- fresh ABC-only build/stage-one/for-binding-pasteboard-final：19 records/
  219 methods，10complete/0partial/9noModule，顶层requested contract
  language_numeric_constructors_v1，无legacy。十份源码独立解析通过，
  无complete INCOMPLETE/UNRECOVERED。实际PasteBoard三个for均有let
  初始化（此前0），空声明22→19，初始化44→47，私有const43保持，
  总声明66及三个for保持；机械更新/phi载体仍存在，未宣称阶段一完成。
- 证据/tmp/abcd-for-binding-{valid-red-build,valid-red,expanded-build,
  expanded-tests,final-build,label-build,label-tests,main-before-label,
  tests-final,precondition-build,precondition-tests,behavior-final,
  export-final,style}.log，node --check及git diff --check通过，
  输入未修改、无新依赖。

### 2026-10-07：规则156，条件投影保留的原生循环体初始化

- 干净10b62dd8继续，production/rules155推进156。共同初始化分析
  使用已有LoopConditionSyntax.body及LoopBody打印归属，将保持语句的
  continuing body从外层条件表达式context中排除；进入原发生点时恢复
  native词法归属，再处理体内自己的loop/nullish投影。只增加一份
  StmtId索引，原子树区间、首次直接store支配及全部观察census继续共用。
- 不识别哪个arm、声明/handler数、opcode/producer列表、样本/名字、
  PC/register、同块、邻接或COPY/selection数量。原条件/body、表达式/
  副作用、异常/暂停、SymbolId及来源不动。外层、for更新及退出后旧值
  读取与真实capture/module escape保持原存储；单次写私有const，后续
  写let。没有推断源码原const、合并SSA/cell/formal或改变每轮capture。
  O(statements+expressions+owned children+symbols+owned class captures)
  时间/预算化scratch保持，一次DFS无逐symbol/loop重扫，新索引O(statements)。
- 有效native local循环体用例先在原规则155的已打印范围断言249行RED。
  修复后新增退出后读取反例继续保持外层，零/一/二/三轮行为、原throw、
  任意Number/BigInt/Symbol/object身份和callback receiver/order另验。
  NativeDeep以相同逻辑覆盖200/201/400/800内层block及相反arena；
  400→800 work均小于三倍，既有exact/under预算保持，不声称物理RSS。
- 新实际decoded loop起初测试错误要求固定retained-body布局，在249行
  失败；其共同Control实际上恢复following native body。测试改为核对
  真正loop condition及体内seed原生const，未修改生产控制/语义证明。
  三profile×两register分配×0/1/200/201 COPY×straight/split共48新循环，
  逐profile DecodeInstruction，经FunctionInput/ValueProgram、共同control/
  evaluation/tree及新鲜TreeSemantics。既有48 decoded保留；十五树族
  六规模/正反arena共180树，加36深度树与96 decoded，共312检查树。
  深度400/800十二树只分析，300完整源码进入独立JS oracle，最终308180
  行为比较/92有效JS mutation通过；原async/yield及for更新反例仍覆盖。
- abc_structure/abc_semantic及十三相关fixture最终构建退出0。27项定向
  27/27通过110.28秒：共同body/binding、loop条件/进入/更新、snapshot/
  optional、scope/structure、numeric update、class/control、module/family
  names/default export，以及实际构造十record、Push、ProgressBar、富文本。
  受控平台矩阵/构造子集不代替设备SDK或所有record body的独立验收。
  修改mjs的node --check、git diff --check通过，输入未修改，无新依赖。
- fresh ABC-only build/stage-one/loop-binding-pasteboard-final：19 records/
  219 methods，10complete source/0partial/9noModule，顶层requested
  contract language_numeric_constructors_v1。十份源Acorn独立解析通过，
  无legacy或complete输出INCOMPLETE/UNRECOVERED。实际PasteBoard空声明
  39→22、初始化27→44、私有const26→43，总声明66及三个for保持；
  取record/type及各MIME分支的getData/await等现在按原发生点在循环体
  内初始化。仍有phi/更新/参数内保存与真实cell，未冒称SSA carrier消除。
- 证据/tmp/abcd-loop-binding-{red-build,red,build,tests,decoded-build,
  decoded,decoded-tests,expanded-build,expanded-tests,final-build,
  tests-final,behavior-final,export-final,style}.log。原字段/全局/signature/
  body gate和阶段一Goal继续active；下一步一般SSA存储/干涉投影、真实
  变量归属、native for let/每轮capture、一般继续程序及全body/style验收。
  full suite、expanded corpus、RSS/benchmark、DFX、scheduler、agents及
  周期audit继续暂停，固定五ABC106/0未重跑。

### 2026-10-07：规则155，私有初始化的共同作用域包含证明

- 从干净2886cc9b继续，production/rules154推进155。BindingInitializers
  去掉空声明与直接赋值必须同一native容器的表示限制；共同迭代DFS
  给词法容器记录子树区间，SymbolId索引记录全部观察容器的上下界。
  首次观察仍必须是直接赋值完成，全部读取/写入必须留在初始化的
  词法生命周期内；祖先空声明可省略，表达式、原store及anchor不动。
  外层/兄弟catch/finally旧值读取、RHS自读、capture/module escape、
  用户local/cell/formal与未知区域仍保留，单次写用私有const，后续写用let。
- 作用域census复用既有LoopSyntax进入/条件/更新及statement-nullish
  的打印owner，投影观察在遍历后解析，owner可晚于原根。外层owner
  传播到子树，native loop程序优先于内嵌nullish，arena编号不决定归属。
  自查发现只按canonical body包含关系会错把for更新读取当成体内读取；
  普通多写私有绑定的有效scope-only probe在233行RED，finally恢复
  生产映射后通过。没有用新root/same-block或样本条件封堵。
- 原ABC→ValueProgram→control/evaluation/scope→canonical FunctionTree→
  common printer保持，没有样本/名称、PC/register、邻接、复制/selection
  数量、producer/opcode白名单或IIFE。O(statements+expressions+owned
  children+symbols+owned class capture references)时间及预算化scratch。
  每个expression至多一条投影context观察，update同时记读和一次写；
  无逐symbol或loop重扫。200/201与400→800 work小于三倍、相反arena、
  深层block及exact/under工作预算保持。不把计数当物理RSS证明。
- 新嵌套分支/try用例先在旧规则154有效打印后的空声明断言159行RED。
  十二原/新增普通树族随后扩展十三族，六规模、正反arena共156树，
  加12深度树和48真实三profile COPY/split decoded，共216被检查树。
  其中深度400/800的四树仅分析，其他212完整源码由独立JS oracle运行，
  25940行为比较/56有效JS mutation通过；包含未执行分支、finally原throw/
  undefined、零/多轮循环、mutable私有值被for更新读取、await/yield请求。
  原decoded经FunctionInput/ValueProgram/common tree及新鲜TreeSemantics。
  扩展fixture最初把bool直接装入typed TreeLiteral而编译失败，改为
  BooleanLiteral；另修正构造loop时删除旧return表达式的时机，未改生产证明。
- 最终两工具、十个主体fixture及两个name/export fixture构建退出0。
  最终25项定向25/25通过66.62秒，含新增unit/源码、loop条件/进入/
  更新、snapshot、tree scope/structure、numeric update、class/control、
  family names/default export及实际构造十record/Push/ProgressBar/富文本。
  projected nullish观察另构建并验证两项snapshot_optional，2/2通过
  48.06秒。早期scope-only模型的25/25为62.80秒，不作最终模型证据。
  受控平台和构造子集不代替设备SDK、十record全部body或全阶段验收。
- fresh ABC-only输出build/stage-one/binding-scope-pasteboard-reviewed：
  19 records/219 methods，10complete source/0partial/9noModule，顶层
  requested contract language_numeric_constructors_v1。十份源Acorn独立
  解析通过，无legacy或complete输出中的INCOMPLETE/UNRECOVERED。
  PasteBoard空声明50→39、初始化16→27、私有const15→26，总声明数
  66及三个for保持；实际HTML处理的fromHtml、await和catch值现在在
  各自try/catch中原位置初始化。配对源码未进入引擎，输入未修改。
- 证据/tmp/abcd-binding-scope-{red,red-build,build,tests,loop-build,
  loop-tests,final-build,names-build,tests-final,projection-red-build,
  projection-red,projection-build,projection-tests,reviewed-build,
  reviewed-tests,optional-build,optional-tests,behavior-reviewed,
  export-reviewed,style-reviewed}.log。修改mjs的node --check及git diff
  --check通过。原header投影仍保守包含retained continuing arm，体内
  初始化尚未细分；参数内保存、一般SSA存储投影、真实变量归属、native
  for let/每轮capture、一般继续程序与全body/style继续未完成。阶段一
  Goal和原字段/全局/signature/body gate保持active；full suite、expanded
  corpus、RSS/benchmark、DFX、scheduler、agents和周期audit暂停保持。

### 2026-10-07：规则154，私有结果的原位置初始化声明

- 干净ae1cb83a继续，production/rules153推进154。共同
  BindingInitializers从canonical FunctionTree的词法容器/statement顺序
  支配和ownership census恢复let/const初始化；只处理已有私有结果
  CompilerTemporary，不合并SSA、formal、cell、phi/cycle等存储身份。
  每个目标的首次观察必须是独立赋值完成，RHS自读/提前读取/嵌套先写
  不提供初始化证明；真实class/closure capture及module escape保留。
  有后续写入保留let，否则用私有const，不冒称源码原const metadata。
- 原expression/assignment/StmtId、scope、catch、执行顺序、NamedEvaluation
  和全部anchor保持；旧空声明在原boundary有空生成范围，赋值位置
  打印初始化声明。原ABC→ValueProgram→control/evaluation/scope→
  FunctionTree→共同printer保持。没有样本/名字、PC/register、CFG同块、
  邻接、固定COPY/selection数量、producer/opcode白名单或IIFE。
  同一native词法容器用于可见性/顺序支配，不把字节码块布局当语义。
- Ruling：先恢复已证明的私有结果初始化，不能从SSA carrier推测用户
  let/const或每轮环境。嵌在调用参数中的保存、条件/其他声明容器、
  loop/nullish表达式程序、未知区域继续用原表示，不新增整函数拒绝。
  每轮capture、实际原变量归属和一般SSA干涉/存储投影仍需共同模型。
  O(statements+expressions+children+symbols+owned class capture references)
  时间、O(statements+expressions+symbols)预算化scratch，迭代栈无递归。
  不逐symbol/loop重扫；400→800 work小于三倍，200/201保持。
- 首轮fixture遗漏anchor→operation归属及正式参数name evidence，修正
  测试后完整结构/语义检查通过，未放宽生产检查。旧printer替换回HEAD
  另构建退出0，同一有效fixture在空声明范围断言134行RED；finally已
  恢复本次printer并重新构建。最初纯直线decoded保存嵌在首个调用参数
  中，测试不再要求错误的提前const；独立branch和split证明原位置声明。
- 八类独立树×六规模×正反arena共96树；三ISA profile×0/1/200/201 COPY
  ×straight/split×直线/条件使用共48实际解码函数，经FunctionInput、
  ValueProgram、原control/evaluation、共同AST与新鲜TreeSemantics。
  总144完整源码，首轮20772独立行为比较/16有效JS mutation通过，包含
  任意primitive/object身份、原throw、后续写入、RHS旧undefined、
  提前读取、条件路径、await拒绝/microtask、yield next/throw/return。
  JS oracle最初误比较抛出后残留的独立Promise/generator对象，现只比较
  实际返回或原throw，保留完整trace及请求；未以失败/INCOMPLETE代替成功。
- 自查后补强条件未执行的原undefined读取与mutable let→const有效错误
  mutation；新增unit/源码行为最后另轮2/2通过2.77秒，最终20820比较/
  18 mutation，仍144完整源码。主运行23项与补强后两项分别留证。
- 最终两工具、九相关fixture及两个name/export fixture构建退出0。
  23项定向23/23通过61.00秒：含新增unit/源码行为、loop初始化/更新/
  条件、numeric update、snapshot、native function names/default export、
  class/control，以及actual十record构造配对、Push、ProgressBar和富文本。
  原实际十record2450 paired/120 native super、Push322 lifecycle/1860
  delivery/八connected、ProgressBar两record及富文本五record受控矩阵
  本轮重新通过，不将受控平台/构造子集冒充所有record body或设备SDK。
- fresh ABC-only pasteboard导出build/stage-one/binding-initializer-pasteboard-final，
  manifest19 records/219 methods，10complete source/0partial/9noModule，
  顶层requested contract为language_numeric_constructors_v1。十份生成JS
  独立Acorn解析通过，无legacy writer或complete源码中的INCOMPLETE/
  UNRECOVERED。实际PasteBoard空声明65→50，带初始化声明1→16、
  私有const 0→15；总声明数保持66、三个原生for保持。不声称这些变化
  已消除SSA私有变量、恢复用户原名/const、native for let或全body风格。
- 证据/tmp/abcd-binding-initializer-{configure,baseline-build,baseline-test,
  expanded-build,expanded-behavior,final-build,name-build,tests-final,
  export-final,style,reviewed-build,reviewed-tests}.log，baseline恢复源及诊断另留。修改mjs的node
  --check与git diff --check通过。输入未修改，paired source仍仅供独立
  oracle；固定五ABC106/0未重跑。阶段一Goal和原字段/全局/signature/
  body gate继续active，full suite、expanded corpus、RSS/benchmark、
  DFX、scheduler、agents、周期audit暂停保持。

### 2026-10-07：规则153，条件参数沿已归属的纯复制恢复调用

- 从干净0e4e695b继续，production/rules152推进153。上一轮普通arrow
  的retained callee失败，根因是规则149已按共同storage lifetime将phi
  首读复制内联，而OperandSelections仍另要求该ValueCopy为kRoot。
  移除此表示准入条件；纯复制、真实input/result、canonical单次消费、
  Prefix/Scope、receiver/identity、效果及handler/catch-copy义务保持。
  原ABC→ValueProgram→control/evaluation/scope→FunctionTree→printer
  保持，不增加样本/名字、PC/register、同块、邻接、固定COPY/selection
  数量或局部图形匹配。每候选复用原索引O(1)，原分组复杂度及预算保持。
- 三个已有通用调用测试先实测0/3 RED，修复后初轮4/4通过195.72秒。
  最终fixture在规则153重新构建，两工具及九fixture退出0。共同调用
  覆盖三profile及不同formal/register、1/2/3/200/201参数；源码行为
  另增加0/1/200/201复制变体，实际getter/receiver、callee替换、不可调用
  值、原throw及.call属性毒化继续检验。没有通过IIFE或宿主Reflect补洞。
- 扩展回归发现旧writer named_call_test:555误取AccumulatorFailure，
  实际BodyEmitFailure为kUnsupportedInstruction、PC10、
  ScalarError::kUnsupportedOpcode。历史状态也已记录同一基线失败。
  两个跳转用例现在独立证明AccumulatorFlow有效，再核对确切typed
  writer拒绝与空输出，不接纳任意failure、不更改生产旧writer。这是
  retained Reference表示缺口，不是未初始化值或语言限制；修正后
  named_call_test和实际生成JS oracle 2/2通过5.04秒。
- 最终25项运行24通过/1旧断言失败，453.55秒；上述断言修正后2/2
  另验通过，覆盖原25项及新增旧writer生成JS oracle共26个测试。
  两次运行分别保留，不冒称单轮26/26。435真实解码joint场景、
  286848 joint源码对照、78624 phased调用、10162 module读取场景
  通过，receiver/identity/TDZ/effect/throw及有效错误mutation均保留。
  ProgressBar两record和富文本五record受控对照也通过。git diff --check
  及修改mjs的node --check退出0；全套及暂停的物理RSS未重跑。
- actual语言构造模式十个record均完整生成，2450 paired构造场景、
  120 native super场景及三mutation通过，包含可构造Proxy父类和
  动态父类变化；默认模式保持。实际Push record3444完整源码的
  322 lifecycle、1860 delivery、三mutation及八connected场景通过。
  构造器对照不代替十record全部行为；Push对照使用受控平台服务，
  不代替设备SDK或实际平台执行。
- fresh ABC-only Push导出build/stage-one/call-transport-push-final：
  24 records/281 methods，15complete source/0partial/9noModule，
  contract language_constructors_v1。Pasteboard导出
  build/stage-one/call-transport-pasteboard-final：19 records/219 methods，
  10complete/0partial/9noModule，contract language_numeric_constructors_v1。
  独立Acorn解析全部生成文件，分别139/87 MethodDefinition；没有legacy
  writer或complete输出中的INCOMPLETE/UNRECOVERED。解析成功不代替
  全部解码method的行为验收；paired正文只进入独立测试，输入未修改。
- 证据/tmp/abcd-call-transport-{red-build,red,build-final,
  tests-final,named-build,named-tests,constructors-pass-1,
  push-ability-pass-1,push-export-final,pasteboard-export-final,
  export-check-final}.log及named/reference诊断保留。一般无法取得owned
  参数表达式的retained Reference、SSA存储投影、native binding/每轮
  捕获、一般继续程序及完整body/style仍待完成。原字段/全局/signature/
  body gate与阶段一Goal保持active；full suite、扩展语料、RSS/benchmark、
  DFX、scheduler、agents及周期audit暂停保持。

### 2026-10-07：规则152，数值更新复用自身结果存储

- 干净ab0d3a43工作树继续，production/rules151推进152。BindingPlan
  仅在SnapshotBinding的producer及ValueId均属于当前数值操作时复用
  它作为私有更新存储；非materialized结果仍使用独占UpdateBinding。
  不借用输入、formal、cell、phi、catch slot或其他producer的snapshot。
  原ABC→ValueProgram→EvaluationPlan→FunctionTree→共同printer保持。
- 新鲜TreeFlow从实际RootExpression重建publication/producer身份，
  独立核对save/update/publication写入及capture排除；正常SSA读取继续
  通过原operand/availability证明。前缀INC/DEC与后缀ToNumeric保持，
  后者外层publication仍恢复旧数值，不能因共享存储而省略。没有新增
  opcode名字、样本/sidecar、PC/register、同块、邻接或局部图形匹配。
  分配仍O(operations)，检查O(operations+symbols+expressions+captures)，
  预算化scratch O(symbols)；200/201/400现有三profile规模检查保持。
- 旧实现在实际结果存储断言RED；新增断言最初误把嵌套更新的输入保存
  当publication，按原operation origin区分后修正。任意值phased fixture
  初次遗漏Arithmetic(identity.contract)，导致输入证明bad_variant；
  修正测试适配并显式检查typed返回，未放宽生产证明。直接更新的null
  catch返回暴露oracle的typeof null分支错误，修正后3584独立场景通过。
- 原108实际解码更新用例保留，新增24任意值语言模式用例（三profile、
  normal/phased、INC/DEC、直接输入/ToNumeric），全部含201 COPY及split。
  包含Number/BigInt、NaN/signed zero、Symbol、字符串及对象number-hint
  转换、原throw、catch旧输入身份、callback receiver/参数、输入未被写入；
  四独立语法树保留。3有效JS行为mutation及原typed树mutation检出。
  生产名称匹配未引入；测试旧$update正则改为已有固定Acorn AST定位。
- abc_structure/abc_semantic、十一相关fixture构建退出0。22项定向实测
  21通过/1失败，58.23秒，不冒称全部通过：language_constructors_paired
  在首个push record3444的普通arrow3613生成前失败。198 decoded语言
  构造器及420函数/353220数值场景通过；实际ProgressBar/富文本受控
  对照保持，设备SDK语义未验。其他21项没有以INCOMPLETE代替行为验收。
- 失败定位为未改动call_plan.cc:215：实际callee为kRoot/
  kCrossRegionValue，而当前MemberCall表示只接受inline/nested callee。
  是retained Reference表示缺口，不是构造语义或语言不变量。暂时移除
  本次allocator复用后相同失败，LLDB证实kReference→kUnsupportedOperation
  →FunctionTreeError::kExpression；恢复原文件并重建两工具后另验仍失败。
  没有删除Reference/receiver/异常义务或以样本条件绕过；历史十record
  验收不作当前通过证据。下一步优先完成这个普通函数的共同调用恢复，
  再继续SSA store投影、native binding/每轮捕获及一般继续程序。
- fresh ABC-only pasteboard显式numeric+constructors导出
  build/stage-one/numeric-storage-final，manifest19 records/219 methods，
  10complete source/0partial/9noModule，contract保持
  language_numeric_constructors_v1。独立Acorn检查private update符号3→1，
  $value49/$phi10、三个原生for（均有init、两个有update）、三次++保持。
  paired正文仅进入独立测试，引擎只收ABC/固定配置，输入未修改。
- 证据/tmp/abcd-numeric-storage-{build-final,build-arbitrary,
  build-constructors,build-restored,tests-final,style-final,export-final}.log；
  exclusive-probe.json及constructor/reference/expression-debug日志保留。
  git diff --check、修改mjs的node --check通过。原字段/全局/signature/body
  gate与阶段一Goal保持active；full suite、扩展语料、RSS/benchmark、
  DFX、scheduler、agents及周期audit暂停保持，不声称全阶段完成。

### 2026-10-07：规则151，共同进入程序与 for 初始化

- 基线f5cc2b69，干净codex/resume-generator-post-loop工作树继续；上一轮
  已提交for更新恢复，是实际progress。production/rules150推进151。
  新loop_initializer复用共同normal completion与statement/expression
  ownership；一次写入census在loop进入/退出时记录SymbolId写入，选择
  条件读取且loop内可能写入的绑定。进入根按原序组成程序，允许跨任意
  无binding/label透明块，不要求同块。保守候选定义索引向左扩展完整
  suffix，重复写入及全部中间操作保留，不冒称精确reaching definition。
- 共同printer用原LoopHeader/完成代数打印for(initializer;condition;update)，
  移动的ExprId/StmtId与父block boundary仅在原生初始化位置输出一次；
  原canonical AST及control/evaluation/scope/source证明保持。条件相关
  分支、handler、声明、binding、label边界保留；没有opcode/寄存器/PC/
  名字/样本/sidecar、相邻、same-block或固定根/COPY数匹配。只读外部
  边界值准备留在原语句；该seed选择用于恢复携带状态，不是语言准入。
- Ruling：仅恢复表达式initializer，不从private phi推出原let或每轮环境；
  未恢复的更新仍在body原位。基于固定ECMAScript 2024 ForLoopEvaluation，
  初始化在首次test前执行一次，即使零迭代也执行，throw/await/yield保持
  原handler和请求顺序。scope/声明/间接或heap-write seed/一般分离继续
  程序与phi/numeric-update存储消除仍是表示缺口，后续共同模型继续。
  原function/ESM completion value边界保持，不新建script eval打印入口。
- 初次144真实解码表示测试在旧实现RED，initializer应存在时实测false；
  首轮新fixture未使用label而违反原target校验，补齐原owned break后
  再验。深块输出超过原8MB测试缓冲、V8编译栈上限，改为有界16MB
  输出和仅该Node测试4MB栈；没有放宽生产校验。只读gate不应被吸入
  initializer的独立表示检查另先RED，再改为共同loop-owned写入census。
  首次新增make target图未刷新，显式configure后构建；失败日志保留。
- phase_copy_test扩展初始化位于原try内/外两种身份，共三profile、locals
  2/7、COPY0/1/4、split与两种handler的144 decoded。经原ExceptionFlow/
  SSA/ValueProgram、共同tree语义校验后打印；独立4176行为比较、504
  有效JS condition/type/参数identity/initializer mutation全部通过。
  原受保护区域外的初始化仍留在原位，不跨handler改变异常时机。
- 新loop_initializer_test/source_test共156树，13种normal/conditional/
  scope/handler/label/TDZ/只读gate/await/yield/early continue/依赖链场景，
  0/1/200/201/400/800与正反arena。透明块深度801，链800个独立符号；
  普通body前缀、深层透明块和initializer定义链分别400→800 work小于
  3倍，字节耗尽typed错误另验。所有原expression/statement/anchor范围
  校验；10372独立行为比较、32有效JS省略/重复初始化mutation通过。
  对照含零迭代、truthy/falsy、Object.is身份、原throw/部分写入、receiver、
  Promise job/rejection，以及初始化yield时throw/return请求。fault位置
  有界采样，非every-event；独立树不是ABC解码证明，两层结果分别列出。
- 最终abc_structure/abc_semantic与十二相关fixture -j4构建退出0；新
  initializer链fixture随后单独重建退出0。22项定向22/22通过83.49秒，
  覆盖phase/reference/copies/exception/liveness、loop syntax/初始化/
  更新/condition/exits/shared exits、closure、generator finally/return-loop、
  optional statement与实际ProgressBar/富文本。旧108更新树仍2418比较/
  14 mutation，132 condition树仍4472/14。actual rich仍70配置/1030原throw/
  206换绑/18 mutation/2等价，ProgressBar仍88配置/570原throw/114换绑/
  15 mutation/2等价；受控平台不能代替设备SDK。602项仅注册。
- fresh ABC-only numeric+constructors pasteboard导出路径
  build/stage-one/loop-initializer-final：manifest19 records/219 methods，
  10complete source/0partial/9noModule，language_numeric_constructors_v1。
  独立AST/style确认PasteBoard三个for都有初始化，其中两个有update，
  第三个update仍在原continue路径；$value49/$phi10保持。外部record/
  类型列表准备未吸入inner for。paired正文只在独立record测试中读取，
  引擎只收ABC/固定配置，输入未修改；固定五ABC106/0仍是历史证据。
- 最终native日志/tmp/abcd-loop-initializer-native-tests-final.log；build-all/
  build-chain/tests-final/export-final/style-final及旧RED/失败日志同前缀。
  三份新增/修改mjs node --check、git diff --check通过。新增分析O(N)
  时间/预算化存储；总LoopSyntax仍O(N + R log N)，打印另计输出字节。
  无新第三方依赖/代码，维护在loop_initializer/loop_syntax/common printer。
  下一步仍是native binding/每轮捕获、SSA更新和一般继续程序；阶段一
  Goal与原fields/globals/signatures/body gate保持active。full suite、expanded
  corpus、RSS/benchmark、DFX、scheduler、agents和周期audit暂停保持。

### 2026-10-07：规则150，共同完成路径与 for 更新程序

- 基线b8ae3902，production/rules149推进150。LoopSyntax共用typed
  LoopCompletionMeaning区分normal/owned break/owned continue；canonical
  LoopStmt、FunctionTree及原control/evaluation/scope/来源校验保持。
  新loop_transition按条件实际读取的SymbolId选定绑定更新，用唯一
  statement定义索引和Euler区间扩展依赖，保留中间所有操作及异常部分
  写入顺序。原owned continue身份的全覆盖证明更新程序执行次数，
  不是固定次数的匹配。声明/handler/label等未证明可跨越的边界保留。
- 共同printer输出for(; condition; update)，原body和break/return/throw
  保留；程序内条件/提前继续按原comma/AND/conditional完成代数表达。
  省略的最终纯completion/continue仍有零宽StmtId/anchor事件，全部
  ExprId及来源不复制、不换图。VisitStatementExpressions与既有children
  接口共用语言ownership，不识别++、opcode序列、PC/register、名字、
  sample/sidecar、producer名单、same-block或固定COPY/guard数量。
- 语言依据为ECMAScript 2024 ForBodyEvaluation/LoopContinues，文档固定
  原文链接；不引入新依赖或第三方代码。N为statement/expression/children/
  symbols/scopes，R为依赖读：O(N + R log N)时间、O(N)存储，scratch经
  共同预算，原typed耗尽保留。scope/handler/binding和一般分离继续程序
  的限制是表示缺口，不称为VM不变量。仅输出空initializer，不擅自
  恢复变量名、原let或每轮环境；完整原生for/body风格仍未完成。
- 新表示断言在旧引擎phase_copy_source_test先RED（0个ForStatement）；
  原三profile72个实际解码register/COPY/split/handler变体后续通过，
  保留2088独立行为及216有效JS错误mutation。新loop_transition_test/
  source_test共108树：九种完成/作用域/handler/async/generator场景、
  0/1/200/201/400/800前缀、0→801透明块、正反arena；共同syntax校验后
  打印。独立2418次行为比较和14有效JS mutation覆盖原throw/状态身份、
  部分写入顺序、receiver、job和yield throw请求；fault为有界采样，
  非every-event。普通前缀与深层block两组400→800 work均小于3倍，
  字节耗尽及全部expression/statement/anchor范围另验，不恢复RSS测试。
- 初次构建漏forward-declare TreeCheckBudget已修正；首次新增target的
  make图刷新后再构建。夹具form4替换mark留下未归属statement而失败，
  改为创建时选择原AST节点；新增anchor未提供唯一OpPlacement时，首轮
  20项18/20通过77.03秒，修正测试合法性后再验。没有据这些夹具失败
  放宽生产校验。首轮失败stdout保留，native日志被随后CTest -N覆盖，
  不冒充保留了该轮完整native输出。
- 最终20项20/20通过69.52秒，覆盖phase/reference/copies/exception/
  liveness、loop syntax/transition/condition、普通/共享loop exit、closure、
  generator finally/return-loop、optional statement与实际ProgressBar/
  富文本。最后meaning类型命名和透明块深度扩展后，相关七项另轮7/7
  通过26.57秒；最后不是假称二十项同轮重跑。actual rich矩阵70配置/
  1030原throw/206换绑/18 mutation/2等价，ProgressBar 88配置/570原throw/
  114换绑/15 mutation/2等价保持。受控平台不能代替设备SDK运行。
- abc_structure/abc_semantic和十一项相关fixture target -j4构建退出0；
  最后五项重建也退出0。600项仅注册；三个新增/修改mjs的node --check
  和git diff --check通过。最终native日志/tmp/abcd-loop-transition-native-
  tests-final-2.log和native-tests-deep-final.log；build/export/style/registration
  及旧失败日志同/tmp/abcd-loop-transition-*前缀，各轮事实分开。
- fresh pasteboard ABC-only显式numeric+constructors导出19 records、
  10complete source/0partial/9noModule，路径build/stage-one/loop-transition-final。
  最后命名/深块版本fresh再导出，与loop-transition-pass-1的所有source
  逐字相同，独立AST/style及source-equality日志另存。
  实际PasteBoard两个嵌套遍历已有原生for/update，第三个loop仍while；
  私有$value49/$phi10保持，未隐藏SSA搬运。paired source不进引擎，
  输入未修改，固定五ABC106/0仍是历史证据。record测试另断言两个for。
  下一步继续initializer/native binding、分离继续程序和SSA更新表示，
  不按最后一个样本循环形状加分支。阶段一Goal和原fields/globals/
  signatures/body gate保持active；full suite、expanded corpus、RSS/
  benchmark、DFX、scheduler、agents及周期audit保持暂停。

### 2026-10-07：规则149，首次 phi 读取复用共同存储/求值证明

- 基线462e449c，production/rules148推进149。PhaseCorridors原先把首次
  ValueCopy的SSA输入一律保留为snapshot，甚至共同唯一使用、原始可用性、
  no-copy interval及效果/异常顺序已成立时也拒绝融合。现在移除该一律
  限制；EvaluationLiveness给出既有存储的typed lifetime，共同phase
  planner和独立verifier复用它。function/parameter存储可按原no-write
  interval读取，不另生成第二个私有slot；原alias图仍保留首次读取的
  exact occurrence，后续COPY只传递不可变结果，不重复producer。最终
  复查将代表性scope改为专用EvaluationStorageLifetime，避免将native
  self/formal错误称为private body声明；实际lexical owner仍归Binding/
  FunctionTree，共同调用不因lifetime获得跨写入或扩展作用域许可。
- PhiCopies的epoch、单一有效使用、原SSA availability、effect suffix、
  catch dispatch/staging和原scope/tree来源检查保持。raw CatchDefinition
  的参数只属于handler，当前phase层尚不能证明移读后的handler scope，
  保留该materializer并明确为表示缺口。既有unique-fallthrough corridor/
  one-operation phase block也是旧表示边界，本轮没有扩展其形状匹配。
  没有新增样本/函数名、PC/register、opcode序列、same-block、producer
  名单或固定COPY数量的生产准入；不能因无language effect盲删真实存储/
  作用域、写入、身份和异常义务。
- 新phase_copy_test实际解码三profile，locals2/7、COPY0/1/4、拆块与否、
  保留当前值/返回原exception两种handler，共72函数；按原ExceptionFlow/
  SSA/ValueProgram、evaluation/control、FunctionTree语义检查后打印实际
  函数。Node独立循环参考覆盖truthy/falsy、NaN/-0、Symbol/BigInt/object/
  function，每个report/step throw位置及receiver，比较2088次。返回值/
  原throw与step参数另外按Object.is验证身份；最后216个有效JS错误
  condition/typeof/参数身份mutation全部被检出，不以同内容对象替代身份。
- 首次semantic RED在旧校验下因首次phi读取不能inline而退出134；前几次
  fixture编译错误不计为semantic RED。初始fixture遗漏ISTRUE，把VM零值
  分支当JS truthiness，独立对照在NaN失败；补齐测试指令后重验，没有
  据此修改生产branch语义。首次记录级两项失败来自旧mutation写死的
  $value编号；改用test-only固定Acorn AST范围和同函数唯一定义关系
  选择受影响表达式，原行为oracle不弱化。别名/改名/函数归属及重复写/
  nested-body/cycle拒绝另有五项手工定向检查，非新增生产分析。
- abc_structure/abc_semantic及十个相关fixture target -j4最终构建退出0；
  既有literal_plan的kNoop switch warning保留，没有修改无关代码。
  首轮18项定向18/18通过77.14秒，含phase/reference/exception/copies/
  liveness、control、普通/共享loop exit、loop syntax/condition、closure、
  generator finally/return-loop、optional statement、ProgressBar及富文本。
  该轮新source有144 mutation；加强身份的216 mutation版本先单独1/1
  再验0.30秒。最后专用lifetime接口重新构建后，完整上述18项另轮全部
  通过76.80秒，该最终同轮包含216 mutation。实际富文本保持70配置/
  1030原throw/206换绑/18 mutation/2等价，ProgressBar保持88配置/570原
  throw/114换绑/15 mutation/2等价；均为受控平台，非设备SDK运行。
- fresh pasteboard ABC-only显式numeric+constructors导出保留19 records、
  10complete source/0partial/9noModule。独立AST核对PasteBoard的$value
  声明60→49，$phi仍10个，三个原生有条件while保持；路径
  build/stage-one/phase-copy-lifetime-final。最终lifetime接口版本fresh导出
  与phase-copy-pass-1的全部source逐字相同。没有修改输入或让paired正文进入引擎，
  固定五ABC106/0仍是历史证据。本轮不是阶段一完成，也没有完成native
  for初始化/更新、每轮binding或全部body风格。
- 598项仅注册；四份新增/修改mjs的node --check与git diff --check通过。
  最终日志/tmp/abcd-phase-copy-{build-lifetime-final,tests-lifetime-final,
  native-tests-lifetime-final,selector-tests-final,export-lifetime-final,
  source-equality-final,registration-final}.log；首轮build/tests/identity及
  旧失败日志保留，各轮事实分开，不把失败计为通过。
  阶段一Goal和原fields/globals/signatures/body gate保持active；下一步
  沿共同loop transition/binding继续高层循环表示。full suite、expanded
  corpus、RSS/benchmark、DFX、scheduler、agents及周期audit保持暂停。

### 2026-10-07：规则148，共同循环完成摘要与原生 while 条件

- 基线9f47f047，生产/rules从147推进148。原LoopStmt保持共同control/
  evaluation/scope与指令来源的规范树，新LoopSyntax在已验证树上做正常
  完成/owned-break组合摘要；共同printer输出原生while条件。Expression
  保留原ExprId，Block原序串联、If按原predicate选择，当前loop的break
  映射false、正常完成true。没有名字、样本、opcode序列、same-block、
  PC/register、邻接、producer名单或固定guard/copy数量的生产匹配。
- header效果/phi复制按原序执行，comma/AND/conditional保持throw与短路。
  body仍是原block，continue/outer transfer/finally ownership不重定向。
  原TreeSemantics、操作/引用/来源校验保留；只省略无效果的常量完成，
  stmt/anchor保存零宽事件，表达式仍保存原ID和输出区间。statement
  children复用既有StatementChild，nullish与loop共享遍历。
- crossed block binding/label仍是当前guard表达式的保守表示边界，保护
  scope/TDZ/每轮环境/target；不宣称语言/VM不允许这样的loop。稍后声明
  的local在条件前仍触发ReferenceError，不能移出作用域消除TDZ。声明/
  try/return/continue/gap前缀没有强制变为表达式；while body中的原try/
  finally与yield/await表达式可以保持执行。原source contract不扩大。
- 新loop_syntax_test与loop_condition_source_test共132项source：36个三
  profile真实解码register重分配/COPY/split变体，加96个独立树的0/1/
  200/201/400/800前缀、arena正逆序、嵌套退出、退出前效果、TDZ、finally/
  await/yield。共同IR/tree校验后直接打印实际decoded函数；Node独立
  参考比较4472次，包含原throw identity、接收者、正常/停止/继续、
  Promise job、generator request及truthy/falsy域；14项有效JS错误条件
  mutation另被检出。这里的fault位置是有界边界采样，不是every-event。
  工作计数400→800小于3倍，byte/work预算耗尽另验；未恢复RSS/benchmark。
- 最终15项定向全部通过70.18秒，包含上述两项、普通/共享/exception
  loop exits、composition、loop closure、generator finally/return、原
  optional statement与snapshot语法/行为，以及实际ProgressBar与五模块
  rich-text。最终rich图70配置/1030原throw/206换绑/18 mutation/2等价，
  ProgressBar图88配置/570原throw/114换绑/15 mutation/2等价保持通过。
  新增async/try fixture的初始arena倒序漏改TryStmt子ID、decoded AST首项
  被private声明占据的测试假设已修正后再验；没有用这些测试失败改生产
  admission。首次共享遍历编辑漏保留旧StatementChild导致构建失败，已
  恢复旧API并令两个计划复用；最终构建和原相关回归通过。
- 最终abc_structure/abc_semantic及六项fixture target -j4构建退出0；
  596项仅注册，不执行full suite。node --check与git diff --check通过。
  最终native输出/tmp/abcd-loop-syntax-native-tests-final.log，构建/tests/
  export/style/registration日志同前缀；旧失败日志保留，不计为通过。
- fresh ABC-only显式numeric+constructors导出pasteboard后独立AST核对：
  manifest19条record，10个complete source、0partial、9noModule；三个
  actual PasteBoard循环均为有条件while、0个while(true)。实际record测试
  也新增该有界表示断言。导出路径build/stage-one/loop-condition-pass-final，
  输入ABC及paired source未修改、paired正文不进入引擎。没有重新导出固定
  五ABC，106/0仍为历史证据；受控平台矩阵不等同设备SDK运行。
- 循环风格仅有进展：条件内仍有SSA snapshots/phi carrier，for initializer/
  update、native binding/每轮捕获、整体body风格未完成。下一步沿共同
  evaluation/lifetime与loop transition还原，不能按当前样本for形状实现。
  阶段一Goal和原fields/globals/signatures/body gate保持active，完整
  pasteboard图及其他record语义/表示仍需证据；full suite、expanded
  corpus、RSS/benchmark、DFX、scheduler、agents与周期audit保持暂停。

### 2026-10-07：富文本、ShareDirect 与 PageA/PageB 五模块行为对照

- 基线7dea75b4，production/rules147保持。本轮补齐实际PasteBoard完整
  constructor/initializer、aboutToAppear、render、copy/paste、HTML method
  与ShareDirect导航正文的独立行为证据。新rich_text_record_source_test
  从ABC-only显式numeric+constructors先导出，结束后才读五份paired TS；
  实际CommonConstants、ShareDitect、PageA、PageB ESM直接相连。ShareDirect
  原push callback调用已注册的实际PageA/B builder，不替换child body。
- 四种view都实际构造/render，storage/cell、ready新path、late params及
  purge/delete另执行。图片buffer.slice、MutableStyledString/attachment
  拼接、HTML/纯文本/pixel按record/type嵌套遍历、跨await getData、copy的
  html/text/pixel entry、clipboard写入与toast执行实际源码。空record/
  类型、200/201 record、数量变化、type length/index getter、falsy spans/
  result、空白/null/number HTML、sync/deferred UI及正反request调度另验。
- test-only ViewPU边界从ProgressBar提取为共同view_protocol，复用原
  NativeRequests及GraphTrace，真实应用body保持。typed SDK语义由受控
  StyledString/controller/record/image/path协议提供，独立记录全部内容、
  entry/descriptor/alias、pixel与controller身份；不接触系统剪贴板。
  Native request/then/catch/async链完整settle，原throw identity与platform
  rejection分开。discarded HTML async的apply Proxy观察精确返回Promise，
  包括首个await前的拒绝；target body/receiver/signature保持，但Proxy有
  独立callable身份。export图在代理前保存，本图不证明该代理下method
  identity的任意观察，也不宣称真实SDK或所有prototype拦截已完成。
- 初始66配置/990 throw/198换绑/12 mutation通过。新增移除slice(0)的
  有效JS mutation先RED：宿主只看buffer字节，错误仍比较相等。现原media
  array/buffer与新image buffer各按身份记录，同时保留字节；该错误及
  像素被同尺寸对象替换均被检出。explicit null HTML不因默认值操作符
  被换成有效字符串，原未等待的HTML拒绝也完整记录。没有生产样本、
  名字、同块、PC/register、序列或图形条件。
- 首轮正式六项定向6/6通过21.00秒，包含原ProgressBar全记录、copy246、
  Home、PageA及reference adapter；当时新图68配置、1030原throw、206
  live-import换绑、13 mutation。扩展后最终新图单独1/1通过12.26秒：
  70配置、1030原throw、206换绑、18有效JS mutation及2等价captured-this/
  词法this变体。两baseline各103 distinct host boundary采样，非every-
  event故障覆盖；正常baseline668事件，五模块evaluated、四view真实
  render与全部request结算另验。原六项未冒充最终同轮重跑。
- 表示验收尚未完成：native class/method/kind/async/formal/default/rest
  及runtime export/signature比较通过，独立AST核对paired有三个ForStatement，
  生成源码是三个WhileStatement(true)，并保留header条件拆分和phi搬运。
  共同LoopStmt目前仅有body/label，printer固定while(true)；这个表示缺口
  必须沿共同控制/求值、loop transition及binding分析继续还原，不能靠
  PasteBoard名字或局部指令/graph pattern投影for，也不能用行为通过关闭
  全body/style门槛。原catch里的未调用箭头及未等待HTML按源码保留。
- configure两次退出0，594项注册；abc_structure／abc_semantic -j4构建
  退出0。新五份mjs及修改ProgressBar mjs语法、git diff --check通过；
  最终native输出/tmp/abcd-rich-text-record-native-tests-2.log，首轮六项
  native-tests-1.log；buffer身份RED及view抽取回归另存。日志前缀
  /tmp/abcd-{rich-text-record,rich-text-buffer-identity,rich-view-protocol,
  pasteboard-rich-export}-*。没有残留进程，输入SHA/size核对原index且未修改。
- 本轮fresh pasteboard导出，不扩大语料；此前固定五ABC106/0仍为历史
  证据，不声称重新全量验收。阶段一Goal、完整pasteboard图、原instance
  fields/globals/signatures/body gate及其他record行为/表示继续active。
  full suite、expanded corpus、RSS/benchmark、DFX、生产scheduler、agents
  与周期audit保持暂停。本次不会把受控语义矩阵替代循环风格或阶段完成。

### 2026-10-07：ProgressBar 真实构造、渲染、复制与粘贴进度贯通验收

- 基线7a45327b，production/rules147保持。原progressbar_cell_reference
  只执行copy callback，没有真实constructor、storage、paste progress、
  timer或完整render证据。本轮新增progressbar_record_source_test：先从
  ABC-only显式numeric+constructors导出，结束后才读paired TS；实际
  ProgressBar与真实CommonConstants ESM连接，body／builder不替换。
- 原模块AppStorage/context读取、两MB string初始化、dstDir形成、原导出
  builder/native class、完整render及ready、copy/paste async callback、
  progress listener、300ms timer和teardown全部实际执行。外部协议控制
  文件/clipboard/UI，不写真实文件或系统剪贴板；文件内容以长度/SHA、
  fd/close状态、传输目的地/删除、clipboard与controller/options身份另验。
  这不证明CommonConstants每个方法、整个pasteboard图或设备协议完成。
- 原Promise tracker提取为test-only NativeRequests，Push与本记录复用。
  native请求与then/catch/async derived返回受owned追踪，以实际活动驱动
  release和完整settle，不用固定ticks。source内callback始终用真实body，
  foreign receiver回放，原throw与platform rejection identity分别比较；
  source realm的then观察保持native receiver/arguments/result及name/length。
- class/method/signature结构与完整runtime export/descriptor另比较；真实
  constructor的default/given、getter参数、id默认/null/0/17与参数lambda
  覆盖，实际super参数及field赋值顺序保留。显式语言构造模式下动态
  Proxy父类、不同new.target和父类返回替代对象另验，不追加父类品牌或
  永久不变假设。没有生产样本、同块、PC/register、序列或图形条件。
- 首轮54配置/570 throw/114换绑/9 mutation通过。新增保持旧pathStack的
  有效JS mutation先RED：宿主以view数量标识多个path，错误仍比较相等。
  改按真实allocation身份编号后，该错误被检出；原path与ready新path，
  以及controller/options alias现在可区分。显式null result/breakpoint在
  fixture中保留，不因默认值操作符变成成功值或sm。
- 最终native新增图与reference adapter两项2/2通过7.31秒；图本身7.25秒，
  88配置、570原throw、114 live-import换绑、15有效JS mutation及2等价
  captured-this/词法this变体。两baseline各57 distinct host boundary采样，
  不是every-event故障覆盖；正常baseline295事件、两模块evaluated、
  真实ProgressBar构造/render、两个button callback及所有request结算另验。
  ctor默认id/lambda、super参数、file内容/close、progress/UI/indicator、
  timer、await、unlink及新path/controller身份错误均被检出。
- 首轮五项定向4/5通过16.80秒：Push图、Home、PageA和新增图通过；旧
  copy test仍匹配共同postfix改动前的三元文本而失败。现用固定Acorn AST
  验证native chain：optional context member、mandatory host call/filesDir
  再拼接/test1.txt，保留全部246 reference/effect/async对照。该旧测试最终
  单独1/1通过1.60秒；未冒充同轮五项全通过。Push提取后另1/1通过8.17秒。
- configure两次退出0，593项注册；abc_structure／abc_semantic -j4退出0。
  新五份mjs及修改mjs语法、git diff --check通过；最终原生输出保存到
  /tmp/abcd-progressbar-record-native-tests-3.log，copy原生输出为native-
  tests-2.log。首轮CTest结果另留tests-1.log，其native-tests-1仅空注册记录，
  不用它证明测试输出；完整旧源失败与identity RED均保留。相关日志前缀
  /tmp/abcd-progressbar-{record,path-identity,push-regression}-*，无残留进程。
- 本轮fresh pasteboard导出，输入SHA/size核对既有corpus index；没有修改
  输入、分类或扩大语料。固定五ABC此前106/0仍是历史证据，不声称本轮
  全量重验。阶段一Goal、其余record独立行为/表示和实例字段/globals/
  完整signatures/body原结构gate继续active；任意字段prototype拦截、
  所有body风格及完整pasteboard/平台图仍待对应证明。full suite、expanded
  corpus、RSS/benchmark、DFX、生产scheduler、agents/周期audit保持暂停。

### 2026-10-07：Push 十五模块真实页面、Ability 与 Service 组合验收

- 基线b6ebc7f5，production/rules147保持。本轮补齐原Push独立Ability／
  VoIP Service验收与页面、组件、form consumer之间的跨record组合证据；
  原八个Ability+Service组合已经存在，不能重复描述为首次支持。本次新增
  push_graph_source_test，十五个模块先从ABC-only显式numeric+constructors
  生成，完成后才读取paired TS，实际方法、initializer及闭包正文保持。
- 实际Ability lifecycle、窗口route、Index导航builder、token/AAID注册、
  通知、form binding、image及VoIP调用连接实际Service与storage/cell。
  WidgetCard／Index／TestAbilityPage／CalleePage／CallComponent／
  GetTokenPage／ExamplePage七种view均经真实路由、builder或模块初始化
  构造并render；双Ability实例与晚期paramsGenerator_/child update另验。
  未调用的私有CreateFormData分配不计为已执行，native class AST另比较。
- SDK宿主只提供外部边界，不替换应用正文。原callback用foreign receiver
  回放，保留事件顺序、字段descriptor、模块/export/reference graph、
  shared/local storage及真实Service.callId。native Promise/derived链按
  实际请求与结算的activity追踪，完整settle；不用固定tick或忽略discarded
  close链。native class/method顺序/kind/static/async/generator/formal/
  default/rest另比较，不能据此宣布所有body/style或任意字段拦截已验收。
- 初轮宿主缺少pixel.then的显式不存在语义，并遗漏await的reject参数；
  后续真实Ability丢弃的catch子Promise未被追踪。修正宿主Promise协议及
  native then观察后矩阵通过，不弱化原异常比较或修改恢复逻辑。显式null
  参数与可删除finalize属性的夹具问题、mutation输入别名也各修正。
- wrong-superclass有效JS mutation的RED揭示四种SDK父类共用一个Base，
  错误替换父类仍比较相等。现四个独立native subclass复用共同实现，
  分别保留constructor/prototype身份；snapshot直接比较实际class与实例
  prototype的继承关系。该mutation最终被检出，production不增加父品牌、
  名字、同块、序列或图形匹配；等价captured-this→词法this仍允许。
- 最终新图native CTest1/1通过8.25秒：87普通/调度场景、430原throw、
  86 live-import换绑、9有效JS mutation及2等价变体。两baseline各43个
  distinct host boundary采样，不是every-event故障覆盖；正常baseline
  2249事件、十五module evaluated及七view实际render另核对。首轮六项
  定向6/6通过19.02秒，含当时八项mutation的新图；其他五项原回归的
  native证据保留，不把最终九项mutation伪装为同轮六项全部重新运行。
- cmake configure退出0，592项注册；abc_structure／abc_semantic -j4
  构建退出0，新四份mjs语法及git diff --check通过。日志/tmp/abcd-push-
  graph-{configure,build,registration,tests,native-tests,heritage-red}-*留存。
  没有残留运行进程。本轮只fresh导出Push十五record，不重复固定五ABC
  全导出；此前106/0统计保持为历史证据，不充当本轮或全阶段重新验收。
- 阶段一Goal及实例字段／globals／完整signatures／body整合继续active。
  受控SDK/ArkUI协议不能替代设备和真实平台验收；其他record独立语义/
  表示及原结构gate仍开放。full suite、expanded corpus、RSS/benchmark、
  DFX、生产scheduler、agents及周期audit保持暂停。

### 2026-10-07：共同后缀链贯通成员、调用与返回函数调用

- 基线a33d153b，production/rules147。旧member path在中间CallExpr丢失
  摘要，无法把`x?.f().a`或`fn?.().a`保留为原生chain。现沿原共同
  MemberExpr.base／CallExpr.callee依赖自底向上传播首个稳定reference与
  guarded access，复用原nullish三域、formation及control/evaluation证明。
  不新建AST／专项body writer，不增加样本、同块、序列、PC/register、
  producer名单或selection数量条件；引用／effect／exception／TDZ保持。
- 共同迭代PostfixSpine仅在首个access加入?.，后续member/call mandatory；
  key转换、getter、callee取得、参数及receiver按原顺序和次数执行。
  CallExpr返回值是非Reference，可以继续值调用；成员的detached value
  invocation不能错误重绑receiver，继续原conditional／`(0, member)(args)`。
  路径摘要一次O(V+E)，没有逐候选走链；任意arena顺序保持。member/call
  原节点正范围、省略guard的base.end事件及真实instruction sources另验。
- 最终203份owned树覆盖深度1/2/3/200/201/402、named/computed、成员/
  首个值调用、尾调用、形成assignment与反向arena。新增返回函数调用、
  两种detached成员及传入原生eval的实际生成函数。135928独立行为场景、
  九个有效JS mutation通过：中途增加?.、括号结束链与错误direct eval
  均被检出。falsy／负零／NaN／BigInt／Symbol、原undefined/null/0/Symbol/
  object throw、getter replay／argument／receiver／原source次数另比较。
  201→402 work几何边界不是RSS／性能benchmark验收。
- 最终960份三profile真实解码fixture，经FunctionInput→ValueProgram→
  Control/Evaluation→FunctionTree→语义／来源→family printer贯通。
  768份深度1/3/200/201，另192份CALLARG消费CALLTHIS返回值；独立组合
  0/201 copies／块拆分、有无副作用参数、named/computed及形成表达式。
  596850独立行为场景通过，原instruction position checks保持；这批不
  把树级九项mutation算作decoded mutation证据。此前768/476550单独
  通过保留，但不计作最终足额960矩阵。
- 首轮19项定向CTest16/19通过303.66秒；两个新增失败来自eval夹具使用
  既有标识符规则不接受的名字，另一项来自扩展夹具时脚本计数先于二进制
  更新。改为原生eval作为原值参数输入，并同步960数量，未修改生产命名
  或弱化校验。最终新增四项CTest4/4通过63.48秒；原十五项均已在首轮
  通过，分别保留native证据，不伪装为同轮19/19。实际33模块Window图
  224.23秒通过，90场景／1400原throw／280换绑／16mutation／2等价变体
  保持，22导出view全部实际构造并render。
- 实际WindowUtil的queryOrientationByResourceManager已输出
  `?.resourceManager.getConfigurationSync().direction`；新增该高层表示
  断言后Window whole-record CTest再次1/1通过0.58秒。不同reference形成
  的外层guard仍明确保留，不宣称整个方法已有单一原生链。其他optional/
  nullish／statement／snapshot／logical及Window旧partial回归均通过。
- configure退出0，591项注册；单-j4 production/CLI及相应测试构建通过。
  build-2的test-only profile/API名称错误修正后build-3/4/5退出0；初始RED
  证明原member path缺口。日志/tmp/abcd-optional-postfix-{red,configure,
  build,registration,tests,native-tests,decode-source}-*留存；新mjs及修改
  Window mjs语法、git diff --check通过，没有残留运行进程。
- fresh固定五ABC仅重跑显式numeric+constructors组合，五导出退出0，
  106 complete／0 partial／46 kNoModule保持，106份mjs全部解析；输入
  SHA/size与原index一致。产物build/stage-one/optional-postfix-numeric-
  constructors-pass-1，batch-summary-1.json在/tmp；不重复六模式三十导出，
  不将这些统计代替所有record独立语义／表示或原结构gate。
- 不同reference、分离formation、一般未融合alias和detached member的
  原生chain仍是表示缺口；未投影时保留已恢复源码，不能新拒绝函数。
  实例字段／globals／完整signatures／body整合与阶段一继续active。full
  suite、expanded corpus、RSS/benchmark、DFX、生产scheduler、agents和
  周期audit保持暂停。本次不是全阶段或任意平台／设备验收完成。

### 2026-10-07：真实 WindowUtil／WindowInfo 与三十三模块导航图

- 基线7eb21287，production/rules146保持。现有Window记录已独立验收；
  本轮补齐原导航图只初始化WindowUtil、页面却读取controlled window对象
  的组合缺口。新window_navigation_graph_source_test沿共同ABC-only exporter，
  导出退出后才读paired TS；三十三实际模块、全部shopping/minigame正文
  连接真实WindowUtil／WindowInfo，未替换类、方法或闭包实现。
- 新navigation_window仅控制平台window/UI/display/hilog。两个真实实例
  执行setUIContext、updateWindowInfo、status注册及orientation异步链，
  实际页面调用system-bar／immersive／recover／release。原window-size、
  avoid-area、title/status闭包用foreign receiver回放；真实WindowInfo进入
  storage cell、child update和晚期paramsGenerator_。全部native Promise及
  derived链受owned列表追踪与allSettled结算，不以固定tick或fire-and-forget
  代替完成。原平台拒绝对象按identity核对，其他原throw与native Error
  brand/category分开；诊断文本不作为恢复或异常身份条件。
- reuse原导航trace/字段/cell/prototype/signature oracle，并提取已有
  classLayouts到test-only class_reference，Community图使用同一实现。
  新图按AST所属class擦除普通无initializer的声明；initializer、method、
  computed/static/private语义保持。native class、method顺序/kind及
  async/generator/formal/default/rest与行为分别核对；不证明所有body表示。
- 实际构造器identity去重barrel/re-export，完整viewAliases仍比较；导出
  类型必须实际构造并render，不能由计数或direct construct代替。private
  route root正文仍实际执行并保留完整trace，只不计入导出类型inventory。
  最终native CTest1/1通过224.46秒，22个实际导出view均构造并render；
  原四项相关回归通过，分别保留其native结果，不伪装为同轮5/5通过。
- 首批两个同步/延迟基线各29415事件通过。扩展阶段的失败分别来自
  错误的固定listener数量（页面会再注册）、把controlled Promise rejection
  当native Error、对非initial分支错误要求child cell，以及按导出名字重复
  统计barrel alias；均修正测试协议／coverage，未新增生产特例或删校验。
  八十八普通组合对照完成后，首轮注册CTest4/5通过391.09秒；新增窗口
  图在一个有效JS wrong-field mutation上失败：所有avoid-kind共用一个
  host对象，使avoidCutout错写avoidSystem没有变化。改为各kind独立结果
  和独立事件payload，保留对象身份与字段归属的完整比较。
- 区域修正后的native CTest1/1通过223.59秒（tests-2），完成90场景、
  1400原throw、280换绑、16有效JS mutation及2等价arrow-this变体。
  两schedule各140个distinct平台boundary，五原throw值含undefined/null/
  0/Symbol/object；并非every UI event。事件853至47587、baseline29433。
  自审发现报告把无export label的private root经JSON null计入rendered
  Set（22类型报告23rendered）；修正输出及相等断言后，最终tests-3再次
  完整通过，22类型／22构造／22render，90/1400/280/16/2保持。错误receiver
  使用globalThis反例，词法this允许等价；不存在未完成测试进程。
- 单-j4 abc_structure/abc_semantic构建和configure退出0；587项注册，新增
  test的RUN_SERIAL及实际command核对。原class_reference_adapter0.06秒、
  Community图89.48秒、minigame导航83.68秒、Window记录0.59秒均在首轮
  通过；不因test-only平台修正重复它们。日志/tmp/abcd-window-navigation-
  {red,baseline,matrix,configure,build,registration,tests}-*保留失败与通过层次。
  最终原生JSON另存/tmp/abcd-window-navigation-verified-1.json。七个相关
  mjs语法及git diff --check通过；未重复五ABC六模式三十次导出。
- 本轮推进真实实例字段/global/闭包/signature/body跨record消费，不增加
  production恢复条件。窗口/UI协议仍为controlled host，不宣称设备、任意
  TS initializer prototype拦截、全部body/style或原结构gate完成。full suite、
  expanded corpus、RSS/benchmark、DFX、scheduler、agents与周期audit仍暂停；
  Goal继续active，下一步仍是阶段一尚未完成的恢复和独立验收。

### 2026-10-07：Ability／Breakpoint／storage／实际视图组合

- 基线a7c619b8，production/rules146保持；上一轮实际模块图已提交并
  定向通过，属于progress。本轮核对发现两个EntryAbility和Logger已有
  独立whole-record验收，不重复宣称它们尚未恢复；补齐的是四十五模块
  图中原本只执行初始化的Ability/Logger正文与实际UI状态组合。
- 新community_platform是test-only controlled平台，复用原GraphTrace/
  retained queue、真实模块link、共同source exporter及独立TS参考边界。
  实际MultiCommunity EntryAbility的生命周期方法、getMainWindow和
  loadContent箭头、windowSizeChange listener、updateBreakpoint调用真实
  BreakpointConstants并写原AppStorage；真正的Root storage cell和渲染
  callback读取这个值。Logger四方法另调用真实singleton、原rest body及
  同一hilog import。EntryAbility直接调用hilog，不伪造它通过Logger调用。
- 新平台控制window/display/device/hilog及void结果，不替换任何原method
  body。Ability实例上的caller-owned updateBreakpoint accessor返回原
  prototype method，记录实际实例与callback时点的成员读取，以使捕获
  身份可观察；方法实现仍原样执行。存活import在明确event处setExport，
  不把类的heritage误当成每次构造都重读import，也不新增host品牌前提。
- resize回放在Root首次render/drain后、目的地子图展开前执行，真实
  Root cell依赖重放已发生；不宣称完整设备window/布局/导航协议。模型、
  child body及完整事件/字段/prototype/alias比较保持；二十个实际view
  仍必须render，直接构造不能替代。同步/延迟、逆序及两Ability实例的
  listener均用foreign this回放。void token的then getter仍拒绝被assimilate。
- 原基线图96/160/32/12在平台提取后完整通过；两个初始平台组合基线
  通过，均实际渲染二十类。首轮扩展64生命周期、320原throw、64换绑、
  12有效JS mutation通过，事件164324至302529。后续新增40个Number/
  BigInt/Symbol输入及十个反例，CTest首轮4/5通过277.71秒，失败位于一个
  错误定义的mutation：箭头中$local1 alias改为词法this仍是原receiver。
  Ruling：该变体按语义等价验收，不要求拒绝——符合原Goal允许等价局部
  表示的要求。改用data receiver作真正反例，另增加两种schedule、双
  实例的等价整图比较；不是生产bug或新增源码匹配/拒绝条件。
- 最终定向5/5通过286.99秒：新community_ability_graph193.83秒、原
  community_graph89.89秒、entry_ability_records2.55秒、Logger0.67秒、
  class_reference_adapter0.05秒。新图104 lifecycle/input场景、330原throw、
  66换绑、22有效JS mutation及2等价变体；二十view实际render。两个
  fault baseline各明确三十三语义boundary、五种原throw值；并非every
  event/every UI API覆盖。新增普通输入含undefined/null/false/零/负零/
  NaN/正负Infinity/BigInt/Symbol以及原对象coercion，生命周期事件数
  164325至302533，完整trace/字段/alias/方法状态比较保持。
- 原96/160/32/12矩阵完整保持；独立Ability2710场景/16 assignment/
  13 mutation及Logger240 basic/848 paired/14 constructor/27 assignment/
  12初始化/6 mutation等原证据重新通过。单-j4的abc_structure/abc_semantic
  构建成功；586项CTest注册且两个graph RUN_SERIAL核对。四个本轮mjs
  语法与git diff --check通过；没有恢复full suite/RSS/benchmark或重复
  五ABC六模式三十次导出。日志/tmp/abcd-community-platform-{red,matrix,
  configure,build,tests}-*.log，原生成功JSON另存verified-1.json。
- 本图推进实际跨record body/捕获/实例字段/global/signature整合；仍不
  关闭全部样本/所有body表示、任意prototype字段拦截、真实设备及原结构
  gate。全面映射/DFX、full suite、扩展语料、RSS/benchmark、生产scheduler、
  agents和周期audit保持暂停；阶段一Goal仍active。

### 2026-10-07：真实 MultiCommunity 四十五模块图

- 基线6b9d5c02，production/rules146保持。本轮继续实际record的独立
  行为/表示验收：MainPage、hot/detail/picture/rank、barrel、常量和模型
  共四十五个ABC-only生成ESM在同一realm连接；导出器结束后才读取配对
  TS。真实view/model正文、getter、builder、watch和closure均不替换。
  EntryAbility也参与模块初始化，本图不宣称它的设备生命周期已执行。
- Controlled host补齐共享storage link/prop通知、cell读依赖、原observer
  的dirty回放、分支编号缓存、导航/惰性content/ListItem、builder参数、
  indicator、focus及图片缓存接口。状态改变后执行原callback；隐藏或
  disabled按钮不作为点击输入。晚注册builder另给空route输入，已存在
  导航builder给四个实际目的地及unknown输入。这是受控协议场景，不能
  推断任意ArkUI分支销毁/祖先可见性、导航栈或真实设备行为。
- 初始六场景的完整图比较通过，但新增coverage gate先检出CommentInput
  未执行，再检出关注页CardItem未执行。补齐可见性/fold输入、共享通知、
  observer及晚注册builder回放后，二十四正常场景全部比较通过，二十个
  实际view constructor身份均出现。新gate另要求各类真实render执行；
  直接构造只能验证构造行为，不能代替render coverage。
- 字段顺序差异来自参考准备：Node会给普通无initializer的class声明
  加DefineField，既有public/private正则未覆盖bare声明。本轮新增test-only
  class_reference，使用Node vendored Acorn8.15.0（MIT、版本断言、需要
  --expose-internals）按AST所属class和UTF-16区间擦除这种普通instance
  声明，保留初始化、方法、computed-key效果、static及private brand。
  不是通用TS编译器或生产依赖；其他既有fixture没有顺带改写。
  解析/遍历O(T)，F个擦除区间排序O(F log F)，输出O(T)。独立adapter
  测试执行setter/初始化顺序、嵌套类、computed key、static和private
  brand，含非BMP文字与字符串中的假class；本机Node25.7.0身份已核对。
- 新GraphTrace先保存所有evaluated module export的descriptor/reference
  图，再分配identity tag，避免错误常量或alias被标签掩盖。完整事件、
  模块状态、字段own-key顺序/descriptor、storage/cell、prototype与签名
  比较保持。AST另比native class、方法顺序、get/set、async/generator及
  参数/default/rest形式；此定向表示检查不等于所有body的表示已验收。
- 单长生命周期Node探针达到V8默认堆上限退出，两个场景worker也在部分
  md/fold输入达到512MiB上限，均不计成功。本图改成每worker一对图、
  512MiB堆及60秒限额，完整比较不缩为hash；realm之间主动释放可回收
  状态，CTest受RUN_SERIAL约束。生产调度/RSS工作仍暂停，不以这个测试
  heap限额证明整个生产进程RSS合规。
- 首轮矩阵的super mutation交换两个undefined参数，未检出，属于场景
  不足；新增不同值的parent/localStorage实参与外部new.target/base返回
  对象构造对照。CTest首轮4/5通过，最后一个有效JS export mutation的
  VM link SyntaxError跨realm，instanceof没有识别；test boundary现核对
  native Error brand和SyntaxError类型。基线错误仍失败，intentional
  mutant可被已解析JS的link失败或严格host contract拒绝。原异常对象
  identity与类别比较保持，诊断message只供失败定位，不要求临时变量
  拼写一致。注册throw的原retained callback还须成功回放。
- 最终定向5/5通过104.41秒：class_reference_adapter、community_graph、
  原ui_conditional_literals/viewmodel_batch/static_class_batch。整图完成96
  场景（64生命周期/宿主状态、32构造配置，每配置实际二十类，共640次
  direct construct）、160原throw、32全局换绑及12有效JS mutation；二十
  类均实际render。两个故障baseline各选十六个明确语义host boundary，
  五种raw值含undefined/null/0/Symbol/object；不是每event或每UI接口
  故障覆盖。完整trace/状态比较保留；生命周期/构造场景事件数24至
  302433，异常/partial状态场景也按实际行为对照，未把它们当正常成功。
- 单-j4 abc_structure/abc_semantic构建通过；CMake585项注册且新图
  RUN_SERIAL属性已核对。六个新mjs语法与git diff --check通过；未跑
  full suite，未重复五ABC六模式三十次导出。日志
  /tmp/abcd-community-graph-{inspect,red,matrix,configure,build,tests}-*.log，
  最终原生JSON另存/tmp/abcd-community-graph-verified-1.json。
- 本图不关闭EntryAbility/Logger全部方法、任意prototype对初始化字段的
  拦截、所有body/style与原结构gate。全面来源映射/DFX、full suite、扩展
  语料/RSS/benchmark、生产scheduler、agents和周期audit仍暂停；阶段一
  Goal保持active，下一步继续尚缺完整独立验收的record/body。

### 2026-10-07：真实 shopping 依赖链／完整视图导航图

- 基线549d7969，production/rules146保持。原计划的全面来源映射/DFX仍
  暂停，不将method map包装作为功能前置。本轮沿原十四模块图接入实际
  shopping barrel/Home/HomeContent、六个section/header视图、CommonView
  七类及四组model/viewmodel、HomeConstants，共新增十九record，三十三
  个真实ABC-only生成模块共同连接；没有继续用Home或其子类替代正文。
- 新shopping_navigation_graph_source_test_0..3复用原导航fixture和ABC-only
  导出/独立paired TS边界；原十/十四模块变体各自保留。实际两个Header
  名字相同，class inventory/instance identity以真实module/export区分，
  不把same-name的minigame Header当作shopping Header已执行。
- Controlled framework新增ObservedPropertySimple、provided/consume
  cell、Scroller、TabContent create/tabBar、ListItem lazy deep render及
  相关UI/enum接口。ListItem第一次lazy初始化记录原callback identity，
  deep render内再次create不递归重复初始化；element/child update沿实际
  父组件callback执行，更新及late paramsGenerator_继续调用真实body。
  Tab change、scroll index和tab builder回放实际闭包，不替换model读取、
  private成员、商品resource构造或render方法。平台仍受控，不推断任意
  ArkUI协议/设备行为，也没有开启引擎调度器/新增生产样本条件。
- 两个baseline分别29,711实际event，三十三module须全部evaluated、
  没有body throw、十五个shopping view export的真实constructor实例均
  存在。七个原class加十五个shopping class共二十二类，按独立TS原
  constructor赋值序列验证0/1/2/3/4/6/200/201实参、foreign new.target、
  继承setter和部分写入；构造矩阵只实例化选定类，module初始化仍真实。
- 新fixture先失败：reference缺失__Common__宿主组件，明确no-body-throw
  baseline检出，补齐host。随后生成/参考字段顺序不同：无type注解的
  private声明没有擦除，Node把它作为constructor前的DefineField。
  统一仅将无initializer的typed/untyped public/private声明标成declare，
  原initializer、constructor赋值、方法与closure均保留。修正后首批64
  生命周期、660构造/setter/partial state以及34有效JS mutation通过。
  Header mutation起初使用错误局部名称导致replace未发生，按实际生成
  getter修正fixture；没有把该失败算成semantic mutation已检出。
- 完整矩阵为482 lifecycle（含xs/sm/md/lg/xl、immersion、nil/zero title
  rect）、660构造赋值、34 mutation；新变体覆盖错误active setter/icon/
  comparison、TabBarBuilder receiver、state传值、back path、scroll边界、
  model方法返回、商品getter，以及此前callee snapshot/资源identity/导出
  等原mutation。完整故障矩阵通过：按distinct event名/首个string参数的
  首次host boundary采样，两baseline各2117项，并非every-event注入。
- 未分片完整测试tests-1到达CTest既有1500.03秒整体上限，退出8，
  期间没有报告语义差异；不能算通过。确认父/子进程结束后，将每个
  phase完整inventory分为四个互不重叠的整数区间；不缩减场景，也不
  提高CTest限额。tests-2四分片3/4通过777.31秒，分片1的单worker
  到达60秒上限（493.10秒时退出），没有给出semantic mismatch。隔离
  diagnostic-1为同一ABC-only导出和完整分片，仅添加批次位置/简短完整
  比较诊断，最终全分片通过121/5292/1059/165/9。无法据此断言原超时
  的唯一原因；通用worker批次减为32且保留60秒限额，所有variant共用，
  每批核对完整counts/inventory/boundaries，并记录完成数和失败range。
  完整比较用isDeepStrictEqual加简短AssertionError，不格式化数万项diff；
  tests-3四分片再次3/4通过657.59秒，分片2在faults first16798/count32
  超时（482.60秒时退出）。fault-batch-1只复现这个确定range，26 raw
  faults/6 swaps全部通过，未复制或改写任何module正文。两次并行超时
  原因尚未唯一确定；这组大型graph CTest加RUN_SERIAL，不与其他CTest
  job竞争，生产scheduler/并发不变。tests-4单独完整分片2在514.80秒
  通过120/5293/1058/165/8，完整运行越过上次超时的确切批次。
- aggregate-1核对tests-3三个完整成功分片和tests-4完整成功分片的
  原生JSON：四个unique shard、同一inventory/counts/boundaries、33
  module、每phase相邻区间从0到完整inventory且各自完成数足额。最终
  482 lifecycle、21170 raw faults、4234 swaps、660 assignments、34
  有效JS mutation全部通过；两个baseline各29711宿主事件、2117 distinct
  边界保持。未完成range/32-case探针均不计入完整分片验收；不是一次
  四并行CTest全绿，也没有宣称唯一超时原因或解除生产RSS约束。
- build-1/build-2/build-3单-j4的abc_structure/abc_semantic通过；最终583项已
  注册且四个新CTest的RUN_SERIAL属性已核对；未执行full suite。
  final-tests-1原十/十四模块及default partial
  定向3/3通过190.05秒：166/10990/2198/96/12与214/3960/792/144/23
  矩阵均保留，default paired486场景通过。两个mjs语法与git diff
  --check通过；简短诊断改动后final-tests-2同三项3/3通过104.18秒。
  最终32-case批次下final-tests-3同三项3/3通过104.66秒：原十模块
  166/10990/2198/96/12、十四模块214/3960/792/144/23及default paired
  486场景保持。最终七个不同定向CTest均有完整成功结果（四graph分片
  分两次运行汇总），两个mjs语法及git diff --check通过。test-only，
  未改生产/rules、输入ABC或重复五ABC六模式30次全量导出。
  日志/tmp/abcd-shopping-graph-{probe,lifecycle,assignments,mutations,
  build,tests,final-tests,diagnostic,fault-batch,aggregate}-*.log。
- 该图推进实际constructor/field/signature/capture/body整合与独立验收；
  本图不单独证明初始化TS字段的任意prototype拦截或WindowUtil全部方法/
  真实平台。剩余record行为/表示、结构metadata和原结构gate仍开放；
  已有组件定向结果保留，不把本次组合验收替代它们。来源映射/DFX、
  full suite/expanded corpus/RSS/benchmark/scheduler/agents/定期audit
  保持用户暂停；阶段一Goal仍active。

### 2026-10-07：真实 minigame barrel／子类接入导航调用图

- 基线12a0805b，production/rules146保持。沿原十模块导航图新增实际
  minigame/Index14270、Content14411、Footer15128、Header15617，共十四
  个真实ABC生成ESM在同一realm连接。原十模块测试及其四个controlled leaf
  独立保留；新图只保留shopping Home及平台服务受控，不再替换三个
  minigame类或barrel exports。先完成ABC-only显式numeric+constructors
  导出并核对SHA/contract/complete/nonlegacy/path，再读独立paired TS。
- Framework fixture按实际indexed/keyIndexed接口调用generator/key，真实
  Content的key收到index；原值对象在进入item closure前赋予identity，
  Image参数必须仍是同一原item。元素ID连接parent到真实child，回放实际
  parent update分支并执行child.updateStateVars；父window cell换值后，再
  调用真实paramsGenerator_两次，将新mainWindowInfo传入真实Synched cell。
  Header/Footer的渲染、删除、空update/purge及参数闭包也完整执行。
- 新baseline明确要求十四模块均evaluated、没有body中的throw，三个真实
  child都存在；每个Content必须实际传递全部60项原资源到Image，更新后
  其cell必须保存新info1，避免reference和generated一起在缺失host处
  停止却误报整图通过。完整trace、状态/descriptor、七个class的prototype
  name/arity/成员顺序及默认参数、module重复evaluate仍分别比较。
- 第一版实际组合通过166 lifecycle、3960 raw faults、792全局换绑、96
  setter/new.target及12旧mutation；同步/延迟baseline各5163真实事件。
  新版本扩为214 lifecycle、144赋值/部分写入及23 mutation，另加入三真实
  child的0/1/2/3/4/6/200/201实参、foreign new.target及继承setter。
  新增错误child/re-export、陈旧parent参数捕获、state传值、closure owner、
  Image资源复制、JSON key、width成员、Header对齐及Footer删除参数变体。
- 新图异常/换绑范围为每种event名与首个string参数对应的首次observable
  host boundary，两baseline各396项；没有把它描述为全部5163 event覆盖。
  原十模块和独立Content保留原every-event矩阵，全部重复资源/索引位置仍
  在新图完整生命周期trace中比较；本次不能替代任意平台协议或设备运行。
- 失败证据：首次fixture读取UI proxy不存在的name，改为只读取plain
  component descriptor；下一次WidthBreakpoint宿主用了错误enum键，按
  实际peer的WIDTH_*边界修正，未改生产语义或扩大恢复准入。补强版
  tests-3在mutation worker触发60秒限额，总139.14秒失败；deliberate
  mismatch的assert.deepEqual生成庞大diff占用预算。改用isDeepStrictEqual
  加简短AssertionError，比较契约不变且不提高限额；同一fresh ABC-only
  mutation batch的23项全部通过，诊断控制器未改任何generated/reference正文。
- 验证：build-1单-j4的abc_structure/abc_semantic通过；final-tests-1
  定向4/4通过246.25秒（Content85.50、default partial0.48、十模块78.34、
  十四模块81.93）。最后完整十四模块结果为214 lifecycle、3960 raw faults、
  792换绑、144赋值/partial state及23有效JS mutation全部通过。原十模块
  166/10990/2198/96/12、Content40/19250/3850/64/9逐event矩阵无回退。
  final-tests-2按实际注册名补验language_constructors_paired_test与
  language_arithmetic_paired_test，2/2通过8.73秒；最终六项定向检查通过。
  579注册不等于运行full suite；两个mjs语法及git diff --check通过。
  test-only，没有生产/rules修改或重复五ABC六模式全量导出。
  日志/tmp/abcd-minigame-graph-{red,build,tests,mutations,final-tests}-*.log，
  原生成功矩阵另存/tmp/abcd-minigame-graph-final-details-1.log。
- 这是实际跨record constructor/field/capture/body整合证据；结构metadata、
  完整method映射、剩余record独立行为/表示、shopping真实库以及原结构gate
  仍开放。全阶段Goal保持active；full suite、expanded corpus、RSS/benchmark、
  DFX/scheduler、agents和定期audit仍维持用户暂停。

### 2026-10-07：原缺口 Content 的完整 record／真实资源闭包验收

- 基线ddfd8749，production/rules146保持。核对实际五ABC输出：授权numeric+
  constructors组合的源码没有INCOMPLETE/UNRECOVERED，默认剩余标记来自
  保留的数值/构造契约。因此继续这些原缺口的完整功能与独立验收，未新增
  omission包装、扩展样本匹配或再优化?.细节。旧Content两项仅验收资源数组/
  key callback，没有完整constructor、state、delete或渲染闭包执行证明。
- 新content_record_source_test核对Immersive SHA/size，先ABC-only完整导出
  Content14411、Breakpoint7395、CommonConstants7167，退出后才读取独立TS。
  route/contract/complete/nonlegacy/executable:false与真实源路径核对。
  三个生成模块在同一realm执行；barrel的两个export受控地连接真实peer，
  没有替代Content constructor/方法、getBirdImages、嵌套item/deep-render/
  Image callback或Breakpoint.getValue body。平台ViewPU/UI/storage仍受控。
- 40组完整生命周期对照，覆盖sm/md/lg/xl、同步/延迟、正/逆callback及item
  调度、两实例交错、更新/清依赖/删除后回放、fallback初始化和paramsLambda。
  实际60项资源及每个params数组独立性、key内容、Image原资源身份、closure
  owner/receiver、注册后失败保存同一callback、事件顺序和部分cell状态核对。
  两种渲染baseline各1,919真实事件全部覆盖，没有固定事件上限；19,250次
  undefined/null/0/Symbol/object原throw、3,850逐event全局/import换绑通过。
  其中第三baseline为继承setter及foreign new.target/返还替代对象构造12事件。
- 原生class/export/prototype完整inventory、父类/constructor身份、七方法与
  getter的name/arity/descriptor/不可构造、module TDZ/export不可写与重复
  evaluate检查通过。constructor六formal、elmtId/paramsLambda默认、JS length3；
  0/1/2/3/4/6/200/201实参、paramsGenerator_/__mainWindowInfo两个真实赋值，
  own-key/descriptor、继承setter、Proxy new.target的prototype读取及baseObject
  路径共64组通过。setter保存原值后的原throw和部分写入在第三baseline另验。
- 九个先解析为有效JS的mutation检出错误super实参、state值、width字段、
  GridItem比例、Image资源、CommonConstants读取、JSON key、fallback guard
  和DefineField绕过继承setter。reference只擦除一个无initializer私有字段和
  peer四个type-only字段；其余constructor/方法/closure/module正文保留。
  new/arrow/Array.from/remainder/string/key/native method等高层表示另验，
  允许必要临时值及名称保留表达式，不要求逐字相同。
- 失败证据：tests-1的fixture误把first-default前参数数写为2，实际源码与
  生成constructor均length3；修正fixture。tests-3最后fallback mutation使
  descriptor缺失，host先读value而抛TypeError，不能计作mutation检出；改为
  明确断言descriptor存在，未放宽对有效JS/AssertionError的要求。tests-2
  早期40/19190/3838/32/8通过；tests-4补强40/19250/3850/64/9通过83.69秒。
- final-build-1两个现有消费者单-j4退出0；final-tests-1定向6/6通过95.33秒，
  包含新Content、旧default partial、parameter origins、完整Breakpoint/
  CommonConstants和独立语言算术。578注册不等于执行full suite，三个mjs
  语法和git diff --check通过。test-only，没有生产更改或无理由重复六模式
  输出。日志/tmp/abcd-content-record-{inspect,build,tests,final-build,
  final-tests,details}-*.log；fresh观察产物build/stage-one/content-record-inspect-1。
- Content14411的受控whole-record行为与表示由pending转accepted，不把
  controlled framework/barrel当作设备、完整commons图或任意平台协议验证。
  未清除metadata结构pending、完整method映射缺口、其他record独立验收
  或原实例字段/globals/signatures/body结构gate；阶段一Goal仍active。
  full suite、expanded corpus、RSS、benchmark、DFX、scheduler、agents及
  定期audit维持用户暂停。

### 2026-10-06：丢弃结果的原生可选调用（规则146）

- 基线b4c4191d。沿原共同null/undefined/other解释新增语句的有序效果摘要，
  ExpressionStmt／Block／If共享reference与formation；表达式／语句typed
  projection root复用同一ProjectOptional、member path、printer和来源计划。
  nil两域无保留效果、其他域保留同一原调用/成员且形成顺序相同时，输出
  原生optional statement。原callee、receiver、key、参数、RHS、执行次数和
  raw异常保留，不修改CFG/AST/ISA，未加sample/name、同块、数量或局部序列准入。
- opaque效果之后的域依赖条件不能继续使用旧reference摘要；不同binding、
  缺失nil域、第二个独立效果继续保留原语句。declaration、return/throw、
  loop、handler、debugger和gap没有丢弃表达式摘要，继续输出原恢复语法；
  这是表示范围，不能描述成语言禁止或整函数拒绝。保留实际语句完成与scope。
  一般多效果sequence、分离形成、跨reference整个chain仍开放。
- 新共同树80组正例覆盖1/200/201/402 guard、逆序语句arena、形成base、
  成员链/计算key/value call；32,000独立行为及两项有效JS mutation通过。
  原callee/member/参数保留非零range，省略expression/property/statement和
  原block anchor统一落在保留base.end；family和serializer使用同一typed根。
  六项额外效果／abrupt／debugger／效果后guard／foreign／缺失nil反例及
  工作/内存不足的typed错误另验；201→402 work边界不冒充benchmark或RSS。
- 实际decoded矩阵增加void调用与live结果汇合，三个profile×四深度×28 forms
  共336函数／75,264独立场景。初始新增用例在tests-3失败：LDUNDEFINED后
  RETURN产生真实phi写入，不能被void投影一起删除；该用例保留作表示反例。
  真正RETURNUNDEFINED观察另验，不弱化写入/来源义务。最终两类行为均通过。
  新测试先RED证明缺少native statement；重构曾遗漏?? undefined fallback，
  已修正独立fallback分支并复验。失败和成功日志均保留。
- 固定五ABC／六模式30次fresh ABC-only导出通过SHA/size、route/contract和
  636 Node语法解析，complete无新增/回退、无legacy。complete/partial依次
  80/26、95/11、96/10、88/18、105/1、106/0，每模式另46 no-module。
  每模式仅MiniGame12527和ShoppingFullScreen13504源码变化：原两项
  setSystemBarEnabled及setImmersiveType/startWindowStatusListener/release
  void guard输出?.。212个现有entry_only/partial_methods位置文件均不变；
  两record相关method映射仍有空缺，组件位置证明不能替代完整record映射。
  产物build/stage-one/optional-statements-*-pass-1，日志/tmp/abcd-optional-statements-*。
- 构建/验证：final-build-1的已存在消费者构建通过，最后误列不存在的
  language_constructor_file_test导致退出2；确认真实目标后，单一-j4的
  final-build-2（snapshot_syntax/language_constructors/abc_semantic/abc_structure）
  退出0。final-tests-1运行28项，27项通过，MiniGame旧固定5个?.断言失败；
  ABC-only实际输出与独立参考均7个（新增两void调用），将style断言放到生成
  后读参考并比较，final-tests-2该项1/1通过2.49秒。前27项包含真实十模块
  导航图、Index、既有空值/位置/模块族及显式构造；无生产修改或放宽行为。
  28项定向检查全部获通过证据；577注册不等于运行full suite。四个mjs语法
  与git diff --check通过。日志/tmp/abcd-optional-statement-{red,build,tests,
  final-build,final-tests}-*.log保留失败与修正，tests-4为早期4/4通过52.40秒。
- 下一优先级遵循用户要求：优先推进INCOMPLETE/UNRECOVERED的共同恢复与
  诚实部分输出路径，剩余?.表示细节随后处理；不因局部风格完成取代功能gate。
- 四页面whole-record style仍pending：ShoppingAvoid外层跨reference形成链
  等表示缺口仍开放。全阶段的结构gate、其余独立record/平台/whole-program
  验收未完成，Goal保持active；用户暂停的full suite、扩大corpus、RSS、
  benchmark、DFX、agents、定期audit未恢复。

### 2026-10-06：Immersive Breakpoint 四字段／全局读取／完整 record 验收

- 基线977cbbe0，生产规则仍145。新增immersive_breakpoint_record_source_test，
  核对固定Immersive ABC SHA后，ABC-only默认V3的--value-source从entry7531
  生成record7395完整源码；route／complete／无legacy／executable:false核对后，
  才读取独立paired Breakpoint.ts。真实module/class/constructor/getValue正文
  全部执行，没有参考body拼接或sample/name分派。此前HomeConstants连接只
  有较窄行为证据，本次补齐该record受控whole-record行为及高层表示验收。
- 1,496组输入、动态enum及receiver对照通过：undefined/null/falsy/-0/NaN/
  Infinity/BigInt/Symbol、禁止coercion对象，正常／全部alias／各分支alias／
  严格Boolean比较、changing字段、nullish/scalar/missing全局enum，真实
  instance、foreign proxy和primitive/nullish receiver。每次原全局读取／
  WIDTH_MD、WIDTH_SM、WIDTH_LG getter及实际返回字段顺序、身份与状态核对。
  5,160个实际event逐点替换全局enum，证明getter取得后保留原值、后续读取
  观察新binding；25,800次undefined/null/0/Symbol/object原throw逐事件验收，
  无固定event上限或把内部TypeError／ReferenceError当作原throw成功。
- sm/md/lg/xl四项真实assignment、constructor四formal与getValue一formal、
  export／class／method name/arity/descriptor、native new-only及方法不可构造
  检查通过。0/1/2/3/4/6/200/201实参数量保留missing／extra／原值身份，字段
  own-key顺序与descriptor另验。复用test-only breakpoint_state构造oracle，
  参数化field/global/import/lookup输入：24组prototype/setter/new.target场景、
  288次原异常与部分字段写入通过，包含两realm fallback。旧Community三字段
  360 lookup／24 constructor／240 throw与六mutation仍通过，未删除其检查。
- 11个先解析为有效JS的mutation检出错误返回／receiver、strict→loose、
  额外enum读取、缓存全局enum、错误第四字段值、错误构造return及DefineField
  代替assignment。独立TS只擦除四项无initializer类型声明；constructor所有
  写入／method／module保持，不能把Node retained DefineField当作Ark赋值oracle。
  该recordwholeRecordBehavior与wholeRecordStyle由pending转accepted；native
  class/signature/member/ordered if及等价最后if极性允许，TS generic/type与
  平台WidthBreakpoint定义本身无恢复证明，controlled host不冒充设备运行。
- 首轮原throw断言失败，最小独立探针确认Node Contextify把外部安装的global
  accessor异常改成ReferenceError。修正test host为realm内部原生getter，再
  执行原host callback，任意throw身份检查保持；同时修正state snapshot必须
  在method调用后取得。tests-1/2失败保留，不当作生产恢复bug或盲放宽验收。
  单一-j4 abc_structure／abc_semantic构建及configure退出0，两mjs语法／diff
  检查通过；首个修正后2/2通过0.98秒，最终六项相关class／constants／
  Community／ViewModel record回归6/6通过7.54秒。575项注册，仅运行上述
  定向范围；日志/tmp/abcd-immersive-breakpoint-{build,configure}-1.log、
  tests-1/2/3.log、details-3.log、final-tests-1.log、final-details-1.log。
- Immersive及Community输入SHA/size与既有paired index逐项一致。没有生产
  source规则变化，未重复六模式三十次导出；上一轮106/0等统计是977cbbe0
  的证据，本轮fresh完整record为上述默认V3输入。按当前status继续T08/T11
  的剩余record字段／global／签名／body独立验收，不能重跑旧计划已交付任务
  或以新增覆盖代替全部阶段一。自检核对actual body、参考隔离、原throw／
  state／mutant断言与共同oracle旧覆盖；agents／全套／expanded corpus／RSS／
  benchmark／scheduler/DFX／周期审计保持暂停，原结构gate及Goal仍active。

### 2026-10-06：共同 nullish 投影恢复复合成员链与成员调用

- 基线3cd68094，生产规则145。原PlanNullishSyntax只允许terminal member的
  直接base具有稳定SymbolId／This owner，这是表示限制。现对原MemberExpr
  依赖自底向上保存一次共同member path摘要，OptionalMember／OptionalCall
  保留terminal member、首个guarded access及原形成base；CallExpr的callee
  同样进入依赖调度。没有样本、名称、opcode序列、producer名单、同块、
  固定寄存器／PC、selection数量或graph shape准入。
- 共同迭代printer输出x?.a.b、x?.a[key]及x?.a.b(args)。首个access之后的
  member仍mandatory，保留中途nullish的TypeError；不插入中途括号结束
  short-circuit。原key转换／getter／参数只在原guard允许时执行，member
  call保留最后真实receiver。外部member／call仍结束本次chain，value call
  仍间接；原形成assignment、任意producer效果／异常／TDZ义务保持。
  每个保留member有真实非零range，省略guard仍在原base.end有零宽event；
  family emission／位置serializer复用同一计划，未复制或新建AST。
- 新增96个共同树fixture：深度1/2/3/200/201/402、named／computed key、
  read／member call、原形成assignment与正逆arena。57,500组独立语言
  对照核对root／中途nullish、falsy/NaN/BigInt/Symbol、key/getter/argument/
  producer的顺序、receiver、一次求值与undefined/null/0/Symbol/object原
  throw；4个先解析的有效JS mutation检出多加?.及中途括号的语义错误。
  201到402的work几何边界、真实serializer及保留／省略来源range另验。
- 既有snapshot_optional增加五项实际decoded复合成员／计算键／调用／
  copy与split组合。三profile×1/200/201/402×23 forms共276函数贯通
  FunctionInput→ValueIR→Control/Evaluation→FunctionTree→语义／来源／
  family writer；61,824组独立行为通过。旧optional_access的effectful base
  正例由RED转native projection，152 fixture／13,376场景保持通过。
- 失败证据保留：red-tests-1先因旧直接base限制失败；首次新增target需
  configure；深链fixture曾有unowned多余symbol，修正fixture；逆序arena
  depth3 member call随后暴露真实callee依赖缺失，修正共同依赖调度，
  未放宽ownership或加局部shape分支。最终单一-j4十二target构建退出0，
  26/26定向回归通过200.50秒；包括新深链、既有nullish/来源、实际十模块
  navigation图、Index／constants／WindowUtil及显式构造回归。新增两项
  后CTest注册574项，仅运行上述26项。三个mjs语法与git diff --check通过。
  日志/tmp/abcd-optional-chain-{red,final}-tests-1.log、final-build-1.log、
  final-details-1.log及tests-2/3/4.log保留真实失败与成功。
- 固定五ABC六模式30次fresh ABC-only ValueTree导出退出0，SHA／size不变，
  无legacy；636份源码全部解析，完整record无回退／新增。按default／
  arithmetic／numeric／constructors／arithmetic+constructors／numeric+
  constructors，complete/partial仍80/26、95/11、96/10、88/18、105/1、106/0，
  每模式另46个no-module。每模式仅ShoppingAvoid11877源码变化，实际padding
  已为?.topRect.height／?.bottomRect.height；外层不同reference形成的
  conditional仍保留。212个现有entry_only／partial_methods映射文件逐模式
  不变（该record两项positions为空），不称完整record source mapping；
  独立fixture的member来源位置另验。日志/tmp/abcd-optional-chains-batch-1.log、
  各mode-summary-1.json，产物build/stage-one/optional-chains-<mode>-pass-1。
- 本次交付共同复合member spine，未将全部四页面的whole-record style改为
  accepted。分离形成／一般alias transport／跨reference整体chain及optional
  void statement表示仍是实现缺口；原结构gate和阶段一继续active。
  106 source_complete仅为组合模式引擎输出状态，独立全部record行为与
  高层表示门槛仍需各自证据。全套／expanded corpus／RSS／benchmark／agents／
  scheduler/DFX／周期审计保持暂停；未重跑已暂停G2 global-call slice或旧
  base_constructor_js_test，历史失败不伪装为通过。

### 2026-10-06：Index 导航十模块图与四个实际页面的组合行为

- 基线e49a93a3，生产规则仍144。新增navigation_graph_source_test，按固定
  Immersive ABC SHA核对输入，ABC-only显式numeric+constructors导出整个
  record tree后，才读取独立paired TS。十个实际生成ESM在同一realm链接：
  Index9572、ROUTES8755、Shopping11285、ShoppingAvoid11877、MiniGame12527、
  ShoppingFullScreen13504及commons6888／7167／7395／7618。实际路径、
  record身份、complete／ValueTree／无legacy逐项核对，无参考正文拼接。
- 相比上一轮，Index构造的四个页面不再用测试class代替。实际class进入
  super、字段、accessor、initialRender及NavDestination内容closure；真实
  commons re-export链提供CommonConstants／ImmersiveType。只有平台接口和
  shopping Home、minigame Content／Footer／Header四个库叶子受控。WindowUtil／
  Breakpoint的模块初始化和export参与本图，不宣称本图执行其所有方法。
- 166组完整图／生命周期／构造场景、10,990组undefined/null/0/Symbol/object
  原异常、2,198组全局／storage／live imported leaf换绑通过。同步／延迟
  两条baseline各1,099个实际事件全覆盖，inventory／实际counter核对。
  page渲染后立即purge／delete，再释放实际内容、组件、shown／hidden和click／
  bound builder回调；两种队列顺序、initial/update、changing storage读取、
  nullish及falsy/NaN/BigInt/Symbol window、finalize、父对象return另验。
  注册先retain后throw，断言精确原callback随后成功重放；方法void结果不得
  泄漏或assimilate宿主poison then。各页面的0/1/2/3/4/6/200/201实参、默认
  elmtId／paramsLambda、native new-only／heritage和替代new.target进入真实构造。
- 96组四页面全部实际构造属性的pinned assignment检查覆盖inherited setter、
  alternate prototype、任意原throw、receiver及部分own-key写入顺序。独立TS
  parser前仅把未初始化类型声明标为declare；initializer／构造／方法保持。
  WindowInfo的Observed在本图是恒等host，参考仅擦除这个decorator；不把本图
  当作decorator替换／异常验收，也不将Node initialized DefineField当赋值oracle。
- 12个先解析为有效JS的mutation全部被真实图检出：wrong super／receiver、
  padding值、cell删除、shown/hidden参数／调用、live Content参数、setter目标、
  void返回及赋值替成DefineField。MiniGame把取得callee从参数效果前延后到
  参数之后的mutation，在真实window getter期间换绑Observed构造器时检出；
  对照原生成代码保留captured constructor identity。仅测试中使用文字mutant，
  生产没有路径／名字／文本／graph shape／same-block条件。
- 单一-j4 abc_structure／abc_semantic构建退出0，三个mjs语法与git diff
  --check通过，CTest新增一项后572项。首轮整图1/1通过59.93秒；增加直接
  构造、setter、精确retained重放及mutation后1/1通过74.27秒。相关Index／
  NavMap／initialRender、两常量整record、WindowUtil、显式构造和MiniGame
  回归8/8通过27.63秒。日志/tmp/abcd-navigation-graph-build-1.log、
  tests-1.log、tests-2.log、details-1.log、details-2.log、regression-1.log。
  每worker顺序64项，核对全inventory，不作生产RSS／性能改善声称。
- 这补齐四页面与Index实际组合的受控行为证据，不将wholeGraphBehavior扩成
  所有模块／库叶子／平台／源码风格验收。ShoppingAvoid复合可选成员访问仍有
  长conditional链；MiniGame／ShoppingFullScreen的optional void调用仍有
  显式guards。共同nullish planner目前以单个稳定reference域及直接member
  为native投影，复合成员链的整体表示仍是实现缺口，下一步沿共同AST、身份、
  效果和异常次序完善，不能再加入样本shape匹配。原结构gate及阶段一继续
  active，structure_only不清除；全套／expanded corpus／RSS／benchmark／
  agents／scheduler/DFX／周期审计保持暂停。无生产修改，未重复固定六模式
  三十次批量导出；本轮的fresh单ABC组合模式输出仅服务上述整图验收。

### 2026-10-06：Index 完整 record、真实 ROUTES 与字段／方法贯通验收

- 基线ae9cd4ab，生产规则仍144；只扩展既有index_record_source_test及
  test-only oracle。先核对固定Immersive ABC SHA，再完成record9572 Index／
  record8755 Constants的ABC-only默认ValueTree导出，两项均complete且无
  legacy，随后才读paired TS。执行完整生成Index和真实ROUTES依赖；child
  view类和平台接口受控，未替换实际constructor／method／callback正文。
- 保留旧36组module／constructor检查，新增136组完整生命周期、6,170组
  实际可达事件的undefined/null/0/Symbol/object原异常、1,234组实时换绑、
  36组字段赋值和10个先解析为有效JS的mutation。同步／延迟两条baseline
  各617个实际事件均逐项fault／swap，没有用固定事件上限截断。真实
  initialRender、NavPageMap、八个prototype方法、两对访问器及static方法
  执行；ROUTES驱动列表／JSON key／click／builder／child构造，比较callee、
  receiver、captured identity、参数、原throw及部分字段／cell状态。
- 注册先保留closure后throw，断言原异常及原callback在失败后成功重放；
  purge／delete后以foreign this释放实际callback／click／bound builder。
  覆盖两实例、正反队列、initial/update分支、缺失／继承／不可调用／不可扩展
  finalize、父构造对象return、changing getter、falsy/NaN/BigInt/Symbol及
  0/1/2/3/4/6/200/201实参。每个实际事件可替换组件、JSON、SubscriberManager、
  NavPathStack／Observed构造器和四个live imported child，保留取得的callee
  身份；void宿主结果不能泄漏或触发poison then。
- Node strip会把paired TS未初始化private类型声明变成空JS字段，产生ABC
  没有的DefineField。仅在独立参考准备中将两项未初始化声明标为declare后
  擦除，所有initializer／constructor赋值／方法保持原样；生产没有文本匹配。
  三项paramsGenerator_／__navPageInfos／__windowUtil setter、alternate
  new.target、任意原throw、receiver及部分写入顺序另作pinned assignment
  断言。把真实赋值替成Object.defineProperty的mutation必须被检出。
- 表示独立核对原生class／constructor／super／默认参数／accessor／arrow／
  member／literal／逻辑OR及UI调用；完整own-key顺序、name／arity／descriptor
  和类调用边界由运行时断言核对。接受mutable Index binding、receiver别名、
  等价if极性及必要的函数名称envelope，不声称恢复TS type/private/const。
  该Index record的受控whole-record语义／高层表示由pending转accepted；
  四个child的完整正文、设备运行、其余record及原结构gate仍须自己的证据。
- 单一-j4十四目标构建退出0，三个mjs语法及git diff --check通过。扩大
  回归18/19通过47.54秒，唯一失败为base_constructor_js_test旧writer的
  assignments检查（base_constructor_test.cc:58），与本页既有基线失败一致，
  未计入通过或修复。当前ValueTree的class_accessors／derived_constructor／
  derived_class_heritage三项补验3/3通过。实际Index新验收及NavMap／initialRender、
  两常量整record、两Widget、signatures／globals／partial与显式构造语义均通过。
  日志/tmp/abcd-index-record-final-build-1.log、final-tests-1.log、
  final-ctest-details-1.log、final-tests-2.log；CTest仍571项。
- 初版oracle失败于未tag的ViewPU.prototype触发getter；修正后tests-2/3/4
  通过。加入同步fault后tests-5发现builder阶段晚注册的失败callback未重放，
  将精确retained callback重放放到完整调度末尾后tests-6通过21.10秒；均为
  test-only修正，失败日志/tmp/abcd-index-complete-tests-{1,5}.log保留。
  子进程顺序分批64场景并核对inventory／counter；不作生产RSS声明。
  本轮无生产修改，未重复固定五／六模式30次导出，规则144的literal-layout
  pass-2仍为现有批量证据。structure_only未清除，阶段一Goal继续active；
  全套、expanded corpus、RSS/benchmark、agents、scheduler/DFX、周期审计暂停。

### 2026-10-06：通用嵌套字面量排版与两份常量 record 整模块验收

- 基线20ad0495，生产规则144。共同TreePrinter按真实ObjectExpr的非空
  aggregate子值选择分行，标量字段／空字面量保持紧凑；ObjectCursor沿既有
  OwnedAction保留tree/depth/origin。对象／数组元素明确使用typed
  AssignmentExpression语法上下文，去除该位置多余对象括号；其他目的地
  的原分组、computed __proto__、函数推断名和operator优先级不变。没有
  recovery准入、SSA/value/reference/effect/exception证明变化、样本分派、
  源文本生产匹配或第二套writer。来源／复杂度见literal-initialization与
  algorithms：每literal多一次owned property扫描，O(P+实际输出字节)，
  既有迭代action stack／work／output限制继续计费，未作RSS或性能改善声称。
- value_constants_record_test和value_static_class_record_test先完成实际
  Immersive ABC-only默认ValueTree导出，再读paired TS。固定SHA与record
  身份8755/7167、complete且无legacy均核对；新共同test oracle实际运行
  全部生成ESM及真实import consumer，不拼接参考body或代替初始化。
- ROUTES的29个literal对象／数组按完整身份图、own keys／descriptor、
  realm prototype、空洞、共享／独立identity及extensibility比较。普通／
  hostile prototype两种整模块场景均通过；Object/Array/Function/Reflect
  可替换global全部undefined时仍创建正确字面量，inherited setter不得执行。
  同realm独立module各节点不共享；consumer读取、nested mutation、删除、
  preventExtensions及重复evaluate保留同一graph，另一module保持原状态。
- CommonConstants原生class、三项static字段、constructor/prototype、
  runtime name/arity/descriptor和不可普通调用边界通过。普通／hostile两种
  整模块场景、20项构造场景覆盖0/1/2/3/4/6/200/201实参、ignored poison
  argument、new.target的class／foreign／realm fallback身份；20项原异常
  检查undefined/null/0/Symbol/object，不调用new.target本体。静态数组
  fresh identity、-0/NaN修改、字段替换／删除、live consumer及重复evaluate
  保持修改。两record合计16个有效JS mutation检出错值／目标／export、共享
  params、hole／undefined差异、错误prototype／freeze、错误constructor
  arity／own field及把static own definition改成inherited-setter赋值。
- 旧打印器在新ROUTES排版断言真实RED，/tmp/abcd-constants-layout-red-1.log。
  首轮8/8定向回归通过；新oracle的freeze mutation先触发TypeError而非
  期待AssertionError，tests-2 1/2通过；将独立初始graph比较置于mutation
  操作之前，错误extensibility由真实graph检出，tests-3 2/2通过，未改生产。
  首个扩大构建误用了不存在native_function_names_test target退出2；按
  已注册的module_family_print_test／record_import_tree_test修正，最终单一
  -j4十六目标构建退出0，build handle关闭后29/29定向CTest通过150.11秒。
  其中phased_literal三profile的200/201及几何规模unit通过100.68秒，
  literal buffer含200/201/402，nested独立558个getter/Proxy/identity/
  原异常场景及原span核对通过。新增四个mjs语法、git diff --check通过；
  census-1三项复验通过。主要日志/tmp/abcd-constants-layout-final-build-2.log、
  final-tests-1.log和census-1.log；CTest仍571项，无暂停的旧G2位置切片。
  最终补齐ExpressionGrammar variant/enum的穷尽处理后，final-build-3六目标
  单-j4构建退出0，final-tests-2相关8/8复验通过11.89秒。
- 当前规则固定五ABC／六模式30次fresh导出退出0，实际SHA/size匹配paired
  index、contract／ValueTree正确且无legacy；每模式106份源码解析通过。
  相比rule143 global-scopes-pass-1，各模式35/43/43/35/43/43份源码排版
  改善，complete/partial仍80/26、95/11、96/10、88/18、105/1、106/0，各
  另46份no-module，无完整回退／新增完整。212份既有position maps每模式
  逐字不变；这些仍是entry_only/partial_methods/complete:false，不称完整
  mapping。产物build/stage-one/literal-layout-<mode>-pass-1，日志
  /tmp/abcd-literal-layout-batch-1.log与各模式-summary-1.json。
  同最终穷尽处理代码再导出30次pass-2，全退出0／636份解析通过，与pass-1
  每份源码及既有位置文件逐字一致；日志/tmp/abcd-literal-layout-batch-2.log
  和各模式-summary-2.json，当前产物可用literal-layout-<mode>-pass-2。
- 两份指定常量record的whole-record语义与高层表示由pending转accepted；
  接受保守let、显式空constructor及资源标量紧凑表示，不宣称源级const/
  readonly/type恢复。其他record与instance fields/globals/完整signatures/
  body结构gate仍开放，不清除structure_only；阶段一Goal继续active。
  全套、expanded corpus、RSS/benchmark、agents、scheduler/DFX和周期审计
  继续暂停。

### 2026-10-06：两份 WidgetCard 的完整模块与生命周期验收

- 基线69aa340f，生产规则仍143。扩展widget_global_literal_source_test，
  先完成push modules.abc record12347及widgets.abc record564的ABC-only
  默认 --value-source输出，再读独立paired WidgetCard.ts。实际ABC SHA/size
  与既有paired index、manifest身份一致，ValueTree、V3、complete且非legacy。
  没有引擎sidecar输入、原body替换、参考fragment拼接或生产样本分派。
- 两份完整ESM实际执行module prelude/polyfill、LocalStorage、class创建、
  ViewStackProcessor启动/结束及loadEtsCard；重复evaluate不得重复效果，
  errored module再次evaluate必须保持原异常身份。实际constructor、六个
  prototype方法、三对访问器和static getEntryName全部进入真实调用路径；
  name/arity/descriptor、默认参数/四项super实参、类调用边界另验。
- 两record合计6,392组独立完整行为对照，其中4,100组真实可达事件的
  undefined/null/0/Symbol/object原异常注入、820组global component与
  SubscriberManager换绑。另覆盖缺失/自有/继承/非callable/不可扩展finalize、
  parent对象return、两实例的正反队列、同步/延迟注册、changing getter、
  falsy/NaN/BigInt/Symbol image、0/1/2/3/4/6/200/201实参数量与poison params。
  注册先保留closure再throw，断言原throw后仍执行原retained callback；purge/
  delete后用foreign this重放真实组件callback及click，原capture/receiver/
  callee身份、部分字段/cell状态和效果顺序逐项对照。void host结果有poison
  then getter，不能由真实方法返回或被assimilate。
- 72组实际生成record的pinned field assignment检查覆盖三项__formId/
  __text/__image inherited setter、替代new.target、任意原throw及部分字段
  顺序/新鲜cell身份。Node initialized TS field的DefineField不作此层oracle；
  单独按原STOBJBYNAME写入义务验收。24个先解析的有效JS mutation检出
  错误字段/set/purge/delete、重复id读取、void返回泄漏、module终止顺序、
  router参数、默认elmtId/lambda、super参数、捕获receiver及DefineField替代
  assignment。原176 globals场景、864 paired constructor、60 throws与6
  constructor mutation继续保留并通过。
- 首轮完整对照通过。随后mutation暴露新oracle只记录owner身份、没有记录
  own fields，补上真实own key/descriptor/value与构造后partial snapshot后
  错误paramsLambda默认值被检出；null/undefined fault也按真实身份传递。
  另一个候选mutation把alias换回arrow lexical this，实际语义等价，不能
  当反编译错误；已改为错误realm receiver。即时branch/observe callback
  原异常现在正常传播，fault/swap必须断言实际触发，不计未进入的场景。
- 扩大单进程测试曾在第一份record3,196组通过后发生Node heap OOM，日志
  /tmp/abcd-widget-record-tests-4.log保留，非通过证据。独立harness改为
  128项一批的顺序子进程，全部完整ESM/场景仍执行；inventory与每batch
  census/总数核对，禁止漏项。没有增大heap来掩盖问题，没有改变引擎执行器、
  语义或规则；这不声称生产进程RSS已经测量或满足1G。
- 最终单一-j4构建两个engines及12个相关target退出0，所有build handle
  关闭后20/20定向CTest通过39.23秒；新增callback-retention断言和receiver
  mutation后Widget单项复验通过10.32秒。主要日志/tmp/abcd-widget-record-
  final-build-1.log、final-tests-1.log与final-tests-3.log；测试三个mjs语法
  和git diff --check通过，CTest仍571项。没有恢复旧G2位置切片/全套测试。
- 两个指定Widget record的受控whole-record语义与高层表示由pending转
  accepted。允许必要的私有class/storage creation carrier和receiver capture
  alias、等价if极性；保留原生class/constructor/super/default/访问器/arrow/
  member/object literal/初始化与生命周期表达，不称源级type/private/const已
  恢复。没有HarmonyOS框架/设备执行receipt，不能扩为其他UI record或平台。
- 本轮没有生产变化或重复固定五ABC/六模式30次导出，上一规则143
  global-scopes-pass-1仍为既有80/26、95/11、96/10、88/18、105/1、106/0
  完整/partial证据。两份Widget是本轮fresh导出，不把完整数量当独立验收。
  其余record、instance fields/globals/完整signatures/body结构gate仍开放，
  不清除structure_only；阶段一Goal继续active。全套、expanded corpus、RSS/
  benchmark、agents、scheduler/DFX和周期审计继续暂停。

### 2026-10-06：按实际词法作用域校验全局与宿主能力读取

- 基线7e35a651，生产规则143。移除TreeGlobals把所有局部symbol名称合并
  作为全局遮蔽证据的准入；复用原TreeStructure迭代遍历和TreeScopeCheck
  的active spelling索引，每个真实GlobalName/realm/Symbol/arguments工厂
  表达式在实际激活词法scope查询。兄弟块同名局部不再误拒绝；当前/祖先
  scope、参数、module/capture/self与声明前TDZ仍遮蔽。外层完整destination
  的record/import/host/parameter索引检查继续保留，无按变量重走CFG。
- TreeGlobals在真实发布边界重新执行共同structure证明，不接受可转移的
  scope certificate。未知或损坏的AST返回新增typed kStructure；真实本地
  遮蔽由typed kGlobalShadowed精确转为kShadowed，资源/work限制仍区分。
  原AST、来源、receiver、作用域及效果/异常校验保持，没有opcode/PC/
  register/同块/邻接/producer数量、样本或源文本生产匹配。
- 新tree_global_scopes_source_test实际打印48份scope-bearing语法树并做
  1,728组独立JS对照及5个有效JS mutation；覆盖全局/realm/Symbol/工厂，
  0/1/200/201/402/804兄弟作用域、读取前后不同遍历顺序、对象身份、全局
  getter/missing/原throw、声明前后TDZ及factory实参。C++另验实际body
  declaration遮蔽、原节点打印范围、损坏ownership拒绝和几何work增长。
  这是作用域/发布/printer证据，不称这些48份输入来自ABC解码；既有
  literal_global_test/source_test的三profile实际解码及effect对照也通过。
- 旧生产规则在新的兄弟块测试真实RED，日志/tmp/abcd-global-scopes-red-
  test-1.log。初次扩大构建误选不存在function_family_test target，改为
  实际module_family_print_test后单一-j4构建两个engines及16个相关target
  退出0；build handle关闭后测试。最初20项19/20，新mutation oracle未
  区分两个空host对象身份且snapshot文本期待错误；修正测试后单项通过，
  未因此改生产语义。最终扩大22项21/22，实际global/family/class/partial/
  module/WindowUtil/Widget独立行为均通过，含新48/1728/5和class54/468/6。
- 此次扩大列表误含暂缓DFX的value_tree_global_call_slice_test，实际失败于
  旧位置文本期待`(this.id__())`而输出`this.id__()`，未进入后续行为验收。
  保留失败证据，不将它算成功、不更新旧G2位置oracle、不继续该暂停工作。
  主要日志/tmp/abcd-global-scopes-final-build-2.log、final-tests-1.log、
  final-tests-3.log、final-tests-4.log；新增Node语法及git diff --check通过。
  CTest当前571项，未运行全套。
- 固定五ABC／六模式30次当前规则ABC-only导出退出0，输入SHA-256/size
  与既有paired index一致、ValueTree且无legacy，每模式106份源码语法通过。
  对比规则142 class-declarations-pass-1，实际源码与212份既有位置文件
  每模式逐字一致；完整/partial仍80/26、95/11、96/10、88/18、105/1、106/0，
  各另46个no-module。当前产物build/stage-one/global-scopes-<mode>-pass-1，
  日志/tmp/abcd-global-scopes-batch-1.log与*-summary-1.json。位置文件仍是
  entry_only/partial_methods/complete:false，非完整mapping证明。
- 本次关闭函数级名称union的过度拒绝，固定阶段一语料没有新增complete。
  公共结构遍历新增一次常数倍发布复验，成本/内存和已有算法来源见algorithms。
  instance fields/globals/完整signatures/body结构gate、其他整record独立
  验收仍开放；阶段一Goal继续active。全套、expanded corpus、RSS/benchmark、
  agents、scheduler/DFX和周期审计继续暂停，旧G2失败不作为这些工作的恢复授权。

### 2026-10-06：共同词法证明驱动的原生 class 声明

- 基线f0eda36b，生产规则142。TreePrinter对真实可写声明的直接ClassExpr
  initializer复用原ClassNames词法计划，名称与真实runtime name一致且新增
  class-self scope不遮蔽任何真实引用时输出原生class声明。外层仍可写，
  创建/heritage/字段/方法求值及TDZ时机保持；const目的、同名可写module/
  capture引用、heritage冲突、未知body和不匹配的名称仍采用原表达式。
  default export不变成hoisted声明，不删除父类品牌、receiver或异常证明。
- 类body/derived header共用ClassCreation/ClassPart及原action stack，保留
  原ClassExpr/DeclarationStmt、initializer ExprId和descendant printer位置。
  未新增opcode序列、PC/register、同块/邻接、固定producer/count/图形或样本
  准入。复制/拆块后仍需保存原创建时点的temp/alias继续由原共同值流输出；
  尚未融合的alias是表示缺口，不是语言不允许原生class。名称比较是必要
  语法/可观察name边界；语义义务/成本与规范来源已更新对应设计文档。
- 新class_declarations_source_test实际通过三profile的54份base/derived
  ABC解码模块及468组独立ESM对照。覆盖0/1/3 COPY与纯拆块、namespace/
  entry TDZ、重复evaluate、外层类binding写入和旧class身份；constructor
  同名外层引用反例保留匿名class，不能被immutable self替换。派生模块
  使用显式语言构造合同，验证普通/bound/Proxy/null/非法父类、prototype
  getter原throw、原constructor throw/对象return、替代new.target及创建后
  更换父类为Proxy；super仍取当前父类且丢弃原未转发实参。
- 6个先解析的有效JS mutation检出错误self scope、hoisting/调用边界、
  名称/绑定值、冻结super父类及错误转发额外实参；const目的和initializer/
  statement真实范围另验。旧规则下实际RED表示断言失败，red-test-1.log
  与red-source-2.mjs保留。派生oracle初把extends-null constructor原型误当
  null，按ECMAScript 2024的Function.prototype规则修正后通过，未改变生产
  语义；原ClassNames的heritage/capture/unknown-body与200/201/400/800规模
  检查也继续通过。CTest当前570项。
- 单一-j4构建两个engines及12个相关target退出0，build handle均关闭后
  测试。首批6/6通过；较广final-tests-1为28/31，final-tests-2为34/36，
  失败均是要求旧匿名/let-class表示的断言。更新原高层class表示期待后，
  EntryAbility生命周期、ViewModel、CardArray/Breakpoint及Backup等独立
  行为通过。更新两个旧测试时曾误写EntryAbility名称，分别改为实际
  EntryBackupAbility/PushMessageAbility；最终两项final-tests-4为2/2，
  2.46秒通过。36项定向回归现均有当前生产代码的成功证据，非单次36/36。
  日志/tmp/abcd-class-declaration-final-build-1.log、final-tests-2.log及
  final-tests-4.log；源码语法与git diff --check检查通过。
- 固定五ABC/六模式30次ABC-only正式导出退出0，实际SHA-256/size与既有
  paired index一致、ValueTree且非legacy，每模式106份源码语法通过；对比
  规则141 boolean-domain-pass-3，54/68/68/62/78/78份实际源码改为更自然
  class表示，无complete回退。完整/partial仍80/26、95/11、96/10、88/18、
  105/1、106/0，每模式另46个no-module。当前产物
  build/stage-one/class-declarations-<mode>-pass-1；日志/summary为
  /tmp/abcd-class-declaration-batch-1.log及*-summary-1.json。
- 各模式212份既有位置JSON文件逐字不变；这些文件仍声明entry_only或
  partial_methods/complete:false，若干完整record的位置列表为空，不能把
  文件计数当作完整来源映射证明。原节点在printer的位置另由定向树检查
  验证；DFX与完整mapping工作继续按原约定暂缓。新增决策复用一次名称
  计划，每声明至多常数次O(log C)查询及实际字符串比较，没有按变量重扫
  CFG或class body，没有新增依赖或全局数据表，不称RSS/benchmark已实测。
- 本次实际改善类/模块源码表示；没有关闭instance fields/globals/完整
  signatures/body结构gate或其余record独立验收，不清除structure_only。
  阶段一Goal继续active。全套、expanded corpus、RSS/benchmark、agents、
  scheduler/DFX和周期审计保持暂停。

### 2026-10-06：三个模型的完整构造、字段及保留回调验收

- 基线baca6e0b，生产规则仍141。扩展既有paired_model_record_source_test，
  以ABC-only默认 --value-source生成Immersive record16752 IconInfoModel、
  record17544 SectionModel及record17175 RecommendModel。按既有paired
  corpus index核对实际ABC SHA-256/size、manifest身份、V3合同、ValueTree、
  complete和非legacy；全部生成结束后才读取三个独立paired TS。完整原生
  class、constructor、initData、getter及实际map arrow均参与执行；没有
  参考body替换、生产样本分派或输入样本修改。
- 2,481组完整行为对照覆盖0/1/2/200/201输入、稀疏map、module live换绑、
  参数求值中import变化、列表替换、重复init与新activation。宿主保留真实
  callback后注册失败也重放，使用foreign this、当次live import及当前列表；
  每callback异常独立记录并继续后续调度。原错误按实际事件及第二/最后一次
  callback注入，比较callee receiver、原throw、部分列表/字段状态和身份。
- 在参数getter中通过test-only hook换绑实际item lexical binding，覆盖原构造callee在参数
  getter之前已取得、后续callback才读取新绑定；没有替换任何原函数body。
  另验12组model/item assignment/setter/new.target、112组item的0/1/200/201
  实参及各字段setter原异常/部分写入、39组Number/BigInt/Symbol/object等
  原参数身份/字段descriptor。每次setter测试恢复prototype，避免早期测试
  污染后续基线；Node未初始化TS字段的DefineField不作为Ark属性赋值oracle。
- 原生constructor/map/arrow/push/new、参数列表、getter及runtime name/arity/
  descriptor、namespace TDZ和void完成另验；9个先解析的有效JS mutation
  检出错误字段/参数、DefineField、callback动态this、map结果泄漏及把构造
  callee读取推迟到参数副作用之后。三个指定record的受控whole-record语义
  与高层表示验收accepted；允许必要receiver alias与mutable class binding，
  不宣称恢复type/private/const或完成HarmonyOS平台执行。
- 首次14项最终回归13/14通过，class_tree_context_test中止。调试器定位
  到旧共享instruction position oracle仍要求已省略的普通this读取有非零
  范围；生产printer/emission早已保留原ExprId/StmtId的共同零宽边界。测试
  现精确验证discarded invocation read及implicit tail completion的原节点、
  owner/body边界和相互一致范围，其余事件仍要求正常非零范围；单项复验
  通过。未放宽生产来源证明或新增同块/邻接/shape准入，临时诊断已移除。
- 最终单一-j4构建两个engines及11个相关target退出0，随后关闭诊断的
  class target重新构建退出0；全部build handle关闭后27/27定向CTest通过
  72.09秒，含原位置、partial、module/family、default/参数、async/generator/
  rest、phased literal/lexical、Home/Model/ViewModel实际生成行为。
  日志/tmp/abcd-model-structure-final-build-2.log、context-build-3.log及
  final-tests-2.log，LastTest.log保留2,481/12/112/39/9结果；Node语法与
  git diff --check通过。CTest仍569项，无C++生产语义变化。
- 本轮未重复固定五ABC/六模式30次导出；上一规则141 boolean-domain-pass-3
  的80/26、95/11、96/10、88/18、105/1、106/0完整/partial数量仍是既有
  证据，不称本轮fresh批量。其余record独立验收、instance fields/globals/
  完整signatures/body结构gate及完整平台模块图仍开放，不能用这三个record
  清除structure_only标记。阶段一Goal继续active；全套、expanded corpus、
  RSS/benchmark、agents、scheduler/DFX及周期审计保持暂停。

### 2026-10-06：Boolean 贯通共同值流与选择来源归属

- 基线3125584f，生产规则141。ValueKinds新增精确Boolean refinement，
  true/false与typed布尔运算正常结果沿既有sparse COPY/phi/SCC传播；
  null保留原Boolean/null转换域，mixed join与内部值继续弱化。结果事实
  仍依赖真实输入，不提前认证未证明的环。BooleanEvaluation查询共同事实，
  不新增copy链遍历、producer名单、同块/邻接或样本名称准入；原effect、
  catch、execution count、receiver、scope与默认/显式数值合同保持。
- 首轮固定输入回归发现pasteboard record8383由complete退到tree_stage；
  定位到共同conditional透明Boolean转换的两处来源表示限制。SelectionOrigin
  原仅logical允许CombinedOrigin，现conditional共用原来源correspondence。
  origin owner明确区分实际scalar predicate执行表达式和整个selection的
  contextual branch标注；context只能属于新鲜TreeFlow证明的那个selection，
  不能覆盖原predicate，后者仍须单独完整覆盖。原CombinedOrigin成员、
  unique owner、backward DAG及真实原始site/PC验证保留。未按失败record、
  函数名或诊断文本分派；所有临时诊断已移除。pass-1/pass-2回退不作成功证据。
- 新增boolean_phi_source_test：三profile真实ABC解码的60份完整函数，经
  原value flow/control/evaluation/AST/来源/位置/printer生成后做11,136组
  独立JS对照，覆盖Boolean/null phi、0/200/201/402 COPY拆块、直接/转换
  predicate、复合null/undefined条件、两次getter分别变化与第一次/第二次
  原throw。5个先解析的有效JS mutation检出重复getter、null结果泄漏和
  错误合并两次getter。C++另验错误CombinedOrigin、保留context但丢失真实
  scalar来源、交换arm仍拒绝，及格join交换/结合、SCC和200/201/402/804
  复制拆块。首次扩展fixture的oracle误把ISTRUE当VM false/zero分类；按
  已有typed scalar adapter的ToBoolean更正后重跑，没有修改生产转换语义。
- 旧规则下Boolean COPY断言真实失败，red-test-1.log保留；首批8项、后续
  normal/phased import/value-flow与新增fixture均通过。最终单一-j4构建
  两个engines及17个相关target退出0，关闭所有build handle后29/29定向
  CTest通过171.61秒，含原phased literal、predicate prefix、Boolean choice/
  precedence、部分输出、模块/family、paired truth-selection及两个主Ability
  完整生命周期。日志/tmp/abcd-boolean-domain-final-build-1.log与
  final-tests-1.log；新增Node语法与git diff --check通过，CTest当前569项。
- 最终固定五ABC／六模式30次ABC-only正式导出均退出0；输入SHA-256/size
  与既有paired index一致，每模式106份完整/明确partial源码语法通过，
  212份entry/method位置映射及源码与规则140 default-callables-pass-2逐字
  相同，无complete回退或legacy。完整/partial仍80/26、95/11、96/10、
  88/18、105/1、106/0，每模式另46个no-module。当前产物
  build/stage-one/boolean-domain-<mode>-pass-3，日志/summary为
  /tmp/abcd-boolean-domain-*-3.*；本轮没有样本完整数量或实际源码字节改进。
- Boolean query/join O(1)，ValueKinds不增宽；原sparse格高度增加一个常数
  refinement。来源owner表增加typed context，按原Array实际width/capacity
  计入内存，仍一次扫描及DAG消费，不按选择重走CFG。成本/来源见algorithms.md，
  不称完整进程RSS/benchmark已验证。本次关闭通用Boolean COPY/phi与
  conditional透明转换的组合缺口；private class alias、其余record独立
  whole-record验收及instance fields/globals/signatures/body结构gate仍开放。
  阶段一Goal继续active；全套、expanded corpus、RSS/benchmark、agents、
  scheduler/DFX及周期审计继续暂停。

### 2026-10-06：两个主 EntryAbility 的完整生命周期验收

- 基线131b102c，生产规则仍140。新增entry_ability_records_source_test先以
  ABC-only --value-source --language-numeric --language-constructors生成
  pasteboard record4767/entry4917与MultiCommunity record40957/entry41080，
  核对既有paired index的SHA-256/size、ValueTree route、complete及非legacy。
  两份生成均结束后才读取独立paired TS；实际完整class、constructor、七个
  prototype method与所有load/main-window/size callback参与执行，没有参考
  body替换、片段提取或新增生产样本条件。此前单回调／构造片段证据保留。
- 新增2,710组完整受控生命周期对照，覆盖宽度／density阈值与NaN/-0/
  BigInt/Symbol/coercion、load/main-window错误真假值、JSON正常／null／
  undefined／原throw、phone/2in1/缺失device，以及两个实例的正反回调队列。
  每个正常完整调度的全部可观察事件逐一注入原错误；live imported logger/
  display/window/device/constants也逐事件换绑。所有真实retained callback
  在原方法返回或注册失败后执行，listener用外部this重放两次，检查捕获
  receiver、每activation字段状态、原callee receiver、effects和AppStorage。
- 专项故障明确进入真实updateBreakpoint catch，确认读取原error.code/
  message再注入第二个异常；不把提前终止的等价trace计为catch覆盖。全部
  void宿主调用返回带poison then getter的对象，实际方法／callback必须返回
  undefined，禁止泄漏或assimilate宿主返回值。namespace TDZ/不可写/重复
  evaluate、runtime class/method name/arity、method descriptor和构造边界
  另验；换绑父类import不能改变已求值heritage。窗口回调由host显式逐项
  释放，不依赖microtask次数，也不宣称HarmonyOS平台执行通过。
- 16组独立pinned实例赋值检查覆盖0/1/200/201实参的原对象身份和curBp
  inherited setter／原throw；setter处理后不伪造own field。正常paired构造
  验字段值与descriptor；有inherited setter时Native TS DefineField不作为
  STOBJBYNAME oracle。13个先解析的有效JS mutation检出错误事件／页面／
  width／storage key、宿主return泄漏、普通callback的动态this和DefineField。
  两个指定record的受控whole-record语义／高层表示由pending转accepted；
  允许mutable class binding、等价显式constructor、conditional与必要快照，
  不宣称恢复源码type/const或消除全部局部暂存。
- 初轮1,454/4/9检查通过；扩大完整事件注入和参数边界后，另一次测试把
  arrow内原生this误当错误变体，且一次预定fault尚未进入目标catch。已把
  mutation改为实际动态this的普通函数，并把故障放到实际display调用／
  density getter；这些是测试校正，未修改生产恢复逻辑或配对样本。最终
  execution-4及final-tests-1均记录2,710/16/13真实通过结果。
- 单一-j4 abc_semantic构建及CMake重新配置退出0，所有build handle关闭后
  16/16定向CTest通过24.49秒：含原pasteboard partial/cell/breakpoint、
  language constructor/numeric unit及JS、forward constructor、备份、Push、
  VoIP、record/module family/function tree/partial。日志
  /tmp/abcd-entry-abilities-final-build-1.log、final-tests-1.log；两个新增
  Node语法及git diff --check通过。最后新增不触发getter的每实例最终curBp
  状态对照，防止异常后的部分状态仅凭trace判定；new test单项再次通过
  2.51秒，final-tests-2.log保留2,710/16/13结果。CTest当前568个，无C++
  生产语义变化。
- 本次测试／CMake／验收文档有实际进展，未重复固定五ABC／六模式的30次
  批量；完整/partial计数仍是上一轮default-callables-pass-2证据，不称本轮
  fresh批量结果。其余record、实际完整base/platform模块图及原instance
  fields/globals/signatures/body结构gate仍需对应验收。阶段一Goal继续
  active；全套、expanded corpus、RSS/benchmark、agents、scheduler/DFX及
  周期审计保持暂停。

### 2026-10-06：直接函数／类默认导出的共同命名上下文

- 基线e3d46ccd，生产规则140。移除ModuleDefaultExport对直接FunctionExpr／
  ClassExpr的整体表示拒绝；沿用原sole default slot、真实首次声明、source
  scope及整family/class生命周期检查。函数必须有实际owned child body和
  creation-proven runtime name；缺失名称证据不能当空名称。缺失／foreign
  body、partial descendant、默认绑定的读取／写入／捕获继续拒绝投影。
- 共同printer新增typed DefaultInferredName，复用原NativeName／ClassNames
  和表达式打印。实际函数名等于default时使用原生匿名表达；不相等或确为
  空名时保留原命名envelope，严格V1的额外分配拒绝保持。function表达式
  由原printer括号分组，class默认导出也用expression grammar；不提前
  hoist函数、不新增module class-name binding、不移动原创建或改变AST。
  来源位置仍由同一action stack输出，未增加专用body writer或样本条件。
- 旧生产代码下先构建真实编码fixture，输出仍为let默认哨兵及export alias，
  函数另有computed命名包装；证据/tmp/abcd-default-callables-red-function.mjs
  和red-class.mjs。没有把该输出观察称为已运行失败的RED行为测试。新独立
  ESM对照共210场景／4个有效JS mutation，包含三profile实际ABC解码的
  函数／类，以及6种callable、4种原名称的共同树打印。检查namespace TDZ、
  重复evaluate身份、name/arity、构造边界、async/generator行为、live import、
  snapshot和具名class的外层scope。错误function声明hoisting／class外层
  binding变体均被检出；初轮重复函数括号使mutation未改变文本，已去掉
  冗余分组后重跑。owned/missing-name/partial/capture反例及200/201/400
  family增长验证补齐；原split/register/loop模块回归继续通过。
- 最终单一-j4构建两个actual engines及11个相关target退出0；关闭build
  handle后25/25定向CTest通过40.44秒。日志
  /tmp/abcd-default-callables-final-build-2.log、final-tests-2.log，新增测试
  default_callable_exports_source_test，CTest共567个。测试只消费真实生成
  源码；独立reference不进入engine，未改样本输入。
- 最终固定五ABC／六模式30次ABC-only正式导出均退出0；输入SHA-256／size
  与既有index一致，各模式106份完整／明确partial源码语法通过、212份
  entry/method位置JSON与module-dominance-pass-1逐字相同，无完整回退或
  legacy。完整／partial依次仍80/26、95/11、96/10、88/18、105/1、106/0，
  每种另46个no-module。当前产物build/stage-one/default-callables-<mode>-pass-2，
  日志及summary为/tmp/abcd-default-callables-*-2.*；pass-1是加强缺失
  函数名称证据检查之前的中间输出，不作为最终代码的证据。
- 每record最多一个默认候选；新增owned函数查询一次O(F)扫描，无逐变量
  CFG/tree walk。原distinct tree/class inventory、work/scratch及preflight
  保持，命名上下文是固定大小variant；算法来源、成本见algorithms.md。
  未新增依赖／默认resource准入，未宣称完整进程RSS／benchmark达标。
- 本次关闭通用直接callable默认导出表示缺口，固定样本完整数未增加。
  private class alias、const／完整写入证明、其余record独立表示／语义及
  原字段／global／signature／body结构gate仍开放。阶段一Goal继续active；
  全套、expanded corpus、RSS／benchmark、agents、scheduler／DFX及周期
  审计继续暂停。默认与各显式数值／构造模式的语义合同没有改变。

### 2026-10-06：VoIP 整体主流程、共享静态状态与延迟回调验收

- 基线c0ffb437，生产规则仍139。voip_service_source_test继续从实际
  record11989/entry12208以ABC-only默认ValueTree生成完整模块；新增按既有
  paired index核对输入SHA-256/size及record/route。生成结束后才读取独立
  paired TS。实际三个static方法、原listener/image-info callback及callId
  字段都参与执行，没有参考方法替换、fake body或新的生产恢复条件。
- 新增366组受控完整主流程调度对照；120组检查双请求、正反release、
  raw/pixel/incoming/change原拒绝、catch中code/message getter异常和暂停
  期间hilog live export换绑；240组检查null/voice/video scene、五种UI事件、
  registration/off异常、无pixel、主流程结束后的image-info回调及原width
  getter异常；另6组重放on已保留listener但随后throw、catch getter致主流程
  拒绝后的真实callback，并检查close的原启动promise和off异常。
- Promise由测试host逐项释放；native await前只启动首个resource请求，
  恢复后incoming descriptor保留原每请求callId/pixel身份，listener和close
  则读取当次共享静态状态。实际caller/receiver、own key/descriptor、类调用
  边界、namespace TDZ/重复evaluate及intrinsic async promise身份均核对。
  close丢弃返回promise时，fixture包装原event method以捕获其实际return，
  再释放/等待该操作；没有用固定microtask次数冒充操作完成。
- 4个新增有效JS mutation检出错误userProfile、listener事件参数、image-info
  height与off事件名。原322组直接行为、768组event受控调度与3 mutation
  全部保留；新证据补齐跨完整main/callback/close的并发与失败后重放层，
  不把source_complete或这些场景称为全部源码表示/平台验收完成。
- 最终9/9相关CTest通过19.43秒，包含原PushAbility实际服务连接、备份
  record、class root captures/static class batch、record/module family及
  function tree/partial；日志/tmp/abcd-voip-main-final-tests-1.log记录全部
  新旧场景结果。首轮360场景执行日志为execution-1.log，最终追加6场景
  的真实结果以final-tests-1为准。两个JS语法及git diff --check通过。
- 本次只改两个独立测试及验收文档，没有C++/CMake、没有构建，CTest仍
  566个；实际服务源码由当前engine重新生成。未重复固定五ABC/六模式
  批量，module-dominance-pass-1计数仍为前轮证据。源码的比较链、private
  class alias及较多snapshot临时变量仍需共同表示能力和独立风格验收，
  不通过替换参考body或移除身份/效果检查改善外观。阶段一Goal继续active；
  其余record、完整结构gate、平台及暂停的全套/expanded/RSS/benchmark/
  agents/scheduler/DFX/周期审计均未由此次验证自动完成或恢复。

### 2026-10-06：三个备份 Ability 的完整 record 独立验收

- 基线b5baaf19，生产规则仍139。新增backup_records_source_test以当前
  abc_semantic的ABC-only --value-source --language-constructors依次生成
  Immersive record9308、pasteboard5246、push6659；三个输入SHA-256/size
  与既有paired index核对。全部生成完成后才读取独立paired TS，完整原生
  class/constructor/两个async method/import/export和DOMAIN初始化实际执行，
  无legacy/INCOMPLETE/UNRECOVERED，不提取或替换方法体。
- 1,224组paired构造场景覆盖参数身份、0/1/200/201项、动态direct/bound/
  Proxy/非构造父类、父类返回对象/primitive/任意原throw、foreign new.target、
  两realm的非对象prototype fallback及prototype getter异常。import换绑
  不反向改变已求值class heritage；类调用边界、method name/arity/descriptor、
  async非构造性、namespace TDZ/不可写/重复evaluate另验。
- 4,068组paired完整异步方法场景覆盖原调用receiver、JSON stringify/
  toJSON/循环对象/BigInt异常、日志callee及JSON/Promise getter中换绑、
  调用期间live import换绑、borrowed null/拒绝访问的receiver、两个并发
  请求、intrinsic promise身份及microtask checkpoint顺序。resolve原值/
  fulfilled/rejected/non-callable then/nested thenable、首次resolve后reject/
  throw和每个getter/call任意原throw均对照；日志返回then不得被额外await。
- 13个有效JS mutation全部检出：错误DOMAIN/日志参数、序列化错误实参、
  添加receiver读取、遗漏super实参、遗漏await及错误返回await值。三个
  record的受控whole-record语义与高层表示验收由pending转accepted；允许
  保守let模块绑定、显式等价constructor和局部await暂存，不宣称const/
  类型信息恢复。paired构造对照使用普通iterator；污染iterator时explicit
  native spread与paired implicit constructor的不同契约仍由原独立测试负责。
- 首轮oracle把非对象prototype fallback归错realm；补齐第二realm检查后
  按实际new.target realm校正，未改生产C++。保留原default partial方法测试
  及其1,200场景，新complete测试单独注册并按三个ABC/paired文件存在准入，
  避免增加原push-only测试的文件依赖。CTest当前566个。
- 单一-j4 abc_semantic构建（含CMake重新配置）退出0，build handle关闭后
  最终10/10定向CTest通过12.05秒，含原partial/backup片段、forward构造、
  language构造unit/paired、PushAbility完整测试及function tree。
  日志/tmp/abcd-backup-records-final-build-1.log、final-tests-2.log，完整场景
  输出另存execution-final.log；两个新增JS语法和git diff --check通过。
- 本次只有测试/CMake及验收文档变化，生产规则/六模式完整数沿用已验证
  module-dominance-pass-1，未重复30次批量导出；该计数不是本轮fresh结果。
  三个指定record的控制依赖验收不能关闭HarmonyOS平台、其余record或原
  global/field/signature/body结构gate。阶段一Goal保持active；全套、expanded
  corpus、RSS/benchmark、agents、scheduler/DFX和周期审计继续暂停。

### 2026-10-06：模块初始化以共同 dominance 取代入口块限制

- 基线eb612ad3，生产规则139。RecordTree的旧entry_initialized只在物理
  block 0首次store时置位，因而把汇合后的native声明及纯split后的同等
  声明拒绝在后续arm/loop使用。本次用每slot实际首次声明OpId取代该bitmap；
  需要处理额外write时，只运行一次当前ValueProgram的共同VerifyValueFlow。
  同块真实operation order／跨块dominator证明声明已在所有正常路径完成，
  后续write继续使用同一module owner/slot和native source binding。
- 没有移动初次store、RHS或后续assignment，没有新opcode/name/sample/
  PC/register/图形匹配。原ControlPlan根source序列仍决定声明的语法位置，
  common tree-flow/scope/module/capture/TDZ及origin检查保持。条件首次
  初始化、声明前write和catch均未凭normal dominance放宽；root-only
  declaration placement是尚未贯通的表示边界，不称语言不变量。
- 可用性索引只构建一次，每额外write O(1)查询，不按slot/write重走CFG；
  typed absence/OpId向量的实际capacity、共同availability scratch和dominator
  peak计入原budget，分配前检查乘法/容量边界。算法来源与复杂度见
  algorithms.md；没有新依赖/公开API、默认work准入或RSS/benchmark声明。
- record_tree_test新增真实编码join、loop、register重分配及1/200/201/
  400/800 pure split；CodeItem header改为真实ULEB字段，避免较长body截断。
  先在旧生产代码下构建fixture，四类red case均为kInitializer，记录于
  /tmp/abcd-module-dominance-red-cases-1.log。当前三profile独立实际ESM
  共54场景/57 valid-syntax mutation通过，6个提前write/conditional-first
  refusal保留；module namespace TDZ、完整exports和实际结果均核对。
- 单一-j4最终构建actual engine及相关10个unit/input targets退出0，所有
  build handle关闭后最终21/21 CTest通过21.29秒；包括module initialized
  read/aliases/enclosure、default/anonymous/family、function tree/partial和
  实际ViewModel、CardArray/真实CardItem/CommentItem、barrel/constants。
  日志/tmp/abcd-module-dominance-final-build-1.log、final-tests-1.log。
- 当前固定五ABC/六模式30次正式ABC-only导出退出0；输入hash/size与index
  一致，各模式106份完整/明确partial源码语法通过，无完整回退、新legacy
  或source bytes变化。每模式212份entry/method位置JSON也逐字不变。
  完整/partial仍80/26、95/11、96/10、88/18、105/1、106/0，各另46个
  no-module。产物build/stage-one/module-dominance-<mode>-pass-1，summary/
  export/batch日志在/tmp/abcd-module-dominance-*；CTest仍565。
- 本次交付通用module lifetime能力，实际固定样本完整数量未增加；不以
  fixture或106/0替代整record独立语义/表示和原结构gate。阶段一Goal继续
  active，const/完整绑定写入、其余record及结构集成仍开放；全套、expanded
  corpus、RSS/benchmark、agents、scheduler/DFX与周期审计继续暂停。

### 2026-10-06：TabBar 全局字面量与实例字段的真实模块链验收

- 基线0729996f，生产规则仍138。viewmodel_batch_source_test先以当前
  abc_structure对固定Immersive/Multi两个ABC完成ABC-only正式导出，再读取
  任何paired TS；新增核对输入及manifest SHA-256与ValueTree route。
  从同一实际manifest取得record41409 TabBarModel与44106 TabBarViewModel，
  两份完整源码均无legacy，并在同一VM module graph中实际连接和执行。
- 两个record的受控独立whole-record表示/行为验收从pending转accepted：
  原生导出数组、嵌套Resource对象、imports/export、class、constructor、
  原tabList赋值/getTabList语法保留。允许原有mutable class/global绑定和
  显式等价空constructor，不宣称恢复了paired TS的const/private/type事实。
  模块槽读写权限不是源级const证明；跨family/class的完整绑定写入分析
  仍需自身实现，不能按样本或仅初始化次数更改DeclarationKind。
- 两次实际完整模块连接分别检查29个对象/数组节点的值、own key顺序、
  descriptor、extensibility、realm prototype和完整identity graph；初始化
  同时面对Object/Array inherited setter及被替换的全局helpers。所有Resource
  params数组保持独立，两个真实实例共享同一导出数组；实例字段替换不改变
  模块绑定/其他实例。另验module TDZ、namespace不可写、重复evaluate、
  原constructor/method的name/arity/prototype、不可普通调用/构造的对应边界、
  borrowed getter receiver及null/primitive receiver行为。
- 新增196组受控live import原值/新值配对，覆盖undefined/null/NaN/-0/
  BigInt/Symbol/object及禁止coercion值；旧实例保留初始化值，新实例读取live
  binding，getter原异常身份和getter内改绑保留。52组独立pinned赋值层验
  inherited setter、foreign new.target、两realm非对象prototype fallback、
  部分存储/原throw身份及无多余own field；此层不以Node initialized TS
  DefineField作oracle。6个有效JS mutation检出错误字段、getter改读import、
  constructor错误返回、DefineField、资源id和共享params身份。
- 新oracle首轮在屏蔽全局Object后又通过它取得prototype，随后改用屏蔽前
  保存的intrinsic；未据测试host错误改变生产恢复。两个JS语法及
  git diff --check通过，最终相关10/10 CTest通过26.14秒，含原六个viewmodel、
  static class batch、两个真实购物model/viewmodel、HomeModel、barrel、
  constants、CardArray/实际CardItem/CommentItem与partial；CTest仍565。
  日志/tmp/abcd-tabbar-records-final-tests-1.log。未改C++/CMake、未构建、
  未重复固定五ABC/六模式批量，上一轮完整计数仍为既有证据。
- 这关闭所测两个真实record与其直接模块链的独立验收，不扩为整个应用/
  UI/platform或所有global/field/signature的结构gate。阶段一Goal继续active；
  全套、expanded corpus、RSS/benchmark、agents、scheduler/DFX与周期审计
  保持暂停，继续其余实际record及结构集成缺口。

### 2026-10-06：CardItem 整 record 与实际 CommentItem 连接验收

- 基线c49c4503，生产规则仍138；此次补齐独立验收，未改生产恢复条件、
  ISA/profile、source contracts或输入ABC。value_card_array_record_source_test
  在读取任何paired TS前，由当前abc_semantic ABC-only生成CardArray、
  CardItem、CommentItem三个实际完整ValueTree record，全部无legacy。
- CardItem独立whole-record表示从pending转accepted：检查原生class binding、
  constructor(card,index)、imports/export、字段赋值、remainder/indexing、
  forEach/native arrow与new CommentItem。允许既有不可变activation receiver
  alias和pinned assignment，不要求逐字class声明或把赋值改为DefineField。
  146组普通/Number/BigInt/Symbol/coercion及11种live import/callee场景，
  544组undefined/0/Symbol/object原异常身份注入通过；覆盖取得push后替换、
  retained arrow、非callable/constructible、失败构造后的callback重放、
  own field顺序/descriptor、共享图片与comment数组身份/部分内容。
- 另356组固定赋值层检查6个inherited setter、foreign new.target、两realm
  非对象prototype fallback、index/length coercion顺序、原异常与此前完成的
  写入、原receiver/return身份及无多余own field。此层使用独立pinned效果
  序列，不能用Node TS initialized DefineField作setter oracle。6个有效JS
  mutation拒绝错误字段/索引/参数、普通函数callback、错误constructor返回
  与DefineField替代assignment。原CardArray及其真实CardItem连接证据保留。
- 新连接实际CardItem和实际CommentItem两个完整ESM，受控barrel仅reexport
  实际CommentItem并提供两个输入列表；没有替换任何生成constructor/body。
  152组连接场景、276组原异常身份/部分数组状态及3 mutation通过，含条件
  falsy/truthy/BigInt/Symbol/object、重复getter、reverse/deferred callback、
  200/201项与失败后重放。reference只擦除CardItem四个无initializer的class
  字段、CommentItem七个class字段及七个interface类型声明；两个完整原始
  constructor/closure与initialized字段保留，reference不进入engine。
- 新oracle首轮对中途throw后重复getter计数作了过强假设，随后按实际独立
  reference保留的input/field值身份比较修正；没有据此改变生产恢复条件。
  两项JS语法/git diff --check通过，相关最终10/10 CTest通过14.36秒，
  日志/tmp/abcd-card-item-state-final-tests-1.log；CTest仍565，未改C++/CMake、
  未启动构建。三个record在本轮真实重新生成和执行，未重复五ABC/六模式
  批量；上一轮80/26、95/11、96/10、88/18、105/1、106/0仍为既有证据。
- 这关闭CardItem的所测独立受控表示/行为验收，不扩为完整base barrel或
  应用/platform、所有字段/types/globals/signatures的结构gate。阶段一Goal
  保持active；全套、expanded corpus、RSS/benchmark、agents、scheduler/DFX
  与周期审计继续暂停，下一步推进其余实际record的结构/独立验收缺口。

### 2026-10-06：共同命名器逐参数保留 debug 事实

- 基线d1b9c5a4，方法规则138。实际输出中的默认参数名缺失源自旧 naming
  adapter 的整组准入：某个 formal 无 debug 证明，就丢弃所有 debug 名字。
  ParameterOffsets / RecoveredParameters 现逐 ABI ordinal 使用 optional；
  SourceTree保留每项unique合法提示，范围冲突不会后续复活，非法/重复名字
  只撤销对应候选。legacy 完整集合/私有namespace约定保持隔离；旧文本
  调用者只消费其完整结果。没有 sample/opcode/layout/同块或数量匹配。
- 原始ParameterOrigins的Before/Commit/Catch、SSA copy/phi/SCC、上游
  named-storage过滤保持；名字不授予存储身份、类型、const或default证明。
  名字进入共同syntax后继续经过NameReservations、module/global/family
  enclosure及实际formal/capture/绑定校验。真实读取/file-change/resource
  失败保持，typed absence不伪装为输入undefined。没有observations时不重建
  无用途SSA；可选名字及string容量/终止字节计预算，排序去重及逐ordinal
  合并不按参数反复扫描CFG。复杂度/边界见function-tree-output.md。
- parameter_bindings_test扩展稀疏/冲突后重复、非法range、empty范围、
  copy/拆块、mixed phi及200/201/402/804缺失ordinal；原legacy拒绝仍检查。
  family_names_test新增24组真实编码三formal族：稀疏/非法/重复 sibling、
  named/plain、直线/拆块、global/module callee。JS独立执行共152个decoded
  families（80原formal/24新partial），3268组参数/closure/effect/原异常/
  live callee行为；88个有效JS mutation拒绝，其中40项新增partial formal
  或其unused sibling遮住真实callee。C++还检查NameEvidence、arity及
  不同enclosure下draft拒绝，没有测试专用production API。
- parameter_origins_source_test在ABC-only完整导出之后才读paired reference，
  保留Content.getBirdImages的176组identity/coercion/throw及count mutation；
  新增实际Index record NavPageMap(name, parent = null)。完整ESM实际执行
  原registerNamedRoute，受控host取得真实factory并创建实际class，再调用
  原prototype method；没有抽取/替换生成函数正文。756组配对route/默认
  arity、同步/延迟callback、receiver、constructor/update、效果及throw对照
  通过。原legacy navmap_source_test复用同一独立oracle，另756组保留。
  framework/import/state对象是控制依赖，此证据不关闭整UI/platform gate。
- 首轮build-1/六项回归通过；扩大构建时final-build-1把CTest名称误当target
  而停止，后续按实际CMake target修正。最终单一-j4 final-build-2退出0，
  handle关闭后final-tests-1 18/18通过23.98秒，含default/captured/string/
  implicit相关、class names、captures/partial、normal/phased命名及真实方法。
  新record oracle先补齐缺失的host registerNamedRoute和实际factory控制依赖，
  没有因这个测试初始化失败改生产语义。日志/tmp/abcd-partial-formals-*。
  四项JS脚本语法/git diff --check通过，CTest注册仍565项。
- 最终固定五ABC/六模式30次导出退出0，输入hash/size与index一致，每模式
  106份完整/明确partial源码Node语法通过，无完整回退/新增legacy；default和
  constructor各7份、四种语言数值模式各14份源码表示改变，实际包括
  NavPageMap、CustomButton、PagesMap、TabBarBuilder等parent/param参数，
  及部分callback的真实debug formal/独立cell名。按同模式两pass核对，所有
  manifest及源码bytes逐一相同。完整/partial仍80/26、95/11、96/10、88/18、
  105/1、106/0，另46个no-module；源码路径保持上一提交结果。产物
  build/stage-one/partial-formals-<mode>-pass-2，summary-2/batch-2日志在/tmp。
- 这关闭所测逐formal命名缺口，不把debug编译绑定当作逐字paired源码名字。
  Goal保持active：instance fields/globals/完整签名/body结构gate、其余record
  独立行为/表示与全模块图/platform仍开放。全套、expanded corpus、RSS/
  benchmark、agents、scheduler/DFX与周期审计继续暂停。

### 2026-10-06：共同包身份规划恢复源码路径

- 基线60c2a64c，方法恢复规则仍137。源码路径改由既有ABC-only
  SourceIdentity/PackageSource adapter与全文件namespace规划生成；删除
  entry==package、entry不得含slash、ASCII-only的表示限制。入口、包名、
  版本是独立身份，排序后一次分组；多个入口/版本用目录区分，Unicode
  scalar经标准UTF-8及共同percent encoder可逆转义。没有sample/name/path
  语义分派、sidecar或source-map读取，也没有改函数恢复准入/默认语义。
- unique filename证据与真实member range/owner、读取/资源失败、输出
  长度及大小写碰撞检查保持。unsupported/conflicting/incomplete身份回退
  descriptor；同stem/跨namespace碰撞以typed PathError拒绝，不能静默选
  winner。archive/declaration/metadata实体路径保持；实际向量/字符串容量
  和固定转换scratch计入原16MiB planner预算。复杂度/来源/表示缺口见
  record-structure.md与shape-restrictions-audit.md，不声称完整进程RSS通过。
- record_path_test以真实file-backed Panda ULEB/MUTF-8字符串贯通现有
  reader/gatherer，新增不同entry/package、scoped package、多入口/版本、
  record反转、BMP/supplementary Unicode、literal percent、无效/缺失身份、
  owner失败、同路径/大小写/extension/namespace碰撞、组件/总路径/内存
  边界及200/201/400/800增长。不存在测试专用production接口/新增依赖。
- 单一-j4构建record_path_test/abc_structure/abc_semantic最终退出0；全部
  build handle关闭后执行。final-tests-1相关16/16通过13.41秒，覆盖
  partial、module/class/constants/barrel、实际Window/Logger/CardArray/
  Breakpoint/ViewModel及结构CLI。git diff --check通过，CTest仍565项。
  日志/tmp/abcd-package-paths-final-build-1.log、final-tests-1.log。
- 固定五ABC/六模式30次导出全部退出0，输入hash/size与index一致，每模式
  106份完整/明确partial源码Node语法通过，全部source_path_origin为包身份。
  每模式67份原descriptor fallback恢复package路径，另39份保持；按entity
  对照native-receiver pass-1，所有源码正文、entry/method位置映射JSON逐一
  相同，source status/complete/contract/route及archive artifact路径不变，
  无legacy。default80/26、arithmetic95/11、numeric96/10、constructors88/18、
  arithmetic+constructors105/1、numeric+constructors106/0，另各46个no-module。
  产物build/stage-one/package-paths-<mode>-pass-1；summary-1.json/batch-1.log
  与各导出日志在/tmp/abcd-package-paths-*。此次仅关闭所测文件名缺口。
- Goal保持active：instance fields/globals/完整签名/body结构gate、其余record
  独立语义/表示验收与完整模块图/平台语义仍开放。显式语言数值/构造模式
  沿用户授权保留，106/0不等于阶段一完成。全套、expanded corpus、RSS/
  benchmark、agents、scheduler/DFX及周期审计继续暂停。

### 2026-10-06：CardArray / BreakpointType 独立整 record 验收

- 基线2a707dc0，生产规则仍137；此次增加实际源码的独立行为/表示证据，
  没有修改生产恢复条件、输入ABC、source contracts、调度器或报告平台。
  当前abc_semantic从同一固定MultiCommunityApplication ABC实际生成两个
  完整record及实际CardItem peer；全部生成结束后才读取paired TS。route均为
  ValueTree、source_complete且无legacy，engine参数只含ABC/实体/正常选项。
- CardArray wholeRecordStyle由pending转accepted：完整named imports、
  mutable class binding、constructor、字段初始化、真实forEach/native arrow/
  new CardItem/getInstance/default export保留。允许已有不可变receiver capture
  alias和pinned赋值初始化；不要求加入native DefineField或逐字重建class声明。
  24组原基础场景/1 mutation保留；新增11组live import/callee、push callee取得后
  替换、reverse/repeat、重入constructor、非callable/constructible及保留arrow，
  196组undefined/0/Symbol/object异常身份、模块初始化状态及部分数组状态。
  失败构造留下的closure在恢复host fault后仍实际调用，failed module保持failed。
- CardArray另84组pinned assignment/setter/new.target检查：class/foreign原型、
  两个realm的非对象prototype fallback、setter/getter/item处异常、顺序、fresh
  数组、receiver/返回身份及无多余own field。Node的initialized TS field采用
  DefineField，此层不拿它当STOBJBYNAME oracle。5个新增有效JS mutation拒绝
  错误参数、plain function代替arrow、unshift、DefineField和错误return。
- CardArray直接连接实际反编译CardItem，而非替换CardItem constructor；仅
  base/Index为控制依赖。7组actual-module连接/336组异常注入核对card getter、
  图片length/index coercion期间的live export替换、CommentItem换绑、零长度
  表、延迟comment callback、module TDZ、参数/对象/数组身份、own-field顺序/
  descriptor与部分状态；真实callback的name/arity/不可构造性也检查。3个peer
  mutation由独立trace/state对照检出。peer reference仅擦除4项uninitialized
  type-only field，初始化字段和constructor/closure保留；不扩大为CardItem
  全部new.target/setter及独立表示验收完成。
- BreakpointType wholeRecordStyle由pending转accepted：原360组lookup/
  getter/live import/3 mutation及原setter检查保留，新增24组构造/字段参数
  身份、240组prototype/setter异常。覆盖NaN/-0/Symbol、禁止coercion对象、
  foreign new.target、两个realm的fallback、部分写入和原异常身份；3个ctor
  mutation拒绝错误值、错误返回对象和DefineField替代赋值。允许class binding
  与等价最终if极性，不改变真实字段存储。reference仍只擦除原3项type-only
  声明，fields/method/constructor均实际执行。内部测试scenario使用Symbol enums。
- 实施中修正新oracle的GetPrototypeFromConstructor realm前提：fallback取自
  new.target的realm；没有据错误预期改生产输出。peer modulo mutation最初仅
  期待自身断言抛错，现以完整baseline trace/state对照检出；未放宽行为断言。
  final-tests-3相关10/10通过15.57秒；新增peer callback元数据检查后，修改的
  两项final-tests-4 2/2通过1.18秒。覆盖partial、Widget、Window、两个Logger、
  viewmodel batch/实际连接等。5份脚本Node语法、git diff --check通过。CTest仍565。
  本轮未改C++/CMake、未启动构建；实际运行当前已构建engine。日志前缀
  /tmp/abcd-record-state-*、/tmp/abcd-card-array-state-*、/tmp/abcd-connected-card-*。
- production源码与rules未变，未重复五ABC/六模式批量；上一提交的30次导出
  与80/26、95/11、96/10、88/18、105/1、106/0完整/partial计数仍为既有证据，
  不是本轮重跑。两个record及peer在本轮实际重新生成并执行。这里只关闭这两
  项受控独立表示/行为验收，不把partial数组/host边界或peer构造扩大为整阶段。
  Goal保持active：其余record、instance field/global/完整签名/body结构gate、
  完整模块图/平台语义仍开放。全套、expanded corpus、RSS/benchmark、agents、
  scheduler/DFX与周期审计继续暂停。

### 2026-10-06：共同 receiver 存储复用与实际构造状态验收

- 基线610f5da0，规则revision137。EvaluationShape 的 typed ReceiverValue
  标记当前 receiver/成功 super 的 initialized receiver；共同 BindingPlan、
  expression forest/copies、FunctionTree 与 TreeFlow/TreeOperands 复用实际
  native invocation storage。原初始化 event、实参、origin、效果及异常保留；
  后续读取使用 ThisExpr，derived VM entry undefined 仍独立。没有样本/类名/
  PC/register、局部opcode序列、同块或selection/producer数量准入。
- 移除 derived ABI 物理入口读数与原生 this 引用数的错误等式；精确值身份
  仍由独立树对应校验负责，错误 entry→this/consumer/owner 或删除初始化
  mutation继续拒绝。普通 ABI census保持。DiscardedInvocationRead 共同
  省略普通非arrow已初始化 this 的直接无效果语句，并保持零宽来源；derived
  this仍保留。静态字段printer复用该投影，删除 arena字段数==child数检查。
  新事实数组O(operation)预算内空间，检查沿原共同流程，不逐receiver走CFG。
- function_tree_test增加16组current/initialized×COPY×拆块×getter/call及4组
  分支组合，验证无需receiver临时符号并拒绝错误消费者/初始化。两份实际
  WidgetCard输出改为直接super/this字段；ABC-only导出全部结束后读取paired
  TS。执行真实constructor及setInitiallyProvidedValue，对照864组current
  direct/bound/Proxy parent、foreign对象return、替代new.target、参数/default
  与字段descriptor状态；60组任意throw identity注入、6个有效JS语义mutation
  拒绝；既有176组callback/effect继续通过。未覆盖完整平台/storage生命周期、
  inherited setter/DefineField普遍等价，不能扩大这些局部证据。
- 较广定向回归发现partial_file旧Boolean-neg夹具已被现有primitive数值
  分析支持；改为unknown receiver coercion的真实未证明域，保留潜在user-code
  effect与未用结果。partial/source位置/嵌套gap传播重新通过。新JS oracle先
  修正跨realm参数列表的比较及已省略implicit-this return的mutation位置，
  未放宽实际参数、receiver、返回对象、异常或字段状态检查。
- 单一构建-j4、所有build handle关闭后才运行；final-build-2和后续partial
  repair-build-1退出0。最终final-tests-2定向23/23通过27.86秒；另共同selection
  组合test在final-tests-1通过118.21秒。含language constructor、class names/
  accessor/static、module/rest、partial、Logger/Window/ViewModel实际输出。
  Node脚本语法与git diff --check通过。CTest仍565项。日志/tmp/abcd-native-receiver-*。
- 固定五ABC/六模式30次导出全部退出0，输入hash/size与index相符，每模式
  106份complete/明确partial源码Node语法通过。相对class-names pass-2，每模式
  54份表示改变，无complete回退、新legacy或完整数量变化；default80/26、
  arithmetic95/11、numeric96/10、constructors88/18、arithmetic+constructors105/1、
  numeric+constructors106/0，各另46个no-module。产物build/stage-one/
  native-receiver-<mode>-pass-1；summary-1.json与batch-1.log保留。
- 显式--language-constructors及独立数值组合沿用户授权继续，默认VM/profile
  语义不变、不假设父类终生不变。上述106/0仅为完整源码输出计数，阶段一Goal
  保持active；实例字段/globals/完整签名/body结构gate和逐record独立语义/
  表示验收仍开放。全套、expanded corpus、agents、RSS/benchmark、scheduler/
  DFX与周期审计继续暂停，不扩大此次执行范围。

### 2026-10-06：共同词法分析恢复原生 class 名称

- 基线874c932c，规则revision136。新增printer私有ClassNames规划，从
  共同FunctionTree的typed owner/symbol/capture/module/expression所有权
  判定新增class-self环境是否遮住真实reference。heritage、constructor、
  方法/访问器、静态initializer、descendant closure都参与；实际读/写及
  host binding保持身份。匹配identifier上下文仍复用NamedEvaluation；
  其他安全目的地改为原生`(class Name { ... })`/extends。冲突、未知正文、
  不可表达名称保留原命名carrier，不额外拒绝已恢复partial AST。没有
  sample/name/path/局部opcode形状/同块/selection数准入或删除真实语义检查。
- 每个新增词法class范围进入/退出一次；同名active栈和可删除链使每个
  冲突candidate最多退役一次。索引/排序、capture memo、显式worklist、
  数组实际容量和lookup均计入现有scratch/work；公开PrintClassTree返回
  统计包括名称规划成本。无新增依赖、许可token或测试专用production API。
  复杂度/语言依据/覆盖缺口见function-tree-output.md/shape-restrictions-audit.md。
- 新增class_names_test使用实际ABC准备的constructor/accessor families，
  独立词法夹具不伪造source receipt；覆盖heritage TDZ/closure、反转arena、
  同名内部local/外部module/capture读写、未知正文、严格保留字与非ASCII、
  malformed capture范围、work/memory边界及200/201/400/800同名嵌套。
  冲突场景work分别65909/66058/143242/293335；安全场景全部保留原生名。
  这是算法计数回归，不是RSS或全流程benchmark证据。CTest注册565项。
- 两个Logger整record新增表示验收：原生class Logger、实际constructor/
  rest formal、字段赋值、hilog import和直接方法调用、无scratch命名carrier。
  wholeRecordStyle现accepted；允许把命名class内联于default构造、保留
  pinned instance assignment，不要求恢复字面相同的独立声明或DefineField。
  原每record240 basic、848 paired、14 constructor、27 setter/new.target、
  12 default initialization和8个有效JS mutation继续通过。独立paired TS
  在ABC-only生成完成后才读取。其他record不会因这两项验收自动完成。
- 单一构建-j4，最终final-build-1退出0，所有build handle在后续运行前
  关闭；早期DerivedClassBody字段及测试夹具类型/不可复制context编译问题
  已修正。最初两Logger表示RED失败，tests-2 11/11、tests-3 12/12通过；
  最终final-tests-1定向24/24通过28.01秒，包含实际类scope/cell可写绑定、
  accessor/static/partial、module/rest、language constructor Proxy-super、
  WidgetCard callbacks、Window、Logger与ViewModel。Widget测试未覆盖完整
  platform/storage/constructor生命周期；不把此局部测试宣称为全产品验收。
  Node检查修改脚本及git diff --check通过。日志/tmp/abcd-class-names-*。
- 最终固定五ABC/六模式30次导出全部退出0，内容hash/size与corpus index
  相符；每模式106份complete/明确partial源码通过Node语法检查。相对
  invocation-bindings同模式无complete回退、新增legacy或完整数量变化；
  每模式6个record表示改变：两个Logger、push modules的6921/11989/12347
  和widgets的564。两轮导出summary/source逐项相同。最终产物
  build/stage-one/class-names-<mode>-pass-2与对应summary-2.json/batch-2.log。
  default80/26，arithmetic95/11，numeric96/10，constructors88/18，
  arithmetic+constructors105/1，numeric+constructors106/0；每模式另46个
  no-module分类保持。106/0仍是显式组合模式的输出完整性，不是整阶段验收。
- 全套、expanded corpus、agents、RSS/benchmark、scheduler/DFX、周期审计
  继续暂停。旧legacy baseline失败未重跑。此次生产实现及验证有实际进展，
  Goal保持active：实例字段/globals/完整签名/body结构gate与所有record的
  独立语义/表示验收继续开放。既有descendant function内class组合缺口和
  非ASCII native binding表示边界仍记录为实现缺口，不能追加局部模式放行。

### 2026-10-06：复用原生 rest / 当前函数绑定，移除重复快照

- 基线3c64a46e，规则revision135。生产BindingPlan对原本需要临时存储的
  ValueRestArguments / ValueFrameFunction复用实际ParameterBinding /
  FunctionSelfBinding；共同alias/copy身份、EvaluationPlan和原operation
  保留。普通根与nested eager snapshot使用同一源绑定，不按样本、PC、
  寄存器、同块或局部producer形状准入。两个实际Logger的四种method现为
  `hilog.debug(this.domain, this.prefix, this.format, args)`等直接调用，
  不再产生rest的`let $value1; ($value1 = args)`。
- TreeFlow新增有预算的线性source assignment/update census，复用绑定
  不可被实际source write改变；TreeOperands独立核对真实rest formal或
  self身份及每个consumer。真实可写capture仍由CellPlan指向独立cell，
  getter/cell/module快照、phi/cycle/exception和numeric update存储保持。
  ABI移除“rest只能恰好一次source reference”的表示限制，继续要求原
  物理实参scalar不可读、原生rest一次已证明分配和source writes为零。
  原ProveRestOwnership的入口迁移、作用域及效果/异常义务均保留。
- printer和FamilyEmission共用私有DiscardedInvocationRead，省略已初始化
  parameter/self的直接ExpressionStmt纯读；local/cell/module/global/TDZ
  读取不受此投影影响。原事件及ExprId/StmtId/origin保留原边界的零宽
  range，来源传输检查两节点同位置及owner边界；不放行任意空区间。
  第一次GREEN发现旧record slice来源传输仍要求非空，现已共同修复，
  旧slice的segment/来源测试也通过，没有绕过或删除旧路径测试。
- 新增function_tree_test的12种原生绑定组合，覆盖普通/嵌套快照、拆块、
  分支、COPY、leading rest和getter插入；错误rest/self及formal write
  mutation仍由fresh structure/correspondence拒绝。真实rest ABC夹具的
  三profile、宽/窄、寄存器移动、48个完整family及24个拒绝场景保持。
  两个Logger整record仍执行原240 basic + 848 paired、14 constructor、
  27 pinned setter/new.target、12 default init与8个有效JS mutation；
  新增独立直接rest表示断言，ABC-only生成后才读paired TS。
- 单一构建、-j4，日志build-1/2/3/4/5/6、final-build-1和frame-build-1
  均退出0，所有build handle在运行测试/再次修改前已关闭。最初表示RED
  两个Logger失败；整合期间ABI引用数/来源零宽传输及新增fixture的name
  census缺口已修正。final-tests-2定向32/32通过36.77秒，覆盖common
  binding/tree/phi/cell/capture/exception、partial output、rest/arguments/
  async、模块default/init、语言构造Proxy-super及Window/ViewModel记录。
  额外frame-tests-1的4/4通过4.82秒：真实生成async/generator代码的
  self/this/private runtime name、暂停/恢复、并发调用及family naming。
  日志统一/tmp/abcd-invocation-bindings-*；Node --check两个修改脚本及
  git diff --check通过。注册仍564项。
- 固定五个ABC、六模式共30次ABC-only导出，输入SHA/size与既有corpus
  index相符；每模式106份complete/明确partial源码均通过Node语法检查。
  相对cell-formal-names同模式基线没有complete回退或新增legacy输出；
  每模式只两个Logger record的source改变，没有新增complete计数。
  default 80 complete/26 incomplete；language arithmetic 95/11；
  numeric 96/10；constructors 88/18；arithmetic+constructors 105/1；
  numeric+constructors 106/0。每模式另46个no-module分类保持。
  产物build/stage-one/invocation-bindings-<mode>-pass-1，证据
  /tmp/abcd-invocation-bindings-{batch-1.log,<mode>-summary-1.json}。
- 自审与设计更新见function-tree-output.md/shape-restrictions-audit.md。
  新增写入census与来源省略索引是有预算的线性表，没有逐变量重扫或新增
  依赖。全套、expanded corpus、agents、RSS/benchmark、scheduler/DFX及
  周期审计继续暂停；既有legacy baseline失败本轮未重跑。此次实现和
  验证有实际进展。Goal保持active：106/0仅为显式组合模式的输出完整性，
  不等于全部record独立语义/表示验收；匿名class命名carrier、实例字段/
  globals/完整签名/body及整阶段结构gate继续开放。两个Logger的rest
  表示已改善，wholeRecordStyle仍为pending，不将局部质量算为整record完成。

### 2026-10-06：两个 Logger 的 rest、实例赋值与默认初始化验收

- 基线88fd8fb8，production/rules134保持。扩展既有两个value Logger
  测试，新增test-only logger_record_state.mjs；仍由ABC-only实际生成
  complete/non-legacy/value_tree整record，生成完成后才读取独立paired TS。
  原每record240 hilog场景、2个有效JS mutation保留；没有修改样本/ISA/
  生产恢复，没有新增CTest注册。两个未初始化private类型声明仍只在
  独立参考中擦除，initialized format字段保留。
- 每个record新增848 paired场景，实际四个原型method和native rest一起
  执行；核对name/arity/descriptor、非constructible方法、借用receiver、
  getter/call/field顺序、live imported hilog在调用前/callee取得/字段读取
  时变化、字段后续状态及任意原抛出身份（undefined/0/object）。原生
  rest数组覆盖0/1/8/200/201实参、稀疏输入的dense副本、元素身份/descriptor、
  原callee receiver与宿主返回值丢弃；getter重入另验每activation数组独立。
  正常完成显式检查，不把提前异常算作等价。
- 两份实际class与参考各验证14种constructor prefix值，含Symbol、BigInt、
  对象和禁止coercion的对象；保留原值、字段顺序/descriptor、constructor
  name/length及prototype关系，实例保持fresh。default模块namespace TDZ、
  单例身份及重复evaluate不重复构造也检查。
- 每record27个独立pinned assignment场景验证format/prefix/domain继承
  setter、new.target/prototype getter及原抛出对象，含prototype getter先
  失败不执行body。每record12个实际默认导出初始化场景在module evaluate
  前安装Object.prototype setters，验证先后写入及暴露的部分receiver。
  构造中止保留errored module、default TDZ和原失败的重复evaluate缓存，
  不重新执行已完成setter。Node initialized TS format采用DefineField，
  因而上述两项义务仅按pinned STOBJBYNAME独立断言，不误用native字段
  定义作其oracle。
- 新增每record6个先解析的有效JS mutation检出：rest内容丢失、跨activation
  数组共享、宿主返回值泄漏、callee receiver丢失、format setter绕过、
  default prefix身份丢失。原2个mutation另保留。tests-1/2两个Logger均通过；
  final-tests-1定向10/10通过3.84秒，包含rest methods/argument snapshot/
  async arguments、module default export/init及Breakpoint/CardArray。
  两个Node文件--check及git diff --check通过；注册仍564项。
  日志/tmp/abcd-logger-state-{tests-1,tests-2,final-tests-1}.log。
- 当前ABC SHA/size与corpus index相符：pasteboard854fb0d9bbc1/112024，
  dynamic_12_0_6_0；MultiCommunity45ffdc7d5075/514872，dynamic_12_0_2_0。
  自审保持ABI/原数组/任意throw与参考边界，test-only无需生产重建或六模式
  重跑。全套、expanded corpus、agents、RSS/benchmark、scheduler/DFX及周期
  审计继续暂停；旧legacy baseline失败本轮未重跑。
- 本轮没有关闭Logger整record表示质量：当前默认构造仍含匿名class的
  computed-property name inference carrier，rest也保留一个snapshot别名。
  printer现有注释明确carrier不增加immutable inner class binding；直接
  改named class会改变self/scope，不能只按样本或外观替换。继续由共同
  binding/命名上下文规划证明实际声明位置、free/captured名字及effects，
  不新增局部匹配或盲删语义检查。测试输出显式wholeRecordStyle=pending。
  阶段一Goal/实例字段/global/完整签名/body与全部record语义及表示gate保持。

### 2026-10-06：两个 ViewModel 与真实模型记录连接验收

- 基线6bcb3600，production/rules134保持。本轮迁移现有
  paired_viewmodel_record_source_test：原--source旧writer输出缺少
  source_complete，新增共同路径完整性检查后RED退出8；改为ABC-only
  --value-source，实际records29119/29619及依赖16752/17544必须均为
  source_status=0、source_complete=true、legacy_writer_used=false。
  CLI结束后独立Node oracle才读取四份paired TS。没有替换生产恢复逻辑，
  没有借paired source填充输出或修改样本。原测试注册保持，仍564项。
- 实际IconInfoViewModel/SectionViewModel完整body执行1603 paired边界
  场景，覆盖四个方法及name/arity/descriptor、模块TDZ/export、实例字段
  descriptor与存储身份、构造前/中及后续构造的live imported constructor、
  字段/方法/slice getter替换后的已获得callee及receiver、无参模型构造。
  原生Array.prototype.slice覆盖0/1/4/200/201、holes、原元素身份、17种
  count（含BigInt/Symbol拒绝、valueOf状态变化/原异常）、length/index/has、
  constructor/Symbol.species及其构造length/结果身份和异常次序。
  正常场景显式拒绝意外TypeError，未把双方提前失败计为成功。
- 四份真实恢复record在同一个ESM graph一起执行1199 paired场景；两个
  ViewModel各建两个真实model实例，实际initData/map/new item/push和
  getter/slice方法都运行。只HomeConstants是受控资源边界。ordinary、
  sparse、getter-time live、第二次init的late-live、reverse deferred回调
  覆盖原捕获receiver和多实例列表归属。第一/第二/第三次观察抛出原对象
  时核对trace、部分已写入state及返回结果。WeakMap确认原资源对象
  身份，实际Item实例与getter/field descriptor及切片原item身份都检查。
- 八个独立pinned assignment场景覆盖两种ViewModel的继承setter、原抛出
  对象及alternate new.target/prototype getter；setter接收实际new model结果，
  模型先构造再写入字段，实例/model保持fresh。与Node initialized TS字段
  DefineField的不同义务分开，不用native字段定义掩盖STOBJBYNAME。
  表示验收另核对class/constructor、字段初始化、真实count签名、原生
  get…().slice(0,count)及exports；不要求逐字或机械临时变量编号一致。
- 五个先解析的有效JS mutation检出：slice参数丢失、callee receiver丢失、
  own definition绕过setter、真实model初始化删除、真实item参数交换。
  tests-1通过1339/1033；tests-2扩展live与较晚异常为1603/1199；最后再加
  元素/资源identity、species length及正常完成检查。final-tests-2定向12/12
  通过17.46秒，含ViewModel/Model/Home完整记录、module init/import/tree、
  function/family naming、cell names、language constructors及Window记录。
  证据/tmp/abcd-viewmodel-record-{red-tests-1,tests-1,tests-2,final-tests-1,
  final-tests-2}.log；最终1603 boundary/1199 connected/8 assignment/5 mutation。
- 输入SHA/size与现有Immersive corpus index一致（8de95a4c6579，293544 bytes，
  dynamic_13_0_1_0）。test-only，不需要生产重建/重新配置，也未重复六模式
  batch；上轮产出计数不作为本轮新增完整记录。自审确认独立source边界、
  原对象身份、原异常/部分状态、真实方法和Node字段差异。旧legacy baseline
  失败本轮未重跑。全套、expanded corpus、agents、RSS/benchmark、scheduler/
  DFX和周期审计继续暂停。阶段一Goal/结构gate仍开放：新增两个真实记录
  及其连接图的受控独立验收，不证明全部UI/platform、缺失元数据完整签名、
  所有实例字段/global/body与所有record表示质量已经验收。

### 2026-10-06：通过共同 cell / entry 身份恢复捕获参数名

- 基线21f3eb57，规则revision134。新增私有NameCellParameters，接在共同
  ResolveFunctionSyntax的scope literal命名之后；普通、分阶段family和
  standalone路径传入已有同revision CellPlan。复用AnalyzeValueIdentities
  的copy/phi/SCC身份，从真实ValueLexicalStore的stored-value找到原始
  ParameterEntry，把allocated cell元数据拼写作为尚未命名formal的偏好。
  实际输出setWindowOrientation(orientation)、onWindowStageCreate(windowStage)。
  debug formal已有证据优先，冲突拼写保留合成名；incoming cell属于另一
  activation，不提供当前formal提示。元数据cell名不证明原源码formal名、
  类型、const或不可变，也不证明两种存储可合并；later write保持原行为。
- 发现并修复interned NameId与binding身份混淆：同一个拼写同时给formal
  和allocated cell时，原tree唯一性检查不能区分两种binding。现使用既有
  sorted formal name index给参数保留拼写，cell继续独立合成binding。
  没有删除真实scope/capture/TDZ、receiver、effect/exception校验，没有
  新增同块、first-store、immutable、opcode序列、PC/register或样本分派。
  family共同ReferenceNames仍防止实际global/module/descendant shadow。
- RED：完整Window记录签名断言失败。真实decoded fixture覆盖copy、
  block split、同源/混合phi、self-copy SCC、多formal、多cell、冲突/同名、
  debug优先、后续写入、200/201/402/804及work/bytes边界。分阶段异常
  fixture保留allocation完成与incoming catch存储之别。初轮失败中，测试
  packed STLEXVAR slot编码错误由LLDB定位修正；production binding冲突
  修复后，旧测试对let/windowStage及$arg0文本的假设改为实际参数transport。
  quota失败不发布部分parameter修改；生产未保留调试instrumentation。
- 最终单-j4构建均退出0，/tmp/abcd-cell-formals-build-3.log、build-4.log及
  final-build-1.log覆盖修改的生产/fixture和其必要消费者。最终定向26/26
  通过26.00秒（final-tests-1.log），注册564项保持；含cell/phased/tree/
  formal/scope、route/record、构造及完整Home/model/Push/VoIP/Window验收。
  Window7442 paired、56 assignment、5有效JS mutation保持；named pasteboard
  240及default240保持；普通/arrow延迟capture扩展为168行为检查与2个
  先解析的有效JS mutation。原2752 naming、17880 numeric、4536 optional、
  2450 paired/120 native constructors证据保留。
- 固定五ABC六模式30次导出全部退出0，每模式106份complete/explicit partial
  source解析通过，SHA/size保持、legacy_writer_used=0、无complete回退。
  complete/incomplete：default80/26、language95/11、numeric96/10、
  constructors88/18、arithmetic-constructors105/1、numeric-constructors106/0；
  另46个no-module记录。与基线计数相同；default/constructors改善15份source，
  其余模式22份，未新增complete记录。证据/tmp/abcd-cell-formals-batch-1.log、
  /tmp/abcd-cell-formals-<mode>-summary-1.json及build/stage-one/
  cell-formal-names-<mode>-pass-1。source_complete不等于阶段一总验收。
- 自审确认命名偏好与存储身份分离，使用既有稀疏身份分析及排序索引，
  不逐参数走图；复杂度/前提见parameter-names.md。两个旧legacy baseline
  失败本轮未重跑，保持开放。全套、expanded corpus、agents、RSS/benchmark、
  scheduler/DFX及周期审计继续暂停。阶段一Goal和结构gate保持：缺少元数据
  的完整签名、实例字段/global/body集成、全部记录独立语义和表示验收仍待推进。

### 2026-10-06：完整 WindowUtil / WindowInfo 记录独立验收

- 基线51141fd0，production/rules133保持。新增window_record_source_test
  使用授权--language-numeric，由ABC-only实际生成record7618的完整共同
  tree；CLI进程结束后才读取paired WindowUtil.ts。source_complete=true、
  route=value_tree、legacy_writer_used=false；executable=false保持。
  输入SHA/size与现有Immersive index身份一致（8de95a4c6579，293544 bytes）。
  原17880数值及4536局部optional调用测试保留，未以新whole record验收
  代替其更细的数值/coercion与短路证据。
- 实际两个class、enum和ESM export一起执行7442 paired场景，覆盖16个
  原型方法及4个实例回调。验证class/method name/arity/descriptor、module
  TDZ/export、WindowInfo八项初始化及字段descriptor、实例新对象/回调
  freshness、enum/外部global读取、真实receiver、事件注册/解除、输入
  getter与多次属性读取、部分状态、原异常/code/message getter异常。
  mainWindow/UI替换及真实window import live-binding更新从既有getter/call
  的不同时间点触发；不提前重取已获得callee。async windowSize callback
  另验返回native Promise/rejection与同步throw之别。
- lifecycle执行实际setUIContext/updateWindowInfo/status/avoid/title/size
  回调及release，以无关this调用保存的回调。concurrent场景从两个实例
  启动实际orientation/system-bar/limits Promise链，pending期间替换窗口并
  新建另一实例，控制正/逆settle和reject；原捕获参数、owner、晚期字段写入
  及所有owned链的异常/完成都核对。独立host只提供平台API边界，实际
  recover、setImmersiveType、updateAvoidPadding与getCalDegree仍由恢复body
  执行，没有用函数double替代这些方法。全部pending链显式等待。
- Node保留TS type-only字段定义，oracle仅擦除八个无initializer的类型声明；
  initialized字段和所有方法保持。STOBJBYNAME与native TS DefineField不是
  同一义务，新增56个独立assignment场景，逐项验证两个class共14个初始化
  属性的继承setter、原抛出对象及alternate new.target/prototype读取。
  pinned constructor assignment的callback空name与native TS field的property
  inferred name分别明确验证，不把后者当成ABC lowering的命名证据。
  Observed host为identity；不宣称真实平台装饰器/观察协议验收。
- 五个有效JS mutation检出：丢失windowSize listener、size参数、延迟
  orientation写入、gravity ID，以及用own definition绕过继承setter。
  mutation先解析，语法失败不计行为检出。源码表示另验native class/new、
  async arrow、sensor call、then链、数值表达式与exports；不要求临时名字
  逐字相同，也未借测试文本匹配添加production规则。
- configure-1退出0，CMake只新增四行；注册564项，原两项Window测试保持。
  tests-1独立首轮7131/4通过，tests-2加入async完成和concurrent后7442/4，
  tests-3增加assignment后7442/4/56通过。最终final-tests-1定向11/11通过
  16.36秒，含Window三项、parameter origins、family names、语言构造、完整
  Home/model/Push/VoIP及module initialization。最终新测试7442/5/56。
  日志/tmp/abcd-window-record-*。test-only，无生产重建/六模式重复导出，
  无全套、expanded corpus、agents、RSS/benchmark、scheduler或DFX。
  两个旧legacy baseline失败仍见上轮记录。阶段一及结构gate继续开放：
  这是一个真实记录的受控完整行为和表示证据，不证明全部记录/平台协议、
  所有外部scope或缺少metadata的完整签名已经验收。

### 2026-10-06：共同 tree 接受合法 debug 参数名

- 基线8f2f56cb，规则revision133。RecoverParameterNames新增私有typed
  ParameterNameDomain；共同ResolveFunctionSyntax选择SourceTree域，使用
  已有IsSourceIdentifier。legacy默认仍保留其私有namespace限制。
  已证明entry身份的$arg0/$value0/$local1/ClassValue/MethodSelfValue等
  合法formal不再因旧writer命名约定丢失。原debug boundary、稀疏SSA
  copy/phi来源、duplicates、预算及真实scope/ABI/effect校验保持；真实
  descendant global/module冲突仍由共同NameReservations规划合成名。
  reserved/非法identifier保持合成名，不新增同块/指令序列/样本准入。
- family_names真实编码DebugInfo/DebugScopes新增80个formal fixture，
  覆盖十种hint、global/module、named/plain和straight/split组合；共
  128 decoded families通过2752行为检查与原48个有效JS mutation。
  检查debug evidence及实际输出formal、identity/name/arity、live callee、
  receiver、getter/原异常，并在同一实际metadata/frame上另验legacy域。
  RED在debug evidence断言失败，production修复后GREEN。ABC-only生成
  及独立native oracle边界保持；这不是全record/platform验收。
- final-build-1单次-j4重建14 targets退出0；final-tests-1定向22/22
  通过32.48秒，包含名字/来源、family/class/record、语言构造、完整模型、
  Push Ability/VoIP及module init。首次tests-1中两个legacy测试失败：
  created_function_test.cc:183、created_class_test.cc:132。保存本轮四个
  production文件，临时使用精确HEAD8f2f56cb内容重建并复测，二者同位置
  失败；恢复本轮内容后完成最终重建。这是已核实baseline失败，不能计为
  本轮通过，也不将22项定向通过描述为全套通过。日志
  /tmp/abcd-tree-formal-names-{red,baseline,final}-{build,tests}-1.log。
- 固定五ABC/六模式30次导出全数退出0，每模式106份完整/明确partial
  source可解析，输入SHA/size匹配index，legacy_writer_used=0；相对
  family-names-pass-1无source字节变化、无完整性回退或新增完整记录。
  complete/incomplete依次为default80/26、language95/11、numeric96/10、
  constructors88/18、arithmetic-constructors105/1、numeric-constructors106/0；
  每模式另有46项无module source。本轮扩展通用合法命名接受范围，
  不声称现有paired corpus覆盖增加。输出build/stage-one/tree-formal-names-*，
  日志/tmp/abcd-tree-formal-names-batch-1.log。测试注册保持563。
- 无新算法/dependency、全套、expanded corpus、agents、RSS/benchmark或
  scheduler/DFX。ASCII identifier范围、未证实完整formal metadata、类型/
  rest/default签名及真实外部scope义务仍有边界；结构gate与阶段一Goal
  继续开放。详见[参数命名](parameter-names.md)与恢复条件审计。


### 2026-10-06：完整 Push Ability 与 VoIP 模块图配对验收

- 基线bdb161f7，production/rules132保持。原push_registration_source_test
  默认模式仍验证明确partial记录中的两个成员，本轮保留其注册和范围。
  新push_ability_record_source_test以授权的--language-constructors从ABC
  实际生成record3444完整共同tree，再生成record11989的VoIP service；
  两个engine进程结束后才读取paired TS。输出source_complete=true、
  route=value_tree、legacy_writer_used=false；CLI的executable=false标志
  保持，未因Node成功伪造完整Ark/platform发布证据。输入SHA/size匹配
  abc-index：b2074425e0e2、113684 bytes，样本与sidecar均未修改。
- 完整PushMessageAbility运行322 lifecycle及1860 delivery paired场景。
  覆盖实际构造/原参数身份/继承、模块TDZ/default export、十一方法的
  name/arity/descriptor、onCreate四类注册、全部其他lifecycle及window
  callback两路；原异常及其code/message getter异常保持。两个实例的
  callback在后续注册后仍读取原owner的context，回调以无关this调用。
  delivery前与参数getter期间更新实际module live binding，acquired
  service callee保持原receiver；原生Promise正序/逆序settle、成功/拒绝、
  then/catch getter/call及晚期log副作用对照。owned chains全数等待，
  包括故意制造的dangling/rejected chain，避免测试自身unhandled rejection。
- 八组connected场景加载实际恢复的Ability+VoIP service两个ESM记录，
  依赖边连接原native源码，不用service函数double替代其body。仅平台
  UIAbility/push/hilog/telephony/image/resource边界由独立host提供。
  两实例并发启动，控制raw请求正序/逆序release，分别raw/pixel/incoming
  reject；对照来电属性、static callId、listener async调用及close/off顺序。
  close未返回其启动promise，fixture包装原processVoipCallEvent以保留
  实际promise并等待，原方法body照常执行；不以日志尾项猜测异步完成。
- 原生class/super/receiveMessage/then表示另验；三个有效JS mutation
  （删除BACKGROUND注册、省略captured context读取、反转window条件）
  均检出，先解析再运行，语法错误不计语义检出。不添加恢复匹配、
  样本分派、生产源码字符串规则或引擎sidecar输入。
- configure-1退出0；tests-1 fixture误用Object.keys读取TDZ namespace而
  失败，改用GetOwnPropertyNames枚举再独立验证TDZ后tests-2通过。
  tests-3连接测试错误等待日志尾部state，按close真实void/启动promise
  语义修复fixture，tests-4通过322/1860/8与三mutation。最终只加强原
  constructor object参数身份及命名microtask deadlock bound；
  final-tests-1定向8/8通过10.35秒，包含原partial Push、VoIP、语言构造、
  module entry initialization、普通construct及上轮paired model回归。
  日志/tmp/abcd-push-ability-record-*；新测试同时要求两份paired source
  存在，configure-2退出0且新/旧Push与VoIP三项注册保持。CMake只新增
  六行，563测试注册。
- 本轮test-only，无production重建/六模式重复导出；无全套、expanded
  corpus、agents、RSS/benchmark、调度器或DFX。该记录及两记录连接的
  独立host语义证据不等于实际平台、所有UI/async方法或全部record验收。
  阶段一Goal与字段/全局/完整签名/body集成gate继续开放。


### 2026-10-06：两个列表模型记录的共同路径独立验收

- 基线2f8aed4b，production与规则revision132保持。现有
  paired_model_record_source_test由legacy --source迁到--value-source，
  manifest要求IconInfoModel/SectionModel完整且legacy_writer_used=false。
  ABC-only生成结束后才读取独立paired TS；输入SHA/size流式校验匹配
  现有abc-index：Immersive/modules.abc，8de95a4c6579，293544 bytes。
  不增加样本分派、恢复条件、引擎sidecar输入或第二套报告设施。
- 两个完整记录（共四个class）执行1476 paired场景：0/1/2/200/201
  输入长度，普通、input getter更新module、参数索引读取期间更新module、
  push getter替换list、延迟callback后替换module/list及sparse map。
  对照完整effect trace、getter/callee receiver、原抛出对象、结果状态、
  callback lexical this、重复init、每实例新数组、class/method name与arity、
  模块TDZ/exports。实际第二组已有第一组的effects时抛出，保留部分状态；
  deferred callback用无关this调用，不靠回调调用方提供receiver。
- Node strip TS保留未初始化字段的DefineField，不作为pinned属性赋值的
  own key/setter oracle。paired比较方法值及效果；生成记录另验实例字段
  顺序、descriptor和SectionProduct未传originalPrice时无own property。
  8 assignment场景分别覆盖两个model及各自item的继承setter、首写原异常
  和alternate new.target；26参数取值场景覆盖undefined/null/boolean/
  ±0/NaN/Infinity/BigInt/string/Symbol/object/array的Object.is及descriptor。
  普通constructor/map/arrow/push/new表示单独检查，条件属性另验行为，
  允许必要capture temporary，不以临时编号或逐字一致作验收。
- 五个有效JS mutation检出：删除init、丢失构造参数身份、交换flash参数、
  optional属性无条件写入及own definition绕过setter。先独立解析mutation，
  语法拒绝不计为语义检出。该fixture仍属测试，允许用测试字段/方法名称
  选择观察点；这些名称和paired内容从不进入production规则。
- 迁移后旧oracle定向1/1通过；paired oracle tests-1及逐步加强的
  tests-2/tests-3均通过，tests-3 verbose打印1476/8/26/5计数。
  final-tests-1定向11/11通过21.19秒，包含local model实际constants/breakpoint
  依赖集成、viewmodel/static class batch、HomeModel、function/construct、
  family names与language constructors回归。日志/tmp/abcd-model-record-*。
  测试注册仍562，无CMake改动；test-only不需重建生产或重跑六模式导出。
- 这是现有完整输出的更强独立语义与表示证据；不新增ISA支持，不计为
  全部record/platform/全部类型签名验收。没有全套、expanded corpus、agents、
  RSS/benchmark、scheduler或DFX工作。结构gate及阶段一Goal保持开放。


### 2026-10-06：按 family 引用索引规划绑定名称

- 基线b14fd6ee，规则revision132。普通/分阶段family先一次解析共同
  syntax/constants/module aliases，再从live ValueGlobalRead及实际module
  aliases建立owned name/owner索引。原incoming父子边与preorder subtree
  区间提供查询范围；局部/formal metadata偏好和合成名都避开真实引用，
  不靠反复猜名或整族retry。实际capture/scope/TDZ/write/effect/exception
  校验保持。指令含义、receiver、执行次数及原资源前提没有因命名改变。
  详见[cell命名](cell-names.md)。
- RED真实编码fixture复现父词法hint遮蔽子global：PreparedFamily的
  FamilyEnclosureFailure=kShadowedGlobal，CTest退出8。GREEN发现旧
  tree标识符检查复用legacy私有namespace策略，额外拒绝合法的$名字、
  ClassValue和MethodSelf前缀。tree/module enclosure/cell hint使用既有
  IsSourceIdentifier并保留真实shadow检查；legacy/旧metadata adapter
  仍隔离原限制。没有把私有标签限制描述为语言规则，也没有盲删校验。
- 新family_names_test 48 decoded fixture覆盖global/module、named/unnamed
  scope、参数copy/拆块、普通名字与旧私有前缀；正向prepared family打印，
  实际external shadow拒绝且无输出。索引200/201/402/804 sparse owner、
  空区间、UINT32_MAX endpoint、work callback耗尽通过。cached syntax、
  owned strings/vector capacity/查询work计入同一预算；O(F+N+G log G+
  Q log G)另计字符比较，不造F×G矩阵或每变量图遍历。callable子树粒度
  可保守省略更窄block里的安全提示，只改变命名选择、不拒绝body。
- family_names_source_test执行实际生成JS，1032 identity/arity/name/
  fresh closure/live binding/getter/call/原异常场景通过；48种初始化值
  与绑定shadow的有效JS mutation检出，先解析再执行。Node sandbox对象
  getter会把原异常转成ReferenceError，改在realm内部定义getter；throwing
  global lookup还可重复访问，最终与同realm独立native函数比较完整trace，
  不硬编码host访问次数。此证据是普通family，不是record/platform验收。
- red-build-1新target尚未configure，build-2有fixture API拼写/参数错误，
  configure及按实际API修复后red-build-3通过。final-tests-1 26/27通过，
  function_tree旧测试仍要求拒绝$参数；debug定位后改为合法$参数、独立
  临时名字及原semantics验证，eval负例保留。最终production十六目标
  单-j4 build-6退出0，最后fixture加强build-7退出0；final-tests-3
  27/27通过24.12秒。其后只加强Node异常trace oracle，source-final-2
  1/1通过0.72秒。562个测试注册，CMake仅新增六行，无删除旧注册。
- family-names六模式pass-1共30次固定五ABC导出全部退出0，每模式106份
  完整或明确partial源码解析，SHA/size匹配index；无legacy、完整回退、
  新complete或源码字节变化。complete/incomplete-source仍80/26、95/11、
  96/10、88/18、105/1、106/0，各46份no-module source保持。该批次与
  最终production相同，后续只有fixture/oracle与文档改变。日志前缀
  /tmp/abcd-family-names-，输出build/stage-one/family-names-<mode>-pass-1。
- 无样本/sidecar生产输入，无新ISA扩展、agents、全套、expanded corpus、
  RSS/benchmark或DFX前置。普通命名冲突覆盖补齐不等于阶段一完成；
  metadata formal集合、保守block命名、外部host绑定及seeded record
  名称仍各有边界，继续实例/全局/签名/body整合与整record独立语义/风格。


### 2026-10-06：函数与实例初始化记录验收迁到共同通路

- 基线1277e970，production及规则revision131保持。本轮补齐独立验收，
  不增加样本分派或恢复准入条件。先重跑上轮三个已确认基线失败：
  record_function_js_test、record_function_construct_js_test和
  paired_home_model_record_source_test均复现；debug定位函数fixture的
  旧export临时名断言，HomeModel仍要求旧$acc文本与legacy --source。
- record_function fixture改为真实merged-module wire metadata、同文件
  context与FunctionCreationSites，调用BuildRecordSource的required
  value-tree route，要求完整artifact。取消legacy writer临时变量文本
  断言；保留modern/historical profile、未初始化构造参数拒绝。Node
  验证12种值的Object.is/返回身份、name/length属性、普通构造的对象
  返回替换、async不可构造及thenable/原异常。没有把method kind当作
  函数行为验收。尝试增加provenance断言时误以为record PrintedSource
  保留member span与module初始化字段，实际该API未提供；撤回该假定，
  不因验收修复扩大暂停的DFX工作。
- HomeModel测试先ABC-only --value-source导出，manifest要求完整且
  legacy_writer_used=false，再读取paired TS。138场景覆盖空/1/2/200/
  201项、import live更新、获取push callee期间替换list、初始化getter/
  调用、map/getter/字段/push各点原异常、重复初始化、每实例新数组及
  方法返回同一list；对象own key顺序和值/descriptor保留undefined。
  constructor/map/push native表示单独检查，不再依赖$acc或$local编号。
- setter-capable实例赋值保留在构造体。Node strip TS保留native field，
  其继承setter规则不等于pinned STOBJBYNAME；四个独立assignment场景
  以可观察预期验证setter先于init、setter抛出阻断init、alternate
  new.target receiver/prototype及新数组身份，不把strip结果作为这项
  义务的oracle。改own definition、删除init、改object属性三种有效JS
  mutation均检出；先独立解析mutation，语法失败不算检出。
- Home测试第一次新增host oracle误用已被instrument的texts.map来
  构造常量数组，导致测试自身失败；改独立Array.from后通过。fixture
  build-1误用不存在的receipt成员退出2，之后按实际PrintedSource API
  修复。final单-j4 record_function_test build-5退出0；final-tests-3
  定向9/9通过17.35秒，包含现有viewmodel/static class batch、module
  tree、paired model以及language_constructors_paired_test。后者当前
  重跑2450 paired constructor cases及120 native iterator cases通过。
  日志/tmp/abcd-record-acceptance-*；Home独立测试报告138+4及3 mutations。
- 这是三个旧验收缺口与一个实际完整record的补强；companion paired
  model旧测试当时仍使用legacy，仅作为行为回归；本页后续“两个列表
  模型记录”条目完成其共同路由迁移与独立paired验收。没有修改ABC/paired
  inputs，没有启动全套、扩大语料、RSS/
  benchmark或agents；test-only修改不要求重跑六模式固定五ABC导出。
  结构gate与阶段一仍开放，平台/全部record独立行为与表示质量未完成。


### 2026-10-06：参数名按入口来源贯通，移除前导布局匹配

- 规则revision131，基线fc67ddfb；MapParameterDebugNames正式移除连续MOV
  prefix、固定五条captured-this和分支进入prefix的布局条件。有效debug
  range start通过只读观察进入既有RegisterSsa；原normal CFG、pinned
  semantic lift和selected exception dispatch提供Before/Commit/Catch拓扑。
  入口参数经真实透明copy/phi一致性建立名称提示，当前callable/ABI、
  标识符、重复及enclosing visibility义务保留。不擦除body指令，不新增
  writer、样本/名字分派或producer/same-block规则。详见
  [参数命名](parameter-names.md)。
- 先添加等价MOV→LDA/STA、NOP与拆块fixture；RED旧resolver发生预期
  bad_variant_access，CTest退出8。共同参数来源版本通过后，增加直接ABI
  range、same/mixed phi、循环及self-copy SCC、异常保留与catch覆盖、
  raw suspension前后/回边、dead opaque tail、上游命名/独立冲突、重复
  range、边界与200/201/402/804 observations。原copy增长用例继续通过。
  称谓提示不建立const、storage identity、源码类型或initializer移动许可。
- 第一版整段语义提升使async及两个callback丢失旧名字，pass-1不作最终
  交付。现在显式opaque边界只沿真实可达控制/handler后继传播未知frame；
  不能让尾段未知阻断更早的入口证据，也不能忽略能回到入口的未知状态。
  后续debug local可能在初始化前暂存参数copy：def-use SCC中的上游
  命名存储优先于下游提示，独立root冲突保留合成名。单一copy环不会
  成为自己的ancestor冲突，不按PC/register/复制距离选择。最终恢复
  count、groupTitle、parent、_item、imageArray等，原有async/callback名
  保留。前期两处窄化转换导致build-3退出2，已显式类型化并修复。
- 新parameter_origins_source_test先ABC-only导出完整Content record，再读
  paired TS；隔离实际getBirdImages(count)验证176个长度、ToPrimitive、
  原异常与对象新鲜身份场景，丢弃count的有效JS mutation检出。用最小
  import/ViewPU宿主只访问prototype方法，不调用UI或构造器，不计为
  整record/platform行为验收。原pasteboard 240+240对照保持。
- 提交前发现CMake新增注册编辑误删旧注册，全部从HEAD恢复再仅增加新
  source/测试；最终diff为8插入/1删除，560个测试注册且抽查原class/
  constructor/record项存在。没有提交误删或启动全套。final单-j4十一
  目标build-8退出0；恢复注册后的final-tests-2定向27/27通过13.74秒。
  新建流程的临时诊断已移除。三个上轮已确认的基线旧测试失败保持记录，
  本轮没有将它们计为通过。RSS/benchmark/expanded corpus/agents未启动。
- final parameter-origins六模式pass-4共30次固定五ABC导出全部退出0，
  各106份完整或明确partial源码解析；SHA/size匹配index，无legacy及
  complete回退。各record合成参数token数未增加（表示回归检查，不是
  独立语义证明）。default/constructors各5份、其余模式各13份源码
  签名与使用表示改善。complete/incomplete-source仍80/26、95/11、
  96/10、88/18、105/1、106/0；46份无module source分类保持。最终
  production与该导出一致；后续CMake修复不改变engine。
- 结构gate及阶段一仍开放：没有完整debug formal集合时继续合成名；
  opaque之后的观察没有来源证明。原default/rest独立所有权与长度义务
  不因命名放宽。setter-capable实例赋值、全局/closure、完整签名及整
  record独立语义/源码风格继续验收；106 source_complete不是Goal完成。

### 2026-10-06：普通 callable 复用词法存储命名

- 规则revision130，基线f5c4c770；普通及phased方法、创建函数和其子闭包
  的ResolveFunctionSyntax接入既有ResolveCellNames。scope literal仅提供
  allocation/slot/NameId提示，原CellPlan、binding scope、capture lifetime、
  instruction origins、family writes/enclosure及文件identity继续独立检查。
  无新增样本分派、opcode序列、同块或producer准入。工作/字节开销纳入
  同一syntax budget。详见[词法存储命名](cell-names.md)。
- 新method_cell_names_source_test先观察到windowStage仍为合成名的预期
  断言失败，再接通共同adapter。ABC-only生成后才读paired源码；完整模式
  onWindowStageCreate及其闭包恢复windowStage，240个nullish/成员callee
  mutation/异常顺序对照通过，替换捕获参数初始化的有效JS mutation检出。
  原默认模式partial callback另240场景保持。十二个新增decoded normal/
  arrow local/nested-writer fixture验证名字、真实捕获、reserved/当前name
  arena冲突和文件关闭后的retained family打印；既有cell_names的200/201/
  几何增长与资源边界继续通过。行为范围是实际方法，不是平台整record。
- build-1/2/3通过；扩大final-tests-1为28项，25通过、3失败。临时恢复
  两个production文件到基线HEAD并完成baseline-build-1/三项重跑，三个
  同样失败：record_function_js_test、record_function_construct_js_test的
  fixture提前失败，paired_home_model_record_source_test仍要求旧$acc文本。
  没有放宽旧断言或把失败计为通过；这些是已确认的基线验证缺口。
  恢复本轮production内容并逐字核对backup后，最终单-j4十目标build-4
  退出0；final-tests-2相关25/25通过19.73秒，包括上述命名/捕获/作用域、
  phased/rest/async、argument snapshots、模块与配对model/viewmodel行为。
  没有启动暂停的全套、RSS/benchmark或扩大语料。
- fresh method-cell-names六模式pass-1共30次固定五ABC导出全部退出0，
  各106份完整或明确partial源码解析通过；SHA/size匹配既有corpus index，
  无legacy或complete回退。相对rules129，default/constructors各16份
  源码命名改变，其余模式各29份；计数保持default80/26、language95/11、
  numeric96/10、constructors88/18、arithmetic-constructors105/1、
  numeric-constructors106/0（complete/incomplete-source）。各46份无module
  source分类保持。最终production内容与导出时一致；fixture补充不改engine。
- 仍未关闭结构gate或阶段一：captured storage名不足以证明formal名，
  $arg0等缺少真实debug/formal证据的参数保持合成名；setter-capable构造
  赋值不能冒充own class fields。普通family的ancestor metadata shadow
  descendant free-name仍由原enclosure拒绝，generated-name retry尚未接入，
  明确为表示缺口。继续完整签名、实例初始化及整record独立行为/源码风格
  验收；source_complete、语法解析和方法对照不能替代这些完成条件。

### 2026-10-06：显式语言构造语义贯通普通 SuperSpread

- 规则revision129，基线d9b6d25e；延续25aa原分支。用户明确允许独立
  显式语言构造模式：`--value-source --language-constructors`按JS/ArkTS
  native super验收，分别组合默认、language-arithmetic和language-numeric。
  三个新route contract保留数值前提与V3宿主政策，既有三个当前模式含义
  保持；CLI冲突/重复/非value-route在读文件前拒绝，receipt/manifest及
  phased route完整保留各自identity。详见[语言构造语义](language-constructors.md)。
- ConstructorSemantics复用共同ExpressionForest、SuperCallExpr、tree
  child/structure/operand检查及printer；参数是原第二输入的owned
  SpreadArguments，不用第二writer/Reflect.construct或把数组当单参数。
  VM-only当前构造器仍不打印为源码变量。family提供原FileFunction，
  fresh checker对同input/work graph/method/route identity调用一次既有
  sparse DerivedValueStates，证明constructor经copy/cell/phi仍是当前调用。
  原class context、receiver initialization/guards/capture、求值与异常
  顺序保持共同pipeline。未知父类的终生JSFunction假设未新增；默认
  SuperSpread和apply的独立差异未被数值或构造selector默默放宽。
- 新定向构造树覆盖三个动态profile、三新契约、rest/普通数组/getter输入、
  复制/拆块及0/1/200/201/402增长，共198个完整函数树通过fresh semantics和instruction
  origins；旧契约/foreign constructor/缺失FileFunction/stale contract拒绝。
  这是解码到完整函数树的证据，独立执行另由实际ABC构造器测试提供。
- 独立构造对照先完成十份ABC-only record生成，再读配对TS。普通iterator
  与普通own descriptors下检查十个构造器的参数身份、Proxy/bound/arrow
  当前父类、alternate newTarget、返回对象/primitive及原异常；LocalStorage
  是显式最小测试宿主。2,450个paired构造场景通过，另120个explicit
  native super oracle检查iterator getter/call/next异常及替换值通过；
  与paired implicit constructor验收分开。dropping args/单数组参数/
  错误curBp初始化三项有效JS mutation均检出。
- 开发途中对native super父类读取时机判断错误。Node独立probe证明range/
  spread都在参数求值后读父类，撤销额外snapshot限制和错误拒绝断言，
  相关evaluation文件全部回到已提交HEAD；rules128真实初始化修复保留。
  getter现是普通正例。初次paired测试缺LocalStorage宿主，补显式shim后
  通过。一次final-build因错误target名退出2，改为实际注册目标后正常。
  final-tests-1已人为中断以撤销上述错误限制，不作最终通过证据。
- 最终build-5单-j4四十四目标退出0。final-tests-2定向92/92通过664.91秒，
  不是暂停的全套。fresh language-constructors-{default,language,numeric,
  constructors,arithmetic-constructors,numeric-constructors}-pass-1的30次
  固定五ABC导出全部退出0；各模式106份源码解析，输入SHA/size与既有
  corpus index匹配，零legacy/source failure/complete回退。前三模式分别
  80complete/26incomplete-source、95/11、96/10，全部源码字节与rules128
  一致；constructor-only88/18，arithmetic+constructors105/1，numeric+
  constructors106/0，各新模式只有十个原构造器record源码变化。单构造
  模式只有八份record新增complete：Multi record40957和pasteboard4767
  的构造器恢复后仍有原pinned数值模式下的其他方法缺口；数值组合分别
  解决这些义务，不能把“十构造器恢复”误报为各模式十record完整。
- 六模式均另有46no-module记录。只读其结构产物核对为11个_ES注解和
  35个@ohos/@system引用、零methods；record metadata fields不视作源码
  class fields。这些与106份模块源码分开统计，不伪造函数体或静态分类。
  numeric+constructors的106source_complete是引擎产物状态，不是阶段一
  整record独立语义、字段/签名及源表示验收已经闭合。
- 日志前缀/tmp/abcd-language-constructors-；结果以本节最终更新及各mode
  summary为准。新增结构步进O(operations+uses)，复用原bounded sparse
  solver/预算，无逐变量CFG walk、样本/名字/sidecar/layout/producer名单
  准入或新依赖。RSS/基准、全套、扩展语料、调度器及周期审计继续暂停。
- source_complete只表示引擎无已知缺口。paired implicit ctor的iterator
  污染、继承field setters、整record方法/宿主平台尚未验收；显式spread
  构造器和receiver snapshots仍有源表示/实例字段整合工作。全局变量、
  完整签名、函数体整合及全部record独立语义/风格门槛仍开放。Goal active，
  本组件完成不替代阶段一；后续优先这些实际功能和验收缺口。

### 2026-10-06：显式语言数值语义补齐 WindowUtil 未恢复方法

- 规则revision128，基线f699c84e；25aa原分支开始干净。遵循用户新优先级，
  本项处理实际INCOMPLETE/UNRECOVERED。用户本轮明确允许新增显式语言
  数值模式；`--value-source --language-numeric`选择language_numeric_v1。
  默认V3与旧language-arithmetic的二元范围保持原义，两selector互斥。
  详见[语言数值语义](language-numeric.md)。
- NEG/legacy NEG/INC/DEC复用共同scalar adapter、normal/phased sparse
  solver及linear lexicals，按真实Language输入提供Number/BigInt正常
  结果。既有更强事实继续保留，未解循环/private markers不获语言事实。
  linear pass先保存原输入，避免写acc后把新结果当原输入。旧版ToNumber/
  ToNumeric仍有分别的typed语义，前者拒绝BigInt、后者保留BigInt。
- 原Unary/Binary/Update AST、exclusive scratch与printer负责复合表达式
  和优先级，没有新writer、样本/源码/名称分派、opcode sequence或布局
  匹配。legacy forest与fresh operand checker消费同一typed数值前提，
  两旧模式的minimum-tag义务保留。已接受NaN literal的原AST bits与来源
  不改，显式numeric模式下tree结构/打印边界允许native NaN；private
  编码仍拒绝。该契约排除payload/tag/C++ UB观察，不是Ark二进制测量证明。
- 新三profile十种modern/deprecated numeric编码、七形态、普通/异常
  形成420个完整函数，Node独立353,220场景检查NaN、signed zero、
  BigInt/Symbol、coercion getter/receiver、求值顺序及原异常。额外三
  profile十编码两flow的200/201/402共180个完整组合检查增长小于3倍。
  copies/拆块、operand/result composition、两NaN literal及旧契约/
  private输入/legacy witness篡改覆盖；INCOMPLETE/refusal不算正例。
- window_numeric_source_test先完成ABC-only生成，再读取配对TypeScript。
  record7618 WindowUtil现source_complete=true，getCalDegree原PC130 NEG
  缺口得到完整native -x、atan2、乘除及条件取模。17,880场景覆盖边界、
  反复变化的转换、Math/Number读取和调用/receiver/原异常；三个numeric
  mutation被检测。测试不构造类，仅隔离完整方法；identity Observed和
  Node decorator适配不验收宿主协议、整个record行为或HarmonyOS平台。
- 扩展numeric回归暴露原临时初始化可随后一参数更新下沉，导致前一参数
  读取undefined。共同snapshot planner按inline forest真实eager use索引，
  在provisional父赋值形成后核对原绑定初始化。失败的赋值和其受影响
  输入依赖用bounded queue保留原Root；fresh checker独立重建availability。
  原值/更新值/callback/异常回归扩为108个三profile普通/异常/copy201/
  拆块函数，Node1,424场景与三个mutation通过；晚赋值篡改拒绝。没有
  INC/call名单、固定参数数或新same-block条件；既有跨块snapshot准入
  仍是明确实现缺口，未把它定义为语言不变量。
- 首轮未注册target请求退出2，configure后正常构建；测试途中修正旧版
  ToNumeric oracle、两次运行对象引用的对照、receipt新label映射与NaN
  tree结构边界。numeric_update行为失败推动上述通用初始化修复。
  final-build-1因测试变量重命名漏项退出2；修正后final-build-2单-j4
  三十五目标退出0。最终相关定向80/80通过632.41秒；不是暂停的全套。
- fresh language-numeric-{default,language,numeric}-pass-1各五ABC退出0，
  各106份源码解析、输入SHA/size与现有corpus index匹配，零legacy/
  source failure/complete回退。default80complete/26incomplete-source/
  46no-module；language95/11/46，二者全部源码字节与rules127保持。
  numeric96/10/46，相对旧language仅record7618新增complete/源码变化。
  这些是引擎输出状态，不是整record独立验收结果。
- 日志前缀/tmp/abcd-language-numeric-；final-build-2、final-tests-1和
  final-batch-1保存最终证据，mode summary-1.json保存五ABC差异。
  数值transfer每nodeO(1)；snapshot索引/依赖保留O(O+U) work/storage，
  无逐变量CFG walk、全图循环retry或新依赖。RSS/基准/代理/调度器与
  周期审计仍暂停，不宣称1GB实测。剩余十个普通SuperSpread构造器
  的未知父品牌/私有数组义务未放宽；实例字段/全局/完整签名/函数体整合
  及所有record独立语义/风格验收继续开放。Goal保持active；后续先这些
  实际未恢复功能，再做?.等细节。

### 2026-10-06：共同空值解释贯通原生调用；后续优先未恢复功能

- 规则revision127，基线3653283e；25aa原分支起始干净。用户要求本项
  `?.` 修复完成后，先解决实际 INCOMPLETE/UNRECOVERED 的反编译语义
  支持缺口，再优化 optional-chain 等细节；执行顺序已同步到 isa-priority
  和 function-tree-output。默认V3及显式language-arithmetic二元范围保持。
- 既有 null/undefined/other 抽象解释复用原 reference/assignment formation，
  识别 nullish 返回 undefined、other 执行原 CallExpr 的语义。原成员
  base 身份和 CallReference 保留 this；值 callable 保留间接 eval。
  原参数/计算 key 在原非 nullish 路径按序执行；不同 receiver、缺少
  guard/fallback 效果及条件外调用不能被错误扩大短路。共同 AST/CFG/
  effects/exception/semantics 检查未改，无第二writer或样本/形状匹配。
- OptionalCallSyntax 由共同 printer 发射原生 `base?.method(args)` 或
  `callee?.(args)`。call/member/key/argument 事件保持实际区间，省略
  guard/被形成表达式替代的纯 base 在实际 base.end 零宽定位。文件族
  emission 和 positions serializer 复用同一计划与 typed optional_call。
- 新真实解码回归先在 count=1 成员调用的源码风格断言 RED。18种形态、
  三profile、1/200/201/402 guard/copy/拆块组合得到216个完整函数；
  reversed operands、NOT、计算 key、参数 getter、原 producer snapshot、
  不同 receiver 和条件外调用均覆盖。原 tree semantics 和 instruction
  positions 检查通过，201→402 的计划工作比小于3；Node 48,384 场景
  对照原异常 token、调用/读取次数及 this，另12项 intrinsic eval 检查
  不暴露生成函数的局部变量。refusal/INCOMPLETE不计为完整正例。
- 新 window_optional_call_source_test 先完成 ABC-only 生成，再独立
  读取配对 TypeScript。三个完整方法 updateAvoidPadding/setSplitScreen/
  cancelSplitScreen 的4,536场景检查条件短路、计算参数、getter/数值
  转换、宿主调用/receiver、catch/回调、状态、padding描述符及原异常
  token；取消短路、重复uiContext读取、改this三mutation检测通过。
  测试宿主提供identity Observed，Node参考转换移除未实例化WindowInfo
  的ArkTS decorator；不验收该宿主协议、构造器、整个record或真实平台。
  内置TypeError仅对照类型，不宣称其平台相关消息字串一致。
- 首次paired测试缺Observed宿主，第二次Node不能解析decorator；明确
  identity宿主桥后第三次通过。final-build-1含未注册tree_printer_test
  请求而退出2；核对实际注册后，final-build-2单-j4二十二目标退出0。
  最终相关定向58/58通过304.84秒；不是暂停的全套测试。
- fresh optional-call-{default,language}-pass-1各五ABC退出0，各106份
  源码解析、输入SHA/size与现有corpus index匹配，零legacy/新增failure/
  complete回退。两模式实际改变WindowUtil(7618)和pasteboard record9550
  的源码：default共三个、language共七个 native optional 调用；其中
  WindowUtil为两/六个，record9550一处模块初始化getHostContext。
  default仍80complete/26incomplete-source/46no-module，language95/11/46；
  complete数量未增加，不以本项表示改进代替剩余record独立验收。
- 日志前缀/tmp/abcd-optional-call-。新增候选检查每节点O(1)，复用
  原arena队列和文本索引，无新逐候选图遍历或依赖；RSS/基准/代理/
  调度器及周期审计仍暂停。实际普通SuperSpread未知父品牌/私有数组、
  WindowUtil NEG、实例字段/全局/完整签名/函数体整合等未恢复义务
  继续开放。下一轮优先这些实际INCOMPLETE/UNRECOVERED，阶段一Goal
  保持active，未取得用户切换默认数值验收的指示。

### 2026-10-05：多次观察的条件结果采用原位置绑定

- 规则revision126，基线a878d048；继续25aa原分支，开始时干净。
  上一轮完成125实际提交，本轮针对实际表示缺口实施。默认V3及显式
  language-arithmetic二元范围保持；全套/RSS/基准/代理/调度器继续暂停。
- 未融合的闭合条件族复用原SelectionRegion/phi，在原分支生成一次
  assignment，后续观察读取同一绑定。single eager use只是使用点融合
  的义务，不再是条件表达式唯一表示；融合仍检查其最终execution/catch。
  原位置candidate共用root-use、EffectOrder、真实phi叶及external use检查。
  根predicate snapshot原位保留，child prefix留在原selected arm。
- 源码控制绑定成功后才授予prefix归属，child闭合及parent pass移除缺失
  owner的整个族。早期试验把prefix放入EvaluationPlan，跨try-wrapper
  失败会提前吞掉getter；已改为source-control阶段归属，未绑定时保留原
  if。builder和fresh flow独立重建regions，已有ExpressionScopes按原
  try layout核对ordered phi/prefix实际protected/handler范围。
- Phi输入索引复用已验证flat aliases，排除纯transport观察，保留真实
  UseId、原availability/全路径必达/异常图。字符串等生产者通过同一
  DescribeEvaluation和索引；普通phi边赋值也解析同一canonical生产者，
  精确表达式对应仍核对，没有删除origin、receiver或catch检查。
  共同conditional/sequence/assignment AST及printer复用；无新writer、
  样本/名称/PC/register/指令模板/producer名单或固定层数准入。
- 三profile解码测试覆盖多次观察的字符串、原输入alias、1/201复制及
  拆块、2/3/200/201/402层；201→402 scoped work小于3倍。交换条件臂
  fresh semantics拒绝；DEBUGGER后保留原控制和所有操作，getter值变化、
  次数/receiver、原异常身份、catch差异、prototype/own descriptors/fresh
  identity由实际生成源码7080场景独立执行，不以refusal/INCOMPLETE计正例。
- final-build-2单-j4十七目标退出0。首次final-tests-1定向44项为43通过/
  1失败（VoIP），275.86秒；fixed-five初次default到pasteboard出现两个
  tree_stage退化。实际定位为普通边赋值仍查看raw alias生产者；共同
  canonical来源对应修复后，repair-tests-1三项通过7.45秒，Home record
  恢复complete。最终final-tests-2定向44/44通过272.54秒。
- 新pasteboard_breakpoint_source_test先生成ABC-only源码，再独立加载
  配对TypeScript；2896场景核对600/840阈值、密度、数值/类型转换边界、
  curBp getter/setter、storage/Logger调用receiver、状态及异常顺序。
  阈值、结果及重复display读取三种mutation被检测。风格要求一个原生
  条件赋值、两个条件表达式及原最终state if，不以能运行代替表示验收。
  普通构造函数/整个record/HarmonyOS平台仍未通过独立整体验收。
- fresh selection-binding-{default,language}-pass-2各五ABC退出0。默认
  80complete/26incomplete-source/0failure/46no-module，language95/11/0/46；
  无complete新增/退化或legacy writer，各106份源码解析及输入SHA/size与
  index核验通过。默认源码变化5521/8383及5980/11989；language另外
  改变40957/4767。两个实际EntryAbility的阈值臂由三次phi写缩为一次
  原生conditional绑定；其他四record消除部分phi生产者机械中转。
  完成数量未增加；这些局部行为/表示证据不代替所有record独立验收。
- 日志前缀/tmp/abcd-selection-binding-。新增closure/index/child queue
  O(O+S)，alias census O(U+P)，加既有discovery/prefix/effect/支配/flow
  成本；无逐phi CFG遍历、递归owner walk或新依赖。不宣称暂停的RSS实测。
  general phi bundles、带pre-throw staging的原位置表达式、reconvergent
  tails、general staged arrays、SuperSpread、WindowUtil NEG、实例字段/
  全局/完整签名/函数体整合及全部record独立语义和风格验收仍开放。
  阶段一Goal保持active，继续真实未完成记录，不以本次提交代替完成。

### 2026-10-05：phased 共享 phi 的嵌套控制臂贯通对象初始化

- 规则revision125，基线05a6ee0d；继续25aa原分支和既有实现。上一轮
  解释数值边界，取得885个nested-arm独立行为证据；本轮完成实现、
  扩展验证和固定五ABC复核。默认V3与显式language-arithmetic的二元
  范围保持，用户未授权改变验收。全套/RSS/基准/代理/调度器仍暂停。
- 新NestedPhaseSelections在原normal CFG后序中索引闭合路径，保留
  异常图和实际phi输入EdgeId。SelectionChild/partial SelectionRegion
  复用同一原phi，不伪造中间值；叶边及child的唯一表达式父归属使计数
  对应不重叠的真实覆盖。完整root才消费原eager use，子臂没有fake input。
  既有flat/shared-scalar发现保留兼容，不通过新增局部模板扩大其边界。
- PhaseSelectionOperand增加typed control child，prefix/catch/identity/
  execution义务共用检查；整个group的child成功后才向依赖授予执行owner。
  fresh checker独立重建record及phase_children，实际syntax parent、原
  phi及最终throw owner分别保留。原inline/module roots抽取共享functional
  forest，移除OpId大小推断，迭代拒绝cycle。公共forest/ObjectExpr/printer
  组合完整嵌套对象，没有新writer、AST或证明/发布包装。
- phased Commit操作范围为空，TreeBuilder改用已有control adapter取得
  真实child分支并保留一次placement。宽跳转变体另外暴露scalar identity
  来源校验缺口：combined origin可属于共同flow已检查的condition节点，
  而非整个ConditionalExpr根；来源成员独立核对不变，篡改仍拒绝。
- 三profile真实解码覆盖1/201复制、寄存器alias/拆块，以及2/3/200/201/402
  层共享phi控制臂；201→402 scoped work小于3倍。清除child归属、input/
  execution/owner篡改、交换条件臂及损坏combined origin均拒绝。getter后
  DEBUGGER保留原if和操作，跨catch保留第一family在try外、第二在try内。
  正例是完整生成源码；typed refusal和INCOMPLETE不计正例。
- final-build-1单-j4十五目标退出0；final-tests-1定向41/41通过265.95秒。
  新Node oracle实际4425场景通过，覆盖普通/alias/四层/DEBUGGER/不同catch，
  变化getter值、次数/receiver、原异常身份、原型、own descriptors、fresh
  identity以及不调用coercion hook。包含pasteboard已恢复callback、VoIP及
  语言算术的独立配对测试；这些局部证据不代替整record或平台验收。
- fresh arm-selection-{default,language}-pass-1各五ABC退出0，默认80complete/
  26incomplete-source/0source-failure/46no-module，language95/11/0/46。
  无complete新增/退化、legacy writer或任何源码字节变化；各106份源码
  解析、输入SHA/size与corpus index核验通过。本轮因此只扩展通用能力，
  没有减少实际五ABC的剩余缺口。日志前缀/tmp/abcd-arm-selection-。
- 路径索引新增O(B+E+O+V+U+S) work/storage，加共同prefix/effect/支配
  成本；分组O(S log S)，child queue逐region一次。未恢复暂停的RSS/基准，
  不宣称内存实测达标。现有单表达式phi edge bundle、predicate表示、多次
  观察的phi materialization、共享reconvergent tails、general staged arrays
  及旧member/joint corridor仍有缺口，不能当成语言不变量。
- 下一步回到真实剩余函数及表示：多次观察的合流值、SuperSpread、
  WindowUtil NEG、实例字段/全局/完整签名/函数体整合，以及全部record的
  独立语义/风格验收。阶段一Goal保持active，不以本次提交代替完成。

### 2026-10-05：嵌套 phased selection 贯通最终执行归属

- 规则revision124，基线bbd9e959；继续25aa原分支，开始时干净。上一轮
  解释算术边界并核对证据，没有开发交付；本轮实际实现、验证并准备提交。
  用户未改变默认数值验收，默认V3和显式language-arithmetic二元范围保持。
  固定五ABC、配对源码独立验收及暂停的全套/RSS/代理等范围不变。
- PhaseSelectionOperand分别保留真实syntax consumer/input及execution owner。
  纯comparison没有throw dispatch，不能作为inner getter的最终catch owner。
  现按实际consumer/operand order分组，由原inline source roots连接外层
  selection，bounded queue从外向内检查；成功parent才传递执行归属，cycle
  或失败parent不授予owner。普通inline subtree复用已经检查的source root。
- selected arms/predicate prefixes使用最终owner的真实catch/staging签名。
  原phi/transport、canonical use、Reference/receiver、条件执行及interval
  义务保持。fresh verifier独立重算execution并拒绝篡改；共同forest/
  ObjectExpr/printer直接组合嵌套条件与对象，无新writer/AST/证明包装。
  无optional-chain字节码模板、样本/名称分派、PC/register布局、producer
  名单或固定selection数量条件。旧candidate/member/joint corridor缺口保留。
- 分组O(S log S)，dependency queue额外O(O+S) work/storage，各group及
  dependency处理一次，加既有discovery/catch/interval成本；无新逐变量
  CFG遍历或递归owner求解。三profile真实解码覆盖2/3/200/201/402层依赖、
  copies/alias及拆块，201→402 scoped work小于3倍；不宣称RSS实测。
  DEBUGGER阻挡inner移动，不同catch范围保留第一个getter未捕获、第二次
  getter原catch；execution/input/owner篡改仍拒绝。正例均为完整生成源码。
- 旧实现新whole-object断言在red-source-1.log退出134，red-build-1退出0。
  首轮build-1输出完整嵌套表达式；build-2三目标退出0，tests-1定向4/4
  通过79.21秒。最终final-build-1单-j4十五目标退出0，final-build-2为
  单元夹具排版后target重建，退出0；新增oracle实际3140个JS组合通过，
  包括getter次数/receiver、原抛出值、scope、typeof、四层组合和own属性。
- final-tests-1定向38项中37通过，220.67秒；VoIP新JSON宿主只提供parse，
  遗漏callback使用的stringify而失败。补齐测试宿主，未改生产/风格断言，
  voip-repair-1重跑1/1通过0.88秒。其余37项没有重复执行：合并覆盖38项
  均通过，不能把首次run写成38/38。VoIP独立配对322场景及768受控schedule
  通过，包含22个getter-backed parse结果及原异常；三种semantic mutation
  仍检测。行为验收不代表HarmonyOS平台或整个阶段一已完成。
- fresh selection-predicate-{default,language}-pass-1各五ABC退出0，默认
  80complete/26incomplete-source/0source-failure/46no-module，language
  95/11/0/46。均无complete新增/退化或legacy writer。各106份源码解析及
  input SHA/size与index核验通过；仅push-kit modules entity11989（VoIP）
  改变，其他源码字节保持。其optional scene phi/比较和enum选择现在全部
  进入原生对象初始化，机械逐字段写和中转phi消除；配对风格断言升级为
  完整字面量、原字段顺序及positive predicate，不以可运行代替表示质量。
- 日志前缀/tmp/abcd-selection-predicate-。下一步继续实际未完成函数及
  控制组合：nested-arm selections仍受旧discovery表示限制，general staged
  arrays、实例字段/全局/完整签名/函数体整合、SuperSpread、WindowUtil NEG
  及所有record的独立语义/风格验收仍开放。阶段一Goal保持active。

### 2026-10-05：phased 条件前缀采用共同依赖与必达证明

- 规则revision123，基线f489b16a；上一轮为实际提交，本轮继续25aa
  工作树及原分支，开始时干净。固定五ABC、独立配对源码和默认V3/
  显式language-arithmetic范围保持，没有恢复暂停的全套、RSS或代理。
- 新PhasePredicatePrefixes消费原source roots及真实canonical uses；
  reverse-use Kahn按所有外部consumer的一致terminal owner求归属，
  owner内部多次使用不被当作多次执行。竞争owner、未解cycle、phi-edge
  观察与callee acquisition保留原归属。eager expression能力来自共同
  DescribeEvaluation，suspension/constructor-frame等协议仍有原owner。
- 共同EffectOrder接受typed execution anchors，即使目标decision纯净也
  能证明到达/执行次数。各prefix的exact predecessor与must-reach
  successor构成全normal路径完整顺序；原完整CFG成功Commit支配关系
  证明可用性，原catch/Reference/input检查继续拥有异常及receiver。
  这不是新增语言效果，也不增加same-block、single-entry corridor、
  相邻、opcode序列、producer名单或选择数量准入条件。
- 原phase selection、prefix sequence/snapshot、ObjectExpr与TreeFlow
  共同组合对象条件RHS。raw VM predicate可能重复读取一个值，必要
  snapshot确保getter仍只执行一次。pure-only materialization保留原
  绑定位置，避免为搬入条件额外生成赋值序列；该绑定消除仍是缺口。
- 依赖索引O(O+U)，分组prefix排序O(P log P)，检查O(P+U)，另加
  原EffectOrder/支配与catch-copy成本；原预算计入容量/work，无逐条件
  CFG遍历、新依赖、private final IR或证明/发布包装。原有member prefix
  和joint suffix corridor能力限制仍是明确实现缺口，没有增加局部模式。
- 旧实现新增getter predicate完整对象断言在red-source-1.log退出134。
  初轮5/5通过52.91秒；新增独立oracle后tests-2.log 9/9通过62.44秒。
  第一次扩大37项有两项失败，final-tests-1.log：循环反例被既有literal
  路径拒绝，纯快照搬入条件使VoIP表示退化。未放宽其风格断言；保留
  pure-only原位置后配对VoIP恢复，voip-repair-1.log 1/1通过2.70秒。
  循环反例独立验证decoded图的anchored must-reach确实不承诺到达，
  并保留既有UnsupportedOperation；它不是完整函数恢复的正例。
- 三profile真实解码覆盖register aliases/copies/拆块、1/200/201/402，
  getter后的DEBUGGER、提前return、纯循环和prefix ownership篡改。
  正例输出完整原生对象；769个独立Node组合检查两次getter重读、变化
  的返回值、第一/第二次抛出原值、raw predicate、fresh identity及own
  descriptors。201→402 work小于3倍，不据此声称RSS或性能实测达标。
- 最终final-build-2单-j4十五目标退出0；final-tests-2.log定向37/37
  通过393.79秒，包含Reference、operator precedence、normal/phased
  literal、selection、closed fields、module default与VoIP/ProgressBar
  配对行为。日志前缀/tmp/abcd-predicate-prefix-；未重启全量测试。
- fresh predicate-prefix-{default,language}-pass-2各五ABC退出0，默认
  80complete/26incomplete-source/0source-failure/46no-module，language
  95/11/0/46。两模式均无complete新增/退化或legacy writer；各106份
  源码解析、输入SHA/size与index核验通过，字节与规则122完全一致。
  因此本轮通用能力扩展尚未减少实际五ABC的剩余缺口。
- 下一步为selection依赖组合：真实VoIP optional scene读取先形成phi，
  再经纯comparison进入外层条件；不能把没有throw dispatch的中间比较
  当作最终异常owner。需让共同合流/操作数归属连接最终字段consumer，
  不能为optional-chain编译序列加模板。general staged arrays、旧nested/
  corridor、实例字段/全局/完整签名/函数体整合、SuperSpread、WindowUtil
  NEG及全部record独立语义/风格验收仍开放，Goal保持active。

### 2026-10-05：phased 已归属操作数组合整个原生对象

- 规则revision122，基线940e62bb；继续25aa工作树及原分支。上一轮
  已提交实际交付，本轮完成已有phased对象操作数实现。用户要求详细
  解释算术边界，未授权修改默认验收；默认V3及显式language-arithmetic
  二元算术模式保持。固定五ABC及独立配对源码边界保持。
- 复用logical_objects、initializer placement及共同ObjectExpr/TreeFlow，
  保留原allocation root/snapshot和独立LiteralPlan的身份、完整own定义、
  无提前观察义务。已通过共同phase expression/selection证明的RHS可
  组合整个staged对象；无新writer、private final IR或证明/发布包装。
- source ownership以typed functional forest表示，迭代解析inline/
  phi arm/native nested/module guard父关系并拒绝cycle。共同EffectOrder
  按已归属source consumer投影效果，exact predecessor及must-reach
  successor证明全normal路径完整写链；成功后不追加同块、相邻、PC/
  opcode序列、producer名单或selection数量条件。
- 原始完整CFG支配关系及一次canonical use census证明external leaf
  在allocation可用；pure late snapshot不因纯净或Language就被提前。
  throw/catch签名及catch copies保留，非throwing allocation的后续catch
  staging仍是表示缺口。fresh verifier重建融合前consumer、重新计算
  对象能力并检查原RHS调度，不能仅凭效果投影证明表达式执行正确。
- forest及census增量O(O+U) work/storage，加原共同EffectOrder/异常
  签名求解成本；容量及work计入同一budget，无逐对象CFG遍历。三profile
  解码conditional/getter/member-call/TYPEOF，1/2/200/201/402 copies及
  definitions、register alias，以及barrier/late snapshot/篡改负例。
  独立Node83场景检查两次重新读取callee、正确receiver、前序效果和
  原抛出值，computed __proto__保持own descriptor/fresh identity。
- 最初旧实现red-source-1.log在新增whole-object断言退出134。新实现
  首轮8/8通过48.29秒，tests-2.log；追加alias、late leaf、owner篡改及
  重复call后9/9通过52.79秒，tests-4.log。final-build-1单-j4十五目标
  退出0；final-tests-1.log定向36/36通过360.08秒，含Reference、
  normal/phased literal、selection、closed fields、module default与
  独立VoIP/ProgressBar行为。日志前缀/tmp/abcd-phased-object-operands-。
- fresh phased-object-operands-{default,language}-pass-1各五ABC退出0；
  默认80complete/26incomplete-source/0source-failure/46no-module，
  language95/11/0/46，均无complete退化/新增或legacy writer。两模式
  各106份源码解析、输入SHA/size与index核验通过，源码字节与规则121
  完全一致。本轮能力扩展未减少固定五ABC的剩余缺口。
- effectful predicate prefix尚无完整source ownership：真实VoIP的
  optional scene读取仍是单独prefix，因此整个对象仍staged。general
  staged array及旧nested/corridor、实例字段/全局/完整签名/函数体整合、
  SuperSpread、WindowUtil NEG和所有record独立语义/风格验收仍未闭合。
  Goal保持active；全套、扩展语料、RSS/基准、代理、调度器/DFX与
  定期审视仍暂停，组件通过不能替代阶段一完成。

### 2026-10-05：phased 字面量复用共同必达效果链

- 规则revision121，基线c84048b2；继续25aa工作树及原分支。上一轮只
  解释算术边界、核对现状，没有开发交付；本轮恢复已有未提交实现。
  用户尚未选择修改默认算术验收标准，默认V3与显式language-arithmetic
  模式保持。固定五ABC、输入及独立配对源码边界保持。
- EffectOrder现接受normal/phased SSA；效果历史与下一必达事件仅沿
  normal edges，成功Commit后的可用性仍由原始完整CFG支配关系证明。
  method/catch entry独立开始历史，即使有正常回边也不靠入度猜入口；
  typed ValueReturn终止源码执行，不被其合成无值Commit遮蔽。
- LiteralPath两种模式共同索引所有字段/数组写入；RHS已在allocation
  可用时，exact predecessor与must-reach链证明单次完整初始化，允许
  纯分支/汇合、复制与拆块。共同证明成功后不追加same-block、相邻或
  fallthrough corridor条件。删除Before/After中的不可达旧布局fallback；
  原def-use、catch-copy、异常签名与表达式归属义务保留。
- 原迭代Kosaraju/稀疏必达求解复用，增量O(B+E+O) work/storage，容量
  计入原budget，没有逐字段CFG遍历或新证明/发布包装。三profile真实
  解码1/2/200/201/402复制、拆块及对象/数组字段规格，完整exception
  tree/fresh语义检查通过，201→402 evaluation work小于3倍。
  绕过写入、getter barrier、可能不终止的纯循环和catch读取部分对象
  都未获得融合能力；各write的Commit不能支配自身抛出时的catch。
- 两种生成源码使用原生ObjectExpr/ArrayExpr；独立Node各19个场景
  检查输入identity、fresh identity、prototype、own keys及descriptors。
  computed __proto__保持own属性语义；纯控制不读取Proxy属性。
  数组初轮tree失败来自夹具遗漏数字NameId，按已有constant boundary
  补齐后通过，没有改变生产forest表示。
- 最初旧实现red-tests-1.log 1/1退出8，1.03秒；新完整树/Node三项
  tests-5.log 3/3通过45.43秒。最终final-build-1单-j4十二目标与
  final-build-2追加三个共同消费者均退出0；final-tests-1.log定向
  35/35通过356.67秒，含新循环反例、normal/phased literal、Reference、
  selection、closed class fields、module default与配对VoIP/ProgressBar。
  日志共同前缀/tmp/abcd-phased-effect-order-。
- fresh phased-effect-order-{default,language}-pass-1各五ABC退出0；
  默认80complete/26incomplete-source/0source-failure/46no-module，
  language95/11/0/46，均无complete退化/新增或legacy writer。两模式
  各106份源码解析、输入SHA/size与index核验通过，源码字节与规则120
  完全一致。此实现扩大通用能力，但尚未减少固定五ABC的剩余缺口。
- phased late/conditional RHS的owned-expression、nested corridor及
  整个staged-to-native对象仍待贯通；pure空分支仍可保留在literal外，
  未宣称源码风格全体验收。下一步需将已证明的条件操作数归属与整个
  initializer效果区间组合，不能为它追加样本或局部图形匹配。
  实例字段/全局/完整签名/函数体整合、SuperSpread、WindowUtil NEG及
  所有record独立语义/风格验收仍未闭合，Goal保持active。全套、扩展
  语料、RSS/基准、代理、调度器/DFX与定期审视仍暂停。

### 2026-10-05：共同条件操作数贯通字段及标量表达式

- 规则revision120，基线d88a8e34；25aa工作树/分支保持，起始无未提交
  修改。上一轮解释算术边界，没有实施变化；本轮推进现有源码表示缺口，
  默认V3和显式language-arithmetic合同均保持原义。
- PhaseSelectionOperand替换成员argument专属记录；typed owner区分直接
  eager消费方和真实成员Reference，消费能力/输入顺序/重复读取与key转换
  边界来自DescribeEvaluation。共同canonical use census、条件区域、
  arm ownership、catch签名及transport/fresh verification继续拥有归属，
  不新增字段opcode名单、same-block、样本或选择数量匹配。实现文件迁至
  phase_selection_operands.cc，全部源码/测试消费方同步迁移。
- 条件结果可直接进入字段RHS、TYPEOF以及其他已支持eager表达式。
  AST/控制验证仍消费同一ConditionalExpr/LogicalExpr；callee/receiver/
  ancestor Reference保持原义。join-to-consumer Inert检查复用原normal
  interval效果索引，O(1)查询，替换逐选择路径遍历；既有census/index为
  线性，selection排序O(S log S)，容量继续计入原预算，不新增证明/发布包装。
- 新三profile真实字节码覆盖1/2/200/201/402复制、拆块和字段数量，
  原条件/getter/member-call arms都能直接恢复字段表达式，TYPEOF组合
  同样走共用消费能力；201→402 evaluation work小于3倍。共享结果仍
  snapshot，中间getter仍在已选值之后、字段定义之前执行；其原始抛出值
  保持。伪造input/owner仍拒绝，完整exception tree与独立Node检查通过。
- 新LogicalValue质量断言先在旧实现失败：red-tests-1.log 1/1退出8，
  5.23秒。中间9/9通过47.62秒，tests-2.log；新增TYPEOF 2/2通过
  36.64秒，tests-3.log。最终build-1发现两份既有integration测试尚使用
  旧记录字段，迁移后final-build-2单-j4十二目标退出0，无兼容别名。
  最终定向28/28通过319.62秒，final-tests-1.log，含联合条件参数、
  selection module、ordinary/phased literal、Reference、优先级及配对行为。
  日志共同前缀/tmp/abcd-selection-operands-。
- 真实VoIP voipCallType字段直接承接原条件枚举读，配对300场景、768个
  Promise schedules和三项语义篡改通过。ProgressBar条件路径直接参与
  文件名拼接，另单项配对246个引用/效果/异常/async场景通过1.55秒，
  progressbar-tests-1.log。两项风格断言对规则119输出失败、120通过，
  style-check-1.json。pasteboard另一个完整record8383也减少predicate
  临时量；本轮未新增该record的全函数独立行为验收，不能据此宣布record验收。
- fresh selection-operands-{default,language}-pass-1各五ABC退出0。默认
  仍80complete/26incomplete-source/0source-failure/46no-module，language
  仍95complete/11incomplete-source/0source-failure/46no-module；无新增
  complete、回退或legacy。两模式各三份complete源码变化，language另有
  一份incomplete-source变化；各106份源码解析通过，输入SHA/size与index
  一致。输入ABC及sidecar未改，引擎未读取配对源码。
- 更多有序兄弟操作数组合、phased staged-to-native整体对象、现有producer
  能力/候选corridor限制仍是实现缺口。实例字段/全局/完整签名/函数体整合、
  SuperSpread、WindowUtil NEG及所有record独立语义/风格验收尚未完成，
  Goal保持active。全套、扩展语料、RSS/基准、代理、调度/DFX和定期审视
  仍暂停，定向通过不能代替这些范围或阶段一完成证明。

### 2026-10-05：分阶段字段写入复用共同RHS调度

- 规则revision119，基线e72d7eb7；上一轮为已提交的实际进展。普通及
  phased staged定义仍是语句根，RHS不再被一律固定为snapshot；原生
  literal RHS保持allocation-owned区间。builder/fresh verifier重建
  同一归属区别，Reference/key转换、独占使用、效果顺序和异常检查保留。
- PhaseCorridors消费能力取自共同DescribeEvaluation的eager/source
  事实，不再要求消费操作有result。resultless consumer是最终执行
  owner，缺少匹配catch证据时立即保留snapshot，避免一次失败的外层
  融合破坏已有callee/receiver内部归属。复用原catch-copy签名和稀疏
  调度/重分区，不新增图索引、额外存储、样本分派或发布/证明包装。
- 新三profile真实解码fixture在已有选中字段后接getter和字段写入，
  1/2/200/201/402复制规格恢复`object.field = argument.value`；201→402
  literal work小于3倍。debugger barrier及两个消费者仍保留snapshot，
  consumer篡改仍拒绝。完整exception tree/fresh verify和独立Node
  oracle覆盖getter次数/顺序、原始抛出值、fresh identity、prototype及
  own __proto__ descriptor。整个对象的分阶段重建未因此完成。
- 最初直线fixture已由旧LiteralPlan处理，不能计为新能力RED；随后改为
  真实staged写入。中间失败分别定位为旧RHS pin、测试新增字段的count
  预期及无catch证据外层融合。恢复e72d7eb7四份生产源文件独立构建后，
  修正fixture在baseline-red-source-1.log退出134；恢复当前实现后重点
  3/3通过36.18秒，tests-4.log。上述日志前缀均/tmp/abcd-phase-write-。
- 最终单-j4构建十一目标退出0，final-build-1.log；定向24/24通过175.44秒，
  final-tests-1.log，涵盖normal/phased literal/selection、Reference、
  evaluation、优先级及配对行为。VoIP真实枚举RHS风格断言对规则118
  源码失败、119通过；加入独立测试后voip-style-tests-1.log单项重跑通过，
  原300配对场景、768受控Promise schedules及三项语义篡改继续通过。
- fresh phase-write-{default,language}-pass-1各五ABC退出0；默认仍为
  80complete/26incomplete-source/0source-failure/46no-module，language仍为
  95complete/11incomplete-source/0source-failure/46no-module。两模式各12份
  源码变化，其中9份complete；各106份源码解析通过，输入SHA/size一致。
  无新增complete、降级或legacy。实际VoIP字段直接承接枚举getter，部分
  async尾部的纯undefined临时量经共同表达式/尾部处理省去；输入/sidecar未改。
- 整个phased staged-to-native、producer能力名单、现有corridor和ObjectRoute
  候选限制继续开放，均是实现缺口。实例字段/全局/完整签名/函数体整合、
  SuperSpread、WindowUtil NEG及每record独立语义/风格验收未全部完成，
  Goal保持active。全套、扩展语料、RSS/基准、代理、调度/DFX及定期审视
  仍暂停；本轮定向证据不代替这些范围或阶段一完成证明。

### 2026-10-05：条件对象初始化复用共同执行前缀和复制身份

- 规则revision118，基线60ab1368；25aa工作树和分支保持。上一轮解释
  数值验收边界，没有实现变化；本轮继续已有RED，不修改默认V3合同。
- 普通ObjectRoute原额外要求allocation.block等于首个selection header，
  即使共同Domain已包含其直线执行前缀。现移除此项重复布局准入；
  原allocation/definition唯一执行、效果顺序、异常和独占消费方程保留。
  对象使用一次按共同EvaluationAliases身份索引，跳过已证明的纯身份
  transport，实际间接观察仍计入。Domain producer及forest staged
  receiver解析同一root，原始指令operands/anchors保持；fresh Verify
  独立重建统计和效果方程。新增O(U+V)工作/暂存计入原TreeCheckBudget，
  没有逐对象alias/CFG遍历、样本分派或新证明/发布包装。
- 三profile实际字节码加入1/2/200/201/402复制与拆块前缀：对象分配
  在条件块之前仍恢复完整嵌套对象表达式；201→402 work小于3倍。
  同样前缀的debugger barrier和额外对象消费反例仍拒绝调用引用融合，
  既有伪造ownership/consumer验证保持。Node新增split layout，与
  原nested/deep/sibling共同通过5,600独立场景，覆盖getter/callee改写、
  receiver、对象身份/prototype/descriptor、原始异常和执行顺序。
- RED /tmp/abcd-object-prefix-red-tests-1.log记录原CallPlan失败；首次
  构建与两项验证退出0，tests-1为2/2通过9.48秒。最终单-j4构建十目标
  退出0，/tmp/abcd-object-prefix-final-build-1.log；定向18/18通过170.30秒，
  /tmp/abcd-object-prefix-final-tests-1.log，包括normal/phased selection、
  literal、Reference、优先级、来源及normal/comparison配对行为。
- fresh object-prefix-{default,language}-pass-1各五ABC导出退出0，分别
  80complete/26incomplete-source/0source-failure/46no-module及
  95complete/11incomplete-source/0source-failure/46no-module。无新增complete、
  降级或legacy writer；两模式各106份源码解析通过，全部源码字节与规则117
  相同，输入SHA/size与原index一致。详见/tmp/abcd-object-prefix-batch-1.log
  及两份summary-1.json；fixture扩展不冒充真实record新增完成证据。
- phased staged-to-native、非直线前缀及ObjectRoute物理字段/最终join候选
  限制继续开放，不能称为语言不变量。实例字段/全局/完整签名/函数体
  整合、未知父类SuperSpread、WindowUtil NEG及阶段一独立语义/风格验收
  仍未全部完成，Goal保持active。全套、扩展语料、RSS/基准、代理、
  调度/DFX及定期审视继续暂停。

### 2026-10-05：共同表达式打印归一化条件极性和等值互补

- 规则revision117，基线50208f29；工作树/分支保持，上一轮为实际进展。
  当前继续阶段一独立表示验收，不修改默认V3数值合同，不恢复暂缓任务。
  真实VoIP条件先以独立表示断言RED：反向`!(scene === "video") ? voice
  : video`。共同printer现消费原ConditionalExpr的NOT极性，奇数交换
  原分支，偶数保持；条件先执行一次，随后仅执行原应选择的分支。
- TreeEqualityComplement通过exhaustive typed enum定义==/!=、===/!==
  的语言互补；这些结果本来就是Boolean，正向多余转换也可省略。关系/
  membership保留NOT，尤其不把`!(a < b)`变成NaN域不等价的`a >= b`。
  实际输出运算符交给原grammar/precedence查询，保留指数、一元与嵌套
  等值的括号。无样本/名称、opcode序列、同块、producer或数量准入。
- 原FunctionTree/control/evaluation及instruction来源不变。交换分支的
  ExprId和实际位置保持；被省略NOT的range仅覆盖条件，并在问号/分支
  前结束。链式处理两次线性遍历，复用原print-action stack，无逐表达式
  CFG扫描、新证明包装、依赖或来源平台。语言依据及边界见
  [operator expressions](operator-expressions.md#conditional-polarity-and-equality-complements)。
- AST/printer隔离fixture覆盖10种比较、6个消费上下文以及1/2/200/201/
  402/804深度；独立Node通过538,560场景，含getter/Proxy、后续值修改、
  NaN、signed zero、BigInt/Symbol、转换顺序/异常、选中分支原值/对象身份。
  错误交换分支和重复条件读取两项有效JS篡改均检测。条件NOT的范围对
  实际leaf逐项核对；201→402/804 work增长保持线性范围，不冒充RSS。
  AST fixture不冒充真实ABC导入证据：后者沿用三个profile的150个实际
  比较/复制/拆块/反向/cell调用函数，134,400独立场景通过，callee修改、
  receiver及异常身份保留。实际类字段及配对记录也执行共同输出。
- VoIP真实record11989输出已为`scene === "video" ? video : voice`；
  原300配对场景、768受控Promise schedule及三项语义篡改继续通过。
  class_accessor独立风格断言原要求`!(typeof... === typeof...)`，实际
  新源码为原生!==，class-style-1观察RED后更新表示预期；原独立Boolean
  初始化oracle及descriptor/identity等行为检查保持，没有修改语义期望。
- 日志前缀/tmp/abcd-predicate-polarity-*。red-source-1与red-tests-1记录
  原样本/共同printer失败；red-build-1误拼UnaryOperator::kTypeOf，
  修正后red-build-2退出0。build-1构建四目标、final-build-1构建十目标，
  单-j4均退出0。首轮5/5通过31.13秒；最终定向19/19通过45.20秒，含
  原Boolean观察/选择、优先级/postfix、optional/nullish、来源、类字段、
  decoded comparison及配对comparison/VoIP。未运行全量套件。
- fresh predicate-polarity-{default,language}-pass-1各五输入退出0，分别
  80complete/26incomplete-source/0source-failure/46no-module及
  95complete/11incomplete-source/0source-failure/46no-module。无legacy、
  新增complete或complete降级。两模式各106份完整/部分源码解析通过，
  ABC SHA/size与原index一致；输入ABC/配对源码未改。相对规则116，默认
  33份源码变化（26份complete）、language 37份变化（35份complete），
  这是通用表示改进，不表示各record的完整独立语义/风格验收均已完成。
- staged literal机械写入、跨语句snapshot/条件链、未知父类SuperSpread、
  WindowUtil NEG输入、实例字段/全局/完整签名/函数体整合与阶段一record
  独立验收继续开放，Goal保持active。代理、全套、扩展语料、RSS/基准、
  调度/DFX及定期审视保持暂停。

### 2026-10-05：普通可用值初始化跨块采用共同全路径效果顺序

- 规则revision116，基线564c2b7d；25aa工作树和分支
  codex/resume-generator-post-loop保持。普通 literal 的同块/物理 OpId
  限制改由共同 EvaluationAliases、EffectOrder 与 ValueAvailableAt 承担。
  每次规划一次构建别名及效果分析，以 forward must-reach successor 和
  exact predecessor 身份证明每 allocation 的全路径单次字段写链。
  数量只限制遍历；值域、receiver/对象身份、own descriptor、效果/异常
  顺序和未完成对象不可观察义务保持，无样本/名称/局部序列分派。
- 字段采用共同 reverse-postorder 位置排序。normal/phased 共用 indexed
  canonical-use census；纯 copy 与同身份 phi 是 transport，真正观察
  必须在末次 definition 后。跨块 read 与异常 snapshot 不再因普通旧
  census 只看同块而遗漏。每 owned write 至多参与一个 allocation 链，
  每 allocation 至多一条失败边；加上既有 alias/effect 分析、字段排序，
  无逐对象 CFG 遍历。详见[literal initialization](literal-initialization.md)
  及[形状限制审计](shape-restrictions-audit.md)。
- 合成 IR 在1/200/201/402字段及反向物理块检查顺序和篡改拒绝；真实
  字节码另覆盖三profile、四布局、1/2/200/201/402字段，共60个完整
  函数。复制链与 diamond 数量随字段数增长；共同来源/树检查贯通。
  独立 Node 新720场景检查两次调用身份、原型、own key 顺序、每字段
  W/E/C/value及继承getter/setter与条件coercion零执行，原12,584场景
  保持。另15个实际解码 bypass/repeat/pure-cycle/read/barrier反例拒绝。
  两处旧测试误以纯unused copy作为观察，改成真实effectful read；纯
  copy改为合法transport正例，没有删除真正观察/共享效果RHS等负例。
- 日志前缀/tmp/abcd-ordered-literal-*。red-test实际在跨块BuildLiteralPlan
  拒绝处失败。早期测试编译的narrowing、ID类型及不存在的GetInfo API
  修正后成功。final-build-1单-j4构建14目标退出0；final-tests-1定向
  19/19通过89.20秒，包括literal/callee/construct/类初始化/来源与VoIP。
  最后的等价if表达及注释整理后final-build-2重新构建五目标完成。
  final-tests-2重验literal_plan/literal_binary/source_route，3/3通过5.43秒。
  未运行全量套件、扩展语料、RSS/基准、调度/DFX；未授权默认数值政策变更。
- fresh ordered-literal-{default,language}-pass-1各五输入退出0，分别
  80complete/26incomplete-source/0source-failure/46no-module及
  95complete/11incomplete-source/0source-failure/46no-module。无legacy、
  新增complete或complete降级。当前组件未改变真实样本源码字节，不把
  能力扩展冒充样本表示或阶段一验收完成。两模式syntax-1各106份完整/
  部分源码解析通过，ABC的SHA/size与原index一致；输入ABC/配对源码未改。
- late-owned RHS 的普通local native interval、phased unique corridor、
  跨条件/分阶段初始化的原生对象表示仍是覆盖缺口。未知父类SuperSpread、
  WindowUtil NEG输入、实例字段/全局/完整签名/函数体整合与各record独立
  语义/风格验收仍开放，Goal保持active。代理和定期审视仍暂停。

### 2026-10-05：无初始化静态字段的原生声明与 VoIP 受控暂停验收

- 规则revision115，基线9498dcce；25aa工作树和
  codex/resume-generator-post-loop保持。上轮算术边界解释未改变默认V3，
  本轮不依赖数值政策变更。固定五ABC与Goal的完整阶段一范围保持。
- VoIP真实record11989/entry12208已完整生成，但静态字段仍显示
  `static callId = (void 0);`。独立表示检查先RED；共同FunctionTree
  printer现对FieldDefinitionExpr的UndefinedLiteral输出无初始化声明。
  原生字段仍DefineOwnProperty，值为undefined，具有writable/enumerable/
  configurable属性；不是省略字段、改成赋值或用读取结果推断undefined。
  RHS的来源保留为字段名后的零宽表达式事件，其他读取/转换RHS仍执行。
  无样本名、opcode序列、寄存器/PC、同块、生产者名单或选择数准入。
- tree_structure_test覆盖1/200/201/402字段、普通/计算名称、constructor/
  prototype等特殊键、零宽来源范围和保留非字面量RHS。真实VoIP的原300
  配对场景新增Function.prototype继承getter/setter反例、实际字段描述符、
  类及方法name/length和方法属性检查。引擎只读ABC；TS只进独立Node oracle。
- 复用voip_service_source_test新增768个受控Promise场景，检查await前
  仅发出首项请求、暂停期间不提前执行、恢复后重读共享callId、双请求
  正反释放顺序、callee先于参数getter保存、方法替换、首/后请求拒绝、
  catch中code/message读取与其抛出原对象身份。合法JS语义篡改三项：
  缓存错误字段值、丢失receiver、去掉首个await，均被独立oracle检测。
  这是受控宿主行为证据，不是HarmonyOS通话/UI实测，也不表示整个record
  的源码风格验收已完成；对象字段机械展开和条件链表示仍存在。
- 日志前缀/tmp/abcd-voip-fields-*。red.log记录表示失败。build-1已构建
  前三个target后因误写class_initializer_file目标名退出2；核对CMake并
  改为class_initializer_file_test，build-2退出0，合计七个target已构建。
  定向tests-1 6/6通过6.90秒，tests-2 3/3通过6.77秒，含共同树、类字段/
  静态类、来源路由与VoIP；未运行全套。默认syntax-1误传合同标签失败，
  核对manifest后syntax-2及language syntax-1均通过，未更改合同来通过检查。
- fresh voip-fields-{default,language}-pass-1各五输入退出0，分别
  80complete/26incomplete-source/0source-failure/46no-module与
  95complete/11incomplete-source/0source-failure/46no-module。无legacy、
  新增complete或complete降级；各106份源码解析、SHA/size对原index检查
  通过。两模式仅record11989源码字节变化；输入ABC与配对源码未修改。
- 当前字面量所有权支持native interval与staged buffered writes；跨条件
  初始化仍分配对象并保留逐项赋值。它是高层表示缺口，不能盲删interval/
  catch snapshot/own-property要求来合并。未知父类SuperSpread、WindowUtil
  NEG数值输入、实例字段/全局/完整签名/函数体整合及所有record独立验收
  仍开放，Goal保持active。代理、全套、扩展语料、RSS/基准、调度/DFX及
  定期审视保持暂停。

### 2026-10-05：共同封闭表达式贯通类字段初始化

- 规则revision114，基线e00cf12e；25aa工作树、分支
  codex/resume-generator-post-loop。上轮数值边界解释未授权改变默认政策；
  本轮沿用默认V3和显式language arithmetic，完成不依赖新数值政策的
  字段初始化与源码表示缺口。Goal仍active，固定五ABC范围保持。
- 实际三profile字节码增加typeof/严格比较/逻辑取反组合初始化，包含
  initializer的纯拆块、反向物理块和外层直线/分支/反向调用，以及ABI
  receiver或真实frame读取。旧实现RED，LLDB定位到ClassSelfOrder调用
  LiteralClassFields的String/Copy生产者名单；不是样本、名称或同块要求。
- ClosedValues复用共同DescribeEvaluation、OrderedEvaluation、实际
  operand角色/语言域及AnalyzeValueIdentities的copy/phi SCC根。单调
  稀疏工作表按原始UseId逐次解除输入依赖，不递归遍历表达式、不按局部
  opcode序列匹配。参数/捕获/frame/cell/heap等隐含上下文不能成为封闭
  RHS；即使类型为Number/String或伪造无效果，仍无封闭叶子事实。
  同一外部身份锚定的phi可传递封闭值，不同选择值不因各臂纯净就成为
  封闭值。依赖全部已证明后才传播结果，私有NaN/内部值边界保持。
- 原证书更名ClosedClassFields，移除String/Copy局部扫描和重复branch
  名单；共同控制shape负责角色/arity，原EffectOrder的全路径单次字段
  链、真实receiver、DefineField descriptor、undefined return、方法
  owner及未暴露self-cell义务保持。实际源码仍由原FunctionTree和
  ClassStaticFields打印，没有另一套字段writer或新的发布前置。
- 新实际源码暴露typeof字面量也加逗号的表示问题。共同printer仅对
  GlobalNameExpr保留强制GetValue包装，防止typeof吞掉未解析名称异常；
  符号保留TDZ语义，其他表达式沿用共同优先级/括号。新字段输出为
  `static _value = !(typeof "value" === typeof "_value");`。并非按字段
  名称、源码文本或样本选择规则。既有自由全局typeof保护测试保持。
- closed_class_fields_test涵盖正常/反向的1/200/201/402层组合、复制链、
  相同身份/不同选择phi、重复同一输入、Number/String参数、伪造无
  效果的global/lexical/frame读取，以及原bypass/循环/重复写/效果barrier/
  receiver/descriptor/NaN反例。201→402组合work为18,394→36,484，
  不代替RSS或全语料性能证据。36份实际解码完整类源码新增540组独立
  Node对照：字段值/属性描述符、顺序、类/原型身份、捕获self构造器、
  实例/Reflect.construct及改写后的隔离；原初始化行为矩阵也保持。
- 日志前缀/tmp/abcd-closed-fields-*。red-source-1与red-trace-1记录
  原始失败；表示检查也先观察到typeof多余逗号。build-1缺少求值头文件、
  build-2测试误用不存在的Boolean工厂，修正后build-3和final-build-1
  均退出0；后者实际10个target、单-j4。首轮8/8通过7.72秒；最终定向
  15/15通过33.50秒，含来源路由、类初始化/default candidates、括号/
  Boolean语境及固定输入中的配对静态类与Content局部行为。未运行全套。
- fresh closed-fields-{default,language}-pass-2各五输入退出0，分别
  80complete/26incomplete-source/0source-failure/46no-module及
  95complete/11incomplete-source/0source-failure/46no-module。
  无legacy、complete降级或新增complete；各106份完整/部分源码解析和
  原index的SHA/size检查通过。相对raw-copy基线，各模式53份源码减少
  typeof逗号包装，其中完整源码分别36/51份；final输出与本轮pass-1
  逐字节一致。输入样本未修改。
- 本轮解决封闭无效果标量组合的类self初始化证明及typeof表示；不声称
  任意字段初始化已完成。带真实效果/异常的RHS、一般控制/暂存字段
  表示、异常相位初始化、未知父构造协议、WindowUtil NEG数值输入、
  实例字段/全局/完整签名/函数体整合及全部record独立验收仍开放。
  代理、全套、扩展语料、RSS/基准、调度/DFX及定期审视保持暂停。

### 2026-10-05：普通与异常路径共用原始 copy/phi 身份投影

- 规则revision113，基线88c040b2；25aa工作树、分支
  codex/resume-generator-post-loop。本轮开始时工作树干净。上一轮数值
  边界解释没有更改代码或默认合同；本轮完成不依赖新算术政策的实际
  raw-copy来源缺口，默认V3、显式language arithmetic及固定五ABC范围保持。
- 两项旧quotient检查在当前HEAD复现2/2失败，LLDB定位到TreeFlow的
  PredicateSnapshots：ValueCopy合并了保存赋值的来源，计算与传输被
  混为一个表达式。未删除或放宽snapshot/origin校验。普通路径现复用
  既有Build/VerifyEvaluationAliases与AnalyzeValueIdentities；纯copy
  和单外部身份锚定的phi仅投影身份，原指令保留原位置/原来源anchor，
  计算、首次保存与其他读取仍分别由原树验证器检查。未锚定循环、多个
  不同身份、内部值、真实mutable phi及catch作用域保留原义务。
- 共同planner、predicate候选/效果区间及ExpressionForest::Use查询
  同一canonical value，仍使用原UseId选择唯一保存位置。扩展raw getter
  测试发现Use漏查身份而产生孤立表达式；共同解析修正后通过。普通
  PhiCopies原未接身份方程，导致已alias的phi仍要求不存在的mutable
  binding；Build/fresh Verify现与异常路径使用同一SCC身份与typed
  PhiCopyUse投影。没有新增opcode序列、样本/名称、PC/寄存器、同块、
  producer名单或selection数量准入，也没有新的来源宽松匹配。
- eager_snapshot增加raw/simplified两路线：三profile、七种消费者、
  0/1/200/201/402复制链、寄存器交换、效果barrier、纯拆块，共1,680
  完整函数；另18个实际分支汇合函数检查身份phi与两种布尔路径。
  fresh TreeSemantics/InstructionSources及复制身份、consumer、赋值
  target和另一真实原始来源的篡改检查保持。201→402共同evaluation
  work比小于3；仍非RSS/全语料性能验收。Node独立对照检查payload
  身份、getter/键转换/写入/调用顺序及次数、receiver、原抛出token，
  正常/异常/改写后不得重读保存值，共528,528个独立场景通过。
- phi_copies旧夹具用同一参数锚定全部phi却强制要求mutable搬运，已
  改为参数/receiver不同身份以保留真实并行复制与循环scratch测试；
  相同身份另独立检查全部原输入保留、执行schedule为空及伪造projection
  拒绝。1/2/200/201/4096的既有定向规模保留，没有扩展语料或基准。
- 日志前缀/tmp/abcd-raw-copy-*。red-tests-1旧quotient为2/2失败；
  tests-2暴露新raw getter，phi-red-test-1暴露汇合binding；原phi单测
  的陈旧强制搬运断言在tests-4失败，修正独立夹具后通过。final-build-1
  因测试误用不存在的Frame访问器编译失败，使用夹具实际参数数后，
  final-build-2实际18个target单-j4退出0。final-tests-1定向35/35
  通过278.23秒，含新raw/phi、既有比较/cell/literal/selection、数值
  identity、语言算术/CLI/配对、异常staging/liveness及来源路由检查。
  没有执行全套或恢复暂缓验证项。
- fresh raw-copy-default-pass-1：80complete/26incomplete-source/
  0source-failure/46no-module；raw-copy-language-pass-1：95complete/
  11incomplete-source/0source-failure/46no-module。两模式各五输入退出0，
  无legacy writer或complete回退，各106份完整/部分源码解析通过，全部
  源码与comparison-call基线逐字节一致；SHA/size匹配原corpus index。
  输入样本未改。该模型修复没有增加本轮完整record数量。
- Goal active。剩余未知父构造协议、WindowUtil成员的NEG数值输入、
  非线性前缀与跨控制literal完整泛化、实例字段/全局/完整签名/函数体
  整合及所有record的独立行为/表示验收仍开放；组件通过、source_complete
  标志和语法解析不代替阶段一验收。代理、全套、扩展语料、RSS/基准、
  调度/DFX及定期审视保持暂停。

### 2026-10-05：比较条件与直线前缀贯通原生成员调用

- 规则revision112，基线de503f4b，当前工作树为25aa、分支
  codex/resume-generator-post-loop。上一轮解释数值边界没有改变实现或
  默认验收政策；本轮核验并接续未提交的comparison_call夹具及实际
  FollowView失败，完成共同恢复代码、解码/行为验证和固定五ABC导出。
  默认V3和显式language arithmetic仍分别记录，不替用户选择新政策。
- FindConditionalExpressions复用BooleanEvaluation证明VM数值分支与
  语言truthiness一致，移除重复的ToBoolean/NOT/strict equality准入
  名单。十种宽松/严格相等、大小比较、in、instanceof的Boolean结果
  使用相同条件表达式路径；任意Number不能因此获得Boolean事实。
- 原normal logical Domain只覆盖header/join，callee与条件之间的纯
  拆块遗漏前缀。现一次索引普通边的直线执行关系，从selection header
  回溯其执行前缀；Build和fresh Verify共用原效果区间方程，已消费的
  旧join prefix在其consumer后停止，避免重扫已消费长前缀。纯无参数
  control anchor依据共同shape及原效果/异常字段不计语言效果，真实
  getter/coercion/environment/throw仍推进事件时钟。拓扑索引O(B+E)，
  前缀在既有有界domain访问内线性处理，无逐变量CFG扫描或新依赖。
  单入口/单后继只是直线区间执行次数的充分证明，不是Reference语言
  准入；非线性前缀与其他历史表示缺口没有因此全部解决。
- 实际后续失败中，已初始化cell guard又被PredicateSnapshots选成
  首次使用，阻止lexical读取的赋值快照。候选census和selected subtree
  闭合复用IsInitializedCellGuard；fresh cell verification仍检查实际
  初始化、revision、读取及owner，guard保留原anchor，cell读保持有序。
  没有新增opcode序列、PC/寄存器/样本/名称、同块或selection数量匹配；
  CallPlan的原生Reference/receiver要求、scope、执行次数、效果/异常
  和来源检查未删除，也未使用可覆写的.call补救。
- 新comparison_call_test实际解码三个dynamic profile，十种比较乘
  direct/copy/1至4块直线拆分/inverted/initialized cell五形式，共150个
  两调用完整函数；另18个200/201/402规模函数均经过FunctionInput、
  FunctionValues、EvaluationPlan、完整FunctionTree、fresh TreeSemantics
  及InstructionSources，201→402共同work比小于3。实际树保留原生
  ConditionalExpr；callee consumer、snapshot consumer和初始化read
  事实篡改均拒绝。Node对150函数134,400场景通过，覆盖Symbol/BigInt/
  NaN/-0、ToPrimitive、Proxy/in/hasInstance、callee在参数getter中
  被改写、hostile own call、receiver、get/call次数和顺序及原异常身份。
  这些不是真实Ark二进制或暂缓RSS/全语料性能验收。
- ABC-only导出FollowView32779完整record后才读取配对TypeScript，
  新comparison_call_paired_source_test通过6,426个注册list item click
  和optional content回调场景，含重复indexTitleSelected读取、callee
  改写、receiver、状态、空值及各事件处原异常。未调用构造器或完整UI，
  不授予整份record/阶段一独立行为验收。已生成源码的scrollToIndex
  恢复原生成员调用加条件参数，content恢复optional member。
- 日志前缀/tmp/abcd-comparison-call-*。原比较夹具在真实tree阶段RED；
  拓扑扩展后的tests-2仍2/2失败，定位到纯control anchor事件计数。
  build-4因局部Pure函数不在该文件作用域而编译失败，改为共同shape和
  明确效果字段后build-5/tests-3通过；新增cell夹具cell-red-test再次
  RED，build-6/tests-4通过。final-build-1实际12个target单-j4退出0；
  final-tests-1定向24/24通过254.35秒。paired-regressions-1另3/3
  通过10.28秒，含ProgressBar、HotRankPage及普通literal配对；route
  target另行构建后route-test-1为1/1通过。没有执行全套，也未重跑或
  宣称修复两项旧quotient来源缺口。
- fresh固定五ABC两模式全部退出0，无legacy writer或complete回退；
  comparison-call-language-pass-1为95complete/11incomplete-source/
  0source-failure/46no-module，新完整FollowView32779。已有完整源码
  中MircoBlogView28438、HotColumnView36781、HotRankPage48945的比较
  选择改为条件表达式，ProgressBar9550的retained read并入首次赋值
  快照；逐份diff已检查，HotRankPage/ProgressBar配对回归通过，前两份
  没有整份配对行为验收。comparison-call-default-pass-1仍80complete/
  26incomplete-source/0source-failure/46no-module，仅ProgressBar快照
  表示变化。两模式各106份源码均解析通过，五输入SHA/size与原index
  一致，无样本修改；记录完整标志和语法检查不代替全阶段一验收。
- 剩余11份部分源码、未知父构造协议、普通literal和非线性前缀的完整
  控制泛化、raw-copy计算/传输来源、实例字段/全局/完整签名/函数体
  整合与全部record的独立行为/表示验收仍开放。Goal active；代理、
  全套、扩展语料、RSS/基准、调度/DFX和定期审视保持暂停。

### 2026-10-05：普通对象初始化贯通共同 cell 证明

- 规则revision111，基线7e8fe5a4。上轮提交是实际进展，数值政策解释
  不视为新授权；默认V3、五ABC范围及暂停项保持。从FlashSale和
  CommentListView的实际throw栈定位到LiteralPlan::VerifyNesting的
  kObserved：普通路径未接收共同cell事实，把已初始化read的guard
  当额外观察；异常路径已有同一事实。诊断日志前缀
  /tmp/abcd-tree-failures-*及/tmp/abcd-normal-literal-cells-*。
- normal BuildLiteralPlan/VerifyLiteralPlan显式接收CellEvaluation；
  fresh flow每次核验环境、实际写/读、revision和初始化owner，原
  LiteralPath的use及effect census复用IsInitializedCellGuard。
  构建和独立evaluation验证均贯通同一事实。未添加guard/opcode名单、
  相邻指令、样本/名称、PC、寄存器或同块准入；未删除scope、捕获
  owner、receiver、异常、效果或来源要求。没有证明仍保守拒绝；
  这份证明不表示闭包不可写。异常路径原语义保持。
- 新normal_literal_cells_test实际解码三个dynamic profile，direct/
  copies/分配前纯块拆分/nested四形式，2/200/201/402规模共48个
  完整FunctionInput→FunctionValues→FunctionTree，fresh TreeSemantics
  和InstructionSources均通过。树保留原生ObjectExpr；独立Node
  4,416个场景覆盖getter顺序、receiver、执行次数、原抛出token、
  payload身份、新对象及属性描述符。无证明、伪造revision/读事实、
  Hole读取的构建与fresh验证反例保持拒绝；201→402 work比小于3。
  新增cell核验沿用共同线性分析，每次use判断O(1)，未引入逐变量CFG
  扫描或新依赖；不是暂缓的真实RSS/全语料性能证明。
- 实际FlashSale(21488)/CommentListView(24096)先从ABC导出，再打开
  配对TS；受控宿主下margin/gutter/padding注册回调3,888个场景
  通过，覆盖嵌套initializer、重复getter、空值fallback、成员调用、
  宿主构造顺序及每个事件处的原异常。未执行这两个类的构造器或
  完整UI，不计作完整record/阶段一独立行为验收；Selection/FoundView
  的新complete也不能替代整份配对验收。
- red-build首次因新增target未配置而退出2，显式configure后red-build-2
  成功；red-test真正失败于普通BuildEvaluationPlan。build-1通过并
  使48函数green。build-2的7target单-j4通过，tests-2为7/8通过：
  旧phased_literal_test把kPhiInput等同纯producer，在line228失败。
  仅恢复五个生产文件到HEAD后重建baseline target，baseline-test在
  同一断言失败，随后完整恢复WIP。现移除这条过时纯度断言，保留
  consumer篡改、完整异常树及Node getter/异常对照，不放宽生产证明。
- final-build的9个target单-j4退出0。final-tests定向17项中16通过，
  新paired测试因受控宿主缺__Common__失败；补齐fixture后只重跑
  paired-test-2，1/1通过5.10秒，其余16项通过证据保持。包含literal
  plan/construct/intrinsic/argument、normal/phased cell、staged/conditional
  literal及decision回归。没有执行全套；两项旧quotient来源缺口未修复。
- fresh固定五ABC两模式全部退出0；normal-literal-cells-language-pass-1
  为94complete/11incomplete-source/1typed source-failure/46no-module，
  105份源码全部语法通过，原有101份逐字节不变。相比规则110新完整
  FlashSale21488、Selection26777、CommentListView24096、FoundView35059。
  normal-literal-cells-default-pass-1仍80complete/26incomplete-source/
  0source-failure/46no-module，106份语法通过，全部逐字节不变。输入
  SHA/size均与既有index一致，没有legacy writer、complete回退或改样本。
- 剩余FollowView32779已定位到CallPlan::VerifyCalls的kReference：
  callee property20在block0，call38在block3，callee被共同evaluation
  保留为kCrossRegionValue。不能删除引用证明或用可覆写的.call替代；
  下一步需将条件参数、getter和receiver顺序贯通共同表达式归属。
  普通literal跨控制流的完整泛化、input-only omission、raw-copy来源、
  未知父构造协议、实例字段/全局/完整签名/函数体及全阶段一独立
  行为/表示验收仍开放。Goal active，代理、全套、扩展语料、RSS/基准、
  调度/DFX及定期审视继续暂停。

### 2026-10-05：共享条件复用共同块放置及已初始化 cell 事实

- 规则revision110，基线1dcd9ad5；上一轮已完成实际开发提交，Goal仍
  active。本轮从实际HotRankPage的tree_stage失败继续，不改变默认
  数值合同或固定五ABC范围。lldb异常栈定位到EmitDecision的第二套
  inline/module-guard名单；原局部load后的kCellGuard已由共同cell分析
  证明初始化，普通块支持它，共享条件却再次拒绝。诊断日志前缀
  /tmp/abcd-tree-recovery-hotrank-*；首个逐调用条件断点太慢，主动
  结束自己的调试进程后改用throw断点，未重启或处理历史进程。
- 普通共享条件的predicate与linear prefix改用同一EmitBlock；原
  EvaluationPlan的expression/anchor/statement归属及控制顺序保持。
  有语句根或环境声明时沿用共同平层predicate staging，其余保留原生
  logical。未增加cell-guard局部生产分支、opcode序列、同块、PC、
  寄存器或样本名单；没有另一套writer或源文本补丁。
- fresh TreeFlow的IndexPlacements独立检查实际cell初始化、receiver/
  class/hole等anchor语义；DecisionOperation消费该已检查placement，
  验证原BlockBoundary及表达式/sequence归属，不再施加第二个producer
  名单。原scope、操作数、效果、异常、身份及来源检查保持；环境声明
  仍须实际语句归属，不能伪装为无声明anchor。上轮raw-copy snapshot
  来源缺口没有通过放宽PredicateSnapshots或origin检查来处理。
- 新decision_cells_test三个dynamic profile分别实际解码，2/200/
  201/402条件和direct/copy/拆块/反向比较/条件内局部作用域共60个
  完整函数，经过FunctionInput→FunctionValues→FunctionTree、独立
  TreeSemantics及InstructionSources；Node通过25,320个getter
  receiver/次数/顺序、异常token和返回身份场景，保留原生logical，
  无INCOMPLETE、switch或Reflect输出。初始化事实篡改、guard anchor
  位置及来源篡改均拒绝；201→402共同work比小于3。恢复和fresh flow
  沿用O(I+B+T)遍历/存储，没有逐变量CFG扫描或新依赖。
- ABC-only生成HotRankPage(48945)完整record后才打开配对TypeScript，
  受控宿主下隔离calcMarginTop/getDisplayCount实际方法，6,467个
  场景覆盖边界、String/BigInt/Symbol、offset转换、属性get/set顺序、
  失败token、状态及原输入对象返回身份。未调用构造器或UI，不授予
  全record/真实平台完整行为验收；新HotColumnView无整份配对行为
  验收，不用其source_complete标志代替最终阶段验收。
- 日志前缀/tmp/abcd-decision-cells-*。red-build成功，red-tests
  1/1失败于真实完整树构建；build-1及final-build的11个实际target
  单-j4退出0。tests-2初版6/6通过40.71秒；补齐局部作用域和实际
  配对后final-tests 17/17通过84.20秒，含normal/phased cell、
  control/snapshot、nullish、language arithmetic/source/paired和
  route检查。随后增加guard origin篡改及logical表示断言，仅重建/
  重跑两个直接受影响测试，mutations-tests.log 2/2通过8.55秒。
  未执行全套。
- 最终规则110 fresh固定五ABC两模式均退出0，input SHA/size保持与
  原index一致；没有legacy writer或既有complete回退/源码变化。
  decision-cells-language-pass-2为90complete/11incomplete-source/
  5typed source-failure/46no-module，101份实际源码全部解析通过，
  原有99份逐字节未变化。相比规则109新增完整HotColumnView(36781)
  和HotRankPage(48945)。decision-cells-default-pass-1仍80complete/
  26incomplete-source/0source-failure/46no-module，106份源码解析
  通过、内容逐字节一致；默认numeric政策没有改变。
- 剩余五个tree失败为FlashSale/Selection/CommentListView/FollowView/
  FoundView。类成员input-only omission及raw-copy来源、未知父构造
  协议、实例字段/全局变量/完整签名/函数体整合和整份阶段一独立语义/
  表示验收继续开放；两项旧quotient检查的失败未在本轮宣称已修复。
  后续继续推进实际失败函数，不能以这两个记录或本组件代替Goal完成。
  代理、全量套件、扩展语料、RSS/基准、调度器/DFX及定期审视仍暂停。

### 2026-10-05：显式语言算术合同贯通共同数值分析

- 规则revision109，基线ca89ec64。用户要求解释INT32_MIN/NaN边界，
  尚未选择新的默认验收标准。默认kLanguageAndHostV3保持；新增显式
  kLanguageArithmeticV1及CLI --value-source --language-arithmetic。
  具体边界、来源和非承诺范围见[语言算术合同](language-arithmetic.md)。
  解释器源码中的UB及标签编码证据不冒充实际Ark二进制结果，也不证明
  固定五ABC实际触发边界。
- SourceContract映射typed ArithmeticSemantics，经FunctionInput/
  SemanticProof和PhasedBody贯通共同scalar adapter、normal/phased SSA
  稀疏求解器；LinearLexicals复用同一转移，并在覆盖accumulator前保存
  两个输入事实。仅已证明语言输入的normal binary completion得到语言
  数值结果；既有有限值/整数/String精度保持，Hole、环境、未初始化及
  impure literal不能升级。默认规则、unary转换/update、未知父构造协议
  保持；效果、异常、receiver、scope及instruction来源检查未删除。
- 普通运算仍使用共同ValueBinary/BinaryExpr、FunctionTree及printer，
  优先级/括号沿用原设计。无新writer、样本/名称分派、opcode序列、PC/
  寄存器、同块、producer名单或selection数量准入。模式身份进入实际
  receipt/manifest；CLI两种flag顺序及不兼容/重复参数验证通过。
- 三个dynamic profile分别实际解码，12个binary operator、5种表达式/
  copy/纯块拆分组合、normal和try/catch共360个完整生成函数。独立Node
  通过302,760个场景，覆盖int32/double边界、NaN、正负零、String、
  BigInt/Symbol、转换getter/receiver/顺序/次数及原抛出token；含正常及
  异常相位。typed输入反例、合同篡改拒绝和0-INT32_MIN后接NEG的
  normal/linear验证通过。200/201/402既有work的201→402比小于3；
  无新逐变量CFG扫描，转移每节点O(1)，沿用O(V+U)稀疏存储。
- 实际Content(14411)/PictureDetail(45391)先仅从ABC生成完整record，
  再读取配对源码；受控宿主下注册的key callback及Content资源数组
  444个场景通过。未执行未知构造器，也不授予整份UI/record独立行为
  验收。sidecar未进入引擎输入，拒绝/INCOMPLETE不计为行为成功。
- 日志前缀/tmp/abcd-language-arithmetic-*。最初red-tests.log 1/1在
  普通ADD语义输入拒绝处失败。final-build.log实际13个target单-j4
  退出0；final-tests.log 17/17通过56.91秒，包含新mode/CLI/paired/
  source及既有additive/exponent/conversion、snapshot和logical检查。
  早期build-4请求未注册predicate_snapshot_test而退出2，改为实际
  注册target后构建成功；未启动全量套件。
- 另两项旧quotient_value_test/quotient_values_source_test在未修改
  TreeFlow时已失败（tests-2.log）。raw ImportNormalValues保留copy
  后，snapshot RHS的计算来源与transport wrapper来源没有共同对齐；
  这是现有来源模型/测试路径缺口。两次局部放宽不能闭合来源证明，且
  会允许既有origin篡改，故全部撤回；最终TreeFlow与基线一致，原
  decision_snapshots_source_test重新通过。这两项仍未修复，不能把
  17项定向通过报告为全部回归通过，后续需共同计算/传输来源建模。
  最终final-known-failures.log再次2/2失败1.38秒，未执行基线二进制
  对照，不能将“本轮未修改此来源路径”当作基线测试失败的完整证明。
- fresh固定五ABC两模式全部退出0、无legacy writer、无既有complete
  回退，原有80份完整源码未变化。default-pass-1为80complete/26incomplete-source/
  0source-failure/46no-module，106份源码解析通过并与基线逐字节相同。
  language-arithmetic-pass-2为88complete/11incomplete-source/
  7typed source-failure/46no-module，99份实际源码解析通过。两模式
  原ABC的SHA/size均与index一致；语法检查不能替代完整行为验收。
- 新完整record为Immersive的Content/Categories/CommonView/Header/
  Recommend/Welfare和MultiCommunityApplication的MircoBlogView/
  PictureDetail。7个新暴露的tree_stage失败为FlashSale/Selection/
  CommentListView/FollowView/FoundView/HotColumnView/HotRankPage。
  默认模式这些记录原有部分源码；新模式通过输入后，现有类成员省略
  只接受input gap，尚不能传播后续tree gap，故只有元数据及typed失败。
  这是显式模式的部分输出覆盖缺口，不能称为无source-failure或全覆盖。
- 下一步需要共同tree gap传播/来源模型、未知父构造协议及实例字段/
  全局变量/完整签名/函数体整合，固定五ABC整体语义与表示验收仍未
  完成。Goal active；代理、全量套件、扩展语料、RSS/基准、调度器/DFX
  和定期审视继续暂停。本组件不改变默认验收政策或阶段一目标。

### 2026-10-05：共同语句树恢复普通共享决策的语句前缀

- 规则revision108，基线7e059c35。上一轮数值说明没有改变验收合同；
  本轮实际补齐FunctionTree的普通语句前缀，不删除语义输入检查或barrier。
  原nullish_selection的12个debugger拒绝夹具增加真实属性读取效果，要求
  完整函数输出；red-tests.log 2/2失败于既有kOwnership树缺口。
- 原ControlDecision/EvaluationPlan保留全部指令、共享/fallback归属及求值
  顺序。共同语句构建以一个块内私有条件变量和同层if保留后续前缀及原
  谓词，共享分支只构建一次。依据typed Root placement选择表示，不按
  debugger或opcode名单、样本、固定guard数、寄存器、PC或同块匹配。
  根表达式、debugger、copy/jump锚点沿用共同EmitBlock；纯表达式路径
  继续使用原native logical。没有第二套writer或原CFG修改。
- Fresh TreeFlow独立验证每个原条件、极性、前缀语句顺序、原边转移及
  私有变量所有访问，拒绝外部读写/update/capture及未知作用域归属。
  原receiver、捕获、异常、操作数和instruction来源校验保持。异常相位
  statement-only前缀及catch staging仍有表示缺口，不计为已恢复。
- 三profile分别实际解码，1/200/201/402的144个完整生成函数包括纯
  副作用前缀、debugger、copy/纯块拆分、反向比较和原nullish反例。
  独立Node通过225,792个返回身份/异常身份/getter执行次数/顺序场景；
  201→402既有work比小于2.1。条件极性、保存目标、最终条件及getter/
  debugger顺序的实际树篡改均被独立校验拒绝，原操作源码区间检查通过。
  不再把这些普通语句前缀的typed拒绝计为行为成功。
- 日志前缀/tmp/abcd-statement-decision-*。build-1.log与tests-1.log
  完整树通过但Node解析深层嵌套if耗尽调用栈，改为同层条件staging；
  build-2.log及tests-2.log 2/2通过14.35秒（108函数/169,344场景）。
  扩展反例build-3.log失败于测试BooleanLiteral构造，修正后39个target
  related-build.log与final-build.log单-j4退出0；tests-3.log 2/2通过
  21.29秒，最后related-tests.log 89/89通过495.48秒。包含normal/phased
  选择、callee/receiver、模块、类/构造器/字段、默认值/capture、真实配对
  记录以及generator return/suspend/loop；未恢复全量套件或暂停项目。
- fresh build/stage-one/statement-decision-pass-1固定五ABC全部退出0，
  仍80complete/26incomplete/0failure/46no-module，无新增完整记录、
  完整回退或legacy writer。原index/manifest SHA与size一致，106份源码
  全部解析通过且内容逐字节未变化；这是回归证据，不是阶段一完成。
- 新增构建与fresh flow开销在既有控制owner下为O(I+B+T+A)，存储相同，
  没有逐变量CFG遍历、逐候选树复制或新依赖。author自检核对private
  scope/identity、平层guard极性、全路径执行次数、来源及所有新恢复条件。
  普通数值、未知父类协议、fields/globals/signatures/body integration与
  整份阶段一独立语义/表示验收仍未完成。下一步仍需直接推进失败函数的
  输入分析和函数体整合，不能以本组件完成代替Goal。Goal active；代理、
  全量套件、扩展语料、RSS/基准、调度器/DFX及定期审视继续暂停。

### 2026-10-05：共同有序保存值恢复带副作用 base 的空值访问

- 规则revision107，基线d848a966。上轮数值边界解释没有改变验收合同；
  本轮继续固定五ABC的阶段一目标，实施恢复功能和独立行为/表示验证。
  新实际decoded用例在旧实现2/2 RED，原函数已通过共同语义验证但没有?.；
  日志前缀/tmp/abcd-snapshot-optional-*，不靠样本或配对源码生成结果。
- PlanNullishSyntax复用原null/undefined/other解释、Kahn所有权与结果合流，
  新增AssignmentExpr返回其SymbolTarget所保存RHS值的语言转移，以及
  Sequence/短路/conditional各域的有序形成与先前读取事实。整个原赋值及
  任意RHS效果成为native base；不匹配两个guard、赋值邻接、producer名单、
  opcode序列、寄存器、PC或同块。已证明SSA/控制/效果/异常/receiver、
  scope与原instruction来源保持，不新增恢复准入条件或第二套writer。
- OptionalMemberSyntax明确保留base和原member/key；被消除的Symbol base
  及guard在保存表达式end记录零宽事件，赋值/RHS仍有实际范围。共同
  printer、family emission与serializer消费同一typed计划；目标写入、
  RHS异常、key转换、getter、调用receiver及外部chain边界保持。
  同一形成也可以作为??的原value。分离的多次形成、旧值条件读取、只在
  部分路径保存不能被改变执行顺序/次数；不能投影时仍完整打印原条件。
- 三profile分别实际解码，1/200/201/402 guard的96个完整生成函数包含
  反向比较、NOT、copy/纯块拆分、getter/call、计算成员键和缺少undefined/
  不同结果反例，15,360个独立Node场景通过。另80组纯语法夹具包含反向
  arena、Sequence、任意RHS复合效果、动态key转换、外部value-call、
  旧值读取、再次赋值、部分路径保存及其他binding效果；384,000个行为
  场景、严格来源序列化与201→402几何work检查通过。语法夹具不冒充
  原ABC完整函数证据，未用typed拒绝/INCOMPLETE代替复合行为成功。
- 实际TestAbility/EntryAbility完整onCreate/onNewWant方法4,896配对场景
  通过并新增原生?.断言，不调用未恢复构造器。Home Tabs/onChange的592
  场景、MiniGame受控宿主下constructor/Column/Content/update/延迟参数
  callback的880场景、Push两注册方法及callback的572场景分别通过；
  这些不授予部分record、其他方法或实际OpenHarmony UI/平台完整验收。
- related-build.log的39个实际target单-j4构建退出0。related-tests.log
  85/86通过491.13秒；唯一失败为旧phased-call测试强制“三元必须存在”，
  核对实际?.输出后按nullable/count更新表示断言，保持原行为oracle。
  最后重新配置注册MiniGame测试，final-tests.log 4/4通过5.52秒，含
  78,624 phased调用receiver/效果/异常场景及上述三个实际配对测试；
  共89项当前相关检查闭合，未重复启动全量套件。早期补充语法夹具把两个
  独立getter fallback误当相同结果，tests-2.log 4/6；该既有表示缺口
  保留，??正例改为同一语言值，tests-3.log 6/6通过20.05秒。首次
  MiniGame测试使用已存在输出目录被writer拒绝，改为临时子目录后通过。
- fresh build/stage-one/snapshot-optional-pass-1固定五ABC均退出0，仍为
  80complete/26incomplete/0failure/46no-module，无新完整项、完整回退或
  legacy writer。原index/manifest SHA和size一致，106份源码解析通过。
  已逐项检查全部diff：6个record（2完整、4部分）的24处展开式改为?.，
  无其他源码文本变化；style-summary-1.json保留各record计数。
- author自检核对typed赋值语义、路径形成/旧值/重复写入、opaque RHS、
  原子树与source event所有权、key/receiver、TDZ及short-circuit异常。
  复杂度保持O(N+A+P+M log(2+M)+C)时间、O(N+A+P+M)存储；没有新
  外部依赖、代理或实测RSS结论。一般alias transport、分离形成效果、
  独立effectful fallback、optional call/整链/child owner及statement-only
  决策前缀仍有表示缺口。普通数值、未知父构造协议和字段/全局/签名/
  函数体整合、整份阶段一独立语义/表示验收仍未完成，Goal active；代理、
  全量测试、扩展语料、RSS/基准、调度器/DFX及定期审视继续暂停。

### 2026-10-05：原保存值经共同四域方程恢复空值选择

- 规则revision106，基线eb8fd3cd。继续固定五ABC的阶段一语义/表示目标，
  用户数值边界解释没有改变验收标准。旧实现新decoded夹具及真实HotList
  width/gap表示断言2/2 RED，日志前缀/tmp/abcd-nullish-selection-*。
- ProjectSelectionLogicals沿用原inline consumer森林和shared-arm控制，
  方程扩展为null/undefined/其他假值/其他真值四域。严格比较、交换操作数、
  NOT、copy及有Boolean输入事实的VM谓词统一传播；Boolean中间结果再与
  null/undefined比较不能获得原输入的nullish域。各scalar原可达域合并后，
  kept值域分别选择既有OR/AND/Nullish LogicalExpr，不按guard数量、样本、
  名称、opcode序列、寄存器、PC、同块或assignment语法匹配。
- 原first UseId与snapshot assignment作为LogicalExpr.left保留；getter或
  call执行一次，其他使用读取原保存值。fallback保留原树和执行条件，
  receiver、类型转换、效果/异常、phi/control及instruction来源不变。
  fresh TreeFlow已有事实后，TreeOperands仍以unary/copy/branch名单额外
  拦截binary比较，tests-1.log记录5/9失败；删除该重复producer条件，
  原保存身份、边选择和来源检查保持。不是删除真实语义义务。
- 三profile、1/200/201/402重复guard的108个真实解码夹具包含反向比较、
  NOT、寄存器copy/纯块拆分、缺少undefined、不同结果、Boolean中间结果
  及成员调用产生原值。96个完整生成函数通过150,528次独立Node行为对照，
  覆盖getter/call receiver、改写、执行次数、原异常身份、正负零/NaN/
  BigInt/Symbol/object/function返回身份及fallback条件；错误OR、交换两支、
  替换保存值的实际树篡改均拒绝。
- 另外12个条件间debugger用例发现既有FunctionTree decision的statement-only
  前缀kOwnership表示缺口。没有native logical投影、完整源码或行为通过，
  不计为成功。共同控制/语句表示仍待实现，不删除barrier、不追加布局匹配。
- 真实HotListItemView完整记录两个新??风格断言及616个callback/default/
  reference、572个detail配对场景通过。新增Immersive完整onWindowStageCreate
  方法及callback的780个配对场景通过，含JSON.stringify的receiver、返回
  身份、callee改写和异常顺序；完整record其他成员/构造器及HarmonyOS host
  不因此获整体验收。pasteboard的同类完整方法/错误callback另通过240个
  场景，仍未调用该partial record的缺口成员。
- build-2.log五目标、related-build.log实际37目标单-j4构建退出0。
  tests-2.log 8/10通过，剩余两项是debugger既有树构建缺口；不伪造输出。
  build-3.log因新unit漏含tree_checks.h失败，补齐后build-4.log与tests-4.log
  2/2通过12.66秒。related-tests.log 80/81通过466.11秒；追加调用夹具时
  运行中的Node已期望108条，尚未重建的exe仅输出96条，导致一项数量失败。
  final-configure.log、final-build.log三目标单-j4退出0，重建后final-tests.log
  五项全部通过16.40秒。原81项加新paired/已有pasteboard callback共83项
  当前相关验证闭合；没有恢复全量套件、402-return已知资源缺口或RSS基准。
- fresh build/stage-one/nullish-selection-pass-1固定五ABC均退出0，仍为
  80complete/26incomplete/0failure/46no-module，无完整回退、新完整项或
  legacy writer。batch-1.log/syntax-1.log核对index/manifest SHA和size，
  106份源码全部解析通过。style-summary-1.json逐项检查全部diff：6个
  record（2完整、4部分）新增6处??，并减少3处旧??冗余括号，没有其他
  源码文字变化；含HotList属性读取和多个应用JSON.stringify调用结果。
- 继承原稀疏方程O(I+V+B+C)时间/存储与共同SSA/census O(I+V+U)成本，
  四域状态大小固定，condition block纯度一次缓存，无逐候选全图遍历。
  author self-review已核对纯谓词、Boolean中间值、snapshot/occurrence、
  shared-arm、负例及所有导出diff；无新依赖、代理或实测RSS结论。
  statement-only前缀、其他alias、optional call/整链/未打印child owner，
  一般数值、未知父构造协议及阶段一独立语义/表示验收仍未完成。Goal active；
  全量测试、扩展语料、RSS/基准、调度器/DFX和代理继续暂停。

### 2026-10-05：共同空值分析恢复原生 fallback 表达式

- 规则revision105，基线fd46be6d。旧HotListItemView将showDetail ?? false
  展开为两层三元；red-tests.log保留新语法fixture与真实record的2/2失败。
  用户要求数值边界解释未授权修改合同，本轮普通算术规则保持。
- 原PlanOptionalAccess重命名为PlanNullishSyntax，typed variant共用
  OptionalMemberSyntax与NullishFallbackSyntax。严格比较、NOT、AND/OR、
  条件结果在null/undefined/other域传播；纯guard按SymbolId/ThisExpr.owner
  关联原稳定读取，其他域返回原值、两个nullish域返回同一fallback时复用
  既有LogicalExpr kNullish和共同printer。原AST/CFG/control/operand与
  instruction来源不变，无sample/名字/opcode序列/寄存器/PC/同块匹配。
- fallback保留为原opaque子树，call/member/sequence/其他binding条件不
  被重放或提前执行，原receiver/getter/异常不变。外部value-call仍间接；
  ??混合&&/||保留语法括号。结果身份依据原ExprId、typed稳定身份或
  literal值，UTF-16文本一次排序规范化，重复池编号不成为准入条件。
  false/0、正负零及不同文本不合并，不凭文本相似合并effectful occurrence。
- 自检新增两项TDZ反例：另一binding的条件两支同值，或先执行该条件再经
  OR得出已知truth，原WIP错误省略读取。foreign-red.log与guard-red-tests.log
  各1/1失败后修正。其他binding的结果保留整个原子树；可消除predicate
  必须证明每个可能执行的分支均纯且属于同一引用域。不是按局部形状补匹配。
- 省略节点及属性在已打印value.end保留零宽事件，emission/serializer共用
  typed计划与nullish_fallback/projection标记，member投影沿用optional_access。
  缺失计划保持typed失败或输出流失败，不捏造有长度的来源位置。
- 新176组语法fixture覆盖1/200/201/402条件组合、反向arena、不同guard
  形式、相同/不同TextId内容、正负零、fallback绑定/call/getter/sequence、
  外部value-call、mixed逻辑及实际let-TDZ。独立Node oracle通过76,032场景。
  fixture只验证共同语法投影；真实decoded组合与paired记录另行验收。
- HotListItemView完整record新风格断言与572个isShowDetail配对场景通过，
  另保留616个callback/default/reference场景。实际TestAbility/EntryAbility
  共4,896个完整onCreate/onNewWant方法配对场景通过，并检查原生??表示；
  不调用未恢复构造器、不授予这些partial record或HarmonyOS平台验收。
- 日志前缀/tmp/abcd-nullish-fallback-*。build-2.log失败于位置测试中变量
  声明顺序，修正后build-3.log五目标单-j4成功；focused-tests-2.log
  8/8通过30.02秒。追加TDZ guard反例修正后，related-build-2.log的36个
  实际目标单-j4退出0。主related-tests.log 76/76通过348.35秒；核对-I
  范围参数发现漏选两项，additional-tests.log 2/2通过113.56秒，另
  entry-paired-tests.log 1/1通过1.63秒，共79项通过。包括类/构造器、
  capture、module、normal/phased选择、getter/effect、优先级/原生名称及
  generator return/suspend/loop。未运行已知402-return资源缺口或全量套件。
- fresh build/stage-one/nullish-fallback-pass-1固定五ABC均退出0，仍为
  80complete/26incomplete/0failure/46no-module，无完整回退、新完整项
  或legacy writer。batch-1.log/syntax-1.log核对原index/manifest SHA和
  size，106份源码全部解析通过。style-summary-1.json核对所有diff：
  3个record（1完整、2部分）的5处展开式改为??，没有其他源码文本变化。
- author self-review核对guard读取/TDZ、原fallback occurrence及effects、
  receiver、精确literal、来源和迭代所有权。O(N+A+P+M log(2+M)+C)时间，
  O(N+A+P+M)存储，M为文本项、C为UTF-16比较工作；没有将字符串比较
  隐藏为常数，也没有逐候选重放。没有新依赖、独立代理审查或实测RSS结论。
  首次effectful读取/assignment前缀快照与alias、optional call/整链/被省略
  子函数owner仍是表示缺口。一般数值、未知父构造协议及阶段一独立语义/
  表示验收未完成，Goal active；代理、全量测试、扩展语料、RSS/基准和DFX
  继续暂停。

### 2026-10-05：以共同 nullish 解释恢复原生可选成员访问

- 规则revision104，基线e7762a23。CommentItem实际完整record旧输出仍将
  optional member展开为嵌套三元；paired-red.log保留1/1表示断言失败。
  本轮不改变数值验收合同，不增加样本/名字/opcode/寄存器/PC/同块匹配。
- PlanOptionalAccess解释null/undefined/other三个语言域，按typed SymbolId
  或ThisExpr.owner证明原稳定读取；严格比较、NOT、AND/OR与条件谓词
  共用传播/合流。Kahn解决任意arena顺序，top-down保留最外层投影，
  不限制guard数或顺序，不逐候选重放子树。原FunctionTree、CFG、控制、
  operand及instruction来源保留，同一printer输出?.name/?.[key]。
- 原selected member/base/key及其getter、转换、receiver和异常仍执行一次；
  nullish路径不执行key/getter。括号结束本次chain，外部member与value
  call仍执行，间接调用不取得成员this或direct-eval语义。不同binding、
  null fallback、truthiness、缺少null路径与有副作用的guard/fallback不
  冒充optional member。effectful base仍需独立快照/alias证明。
- 省略节点在已打印base.end、?.之前记录零宽事件；emission/serializer
  共用计划和optional_access/projection标记。新dead-arm反例在旧实现
  source位置断言RED（dead-arm-red.log，exit134），修复沿完整AST所有权
  流式保留调用/聚合子节点及属性事件，不捏造源码长度；资源/work失败
  保持typed分类，serializer无法提供真实计划时传播写入失败。
- 新152组语法fixture覆盖1/200/201/402重复guard、反向arena、嵌套谓词、
  quoted/computed key、外部member/value-call、discarded call/aggregate
  及效果反例；独立Node oracle共13,376场景通过，另检查来源序列化。
  fixture只验证语法投影，实际decoded logical/normal/phased组合及
  CommentItem完整module的2,422配对构造场景分别验收，不读取sidecar生成。
- 日志前缀/tmp/abcd-optional-access-*。首轮focused-tests-1.log 4/6通过，
  两项停在旧的每访问两个三元表达式断言；核对实际生成源码后更新形式
  断言及严格零宽事件oracle，focused-tests-2.log 6/6通过27.13秒。
  related-build.log的35个实际注册目标单-j4成功，related-tests.log
  最终73/73通过454.30秒，含类/构造器、捕获、联合选择、模块、异常
  相位、getter/effect及generator return/suspend/loop回归。
- fresh build/stage-one/optional-access-pass-1固定五ABC均退出0，仍为
  80complete/26incomplete/0failure/46no-module，无新完整项、完整回退
  或legacy writer。batch-1.log/syntax-1.log核对原index/manifest SHA和
  size，106份源码全部解析通过。4个record（3完整、1部分）的6处展开式
  恢复为?.；style-summary-1.json逐项核对完整diff。部分record仍不作为
  整份等价执行输入，源码语法通过不代表所有record行为已验收。
- author self-review核对稳定身份、纯谓词、false/0/-0/NaN/string/BigInt/
  Symbol、key/getter/call次序、chain边界、来源位置及线性所有权队列。
  O(N+A+P)时间/空间，无新依赖；没有独立代理审查或进程峰值RSS结论。
  原生optional call、effectful base快照/alias、整链压缩及被省略子函数
  owner的完整family发布仍是表示缺口。一般数值、未知父构造协议、字段/
  全局/签名/Reference/phase与阶段一独立语义/表示验收仍未完成，Goal
  active；全量测试、扩展语料、RSS/基准、调度器/DFX及代理继续暂停。

### 2026-10-04：构造器以原生完成表示尾部 receiver 返回

- 规则revision103，基线06701bf9。真实HomeModel构造器和独立语法fixture
  在旧printer中仍输出return this；red-tests.log 2/2失败3.07秒，实际
  constructor及完整record各自增加源码表示断言。此次不改变数值验收合同。
- ImplicitTailCompletion扩展规则100的共同尾部投影：实际base/derived
  constructor body末尾直接返回自身ThisExpr时使用自然完成。AST、控制、
  operand、原instruction来源及零宽位置保留，printer/emission/serializer
  共用同一查询。不依赖样本、函数名、PC、寄存器、opcode或CFG同块条件。
- ECMA-262 2024 Construct保证同一receiver身份；未初始化derived this
  的原生完成仍抛出ReferenceError。提前/嵌套、try/catch/finally保护内的
  返回、普通function receiver返回、其他对象、alias/member/call/sequence
  均保留。语法尾部是安全投影边界；更广完成消除仍需控制/异常归属证明。
- 新24组base/derived语法fixture经真实ClassTreeContext和同一printer，
  独立Node oracle共8,640场景通过：原对象/替代super对象/alternate
  newTarget身份，super/event/getter/prototype异常，保护范围、finally覆盖、
  arity和不可Call。fixture只验证语法投影，不冒充原ABC构造器发布证据；
  实际CommonConstants/HomeModel解码及完整HomeModel配对另行验收。
- /tmp/abcd-constructor-tail-build-1.log四目标单-j4成功；tests-1.log
  首轮6/6通过9.29秒。related-build.log的25个实际注册目标单-j4成功，
  related-tests.log 41/41通过151.81秒，含类/派生捕获/default forwarding、
  record整合、tail原生async/generator job顺序及两项返回清理组合。
  已知402选择的资源失败用例不在此相关选择中，未宣称全量测试通过。
- fresh build/stage-one/constructor-tail-pass-1固定五输入全部退出0，
  仍80complete/26incomplete/0failure/46no-module，无完整回退、新完整
  项或legacy writer。batch-1.log/syntax-1.log核对原index/manifest SHA和
  size，106份源码解析通过。23完整和2部分记录表示改善；所有文本变化
  仅删除29行构造尾部return this，原30处降至1处，style-summary-1.json
  保存逐文件diff核对。部分record仍不作为整份等价执行输入。
- author self-review核对native完成、未初始化异常时点、protected返回、
  原节点/来源位置和class上下文；O(1)查询，无新增依赖或全图遍历。
  普通数值、未知父类构造协议、Reference/phase/字段/全局/签名和整份阶段一
  独立语义/表示验收仍未完成，Goal active。代理、全量测试、扩展语料、
  RSS/基准及DFX继续暂停，不宣称实测进程峰值内存达标。

### 2026-10-04：普通初始化与捕获使用支配区域及真实路径

- 规则 revision102，基线 dd07273e。完成此前已复现的普通 cell 初始化
  地址限制修复：原入口对 allocation→store→load 的合法执行，只因 store
  或 allocation 的块放在更晚地址就拒绝。日志前缀
  /tmp/abcd-normal-initialization-*，原 red-tests.log 保留 FunctionInput
  拒绝证据。没有用样本、名字、opcode 序列或固定块布局选择生产语义。
- LocalAccessPlan 收集各 cell 的初始化 store 支配区域，保留外层、互不
  重叠的区域；读取和 NormalScopeFacts 共用 InitializationBefore。
  allocation 身份、支配关系和所有写入的 Language 义务保留，只在同一
  实际 block 比较 instruction 顺序。Defined guard 的创建点查询也改用
  实际 block/支配事实，跨块不额外比较原地址。
- 未保留 capture 的暴露沿正常 DAG 前驱传播，捕获前状态排除当前创建点；
  地址更晚但执行更早的捕获仍拒绝，兄弟分支及 fresh allocation 之前的
  捕获不误算当前环境暴露。复用 Kahn/Lengauer-Tarjan/Euler，抽出共同
  InstructionRank 类型；rank 不替代支配、路径、reaching value 或效果证明。
- 新解码夹具有两种反向布局，三 profile 分别实际解码，1/200/201/402
  读取贯通 FunctionInput→ValueProgram→FunctionTree→同一 printer。
  另有 1/200/201/402 个独立初始化区域、兄弟捕获、旧环境捕获、可写
  暴露、store bypass 和晚 guard 正反例。Defined guard 单元视图仅测试
  Rebase 查询，不冒充完整 Defined producer 发布验收。
- related-build-2.log 的23个目标单-j4构建退出0；related-tests.log
  29/29通过133.15秒。首次相关构建因CTest别名误当target而退出2，已按
  注册的实际可执行目标修正。function_input旧源码位置断言仍要求每个
  expression非零宽，与规则100的undefined尾部完成契约不符；现严格
  检查实际return/literal、owner/body、共同零宽边界及结束缩进/括号。
  更正后的首次4项复验通过3.23秒。最后补实各profile解码并重建新目标，
  profile-tests.log 2/2通过4.02秒；24份实际生成源码的独立Node oracle
  共336个身份/副作用场景通过，不用参考源码返回或INCOMPLETE充当成功。
- fresh build/stage-one/normal-initialization-pass-1 固定五输入均退出0，
  仍80complete/26incomplete/0failure/46no-module，无新完整项、回退或
  legacy writer。batch-1.log/syntax-1.log核对原index/manifest SHA和size，
  106份源码解析通过，全部与generator-completion-pass-1字节相同。
- author self-review核对区域嵌套/剪枝、同块顺序、所有前驱暴露、分配
  身份、查询调用方及保留写入义务。算法成本和边界见algorithms.md。
  所有路径初始化的合流、真实循环/异常activation transfer、旧deferred/
  module prefix布局和协程包装仍是缺口，不宣称所有形状条件审计闭合。
  普通数值和未知父构造器仍阻断当前26项；数值验收标准尚无用户新选择，
  本轮不改变合同。字段/全局/签名/Reference/phase及整份阶段一独立语义/
  表示验收未完成，Goal active。代理、全量测试、扩展语料、RSS/基准及
  DFX继续暂停；本轮不宣称进程峰值内存达标。


### 2026-10-04：以共同语法退出恢复 generator RETURN 清理

- 规则revision101。此前尾部变更暴露的generator暂停/循环失败已定位到
  RecoverFinalizers：旧TailWithin只允许尾部Block/If，且固定alternate
  为RETURN arm。改为共同AST Euler祖先区间，实际原位的labelled block
  break，以及原resume fallthrough与新鲜TreeFlow polarity的映射。
  不是扩大loop/try/opcode白名单，也没有删除原控制/operand/effect检查。
- BlockStmt支持LabelId；共同StatementChild接口复用于标签和completion
  祖先遍历。break只能退出真实祖先block/loop，continue仍只接受loop。
  语法校验拒绝缺标签、错目标、continue-to-block、function body label及
  try/catch/finally语法必需块上的标签。投影后重分配实际语法标签。
- 原preserving ReturnStmt及operand/origin保留在清理外的NEXT审核路径；
  清理原终止位置改为break，嵌套循环不会继续执行，中间finally仍可覆盖
  完成。逆投影核对目标/标签/origin/实际RETURN arm，再恢复原序列交给
  原SSA和control检查。不合成/读取调用者payload，不伪造VM指令来源。
- /tmp/abcd-generator-gaps-throw-backtrace-1.log与new-route-1.log记录
  原尾部拒绝和实际consequent RETURN；首次tests-1仍有两项失败，依据
  共同polarity修正后tests-2.log 3/3通过47.93秒。最终final-build.log
  37个相关目标单-j4成功；final-tests.log 77项中76通过、1失败，
  202.67秒。两项file_generator_return_suspend/loop_source通过实际
  暂停、返回/抛出覆盖、loop/nested-loop/post-loop和200/201/402对照。
  block target新增1/200/201/402结构/打印正例及非法transfer负例通过。
  self-review再补try/finally必需块的合法基线及非法标签负例，grammar-build
  单-j4成功、grammar-tests.log tree_structure_test 1/1通过。
- 唯一剩余file_generator_return_source失败于select-many的402规模，
  不是源码语义通过。resource-backtrace-1.log定位FamilyEnclosure的
  TreeStructure资源拒绝：entry预算56,482,117 bytes，enclosure入口
  剩2,712,854。默认64MiB累计准入包含重复借用树容量/已结束scratch，
  不等同进程峰值。本轮未抬阈值、删检查或修改正例。select-partial
  独立记录原进程SIGABRT及其此前输出的8份200/201 fixture；原native
  行为oracle验证这8份通过，不能冒充原12份或整个CTest通过。
- fresh build/stage-one/generator-completion-pass-1固定五输入全退出0，
  仍80complete/26incomplete/0failure/46no-module，无新完整项、回退或
  legacy writer。batch-1.log及syntax-1.log核对原index/manifest SHA和
  size，106份源码解析通过且与tail-completion-pass-1字节相同。
- author self-review核对分支polarity、真实祖先、nested finally退出、
  原节点/来源及label消费者；共同接口无新依赖，索引/标签O(S+C)。旧
  yield/guard同一源码序列包装、following placement、cloned completion
  精确身份以及semantic_locals/scope_facts初始化PC条件仍是实现缺口。
  数值/父构造器/Reference/phase/字段/签名与完整阶段一验收保持开放，
  Goal active。代理、全量测试、扩展语料、RSS/基准及DFX继续暂停。

### 2026-10-04：以共同语法投影恢复自然函数尾部

- 规则revision100。实际HomeModel表示RED复现callback/method末尾的
  return (void 0)。PureTailCompletion复用原module纯完成规则，检查实际
  function body末尾直接子语句的bare/typed undefined return；共同printer
  表示为自然结束，FunctionTree/IR/控制/原指令来源保持。更早/嵌套返回、
  symbol、call、sequence、await/yield和finally完成覆盖继续打印。
- 首轮值/iterator队列对照没有发现async-generator有操作数return的
  implicit Await差异。新增microtask marker RED复现completion提前；按
  ECMA-262 14.10.1保留所有这种return，即使operand是undefined literal。
  bare return没有这个等待。不是样本/PC/寄存器/块布局准入分支。
- 原return及可省略literal在closing block前保留同一零宽事件；family
  emission核对实际owner/body及prior sibling范围，serializer保留原node
  ID和tail_completion标记。module_completion旧标签及module末尾边界
  不变；static fields/implicit constructor沿用自己的映射约定。新检查
  每函数O(1)，无额外graph walk、依赖或未界定缓存。
- 新54组native callable fixture覆盖ordinary/arrow/async/generator/
  async-generator。2592行为场景、27项完成job顺序及queued iterator
  next/return/throw对照通过。HomeModel配对行为/style通过，保留callback
  空名称、arity、不可构造、getter/callee和hostile map receiver验证。
  模块/普通函数/async-generator位置序列化及原source receipt检查通过。
- /tmp/abcd-tail-completion-style-red.log、job-order-red.log分别记录
  实际表示及任务顺序RED。final-build.log首次广建发现tool仍引用旧
  header；同步公共位置serializer后final-build-2.log相关34目标单-j4
  成功。final-tests.log选择72项，69通过、3失败，148.25秒；不是全量
  测试通过。失败位于generator返回选择/暂停/循环override的FunctionTree
  构建，尚未进入printer，具体基线对照见下文记录。
- /tmp/abcd-tail-completion-baseline用HEAD tree_printer/family_emission/
  tree_position_json重新编译并替换archive/link对象，复用未变更的实际
  analysis/fixture，三项原模式均SIGABRT，typed failure分别22/3、5/9、
  5/9及member1与本轮完全相同。summary.json/build.log及独立stdout/
  stderr保留证据。该对照隔离本轮输出改动，不表示旧generator缺口已
  修复，也不把它们排除后改称72项通过。
- fresh build/stage-one/tail-completion-pass-1固定五输入均退出0，仍
  80 complete/26 incomplete/0 failure/46 no-module，无新完整项、回退或
  legacy writer。batch-1.log及syntax-1.log核对原index/manifest SHA和
  size，106份源码解析通过。48个完整和24个部分记录表示改善，原
  return (void 0)从1303处降至21处，保留真实控制/异步义务。
- author self-review检查共同尾部规则、async完成job、body边界、source
  occurrence及special projection；没有把终值对照代替任务顺序验证。
  旧未知父类rest/spread构造器的IsJSFunction/private-array协议与原生
  默认构造器仍不等价，保留缺口；严格VM数值标准没有因解释性问题改变。
  一般Reference/phase/字段/签名/父作用域及whole-stage验收保持开放，
  Goal active；代理、全量测试、扩展语料、RSS/基准及DFX继续暂停。

### 2026-10-04：以共同控制流顺序恢复构造器捕获的倒置布局

- 规则revision99。decoded完整构造器RED复现：super初始化块的地址高于
  closure创建块时，旧局部SSA以backward PC拒绝。改用共同Kahn正常图
  无环证明；原cell身份、实际allocation及无环境逃逸义务保持。类型化
  site rank表达可达图顺序，不表达支配或允许重排。捕获暴露按实际路径
  传播，Range/Derived读取查询PreservesRead；旧first_capture改为单纯
  unpreserved_capture见证，不再用其地址截断所有读取。
- DerivedProjection及DefinedCaptures复用共同rank，在一次indexed
  access扫描中证明没有后执行的非receiver/直接写入。FamilyTrees保存
  实际原block身份，以dominator interval证明跨块初始化；仅同块比较
  真正的store/creation先后。没有增加样本、opcode序列、固定PC、块布局
  或producer白名单。候选不等于创建许可，分支绕过store仍不能省略child
  guard；混合分支的旧断言已改为核对实际family拒绝。
- 两种相同执行、不同地址的fixture分别把closure放在super store前、
  把初始undefined store放在super/closure/return后。三profile、直接和
  转发arrow、有无guard均产生完整共同FunctionTree源码。没有保留证明
  的raw局部SSA仍承认实际创建前的高PC读取，拒绝实际创建后的低PC读取。
  1/200/201/402规模及精确derived-value work边界通过；可写/逃逸cell、
  早创建、绕过初始化、错误guard及store/origin反馈保持负例。
- /tmp/abcd-receiver-layout-red.log记录原拒绝。final-build.log相关18
  目标单-j4成功；final-tests.log 41/41通过83.10秒。test-output.log
  保留完整输出，八项新增native行为测试各2268场景，共18144追加场景，
  包括即时setter、Proxy替换receiver、getter顺序、callee替换、poisoned
  call helpers、noncallable及任意抛出值；已有实际配对模块行为检查通过。
- fresh build/stage-one/receiver-layout-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无新完整项、回退或
  legacy writer。batch-1.log与syntax-1.log核对原index/manifest SHA和
  size，106份源码语法通过，源码字节与native-function-name-pass-1相同。
  本次增加通用布局覆盖，没有宣称实际26个缺口减少或全部独立验收完成。
- author self-review跟踪全部旧first_capture消费者，并同步替换其潜在
  PC顺序安全前提。DAG suffix是充分证明；互不支配写入的精确创建点
  cell快照、循环/异常activation transfer仍开放。审计另定位
  semantic_locals和scope_facts中的初始化跨块PC比较，见规则99审计。
  一般数值域、未知父类及此前Reference/phase/字段/签名缺口保持开放。
  阶段一未完成，Goal active；代理、全量测试、扩展语料、RSS/基准及DFX
  继续暂停。本次没有调整严格VM数值等价边界。

### 2026-10-04：按实际源码上下文恢复原生匿名函数表示

- 规则revision98。实际HomeModel配对表示RED复现调用参数被强制打印为
  computed-property命名包装。共同printer从FunctionTree的直接语法所有权
  计算NameContext：声明、identifier赋值和对象数据属性推断名称；调用、
  返回、数组、成员赋值、sequence/conditional/logical操作数不推断名称。
  比较实际目的名称与原runtime name，仅在相等时使用原生匿名语法，不添加
  内部具名绑定。原self创建envelope和arrow-method arguments carrier保持。
- wrapper额外分配校验移至最终命名语法的preflight。原生名称匹配的V1
  函数族不再因inferred_name元数据被拒绝；真正需要包装的V1仍在输出前
  拒绝。名称是语言可观察的属性及文本边界，未用函数名选择恢复算法，也
  未加入样本、指令序列、块布局或表达式形状准入条件。
- 新增648组printer/native行为对照：6种普通/arrow/async/generator
  callable、9种表达式位置、普通/__proto__/换行属性名和4种原名称。
  验证name descriptor、arity、prototype、构造行为、async拒绝、generator
  yield/next/throw及重复创建身份；可变外部chosen捕获对照排除内部self
  遮蔽。实际HomeModel通过getter顺序、callback空名称/arity/不可构造和
  hostile map thisArg检查，保留独立配对源码行为对照。
- 1/200/201/402成员规模及精确work边界通过。整体printer work为
  253/35949/36132/73353，计费scratch为7449/524132/526621/1048414；
  不代替全流程RSS或基准。新增命名扫描复用既有owner二分索引，按语法
  occurrence扫描，没有逐变量/绑定的子树或CFG遍历。
- /tmp/abcd-native-name-red-test.log复现实际表示RED。final-build-2.log
  相关33目标单-j4成功；final-tests-2.log 50/50通过98.53秒，具体规模
  及行为证据见test-output.log。首轮52项中created_function_test及其JS
  包装测试失败于旧寄存器表示断言；使用HEAD0304a097原测试和原printer
  独立对照仍失败于183。临时表示诊断进一步复现legacy LeafSources
  kCreation拒绝，原printer亦相同。最终未修改这两项旧测试，不计为通过；
  baseline/original-test.log、test.log和created-backtrace-2.log保留证据。
- fresh build/stage-one/native-function-name-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无回退、新完整项或
  legacy writer。46个完整及23个仍incomplete的记录表示改善；空函数名
  包装从732处降至0。batch-1.log与syntax-1.log核对原index/manifest
  SHA和size，106份源码解析通过；这不表示所有记录已完成独立语义验收。
- author self-review检查直接命名上下文、实际名称、自绑定/arguments、
  唯一函数所有权及预输出资源校验。default export直接function/class
  initializer的命名投影仍是独立缺口，未放宽其既有检查。一般数值域、
  未知父类协议、inline-owned Reference、module guard邻接、相位corridor、
  一般字段/签名/父作用域及CanRetainPrefix边界仍开放。阶段一未完成，
  Goal active；代理、全量测试、扩展语料、RSS/基准及DFX继续暂停。

### 2026-10-04：以 SSA 身份消除原生类的丢弃 prototype 读取

- 规则revision97。实际六个配对模块的表示RED显示，未使用的.prototype
  读取迫使类保留carrier及computed NamedEvaluation包装。共同分析先新鲜
  验证availability/uses，再复用AnalyzeValueIdentities的copy/phi SCC；
  记录原read、ClassCreation及计算键的String producer。常量边界单独
  核对builtin prototype，不以类名、样本、PC、寄存器或同块决定恢复。
- pinned runtime f04900cf的class_info_extractor安装不可写/不可配置的
  自身prototype并在发布前设置ProtoOrHClass；JSFunction::PrototypeGetter
  返回已有对象。实际ClassTree/layout及原创建身份仍须通过最终检查。
  因此未使用读取不需要escape或效果corridor限制；活跃结果、普通对象、
  未知键/混合phi和用户getter保持原操作。已有方法安装/初始化效果合同
  不放宽。计算键来源精度不足时保持读取，不拒绝整个函数。
- EvaluationPlan保留条件事实，独立验证重新分析实际图并核对原owner；
  TreeOperands核对原常量键与实际ClassTree.operation。投影后沿用既有
  表达式所有权及NamedEvaluation，实际模块输出let HomeViewModel = class
  等原生绑定；不增加内部具名class，以免改变可变模块绑定的解析。
- 新增三profile、15种情形、1/200/201/402规模的180组解码测试，涵盖
  split/diamond/loop、copies/phi、逃逸及其他效果、named/computed键、
  unknown/mixed输入、活跃结果和错误键。伪造创建/key/重复事实及owner
  拒绝，精确work边界通过。named work557/46128/46357/92386，computed
  545/43728/43945/87562，phi1163/164958/165781/331204；这些是共同
  发现及投影合计work，不代替暂停的RSS/基准。
- 完整file/FunctionTree/printer增加named与computed丢弃读取行为对照，
  变造源键及ClassTree身份拒绝。实际六个ViewModel/CardItem/TabBar模块
  通过新增表示断言及原配对行为对照，包含prototype descriptor不可变
  检查。配对TS stripper的字段枚举/编译策略限制保持，不宣称全类行为
  验收。原initializer135+135、frame270、body1620、observer432及
  capture96对照保持。
- /tmp/abcd-class-prototype-red-test.log复现实际HomeViewModel表示失败；
  build-1生成引擎后因Make重载时机未执行新目标，build-2顺序补建成功，
  build-3全部相关30目标单-j4成功。tests-2.log 45/45通过95.33秒，
  test-output.log保留具体对照及work。author self-review检查SSA、键、
  ClassTree条件证明与独立验证边界；未使用代理或独立reviewer。
- fresh build/stage-one/class-prototype-pass-1固定五输入全退出0，80
  complete/26 incomplete/0 failure/46 no-module，无回退、新完整项或
  legacy writer。23个完整及12个仍标记incomplete的记录表示简化；
  batch-1.log与syntax-1.log核对index/manifest原SHA/size，106份源码
  解析通过。阶段一语义和表示双验收未完成，Goal active。
- CallPlan的inline-owned Reference接口、module guard邻接、相位选择
  corridor、一般字段/签名/父作用域及单块CanRetainPrefix边界仍开放。
  继续禁止追加样本/布局匹配；没有把这些实现边界当作语言不变量。
  full-suite、扩展语料、RSS/基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：字面量来源与无依赖纯值脱离物理块顺序

- 规则revision96。LiteralClassFields用SSA OpResult及透明copy的迭代memo
  替换String/copy物理扫描；每个值最多展开/回填一次。原首/后效果、
  receiver、字段descriptor、可用性和undefined正常返回合同保持。
  不把String域、receiver、未知入口或phi当作字面量来源。一般RHS、phi
  来源和phased字段仍开放；没有追加opcode序列、样本或块布局匹配。
- 完整逆序字符串文件RED进一步暴露普通流强制局部存储和Forest物理
  构造顺序。共同SourceEvaluation::kPure、无依赖/effects/异常/隐式
  上下文及Language结果给出closed pure事实；EvaluationPlan记录确切
  唯一消费UseId，独立验证实际census/可用性/源语义。它与logical/
  phased所有权分开，Forest按原constant/origin预构造一次，没有重读
  getter/this或改变receiver。不为一般依赖表达式授予移动权限。
- 新增36组三profile真实字节/file/context/FunctionTree/family/printer
  用例，含外部split/diamond/reverse、正文逆序string、ABI/frame来源及
  captured class self；540组独立native行为对照通过。总body模式108组/
  1620对照，原135+135 initializer、270 frame、432 observer及96 capture
  对照保持。验证值、字段顺序/描述符、class/prototype身份、写后行为和
  factory独立性，输出原生static字符串字段，未以INCOMPLETE计成功。
- 数字/字符串复制链正向与逆序覆盖1/200/201/402及精确work边界，402
  work小于201的三倍；string work328/14855/14928/29601，数字332/
  14859/14932/29605。缺失/错误消费证书、HeapRead来源、隐式frame读取
  及Unknown域反例拒绝；原bypass、divergence、重复字段、barrier、错误
  receiver/descriptor、私有tagged NaN负例保持。不代替暂停的RSS基准。
- 证据前缀/tmp/abcd-literal-origin-：unit-red-test.log在新鲜EffectOrder
  成功后复现逆序copy kUnsupported；red-test.log复现完整string文件
  拒绝。中间组件GREEN仍有完整文件失败，补齐上述共同表示后才通过。
  测试整数类型歧义经完整qualification修正；首40项38通过，新增错误
  use变造实际未变位置、旧phased expression范围要求多余括号，均修正
  为真实变造/原生调用范围。final-build-4.log单构建-j4退出0；最终
  final-tests-2.log 40/40通过58.90秒，test-output.log保留全部对照。
- fresh build/stage-one/literal-value-origin-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无新增完整项、
  完整回退或legacy writer。batch-1.log与syntax-1.log核对index/manifest
  原SHA及size，106份源码解析通过，与规则95字节一致。仍未取得普通
  参数/属性/宿主返回算术的安全数值域事实，也未改变未知父类super协议。
  阶段一语义与表示双验收未完成，Goal active。
- 按本次用户审计继续登记CanRetainPrefix单块normal限制为实现边界，
  后续按共同控制/完成与缺口归属替换；不扩充局部布局模板。非inline
  callee Reference存储、一般字段/表达式、phased与作用域接口及旧协议
  快路径仍未全闭合，不宣称逐分支全库审计完成。full-suite、扩展语料、
  RSS/基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：字段初始化正文以共同效果链取代单块门槛

- 规则revision95。LiteralClassFields删除单块/无边限制及按物理遍历遇到
  return后禁止后续field的检查，消费共同EffectOrder的原前驱身份、支配、
  必达首效果、下一效果和normal return事实。返回实际执行顺序的Fields，
  不按OpId/PC排序。每条正常路径须完整执行同一字段链一次；绕过、重复、
  分支效果和前后divergence仍拒绝，没有新增split/diamond/reverse模板。
- 共同逆向must索引区分exact OpId、NormalReturn及未证明endpoint。
  不同原return可以同意正常完成，但原返回值由消费方另查；此证书要求
  undefined。纯循环不因存在出口就获终止证明。空入口reverse RED暴露
  首物理op不等于入口控制位置，现从同一方程给出EffectOrder.first。
  ReceiverEntry与FrameThis仍保留各自原读取，this初始化/字段descriptor
  和真实initializer owner义务不变。
- 72组三profile真实字节/file/context/FunctionTree/family/printer用例，
  含外部split/diamond/reverse、正文split/reverse、ABI/frame两种来源及
  captured class self；新增1080组独立native对照。恢复两个原生static
  字段，验证定义顺序、值、描述符、class/prototype身份、写后结果与
  factory独立性。270组frame、135+135组initializer、432组observer、
  96组capture原对照继续通过。没有用拒绝或INCOMPLETE计完整成功。
- 五种组件布局含空入口逆序、纯菱形和分支不同return，均覆盖1/200/201/
  402、原字段顺序、精确work边界和几何增长；5组bypass、前后纯循环、
  重复field及debugger反例拒绝。split work190/14319/14390/28661，
  diamond337/14466/14537/28808，空入口181/14310/14381/28652。
  这不代表全部菱形native字段投影完成，也不是暂停的RSS/延迟基准。
- /tmp/abcd-field-body-保存RED/GREEN及最终证据：首完整文件先成功打印
  ordinary两个字段，再在self正文证书失败；组件原EffectOrder通过，
  LiteralClassFields报kUnsupported。夹具两次类型/成员编译错误纠正后
  才计RED。空入口count200另经RED→GREEN。final-build-2.log单构建-j4
  退出0；final-tests.log最终22/22通过36.59秒，含真实Immersive字段与
  accessor行为回归；test-output.log保留work和native对照。
- fresh build/stage-one/field-body-order-pass-1固定五输入全退出0，80
  complete/26 incomplete/0 failure/46 no-module，无完整回退/新增或legacy
  writer。batch-1.log和syntax-1.log核对原index/manifest SHA及size，106
  份源码解析通过，与规则94字节一致；阶段一语义和表示双验收未完成。
- literal RHS事实的局部String/copy扫描及更一般表达式、phased字段、
  多次/保留receiver存储的native投影、条件字段与初始化器作用域仍开放；
  本次没有以首效果或return计数替代这些语义/表示义务。Goal active。
  full-suite、扩展语料、RSS、基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：字段 receiver 共用来源接口并保留跨块原读取

- 规则revision94。IsReceiverOperand共用typed来源查询：实际ReceiverEntry
  和已证明Language的ValueFrameThis分别保留原SSA身份；共同字段识别与
  LiteralClassFields消费同一接口。原callable owner证明this初始化，不能
  从ABI槽、LDTHIS出现或样本正文推断；不把frame read别名成entry快照。
- 新增跨块源树用例发现FieldDefinitionExpr结构检查强制直接ThisExpr，
  拒绝共同BindingPlan保留的原receiver。现由共同operand/origin/flow验证
  原读取与存储身份；结构检查只管合法树边，避免为了内联重读this。
  static打印仍单独关闭implicit receiver上下文义务；需存储的receiver
  尚不能投影成native static syntax。字段描述符没有替成普通assignment。
- 十八组三profile真实字节/文件/initializer owner/FunctionTree/family/
  printer用例，含外部split、diamond、逆物理布局及真实class self捕获；
  新增270组独立native JS对照验证字段描述符、类/原型身份、写后结果与
  factory独立性。原135+135组初始化器、432组observer及96组capture
  对照继续通过。原frame读取在脱离真实owner时仍报InternalValue。
- 两种receiver来源的1/2/200/201/402字段证书、精确work边界与几何
  增长通过。frame work39/50/2228/2239/4450，entry work36/47/2225/
  2236/4447。Unknown、错FrameFunction来源、receiver充当literal RHS、
  错this owner/参数身份和assignment描述符变造继续拒绝。规模检查是
  组件证据，不是多字段完整native表示或暂停的RSS/延迟基准。
- /tmp/abcd-frame-receiver-保存构建/验证：首轮跨块夹具的错误successor
  ordinal修正后，实际树语义拒绝暴露上述直接ThisExpr结构门槛。中途
  诊断编辑的编译错误已纠正，失败不计成功。final-build-2.log单构建-j4
  退出0；final-tests-2.log最终15/15通过23.51秒，test-output.log保存
  work与native对照。没有运行全套、扩展语料、RSS或基准。
- fresh build/stage-one/frame-receiver-fields-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无完整回退或legacy
  writer。batch-1.log/syntax-1.log核对原index/manifest SHA及size，106份
  源码解析通过，与规则93源码字节一致；本次没有新增complete record。
- LiteralClassFields的单块/literal-only快速路径、多次/保留frame读取
  的native字段投影、phased字段和一般cell生命周期仍开放。module guard
  邻接、inline-owned Reference、phased多RHS/入口及父scope/capture等
  已登记，不扩充局部模板。阶段一语义与表示双验收未完成，Goal active；
  full-suite、扩展语料、RSS、基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：class self 初始化贯通共同效果顺序、原捕获和函数族写入

- 规则revision93。FindClassSelfStores删除allocation/creation/store/call
  同块与局部order比较，删除首次store前的效果数量比较。共同EffectOrder
  证明SSA/dominance及allocation→creation→store的原效果身份/全路径必达；
  call须支配所有原cell读取。早期观察/发布、夹杂效果、divergence仍拒绝。
  原self cell store保留在父FunctionTree，没有替成immutable named class。
- ClassSelfOrder仍验证真实同文件初始化器正文及LiteralClassFields，新增
  原class capture occurrence证书，绑定input/working arena和revision。
  ScopeFacts仅对这个真实capture消费Language事实；Defined和未知child
  writes不因此放行。完整构造器捕获真实父cell，BuildRecordFamilyTrees
  闭合实际原store/后代write前提，再打印共同源树。
- 完整用例暴露FamilyWrites未登记已准备initializer method的原barrier。
  现从实际ClassExpr的initializer context/body共用operation-owner索引，
  核对原method occurrence/identity/目标body，而不是按opcode串或布局猜测。
  缺initializer_site、错body method和错call method共27个变造拒绝。
- 逆物理布局暴露ProveLocalAccesses以较小target block编号代表真实cycle。
  现复用已有OrderAcyclicBlocks判定真实无环图，再使用既有支配初始化路径。
  Kahn排序O(B+E)、scratch O(B)，失败cycle也charge遍历上界。一般cell
  PC扫描及循环activation限制尚未全部迁移，已登记为实现缺口；没有加入
  新的物理布局分支或图形模板。
- 三profile、1/200/201/402规模：84组新增跨allocation/store/call分块及
  菱形、晚期cell读取正例，9组早期观察/divergence/分支效果反例。self发现
  精确work边界及几何增长通过；1206拆块work737/107951/108491/217177，
  allocation diamond为1109/180361/181263/362711。不是暂停的RSS或基准。
- 九组完整真实字节/文件/context/capture/FunctionTree/family-write/printer
  用例含split、diamond及method物理晚于call的布局。新增135组独立native
  JS对照，验证constructor返回实际外部cell中的class、跨factory身份、
  static字段描述符、写后实例返回和其他factory独立性。原135组initializer
  与432组class observer对照继续通过，不用拒绝或INCOMPLETE计成功。
- /tmp/abcd-self-order-保存验证：早期unit未提供条件closure-write前提，
  明确拒绝；完整family验证最初报kCoverage，修复实际initializer barrier
  遗漏后通过。完整逆布局最初被原forward检查拒绝，复用拓扑排序后通过。
  两次批量build的目标名record_default_tree_test/cleanup_flow_test不存在，
  退出2；纠正为实际module_default_export_test/environment_cleanup_test。
  final-build-3.log单构建-j4退出0；final-tests.log最终20/20通过30.61秒，
  含作用域/capture/store/module guard/alias/default/cleanup相关回归。
  test-output.log保留work、135组新增native对照和既有输出。
- fresh build/stage-one/self-effect-order-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无完整记录回退、无
  legacy writer。batch-1.log和syntax-1.log核对原index/manifest SHA及size，
  106份源码解析通过，源码与规则92字节一致。本次扩大共同布局覆盖，没有
  新增真实complete record；阶段一语义与源码风格双验收仍未完成。
- shape审计继续开放module guard邻接、inline-owned Reference、phased
  单RHS/入口、父scope/capture、literal字段单块和一般cell生命周期；禁止
  新增样本/名字/PC布局/数量匹配规则仍生效。Goal active；full-suite、扩展
  语料、RSS、基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：类初始化调用以共同 SSA 与效果必达事实替换同块门槛

- 规则revision92。FindClassInitializerCalls删除creation/method/call同块及
  局部order比较，以fresh VerifyValueFlow证明原home/callee/receiver使用处
  可用性。ProjectClassMembers删除creation/call同块、物理OpId先后及
  prefix效果数量比较，复用BuildEffectOrder的前驱身份/dominance与新增
  after全路径下一效果索引。纯块拆分、菱形分支及逆物理布局共享同一通路。
- 原creation到initializer call必须形成唯一私有效果链，且每步所有路径
  必达原下一效果。绕过call、可能无限循环、重复执行call及分支夹杂效果
  不能挪入class syntax。所有非私有class观察须由call支配；phi按实际
  incoming edge检验，已证明self store保留实际cell写入。getter/prototype
  名称、actual child body、DefineOwnProperty、scope和receiver义务未删除。
  没有加入样本/名字/PC布局/指令序列或更多局部图形匹配。
- 三profile、1/200/201/402规模，48组新增跨creation/call拆块及菱形
  解码/投影正例，12组绕过/不终止/重复调用/不同分支效果屏障反例。
  发现与投影分别验证精确work边界。1206拆块projection work为
  449/61893/62203/124659；菱形为717/114100/114671/229588，
  201及402增长断言通过。不是暂停的RSS或完整延迟基准。
- 新增九组三profile真实字节/文件/类上下文/FunctionTree/printer正例，
  包括method物理排在call之后但CFG先定义再调用。生成完整native
  static _value = 42，经独立oracle做135组空值/Boolean/number/BigInt/
  Symbol/对象输入、类/原型身份、字段描述符及写后独立性对照。
  完整源码进入对照；没有以拒绝、INCOMPLETE或返回参考源码计成功。
- /tmp/abcd-class-order-日志保留过程：初版新增菱形夹具未重新读取home/
  callee，unit在candidate断言退出134；修正夹具真实accumulator后通过。
  首版完整树夹具使用LDTHIS，现有IsReceiverField接口只接受实际ABI
  ReceiverEntry，得到EvaluationError::kUnsupportedOperation；改用固定
  profile ABI实际receiver读取后通过。这个FrameThis统一身份接口仍有
  覆盖缺口，未向生产增加LDTHIS局部序列特例。诊断不计入验证通过。
- final-build.log及final-build-2.log均单构建-j4退出0，重链实际
  abc_structure/abc_semantic和相关类/初始化/字段/cleanup targets。
  tests.log首轮17/17通过256.58秒，含joint-selection及selection-module
  原语义/Reference回归；追加逆物理布局、work增长断言并释放只用一次的
  SSA证明scratch后，final-tests.log最终13/13通过22.65秒。
  test-output.log保留最终work、135组新增对照及432组既有class observer输出。
- fresh build/stage-one/class-effect-order-pass-2固定五输入全退出0；
  80 complete/26 incomplete/0 failure/46 no-module，无完整记录回退或
  legacy writer。batch-2.log/syntax-2.log核对原index与manifest SHA/size；
  106份源码全部解析，与规则91源码字节一致。此项修复扩大通用布局覆盖，
  未增加真实输入complete record，阶段一双验收仍未完成。
- shape审计额外登记class_self同块/效果数量、literal_class_fields单块
  快速路径及单initializer表示接口；module guard邻接、inline-owned
  Reference、多copy RHS、phased parent scope/capture等尚未全部迁移。
  Goal active。full-suite、扩展语料、RSS、基准、DFX、代理及周期审计继续暂停。

### 2026-10-04：保留值选择以共同真值方程恢复 logical 表示

- 规则revision91。SelectionLogical只投影既有已验证SelectionRegion的表示，
  原consequent/right、phi原输入、control、Reference和exception staging不改写。
  ProjectSelectionLogicals按typed UnaryOperator/ValueBranch语义组合极性，
  沿实际inline consumer森林一次索引纯truth方程；与保留分支的canonical
  SSA身份相等时生成&&/||。不要求指令邻接、同块、固定链深或条件数量；
  raw VM zero/nonzero谓词只有Boolean输入才具有该truth等价。
- 原始UseId随方程保留，原first saving use仍执行PredicateSnapshot赋值。
  有外部观察的Boolean结果、其他效果/异常/环境事件不能随表示投影消失；
  ordered prefix、receiver、TDZ、原phi输入及来源仍由共同树通路分别校验。
  ExpressionForest不生成被投影的中间truth树，原操作来源共同归属native
  logical根；TreeFlow独立复算投影并核对实际输入、保存位置和原边，operand/
  origin检查继续验证，不以打印文本回推语义。
- 三profile、1/2/200/201深度共120组新增完整解码/draft/树用例，包括纯
  truth链、NOT、重复分块测试、非Boolean VM谓词和额外property观察。
  后两类保留ConditionalExpr/原Boolean计算，不用拒绝冒充完整组合成功。
  wrong operator/交换输入变造仍拒绝。50/100/200/201/400重复条件的draft
  work为514547/977270/1902979/1912237/3754389，几何增长通过。
- /tmp/abcd-logical-truth-red-build.log、abcd-logical-truth-red.log及
  abcd-selection-truth-日志记录边界：
  旧实现在native logical表示断言退出134；第一版漏branch自身predicate
  极性，表示断言又拒绝；修正后native对照通过。首轮fixed-five在DetailPage
  出现complete回退，查明是丢掉first saving occurrence；保留原UseId并贯通
  保存赋值后回退消失。测试夹具另修正raw positive predicate缺number文本、
  重复条件control计数、旧强制conditional断言及临时输出目录已存在的问题。
  这些失败未计入通过，临时诊断已移除，没有生产样本/图形匹配。
- final-build.log、saving-build.log、final-build-2.log均单构建-j4退出0；
  最终重链14个相关target和实际abc_structure/abc_semantic。
  verified-tests.log最终32/32通过163.99秒，test-output.log保留完整输出。
  包括joint解码/独立行为、模块/guard/literal/异常/引用、logical/eager
  snapshot、保留输入、pasteboard callback和既有PageA/ProgressBar/
  Backup/VoIP/Home配对检查。模块选择native扩至10162组；新增
  selection_truth_records_source_test完整导出ABC后才读取配对源码，比较
  DetailPage.isShow与itemHeader的1390组动态getter/精确receiver/闭包标题/
  延迟callback/任意throw场景；不调用其constructor，不宣称平台验收。
- fresh build/stage-one/selection-truth-pass-2固定五输入全退出0；80 complete/
  26 incomplete/0 failure/46 no-module，无complete回退或legacy writer。
  batch-2.log/syntax.log核对原index/manifest的SHA与size，106份源码全部解析。
  MultiCommunity的25350、48379两个complete record及24096一个partial
  record改变表示，前两者相关方法新增独立配对；不据此宣称全部方法或
  partial record运行通过。完整record数量没有增加，阶段一双验收未完成。
- shape审计补充当前仍有inline-owned Reference、single-entry candidate/
  prefix、多份真实copy RHS、class initializer同块、module guard邻接、
  phased parent scope/capture和legacy模板。沿同一通用模型替换，不再追加
  局部形状快路径。Goal active；full-suite、扩展语料、RSS、基准、DFX、
  代理和周期审计继续暂停。

### 2026-10-04：模块选择参数删除 producer-specific pin

- 规则revision90。直接受TDZ检查的模块值作为conditional参数曾命中
  kReference；实际IndexPhiExpressionInputs已经给出原UseId候选，但
  BuildEvaluation和VerifyEvaluationForFlow又把所有phased ValueModuleRead
  一律pinned，使共同原边证明不能被消费。删除这层producer-specific准入，
  同时删除inline verifier中用于绕过该pin的module类型例外。
- 沿用共同观察普查、phi原边/all-path尾部、选择参数、Reference和树通路，
  没有新增direct/property、样本、opcode序列、寄存器/PC或layout匹配。
  IndexModuleEvaluations仍核对模块绑定/原guard输入和原phase order；
  VerifyModuleCatchCopies、exception staging、原effect/operand/scope树验证
  保留。实际多次观察或不同catch签名继续保留snapshot；不假定模块已初始化。
- selection_module_test新增三profile、1/2/200/201规模、conditional/and/or
  和direct/property共72组完整解码/draft/树正例，另18组分支debugger效果
  屏障拒绝。缺phase_calls记录、删guard、变造原树guard归属仍拒绝。
  独立native模块对照扩至9252组，覆盖原callee/getter替换、receiver、动态
  模块绑定、精确身份、任意throw/catch、非callable、TDZ和未取分支。
  短路行为通过；phased and/or仍以ConditionalExpr打印，惯用logical形式
  尚未统一，不把行为通过说成该表示质量验收完成。
- /tmp/abcd-module-input-日志：red-build.log退出0，red.log在旧实现
  kReference正例断言处退出134；diagnostic-build/log确认有原phi候选却
  未被消费，临时诊断已移除。build.log退出0；final-build.log及
  final-build-2.log均单构建-j4退出0，完整重链相关12target，最终包含
  rules90、新增正/负矩阵与实际abc_structure/abc_semantic工具。
  final-tests.log最终28/28通过143.24秒；test-output.log保存完整输出，
  包含模块/初始化/异常/引用unit、435组joint解码、177984组joint native、
  模块literal、phased选择/保留输入、pasteboard callback和PageA/
  ProgressBar、Backup、VoIP、Home的独立配对检查。未恢复full-suite、
  扩展语料、RSS、基准、DFX、代理或周期审计。
- fresh build/stage-one/module-value-phi-pass-1固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无complete回退或
  legacy writer。batch.log/syntax.log核对SHA/size与原index/manifest一致，
  106份源码全部解析，源码字节与revision89一致；没有新增真实完整record。
  直接模块选择参数缺口关闭，但single-entry候选、inline-owned Reference、
  多份真实parallel-copy RHS、class initializer、phased scope/capture及
  module guard邻接接口仍开放。未知算术域和导入super协议缺口仍在，
  阶段一语义/风格双验收未完成，Goal active。

### 2026-10-04：共同 SSA 身份投影消除冗余 phi/copy 屏障

- 规则revision89。AnalyzeValueIdentities复用迭代Tarjan，对原phi及纯copy
  方程证明单一外部ValueId锚点；多锚点、无锚点或不相容域不折叠。普通
  SimplifyPhis继续只改phi方程，source使用只读索引。原phased SSA和全部
  UseId保留；catch绑定/异常staging、真正mutable phi首次快照仍独立。
- PhiCopies保存原复制全集与typed projection，独立复算身份后仅安排实际
  executable schedule；expression_input记录唯一剩余原UseId。normal边
  屏障、选择发现、Reference corridor、BindingPlan和树flow统一消费投影，
  不再以冗余phi的raw count挡住arm自复制。多份真实不同值的parallel-copy
  RHS共同安排仍开放，不能把本次说成删除所有single-copy表示边界。
- IndexEvaluationUses一次普查canonical原观察，保留穿过身份phi/copy的
  最终真实使用，排除已证明搬运，替换逐producer原use-chain计数。Builder
  和verifier分别复算；module/literal/class/cell/receiver义务继续保留。
  没有新增样本路径、名字、寄存器/PC布局、局部opcode串或producer名单。
  原callee/base/receiver、getter一次求值、throw/effect/scope仍独立验证。
- joint_selection_call_test三profile435组解码矩阵通过，覆盖base/callee
  arm内MOV自身、两侧getter、prefix/outer member Reference、多参数、
  1/2/200/201/402复制及200/201边界。混合外部值、实际receiver改变、
  debugger、反向参数、alias/projection变造及精确配额负例保留。evaluation
  work为625937/1234982/2453012/2465192，几何增长通过。完整生成源码
  177984组独立行为对照、36项有效mutation通过；没有用拒绝代替正例。
- 旧phased_selection_reference_refusal_test期待普通conditional模块属性
  参数必拒绝，现在该函数生成完整draft/原生MemberCall并通过树语义/flow；
  改名phased_module_selection_test。新增1/2/200/201和三profile正例，
  selection_module.mjs扩至5397组effect/receiver/identity/throw/TDZ对照，
  包含getter替换、模块绑定变化、非callable、未取分支和caught异常。
  补测direct module值选择参数仍命中kReference；该路径未通过，不作覆盖
  声明，也没有增加生产direct/property匹配。待贯通原guard/参数接口。
- /tmp/abcd-phi-identities-日志：red-build.log退出0，red.log在旧实现
  命中arm自复制phase_calls数目断言；build.log重建核心退出0。
  final-build.log曾因测试variant equality不可编译退出2，修正测试的
  ValueId比较后final-build-2/3/4.log均退出0，单构建-j4。
  final-tests.log前置19/19通过121.27秒；最终verified-tests.log有26项
  通过及1项selection_module_test失败108.10秒，该失败是补测仍不支持的
  direct module路径。明确收窄新增正例至已证明的module-property参数后，
  module-final-build.log退出0，module-final-tests.log 8/8通过78.07秒，
  其中包括完整selection解码矩阵及新增native模块对照。最终合计28项
  不同相关检查通过；失败和旧二进制结果不充当成功证据。随后native对照
  明确区分catch返回与异常外泄，module-catch-tests.log另1/1通过，仍为
  5397组；不重复计入28项。
- fresh build/stage-one/value-phi-identities-pass-2固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无complete回退或
  legacy writer。final-batch.log/final-syntax.log核对SHA/size与原index/
  manifest一致，106份源码全部解析。与revision88相比7个complete record
  和1个partial record减少无用await结果快照；其中pasteboard PageA/
  ProgressBar、push Backup class/VoIP及Home实际配对行为检查通过。
  不据此宣称全部受影响方法、HarmonyOS主机或源码风格验收完成。
- shape审计和algorithms同步说明readonly SCC、原输入全集、canonical
  census及成本边界。inline-owned Reference、single-entry候选、class
  initializer同块、phased scope/capture/module和legacy模板仍开放；未知
  算术域和导入super协议缺口仍在，阶段一语义/风格双验收未完成，Goal active。
  full-suite、扩展语料、RSS、基准、DFX、代理和周期审计继续暂停。

### 2026-10-04：computed phi 跨纯控制尾部复用全路径证明

- 规则revision88。PhiExpressionProducer的computed Commit邻接及
  primitive-literal-only尾部准入删除。普通与phased共用
  IndexPhiExpressionInputs：一次原UseId观察普查、原normal-copy边
  endpoint索引与反向全路径worklist，不逐参数/phi重扫尾部。typed
  EvaluationShape区分normal control；没有新增opcode串、producer名单、
  固定前驱或图形条件。原module guard/literal所有权、单次观察、throw
  staging、最终树operand/effect/protected-handler scope核对继续保留。
- joint_selection_call_test扩至三profile216组解码矩阵，覆盖双getter、
  1/2/3/50/100/200/201参数、prefix/outer member Reference及复制规模；
  实际draft/树flow及精确配额负例通过。joint_selection_call.mjs有88992组
  生成源码行为对照和18项有效mutation，保留receiver、getter替换、每个
  参数的独立选择、精确身份、throw与noncallable时机。phi_copies_test
  新增正常图branch/merge、1/200/201/402块尾部及divergence、早退、
  debugger、额外观察和中间复制拒绝。evaluation work为
  574601/1133946/2252576/2263762，几何增长通过。
- 初始双getter加arm内MOV自身夹具命中phase_calls数目断言；诊断发现
  bundle实际有两份SSA phi复制，除邻接外还触发既有single-copy候选限制。
  移除这个独立arm复制变化后双getter通过，branch前复制规模变化仍测。
  该初始失败不能作为单独邻接RED证据。多parallel-copy RHS共同安排尚未
  贯通，作为审计缺口保留；不是去掉count检查就获得赋值顺序证明。
- /tmp/abcd-phi-tail-final-build-2.log单构建-j4退出0，重链相关六target。
  初次final-build.log因不存在的phased_call_selection_test target退出2；
  正确注册的source test使用selection_module_test，已纠正并完整重建。
  initial-tests.log 3/3通过119.11秒，含完整selection_module_test矩阵；
  final-tests.log 12/12通过69.16秒，含新phi unit、joint unit/native、
  phased选择/保留输入/module、pasteboard callback、三项phased literal
  和Home配对检查。voip-tests.log另1/1通过0.44秒，有300组独立配对
  VoipCallService行为对照。合计14项不同相关检查，不计失败或旧二进制。
  test-output.log保存最终12项详细输出。未恢复full-suite、扩展语料、
  RSS、基准、DFX、代理或周期审计。
- fresh build/stage-one/value-phi-inert-tail-pass-1 固定五输入全退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无complete回退或
  legacy writer。batch.log/syntax.log核对SHA/size与原index/manifest一致，
  106份源码全部解析。一份真实push VoIP record11989的双分支属性读取
  从if/额外快照恢复为ConditionalExpr；其余源码字节与revision87相同。
  这是源码表示改进，没有新增完整record，不等于主机运行/全面风格验收。
  MemberCall inline-owned表示、single-copy/single-entry候选、initializer
  同块、phased scope/capture/module及legacy模板仍开放；未知算术域和
  导入super协议缺口保留。阶段一语义/风格双验收尚未完成，Goal active。

### 2026-10-04：同一成员调用的选择参数联合恢复

- 规则revision87。删除phase_selection_calls的phi_arguments和
  root_selections各为1门槛，按实际call/argument UseId联合规划全部选择，
  分段检查callee Commit、各header/join和最后call。共同ConditionalExpr、
  MemberCall、ExpressionForest/TreeFlow与printer直接消费这些记录；
  多记录共用同一真实callee-use，没有新样本/opcode串/寄存器布局或
  placement白名单。真实receiver/getter、phi edge、transport、prefix
  ownership、normal copies及catch scope/staging仍逐项验证，整组失败
  不部分移动callee。普通branch也登记decision ownership。
- 祖先Reference按真实inline consumer连接，正常corridor的效果归属区间
  与catch一致性一次索引，每条实际祖先关系只查询/保存一次，不再逐选择
  重扫共享长前缀或复制祖先链。区间在进入终点前结束，终点多个前驱不
  新增准入限制；callee/branch/child的物理OpId排序门槛由实际路径替代。
  共享Reference的记录和独立验证按同一UseId去重，未改SourceContract V3。
  算法、预算/复杂度及剩余single-entry候选边界见algorithms.md。
- joint_selection_call_test覆盖三profile的129组解码用例，包含
  1/2/3/50/100/200/201参数、1/2/200/201/402复制、getter prefix、外层
  成员调用、反向参数与debugger屏障，以及缺记录/错argument/callee负例。
  精确verifier工作配额通过，少一步及零字节拒绝。50/100/200/201参数
  evaluation work为538235/1062180/2110010/2120488，几何增长检查通过。
  joint_selection_call.mjs另验证44496组完整生成源码的receiver、getter
  替换、混合truthiness、精确值/身份、任意throw和非callable时机，9项
  mutation实际失败。此为解码/draft及Node隔离层，非新增真实完整record。
- 初始双getter arm夹具因旧PhiExpressionProducer的computed Commit
  outgoing限制发现0候选，未计为单选择门槛RED；交换测试arm后发现2
  候选，未追加生产tail匹配。联合实现初轮还发现普通branch ownership和
  前一选择的实际transport被当作后一prefix两个问题，已按共同owner/
  transport索引修复。confirmed-red-build.log在临时恢复HEAD原生产实现
  后重新构建，confirmed-red.log确认双选择断言失败；生产已完整恢复并
  重建。临时诊断已移除，以上失败均未计入通过。
- final-build.log单构建-j4退出0；final-tests.log 19/19通过59.63秒。
  核对注册后另重新链接abc_semantic/phased_literal_test，再运行
  paired-tests.log：7/7通过96.92秒，包含预算新增矩阵、完整selection
  解码回归及fresh pasteboard/Home/三项phased literal行为。合计20项
  不同相关检查通过；旧二进制的5项结果不作为新鲜配对验证证据。
  前置index-tests.log 7/7通过94.35秒，最终证据以后两组为准。日志前缀
  /tmp/abcd-joint-call-；full-suite、扩展语料、RSS、基准、DFX及代理继续
  暂停。本次未安排周期审计。最终将catch一致性标记改为零初始化bad
  状态，省去未单独计费的重复fill；accounting-build.log退出0，重新链接
  后accounting-tests.log 7/7通过36.42秒，工作数与行为不变。
- fresh build/stage-one/value-joint-selection-pass-2 固定五输入全部
  退出0，80 complete/26 incomplete/0 failure/46 no-module，无完整
  回退或legacy writer。final-batch.log/final-syntax.log核对输入SHA/size与manifest/
  index一致，106份源码全部解析且与revision86字节相同。shape审计记录
  已更新：单选择数量门槛关闭，但MemberCall inline-owned表示、single-
  entry/arm/phi候选、computed arm inert tail、class initializer同块、
  phased scope/capture/module和legacy模板仍开放。未知算术域与导入super
  协议缺口保留，阶段一语义/风格双验收尚未完成，Goal active。

### 2026-10-04：显式派生构造器发布复用共同初始化证明

- 规则 revision86。删除PreparedFamily::Prepare中显式DerivedConstructor
  必须有receiver_guards的第二重准入条件。实际root/ClassContext和placement
  保留；FamilyTrees已经对同owner输入执行ProjectDerivedConstructor，再
  构建并验证树/flow/ABI/来源、捕获与整族写入。发布消费这条既有通路，
  没有新证明包装、DFX前置或局部序列/布局匹配，SourceContract V3不变。
- 无guard不等于捕获receiver已知。没有descendant preservation时仅寄存器
  初始化可成立，不能产生immutable cell事实；可写/混合cell与子函数实际
  initialized guard分别验证。修改当前store、伪造guard列表、陈旧receiver
  事实、错误来源与owner继续拒绝。协议、guard省略与源码表示义务分开，
  不能把“全部捕获必须只读”或“必须有guard”作为构造器语言要求。
- 文件化编码夹具覆盖三profile的60组完整构造/字面量/分支捕获/成员调用/
  传递捕获，1/200/201/402复制变化及21组负例；不是新增真实完整record。
  生成的完整constructor fragments另有7686组独立无guard行为对照，检查
  父返回、newTarget/Proxy、getter/descriptor、receiver、创建时调用闭包、
  任意抛出值和负例mutation。输入和高层oracle分离，无sidecar进入引擎。
- RED早期曾因旧夹具把协议与guard义务混合而bad_variant_access/断言失败，
  分解后refined-tests.log明确命中原kCallable门槛。省略literal guard时
  夹具漏掉accumulator receiver load，导致错误getter输入及evaluation拒绝，
  已修正编码而未添加生产模式。boundary-tests.log最终4/4通过18.55秒。
  integration-tests.log首轮16/18通过，两项captured/forwarded旧文本mutant
  未改变嵌套保存赋值；现保留真实super效果而破坏cell receiver身份，负例
  实际生效。reference-tests.log最终5/5通过28.57秒，含更新后的完整C++
  矩阵、两个修复测试及两个新无guard成员调用测试。上述失败未计入通过。
- 合计24项不同相关检查已通过，包括normal/phased类、捕获、range/spread/
  forwarding、async方法和pasteboard/Home/class-root配对行为。相关单构建
  -j4的final/repair/boundary/integration/reference-build.log均退出0；日志
  前缀为/tmp/abcd-constructor-publish-。full-suite、扩展语料、RSS、基准、
  DFX及代理继续暂停。临时诊断代码已移除，未新增生产文本匹配。
- fresh build/stage-one/value-constructor-publish-pass-1 固定五输入全部
  退出0，80 complete/26 incomplete/0 failure/46 no-module，无完整回退
  或legacy writer。batch.log/syntax.log确认输入SHA/size与manifest/index
  一致，106份源码均解析且与revision85字节一致。此次关闭显式派生根
  的guard存在门槛，没有减少现有未知算术域、导入super协议、共享Reference、
  phased多selection与正文缺口；阶段一语义/风格双验收未完成，Goal active。

### 2026-10-04：布尔选择与 NOT 复用共同打印观察

- 规则 revision85。共同 printer 将观察扩为 Value/Truthiness/Discarded，
  用 typed polarity 迭代组合 NOT 与 BooleanLiteral conditional。正向选择
  的精确值为!!condition，反向为!condition；truthiness消费可省去正向
  转换。相同布尔结果用(condition,true/false)保留原GetValue、getter与
  异常；discarded只省去无用户钩子的转换/结果。&&/||左侧仍消费truthiness，
  ??左侧仍观察精确值。原AST身份、效果、scope和输入接受域/V3没有改变。
- 共同grammar对剩余unary及被暴露子表达式重新分组，含exponent左侧。
  被省略的NOT、选择和literal节点保留非空/有界来源范围；无样本/producer/
  opcode/CFG准入匹配。44种选择/上下文，1/2/200/201/402/804深度与工作量
  检查通过，270080组独立getter/Proxy/短路/任意抛出值/返回身份对照及4项
  语义mutant有效。另保留75520组布尔上下文对照。这是printer隔离层；
  原始ABC完整路径由既有normal/phased/配对回归另行验证。
- RED boolean-choice-red.log证实旧printer保留布尔ternary。首轮Node oracle
  错将不同执行的新Symbol/对象直接比较，随后改为先验证各自输入身份；
  另一轮mutant误认为去掉NOT会丢getter，现区分结果类型与丢condition负例。
  追加discarded测试时曾将夹具代码放错条件块，编译失败，已修复。上述
  失败均未计入通过。discard-repair-build.log退出0，discard-tests.log
  3/3通过；final-build.log、module-build.log均以单构建-j4退出0。
- /tmp/abcd-boolean-choice-final-tests.log 27/27通过82.18秒，包括源码
  precedence/postfix、branch、normal/phased snapshot、class/module、
  unary/literal和pasteboard/Home/DetailPage配对行为。没有恢复暂停的
  full-suite、扩展语料、RSS、基准、DFX或代理。
- fresh build/stage-one/value-boolean-choice-pass-1 固定五输入全部退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无完整回退或legacy
  writer。batch.log/syntax.log确认输入SHA/size与manifest/index一致，
  106份源码全部解析；38份complete及16份incomplete表示改善，其余字节
  不变。实际PageB/HotTitleView的membership否定由false/true ternary变为
  !("finalizeConstruction" in ViewPU.prototype)，来自ABC AST而非sidecar。
- 同步shape-restrictions-audit.md：CallPlan和Receiver同块门槛未回归，
  phased单selection仍在；补充prepared_family.cc显式派生根要求非空
  receiver_guards的发布边界，尚需共同初始化/receiver生命周期证明接入。
  当前38份源码改善不新增完整record，未知算术域、导入super协议、共享
  Reference与剩余正文/高层结构缺口保留；阶段一双验收未完成，Goal active。

### 2026-10-04：构造初始化改用真实 CFG 单次调用证明

- 规则 revision84。按原用户审计请求继续核对构造路径，删除
  ProveInitialization 的 backward-PC 一律拒绝、至少一个guard及super支配
  全部guard门槛。range/spread共享一次successor可达性遍历，调用所在块
  可重复时拒绝；独立前/后循环和向后物理布局不再需要新匹配。正常return
  支配与每项guard的真实SSA输入独立验证，没有虚构guard、父类或值域。
  current-IR投影继续复验owner、来源、receiver和完整guard对应。
- 新遍历O(B+E)时间/O(B)scratch，每块至多入队一次，容量、初始化及
  边访问计入预算；原支配/稀疏SSA算法保留。算法/剩余接口边界分别更新
  algorithms.md、shape-restrictions-audit.md。单super site、异常/capture/
  parent brand/spread展开及legacy构造模板仍开放；这不是原生整类许可。
- RED /tmp/abcd-constructor-flow-red.log确认旧guard存在门槛拒绝真实解码
  输入。720个三profile、range/spread、有/无guard、复制槽、前/后循环/
  重复调用/向后布局用例通过；另有1/200/201/402/804块CFG链和精确work
  边界、40个same-file owner下的input/current-IR投影与伪造guard负例。
  解码/projection夹具并非真实新complete record，未声称完整源码验收。
- 单构建-j4：首次final-build.log在测试夹具returned声明位置编译失败，
  修正后repaired-build.log与boundary-build.log均退出0。首轮19项检查
  中，derived_cells_source_test的旧文本mutant未实际改变嵌套保存赋值；
  已修正为保留super效果而破坏捕获receiver，实际负例生效。另有已知
  base_constructor_js_test旧writer assignments失败，与历史基线位置
  一致，未计入通过。最终final-tests.log的19/19通过43.78秒，含630+630
  构造/闭包、1890receiver literal、240配对回调、362Home及185构造/
  70async对照。full-suite/RSS/扩展语料/基准/DFX和代理保持暂停。
- fresh build/stage-one/value-constructor-flow-pass-1固定五输入全部退出0，
  80 complete/26 incomplete/0 failure/46 no-module，无完整回退及legacy
  writer。batch.log/syntax.log确认输入SHA/size与manifest/index一致，
  106份源码全部解析且与revision83字节一致。本次替换实际证明边界，未
  减少现有未知算术域、导入super及正文缺口；阶段一双验收未完成，Goal active。

### 2026-10-04：共同打印器的布尔观察上下文

- 规则 revision83。打印操作数增加 typed Value/Truthiness 观察上下文，if、
  conditional test 和逻辑 NOT 消费 truthiness；&&/|| 和选中的 conditional
  分支只在同一上下文传递该观察。共同 printer 省去该上下文的成对 NOT，
  不按样本、opcode序列或 CFG 形状决定恢复。return、赋值 RHS、参数、属性
  引用、算术和 nullish 操作数继续观察精确值；不能把 false 改成0、undefined
  或对象，也不能改变 ?? 的 RHS 执行条件。其他数值/tag/构造协议准入未变。
- 被暴露的子表达式重新使用原父 grammar context，保留 &&/||/??、比较及
  unary grouping；NOT 的原始两个 span 覆盖实际输出。树、来源、绑定和
  原效果证明不变，显式栈逐对处理，O(nodes)时间和空间，不递归重扫链。
  ECMA-262 ToBoolean/Logical NOT 的无用户转换钩子语义为依据；具体合同和
  官方资料链接见 operator-expressions.md。没有新增宿主前提或更改V3。
- RED /tmp/abcd-predicate-context-red.log 确认原打印器仍输出 !!a.value。
  新增14种 printer隔离观察用例、1/2/200/201/402/804深度及非空/有界
  unary来源span，75520组独立getter/Proxy/短路/nullish/conditional/value/
  mutation/抛出值对照通过。printer夹具不是原始ABC输入证明；既有真实
  normal/phased/branch/snapshot 和配对record测试另验证完整源码路径。
- 单构建 -j4 退出0：/tmp/abcd-predicate-context-build.log，26/26相关
  检查78.60秒通过：predicate-context-tests.log，含Home/DetailPage实际
  配对行为和decode生成源码。随后只增补conditional分支及span断言，
  nested-build.log / span-build.log均退出0，span-tests.log的2/2复核通过。
  初次语法检查曾与仅测试夹具的构建重叠；构建结束后重新执行，最终采用
  predicate-context-final-syntax.log。full-suite/RSS/扩展语料/基准暂停。
- fresh build/stage-one/value-boolean-context-pass-1 固定五输入均退出0，
  80 complete /26 incomplete /0 failure /46 no-module，无完整回退或
  legacy writer。输入SHA/size与manifest/abc-index一致，106份源码全部
  解析；39份complete、19份incomplete采用更自然的条件表达，其余字节不变。
  来源为当前ABC生成AST，配对源码未进入引擎。此为源码表示质量推进，没有
  新增完整record；未知域算术、导入super协议、存储Reference、多selection
  及剩余高层结构仍开放，阶段一语义/源码风格双验收未完成，Goal active。

### 2026-10-04：共同 eager 使用点保存与成员 Reference 复核

- 规则 revision82。删除 tree_operands::Receiver 中残留的第二重
  property/call 同块或 CrossPhaseInline/CrossLogicalInline 枚举门槛。
  VerifyTreeFlow 在操作数检查前新鲜复核 normal/phased EvaluationPlan；
  实际 property result、consumer、base/receiver 身份和存储对应继续成立。
  没有重读 callee/getter 或引入可变 .call/.apply。历史同块条件是局部
  实现边界，不能充当语言/VM 不变量；禁止新增布局匹配的规则仍生效。
- predicate snapshot 的 strict comparison→branch 候选条件改用共同
  DescribeEvaluation eager 语义与完整效果区间；不再要求函数有 phi。
  森林和操作数复核携带真实 UseId，保存赋值只在该使用点执行，其他使用
  读取同一个私有槽。logical/conditional 保留各自实际谓词 occurrence；
  native logical keep 绑定已经验证的左值，不重复保存，不把普通 phi
  私有槽读取误当成 logical keep。事件域与主森林一致，已经独立证明的
  receiver/cell/module guard 和 folded literal/class 定义不产生独立事件；
  staged 定义恢复其原始 root 事件。没有放松 guard/literal 的独立证明。
  契约见 expression-snapshots.md，最新审计见 shape-restrictions-audit.md。
- 新增三 profile、0/1/200/201/402 copy、寄存器互换、getter barrier、block
  split 的840个真实解码夹具及258720组独立 Node 行为对照，覆盖 setter、
  typeof、computed read/equality、单次/重复参数、computed-write reference
  边界、receiver、共享身份、key coercion、mutation及任意抛出值。缺失
  consumer、错误私有符号、跨效果/块的伪造保存与精确 work 边界负例保留。
  引擎仅接收编码输入；native reference 仅进入独立测试。
- 调试发现两项真实 complete 回退：普通 saved phi 被误要求拥有 logical
  owner，以及构造函数 snapshot 复核把已证明的 guard/折叠定义算作独立
  事件。均修复共同对应关系，没有样本生产分支；失败日志保留。新增的
  临时出版检查曾误断言平台 executable=true，已撤去重复规格，复用既有
  Home 完整配对测试；source_complete 不授予平台执行收据。
- 最终单构建 -j4 退出0：/tmp/abcd-eager-snapshot-projection-build.log。
  projection-final-tests.log 记录27/27相关检查82.68秒通过，包含真实
  pasteboard 回调240组和 Home record362组配对行为；新增源码对照通过。
  changed DetailPage 额外复核1/1通过3.98秒：detail-tests.log，包含1080组
  短路谓词和528组嵌套GridCol场景。此前 guard-tests/projection-tests 的
  失败不计通过。full-suite/RSS/扩展语料/基准/DFX保持暂停。
- fresh build/stage-one/value-eager-snapshot-pass-3 固定五输入均退出0：
  80 complete /26 incomplete /0 failure /46 no-module，没有完整回退或
  legacy writer。五输入SHA/size与manifest/abc-index一致，106份源码
  全部解析。相较revision81，四份complete和两份incomplete源码改为实际
  使用点保存，其余字节不变；检查见 guard-batch.log / guard-syntax.log。
  当前 interval 仍为 normal 局部证明，跨块继续保留原保存；phased commit/
  throw/catch版本、存储Reference、多selection参数、首次own-definition及
  legacy构造模板仍开放。阶段一语义/源码风格双验收未完成，Goal active。

### 2026-10-04：literal 早期输入复用共同分配点可用性

- 规则 revision81。删除 EarlyValue 的 phased phi.block==allocation.block
  及 producer 块身份分派；保留 fresh normal/phased ValueFlow 已产生的支配树，
  在真实分配点查询共同 ValueAvailableAt。成功 Commit、phi/catch entry 与
  原始 operand 可用性检查共用语义。保留值域、实际 SSA 输入、初始化区间/
  unique phase corridor、未完成观测及 catch-copy 义务，不放行晚定义值。
  支配树保留存储计入现有预算；O(B)保留空间、每 early operand O(1)查询，
  没有额外支配树重建、每变量图遍历、sample或图形匹配。设计与审计分别更新
  literal-initialization.md / shape-restrictions-audit.md。
- 三profile、0/1/200/201/402 copy与block split验证原始编码路径及纯复制
  消除后的候选IR。候选RED确实在Evaluation拒绝，日志
  /tmp/abcd-literal-phi-forwarded-red.log；原始编码此前已保留phi快照并可输出，
  不能把它称为本次新完成正文。候选独立literal/evaluation复核通过；原始
  编码完整exception源码树与43组独立native行为对照通过，含fresh identity、
  own __proto__ descriptor、prototype和无getter/coercion副作用。
- raw import对应关系仍是独立义务：对转发候选尝试原始ExceptionPlan时被
  VerifyImportedPhases拒绝，backtrace及green-tests.log保留失败证据。测试
  明确分开候选证明与原始编码源码路径，没有为copy形状加发布例外，也没有
  将该候选证明作为端到端成功。first-own-definition跨选择仍开放；空对象
  的DefineOwnProperty不能偷换成可能触发继承setter的普通Set。
- 最终单构建 -j4 退出0：/tmp/abcd-literal-phi-final-build.log。
  final-tests.log记录11/11相关检查35.94秒通过。随后只增补可用性查询的
  早/晚phi正负断言，availability-query-build.log退出0，受影响两项检查
  2/2通过25.97秒；生产未再修改。初次测试夹具的narrowing编译失败及首次
  raw import对应失败均未计通过。full-suite/RSS/扩展语料/基准/DFX保持暂停。
- fresh build/stage-one/value-literal-phi-availability-pass-1 固定五输入均
  退出0：80 complete /26 incomplete /0 failure /46 no-module，无完整回退，
  legacy writer均0。五输入SHA/size与manifest/abc-index一致，106份源码
  全部解析且与revision80字节一致：final-batch.log / final-syntax.log。
  这些证据没有减少参数/属性算术、导入super构造协议及其他正文/表示缺口。
  阶段一语义与源码风格双验收仍未完成，Goal保持active。

### 2026-10-04：布尔/null 转换事实贯通共同算术规则

- 规则 revision80。共同 ValueKinds 增加 ZeroOrOnePrimitive，准确表示Boolean/
  null而非Number；依 pinned JSTaggedValue::ToNumber/ToNumeric 的0/1语义，
  ArithmeticNumber 给既有算术/转换规则提供有限整数与tagged-minimum排除事实。
  正常/异常SSA和linear lexical复用这些规则，比较/布尔运算的normal结果与
  typeof的String结果在共同ValueNode中传播，输入义务成立之前不seed，防止
  未证明循环自证。源打印和legacy negation独立校验仍走同一树/operand接口。
- 原始Boolean/null值不变成Number，也不省略真实转换；与其他Number/String
  混合会失去此精确域，undefined/Hole/environment/一般Language不提供该事实。
  不猜参数、属性或宿主返回类型；任意NaN/tag边界与MIN_INT32的旧负例保留。
  支持比较结果/Boolean-null phi/typeof字符串组合及现代/旧版update/negation/
  转换参与共同算术表达式，无sample、opcode序列、producer或块布局匹配。
- 真实编码新增三profile normal/catch组合，分别覆盖加/减/乘/除/幂、两侧
  primitive操作数、null负零、比较副作用及所有四种update；共享值流oracle
  更新为真实Boolean结果，保留输入未证明的typeof/environment及istrue/Hole
  负例。1/200/201/402 phi维度通过，新增112840组独立native行为对照通过，
  包含getter/ToPrimitive、Proxy、Symbol/BigInt、NaN payload、mutation和任意
  抛出值。引擎输入只有真实编码，reference源码仅在独立测试使用。
- RED true+2实际在输入证明拒绝：/tmp/abcd-primitive-arithmetic-red.log。
  初次phi用例的first-own-store聚合跨branch在Evaluation阶段失败（保留debug
  backtrace）；改用真实named property writes验证本次算术/phi能力，不为该
  聚合缺口增设生产匹配，也不把它算成功。首轮旧oracle失败已按语义更新，
  失败不计通过。最终 -j4 构建 final-build.log / guard-build.log 均退出0，
  当前稳定二进制22/22相关检查72.20秒通过：
  /tmp/abcd-primitive-arithmetic-final-tests.log；native数量见LastTest.log。
- fresh build/stage-one/value-primitive-arithmetic-pass-1 固定五输入均退出0，
  80 complete /26 incomplete /0 failure /46 no-module，没有complete回退或
  legacy writer。五输入SHA/size与manifest/abc-index一致；106份完整/部分
  源码全部解析并与revision79字节一致：final-batch.log / final-syntax.log。
  新共同事实没有消除这些输入的参数/属性算术缺口，阶段一仍未完成。
- 实际检查pasteboard方法5165、MultiCommunity方法41244的除法及Immersive
  方法8620的属性加减，尚无非NaN/数值域事实可用。导入父类构造协议、跨branch
  首次声明/聚合归属和phased初始化也仍有开放项。SourceContract V3未改；
  full-suite/RSS/扩展语料/基准/DFX保持暂停。Goal active，继续实际开发。

### 2026-10-04：普通模块读取与创建点复用共同初始化分析

- 规则 revision79。ModuleSlotSet 与完整 SSA/IDF 初始化算法移入 analysis，
  原始 SemanticMethod 与当前 ValueProgram 分别提供真实 store/read/creation
  站点，共用 Boolean must meet、支配树与持久化 before-site 快照。删除原始
  raw local read 的 entry-block store 准入，以及 source stored-read 的
  current_store.block==0 / 同块order准入。汇合事实不需要伪造一个支配store；
  kStored 的直接入口 backlink 仅是可选 provenance，没有第二套算法。
- 原始候选先形成结构事实，完整值域验证随后核对每个store操作数为Language，
  再发布读取/子创建权限；单侧store、零次loop、Hole及read/store循环不能自证。
  source 独立验证 CURRENT 图的可用性、actual write/use/domain，再重算初始化，
  移除/移动store不能借旧输入放行。原始slot/origin/effects、live binding、TDZ
  和正常/异常边界保留；FunctionInput只持有真实创建点快照。不扩大样本名称、
  producer白名单、branch形状或opcode序列匹配。设计见module-reads.md。
- 真实编码回归包含joined/two-arm/one-arm、zero-trip/do-first、未知/Hole循环、
  当前store移除、before-site顺序、200/201/256/512/1024维度及精确work/resource。
  候选分析先保留lexical验证work，新增预算回归防止unsigned quota wrap。
  构建临时根在独立复核前释放，两个pass复用一个scratch额度。普通及catch
  family的父分支raw read都输出完整源码，三profile独立native ESM对照扩展到
  720组，覆盖分支两方向、身份、receiver、无转换、arity/name、live exports、
  初始化异常及cycle TDZ。reference source仅进入测试。
- 最终单构建 -j4 退出0：/tmp/abcd-module-normal-read-lifetime-final-build.log。
  稳定二进制 lifetime-final-tests.log 中13/13相关检查10.67秒通过；720组对照
  包含于module_writer_source_test。首次实际raw family输出RED拒绝保留在
  red.log；搬移类型冲突及拼写错误的编译失败未计成功。full-suite/RSS/
  扩展语料/基准/DFX仍暂停。
- fresh build/stage-one/value-module-normal-read-pass-2 固定五输入均退出0；
  80 complete /26 incomplete /0 failure /46 no-module，无完整回退、legacy
  writer均0。final-batch.log记录本次当前构建；五输入SHA/size与manifest及
  abc-index一致，106份完整/部分源码全部通过Node语法解析，字节与revision78
  均一致：/tmp/abcd-module-normal-read-final-syntax.log。共同能力已闭合，
  没有减少这五个输入的剩余缺口，不能替代阶段一语义/表示双验收。
- 仍开放：phased parent 的成功Commit初始化、创建先于初始化的escape/
  invocation证明、仅branch arm首次初始化的源码声明归属；算术输入域与导入
  父类构造协议继续阻断部分函数正文。Goal active，继续实际开发。

### 2026-10-04：模块初始化通过共同 SSA 传播到真实创建点

- 规则revision78。删除 FunctionInput 的 parent entry-block 初始化准入；
  复用现有 RegisterSsa/IdfIndex 的完整 placement、Boolean must meet 与
  DominatorTree 遍历，在每个真实 function/class allocation 保存持久化
  ModuleSlotSet。成功且已证明 Language 的原始 store 建立事实；FALSE
  沿真实 phi 输入传播，循环不自证。分支双方初始化可在汇合成立，仅一方
  初始化、零次循环、未来或不可达 store 均不能授予权限。普通 parent 的
  创建点集合同时供 normal/phased child 使用，不省略异常/resume 边。
- EntryModuleStore 只保留可选直接 provenance，已不决定初始化接受范围；
  family 独立重核真实 owner/创建 occurrence 和 immutable root 身份。旧
  初始化 input fixture 的非入口 store 现成功，更新为正例，保留缺失store、
  错误 owner/creation、套用旧 parent root 的负例。真实 module metadata、
  TDZ、作用域、原始 effects 和 child write 的身份校验未放宽。设计与复杂度
  见 module-reads.md / algorithms.md；不按具体分支/寄存器/PC形状恢复。
- 新编码状态用例覆盖两侧/单侧初始化、post-join store、zero-trip/do-first
  loop、创建先于store、不可达store、200/201/256/512/1024独立slot及creation
  维度、精确work和resource失败。新增完整ESM生成用例在父分支之后初始化，
  再创建普通writer或带catch的writer；三profile、分支执行/跳过和20种值
  共480组独立行为对照通过，包含身份、receiver、无强制转换、live exports、
  arity/name、TDZ、初始化抛出和cycle失败。reference source仅进入测试。
- -j4最终构建退出0：/tmp/abcd-module-initialization-final-build.log；
  更新旧spec后的fixture构建 updated-spec-build.log退出0。稳定二进制
  final-tests.log 中12/12相关测试6.84秒通过。此前旧测试取failure variant
  而新实现已成功，调试定位在module_initialization_input.cc原136行；修正
  语义预期后复验，没有把旧拒绝当正例成功。首轮integration build未结束时
  的过早测试使用旧fixture，未计入本次验证。未运行full-suite/RSS/扩展语料。
- fresh build/stage-one/value-module-init-flow-pass-1 固定五输入导出均退出0；
  80 complete /26 incomplete /0 failure /46 no-module，完整结果无回退、
  legacy writer均0。本共同能力没有减少这些输入的剩余缺口，也不代替阶段一
  的语义/表示双验收。批次：/tmp/abcd-module-initialization-verified-batch.log。
  五输入SHA与manifest/abc-index一致，106份完整/部分源码全部通过Node语法
  检查，与revision77源码字节均未改变：syntax-all.log。
- 仍开放：phased parent 的成功Commit初始化、创建先于初始化的escape/
  invocation证明、parent raw local read接入、仅branch arm首次初始化的
  source声明归属。数值输入域与宿主构造协议等阶段一缺口保留；Goal active。

### 2026-10-04：子函数模块写入复用初始化证明和共同别名/源码树

- 规则revision77。正常及phased family 绑定实际 ValueModuleWrite，包含没有
  module read 的只写函数。共同 metadata/slot 身份、原始 store/effects/origin、
  Language 值域和 creation-time 持久化初始化集合均核对；参数和 literal
  不按 producer 或 child basic block 分派。phase before/commit/catch 保留。
  所有写入仍走共同 AssignmentExpr、TreeOperands、scope 和 destination；
  子函数别名保持 module scope / local=false，不补声明、不提前初始化。
- VerifyModuleAliasTree 从实际写入 inventory 推导 mutable access；同时修改
  expected/actual access 和符号let仍不能给只读别名授予写权限。literal 复用
  pinned numeric规则，原始slot不对应、缺失初始化证明、超出work预算均拒绝。
  原始普通模块发布继续排除 shared module；导入仍不能写。设计与复杂度见
  [module-reads.md](module-reads.md) 的 rules77。
- 初始化是具体语义义务：STMODULEVAR可初始化Hole，native let assignment
  保留TDZ。此次复用的是既有immutable entry证明；parent entry-block准入仍
  是旧实现边界，不能当语言不变量。创建早于初始化、其他parent块中的首次
  初始化及child成功guard之后的写入，还需要共同must-initialization与
  escape/invocation证明；不以增加同块/固定序列匹配扩展该边界。
- 三profile真实ABC编码覆盖参数、分支、常量和catch写入。240组独立native
  ESM行为对照通过，含undefined/null/boolean/NaN/Infinity/BigInt/Symbol/
  object/function身份、无强制转换、name/length、live export及alias、初始化
  抛出和cycle TDZ。fixture另检查fresh只写别名、actual原始slot不对应、精确
  work预算，以及early-created writer的typed initialization拒绝，未将拒绝
  计入成功覆盖。新增test直接读取ABC，不传reference source给引擎。
- -j4最终构建退出0：/tmp/abcd-module-writer-proof-build.log；新版本5/5相关
  检查4.88秒通过：/tmp/abcd-module-writer-verified-tests.log。此前fixture
  length、prefixed throw编码与测试不可复制input的失败已修正，失败不计通过。
  full-suite/RSS/扩展语料仍暂停；未用过去的测试或旧二进制代替本次证据。
- fresh build/stage-one/value-module-writes-pass-2 固定五输入导出均退出0；
  80 complete /26 incomplete /0 failure /46 no-module，与revision76相比没有
  complete回退或源码内容变化，legacy writer均0。此共同能力交付没有减少
  固定五输入的剩余缺口，不能宣称阶段一完成。批次证据位于
  /tmp/abcd-module-writer-verified-batch.log。五份原始输入SHA与abc-index一致；
  106份完整/部分源码全通过Node语法检查，源码字节对照revision76均未改变：
  /tmp/abcd-module-writer-syntax-all.log。Goal active，继续实际开发。

### 2026-10-04：类表达式复用声明与赋值的原生名称推断

- 规则revision76。共同 TreePrinter 给直接 identifier 初始化/赋值的 RHS
  传递实际 binding NameId。ClassExpr 的原 runtime name 与实际打印 spelling
  一致时输出匿名 `class` / `class extends`，由语言 NamedEvaluation 保持
  `.name`；不添加会遮蔽可变外部绑定的 immutable class self。名称不匹配、
  属性赋值或其他表达式 destination 继续用 computed-property 推断。此为
  已恢复 AST 的语法表示选择，不新增 ValueIR/CFG 或 recovery 准入条件。
- 三profile真实编码模块覆盖首次 class 声明、已有绑定上的 class 赋值及
  runtime name 不匹配；随后把同一个模块 binding 改为导入值，原构造器
  必须继续读到该值。63组独立 ESM 对照通过，含 TDZ、导入/初始化、导出
  alias、类身份/name/length/descriptor、NaN/Infinity及构造调用。6个合法
  语法 mutant 添加 named class self 后改变构造返回身份，检测到这项差异；
  原 computed-property reference source 仅进入测试，不进入引擎。
- -j4生产构建退出0：/tmp/abcd-class-binding-final-build.log；最终fixture
  构建assignment-build.log。稳定二进制 verified-tests.log中9/9检查3.90秒
  通过；新增mutant后record_import_tree_source_test单独复验1.18秒通过。
  paired-tests.log另外4项固定输入独立对照（static class、viewmodel、local
  model及HomeModel）通过，共13项不同相关检查。此前fixture pool/参数元数据
  和新print form错误、未准备的fixture均有失败日志，未计入通过。优先链接
  HEAD旧printer的实际编码正例在第404行失败：class-binding-red.log；这是
  有界翻译单元链接对照，非整个HEAD重建。full-suite/RSS/扩展语料仍暂停。
- fresh build/stage-one/value-class-binding-pass-1 固定五输入均退出0，SHA
  匹配abc-index；仍80 complete /26 incomplete /0 failure /46 no-module，
  无legacy或完整模块回退。38份完整及15份部分源码实际改善，106份全部通过
  语法解析。输出有47个直接class声明，名称/使用上下文不同的48个包装保留；
  /tmp/abcd-class-binding-content.log、syntax.log及batch.log记录当前证据。
- 新fixture曾暴露子函数 ValueModuleWrite 没有 ModuleStoreConstant 对应，
  在 expression forest kConstant 拒绝；测试改用真实模块入口重新赋值来
  验证本次语法规则。该子函数写入能力仍是具体共同接口缺口，不能称已支持。
  数值输入域、宿主构造协议和缺失正文仍阻断阶段一验收；Goal active。

### 2026-10-04：类成员跨块安装采用共同效果前驱与支配证明

- 规则revision75。FindClassDefinitions 删除 creation/installation 与
  method/installation 的同块条件；ProjectClassDefinitions 的 claim 也不再
  依赖同块布局。共同 EffectOrder 复用当前 VerifyValueFlow 的真实支配树，
  记录每个操作的效果前驱身份。无效果分块、分支汇合及纯循环透明传播；
  不同效果历史保持 typed opaque join，不能用效果数量相同证明等价。
- 被投影的 prototype 读取、方法创建和成员安装必须组成从原 class 创建
  开始的单一效果链；前驱支配后继，唯一尾部是最后安装，所有外部直接/
  phi边观察晚于该安装。HomeObject、捕获环境、方法单次使用、描述符及
  异常/效果义务保留。带活跃结果的 accessor 安装仍因 alias 对应缺口拒绝，
  phased class 与 class initializer 尚未贯通，审计第6类没有整体关闭。
- native 成员按共同 CFG reverse postorder 的 DefinitionOrder 排列，
  该顺序仅用于已通过效果链/支配证明后的序列化，不授权移动。候选表仍按
  definition ID 索引；实际物理指令顺序反转不改变方法覆盖语义。共享 SSA
  验证返回已构造的支配树，避免调用者另建；候选发现和独立投影各运行一次
  有界全图构造并记账，无每类 CFG 扫描。复杂度与算法来源见 algorithms.md。
- 编码 fixture 覆盖三profile、1/200/201/402安装、跨块/纯diamond/纯loop，
  debugger单臂干预、两臂相同数量但不同身份的效果、跳过/重复安装以及伪造
  DefinitionOrder 负例。优先链接 HEAD 原候选发现实现后的 --cross-install
  正例在第87行失败：/tmp/abcd-class-effects-red.log；这是有界原实现链接
  对照，非整个HEAD重建。432组独立 observer 源码行为对照通过，比revision74
  增加288组，另保留96组capture对照。包含反转物理指令、getter/setter与
  普通方法覆盖、函数身份/名称、描述符、receiver、Proxy及任意抛出值。
- -j4最终构建退出0：/tmp/abcd-class-effects-final-build.log。稳定二进制上
  13/13相关定向检查18.32秒通过：final-tests.log；含 normal/phased 值流
  精确费用与资源负例、evaluation、tree/source route、文件级class与成员
  遗漏检查。前节两项已知legacy失败此次未重跑，不把它们算作通过。
- fresh build/stage-one/value-class-effects-pass-1 固定五输入均退出0，
  SHA匹配abc-index.json；仍80 complete /26 incomplete /0 failure /
  46 no-module，无legacy或完整模块回退。106份完整/部分源码逐字未变并
  通过语法解析，/tmp/abcd-class-effects-content.log与syntax.log。
  full-suite/扩展语料/RSS/基准/DFX继续暂停。Goal active；数值输入域、
  构造协议和遗漏正文仍阻断阶段一完整语义与源码表示验收。

### 2026-10-04：类成员安装后的观察复用共同 SSA 可用性证明

- 规则revision74。ProjectClassDefinitions 不再要求 class/prototype 的外部
  观察者与最后安装同块。先独立运行 VerifyValueFlow，证明真实定义支配
  直接使用或phi输入边；原安装仍在创建块，完成该块即完成安装。块内
  观察仍须晚于最后安装，跨块不以block/op数值顺序推断时序。phi读取按
  实际incoming edge source定位，保留仍有外部使用的prototype原读取。
- HomeObject、原方法单次使用、描述符、捕获环境、整个安装区间的所有效果
  及早期逃逸校验保留。候选发现/claim 中 creation、method、installation
  同块条件仍是公开的未完成边界；没有替换跨块安装的全路径效果/次数证明，
  也没有放宽phased类或任意宿主构造器语义。审计第6类未整体关闭。
- ValueFlowProof/PhasedValueFlowProof 返回共同验证的保守scratch字节费用，
  投影在共享预算中记账；normal/phased精确费用及少一字节反例通过。每次
  投影只有一次共享可用性证明，后续线性所有权/使用普查，无每类CFG扫描。
  编码用例覆盖三profile、1/200/201/402安装、后续分块/phi/循环使用；
  非法边、仍保持def-use但提前读取自身结果、伪造投影、早期逃逸、环境/
  debugger干预负例仍拒绝。旧投影的分块正例失败见
  /tmp/abcd-class-observer-red-test.log: class_initializers_test第79行。
- 新144组独立源码行为对照通过：三profile、静态/prototype成员、分块及
  phi选择、任意参数/不触发Proxy转换、类和函数身份、描述符、getter/setter
  receiver/效果与任意抛出值。真实编码输入经共同ValueIR、evaluation、
  FunctionTree、class/member构建、tree flow及printer；源码仅进入测试。
  带参数fixture输出完整函数，module-body destination拒绝丢弃其签名。
  这是共同树/草稿行为验证，不冒充阶段一所有模块的publication验收。
- -j4构建退出0，/tmp/abcd-class-observer-final-build.log及
  final-fixture-build.log。12项相关定向检查中9项当次通过；新JS断言修正
  NamedEvaluation允许的匿名class表示后，class_accessor_tree_source_test
  单独通过，paired-tests.log，合计10项不同检查通过。两项legacy检查
  created_class_test第132行、record_accessor_test仍失败：将本次改变的
  两个生产翻译单元重新编译为HEAD原投影/值流实现并优先链接后同样失败，
  /tmp/abcd-class-observer-baseline.log；这是有界链接对照，非整个HEAD重建。
  没有把这两项算作通过，失败日志及fixture修复日志同前缀保留。
- fresh build/stage-one/value-class-observer-pass-1 固定五输入退出0，SHA
  匹配现有abc-index.json，仍80 complete /26 incomplete /0 failure /
  46 no-module，无legacy或完整模块回退。106份完整/部分源码逐字未变且
  语法解析通过，/tmp/abcd-class-observer-content.log及syntax.log。
  full-suite/扩展语料/RSS/基准/DFX保持暂停。Goal active；剩余数值输入域、
  构造协议、跨块安装及遗漏正文继续阻断阶段一完整语义/源码表示验收。

### 2026-10-04：literal 嵌套采用共同源码求值能力，移除独立类型准入列表

- 规则revision73。EvaluationShape 增加 typed SourceEvaluation，描述直接
  表达式、纯值、重复snapshot输入、暂停、调用帧协议及尚无源码降低的操作。
  LiteralExpression 删除 PrimitiveLeaf/LiteralUnary 和 call/construct/read/
  closure/binary 类型列表，消费同一形状、输入元数、eager语义及Language域。
  PredicateSnapshot 也复用共同重复输入描述；没有新增样本/布局匹配。
- literal 原有所有权、Reference、效果/异常区间、捕获环境和逃逸证明保留。
  模块TDZ仍依赖真实guard证明；纯copy/string不得隐藏附加效果。嵌套输入
  复用 CheckEvaluationUse 核对角色/purpose；未知值的VM谓词不得重复执行
  getter/其他owned表达式。早先存储值可作为snapshot，已证明Boolean的输入
  可采用共同单次降低；await/yield、super及IC var lookup的既有缺口未放宽。
- 新真实DEFINEFUNC occurrence/ABI上下文的编码fixture经输入、ValueIR、
  literal/evaluation、FunctionTree、tree semantics、ABI和instruction sources
  恢复嵌套对象里的this与当前函数身份、普通调用及复制/NOP变体。旧实现在
  evaluation阶段拒绝，/tmp/abcd-literal-capability-red-test.log。1/2/200/201/
  402对象与线性工作增长、共享值、伪造所有权、额外效果及debugger负例通过。
- 新272组独立源码行为对照通过，含严格destination下primitive/nullish/
  object receiver、函数自身身份、构造调用/不同newTarget、Proxy callback、
  任意抛出值、属性描述符、早先getter的一次读取及VM谓词。谓词部分为共同
  ValueIR/AST语义fixture，不冒充原ABC协程/控制恢复。纯binding copy单次
  使用正例通过；多个初始化器共享copy仍由使用普查拒绝，旧LateValue断言
  更新为Observed，未删除反例。fixture源码不进入引擎。
- -j4构建退出0：/tmp/abcd-literal-capability-verified-build.log，另copy、
  branch、semantic构建日志同前缀。24项相关定向检查通过：tests.log中17项、
  copy-tests.log中的literal_plan_test复验、branch-tests.log中5项及
  pasteboard-tests.log中1项。初次失败及修复记录保留，没有算入通过。
  full-suite/扩展语料/RSS/基准/DFX保持暂停。
- fresh build/stage-one/value-literal-capability-pass-1 固定五输入退出0，
  SHA均匹配现有abc-index.json；仍80 complete /26 incomplete /0 failure /
  46 no-module，无legacy或complete回退。106份完整/部分源码内容逐字未变，
  语法解析通过，/tmp/abcd-literal-capability-syntax.log；导出/比较日志同前缀。
  此次实现关闭literal独立表达式准入列表事项，未解决其余跨块/相位组合
  限制或26份不完整模块的数值、构造协议和遗漏正文。Goal active，完整
  阶段一语义及源码表示验收仍未达到。

### 2026-10-04：恢复准入条件当前复核，补充 literal 嵌套能力列表

- 基线66e48615。历史引用同块校验的来源和删除再次核实；当前 VerifyCalls
  没有同块条件，仍有 inline-owned callee 表示边界。AGENTS.md 中禁止在
  共同语义证明之后追加布局、邻接、producer列表或selection数量准入的
  规则继续有效；没有新增生产特例。
- [条件审计](shape-restrictions-audit.md) 补充 LiteralExpression/LiteralUnary
  的独立表达式类型列表，当前13类开放事项；同时明确 class 投影的 claim
  与外部观察者同块限制也需共同替换，仅放宽候选发现不能完成修复。
  区分输入元数、数值域、重复snapshot读取和暂停语义等真实义务，不将所有
  typed dispatch 误判为特例，也不盲删校验。
- 现有build重跑5/5调用/快照/选择定向回归15.10秒通过，日志
  /tmp/abcd-reference-shape-current-tests.log。本次只改文档，未重新构建；
  full-suite/扩展语料/RSS保持暂停。尚未通用替换所有13类限制，阶段一
  完整语义/源码表示验收仍开放。

### 2026-10-04：调用、构造与成员链使用共同语言语法分组

- 规则 revision72。共同 TreeOperatorContext 增加 typed PostfixContext，
  区分 member base、call callee 与 new 的 MemberExpression 目标，并接入
  既有优先级表。printer 仅消费 AST 和该语法上下文，不新增字节码、样本、
  同块、邻接或 producer 准入条件，也不改 callee/receiver/effect 证明。
- 正常/异常、range/apply 和 record/member 复用同一打印路径：
  `(new (Ctor)(arg))`→`new Ctor(arg)`，`(object.method(arg))`→
  `object.method(arg)`。构造上下文沿 member base 传播，工厂结果可写为
  `new (factory()).Ctor(arg)`，不误解为先构造 factory 后调用 Ctor。
  数字 base 保留 `(1).field` 分组；binary/unary/logical 优先级继续共同处理。
  值调用仍为 `(0, callee)(args)`，保留 receiver 擦除及 indirect eval；逗号
  内部冗余括号移除。语法依据 ECMA-262 13.3 Left-Hand-Side Expressions、
  13.3.5 EvaluateNew、13.3.6 EvaluateCall：
  https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-left-hand-side-expressions。
- 新增14个共同 ValueIR→AST/printer 场景；逻辑/数字两项明确为 AST 语法
  fixture，不冒充原 ABC 数值域或协程证明。2538组独立 postfix 行为对照
  通过，覆盖工厂、长 member 链、bound/Proxy、getter、receiver、newTarget、
  身份、primitive/object返回与任意异常。1/2/200/201/402/804链和既有
  binary组合使用迭代 action stack，线性工作增长检查通过；未恢复RSS/基准。
- -j4相关生产和测试目标构建退出0，/tmp/abcd-postfix-verified-build.log；
  新增长链目标构建退出0，/tmp/abcd-postfix-chain-build.log。20项相关定向
  检查通过（final-tests.log 中18项、default-final-tests.log 中CardArray/Logger
  两项），日志前缀 /tmp/abcd-postfix-。旧括号表示断言与CardArray的反语义
  mutation更新到新拼写，其行为对照和错误索引拒绝仍保留。
- 另运行的历史 record_function_test 在期望 `$local0` 导出名的第134行失败。
  用 HEAD 的旧 printer/token 源码单独编译并链接同一fixture，在同一断言
  复现；未将它算作通过或本次打印回归，未改该无关旧命名断言。证据
  /tmp/abcd-postfix-baseline-build.log、baseline-test.log。全套仍未运行。
- fresh build/stage-one/value-postfix-pass-1 固定五输入退出0，仍80 complete /
  26 incomplete /0 failure /46 no-module，无legacy或complete回退；55份完整
  模块应用新的语言表达写法。80份完整源码共445080→435138字节只是产物
  变化统计，不是性能或整体表示质量验收。106份完整/部分模块语法解析通过，
  /tmp/abcd-postfix-final-syntax.log；比较见final-batch-summary.json。
- 此项推进阶段一的源码表示要求，没有解决26份不完整模块的数值、构造协议
  和其他正文缺口，也未声称整个record/平台/全阶段语义或源码风格已验收。
  Goal active；下一项继续从共同输入/效果/控制证明处恢复遗漏正文，不能
  以未知语言值假设或宿主constructor类型假设消除真实runtime差异。

### 2026-10-04：原调用帧 super 转发贯通共同 IR 与隐式派生构造器

- 规则 revision71。SuperForward 保留 typed invocation-frame 参数来源、
  constructor SSA 身份、ArgumentsRead 效果及真实 frame fact；normal/exception
  输入、值导入和 EvaluationPlan 的构建/验证共用 NeedsArgumentFrame 能力，
  不再仅为 GETUNMAPPEDARGS 保存隐含输入。spread 的 runtime 类型限制未放宽。
- ProveImplicitConstructor 在原 CFG、共同 DerivedValueStates 和寄存器访问
  窗口上证明：每条正常路径恰好转发一次、实际 CallableEntry、未改变的原
  参数/newTarget、无其他可观察效果、真实 this guard 和返回 super receiver。
  phi 经稀疏依赖传播，可有多个互斥分支调用；不调用旧 forwarding prologue
  模板，不按样本、固定寄存器、PC、同块或邻接分派。
- SourceTree 保留原始节点与来源，同时显式记录 implicit constructor 形式；
  tree operands 重新证明整个函数，ABI 核对 source length0。共同 class printer
  输出默认构造器注释并省略显式 constructor，来源 fragment 明确标记 implicit。
  原调用帧转发不读 iterator，不能写成 super(...args)。语义、复杂度及未证明
  范围见[隐式构造器设计](implicit-constructors.md)和[条件审计](shape-restrictions-audit.md)。
- -j4 相关生产/测试构建退出0，日志 /tmp/abcd-super-forward-verified-build-2.log；
  最后 profile/exception 测试目标构建退出0，profile-build.log 同属该前缀。
  15/15 定向回归32.10秒通过，/tmp/abcd-super-forward-final-green-tests.log。
  新 fixture 经共同输入、值、projection、evaluation、tree 和普通 ABI 验证，
  覆盖寄存器复制、0/1/200/201间隔、多分支转发及身份/次数/效果/返回反例。
  新增 lifting 测试使用既有1202/1206/1301语义 profile；未扩展2400支持。
- 独立 paired TS 只进入测试，引擎只读 ABC。185组直接/bound/Proxy parent、
  动态 parent/newTarget、原始参数身份、iterator getter、返回/抛出及200/201
  参数对照通过；另70组 Backup async方法/getter/异常对照通过。未宣称
  EntryAbility 的所有 instance fields、宿主平台或阶段一完整风格/语义验收。
- fresh build/stage-one/value-super-forward-pass-2 固定五输入退出0：80 complete /
  26 incomplete /0 failure /46 no-module，Immersive8917/9308新完整输出2083/697
  字节；原78份complete源码逐字未变，无legacy和complete回退。日志与比较
  /tmp/abcd-super-forward-verified-batch.log、verified-batch-summary.json。
  两份新模块语法解析通过，/tmp/abcd-super-forward-final-syntax.log；语法与
  source_complete不代替整阶段独立行为/风格验收。
- 当前 native proof 仍未覆盖循环终止、异常相位、额外效果/词法环境，以及
  调用前写入后恢复的 frame 值等共同证明缺口；range/spread/legacy 模板和
  审计中的其他开放项未全部替换。Goal active，阶段一/object creators全组
  目标未完成；full-suite/扩展语料/RSS/基准/DFX/定期审视保持暂停。

### 2026-10-04：按用户要求复核引用同块校验，扩展局部恢复条件审计

- 历史 c9c8a93b 的 property/call 同块条件为早期块内求值证明边界，非成员
  调用语义；766afe0c 已移除该条件和 cross-flow 例外枚举。当前 VerifyCalls
  通过共同 EvaluationPlan 的新鲜验证证明顺序，保留原 callee/base/receiver
  身份。仍有 inline-owned callee 的表示边界，不能声称任意共享引用已支持。
- 更新[当前条件审计](shape-restrictions-audit.md)：复核既有七类事项，并新增
  确认共同 class definitions/initializers 同块候选、phased literal phi 同块
  输入、legacy super 参数邻接、array/class 连续整理和 module Hole guard
  邻接。逐项区分真实语义义务、实现边界和通用替换方向；非全生产逐行审计。
- AGENTS.md 明确禁止在共同身份/可用性/执行次数/效果/异常证明之后另加
  块布局、邻接、producer 列表或 selection 数量的准入条件。现有缺口须公开，
  不能通过扩大局部模板掩盖；变形布局与反语义场景进入定向验证。
- 本次为规则/审计文档更新，没有扩大生产接受域。现有 build 的5/5定向
  回归15.11秒通过，/tmp/abcd-reference-shape-audit-tests.log；另直接验证
  270组通用快照场景。pasteboard 测试覆盖240组callback配对，record仍为
  incomplete。未重新构建工作区未完成的 SuperForward 修改；其15个源码
  改动保留，未纳入本次提交。full-suite/扩展语料/RSS/定期审视继续暂停。
  Goal active，阶段一与 object creators 全组目标尚未完成。

### 2026-10-04：成员函数族输入失败保留独立类成员，统一定义成员缺口

- FamilyReader 在读取根输入前记录请求的 root，DFS 中继续保留真实失败
  method、parent、creation PC。ClassMemberOmission 复用这份来源，将根或
  任意深度后代的原始 InternalValue 输入失败标记为整个所属成员未恢复。
  不造 callback body/signature，不把资源、损坏输入、捕获或后续树证明失败
  转成缺口。正常与异常相位复用同一机制，没有按样本、PC、opcode序列或
  嵌套深度分派；构造器及静态初始化器规则未放宽。规则revision70。
- DEFINEMETHOD 的普通成员、getter/setter 与 class buffer 成员共用缺口表示。
  父树逐个核对原定义、identity、kind及实际创建PC；family writes 只证明
  原 class 创建屏障，不授予缺失函数族的内部写入/ABI/source证据。
  BodiesPrepared 对所有缺口返回false，打印与record均保留 incomplete。
- 根输入、1/2/3层真实嵌套、三动态profile、普通/异常输入、literal/defined
  成员、200/201 occurrence验证通过。缺失/错误root、缺失缺口、body与
  omission冲突、父创建来源不完整、损坏寄存器、内存/工作预算负例保留。
  accessor测试另核对父树与record-family整条组合路径及不完整状态传递。
- -j4生产及相关目标构建退出0，/tmp/abcd-member-family-gap-final-build.log；
  13/13定向回归6.98秒通过，/tmp/abcd-member-family-gap-final-tests.log。
  84个独立retained setter场景与486个Content方法/callback配对场景通过；
  配对TS仅进入测试，引擎只读取ABC。/tmp/abcd-member-family-gap-paired.log。
  未运行遗漏initialRender、UI平台或整个record/constructor的语义验收。
- fresh build/stage-one/value-member-family-gap-pass-3 五输入退出0，78 complete /
  26 incomplete /2 failure /46 no-module。15个整record failure→incomplete，
  新保留Content(2245字节)、CommonView(20203字节)及其他独立成员；原78份
  complete源码逐字未变，无legacy。final-batch.log、final-batch-summary.json
  位于/tmp/abcd-member-family-gap-前缀下；15份新部分模块仅语法解析通过，
  /tmp/abcd-member-family-gap-syntax.log，不等于执行或完整语义验收。
- 剩余整record failure为Immersive 8917/9308；两个constructor 9167/9544
  的PC12均为callruntime.supercallforwardallargs，FamilyReader明确报告
  SemanticFailure::kUnsupportedInstruction。LiftSuperInstruction已有typed
  SuperForward，共同SemanticInstruction/ValueIR尚未接入。证据为
  /tmp/abcd-member-family-gap-next-root-failure.log和next-*-constructor.json。
  pinned runtime_stubs-inl.h::RuntimeSuperCallForwardAllArgs检查IsConstructor，
  从原调用frame直接转发全部参数；不能混同JSFunction限制的super spread，
  也不能用ProveForwardingDerivedConstructor的直线状态序列补一个样本。
  下一项需在共同frame/SSA/this状态模型保留直接转发与可观察参数修改。
  数值/tag输入、派生super spread与遗漏函数体缺口继续开放。缺口注释包含
  所属成员、实际失败method及parent/creation PC，不能以failure数量下降
  冒充body覆盖率提升。Goal active，阶段一/object creaters原范围未完成。
  full-suite/扩展语料/RSS/基准/DFX/定期审视保持暂停。

### 2026-10-03：异常选择转发保留值，原始 phi 边归属与不变表达式贯通

- FindPhasedPhiSelections 复用 HasRetainedEvaluationValue 与既有复制别名索引，
  惰性分支可转发已有 binding，首次 phi/catch materialization 仍保留。输入
  表示复用 SelectionSnapshot；原始异常边、copies、可用性及分支效果仍验证。
- PhaseSelectionCalls 将当前选择的真实 phi-input edge 计入同一参数表达式
  的观察归属；外部phi、其他call的较早参数仍拒绝。不是因为没有producer
  就跳过值/效果检查，也不靠样本名、固定PC或新opcode序列分派。
- PhaseCorridors 对无效果、无异常、Language结果且只含literal输入的共同
  eager表达式，允许跨CFG分块内联。该类不读可变binding，按共同求值语义
  判断；phi/catch首次复制与其他有状态输入继续走原有控制/顺序检查。
  规则revision69，旧单selection/corridor候选边界尚未全部替换。
- Push record5374 failure→incomplete，输出4601字节，恢复 EntryAbility 的
  onCreate/onNewWant、colorMode catch、token注册及其他成员；constructor
  PC8仍未恢复。整个record/平台/真实Promise callback执行未验收。
- 三既有动态profile ×1/2/200/201，覆盖入口、前置getter、调用后的保留
  snapshot与较早literal参数；含分支效果拒绝、丢失求值计划及错换分支值
  的反例。2912 native场景与3312配对方法场景通过；配对TS只进入测试，
  引擎命令只读取ABC。/tmp/abcd-phase-retained-ownership-native.log、paired.log。
- -j4生产及相关目标构建退出0，final-build.log；14/14相关回归85.56秒通过，
  /tmp/abcd-phase-retained-ownership-final-tests.log。最终分支值负例目标重建
  退出0，/tmp/abcd-phase-retained-ownership-negative-build.log；最终3/3回归
  64.90秒通过，/tmp/abcd-phase-retained-ownership-negative-tests.log。
- fresh build/stage-one/value-phased-retained-pass-1 五输入退出0，78 complete /
  11 incomplete /17 failure /46 no-module；已有完整源码逐字未变，无legacy。
  /tmp/abcd-phase-retained-ownership-final-batch.log、final-batch-summary.json。
  Push现无failure，7份incomplete均保留constructor PC8；其余缺口仍包括
  WindowUtil/其他record的数值输入、派生super spread等运行时语义边界。
- Goal active；阶段一及object creaters原范围保持，全record语义/表示验收
  尚未达到。full-suite/扩展语料/RSS/基准/DFX/定期审视保持暂停。

### 2026-10-03：共同保留值分类贯通后续选择表达式，恢复 TestAbility 方法

- 新 HasRetainedEvaluationValue 共享入口参数/receiver、phi、catch binding 与
  已有 operation temporary 的表示事实，选择表达式与调用 receiver 复用。
  不增加 producer 白名单、样本身份或局部序列分支；原输入/求值验证仍承担
  可用性、phi copies、单次执行与效果顺序。该查询不授权移动定义或重复 getter。
- 更早汇合的 phi 现可作为后续条件表达式的保留输入。Push record4224 从
  failure→incomplete，输出2326字节，恢复 onCreate/onNewWant 及其他成员；
  constructor PC8 缺口保留。规则revision68。
- 1440组独立 native 对照通过，涵盖原 JEQZ predicate、phi identity、getter
  改写、receiver 与异常顺序；1584组配对 TS 方法场景通过，配对源码只进入
  测试，引擎只读取ABC。未调用不完整constructor，不计作record/平台验收。
  /tmp/abcd-retained-values-paired.log；对应两项已注册CTest并再次通过。
- -j4生产及相关目标构建退出0，/tmp/abcd-retained-values-final-build.log。
  12/12相关回归23.23秒通过，/tmp/abcd-retained-values-final-tests.log；包含
  缺少phi计划时拒绝、调用身份/顺序与已有选择表达式回归。
- fresh build/stage-one/value-retained-selection-pass-2 五输入退出0，78 complete /
  10 incomplete /18 failure /46 no-module；已有完整源码逐字未变，无legacy。
  /tmp/abcd-retained-values-final-batch.log、final-batch-summary.json。
- 下一阻断已定位：Push record5374 的 phased member reference，原callee PC242、
  call PC298，callee仍保留为root；/tmp/abcd-retained-next-reference.log。
  需将保留值/选择恢复贯通异常相位，不能删除引用身份检查或增加局部模式。
- Goal active；阶段一、object creaters全组及独立语义/表示验收尚未完成。
  full-suite/扩展语料/RSS/基准/DFX/定期审视保持暂停。

### 2026-10-03：ValueTree rest 参数从 CFG/SSA 来源恢复，移除固定前导布局

- ResolveFunctionSyntax 不再依赖 ProveRestPrologue 的固定复制数量/位置/寄存器
  排列。新 ProveRestOwnership 检查 owner argument window、独立 allocation、
  原 CFG 前驱效果/重复执行、physical tail 输入写入、SSA transport 的可观察
  使用。资源/工作超限类型化返回，未根据样本或局部序列扩大白名单。
- [审计替换进度](shape-restrictions-audit.md) 标明本项覆盖 normal ValueIR；
  legacy writer/FindRestLocal/专用派生构造器的旧模板尚未全部替换。原语义
  义务保留：额外数组、GETUNMAPPEDARGS 和原始 scalar argument 不能当同一rest。
- 三既有动态profile、prototype/static 的实际文件内函数族覆盖 copy重排、NOP
  和rest暂存local改变，48 complete families /24 protocol refusals原生对照通过。
  /tmp/abcd-rest-origin-native.log。额外0/1/200/201 NOP、physical scalar使用、
  先前observable效果和精确资源/工作预算通过。
- -j4生产和相关目标最终构建退出0，/tmp/abcd-rest-origin-final-build.log。
  6/6相关回归4.60秒通过，/tmp/abcd-rest-origin-tests.log。规则revision67。
- fresh build/stage-one/value-rest-origin-pass-1 五输入退出0，仍78 complete /
  9 incomplete /19 failure /46 no-module；完整源码逐字未变，无legacy。
  /tmp/abcd-rest-origin-final-batch.log、final-batch-summary.json。signature来源
  扩展不代表remaining constructor/数值/其他body缺口已解决。
- Goal active；阶段一与object creaters原范围不变。full-suite/扩展语料/RSS/
  基准/DFX/定期审视保持暂停，工程计费不作为整进程峰值证明。

### 2026-10-03：删除引用同块门槛与快照 producer 白名单，两真实 record 出稿

- CallPlan 删除 property/call 同块及手工 cross-flow 分类；共同 EvaluationPlan
  新鲜独立验证承担定义/使用、可用性、单次所有权和事件顺序，引用仍核对原
  callee result、base/receiver、原使用关系。未删除正确性义务。
- 谓词快照从共同 EvaluationShape 的 expression/eager 语义能力选择 producer，
  不再用 getter/call 等 opcode 白名单；原正向区间覆盖全部事件，其他使用保留
  private snapshot。未增加样本路径/名称/局部序列分派。
  [审计替换进度](shape-restrictions-audit.md) 明确还保留的候选范围，不宣称七类
  问题全部修复。规则 revision66。
- 原字节码回归含多层调用结果的 nullable 选择、1/2/200/201表达式和伪造快照
  使用拒绝。270组独立 native 场景通过，涵盖早期nullish、identity、原callee /
  receiver、callee改写和任意异常；/tmp/abcd-expression-snapshot-native-final.log。
- 240组配对 pasteboard onWindowStageCreate/error callback 场景通过，
  /tmp/abcd-reference-paste-paired.log。配对TS仅进入测试，引擎命令只接收ABC。
  未构造不完整派生类、调用updateBreakpoint或验收平台/整个record。
- -j4生产及相关目标最终构建退出0：/tmp/abcd-reference-model-final-build.log。
  12/12相关回归33.84秒通过，/tmp/abcd-reference-model-tests.log。
  新增早期nullish和配对测试注册后2/2通过，reference-model-paired-tests.log。
- fresh build/stage-one/value-expression-reference-pass-1 五输入退出0，78 complete /
  9 incomplete /19 failure /46 no-module；已有完整源码逐字未变，无legacy。
  pasteboard record4767输出3487字节，MultiCommunity record40957输出2682字节，
  两者failure→incomplete。仍分别保留constructor和updateBreakpoint的两个缺口。
  /tmp/abcd-reference-model-final-batch.log、final-batch-summary.json。
- Goal active；阶段一与object creaters原目标保持，完整语义/表示验收未完成。
  full-suite/扩展语料/RSS/基准/DFX/定期审视没有恢复。

### 2026-10-03：按用户要求审计局部形状限制，禁止追加样本匹配

- 用户明确禁止因为样本增加条件匹配。本次已加入 AGENTS.md §1a：样本只作
  故障发现/独立测试，恢复按 ISA 语义、通用 reference/def-use/控制依赖/效果
  与异常分析；不能追加局部序列、固定寄存器/PC、graph 形状或 producer 白名单。
  真正的身份/receiver/作用域/异常/资源义务保留，但不再把某个编译器布局当定义。
- [审计报告](shape-restrictions-audit.md) 明确限定本次范围。src/include/tools
  的身份文本搜索未发现列举的应用名称/样本路径生产语义分派；文本搜索不是
  全分支证明。人工核对定位七类形状限制，分列 P1/P2 与通用替换机制。
- 当前 callee/call 同块并非语言要求，是尚未提供通用引用顺序证书的实现门槛。
  已撤回未提交的 ValueInvoke 谓词快照白名单扩展；不靠为 pasteboard 再增加
  producer 形状解决它。源码已恢复 87176c10，生产工具重建退出0，
  /tmp/abcd-shape-audit-restored-build.log；git diff --check 通过。
- 本次为明确请求的专项审计，不恢复定期审视，不声称已改完七类实现。
  pasteboard record4767 仍失败；下一实施交付是共同 Reference/有序表达式模型，
  处理 normal/phased、多条件参数、复制/分块、getter、receiver 和异常快照。
  阶段一及 object creaters 原范围不变，Goal active；既有暂停项保持。

### 2026-10-03：父级模块初始化贯通异常成员，恢复 setImmersiveType

- 从真实 method8539 PC15 定位：class 创建前的本地模块初始化未传到 phased
  reader。共享 IsCreatorOf 验证创建 identity/record/PC/typed operation，normal
  与 phased 复用；异常输入保留 immutable 初始化 premise，独立 importer、
  ModuleGuards、实时 alias、Before/Commit、单事件顺序和 family-write 再核对。
  [实现与边界](function-tree-output.md) 没有制造正常输入或冻结模块值。
- 创建前/后、Conditional/Closed、1/200/201 实际读取、缺失 premise/guard
  声明和工作预算通过。已有带 guard 的读取继续保留配对异常证明；单读取不
  伪造 guard 或搬移 catch copies。root/normal-parent phased child 的原 store
  Language 域重新验证；ownerless 初始化 publication 仍拒绝。
- -j4 生产与相关目标构建退出0，/tmp/abcd-phase-module-final-build.log、
  /tmp/abcd-phase-module-related-build.log、unit-final-build.log。
  11/11相关回归14.37秒通过，/tmp/abcd-phase-module-tests.log。规则 revision65。
- WindowUtil setImmersiveType(type) 正文现恢复，余两个明确数值缺口：8620
  PC127、8431 PC34。输出14046字节。1428组配对 prototype 方法/callback
  对照通过，/tmp/abcd-phase-module-paired-2.log；含新增 setter/enum getter/
  receiver/异常。配对 TS 仅进测试，enum/类分开转换防止 Node 内联 enum 常量
  掩盖 ABC 的属性读取。没有构造不完整类或调用遗漏方法，不计为平台/record验收。
- fresh build/stage-one/value-phase-module-pass-1 五输入退出0，仍78 complete /
  7 incomplete /21 failure /46 no-module；已有完整源码逐字未变，无 legacy。
  /tmp/abcd-phase-module-final-batch.log、final-batch-summary.json。
- Goal active，阶段一及 object creaters 全组未完成。full-suite/扩展语料/RSS/
  基准/DFX/定期审视均未恢复；当前工程计费不是整进程峰值证明。

### 2026-10-03：类成员输入缺口保留其他恢复结果，真实 WindowUtil 出稿

- 实际 record7618 先在 method8620 phased kValues 阻断整个类；保留原输入失败
  后定位为 PC127，再发现 8539 PC15、8431 PC34。新 `ClassMemberOmission`
  从实际成员根的 normal/phased kInternalValue 生成，保存原 ClassTreeContext，
  未创建 FunctionTree、body、ABI 或 complete receipt。共享 ClassTree/printer
  输出 wire-order 缺口注释和 INCOMPLETE class，其他 PreparedFamily 不被丢弃。
  [边界与实现合同](function-tree-output.md) 未放宽数值/NaN 域，也未读取 sidecar。
- DraftPrepared 核对缺失槽/occurrence/identity/原创建PC/property/typed reason；
  BodiesPrepared 对 omission 拒绝，phased parent 独立性和完整 default-export
  移动仍拒绝。原 class barrier 只证明 parent family-write 覆盖，不证明缺失
  成员写入。失败子函数、后续 family/tree、损坏输入、限额和 initializer/ctor
  失败保留原拒绝；不 catch-all、不合成空方法或假异常正文。
- 原 ExceptionInputFailure 和归一化后的原始 PC 进入共同失败投影；normal /
  phased 及 file/family 准备保留失败分析费用，恢复前保守计费。成员槽线性扫描，
  omission arena 两倍增长；固定数字缓冲打印注释。normal/try-catch、重复模板
  3/200/201 个实际成员、缺失/错配 omission、完整模式和损坏输入边界已验证。
- -j4 生产及相关目标构建退出0：/tmp/abcd-member-gap-verified-build.log。
  13/13相关回归50.91秒通过，/tmp/abcd-member-gap-tests.log；强化原失败
  kind/PC/费用断言后单测通过1.19秒，final-proof-build.log/test.log。
  新增 INCOMPLETE class 标记后生产构建退出0，marker-build.log；最终2/2
  通过3.96秒，/tmp/abcd-member-gap-marker-tests.log。规则 revision 64。
- 572组配对 WindowUtil setUIContext、setSystemBarEnabled/callback 场景通过，
  /tmp/abcd-member-gap-paired.log。配对 TS 仅进独立测试；引擎工具只接收 ABC。
  此测试以完整 prototype 方法隔离调用，未构造不完整类、调用缺口成员或
  验证平台 API；不计为 record 执行/语义/源码风格的完整验收。
- fresh build/stage-one/value-member-gap-pass-2 五输入退出0，78 complete /
  7 incomplete /21 failure /46 no-module。WindowUtil record7618 failure →
  incomplete，13155字节保留其他成员与 WindowInfo 构造器/字段，仍有上述三个
  明确缺口。既有完整源码逐字未变、无 legacy，native default 记录仍保留。
  证据 /tmp/abcd-member-gap-marker-batch.log、marker-batch-summary.json。
- Goal active；阶段一与 object creaters 全组未完成。未恢复 full-suite、扩展
  语料、RSS/基准、DFX 或定期设计审视；此次费用计数不是完整进程峰值证明。

### 2026-10-03：Apply 构造复用直接函数来源证明

- `DirectFunctionObject` 共同类型接口从原 canonical SSA 创建来源区分类/函数，
  CallPlan 与最终 AST 分别核对。直接 DEFINEFUNC 现进入原 apply 数组、顺序、
  native ConstructExpr 路径；保留完整真实 child 正文/捕获/ABI，不新增 writer。
  [固定运行时语义与边界](object-creators.md) 仍拒绝未知参数/getter/Proxy 来源，
  不给任何宿主函数授予 lifetime brand；arrow 保留原构造 TypeError 行为。
- 文件内实际 Code 实体、DEFINEFUNC occurrence 与 constructor 正文先复现拒绝，
  后贯通 PreparedFamily/共享打印；三 profile、两 cache 宽度、normal/try-catch、
  空/两个 getter 参数，连续 1/2/200/201 构造通过。隐式 new.target 读取仍由
  原 owner/body gate 拒绝，身份接口不代替该证明。没有读取配对源码做恢复。
- 6148 组 native 独立 length/index 参数 oracle 通过，核对返回值、对象/函数
  身份、getter 顺序、任意异常值、arrow 非可构造路径及 Array/iterator/
  Reflect.construct/Function.prototype.apply/继承索引污染。日志
  /tmp/abcd-apply-function-native.log；这是 Node 对照，不是 Ark VM 验证。
- -j4 生产和全部相关目标最终构建退出0，日志
  /tmp/abcd-apply-function-verified-build.log。8/8相关回归68.56秒通过，
  /tmp/abcd-apply-function-tests.log。新增 fixture 的首次红测退出134，
  /tmp/abcd-apply-function-red.log。规则 revision 63。
- fresh build/stage-one/value-apply-function-pass-1 五输入退出0，仍为
  78 complete /6 incomplete /22 failure /46 no-module；没有完整 record 回归，
  既有完整源码逐字未变，无 legacy。日志
  /tmp/abcd-apply-function-final-batch.log、final-batch-summary.json。
  固定五输入没有 apply 使用；本项扩展不提高这批 record 完成数量。
- object creaters 全组及阶段一 Goal 仍 active。regexp、excluded-keys、历史
  buffer profile、完整 generator/async-generator 和未证明 apply 输入仍开放；
  full-suite/扩展语料/RSS/基准/DFX/定期审视未恢复。

### 2026-10-03：嵌套成员引用跨已验证条件参数，真实 Push 拒绝消除

- 复现实际 Push method3613 / outer Invoke69 / callee29 的 kReference。
  PhaseSelectionCall 现保留原 ancestor callee/call 链；phase_references 按 OpId
  索引 actual callee UseId。原 local forest、receiver 与 AST 操作数校验仍独立。
  [顺序与异常证明](phased-call-references.md) 不使用样本名称、打印文本或源 sidecar。
- 单入口/出口原 commit 前缀只接受纯操作和子引用已有的效果依赖，异常签名
  与调用相同、无 catch/phi 暂存；独立效果、较早 effectful 参数与多 selection
  owner 不放开。最终 owner 反向索引一次，ancestor 前缀不重叠，向量几何准入。
  Field/call/use/index 声明丢失或伪造均拒绝，不重读 getter，不借助 .call。
- 三动态 profile 覆盖 1/2/200/201 连续调用，单链深度 1/2/200/201 也通过。
  78624 组 native 场景含两级 getter/receiver、callee 改写、非 callable、短路与
  各处异常。新增独立效果前缀拒绝。572 组真实配对 Push registration/callback
  场景通过；配对 TS 只作为测试 oracle，运行引擎的工具命令只接收 ABC。
- -j4 生产及相关目标构建退出0；11/11相关回归88.60秒，日志
  /tmp/abcd-call-spine-related-build.log、/tmp/abcd-call-spine-related-tests.log。
  新增 barrier/计费注释后最终5/5通过55.54秒，/tmp/abcd-call-spine-final-build.log、
  /tmp/abcd-call-spine-final-tests.log。规则 revision 62。
- fresh build/stage-one/value-call-spine-pass-1 五输入退出0：78 complete /
  6 incomplete /22 failure /46 no-module；既有完整源码逐字未变，无回退/legacy。
  Push record3444 从 failure 转 incomplete，输出6425字节注册方法及 callbacks；
  剩余 derived constructor PC8 仍标记缺口，不构造此不完整类做完整行为声明。
  配对测试以 prototype 方法调用隔离该缺口，不代表 record/platform 验收完成。
- 证据 /tmp/abcd-call-spine-final-batch.log、batch-summary.json、
  /tmp/abcd-call-spine-push-record.jsonl、/tmp/abcd-call-spine-paired.log。
  Goal active；继续真实阶段一与 object creaters 未完成目标，full-suite/扩展语料/
  RSS/基准/DFX/定期审视均未恢复。

### 2026-10-03：创建表达式进入共同字面量与异常相位组合

- 只读检查固定五份 ABC 全部方法：解码失败为 0，regexp/excluded-keys/
  newobjapply 未出现；现代缓冲对象分别 524/1480/179/62/2 次，其 literal
  均不含 method/accessor tag。未读取配对源码或修改输入；不以未出现缩减
  用户要求的完整 object creaters 覆盖。日志 /tmp/abcd-creator-census-summary.log、
  /tmp/abcd-buffer-creator-census-summary.log。
- 新真实字节码先复现迭代结果作为对象 initializer 时的求值规划拒绝。
  LiteralPlan 与 PhaseCorridors 现将 ValueIterResult 视为拥有输入的创建表达式，
  复用原嵌套所有权、效果区间及异常签名检查。共同 Lower/ObjectExpr/property
  arena 输出嵌套的空/缓冲对象及 native 成员调用参数，没有 group writer。
  原 fresh identity、done 精确 true 转换及所有输入/来源验证保持；共享/逃逸
  record 与删除 nested ownership 的反例仍拒绝。规则 revision 61。
- 三动态 profile、normal/phase、直接/computed/Boolean flag，以及
  1/2/200/201/402 连续创建组合通过。3179 组独立 native 对照验证两层对象
  身份、descriptor、getter 顺序、callee 改写、receiver、.call 污染和任意异常值。
- -j4 生产及相关目标构建退出 0；12/12 回归 82.74 秒，日志
  /tmp/abcd-record-composition-related-build.log、
  /tmp/abcd-record-composition-related-tests.log。原 iterator-result、apply、
  literal/constructor、class/generator 及收据 revision 回归均在此次范围内。
- fresh build/stage-one/value-record-composition-pass-1 五输入退出 0，仍为
  78 complete /5 incomplete /23 failure /46 no-module；完整源码逐字未变，
  无回退/legacy。证据 /tmp/abcd-record-composition-final-batch.log 和
  /tmp/abcd-record-composition-final-batch-summary.json。此次未新增完整 record。
- Goal active；实际阶段一仍需解决真实成员调用/控制/数值等阻点，同时保留
  object creaters 未完成的 regex/rest/历史格式及创建 owner 协议。full-suite、
  扩展语料、RSS/基准、DFX 和定期审视均未恢复。

### 2026-10-03：apply 构造复用共同参数接口与类创建证明

- NEWOBJAPPLY 的窄/宽 cache 编码进入同一个 ApplyConstructorCall /
  ValueConstructApply、register/value/exception 输入、OperandView、求值规划和
  CallPlan。constructor 与 acc 数组保留为两个输入，缓存元数据保留；索引读取、
  构造副作用和异常均未标成纯操作。未添加 sample writer 或 opcode 名字派发。
- ConstructArguments 以 variant 复用已有 ArgumentRange 和拥有原创建子树的
  ArrayArguments。完整且未逃逸的数组在顺序允许时输出直接 native 参数；
  存在后续 getter/观察时保留原数组快照，按连续索引取参，复用原 MemberExpr。
  没有 spread、Reflect.construct 或可变全局工厂依赖。原创建、初始化及参数
  来源区间保留；容量准入与 canonical 使用普查按操作/使用/输出数线性计费。
- 复核固定 NewObject / Proxy::ConstructInternal 发现重要差异：Proxy target
  不可构造时，固定 C++ 路径可能仍读取执行 construct trap；JS new 先拒绝。
  初期“任意 constructor”对照因此不作为最终验收。现 CallPlan 及独立 AST 检查
  均要求真实 ValueClassCreation 的原始 constructor OpId，拒绝未知参数/getter
  值，不增加用户拒绝过的 JSFunction 生命周期假设。普通函数等其他来源及
  完整 Proxy 差异仍待建模。边界模型不是 Ark VM 实际执行结果。
- 已准备且无父 cell/module 依赖、无 initializer/defined-member 投影的类，通过
  计费的 CheckClassParentScope 进入同一 phased forest/独立操作数检查；缺失正文
  拒绝，其他类仍需要 phase capture/projection 桥。原 phase 图不被擦除或替换。
  测试 constructor 正文从真实文件字节码解码、准备；低层 phase fixture 独立
  建立 handler，创建位于保护区域之前，不声称生产 phased class reader 全完成。
- 三动态 profile、两编码、normal/phase，覆盖 1/2/200/201/402 连续构造和
  1/2/200/201 参数；缺 constructor/array 证明、共享/外部数组、错属性/索引、
  V1 资源范围、未准备正文和预算反例均拒绝。4626 组 native 对照通过，覆盖
  返回对象身份、getter 顺序、任意异常值、原型索引/iterator/Reflect 污染。
- -j4 生产及相关目标构建退出 0：/tmp/abcd-apply-final-build.log；14/14 回归
  68.10 秒：/tmp/abcd-apply-final-tests.log。新增索引伪造反例后最终 2/2 通过
  32.68 秒，/tmp/abcd-apply-proof-final-tests.log；纯 include/规则注释收尾构建
  日志 /tmp/abcd-apply-include-final-build.log。规则 revision 60。
- fresh build/stage-one/value-apply-construct-pass-1 五输入均退出 0，仍为
  78 complete /5 incomplete /23 failure /46 no-module；既有完整源码逐字未变，
  无回退/legacy。证据 /tmp/abcd-apply-final-batch.log 和 batch-summary.json。
  此次未增加完整 record。Goal active；继续 object creaters 的完整缺口与阶段一
  record 验收，未恢复 full-suite、扩展语料、RSS/基准或定期审视。

### 2026-10-03：object creaters 优先，迭代结果对象贯通

- 已按用户最新要求把 object creaters 的完整 26 格式置于当前优先级；
  [完整清单与公共接口](object-creators.md) 分别记录既有路径、协议条件及缺口，
  不将 decode、保守拒绝或 generator owner 子集计作全组完成。
- CREATEITERRESULTOBJ 现从既有 LiftIterResult 进入共同 SemanticOperation /
  ValueOperation、OperandView、EvaluationShape、常量解析和 ExpressionForest；
  normal 与 Before/Commit/Catch 图使用同一描述。输出复用 ObjectExpr /
  DataProperty 和原 printer，未添加 group writer、样本规则或可变全局工厂。
- 创建点保留 allocation/may-throw、正常完成才更新 acc 和新对象身份；
  value/done 的键、顺序、descriptor、原输入与来源由独立 TreeFlow /
  TreeSemantics 验证。canonical Boolean flag 直接输出，其余 Language 值
  使用严格等于 true。规则 revision 升为 59，旧收据不会误复用。
- 新测试覆盖三动态 profile、normal/异常图、直接/计算/布尔输入、
  1/2/200/201/402 连续创建及伪造 IR/键/顺序/表达式/来源拒绝；
  2312 组 native 对照验证身份、属性、getter 顺序、异常值和工厂/原型污染。
  新真实字节码先复现共同 semantic lift 拒绝后通过；fixture 临时文件 unlink
  改变 ctime 的错误已修正，最终通过来自成功构建。
- -j4 最终生产及相关目标构建退出 0；10/10 回归 32.11 秒，日志
  /tmp/abcd-object-creator-final-build.log、/tmp/abcd-object-creator-final-tests.log。
  包括原 iterator-result、缓冲字面量、创建导入、常量解析、generator method
  与收据 revision 回归。没有恢复 full-suite、扩展语料或 RSS/基准任务。
- fresh build/stage-one/value-object-creators-pass-1 五输入均退出 0，保持
  78 complete /5 incomplete /23 failure /46 no-module；既有完整源码逐字未变，
  无 complete 回退或 legacy writer。证据 /tmp/abcd-object-creator-final-batch.log、
  /tmp/abcd-object-creator-final-batch-summary.json。本次没有新增完整 record。
- 当前缺口仍含 regexp、excluded-keys rest、apply 构造、deprecated 池布局及
  完整协程协议矩阵。后续沿共同表达式/区域接口补齐；Goal active，阶段一未完成。

### 2026-10-03：phased 共享分支与嵌套调用

- 图发现一次索引相邻纯判断路径及链父关系，仅从最大链根恢复SelectionRegion，
  保留每个scalar branch及短路方向；共享臂入口数必须恰好匹配链长度。
  condition_phases保留原WorkBlockId，再逐点绑定真实ControlId，不识别null字面量
  或样本名字来选择规则。第二判断的scope/空catch-copy继续独立检查。
- 条件参数允许其消费调用已有合法phase inline父节点。新前缀使用普查要求
  被移动快照只被所属前缀、条件或选择臂消费，防止外层较早参数先读未初始化
  快照；phased_prefix_escape_refusal_test专门验证这一边界。
- Native 67392组对照覆盖null/undefined、通过原型getter观察非空假值读取、
  callee改写、各位置异常及嵌套间接调用。三profile及1/2/200/201调用通过；
  单条2/200/201/402判断链也通过，避免只测试许多短链掩盖后缀重复扫描。
- -j4构建最终退出0，9/9组合回归88.64秒，最终3/3通过1.90秒；日志
  /tmp/abcd-phase-shared-related-build2.log、/tmp/abcd-phase-shared-related-tests.log、
  /tmp/abcd-phase-shared-final-tests.log。嵌套fixture显式使用生产路由的
  NormalCallResults(kSourceContract)，输入证明未放宽；长链测试参数重名已修正。
- fresh build/stage-one/value-phase-shared-pass-1五输入均退出0：78 complete /
  5 incomplete /23 failure /46 no-module，无回退/legacy。8383/9550/11989
  的共享空值if/else赋值收敛为共同条件表达式；Voip/ProgressBar配对通过。
- 实际Push record3444方法3613已越过原callee op40/调用op66的拒绝；当前
  下一处为外层callee op29（pool32）、Invoke op69（显式receiver、4参数），
  仍为kReference。证据 /tmp/abcd-phase-shared-next-call.log。下一步需将嵌套
  调用链外层方法引用纳入顺序证明，不能把内层成功当整record完成。
  Goal active；full-suite/扩展语料/RSS未恢复。

### 2026-10-03：条件参数调用的有副作用前缀

- PhaseSelectionCall保存原执行顺序的前缀表达式根及phase_prefixes唯一归属；
  共同forest在条件中生成SequenceExpr和既有私有快照赋值。callee仍先读取，
  prefix中的getter/调用只执行一次，重复使用的返回对象保留同一快照。
  生成器不再另发这些根的语句，独立TreeFlow按序核对每个根及赋值/作用域。
- Prefix证明仍拒绝normal phi复制、非空catch暂存、环境/其他独立语句；
  每个may-throw site须与最终调用具有相同异常签名。另一个保留的方法引用
  是扫描屏障，避免前缀重叠与重复长路径扫描；原局部forest/receiver验证保留。
- 新真实字节码先复现getter前缀拒绝，现支持getter和函数调用前缀，函数结果
  在谓词及参数分支中复用。三profile及1/2/200/201连续调用通过；删除前缀
  列表/归属表的负例拒绝。44928组native对照覆盖前缀改写method、非callable、
  getter/调用/参数各位置抛错、receiver及共享返回对象，无.call替代。
- 相关targets -j4构建退出0；9/9组合回归通过76.73秒，日志
  /tmp/abcd-phase-prefix-related-build.log、/tmp/abcd-phase-prefix-related-tests.log；
  native独立日志 /tmp/abcd-phase-prefix-native.log。Voip和ProgressBar配对通过。
- fresh build/stage-one/value-phase-prefix-pass-1五输入均退出0，仍78 complete /
  5 incomplete /23 failure /46 no-module，无完整源码变化、回退或legacy。
- 方法3613仍需shared-arm空值分支发现/绑定才能使用此次前缀能力；多条件
  参数和更长phi传递也未放开。本轮不是目标缩减或阶段完成。Goal active，
  full-suite/扩展语料/RSS未恢复。

### 2026-10-03：phased 条件参数与保留callee的原生调用贯通

- 新phase_selection_calls.cc在phased局部表达式规划后消费同一图候选，绑定
  callee、条件phi、参数use、调用及可选的一次唯一使用寄存器传递。前后路径
  必须无独立效果/phi复制；callee、计算臂和调用保留相同异常签名且无移动
  catch暂存。CallPlan继续独立证明receiver，不重读getter或使用.call辅助。
- 构建器与独立验证接入logical_values及精确跨区域引用；原局部forest仍按
  原位置验证，独立VerifyPhaseExpressions入口也验证调用单元后才排除这些
  跨区域边。删除声明、错参数use、越界consumer、错传递表均拒绝。
- phi后的寄存器复制归属到条件值根，标量谓词保留原分支PC；没有把分支PC
  合并到两个表达式产生重复执行归属。独立来源校验拒绝丢失复制来源。
- 真实字节码三profile、1/2/200/201连续调用验证原生method(conditional)；
  12096组独立运行对照覆盖字面量/getter参数、getter改写method、非callable、
  返回/抛出值身份、receiver、短路和.call污染。旧复杂可选链拒绝规格仍通过。
- -j4构建最终退出0；selection_module_test通过25.02秒，相关11/11回归
  42.68秒。独立入口与多phi参数预索引补强后最终4/4通过27.97秒，日志
  /tmp/abcd-phase-call-final-build2.log、/tmp/abcd-phase-call-related-tests.log、
  /tmp/abcd-phase-call-final-tests.log。初次编译的CombinedOrigin字段及测试
  缺少budget实参已修正；运行过的通过结果均来自成功构建。
- fresh build/stage-one/value-phase-call-pass-2五输入均退出0，仍78 complete /
  5 incomplete /23 failure /46 no-module；既有完整源码逐字不变，无回退/legacy。
- 边界未缩小：当前每调用只组合一个条件参数，允许一次唯一使用的phi复制，
  前缀独立效果、shared-arm可选链、多条件参数仍不支持。提前统计phi参数数
  以避免多个选择反复扫描同一callee长前缀。下一步继续处理方法3613的
  JSON.parse前缀及共享空值分支，不将本轮子集视作阶段完成。Goal active，
  full-suite/扩展语料/RSS未恢复。

### 2026-10-03：phased 条件发现解除控制树顺序依赖

- 将现有phased区域发现改为直接读取原始phase图与phi边：一次索引normal
  入边、phi输入、表达式根和catch暂存；每臂在首个合流/分支/catch/phi复制处
  停止，保留原exception边。FindPhasedPhiSelections在控制树建立前可用，
  返回既有SelectionRegion及原block/edge身份，不授予源控制树归属。
- 现有源码路径已改用上述候选，再由FindPhasedSelections核对真实控制树的
  header、join、两臂完整路径并绑定ControlId。没有第二个源码writer，也没有
  将ControlId与WorkBlockId混用。错绑taken到fallthrough的用例明确拒绝。
- 三profile的1/2/200/201/402字面量/getter/成员调用区域同时验证独立图发现
  与控制绑定；phased_literal_test通过23.85秒。相关targets -j4构建退出0，
  另11/11回归39.93秒；日志 /tmp/abcd-phase-graph-{regions,related}-build.log、
  /tmp/abcd-phase-graph-unit-tests.log、/tmp/abcd-phase-graph-related-tests.log。
- fresh build/stage-one/value-phase-graph-pass-1五输入均退出0，保持78 complete /
  5 incomplete /23 failure /46 no-module；所有既有完整源码逐字不变，无回退/
  legacy。此次是已用于生成器的区域算法整合，没有新增完整record。
- 下一步在phased求值规划阶段消费同一图候选，闭合条件值到调用参数的唯一
  使用关系、callee与条件前缀的效果顺序、catch签名和receiver；方法3613
  仍未修复。不能以候选发现代替调用融合完成。Goal active，暂停项未恢复。

### 2026-10-03：phased 计算型选择臂与原生成员调用

- PhiExpressionProducer允许唯一使用的计算结果直接在自身Commit的出边phi
  赋值中求值，没有跨过其他操作；原Before异常暂存位置仍保留。跨inert尾部
  的优化仍只限字面量，不授予一般计算跨跳转/作用域的移动许可。
- 同一FindPhasedSelections以一次反向索引确认每个臂只有一个闭合表达式
  根，允许该根的已验证inline子树与模块guard。沿真实normal路径发现臂，
  不删除exception边；非空pre-throw copy、独立效果、嵌套区域仍拒绝。
  独立ExpressionScopes另核对phi计算及inline事件的原始try/handler作用域。
- 真实字节码三profile覆盖1/2/200/201/402连续getter与成员调用选择臂；
  共同AST输出 condition ? object.value() : null，未使用.call替代receiver。
  43组getter、59组成员调用对照通过，覆盖短路、返回值/异常值身份、getter
  与调用分别抛错、receiver与被修改的.call属性。交换选择结果仍拒绝。
- 相关targets -j4构建退出0，11/11组合回归61.27秒；增加成员调用用例后
  最终5/5通过23.71秒。日志 /tmp/abcd-phase-computed-related-{build,tests}.log、
  /tmp/abcd-phase-computed-call-build.log、/tmp/abcd-phase-computed-final-tests.log。
  控制组合、catch snapshots、Voip300组及ProgressBar配对均通过。
- fresh build/stage-one/value-phase-computed-pass-1五输入均退出0，仍78 complete /
  5 incomplete /23 failure /46 no-module，无回退/legacy。8383/9550/11989
  减少计算结果的中间快照；其他完整源码未变化。
- 仍未闭合：分支前保留的方法引用与整个条件参数融合、可选链shared-arm
  条件和嵌套选择。Push方法3613的kReference仍保留，不把本轮分支内部调用
  支持冒充完整参数组合。Goal active；full-suite/扩展语料/RSS仍未恢复。

### 2026-10-03：phased 条件区域进入共同 SelectionRegion

- 新 phased_selections.cc 从真实Commit分支、单入口控制路径和两条原始phi
  边发现闭合条件区域，返回既有SelectionRegion；共同表达式forest、函数树
  和独立TreeFlow均使用它。没有复制normal替身图、删除异常边或另建writer。
- 当前两臂限已验证的纯字面量phi输入及无效果控制/别名；共享phi、额外边
  赋值、嵌套区域、独立效果、catch入口继续拒绝。保留原ValueBranch谓词，
  不把VM的false/zero判断猜成JS truthiness。TreeEdgeFlow::Select只扩展到
  与原edge匹配的NormalCopies；ThrowCopies仍不允许直接成为选择表达式。
- 新真实字节码规格先复现原语句输出，现生成原生ConditionalExpr；三profile
  覆盖1/2/200/201/402连续区域，交换两个结果会被独立语义校验拒绝。native
  43组字面量选择对照通过，覆盖false/0/-0、null/undefined、Symbol和对象。
- 相关targets -j4构建退出0；9/9回归56.12秒。补强连续区域及拒绝复用同一
  phi边后，最终3/3通过20.65秒；日志
  /tmp/abcd-phase-selection-related-{build,tests}.log 与
  /tmp/abcd-phase-selection-final-{build,tests}.log。Voip配对300组仍通过。
- 五输入fresh build/stage-one/value-phase-selection-pass-2确认78 complete /
  5 incomplete /23 failure /46 no-module，无回退/legacy。与phase-phi基线
  比较，8383和11989仅把纯字面量if/else赋值改为条件表达式；二次导出一致。
- 未完成的目标明确保留：计算型phased分支、可选链参数与保留receiver的
  调用组合尚未闭合，phased_selection_reference_refusal_test仍为拒绝。
  下一步需把效果/异常签名和分支子表达式归属纳入同一SelectionRegion，
  不以本轮纯字面量覆盖替代完整范围。Goal active，未运行full-suite/RSS。

### 2026-10-03：phased phi 边的纯常量表达式

- 共同 PhiExpressionProducer/求值构建器/独立校验器开始支持 phased 普通
  phi边的唯一使用原始值字面量。只跨Commit之后的单出口无效果jump/nop
  尾部；拒绝catch入口/异常边、共享值、计算值及任何独立效果。源码直接
  给phi赋常量，移除多余寄存器快照；没有删异常表或复制normal替身CFG。
- phased staged字节码回归先在旧实现失败，现三profile及1/2/200/201/402
  字段用例通过；伪造phi consumer继续拒绝。相关targets -j4构建退出0；
  9/9定向回归54.54秒，额外ProgressBar配对1/1通过1.38秒。日志
  /tmp/abcd-phase-phi-related-{build,tests}.log 与
  /tmp/abcd-phase-phi-progressbar-tests.log。
- fresh build/stage-one/value-phase-phi-pass-1五输入均退出0，仍78 complete /
  5 incomplete /23 failure /46 no-module，无回退/legacy。8383/9550/11989
  的源码移除了常量快照及随之重编号，其他完整项未变化；对应Voip300场景
  配对仍通过。这不是完整条件值/可选链组合验收。
- 新阻塞定位：PushMessageAbility record3444子方法3613，ValueNamedRead
  op40/PC109的方法引用跨过可选链参数，到ValueInvoke op66/PC170；
  BuildPhasedCallPlan返回kReference。完整区域尚未成为原生参数表达式，
  因此不得重读getter或借.call绕过。记录于
  /tmp/abcd-push-message-reference.log 与 /tmp/abcd-push-message-method.log。
- tests/integration/selection_module.cc新增真实字节码phased条件参数回归，
  phased_selection_reference_refusal_test证明上述接收者边界仍在；后续须
  将其改为完整成功/行为对照，不能把拒绝本身当成目标。下一步扩展统一
  SelectionRegion对phased分支路径/phi边的归属，组合条件值与调用引用。
  Goal active；full-suite/扩展语料/RSS未恢复。

### 2026-10-03：phased 单分支正文直接输出

- 将共同分支方向选择及独立 TreeFlow 对应规则用于 phased Commit 节点。
  当 taken 分支无正文且无phi赋值时，反向谓词并直接输出 fallthrough 正文；
  不再生成空if加else。原条件的求值次数、边归属、catch边、源PC不变。
  仍消费被省略边的空copy bundle，不允许隐藏必要phi赋值。
- 新真实字节码单分支AST规格先复现旧输出；三profile、1/2/200/201重复
  条件验证无alternate且正文非空。3624组独立短路/效果/TDZ场景通过。
- 相关targets -j4构建退出0；10/10回归54.44秒，额外Home与ProgressBar
  配对2/2通过2.12秒。Voip配对300组保持通过。日志
  /tmp/abcd-phased-arm-related-{build,tests}.log 与
  /tmp/abcd-phased-arm-pasteboard-tests.log。
- fresh build/stage-one/value-phased-arm-pass-1 五输入均退出0；仍78 complete /
  5 incomplete /23 failure /46 no-module，无回退或legacy。四个完整record
  5521/8383/9550/11989的diff仅为上述空分支反向；其他完整源码未变化。
  这是一项表示质量改进，不代表临时变量/可选链/switch等完整风格验收通过。
- 检查CommonView失败时确认实际阻塞来自子回调19852、父19712创建PC72：
  SemanticProofFailure::kInternalValue PC41，紧邻PC38通用算术结果。
  并非模块入口19740的分支PC41；证据 /tmp/abcd-common-view-nested.log。
  保留算术语义边界，不凭样本猜测数值类型。Goal active；未跑full-suite/RSS。

### 2026-10-03：异常短路条件中的模块 guard 归属贯通

- 方法12319拒绝已定位到 EmitDecisionBlock：kModuleGuard 被错误地要求作为
  独立求值根。生成器及独立 TreeFlow 校验现复用已有精确 module read/guard
  共享 ExprId，只登记原PC归属，不重复读取 live binding；两个原始 throwing
  site 的 scope 与 catch-copy 检查均保留，不放宽模块绑定或TDZ前提。
- 新真实字节码三profile用例覆盖 AND/OR、直接模块值/属性读取、1/2/200/201
  重复条件和真try/catch。伪造guard缺失、错误AST归属继续拒绝；源实现前
  已复现 kOwnership，校验器对应缺口同步修正。
- 新增 phased 模式独立对照7248组短路/副作用/TDZ场景，并检查异常必须由
  原catch返回，不能用外抛冒充。普通模式同一对照继续保留。
- VoipCallService record11989首次 source_complete=true，经共同模块/类/
  async/闭包路径输出。独立读取配对TS、strip types后，用宿主stub对照300组
  场景：主消息处理、各事件、回调、关闭、同步抛错、Promise拒绝、call参数
  own字段属性与pixel对象身份。非HarmonyOS运行证据，非完整应用验收。
- fresh build/stage-one/value-phased-decision-pass-1 五输入均退出0：
  78 complete /5 incomplete /23 failure /46 no-module；唯一新增11989，
  无完整项回退、既有完整源码变化或legacy。日志
  /tmp/abcd-phased-decision-final-batch.log 与
  /tmp/abcd-phased-decision-final-batch-summary.json。
- 相关targets -j4构建退出0，10/10回归通过52.71秒；日志
  /tmp/abcd-phased-decision-related-build.log 与
  /tmp/abcd-phased-decision-related-tests.log。
- 表示质量仍未通过最终验收：该record保留原生class、static async、await与
  try/catch，但字段初始化/可选链/条件值仍有多个phi及快照临时变量，switch
  显示为条件链。后续沿共同phased表达式恢复改进，不用配对源码修饰输出。
  full-suite/扩展语料/RSS继续暂缓，Goal active。

### 2026-10-03：异常图跨分支的分步对象构造

- LiteralPlan 复用既有 kStatements/共同 AST，在 phased CFG 中允许缓冲对象
  的字段定义跨分支；分配、RHS、字段写入仍保留各自原始控制流与异常位置。
  不复制源码、不引入样本识别或 IIFE。现有 writable own data property 名称
  验证继续决定能否输出普通赋值；不允许借此触发继承 setter。
- 支配分析包括 Before/Commit/Catch 全部边；统一使用规范化 operand 做一次
  use census，寄存器 copy 不能隐藏提前逃逸。外部观察必须位于最终写入的
  正常 Commit 支配范围；提前观察、catch 快照和循环继续拒绝。
- 真实字节码用例先在原实现复现失败；新增三 profile 分支/异常/提前观察/
  catch 观察及 1/2/200/201/402 字段用例。native 对照43组验证 getter次数、
  receiver、抛出值身份、对象身份、字段属性与 __proto__。这是定向行为验证，
  不替代 Ark 平台或完整应用验收。
- 相关 targets -j4 构建退出0；12/12测试通过38.36秒，日志
  /tmp/abcd-phased-staged-related-{build,tests}.log。固定五输入 fresh
  build/stage-one/value-phased-staged-pass-1 全部退出0，仍为77 complete /
  5 incomplete /24 failure /46 no-module；无完整项回退、源码变化或legacy。
- Voip 方法12264已越过原LiteralError::kReceiver；下一处为方法12319的
  FunctionTreeError::kOwnership，证据 /tmp/abcd-phased-staged-next-method.log
  和 /tmp/abcd-phased-staged-next-family.log。record11989仍失败，阶段一未完成。
  未运行 full-suite/扩展语料/RSS；Goal保持active。

### 2026-10-03：self 顺序证据接入投影与捕获

- FamilyReader 批量读取候选对应的真实初始化器并建立 ClassSelfOrder；父
  FileFunction 持有证据，EvaluationPlan 保留不可变副本，独立校验再次使用。
  ProjectClassMembers 仅将有证据的 self store 计入允许区间，仍保留它的
  普通 cell 写入，不删除写入或取消 prototype spelling/效果区间检查。
- 组合证据另映射到原始 input local-cell/allocation/store，绑定原始指令与
  cell arena。class-root 捕获通过同一 ScopeFacts 规则使用这项私有初始化
  前缀；仍限 Language 和原写入合同，Defined/其他路径未放宽。复制父图、
  revision变化、缺少证据和错配调用继续拒绝。
- 实际 Voip越过 self-store 和 class-root capture 拒绝。下一处是方法12264
  的 phased BuildLiteralPlan 返回 LiteralError::kReceiver，见
  /tmp/abcd-self-order-phase-next.log；未宣称record11989完成。
- 16 targets 构建退出0，相关21/21通过17.70秒，日志
  /tmp/abcd-self-order-related-{build,tests}.log。首次工具构建暴露新错误
  variant 未穷尽以及嵌套 cause/reason 处理缺项，修正现有诊断访问器后通过；
  原错误variant次序保留。实际入口测试验证有证据投影通过且self写入仍未投影。
- 五输入 fresh build/stage-one/value-self-order-pass-1 均退出0，仍为
  77 complete /5 incomplete /24 failure /46 no-module，无完整项回退、
  完整源码变化或legacy。未运行 full-suite/扩展语料/RSS，Goal active。

### 2026-10-03：同 occurrence 的 self/初始化器组合证据

- 新 ClassSelfOrder 对整个父图一次索引 self 候选，再逐初始化器验证同文件/
  record/方法、实际 creation PC、定义的 wire 属性、调用 callee 的真实 SSA
  producer及零参 Method 上下文，结合 LiteralClassFields；不把另一方法的
  字面量体作为当前调用的许可。重复凭据拒绝，无逐类全图重扫。
- 结果绑定父图 move-stable op/value arena、规模和 IR/CFG revision。图副本
  即使 revision 相同也不取得 stores，父图 revision 变化后凭据不再适用。
  原型 spelling、完整效果区间与后代捕获发布仍是独立义务；尚未传入
  ProjectClassMembers，不据此宣称已恢复实际 record。
- 实际 Voip入口与初始化器组合通过。错配 callee/method、重复 witness、
  Store 替换 DefineField、图复制/修改及精确 work 边界负例通过。相关3/3
  通过5.22秒，日志 /tmp/abcd-class-self-order-boundary-{build,tests}.log。
- 未重跑五输入/full-suite/RSS，基线77/5/24/46不变，Goal active。

### 2026-10-03：self cell 首次写入前的私有区间

- FindClassSelfStores 增加共享效果前缀计数：本地环境分配到唯一 self store
  之间只能有本次 class creation 的效果。要求 absent heritage，并拒绝同块
  在 initializer call 前读取该 cell；不根据类名、属性名或固定槽位推断。
  该检查仍是后续顺序证明的前提，不直接放行 ClassProjection。
- 三 profile、非零槽位、200/201规模和既有预算边界通过；新增分配后
  DEBUGGER 暴露反例被拒绝。实际 Voip 入口仍能得到同一候选关联。
  相关2/2通过4.76秒，日志 /tmp/abcd-class-self-private-{build,tests}.log。
- 字面量初始化器证据与该私有区间尚未组合成同文件、同创建 occurrence 的
  重排许可；生产输出准入未改变。未重跑五输入/full-suite/RSS，基线仍
  77/5/24/46，Goal active。

### 2026-10-03：字面量静态字段初始化的效果证据

- 新 LiteralClassFields 验证单块普通 IR：纯 literal/copy/string、对入口
  receiver 的 DefineField 字面量写入，以及 undefined return。共享 ValueFlow
  校验先执行；拒绝其他操作、普通属性写入、Store 模式、自身/参数等非字面量
  RHS、错误 receiver、私有 tagged NaN 编码和非 undefined 返回。
- 该证据只描述初始化器；调用方仍必须证明 receiver 是本次创建的普通类对象，
  且有关 cell/类对象尚无外部观察。不能单凭此结果重排 self store，未改变
  ClassProjection 的生产准入，也未新增类名遮蔽行为。
- 实际 method12292 通过，识别一个字段；1/2/200/201/402字段、线性增长、
  work精确边界和零内存测试通过。相关2/2通过1.54秒，日志
  /tmp/abcd-literal-class-fields-boundary-build-2.log / boundary-tests-2.log。
  首版测试漏写 ValueOwnWrite.kind 导致编译失败；修正并明确仅接受
  DefineField 后通过，未把 Store 与 DefineField 混为一类。
- 当前尚未接入 self 写入与初始化调用的顺序证明，未重跑五输入/full-suite/RSS；
  基线77/5/24/46不变，Goal active。

### 2026-10-03：确认实际静态初始化器的观察边界

- 扩展实际入口回归，经同文件 MethodLookup、MethodRoleIndex、FunctionCreationSites
  和 ReadClassInitializerFunction 读取真正的初始化器，未用 sidecar 推断行为。
  method12292 的 closed 输入没有 captures，活跃操作只有一次 this 自有字段
  写入 undefined 和 return（允许纯 copy）；真实入口测试1/1通过1.06秒。
  日志 /tmp/abcd-class-self-initializer-{build,test}.log。
- 其他成员仍有 self cell 读取：方法12125/12153/12264/12319 的 LDLEXVAR
  不能删除或改为任意调用 receiver。因此“初始化器读取 self”不是当前事实。
  下一步优先验证无观察静态初始化与唯一 cell 初始化之间的顺序关系，探索
  保留既有 cell/捕获模型的恢复；不预先强制新增类名绑定，也不直接放宽投影。
- 本轮仅增加真实输入回归及上述实现边界，未修改生产恢复规则、未重跑五输入
  或 full-suite/RSS。基线77/5/24/46，Goal保持active。

### 2026-10-03：类结果到本地 cell 的初始化关联分析

- 新 FindClassSelfStores 复用 ValueFlow、CellPlan 和 ClassInitializerCalls 的
  独立检查，以操作 ID/cell 身份关联 class creation、本地唯一 store 和随后
  的 initializer call。要求新分配的本地环境、同块原顺序；不读取类名或
  假定 slot0，不接受调用方篡改的 initializer occurrence。
- 这是候选存储关系，不是原生类内部名称绑定的证明。现有 ClassTree/捕获
  模型没有独立的 class-self 源绑定；仍需验证后代捕获归属、名字遮蔽和静态
  初始化期间的绑定身份。ProjectClassMembers 继续拒绝提前移动该写入。
- 三 profile、非零槽位2、1/2/200/201规模、重复写入拒绝、陈旧 cell计划、
  错误调用和精确work边界通过。实际 Voip入口识别 creation PC13 -> store
  PC28 -> initializer PC51。相关3/3通过3.85秒，日志
  /tmp/abcd-class-self-final-tests.log；构建日志同前缀。
- 尚未接入原生类自绑定输出，未重跑五输入/full-suite/RSS；基线仍为
  77 complete /5 incomplete /24 failure /46 no-module，Goal active。

### 2026-10-03：清理型模块入口接入读取与发布

- FileFunction 增加 CleanupBody 所有权；FamilyReader 对普通同步模块的零参
  func_main_0 尝试明确的 CleanupEntry 模式，其他 callable 仍走原 phased
  路径。失败保留类型化 CleanupFailure，所有原始/正常副本纳入内存预算。
  原异常信息留存，未制造 callable creation owner。
- Record 产物保存 EnvironmentCleanupSite，entry-positions JSON 输出独立
  environment_cleanup 数组（pc、pop/rethrow、allocation_pc、range=null）；
  不混为 unreachable 或普通语法范围。函数族源码映射也传递其 cleanup owner。
- 清理型合成模块三 profile 完整输出。独立模块执行对照验证正常导出，以及
  对象、Symbol、字符串、0、undefined、null 抛出值的身份和模块中止状态。
  覆写异常值的 handler 仍拒绝，合成产物核对两个原始清理 PC。
- 另补类初始化之后跨基本块的直接使用：共同 VerifyValueFlow 先证明 SSA
  可用性；创建和初始化调用仍必须同块，所有正常跨块使用因支配关系发生于
  调用之后。phi escape、初始化前发布及效果干扰仍拒绝。新用例由 RED 转绿，
  三 profile 和200/201规模沿用共同测试。
- 实际 Voip entry12208 越过 kOccurrence 后，定位到 class creation PC13 与
  initializer call PC51 之间的 lexical store PC28（class self binding）。
  该存储不是普通 prototype read，仍被 ProjectClassMembers 拒绝；需另外
  证明并连接原生类内部名称绑定，不能直接放行。证据
  /tmp/abcd-cleanup-class-store.log，尚未完成实际 record11989。
- 14 targets 构建退出0，相关19/19通过15.23秒，日志
  /tmp/abcd-cleanup-entry-related-{build,tests}.log。五输入 fresh
  build/stage-one/value-cleanup-entry-pass-1 均退出0，77 complete /5 incomplete
  /24 failure /46 no-module，无完整项回退、完整源码变化或legacy。
  /tmp/abcd-cleanup-entry-final-*；未运行 full-suite/扩展语料/RSS，Goal active。

### 2026-10-03：环境清理 PC 的独立来源类型

- InstructionSources 新增 CleanupInstruction，与纯 elision 和正常输入的
  unreachable 分开。仅在提供同一输入的 CleanupBody 时标记；检查输入规模、
  每个 PC、正常路径不可达性及 pop/rethrow 的实际语义操作。错误投影拒绝。
- family emission 接受原始来源但不给这些指令伪造语法范围；位置 JSON 使用
  kind=environment_cleanup、cleanup=pop/rethrow、range=null，保留原 site
  和 origin。普通未投影路径仍使用原 unreachable 分类。
- 三 profile 的合成树经实际 printer 和位置 JSON 验证，每个处理器两个
  清理站点均明确标记。扩展既有 instruction_positions 测试访问器以穷尽
  新 variant；初次相关构建因该访问器漏分支失败，补齐后通过。
  六 targets 构建退出0，相关9/9 CTest通过5.64秒，日志
  /tmp/abcd-cleanup-source-related-build-2.log / related-tests.log。
- FileFunction/FamilyReader 尚未接入 CleanupBody，因此没有新增完整项，
  本轮未重跑五输入/full-suite/RSS。下一步将原始/投影所有权接入模块入口
  读取，并把这些来源传到入口产物；基线77/5/24/46，Goal active。

### 2026-10-03：环境清理的原始/正常路径双份所有权与不可达槽

- 新 CleanupBody 工厂保存原始 instructions/tries/handlers 和正常路径视图，
  内部建立 EnvironmentCleanup 证明后才返回。正常视图只省略异常表，所有
  decoded 指令、分支目标和 PC 保持；单独保存 pop/rethrow 来源标记，不能
  在后续发布时把原来可执行的异常清理误称为全局不可达。
- 实际 Voip 入口的探针暴露通用导入缺口：正常 FunctionInput 已通过，
  BuildFunctionValues 却在环境分析拒绝 dead POP 的原效果。ImportNormalValues
  现在仅对原可达性证明排除的站点生成 removed/NOP/无执行效果槽；原操作留在
  不可变 SemanticMethod，PC/origin 保留。独立 VerifyImportedValues 分别检查
  live 原操作及 dead 占位，伪造 live 删除、dead 复活或附加效果均拒绝。
- 三 profile 的不可达 pop/throw、正常环境重建、投影 PC/opcode/branch
  保留、双层清理、200/201 边界及精确 work 限额通过。实际 entry12208 现在
  可由 CleanupBody.Normal 建立正常 FunctionInput 和 FunctionValues，测试
  明确不发布源码。尚未接入 FileFunction/FamilyReader 及源位置序列化。
- 11 targets 构建退出0，相关10/10通过7.19秒；日志
  /tmp/abcd-cleanup-body-related-{build,tests}.log。最初 probe 的环境 kFlow
  拒绝见 /tmp/abcd-cleanup-environment-gap.log，修复后真实入口测试通过。
- 五输入 fresh build/stage-one/value-cleanup-body-pass-1 均退出0，仍77
  complete /5 incomplete /24 failure /46 no-module，无完整项回退或legacy。
  唯一既有完整源码变化为 BreakpointType19234 的末尾 return 移入等价 else；
  现有配对 value_breakpoint_record_source_test 通过1.43秒。
  日志 /tmp/abcd-cleanup-body-final-* / breakpoint-tests.log。未运行
  full-suite/扩展语料/RSS；下一步接入清理来源与入口发布，Goal active。

### 2026-10-03：实现原始异常流上的环境清理证明

- 新 EnvironmentCleanup 从原 MethodBody 构建 ExceptionFlow 和异常环境分析，
  不接受调用方伪造的环境证书。要求所有 region 为单一 catch-all，handler
  仅由本方法拥有的 POPLEXENV 和最后 THROW acc 组成，原样抛到 caller；
  拒绝从正常控制流进入 handler。保留 handler/rethrow instruction IDs 及
  每个 pop 对应的原 allocation instruction ID，不用跨图 EnvironmentId。
- 工厂输出封装证明，尚未授权删除 exception table；正常路径对应性和原始
  清理 PC 的源码归属仍需 residual owner 完成。当前严格范围不包含向外层
  catch 转发、typed catches 或未使用的异常处理器，不静默忽略它们。
- 三 profile 的1/2/200/201/402清理区域、双层本地环境、work精确边界及
  零预算测试通过。负例包括异常值改写、return替代重抛、正常跳入、外部
  环境 pop、debugger和typed catch。区间二分索引、归属表和单次边扫描，
  无逐handler全图遍历；分析器沿用上游 byte-bounded 环境分析及其访问计数。
- 实际 VoipCallService entry12208 通过：pop PC58 -> allocation PC6，
  rethrow PC59。独立测试直接读取 ABC，没有 sidecar 输入。相关4/4通过
  1.55秒，日志 /tmp/abcd-environment-cleanup-related-tests.log；新目标首次
  构建前需重新运行 CMake 配置，配置与构建日志均保留在同前缀目录。
- 尚未接入 FamilyReader/record 发布，没有重跑五输入/full-suite/RSS。
  当前基线仍77 complete /5 incomplete /24 failure /46 no-module。
  下一步构造拥有原始异常信息的正常路径投影，验证来源与捕获映射后接入
  模块入口；Goal保持active。

### 2026-10-03：剩余失败定位到值域与环境清理入口

- 本轮未修改引擎，新增了下一步实现所需的实际失败证据。WindowUtil record7618
  / entry8202 的类方法8620（updateAvoidPadding）在 VerifyExceptionInputs
  返回 kInternalValue：PC127、node106、value210。PC124 为两个属性读取
  结果的 ADD，属于仍未证明的一般算术值域；未假设宿主属性为 Number。
  日志 /tmp/abcd-windowutil-values-2.log / 8620.jsonl。
- VoipCallService record11989 / entry12208 在 ReadFunctionFamily 返回
  kOccurrence，因为根方法有 try_count 而没有 callable creation owner。
  原始入口的异常处理器是 PC58 POPLEXENV、PC59 原样 THROW acc；正常
  路径 PC61 POPLEXENV，随后 PC62 模块发布。异常处理器未出现用户调用，
  是下一步环境清理投影的候选，不能据此直接清空 exception table。
  日志 /tmp/abcd-voip-read.log / entry.jsonl。
- 实现边界：用原 ExceptionFlow 和异常环境分析证明 catch-all、pending
  exception 未改写、只弹出当前方法拥有的环境、无正常控制流进入 handler，
  并保持异常向外路由及原始 PC 归属；证据不闭合时继续走原拒绝路径。
  当前普通 SemanticInput 明确拒绝 exception regions；不得制造普通函数
  occurrence 或把真实用户 catch 当作清理删除。
- 未运行新的构建、测试、五输入导出或 RSS；基线仍为上一节77 complete /5
  incomplete /24 failure /46 no-module。Goal active；下一步优先实现并测试
  透明环境清理证明及其来源保留，再接入入口恢复。

### 2026-10-03：参数 cell 的池字符串默认值，HotListView 贯通

- 方法48581 的缺省分支包含 ValueString 再写入参数 cell。CellDefaults 与
  独立 CellDefaultProof 现在保留 pool index，验证同 arm 的纯字符串 producer、
  精确 store、String 值域及独占 use；错误 pool/缺失字符串证据仍拒绝。
- DefaultBody 保存带来源的 SignatureString 请求。共同 TreeConstants 解析器
  合并普通与签名请求，复用 slot/entity 排序去重及 UTF-16 arena；签名请求
  必须指向 removed/retained-origin 操作，重复或活跃站点拒绝。请求及临时
  seen 表纳入预算，遍历前检查 work。签名构建/独立验证使用同一已解析 TextId，
  不把 pool index 输出为源码，也不读取 sidecar。普通和 standalone 语法解析
  均传递请求；目前新增发现路径为 cell-backed 字符串默认值。
- 另修正 SSA literal defaults 对严格普通函数/箭头的旧限制；同一入口逃逸、
  参数环境、ABI、无 self 及来源证明保留。实际 HotListView member13 因该
  旧限制拒绝，修复后 record48379 完整输出。
- 字符串 cell 用例从 RED 转绿，三 profile 的真实编码 fixture 覆盖引号、
  反斜杠和换行；原生测试核对 length 和非 undefined 参数身份。错误 pool、
  TextId、重复请求、活跃站点和 elided-pure origin 有负例。首轮编译的
  standalone constant_view/control 引用已修正；fixture 漏报 pool 槽数导致
  kIndexRange，修正其元数据后通过。
- 17 targets 构建退出0，相关23/23通过12.66秒；工作预算预扣调整后定向
  7/7通过6.07秒。日志 /tmp/abcd-string-default-related-* / final-tests.log。
  新独立 HotListView 配对测试462组，执行 header 注册及其 Text 回调，覆盖
  默认标题、parent/receiver 分离、捕获默认值与异常；不执行 ForEach 子视图
  或真实渲染。HotListItemView 既有配对回归同时通过。
- 五输入 fresh build/stage-one/value-string-default-pass-1 均退出0，
  77 complete / 5 incomplete / 24 failure / 46 no-module。仅新增48379，
  无完整项回退、既有完整源码变化或 legacy，三个原生 default records 保留。
  证据 /tmp/abcd-string-default-final-*；未运行 full-suite/扩展语料/RSS，
  其他失败和全平台验收尚未完成，Goal 保持 active。

### 2026-10-03：条件分支复用既有快照，HotListItemView 完整源码贯通

- 方法48351 的失败实际为 CallPlanError::kReference：getter/op112 在 block5，
  call/op138 在 block9。条件的空分支直接转发既有 SSA 值，旧 selection
  识别只接受分支内的 producer，导致整个参数无法组成表达式。
- 新增类型化 SelectionSnapshot，空且无效果的 arm 可引用已可用的入口值或
  私有快照。共同 forest 读取其绑定，不重放 getter；TreeFlow 保留实际 phi
  edge occurrence，独立 operand checker 验证精确来源。原 receiver、效果
  区间和消费归属检查未放宽。额外效果 arm、错误快照 symbol 均有拒绝测试。
- 初版与既有 `x || rhs` 争用条件归属，decision-prefix 回归 RED；现将复用
  ToBoolean 原操作数的短路形态交给原 logical owner，避免提前占用其子树。
  三 profile 的 1/2/200/201/402 组合及增长边界通过；相关 28/28 CTest
  通过61.38秒，另 decision_prefix_test 在 owner-tests.log 通过40.79秒。
  17 targets 构建退出0，日志 /tmp/abcd-retained-selection-related-*。
- ABC-only 导出后，独立读取配对 TS 验证 HotListItemView 的 Text.width 与
  Row.margin 两个回调：616组场景/1232次执行，覆盖 captured/default 参数、
  nullish 分支、身份、callee 替换及成员/参数 getter 异常。记录全局读取，
  不在 VM 全局名称 getter 上注入异常，避免 harness 将任意抛出值改为
  ReferenceError。不是完整 ArkUI 渲染或平台验收。
- 五输入 fresh build/stage-one/value-retained-selection-pass-1 均退出0，
  76 complete / 5 incomplete / 25 failure / 46 no-module，无完整项回退或
  legacy。新增完整 record47697；唯一既有完整源码变化为 DetailPage25350
  的 isShow()，phi 临时量改为条件表达式。其新增1080组配对谓词场景及原有
  480组 GridCol 场景通过，detail-tests.log 1/1通过3.73秒。
  三个原生 default records 保留，日志前缀 /tmp/abcd-retained-selection-final-*。
  未运行 full-suite/扩展语料/RSS；其他 record 和整个平台语义验收仍未完成，
  Goal 保持 active。

### 2026-10-03：未使用形参的捕获默认值

- 方法48295 是普通流；默认形参在函数体中未使用，phi 已消除，但缺省分支
  的 lexical load/guard 仍活跃。Finder 现在让有/无 merge 共用同一捕获
  识别规则；独立证明仍检查完整控制边、入口 use、读取值域、guard 和逃逸。
  未把未使用参数等同于可删除默认值初始化，也未合成 `= undefined`。
- 新 record_unused_captured_default_test 在修改前 RED（route refusal5），
  修改后三个 profile 均贯通原生签名。独立模块对照验证 length0 和各类
  参数下返回99；单元检查无 phi 时仍迁移四个原始操作、保留捕获 guard，
  以及遗漏 guard 的拒绝。原有使用默认值的路径继续通过。
- 七 targets 构建退出 0，相关 11/11 CTest 通过 4.77 秒，日志
  /tmp/abcd-unused-capture-related-{build,tests}.log。实际 HotListItemView
  越过 member3/method48295，下一处为 member4/method48351 的
  ExpressionError::kUnsupportedOperation（包装为 FunctionTreeError::kExpression），
  证据 next-method.log / next-expression.log；仍未完成该 record。
- 五输入 fresh build/stage-one/value-unused-captured-default-pass-1 均退出0，
  仍为 75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项
  回退、源码变化或 legacy。日志 /tmp/abcd-unused-capture-final-*。
  未运行 full-suite/扩展语料/RSS，Goal 保持 active。

### 2026-10-03：捕获默认参数贯通函数族和原生签名

- FamilyFunction 持有 DefaultBody，生命周期覆盖全族重绑定、构树和最终
  writes 检查。PlanFamilyCells 从封装证明引入入口存储，原始读取的独立
  capture 校验检查 retained PC、移除后的操作、原词法地址及同一 incoming
  storage；普通读取继续要求活跃操作，未取消原 capture domain/creation 检查。
- 参数签名通过共同 TreeFlow 独立取得 capture symbol；结构、ABI、printer
  接受外层捕获默认表达式，仍拒绝参数/局部替代、名称遮蔽和无 capture context
  的证明。普通/方法/构造签名共享原生表达式；phased defaults 仍未迁移。
- 合成 ABC 的捕获在模块中先设为 1，发布 reader 后改成 42；三个 profile
  均生成 `function($arg0 = $local0) { return $arg0; }`。独立模块执行对照
  验证缺省/显式 undefined 取当前值、length=0、所有传入值保持身份，Proxy
  参数不触发 coercion。新增 record_captured_default_test 和签名篡改负例。
- 19 targets 构建退出 0；相关 24/24 CTest 通过 18.06 秒。
  /tmp/abcd-captured-signature-related-{build,tests}.log。初次构建的强 ID 比较
  与 ControlPlan 前置声明已修正；合成 fixture 的 CodeInfo 参数数与字节头
  不一致已修正。原生模块及已有 literal defaults 回归通过。
- 实际 HotListItemView 的 member2/method48015 越过签名、ABI 和来源检查；
  下一处是 member3/method48295 的 FunctionAbiError::kLength，length2 /
  parameters3，见 next-default.log。尚不能宣布 record 完成。
- 五输入 fresh build/stage-one/value-captured-signature-pass-1 均退出 0，
  75 complete / 5 incomplete / 26 failure / 46 no-module；无完整项回退、
  完整源码变化或 legacy，三个原生 default records 保留。日志前缀
  /tmp/abcd-captured-signature-final-*。未运行 full-suite/扩展语料/RSS，
  源码合同未变，阶段一 Goal 保持 active。

### 2026-10-03：签名外层存储进入共同 cell/evaluation 计划

- CellPlan 新增 entry_demands，显式保留残余函数体之外使用的 IncomingCell；
  不伪造 load、access 或 creation。共同排序/索引合并函数体访问、子函数
  demand 和入口 demand；校验器验证存储覆盖，遗漏或改错地址会被拒绝。
  该表只表达待绑定义务，不提供初始化证明；当前 phased 计划拒绝此需求。
- 普通 BuildCellEvaluation/BuildEvaluationPlan 与独立 evaluation verifier
  已传播该表，零 body access 时仍构建 cell 计划。Conditional 模式保留义务，
  Reject 模式仍拒绝外层存储。全部持久 vector 纳入预算，无逐需求全图扫描。
- 上一提交的真实默认残余图形态已通过单元验证：移除 load/guard 后保留
  原 incoming cell，operations 不伪造读取，求值计划保留 entry demand。
  0/1/2/200/201 项验证重复入口需求、与子函数需求共用 storage 的去重。
  八 targets 构建完成，相关 8/8 CTest 通过 6.49 秒；日志
  /tmp/abcd-signature-cell-related-{build,tests}.log，末次单目标构建
  /tmp/abcd-signature-cell-closed-build.log。首次编译暴露旧 ternary 函数指针
  的签名差异，已改为显式普通/异常调用并完成验证。
- 下一步仍是端到端接线：FamilyFunction 持有 DefaultBody、PlanFamilyCells
  从封装证明引入 entry demand、捕获校验将已迁移的原读取映射到签名 cell，
  最后构建并验证原生捕获默认表达式和参数作用域。尚未接入 family 发布，
  record 完成数未验证变化；未运行五输入/full-suite/RSS，Goal 保持 active。

### 2026-10-03：捕获默认值的独立证明与残余投影

- DefaultValueProof::BuildCaptured 验证原环境/cell plan，要求默认读取属于
  IncomingCell，并复用原默认参数 CFG、SSA 合流及 use 逃逸检查。校验读取
  的真实 LexicalRead|EnvironmentRead 效果、Language 值域、可选同值 Hole
  guard；暂不允许与局部环境分配/初始化前缀交换次序。literal 入口不自动准入。
- DefaultBody::BuildCaptured 实际生成残余图，将比较、分支、读取和 guard
  的来源保留为签名操作；封装原 incoming storage 和候选身份。验证图中的
  剩余 use/dominance，未复制原对应性证书。它尚未接入 family 发布：下一步
  必须让签名绑定、capture rebinding 和独立 tree verifier 消费该存储义务。
- 新测试覆盖错误地址/控制边、遗漏或错值 guard、raw parameter 逃逸、陈旧
  cell revision、零 work/bytes，以及残余读取/guard 移除和来源存储保留。
  初次测试的逃逸夹具破坏 dominance，已改为可达原参数逃逸；实际 ABC 暴露
  EnvironmentRead 效果缺项后已修正。五 targets 构建退出 0，定向 6/6
  通过 4.23 秒，/tmp/abcd-captured-default-proof-final-{build,tests}.log。
- 在实际 method48015 的原工作图上调用 BuildCaptured 成功，返回 DefaultBody，
  有第三参数 initializer/op11、guard/op12 和 incoming slot0，残余图成本
  26,212 bytes / 4,478 work；这是该组件报告，不是 RSS。证据
  /tmp/abcd-captured-default-residual-real-final.log。此前 LLDB expression 的
  C++ 异常陷阱曾中断观察，使用 SetTrapExceptions(false) 获得真实返回值。
  record 仍未贯通，未运行五输入/full-suite/RSS；阶段一 Goal 保持 active。

### 2026-10-03：有限 Number 乘法的复杂表达式贯通

- 修复共同 MultiplyNumbers 类型规则：两边均有限时，即使可能为零，乘法
  仍产生非 NaN Number。依据 pinned FastRuntimeStub::FastMul 的 double
  乘法；溢出仍可能为 Infinity，不保留 finite/nonzero。零与可能 Infinity、
  未知 NaN 输入的拒绝保持，并有编译期负例约束。
- 实际编码的 `((left << 0) * (right << 0)) ** 2` 在修改前入口拒绝，
  修改后经共同 IR、普通/异常构树、独立语义验证和 printer 成功。三个历史
  profiles 都覆盖；原生行为对照包含转换次序、变异、BigInt/Symbol 和异常。
- 七 targets 构建退出 0，相关 9/9 CTest 通过 24.32 秒。新增组合 2,480
  场景；既有参数 42,160、位运算组合 47,120、数值 665、优先级 8,400
  场景通过。日志 /tmp/abcd-finite-product-{red,related-build,related-tests}.log。
- 三组 38 编码格式覆盖不代表任意输入语义完成。一般 ADD/SUB/MUL/DIV/EXP
  仍有值域准入边界，原 NaN/private-tag、SUB INT32_MIN 等限制未放宽。
  未运行固定五输入、full-suite、扩展语料或 RSS；阶段一 Goal 保持 active。

### 2026-10-03：捕获默认值候选边界（尚未迁移）

- FindDefaultCandidates 在共同入口默认值分支中记录词法读取及可选同值 guard，
  与已证明的 literal defaults 分开。重复读取、错误 guard、额外效果不会匹配。
  候选保留参数、合流、控制边、词法地址和原始操作；未改变签名迁移准入。
- 修正新测试夹具的悬空 removed use 后，default_candidates_test 通过。
  四个相关 targets 构建退出 0，定向 6/6 CTest 通过 3.19 秒，日志
  /tmp/abcd-captured-default-related-build-2.log / related-tests.log。
  首次相关构建误用了不存在的 default_signature_test，已按注册目标修正。
- method48015 的捕获默认参数仍需环境、捕获重绑定、签名来源和独立验证；
  本改动不能修复其 length 拒绝。未重跑五输入、full-suite 或 RSS；Goal 未完成。


### 2026-10-03：限定模块发布窗口的捕获准入

- ModuleCapturePrefix 将候选区间限定到同文件普通同步 record 入口、V2/V3
  资源排除合同、有效本地模块槽和普通非发送型函数创建。复用 metadata
  slot resolver；独立指令前缀计数检查观察点，核对 allocation、同块范围和
  对应 cell store。V1、重入、越界槽位及不同输入实例不能获得该证明。
- 证明绑定不可变 input identity/method 与 move-stable 指令/cell arena；
  family-owned storage 随父函数传递。原 capture binder/verifier 的精确
  creation、环境 rebasing 和 transitive Language writes 检查保留；只补充
  Language 延迟初始化，Defined 和 phased 规则未放宽。此前 RED 的
  record_published_capture_test 已转绿，注册行纳入正式提交。
- 三 profile 的定向 4/4 通过 2.10 秒，含独立模块执行对照；初次 C++
  size_t/uint64_t 同行 auto 推导冲突已修正。19 targets 构建退出 0，
  related-tests.log 27/27 通过 41.85 秒，按 selected.json 名称核对。
- 实际 HotListItemView record47697 / entry47847 越过原 child47875
  捕获失败。下一处为 family member2 / method48015 的 FunctionAbiError
  kLength，defaults_proved=false；hotitem-next.log 记录定位，不能宣称
  record 已完成。五输入 fresh
  build/stage-one/value-qualified-module-prefix-pass-1 均退出 0，仍为
  75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项回退、
  完整源码字节变化或 legacy，原生 default records 保留。
  合同见 module-capture-prefix.md，日志前缀
  /tmp/abcd-qualified-module-prefix-*。未运行 full-suite/RSS/扩展语料，
  源码等价合同及 superclass 限制未变，阶段一 Goal 保持 active。


### 2026-10-03：模块发布窗口的独立候选分析

- LocalAccessPlan 新增 module_prefix_initializations，沿现有线性扫描记录
  经过 ModuleStore 的 allocation/store 候选区间；与严格 unexposed 表分开。
  调用、读取、分支、其他观察点和环境不匹配均终止候选，不从中途重启。
  新表计入分配预算，不新增逐变量 CFG 遍历。
- 候选不构成初始化许可：当前 CapturePlan/ScopeFacts 尚未消费它。
  普通模块身份、有效导出槽、资源排除合同和非发送型创建仍必须随后结合
  验证；不能仅凭候选表或 OrdinarySynchronous bool 放宽捕获。
- 合成发布/重入 fixture 在三 profile 下核对候选及严格表的差异。
  module_publication_window_test、capture_plan_test、record_import_tree_test、
  lexical_value_flow_test 4/4 通过 8.17 秒；日志
  /tmp/abcd-module-prefix-related-build-2.log / related-tests.log。
  首次构建请求了不存在的 semantic_validation_test，已改用实际注册目标。
- 实际 record_published_capture_test 仍 RED，注册行继续留在未提交工作树。
  本提交没有恢复该 record，也没有重跑固定五输入；下一步需接入经过限定的
  捕获证明并关闭 RED。阶段一 Goal 保持 active。


### 2026-10-03：模块发布证明的元数据前提

- RecordModuleContext 保留经 ReadMergedRecordAttributes 校验的普通同步
  模块属性；OrdinarySynchronous 同时检查文件对象身份、record owner、
  模块表存在以及非 shared/非 top-level-await。CommonJS 仍在读取时拒绝。
  该事实不等于无重入或捕获初始化证明，尚未改变生产捕获准入。
- record_module_context_test 构建退出 0，定向 1/1 通过，覆盖 shared、
  top-level-await、错误 owner 和第二文件对象的拒绝。日志
  /tmp/abcd-module-publication-context-build.log / tests.log。
- 上一节 record_published_capture_test 仍 RED；CMake 与合成发布/重入
  夹具改动继续保留未提交。下一步将本元数据前提与资源合同、有效模块槽
  及无观察初始化窗口连接，不能把此 bool 单独当作允许跨发布的权限。
  阶段一 Goal 保持 active，固定输入统计未变化。


### 2026-10-03：正在定位模块发布前后的延迟捕获（未完成）

- 实际 HotListItemView record47697 / entry47847 的 conditional-family
  失败定位为 child47875（isShowDetail）creation PC17，CaptureCellError::kInput。
  该 child 读取 slot1 的 isTopThree，实际 slot1 到 entry PC37 才初始化。
  entry PC15 已发布另一个函数，现有 allocation-to-store unexposed-prefix
  在 ModuleStore 处终止。证据 /tmp/abcd-hotitem-family.json、
  /tmp/abcd-hotitem-entry.jsonl、/tmp/abcd-hotitem-detail-instructions.jsonl。
- 新合成 kPublishedCapture：创建词法环境、创建捕获 reader、模块发布、
  再做常量/词法初始化。另有 kReentrantCapture 在发布与初始化之间插入
  module read/call，必须仍拒绝。新 record_published_capture_test 已注册，
  当前 RED（1.12 秒，route refusal 5），日志
  /tmp/abcd-published-capture-red-build.log / red-test.log。
  这两项测试改动尚未提交；没有生产放宽。
- pinned ModuleValueAccessor::StoreModuleValueInternal 直接进入
  SourceTextModule::StoreModuleValue；普通已验证模块是本地数组写入/可能分配，
  但 shared module 的非共享值导出有普通 SyntaxError，sendable function
  GetCurrentModule 也走不同解析路径。因此不能把所有 ModuleStore 简单加进
  unexposed 白名单，也不能仅用未来支配 store 当作创建时初始化。
- 下一步需构造受同文件/record 普通模块元数据与资源排除合同约束的无重入
  初始化证据，维持原默认和 phased 路径严格性，并在独立 capture verifier
  中重验。公开方法、共享/TLA/发送型上下文及用户调用边界须分别拒绝或证明。
  现有 superclass 与一般算术合同不变，Goal 保持 active。


### 2026-10-03：省略模块末尾无效果完成语句

- 新模块空完成用例在旧 printer RED。共同 module-body printer 现在仅省略
  最后 ReturnStmt 的缺省值或直接 UndefinedLiteral，消除 `((void 0));`。
  普通函数/类方法返回不变，symbol、call、assignment、sequence 等仍求值；
  独立模块执行验证 `(state = 42, undefined)` 的赋值没有被删掉。
- 原 IR、树与 origin 保留，printer 为完成 stmt/可选 literal 记录真实
  模块 body 末尾的零宽事件。family emission 只对该确定情形允许空范围；
  空 module body 单独验证。位置 JSON 保留 node kind/ID，明确增加
  `emission: module_completion`，不伪造表达式范围或 AnchorId。
- boundary-tests.log 6/6 通过 4.24 秒；相关 31/31 通过 37.08 秒，
  额外位置序列化 1/1 通过 1.01 秒。合并 final-selected.json 核对
  24 targets / 32 test names 均有构建/通过证据。
- 五输入 fresh build/stage-one/value-module-completion-pass-1 均退出 0，
  保持 75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项
  退化或 legacy，原生 default records 保留。80 份完整/部分源码均可解析；
  AST 对照证明变化仅为各移除一个末尾无效果 module completion，其他
  AST 不变。此对照不是完整平台语义验收，构造与一般算术缺口仍在。
  合同见 module-completion.md，日志前缀 /tmp/abcd-module-completion-*。
  未运行 full-suite/RSS/扩展语料，Goal 保持 active。


### 2026-10-03：整数消费者中的 NaN payload 规范化

- 原非规范 NaN 的 bitwise/NOT 用例改为要求源码贯通后 RED；旧源码树因
  literal payload 拒绝。新增整数用途投影，只允许已证明 Number 的
  非规范 NaN 在 bitwise/shift/NOT 中改为 canonical NaN，保持 Number 类型、
  转换顺序和 mixed BigInt 异常。其他算术、返回、调用、存储不获得权限。
- 对 materialized 常量，纯复制函数图一次压缩，再一次统计有效用途；
  所有实际观察均为整数转换且至少有一个观察时才标记 producer。普通
  未简化复制链与 phased alias 共用该规则，O(values+ops+uses)，原始 IR
  和来源保持。独立 evaluator verifier 检查标记和用途，operand verifier
  只在授权位置接受 canonical literal，不全局放宽字面量检查。
- 初版遗漏普通未简化复制链，Source 构造失败；补齐一次性复制身份压缩
  后通过。返回同一原始 NaN、伪造 projection、把 canonical NaN 改成零
  均拒绝。1/2/200/201/402/804 链验证通过，boundary-tests.log 1/1
  通过 2.27 秒。正常/异常 bitwise 和现代/deprecated NOT 均覆盖。
- 25 targets 构建退出 0，相关 33/33 CTest 通过 67.61 秒，与
  selected.json 按名称一致。独立原生场景：42,160 参数、47,120 组合、
  665 数值/运算符场景通过。五输入 fresh
  build/stage-one/value-nan-erasure-pass-1 均退出 0，保持
  75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项回退、
  完整源码字节变化或 legacy，原生 default records 保留。
  日志前缀 /tmp/abcd-nan-erasure-*；未运行 full-suite/RSS/扩展语料。
  一般算术、其他 NaN 观察、构造缺口与阶段一验收仍未完成；合同未变，
  Goal 保持 active。


### 2026-10-03：已证明 Number 的 ToNumeric 身份恢复

- 新 decoded 现代/deprecated ToNumber→ToNumeric 用例在旧源码树构造 RED，
  1/1 失败 1.99 秒。旧 NonNaNNumber 前提使 Number（可含 NaN）仍分配
  postfix scratch，V1 因额外运算被拒绝。现用共享 NumberEvaluation
  判断，binding allocation、forest 和独立 operands verifier 一致消费证明。
- 提取 FloatNumberKinds 复用原有 IEEE/私有 tag 分类，避免 source 层另写
  边界；Integer、特殊 NaN/Infinity 和 ValueId Number 分别处理。impure
  编码和越界 ValueId 拒绝，非规范 NaN 字面量的可表示性 gate 不变。
  一般未知 Number/BigInt 输入仍使用原 scratch 与资源排除约束。
- 三 profile、1/2/200/201/402 长度的普通/catch 路径在 V1 下通过，树中
  不含 UpdateExpr；删除 Number witness 后独立语义检查拒绝已有身份树。
  现代/旧单次链均实际生成 `return +value;`。测试中的 IntegerLiteral
  名称歧义已限定 analysis 命名空间修正。
- 25 targets 构建退出 0，相关 33/33 CTest 通过 71.32 秒，与
  selected.json 按名称一致。独立 ToNumber 语义 oracle 对照生成链，
  2,544 场景通过，包括原参数身份、一次转换、异常、signed zero 和 NaN
  payload；没有可修改的全局转换 helper 或新增 writer。
- 五输入 fresh build/stage-one/value-numeric-identity-pass-1 均退出 0，
  保持 75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项
  回退、完整源码字节变化或 legacy，原生 default records 保留。
  日志前缀 /tmp/abcd-numeric-identity-*；未运行 full-suite/RSS/扩展语料。
  等价验收环境选择仍未收到答复，现有合同未变，阶段一 Goal 保持 active。


### 2026-10-03：区分 Number 类型与 non-NaN 数值安全事实

- decoded `(+left << +right) ** 2` 在旧输入准入 RED，number-type-red-test
  记录 opcode21 拒绝；NOT 生成的指数另行 RED。新增 Number refinement，
  包含 NaN、排除 BigInt/private marker，不授予 finite/non-NaN/integral。
  Join 保留共同 Number 类型；有限和表示排除事实仍独立削弱。
- ToNumber 使用独立 ConvertedNumber 规则；ToNumeric 保留 BigInt 的
  一般 Defined 边界，并保留已有 Number 事实。已接受 FLDAI NaN 与 canonical
  NaN 常量获得 Number，impure 编码仍 Unknown。普通/phased SSA 与 linear
  lexical 共用规则，无源码文本或样本分支。
- 位运算可据 Number（含 NaN）正常返回 int32。现代/deprecated NOT 的
  Number 路径新增有限整数结果证明；一般 Language 仍可能 BigInt，只到
  Defined。没有授予 tagged-minimum 排除，没有删除一般算术/求负的
  NaN/private-tag 检查。组合继续复用原生表达式树与优先级打印。
- boundary-tests.log 6/6 通过 16.79 秒；三 profile 的普通/异常路径、
  subtype join、Number/BigInt/Symbol、signed zero、NaN 与幂/位运算组合
  验证通过。独立原生场景分别为 42,160 参数、47,120 组合、645 数值场景。
- 24 targets 构建退出 0，相关首轮 30/31 通过 60.66 秒。原寄存器测试
  两处仍期望 NaN 输入为宽泛 Language，更新为 Number；危险 ADD/NEG
  结果仍 Unknown 和原拒绝 PC 的断言不变。repair-tests.log 4/4 通过
  8.58 秒；按 selected.json 合并核对，31 项均有通过证据。
- 五输入 fresh build/stage-one/value-number-type-pass-1 均退出 0，保持
  75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项退化、
  完整源码字节变化或 legacy，原生 default records 保留。
  日志前缀 /tmp/abcd-number-type-*；未运行 full-suite/RSS/扩展语料。
- 已异步询问最终等价验收应以同版 Ark 重编译执行还是现有跨引擎合同为准；
  尚未收到选择，当前合同及此前拒绝的 superclass 非 Proxy 假设都未改变。
  本次类型改进不依赖合同变更，阶段一 Goal 保持 active。


### 2026-10-03：命名环境恢复 record 局部变量名

- 真实 ABC 已有 DOMAIN 等命名环境信息，旧路径只校验而不用于源码符号。
  新 decoded class-only named-cell 用例 RED（record_named_cell_test，1.95 秒），
  实际仍输出 $local。共享 ResolveCellNames 按 literal 分组，验证 string/slot
  对并生成 allocation/slot 名称提示；共同 FunctionTree 仍按 cell 身份绑定。
  禁止名称、重复槽位不冒充语义，当前名称 arena 冲突保留生成名称。
- record 首次用名称提示构造；若后续作用域/类验证拒绝，可销毁未发布候选
  后用生成名称重试一次，语义和发布 gate 完全相同。资源、工作量、IO、
  文件变化和输出限制不重试，没有 legacy 路由。跨类全局 capturedValue
  读取的负例证明不会被同名局部捕获；原有 Hole/未初始化拒绝仍通过。
- 三 profile 的真实解码例，以及 alias pool、歧义/非法名称、非法槽位、
  work 限制和 1/2/200/201/402/804 规模验证通过。首次构建的缺失 include
  和局部变量重名已修正；boundary-tests.log 5/5 通过 5.67 秒。
- 22 targets 构建退出 0；related-tests.log 29/29 通过 40.47 秒，按
  selected.json 名称核对。受影响记录追加 5/5 配对测试通过 8.23 秒；
  EntryBackupAbility 方法 1,200 场景继续通过，并明确断言 DOMAIN 已恢复。
- 五输入 fresh build/stage-one/value-cell-names-pass-1 均退出 0，保持
  75 complete / 5 incomplete / 26 failure / 46 no-module，无完整项退化或
  legacy。11 complete 和 1 incomplete 源码发生标识符改名；例如 DOMAIN、
  KEY_TEXTAREA、DATA_KEY、profileId、uiContext。80 份完整/部分源码均经
  Acorn 解析，非标识符 token 一致。检查脚本最初漏列既有 $binding 生成
  名称，核对 SyntheticName 后补入；这只是 token 结构证据，不是语义证明。
  作用域和配对行为验证另行成立；源码位置仍由共同 printer 生成。
- 合同与复杂度见 cell-names.md。本节日志前缀 /tmp/abcd-cell-names-*。
  未运行 full-suite/RSS/扩展语料；普通函数局部命名、const 表示、构造缺口、
  一般算术及阶段一整体验收仍未完成，Goal 保持 active。


### 2026-10-02：只有类的 record 根词法环境贯通

- 当前实测 HotRankPage 的一般 SUB 和列表 key 回调的一般 ADD 仍未通过
  独立值域验证，没有借本次修改放宽。另定位 Push EntryBackupAbility
  record6659 / entry6809：BuildRecordTree 的根 EvaluationPlan 将 class
  closure writes 留为 Unknown，CellInitialization 在类创建处拒绝；不是
  ABC 读取失败。back-up 调试证据位于 /tmp/abcd-backup-*-debug*.log。
- 新 class-only 合成模块在旧路径 RED，record error 6；根环境先存入
  import 的值，随后只创建捕获该 cell 的类，不靠普通 DEFINEFUNC 顺带
  提供写域。record_tree 现在沿既有原子构造流程提供条件性 Language
  写域：先建原生父 cell，再准备每个类体，最后重验并发布。任一类体失败
  仍丢弃未打印的父树；没有增加 superclass 非 Proxy 或不可变前提。
- 三 profile 的正例、类体写入私有 Hole 及缺少父初始化负例通过。独立
  模块 oracle 验证 import 后续更新不改变已捕获值，对象/函数身份、原始值
  构造返回规则、子类构造和无 new 拒绝。真实 onBackup/onRestore 对照配对
  TS 源码，共 1,200 场景通过：logger/JSON/Promise getter 和 receiver、
  可变 callee、toJSON、异常身份、thenable 与 await 顺序。构造函数及平台
  生命周期未执行；paired 测试显式要求该 record 仍 incomplete。
- 18 targets 构建退出 0，相关 24/24 CTest 通过 39.19 秒，按
  selected.json 名称核对。五输入 fresh
  build/stage-one/value-record-class-cells-pass-1 均退出 0，变为
  75 complete / 5 incomplete / 26 failure / 46 no-module。record6659 从
  failure 推进到带构造缺口标记的部分源码；原 75 个 complete 无回退或源码
  字节变化，无 legacy，原生 default records 保留。未运行 full-suite/RSS。
  本节主要日志前缀 /tmp/abcd-record-class-cells-*；阶段一 Goal 保持 active。


### 2026-10-02：位运算／移位的正常结果事实贯通

- decoded `(1 << value) ** 2` 在旧共同 FunctionInput 阶段 RED，
  red-tests.log 记录 opcode 21 的准入失败。新增共享 kBitwise/kUnsignedShift
  规则，由普通/phased SSA 与 linear lexical 调用同一 helper。
- 对照 pinned interpreter-inl.cpp、RuntimeShl2/Ashr2/And2/Or2/Xor2：
  一侧已证明 non-NaN Number 时，混合 BigInt 走异常，正常结果为有限整数
  Number；不能继承 nonzero 或 tagged-minimum 排除。双方仅 Language
  时仍可能是 BigInt，仅保留 Defined。RuntimeShr2/BigInt unsigned-shift
  路径拒绝 BigInt，正常 uint32 结果额外证明排除 tagged INT32_MIN。
- 六种位运算/移位与幂组合经过真实解码、普通/catch 共同表达式树和
  独立验证，保留左右转换顺序与异常。独立原生对照：42,160 参数场景、
  32,240 位运算组合场景、587 数值/组合场景通过，final-native.log。
  未加入源码文本识别、样本判断、额外 writer 或 Number 全局辅助调用。
- 边界扩展发现非规范 NaN 字面量被现有源码树拒绝。调试定位左操作数
  0xfff7000000000005；数值结果证明成立，但普通/异常构造明确返回
  kSemantics，不能将数值域准入当源码可表示性。现分别断言该拒绝，保留
  canonical NaN 源码案例和同一非规范 NaN 的运行时参数行为对照。
  boundary-tests-2.log 3/3 通过 9.63 秒；未靠改写 NaN payload 消除失败。
- 24 targets 构建退出 0，相关 31/31 CTest 通过 61.66 秒，与
  selected.json 按名称一致。五输入 fresh
  build/stage-one/value-bitwise-facts-pass-1 均退出 0，保持
  75 complete / 4 incomplete / 27 failure / 46 no-module；无完整记录回退、
  完整源码字节变化或 legacy，三个原生 default records 保留。
  本节日志前缀 /tmp/abcd-bitwise-facts-*；full-suite/RSS/扩展语料未运行。
  任意算术输入、NaN 字面量及完整阶段一验收仍未关闭，Goal 保持 active。


### 2026-10-02：一般参数二元／比较运算的行为验证

- binary_initializer 从纯常量矩阵扩展到双 Language 参数，17 个已准入
  运算经过真实解码、Value IR、普通/异常表达式树、独立验证和共同 printer。
  ADD/SUB/MUL/DIV/EXP 的一般输入拒绝作为明确负例保留，不把编码覆盖
  误报为任意输入支持。三个 profile 的 decoded 普通/catch 用例通过。
- 独立 oracle 比较重复属性初始化中的求值/转换次序、左右转换期间的
  状态修改、BigInt/Symbol/NaN/signed zero、Proxy、对象/参数/receiver
  身份、@@hasInstance 及注入异常。catch 路径与 ABC 一致返回 undefined，
  不把被中断的后续字段视为已执行。40,800 个一般参数场景通过，既有
  335 个数值/组合场景也通过；final-native.log。
- 首次夹具构建发生本地 input 命名冲突，已修正；首轮 oracle 将包装前
  对象误当作 @@hasInstance 参数，改为核对实际传入参数后通过。
  没有因此修改生产语义或放宽异常对照。
- binary_initializer_test 构建退出 0，普通及 catch 2/2 CTest 通过
  10.83 秒，phased-build.log / phased-tests.log。本节日志前缀
  /tmp/abcd-binary-arguments-*。本轮仅修改测试与文档，没有生产修改，
  未重复五输入导出或扩大测试/RSS范围；最新五输入证据仍为上一节的
  value-update-facts-pass-1，75 complete / 4 incomplete / 27 failure /
  46 no-module。此验证不是 Ark 二进制或阶段一完成声明，Goal 保持 active。


### 2026-10-02：更新表达式保留有限整数事实

- 真实 decoded 组合用例复现 INC/DEC 产生的指数在 FunctionInput 被拒绝，
  red-tests.log 记录失败。原因是旧 kNonNaNUnary 只保留粗粒度 non-NaN。
  新 kNumericUpdate / UpdatedNumber 由普通/phased SSA 与 linear lexical
  共用；有限输入更新一后仍有限，已证明整数仍为整数，无样本分支。
- 对照 pinned interpreter-inl.cpp 的四种现代/deprecated 更新编码：
  整数路径保护溢出，double 路径加减一。规则不继承 nonzero 或 tagged
  INT32_MIN 排除，避免错误地授权后续 SUB2/legacy NEG；NaN 与一般
  Language 准入仍未放宽。更新仍复用既有独占 scratch 和共同 printer。
- 新边界用例覆盖 signed zero、subnormal、最大有限 double、无穷、
  拒绝 NaN，四编码均经过普通/phased 树与独立验证。幂表达式分别从
  literal/NEG/INC/DEC 构造指数，三 profile、1/2/200/201/402 长度均验证。
  boundary-tests.log 3/3 通过 5.83 秒；原生行为矩阵扩至 335 场景通过。
- 24 targets 构建退出 0；选定 31/31 CTest 通过 66.86 秒，按
  selected.json 名称核对一致。五输入 fresh
  build/stage-one/value-update-facts-pass-1 均退出 0，保持
  75 complete / 4 incomplete / 27 failure / 46 no-module；无完整记录回退、
  完整源码字节变化或 legacy，原生 default records 保留。
  本节日志前缀 /tmp/abcd-update-facts-*；未运行 full-suite/RSS/扩展语料。
  编码矩阵不等于一般输入语义或阶段一完整验收，Goal 保持 active。


### 2026-10-02：整数指数与完整二元／比较编码矩阵

- 新 decoded binary_initializer 矩阵按 PinnedIsaCatalog 核对全部 22 个
  binary/comparison 编码，通过同一 FunctionInput、Value IR、表达式树、
  独立语义/origin 验证与 printer。不同 acc/register 种子检查方向，
  负数种子区分有符号/无符号移位；与既有 16 unary 编码矩阵合计 38。
  这是当前输入前提下的编码覆盖，不是任意输入值域完成声明。
- 首轮在 EXP opcode 27 的 FunctionInput 准入 RED，1/1 失败 4.81 秒，
  /tmp/abcd-binary-initializer-red-tests.log。新增有限整数 Number 事实，
  与 VM tagged-int32 排除独立；整数/有限整数 double 字面量建立，复制、
  身份转换和所有入边均成立的 join 保留。共同 ExponentNumbers 规则
  在普通/phased SSA 和 linear lexical 分析中复用，无样本分支。
- pinned EXP / RuntimeExp 的 Number pow 路径在 non-NaN 底数、有限整数
  指数下不会进入 NaN 域，正常结果为 NonNaNNumber。保留负底数、正负零、
  无穷、溢出及下溢；小数/无限指数、NaN 底数及任意 Language/BigInt
  仍无通用准入证明。依赖的 std::pow/源语言幂运算不等于 Ark 二进制验证。
- 新复合用例另行复现 NEG 计算的负指数丢失整数事实，negated-red-tests.log
  失败于共同输入准入。修正共享 NegatedNumber，使已有整数事实在已证明
  求负中保留；现代/旧求负仍分别遵守原有 tag 前提。
- 指数值域测试包含 ordinary/phased 完整树、lattice 结合/交换律及拒绝
  边界；连续左嵌套幂与 NEG 指数经真实解码，覆盖三 profile 以及
  1/2/200/201/402 长度和 work 增长。223 个独立原生场景通过，涵盖
  运算方向、符号、@@hasInstance 顺序/异常与 Object.is 数值对照；
  native-cases.log。后续仍需更广的复合表达式和一般输入证明。
- 24 个相关 targets 构建退出 0；最终选定 31/31 CTest 通过 66.95 秒，
  与 selected.json 按名称一致；final-build.log / final-tests.log。
  五输入 fresh build/stage-one/value-exponent-pass-2 均退出 0，仍为
  75 complete / 4 incomplete / 27 failure / 46 no-module，无完整记录回退、
  完整源码字节变化或 legacy，三个原生 default records 保留。
  除首轮矩阵 RED 外，本节日志前缀 /tmp/abcd-exponent-*。
  full-suite/RSS/扩展语料仍未运行；完整 ISA 输入域及阶段一 Goal 保持 active。


### 2026-10-02：一般 ToNumeric 的独占临时转换

- 独立现代/deprecated decoded 用例在旧 FunctionInput 阶段 RED，
  red-tests.log 1/1 失败 2.17 秒。正常结果为 Defined Language，保留
  Number/BigInt 而不授予 non-NaN 事实；已证明 Number 仍走身份下降。
- 其他输入复用独占 UpdateBinding，保存输入一次并返回原生 postfix 的
  oldValue。依据 ECMAScript 13.4.2.1，oldValue 正是 ToNumeric 的结果；
  额外更新只写不可观察的编译器 scratch。构造、TreeFlow 和操作数验证
  均检查隔离与 postfix 形式；仅资源差异排除契约允许该下降，V1 拒绝。
  无可修改的全局转换器或独立 writer。此处是源语言下降，不声称 Ark VM
  二进制或平台端到端验证。
- boundary-tests.log 2/2 通过 4.37 秒，覆盖改成 prefix、改写参数、V1
  契约及私有 Hole 拒绝。expanded-tests.log 5/5 通过 23.55 秒，包含既有
  更新表达式与 phased 字面量。独立 ToPrimitive(number)/BigInt 保留 oracle
  对照现代/旧编码及普通/catch 路径，共 2,624 场景通过，验证调用次数、顺序、异常、BigInt、
  signed zero、原始与对象返回的 NaN payload。三 profile 的
  1/2/200/201/402 次转换均验证。
- 规范依据：https://tc39.es/ecma262/2023/multipage/ecmascript-language-expressions.html#sec-postfix-increment-operator
  115 targets 构建退出 0；相关 223 项首轮 221/223 通过，539.75 秒。
  两个 partial 夹具仍把一般 ToNumeric 当源码缺口；改用缺少数值/tag
  证明的 deprecated NEG，并保留 prefix 回滚、PC 和嵌套不完整传播断言。
  中间尝试的 modern NEG 在结果未使用时本可恢复，未作为最终负例。
  修正后 repair-tests.log 5/5 通过 4.82 秒；按名称合并核对
  /tmp/abcd-numeric-conversion-selected.json，223 项均已有通过证据。
  五输入 fresh build/stage-one/value-numeric-conversion-pass-1 均退出 0，
  75 complete / 4 incomplete / 27 failure / 46 no-module，无回退、完整源码
  字节变化或 legacy，原生 default records 保留；final-batch-summary.json。
  该项不是任意算术值域或阶段一完成声明；日志前缀
  /tmp/abcd-numeric-conversion-*。完整 ISA 与阶段一 Goal 保持 active。


### 2026-10-02：一般 ToNumber 的正常结果证明

- pinned JSTaggedValue::ToNumber 保持已有 Number，转换字符串/原始值或在
  ToPrimitive(number) 后递归；BigInt/Symbol 为异常路径。正常结果为 Defined
  Language，不等于 non-NaN。新 kToNumber 规则在普通/phased/linear lexical
  路径共享 ConvertedNumber；已证明 non-NaN 输入保留原数值/tag 事实。
  ToNumeric 仍单独使用数值身份前提，没有因这次修改而放宽。
- 真实 decoded 现代/deprecated 用例在旧 FunctionInput 阶段 RED，
  red-tests.log 1/1 失败 1.79 秒。现复用共同 UnaryExpr 一元 +，没有可修改
  Number 辅助调用或额外恢复器。三 profile 的 1/2/200/201/402 次转换经过
  普通/phased 完整树与独立验证；异常处理返回原参数，验证未被转换写回。
- focused-tests.log 4/4 通过 8.53 秒。独立原生 2,544 场景覆盖 primitive、
  自定义转换、valueOf/toString 顺序、Proxy、BigInt/Symbol TypeError、注入
  异常、转换次数、NaN payload 和 signed zero。私有 Hole 输入仍拒绝，未知
  参数的转换结果不获得 non-NaN 或 tagged-minimum 排除事实。
- 114 个相关 targets 构建退出 0，221/221 回归通过 533.40 秒，
  related-build.log / related-tests.log，与
  /tmp/abcd-number-conversion-selected.json 按名称核对一致。
  五输入 fresh build/stage-one/value-number-conversion-pass-1 均退出 0，
  75 complete / 4 incomplete / 27 failure / 46 no-module，无回退、完整源码
  字节变化或 legacy，原生 default records 保留；final-batch-summary.json。
  该交付不代表一般 ToNumeric、
  任意算术值域或阶段一整体验收完成。本节日志前缀
  /tmp/abcd-number-conversion-*；Goal 保持 active。


### 2026-10-02：deprecated 求负的独立语义与证明

- 将 DEPRECATED_NEG 保留为 UnaryOperator::kLegacyNegate，不与现代 NEG
  混同。共享 NegatedNumber/LegacyNegatedNumber 规则保留有限/非零事实，
  输出排除 tagged INT32_MIN；legacy 额外要求输入已有该排除。现代 NEG
  的整数最小值 double 路径与 legacy 的缺失路径分别对照 pinned 源码。
  共同表达式构造和独立操作数验证都检查 legacy 前提后下降为原生求负。
  缺少证明的旧轻量 scalar writer 仍拒绝，而非另开无证明输出路径。
- 新真实 decoded 夹具在旧 FunctionInput 阶段 RED，red-tests.log 1/1
  失败 1.06 秒；首次构建的 IntegerLiteral 名称歧义已修正，构建日志保留。
  三 profile 覆盖 int/double、signed zero、infinity、连续 1/2/200/201/402
  次求负；tagged 最小整数和危险 NaN 拒绝。普通/phased 完整树与独立语义
  验证通过，phased-tests.log 2/2 通过 2.22 秒。
- 移除输入排除证明后，构造及独立 TreeSemantics 都拒绝，witness-tests.log
  2/2 通过 2.09 秒；该项是显式数值前提的隔离 verifier 边界测试。8 个原生
  求负结果对照通过。统一 unary_initializer 矩阵按 PinnedIsaCatalog 检查
  全部 16 种一元编码均列入，并保持 acc/register 种子区分及三 profile 验证。
- 更新仍把 legacy neg/instanceof 当 unsupported 的标量/寄存器效果测试，
  class heritage 保留 caller-private 寄存器事实；unsupported 失效用例使用
  真正未支持的 runtime 指令。integration-tests.log 定向 7/7 通过 7.02 秒。
- 113 个相关 targets 构建退出 0，219/219 回归通过 535.34 秒，
  related-build.log / related-tests.log，与 /tmp/abcd-legacy-negate-selected.json
  按名称核对一致；未宣称任意输入值域均支持。
  五输入 fresh build/stage-one/value-legacy-negate-pass-1 均退出 0，仍为
  75 complete / 4 incomplete / 27 failure / 46 no-module，无回退、完整源码
  字节变化或 legacy，原生 default records 保留，final-batch-summary.json。
  本节日志前缀 /tmp/abcd-legacy-negate-*；完整 ISA 与阶段一 Goal 保持 active。


### 2026-10-02：共享加减数值证明

- additive_expression_test 从真实 ADD2/SUB2 解码进入 FunctionInput、
  ValueProgram、共同树、独立语义及来源验证。旧实现在 FunctionInput
  断言 RED，red-tests.log 1/1 失败 1.05 秒。现增加含 signed zero 的
  FiniteNumber 事实，加入共享 AddValues/AdditiveNumbers/SubtractNumbers，
  普通/phased 传播与 linear lexical 路径一致；后者也补齐遗漏的除法分支。
- 核对 pinned interpreter-inl.cpp 的 SUB2 fast path 时发现，整数 RHS
  INT32_MIN 先被取负，C++ 有符号溢出无法据此证明原生减法等价。
  /tmp/abcd-sub2-boundary.log 的 UBSan 小探针确认源码级未定义行为，
  不是 Ark VM 二进制运行证据。减法新增 ExcludesInt32MinTag 前提，常量
  整数最小值不授予，同数值的 FLDAI double 独立授予；合流按交集保留。
  已知 Number 的 ToNumber/ToNumeric 保留其原事实，递增递减不沿用该排除。
  其他未知输入及既有非规范 NaN 内部标记反例仍拒绝。
- boundary-tests.log 4/4 通过 14.17 秒；value-tests.log 2/2 通过 1.99 秒。
  原有 quotient 证明保持。新增格的合流代数检查、有限/无限/NaN 位模式矩阵、
  正常/phased 拒绝与 Commit 结果验证；整数最小值经身份转换、由 DEC 产生
  的拒绝也通过，tag-tests.log 3/3 通过 1.60 秒。
- 原生结果对照 264 个整数/float/signed-zero/overflow 场景通过；source-tests.log
  4/4 通过 3.25 秒。真实整数解码覆盖三 profile 和 1/2/200/201/402 次链式
  运算。整体验收仍未完成；未宣称全数值域可用。
  五输入 fresh build/stage-one/value-additive-pass-1 均退出 0，仍为
  75 complete / 4 incomplete / 27 failure / 46 no-module；无回退、完整源码
  字节变化或 legacy，原生 default records 保留，final-batch-summary.json。
  首轮 111 targets 构建成功，related-tests.log 215/216 通过 507.68 秒。
  新纳入的 value_global_import_test 暴露既有 LDGLOBALVAR acc 定义缺失；
  寄存器效果改用 ReadsGlobal，保留 Var 独立类型、Unknown 结果、消费未知
  结果的拒绝和源码字面量拒绝。global-tests.log 2/2 通过 3.11 秒。
  修复后的 fresh build/stage-one/value-additive-pass-2 五输入也均退出 0，
  complete/partial/failure/no-module 计数及完整源码字节与 pass-1 一致，
  final-2-batch-summary.json 留存证据。
  包含生产效果修复后重新构建成功，完整 216/216 回归通过 508.02 秒，
  final-related-build.log / final-related-tests.log；与
  /tmp/abcd-additive-selected.json 按测试名称核对一致。
  共享事实合同见 numeric-facts.md。本节日志前缀 /tmp/abcd-additive-*；
  完整 ISA/阶段一 Goal 保持 active。

### 2026-10-02：统一表达式优先级与子表达式上下文

- 共同 tree_print_tokens 提供强类型 binding strength/associativity；printer
  的 ExpressionOperand 携带父操作和左右位置，迭代打印保留 ExprId 来源区间。
  二元/一元/逻辑节点按上下文分组，不做文本替换或 IR 重排。显式处理幂的
  右结合及左侧一元限制、相邻正负号、nullish/logical 混用；callee 与 await
  采用相应最低绑定强度。typeof 保留强制 GetValue 的逗号包装。
- 新 AST/printer 隔离夹具在旧 printer 的分组输出断言 RED，red-tests.log
  1/1 失败 1.02 秒；不是 ABC 数值域证明。最终 30 个分组/结合性用例、
  8,400 个独立原生行为场景通过，含 await 时序和逻辑 callee。逻辑/await
  语法夹具明确构造源 AST，不当作原 IR 或 coroutine 的等价证明。
  左右深树 1/2/200/201/402/804 检查迭代打印、范围边界和 work 增长；
  402/804 work 小于前一规模三倍。无物理 RSS 或性能基准声明。
- 96 个相关 targets 构建退出 0；首轮 related-tests.log 196/199 通过
  477.65 秒。三个旧格式断言/变异定位点更新后，final-tests.log 5/5 通过
  14.06 秒。typeof 的 GetValue 包装、变异必须改变源码且被行为 oracle
  拒绝的断言均保留。按 /tmp/abcd-precedence-selected.json 核对，199 项
  都有通过证据。生产代码未因这些格式测试失败而放宽语义。
- 五输入 fresh build/stage-one/value-precedence-pass-1 均退出 0，仍为
  75 complete / 4 incomplete / 27 failure / 46 no-module，无回退或 legacy。
  75 个完整源码全部 Node --check 通过；42 个文本变化，用本机 Node 附带
  Acorn 8.15.0 忽略位置/raw 后逐 record 比较，75 个解析树均相同。
  syntax.log / ast-check.log / final-batch-summary.json 留存证据。该解析
  仅用于格式回归比较，不是原 ABC/配对源码语义 oracle，PC 映射仍由打印
  事件生成；未新增项目依赖。
- 本节日志前缀 /tmp/abcd-precedence-*。完整 ISA 的数值域、一般 ToNumeric
  与 deprecated.neg 等剩余能力及阶段一验收仍未完成，Goal 保持 active。

### 2026-10-02：统一一元操作的字面量组合

- LiteralUnary 原先拒绝 ToNumeric/Increment/Decrement，即使输入已通过
  数值域证明，共同 AST 也已有独占 UpdateBinding 和原生 Sequence/Update
  下降。新的正例先在旧实现失败，/tmp/abcd-unary-initializer-red-tests.log，
  1/1 RED 1.08 秒。现只扩展共同字面量归属支持域，不改变数值证明或
  ToNumeric 的 Number/BigInt 区别，不写回原操作数。
- 现代三个操作在 try/catch 字面量中经过完整树与独立语义检查。未知输入
  值域、共享结果拒绝仍有效，三 profile 的 1/2/200/201/402 属性验证通过。
  focused-tests.log 4/4 通过 18.89 秒，含既有 numeric_update_tree 的行为对照。
- 新普通路径矩阵覆盖 15 种一元编码，现代 acc 种子 9、deprecated 寄存器
  种子 7，显式检查不同输入路由；每种均覆盖三 profile 和 1/2/200/201/402
  初始化。normal-source-tests.log 2/2 通过 4.05 秒；独立手写原生结果核对
  15 种编码，另核对三种 phased 输出的分组、own property 与分配身份。
  这是已证明数值输入上的组合覆盖，不能称任意输入类型均已完成。
- deprecated.neg 仍未纳入成功清单，其固定运行时边界单独待解；完整 ISA
  运算符、复杂表达式优先级及阶段一验收仍未完成。95 个相关 targets 构建退出 0，197/197 回归通过 475.51 秒，
  related-build.log / related-tests.log，与 selected.json 按名称核对一致。
  五输入 fresh build/stage-one/value-unary-initializer-pass-1 均退出 0，
  75 complete / 4 incomplete / 27 failure / 46 no-module，无回退、完整源码
  字节变化或 legacy，原生 default records 保留；final-batch-summary.json。
  本节日志前缀 /tmp/abcd-unary-initializer-*。


### 2026-10-02：完整 ISA 运算符与复杂表达式覆盖（用户新增优先任务）

- 用户明确要求在共同贯通方案上覆盖 isa.yaml 的所有 unary/binary/
  comparation 操作，兼顾优先级和复杂表达式，禁止按样本定制。实施合同见
  operator-expressions.md。固定 runtime_core/isa/isa.yaml 的三个组共
  37 条签名、38 种编码（typeof 两种宽度）；包含 deprecated 一元形式。
  runtime_core 与 ets_runtime 的当前 HEAD 均与固定 lock 一致。
- 首个明确缺口 instanceof 已新增到共同 BinaryOperator、标量提升、正常
  Language 结果分类、布尔谓词、源码树验证及 printer。参考 pinned
  interpreter-inl.cpp 的 INSTANCEOF_IMM8_V8（寄存器左、acc 右）和
  RuntimeInstanceof -> JSObject::InstanceOf；保留 user-code/throw 效果。
- 新 decoded 共同流水线夹具在旧实现 FunctionInput 阶段 RED，
  /tmp/abcd-instanceof-red-tests.log，1/1 失败 1.04 秒。接入后三 profile
  1/2/200/201/402 次组合通过；原 scalar_test 中的“instanceof 不支持”断言
  已更新为成功和编码越界拒绝。second-tests.log 2/2 通过 0.91 秒，
  behavior.log 独立原生 144 个 @@hasInstance、Proxy、绑定函数、primitive、
  TypeError/注入异常场景通过。换操作数/伪造运算类型拒绝检查已通过；negative-tests.log
  3/3 通过 1.22 秒。
- 94 个相关 targets 构建退出 0。首轮 related-tests.log 194/195 通过
  478.23 秒；register_ssa_test 仍把 instanceof 当作 unsupported。更新为
  正例，并以合法但未支持的 runtime 指令保留不可达/拒绝边界后，
  ssa-tests-3.log 4/4 通过 3.23 秒；与 /tmp/abcd-instanceof-selected.json
  按名称核对，195 项均有通过证据。第一次替代夹具使用无效 opcode，被
  CFG 提前拒绝，已修正；相关失败与构建日志保留，没有改生产验证来迁就测试。
- 五输入 fresh build/stage-one/value-instanceof-pass-1 均退出 0，仍为
  75 complete / 4 incomplete / 27 failure / 46 no-module；无回退、完整源码
  字节变化或 legacy，原生 default records 保留。证据日志前缀为
  /tmp/abcd-instanceof-*。
- 当前只完成 instanceof 子项，不宣称全覆盖。deprecated.neg 的 INT32_MIN 行为、数值域
  证明、ToNumeric/update 组合及统一优先级/结合性打印仍需逐项完成。
  阶段一目标保留，新增工作不读取配对源码生成答案；Goal 保持 active。


### 2026-10-02：原生二元字面量初始化

- HotRankPage child 49640 的已证明 String ADD2 在 buffered 对象之后产生，
  旧 LiteralExpression 不接纳二元操作，导致 EarlyValue kLateValue。
  新 literal_binary_test 使用真实 decoded 全局属性读取和字符串加法，
  经 FunctionInput/ValueProgram 到统一树与来源检查；旧实现构建成功后
  在 EvaluationPlan 断言失败，red-tests.log 1/1 RED 1.03 秒。
- 接纳有两个操作数、结果已证明为 Language 的 ValueBinary，复用原生
  BinaryExpr 及既有用途/效果/来源验证，不修改算术值域规则。
  三 profile 覆盖字符串加法、严格相等、关系比较、按位或，以及任意未知
  ADD2 输入拒绝。共享结果额外返回、删除 nested 映射、debugger 阻隔均拒绝。
  normal/phased 初始化覆盖 1/2/200/201/402 属性，后者也经过异常源码树及
  TreeSemantics。phased-tests.log 3/3 通过 19.06 秒。
- 独立原生表达式对照 12,584 个 coercion/getter/异常场景通过，包括
  Symbol/BigInt/NaN/-0 与 default/number 转换 hint，behavior-2.log。
  首次 harness 使用跨环境的 global getter 参考导致 ReferenceError 差异，
  behavior.log 保留失败；改为在相同 VM 环境运行独立手写原生语法后通过。
  这是生成 JS 行为证据，不是 Ark VM 的全语义证明。
- 五输入 fresh build/stage-one/value-literal-binary-pass-1 均退出 0：
  75 complete / 4 incomplete / 27 failure / 46 no-module。原 75 个完整项
  无回退或源码字节变化，无 legacy，原生 default records 保留；
  final-batch-summary.json 留存证据。没有新增完整 record。
- HotRankPage 越过原字面量失败，随后 method 49199 在原 PC 68 报
  SemanticProofError::kInternalValue：PC 65 为 opcode 11 的减法，PC 68
  为结果运输；read SSA value 102 的 domain bits 为 0x0f，未证明 Language。
  hotrank-domain.log / hotrank-domain-instructions.jsonl 保留定位，不放宽
  数值 NaN/内部标记限制来凑完整率。
- 90 个相关 targets 构建退出 0；190/190 回归通过 468.57 秒，
  related-build.log / related-tests.log，与选择清单
  /tmp/abcd-literal-binary-selected.json 按名称核对一致。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及
  定期审视仍暂停。本节日志前缀 /tmp/abcd-literal-binary-*；Goal 保持 active。

### 2026-10-02：phased 词法读取作为成员调用参数

- ProgressBar call 35 / property 29 的下一处阻塞确认为
  PhaseCorridors::Expression 不接纳 ValueLexicalLoad。新增共同
  phase_reference_test 的 kLexical/kLexicalGuard：在 callee 获取后读取
  已初始化 cell 作为参数，保留原 guard 和异常图。旧实现在 callee 必须
  inline 的断言失败，red-tests.log 1/1 RED 0.88 秒。
- 将 ValueLexicalLoad 接入既有 phased 表达式支持域，仍作为有序效果，
  不改变 CanFuse 唯一使用、连续效果区间、catch 快照和绑定检查。
  first-tests.log 1/1 通过 8.07 秒；新增读取后 cell 写入阻隔拒绝例及
  1/2/200/201/402 复制运输规模用例，barrier-tests.log 1/1 通过 8.51 秒。
- ProgressBar 9550 / entry 9915 完整输出通过 Node --check，保留两个原生
  async onClick 回调，无 legacy。独立配对复制按钮测试 246 个场景通过，
  paired-2.log：正常/拒绝/thenable、各效果点异常、读取参数中替换 callee、
  receiver、await 前后顺序及文件/资源参数。首次 harness 缺少注册阶段的
  SymbolGlyph 全局，paired.log 保留失败，补齐隔离组件后通过；未执行
  构造/storage、另一个粘贴进度回调、真实文件/剪贴板或平台渲染。
- 五输入 fresh build/stage-one/value-phased-cell-reference-pass-1 均退出 0，
  75 complete / 4 incomplete / 27 failure / 46 no-module，新增 ProgressBar。
  原 74 个完整项无状态回退、无 legacy、原生 default records 保留。
  pasteboard Home 5521、PageA 6897、PageB 7640、PasteBoard 8383 源码有变化：
  相邻词法读取直接组成成员访问/调用，删除多余快照及随之发生的局部编号变化；
  changed-source.diff 留存人工核对。其他完整源码字节未变。
- 89 个相关 targets 构建退出 0，188/188 回归通过 465.21 秒；
  related-build.log / related-tests.log。与选择清单
  /tmp/abcd-phased-cell-reference-selected.json 按名称核对一致。
  上述局部配对证据不代表 record 全部行为验收。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
  本节日志前缀 /tmp/abcd-phased-cell-reference-*；Goal 保持 active。
- 下一处定位 HotRankPage 48945 / entry 49171 的普通箭头 child 49640，
  parent 49226、创建 PC 171、family member 5、profile 1202。普通
  LiteralPlan 的 EarlyValue 报 kLateValue：buffered allocation op 17
  （literal 2939）之后，op 28 ValueBinary kAdd 产生 value 44，其已证明
  domain 为 Language/kString，但未纳入 LiteralExpression。原始函数含
  PC 64/72 的 ADD2。不能用此证据放行任意数值 ADD2；应保持值域证明并
  补原生二元初始化表达式的单次使用/效果顺序。next-hotrank.log、
  next-hotrank-literal.log、next-hotrank-cause-2.log、
  next-hotrank-instructions.jsonl 留存证据。


### 2026-10-02：phased 字面量中的已初始化 cell guard

- ProgressBar 9550 的 async child 10333 在字面量内使用词法 load 72，
  其 guard use 70 被独立归属检查视为额外观察。现将已通过共同 cell
  初始化证明的检查在 literal/corridor 规划前分类；phased 验证同样使用
  completed-store 和异常路径的初始化证据。LiteralPlan 独立重新验证 cells，
  只从用途和效果计数中排除已证明不会触发 hole 异常的检查；保留原图和来源。
- 新增三 profile 的真实 decoded 词法初始化/嵌套对象/try-catch 用例。
  夹具前两次分别因环境 catch 范围、缺少 ProveExceptionLocals 输入事实而
  过早失败，已修正；red-3-tests.log 才是目标 BuildPhasedEvaluationPlan
  断言 RED，1/1 失败 1.05 秒。第一版实现错误地在操作表分配前写 placement，
  first-cause.log 确认并已修正；second-tests.log 1/1 通过 9.92 秒。
- 扩展到 1/2/200/201/402 个属性、证明缺失/失效负例、异常 FunctionTree、
  独立 TreeSemantics 及统一 printer。focused-tests.log 4/4 通过 16.86 秒，
  含 phased_initialization_test、cell_tree_test、phased_literal_test 和
  literal_cell_guard_source_test。15 种独立 payload 检查身份、描述符、
  __proto__ 数据属性、跨调用分配隔离，以及不得访问 payload 的 Proxy。
- ProgressBar 已越过原 LiteralError::kObserved，但同一 family member 16
  随后在 BuildPhasedCallPlan 报 kReference，expression-cause.log /
  call-cause.log；record 仍不完整，不能计新增成功。下一步核对成员 Reference
  的原始获取点、参数表达式区间及 catch staging。
- 五输入 fresh build/stage-one/value-literal-cell-guard-pass-1 均退出 0，
  74 complete / 4 incomplete / 28 failure / 46 no-module；原 74 个完整项
  无回退或源码字节变化，无 legacy，原生 default records 保留。
  final-batch-summary.json 留存逐输入证据；本轮尚无新增完整 record。
- ProgressBar 后续具体为 member call 35 的 property 29，phased block
  58 到 70；参数 op 31 是 depth 2 / slot 0 的 ValueLexicalLoad，op 32
  已分类 kCellGuard，但 op 31 仍为 kRoot/kMultipleUses，property 29 为
  kRoot/kCrossRegionValue。PhaseCorridors::Expression 尚不接纳 lexical
  load，需在现有单次使用、效果区间和异常快照约束下继续贯通，不能直接
  允许快照 callee 变成员调用。next-reference-2.log / next-call.log。
- 89 个相关 targets 构建退出 0；187/187 回归通过 463.46 秒，
  related-build.log / related-tests.log。与选择清单
  /tmp/abcd-literal-cell-guard-selected.json 逐项核对测试名称一致。
  本节日志前缀 /tmp/abcd-literal-cell-guard-*。全库、扩大语料、RSS/基准、DFX、
  调度器、代理及定期审视仍暂停；Goal 保持 active。


### 2026-10-02：条件值前导区域贯通 Home

- ControlDecision 增加有明确 predicate 续接的区域前导，follow-tree 索引查找
  共享目标谓词，保留原 CFG、控制边及单一 owner。独立控制验证与 cell scope
  遍历遵守同一边界，未复制 Home block 40 的模块读取、guard 和属性读取。
- 共同选择发现、求值规划、FunctionTree 和独立 TreeFlow/操作数校验接入
  闭合条件值前导、分支 PhiInput 后缀、相邻选择及对象字段组合。模块配对、
  成员 callee/receiver 获取顺序不变。任意语句型非线性前导仍未实现，不能
  将控制表示能力当作完整语句源码恢复能力。
- 最新 focused-build.log 构建退出 0；focused-tests.log 4/4 通过 43.90 秒：
  decision_prefix_test、control_plan_test、decision_prefix_source_test、
  home_decision_prefix_source_test。三 profile 的 1/2/200/201/402 组合覆盖
  映射缺失、交换条件臂、debugger 阻隔等拒绝例及 work 增长检查。
  原生表达式独立对照 8,640 场景；Home 配对 592 场景覆盖可空窗口信息、
  变化 getter、读取中替换 Tabs、receiver、资源身份、描述符及各效果点异常。
  不覆盖构造、storage、ForEach、完整渲染或真实平台。
- 五输入 fresh build/stage-one/value-decision-prefix-pass-2 均退出 0：
  74 complete / 4 incomplete / 28 failure / 46 no-module。新增 Home 23956，
  原 73 个完整项无回退或源码字节变化，无 legacy，原生 default records
  保留。final-batch-summary.json 留存逐输入证据；完整输出不代表 executable
  或整体配对验收通过。
- 最终 89 个相关 targets 构建退出 0，186/186 回归通过 479.86 秒；
  与 /tmp/abcd-decision-prefix-selected.json 按名称核对一致，无遗漏。
  构建及测试日志为 related-build.log / related-tests.log。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
- 新核对 Content 14411 / entry 14684 仍在 tree_stage PC 41 拒绝；子方法
  14712 原始 PC 38 的 opcode 10 之后是 PC 41 return，父方法 14656 在
  PC 87 创建它。next-target.log、next-functions.log 和
  next-child-instructions.jsonl 保留当前证据；需沿值域前提诊断，不能只为
  完整率取消内部值约束。本节日志前缀 /tmp/abcd-decision-prefix-*；其中
  定向构建和测试完整文件名为 final-focused-build.log / final-focused-tests.log。
  Goal 保持 active，阶段一语义和表示验收均未完成。
- 下一项 ProgressBar 9550 / entry 9915：async arrow 10333 由 10222 在
  PC 145 创建，family member 16。phased LiteralPlan 在 VerifyNesting
  literal_plan.cc:365 报 kObserved；op 72 是 ValueLexicalLoad，计划 nested
  use 72，但真实 use 70 属于 op 73 的 kGuardValue，仍为 live。需核对词法
  read/guard 配对及异常归属，不能把它按无效果额外用途忽略。证据为
  next-progressbar.log、progressbar-cause.log、progressbar-literal.log、
  progressbar-observed.log 和 progressbar-use-2.log。

### 2026-10-02：语句短路 decision 保留模块 read/guard 配对

- DetailVerticalView 44437 / entry 44592 的 getPaddingTop 在 RHS 中遇到
  kModuleGuard，旧 EmitDecision 只接受 kInline。新增真实 decoded AND/OR
  语句分支在旧实现构建成功后于 FunctionTree 构造失败，red-build.log /
  red-test.log。现保留 guard 的共享表达式 placement，TreeFlow 复用
  ModulePairs 的精确 read/guard ExprId 检查；没有删除 guard、新增语句或
  无条件执行 RHS，相位 staging/handler 接受域未变。
- 三 profile 覆盖 direct/property module reads、AND/OR 两种共享分支及
  1/2/200/201/402 次组合；缺失/删除 guard 和篡改 guard 表达式均拒绝。
  green-tests.log 2/2 通过 12.71 秒；最终定向 4/4 通过 15.17 秒，
  expanded-build.log / expanded-tests.log。独立原生语法共 7,248 个短路、
  getter/call顺序、receiver、异常、undefined和词法 TDZ 场景通过。
- 最终 88 个相关 targets 构建退出 0，related-build.log；183/183 相关
  回归通过 414.87 秒，related-tests.log，并按名称核对与选择清单一致。
  全库、扩大语料、RSS/基准、DFX、调度器、代理和定期审视仍保持暂停。
- vertical_decision_module_source_test 独立读取配对 TS，getPaddingTop
  362 个方法场景通过，paired-test.log；实际 SyntheticModule export 在
  state getter 中替换，核对 live binding、各读取点抛错、严格比较不触发
  coercion，以及两次返回资源对象/params数组的身份和描述符。未覆盖构造、
  storage或实际渲染，也不将Node词法TDZ对照当作Ark VM验证。
- 五输入 fresh build/stage-one/value-decision-module-pass-1 均退出 0：
  73 complete / 4 incomplete / 29 failure / 46 no-module；新增完整
  DetailVerticalView 44437。原 72 个完整项无回退或源码字节变化，无 legacy，
  原生 default records 保留，final-batch.log / final-batch-summary.json。
  新源码 Node --check 退出 0，阶段一整体语义/表示验收仍未完成。
- 下一处已定位 Immersive Home 23956 / entry 24314 的 child 24258，
  parent 24645、创建 PC 56、family member 2。ControlBuilder::Regions
  在 control_plan.cc:218 报 kCrossEntry：task first=40 / stop=42 再进入
  已 owned 的 cursor 40（covered=31、pending=10）。需核对区域和共享
  decision 的续接边界，不可跳过 ownership 检查来重复/遗漏执行路径。
  next-target.log / next-owner.log / next-control.log / next-instructions.jsonl。
  本节日志前缀 /tmp/abcd-decision-module-*，Goal 保持 active。

### 2026-10-02：闭包绑定按 creation 身份独立于 AST 构造次序

- TabContentView 42938 / entry 43110 的字面量依赖先构造 OpId 21，而旧
  next_function_ 游标取到 creation 10 / child 23，当前 owner 22。现从已验证
  有序唯一的 FunctionBinding 表按原 OpId 二分查找，保留 child 非自身 owner、
  capture映射及完整创建计数；没有删除检查或改变运行时函数创建顺序。
  每个函数族 F 次创建合计 O(F log(F+1)) 查找、常量额外空间。
- 新增文件支持的真实 Code 解码函数族（元数据是测试夹具，不是容器解析证据），
  普通闭包先交给消费回调，之后返回字面量内读/写闭包，分别读取两 cell 并
  共享写入。旧路径 1/1 RED 1.03 秒，FamilyTree kExpression，red-tests.log /
  red-cause.log。修复后小例成功；402 组的 1,207 成员耗尽独立准备接口默认
  64 MiB 累计额度，明确报 FamilyEnclosure kResourceLimit，large-detail.log。
  规模夹具显式使用 128 MiB，生产额度不变，三 profile 的 1/2/200/201/402
  组全部通过，402 的全族 work 小于 201 的三倍；sized-tests.log 1/1 通过
  5.34 秒。未执行 RSS 或宣称全进程内存达标。
- 原生 JS 独立对照共 1,100 个名称/length/身份、不同 capture、调用中抛错、
  返回或抛错后继续写共享 cell 的场景，另验证两次 factory 调用隔离。
  负例最初放在会重新绑定的 family 入口，被正常修正；现移至实际
  evaluation/expression 输入边界，乱序、重复 creation、自身 owner 均拒绝。
  最终定向 3/3 通过 9.28 秒，boundary-build.log / boundary-tests.log。
- 88 个相关 targets 构建退出 0，related-build.log；181/181 相关回归通过
  408.83 秒，related-tests.log，并按测试名称核对与选择清单一致。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
- tab_closure_schedule_source_test 独立读取配对 TS，100 个 TabContent
  普通 content/字面量 builder 回调场景通过，paired-test.log；延迟执行、
  改变 tabBarsInfo、改变执行顺序、alien receiver、资源对象、异常及 HotPoint
  子回调注册均对照。不覆盖 ViewPU 构造、storage、子视图执行或真实渲染。
- 五输入 fresh build/stage-one/value-closure-schedule-pass-1 均退出 0：
  72 complete / 4 incomplete / 30 failure / 46 no-module；新增完整
  TabContentView 42938。原 71 个完整项无回退或源码字节变化，无 legacy，
  原生 default records 保留，final-batch.log / final-batch-summary.json。
  新源码 Node --check 退出 0；这些证据仍不是阶段一整体配对验收。
- 下一处失败为 DetailVerticalView 44437 / entry 44592 的 getPaddingTop
  （method position 764、family member 0）。共享短路条件的第二块包含
  已配对 ValueGuard / kModuleGuard（OpId 17）；EmitDecision 在
  function_tree.cc:926 仅接受 kInline，报 FunctionTree kOwnership。
  需沿共同 decision 构造及独立 TreeFlow 处理原模块 read/guard 配对，不能
  删除 guard 或使短路 RHS 无条件执行。next-target.log / next-owner.log /
  next-cause.log 保留证据。
  本节日志前缀 /tmp/abcd-closure-schedule-*，Goal 保持 active。

### 2026-10-02：嵌套条件对象按真实父属性组成归属树

- DetailPage 25350 / entry 25577 的 child 26659 将条件值先写入内层 md，再
  写入外层 span；此前单对象 Domain 只折叠内层，外层成员调用仍被 CallPlan
  kReference 拒绝。nested_selection_test 构建成功后在两层真实 decoded
  用例退出失败，red-build.log / red-tests.log，1/1 RED 1.52 秒。
- 对各 staged 对象索引唯一外部 UseId，只把真实父属性 stored RHS 连接为
  归属边；父 allocation 先于子 allocation，一次正向解析根，前缀和分组并按
  原操作顺序归并定义。共同 Domain 对每个 allocation 单独维护效果区间，
  并逐对象保留 consumer、末定义、ObjectExpr、字段及 PC 来源。未新增 writer、
  wrapper、图改写或样本逻辑；每组仅由首选择遍历，复杂度见 algorithms.md。
- 三 profile 覆盖 1/2/7/32/64/128/200/201 层，以及 1/2/200/201/402 组
  独立对象树和单父对象下兄弟子对象。真实 V8 operand 限制保留；402 的既有
  evaluation work 小于 201 的三倍。额外返回共享 child、缺失 child 映射、
  伪造 consumer/末定义和效果阻隔拒绝。首轮 3/3 通过 33.87 秒，green-tests.log；
  扩展 3/3 通过 3.27 秒，expanded-tests.log；兄弟子对象 3/3 通过 7.64 秒，
  siblings-tests.log。独立原生语法共 4,200 个身份、原型、属性描述符、分支、
  getter 替换和异常场景通过。
- 最终 87 个相关 targets 构建退出 0，related-build.log；178/178 相关
  回归通过 400.50 秒，related-tests.log，并按名称核对无遗漏，包含兄弟子对象
  间的 debugger 拒绝。全库、扩大语料、RSS/基准、DFX、调度器及代理仍暂停。
- detail_nested_selection_source_test 独立读取配对 TS，两个 GridCol 回调共
  480 个场景通过，paired-test.log；验证条件 getter 中替换 GridCol 后，先前
  create callee/receiver 及后续新对象调用，保留 span 内属性顺序、资源对象与
  params 数组。未覆盖 ViewPU 构造、storage、子视图实例化和真实渲染。
- 五输入 fresh build/stage-one/value-staged-object-tree-pass-1 均退出 0：
  71 complete / 4 incomplete / 31 failure / 46 no-module；新增完整 DetailPage
  25350，原 70 个完整项无回退或源码字节变化，无 legacy，原生 default records
  保留，final-batch.log / final-batch-summary.json。原定旧输出目录已存在，
  首次脚本按保护断言停止，未覆盖旧产物，随后使用上述新目录完成导出。
  新源码 Node --check 退出 0；完整输出仍不等同整体配对/平台验收。
- 下一项定位到 TabContentView 42938 / entry 43110。表达式构造
  ValueFunctionCreation 报 kOwnership；实际 lowering OpId 21，却由
  next_function_ 游标取到 creation 10 / child 23，当前 owner 22，说明失败
  是 creation 不匹配而非 self owner。NestedTrees 在普通操作前构造字面量
  依赖，需按真实 creation 身份关联 FunctionBinding，不能依赖构造遍历次序，
  也不能删除 owner/capture 检查。next-expression.log / next-function-id.log。
  本节日志前缀 /tmp/abcd-nested-selection-*，Goal 保持 active。

### 2026-10-02：混合普通与条件属性的原生对象参数

- MultiCommunity CommentItemView 22807 / entry 22958 的 child 23400 在
  CallPlan 拒绝外层方法读取/调用组合。实际 allocation 68 有定义 76/126/133，
  只有中间属性含条件构造参数；ObjectRoute 错把“首属性必须来自选择”作为
  组合前提。mixed_literal_test 的真实 decoded 回归在旧路径构建成功后
  1/1 RED（1.03 秒），red-build.log / red-tests.log。
- 现在一次索引每个属性的选择归属和每个对象的首选择，按原定义顺序允许普通
  前缀、中间和后缀属性。只由首选择遍历对象，避免逐选择重复扫描；多个选择
  同属一个 RHS 仍不在该路线内。局部 LiteralPlan 已验证的 nested/initializer
  不重复计入外层事件，staged 外层定义保持独立事件。复用共同区间规划、
  FunctionTree、Printer 和独立检查，无样本分派、额外 writer 或生产源码输入。
- 三 profile 覆盖多对象和单对象内重复键两种维度的 1/2/200/201/402 次组合，
  含标量或嵌套对象的构造参数与成员 receiver；402 的现有 evaluation work
  小于 201 的三倍。共享对象额外返回、伪造末定义、debugger 效果阻隔均拒绝。
  green-tests.log 1/1 通过 10.46 秒；扩展后的 5/5 通过 39.36 秒，
  expanded-build.log / expanded-tests.log。新增 Proxy 后 1/1 通过 0.12 秒，
  proxy-tests.log；mixed_literal_source_test 共 1,440 个独立原生语法场景，
  对照 getter/构造/参数顺序、异常身份、callee 替换、new.target、Proxy trap、
  对象与数组身份、重复键和参数逃逸后的修改。没有执行 RSS 或基准。
- 最终 86 个相关 targets 构建退出 0，related-build.log；175/175 相关
  回归通过 395.69 秒，related-tests.log，并按测试名称核对与选择清单完全
  一致。全库测试、扩大语料、RSS/基准、DFX、调度器及代理仍保持暂停。
- comment_mixed_literal_source_test 独立读取配对 TS，共 400 个嵌套 Column
  回调场景通过，paired-test.log；包括构造中替换 Column，验证先前 margin
  callee/receiver 与后续新 Column 调用，保留资源对象/params 数组及属性描述符。
  不覆盖 ViewPU 构造、storage 生命周期、完整渲染或真实平台。
- 五输入 fresh build/stage-one/value-mixed-literal-pass-1 均退出 0：
  70 complete / 4 incomplete / 32 failure / 46 no-module。新增完整 record
  CommentItemView 22807；原 69 个完整项无回退或源码字节变化，无 legacy，
  原生 default records 保留，final-batch.log / final-batch-summary.json。
  新源码 Node --check 退出 0；manifest executable 仍为 false，完整输出及
  选定回调通过不代表阶段一整体配对语义/表示验收完成。
- 下一处真实失败是 MultiCommunity DetailPage 25350 / entry 25577 的
  child 26659（parent 25714、创建 PC 188，family member 8）。PC 23 方法
  读取到 PC 84 调用之间，PC 29/35 分配外/内对象，PC 66 条件值写入内层
  md，PC 73 把内层对象写入外层 span；CallPlan kReference 仍拒绝。
  需要继续组合两层 staged 对象的归属，不能简单放行原方法快照。
  detail-next.log / detail-cause.log / detail-owner.log / detail-instructions.jsonl。
  本节日志前缀 /tmp/abcd-mixed-literal-*，Goal 保持 active。

### 2026-10-02：嵌套选择的独立 phi 经空汇合块组合

- Immersive Header record 22712 / entry 23320 的 child 22872 在成员调用
  规划处拒绝：PC 38 读取方法，PC 152 调用，参数内层选择的 phi 经纯跳转
  join 转交外层 phi。旧选择发现只接纳共享同一 phi 的嵌套，导致方法保留为
  cross-region snapshot。新增 decoded 回归在旧实现构建成功后退出 134，
  red-build.log / red-test.log；新实现已通过该真实路径，未放宽 CallPlan。
- 共同 SelectionRegion 记录精确 forwarded UseId；单次活跃使用、完整 child
  选择及只含纯跳转的 join 才能组合。保持原 CFG、两层 phi 和入边身份，父
  覆盖计数只计一次转交；求值规划、共同 FunctionTree、边复制及独立操作数
  检查贯通，未增加专项 writer、运行时 helper 或配对源码生产输入。
  O(values + uses + control nodes + operations)，复用 postorder/worklist；
  算法与边界见 algorithms.md、source-tree.md。
- 三 profile 的 1/2/200/201/402 次组合通过共同 AST、TreeSemantics 和
  InstructionSources；错误 consumer、缺失 phi 映射及交换子条件两臂均拒绝。
  getter 在参数之后时保留参数快照；effectful join 保留原 if/debugger，提前
  读取方法的对应不能融合情形仍拒绝。首轮 5/5 定向测试通过 24.11 秒；扩展
  后 2/2 通过 14.86 秒，expanded-build.log / expanded-tests.log。
  forwarded_selection_source_test 对实际生成 JS 与独立原生条件语法比较
  3,960 个效果、异常身份、receiver、getter 替换及新数组身份场景。
- 85 个相关 targets 构建退出 0，related-build.log。相关回归在中断前已
  96 项通过；原进程句柄丢失且无存活构建/CTest 进程，按通过名称清单补跑
  剩余 76 项，全部通过，remaining-tests.log。两段记录去重核对覆盖全部
  172 个选择目标，无遗漏或失败；没有重新启动已通过部分，也未执行全库测试。
- 五输入 fresh build/stage-one/value-forwarded-selection-pass-1 均退出 0，
  69 complete / 4 incomplete / 33 failure / 46 no-module；原 69 个完整项无
  回退或源码字节变化，无 legacy，原生 default records 保留，final-batch.log
  / final-batch-summary.json。本次闭合真实子函数路径，未新增完整 record。
- Header 后续独立缺口已确认：child 23436，parent 23152，创建 PC 76；
  PC 16 ADD2、PC 19 RETURN 报 SemanticProof kInternalValue，
  header-next-owner.log / header-next-instructions.jsonl。独立诊断
  23655 --family-source-draft 仍非完整/可执行证明，不能据此执行或宣称配对通过。
- 下一项已定位 MultiCommunity CommentItemView record 22807 / entry 22958，
  child 23400（parent 23624、创建 PC 18）的 CallPlan kReference：PC 182
  方法读取到 PC 378 调用之间有混合普通/条件属性的对象参数；需检查 ObjectRoute
  的首字段/连续字段限制，以及条件值经构造和成员调用到属性 RHS 的归属。
  实际 allocation 68 的定义为 76/126/133，中间定义含条件值；
  comment-next.log / comment-reference.log / comment-instructions.jsonl，
  进一步的计划快照为 /tmp/abcd-mixed-literal-plan.log。
  整体语义和表示验收未完成，Goal 保持 active；全库、扩大语料、RSS/基准、
  DFX、调度器、代理及定期审视仍暂停。本节日志前缀 /tmp/abcd-forwarded-selection-*。

### 2026-10-02：原生字面量接纳单次全局名称读取

- 实际WidgetCard member728的nested NamedRead输入是allocation之后的
  GlobalRead/kName，共同AST已有GlobalNameExpr，但LiteralExpression种类
  判定尚未接纳，造成LiteralPlan kLateValue。新增真实decoded fixture在
  旧路径1/1 RED 1.03秒，red-tests.log；实现后1/1通过1.02秒，green-tests.log。
  现在只接纳零operand的kName，kVar继续拒绝；共同表达式、成员Reference、
  scope和operand检查复用，未新增writer、样本分派或配对源码生产输入。
- 单次消费、unexposed allocation、属性/operand效果顺序、catch dispatch及
  捕获快照要求不变。真实三profile decoded测试覆盖buffered/unbuffered、
  direct/member全局读取、200/201/402字段；402 verifier work小于201的
  三倍。共享读取、debugger阻隔、伪造nested witness、kVar和外部同名binding
  拒绝。phased测试增加同catch正例和不同dispatch拒绝，并经共同异常AST。
- 最终定向构建退出0，final-focused-build.log；4/4通过12.60秒，
  final-focused-tests-2.log。独立native syntax oracle包含1,280个全局/属性
  getter、binding替换、缺失名字、null、任意相关抛出、异常返回与对象identity
  场景。Node contextified external global getter可能把抛出替换为ReferenceError，
  此宿主限制在测试中显式说明，不将其当成实际Ark VM异常证明。
  最终85个相关targets构建退出0，related-build.log；171/171相关回归
  通过362.24秒，related-tests.log。未执行全库测试或RSS/基准。
- 独立widget_global_literal_source_test读取配对TS，生产导出仍ABC-only。
  两个WidgetCard record共176个选定回调场景，含FlexDirection getter中替换
  Flex、保持先前create callee/receiver，以及padding/onClick/branch/route
  效果与异常对照。不覆盖构造、storage生命周期、完整渲染或实际平台。
- 五输入fresh build/stage-one/value-literal-global-pass-1均退出0；
  69 complete / 4 incomplete / 33 failure / 46 no-module；新增7个完整record：
  CardItemView31525、CallComponent4631、ExamplePage7989、GetTokenPage8867、
  Index9604、WidgetCard12347（modules）及564（widgets）。原62完整项无
  回退或源码字节变化，无legacy；native default records4466/20246/30597
  保留，final-batch.log / final-batch-summary.json。7个新.mjs均Node --check
  退出0，new-source-syntax.log。完整输出不是整个record的配对语义验收。
- 下一步沿其余33个failure/4个incomplete的真实共同路径继续；保留已记录
  TestAbilityPage模板/ADD2转换差异、ModuleStore prefix、NaN和SuperSpread
  未证明边界。fresh Immersive CommonView19072 / entry19740在
  FamilyReader读取真实child19852（parent19712，创建PC72）时报告
  SemanticProof kInternalValue/PC41；实际PC38为ADD2，PC41为RETURN，
  common-view-reader.log / common-view-target.log / common-view-instructions.json。
  尚需定位其具体值域原因，不假定NaN canonicalization或放宽内部值返回。
  阶段一整体语义及表示验收未完成，Goal保持active；全库、
  扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
  本节日志前缀/tmp/abcd-literal-global-*。

### 2026-10-02：类根捕获贯通完整写入、实际父作用域与共同打印

- ConstructedFamily/PreparedFamily拥有外部root_capture，区别于intra-family
  incoming边，不保留creator/context/parent tree指针。完整FamilyWrites重新
  核对实际原始创建点、初始化、父子revision与source aliases，保留所有
  后代Language stores、barriers和模块初始化要求；不是immutable snapshot。
- 最终父TreeStructure在实际ClassExpr活跃作用域解析每个prepared root的
  qualified capture，并检查全族global/module/宿主辅助绑定的外层遮蔽。
  复用active-symbol和sorted-spelling索引，child module targets一次索引，
  不逐read扫描aliases或逐member重放父图；复杂度见algorithms.md。
  共同printer传入实际ExprId/syntax，独立捕获类仍不能脱离父目的地打印。
  construction-only capture view不能代替final enclosure；该回归旧路径
  1/1 RED 1.03秒，final-destination-red-tests.log，收紧后3/3通过5.07秒，
  final-destination-green-tests.log。
- 实际decoded fixture三profile、prototype/static覆盖constructor、getter、
  setter及class-buffer→真实后代转发。完整writes与目的地各自先RED，
  root-writes-rebind-tests.log最终1/1通过1.25秒；最终父scope拒绝sibling
  block的同名cell、stale revision及缺失外部目的地。独立native class oracle
  96个共享可变cell、分离factory、receiver/property写入抛出后的状态及
  函数/实例身份场景通过。没有伪造FunctionExpr或样本定制生产逻辑。
- 相关84 targets构建退出0，related-build.log；167/167相关回归通过
  357.95秒，related-tests.log。新配对测试注册后abc_structure和
  class_accessor_tree_test构建退出0，final-destination-green-build.log；
  上述3/3包含class_root_captures_source_test，独立读取配对TS，仅执行
  三个新record的选定UI回调，共288个primitive参数/host效果受控场景。
  不覆盖ViewPU构造、属性生命周期、完整渲染或实际应用平台行为。
  最终目的地收紧后84 targets重新构建退出0，final-related-build.log；
  168/168相关回归通过361.29秒，final-related-tests.log。未执行全库测试。
- 五输入fresh build/stage-one/value-class-root-capture-pass-1均退出0；
  62 complete / 4 incomplete / 40 failure / 46 no-module，新增完整
  CommentBarView21042、CommentInputView22215、TestAbilityPage11277。
  原59个完整项无回退或源码字节变化，无legacy，native default records
  4466/20246/30597保留，final-batch.log / final-batch-summary.json。
  三个新.mjs均Node --check退出0；source_complete仍不是整体配对验收。
  最终目的地收紧后重新五输入value-class-root-capture-pass-2均退出0，
  原62个完整项无回退或字节变化，final-destination-batch-summary.json；
  三个新record的该版本再次Node --check退出0。
- TestAbilityPage配对模板插值的自定义Symbol.toPrimitive对象收到string
  hint，生成的ABC ADD2加法收到default hint。独立反例明确保留不等价；
  不以primitive域通过代替整个语义域。固定运行时f04900cf的RuntimeAdd2
  调用ToPrimitive无preference，js_tagged_value.h默认NO_PREFERENCE，
  支持当前ABC加法语义；尚无实际Ark VM执行证据。不能为模板风格修改
  已确定的ABC行为，阶段一整体语义/表示验收仍未完成。
- fresh WidgetCard record564 / entry1118的真实defined method1419族在
  member1（method728，creator1419，PC24）EvaluationPlan报告Unsupported。
  LLDB进一步定位到LiteralPlan kLateValue：nested NamedRead op15的
  输入来自allocation之后的GlobalRead op12，literal_plan.cc:752；
  widget-location.log / widget-member.log / widget-literal-location.log。
  没有视为可打印完整record，下一步沿共同literal表达式及独立顺序检查
  处理该实际阻断，并继续其余40个failure/4个incomplete。
  不放宽未暴露prefix的ModuleStore或未证明的NaN/SuperSpread语义要求。
  Goal保持active；全库、扩大语料、RSS/基准、DFX、调度器、代理及定期
  审视仍暂停。本节日志前缀/tmp/abcd-class-root-capture-*，另有
  /tmp/abcd-class-root-writes-*及/tmp/abcd-class-root-enclosure-*。

### 2026-10-02：条件臂原生字面量贯通 phi 输入与成员调用

- PhiExpressionProducer复用fresh LiteralPlan，同块kExpression对象/数组的
  allocation→最后definition一次索引；唯一phi使用只排除实际own-definition
  base uses。初始化整体仍在原始选择臂内，独立suffix效果、staged定义和
  额外观察继续拒绝。正常求值及独立checker使用该索引，conditional发现
  核对完整初始化范围并等待最后definition；没有.call/Reflect.apply、IIFE、
  新writer、样本ID分派或配对源码输入。索引O(I+D)，无逐phi全表扫描。
- 实际decoded buffered两臂对象、nested empty array/late getter在旧实现上
  1/1 RED，red-tests.log；基本实现1/1通过8.23秒，green-tests.log。扩展到
  三profile、200/201/402连续选择、初始化后debugger及伪造PhiInput拒绝，
  含独立TreeSemantics和实际InstructionSources。logical_regions.mjs新增
  receiver、getters替换方法、任意相关抛出、own descriptors及重复调用的新
  对象/数组身份对照；相关最终运行记录28,024个场景通过。
- 相关84 targets构建退出0，related-build.log；166/166相关回归通过361.92秒，
  related-tests.log。五输入fresh build/stage-one/value-conditional-literal-pass-1
  均退出0，59 complete / 4 incomplete / 43 failure / 46 no-module；新增
  MultiCommunityApplication records27518/37896/38925，原56个完整项无回退、
  字节变化或legacy，native default records4466/20246/30597保留，
  final-batch.log / final-batch-summary.json。
- 新完整产物DetailTitleView、HotPointPage、HotTitleView均通过Node --check。
  独立读取配对.ts之后新增ui_conditional_literals_source_test；注册后重新
  构建abc_structure退出0，paired-build.log；1/1通过2.08秒，paired-tests.log。
  144个受控场景对照initialRender注册及Blank.height、Column.padding、
  Text字体/透明度相关回调参数、getter/receiver顺序和异常。仅该测试读取
  配对源码，生产导出仍ABC-only。此证据不覆盖ViewPU构造、属性生命周期、
  完整渲染或应用平台行为；整体语义及表示验收仍未完成。
- fresh CommentBarView entry21219仍unproved/tree_stage，comment-bar.jsonl；
  LLDB确认initialRender21274的实际函数族22个成员均完成共同构建，member2
  method21436已越过旧CallPlan kReference，随后在root的FamilyWrite kCapture
  拒绝，actual-members.log。不能将这些conditional AST计为可打印完整record。
- 下一步继续闭合实际parent ClassExpr捕获可见性、external-root transitive
  writes及拥有的enclosure/printing证据。阶段一Goal保持active；全库、扩大
  语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。本节日志均在
  /tmp/abcd-conditional-literal-*。

### 2026-10-02：类根捕获接入真实 enclosing AST 和共同 source aliases

- ClassCaptureAnchor通过实际parent TreeFlow定位ClassExpr，核对constructor
  的完整创建元数据、buffer member的不可变ClassSiteId/property位置，以及
  defined member的实际method operation/kind/placement。共同alias builder和
  独立capture-tree检查复用父cell到symbol映射，复制名称和来源，保留qualified
  CapturedSymbol；没有伪造FunctionExpr、局部存储或替代正文。
- RecordTree每个record准备一次实际parent flow；ClassTree/PreparedFamily借用
  parent AST及真实creator接通root构建。default projection和后代需求传播后，
  共享原始创建点resolver重新绑定并独立核对root cells，拒绝过期revision。
  enclosing ClassExpr作用域可见性和完整family writes尚未闭合；write入口仍
  明确拒绝external root，没有获得PreparedFamily或可执行打印许可。
- 未实现anchor时实际decoded alias正例1/1 RED，red-tests.log；最终定向4/4
  通过2.59秒，focused-tests.log。三profile/prototype/static覆盖constructor、
  getter、buffer零访问转发及真实后代；改target/name/anchor/member occurrence
  拒绝，ExternalFamilyCapture完整AST构建后仍在FamilyWrite kCapture拒绝。
- 相关84 targets构建退出0，related-build.log；166/166相关回归通过348.68秒，
  related-tests.log。五输入fresh build/stage-one/value-class-capture-alias-pass-1
  均退出0，56 complete / 4 incomplete / 46 failure / 46 no-module；无新增完整
  项、回退、既有完整源码字节变化或legacy，native default records
  4466/20246/30597保留，final-batch.log / final-batch-summary.json。
- fresh CommentBarView entry21219仍unproved/tree_stage。LLDB实际看到root
  initialRender21274的AST包含aliasSymbol1→owner0/symbol0，首个后代21548
  也构建成功；下一后代arrow21436在ExpressionForest的UnsupportedOperation
  失败。它的真实creator21274、创建PC56、source first_owner14；不是旧root
  Context/OuterCapture拒绝。source-location.log / expression-error.log。
- 下一步定位21436的具体expression/call失败，并继续闭合parent ClassExpr
  捕获可见性、transitive writes和目的地打印。阶段一语义与表示验收未完成，
  Goal保持active；全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视
  仍暂停。本节日志均在 /tmp/abcd-class-capture-alias-*。

### 2026-10-02：类根函数族在真实creator处绑定完整捕获需求

- ReadFunctionFamily在完整发现及PlanFamilyCells之后，接受借用的实际normal
  root_creator；通过原始PC的二分查询定位创建点。constructor/buffer使用
  真实ClassCaptureSite，defined member重新BindCreation并核对不可变owner
  occurrence。所有root incoming cells复用共同binder，只有ParentCell结果
  可保留；未提供creator、错误creator、未初始化和未解决outward仍拒绝。
- FileFamily.root_capture拥有身份、revision和稀疏cell证明，不是伪造的
  intra-family FunctionExpr边，也不保留creator/context指针。生产constructor、
  buffer和defined-member准备入口均已接入实际creator。standalone tree/write
  入口仍拒绝仅有external cell证据的输入，直到接通真正的enclosing AST、
  source aliases、作用域和写入闭合；没有授权闭包源码直接打印。
- 新decoded reader要求在忽略creator的旧路径上1/1 RED，red-tests.log；
  实现后3/3定向通过1.51秒，green-tests.log。最终补充错误creator拒绝后，
  相关84 targets构建退出0，related-build.log；166/166相关定向回归通过
  348.65秒，related-tests.log。三profile/prototype/static案例覆盖实际发现
  的constructor、buffer转发count0和defined getter，并独立VerifyCaptureCells。
- 五输入fresh build/stage-one/value-class-root-reader-pass-1均退出0；
  56 complete / 4 incomplete / 46 failure / 46 no-module；没有新增完整项、
  回退、源码字节变化或legacy，native default record4466/20246/30597保留，
  final-batch.log / final-batch-summary.json。未执行全库测试。
- fresh CommentBarView record21042 / entry21219仍unproved/tree_stage，
  comment-bar.jsonl。LLDB确认拒绝点已到FamilyTrees::Build的external-root
  AST边界：root_capture存在，parent21219/child21274，原始PC264/site101、
  working creation101，唯一child cell1映射ParentCell，first_owner14；调用栈
  来自真实class operation40 / definition102，capture-location.log。不是
  Reader的kOuterCapture，也没有新增完整record或伪造正文。
- 下一步在实际parent ClassExpr中建立class-root source aliases，核对定义
  投影前后的捕获点、可见性和初始化；default residual之后重新绑定，随后
  独立闭合family writes、enclosure及打印目的地。阶段一语义与表示验收尚未
  完成，Goal保持active。全库、扩大语料、RSS/基准、DFX、调度器、代理及
  定期审视仍暂停。本节日志均在 /tmp/abcd-class-root-reader-*。

### 2026-10-02：真实类创建点复用共同 capture cell 绑定

- CaptureSite以variant区分原始CreationBinding和借用真实FunctionContext的
  ClassCaptureSite。constructor/buffer member在DEFINECLASSWITHBUFFER绑定；
  defined member保留实际DEFINEMETHOD，未伪造DEFINEFUNC。原始normal/Before
  scope核对完整ClassCreation、PC和environment，working binder核对真实class
  operation/backlink、creator、file/child identity及不可变owner stamp。
  CaptureCells只保留拥有的身份、revision和cell证据，不保留context引用。
- 既有initialized/domain、unexposed-prefix及独立original/work匹配未放宽。
  decoded三profile、prototype/static fixture覆盖constructor、buffer、defined
  getter和真实DEFINEFUNC后代；buffer自身没有lexical access，PlanFamilyCells
  产生count0转发cell，仍能绑定同一parent storage。未初始化、错environment、
  改PC/target拒绝；phase scope检查真实Before可用、Commit不可用，不声称整个
  phased class-containing body已恢复。
- 功能RED在未实现class matcher时class_accessor_tree_test拒绝，
  red-tests.log；最终定向3/3通过1.64秒，forwarded-tests.log。相关84 targets
  构建退出0，related-build.log；166/166相关定向回归通过345.18秒，
  related-tests.log。没有执行全库测试。
- 五输入fresh build/stage-one/value-class-capture-cell-pass-1均退出0；
  56 complete / 4 incomplete / 46 failure / 46 no-module，没有新增完整项、
  回退或legacy，原56个完整record源码字节不变，native default record
  4466/20246/30597保留，final-batch.log / final-batch-summary.json。
- fresh CommentBarView record21042 / entry21219仍unproved/tree_stage，
  comment-bar.jsonl。当前是原始/working cell绑定能力，Reader仍拒绝root
  incoming cells；下一步在真实creator处绑定class root，再连接parent AST
  cell symbol/source alias、作用域可见性及family write/publication证明。
  不将scope/cell测试计为完整类源码恢复。阶段一语义与表示验收尚未完成，
  Goal保持active。全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视
  仍暂停。本节日志均在 /tmp/abcd-class-capture-*。

### 2026-10-02：函数族 occurrence 索引与源码 owner 分离

- FileFamily/ConstructedFamily 的 FamilyId 继续以0为根索引；新增 caller-reserved
  first_owner，映射为 first_owner + occurrence。Reader 在追加前检查 uint32
  范围，tree/write 入口重新核对完整范围；普通及 phased capture evaluation、
  default 后的重新绑定、syntax 和 family root 构建均使用映射后的 owner。
  不把 method entity、property ordinal 或 FamilyId 当源码绑定身份。
- PreparedFamily 传递真实 destination namespace。BuildClassTree 按实际函数族
  occurrence 数为 constructor、buffer members、definition bodies 和 static
  initializer 分配不同范围；BuildRecordTree 先保留整个 record family 的范围，
  再逐类推进。uint64 cursor 表示耗尽后的尾位置，不允许 uint32 回绕。没有新增
  writer，也没有放宽 outer capture、初始化或 transitive-write 的检查。
- 最初两个非零 owner 用例在旧实现上2/2失败，family-owner-red-tests.log；实现后
  2/2通过2.08秒，green-tests.log。补充实际解码的200/201/400/800创建 occurrence、
  嵌套写入 capture 的 parent owner、async/generator class root、async arrow
  三成员函数族、最大编号/溢出和write输入owner错配；定向4/4通过3.65秒，
  boundary-tests.log。移动/借用合同保留，write核对使用重新读取的原始输入。
- 关联84 targets构建退出0，related-build.log；最新相关146/146通过256.03秒，
  related-tests.log。追加19项await/generator/async行为测试全部通过91.62秒，
  phase-tests.log；其中可执行文件已包含在上述最新构建中。新增非零owner的
  file_async_arguments_source_test已在定向4项内实际执行。没有执行全库测试。
- 五输入 fresh build/stage-one/value-family-owner-pass-1 顺序导出均退出0，
  56 complete / 4 incomplete / 46 failure / 46 no-module；没有新增完整项、回退
  或legacy，56个原完整record源码字节不变，native default record4466/20246/30597
  保留。final-batch.log / final-batch-summary.json。
- fresh CommentBarView record21042 / entry21219仍unproved/tree_stage，
  comment-bar.jsonl。LLDB在FamilyReader::BindEvaluations的原始拒绝点确认
  method21274已分配first_owner14；它的IncomingCell depth0/slot0具有count0，
  为子函数转发义务而非直接access。真实BuildClassTree operation40从owner2开始，
  PrepareDefinedMember definition102到达此处，root-capture-location.log。
  下一步将实际creator scope/cell、ClassExpr中的source alias和写入证明连接到
  这些class roots，包括constructor、buffer member和defined member；仅分配owner
  不代表外层capture已恢复。阶段一语义与表示验收尚未完成，Goal保持active。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
  本节日志均在 /tmp/abcd-family-owner-*。

### 2026-10-02：类布局先行，成员在真实 parent 树建立后准备

- 原 record 路径在 parent AST/符号尚不存在时，先把 class 成员作为独立
  rooted family 准备，无法接入实际外层 capture。新增 BuildClassLayout
  只解析真实 constructor context/callable、literal buffer occurrence、
  按 wire 顺序的定义 sites 和静态初始化调用；无 PreparedFamily 或假正文。
  class_tree_source_test 的布局先行要求先 RED，red-test.log；随后共同
  ClassExpr 可建立 parent 树，布局本身仍拒绝打印。
- 生产 BuildRecordTree 已先放入布局，建立真实 record AST/符号和完整
  native enclosure，再准备 constructor、buffer 成员、定义成员和静态
  字段。ClassExpr 指针不变，只有当前 builder 持有可变构建引用；失败
  丢弃未打印 record。完成后再次 VerifyTreeSemantics 核对实际 callable、
  原始调用、定义、heritage 与 body 对应，不将 provisional 检查当成正文
  证明。printer、默认导出分析及 family write barrier 拒绝未准备的类。
- 回归覆盖 IconInfo 的 ABC-only 布局 → parent AST → 实际成员准备 →
  配对 JS 执行；缺 constructor、缺 buffer member、错误 callable、缺
  descriptor half 均拒绝。三 profile 的 decoded 定义成员也先建立布局
  和 parent AST，再准备/打印正文，既有原始 operation/PC 与 family
  检查保留。首批8/8通过5.61秒，green-tests-2.log。
- 关联选择146项；首轮81 targets构建退出0，141通过/5未通过，250.54秒，
  final-tests.log。选择脚本漏建 class_writer_test、constructor_call_test、
  class_accessor_test，后两项的JS测试也依赖缺失可执行文件；已补建三个
  目标，实际关联构建覆盖84 targets。补建后的13项复验12通过/1失败，
  final-repair-tests.log；LLDB证实旧 class_accessor_test 的唯一失败断言
  在54行，仍期待 get ["value"]()，已存在的 printer 输出 get value()。
  更新这一过时表示断言，保留错误 kind 的拒绝及无输出检查。最终定向
  13/13通过2.56秒，final-focused-tests.log；本轮146项均有通过证据，
  不声称执行了全库或一次全绿146项命令。构建日志 final-build.log、
  final-repair-build.log、accessor-final-build.log，选择清单 selected.json。
- 五输入 fresh build/stage-one/value-class-layout-pass-1 顺序导出均退出0，
  56 complete / 4 incomplete / 46 failure / 46 no-module；无新增完整项、
  回退或legacy，56个原完整record的源码字节均未变化，三个native default
  record仍保留。final-batch.log / final-batch-summary.json。此次对原
  value-member-chain-pass-1 manifest 的重新统计亦为56/4/46/46，上一节
  56/3/47/46属于汇总错误，已更正；没有因本轮改变完成或失败分类。
- 真实 CommentBarView record21042 / entry21219仍以unproved/tree_stage
  拒绝，comment-bar.jsonl。本轮解决准备顺序，尚未绑定 class rooted
  family 的外层 incoming cell；下一步分离record与成员root的owner，
  在实际创建/定义位置绑定原始cell、source alias和transitive-write证明。
  不忽略 kOuterCapture、不伪造初始化值、不放宽原始ModuleStore escape
  或外层cell前提。阶段一完整语义/表示验收仍未完成，Goal保持active。
  全库、扩大语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停。
  本节日志均在 /tmp/abcd-class-layout-*。

### 2026-10-02：普通流的成员调用链与真实源码表示

- 普通 ProjectEvaluationUses 将原成员引用的 base 和显式 VM receiver
  当作两个源码观察者，使 SectionViewModel 等输出多级临时变量。最小
  decoded 链在无 `$value` 表示断言 RED，member-chain-red-tests.log；
  修复复用已存在的 IsMemberReceiver，只有原属性 base 与 receiver 相同
  才投影该重复协议使用。原始使用图不变，独立 evaluation forest 同步
  消费规则，原 CallPlan/TreeOperands/效果顺序检查不放宽。
- 三 profile、1/200/201/400 层链经完整 FunctionTree、TreeSemantics、
  原始 InstructionSources 和打印范围检查。额外真实使用保留快照；
  DEBUGGER 引用隔断和不同 receiver 分别保持 kReference/kReceiver
  拒绝。共同 printer 输出原生成员调用，不读取可变 function.call。
- 新 member_chain_source_test 执行15份完整 decoded 输出，111个独立
  getter/receiver/对象身份/状态/异常场景；getter 取得后替换属性仍使用
  已取得引用，3种合法语法的重复求值/错 receiver/错属性变异被检测。
  这不是完整应用或 Ark 宿主验收。
- viewmodel_batch_source_test 顺序导出真实 ABC 后对照配对 TS：六份
  record 行为通过，Immersive 四个 ViewModel 的六个方法另外要求原生
  成员/调用链且无方法内 `$value`。新增字段/方法/slice getter、接收者、
  参数强制转换、实时替换和精确异常身份场景。SectionViewModel29619
  已恢复为 this.model.getSectionProductList().slice(0, count) 的调用树，
  包括 printer 既有括号；不再拆成两级方法内临时变量。
- 关联75 targets 构建退出0，首轮127项为126通过/1失败：PageA 的
  wrong-receiver 文本变异仍定位旧临时绑定，替换无变化。原配对行为
  已通过，改为直接改变 getUIContext 成员调用 receiver，并先独立解析
  三个变异。PageA 单独通过0.58秒，pagea-tests.log；没有弱化观察义务。
  符号 opcode 整理后的定向3/3通过11.99秒，final-focused-tests.log。
  最终关联127/127通过203.01秒，final-tests.log，选择清单见selected.json。
- 五输入 fresh `build/stage-one/value-member-chain-pass-1` 顺序导出均
  退出0，仍为56 complete / 4 incomplete / 46 failure / 46 no-module，
  没有新增 complete、完整项回退或 legacy writer，native default 三项
  保留。29个原完整 record 的源码变化，相关临时声明967降至204；
  此计数是表示变化，不是这些 record 的全量语义验收。证据
  final-batch.log、final-batch-summary.json、source-changes.json。
  本节日志均位于 /tmp/abcd-member-chain-*。
- Goal仍active，阶段一完整配对语义与源码表示验收未完成。外部类词法
  捕获、数值域和 super spread 等已记录缺口仍在；不以减少临时量或
  组件验证替代整应用接受。全库、扩大语料、RSS/基准、DFX、调度器、
  代理及定期审视仍暂停，既有运行环境前提不变。

### 2026-10-02：共享条件分支与成员调用参数贯通

- 真实 WindowUtil 箭头7754 的 getter op14 在 block0，调用 op50 在
  block4；callee 被保留为 kCrossRegionValue。参数条件拥有共享 fallback
  与 ControlDecision，旧 selection 跳过 decision 根，仅留下不完整子
  selection，无法融合调用。最小同形 decoded 回归成功构建后在 CallPlan
  RED：/tmp/abcd-decision-selection-red-{build,test}.log；原始 getter/
  receiver/控制证据见 member-conditional-call-real-{detail,full}.log 与
  member-conditional-call-tree-control.log。
- selection 现在保留最大 decision 根的 flat 条件与路径，逐一检查原
  inline/guarded-module 所有权与原共享边极性。保留根、phi、独立入口
  或 DEBUGGER 路径不融合；normal/phase 边界不变。共同效果区间证明
  将完整短路条件作为有条件事件，callee/getter 仍先于参数，最后通过
  原 MemberCall/稳定 receiver 检查。输出共同 LogicalExpr/ConditionalExpr
  与原生成员调用，没有 `.call` 或样本专用 writer。
- 原分支既有 scalar 表达式不能代表完整 selection 已构造；新增 built
  索引区分两者。最初接通后实际 tree_structure 拒绝未归属条件表达式，
  修正后继续在 origin 对应拒绝。TreeFlow 分别恢复原 scalar 判断和
  最终选值，operand/origin 使用已核对 scalar，选值用途保留 conditional；
  独立检查原根 branch origin。没有放宽结构/来源义务。
- 三 profile、1/200/201/400 次 decoded 调用，经共同完整 FunctionTree、
  TreeSemantics、InstructionSources 和打印范围验证。条件 operator/顺序、
  选值 arm 和根来源变异拒绝；原测试将来源改为条件来源在新组合中会
  成为无变化，改为明确不同的原始来源，保留变异拒绝义务。
  已有定向单元通过7.18秒，expanded-test.log。
- 新 decision_selection_source_test 独立执行12份生成源码，1092个 getter
  顺序、truth conversion、实时状态、对象身份、receiver 和异常场景，
  包含 hostile coercion 与可变 function.call；三个合法语法 mutation 被
  检测。注册定向3/3通过3.94秒，oracle-registered-tests.log。这是完整
  decoded fixture 的独立 oracle，不是配对 WindowUtil 或真实 Ark/宿主验收。
- 实际 ReadFunctionFamily 构造器8339/箭头7754 的完整函数族现在通过
  BuildFileFamilyTrees；新增实际回归。初次测试试图复制 move-only family
  的构建错误已修正，不算生产语义 RED。真实 record 仍不完整：下一
  首次拒绝是 updateAvoidPadding8620 / class property0 的异常输入
  kInternalValue PC127 / node106 / value210，next-phase-detail.log。
  不能把 normal 初始化前提直接套到异常成功/失败通路。
- 五输入 fresh `build/stage-one/value-decision-selection-pass-1` 顺序导出
  全退出0，仍为56 complete / 3 incomplete / 47 failure / 46 no-module，
  无新增 complete、原完整项回退或 legacy writer，native default 三个
  record 断言保留。证据 final-batch.log、final-batch-summary.json。
  内部 complete 不代替配对语义与表示 Accepted。
- 最终关联75 targets build退出0，125/125 tests通过241.88秒，范围见
  selected.json。上述日志均位于
  /tmp/abcd-decision-selection-*（原 getter/control 为 /tmp/abcd-member-*）。
  Goal仍active；完整阶段一配对语义/源码表示验收未完成。全库、扩大
  语料、RSS/基准、DFX、调度器、代理及定期审视仍暂停，super spread
  的运行环境前提不变。

### 2026-10-02：类创建与嵌套函数的模块初始化贯通

- 实际 WindowUtil record7618 / entry8202 在 PC45 创建类，其构造器8339
  PC39 创建箭头7754。slot0 已在入口 PC10 初始化，但构造器没有本槽
  store，旧直接父级规则在箭头 PC100/112 raw load（复制 PC102）拒绝。
  最小实际解码的类构造器 input admission 成功构建后 RED，
  /tmp/abcd-class-module-init-red-{build,test}.log。测试初次配置及 site
  强类型转换修正不算生产语义 RED。
- 正常 FunctionInput 新增 typed ModuleSlotSet 持久化前缀；已证明
  Language 的首次入口 store 按原指令排序，每次只复制槽宽路径。类缓冲
  member 保留真实 ClassCreation 操作数，构造器、定义成员及嵌套函数按
  原始可达创建点共享父级初始化集合。同一 file/record/creator/PC/完整
  allocation tuple 必须匹配，分支或未来 store 不提供该依据。
  初始化 membership 不包含常量、对象身份或非 undefined 精化；读取
  仍是原生实时模块绑定。已有 guard 对及 normal/phase 边界不变。
- 完整函数族独立检查继承前缀根身份及原始 read/store/creation，类根
  即使没有 raw read 也必须提交实际外部创建边。Missing/substituted
  root edge 不能发布；原始全部 store 的 Language 域仍由共同函数族
  关闭。FileFamily 外部 parent 引用只借用至本次 tree/write 检查，
  PreparedFamily 不保留借用。词法捕获的 closed-root 约束不变。
- 新增实际类/箭头 owner 链测试、1/200/201/400 raw read、错创建/错
  prefix/branch-late store 反例及透明根发布变异。持久化集合核对
  1/200/201/400/800、最大65535槽、重复插入零分配、耗尽后不变及恢复，
  两线程独立扩展共享根并由另一线程释放。实际 allocate_shared rebind
  分配计入创建方预算；allocator 不保留 stack quota。二分和固定槽宽
  路径计入 work budget；无 read 的继承边也验证精确/不足预算。
- 新增 class_module_initialized_read_source_test，经实际 decoded slice
  →共同 FunctionTree/printer→Node ESM 执行，9种值核对 live update/
  Object.is/arity，两种合法语法 mutation 被检测。它是独立切片 oracle，
  不是完整 WindowUtil 配对语义/风格或 Ark/宿主验收。扩展及根发布的
  定向3/3通过1.49秒，/tmp/abcd-class-module-init-publication-tests.log。
- 最终关联73 targets build退出0，121/121 tests通过238.45秒；
  /tmp/abcd-class-module-init-final-verified-{build,tests}.log 与 selected.json。
  扩大范围首次120/121通过，旧 class_method_test 仍期待 computed key
  `["method"]`，未改的现行 writer 已输出 native `method`。同步5处表示
  断言后单独通过；原 Node 行为对照也通过。首次失败证据保留于
  /tmp/abcd-class-module-init-expanded-first-tests.log，不算本轮生产 RED。
- 预算修正后的五输入 fresh `build/stage-one/value-class-module-init-pass-2`
  顺序导出均退出0，56 complete / 3 incomplete / 47 failure / 46 no-module，
  无新增 complete、原完整项回退或 legacy writer；native default 三个
  record 断言保留。证据 /tmp/abcd-class-module-init-final-batch.log、
  final-batch-summary.json。complete 仅表示内部恢复闭合，不能代替整应用
  配对语义与源码表示 Accepted。
- WindowUtil 已越过初始化输入失败，仍为 tree_stage/unproved、无 source
  complete。真实箭头7754 的下一阻点是共同 CallPlan kReference，
  /tmp/abcd-class-module-init-window-cause.log（VerifyCalls call_plan.cc140）。
  后续应核对该调用的原 receiver、callee 快照及条件参数求值次序，不能
  以 `.call` 或放宽 intrinsic/receiver 前提替代原生表示证明。
  Goal仍active；全库、扩大语料、RSS/基准、DFX、调度器、代理和定期审视
  仍暂停，super spread 的运行环境前提未改变，阶段一完整验收未完成。

### 2026-10-02：同函数入口模块初始化与条件更新

- WindowUtil record7618 / entry8202 原先在 PC10 写入 local slot0 的真实
  undefined，PC19 读取后于 PC21 被 kInternalValue 拒绝。同函数入口 store
  未形成模块读取依据。最小实际解码测试成功构建后 RED，
  /tmp/abcd-module-entry-init-red-{build,test}.log。
- 正常 SemanticProof 新增 kStored 及原始 store InstructionId。排序索引保留
  每槽首个入口 store，仅授权后续可达本地 load；完整 SSA operand 检查
  仍要求首个和所有后续 store 都为 Language，拒绝 Hole、循环自证、其他
  slot、外部读取、未来/分支 store。读取保留实时绑定及宽 Language 域，
  不转发初值、对象身份或非 undefined 精化。已有编码 guard 对保留；
  异常/恢复通路不使用 normal 见证。原始/工作 store 的 slot、PC、effects、
  entry block/顺序、live 状态和 operand 在共同 module_guards 中复核。
- 真实入口随后在 record kInitializer15 拒绝，最小 decoded record 同样
  RED：/tmp/abcd-module-entry-init-record-red-{build,test}.log。record_tree
  仍由根控制序列放置 native let；首次声明于入口块的槽可以在 arm/loop
  中输出原绑定赋值。条件首次声明和先 arm 写入后根声明仍拒绝，未提升
  store 或合成提前初始化；非入口支配位置的声明仍待扩展。
- 三 profile、窄/宽 slot、1/200/201/400、guard/raw 共48个输入/源码对应
  fixture，另保留实际 undefined 正例、7个不安全初始化反例及 witness
  slot/life/order/block/PC/domain 变异拒绝。扩展反例初次缺少 dead tail
  的终结指令而在错误阶段失败；修正 fixture 后保留 kInternalValue 断言，
  不算生产语义 RED。/tmp/abcd-module-entry-init-undefined-{build,test}.log。
- 新增 module_entry_initialization_source_test：三 profile、0/1 两种原始
  分支，6次实际解码→共同 FunctionTree→printer→ESM 执行；核对 namespace
  TDZ、最终值、唯一根声明与 arm 赋值，12个合法语法 mutation 被检测。
  6次条件首次声明/未来根声明拒绝。原定向3/3通过2.46秒：
  /tmp/abcd-module-entry-init-oracle-tests.log。此为独立 fixture oracle，
  不是配对 WindowUtil 或真实 Ark/宿主验收。
- 最终相关66 targets build退出0，112/112 tests通过237.10秒；
  /tmp/abcd-module-entry-init-final-verified-{build,tests}.log，selected.json
  保存范围。补充 undefined fixture 的 module_guards_test 重建并通过1.55秒。
  自查完整 Language 检查、第一 store 选择、正常/相位隔离、原生声明与
  assignment 放置，以及 enum 的所有消费者；新增成本见 algorithms.md。
- 五输入 fresh `build/stage-one/value-module-entry-init-pass-1` 顺序导出均
  退出0，56 complete / 3 incomplete / 47 failure / 46 no-module，无新增
  complete、无原完整项回退、无 legacy writer。证据
  /tmp/abcd-module-entry-init-final-batch.log、final-batch-summary.json。
  complete 仍仅表示内部恢复闭合，不代表整应用语义/表示 Accepted。
- Goal仍active。WindowUtil 已越过入口 PC21 和 record 初始化拒绝；新
  manifest 保留 tree_stage / unproved PC102。实际失败是 WindowUtil8339
  构造器 PC39 创建的箭头7754，在 PC100/112 无 guard 读取 slot0 后被
  拒绝；不是新的 RegionPlan 失败。构造器没有该槽的入口 store，现有
  ParentModuleInput 只承认直接父级 FunctionCreation/entry-store，尚未
  传递 record→class→nested function 的初始化依据。下一步必须沿真实
  class/creation owner 链建立并独立验证依据，不能按模块名或完整 metadata
  直接宣布所有槽已初始化。证据 /tmp/abcd-module-entry-init-next-method-final.log、
  next-instructions.jsonl、next-parent-instructions.jsonl。
  全库、扩大语料、RSS/基准、DFX、调度器、代理和定期审视仍暂停，
  super spread 的运行环境前提未改变；阶段一完整语义与风格验收仍未完成。

### 2026-10-02：正常流不可达指令与 SSA/源码贯通

- Community BreakpointType record19234 / getValue19360 在 PC88 已返回，仍有
  PC89 LDUNDEFINED / PC90 RETURNUNDEFINED 死尾。原 SemanticSource 对每个
  物理指令要求 SSA write，错误以 kSsaMismatch 拒绝。最小真实解码回归在
  成功构建后 RED：/tmp/abcd-reachable-ssa-red-{build,test}.log。
  共同验证现在沿用原 EnvironmentFlow.before 的正常 entry-reachable 域，
  逐指令保留 PC 对应；活 write 与不可达 read/write 仍严格检查。
- 顺次接通原 argument snapshot ABI、normal importer、独立 input
  correspondence、rewrite origins 和 instruction sources。GETUNMAPPEDARGS
  死槽仍核对编码/frame/effects；不制造 SSA。物理 OpId/PC 不变，工作 CFG
  只纳入可达块/边，死槽以 Removed 保留在前一活块的存储区间；正常边、
  phi 定义及 incoming 同时按原始到工作块映射。既有 handler/resume 拒绝
  边界不变，不将异常图套入本次 normal reachability。
- 独立 verifier 的块映射 scratch 使用原始块数，而非压缩后的块数；修复
  真实方法接线时的 memory_limit。rewrite 只为新增的活操作删除要求纯消除，
  原已 Removed 的死槽保持 Retained。共同来源清单区分 UnreachableInstruction
  和 ElidedInstruction / recovery gap；死槽保留 OriginalSite，生成范围为空，
  不以 fake anchor/语句完成 coverage。经历 importer、rewrite 和 coverage 的
  真实阶段失败后贯通，不是绕开校验或将失败标记改成 complete。
- 三 profile、1/200/201/400 死尾及 argument snapshots，共24个 analysis
  fixture；另3个中间死前驱/phi join fixture验证原块4→工作块3、原块3的
  incoming→工作块2，保留两条实际 incoming。死槽激活、origin 伪装、错 PC、
  活 write 缺失、snapshot 范围、phi/边/存储区间及 scratch 下界负例均拒绝。
  15个真实解码→共同 FunctionTree→printer/位置 fixture核对死槽原 PC、
  无语法位置、无 INCOMPLETE；伪位置、改 PC、漏活位置与假纯消除拒绝。
- 新增配对 BreakpointType 独立 oracle：引擎只消费 ABC 和 entry19387。
  360个严格比较、动态常量 getter、实际 Proxy receiver、字段读取、状态修改、
  任意 Symbol throw 和截止轨迹场景；另验 ESM live import replacement、
  参数/方法签名、字段 descriptor/顺序及构造器 prototype setter/异常截止。
  3个合法语法 mutation 可被检测。参考只按此样本 assignment-field 前提
  擦除3个未初始化 TS 类型字段，避免Node stripper引入 native field define；
  不推广为所有 Ark profile。证据 /tmp/abcd-reachable-ssa-paired-test.log。
  这不是Ark VM/真实宿主差分；整record类carrier、快照别名和表示仍未Accepted。
- 最终64个相关targets build退出0，108/108 tests通过234.90秒，
  /tmp/abcd-reachable-ssa-final-verified-{build,tests}.log；选择清单
  /tmp/abcd-reachable-ssa-selected.json。作者核验稳定物理OpId、块与phi remap、
  死槽无effects/SSA/语法事件及活路径拒绝边界；算法新增成本见algorithms.md。
  不是全库/RSS/性能验证，不恢复代理、DFX、scheduler或定期审视。
- 五输入 fresh `build/stage-one/value-reachable-ssa-pass-1` 顺序导出均退出0，
  56 complete / 3 incomplete / 47 failure / 46 no-module，新增19234，
  无原完整项回退、无legacy writer。/tmp/abcd-reachable-ssa-final-batch.log
  和 final-batch-summary.json。complete仅表示当前恢复闭合，不证明整应用
  语义/风格通过；no-module分类亦不作为阶段一完成的依据。
- Goal仍active。WindowUtil record7618 / entry8202 复核仍 tree_stage / unproved
  PC21：/tmp/abcd-reachable-ssa-window-next.json。继续真实阻断和共同类/字段/
  绑定表示；super spread 运行环境前提未改变。全库、扩大语料、RSS/基准、
  DFX、调度器和代理仍暂停。

### 2026-10-02：原生匿名默认导出表达式

- 最小实际解码匿名默认模块在构建成功后 RED，因输出仍是合成 let +
  export alias；/tmp/abcd-native-default-red-{build,test}.log。共同路径现可
  在原首次 store 的位置生成 `export default expression;`，保留原 AST、
  初始化顺序及语句/表达式 origin ranges。不是替换生成文本或另建 writer。
- record adapter 用原 wire 哨兵的已验证 synthesized evidence 和唯一公开
  default alias 选择候选。打印器再次检查 typed StmtId / ModuleSymbol，
  不猜源码名字。不删除仍有读、写、更新、capture、其他公开 alias、
  重复声明或不完整 descendant 的绑定。遍历 ordinary family、类构造器、
  成员、definition families 和静态初始化。直接 FunctionExpr / ClassExpr
  初值保留，因为 default NamedEvaluation 仍需单独名字证明。
- 三 profile 的实际解码 scalar/array/late-array/repeated-store/named-default
  ESM 执行共15例、3个合法语法 mutation；TDZ、最终值、native array realm
  及 own property descriptor 与初始化位置分别核验。新增类 fixture 仅改变
  构造器是否读原默认槽：24次独立执行及3个合法语法 mutation，依赖变更后
  核对构造返回的原对象身份，错误替换为 live import 可被检出。
  历史36次 alias/self-reader执行与6个mutation继续通过。
- 初次新手工 AST unit fixture 缺 module_scope，打印前结构检查正确拒绝；
  补齐 fixture 后正负例通过，不计作生产语义失败。新增检查覆盖 root/child
  identity、owner/access/evidence、reads/updates/captures、partial、invalid IDs、
  finite memory/work；family root 放在最后，子树同号语句不获得 export syntax。
  200/201/400 root工作量608/611/1208，wide family 5420/5453/12352；这是
  bounded counters，不是RSS或整应用性能证明。
- Pasteboard/Community Logger 和 CardArray 的原行为 oracle 继续通过，
  并独立断言原生 `export default (new (...))` 和移除无引用的匿名 let。
  各240及24个mock-host场景的原限制不变；不是Ark VM/真实host differential。
  类 carrier、字段表示、snapshot aliases 仍阻止整record风格验收。
- 自查新增一个 runtime target 冲突负例，成功构建后 RED 于 eligibility：
  同一个本地 symbol 不能兼作另一个模块槽的 alias。检查现在明确拒绝；
  /tmp/abcd-native-default-alias-red-{build,test}.log。原已验证 record inventory
  不会产生这种形态；此约束同时保护直接 printer output-context 调用。
- 最终相关54 targets构建退出0，97/97 tests通过225.76秒；
  /tmp/abcd-native-default-final-verified-{build,tests}.log；选择清单
  /tmp/abcd-native-default-selected.json。全库、扩大语料、RSS/基准、DFX、
  scheduler和代理仍暂停。作者核验typed selection、保守transitive lifetime、
  PMR admission、iterative ownership和source ranges；未恢复定期审查。
- 五输入在 fresh `build/stage-one/value-native-default-pass-2` 顺序导出均退出0，
  55 complete / 3 incomplete / 48 failure / 46 no-module，无新增完整项，
  无完整项回退、无legacy writer。记录4466、20246、30597均使用原生默认
  new表达式导出；这是表示改进，complete计数不变。证据
  /tmp/abcd-native-default-final-batch.log、final-batch-summary.json。
- Goal仍active。这轮只有独立默认导出表示投影通过，不构成整应用语义或
  源码风格Accepted。继续类/字段/绑定表示和剩余真实阻断；super spread的
  运行环境前提仍未改变。

### 2026-10-02：匿名默认导出的共同模块绑定

- Logger record4466 的实际断点显示 local `*default*` / exported `default`，
  原 record_tree 把内部哨兵当源码 identifier 而以 kExport 拒绝。
  最小实际解码回归成功编译后同样失败，
  /tmp/abcd-anonymous-export-red-build.log、red-test.log。
  原目录的 frontend HEAD 与 release lock 一致，为
  c754cf6300873cdd8cdb71c7340a98abfd885389；其 DEFAULT_LOCAL_NAME 定义
  和 default-export emitter 已现场核对，不是配对源码给引擎的答案。
- 共同 ModuleBindingNames 边界保留 adjacent-equal VM 槽分组，非相邻同名
  不合并。完整 import/local 元数据决定不会冲突的合成名；record 声明、
  normal/phased 子函数读取使用同一映射，TDZ guard 仍先检查原始 wire 名。
  普通名保留 Metadata，匿名名明确 Synthesized；record 和 destination
  检查实际树的 owner/slot/name/evidence/access 与显式声明前提。
  未放宽初始化、只读、遮蔽、未知非法名和 callable 规则；无新 writer/最终IR。
- 新增三 profile 完整 decoded record、嵌套自引用函数、别名/live import、
  用户名冲突，以及 normal/phased 读取和证据/guard/slot/access 反例。
  独立 ESM oracle 36 次执行、6 个合法语法 mutation 通过。
  名称实体去重解码；长名称被200/201/400个别名引用的 RED 工作量
  81182/81989/322182，GREEN 为2212/2222/4212。
  提交前自检再将两个不同实体的同一长名称交替使用，RED 为
  42526/43545/164838；增加文本等价类后最终为3001/3621/5713。
  /tmp/abcd-anonymous-name-equal-entity-red-build.log、red-test.log。
  不仅实体解码去重，行分组也必须常数比较等价类。
  单次适配复杂度和既有 per-function reader 边界见 algorithms.md 与
  module-binding-names.md；没有全文件缓存、RSS 或基准证明。
- 首轮扩大回归93/94，module_scope_test 对被现有规则拒绝的 `$arg0`
  元数据参数强取 ConstructedFunction。断点确定在该旧断言，HEAD 的
  参数准入条件相同。保留 kName 拒绝，并用合法名验证实际遮蔽；没有
  改生产参数规则。最终53个相关目标 build退出0，94/94通过221.47秒，
  /tmp/abcd-anonymous-export-final-verified-build.log、final-verified-tests.log，
  selected.json保存清单。不是全库测试。
- 真实 Pasteboard Logger、MultiCommunity Logger、CardArrayViewModel 首次
  完整导出。配对源码仅在独立测试读取，引擎只有ABC及实体选择。
  两个Logger各240个hilog mock场景、各2个合法语法mutation；CardArray
  24个mock场景和1个mutation，包括0/1/2/200/201/400输入、箭头receiver、
  构造参数/异常身份/截止、fresh数组/元素、单例与具名类导出。
  Logger参考先按样本assignment-field前提消除两个未初始化TS类型声明，
  避免Node类型擦除保留native字段导致不同键顺序；不是所有Ark profile的
  字段规则。Community输入hash以当前文件和已有corpus index复核，修正了
  初次oracle误用Immersive hash的配置错误，该错误不算语义RED。
- 新增两项配对测试注册后，最终相关7/7通过4.55秒：
  /tmp/abcd-anonymous-export-final-paired-tests.log。
  以上不是Ark VM或真实hilog差分；整record的类carrier、快照别名和discarded
  expressions仍未通过源码风格验收，不能把504个mock场景算整应用Accepted。
- 五输入最新顺序导出全部退出0，55 complete / 3 incomplete / 48 failure /
  46 no-module，较上一轮新增3个complete（20246、30597、4466），无原完整项
  回退、无legacy writer。build/stage-one/value-anonymous-exports-pass-2；
  /tmp/abcd-anonymous-export-final-batch.log、final-batch-summary.json。
  complete只表示结构/函数族无已知恢复缺口，不表示阶段一语义/风格完成。
- Goal仍active。继续共同类/字段/绑定表示及剩余真实阻断；派生super spread
  的目标运行环境选择仍待明确，不移除其语义保护。全库、扩大语料、RSS/基准、
  DFX、调度器和代理仍暂停。作者已检查映射、guard-before-rename、证据及
  生命周期和容量增长；没有恢复代理审查或定期审视。

### 2026-10-02：共享 decision 中保留 RHS 条件求值

- 三 profile 的实际解码条件调用夹具，在成功构建后 RED 于原
  EmitDecision 的 kOwnership，与真实9193 Less115的阻断相同。
  /tmp/abcd-decision-snapshot-red-build.log、red-test.log、red-cause.log。
  测试最初的 MethodBody 字段拼写编译错误不计作语义 RED。
- 在共同 FunctionTree 的每个后续 predicate 内，用已有 SequenceExpr 保留
  原始单入口线性路径上的 expression roots 和快照 assignment，然后汇入
  原有 LogicalExpr。RHS 仍条件执行，每个 root 只执行一次且原顺序不变；
  快照只在 operand 正常完成后更新，不将 throwing child 融入无 dispatch
  的 phase branch。普通 normal 路线维持原有纯表达式准入。
- builder 和独立 TreeFlow 对照原始 protected/handler scope、实际 sequence
  顺序、快照目标/声明/一次 assignment、原 PC/origin、原始 normal edge；
  catch Stage 对空序列独立消费，非空仍拒绝。debugger、环境/声明和不支持
  的 root shape 不变成逗号语句；没有新最终IR、writer、发布许可或平台。
  原有 origin verifier 对无独立 statement 的快照避免解引用空 statement；
  实际 assignment origin 已在独立 RootExpression 中严格核对。
- 容量按 C/O/R（control nodes/operations/RHS roots）一次预留，保留 byte/
  strong-ID 准入；额外 O(C+O+R) 工作/空间，避免逐 root reserve 引起反复
  复制。algorithms.md 记录算法和边界，无新增依赖/许可证变化。
- 96份完整解码函数覆盖三profile、call/Less、2/200/201/400谓词、两种极性、
  protected/unprotected；完整 draft work 检查200→201及201→400增长。
  独立 Node oracle 已通过5688个场景及3个合法语法mutation：原生receiver/
  零参数、调用次数/顺序、number-hint coercion一次、任意throw身份、catch
  返回值、typed VM branch（不是通用JS truthiness）、BigInt及Symbol比较。
  typed tree反例核对outer短路、逗号倒序、user参数目标和错误wrapper origin。
- 额外拒绝夹具含6个debugger场景，以及6个catch-carried状态场景；后者具有
  selected-arm phi copies，先在control preflight以kControl拒绝，不能宣称
  它验证了新RHS staging guard已触发。跨 protected 尝试也在更早控制阶段
  拒绝，未将它报告为恢复特性。普通未知快照不能因此被移出短路。
- 真实 pasteboard record8383（parent9277）首次 abc_semantic 退出0，
  source_complete=true，没有 __cxa_throw；/tmp/abcd-decision-snapshot-real-next.log。
  LLDB 随后对已退出进程执行 bt 的命令失败不是引擎失败。此证据不是配对
  应用的语义/风格 Accepted，scratch comma和其他结构表达仍需独立验收。
- 最终相关48个构建目标退出0，85/85项定向回归通过218.10秒；
  /tmp/abcd-decision-snapshot-final-verified-build.log、final-verified-tests.log，
  selected.json保存实际测试/构建清单。不是全库回归。
- 五输入顺序导出全部退出0，52 complete / 3 incomplete / 51 failure /
  46 no-module；pasteboard record8383新增完整源码，原完整项零回退，
  legacy writer为0。build/stage-one/value-decision-snapshots-pass-1，
  /tmp/abcd-decision-snapshot-final-batch.log、final-batch-summary.json。
  这是完整结构/函数族导出进展，尚未提供该record的配对行为/风格验收。
- 剩余 EntryAbility record4767（entry5001）的首个真实 throw 是
  ForestBuilder 显式拒绝 ValueSuperSpread，op9，normal路线/derived constructor。
  /tmp/abcd-decision-snapshot-next-entryability.log、next-entryability-cause.log。
  下一步贯通 native super spread 与派生 receiver/构造语义，并继续配对验收；
  Goal仍active。
  全库、扩大语料、RSS/基准、DFX、调度器和代理仍暂停，内存实测未验证。

### 2026-10-02：共同源码树的原生 numeric update

- 实际解码对象返回 old/updated 两值的用例，在成功编译并修正测试编码后，
  RED 于 ForestBuilder::Unary 的 ToNumeric kUnsupportedOperation，与真实9165
  相同。证据 /tmp/abcd-numeric-red-build.log、red-test.log、red-cause.log。
  初次测试编码/元数据错误不计作语义 RED。
- 原有非 NaN Number 事实或非 NaN 数字 immediate 允许 ToNumeric 保持原值，
  保留一次输入求值、signed zero、infinity 与 CombinedOrigin；builder 和
  operand verifier 都核对事实。Language、对象、字符串、BigInt、NaN 不提供
  该身份转换证明，不能从后续 increment 或变量名推断。
- ValueUnary INC/DEC 汇入共同 UpdateExpr/SequenceExpr 与 printer：一次保存
  input 到专有 UpdateBinding，再对私有存储执行 native prefix ++/--。
  不覆盖仍可观察的旧 SSA 值，不用 x+1、函数包装或 legacy writer。
  TreeFlow 从实际 assignment/update targets 推导所有权，一次引用扫描拒绝
  scratch 外泄、复用、用户绑定及求值倒置；operand/origin 与 ABI 独立核对
  输入、direction、新结果、PC 和参数读写。原导入值域准入不变。
- 新增绑定 O(O) 工作/O(O+U) 存储，仅有 update 时分配 OpId 表；已有预算
  包含符号、名称、节点。额外 tree 检查 O(O+S+X) 工作/O(S) scratch。
  algorithms.md 与 numeric-updates/to-numeric 参考说明更新，无新增最终IR、
  通用证明包装、发布许可、依赖或报告平台。
- 三 profile 的 normal/phased 实际解码夹具覆盖1/2/200/201/400次增减，
  完整 draft work 的201→400增长受检；old/new独立、回调receiver/2参数、
  catch保存值、负零、int32溢出和正负infinity。typed tree负例包括改方向、
  prefix改postfix、写parameter、scratch读出、交换sequence及成对复用scratch。
  旧 partial build/file 测试改用仍未恢复的通用 ToNumeric，继续验证缺口和
  family不完整传播，不继续将已支持的整数 increment 当作必然拒绝。
- 独立 Node oracle 执行72份真实解码完整函数及4份syntax-only AST，
  992场景及3个合法语法mutation通过；前者检查调用和异常身份、结果及对象
  descriptor，后者单独核对native Number/BigInt、post/pre和一次number-hint
  coercion/任意抛出值。后者不是任意值域ABC支持、Ark差分或配对应用 Accepted。
- 当前源码中的scratch comma是正确的原生增减表示；尚未证明可折叠成原源码
  引用的 x++，不能算整应用源码风格验收。通用转换/NaN值域仍是未完成工作。
- 最终相关构建退出0，84/84定向回归通过176.20秒，包括old partial、ABI、
  flow/structure、控制/异常、捕获/class、async/generator以及Home/PageA完整
  record。首轮83项通过，但partial_tree_test因未构建而Not Run；修正构建
  目标清单、补充成对scratch复用反例后，以上完整84项重跑通过。
  /tmp/abcd-numeric-final-verified-build.log、final-verified-tests.log；
  /tmp/abcd-numeric-selected.json保存实际测试/构建目标，不是全库回归。
- 五输入 build/stage-one/value-numeric-update-pass-1 顺序导出均退出0，保持
  51 complete / 3 incomplete / 52 failure / 46 no-module，无完整项回退、
  新增完整record或 legacy writer。
  /tmp/abcd-numeric-final-batch.log、final-batch-summary.json。
- 真实9165在 TreeBuilder::Build 返回点 recovery_issues为空，已完成 completion
  和树语义检查，/tmp/abcd-numeric-real-9165-built.log；不是独立配对语义/风格
  验收。首个后续 __cxa_throw 属于9193的 EmitDecision：branch op108 原始
  PC279/工作PC276，RHS op115是 Less，原始PC298/工作PC295，Root、
  CrossRegionValue，原本49条语句变50条触发kOwnership。
  /tmp/abcd-numeric-real-decision.log、decision-statement.log、decision-op.log。
- 下一步在共同decision/求值/树流中保留短路RHS快照的条件执行，不能将语句
  移到短路外或删除ownership检查；继续完整函数族及配对验收。Goal仍active。
  全库、扩大语料、RSS/基准、DFX、调度器和代理仍暂停，内存实测未验证。

### 2026-10-02：无异常出口的数组初始化与原始 try 归属

- 三 profile 的实际解码 own-indexed array 初始化新增正例，成功编译后
  RED 于 VerifyNesting::SameCatch 的 kObserved，与9165的 allocation92
  相同。初始夹具错用了普通 indexed store，被 lift 拒绝；改为已有
  STOWNBYINDEX 后才确认上述语义 RED。
  /tmp/abcd-array-catch-red-fixed-build.log、red-fixed-test.log、
  red-fixed-cause.log。没有为测试新增普通 store 兼容入口。
- 非 throwing allocation 保留 creation/identity 顺序，使用首个真实
  throwing initializer 核对后续 read、TDZ guard、call 和 definition 的
  dispatch。当前这类区间必须没有 catch-copy staging；否则会在 native
  初始化表达式执行之后才写快照，故继续拒绝。原 throwing allocation
  的 SameCatch/staging 合同不变，没有给非 throwing 操作伪造 handler。
- 已有 TreeFlow 验证复用 TreeStructure 实际 expression_scopes 和原始
  try/body/handler 对应关系；一次 memoize 最近 CatchScope，对照 allocation、
  definition、nested operand 和配对 guard 的原始 instruction scope。
  因而也检查 literal 被后续 call 吸收的实际执行位置，不假定它仍在
  allocation 处。不能扩大 protected range 或将 getter/call 移出原 scope。
  新增 O(S+R+I+F) 工作、O(S) 临时空间；catch anchor 增加 O(I) 工作和
  O(1) 状态，复用已有预算，没有新 IR、报告、依赖、writer 或证明包装。
- 三 profile 的数组覆盖2/200/201/400元素的完整树构建、独立流验证和
  printer，201→400完整 draft work 增长受检。构造器需要十进制属性键
  constant；初始夹具漏了键映射导致 kExpression，修正测试边界后才 GREEN。
  六个跨 protected 边界反例在树语义阶段拒绝，六个需要 catch staging
  的反例仍在 evaluation 阶段拒绝。原模块 guard、slot、definition、
  nested ownership、PC 和 exact-work 检查继续执行。
- 12对象和12数组完整函数的独立 lexical TDZ/live-binding oracle通过
  1776场景及6个合法语法 mutation，其中3个只修改数组输出。核对初次和
  后续TDZ、初始化期间替换绑定、getter/member receiver与顺序/次数、
  任意抛出值、catch保存值身份、fresh identity、数组prototype/长度和
  dense own-data descriptor。不是实际应用整体语义/风格 Accepted。
- 最终相关构建退出0，79/79定向测试通过199.08秒，
  /tmp/abcd-array-catch-final-build.log、final-fixture-build.log、
  verified-tests.log。覆盖既有模块、literal、异常、控制、捕获/class、
  async/generator RETURN循环及 Home/PageA 完整record。全库、扩大语料、
  RSS/基准、DFX、调度器、代理仍暂停，内存实测未验证。
- 五输入 build/stage-one/value-array-catch-pass-1 顺序导出均退出0，保持
  51 complete / 3 incomplete / 52 failure / 46 no-module，无完整项回退、
  新增完整record或 legacy writer。
  /tmp/abcd-array-catch-final-batch.log、final-batch-summary.json。
- 真实9165（creator9082、creationPC145、family member11）已越过旧数组
  literal planning 的 kObserved；下一处首次 __cxa_throw 是 ForestBuilder
  的 ValueUnary::kToNumeric op299，原始PC840、工作PC751，输入Value244、
  结果245，kUserCode/kMayThrow；ops300/301是Copy，302是Increment，303
  是Copy。证据 /tmp/abcd-array-catch-real-unary.log、
  next-update-sequence.log。此失败发生在 TreeFlow 之前，不能把它称为
  真实9165的完整 try-scope 源码或整函数验收通过。
- 下一步沿共同 evaluation/tree/operator/printer 恢复原生 numeric update，
  保留 Number/BigInt、转换/求值次数、post/pre结果、存储与异常顺序，
  不用 x+1 替代；之后继续完整函数族和配对语义/表示验收。Goal仍active。

### 2026-10-02：模块 TDZ 配对贯通相位对象初始化

- 新的真实解码对象初始化夹具在成功编译后 RED：与函数9165一样，
  BuildLiterals 在模块读取作为 named-read 的晚输入时抛出 kLateValue。
  /tmp/abcd-module-literal-red-build.log、red-test.log、red-cause.log。
  新目标首次需要刷新 CMake 配置，未把找不到构建目标计作语义 RED。
- phased LiteralPlan builder 与独立 verifier 接收已经存在且计费的
  IndexModuleEvaluations 对称 peer 表；两条路线各自新建/核对索引。
  共用普通字面量的唯一消费者、operand/property 顺序及 guard-use 投影。
  模块 read 与 TDZ guard 都独立比较外层 allocation 的 catch dispatch 和
  staging 来源，不能只比较 read。保留原始 CFG、两次 throwing 事件、
  slot/record 绑定证明及两个 PC 的 CombinedOrigin，没有改 writer。
- 原有索引只作为 span 传递，新增校验 O(I) 加原始 catch-copy 量，沿用
  字节/工作预算和已有 corridor 索引，无逐字段 CFG 搜索或新依赖。
  详见 algorithms.md。三个 profile 的 getter/member-call 对象夹具覆盖
  2/200/201/400 字段和201→400工作增长；缺 guard、改 slot、移除 guard、
  丢 nested 归属、交换定义及改变 guard handler 均拒绝。独立 literal
  verifier 另检验缺索引、guard dispatch、恰好预算及少一单位的拒绝。
- 共同 phased evaluation/tree/family/printer 输出12份完整函数；独立
  lexical TDZ/live-binding oracle 通过888场景和3个合法语法 mutation。
  包含第一/第二读取 TDZ、getter 内和成员调用内替换后续绑定、receiver、
  求值顺序/次数、任意调用点抛出的值、catch 保存值身份、fresh object、
  key 顺序与 own-data descriptor。不是配对应用的整体语义/风格 Accepted。
- 相关构建退出0，79/79定向测试通过197.84秒；补充独立验证器负例后再
  构建退出0并通过2/2（6.69秒）。证据
  /tmp/abcd-module-literal-verified-build.log、verified-tests.log、
  final-extra-build.log、final-extra-tests.log。全库、扩大语料、RSS/基准、
  DFX、调度器和代理仍暂停，内存实测未验证。
- 五输入 build/stage-one/value-module-literals-pass-1 顺序导出均退出0，
  仍为51 complete / 3 incomplete / 52 failure / 46 no-module；无完整项
  回退、新增完整record或 legacy writer。
  /tmp/abcd-module-literal-final-batch.log、final-batch-summary.json。
- 真实9165已越过旧 kLateValue；直接 __cxa_throw 确认下一处 kObserved
  来自 VerifyNesting::SameCatch：外层 allocation op92 是空数组
  ValueArrayAllocation（原始PC254、工作PC228、kImpossible），没有
  exceptional exit；其 nested module read op94（原始PC258、工作PC232）
  有 handler PC905、region0/handler0，且双方均无 throw-copy bundle。
  /tmp/abcd-module-literal-real-next.log、real-catch.log、real-dispatch.log。
  先前调试前把 allocation 口头称为 buffered object，现以实际类型纠正；
  并非已证明 catch 快照不同，也不是完整9165源码验收。
- 下一步为无 throwing exit 的数组 allocation 建立初始化表达式的原始
  try 归属与异常现场合同，然后继续完整函数族及独立语义/表示验收。
  不删除 SameCatch 或凭空赋予 handler。阶段一 Goal active、尚未完成。


### 2026-10-02：跨线性相位路径的共享条件与末端分支贯通

- 投影图新增回归在成功编译后 RED：旧 skeleton 重复接纳共享 arm，
  ControlError::kCrossEntry；真实解码的调用条件夹具也在控制阶段 RED。
  /tmp/abcd-phase-decision-red-build.log、red-test.log、red-source.log。
  后者仍有条件快照缺口，不能把控制推进称为调用条件源码支持。
- ControlDecision 增加可选末端 predicate，逆拓扑摘要跨唯一入口的线性
  Before/Commit 或普通 jump 路径。每个原节点与弧仍独立拥有；条件求值路径的共享前驱、
  独立 root、synthetic 及跨 scope 不可吸收。末端非共享尾段作为真实 fallback
  保留，用两侧实际 LCA 选择汇合，不能使用忽略私有 abrupt arm 的 follow
  选择替代。摘要/归属 O(B+E)，候选 LCA 至多 O(B log B)，新增 O(B) 空间。
- acyclic verifier 复用已有独立 ControlRegions 原弧/ownership 检查，不重跑
  发现。FunctionTree、TreeFlow 和 cell lifetime 共用该候选路径，显式核对
  PhaseControlUnit/Commit 谓词、实际边复制、空 staging、条件操作归属和 PC。
  输出已有 LogicalExpr/IfStmt/shared printer，共享正文一次发射，fallback
  不丢弃；没有改原 CFG、扩 try 范围、恢复旧 writer 或新增最终 IR。
- 无独立 throw dispatch 的分支只接纳零效果子树；builder 与 phase verifier
  均检查该边界。需要 snapshot 的任意调用保持 kOwnership 拒绝，三 profile、
  两种极性的六个反例防止提前执行。该缺口仍待条件性效果/快照表示，不能
  放松 SameCatch、复制调用或用外部源码补齐。
- 投影图覆盖1/200/201/400路径、两侧四种极性、201→400 work增长、额外root
  和合法ID的跳过路径/错predicate反例。真实解码普通/相位夹具各24份完整
  函数，含2/200/201/400比较和真实jump锚点；独立 oracle各768场景+72个
  mutation，核对严格比较不强制转换、shared/after顺序与次数、receiver、
  任意调用点异常和payload身份。树中OR→AND及比较取反的合法语法变体
  被独立流/语义检查拒绝。相位夹具不含 native await/platform 验收。
- 扩展曾暴露原 abrupt shared-arm 回归，修正两侧 LCA 后保留原 getter
  短路行为。旧 OptionalAccess(true) 控制拒绝更新为实际接受，共同树保留
  一个 debugger；完整源码独立12场景+1个改属性mutation，检查nullish、
  getter receiver/次数、对象与异常身份。此 debugger 保留检查是结构证据，
  没有伪称执行调试器暂停的宿主验收。
- 最终相关构建退出0，64/64定向测试通过172.52秒；
  /tmp/abcd-phase-decision-verified-build.log、verified-tests.log。包含既有
  shared getter/selection、普通/phased控制、cell/capture、class/derived、
  async/generator RETURN循环及Home/PageA完整record回归。未恢复全库、
  扩大语料、RSS/基准、DFX、调度器或代理；内存实测仍未验证。
- 最终五输入 build/stage-one/value-phase-decisions-pass-2 顺序导出均退出0，
  保持51 complete / 3 incomplete / 52 failure / 46 no-module，没有新增完整
  record，也没有完整项回退或legacy writer。证据
  /tmp/abcd-phase-decision-final-batch.log、final-batch-summary.json。
  阶段一语义/表示验收仍未闭合；no-module和部分输出不是完成。
- 直接 __cxa_throw 核对，旧控制归属失败已越过；现在首次失败在函数族
  member11的async arrow9165，creator9082、creation PC145，BuildLiterals
  kLateValue：allocation op92之后，NamedRead op110原始PC306（工作PC280）
  使用ModuleRead op106原始PC296（工作PC270）的结果。证据
  /tmp/abcd-phase-decision-real-next.log、real-owner.log、real-final.log、
  next-literal.log。不是9193完整源码或真实应用 Accepted。
- 下一步在相位字面量与共同模块求值合同中恢复该有序模块依赖，保留TDZ
  guard、catch快照和读取次数；之后继续完整函数族及独立语义/表示验收。
  阶段一 Goal active且未完成。

### 2026-10-01：循环共享退出段及嵌套提前转移的共同恢复

- 纠正上一任务的调试结论：在 catch 入口读取未赋值局部量误报
  kInvalidGraph。直接在 __cxa_throw 核对类型、值和调用栈，确认 method9193
  （MethodPosition140）实际先触发 kLoopExits；真实退出目标为412/348，
  348有来自循环内339/345的两个前驱，358也有内部汇合。证据
  /tmp/abcd-control-9193-types.log、abcd-control-real-targets2.log 与
  abcd-control-9193-graph.json；这些调试快照不作为完整源码验收。
- ExtendLoopExitCorridors 改为全部原始前驱到达同一 owner 后接纳共享段。
  外部前驱、独立入口、混合 owner、其他循环及普通零边端口仍阻断归属扩展；
  保留正常/typed preserving 续接，原始边不变。一次 O(V+E+L) 队列与计数，
  新增 O(V) 临时索引，沿用现有 quota，不新增 work 准入或通用证明包装。
  共同 DAG 的 follow tree 在一条私有 abrupt 路径与普通路径之间选择普通
  续接，使嵌套 continue/return 的普通路径可以汇合；这不是声称该续接
  postdominate 已离开区域的路径。已有独立 arc/owner verifier仍逐边核对。
- 新投影图用例在构建退出0后实际 RED：共享段4未归属循环，
  /tmp/abcd-shared-exit-red-build.log 与 red-tests.log。覆盖1/200/201/400/800
  段长、几何 work增长、独立入口/额外外部前驱及不改原始邻接；合法 ID 的
  改后继、跳过共享段 mutation被独立控制验证拒绝。既有“共享内部 join
  必须拒绝”用例更新为成功，并仍执行原图验证，没有取消外部入口边界。
- 真实解码 fixture 经普通及相位 SSA、共同 control/evaluation/tree/printer
  分别生成完整源码。独立 oracle各通过 **1080场景和6个语法合法 mutation**，
  验证每个调用的 receiver、条件选择、continue/backedge、共享 side/break
  单次执行、after顺序、payload身份及任意调用点抛出的立即终止。
  /tmp/abcd-shared-exit-oracle.log；shared_loop_exit_source_test 与
  phased_shared_loop_exit_source_test。phased fixture不包含真实平台运行或
  native await wrapper验收，不能据此授予真实应用整体 Accepted。
- 两组相关构建退出0，**35/35通过74.99秒，22/22通过29.87秒**，共54个
  不同目标（三个新/更新目标重复验证）；/tmp/abcd-shared-exit-final-build.log、
  final-tests.log、extra-build.log、extra-tests.log。包含完整 RETURN清理循环、
  普通/phased组合、derived/class/literal/closure及 Home/PageA/Index回归。
  全库、扩大语料、RSS/基准、DFX和代理仍暂停，内存实测未验证。
- 五输入 build/stage-one/value-shared-exit-pass-1 均退出0，保持
  **51 complete / 3 incomplete / 52 failure / 46 no-module**，无完整record
  回退或 legacy writer；/tmp/abcd-shared-exit-batch-summary.json。
  本任务没有新增完整record，阶段一语义和表示验收仍未完成。
- method9193已越过多出口拒绝，当前真实失败为 **kCrossEntry**：共同
  skeleton重复接纳348，task(first348,stop358)，
  /tmp/abcd-shared-exit-next-cause.log。下一步恢复相位图中跨线性求值段的
  共享条件 arm，保留有条件执行的调用/效果、phi copies和原PC；不能直接
  移除 cross-entry 检查、复制共享正文或在 printer补结构。Goal继续active。

### 2026-10-01：DEFINE METHOD 异步成员贯通共同函数族

- CreationTreeContext 的 actual MethodBody 路线保留 kAsync → kAsyncMethod，
  ClassDefinedMember 同样保留该 callable；仍要求 DEFINE METHOD 的 accumulator
  home、同文件 occurrence/role/名称与 ABI。普通及 async body 复用已有函数族、
  await normalization、树和 printer，不新增 writer。非普通 get/set 定义不能
  直接采用 async 方法语法，维持明确 kKind 拒绝。
- 依据 pinned OH7 ObjectFactory::NewJSFunctionForDefineMethod：ASYNC_FUNCTION
  选择 AsyncFunction class 后设置实际 HomeObject。已核对本地上游 HEAD 为
  f04900cf951c66c2ea18b2bab5b591d5336c34b9，与 reference lock 一致。
- 三 profile 的实际序列化完整 class fixture 覆盖 prototype/static、四次
  getter/async method descriptor 替换、await/resume/rethrow/reject 原始包装、
  普通 object literal 与 class projection 隔离，以及不合法 async accessor
  拒绝。新 fixture 在构建退出0后实际 RED：MethodBody NONE-only kKind，
  /tmp/abcd-defined-async-red-fixed-build.log、red-test.log、red-cause.log。
  最初测试配线缺 abcd_json 导致编译失败，修正后才执行上述语义 RED。
- 独立整份 factory/class oracle 通过 **390 场景**：native AsyncFunction/Promise
  realm prototype、name/length/不可构造、own descriptor/fresh identity、
  严格 receiver、primitive/thenable、then getter/调用抛出与拒绝、deferred
  resume、resolve 后抛出、microtask 次序、两个 invocation 乱序恢复及 ambient
  call/apply/bind 毒化。移除 await、替换 receiver 的语法合法 mutation 被拒绝；
  defined_async_method_source_test。表示保留原生 async method/await，但仍有
  必要性待改善的临时量，不据此授予真实应用的整体风格 Accepted。
- 相关构建退出0，**40/40 定向回归通过54.80秒**，
  /tmp/abcd-defined-async-final-build.log 与 final-tests.log，包含原普通
  accessor/class、class-buffer async、creation/capture/derived、正常/phased
  literal、完整 Home/PageA/Index 及上一任务 receiver oracle。全库/RSS/基准/
  DFX/代理仍暂停；现有旧 writer 基线失败没有计作本任务通过。
- 五输入 build/stage-one/value-defined-async-pass-1 均退出0，保持
  **51 complete / 3 incomplete / 52 failure / 46 no-module**，没有 complete
  回退或 legacy writer，/tmp/abcd-defined-async-batch-summary.json。
- 真实 PasteBoard 已越过 entry8831/PC182 的 async method9468 拒绝；下一个
  是 parent9110/creation PC217 的 child9193，FamilyFileFailure 的
  PhasedMethodError::kControl，/tmp/abcd-defined-async-pasteboard-next.log。
  BuildExceptionControl 捕获原始 ControlError::kInvalidGraph，
  /tmp/abcd-defined-async-next-control-cause.log。下一步从真实 phase projection
  输入定位共同 control graph 的不变量/支持边界，不删除异常边或伪造 successor。
  PasteBoard 整 record 与阶段一语义/表示验收均未完成，Goal 继续 active。

### 2026-10-01：已证明 receiver 检查贯通原生字面量初始化

- normal LiteralPlan 接收既有 derived/captured receiver 的原始有序检查清单。
  独立 VerifyLiteralPlan 同样消费该清单，一次 O(O+G) 初始化索引供 use/interval
  census 使用；仅 RequireInitialized 能位于 native initializer 内。重复、乱序、
  非检查、越界及非 protocol 读拒绝；RequireUninitialized 不获该许可。
  receiver 来源仍由实际 family projection 独立闭合，不从 Language 域猜测，
  不改原始 op、分配或 guard。EvaluationBuild/Verify 传递同一证明，TreeFlow
  保留原 PC 的 AnchorId，未新增 writer 或放宽相位捕获。
- 三 profile 的真实序列化派生构造器 fixture 在 buffered object 内恢复
  callback 与 receiver.argument；1/200/201/400/800 guard/捕获读取及几何 work
  验证，逐检查锚点保留。新用例先实际 RED：LiteralError::kObserved，op15
  RequireInitialized；/tmp/abcd-literal-receiver-red-build.log、red-test.log 与
  red-cause.log。缺失 receiver proof 和错误重复 super 检查仍拒绝。
- 独立完整构造器 oracle 通过 **1890 场景**，每场两次构造，覆盖 getter
  单次/状态变更/抛出、父 constructor 普通/替换对象/primitive/null/throw、
  Proxy、setter 中调用新闭包、native own descriptor/prototype/fresh identity
  及跨调用 receiver 捕获；Object.prototype setter 和 call/apply/bind 毒化。
  重复 getter 与错误属性值的语法合法 mutation 均被拒绝，
  derived_literal_receiver_source_test，/tmp/abcd-literal-receiver-final-tests.log。
- 相关构建退出0，最终 **36/36 定向回归通过52.42秒**；上述 final-build/tests
  日志。补充锚点断言后重建并复跑 derived_cells_test 与新 oracle **2/2通过
  14.90秒**，/tmp/abcd-literal-receiver-anchor-build.log 与 anchor-tests.log。
  无关全库/RSS/基准/DFX/代理保持暂停。
- 五输入 build/stage-one/value-literal-receiver-pass-1 顺序导出均退出0，保持
  **51 complete / 3 incomplete / 52 failure / 46 no-module**；没有 complete
  回退或 legacy writer，/tmp/abcd-literal-receiver-batch-summary.json。
  真实 PasteBoard 构造器不再触发先前 op86 的 literal guard 拒绝，但整 record
  仍未完成，不能授予配对语义或源码表示 Accepted，阶段一 Goal 继续 active。
- 下一拒绝已定位：PasteBoard entry8831/PC182 创建 async method9468，
  DEFINE METHOD 的 accumulator home 与 kAsync attrs 被 CreationTreeContext
  kMethodBody 的 NONE-only 前提拒绝，随后映射为 ClassContextError::kSite。
  /tmp/abcd-literal-receiver-defined-kind.log 与 defined-kind-fields.log。
  下一步核对 pinned factory 语义并贯通共同 async method context/body、实际
  home/receiver 与独立行为验证，不能将它改成普通同步方法或跳过正文。

### 2026-10-01：primitive 数组屏障贯通完整 Home record

- FamilyReader 复用 ReadBufferedArray，保留每个原始 BufferedArrayCreation
  的 primitive-only 内容与 site；普通及 phased family 都经相同读取分支，
  不将数组送入 BindCreation，不清除分配/捕获屏障。FamilyWrites 独立核对
  原始 opcode、literal pool、唯一 site 与屏障消费，新增 primitive_arrays
  census；实际元素仍由共同 constants/AST/TreeSemantics/printer 校验。
  callable/未知 literal、重复/缺失/错误 pool 证据保留 typed refusal。
- 每个 buffer 的解码/retained storage 服从现有共享内存预算，新增读取成本
  按各原始 buffer 展开的总元素及字符串字节计算，不按唯一 pool 数量宣称
  线性。三 profile 原始序列化 file/family 用例覆盖1/200/201/400/800创建，
  几何 work、完整 ABI/tree/source、buffer callable 拒绝；原 class family
  新数组用例实际 RED，/tmp/abcd-array-barrier-red-build.log 与 red-tests.log。
  FamilyRouteFailure 也补齐 fallback creation_pc，保持更深 bytecode PC 优先，
  不将 literal EntityOffset 当作 PC；新定位回归先 RED 再 GREEN。
- 普通/异常数组完整源码独立对照 **474 场景**，核对 native Array/realm
  prototype、元素及 length、own descriptor、负零、每次调用分配身份、
  回调 mutation、receiver、任意 throw 与 catch lexical value；原型 setter、
  iterator、Array constructor 及 call/apply/bind 毒化。元素值及 array→object
  的语法合法 mutation 被拒绝，family_array_source_test。
- 真实 Home record5521 完整恢复，原 method6760/PC85 的屏障拒绝已消失，
  /tmp/abcd-array-barrier-home.json。新增 home_record_source_test：引擎读取
  ABC 并退出后，oracle才读取整份配对 Home.ts，按转换后 ArkTS 的legacy
  TS语义擦除恰好7条未初始化private类型声明；不拼接方法、不输入sidecar。
  两份完整模块在独立realm对照 **362 场景**，覆盖imports/注册、constructor
  length/prototype/own字段、accessor/lifecycle、共享条件链、权限array参数、
  同步thenable及native立即/延迟/拒绝Promise、dialog与paramsGenerator闭包、
  resource/Breakpoint、同步/延迟UI创建、click receiver及各处getter/call异常。
  权限值的语法合法mutation被拒绝；native回调实际执行有独立断言。
  首版mock遗漏Promise对返回对象then的探测，补齐普通缺失then后重新通过。
- 相关构建退出0，/tmp/abcd-array-barrier-final-build.log 与
  /tmp/abcd-array-barrier-route-fixed-build.log；最终 **34/34 定向回归通过
  36.14秒**，/tmp/abcd-array-barrier-commit-tests.log，含上述完整两套oracle、
  原始object屏障、class/capture/derived、普通/phased literal、PageA及Index。
  既有旧writer named_call_test/base_constructor_js_test失败未纳入本任务通过
  数；前者上一任务已实际恢复HEAD复现。全库/RSS/基准/DFX/代理仍暂停。
- 五输入 build/stage-one/value-array-families-pass-1 均退出0，新增Home5521，
  **51 complete / 3 incomplete / 52 failure / 46 no-module**，无complete回退
  或legacy writer，/tmp/abcd-array-barrier-batch-summary.json。Home保留native
  class/accessor、默认参数、if/||、箭头闭包、数组和try/catch，已有独立配对
  行为证据；NamedEvaluation包装、临时变量及括号仍有表示差距，尚不授予
  整体style Accepted或平台执行收据。阶段一Goal active。
- 下一个实际拒绝是 PasteBoard record8383（入口8831、构造器9277），
  FamilyTreeFailure root member0 / EvaluationError::kUnsupportedOperation，
  /tmp/abcd-array-barrier-pasteboard-next.log。下一步从原始 live operation 和
  evaluation支持边界定位，不删效果/guard、回退writer或跳过失败record。

### 2026-10-01：共享代码分支贯通原生短路条件链

- normal ControlPlan 从原始 CFG 恢复单入口、共享同一代码分支的条件链，
  使用反向拓扑 memo，新增发现为 O(B+E)。保留每条原始边、分支方向、
  condition owner 和实际 continuation；共享 body 只归属一次，支持执行后
  合流及 return 提前结束。ControlVerify 从原始边与 indegree 独立核对，
  错误 evaluate、方向、join、终止节点注解和真正交叉进入继续拒绝；
  loop/exception projection 不获得此规则，投影 skeleton 拒绝该注解。
- FunctionTree/TreeFlow 经同一表达式森林输出原生右结合 || 和一个 if。
  每个 predicate 与原始 branch/PC 一一核对，只有已拥有的 inline operand
  才能位于条件 RHS；独立 effect/snapshot、cell/environment 变化及 phi
  edge copies 继续拒绝，不提前运行后续 getter、不复制共享 body，不增加
  helper/writer。附加条件块使用实际 enclosing lexical scope，logical/value
  selection discovery 不误将此控制链当作 phi 选择。
- 新用例从三 profile 实际 opcode 字节解码，经 FunctionInput、ValueGraph、
  control/evaluation、共同 AST、独立 semantics、ABI 和 printer。覆盖四种
  条件方向、普通/提前 return、1/2/200/201/400/800 条件及几何 work；
  discarded getter 保留拒绝。旧 ControlPlan 上新用例实际 RED，
  /tmp/abcd-shared-condition-red-build.log 与 red-tests.log。
- 独立完整函数对照通过 **192456 场景**：不同 primitive/object 值、严格
  比较无 coercion、getter 改写后续状态、任意 throw、Proxy、native member
  receiver、non-callable、callee/continuation getter 异常、提前 return，
  callee 自身及 ambient call/apply/bind 毒化。OR→AND 和重复 getter 的
  语法合法 mutation 被 oracle 拒绝，
  /tmp/abcd-shared-condition-completion-behavior.log。
- 恢复本次实现后的相关构建退出0，
  /tmp/abcd-shared-condition-restored-build.log；最终 **37/37 定向回归通过
  130.98秒**，/tmp/abcd-shared-condition-verified-tests.log，含上述完整行为
  oracle、control/tree、logical/loop、cell/capture、普通/phased literal、
  派生构造器及完整 PageA/Index 回归。排除的旧 writer 基线失败见下。
- 五输入 build/stage-one/value-shared-conditions-pass-1 均退出0，保持
  **50 complete / 3 incomplete / 53 failure / 46 no-module**，无 complete
  回退、新增 complete 或 legacy writer，
  /tmp/abcd-shared-condition-batch-summary.json。这未新增完整 record 或
  整体配对表示 Accepted；全库/RSS/基准/DFX/代理仍暂停。
- 扩大的定向回归最初36/37通过；额外旧 writer named_call_test 在555行
  的 AccumulatorFailure variant 断言失败。恢复全部本次生产改动到 HEAD
  后构建退出0、同一测试仍失败，/tmp/abcd-shared-condition-baseline-build.log
  与 baseline-test.log，现已完整恢复本次修改。该既有失败不计作通过。
- 真实 Home 已越过 method5739 的 normal kCrossEntry，整份 record 的首个
  拒绝移至 method6760/PC85，/tmp/abcd-shared-condition-home-next.log。
  实际是 BufferedArrayCreation，FamilyReader 调 BindCreation 后得到
  CreationError::kUnsupportedOpcode，/tmp/abcd-shared-condition-creation-cause.log。
  下一步贯通 primitive array barrier 与现有共同 array constants，不忽略
  callable literals、capture barrier 或分配效果。阶段一 Goal active。

### 2026-10-01：派生 receiver 贯通实际闭包创建作用域

- DerivedProjection 从真实 super stored operand、原始 local SSA 及同文件
  后代捕获保留结果恢复 receiver cell。所有直接 store 均为 super 时仍须
  创建初始化证明；先 undefined 后 super 的 cell 另外要求实际 super store
  支配原始创建点。每个父方法只构建一次原始 normal dominator 索引，通过
  重新绑定的 ParentCell 传递，也覆盖无直接读取的中间闭包。一般 Language
  初始化、早于 super store 的创建、分支局部 store、未知写入及环境 escape
  不获得 receiver 证明，不授权一般 capture constness。
- 既有 guard 来源锚点统一为 receiver_guards，普通子函数仅消费已证明
  receiver 的 RequireInitialized，反向检查仍限派生构造器。EvaluationUses
  同步排除已消费 guard operand，恢复真正 native member call 的 callee、
  参数及 receiver，保留原始 PC/效果。新增索引按 cell accesses/图大小
  建立，use census 为 O(U log(G+1))，不逐 cell/创建遍历全图，无新 writer。
- 派生 VM 的 ReceiverEntry 是 super 前已知 undefined，共同 AST 正确输出
  (void 0)，不再误打印提前访问的 native this；真正初始化后的 frame-this
  仍要求 receiver 引用。TreeOperands 与 ABI 分别核对，错误 ThisExpr 和
  输出 receiver/pre-super-this mutation 均被独立检查拒绝。
- 三 profile 实际序列化构造器/子闭包经完整 file/class/family/printer，新增
  直接及转发 receiver oracle 共 **4536 场景**，核对父替代对象/Proxy、
  即时 setter 调用、callee/参数 getter、任意 throw、non-callable 参数顺序
  和 callee 被 getter 替换；callee 自身及 ambient call/apply/bind 均毒化。
  1/200/201/400/800 个转发调用核对 guard 数和几何 work；undefined、
  混合来源、后代 writer、escape、早创建、分支 store 和错误 guard 保留反例。
  真正 Home/5851 投影核对其前置 undefined 后的 receiver initialization。
- 本次重新相关构建退出0，/tmp/abcd-captured-receiver-commit-build.log；
  **25/25 定向回归通过32.02秒**，
  /tmp/abcd-captured-receiver-commit-tests.log，含既有构造器、capture、
  普通/phased literal、member reference、tree、完整 PageA/Index 回归。
- 五输入 build/stage-one/value-captured-receiver-pass-1 均退出0，维持
  **50 complete / 3 incomplete / 53 failure / 46 no-module**，无 complete
  回退、无新增 complete 或 legacy writer，
  /tmp/abcd-captured-receiver-batch-summary.json。尚未新增完整 record 或
  整体表示 Accepted；全库测试/RSS/基准/DFX/代理仍暂停。
- Home 构造器5851 的 child6322 已越过此前 kExpression。现整份 record
  阻塞于 method6733 的 child5739，normal BuildControlPlan 报 kCrossEntry，
  /tmp/abcd-captured-receiver-home-next.log。下一步从真实控制图定位共享
  合流/区域归属缺口，不删除边、复制效果或回退旧 writer。阶段一 Goal active。

### 2026-10-01：字面量 initializer 贯通有序闭包创建

- LiteralExpression 接纳共同 AST 已支持的 kFunction/current-function、无显式
  operand 的 ValueFunctionCreation。沿用原有唯一 consumer、层叠 initializer
  区间、canonical use census、环境边界及 phased catch-copy 核对；不把
  closure creation 当作 body execution，也不授予捕获恒定或省略 child owner。
  EvaluationPlan/FamilyTree 仍核对完整 child/capture。生产只增加 typed
  predicate，无新遍历、存储、writer、样本选择或准入设施。
- 三 profile 的真实序列化构造器先 allocation，再 DEFINEFUNC，最后 own
  definition；经共同 file/class/family/printer 输出完整 object literal/arrow。
  旧实现实际 RED，/tmp/abcd-literal-closures-red-tests.log；新增独立对照
  **630 场景**检查 base/Proxy/替代 receiver、闭包 this、setter 任意 throw、
  own descriptor、VM realm prototype 和继承 setter 不被 own define 调用，
  /tmp/abcd-literal-closures-behavior.log。既有直接 callback 630 场景也通过。
  捕获 receiver mutation 及移除 empty NamedEvaluation 的名称 mutation 均
  被 oracle 拒绝；名称反例先通过语法检查，避免把语法破坏算作语义负例。
- LiteralPlan 单测覆盖1/200/201/400/800个创建，所有创建都归属唯一字段，
  几何 work、已有精确额度及缺失 witness 检查通过。共享 callback、跨环境
  pop 和错误 home-object 继续拒绝。相关构建退出0，
  /tmp/abcd-literal-closures-final-build.log；最终 **21/21 定向回归通过22.45秒**，
  /tmp/abcd-literal-closures-verified-tests.log，含普通/phased literals、
  member reference、capture、tree、PageA/Index及派生构造器。
- 五输入 build/stage-one/value-literal-closures-pass-1 全部退出0，维持
  **50 complete / 3 incomplete / 53 failure / 46 no-module**，无 complete
  回退或 legacy writer，/tmp/abcd-literal-closures-batch-summary.json。
  未新增完整 record 或配对表示验收；全库/RSS/基准/DFX/代理仍暂停。
- 真实 Home/5851 越过此前 PC392 allocation/PC398 closure 的 LateValue
  拒绝，FamilyTrees 现到 member1 的 FunctionTreeError::kExpression；
  /tmp/abcd-literal-closures-home-next.log。实际为 normal CallPlan::kReference，
  call op75 的 callee property 仍为 kRoot/kEffectSnapshot，
  /tmp/abcd-literal-closures-home-call-reference.log。下一步处理原生成员引用
  的此种参数/receiver 求值组合，保留 getter 次数、callee、receiver 和异常
  顺序，不用可篡改 call/apply 替代。阶段一 Goal 继续 active。

### 2026-10-01：派生构造器接收者贯通保留的词法 cell

- FamilyTrees 将同一不可变 ABC/index/lookup 交给派生构造器投影，独立调用
  现有可达子模板捕获保留证明，并同时用于原始 range/spread 协议及当前工作图。
  原始 local SSA 只选择 reaching stores；实际工作 load/store 的地址、效果、
  来源和 stored operand 仍逐项核对。值/cell 共用有界来源格与稀疏依赖传播，
  guard 仅消费真正的 super 结果或初始 undefined；子闭包写入、环境 escape、
  未证明的 capture、混合来源和不可用反馈继续拒绝，不授权一般捕获恒定。
- 捕获保留状态改为只索引可达目标的受计费平衡树，不再为每个构造器分配和
  扫描全文件 method 状态表；保留反向依赖固定点。新增来源传播为
  O(graph values/uses + original instructions + local SSA values/inputs)，
  每节点至多两次状态变化，循环无具体来源不能获得初始化证明。
- 真正 pasteboard Home/5851 在旧工作投影以 kProtocol 失败，
  /tmp/abcd-derived-cell-red-tests.log；现在同一 ABC 的原始及工作协议均通过，
  并重验完整 guards，缺少同文件捕获输入仍拒绝。三 profile 序列化构造器
  测试覆盖 1/200/201/400/800 个读取、同源及混合 cell 分支、实际 operand/
  地址/PC 篡改、不可用反馈及缺失 guard；200/201/400/800 个无关方法不再
  导致线性状态 census。完整 file/class/family/printer 输出通过 **630 个**
  constructor/capture/父返回对象/Proxy/异常对照场景，
  /tmp/abcd-derived-cells-verified-behavior.log，独立输出 mutation 被拒绝。
- 相关构建退出0，/tmp/abcd-derived-cells-verified-build.log；**16/16 定向
  回归通过10.53秒**，/tmp/abcd-derived-cells-verified-tests.log。扩大检查中
  base_constructor_js_test 在旧 writer 的 assignments 用例失败；临时将本次
  生产改动还原 HEAD 后同位置仍失败，基线构建退出0、测试退出8，
  /tmp/abcd-derived-cells-base-baseline-{build,tests}.log。未把该测试计入通过，
  当前实现已恢复重建；全库测试/RSS/基准/DFX/代理仍暂停。
- 五输入 build/stage-one/value-derived-cells-pass-1 全部退出0，维持
  **50 complete / 3 incomplete / 53 failure / 46 no-module**，没有 complete
  回退及 legacy writer，/tmp/abcd-derived-cells-batch-summary.json。本任务
  消除真实协议缺口，但尚未新增完整 record 或配对语义/表示验收。
  Home/5851 现进入 EvaluationPlan，具体在 LiteralPlan 的 EarlyValue 以
  kLateValue 拒绝：allocation value136/order91 与 later stored value138。
  /tmp/abcd-derived-cells-home-literal.log。下一步处理真正的字面量 initializer
  求值顺序，不能通过删除效果或放宽 LateValue 检查绕过；Goal 保持 active。

### 2026-10-01：PageA完整record的独立配对语义回归

- 新增pagea_record_source_test。生产abc_semantic只读取固定SHA的ABC并退出，
  oracle随后读取完整PageA.ts，不截取方法、拼接输出或向引擎提供sidecar。
  转换后ArkTS的两条未初始化private type declaration按legacy TS擦除语义
  删除，再由Node stripTypeScriptTypes去类型；其他private形式立即失败，
  不让JS字段undefined初始化污染参考。没有新生产依赖或引擎修改。
- 两份完整module在独立VM realm运行，对照**474个场景**：模块导入/注册、
  class及builder名称/length、默认参数、prototype/实例own字段、params两次
  getter、storage/accessor/lifecycle、初建/更新builder、同步/延迟页面创建族、
  breakpoint与资源值、onReady捕获，以及async click的值/Promise/thenable、
  延迟settle、拒绝和任意throw身份。component及pasteboard/系统/UI surface
  使用Proxy逐次核对getter/call顺序及receiver；ambient call/apply/bind被毒化。
- 独立输出mutation可捕获移除await、替换捕获receiver和改变toast offset；
  /tmp/abcd-pagea-paired-mutations.log。初始receiver mutation使用箭头内this，
  实为语义等价而未被拒绝，随后改为错误global receiver才构成有效负例。
  CMake配置退出0，/tmp/abcd-pagea-paired-configure.log；PageA、既有Index、
  相位字面量参数JS及class/family fixture **4/4定向检查通过1.32秒**，
  /tmp/abcd-pagea-paired-tests.log。本任务仅测试/注册，未修改生产代码。
- 表示检查：PageA完整输出保留native class、accessor、默认参数、if/条件式、
  component调用/箭头闭包、嵌套resource object/array和async/await，没有assembly
  或通用machine writer；但NamedEvaluation包装、较多临时变量/括号与原源码
  仍有表示差距。因此这只是独立配对行为证据，不授予整体表示Accepted、平台
  执行收据或全样本完成；PageA CLI仍executable=false，阶段一Goal保持active。
  全量/RSS/基准/DFX/代理继续暂停。下一步处理其他实际tree_stage拒绝，并保留
  PageA表示质量改进项，不因一个页面通过停止阶段一开发。

### 2026-10-01：原生成员引用贯通完整相位字面量参数

- PhaseCorridors一次索引已由LiteralPlan证明的完整initializer区间；native call
  组合消费allocation至最后own definition的全部原始效果，而不是只消费创建点。
  隐式initializer base从canonical source-value census分离，仍由LiteralPlan
  独立核对ownership。getter、嵌套读/调用、创建身份、参数顺序和receiver保留；
  不新增writer，不删SSA/phase/PC，不改用可篡改.call/.apply。
- 可能抛异常的inline event仍核对最终root的handler和staged catch-copy source；
  非抛异常allocation只承担身份/顺序义务。共享、反向参数、改变handler和catch
  可见快照继续拒绝。LiteralPlan重验只需要catch索引，明确排除候选literal区间
  再索引；实际exact-budget回归发现并修复了重复计费，未删除预算边界检查。
  新增O(operations+definitions+uses)工作和O(operations)受计费索引，无新依赖。
- 真正解码的相位参数在旧实现RED，
  /tmp/abcd-phase-literal-arguments-red-tests.log。三profile的1/200/201/400/800
  属性，nested object、empty array、两个参数通过独立evaluation/exception tree
  检查，几何work增长和负例通过。序列化file/class/family、ABI、instruction
  sources与共同printer产生原生member call；完整函数独立行为对照通过
  **2646个场景**，含getter先于参数、non-callable仍先求参数、Proxy/receiver、
  throw身份、fresh object/array、prototype/descriptor及继承setter。
  /tmp/abcd-phase-literal-arguments-behavior.log。最终相关构建退出0，
  /tmp/abcd-phase-literal-arguments-final-build.log；**21/21定向回归通过29.90秒**，
  /tmp/abcd-phase-literal-arguments-final-tests.log。全量/RSS/基准仍暂停，无代理。
- 真实PageA record6897现经共同树路线生成完整源码，包含async child7421；
  /tmp/abcd-phase-literal-arguments-pagea.json为source_complete=true、
  executable=false、legacy_writer_used=false。这不是平台执行或配对语义验收。
  五输入build/stage-one/value-phase-literal-arguments-pass-1均退出0，现为
  **50 complete / 3 incomplete / 53 failure / 46 no-module**；新完整record为
  pasteboard6897及7640，无完整项退化、无legacy writer，
  /tmp/abcd-phase-literal-arguments-batch-summary.json。
- 下一项是对PageA完整生成record与独立配对源码核对module、constructor、
  生命周期、render创建族及async click的暂停/恢复、效果与异常身份；同时单独
  检查高层表示。不得将source_complete升级为阶段一Accepted或Goal完成。

### 2026-10-01：纯搬运消除与非抛异常数组贯通相位字面量

- 两个canonical use census不再把独立证明为dead的纯操作输入当作对象观察。
  在使用该前提前，VerifyElidedPureEvaluations先检查每个ElidedPure操作没有
  可观察效果/异常/consumer/temporary，再检查所有uses的dead closure；结果
  不能进入保留操作或phi/catch copy。检查独立于builder的backwards worklist，
  新增O(operations+uses)时间、O(1)临时空间，work计入现有LiteralBudget。
  原始SSA、phase、uses、PC及最终source anchor保留，没有删图或新writer。
- 非抛异常的ordered allocation仍保留创建身份及区间顺序，不再要求不存在的
  exceptional exit；可能抛异常的owned effect继续通过SameCatch核对实际
  handler和staged catch-copy source。没有放宽共享、未完成观察或catch快照。
- 真正解码的dead-copy用例在旧实现失败，
  /tmp/abcd-literal-dead-use-red-tests.log；接用途检查后，额外对象观察census
  又拒绝同一搬运，/tmp/abcd-literal-dead-use-green-debug.log。两处均接入
  同一个独立dead closure前提。嵌套非抛异常array在后续SameCatch处实际RED，
  /tmp/abcd-literal-dead-array-red-tests.log。三profile的2/200/201/400/800
  copy链及array属性通过求值、异常AST和semantics；几何work增长检查通过。
  伪造effect/有用途copy为ElidedPure仍以Flow拒绝，既有shared和snapshot负例保留。
- 三profile序列化dead-read及dead-array夹具贯通共同file/class/family、ABI、
  instruction sources与printer。完整函数独立对照现为**558个场景**，
  getter/Proxy/receiver/任意throw身份、保留效果、fresh object/array、prototype、
  own descriptor及继承setter通过，/tmp/abcd-literal-dead-use-behavior.log。
  被消除搬运的各原始PC仍有anchor，位置检查及独立instruction correspondence
  通过。最终构建退出0，/tmp/abcd-literal-dead-use-final-build.log及-scaling-build.log；
  **20/20定向回归通过21.98秒**，/tmp/abcd-literal-dead-use-final-tests.log。
  全量/RSS/基准继续暂停，无代理或平台最终验收。
- 五输入build/stage-one/value-literal-dead-use-pass-1均退出0，维持
  **48 complete / 2 incomplete / 56 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-literal-dead-use-batch-summary.json。
  没有新增完整record或配对语义/表示验收，阶段一Goal仍active。
- PageA family成员5（7421）现通过LiteralPlan和相位EvaluationPlan，并进入
  共同ExpressionForest；FamilyTrees仍以FunctionTreeError kExpression拒绝，
  /tmp/abcd-literal-dead-use-pagea-family.log。具体为CallPlan kReference：
  property op58/value70（NamedRead，kRoot/kCrossRegionValue）与invoke op81
  分处phase block116/162，中间构造嵌套字面量参数，
  /tmp/abcd-literal-dead-use-pagea-reference.log。
  下一步接通native member reference的有序字面量参数，保留先getter后参数、
  receiver及异常快照，不改用可篡改.call/.apply适配。整record仍tree_stage，
  /tmp/abcd-literal-dead-use-pagea.json；组件通过不是完整7421或PageA发布。

### 2026-10-01：共同字面量接入受检的一元表达式与原始常量

- LiteralPlan接纳共同AST已有的Negate、BitNot、LogicalNot、TypeOf、ToNumber、
  ToBoolean单次输入表达式，复用单用途、层叠区间、原始效果和实际catch-copy
  检查。整数/浮点/special常量ValueCopy只在typed primitive input、pure且
  不抛异常时接纳；ValueId变量复制不成为primitive leaf。没有新writer、
  样本分支、依赖或额外图遍历，算法复杂度和内存边界沿用共同nesting规划。
- 原数值域证明保留：真实7421拒绝点的NEG输入为NonNaNNumber，
  /tmp/abcd-literal-unary-pagea-input.log；任意Language NEG仍在独立输入证明
  拒绝。ToNumeric、INC/DEC和需要重复snapshot的VM谓词没有扩大准入。
  一元RED实际失败，/tmp/abcd-literal-unary-red-tests.log；接一元后又确认
  LDAI的ValueCopy常量在dependency discovery拒绝，
  /tmp/abcd-literal-unary-late-debug.log。修复没有删除late-value检查。
- 三profile的六种一元形式通过相位evaluation/exception tree/独立semantics，
  2/200/201/400/800属性覆盖；共享一元值、未经证明的NEG及numeric update
  继续拒绝。普通路径的共享常量由LateValue改报Observed，仍拒绝；单用途
  常量接纳，新增真正的晚到binding copy负例保留LateValue拒绝。
- 三profile序列化constant NEG和dynamic BitNot贯通共同file/class/family、
  ABI、instruction sources与printer。完整生成函数通过**1128个独立场景**，
  包含getter/Proxy/ToPrimitive顺序、BigInt/Symbol、数值边界、任意throw身份、
  保留效果、fresh对象、own descriptor及继承setter，
  /tmp/abcd-literal-unary-behavior.log；原始constant/unary/property PC及
  combined origin的printer位置也核对。最终构建退出0，
  /tmp/abcd-literal-unary-final-build.log及-refusal-build.log；
  **19/19定向回归通过15.34秒**，/tmp/abcd-literal-unary-final-tests.log。
  全量/RSS/基准继续暂停，无代理或平台最终验收。
- 五输入build/stage-one/value-unary-literal-pass-1均退出0，维持
  **48 complete / 2 incomplete / 56 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-literal-unary-batch-summary.json。
  完整项数量没有增加，不能据此宣布阶段一语义或源码表示验收完成。
- PageA family成员5（7421）已越过原一元discovery拒绝，但整个LiteralPlan
  仍不通过。当前VerifyNesting的canonical census拒绝nested array allocation
  op64/value76的另一个consumer op65（ValueCopy，evaluation为ElidedPure），
  /tmp/abcd-literal-unary-pagea-shared.log及-shared-owner.log。
  下一步区分真正观察和独立可消除的无用途搬运，并保留original input/origin
  对应；不能直接豁免共享对象、catch/phi或有用途的snapshot。整record仍
  tree_stage/unproved，/tmp/abcd-literal-unary-pagea.json；Goal保持active。

### 2026-10-01：相位嵌套初始化贯通共同字面量与原生调用

- LiteralPlan将已有单次用途/层叠区间规划接入unique normal phase corridor，
  使用受检alias解析实际RHS、callee和receiver；一次canonical use census检查
  所有间接消费者、catch/phi观察。没有删除原始SSA/phase/PC或新建writer。
  共同ExpressionForest依赖工作表和origin检查也解析实际初始化base/RHS，
  保留allocation/read表达式与各own definition的独立来源。
- 每个折叠效果及definition保留最外allocation的exception target和实际
  staged catch-copy source，复用PhaseCorridors::SameCatch。原生member reference
  可经受检nested placement跨phase，CallPlan/TreeOperands继续核对receiver；
  没有使用可篡改的.call/.apply适配。共享对象、未完成观察、跨handler范围、
  改变catch可见局部值、缺失/错误consumer及重排definition仍拒绝。
- 解码嵌套夹具在旧实现实际失败，/tmp/abcd-phased-nesting-red-tests.log；
  序列化nested call曾在实际准备入口拒绝，
  /tmp/abcd-phased-nested-call-red.log。三profile的1/2/200/201/400/800属性
  贯通evaluation、异常AST、TreeFlow和独立semantics。临时移除SameCatch检查后，
  kNestedSnapshot错误接受立即被测试捕获；见
  /tmp/abcd-phased-nesting-catch-mutation-tests.log及对应-debug.log。检查已恢复。
- 三profile序列化nested read/call通过共同文件、class/family、ABI、instruction
  sources与printer；完整生成函数通过306个独立getter/Proxy、receiver、任意
  throw身份、保留效果、fresh outer/inner、own descriptor和继承setter场景，
  /tmp/abcd-phased-nesting-behavior.log。各原始PC及property printer位置也核对。
  最终生产/测试构建退出0，/tmp/abcd-phased-nesting-final-build.log；18/18
  定向回归通过20.49秒，/tmp/abcd-phased-nesting-final-tests.log。
  全量/RSS/基准保持暂停，本次是作者自审，没有代理或平台最终验收。
- 五输入build/stage-one/value-phased-nesting-pass-1均退出0，维持
  **48 complete / 2 incomplete / 56 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-phased-nesting-batch-summary.json。
  这不是阶段一配对语义/表示质量最终验收，Goal仍active。
- 真实7421不再首先拒绝allocation60之后的nested allocation62候选；当前
  先停在后续allocation69的RHS ValueUnary kNegate（op74/value84），仍为
  kLateValue，/tmp/abcd-phased-nesting-pagea-next-rhs.log。整个LiteralPlan尚未
  通过，不能将前一个候选接纳算完整验证；PageA整record仍tree_stage。
  下一项接入后生成算术RHS及其真实coercion/异常顺序，再复验7421整family。
- 决策：沿用功能优先的现有status记录与定向验证；用户暂停全量/RSS及禁代理
  的约定优先于Skill的默认全套验证/委派流程，不增加独立审查或发布设施。

### 2026-10-01：相位函数族的实际闭包写入前提贯通

- BuildPhasedEvaluationPlan新增显式ClosureWrites参数，默认仍为Unknown。
  FamilyTrees传入实际FilePhasedFunction::Locals().writes，完整family仍在返回前
  执行VerifyFamilyWrites。之前上游已经持有Language前提，但求值入口固定丢弃
  它，导致零child binding的buffered object加cell访问被kClosure拒绝。
  现有plan与cell witness的一致性、原始store域、buffer覆盖及catch路径的
  store-Commit支配检查全部保留；没有把Language前提改成已初始化或稳定快照。
- 实际解码回归在接参但仍传Unknown时确实失败，
  /tmp/abcd-phase-write-premise-red-tests.log。三profile的buffered cell用例及
  2/200/201/400/800规模通过相位求值与独立检查；默认Unknown、擦除前提、
  非法枚举及catch路径缺少store的情况仍拒绝。
- 新序列化ABC含primitive buffer、词法cell、普通call和catch store，没有
  child binding。三profile贯通实际PreparedFamily、共同AST/printer、TreeFlow、
  ABI、instruction sources与完整FamilyWrites；删除buffer coverage和伪造
  Unknown局部前提仍拒绝。恢复旧family参数后，该夹具确实在
  PreparedFamily失败，/tmp/abcd-phase-write-premise-family-red-debug.log。
- 完整生成函数经独立参考通过237个callback/Proxy、receiver、返回/异常身份、
  fresh object、own descriptor、继承setter及保留修改场景，
  /tmp/abcd-phase-write-premise-behavior.log。生产与相关测试构建退出0，
  /tmp/abcd-phase-write-premise-final-build.log；最终12/12定向回归通过14.85秒，
  /tmp/abcd-phase-write-premise-final-tests.log。全量/RSS/基准未恢复。
- 真实PageA child7421现越过CellInitializations kClosure；下一拒绝为
  相位LiteralPlan kLateValue，/tmp/abcd-phase-write-premise-pagea-literal-kind.log。
  它仍未生成完整tree，整record仍tree_stage；不能据此宣布该子函数恢复完成。
  下一步接入相位初始化RHS的实际求值/异常归属，不能只放宽late value条件。
- 五输入build/stage-one/value-phase-write-premise-pass-1均退出0，维持
  **48 complete / 2 incomplete / 56 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-phase-write-premise-batch-summary.json。
  本次fixture行为对照不是配对源码/平台最终验收，阶段一Goal继续active。

### 2026-10-01：条件模块读取与相邻选择调用贯通

- 共同选择规划使用一次受检module peer索引，在条件及&&/||臂中接纳已归属
  inline或phi输入的read/guard配对。PhiExpressionProducer只吸收该guard的原始
  用途和相邻事件，仍要求一个真实phi用途和惰性尾部；其他guard、debugger、
  环境及额外效果继续作为屏障。guard保留kModuleGuard，两条原始PC共享同一
  module表达式，未删除检查、增加writer或使用样本分支。
- 选择Domain区分header归属和已消费join前缀。前一次调用结束后的同块后缀可
  作为下一次选择的header，独立正序区间及一次消费检查仍覆盖全部原始效果。
  每块至多参与前一次join与后一次header，新增O(blocks)索引空间；没有逐分支
  全图搜索。模块索引新增O(operations+modules)时间/O(operations)临时空间。
- 新实际解码回归覆盖条件、&&、||的直接模块值及模块属性，宽度
  1/2/200/201/400/800，经共同树、semantics、TreeFlow及instruction sources；
  丢失配对guard、removed guard和臂内debugger均被拒绝。旧实现红例见
  /tmp/abcd-selection-module-red.log；相邻第二次调用的原kReference失败见
  /tmp/abcd-selection-module-call-failure.log。
- 完整生成函数独立行为对照4626场景通过，覆盖严格比较/短路、TDZ、原生
  receiver、getter/Proxy、异常身份/顺序、动态module binding及连续两次调用，
  /tmp/abcd-selection-module-behavior.log。生产与测试构建退出0，
  /tmp/abcd-selection-module-final-build.log；12/12定向回归通过23.45秒，
  /tmp/abcd-selection-module-final-tests.log。没有恢复全量/RSS/基准。
- 真实PageA子函数7366现为完整tree、零recovery issue，已通过family循环内
  TreeFlow、ABI及instruction source检查，/tmp/abcd-selection-module-pagea-family.log。
  整份PageA仍为tree_stage；下一成员7421在phased CellEvaluation返回
  kInitialization，底层CellInitializations为kClosure，见
  /tmp/abcd-selection-module-pagea-cell-kind.log及
  /tmp/abcd-selection-module-pagea-initialization-detail.log；需接入实际family
  closure-write前提与独立写入检查，不能跳过
  其cell生命周期/异常语义来放行。
- 五输入build/stage-one/value-selection-module-pass-1均退出0，新增
  pasteboard ShareDitect record10857的完整共同源码；现为
  **48 complete / 2 incomplete / 56 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-selection-module-batch-summary.json。ShareDitect.mjs
  通过Node语法检查，恢复两处Button.width的原生条件模块读取；配对TS仅在引擎
  退出后用于独立阅读对照。整record的平台行为及表示质量仍待验收，manifest的
  source_complete不等于阶段一完成，Goal继续active。

### 2026-10-01：嵌套聚合参数与混合表达式依赖贯通

- LiteralPlan现在接纳正常同块表达式中的fresh object/array参数，包括primitive
  buffer及已有own initialization。独立检查以最终definition作为参数区间末端，
  要求在consumer和下一参数之前完成；enclosing owner通过表达式消费者传播。
  单次用途、未完成对象不逃逸、laminar初始化和原始效果顺序继续核对，未放宽
  arbitrary late value或phased nesting。
- ExpressionForest以有界Kahn工作表构建混合依赖，将initializer的语法父节点
  指向真实allocation；每个nested node一条parent edge。替换不能处理交替
  aggregate/call依赖的正反两遍扫描；额外O(operations+nodes)时间/O(operations)
  scratch，沿用byte/work预算，不递归或反复全图扫描。完整源码保留
  new Ctor({value: arg.value}, [9], {})，无新writer/依赖/样本分支。
- 新真实解码夹具在旧实现确实于EvaluationPlan失败，
  /tmp/abcd-literal-argument-red-source.log。修复后宽度1/2/200/201/400/800
  贯通共同树、TreeFlow、semantics及printer；独立流有效的参数重排、聚合
  共享和对象逃逸仍被拒绝，匹配重排operand的伪造nested proof也被拒绝。
  另有1/2/200/201/400/800层object/construct交替嵌套通过共同树和printer。
- 独立JS完整函数对照通过288个参数getter、构造器/Proxy、异常、替代对象、
  prototype/descriptor、参数身份及保留修改用例，加6种argument-before-TypeError
  检查，/tmp/abcd-literal-argument-behavior.log。生产和相关目标构建退出0，
  /tmp/abcd-literal-argument-final-build.log；最终12/12定向测试通过15.03秒，
  /tmp/abcd-literal-argument-final-tests.log。全量、RSS及基准保持暂停。
- 真实PageA子函数7255现为完整tree、零recovery issue，已检查TreeFlow、ABI及
  instruction sources，/tmp/abcd-literal-argument-pagea-7255-tree.log；其父family
  尚未完成。当前下一子函数7366的CallPlan返回kReference，PC135显式receiver
  call的callee为跨region root snapshot，原读取在PC84/block0、调用在block3，
  /tmp/abcd-literal-argument-pagea-call-reference.log及
  /tmp/abcd-literal-argument-pagea-callee-origin.log；
  ExpressionForest因此拒绝kUnsupportedOperation，
  /tmp/abcd-literal-argument-pagea-expression-error.log。先前catch/返回行的
  无效局部变量曾被误读为kControl，已在真正FamilyTrees::Fail入口核正。
- 最终五输入build/stage-one/value-literal-argument-pass-2均退出0，维持
  **47 complete / 2 incomplete / 57 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-literal-argument-final-batch-summary.json。
  不以子函数通过替代record发布、配对语义或表示验收；Goal仍active且未完成。
- 下一步恢复跨控制region的native member reference及其参数求值，不猜测receiver
  或改写为依赖可变Function.prototype.call的调用。输入ABC和配对源码未修改。

### 2026-10-01：对象初始化中的单次构造表达式贯通共同源码路径

- 正常同块LiteralPlan接纳ValueConstruct作为受检的late value expression；
  复用现有求值形状、调用、ExpressionForest及TreeFlow，不增加writer或样本
  分支。唯一消费、未完成对象不逃逸、操作数及属性效果顺序继续独立核对；
  phased初始化准入不变。顺带修复原字面量测试对optional module guard的
  聚合初始化编译错误。
- 新解码夹具在旧规则确实于EvaluationPlan失败，
  /tmp/abcd-literal-construct-red-tests.log；修复后1/2/200/201/400/800个
  new/read/own-store初始化贯通共同树及printer。伪造nested proof被拒绝；
  独立ValueFlow有效的对象逃逸和共享构造结果图仍被拒绝。生成的完整函数
  经独立JS对照通过28个constructor/getter/Proxy顺序、异常和对象身份用例，
  并验证own descriptors和6种不可构造值，
  /tmp/abcd-literal-construct-behavior.log。
- 生产和相关测试目标构建退出0，/tmp/abcd-literal-construct-final-build.log；
  最终11/11定向测试通过，8.68秒，
  /tmp/abcd-literal-construct-final-tests.log。全量、RSS和基准保持暂停。
- 五输入build/stage-one/value-literal-construct-pass-1均退出0，维持
  **47 complete / 2 incomplete / 57 failure / 46 no-module**，无完整项退化、
  无legacy writer，/tmp/abcd-literal-construct-batch-summary.json。
- 真实PageA 7255的PC327构造现越过原拒绝点；继续在构造参数拒绝kLateValue。
  首个参数是PC270的buffered object allocation（op97，literal340），晚于
  外围allocation55，/tmp/abcd-literal-construct-pagea-next-operand.log及
  /tmp/abcd-literal-construct-pagea-aggregate-origin.log。
  整个record6897仍tree_stage失败，未宣称通过配对语义或表示验收。
- 下一步为共同嵌套聚合参数建立可独立核对的顺序/所有权证明，继续真实record
  发布和阶段一验收。Goal仍active且未完成；输入ABC和配对源码未改动。

### 2026-10-01：创建前模块初始化贯通正常子函数读取

- 真实 pasteboard PageA 入口7283在PC11写入local slot1（PageABuilder），
  然后在PC196创建7061；子函数PC44无guard读取该slot，原在PC46保存结果时
  被拒绝为kInternalValue，/tmp/abcd-pagea-file-debug.log。当前完整入口函数族
  ReadFunctionFamily已成功，不再靠配对源码或名字推断该读取的值域。
- FunctionInput每方法一次索引正常入口块的最早Language模块写入，查询实际
  后续创建点；排除未来、分支局部和入口块之外的写入。子函数保留原始read、
  slot、store、creator及creation PC；同file/profile/record与真实创建操作
  必须相符。有相邻原始guard时继续旧guarded路径，不据初始化事实删除guard。
- ModuleReadSite以typed absence表示已初始化的无guard读取。共同aliases、
  evaluation、ExpressionForest及TreeFlow保留单次读取效果和原始来源；有guard
  的读取仍保留两个事件。完整family写入检查独立核对父方法原始store及SSA
  Language输入；含继承初始化前提的detached root继续拒绝。当前仅支持直接
  normal父创建边，phase和祖父初始化传递仍未支持。
- 新完整read/return解码夹具先暴露kOrder：旧求值器要求每次module read都消费
  第二个guard事件。修复后1/2/200/201/400/800次无guard读取贯通共同AST、
  TreeFlow、metadata aliases及printer；独立JS验证9种值更新后的身份保持，
  包括-0、NaN、BigInt、Symbol、对象及Proxy，
  /tmp/abcd-module-initialization-behavior.log。该夹具不是整份PageA执行验收。
- 定向12/12通过，7.39秒，/tmp/abcd-module-initialization-final-tests.log；
  其余enclosure/创建点/类投影目标重新链接后5/5通过，4.64秒，
  /tmp/abcd-module-initialization-enclosure-tests.log。生产和相关目标构建退出0，
  /tmp/abcd-module-initialization-effect-build.log、
  /tmp/abcd-module-initialization-final-build.log及
  /tmp/abcd-module-initialization-enclosure-build.log。
- 最终五输入导出 build/stage-one/value-module-initialization-pass-2 均退出0，
  维持 **47 complete / 2 incomplete / 57 failure / 46 no-module**，无完整项
  退化，无legacy writer，/tmp/abcd-module-initialization-final-batch-summary.json。
  PageA record6897仍不完整；现在在另一类成员的子函数7255求值规划失败，
  BuildLiteralPlan返回kLateValue，/tmp/abcd-pagea-7255-literal-debug.log。
- 下一步解决共同对象初始化/求值顺序缺口，再验证真实record发布、语义和表示。
  阶段一Goal仍active；全量测试、RSS、基准和更大语料未恢复。

### 2026-10-01：可证明除法值域贯通共同正常/相位源码路径

- 真实EntryAbility成员5165确认为updateBreakpoint；LLDB定位kValues的具体
  拒绝为PC44的寄存器保存读取PC41 DIV2的Unknown结果，
  /tmp/abcd-entryability-5165-input-debug.log。属性和参数在ABC中仅有Language
  事实，不能借配对源码的数值类型补齐；当前仍拒绝该完整成员。
- FLDAI以原始binary64 magnitude区分有限非零、signed zero、infinity和NaN，
  保留既有impure阈值。DIV2仅在两个NonNaNNumber且至少一个有限非零时给出
  NonNaNNumber；copies/phis复用共同稀疏传播，未引入样本分支或writer。
  结果不保留有限非零前提，不抹掉user-code效果、异常边或正常Commit所有权。
- 固定commit的FastDiv源码确认非零除数路径直接封装double结果。本机探针
  复现0xfff7000000000005除以1得到0xffff000000000005，tagged编码为Hole；
  /tmp/abcd-quotient-nan-probe.log。这是native arithmetic/锁定源码证据，
  不是Ark VM运行结果；任意Language数值的准入没有放宽。
- 新回归在旧实现确实失败，/tmp/abcd-quotient-baseline-test.log；修复后4项
  定向测试通过，4.60秒，/tmp/abcd-quotient-final-tests.log。覆盖int/float、
  signed zero、infinity、subnormal underflow、正常和相位import/独立核对、
  mixed Phi及1/2/200/201/400/800链；7个完整生成函数经独立JS数值对照通过。
  构建退出0，/tmp/abcd-quotient-final-build.log。
- 五输入 build/stage-one/value-quotient-pass-1 均退出0，维持
  **47 complete / 2 incomplete / 57 failure / 46 no-module**，没有完整项退化、
  没有legacy writer；/tmp/abcd-quotient-batch-summary.json。当前5165仍kValues，
  /tmp/abcd-quotient-entryability-member.json。全量/RSS/基准保持暂停。
- 下一步仍需通用、可核对的数值来源事实或显式语义支持，以及spread展开/父类
  品牌合同和其余类成员/控制结构阻塞；配对语义和表示验收未闭合，Goal保持active。

### 2026-10-01：类成员缺口贯通部分record源码输出

- ValueSuperSpread现可经共同EvaluationShape保留构造器/数组两个用途和全部
  原有效果。CallableEntry仅在spread的constructor用途保留为未绑定协议输入；
  newTarget、unknown entry及任意普通读取仍拒绝。ExpressionForest在读取该
  VM-only输入前拒绝整个操作，不授权native super spread或数组参数改写。
- 显式AllowPartial接入既有类成员/构造器PreparedFamily和BuildClassTree；复用
  直线、无捕获leaf的prefix回滚及UnrecoveredStmt，未新增writer或补造函数体。
  原CompleteOnly接口继续拒绝缺口。部分成员无完整ABI/source证据，receipt停留
  BindingsFrozen；完整FamilyEmission在写入前拒绝，不能升级为完整发布。
- 共同printer将成员incomplete传播至类及module根，再经PrintedSource传到CLI和
  manifest。真实EntryAbility构造器保留constructor(...args)，在PC8标记整个
  rest/spread表达式缺口；不打印依赖其结果的字段写入、return或虚构super调用。
- 实际五输入导出 build/stage-one/value-partial-class-pass-1 为
  **47 complete / 2 incomplete / 57 failure / 46 no-module**，无完整项退化、无
  legacy writer；/tmp/abcd-partial-class-batch-summary.json。新增部分record：
  pasteboard EntryBackupAbility（5246）保留async onBackup/onRestore，pushkit
  EntryFormAbility（6921）保留6个回调及CreateFormData类；两者source_complete=false。
  输入ABC未改，未读取配对源码生成正文。
- 7项定向回归通过，6.12秒，/tmp/abcd-partial-class-tests.log；新增协议绑定
  正反例后binding_plan_test通过，0.78秒，/tmp/abcd-partial-class-binding-tests.log。
  2项既有派生构造器/继承JS行为回归通过，0.33秒，
  /tmp/abcd-partial-class-behavior-tests.log。两个新部分文件node --check通过，
  未执行部分代码；构建日志 /tmp/abcd-partial-class-regression-build.log。
- EntryAbility record4767仍失败。构造器之后的实际类成员5165在共同相位值分析
  kValues处拒绝，/tmp/abcd-partial-class-entryability-member.json；不能用部分
  constructor掩盖该成员。spread完整源码的数组展开/父类品牌合同、其余输入失败及
  配对语义/表示验收继续开放。全量/RSS/基准保持暂停，阶段一Goal保持active。

### 2026-10-01：spread-super 初始化分析接入共同函数族投影

- 修复恢复时未提交代码的SSA读取字段错误；range与spread调用复用已有正常流
  支配、寄存器/局部SSA和稀疏来源传播。spread额外检查当前构造器ABI槽身份；
  原range入口仍拒绝spread，不改变旧调用者合同。等同构造器来源phi可合并，
  混入newTarget、错误初始化guard、重复super及无效ABI槽均拒绝。
- ProjectDerivedConstructor从实际原始协议选择分析，并独立检查当前工作图的
  callable来源、两个用途及其owner、cache、效果与三个初始化guard输入。
  已进入现有BuildFileFamilyTrees调用路径，没有专用样本writer；工作phi的
  接受域未扩大，数组展开及父类品牌也未被初始化事实代替。
- 新增真实pasteboard EntryAbility创建点/构造器5083集成测试，PC18的spread
  初始化投影及全部3个guard通过；构造器操作数、数组角色、cache、guard输入与
  guard覆盖篡改均拒绝。临时使用HEAD旧投影构建同一测试，确实在kProtocol失败
  （退出-6），恢复实现后通过；/tmp/abcd-spread-projection-baseline-test.log。
- 原始解码回归覆盖1/2/200/201/400/800构造器copy、同源与混合phi以及旧range
  限制。3项定向回归全部通过，1.81秒，包含既有完整range构造器源码测试；
  /tmp/abcd-spread-projection-final-tests.log。生产与测试构建退出0，
  /tmp/abcd-spread-projection-restored-build.log；全量/RSS/基准未恢复。
- 五输入导出 build/stage-one/value-spread-projection-pass-1 均退出0，维持
  **47 complete / 59 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-spread-projection-batch-summary.json。EntryAbility record4767仍为
  tree_stage不完整，/tmp/abcd-spread-projection-entryability-record.json。
- 下一阻塞仍是共同EvaluationShape/展开数组AST、真实参数恢复及父类运行时品牌
  合同；当前明确拒绝ValueSuperSpread源码，不能据初始化投影宣布构造器、整个
  record或阶段一完成。语义和配对源码表示验收仍开放，Goal保持active。

### 2026-10-01：SUPERCALLSPREAD 贯通共同语义与值导入

- 五输入中的多个Ability类原在隐式派生构造器PC18拒绝SUPERCALLSPREAD。
  已将既有SuperSpread解码接入寄存器效果、SemanticOperation、语义值域和共同
  ValueIR导入；不再把已能解码的协议丢在语义提升入口。
- ValueSuperSpread独立于range super：显式保留acc中的当前构造器与寄存器中的
  参数数组两个用途，cache slot只是元数据；newTarget仍来自真实调用上下文。
  保留heap read/write、user code、allocation及MayThrow，只有正常完成写回acc。
  FunctionInput和相位输入不会在该协议之后沿用已初始化receiver的入口假设。
- 独立导入验证检查原始PC/操作/用途；篡改cache、交换操作数、改成单参数range
  call均拒绝，Hole参数数组仍在原始语义检查中拒绝。三种既有profile及
  1/2/200/201调用规模通过；简化保持调用与效果，未将数组误作一个JS实参。
- 9项定向回归全部通过，8.98秒，/tmp/abcd-super-spread-regression-tests.log；
  构建日志 /tmp/abcd-super-spread-regression-build.log。真实pasteboard
  EntryAbility构造器5083的--conditional-values已values_prepared=true、23操作，
  /tmp/abcd-super-spread-entryability-values.json。
- 五输入导出 build/stage-one/value-super-spread-pass-1 均退出0，维持
  **47 complete / 59 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-super-spread-batch-summary.json。EntryAbility record4767仍tree_stage
  不完整，/tmp/abcd-super-spread-entryability-source.json。
- 这不是super spread源码完成：EvaluationShape/表达式构建仍明确拒绝该协议，
  后续需贯通共同派生构造器投影、展开参数AST及实际初始化/返回协议；既有
  RuntimeSuperCallSpread对父类品牌的合同不能被native super(...args)直接替代。
  固定参考interpreter-inl.cpp:3901明确读取thisFunc=acc与array=vreg，使用已锁定
  commit的源码证据。全量/RSS/基准未恢复，阶段一Goal继续开放。

### 2026-10-01：Categories 数值加法边界的锁定源码与回归证据

- 已取得锁定ets_runtime commit f04900cf951c66c2ea18b2bab5b591d5336c34b9的
  interpreter-inl.cpp和js_tagged_value.h；ADD2数值路径直接构造tagged double，
  未见NaN规范化。链接与文件SHA-256见register-value-flow.md，未使用版本不同的
  本地checkout替代固定参考。
- 本机算术探针复现0xfff7000000000005加0成为0xffff000000000005，按固定offset
  编码为Hole。此为本机算术/源码合同证据，不是Ark VM运行结果。新增真实解码
  回归确认原FLDAI为Language、ADD2仍为Unknown，并在PC19返回处拒绝。
- register_value_flow_test构建与定向测试通过，4.34秒；
  /tmp/abcd-add-nan-build.log、/tmp/abcd-add-nan-tests.log。
  因此不能直接把任意ADD2提升为Language来放行Categories；需要可证明的数值
  来源细化或完整的对应语义支持。该真实record仍未完成。
- 本轮没有修改生产准入，没有重跑五输入，最近47/59/46基线不变；全量测试/RSS/
  基准仍暂停。阶段一仍需继续实现及独立配对/风格验收，Goal保持开放。

### 2026-10-01：类型化boolean结果的重复转换消除

- ExpressionForest对输入已被BooleanEvaluation证明为boolean的ToBoolean复用原
  表达式，并用既有CombinedOrigin保留转换来源；独立TreeOperands核对相同的
  类型化前提。任意语言值仍输出必要的!!，不通过printer字符串替换简化。
  每次转换仅常数检查并追加两个来源链接，未递归重走表达式树。
- 真实解码用例覆盖1/2/16/200/201层转换：getter值保留一组!!，严格比较结果
  不再增加!!。所有原始指令来源覆盖及缺失/篡改来源反例通过。52组独立JS对照
  覆盖假值、BigInt/Symbol、禁止强制转换的对象、getter次数/receiver和异常身份。
- 8项定向回归通过，9.69秒，/tmp/abcd-boolean-tests.log；加入来源覆盖后2项
  重点回归通过，4.59秒，/tmp/abcd-boolean-origin-tests.log。构建证据
  /tmp/abcd-boolean-final-build.log、/tmp/abcd-boolean-origin-build.log。
- 五输入导出 build/stage-one/value-boolean-conversion-pass-1 均退出0，维持
  **47 complete / 59 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-boolean-conversion-batch-summary.json。MiniGame实际输出的in比较已
  去掉一层冗余!!，node --check通过；外层条件/可选访问等表示仍未达到最终风格验收。
- Categories method18433的原始失败已确定为PC41返回值kInternalValue：PC38为
  未限定类型的ADD2，当前kStringAdd只在已知字符串前提下证明Language。
  /tmp/abcd-categories-next-detail.log、/tmp/abcd-categories-18433-instructions.jsonl。
  既有NaN位模式边界未放宽；本地运行时checkout与锁定commit不同，不能据其当前
  文件直接修改固定profile合同。数值加法能力仍待进一步取证和恢复。
- 全量测试/RSS/基准仍暂停；本轮改善源码表示，不代表配对平台或阶段一验收完成。

### 2026-10-01：未暴露环境中的延迟捕获初始化贯通

- 原始LocalAccessPlan新增每cell的unexposed initialization证据：同一块内新环境
  分配后只允许无效果ScalarAssignment、FunctionCreation和对该环境的LexicalStore。
  任何读取、调用、debugger、外部写入、控制边或其他操作终止此前缀；不会越过
  观察者重新开始。记录真实首个store，原始SSA的Language写入检查保持不变。
- NormalScopeFacts可据此接受创建早于store的Language捕获，但要求完整族的
  Language-only transitive writes；Defined和unknown writes仍拒绝。局部读取仍
  要求其读取点已初始化，未改相位/异常捕获合同。不是把未来store冒充创建点已初始化。
- 额外证据构建O(I+C)工作、O(C)存储，查询仍二分cell/store索引后常数检查；
  不逐捕获扫描指令。容量计入原LocalAccessPlan预算。没有新printer或样本特判。
- 真实解码正例贯通完整file family、共同树、捕获与写入验证、PreparedFamily及
  printer：先创建闭包，写入参数值，再返回该闭包。新增deferred_capture_source_test
  通过56组返回身份、重复读取、跨调用独立cell行为对照。1/2/200/201闭包绑定规格
  通过；debugger、外部槽写入、Defined、unknown writes及无前缀证据反例继续拒绝。
- 11项相关定向回归通过，8.26秒，/tmp/abcd-deferred-regression-tests.log；补齐
  完整源码/行为夹具后3项重点回归通过，1.41秒，/tmp/abcd-deferred-final-tests.log。
  构建日志 /tmp/abcd-deferred-final-build.log；全量测试/RSS/基准未恢复。
- 五输入导出 build/stage-one/value-deferred-capture-pass-1 均退出0，维持
  **47 complete / 59 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-deferred-capture-batch-summary.json。Categories越过原捕获边界，但仍在
  method18433（parent18321、creation PC72）的FunctionInputFailure/SemanticProofFailure
  失败，CLI报告PC41/tree_stage；/tmp/abcd-deferred-categories-failure.log。
  尚未恢复完整Categories或完成配对平台/风格验收，阶段一Goal保持开放。

### 2026-10-01：Categories 延迟初始化捕获的真实边界

- 当前HEAD上重新定位Categories record18101：父方法19017在指令9/PC20创建
  子方法18906；环境0的slot0直到指令18/PC47才首次写入。slot1、slot2分别在
  指令12、8写入。实际LocalInitialization也指向18，故不是区间索引丢失。
  证据 /tmp/abcd-categories-initialization-detail.log 和
  /tmp/abcd-categories-parent-instructions.json（逐行JSON）。
- 子方法18906在PC48读取depth0/slot0，PC50有原始检查，随后作为调用参数使用；
  /tmp/abcd-categories-child-instructions.json。必须保留读取和检查的实际执行
  时点；不能把未来初始化当作创建点的Language值证明。下一实现边界是捕获cell
  身份与调用时值域义务的区分，而不是删除ScopeFacts初始化条件。
- capture_plan_test新增未来store和同site store反例，即使给定覆盖当前块的
  initialization区间也拒绝；将store改回创建前后正例通过。构建
  /tmp/abcd-capture-future-build.log，定向测试通过，0.85秒，
  /tmp/abcd-capture-future-tests.log。本项仅固定回归边界，没有新增完整record，
  不声称已解决Categories；47/59/46导出基线未重跑。阶段一Goal继续开放。

### 2026-10-01：快照与逻辑组合的依赖范围修复

- MiniGame 方法12784的前置字面量调用不属于后续快照/逻辑对象参数的表达式树，
  旧的附加区间证明却检查整个块，导致op57的serial22/children.end20误拒绝。
  快照验证现在从选定比较消费者反向标记原inline依赖；逻辑Domain从最终消费者、
  选择分支和对象定义反向标记组合依赖。两处仍计入全部中间效果事件，未放宽连续
  求值、精确consumer、receiver或全局已移动操作的消费计数检查。
- Domain复用操作标记与清理列表，每个候选结束只清理已标记操作；额外存储O(O)，
  每条已选路线增加一次操作/用途逆序扫描，不逐producer遍历依赖图。原局部表达式
  验证保持不变，printer没有文本修补或样本分支。
- 真实解码的前置对象参数调用复现先失败，修复后完整共同树通过；新增117组前置
  调用/getter/receiver/参数身份/返回身份/抛出身份对照。伪造跨debugger快照仍拒绝。
  logical_regions_source_test共15856行为场景通过。
- 构建日志 /tmp/abcd-snapshot-domain-final-build.log；19项定向回归全部通过，
  16.25秒，/tmp/abcd-snapshot-domain-tests.log。全量测试/RSS/基准继续暂停。
- 五输入导出 build/stage-one/value-snapshot-domain-pass-1 均退出0，达到
  **47 complete / 59 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-snapshot-domain-batch-summary.json。新增Immersive MiniGame record12527，
  完整mjs通过node --check。此为完整性/语法证据，尚非独立配对平台行为或风格验收。
- 下一步继续剩余真实失败的通用恢复，包括Categories捕获初始化边界；保持初始化
  语义约束。可选访问表示等风格缺口仍存在，阶段一Goal保持开放。

### 2026-10-01：常量消除后的共享跳转尾部贯通

- 真实MiniGame方法12896的cross-entry已定位到block3：常量op15已是kRemoved，
  仅剩op16跳转。原规则要求活跃常量，故未拆分共享尾部；定位证据
  /tmp/abcd-minigame-tail.log。没有通过关闭交叉入口检查处理此问题。
- SpecializePureTails现在支持无phi/无效果的单跳转尾部，原始常量若存在仍遵循
  原用途限制。复制出边的phi输入，只有真实克隆的常量结果才重映射ValueId，
  其他ValueId/primitive操作数保持原值；不虚构结果、不复制getter/分配/环境效果。
  每条原指令来源继续保留，新增jump occurrence由已有来源表完整记录。
- 一次迭代状态遍历识别纯尾部环，环及进入环的纯链保持不变；1/2/16/200/201个块
  的纯跳转环连续两次简化均不增长。索引和输出容量均计入预算，实际新增操作/值
  分别计数，不为jump-only副本分配虚假ValueDefinition。复杂度见algorithms.md。
- 真实解码的“结果丢弃但getter仍执行”红例在修改前退出134，
  /tmp/abcd-empty-tail-red.log；现1/2/16/200/201串接规格通过。额外验证原参数值
  经过共享跳转尾部进入phi，46组可选读取/getter/抛出身份/返回身份行为对照通过。
- 构建 /tmp/abcd-empty-tail-final-build.log；20项定向回归全部通过，16.03秒，
  /tmp/abcd-empty-tail-tests.log；命名/合同更新后3项重点重跑通过，5.25秒，
  /tmp/abcd-empty-tail-final-tests.log。全量测试/RSS/基准仍暂停。
- 五输入导出 build/stage-one/value-empty-tail-pass-1 均退出0，达到
  **46 complete / 60 failure / 46 no-module**，无完整项退化、无legacy writer；
  /tmp/abcd-empty-tail-batch-summary.json。新增Immersive ShoppingFullScreen
  record13504，完整mjs通过node --check，尚未完成其独立配对平台行为验收。
- MiniGame现在越过原控制错误，但其后续函数在VerifyPredicateSnapshots的op57
  失败，serial22与children.end20不符，/tmp/abcd-empty-tail-next-snapshot.log。
  需要继续核对快照证明与同块其他表达式区间，不能删掉区间约束冒充恢复完成。
- 另一条已定位的失败为Categories闭包创建site9：depth0/slot0的kLanguage捕获
  在ScopeFacts返回kInitialization，/tmp/abcd-categories-scope.log。此初始化约束
  保持不变，不能以闭包稍后可能调用作为已初始化证据。两项均留待通用根因修复。
  当前可选访问仍为机械条件语句，风格验收未完成，阶段一Goal继续开放。

### 2026-10-01：普通条件语句的空 taken 臂消除

- 共同 FunctionTree 构建现在可以将非空 fallthrough 臂作为 then：原 taken 臂必须
  直接到合流点、没有控制节点且没有 phi 边赋值。按边建立复制索引，不逐分支扫描
  所有 bundle；新增工作/存储为 O(O+E+C+P)，计入原预算。选择表达式及异常相位
  分支不改变原方向，原始 CFG/边/指令保持不变。
- 类型化 BranchPolarity 传给表达式森林；对已证明 boolean 的输入选用相反谓词，
  对一般语言值在原 VM 谓词之外取反。false-or-zero 和 true-or-nonzero-number
  在一般 JS 值上不是互补关系，不能直接交换这两个谓词。不会重复 getter/强制转换。
- 独立 TreeFlow 从实际 AST 臂与原 control 推导极性，重新检查被省略边确无赋值；
  TreeOperands 验证相应谓词，来源验证仍覆盖条件与分支。printer 没有新增推断或
  文本替换。原先空 then/else 的位置回归已更新到实际非空 then 的 anchor。
- function_tree 的新空臂规格在原实现退出134，/tmp/abcd-branch-polarity-red.log；
  当前保留原边来源/作用域并通过语义检查。篡改条件运算符/来源/臂归属反例通过；
  真实解码 GuardedCopy 验证“空控制臂但有合流赋值”仍保留，未删除必要 phi copy。
  1/2/16/200/201 连续条件规格、384组谓词/getter行为及既有15739组逻辑行为通过。
- 最终构建 /tmp/abcd-branch-polarity-final-build.log；18项定向回归全部通过，
  16.29秒，/tmp/abcd-branch-polarity-final-tests.log。CommentItem完整模块的
  2422组独立配对构造行为继续通过。全量测试/RSS/基准仍暂停。
- 五输入导出 build/stage-one/value-branch-polarity-pass-1 均退出0，维持
  45 complete / 61 failure / 46 no-module，无完整项退化、无legacy writer，
  /tmp/abcd-branch-polarity-batch-summary.json。本轮改善表示，未新增完整record。
- CommentItem两处可选字段检查已去除空then/else，但条件仍有ToBoolean形成的
  双重否定及展开的可选访问；类命名包装和其他临时变量也未全部恢复为源码惯用形式。
  后续继续条件上下文的布尔表达简化、原生可选访问及其余失败record的通用恢复，
  源码风格/阶段一最终验收均未完成，Goal继续开放。

### 2026-10-01：尾部复制来源贯通及两个新增完整 record

- InstructionSources 改为按工作操作保留全部 occurrence，每项显式携带原始
  InputSiteId、独立 origin 和源码目标。原始 site 必须全部覆盖，操作与 origin
  一一对应，所有 retained/elided/signature 目标继续检查；不再以原始指令数量
  限制已证明的 primitive-tail duplication。变换合法性仍由已有变换及新鲜语义/
  流检查负责，映射不授予额外复制或消除权限。复杂度 O(I+O+P)，新增索引计入预算。
- FamilyEmission、JSON 位置输出和测试消费者已迁移；JSON instruction_sources
  项新增 site 字段，数组顺序是操作 occurrence 顺序，不能用数组下标代替原指令号。
  原始输入计数与 occurrence 数量分别保存；复制出的表达式/anchor 不被覆盖或丢弃。
- 真实解码 getter 选择 fixture 在修改前于来源检查失败（退出134），
  /tmp/abcd-source-occurrences-red-build.log、/tmp/abcd-source-occurrences-red.log。
  当前 1/2/16/200/201 层复制规格通过，并逐 occurrence 检查打印位置；缺失克隆目标、
  错误 PC、重复 origin、重复 placement 和精确资源/work 边界反例通过。
- 更新两个旧测试对纯常量消除的过时预期：默认参数 PC20/26 显式核对 elided_pure，
  常量 fixture 区分真实 intrinsic/ABI 节点与消除的 primitive copy，继续运行行为对照。
  24 项定向测试全部通过，10.62秒，/tmp/abcd-source-occurrences-final-tests.log；
  构建 /tmp/abcd-source-occurrences-final-build.log。全量测试/RSS/基准仍暂停。
- 五输入正式导出 build/stage-one/value-source-occurrences-pass-1 均退出0，达到
  **45 complete / 61 failure / 46 no-module**，无完整项退化、无 legacy writer，
  /tmp/abcd-source-occurrences-batch-summary.json。新增 Immersive ShoppingAvoid
  record11877 和 MultiCommunityApplication CommentViewModel record20523。
  两份完整 mjs 均通过 node --check；ShoppingAvoid 尚未完成平台行为验收。
- 新增 comment_item_source_test：引擎只读 ABC，独立测试从配对 TS 读取原构造器
  正文，排除无运行时写入的类型声明。完整生成模块的 CommentItem 导出通过2422组
  构造行为对照，覆盖真假值、属性身份/描述符、两次不同 getter 返回、异常对象身份、
  继承 setter 和 receiver。该结果不是完整应用或所有宿主上下文的等价证明。
- 输出仍有嵌套条件、空 then/else、合成临时变量与类命名包装；源码表示风格尚未
  通过。下一步继续共同 AST 的高层条件/可选访问恢复及其余61个失败 record，
  不能把 complete 导出计数或本次定向配对验证当作阶段一完成。Goal 继续开放。

### 2026-10-01：getter 快照进入严格谓词及消费者边界

- 新增类型化 kPredicateSnapshot：将多用途 named/computed getter 的私有快照赋值
  放在同块最早的严格比较用途，比较必须内联进入分支；后续用途仍读取同一私有符号。
  连续事件区间证明限制移动位置，独立前向验证核对用途、快照原因与消费计数。
  共享 AST 保留原 getter 表达式和来源，精确 UseId 对应赋值，流验证核对声明和唯一写入。
  没有重复 getter、丢弃快照或改变方法 receiver。
- 条件对象组合的额外事件证明截止于最后一个实际消费者，不再扫描随后无关的
  字面量组；后续操作仍由原局部验证覆盖。新增目标调用后另一次 getter/字面量
  调用的真实解码回归，核对两次调用的顺序、参数、结果身份与各位置异常。
- 2/3/16/64/200/201 层 getter 选择规格通过；debugger 屏障、缺失消费者、
  错误快照原因和篡改赋值目标的反例通过。关闭快照投影后已修正 fixture 的
  源码生成失败，/tmp/abcd-predicate-snapshot-red.log；这是功能禁用敏感性验证，
  不是 HEAD 基线结果。当前 15739 组逻辑表达式 JS 行为对照通过。
- 构建 /tmp/abcd-predicate-consumer-final-build.log；14 项定向回归全部通过，
  11.90 秒，/tmp/abcd-predicate-consumer-final-tests.log。源码证据
  /tmp/abcd-predicate-consumer-sources.js。全量测试、RSS、基准仍暂停。
- 五输入正式导出 build/stage-one/value-predicate-consumer-pass-1 均退出0，保持
  43 complete / 63 failure / 46 no-module；无完整项退化、无 legacy writer，
  /tmp/abcd-predicate-consumer-batch-summary.json。尚无新增完整 record。
- 真实 ShoppingAvoid 回调 12139 已越过调用、FunctionTree 和树语义检查，当前
  在 instruction_sources.cc 的来源数量检查拒绝，证据
  /tmp/abcd-predicate-consumer-verified-next.log。已有 primitive-tail duplication
  保留原指令来源但新增操作，旧指令 census 仍假定一个输入 site 恰好对应一个操作。
  下一步需要让来源映射完整保留同一指令的多个已证明 occurrence 及源码目标，
  不能仅删除数量/唯一性检查或丢弃克隆目标。该 record 仍未完整导出。
- 当前输出仍使用带私有快照的条件表达式；原生可选链及独立源码风格验收尚未完成。
  本次定向行为验证不构成阶段一配对源码最终验收，Goal 继续开放。

### 2026-10-01：条件结果经字段表达式进入对象参数

- 将对象初始化链从“条件值直接写入字段”扩展到条件值经过同块表达式树再写入。
  以staged definition为起点，按逆值顺序通过唯一的已固定RHS根及既有inline边
  建立字段归属索引；不逐选择重扫表达式树。新增O(I+U)扫描和O(I)预算内索引。
- 所选字段RHS原本的effect snapshot仍由BeforeLogicalFusion恢复并执行局部验证；
  独立前向区间证明核对实际内联消费者及连续事件，完整消费计数禁止伪造字段RHS。
  没有放宽普通存储Reference、共享值或插入效果的限制。
- 真实解码后缀红例在HEAD基线验证失败，/tmp/abcd-field-expression-red.log；
  现输出value.items({probe: 条件值 + "px" + "px", other: 条件值 + "px" + "px"})，
  /tmp/abcd-field-expression-source.js。字符串加法保持Symbol.toPrimitive调用、
  getter/转换/方法的顺序、转换异常及调用receiver；不扩大已有数字NaN-payload准入。
- 1/2/16/64/200/201表达式深度、201字段加后缀规格通过；运算与写入之间插入
  debugger、篡改RHS消费者、保留不匹配快照的反例均拒绝。13750组JS行为对照
  通过。构建 /tmp/abcd-field-expression-final-build.log；14项定向回归通过，
  14.64秒，/tmp/abcd-field-expression-tests.log。全量测试/RSS/基准仍暂停。
- 五输入正式导出 build/stage-one/value-field-expression-pass-1 均退出0，保持
  43 complete / 63 failure / 46 no-module，无完整项退化、无legacy writer；
  /tmp/abcd-field-expression-batch-summary.json。未新增完整record，不构成阶段一
  独立配对源码或平台最终验收。
- 真实ShoppingAvoid较长可选链的精确剩余缺口已确认，/tmp/abcd-shopping-prefix.log：
  op41是block0的kRoot/kMultipleUses getter；op57是block4的kRoot/kMultipleUses
  getter，op54为其inline读取前缀。内层op61/64已发现局部选择，但complete_phi=false；
  子header中的op57快照阻止外层吸收。需要证明这些多用途getter快照在条件表达式
  内的单次求值与剩余使用，不能直接删除快照或重复getter。op76字段后缀的通用
  融合机制已实现，但该回调仍需先完成上述完整选择恢复。Goal继续开放。

### 2026-10-01：多字段条件初始化链与对象参数贯通

- 将单字段对象区间扩展为多个相邻完整选择区域的初始化链。按原字段定义顺序
  通过header索引连接前一join与下一header，共用基本块只处理一次；对象区间
  在每次own-definition后扩展，最后由唯一外部用途消费。根选择按原操作顺序
  处理，避免后面的选择先占用共用join而阻断整个对象。
- 独立前向验证重新核对链、每个phi消费者、全部字段和最终对象消费；原局部
  快照/写入验证仍保留。索引与route存储复用并计入预算，不逐字段重扫整图或
  use-chain。森林到最后一个字段才构建共享ObjectExpr，保留每个DataProperty
  的写入PC和独立条件子树来源。支持重复属性名，不能消除被覆写字段的RHS副作用。
- 新增真实解码多字段红例 /tmp/abcd-object-chain-red.log，现可输出
  value.items({probe: 条件值, other: 条件值})；不同字段和同名重复写入均验证。
  2/3/16/64/128/200/201/256字段规格通过；插入debugger、漏掉首字段、伪造
  提前完成位置继续拒绝。11734组JS对照通过，新增两次getter顺序、两次属性值
  身份、方法替换、不同抛出位置、最终属性描述符和receiver检查。
- 构建 /tmp/abcd-object-chain-final-build.log；14项定向回归通过，14.10秒，
  /tmp/abcd-object-chain-tests.log。源码 /tmp/abcd-object-chain-sources.js。
  全量测试/RSS/基准仍暂停，规模规格属于正确性证据，不是性能实测。
- 五输入正式导出 build/stage-one/value-object-chain-pass-1 均退出0，保持
  43 complete / 63 failure / 46 no-module，无完整项退化、无legacy writer；
  /tmp/abcd-object-chain-batch-summary.json。未新增完整record，未完成配对源码
  或平台最终验收。
- 真实ShoppingAvoid回调的下一证据见 /tmp/abcd-object-chain-next-values.log：
  成员调用op121仍在CallPlan的kReference处拒绝，property来自block0/op34；
  当前logical_values/logical_objects均为空。block9/op76是ValueBinary，op77
  才是ValueOwnWrite。需要继续定位较长可选链的选择发现及字段前二元运算组合，
  不能把本次直接条件字段的成功视作该真实回调已完成。阶段一Goal继续开放。

### 2026-10-01：条件字段与原生成员调用对象参数贯通

- 在既有选择树和事件区间证明中，接入单个staged own-definition的缓冲对象：
  对象只有该初始化base用途及一个join外部用途，原分配、条件RHS和写入必须形成
  连续区间。逆向构建候选，独立正向核对区间及唯一消费；原局部验证仍恢复并检查
  multiple-use对象快照和原位置写入。独立常量检查继续要求原缓冲区已有可写own数据键。
- 共享表达式森林延迟到条件RHS可用时构建ObjectExpr，DataProperty保留原写入PC，
  条件子树保留分支来源；TreeFlow、TreeOperands及来源验证分别核对该映射。
  未通过移动调用、重新读取callee或调用call/apply/bind绕过成员Reference检查。
- 原先 --buffered-call-source 的真实解码拒绝例现输出原生
  value.items({probe: 条件表达式})；对象直接return亦无对象临时变量。
  迟读方法仍保持原字段初始化后再读取方法的顺序。源码证据
  /tmp/abcd-selected-object-sources.js；8354组JS行为对照通过，覆盖getter、方法
  替换、抛出对象身份、返回身份、属性描述符、receiver和被污染的调用辅助属性。
- 插入debugger、重复使用对象继续拒绝不合法提前成员调用；篡改投影尺寸、定义、
  消费者、initializer放置及属性来源均被拒绝。一次定义索引避免每个字段重复
  遍历对象use-chain，新增索引/持久表均计入预算，算法约束见algorithms.md。
- 构建 /tmp/abcd-selected-object-final-build.log；14项定向回归全通过，11.48秒，
  /tmp/abcd-selected-object-tests.log。新增属性来源反例后2项重跑通过，3.25秒，
  /tmp/abcd-selected-object-origin-tests.log。全量测试、RSS、基准仍暂停。
- 五输入正式导出 build/stage-one/value-selected-object-pass-1 均退出0，保持
  43 complete / 63 failure / 46 no-module，无完整项退化、无legacy writer；
  /tmp/abcd-selected-object-batch-summary.json。尚无新增完整record，本轮不是
  阶段一配对源码/平台最终验收。
- 下一缺口仍是多字段及连续选择域的对象组合。当前证明仅覆盖一个条件定义，
  不接受ShoppingAvoid那类多个条件属性作为已完成；原生?.及完整风格验收也未完成。
  阶段一Goal继续开放。

### 2026-10-01：嵌套选择树及可选访问调用参数贯通

- SelectionOperand 用类型化 SelectionChild 表达子选择，不伪造SSA值或代表边。
  控制森林迭代后序发现父子关系，每个子选择只有一个父选择；累计真实叶入口
  数量核对同一个phi的覆盖。子头部必须只有原条件的独占内联子树，额外语句、
  环境状态或局部phi阻止吸收。局部覆盖限制仍保留，不能替换其他未覆盖入口。
- 表达式森林、语句放置和独立TreeFlow均使用有界工作列表构建/核对父子选择，
  避免深层C++递归和逐节点全图搜索。完整树复用原前向/后向事件区间证明，
  可直接作为唯一join消费者的参数；子选择本身不单独取得phi替换资格。
- 对数组位置晚于join、但无操作数依赖的原始primitive-copy叶子，允许提前构建
  其AST，并在原臂中保留其语义位置和来源。读取、分配、调用及其他效果不走此路径；
  最晚非恒值依赖必须已经构建。修复先前共享常量出口复制到操作数组尾部的调度缺口。
- 最小可选访问现在直接返回嵌套?:，无if和phi临时变量；提前读取成员的方法调用
  现在可输出原生 value.items(条件值)，保持callee先读取、receiver及后续getter
  的顺序。源码证据 /tmp/abcd-selection-tree-sources.js。未使用call/apply/bind。
  尚未收拢为原生?.，源码表示质量仍有后续工作。
- 树组合红例 /tmp/abcd-selection-tree-red.log；调用集成红例
  /tmp/abcd-selection-call-red.log。真实解码2/3/200/201/402层嵌套选择通过；
  交换分支、篡改来源、子选择冒充完整phi继续拒绝。子头部debugger保留外层if，
  只组合安全内层。8250组解码JS对照覆盖不同短路返回、getter与方法变更、抛出
  对象身份、返回身份、可变调用辅助属性以及receiver。
- 14项定向测试通过（11.24秒），/tmp/abcd-selection-tree-final-tests.log；构建
  /tmp/abcd-selection-tree-final-build.log。新增子头部拒绝规格后2项重跑通过
  （3.26秒），/tmp/abcd-selection-header-tests.log。全量测试/RSS/基准仍暂停。
- 五输入正式导出 build/stage-one/value-selection-tree-pass-1 保持43 complete /
  63 failure / 46 no-module，无完整项退化、无legacy writer；汇总
  /tmp/abcd-selection-tree-batch-summary.json。没有增加完整record，未声称独立
  配对语义/风格或平台验收完成。
- 下一实际缺口是 buffered optional call 的对象字面量组合：可选条件本身及原生
  调用参数已打通，但分配/字段初始化还保留为独立语句，提前读取的方法引用仍拒绝。
  默认解码回归继续核对该kReference边界。阶段一Goal持续开放。

### 2026-10-01：共享多入口 phi 的局部条件选择

- 根据最小真实解码CFG（/tmp/abcd-optional-join-graph.log）确认：可选访问内层
  两条出口与外层短路出口共同进入三输入phi，且读取臂前有纯jump块。未改写原图
  或制造中间SSA值；SelectionRegion现在可以在原内层分支位置，对同一phi的两条
  输入生成条件赋值，同时保留其他入口的原始赋值。
- 以边索引读取唯一phi的输入，不再假定整个join只有两条输入。complete_phi仅在
  所有输入均被覆盖且拥有本地join时成立；Build/VerifyLogicalValues共同禁止把
  局部选择当成整个phi的表达式替换。控制路径只允许纯jump前缀，不跳过读取、
  debugger、环境状态或另一个phi；树构建与独立流验证保留并检查所有jump锚点。
- 最小可选访问现在输出外层if加内层原生?:，经共同树语义及JS对照通过。源码
  /tmp/abcd-nested-selection-sources.js。尚未形成整个嵌套条件表达式或原生?.，
  不宣称最终源码风格完成。新增外层null、内层undefined的不同返回规格，避免
  相同undefined掩盖入口遗漏；伪造完整phi融合、effectful corridor继续拒绝。
- 红例 /tmp/abcd-nested-selection-red.log；构建
  /tmp/abcd-nested-selection-final-build.log。14项定向测试通过（9.63秒），
  /tmp/abcd-nested-selection-final-tests.log；解码JS行为对照5507场景通过。
  2/3/200/201段可选访问均确认每段包含条件表达式，并继续检查交换两臂的拒绝。
- 五输入正式导出 build/stage-one/value-nested-selection-pass-1 保持43 complete /
  63 failure / 46 no-module，无完整项退化，无legacy writer；汇总
  /tmp/abcd-nested-selection-batch-summary.json。未把源码完整状态计为独立语义或
  风格验收，也未因内层恢复而宣称ShoppingAvoid完整。
- 下一步需要将父选择与子选择组合为同一条件值，再与对象初始化一起作为原生
  调用参数。当前提前读取成员的buffered optional call仍由kReference拒绝，
  不能用本次内层表达式恢复代替该目标验收。阶段一保持开放；未恢复暂停的
  全量测试、RSS、基准或更大语料工作。

### 2026-10-01：共同条件表达式与成员调用参数贯通

- FunctionTree 新增原生 ConditionalExpr，结构检查按 condition/consequent/alternate
  独立拥有三个孩子，共同迭代 printer 输出带括号的 ?:。没有专用文本后端或调用辅助函数。
  原短路区域记录推广为 SelectionRegion；实际双臂选择由原正常控制图、单结果phi
  及两条原始输入边识别。每条臂必须是单一独占求值子树，不能带额外语句或环境效果。
- 条件值限定为已证明返回boolean的 ToBoolean/LogicalNot/严格比较；原VM的
  false-or-zero分支不能直接当JS truthiness，新增原始非boolean分支拒绝收缩规格。
  两条臂及结果均保留原操作来源。独立TreeFlow核对条件、两条输入、phi去向和边界，
  TreeOperands核对条件操作数；调换生成树的两条臂会被拒绝。
- 复用现有前向/后向事件区间证明，把原生条件值组合到唯一join消费者，支持
  “先读取成员方法，再计算条件参数”的原生调用，并验证callee/receiver配对。
  没有删除CallPlan检查，也没有call/apply/bind或IIFE绕行。解码回归同时覆盖
  先读/后读方法，两种分支方向、getter改变后续选择、异常身份、返回身份和接收者。
- 真实解码规格先观察FunctionTree红例 /tmp/abcd-conditional-decoded-red.log；
  调用集成红例 /tmp/abcd-conditional-call-red.log。2/3/200/201段条件选择、
  1/200/201/402层源码树及分支内debugger拒绝收缩通过。14项定向测试通过
  （10.39秒），/tmp/abcd-conditional-final-tests.log，构建
  /tmp/abcd-conditional-final-build.log；后续两个拒绝边界测试重跑2项通过（2.78秒），
  /tmp/abcd-conditional-negative-tests.log。解码JS对照5468场景、AST/printer对照
  18928场景通过；后者仅证明源码节点组合，不作为ABC识别证据。
- 五输入正式导出 build/stage-one/value-conditional-pass-1 保持43 complete /
  63 failure / 46 no-module，无完整项退化、无legacy writer，汇总
  /tmp/abcd-conditional-batch-summary.json。阶段一仍未完成，未扩大语料或恢复
  全量测试/RSS/基准。没有新增完整record不能抹去本次共同条件表达式能力交付，
  也不能把本组件完成计为阶段完成。
- 下一实际缺口仍是 buffered optional call：可选访问共享多条phi输入，尚不能
  分层组合为条件值，分步对象初始化也尚未成为单一参数子树。显式
  --buffered-call-source 目标仍未通过，默认回归继续核对其kReference拒绝边界。
  后续继续扩展共同区域组合，再收拢为自然对象字面量和原生可选链表达。

### 2026-10-01：成员引用与可选参数的最小解码集成回归

- 将 ShoppingAvoid 的下一阻塞缩减为动态ABC规格：成员读取、buffered object
  分配、严格null/undefined分支、属性初始化、带receiver调用。使用同一生成器
  对照“先读方法”和“后读方法”，不依赖应用、类、捕获或配对源码。
- 后读方法版本可经共同管线生成并执行；新增104组行为对照，覆盖参数getter、
  方法getter及调用异常、receiver、属性描述符、返回身份，以及getter改变后续
  方法选择。call/apply/bind访问器设为抛异常，排除通过可变调用辅助方法绕行。
  总计1074场景通过；独立敏感性对照证明移动方法读取会改变实际调用对象。
- 先读方法版本的 ControlPlan/EvaluationPlan 均可构建，BuildCallPlan 明确返回
  kReference。普通回归保留此拒绝边界，防止仅删除检查就发布错误源码。
  显式开发探针 `build/logical_regions_test --buffered-call-source` 仍为红例，
  在要求完整 FunctionTree 时失败（/tmp/abcd-buffered-call-red.log）。此拒绝测试
  不是目标能力通过；实现完整组合后应升级为成功与行为对照。
- 当前缺口包含两个关联环节：可选访问的条件值尚未作为高层表达式组合，分步
  对象初始化尚未与调用参数形成单一求值子树。现有 LogicalRegion 仅识别
  ToBoolean短路diamond，不能直接覆盖严格null/undefined分支。下一步在共同
  表达式/求值组合中解决两者，再由独立顺序验证允许提前取得的成员Reference。
- 4项定向测试通过（1.75秒），/tmp/abcd-buffered-call-tests.log；构建
  /tmp/abcd-buffered-call-build.log；行为日志 /tmp/abcd-buffered-call-js.log。
  本次仅增加可复现回归和验证，未扩展生产接受域，未重新执行五输入导出；最近
  43/63/46计数不变。阶段一仍未完成，全量/RSS/基准/更大语料仍暂停。

### 2026-10-01：跨控制流缓冲对象初始化保留原始求值位置

- 修正上一条定位：ShoppingAvoid op77 的接收者已正确解析为 op36 的对象分配，
  实际拒绝来自 LiteralPath::Before 要求同一块，而不是接收者 phi。
  证据 /tmp/abcd-literal-receiver.log。分配位于块0，字段定义位于块9。
- LiteralPlan 增加类型化 statements 形式：保留 buffered object 分配及每次写入的
  原始位置，正常 DAG 中用拓扑序与支配树证明最终写入前对象没有其他观察/逃逸。
  树构建与独立操作数验证均使用排序的缓冲键索引，证明目标已经是自有可写数据属性，
  才将 own definition 表达为赋值，避免原型 setter。没有移动 getter、分配或异常。
  循环、非缓冲对象、新增键以及提前观察继续拒绝。
- 嵌套字面量验证允许多块函数中的块内组合；直接消费者必须同块，跨块边界时
  嵌套栈必须为空。此项不允许嵌套表达式跨控制流边移动。
- 新增真实解码 buffered object + optional access 回归，及提前观察、篡改映射、
  缓冲缺键的拒绝检查。独立 JS 对照达到970场景，新增属性描述符、getter异常、
  对象身份和继承 setter 检查。红例 /tmp/abcd-staged-red-test.log；构建
  /tmp/abcd-staged-final-build.log；12项定向测试全部通过（8.37秒），日志
  /tmp/abcd-staged-final-tests.log。未执行全量测试、RSS或基准。
- 五输入正式导出 build/stage-one/value-staged-buffer-pass-1 合计
  43 complete / 63 failure / 46 no-module；新增 Immersive record25083 与
  MultiCommunity record41552，无已完整项退化，legacy writer 为0。汇总
  /tmp/abcd-staged-buffer-batch-summary.json。完整状态不等于独立语义/风格验收。
- ShoppingAvoid 已越过 LiteralPlan，下一处实际阻塞为 CallPlan::VerifyCalls
  kReference：参数经过条件控制流时，需保留此前成员读取的 callee 和 receiver。
  证据 /tmp/abcd-staged-next-block2.log；该 record 仍 unproved/tree_stage。
  分步初始化是当前正确性推进，不代表原生对象字面量/可选链表示质量已完成。
  阶段一 Goal 持续开放；既有暂停事项保持不变。

### 2026-10-01：共享常量出口拆分，贯通可选访问的控制图

- ShoppingAvoid的真实12139包含连续严格null/undefined判断，共享undefined出口。
  原BuildControlPlan在Regions报kCrossEntry（/tmp/abcd-shopping-avoid-block.log）。
  新增不依赖类/捕获/配对源码的动态ABC最小可选访问规格，先观察共同Source构造
  的红例（/tmp/abcd-optional-red-test.log）。
- SimplifyValues在已有别名/常量/phi验证后，对共享常量出口进行受限tail duplication：
  仅接纳无phi、恰好一个language primitive copy及一个无效果jump的块，定义只能
  供该出口的phi输入使用。保留一条原入边，其余入边各自拥有常量/jump副本，并为
  后继每个phi复制对应输入；原始块/操作/PC不删除，副本保留来源。统一重建用途链，
  验证SSA可用性与用途，原子提交并推进IR/CFG revision。getter/call/environment
  不复制；含debugger的共享出口继续拒绝。只实现正常图，未扩大异常/恢复域。
- 源码现在可通过共同控制、求值、树语义和printer生成单层/两层可选读取的等价
  条件结构；尚未恢复原生?.语法，不能视为表示质量验收完成。保留所有非空值，
  getter的receiver、读取次数、返回身份及异常对象与独立?.参考一致；通用JS
  行为对照扩展至942场景。2/3/200/201段、重复简化幂等性与效果负例通过。
- 7项相关定向测试通过（6.36秒）：logical_regions及JS、value_simplify、control_plan、
  phi_copies、index_record及initial_render。日志/tmp/abcd-optional-final-tests.log；
  构建/tmp/abcd-optional-final-build.log。前一组六项也包含class_initializer_file。
- 五输入正式导出build/stage-one/value-constant-joins-pass-1仍41 complete/
  65 failure/46 no-module，无完整项退化/legacy writer；汇总
  /tmp/abcd-constant-joins-batch-summary.json。ShoppingAvoid已越过控制图构建，
  后续在LiteralPlan::Allocation拒绝经phi传递的buffered object接收者（op77，
  /tmp/abcd-optional-next-block.log）。接着处理这一共同字面量集成缺口。
- 阶段一未完成。全量测试、RSS/基准和更大corpus仍暂停；未声称资源实测或平台验收。

### 2026-10-01：模块入口全局上下文贯通，新增七个完整 record

- BuildRecordTree 对入口自由全局名复用已建立的完整模块作用域：imports、模块
  locals、入口词法声明与类/子函数采用同一 enclosure；不再把 nullptr 传给
  VerifyTreeGlobals。未虚构 realm/Symbol/arguments 宿主能力，既有名称和遮蔽
  检查保留；无新增宿主前提、白名单或样本特判。
- 动态ABC模块回归新增未被遮蔽的全局读取，以及与import/local同名的拒绝规格。
  先观察 record error 6 红例（/tmp/abcd-record-global-red-test.log）。独立模块
  行为对照覆盖全局getter、副作用顺序、undefined/null/对象值、抛出对象身份和
  缺失名称 ReferenceError；Node contextified global 抛异常时会重试getter，故
  对比两份独立程序在同环境的trace，未把宿主模拟行为当成Ark规范。
- 完整Index record9572首次通过共同函数族、原生extends、全部成员和模块入口
  输出。新增 index_record_source_test 通过ABC-only正式工具取得整份源码后，
  才在独立Node进程读取配对Index.ts并去类型，36组整模块/构造器/成员行为对照
  通过：导入顺序、路由注册、finalize存在/补充两种情况、原型链、new.target、
  属性访问器、路径跳转、依赖清理及异常身份。原initialRender720组和NavPageMap
  定向测试继续通过；这些均为受控框架证据，不是设备/整App验收。
- 13项相关测试通过（11.48秒），日志/tmp/abcd-record-global-final-tests.log；
  构建日志/tmp/abcd-record-global-final-build.log。未执行全量测试/RSS/基准。
- 五输入正式导出 build/stage-one/value-record-global-pass-1：Immersive19/16/10，
  MultiCommunity20/25/9，pasteboard1/9/9，Push模块1/14/9，widgets0/1/9
  （依次complete/failure/no-module），合计41/65/46，无完整项退化或legacy writer。
  新增9572、11285、15128、15617、39704、40430、7313，均通过Node语法检查；
  汇总/tmp/abcd-record-global-batch-summary.json。source_complete是源码恢复状态，
  不等同于语义/风格已全面验收，正式工具仍明确 executable=false。
- Index输出仍有空分支、多余布尔翻转和临时变量；风格验收未完成。其他65个失败
  record仍需贯通，例如ShoppingAvoid11877报告unproved/tree_stage。阶段一Goal
  保持开放，继续共同功能恢复与独立配对验收，不转入更大corpus。

### 2026-10-01：共同 ClassExpr 贯通原生继承表达式

- ClassExpr 将 heritage 作为外层 FunctionTree 拥有的表达式，沿用共同求值顺序、
  操作数对应和节点所有权验证。Hole 表示无 extends；实际 null 保留为 extends null。
  绑定真实 Base/Derived callable，不根据类名或样本路径判断。语义依据为 pinned
  v7 RuntimeSetClassInheritanceRelationship：父类构造能力、prototype 读取与异常、
  静态/实例原型链均由原生 class extends 表达。
- printer 使用已有迭代动作栈先输出外层 heritage，再组合既有类体及成员位置。
  无独立 heritage 的公开 PrintClassTree 仍拒绝派生构造器；内部类体路径只供
  完整 ClassExpr 组合。保留计算属性 NamedEvaluation，避免虚构遮蔽模块绑定的
  内部类名。新增一次表达式子节点访问，未引入图扫描或独立 writer。
- 新回归以实际 Index 派生构造器组成最小创建图，覆盖共同树构造/验证/打印；
  删除或替换 heritage 的篡改被拒绝。该 fixture 不含 Index 其他成员，不当作
  整类完成证据。初始构造红例 /tmp/abcd-heritage-red-test.log 已观察。
- derived_class_heritage_source_test 经真实 ClassExpr 创建类，264组构造行为对照
  通过，并覆盖 extends null、非法父类、prototype 单次读取和异常身份、静态/
  实例继承及重复创建身份。Proxy 仅验证类定义时的 prototype 读取，不扩大既有
  super range 的 Proxy 支持域。8项定向测试全部通过（5.12秒），构建与测试日志
  /tmp/abcd-heritage-verified-build.log、/tmp/abcd-heritage-verified-tests.log。
- 五输入正式导出 build/stage-one/value-class-heritage-pass-1 仍为34 complete/
  72 failure/46 no-module，无完整项退化或legacy writer；汇总
  /tmp/abcd-heritage-batch-summary.json。下个阻断是 record_tree.cc 对真实模块入口
  调用 VerifyTreeGlobals 时仍传 nullptr（/tmp/abcd-heritage-next-block.log）。
  阶段一未完成；全量测试/RSS/基准仍暂停。

### 2026-10-01：逻辑参数与前置成员callee贯通，initialRender完整函数族生成

- 新增normal逻辑值求值组合：已验证单入口菱形的RHS仍由原条件控制，将其作为
  一个条件事件与header/join效果序列一起规划。只消费单一join用途的逻辑phi；
  逆序选择连续效果后缀，独立验证正序子区间无缺口/重叠。原局部森林按融合前
  位置重新验证，原RHS、scope/environment边界、cell guard和copy证明均保留。
  不借助call/apply/bind替代成员引用，也没有仅放宽跨块callee检查。
- kLogicalValue及ValueId索引接通共同绑定、森林、控制、边输入和操作数校验；
  合流值不再声明phi变量，LogicalExpr在join消费者构造前生成，条件分支仍保留
  原PC来源和jump锚点。后置getter不能把逻辑参数后移；错误consumer、缺失/
  错误phi映射、强制后置getter融合及工作/资源欠额规格均拒绝。
- 当前选择非重叠header/join区域，依赖位于join之前的已有定义布局；其他布局、
  相邻重叠组合仍走原phi路径，尚未宣称任意嵌套逻辑表达式均已恢复。每区域块/
  用途互不重叠或受控制结构约束，额外扫描/存储线性并计费，另含现有ControlPlan
  构造/验证成本；没有逐callee重复扫描参数或全图。
- 真实10510的PC46成员callee、PC66左值、PC72/75条件及PC77数组现在共同进入
  PC106调用；完整initialRender(10647)及全部子函数经PreparedFamily和实际模块
  enclosure成功构造/打印。输出证据/tmp/abcd-logical-value-render-source.txt，
  约9.5KB。新增initial_render_source_test仅在独立Node进程读取配对Index.ts，
  引擎输入始终仅ABC；720组受控框架对照通过，覆盖children假值/数组/对象、
  同步/延迟回调、index分支、getter修改方法选择、JSON key、点击闭包后续字段
  变化、bound builder、receiver及异常。此为完整方法族的受控验收，不是设备/整App验收。
- 通用动态ABC逻辑用例扩展至708行为场景。13项定向回归全部通过（12.77秒），
  含原十项、call_plan_test、phase_reference_test及initial_render_source_test；
  日志/tmp/abcd-logical-value-verified-tests.log，构建日志
  /tmp/abcd-logical-value-verified-build.log。真实guard重复输入曾在新的独立区间
  校验中暴露并修正，原guard/cell证明未放宽。
- 五输入正式导出build/stage-one/value-logical-value-pass-1仍34 complete/
  72 failure/46 no-module，无完整项退化/legacy writer，mjs最终文件差异为0；
  汇总/tmp/abcd-logical-value-batch-summary.json。整record仍未完成。
- 最新实际阻断已越过函数族：Index入口10009在ExpressionForest的class creation
  分支拒绝非Hole heritage（/tmp/abcd-logical-value-next-block.log）。下一步须给
  共同ClassExpr接通真实extends表达式及其求值/receiver/来源校验，再处理派生类
  打印约束，不能直接删除Derived打印保护。阶段一继续；全量/RSS/基准仍暂停。

### 2026-10-01：逻辑左值消除重复SSA观察快照

- 正常EvaluationPlan新增kLogicalLeft：只有同一header中的真实producer，其全部
  活用途恰好是ToBoolean输入及原值保留边phi输入，且所属逻辑区域/RHS可完整
  组合，才把两个SSA观察交给一个原生&&/||左值。producer至判断之间存在效果或
  环境变化即保留快照；已验证的初始化cell guard保持原锚点。没有放宽跨块callee。
- 规划和验证分别从当前IR/ControlPlan核对用途与区域，原求值DFS仍展开左值完整
  子树并验证原效果顺序。共同森林/TreeFlow/TreeOperands传递同一实际表达式，
  原keep边和布尔输入分别检查它；保留来源，不复制getter或去掉真实消费者。
  无候选ToBoolean时跳过额外控制构造；有候选时包含一次现有ControlPlan成本，
  其余索引、互不重叠header区间和用途链总量线性，均计入既有资源/工作预算。
- 动态ABC正式共同路径现生成$phi = (value.items || [])及&&形式，省去$value
  快照；真实10510/PC66在完整children/capture前提下确认kLogicalLeft，PC77
  仍kPhiInput。合流phi尚未作为后续调用的原生表达式输入，前置callee仍拒绝。
- 新增额外消费者与真实DEBUGGER屏障负例、错误consumer/伪造普通inline/强制
  logical-left负例，以及1/200/201/402左值区域和201区域存储/工作欠额测试。
  JS行为对照扩展至500场景，含共享左值作为第二参数时只读取一次、对象身份、
  receiver、异常顺序、方法替换及可变call/apply/bind拒用。
- 十项定向回归通过（8.27秒）：logical_regions_test、logical_regions_source_test、
  evaluation_plan_test、phi_copies_test、phi_call_test、phi_placement_test、
  function_tree_test、class_initializer_file_test、derived_constructor_source_test、
  navmap_source_test。新增预算规格后两项逻辑测试再次通过（1.87秒）。日志
  /tmp/abcd-logical-left-final-tests.log、/tmp/abcd-logical-left-budget-tests.log；
  最终生产工具构建日志/tmp/abcd-logical-left-final-build.log。红例先确认旧实现
  仍打印$value，证据/tmp/abcd-logical-left-red-test.log。
- 五输入正式导出build/stage-one/value-logical-left-pass-1仍34 complete/
  72 failure/46 no-module，无完整项退化或legacy writer；对比前轮mjs差异为0。
  汇总/tmp/abcd-logical-left-batch-summary.json。后续仍需把合流phi与header
  callee一起纳入join调用的求值顺序规划；整类/阶段一验收未完成，全量/RSS/
  基准继续暂停。

### 2026-10-01：跨逻辑参数的成员调用建立独立顺序回归

- 将10510的阻断收敛成无捕获、无类、无模块依赖的动态ABC最小规格：同一个
  obj.probe成员读取分别位于obj.items的&&/||菱形之前和之后。两种形式均能
  识别完整逻辑区域；前置callee仍是kCrossRegionValue，BuildCallPlan返回
  kReference，强改kInline被VerifyEvaluationPlan拒绝。这明确是待修复的
  集成缺口；规格记录当前拒绝不代表阶段一接受拒绝结果。
- 后置callee形式已经通过导入、简化、共同树语义验证及打印，实际输出先执行
  items getter/逻辑赋值，再执行probe getter/成员调用。新增运行时对照覆盖
  items getter修改方法选择、两处getter及调用分别抛出同一异常、receiver、
  参数/返回对象身份，以及callee.call/apply/bind被替换为抛异常getter。
  前置/后置参考程序的敏感性负对照确认顺序交换确实被检测，不能仅比较返回值。
- 两项定向测试logical_regions_test/logical_regions_source_test通过（0.60秒），
  生成代码行为场景从182扩展为396。构建与测试日志分别为
  /tmp/abcd-logical-call-gate-build.log和/tmp/abcd-logical-call-gate-tests.log；
  生成源码/tmp/abcd-logical-call-sources.js。此次仅新增测试/定位证据，没有修改
  生产恢复能力，没有重跑五输入导出或提升34/72/46覆盖；全量/RSS/基准仍暂停。
- 下一实施边界明确为联合规划：消除逻辑left的重复SSA观察所需快照，将phi作为
  join调用的表达式输入，并在同一顺序证明中包含header callee与join参数。
  当前仅把phi内联到call会把前置callee移到items之后；仅放宽CallPlan不安全。
  此最小规格应在实际实现完成时改为原生obj.probe(obj.items || [])/&&成功验收。

### 2026-10-01：正常控制分支贯通原生逻辑表达式输出

- FindLogicalExpressions将现有区域识别与已验证求值森林接通：只选择ToBoolean
  单一分支消费者、同一保留值、单phi以及完整且独占的RHS子树；独立根、debugger、
  环境变化和非空保留臂不合并。原检测索引/选择数组接入调用者现有存储和工作计费，
  无新增DFX或证明收据。输入求值表尺寸/revision不匹配时不进入索引。
- ExpressionForest直接构造共同LogicalExpr和原phi赋值，省去两个分支copy语句及
  独立布尔谓词表达式，不在printer推断CFG。共同TreeFlow重新检查实际语法、
  两条输入边及phi目标，TreeOperands检查左右真实值与操作，来源验证保留布尔/
  分支的CombinedOrigin、RHS原PC和jump边界锚点。异常相位路线仍保持原处理。
- 动态ABC fixture经完整导入、简化、求值、共同树语义检查与打印后，实际输出
  $phi = (value || [])、$phi = (value && value.probe())等原生表达式；仍保留合流
  临时变量，尚未将逻辑值内联到后续调用。182组JS行为对照通过，包括假值、对象
  身份、getter/成员调用receiver、异常和短路；200/201/402连续区域继续通过。
- 新增错误逻辑运算符/错误RHS负例、带真实DEBUGGER字节码的拒绝合并用例（保持
  if及debugger）、201区域的有限存储/工作边界及损坏求值表负例。实际10510在
  完整FileFamily的children/capture前提下确认唯一可组合kOr区域；完整方法仍在
  森林构造之前受到成员callee跨块引用检查阻断，没有宣称该方法已经打印成功。
- 十项定向回归通过（5.17秒）：logical_regions_test、logical_regions_source_test、
  evaluation_plan_test、phi_copies_test、phi_call_test、phi_placement_test、
  function_tree_test、class_initializer_file_test、derived_constructor_source_test、
  navmap_source_test。日志/tmp/abcd-logical-compose-tests.log，构建日志
  /tmp/abcd-logical-compose-export-build.log；abc_structure/abc_semantic已重建。
- 五输入正式导出build/stage-one/value-logical-compose-pass-1仍34 complete/
  72 failure/46 no-module，无完整项退化、无legacy writer；对比上一轮生成的mjs
  文件，实际差异数为0，不能用fixture进展冒充整个样本集覆盖提升。汇总
  /tmp/abcd-logical-compose-batch-summary.json。
- 下一步接通逻辑phi值作为后续调用参数的求值组合，同时保留callee getter先于
  参数的顺序；不能仅放宽CallPlan的跨块限制。整类继承、阶段一及配对源码最终
  表示质量验收仍未完成，全量/RSS/基准仍暂停。

### 2026-10-01：phi右值表达式进入正式求值与源码通路

- normal EvaluationPlan新增kPhiInput：已验证的单copy/无cycle临时量bundle、唯一
  值消费者、同一单出口块，并且producer之后只有无效果jump/nop，才能把末尾
  表达式直接移入phi赋值。后续getter、store、环境变更、debugger等仍是屏障。
  各候选源块唯一，尾部检查总量线性；phased/异常拷贝路径未开放此变换。
- 求值验证复核producer/consumer及尾部条件，仍展开原子树检查全部效果顺序。
  ExpressionCopies直接接收表达式所有权；共同树省去额外快照/声明。TreeFlow、
  TreeOperands独立核对赋值边、producer表达式与来源，原PC继续保留；catch/stage
  仍要求原有边来源叶值。没有合并控制边，也没有放宽跨块成员调用。
- 从动态ABC字节码导入的逻辑分支现在实际输出$phi = []或$phi = value.probe()
  等表达式，省去$value中转；已通过共同树语义检查、打印及182组JS执行对照：
  &&/||、假值、NaN/-0、BigInt/Symbol、对象/数组身份、getter/成员call的receiver、
  异常与短路。树构造覆盖200/201/402连续区域；错误consumer、删除实际RHS和
  在数组创建后加入真实DEBUGGER字节码的强行融合负例均拒绝。
- 真实10510的PC77数组在完整FileFamily/实际children绑定下确认为kPhiInput，
  没有临时快照。十项定向回归通过（4.38秒）：logical_regions_test、
  logical_regions_source_test、evaluation_plan_test、phi_copies_test、phi_call_test、
  phi_placement_test、function_tree_test、class_initializer_file_test、
  derived_constructor_source_test、navmap_source_test。新增成员call规格后两项逻辑
  区域测试再通过（1.56秒）。日志/tmp/abcd-phi-expression-tests.log及
  /tmp/abcd-phi-expression-call-tests.log；最新构建含abc_structure/abc_semantic。
- 五输入正式导出build/stage-one/value-phi-expression-pass-1仍34 complete/
  72 failure/46 no-module，无完整项退化、无legacy writer；汇总
  /tmp/abcd-phi-expression-batch-summary.json。没有将manifest指标当作语义验收。
- 下一步把LogicalRegion保留边与已内联RHS组合为共同LogicalExpr，接通phi/control
  消费，再处理原callee在参数分支之前读取的跨区域调用。当前仍输出if及phi变量，
  10510完整恢复、整类继承和阶段一仍未完成；未恢复全量/RSS/基准。

### 2026-10-01：依据真实IR识别逻辑短路值选择区域

- 更正上一条交接对10510的判断：PC72的5115是固定ISA中的CALLRUNTIME_ISTRUE，
  导入为ValueUnary::kToBoolean；PC75为JNEZ/TrueOrNonzeroNumber。真分支保留
  PC66读取的原值，假分支执行PC77数组创建，合流phi20。因此这里是||，不是??。
  原始IR定位记录/tmp/abcd-short-circuit-ir.txt；上一轮??共同树能力保留，但不作为
  此真实分支的恢复依据。定位过程未使用配对源码。
- 新增FindLogicalRegions，直接消费已验证normal ValueProgram与ControlPlan，
  通过块摘要、单phi与输入边索引发现&&/||值选择菱形。支持直接保留边及空保留块，
  根据实际分支极性选择运算符；要求ToBoolean单一消费者、纯谓词、Language左值/
  合流值、精确保持同一SSA输入，拒绝含效果的保留分支、其他谓词及额外phi。
  时间O(ops+values+uses+control nodes)，无逐分支全图扫描；没有改动原CFG。
- 真实initialRender完整FileFamily中的10510现在由集成规格确认唯一kOr区域，
  对照原PC66/72/75/77及实际phi输入。合成规格从动态ABC字节码导入/简化，覆盖
  &&/||、三块/四块形式、1/200/201/402连续区域；负例覆盖typeof替代、非空保留臂、
  不同保留值、Hole、谓词逃逸及过期revision。
- 七项定向测试通过（3.96秒）：logical_regions_test、logical_tree_test、
  control_plan_test、class_initializer_file_test、logical_tree_source_test、
  derived_constructor_source_test、navmap_source_test。构建/测试日志
  /tmp/abcd-logical-region-final-build.log及/tmp/abcd-logical-region-tests.log。
- 这是实际IR上的区域识别算法，尚未把区域纳入EvaluationPlan/ExpressionForest、
  消除phi copies或改变正式源码。下一步必须接入RHS表达式所有权、控制边消费和
  跨区间效果顺序；在此之前不能把识别结果当作callee跨块内联权限。
  新识别器目前只由规格调用，生产接入时须随owner一起计入临时索引/输出存储。
  未重跑五输入导出，未更新34/72/46统计；全量/RSS/基准仍暂停，阶段一仍开放。

### 2026-10-01：共同源码树支持原生空值回退表达式

- TreeLogical新增kNullish，接入共同结构验证和printer，复用既有表达式所有权、
  作用域和遍历机制。每个逻辑节点保持括号，满足JS对??与&&/||混用的语法约束。
  左值仅执行一次，null/undefined才执行右值；不将假值或用户类型转换当作空值。
- 新增logical_tree_test：1/200/201/402节点链、非法枚举和重复表达式所有权负例。
  logical_tree_source_test使用共同AST/printer生成五种成员调用参数表达式，与独立
  手写JS执行对照；10140组覆盖null/undefined、假值、NaN/-0、BigInt/Symbol、
  对象身份、禁止隐式转换、getter修改参数源以及各执行点抛错。
- 四项定向测试通过（2.72秒）：logical_tree_test、logical_tree_source_test、
  tree_structure_test、function_tree_test。红灯为缺失kNullish编译失败，随后实现并
  构建通过；日志/tmp/abcd-nullish-red-build.log、/tmp/abcd-nullish-green-build.log、
  /tmp/abcd-nullish-tests.log。未恢复全量/RSS/基准。
- 本次交付是共同树和原生输出能力，测试fixture不从ABC恢复CFG，不证明10510已
  完成反编译。10510实际conditional-values仍有46个操作、2个capture、2个child
  义务；/tmp/abcd-10510-values.json。下一步仍须把空值分支/phi恢复接入共同求值
  与控制组合，证明callee读取在参数分支之前；现有跨块CallPlan拒绝未放宽。
  未重跑五输入正式导出，不更新上一轮34/72/46指标；整类继承和阶段一仍未完成。

### 2026-10-01：按既有cell初始化证明恢复检查与调用组合

- 共同CellEvaluation提供常数时间的IsInitializedCellGuard判定：精确ValueGuard
  读取实际live ValueLexicalLoad的Language结果，且同revision的初始化表必须有
  StoredInitialization或明确Conditional incoming义务。没有新建父作用域假设；
  完整CellEvaluation验证及父捕获/传递写入验证继续负责解除义务。
- normal求值新增kCellGuard，验证器独立复核对应关系；来源树保留原PC的
  BlockBoundary锚点，操作数/流/来源对应同步检查。已证明守卫的use也从可见
  使用计数投影掉，避免虚假多用途快照阻断成员调用。未开放phased路径或删去未知守卫。
- 新增1/200/201/402守卫共同树规格：保留锚点，单守卫的读取可以正确内联；
  Hole域、撤销初始化见证、伪造placement均拒绝。真实initialRender完整读取族
  中10400（父11202）在实际children绑定下通过EvaluationPlan和CallPlan，PC26
  为kCellGuard；改为IncomingCells::kReject仍拒绝，不授予独立闭包发布权限。
- 七项定向回归通过（3.80秒）：evaluation_plan_test、function_tree_test、
  tree_structure_test、cell_tree_test、class_initializer_file_test、
  derived_constructor_source_test、navmap_source_test。构建/测试日志
  /tmp/abcd-cell-guard-regression-build.log及/tmp/abcd-cell-guard-regression-tests.log。
- 五输入正式导出build/stage-one/value-cell-guard-pass-1仍34 complete/72 failure/
  46 no-module，无完整项退化、无legacy writer；/tmp/abcd-cell-guard-batch-summary.json。
  整类已越过member6，当前member8（10510，父11202，创建PC115）在CallPlan
  属性引用上下文检查拒绝kReference；/tmp/abcd-cell-guard-projection-probe.log及
  /tmp/abcd-render-family-after-cell-guard.json。下一步处理该调用引用的控制流组合。
  整类继承和阶段一仍未完成，未恢复全量/RSS/基准。

### 2026-10-01：字面量中的成员调用贯通共同树

- LiteralPlan接纳拥有明确消费者的晚读取/调用子树，包括词法读取；对成员调用，
  仅精确匹配callee属性base与显式receiver的SSA身份时，重复wire receiver才不再
  作为第二次执行读取。现有单块、属性区间顺序、使用计数及新对象不逃逸检查保留；
  没有把不匹配receiver或全局读取加入准入。
- CallPlan与独立TreeOperands接入NestedLiteral对应关系，ExpressionForest在
  值顺序中使用共同Call构造路径，生成原生value.method.bind(value)而非复制getter。
  非法receiver仍拒绝。方法调用效果、异常、属性顺序和callee引用均未消除。
- 新增1/2/200/201/402属性共同树/语义规格、伪造计划和错误receiver负例；
  literal_call_source_test执行14组getter/get-bind/call逐步抛错及native/custom
  bind返回场景，对照对象字段顺序、身份、描述符及真正绑定的this。
- 实际9809在真实RecordModuleContext/ModuleAliases下通过共同EvaluationPlan与
  CallPlan，PC180确认为NestedLiteral。条件捕获测试仅证明该求值/调用层，不把
  脱离父owner的closure作为可发布完整源码。整类构建越过member1，停在member6：
  11202于PC32创建的arrow10400，在order11的ValueGuard(name325)拒绝
  kUnsupportedOperation。证据/tmp/abcd-literal-call-next-blocker.log；下一步处理
  该词法初始化检查，不能直接省略异常。
- 七项定向回归全部通过（1.28秒）：call_plan_test、literal_plan_test、
  class_initializer_file_test、literal_buffer_source_test、literal_call_source_test、
  derived_constructor_source_test、navmap_source_test。构建日志
  /tmp/abcd-literal-call-final-build.log、/tmp/abcd-literal-call-real-build.log，
  测试/tmp/abcd-literal-call-tests.log。五输入导出build/stage-one/value-literal-call-pass-1
  仍34 complete/72 failure/46 no-module，无完整项退化、无legacy writer；
  /tmp/abcd-literal-call-batch-summary.json。整类继承与阶段一验收仍开放；全量/RSS/基准未恢复。

### 2026-10-01：定位initialRender回调的成员调用字面量组合

- 用当前构建复现Index整类的下一阻断：initialRender10647在PC24创建的arrow9809，
  family member=1。LiteralPlan::EarlyValue因allocation order56之后的显式receiver
  ValueInvoke order69拒绝kLateValue；/tmp/abcd-literal-order-evidence.log。
- 原ABC PC148创建含builder槽的缓冲对象，PC154读取捕获receiver，PC160读取
  NavPageMap、PC168读取bind、PC174再次读取receiver，PC180成员调用，PC184定义
  builder。原指令证据/tmp/abcd-initial-render-callback-instructions.jsonl。
  此次定位使用ABC和当前代码，未读取配对源码来选择生产行为。
- 需要共同LiteralPlan与CallPlan接通：既保留对象创建/属性值执行顺序，也验证
  callee属性读取和显式receiver的对应。当前单消费者late-read树不能直接容纳
  成员调用的共享base；CallPlan::StableBase当前仅接受已有root快照或inline基值，
  不接受NestedLiteral。仅放宽ReadExpression或EarlyValue不足以证明完整组合。
- 本次为精确诊断，没有修改生产准入、重跑全量测试或宣称initialRender成功。
  下一实现从真实成员调用形态推进，同时保留getter次数、receiver身份、调用异常和
  多属性顺序的负例；不用删除getter/改成普通函数调用来绕过阻断。

### 2026-10-01：恢复未使用默认参数并贯通真实NavPageMap函数族

- DefaultCandidate的merged为显式optional，支持无live phi的未使用默认参数。
  仅识别缺参arm中唯一的原始字面量来源；带InputRead的寄存器传播副本不充当
  原始字面量。独立证明核对比较/分支/边、纯效果、Removed状态、ElidedPure来源、
  无结果使用、参数入口不逃逸及精确fallback。未按length猜默认值。
- DefaultBody仅将已证明的删除字面量来源重新归属signature，指令继续Removed；
  已有live操作的来源不重写。共同AST、签名、语义与instruction来源验证继续执行。
  新增无phi规格及伪造fallback/来源、传播副本、原始参数逃逸负例。
- 真实NavPageMap10428的原PC28恢复为第二参数null默认值；环境初始化保持body归属。
  在实际ModuleEnclosure下PreparedFamily与PrintDraft成功，包含全部嵌套闭包；
  输出/tmp/abcd-navmap.js。新增--navmap测试入口及navmap_source_test，从配对
  Index.ts独立提取参考方法（仅测试端去除一个参数类型标注），不向引擎提供源码。
- 756组受控框架行为对照通过：四路由及未匹配/undefined/对象、三种parent、
  首次渲染/更新、同步/延迟回调、receiver捕获、构造元数据、paramsGenerator名字/
  新对象结果，以及observe/create/pop/branch/constructor/view/setter/update异常。
  这是方法族行为验证，不是真实平台整应用或最终表示质量验收；仍有合成参数名和临时变量。
- 构建及四项定向测试通过（2.41秒）：default_candidates_test、class_initializer_file_test、
  derived_constructor_source_test、navmap_source_test。日志/tmp/abcd-unused-default-final-build.log、
  /tmp/abcd-unused-default-final-tests.log。五输入正式导出
  build/stage-one/value-unused-default-pass-1仍34 complete/72 failure/46 no-module，
  无完整项退化、无legacy writer；/tmp/abcd-unused-default-batch-summary.json。
- 整类已越过原definition op83，下一处definition op86在LiteralPlan::EarlyValue
  报kLateValue；调用链/tmp/abcd-unused-default-next-blocker.log。下一步处理后续成员
  对象字面量的求值顺序组合，继承表达式和整类验收仍开放。未恢复全量/RSS/基准。

### 2026-10-01：默认参数迁移保留入口环境初始化

- 共同DefaultCandidates现识别首次默认判断前连续的环境创建/词法存储前缀。
  DefaultValueProof独立验证：仅一次新环境、depth=0且slot在分配范围内、
  存储值为参数或receiver入口；所有待迁移参数的原始入口逃逸仍由全图Use检查拒绝。
  这些前缀操作不进入signature迁移集合，原操作、效果、结果及origin保留在残余body。
  未放宽调用、闭包创建、外层词法槽或任意计算的前缀准入。
- 新增1/200/201/402次存储的规格：共同默认证明及残余流检查成功，迁移集合仍仅
  三条默认操作；前缀保持live且来源/效果不变。提前存储待默认参数、写外层环境及
  environment-pop前缀拒绝。含有结果值的环境创建也保留，不伪装为无结果指令。
- 真实NavPageMap10428仍有第二处阻断：当前35个op、3个block，默认arm无live操作，
  join无live phi。原PC28的literal及PC29复制已为Removed；这是未使用默认参数
  的简化后形态，当前候选依赖live phi，尚不接受。证据
  /tmp/abcd-nav-default-shape.log、/tmp/abcd-default-prefix-probe.log。下一步需从
  保留的原始指令和删除证据恢复未使用参数默认值，不能按length猜测fallback。
- 构建default_candidates_test、class_initializer_file_test通过；构建完成后
  三项定向复测通过（1.67秒）：default_candidates_test、class_initializer_file_test、
  derived_constructor_source_test。日志/tmp/abcd-default-prefix-final-build.log和
  /tmp/abcd-default-prefix-tests.log。未重导出五输入或恢复全量/RSS/基准；
  真实整类及阶段一仍未完成。

### 2026-10-01：定位整类默认参数阻断，保护继承打印边界

- 实际 Index 10009 的 BuildClassTree 已通过构造器及前序成员，停在
  NavPageMap 10428：FunctionAbiError::kLength，运行时 length=1，frame参数=2，
  树的两个参数均无initializer。LLDB证据 /tmp/abcd-class-length-values.log；
  完整调用链 /tmp/abcd-class-member-failure.log。ABC指令显示默认判断前有
  环境创建及词法槽初始化，当前直接默认候选要求header仅含比较/分支及undefined，
  不能接纳该前缀。下一步需共同默认参数证明保留此前缀的归属和效果，而非放宽ABI。
- 发现PrintClassTree目前仅能输出无extends的类，但PreparedFamily已允许真实
  派生构造器片段。现整类打印在写任何字节前拒绝非基类构造器，防止super正文被
  嵌入无继承的类。新增实际9865片段组成最小容器的负例，验证明确拒绝且输出为空；
  此容器不表示实际整类已恢复。父类表达式组合仍待实现，未用拒绝替代完成目标。
- 构建class_initializer_file_test及class_tree_sample通过；四项定向测试通过
  （2.31秒）：class_initializer_file_test、derived_constructor_source_test、
  class_tree_source_test、constructor_tree_source_test。日志
  /tmp/abcd-class-heritage-boundary-build.log及/tmp/abcd-class-heritage-boundary-tests.log。
  未重跑五输入导出、全量测试、RSS或基准；阶段一保持开放。


### 2026-10-01：真实派生构造器贯通共同函数族与原生执行

- 求值计划新增 kConstructorGuard：仅同 owner 初始化投影列出的 live 守卫不再
  阻断顺序，普通守卫继续保留。独立验证检查有序集合、操作身份、protocol读取
  及 placement；源码适配器、求值计划和树必须使用相同集合。无关成员调用规则
  未放宽，真实9865此前的 CallPlanError::kReference 已解除。
- FamilyTrees 在默认参数迁移后证明协议，再传入求值与树构建。已证明的字面量
  默认值现可用于基类/派生构造器，非字面量和 receiver/arguments 引用仍拒绝。
  真实9865通过完整 BuildFileFamilyTrees、ABI、签名与源码对应检查，并通过
  PreparedFamily::PrepareConstructor/PrintDraft 生成原生 super(...) 正文。
- 打印入口仅对实际类根开放派生 callable；PreparedFamily 要求有构造器协议
  集合。正文保留 super 的对象结果及 return，未替换为普通 new、反射调用或旧writer。
  临时 receiver 别名与反向条件仍在，尚不是最终源级风格完成证据。
- 新增 derived_constructor_source_test：C++共同引擎从真实ABC打印正文，Node
  使用独立行为参考和受控框架替身比较264组场景，覆盖省略/非函数/函数参数、
  默认id与null/0/42、父构造器返回替代对象、new.target/length、setter效果及
  父构造器/初始化各步骤异常。配对参考只在测试端，生产不读源码。
- 八项定向回归通过（5.55秒），/tmp/abcd-derived-native-regression-build.log 与
  /tmp/abcd-derived-native-regression-tests.log；补充setter/default场景复测通过，
  /tmp/abcd-derived-native-setters-tests.log。生成正文 /tmp/abcd-derived-constructor.js。
- 五输入最新正式导出 build/stage-one/value-derived-constructor-pass-1，仍为
  34 complete / 72 failure / 46 no-module，无完整项退化、无legacy writer；
  /tmp/abcd-derived-native-batch-summary.json。这是构造器组件验收，未完成整个
  Index类/页面的继承表达与配对验收；下一步继续类heritage组合及receiver表达。
  未恢复全量测试、RSS、基准或更大corpus。阶段一保持开放。



### 2026-10-01：构造器守卫映射到共同源码锚点

- FunctionTree 增加有序 constructor_guards；共同构建通过来源适配器的已证明
  guard 集合将操作保留为原 PC 的 BlockBoundary anchor，不生成执行语句或
  未初始化 this 读取。存储计量、结构/流/操作数/原始位置核对同步接入；ABI
  不把这些已消费的协议读取当作 JS receiver 观察。
- FamilyTrees 在默认参数迁移和重绑定后，对真实 derived owner 调用
  ProjectDerivedConstructor，保留投影到同步树构建/检查结束，操作数核对要求
  树中的 guard 集合与来源适配器提供的集合完全一致。普通调用上下文不接受。
- 合成派生构造器通过共同 BuildFunctionTree 与 VerifyTreeSemantics，两个
  初始化守卫有锚点且无 ThisExpr；撤掉来源投影后树构建/语义检查均拒绝。
- 真实9865尝试构建已执行，当前失败为 ExpressionError::kUnsupportedOperation，
  深层原因 CallPlanError::kReference：仍有成员读取与调用被 guard 求值事件隔开。
  本次没有放宽成员调用校验；下一步必须在求值调度中消费已证明无作用的守卫，
  而非仅在树输出中省略。真实派生构造器仍未成功生成。
- 五项定向回归通过（3.00秒）：tree_structure_test、function_tree_test、
  class_initializer_file_test、constructor_tree_source_test、record_import_tree_source_test；
  构建含abc_semantic，日志 /tmp/abcd-guard-anchor-regression-build.log 与
  /tmp/abcd-guard-anchor-regression-tests.log。未恢复全量/RSS/基准或重导出五输入；
  阶段一保持开放。



### 2026-10-01：默认参数迁移后重新验证派生初始化投影

- 核对实际 FamilyTrees 顺序：默认参数先投影为 DefaultBody 残余图，再建立求值
  与源码树，因此构造器投影不能从迁移前缓存直接消费。
- 新增 VerifyDerivedProjection：同时检查 IR/CFG revision，基于当前同 owner 的
  body/工作图重建原始协议及输入证明，再比较 super 身份和完整守卫集合。
  相同版本号不替代覆盖检查，少一个守卫或换成错误 call 均拒绝。
- 真实9865先 FindDefaultCandidates→DefaultBody，再更新工作图；新初始化投影
  与求值计划均通过。旧投影按 kRevision 拒绝，缺守卫/错call按 kCoverage 拒绝。
  只变换测试内存图，原始ABC与配对源码不变。
- class_initializer_file_test 通过（1.11秒），/tmp/abcd-derived-defaults-build.log、
  /tmp/abcd-derived-defaults-tests.log。本次未完成守卫到AST锚点的消费，下一步
  继续接入这一环节及 receiver/返回协议；阶段一未完成，未恢复暂停的验证项目。



### 2026-10-01：共同源码树新增原生 super 调用

- SuperCallExpr 独立表达隐式父构造器/newTarget 的调用，不复用普通 CallExpr 或
  ConstructExpr。共同 ExpressionForest 按原参数顺序构建无 callee 前缀的参数区间，
  TreeOperands 与原 ValueSuperCall 逐参数核对；结构遍历保持独占节点/参数所有权。
- VerifyTreeStructure 只允许 derived callable 中的 SuperCallExpr；打印器新增
  原生 super(...) 分支，并要求 derived + class-root 上下文。现有发布入口仍未
  开放派生构造器，因此这里不是已生成/执行派生源码的证据。
- 0/1/200/201/402参数共同 BuildFunctionTree→VerifyTreeSemantics 回归通过；
  普通函数/方法/基类中的super及越界区间被拒绝。交换参数后的结构仍合法，但
  独立语义核对拒绝，防止将语法正确误当语义正确。
- 四项定向回归通过（3.65秒）：tree_structure_test、function_tree_test、
  class_initializer_file_test、constructor_tree_source_test。日志
  /tmp/abcd-super-ast-build.log、/tmp/abcd-super-ast-tests.log；参数顺序负例补测
  function_tree_test 通过（0.89秒），/tmp/abcd-super-ast-order-tests.log。
- 下一步真实构造器中的守卫消费、this/返回协议与发布入口；未恢复全套测试、
  RSS、基准或重新导出五输入。阶段一保持开放。



### 2026-10-01：super 协议进入共同求值计划

- DescribeEvaluation 新增范围 super 的有序参数形状和独立构造器守卫输入模式。
  super 保留结果、异常及效果；守卫保持独立 root 事件，protocol 输入必须取已
  计算值，禁止把其 producer 融入检查。独立 VerifyEvaluationPlan 同样验证这些
  约束；本次没有省略守卫，没有把未初始化 receiver 变成 JS this 读取。
- 真实9865现在通过 BuildEvaluationPlan 与 VerifyEvaluationPlan，初始化投影
  回归仍独立通过。这证明共同表达式调度已贯通，不是共同 AST/源码已经完成。
- 新增0/1/200/201/402参数测试，每个参数由具有用户代码/堆读取效果的操作产生，
  验证完整调度；把守卫改为 ElidedPure 或把 protocol 读取改为普通读取均拒绝。
- 四项定向回归通过（2.63秒）：evaluation_plan_test、class_initializer_file_test、
  constructor_tree_source_test、record_import_tree_source_test。构建与测试日志
  /tmp/abcd-super-evaluation-regression-build.log、/tmp/abcd-super-evaluation-regression-tests.log。
- 下一步原生 super AST、构造器 receiver 和已证明守卫的源码消费；上下文/投影
  尚未闭合源码发射。未恢复全量、RSS、基准或重新导出五输入，阶段一保持开放。



### 2026-10-01：派生构造器初始化证明映射到工作图

- 新增 ProjectDerivedConstructor，要求真实 derived owner 与同一 FunctionInput
  归属匹配，重跑原始 body/frame/profile 的范围 super 证明，再核对工作图每个
  super/guard 的原始位置、效果、模式、参数和存活状态。结果绑定 IR/CFG revision。
- 守卫实际输入必须仍满足证明：未初始化检查接受该 derived invocation 的入口
  ReceiverEntry 或 undefined，已初始化检查只接受同一次 super 的结果；共享纯
  copy 链以缓存的有界迭代遍历解析，不按每个守卫重复扫描，不接受环。
- 原始证明与工作图投影是使用同一上限的独立有界 pass，scratch 生命周期分离；
  该约束已写入接口。投影不授权 heritage 发射、this 替换或构造器 return 改写。
- 真实9865投影通过，改守卫输入为null、改检查模式或super参数数均被拒绝；
  零字节/工作预算亦拒绝。两项定向回归通过（1.44秒），包括既有基类源码输出；
  /tmp/abcd-derived-projection-build.log 和 /tmp/abcd-derived-projection-tests.log。
- 尚未将投影交给 EvaluationPlan/AST 消费，派生类源码仍未完成；下一步须将
  原生 super 表达、已证明守卫省略与 receiver 使用作为共同构造器投影接入。
  未执行全量、RSS、基准或重新导出五输入，阶段一保持开放。



### 2026-10-01：真实类创建绑定派生构造器上下文

- ClassTreeContext::ResolveConstructor 不再一律要求 heritage 为 Hole；对同一
  原始类创建保留的 live ValueId，Hole 解析为基类，language domain 解析为派生类。
  依据固定 OH7 RuntimeSetClassInheritanceRelationship：序列化方法属性仍仅接受
  NONE/BASE，DERIVED 是父类输入决定的运行时类别，不能虚构 ABC 枚举。
- 此变化仅解析实际创建归属；父类表达式、null/Proxy、抛出行为和构造器协议
  仍由后续类树证明负责。未开放 printer、未丢弃 heritage，也未给派生 this
  添加已初始化前提。非 language/Hole 输入和不对应的创建操作继续拒绝。
- 真实 Index 的类创建解析到派生构造器9865，再以该 owner 通过正式
  ReadFunctionValues；回归检查 MatchesOwner 和 FrameReceiver::kUnknown。
- 三项定向测试通过（1.13秒）：class_tree_context_test、class_initializer_file_test、
  constructor_tree_source_test，覆盖原基类输出和拒绝边界。日志
  /tmp/abcd-derived-context-regression-tests.log；构建同前缀 regression-build.log
  与 /tmp/abcd-derived-context-owner-build.log。
- 尚未输出派生构造器源码；下一步同输入初始化证明到工作图的投影、super 原生
  表达及 receiver 使用。未执行全量/RSS/基准或重新导出五输入；阶段一保持开放。



### 2026-10-01：真实派生构造器的初始化证明回归

- class_initializer_file_test 现从实际 Immersive ABC 读取构造器9865，经正式
  ReadFunctionValues 完成导入，再对同一原始 body/frame/profile 调用现有通用
  ProveRangeDerivedConstructor，成功定位 super PC79。证明检查正常返回支配关系
  和原始检查输入；尚未授权共同 AST 消费该证明。
- 对每个 super 调用和检查验证简化后仍为 live、输入站点映射、原始 PC、效果、
  参数数/IC、protocol 读取及无伪造结果。只在内存副本中把首次未初始化检查
  改为已初始化检查，证明按 kGuard 拒绝；未修改 ABC 或读取配对源码正文。
- 构建 /tmp/abcd-derived-protocol-build.log；class_initializer_file_test 通过，
  日志 /tmp/abcd-derived-protocol-tests.log。该回归证明真实输入及拒绝边界，
  不等于生成源码行为验收。下一接入点是带创建上下文的构造器投影：共同
  EvaluationPlan 当前仍拒绝 ValueSuperCall/ValueSuperCheck，必须把同输入证明
  与工作图对应，并保留 receiver/heritage/构造器返回协议；不能直接放宽形状检查。
- 未执行全套测试、RSS、基准或五输入重新导出；阶段一仍未完成。



### 2026-10-01：super 初始化检查贯通共同导入

- ValueSuperCheck 保存 RequireInitialized/RequireUninitialized/Noop 模式，保留
  accumulator 的 protocol 读取，无伪造结果；守卫异常和语言效果沿原语义输入导入。
  即使 Noop 无语言效果，普通纯操作删除规则也不擅自移除该协议操作。
- SemanticProof 明确允许 Hole 被 SuperCheck 的 accumulator 读取；此前仅允许
  marker 搬运和类 heritage，导致合法守卫 fixture 被提前拒绝。其他可观察 Hole
  使用仍拒绝，源码层仍须独立证明并消费构造器协议。
- 三模式 fixture 验证输入角色、无结果、异常区别、不能纯删除及模式篡改拒绝。
  构建 /tmp/abcd-super-check-regression-build.log；三项定向测试通过（2.97 秒），
  包括 value_import_test、class_accessor_tree_source_test、record_import_tree_source_test；
  /tmp/abcd-super-check-regression-tests.log。未恢复全量/RSS/基准。
- 真实 Immersive 方法9865 --conditional-values 现为 values_prepared=true、124操作，
  证据 /tmp/abcd-index-constructor-values-check.json。source_complete=false，
  executable=false；尚未贯通派生构造器源码表达与 heritage，阶段一未完成。



### 2026-10-01：范围式 super 调用进入共同 Value IR

- 新增独立 ValueSuperCall，保留参数数和 IC 槽；父构造器/newTarget 仍是调用上下文
  的隐式输入，不伪装成普通 new 的显式 constructor 操作数。紧凑 CallOperands
  视图按原顺序提供参数，校验寄存器效果与预算，保留正常结果及异常/语言效果。
- ImportNormalValues 和导入验证支持 SuperRange；简化器保留该效果操作。源码树
  尚未接入派生构造器初始化协议，因此源码层继续显式拒绝，未放宽基类检查。
- 覆盖三个动态 profile、普通/宽编码、0/1/200/201/402 参数，以及视图 65535
  参数边界；篡改参数数、替换为普通构造操作、缺失 accumulator 写入等负例被拒绝。
  零参数 fixture 的起始寄存器仍须在帧内，已修正测试帧范围。
- 构建 /tmp/abcd-super-regression-build.log；四项定向测试通过（2.45 秒）：
  call_operands_test、value_import_test、class_accessor_tree_source_test、
  record_import_tree_source_test。日志 /tmp/abcd-super-regression-tests.log。
- 真实 Immersive 构造器 9865 的 --conditional-values 已越过 PC79 范围 super，
  现在停在 PC87 的 super 初始化检查导入，证据
  /tmp/abcd-index-constructor-values-super.json。仍为 source_complete=false，
  不是派生类或页面源码完成；后续继续贯通 SuperCheck、receiver 初始化和 heritage。
- 未重跑五输入正式导出，最新完成率仍以此前 mixed-class-family-pass-1 为准；
  未恢复全套测试、RSS、基准或更大 corpus。阶段一 Goal 保持未完成。


### 2026-10-01：类与普通函数混合入口贯通共同模块输出

- record 根已构建的 ClassTree 现可进入 BuildRecordFamilyTrees，普通后代仍走
  原函数族路径。FamilyWrites 使用实际 ClassExpr/flow 与原始 instruction/operation
  归属索引，覆盖 class 创建及后置成员创建；缺失原始成员归属仍拒绝。索引按
  ops + class/member occurrences 线性构建，不逐成员扫描全部类，不丢弃创建屏障。
- 共同 printer 仅对模块 family.root 开放已组合 ClassExpr，类正文继续由既有
  PreparedFamily 打印并保留源码位置；普通嵌套函数里的类仍需显式捕获组合，
  没有无界类/函数 printer 递归，也未扩大派生类接受域。
- 文件级 record fixture 新增类+普通函数共同导出，以及普通闭包捕获刚创建的
  class 对象。正式 BuildRecordSource 生成原生 class/constructor 和闭包；独立
  Node ESM 对照验证同一构造函数身份、类名/length、prototype/constructor 关系、
  新实例身份、类不可普通调用，以及原有导入活绑定和循环依赖 TDZ。
- 后置访问器/普通方法 fixture 经模块根函数族再组合，五种模式保留5个类正文
  位置；删除一个原始成员归属的负例被拒绝。原独立操作数检查的缺失类正文负例
  继续通过。这些是合成 ABC 的通路验证，不等于真实 Index 页面验收。
- 构建 /tmp/abcd-mixed-class-regression-build.log；十四项定向回归通过（9.94 秒），
  /tmp/abcd-mixed-class-regression-tests.log。补充类捕获后的两项复测通过（1.35 秒），
  /tmp/abcd-mixed-class-capture-tests.log，构建日志同前缀 capture-build。
- 五输入最新正式导出 build/stage-one/value-mixed-class-family-pass-1，仍为
  34 complete / 72 failure / 46 no-module，无完整项退化、无 legacy writer；
  /tmp/abcd-mixed-class-batch-summary.json。未恢复全量测试、RSS、基准或更大 corpus。
- 下一步直接处理 PC98 的派生类 heritage/super/constructor 语义与相应源级表达。
  不能只放宽基类检查、忽略未初始化 this 或伪造父类。阶段一 Goal 未完成。


### 2026-10-01：普通子函数与模块入口正式组合输出

- BuildRecordTree 不再因普通 children 非空直接拒绝。入口按真实模块绑定构建根树和
  flow，然后 BuildRecordFamilyTrees 复用现有 FamilyTrees 的默认参数、捕获重绑定、
  子树、ABI、原始写入及作用域检查；模块入口不伪造 FunctionContext 或函数参数。
  正式输出接入 PrintModuleFamilyDraft，导入/导出仍由原 record 适配器组织。
- 函数族目的地新增明确 ModuleRole，只有实际根可作为 initializer，子函数始终是
  reader。入口模块自有 let 与可变 alias 通过 slot/name/scope 检查；子函数读这些
  绑定仍为只读 alias，不扩大子函数模块写入权限。
- 文件级测试发现并修复 family_enclosure 的局部区间表越界：旧容量按
  bindings - modules 计算，却仅按 BindingKind::kModule 排除符号，模块自有 kLocal
  被重复写入。现按已检查 alias 表构建线性索引，排除同一模块绑定，并保留真正
  lexical shadow/TDZ 检查。LLDB 证据 /tmp/abcd-record-children-crash.log；没有
  绕过 scope 检查，新增 own-local+descendant-alias 定向回归。
- record_import_tree 的真实文件字节码 fixture 新增普通 DEFINEFUNC：子函数读取
  外部模块活绑定、读取入口共享词法变量、读取本模块导出绑定三种场景，经正式
  BuildRecordSource 输出。Node ESM 与独立源码对照函数 name/length/identity、
  dependency mutate 后的读取、创建后 cell 更新、初始化顺序与循环依赖 TDZ。
  配对参考只在测试端；生产未读取源码、未增加样本路径/PC 特判。
- 初次 fixture 的目标缺少普通函数 kind 标志已修正。随后捕获路径暴露上述越界及
  root initializer 被 reader 限制拒绝，两者均通过共同层修复。最终构建日志
  /tmp/abcd-record-children-final-build.log；十五项定向回归全部通过（10.62 秒），
  /tmp/abcd-record-children-final-tests.log，涵盖创建上下文、捕获/模块作用域、类、
  module printer、真实 model/static-class 输出；未恢复全量测试/RSS/基准。
- 五输入以最新构建正式导出至 build/stage-one/value-record-children-pass-2，仍为
  34 complete / 72 failure / 46 no-module，无完整项退化、无 legacy writer；
  /tmp/abcd-record-children-batch-summary.json。fixture 贯通不等于真实页面已完成。
- 剩余：含保留 ClassTree 义务的混合入口尚未进入完整函数族组合；PC98 派生类
  仍需 heritage/super/constructor 共同恢复。下一步完成类与函数族组合，不能
  通过跳过类正文、伪造函数体或只放宽 callable 检查增加覆盖。阶段一未完成。


### 2026-10-01：共同 printer 支持模块入口与嵌套函数族

- 新 PrintModuleFamilyDraft 复用 TreePrinter 的迭代 owner 事件栈和完整函数位置表。
  模块模式只作用于 family.root：省去入口外壳及终止 return 关键字，保留返回值
  表达式的副作用；子函数的 block、return、receiver、await/yield 语法照常保留。
  模块根按 FamilyId 判断，不假定存储下标为0；没有第二套文本 writer。
- 原 PrintModuleTree 与新路径共用根限制检查。缺失子树、模块入口 this、非末尾
  return、输出预算耗尽等仍拒绝。源码片段类型分别标记 ModuleBody 和普通子函数，
  FunctionExpr 范围与对应子函数范围一致；仍不赋予输入/capture/class 完成收据。
- 新 AST printer fixture 将根存于最后，以模块变量、捕获读取和两个兄弟函数覆盖
  owner 切换。Node 原生 ESM 执行确认入口终止表达式更新变量为42，闭包读到更新，
  普通子函数正确返回调用 receiver。额外 async/generator 变体验证 await 结果与
  rejection 身份、yield/next 恢复值和 throw 异常身份。fixture 是输出层验证，
  不是 ABC 解码或完整页面验收；正式 record 的父子树组合尚待接入。
- 构建日志 /tmp/abcd-module-family-regression-build.log；九项定向回归通过
  （5.81 秒），/tmp/abcd-module-family-regression-tests.log。未运行全量测试、
  RSS、基准及更大 corpus；本次未重新导出五输入，不宣称 complete 数量增加。
- 已核实 PC98 类阻塞：ResolveConstructor 仅接受 Hole heritage / base constructor，
  expression_forest 和 tree_operands 也仅接受无继承类。派生类需要真正连接 heritage
  表达式与 super/receiver 语义，不能只放宽 callable allowlist。
- 下一步将 record 的已构建父树与共同函数族子树组合，接入上述模块 printer；
  继续处理派生构造器。阶段一 Goal 保持未完成。


### 2026-10-01：record 入口普通函数族与真实父层捕获绑定

- ReadFunctionFamily 新增显式根类保留模式：共同 ProjectClassMembers 证明归属的
  类创建及后置方法创建保留原始 instruction/operation/class 对应，不再误作普通
  DEFINEFUNC 边。类正文仍由 ClassTree 处理；普通函数族树入口拒绝未处理类义务。
  普通诊断模式的接受域保持不变，没有按样本名称或 PC 分发生产逻辑。
- record 入口使用实际函数族发现、后代 cell demand 及捕获绑定，向共同
  BuildEvaluationPlan 传入真实子函数和共享词法单元。保留无文件级函数索引调用的
  原路径；类处理和已有模块绑定继续走同一输出链路。普通子树尚未接入模块打印，
  因此含这些子树的 record 仍明确不完整，不能静默省略函数。
- 真实 Index entry10009 新增定向断言：PC77/285 分别连接9698/9726；9726 的
  非空捕获均绑定实际 ParentCell，根求值规划通过；10 个类创建/成员创建义务
  保持原始位置并归属 PC98。默认普通函数族模式仍拒绝该混合入口。
- 正式 Index record 的失败从 kValue 推进到 kClass、PC98，证据
  /tmp/abcd-record-family-index.json；这不证明类正文或完整页面已完成。
- 初次构建暴露诊断 visitor 未处理新增 ClassInitializerError，已补齐类型分支。
  构建日志 /tmp/abcd-record-family-build.log、
  /tmp/abcd-record-family-regression-build.log。十一项定向回归通过（7.77 秒），
  /tmp/abcd-record-family-regression-tests.log；包括真实入口、模块 import、类成员
  执行、model 与静态类批次。未恢复全量测试、RSS、基准或更大 corpus。
- 五输入正式导出 build/stage-one/value-record-family-pass-1：
  34 complete / 72 failure / 46 no-module，无完整项退化、无 legacy writer，
  /tmp/abcd-record-family-batch-summary.json。该数量仅为输出状态，不是语义验收。
- 下一步：组合入口父树、普通子函数树及共同模块 printer，继续处理真实类的
  constructor/heritage 阻塞；阶段一 Goal 保持未完成。


### 2026-10-01：普通后置类方法与访问器按原始顺序共同恢复

- 固定 OH7 RuntimeStOwnByName 证实类/原型上的 STOWNBYNAME 定义 writable、
  configurable、non-enumerable 的 own data property，不能替换成普通赋值。
  将访问器专用 occurrence/projection 泛化为 ClassDefinitionSite / FindClassDefinitions /
  ProjectClassDefinitions，用 variant 区分普通方法和 getter/setter 对。源码文件统一为
  class_definitions、class_definition_projection、defined_member_context，移除旧内部
  accessor-only API；没有引入样本旁路或字符串语义分发。
- 同一有序定义链同时承接普通方法、访问器及其互相覆盖，复用 class/home、唯一消费、
  无外部观察区间、key/运行时名字、原始创建 PC 和实际 PreparedFamily 检查。
  ClassTree 的定义正文与 getter/setter 不再分开排序，原生方法仍不可构造；仍不接纳
  普通 [[Set]]、DefineField 或额外 name-setting 操作。
- 修复两处贯通冲突：共同 LiteralPlan/独立 verifier 使用已证明的 class projection
  排除方法安装，保留同一图中真实对象字面量的完整覆盖；TreeFlow 在判断 own-write
  的 PropertyId 前先核对类投影 AnchorId。混合测试初次在 tree_flow.cc 的 placement
  检查失败，LLDB 定位后修正，没有删除外层验证。
- 合成 ABC 外层工厂新增普通方法覆盖访问器、访问器覆盖普通方法、重复定义和
  同图普通对象字面量。实际生成源码与独立 native class 比较最终属性种类、丢弃的
  getter/setter 半部、name/length、descriptor、receiver/Proxy 轨迹、异常和身份。
  三个动态 profile 上普通方法的 1/200/201/402 和逃逸/错误 home/环境屏障等负例通过。
- 真实 Index entry10009 的九个后置定义均被共同证明：四个 accessor，加上安装 PC
  218/230/242/254/266 的 go、NavPageMap、initialRender、rerender、getEntryName。
  共享 prototype 的所有相关使用现均属于投影。新增真实 getEntryName 的共同正文
  prepare/print/执行对照（返回 Index，原生静态方法 descriptor/不可构造性）；这不等于
  其余四个普通方法正文或完整页面已完成。
- 构建成功：/tmp/abcd-class-definition-regression-build.log、
  /tmp/abcd-class-definition-file-build.log。十六项定向回归全部通过（13.49 秒），
  /tmp/abcd-class-definition-regression-tests.log；中间失败/修复证据见
  /tmp/abcd-class-definition-tests.log、/tmp/abcd-class-definition-flow-tests.log。
- 五输入重新正式导出至 build/stage-one/value-class-definitions-pass-1，仍为
  34 complete / 72 failure / 46 no-module，无已完整项退化、无 legacy writer，
  /tmp/abcd-class-definitions-batch-summary.json。未恢复全量测试/RSS/其他 corpus。
- Index 正式 record 仍 kValue、source_complete=false，
  /tmp/abcd-class-definition-index.json。精确未绑定创建点现为普通函数 PC77/285
  （methods9698/9726），测试断言二者仍不得被成员投影丢弃。9698 独立条件函数族
  及树准备成功；9726 独立根探测为 CaptureEvaluationError::kOuterCapture，需在
  实际父层绑定捕获，不能将其根探测失败等同函数本身不可恢复。证据为
  /tmp/abcd-index-function-9698.json、/tmp/abcd-index-function-9726.json。
  下一步在 record 外层组合真实函数族/捕获，再继续派生构造与 heritage；阶段一未完成。


### 2026-10-01：后置访问器组合进入共同类树与外层求值验证

- 新 ProjectClassAccessors 复核实际 occurrence，使用一次 class/prototype use census、
  effect prefix 与 owner 索引检查 creation 至最后定义的完整区间，复杂度
  O(ops + uses + sites)。拒绝提前发布类/原型、外部观察、副作用屏障、环境变化和
  活跃 definition result；不逐访问器重复遍历类的全部用途。纯 key/name-flag 叶子
  仅在所有用途都属于同一投影时消去，不留下无意义的字符串和 false 表达式。
- 真实 Index 的 prototype 在四个访问器之后仍用于普通方法安装。现保留这个
  命名读取及 SSA 身份，只把已经承接的成员操作移入类创建；prototype 字符串
  仍由 constants/独立 tree correspondence 验证，不把任何 named read 都当成它。
- 原初始化投影统一命名为 ProjectClassMembers / ClassProjection；正常
  EvaluationPlan 和独立 verifier 接入 accessor 请求与同一 owner 证明。
  ClassTree 按 wire order 保存 PreparedFamily getter/setter，正式 record 路径
  按 class 索引分组传入；printer、外层 operand 检查和 member source positions
  共同消费实际正文。缺少正文、错误 occurrence 或伪造 owner 不能通过。
- 新 class_accessor_tree_test 从独立合成 ABC 指令完整贯通值图、求值计划、
  ClassTree、外层函数树和共同 printer，覆盖 prototype/static、重复定义及
  仍被返回的 prototype。class_accessor_tree_source_test 执行实际生成的外层
  工厂函数，与独立 native class 对照 name/length、属性顺序、getter/setter
  描述符、跨创建身份、Proxy receiver/读写顺序、异常身份和严格 setter 写入失败。
  这些是合成输入的完整类组合验证，不能代替真实页面与配对源码验收。
- 单元规格覆盖三个动态 profile、1/200/201/402 个访问器、同定义 getter+setter、
  提前类/原型发布、后续类/原型使用、debugger/环境屏障、结果逃逸、伪造 key/顺序、
  缺失正文与资源/work 限制。真实 Index 四个成员正文及共享 prototype 保留继续通过。
- 构建成功，日志 /tmp/abcd-accessor-final-build.log、
  /tmp/abcd-accessor-regression-build.log。十四项定向回归全部通过（9.56 秒），
  /tmp/abcd-accessor-regression-tests.log；后续 retained-prototype 两项通过
  （1.06 秒），/tmp/abcd-accessor-retained-tests.log；补充 pair/environment 和真实
  ownership 拒绝的三项通过（3.17 秒），/tmp/abcd-accessor-negative-tests.log。
- 五输入正式导出位于 build/stage-one/value-accessor-projection-pass-1，结果
  34 complete / 72 failure / 46 no-module，没有已完整 record 退化、没有 legacy
  writer；/tmp/abcd-accessor-batch-summary.json。未恢复全量测试/RSS/其他 corpus。
- Index entry10009 正式 record 仍为 kValue，source_complete=false，
  /tmp/abcd-accessor-projection-index.json；真实外层求值确认为 kOwnership，访问器
  之后的普通方法创建尚未被共同成员组合接纳（PC213 起）。下一步处理这种真实
  DEFINEMETHOD + 属性安装，再继续派生构造/heritage。初始化调用与访问器同类
  交错仍拒绝，尚需统一顺序证明；本轮不声称页面或阶段一完成。


### 2026-10-01：真实访问器正文贯通共同成员上下文、函数族与 printer

- ClassTreeContext 新增来自真实后置定义的 accessor owner：按现有有序 occurrence
  表查找，复核实际 DEFINEMETHOD、pool target、file identity、prototype 名称、
  属性 key 与运行时名字、getter/setter 参数数量。上下文保留实际函数创建点，
  没有伪造 class-buffer member ordinal。查找按索引进行，不逐成员重扫全图。
- PreparedFamily::PrepareAccessor 使用同一个 ReadFunctionFamily、BuildFileFamilyTrees、
  ABI、来源与目的地检查；共同 printer 按成员上下文输出原生 get/set。
  修正 FamilyTrees 对 creation site 的名称检查，使其与 ResolveFunctionSyntax
  一致：方法不要求普通函数的 inferred_name，也不引入 self binding。
- 依据 pinned OH7 RuntimeDefineGetterSetterByValue 与 JSFunction::NameGetter，
  安装操作设置 getter/setter function kind，lazy name getter 添加 get/set 前缀。
  Native accessor 的 name/length/不可构造性须实际检验，不能只凭方法名字猜测。
- 真实 Index 的四个 accessor 正文均已通过共同链路输出，保存
  /tmp/abcd-index-accessors.js。测试包装类仅承载这四个实际成员，不冒充原页面类。
  新 class_accessors_source_test 验证属性顺序、enumerable/configurable、函数
  name/length、无 prototype、不可 new、get/set receiver 和参数身份，以及 base
  读取、方法读取、调用各阶段的 Proxy 轨迹和原异常对象传播。此为独立行为
  契约测试，不是配对整页源码或平台端到端验收。
- 初次真实 prepare 在 FamilyTreeError::kContext 处拒绝，名称规则对齐后通过。
  错误文件 identity 和请求不存在的 getter/setter occurrence 仍被拒绝。
- 八项定向回归全部通过（5.79 秒），日志
  /tmp/abcd-accessor-context-regression-build.log、
  /tmp/abcd-accessor-context-regression-tests.log；两项新路径 oracle 亦通过，
  /tmp/abcd-accessor-context-oracle.log。未恢复全量测试/RSS/其他 corpus。
- 尚未把后置 accessor definition 投影到外层 ClassTree：仍需 class escape/
  effect corridor、属性覆盖顺序和来源组合检查。该步骤完成前页面正式输出
  继续拒绝，不把成员正文成功计作完整 record。最近五输入基线 34/72/46，
  本轮未重新统计全五输入，阶段一未完成。

### 2026-10-01：后置访问器的实际 class/home/function 归属贯通

- 新 FindClassAccessors 在共同正常 FunctionValues 恢复流程中运行，以 O(ops+uses)
  扫描实际 SSA：匹配类创建、静态或 prototype 候选 home、字符串池 key、
  DEFINEGETTERSETTER 的 getter/setter 方法 occurrence；不按方法名称推断身份。
  方法须同 block、同 home、正确 length，且仅供该定义消费。逃逸、错误 home、
  启用额外命名副作用或未知形状不产生可组合候选。
- 命名标志属于 protocol read，SimplifyValues 刻意保留其 SSA literal copy。
  初始用例发现直接查看字面量会漏掉所有真实候选，现只读取保留的纯定义，
  不改写协议 use，也不放宽简化器的输入规则。
- 分析结果保存在 FunctionValues.accessors，已接入生产读入流程；它只证明
  occurrence 对应，不授权移动定义。prototype 名称解析、descriptor 语义、
  class escape/effect corridor 及实际 accessor body 仍须在 ClassTree 组合时闭合。
  当前未改变源码 printer，也不把发现结果计作完整访问器源码。
- 单元规格覆盖三个动态 profile、1/200/201/402 个访问器、静态 getter、
  prototype setter、错误 home、方法逃逸、name flag 与资源限制。
  真实 Immersive entry10009 四个定义 PC137/160/183/206 均关联 class PC98，
  两对 getter/setter occurrence 正确，测试没有读取配对源码。
- 构建日志 /tmp/abcd-class-accessors-final-build.log、
  /tmp/abcd-class-accessors-file-build.log。四项类/模型/模块定向回归通过（4.09 秒），
  真实文件测试通过（1.14 秒），日志 /tmp/abcd-class-accessors-final-tests.log、
  /tmp/abcd-class-accessors-file-test.log。未恢复全量测试/RSS/其他 corpus。
- 下一步必须把这些已定位的 occurrence 与共享成员上下文和副作用投影集成，
  不能停在候选表。五输入完整输出最近基线仍为 34/72/46，阶段一未完成。

### 2026-10-01：record 入口允许前置分支及汇合后的本地声明

- BuildRecordTree 复用共同 ControlPlan，按顶层 next 链的源码顺序识别首次
  STMODULEVAR，支持前置条件逻辑后在汇合处声明。没有无条件提升 let；所有
  模块 store 必须属于该顶层序列，分支臂/循环内的初始化及更新仍明确拒绝。
  首次声明身份不再依赖物理 SSA block 编号。后续 TreeFlow 继续核对实际声明
  位于 body scope；模块 printer 保留只允许最终 return 的约束。
- 合成 ABC 正式入口增加条件调用后初始化的正例和分支内 store 的拒绝用例。
  独立 ESM oracle 对照条件执行/跳过、依赖 value 变更、调用 receiver、导出别名、
  namespace 身份、live export、链接义务及循环依赖 TDZ。引擎不读取 oracle。
- 构建成功：/tmp/abcd-record-control-build.log 和
  /tmp/abcd-record-control-regression-build.log。六项 record/import/真实模型链
  定向回归全部通过（2.89 秒），/tmp/abcd-record-control-regression-tests.log。
- 真实 Index entry10009 已越过 kFrame，正式输出当前返回 kValue（后续求值
  规划），/tmp/abcd-record-control-index.json。仍需访问器 class/home-object
  组合及派生类支持，不能把入口控制流进展声称为完整页面恢复。
- 五输入正式重跑保存 build/stage-one/value-record-control-pass-1，计数仍为
  34/72/46，无旧完整 record 回退、无 legacy writer。逐输入证据
  /tmp/abcd-record-control-batch-summary.json。未恢复全量测试/RSS/其他 corpus，
  阶段一 Goal 继续未完成。

### 2026-10-01：访问器定义接入共同 Value IR，真实页面入口完成值导入

- DEFINEGETTERSETTERBYVALUE 通过 ValueDefineAccessor 导入共同 IR；五个有序输入
  保留 base/key/getter/setter/name flag 独立 use，最后一个为协议输入，accumulator
  结果仍来自原定义。normal/before-throw/exceptional 效果完整沿用语义层，
  没有把命名标志误作 enumerable，也没有把访问器定义转为普通赋值。
- 简化器明确保留该操作，即使被伪造为空效果也不允许擦除。源码求值/表达式/
  operand 检查穷尽处理该新 variant；类及 descriptor 组合尚未实现时继续拒绝，
  不生成 Object.defineProperty 替身或伪造 getter/setter 正文。
- value_import_test 覆盖三个动态 profile、1/200/201/402 个操作、同一寄存器
  的重复输入、五类 operand role、acc 结果、效果保持、错误 role 拒绝及简化后
  操作存活。evaluation_plan_test 明确检查尚不能直接生成源码且操作有序。
- 实际 Immersive Index entry10009 的 --conditional-values 已 values_prepared=true，
  111 ops、1 cell、13 child obligations、class heritage unknown。证据
  /tmp/abcd-accessor-index-values.json。原 PC137 import_stage 阻塞消除；正式
  record9572 目前返回 kFrame（单块模块初始化限制），证据
  /tmp/abcd-accessor-index-record.json。完整页面仍未恢复。
- abc_semantic/value_import_test 构建成功，日志 /tmp/abcd-accessor-import-build5.log；
  abc_structure 构建成功，/tmp/abcd-accessor-structure-build.log。
  四项 import/phased-import/simplify/evaluation 定向回归通过（2.37 秒），
  两项真实模型链回归通过（1.98 秒），日志 /tmp/abcd-accessor-regression-tests.log
  和 /tmp/abcd-accessor-model-regression.log。未恢复全量测试/RSS/其他 corpus。
- 下一步需沿现有控制组合处理 record 入口分支与模块声明生命周期，且将实际
  后置 accessor 定义按 class/home-object/descriptor 证据合入共同 ClassTree。
  不能简单删除 single-block 检查或绕过 derived constructor 限制。最近五输入
  完整计数基线仍为 34/72/46，本轮没有重新统计五输入完整数，阶段一未完成。

### 2026-10-01：补充本地类 TDZ、显式字段及 getter receiver 验证

- local_model_batch_source_test 在 SourceTextModule 完成 link、尚未 evaluate 时，
  通过测试专用函数读取实际本地类绑定，三个生成模块与独立配对 TS 均抛出
  ReferenceError；evaluate 后读取返回实际导出类。覆盖 native 模块绑定 TDZ，
  不将其扩大为所有循环依赖或中途调用时机的证明。
- 显式给三个值类的每个构造参数传入不同对象，检查 getter 返回相同对象，
  并对照 own-key 顺序、字段值及 writable/enumerable/configurable 描述符。
  SectionProduct 此处显式提供 originalPrice；缺省 optional 字段在 Node TS
  转换后的差异仍未解决，不能据此宣称所有构造分支的描述符已验收。
- 借用全部 getter 的 receiver，比较 Proxy 属性读取轨迹及返回身份；Proxy
  读取抛出 sentinel 时，两边都传播原异常对象。测试仅增加独立 oracle，
  没有更改生产引擎、样本或正式输出。测试通过（日志
  /tmp/abcd-local-model-fields-test.log），未恢复全量测试/RSS。
- 下一处集中阻塞已核对真实 ABC：Immersive Index record9572 的 entry10009，
  PC137 为 accessor_definition（opcode188），语义解码已给出 object/key/getter/
  setter/name_flag，但 ImportValueOperation 尚无该操作的 Value IR 映射，
  导致 import_stage 拒绝。证据 /tmp/abcd-index-entry-instructions.jsonl。
  需要沿共同 IR/类成员集成实现访问器定义，不能省略指令或以普通赋值替代。
- 另一个独立缺口：EntryBackupAbility record9308 entry9460 在类创建 PC14
  拒绝，constructor9544 的实际输入在 PC12 失败。不得将两个缺口混为同一修复。
  最近正式五输入计数仍为 34/72/46，阶段一未完成。

### 2026-10-01：同 record 本地类绑定贯通，三个模型正式恢复

- 实际阻塞为 FamilyEnclosure 的 module binding：已有模块别名能识别 local slot，
  但目的地库存仅包含 imports，导致 IconInfoModel/RecommendModel/SectionModel
  的完整函数族在正式类准备处拒绝。没有绕过该检查。
- record 先建立实际首次 STMODULEVAR 声明、slot/name 对应和求值规划，再准备类。
  GlobalScope 借用已证明的本地声明名字；共同目的地检查按 owner/slot/name 校验
  只读别名，生成代码仍直接读取 native let，保留 live binding 与 TDZ。
  本地声明所在 record 层与中间 wrapper 分开，global/realm/closure 检查仍将
  record 名字作为遮蔽；参数、自名、类名及中间层同名继续拒绝。独立方法默认
  没有本地声明前提，不能仅凭 local-export 元数据授权输出。
- module_enclosure 定向用例覆盖缺失声明、错误 owner/name、可写别名、四类遮蔽，
  以及 record 层对 import/global 的遮蔽；沿用 1/200/201/402/804/1608 规模。
  family_enclosure 增加 record 层遮蔽回归。
- 新 local_model_batch_source_test 从 ABC-only 正式导出加载三个模型、真实生成的
  HomeConstants 和 Breakpoint，独立配对 TS 验证列表/getter 结果、常量读取轨迹、
  原型身份、重复初始化、实例隔离，以及测试钩子替换本地类绑定后的参数与对象
  身份、构造器抛错传播。钩子仅存在于测试，不进入引擎或正式输出。
- 该测试尚未证明模型字段的全部 own-key/描述符语义，尤其 SectionProduct 的
  可选 originalPrice 声明在 Node TS 擦除后与 ABC 可能不同。不能将受控测试
  宣称为所有可观察行为或平台端到端验收；新增 native local TDZ 无独立运行用例。
- 构建日志 /tmp/abcd-local-module-build2.log、/tmp/abcd-local-shadow-build.log。
  五项原模块/模型定向回归全部通过（5.87 秒），新增三模型 oracle 通过（0.58 秒）；
  最后四项 local/global/family 检查及 oracle 全部通过（2.18 秒）。日志分别为
  /tmp/abcd-local-module-tests.log、/tmp/abcd-local-model-oracle.log、
  /tmp/abcd-local-shadow-tests.log。未恢复全量测试/RSS/其他 corpus。
- 五输入正式输出 build/stage-one/value-local-modules-pass-1：34 完整 /72 失败 /
  46 无模块上下文，新增 Immersive 16752/17175/17544；相对上一批无完整回退，
  无 legacy writer。逐输入证据 /tmp/abcd-local-modules-batch-summary.json。
  class 命名包装、捕获搬运及剩余 UI/异常等失败仍待处理，阶段一保持未完成。

### 2026-10-01：字面量晚执行读取贯通，HomeModel 正式输出及配对执行通过

- LiteralPlan 支持唯一消费的 named/computed/module read 链进入新建字面量；
  使用已验证的模块 read/guard 配对，独立检查操作数顺序、属性区间、所有权、
  共享使用及外部副作用。提前求值的快照必须位于最外层分配之前；不搬移任意调用。
- 共同 expression forest 按依赖顺序构造读取，再组合嵌套字面量；模块符号和
  guard 来源复用现有绑定路径。完整 family 显式传递 Language 写入前提，
  后续仍用真实 primitive-object 证据和 VerifyFamilyWrites 验证，不能由无子函数
  推断缓冲对象没有捕获屏障。未改变 phased 路径的前提。
- literal_plan/buffer 测试覆盖 1/200/201 条模块读取、getter 次序、异常身份、
  属性描述符，以及乱序输入、外部 debugger 和伪造所有权的拒绝。
- 新增 homemodel_record_source_test：仅给正式引擎 ABC，在独立测试中将实际生成
  的 Breakpoint、HomeConstants、HomeModel、HomeViewModel 四模块链接；对照配对 TS
  验证字段值/描述符/顺序、对象身份、getter 轨迹、重复初始化、实例隔离和异常传播。
  该受控模块链通过不代表整个应用或 HarmonyOS 平台端到端验收。
- 构建成功：/tmp/abcd-home-build.log。五项基础定向测试通过，记录于
  /tmp/abcd-home-tests.log；该轮新增集成测试因目标目录已存在失败，修正测试
  使用临时目录下的新输出子目录后，八项集成/闭包/函数树回归全部通过（6.86 秒），
  日志 /tmp/abcd-home-integration.log。未恢复全量测试、RSS 或其他 corpus。
- 五输入 ABC-only 正式输出位于 build/stage-one/value-literal-reads-pass-1，
  计数为 31 完整 / 75 失败 / 46 无模块上下文，新增 Immersive record16474。
  相比 value-initializer-modules-pass-1 无完整 record 回退，无 legacy writer；
  逐输入证据 /tmp/abcd-literal-reads-batch-summary.json。
- 实际 HomeModel 已恢复 map 回调中的对象字面量及两条导入常量数组读取；
  class/function 命名包装、this 捕获搬运及部分临时变量仍是表达质量缺口。
  此次只完成该通用读取集成与模块链验证，阶段一 Goal 继续保持未完成。

### 2026-10-01：补充六个 viewmodel 的独立配对行为测试

- 新增 viewmodel_batch_source_test，在临时目录对 Immersive 和 Multi 的 ABC-only
  正式导出重新生成源码，再独立读取配对 TS。覆盖 record
  28887/29119/29382/29619/44106/30839，生产引擎不读取 oracle。
- 四个购物 viewmodel 验证构造依赖、方法 receiver、返回对象身份、slice 的
  undefined/负数/零/正数/越界/NaN/带 valueOf 参数、getter/调用/强制转换顺序、
  抛错传播及更新导入构造器后的新实例。TabBar 验证旧实例保留旧数组、
  新实例读取更新后的 live import，以及数组变更可见性。
- CardItem 验证 card 属性读取顺序、index coercion、pictureArray 共享身份、
  callback 捕获 this、CommentItem 构造顺序/异常、commentList 实例隔离，
  以及各字段的值和描述符。依赖均为两边相同的显式受控测试输入，
  不代表真实模型依赖或整个应用的端到端验收。
- CardItem 字段枚举顺序仍未验收：Node stripTypeScriptTypes 保留配对 TS
  的无初始化字段声明，而 ABC 实际属性创建顺序受编译语义影响。
  本测试按字段名比较描述符，不能把这一结果声称为完整语义等价。
- CMake 已注册。配置日志 /tmp/abcd-viewmodel-configure.log；测试通过
  （1.46 秒），日志 /tmp/abcd-viewmodel-test.log。未改生产实现，
  最近五输入正式计数仍为 30/76/46，未恢复全量测试/RSS/其他 corpus。
- 下一处实际阻塞：Immersive HomeModel record16474（entry16638）为 tree_stage；
  callback16610 已读入，含一个 capture、两条 HomeConstants guarded module
  read（op17/op28），应继续沿共同 family/literal 路径定位，不能复制配对函数体。

### 2026-10-01：类初始化模块绑定贯通，正式完整 record 增至 30

- ClassTree 复用 record 的 ModuleEnclosure 读取真实 initializer，BindModuleAliases
  绑定规范 slot/name/owner，构树后 VerifyModuleAliasTree 再核对实际别名。
  PrintStaticFields 允许经目的地核验的 module symbols，拒绝缺失目的地、
  错误 record 和遮蔽；仍禁止任意 locals/captures 或伪造字段正文。
- 正常流的 guarded module read 进入已有 ordered suffix 合并算法；消费读和
  相邻 guard 两个原事件，存在插入副作用或多次使用时保留快照。phased 路径
  保留原有限制。模块测试覆盖 local/external、0/1/2/200/201/4096 及调用屏障。
- 正式 record 为类创建和打印传递同一模块目的地，并建立完整非 import 词法
  名字清单防止子类正文全局引用被外层绑定捕获。资源与工作量失败继续分类。
- HomeConstants 16247 正式输出 31 个静态字段，含 20 个缓冲数组和导入类构造；
  BreakpointType 7395 同时完整导出。独立 oracle 以实际生成的 Breakpoint 模块
  链接生成的 HomeConstants，对照独立配对 TS：静态值/递归描述符、字段顺序、
  对象隔离、类身份/构造器属性、Breakpoint 方法输入/状态变更、构造器抛错，
  以及循环 ESM 依赖的 TDZ。Host WidthBreakpoint 是测试的显式共同输入。
- 12 项定向回归全部通过（12.53 秒），日志
  /tmp/abcd-init-modules-regression-build.log、/tmp/abcd-init-modules-regression-tests.log。
  扩展为实际生成模块链接后批量 oracle 通过（1.67 秒），日志
  /tmp/abcd-init-modules-linked-test.log；共九个 record 有这批配对执行证据。
- 五输入正式导出保存 build/stage-one/value-initializer-modules-pass-1：
  Immersive 11/24/10，Multi 18/27/9，pasteboard 1/9/9，Push modules 0/15/9，
  widgets 0/1/9（完整/失败/无模块上下文），总计 30/76/46。
  对比 value-static-class-pass-1 没有旧完整 record 回退、没有 legacy writer。
  /tmp/abcd-initializer-modules-batch-summary.json 保存逐输入计数及新增 ID。
- 新增八个：Immersive 7395/16247/28887/29119/29382/29619，Multi 30839/44106。
  其中 HomeConstants 与 BreakpointType 已独立配对；其余六个 viewmodel 的
  独立行为验收待补。class-name 包装和部分方法临时变量/控制表示仍有质量缺口。
  source_complete 不等于整应用可执行或端到端验收，executable 标志仍为 false。
  未运行全量测试、RSS 或其他 corpus；阶段一未完成。

### 2026-10-01：晚加载字符串进入初始化表达式，HomeConstants 字面量规划通过

- 将无输入、无效果、无异常的 ValueString 作为原生字面量 primitive leaf，
  纳入现有 nested initializer 归属。只接受唯一 RHS 使用；沿共同求值规划和
  逆序构树保留真实来源，禁止留下在数组创建之后才定义的 snapshot。
  任意纯计算、共享值、模块读取和可观察操作不因此获得移动权限。
- 新增 1/200/201/402 个晚字符串的双层嵌套数组测试；检查无 $value 搬运、
  值篡改、效果篡改和移除归属的负例。独立 Node 执行检查嵌套数组长度、
  每项值/描述符，以及多次调用的内外数组身份隔离。
- 真实 HomeConstants initializer 16447 的 BuildLiteralPlan 现在通过；
  BuildEvaluationPlan 仍拒绝未绑定模块 guard。CLI --conditional-values
  确认 op53 读取 external slot0，配对 guard 一条，解析名 BreakpointType，
  BindModuleAliases 已能解析。下一步将实际 record 的模块作用域贯通到
  ClassStaticFields 和打印目的地，不能删掉 guard 或假造局部变量。
  诊断证据 /tmp/abcd-home-late-leaf-values.jsonl。
- 最终构建日志 /tmp/abcd-late-leaf-final-build.log；五项定向测试通过，
  日志 /tmp/abcd-late-leaf-final-tests.log，含生成源码独立执行和七个既有
  静态类 record 的配对回归。没有恢复全量测试、RSS 或其他 corpus。
  没有新增完整 record 的结论，阶段一未完成。

### 2026-10-01：缓冲数组的后续元素定义进入共同字面量恢复

- 从 HomeConstants initializer 16447 的真实字节码确认缓冲数组创建后仍有
  indexed own definitions，例如 PC1744 创建后 PC1770 写入 index1，
  PC2068 创建后 PC2094/2103/2128/2153 写入 index1..4。
- LiteralPlan 接受严格递增的缓冲数组定义，保持现有无提前观察、嵌套归属和
  RHS 顺序约束；常量感知的表达式构造保留原前缀、替换已有项并连续追加，
  拒绝会产生 hole 的跳跃。共同 tree flow/operands/structure 核对具体位置、
  保留项常量、定义来源及数组 dense keys。没有新增生产样本特例或旧 writer。
- 测试覆盖从 index0 覆盖/追加、保留 index0 后从 index1 覆盖/追加，
  1/200/201/402 大小、逆序/重复下标、缺口及保留常量篡改拒绝。
  独立 Node oracle 从缓冲数组和 DefineOwnProperty 序列计算期望，比较
  全部 own keys、length、属性描述符和两次调用的数组身份隔离。
- 构建日志 /tmp/abcd-buffer-writes-final-build.log；六项定向测试全部通过
  （4.91 秒），日志 /tmp/abcd-buffer-writes-final-tests.log，包含现有七个
  静态类 record 的配对执行回归。未运行全量测试、RSS 或其他 corpus。
- HomeConstants 已越过 kReceiver，但仍在 kLateValue 处失败；实际指令含
  数组创建后的字符串加载（例如 PC2100），下一步处理晚产生 RHS 的共同
  求值位置。新增真实文件测试固定此未完成边界，不把它当作完整正文。
  本次未刷新全部五输入计数；前次 22/84/46 仍是最近完整批次证据，
  不宣称本次新增完整 record，阶段一保持未完成。

### 2026-10-01：静态初始化贯通正式 record，新增七个完整导出

- 实际 initializer 正文进入 ClassTree，沿共享语法解析、控制/求值规划、
  FunctionTree 和 PrintStaticFields 输出，原 method/call 及未使用 prototype
  读取保留来源 anchor。字段方法的全局引用仍需目的地作用域证明。
- ClassInitializerProjection 用效果前缀和 def/use 索引核对创建到调用之间的
  顺序，拒绝类提前发布、额外可观察效果、重复归属和伪造调用。名字无关的
  调用/receiver 检查与源常量中实际 "prototype" 的核对分开；任意属性读取
  不能被删去。O(ops + uses + calls log calls)，覆盖 1/200/201/402 和三个 profile。
- 共同调用规划、求值可见使用、表达式构树、语句位置和操作数检查同步消费
  初始化投影。真实 class initializer 绑定必须存在于对应类定义中，不能仅凭
  标志省略调用。类合并后的无效额外 use 不再强制产生 $value 临时变量。
- 正式 ABC-only record 导出现在恢复 CommonConstants 的类、构造器、静态
  字段和模块导出。独立测试对照 CommonConstants.ts，验证初始化前 TDZ、
  class name/length、构造器及 prototype 身份、字段描述符、继承 setter 不触发、
  重复模块求值的数组隔离；旧 writer 未参与。
- 五个阶段一输入刷新至 build/stage-one/value-static-class-pass-1：
  Immersive 5/30/10，MultiCommunityApplication 16/29/9，pasteboard 1/9/9，
  Push modules 0/15/9，widgets 0/1/9（完整/失败/无模块上下文）。
  合计 22/84/46，较前次新增 7 个完整 record，无旧完整记录退回失败。
  /tmp/abcd-static-class-batch-summary.json 保存输入身份和逐文件计数。
- 新增独立三应用批量 oracle，重新以 ABC-only 模式导出并执行七个新 record，
  与配对 TS 比较全部导出类、静态值/嵌套对象和数组的属性描述符、对象隔离；
  pasteboard 额外比较 BreakpointType 构造及 GetValue 的 sm/md/lg、未知值、
  undefined、禁止 coercion 的对象输入与实例字段变更。
  七个记录 7167/19020/19442/20830/31170/47487/4182 全部通过。
- 12 项定向测试通过（6.45 秒），日志 /tmp/abcd-init-integration-build.log、
  /tmp/abcd-init-integration-tests.log；批量 oracle 通过（1.62 秒），日志
  /tmp/abcd-static-batch-tests.log。完善资源失败分类后，两项正式导出测试
  再次通过（3.62 秒），日志 /tmp/abcd-init-classify-build.log、
  /tmp/abcd-init-classify-tests.log。未恢复全量测试、RSS 或其他 corpus。
- 阶段一仍未完成：HomeConstants 在静态正文的 LiteralPlan kReceiver 受阻；
  其他类的 module/capture/derived 与未恢复函数体继续开放。源码仍有用于保持
  name/binding 语义的类名包装及部分控制结构风格差距；本次测试不是整个应用
  或 HarmonyOS 平台验收，22 个完整导出计数不能替代阶段一整体完成。


### 2026-10-01：真实初始化正文进入原生静态字段片段

- 共同求值与 LiteralPlan 区分直接 receiver 的 DEFINEFIELDBYNAME 和
  fresh object/array 字面量字段：前者保留原顺序的语句根，不合并到新建对象。
  FieldDefinitionExpr 显式保留 receiver/name/value，与 AssignmentExpr 分离；
  构树、树结构、操作数与 ABI 检查同步支持。没有将 DefineOwnProperty 降为 Set。
- 同一 TreePrinter 新增 PrintStaticFields 目的地，只接受 closed、strict method
  的平直字段定义及最终 undefined return；拒绝普通赋值、局部符号、控制流、
  不完整语句和嵌套字段定义。普通函数输出仍拒绝此节点。名字使用正常字段形式，
  特殊键保留 computed；实际方法 runtime name 不引入 named-function self binding。
- CommonConstants 7368 的未改写实际值图，经实际创建上下文、共同语法解析、
  求值/控制规划和 FunctionTree 输出三条静态字段：FULL_PERCENT、FONT_WEIGHT_500、
  TEXT_OPACITY。产物 /tmp/abcd-common-static-fields.txt，未读取配对源码参与生成。
- 独立 Node oracle 读取配对 CommonConstants.ts 比较字段值、顺序与 descriptor，
  验证继承 setter 不触发、重复 class 创建的数组不共享。该测试外层 class/extends
  是明确的行为夹具，不是完整 record 导出。把字段节点改为 AssignmentExpr 的
  负例同时被共同语义检查和静态字段 printer 拒绝。
- 13 项定向测试通过（8.24 秒），日志 /tmp/abcd-static-final-build.log、
  /tmp/abcd-static-final-tests.log。补充嵌套字段拒绝和赋值篡改后，2 项相关
  集成测试再次通过，日志 /tmp/abcd-static-negative-build.log、
  /tmp/abcd-static-negative-tests.log。未运行全量测试或 RSS 验收。
- HomeConstants 正文读取及常量解析仍成功，但 BuildLiteralPlan 返回 kReceiver，
  BuildEvaluationPlan 返回 kUnsupportedOperation，尚未输出其全部静态字段。
  正式 record 尚未消费初始化字段片段：下一步证明 class 创建到 method 调用之间
  的使用/效果顺序并接入类 AST，随后处理 HomeConstants 的字面量组合。
  本次不增加完整 record 计数，阶段一目标继续进行。


### 2026-10-01：缓冲数组进入共同常量、AST 和源码输出

- AggregateConstant 显式区分对象和数组，复用现有常量属性存储、ObjectExpr
  的 array 形式与同一个 TreePrinter。按 kind/pool 排序去重读取；共享的是
  primitive 常量存储，每次创建仍生成独立数组。没有调用旧 body writer。
- 数组 literal 直接经 ABC reader 解析，保留元素顺序、字符串、null、boolean、
  double 位型和 unsigned integer 语义。整数依据 pinned v7
  LiteralDataExtractor::GetDatasIgnoreType 的 JSTaggedValue(uint32_t)，
  不沿用旧数组读取器的 signed reinterpretation。方法/其他未支持 tag 拒绝。
- 构树预留、结构检查、位置/操作数验证共同支持密集缓冲数组；数组仍禁止
  随后 indexed writes 的合并，避免以空数组初始化规则覆盖已有 buffer。
  错误 aggregate kind、元素篡改和未支持方法 literal 均有负例。
- 新测试覆盖三个 dynamic profile、0/1/6/200/201/402 元素及重复池引用。
  生成 /tmp/abcd-buffered-array.mjs，实际 Node 执行确认 null/boolean/
  4294967295/负零/字符串/小数、元素 descriptor 及重复调用的独立身份。
  真实初始化正文常量解析通过：CommonConstants 1 个、HomeConstants
  20 个缓冲数组；这不等同于整个初始化方法已经输出或行为等价。
- 10 项定向测试全部通过（6.28 秒），日志 /tmp/abcd-array-final-build.log、
  /tmp/abcd-array-final-tests.log。旧对象测试中已过时的计算属性文本断言
  按当前普通属性 printer 输出更新，__proto__ 特殊语义断言继续保留。
- 五个阶段一输入刷新至 build/stage-one/value-buffered-array-pass-1，
  结果仍为 15 complete / 91 failure / 46 no-module，legacy writer 为 0；
  /tmp/abcd-array-batch-summary.json 记录输入身份及逐文件计数。
  未新增完整 record，未恢复全量测试或 RSS 验收。下一步继续将实际静态
  初始化的 own-field 定义与调用接入 class，保留效果顺序及 receiver 语义。


### 2026-10-01：实际类初始化调用与正文读取贯通

- 共同 FunctionValues 在简化后的正常图上识别 class → DEFINEMETHOD →
  同 receiver、零参数、单次且结果不逃逸的调用；使用 def/use 索引，
  不按名字、固定 PC 或样本选择生产逻辑。此结果只证明调用身份，
  尚不证明把调用移动进 class 的效果顺序或类逃逸条件。
- ReadClassInitializerFunction 绑定实际方法创建 occurrence 和 no-prototype
  method owner，复用 FunctionInput/FunctionValues 读取正文；默认普通函数
  创建入口仍拒绝 DEFINEMETHOD，避免改变 callable/prototype/home 语义。
- CREATEARRAYWITHBUFFER 的 literal pool 身份进入共同 ValueArrayAllocation，
  原始操作数导入与验证同步支持。缓冲数组尚未接入共同 AST 常量输出，
  构树、操作数检查和 indexed literal folding 均明确拒绝，不能变成空数组。
- 实际 Immersive CommonConstants 7312 → 7368 正文读取成功，3 个 this
  字段；HomeConstants 16391 → 16447 读取成功，31 个 this 字段及 93 个
  新建对象字段。测试分别验证 receiver 来源；未将嵌套对象字段计为类字段。
- 9 项定向测试通过（4.36 秒），含三个 profile 的初始化识别及
  1/200/201/402 规模、普通创建上下文、窄/宽缓冲数组与池身份篡改、
  实际初始化正文、共同 function/class AST 和三组 record 回归。
  构建日志 /tmp/abcd-buffer-import-build.log、
  /tmp/abcd-initializer-regression-build.log、/tmp/abcd-init-final-build.log；
  测试日志 /tmp/abcd-init-final-tests.log。未运行全量测试或 RSS 验收。
- 本次未新增完整 record，也未宣称静态字段源码恢复完成。下一步复用
  ABC literal reader 将缓冲数组接入共同 AST，再处理初始化方法中的
  own-field 语义及其进入 class 的位置；全阶段一验收仍未完成。


### 2026-10-01：类创建贯通表达式 AST 与正式 record 适配器

- FunctionTree 新增持有已恢复 ClassTree 的 ClassExpr；表达式常量绑定、
  实际操作数检查共同核对输入身份、creator、PC、constructor/literal pool、
  length/IC 和 definite Hole。缺失或错误定义拒绝，不以空类替代。
  当前接入 closed base class；family 内嵌类、捕获 rebase 和 derived/super
  尚未开放，不把已有独立类表达式测试扩称为这些能力通过。
- 正常图中经 HoleProtocol 检查的无继承 Hole/纯 copy 仅映射到原始位置
  anchor，不创建 JS sentinel、临时绑定或 extends 值。构树、绑定、语句位置
  和操作数检查共同消费这一协议；有副作用的伪造 transport 不允许省略。
- 同一个 TreePrinter 直接打印 ClassExpr，构造器和方法仍由共享 family
  printer 输出。嵌套成员保留自己的身份、局部位置和 enclosing_offset；
  资源/工作/写入失败保留分类。没有添加 raw source body 或专项 body writer。
- BuildRecordTree 现在为真实入口的 class occurrence 构建定义并接入 AST。
  abc_structure 每文件构建一次 function/class creation 索引，跨 record
  借用；abc_semantic 的正式 record 路径也传递同样索引，避免逐类全文件扫描。
  静态初始化、模块/捕获依赖及不支持的成员仍可能阻断完整 record。
- IconInfo 配对测试新增 FunctionTree 创建包装器：类定义来自实际 ABC，
  外围创建/return 图为明确的合成测试夹具，不冒充原始完整 record。检查
  每次调用产生独立 class/prototype/method、既有行为及无 $hole/$value/extends，
  并检查错误 owner、错误 constructor、缺失定义拒绝和嵌套成员位置范围。
  产物 /tmp/abcd-icon-info-class-ast.mjs；类名包装/字段表示仍未最终风格验收。
- 8 项定向测试通过（5.76 秒），日志 /tmp/abcd-class-ast-verified-build.log、
  /tmp/abcd-class-ast-verified-tests.log。补全 literal I/O 失败分类后再次构建，
  3 项相关集成测试通过（2.17 秒），日志
  /tmp/abcd-class-ast-record-final-build.log、
  /tmp/abcd-class-ast-record-final-tests.log。未运行全量测试或 RSS 专用验收。
- 五个阶段一输入实际刷新至 build/stage-one/value-class-ast-pass-2：
  Immersive 4/31/10，MultiCommunityApplication 11/34/9，pasteboard 0/10/9，
  Push modules 0/15/9，widgets 0/1/9（完整/失败/无模块），合计 15/91/46。
  均无 legacy writer；汇总 /tmp/abcd-class-ast-batch-summary.json。
  没有新增完整 record，阶段一未完成。下一步处理创建后的静态初始化和
  类成员的模块/捕获绑定；CommonConstants/HomeConstants 仍在前者受阻。

### 2026-10-01：类创建进入共同求值规划

- DescribeEvaluation 现在按显式 kClassHeritage 输入处理 ValueClassCreation，
  保留真实 protocol read、class creation 的顺序和结果身份。语言 heritage
  按输入顺序求值；definite Hole 仅表示无 extends，null/undefined 不改写成
  无继承。混合 Hole/语言或其他 VM 内部域、错误 role/purpose 均拒绝。
- HoleProtocol 对正常值图仅开放闭合 copy 到 class heritage 的用途；原有
  phased comparison/rethrow 保持独立限制。没有把内部 Hole 升级为语言值。
  共同求值检查同时要求 class/function creation 的最终表达式根处于相同
  environment epoch，防止跨 push/pop 搬移捕获时点。正文、捕获、derived
  constructor 和真正继承语法仍由下游负责，求值准入不表示这些已经通过。
- 新增三个 dynamic profile 的实际字节码导入/简化/求值检查，覆盖
  Hole/null/undefined；1/200/201/402 次创建覆盖共享 heritage、未使用结果
  的保留、副作用隔离、伪造移动、错误协议和 Hole 逃逸拒绝。
- class_tree_sample 另对真实 IconInfoModel 16948 完整且未修改的入口值图
  执行 BuildEvaluationPlan/VerifyEvaluationPlan，再构建 IconInfo 表达式。
  配对行为测试仍通过；这证明完整入口的求值层可用，不证明完整 record
  AST、IconInfoModel 方法正文或整个应用通过。
- 构建成功，6 项定向测试全通过（5.34 秒），真实入口 fixture 更新后
  class_tree_source_test 再通过（0.97 秒）。日志
  /tmp/abcd-class-evaluation-final-build.log、
  /tmp/abcd-class-evaluation-final-tests.log、
  /tmp/abcd-class-evaluation-record-build.log、
  /tmp/abcd-class-evaluation-record-tests.log。未恢复全量测试、RSS 或其他语料。
- 下一阻断是表达式 AST 的类树绑定，以及仅在内部消费 absent heritage。
  静态初始化和正式 record 输出尚未完成，完整 record 数保持 15，Goal 继续。

### 2026-10-01：共同类创建表达式组装与真实 IconInfo 对照

- BuildClassTree 从父函数真实 class creation、constructor context 和 literal
  buffer 组装构造器及 wire 顺序的成员，所有正文仍通过 PreparedFamily 和
  共同树打印器。ClassMethodSites 新增精确 occurrence 二分查询，避免每个
  成员重复扫描同一 target 的全部引用；不使用配对源码生成正文。
- PrintClassTree 输出 native class expression，保留各成员相对位置与整体
  offset，类成员和正文缩进由共同 printer 处理。通过 computed-property
  NamedEvaluation 保留 class.name，避免引入会遮蔽可变模块绑定的词法类名。
  该包装及构造器内赋值仍需后续表示质量改进，不宣称最终源码风格验收。
- 真实 Immersive IconInfo 已输出 constructor(info, icon)、两个实例属性
  赋值、getInfo/getIcon。独立配对测试检查返回身份、receiver、异常、实例
  与原型属性描述符、派生实例，以及重复创建的 constructor/prototype/method
  独立性。测试不证明任意原型 setter 条件下 TS 字段与赋值等价，也不证明
  完整 IconInfoModel record 或宿主应用通过。
- 生产及 fixture 构建成功，6 项定向 CTest 全通过（4.88 秒）：类 context、
  类配对、构造器配对、实际类 record 边界、barrel 和 Constants 回归。
  日志 /tmp/abcd-class-tree-indent-build.log、
  /tmp/abcd-class-tree-indent-tests.log；产物 /tmp/abcd-icon-info-class.mjs。
  未运行全量测试、RSS/基准、其他语料或代理。
- 本次只组装类创建表达式；静态初始化、捕获和正式 record 的 class value
  接入仍未完成，完整 record 数保持 15。下一步把类表达式纳入共同求值与
  AST/record 输出，并保留创建后静态初始化的执行顺序。阶段一 Goal 继续。

### 2026-10-01：真实基类构造器贯通共同函数族和打印器

- ClassTreeContext 现在以 variant 区分真实 buffer member 与 constructor
  occurrence。构造器从父 record 已准备且未修改的 FileFunction 绑定，
  检查原始创建与工作操作、heritage use、definite Hole、目标/角色/ABI。
  每次仅做 O(log M + log R) 查找及目标 annotation 读取，不按每个类
  重新解码和扫描父 record。null/普通继承值仍不冒充 base constructor。
- 按 pinned OH7 runtime_stubs-inl.h RuntimeSetClassInheritanceRelationship，
  Hole 会设置 CLASS_CONSTRUCTOR，因此允许目标初始 NONE 或 BASE。
  owner 向共同 FunctionInput 提供已初始化 this；PreparedFamily 和现有
  迭代类成员 printer 输出 constructor，仍执行 family/ABI/source census
  与实际 destination 检查，不能把 constructor 片段放进普通函数位置。
- ABC-only 真实 CommonConstants 构造器 7340 经共同路径输出
  constructor() { return this; }。HomeModel 构造器 16666 也输出真实实例
  初始化 this.tabBarList = []、this.initTabBarList() 和 return this。
  产物 /tmp/abcd-common-constructor.mjs、/tmp/abcd-home-constructor.mjs。
  没有复制配对源码、伪造空 body 或调用旧 constructor writer。
- 独立配对测试分别验证 native constructor 不可普通调用、newTarget
  prototype 读取、派生调用、实例身份、prototype.constructor descriptor；
  HomeModel 检查数组逐实例身份、字段初始化先于方法 getter/call、receiver
  和异常传播。两边替换 initTabBarList 方法来隔离构造器行为，不能据此
  宣称该方法或完整类通过。输出目前仍是赋值形式，未验收高层字段声明。
- 新增错误创建/长度篡改/null heritage/预算及 destination 负例。
  9 项定向 CTest 全通过（5.10 秒）。构建日志
  /tmp/abcd-constructor-accept-build.log，fixture 另见
  /tmp/abcd-constructor-instance-build.log，测试日志
  /tmp/abcd-constructor-accept-tests.log。未运行全量测试、RSS/基准或其他语料。
- 正式 record 尚未接 class expression 组装，因此完整 record 数仍为 15。
  下一步将已恢复 constructor、buffer methods 和静态初始化纳入同一个
  class expression 的创建、捕获与求值顺序；derived/super 保持原有义务。
  Goal 仍进行中，构造器片段通过不等于整个类或阶段一通过。

### 2026-10-01：简化后值流的类继承分类接入

- 原 ProveClassHeritage 只处理 decoded register 流，无法直接为共同 SSA
  类创建提供继承输入。新增 AnalyzeValueHeritage 并实际接入正常函数
  BuildFunctionValues：保存工作 revision、类操作和真实 heritage use，
  识别 definite Hole 为无 extends、纯副本链上的 null 为 derived-null；
  其他语言值保留 unknown 及其运行时失败可能，undefined 不视为无继承。
  混合 Hole/语言值、environment 等内部域拒绝，不能伪造 source operand。
- 逐值缓存纯副本链，显式 path 栈检查环；全体类合计 O(O+V) 时间和
  O(V+C) 空间，不按每个类重走共享链。工作量、数组及扩容容量进入现有
  预算，FileFunction 同步计入继承分析 charge。产物仅适用于 unchanged
  graph，后续重写需重建，不是删除 Hole 或发布 class body 的凭证。
- 两种 IC、三个 profile、Hole/null/undefined、1/200/201/402 的既有类
  导入组合新增继承结果检查、错误 role/混合域/资源及 work limit 负例。
  8 项定向 CTest 全通过（5.09 秒），包含真实四个类入口和既有模块、
  Constants 行为对照。日志 /tmp/abcd-value-heritage-final-build.log、
  /tmp/abcd-value-heritage-tests.log；本轮未跑全量测试或 RSS/基准。
- 真实入口 7312、7531、16638、29031 的 PC 9 均解析为 absent；已有
  conditional-values 输出现在报告该输入绑定，仍 source_complete=false。
  CommonConstants 原始入口还包含 DEFINEMETHOD 静态初始化及其调用；
  class buffer 为空不代表整个类无 body。共同 ClassTreeContext 当前只
  覆盖 buffer method，缺 constructor owner 及完整 class expression。
  下一步需要贯通 constructor/member/static initialization，不能把空
  buffer 当成可省略构造器和初始化的依据。完整 record 数仍不增加，
  阶段一 Goal 保持进行中。

### 2026-10-01：类创建进入共同值流，保留继承与闭包义务

- 真实类 record 的首个阻断确定为 DEFINECLASSWITHBUFFER 值导入，而非
  已有类方法 body writer。现将普通现代类创建接入 ImportNormalValues：
  ValueClassCreation 保存 constructor/literal pool identity、16 位 length
  和 IC；独立 kHeritage protocol use 绑定实际 parent SSA，acc 只在正常
  完成后获得新结果。Hole（无 extends）、null 与 undefined 保持不同值。
- 沿用 pinned ClassCreation 的环境/module/heap/user-code/allocation
  效果及异常语义，不把创建当成纯分配或可消除副本。逐 occurrence 结果
  保持独立；导入 verifier 比对全部元数据、操作数及效果。正常和相位
  方法均保留类的环境需求，cell 初始化将未验证类成员列入闭包写前提。
- 构建及 13 项定向 CTest 全通过（9.95 秒），包括两个 IC 宽度、三个
  dynamic profile、三种 heritage、1/200/201/402 创建规模、篡改元数据
  和协议用途、消除保护、已有模块与 Constants 输出。补充类闭包前提
  测试单独通过（0.79 秒）。日志 /tmp/abcd-class-value-final-build.log、
  /tmp/abcd-class-value-final-tests.log、/tmp/abcd-class-cell-tests.log。
- value_import_test 中旧 module store 断言曾要求简化后仍为 ValueId，
  与此前已实现的 primitive copy propagation 不符；已改为明确验证 store
  与 return 都是同一 JS undefined。类导入自身的新增组合已通过。
- ABC-only 真实 Immersive 入口 7312、7531、16638、29031 均报告
  values_prepared=true，操作数分别 21/15/15/15，保留子成员义务
  2/1/1/1。新增 value_class_record_test 固定输入身份验证此边界。
  CommonConstants 正式 record 已越过 PC 9 import_stage，仍以 kValue
  拒绝；/tmp/abcd-class-common-values.json 和
  /tmp/abcd-class-common-record.json 保留实际检查结果。
- 未新增完整类源码，不更新 15 个完整 record 为更高覆盖率。共同求值、
  表达式构建与实际操作数检查明确拒绝尚未绑定的 ValueClassCreation；
  下一步必须接构造器/成员 occurrence、heritage Hole 消费、捕获和同一
  class AST 打印，不能仅放宽这些拒绝或使用伪造空 body。阶段一 Goal
  保持进行中；未运行全量测试、RSS/基准、其他语料或代理。

### 2026-10-01：共同 record 接入原生模块导入与转导出

- BuildRecordTree 复用 ModuleEnclosure 和 BindModuleAliases，保留全部
  ordinary/namespace/未使用导入、请求顺序、间接与 star exports。导入和
  本地槽位使用统一 inventory；首次本地写仍在原位置声明，后续读写共享
  binding，不复制 import，也不把本地读错误声明为外部导入。重复 scope
  名及显式 export 名在输出前拒绝。仍只接受单一无条件入口区域。
- inventory 排序检查 O(N log N)，逐 read/实际树校验线性；名字 arena
  扩容、seen 表和输出 staging 纳入现有预算。资源、工作量、文件变化
  错误保留类型。未放宽 detached-method 的只读模块校验器。
- 新 fixture 实际经共同 record 生成 ESM，独立 reference 比较副作用顺序、
  调用 receiver、live binding、namespace 身份、local alias、循环 TDZ、
  未使用 default import 的链接失败、star 默认排除和显式导出优先。
  冲突/输出预算/资源耗尽负例通过。真实 Immersive 三个 Index 的配对
  oracle 同时验证原生 reexport 表示、导出集合、身份、变更传播、依赖
  顺序和缺失导出拒绝；依赖使用测试替身，不能当作其函数体或应用验收。
- 构建成功，10 项定向 CTest 全通过（8.10 秒）。日志：
  /tmp/abcd-record-import-verified-build.log、
  /tmp/abcd-record-import-verified-tests.log。首次测试曾发现未编译的
  module_aliases 目标及 oracle 用 Object.keys 提前触发 TDZ；已补编译，
  改用属性名枚举并独立检查 TDZ，再执行上述完整定向集合。
- 阶段一五个输入已实际刷新：Immersive 4 完整恢复/31 恢复失败/10 无模块；
  MultiCommunityApplication 11/34/9；pasteboard 0/10/9；Push modules
  0/15/9；Push widgets 0/1/9。共 152 records，15/91/46。
  所有产物走 value_tree、未用 legacy writer；executable 仍为 false。
  Multi 的 11 个包括 5 个转导出模块和 6 个字面量初始化模块，本轮未做
  它们的独立配对验收，不能把 source_complete 数当成阶段一通过数。
- 产物：build/stage-one/Immersive/value-import-pass-1，以及
  build/stage-one/value-record-import-pass-1/<应用>/<ABC名>；批量日志
  /tmp/abcd-record-import-batch.log、/tmp/abcd-record-import-all-batch.log。
- 下一阻断是共同 record 的子函数/类及其捕获、初始化和宿主绑定贯通。
  冗余 bare import、机械括号和末尾 discard 表示仍待改善，整体源码风格
  尚未验收。未运行全量测试、RSS 专项、基准、其他语料或代理；Goal 保持进行中。

### 2026-10-01：唯一外部使用的字面量直接进入模块初始化

- 正常流的共同求值规划不再把已验证初始化组的 own-write 接收者计为
  外部值使用。每个 producer 的投影缓存一次；唯一外部使用沿用效果区间
  与独立求值顺序检查后可以内联，多外部使用、观察屏障、跨区域仍保留
  快照。异常相位的初始化仍固定，未放松其异常或别名约束。
- Constants 正式 record 输出从根 $value0 搬运变为 let ROUTES = [...]，
  独立配对 oracle 新增无根临时值断言，原有 TDZ、对象身份、属性顺序、
  descriptor 和继承 setter 检查继续通过；标量早载/晚载数组模块也直接
  输出 let answer = [42]。配对源码不进入生产路径。
- 对象/数组 1/200/201/402 项分别覆盖唯一使用、共享身份和观察屏障。
  伪造唯一 consumer 或越过屏障的内联会被拒绝；Node 执行 24 个生成函数，
  共享对象严格相等仍为 true。模块读取、phased literal、arguments 快照
  等共用路径一并验证，11 项定向 CTest 全通过（8.77 秒）。构建/测试日志：
  /tmp/abcd-literal-use-final-build.log、/tmp/abcd-literal-use-final-tests.log；
  初始目标构建另见 /tmp/abcd-literal-use-build.log。
- 已刷新批量产物 build/stage-one/Immersive/value-root-literal-pass-1：
  Constants 1282 字节，无根快照；manifest 仍为 1 完整恢复、34 恢复失败、
  10 无模块上下文。其他四个输入本轮未刷新，不能外推新的阶段一覆盖率。
- 模块集成的实际阻断仍存在：BuildRecordTree 入口拒绝 requests/imports/
  namespace/间接及 star exports，尚未连接已有 BindModuleAliases 与
  ModuleEnclosure；子函数也未贯通。Constants 的机械括号和模块末尾
  丢弃表达式仍保留，不能宣称整体表示质量或阶段一完成。未运行全量测试、
  RSS 专项、基准、其他语料或代理。

### 2026-10-01：共同树消除可安全合并的缓冲对象占位

- 共同表达式构建按实际 UTF-16 属性名定位缓冲区最后一个同名位置，
  在保持 own-write RHS 单调求值顺序时将最终值写回该位置。保留原始键
  插入顺序，不产生先写入后覆盖的占位表达式；逆序和重复 own-write
  无法合并时保留追加形式，不丢弃 RHS。只消费既有 LiteralPlan 的未观察
  初始化证明及 primitive 缓冲值，配对源码仍完全在测试侧。
- ObjectExpr 保留原始缓冲前缀长度；独立树检查验证实际键、每次 own-write
  的唯一位置、RHS 顺序、最后同名缓冲项和原始 PC。覆写值归属 store，
  allocation 仍拥有对象及未覆写的缓冲常量；不以 printer 隐藏属性绕过验证。
  排序/二分代替逐写扫描，名称比较和存储计入现有预算。
- Immersive Constants entry 8889 的真实输出已无 title/name/children/params
  的 null 占位，输出 1316 字节；独立配对 oracle 同时验证行为和该项表示要求。
  2/200/201/402 项覆盖正序、逆序、缓冲重复键、同名不同 NameId，另有
  篡改缓冲长度、错误覆写位置及 RHS 重排反例。Node 独立执行 12 组生成
  函数，核验 own keys、descriptor、__proto__ 数据属性及继承 setter 不调用。
- 9 项定向 CTest 全通过（6.98 秒）：literal plan、缓冲运行时对照、
  tree structure、function tree、class context、record tree/运行时、
  Constants 独立对照、phased literal。构建和测试日志分别为
  /tmp/abcd-buffer-fold-final-build.log、/tmp/abcd-buffer-fold-final-tests.log。
- 当前批量 Immersive 仍为 45 records：1 完整恢复、34 恢复失败、10 无模块。
  产物 build/stage-one/Immersive/value-buffer-fold-pass-1，manifest 已核对。
  Constants 仍有根临时变量、机械括号及单行嵌套表示，完整表示质量未验收；
  模块 import/子函数贯通及其余阶段一样本仍未完成。未执行全量测试、
  RSS 专项、基准、其他语料或代理，不以工具成功或单 record 代替整体完成。

### 2026-10-01：对象属性原生表示与常量传播集成回归修复

- 共同 tree printer 复用 ClassifyObjectKey，将普通 ASCII IdentifierName
  属性输出为 title: 等原生写法；__proto__ 和需引用的名称仍保留计算属性，
  不引入对象原型 setter 语义。属性顺序、重复定义和 origin/字节范围保留。
  现有类属性范围回归同步更新，__proto__ 的 1/2/200/201 属性规格通过。
- 定向回归发现此前 primitive copy 传播会把 phi 输入和条件 arguments
  snapshot 的 protocol 输入替换为字面量，分别破坏默认参数前的 SSA
  流验证和快照求值形状。现只替换普通操作数，保留两类 SSA 输入及其
  定义；独立 rewrite verifier 也拒绝这两类字面量替换。未放宽验证器。
- abc_semantic、class_tree_context_test、creation_tree_context_test、
  literal_plan_test、value_simplify_test 构建成功。6 项定向 CTest 全通过
  （8.66 秒）：类上下文、literal plan、SSA simplify、真实 Constants
  独立行为对照、phased literal 和 51 案例 arguments snapshot 运行时对照。
  日志 /tmp/abcd-object-key-build.log、/tmp/abcd-object-key-tests.log。
- Constants 的普通属性拼写改善，独立 oracle 仍检查值、顺序、descriptor
  和对象身份；占位 null/重复键检测兼容新拼写，缺口仍如实保留。
  本次没有消除缓冲占位、没有宣称整体表示质量达标，也未刷新批量覆盖。
  完整模块 import/子函数贯通及阶段一整体验收仍未完成；未运行全量、
  RSS、基准、其他语料或代理。

### 2026-10-01：真实 Constants 嵌套初始化首次贯通共同 record

- LiteralPlan 记录内部 allocation 到唯一外层 initializer RHS 的依赖，
  单块无边场景用线性栈验证嵌套初始化区间。拒绝共享/逃逸子对象、外部
  可观察操作，以及外层分配之后才产生的普通 RHS 快照。数组/对象身份
  保留；没有把 allocation 当作 primitive，也没有解除 EarlyValue。
- kNestedLiteral 明确区分嵌套表达式与独立根；共同表达式构建按反向
  allocation 顺序迭代构建，不递归、不生成先使用后赋值的快照。求值、
  树流、操作数和 origin 检查同步消费依赖，属性写入 PC 保留在 property，
  子分配保留自身 PC。篡改依赖表、placement 和不完整表都拒绝。
- Immersive Constants entry 8889/record 8755 已通过正式共同 record 入口
  生成完整恢复源码，无旧 writer。独立读取配对 Constants.ts 的测试使用
  Node 类型擦除，仅在测试侧执行参考模块；ABC SHA 固定。比较 ROUTES
  的值、属性键顺序、descriptor 和双向对象身份关系，并验证 ESM TDZ、
  export 集合及继承索引 setter 不被调用。行为对照通过。
- 表示质量仍未通过：输出保留缓冲 null 占位和重复键，需继续在共同
  恢复中处理属性覆盖及高层表示。原接口 Route 等源码类型信息也不由
  本次行为对照证明。不能据 source_complete=true 宣称阶段一完成。
- 嵌套深度 1/2/200/201/402、提前观察、外部效果与篡改反例测试通过。
  5 项定向 CTest 全通过（4.59 秒），随后实际 Constants oracle 再通过。
  日志 /tmp/abcd-nested-final-build.log、/tmp/abcd-nested-final-tests.log。
- 批量 Immersive 45 records 当前为 1 份共同源码、34 恢复失败、10 无模块；
  输出 build/stage-one/Immersive/value-nested-pass-1/sources/default/src/main/ets/constants/Constants.mjs，
  manifest 同目录根下。其余 ABC 本轮未重跑，旧 152-record 结果不能当作
  当前整个阶段的新覆盖统计。未执行全量/RSS/基准/其他语料/代理。

### 2026-10-01：批量共同 record 路径实际运行全部阶段一输入

- abc_structure --value-source 不再以入口占位 refusal 跳过每个 record。
  现在建立 source paths/roles，调用与 abc_semantic 相同的 BuildRecordSource
  required-value 分支并传入真实输入身份。成功才发布 source，失败保留
  实际 typed failure；所有 value record 显式标记 legacy_writer_used=false，
  recovery completeness 与可执行验收分离。没有回落到旧 writer。
- 全部 5 个 ABC 批量运行成功结束，但没有任何完整共同 record 源码：
  Immersive 45 records（35 恢复失败、10 无模块），MultiCommunityApplication
  54（45/9），pasteboard 19（10/9），Push Kit modules 24（15/9），
  Push Kit widgets 10（1/9）。合计 152，恢复失败 106，无模块上下文 46，
  完整源码 0。不能用工具退出 0、方法草稿或独立夹具替代阶段一完成。
- 当前逐输入身份、record 路径及失败结果位于
  build/stage-one/value-record-pass-2/<应用>/<ABC名>/manifest.json，
  Constants record 8755 真实报告 kValue，嵌套初始化阻断仍未解决。
  同时 record adapter 的 import/子函数支持仍缺失，需沿通用方案继续。
- abc_structure 构建成功；source_route_cli_test 和 structure_cli_test
  两项定向回归通过（4.75 秒）。/tmp/abcd-batch-final-build.log、
  /tmp/abcd-batch-tests.log；旧 shared 模式在同一 CLI 回归中保留。
  未运行全量测试、其他语料、RSS 专项/基准或代理。阶段一保持未完成。

### 2026-10-01：纯 SSA 常量复制传播解除标量晚值阻断

- 原 SimplifyValues 只传播 ValueId 复制代表，数字等 primitive 的
  ValueCopy 仍占用 SSA 节点，导致分配后 LDAI 被 LiteralPlan 拒绝。
  现在在复制/phi 代表压缩后单次遍历使用点，以原始 primitive 替换
  纯复制根；复用批次的原子提交、使用链维护、死代码及来源处理。
  没有移动分配/副作用，也没有放宽 LiteralPlan 的 EarlyValue。
- 独立 VerifyValueRewrite 从变换前固定代表读取常量，逐位核对替换，
  不相信变换后的根操作数。1/200/201/4096 复制链覆盖整数、null、
  undefined、负零及带 payload 的 NaN，错误常量与 effectful copy 拒绝。
- 正式 record 的 ABC 编码夹具增加分配后加载 42 的数组路径；与原
  分配前快照路径一起通过原生 ESM 的值、descriptor、prototype 和
  继承 setter 验证。7 项定向测试全通过（3.17 秒），构建/测试日志
  /tmp/abcd-literal-regression-build.log、/tmp/abcd-literal-regression-tests.log。
- 真实 Constants 仍未生成源码；调用栈进一步确定当前 kLateValue 为
  数组分配后创建的 buffered ValueObjectAllocation（literal slot 216），
  不是标量复制。必须接入嵌套分配依赖及构建顺序，不得以常量复制规则
  处理对象身份。证据 /tmp/abcd-literal-nested-debug.log。阶段一仍未完成，
  未执行暂停的全量测试、RSS/基准、其他语料和代理。

### 2026-10-01：密集数组初始化的共同源码输出

- LiteralPlan 复用现有新分配/不可观察验证，支持 ValueArrayAllocation
  后从 0 开始的连续 ValueIndexedWrite。共享属性 arena 增加类型化
  AggregateKind，数组键必须等于元素序号，printer 输出原生数组字面量。
  流、操作数、结构和常量解析同步验证数组/对象区别，没有生成 [[Set]]。
- 1/2/200/201 元素经过共同树；拒绝稀疏索引、错误接收者、改成对象、
  初始化前观察及晚产生值。真实编码 ABC 夹具经正式 record 路径生成
  数组导出；Node 验证值 42、length、自有属性 descriptor、原型，并在
  Array.prototype[0] 安装抛错 setter，验证输出不调用继承 setter。
- 5 项定向 CTest 通过（4.13 秒），最终类型检查调整后重跑 record 与
  literal 三项通过。日志 /tmp/abcd-array-final-build.log、
  /tmp/abcd-array-final-tests.log、/tmp/abcd-array-kind-tests.log。
- 仅支持分配前已可用的元素。后加载数字仍是 SSA copy，也触发
  EarlyValue；不能借本次数组支持解除晚值/效果顺序检查。真实 Constants
  的嵌套初始化 kLateValue 尚未解决，后续需处理字面量依赖、分配与
  RHS 顺序及共同树构建顺序。阶段一未完成；未执行全量/RSS/基准/代理。

### 2026-10-01：索引自有属性写入进入共同值 IR

- ValueIndexedWrite 独立保存立即数索引与可选 IC 槽位，导入 base/存储值
  两个操作数，保留累加器、原有效果和异常信息。没有转换成 [[Set]]，
  没有把 index 当作常量池槽位，纯操作删除仍拒绝该写入。尚未具备
  数组初始化的源码表示，树构建仍明确拒绝直接输出该操作。
- STOWNBYINDEX 的 8 位 IC、16 位 IC、wide 无 IC 三种编码，分别经过
  1202/1206/1301 与 1/200/201/402 项导入验证。测试包含 UINT32_MAX
  索引、IC 缺席/零值区别、索引篡改和误改为普通属性写入的反例。
- 真实 Constants 8889 已越过 PC 94 的 import_stage 失败；常量池与
  对象缓冲解析成功。后续 BuildEvaluationPlan 失败已用调用栈定位到
  literal_plan.cc 的 EarlyValue/kLateValue：对象分配之后才产生嵌套
  属性值。需要实现保留分配/求值顺序的嵌套数组/对象初始化，不能移除
  原检查后直接把效果前移。真实 record 尚未生成源码。
- abc_semantic/value_import_test 与相关目标构建成功；5 项 CTest 通过
  （5.09 秒）。证据：/tmp/abcd-indexed-build.log、
  /tmp/abcd-indexed-final-build.log、/tmp/abcd-indexed-tests.log、
  /tmp/abcd-indexed-literal-debug.log。阶段一与表示验收仍未完成；没有
  恢复全量测试、其他语料、RSS/基准或代理。

### 2026-10-01：正式 record 入口的模块共同树分支

- BuildRecordSource 在 RequireValueTree 且提供输入身份时，复用 record
  flags/唯一入口检查，调用共同 FunctionInput/Values/Evaluation/FunctionTree
  与模块 printer；不会回落旧 writer。导出名来自 ABC 模块元数据，保留
  相邻同名槽位规则，首次写入声明与后续赋值沿用共同树。现阶段明确拒绝
  imports/子函数及非单块初始化；没有推断未经建立的全局环境。
- abc_semantic 的 --record-source --value-source 已调用这一正式分支；
  abc_structure 的批量 required-value 分支尚未接入。独立 ABC 夹具生成
  answer 声明/导出，原生 ESM 验证执行前 TDZ 和执行后 answer=42；错误
  输入身份及不足输出额度拒绝。保留 source_complete 与可执行验收的区分。
- 真实 Immersive Constants entry 8889 经新入口到达值导入 PC 94：
  STOWNBYINDEX（对象寄存器 7、索引 0）尚未表示，返回 typed import_stage
  失败，而非旧入口的 context 拒绝。不能将自有属性定义直接改为 [[Set]]。
  尚未生成真实 Constants 源码，语义及源码风格验收没有完成。
- abc_semantic 和相关测试构建成功；5 项相关 CTest 通过（1.14 秒）；
  最后名字索引调整后两项 record 测试再通过（3.31 秒）。日志：
  /tmp/abcd-record-final-build.log、/tmp/abcd-record-tests.log、
  /tmp/abcd-record-final-tests.log、/tmp/abcd-record-constants-refusal.json。
  后续接通索引自有属性及数组字面量恢复，并接入批量入口；未恢复暂停的
  全量测试、RSS/基准、其他语料或代理。阶段一仍未完成。

### 2026-10-01：共同树的模块槽位初始化与更新

- ValueModuleWrite 现在通过类型化 ModuleStoreConstant 绑定模块槽位与源码
  符号；首次初始化在原操作位置形成 let 声明，后续写入形成赋值。
  共同作用域、执行流、操作数和来源检查区分本地模块声明与外部模块别名，
  不把所有模块槽位预先初始化。当前初始化声明仅支持根块位置。
- 共享 printer 增加模块体片段，不打印函数外壳，将唯一尾部 entry return
  转为带括号的丢弃值表达式。参数、捕获、this、await/yield 和嵌套函数等
  尚未具备模块上下文的组合仍拒绝；正式 record 适配尚未接入此入口。
- 新增三 profile、1/200/201/402 次写入测试，覆盖槽位/目标篡改、重复
  初始化、首次写入缺少声明和只读绑定等反例。独立 Node 原生模块测试
  验证链接后执行前 TDZ、执行后导出值及与参考模块结果一致。
- module_store_tree_test、module_store_tree_source_test、module_evaluation_test、
  tree_structure_test 四项通过（4.51 秒）；补充反例后重跑新增两项通过。
  构建日志 /tmp/abcd-module-build.log、/tmp/abcd-module-final-build.log。
- 这是模块共同树能力，尚非真实 record 完成证据。下一步在正式入口
  从模块元数据绑定槽位、初始化及导出，贯通真实 Constants record 8889；
  阶段一、整 record 与源码表示验收仍未完成。未执行全量测试/RSS/基准。

### 2026-09-30：真实 onRestore 嵌套成员调用贯通

- Pasteboard 5465 的 JSON 全局读取被显式 VM receiver 的第二个 use 固定成
  临时变量，切断外层 hilog.info 的效果区间。相位表达式现在接纳有序全局
  读取，并仅在 callee 来自属性读取且 base/receiver 解析为同一 SSA 值时，
  将 receiver 视为同一原生引用。保留其他 use、连续效果区间、catch 快照
  和无复制走廊检查；CallPlan 与 TreeOperands 独立要求同一属性 base 节点。
- Pasteboard 与 Immersive 的真实 onRestore 生成完整共同 async 方法草稿；
  独立配对行为 oracle 验证全局/getter/调用顺序、receiver、全局重绑定、
  不错误 await 丢弃结果，以及 undefined/null/Symbol/对象异常身份。
  三 profile 的命名/计算属性、200/201/402 项及错误 receiver、重复引用、
  效果间隙、catch/handler 变化均有定向相位测试。输入 SHA 固定在测试侧。
- 相关生产及 C++ 测试目标构建成功；9 项定向 CTest 全通过（8.46 秒），
  call_plan/phi_call 两项亦通过（3.63 秒）。证据为
  /tmp/abcd-restore-regression-build.log、/tmp/abcd-restore-regression-tests.log、
  /tmp/abcd-restore-call-tests.log；原红例 /tmp/abcd-restore-red.log。
- 整份 Backup record 在类组装 PC 14 拒绝，已定位为构造器 5493 的
  kSuperclass：COPYRESTARGS/SUPERCALLSPREAD 路径要求父类在每次构造时
  始终为 JSFunction，外部 import 本身不能证明，不能解除既有 Proxy-super
  拒绝。Push Kit onRestore 6865 在相位输入 Values 阶段拒绝；实际方法
  读取外层 lexical cell，而当前 family root 使用 closed capture mode。
  后续须接入 record 初始化及创建环境，不能把捕获替换为常量。
- 共同 record、初始化和源码表示 gate 未完成。未恢复全量测试、其他
  语料、RSS/基准或代理；继续实际集成阻断。临时诊断代码已移除，CLI
  再构建成功（/tmp/abcd-restore-clean-build.log）。

### 2026-09-30：共同 async 方法的迁移桥接与发布

- 真实 onBackup 共同草稿已完整；集成回归暴露 record 方法桥接的
  NONE-only 限制、1202 async class member 拒绝和 PreparedFamily 的
  plain-only callable 列表。现在入口接受已支持 profile 的 async，
  PreparedFamily 保留实际 creation/phase owner 已验证的 async/generator
  callable；原始协议、类型、strictness、ABI 和 destination 校验继续执行。
  不另建协程 writer，未解除尚不支持的 generator class 旧组装限制。
- 六个三 profile × 静态/实例 async 方法通过实际选中方法桥接，比较
  输出并核对保留 segment/receipt；全部 async/generator method fixture
  也经过 PreparedFamily 与 FamilyEmission，再执行原独立生成 JS oracle。
  实际 onBackup 草稿仍不能令整体 record 成功：其余类组装/同 record
  方法继续阻断。pasteboard onRestore 5465 已定位为共同表达式构建缺口，
  配对源码含嵌套 JSON.stringify(bundleVersion)，待继续修复，不算完成。
- abc_semantic、abc_structure、class_tree_context_test、creation_tree_context_test
  构建成功；8 项相关 CTest 全通过（4.52 秒），含 async/generator methods、
  snapshots、真实 rest record slice 和 structure CLI。红例
  /tmp/abcd-async-bridge-red.log；最终证据
  /tmp/abcd-async-bridge-regression-build.log 与
  /tmp/abcd-async-bridge-regression-tests.log。独立实施、自查，无 CC/子代理。
- 阶段一及默认共同 record 迁移仍未完成；继续真实 onRestore 阻断。

### 2026-09-30：共同 Logger 方法的 record 模块上下文集成

- 完整 rest 方法草稿进入 --record-value-slice 时仍在 record 类创建
  PC 9 失败。根因是 BuildRecordSource 只传旧 import 名单，未将现有
  ModuleEnclosure 交给共同函数族；现在为显式迁移创建同一输入/record
  的 enclosure，核对其名字与实际 record import 输出一致，沿已有
  GlobalScope 传递。名字冲突或无法对应仍拒绝，不试旧方法体。
- 四个真实 Logger 方法均在 record 中保留完整 value_tree segment、
  receipt、方法位置和原 PC；与独立共同草稿逐字对应，导入仍为 hilog。
  其余 record entry/constructor/方法仍是旧路径，整份 record 继续明确
  incomplete；此项不是默认共同 record 迁移或完整应用验收。
- abc_semantic、abc_structure、module_enclosure_test、class_tree_context_test、
  creation_tree_context_test 构建成功；8 项相关 CTest 全通过（5.23 秒），
  含四真实方法及其行为 oracle、原 record slice、module enclosure、
  参数快照、rest 与 structure CLI。红例 /tmp/abcd-rest-record-red.log，
  /tmp/abcd-rest-record-regression-build.log 与
  /tmp/abcd-rest-record-regression-tests.log 为最终证据。独立实施、自查。
- 接下来清除共同方法进入 record 时的旧 callable 限制，并推进整份
  record 的共同输出；默认迁移、初始化与源码表达质量 gate 仍开放。

### 2026-09-30：真实 Logger rest 参数接入共同源码主干

- 阶段一 Pasteboard 的 info/warn/debug/error 在共同值导入 PC 8 的
  COPYRESTARGS 处失败；现在保留独立 ValueRestArguments 分配及原 PC，
  经共同参数语法、FunctionTree、ABI 与 printer 恢复原生 ...args。
  复用原方法 ProveRestPrologue，核对纯 ABI 入口拷贝、唯一分配与 store、
  length/最后参数；不把第一个物理实参当 rest 数组。保留原槽读取、重复
  copy、GETUNMAPPEDARGS、回跳和异常入口的拒绝。allocation 的资源失败
  按现有语言契约排除，未新增证明或发布框架。
- 30 个完整文件函数族覆盖三 profile、静态/实例、窄/宽编码、前置参数、
  未使用和重赋值；24 个协议反例均拒绝。生成 JS 验证 length、非可构造
  method、空/单个/多个/201 个实参、每次调用数组新身份及元素身份。
  四个真实 Logger 方法均生成不含缺口的共同 class-member 草稿，独立
  oracle 验证 hilog receiver、四个参数、getter/调用顺序、抛出身份、
  调用结果不被 await 和 rest 数组独立性；只是完整方法片段，不是 record
  完成或平台实机验证。
- abc_semantic、class_tree_context_test、rest_arguments_test、
  creation_tree_context_test、evaluation_plan_test、tree_structure_test
  构建成功；9 项相关 CTest 通过（6.39 秒），证据
  /tmp/abcd-rest-regression-build.log、/tmp/abcd-rest-regression-tests.log。
  原始红例 /tmp/abcd-stage-one-4604.json；四方法现状
  /tmp/abcd-rest-{4604,4631,4658,4685}.json。独立实施、自查，无 CC/子代理。
- 接下来把共同方法能力接入正式 record 输出，并改善剩余的必要性不足
  的临时变量；默认值与 rest 混合、非纯入口及整份模块/类初始化仍需
  恢复与验收。阶段一未完成；全量测试、其他语料、RSS/基准继续暂停。

### 2026-09-30：阶段一配对输入基线与 macOS record 导出入口

- 新 Goal 将本阶段范围固定为 abc_with_code 的全部五个 ABC，复核 SHA-256
  与现有 abc-index 的五条 paired 记录完全一致，均为动态 12.0.2/12.0.6/
  13.0.1；配对源码位置及逐输入结果见 tests/corpus/README.md 的阶段一表。
  本阶段必要批量反编译已授权；其他全语料、全量测试、RSS/基准仍暂停。
- 首次构建发现 abc_structure 的 Linux RENAME_NOREPLACE 无法在 macOS
  编译；改为 Darwin renamex_np/RENAME_EXCL，保留原子且不覆盖目的目录
  的语义。Darwin 还拒绝 Linux RLIMIT_AS 设置，现限 Linux 使用该保护，
  保留跨平台已有 stage 内存限制，不宣称 RSS 达标。现有非 shared 报告
  中 getrusage 的 Darwin 字节单位也已修正，未新增 RSS 测量任务。
- abc_structure/index_fixture 构建成功；structure_cli_test 通过（3.50 秒），
  包含正常发布、已有目录拒绝、静态输入、非法路径与 staging 清理。
  五份 --shared-source 当前运行均退出 0，产物在 build/stage-one/；合计
  152 record、2111 方法，57 record 产生标记的不完整源码。这只是旧
  shared record 入口的当前基线，不是共同 FunctionTree 或完整行为验收。
- 发现实际集成阻断：默认 BuildRecordSource 仍经 SemanticMethod/
  BuildEntrySource/SourceAst，近期共同函数族能力未成为 record 默认输出。
  下一步围绕真实 record 的创建点与方法集成推进，不能继续用合成协程
  fixture 代替阶段一样本贯通。阶段一未完成。

### 2026-09-30：async generator 实际 yield 与普通 await 共同贯通

- 精确匹配锁定 OH7 AsyncGeneratorFunctionBuilder::Yield 与
  FunctionBuilder::AsyncYield 的五个私有槽、SUSPENDED_YIELD/COMPLETED、
  done false/true、resolve 操作数、NEXT/THROW/RETURN 分支及返回等待。
  两个 intrinsic await 折叠为一个共同 ValueYield，以原出站 await PC 和
  incoming accumulator 为 operand；共同 FunctionTree/Printer 输出原生
  yield，普通源码 await 保留独立 AwaitSite。无专用 writer 或请求调度器。
- NEXT 的结果/模式提交保留，其余私有槽须在 continuation 不被观察；
  三条异常路由必须选择同一用户 catch，catch 不得读取原生路径跳过的
  五槽写入。共享逆序 catch-input 分析同时认识未来的 exact yield、普通
  await 与已由 owner 验证的隐式 undefined/外层 reject 尾部；不把这些
  VM 内部的新定义当成前一次暂停槽的读取。外部跳转与用户 try/handler
  边界不能进入或切开被折叠的协议，包括保留的 VM store/continuation。
- 新集成消费 120 个完整文件解码/合成元数据函数族及 39 个协议反例：
  三 profile × 三父环境 × catch × 普通 await 混用 × 8/16/32 位分支，
  以及 200/201/402 个连续 yield。实际生成 JS 验证 outbound thenable
  次数/receiver、暂停后动态 cell 重读、NEXT payload 不被 await、被
  丢弃的回调结果不被 await、排队 return 的等待及后续 next/throw、
  callback/then getter/then call/注入 THROW/RETURN reject 的 catch 选择、
  异常身份和 catch 覆盖，以及未启动 return/throw 不运行函数体。
- 有效模板红例 `/tmp/abcd-async-generator-yield-red.log`；生产及相关
  八个 C++ 目标构建成功，15 项定向 CTest 全通过（85.62 秒），新
  yield 集成为 12.72 秒。证据为
  `/tmp/abcd-async-generator-yield-regression-build.log`、
  `/tmp/abcd-async-generator-yield-regression-tests.log`；`git diff --check`
  通过。独立开发、自查，未调用 CC/子代理。
- async generator 的显式 return、yield*、method、注入 finally 及更广
  控制组合仍开放；普通 await 与直接 yield 的这项贯通不代表 C08 或
  总目标完成。下一步推进显式返回及组合；全量测试/语料/RSS/基准仍
  暂停，未宣称实际应用覆盖或 1GB 实测。

### 2026-09-30：async generator 源 await 的共同源码贯通

- 复用现有 AwaitRegion 与 catch-input 分析，核对真实 await state、resume
  result/mode、分支及用户 handler；规范化时保留 await、结果/模式提交和
  continuation，原 PC 单调重定位后进入共同 ValueProgram/ExceptionFlow。
  原生 async function* 负责请求排队，不增加 async writer 或调度器。
- 新集成消费 66 个完整文件解码函数族及 18 个协议反例，覆盖三 profile、
  三父环境、8/16/32 位分支、用户 catch 和 200/201/402 个 await。
  实际生成 JS 验证 thenable getter/call/receiver/参数次数、暂停后的动态
  cell 重读、排队 return/next/throw 等待源 await、拒绝对象身份和 catch
  覆盖；被丢弃的 catch 回调结果不被错误 await。反例含错误 state/mode、
  continuation 分支、外部入口及 catch 观察跳过的结果/模式写入。
- 有效输入红例见 `/tmp/abcd-async-generator-await-red.log`。相关八个
  C++ 目标构建成功，15 项定向 CTest 全通过（67.93 秒），包含新 await、
  async prefix、同步 yield/delegate、finally 和共同控制回归。证据为
  `/tmp/abcd-async-generator-await-regression-build.log` 与
  `/tmp/abcd-async-generator-await-regression-tests.log`；`git diff --check`
  通过。同时补齐规范化后 user try/handler 容器的现有内存记录。
- async generator 的实际 yield、显式 return、yield*、method 和更广的
  finally 组合继续开放；下一步贯通实际 yield 与 await 的共同恢复。
  C08/总目标未完成，未宣称真实应用覆盖或 1GB 实测；全量测试、全语料、
  RSS 和基准继续暂停。独立开发与自查，未调用 CC/子代理。

### 2026-09-30：async generator 入口与隐式结束的共同源码贯通

- 创建上下文保留 ASYNC_GENERATOR callable kind，共同 FunctionTree/Printer
  输出原生 async function*；复用 NormalizeGenerator 的入口寄存器检查、
  原 PC 保留、分支和 handler 重定位。锁定 OH7 Prepare、ImplicitReturn/
  DirectReturn、CleanUp：验证首次 suspend/resume、COMPLETED 状态、true
  canSuspend、resolve 的三个槽和 catch/reject 身份，仅删除这些确定的
  VM 操作，保留真实隐式 undefined load/return 与函数体效果。无 async
  body writer、额外请求调度器或新发布准入。
- 12 个完整文件解码/合成元数据函数族：三 profile × 三父环境的九个
  双回调组合，以及 200/201/402 个回调的三个组合。实际生成 JS 验证
  native prototype/name/length、调用前无效果、动态 cell 读取和 sibling
  更新、receiver/参数/次数、被丢弃的 thenable 回调结果不被 await、
  隐式结束、return 的 pending thenable 阻塞后续 next/throw 请求、抛出
  身份、未启动 return/throw 以及独立捕获环境。首轮 harness 使用
  assert.rejects 的 async 返回会探测带 then getter 的抛出理由；改为
  返回 undefined 的 rejection observer，生成源码无需改动。
- 十个协议反例检查 callee、私有 state/result/mode 读取、false done、
  错误 complete 状态、resolve/reject 槽，以及进入删除入口/直接进入
  return 绕过 settlement 的跳转。入口 slots 的源级复用仍保守拒绝；
  支持范围不包括源 await/yield、显式 return 或 async generator method。
- 中断恢复后原 PTY handle 已不存在，重新运行当前构建与相关验证。
  `cmake --build build --target abc_semantic loop_closure_file_test
  tree_structure_test class_tree_context_test control_composition_test
  async_semantics_test -j 4` 成功；11 项定向 CTest 全通过（13.73 秒），
  含新 prefix、同步 generator/delegate、await、共同控制和树约束。
  `/tmp/abcd-async-generator-prefix-final-build.log`、
  `/tmp/abcd-async-generator-prefix-final-tests.log`；`git diff --check`
  通过，独立开发与自查，未调用 CC/子代理。
- C08/总目标未完成。下一步复用现有 AwaitRegion/共同 ValueProgram
  贯通 async generator 的实际 await，再推进 yield、显式返回及组合。
  未宣称真实应用覆盖或 1GB 实测，全量测试/语料/RSS/基准继续暂停。

### 2026-09-30：yield* 非默认 API truth-test 编码贯通

- 锁定 OH7 PandaGen::{IsTrue,BranchIfFalse} 对非默认 API 发出
  callruntime.istrue/isfalse。精确委托解析现在同时接受这两个编码与
  原生 istrue/isfalse，并复用已检查的 ScalarAssignment/UnaryExpression
  映射；IC slot 保持缓存边界数据，不解释成寄存器或源码操作数。
  InterpreterAssembly 四种 handler 均调用 ToBoolean，compiler stub
  的 profiling 变体保留同一真假转换语义。无新控制/输出路径。
- 新增三个 truth 编码组合 × 三 profile × 三父环境 × 有无用户 catch ×
  16/32 位分支的 108 个完整族，以及 2/201 个连续委托 × 三种编码 ×
  两种分支的 12 个完整族；六个反例用相反谓词保持编码长度不变。
  委托集成目标总计消费 164 个完整族、32 个协议反例。追加 done 的
  undefined/null/false/0/-0/NaN/空字符串/0n 及 true/数字/字符串/bigint/
  symbol/object/array 行为，保留原返回对象身份及 value getter 次数。
- 新编码首轮红例 `/tmp/abcd-yield-delegate-truth-red.log`；
  `cmake --build build --target abc_semantic loop_closure_file_test -j 4`
  成功，delegate、generator branches、async_semantics 三项定向测试
  全通过（7.28 秒），见 `/tmp/abcd-yield-delegate-truth-final-build.log`、
  `/tmp/abcd-yield-delegate-truth-final-tests.log`；`git diff --check` 通过。
  独立自查，原生模板的 200/201/402 规模用例继续通过。
- 上一项非默认 API 缺口已关闭；委托 finally 注入、async generator/
  async yield* 和整体结构 gate 继续开放。下一步推进 async generator
  的真实 entry/await/yield/settlement，经共同 ValueProgram/FunctionTree
  输出并验证请求顺序；全量测试/语料/RSS/基准仍暂停。

### 2026-09-30：同步 yield* 的共同源码贯通

- 依据锁定 OH7 `FunctionBuilder::YieldStar`、`Iterator::Close` 和 PandaGen
  的真实字节码，精确验证寄存器分配、属性池名称、NEXT/THROW/RETURN
  分支、调用 receiver/参数、对象检查和内部 close catch。保留 GetIterator
  原 PC 为共同 ValueYield 的委托操作，经共同 FunctionTree/Printer 输出
  原生 `(yield* operand)`；仅移除完全匹配的私有 close handler，其他
  用户 handler 继续按原边界重定位。无专项 body writer 或新发布准入。
- 共同 catch-input 分析检查十三个私有槽在 continuation/user catch
  不被观察；精确委托内部已定义的槽读取和控制边不污染后续委托的
  旧值检查。连续两次委托复用同一窗口的红例已修复，新增分析沿原
  逆序扫描和索引执行，无逐暂停点遍历。类型化 AST 区分直接/委托
  yield，operand 检查、cleanup 等价和语法检查均保留这一差异。
- 新增 44 个完整文件解码/合成元数据函数族：三 profile × 三父环境 ×
  有无用户 catch × 16/32 位分支的 36 个组合，及 2/200/201/402 个
  连续委托 × 两种分支的 8 个组合。Node 消费实际完整生成 JS，验证
  done:false 结果对象身份、next 缓存、首次 undefined 参数、方法查找/
  receiver/次数、return/throw 完成或继续、缺少 throw 的无参 close、
  getter/call 错误、无效结果/方法、未启动请求及连续委托的惰性启动。
- 26 个真实协议反例检查属性名称/池、缓存、receiver、state、mode、
  分支、外部入口、continuation/catch 私有值泄漏、close/user handler
  边界和缺失 close catch。损坏池引用保留 MetadataError，不转为普通
  unsupported。额外树单元检查委托 callable 约束与非法 form。
- 有效协议在原基线上的红例见 `/tmp/abcd-yield-delegate-valid-red.log`；
  连续委托红例见 `/tmp/abcd-yield-delegate-scaling-red.log`。八个相关
  C++ 目标构建成功，22 项定向 CTest 全通过（93.99 秒），见
  `/tmp/abcd-yield-delegate-regression-build.log`、
  `/tmp/abcd-yield-delegate-regression-tests.log`。最终 enum 边界映射与
  树反例调整后重新构建 abc_semantic/loop_closure_file/tree_structure，
  两项受影响测试通过，见 `/tmp/abcd-yield-delegate-final-build.log`、
  `/tmp/abcd-yield-delegate-final-tests.log`。`git diff --check` 通过；独立
  开发、自查，未调用 CC 或子代理。
- 支持域为精确同步模板；非默认 API 的 callruntime truth-test、注入
  RETURN finally 的委托、async generator/async yield* 与更多控制组合
  仍开放，下一步继续功能实现。未据合成元数据 fixture 声称实际应用
  覆盖；结构整合 gate/总目标未完成。全量测试/语料/RSS/基准保持暂停。

### 2026-09-30：RETURN 清理中循环再次 yield 的源码行为贯通

- 在已提交的嵌套暂停规范化和共同 preserving 出口上，加入真实 opcode
  while/do-while 清理：循环体每轮包含直接 yield，回边重读捕获 cell，
  循环后执行 work(cell) 并保留 caller 的原 RETURN payload。新增的
  48 个完整函数族首轮通过，无需新增生产算法或专项输出路径。
- 三 profile × 三父环境 × 有无用户 catch × 两种循环形式的 36 个
  双原始暂停点组合，及 200/201/402 个每轮暂停点 × 16/32 位分支 ×
  两种循环形式的 12 个组合。暂停清理集成目标现消费 78 个完整函数族
  和 18 个内层协议反例，仍只执行完整生成 JS，不执行 INCOMPLETE。
- 检查原始第 1/2 暂停点及清理第一个/最后一个暂停点的 NEXT、再次
  RETURN、THROW；循环 0/1/2/7 轮、while 零轮退出与 do/while mandatory
  iteration、每轮动态 cell 更新、suffix callback 参数/receiver/次数、
  原 catch 二次清理/抛出覆盖、Object.is payload 和未启动 generator。
- `cmake --build build --target loop_closure_file_test -j 4` 成功；扩展后的
  集成首轮通过（18.99 秒），
  `/tmp/abcd-return-suspend-loop-build.log`、
  `/tmp/abcd-return-suspend-loop-tests.log`。七项共同控制/出口、函数族及
  generator RETURN/循环/暂停清理定向回归全通过（75.82 秒），
  `/tmp/abcd-return-suspend-loop-regression-tests.log`；`git diff --check`
  通过，独立自查。yield*、async generator 及结构整合 gate 继续开放；
  全量测试/语料/RSS/基准保持暂停，不外推全域完成或 1GB 实测。

### 2026-09-30：双清理暂停走廊的诊断草稿内存生命周期修复

- 定位上一项两段清理各含 402 个 yield 的失败：树构建成功，但打印
  仅剩约 7.8MB scratch；诊断工具在 BuildFileFamilyTrees 消耗并销毁
  FileFamily 后仍保留原解码/值图的内存费用。调用返回后的实际 owner
  已只有 AST/flow/source 和移入的 incoming CaptureCells，原图不再存在。
- 工具在打印前释放已销毁图的额度，继续保留移入 cell 的原费用；树的
  保守构建费用、输入/上下文、输出缓冲仍照旧计入。不提高默认配额、
  不增加预算系统、不开启全量资源测试，也不改变源码或通用 Printer。
- 保留双走廊红例并新增六个完整函数族：两个原始暂停点 × 每段
  200/201/402 个清理 yield × 16/32 位分支。集成目标现消费 30 个完整
  函数族和 18 个内层协议反例；同一行为 oracle 验证任一原始暂停点、
  清理第一个/最后一个暂停点的 NEXT/RETURN/THROW、payload、cell 和
  效果身份。双 402 个暂停点现正常生成并执行完整 JS。
- 红例 `/tmp/abcd-return-suspend-pair-red.log`；abc_semantic、partial_file、
  class_tree_context 和 loop_closure_file 构建成功。七项相关定向回归
  全通过（68.63 秒），含 partial_file、类/函数族、generator method、
  RETURN、循环及暂停清理：
  `/tmp/abcd-return-suspend-pair-regression-build.log`、
  `/tmp/abcd-return-suspend-pair-regression-tests.log`。`git diff --check`
  通过；独立自查。上一项双 402 打印缺口已解决；剩余 C07/C08、ISA/
  结构整合 gate 未关闭，全量测试/语料/RSS/基准继续暂停。

### 2026-09-30：RETURN 清理内再次 yield 的共同源码贯通

- NormalizeGenerator 先识别所有真实同步 generator 握手，再按原 PC
  统一保留源操作和删除 VM 协议，包括外层 RETURN 清理中的多个 yield。
  内层只能使用独立 result/mode 槽；state 访问仅授权给已匹配的精确
  协议指令。保留外层 pending payload、用户 catch 及原始 source PC。
  依据固定 OH7 FunctionBuilder::HandleCompletion 的 handleReturn_ guard，
  支持内层无额外 RETURN route 的直接 yield；递归注入新清理仍未支持。
- 共同 normal CFG 继续证明清理 accumulator 可用性与覆盖后的尾部
  可达性；共同 ValueProgram/CompletionPlan/FunctionTree/Printer 输出
  原生 yield/finally，无专项 writer。分支不能进入内层 resume 私有
  指令；单调 owner 游标保留外层清理归属，不被内层暂停点抢占。
  清理区间互不嵌套，每条原指令至多属于一段清理和一个握手，新增
  识别/保留 O(I + Y log I)，存储 O(I+Y)，无按暂停点重复图遍历。
- shared catch-input 分析为 continuation 单独建立排序索引；内外 yield
  的创建顺序不等于 continuation 顺序，不能直接对创建序列 lower_bound。
  现有外部 continuation 入口/旧私有槽读取反例继续保留。
- 新增真实 opcode 文件解码、合成元数据的 24 个完整函数族，以及三种
  恢复分支宽度下的 18 个内层协议反例。实际生成 JS 核对清理暂停前后
  的 NEXT/再次 RETURN/THROW、原 payload 的 Object.is 身份及 thenable
  不同化、捕获 cell 更新、receiver、效果顺序/次数、原 catch 二次清理、
  cleanup/suffix 抛出覆盖和尚未启动的 generator。三个 profile、三个
  父环境、有无用户 catch 的 18 个双原始暂停点组合；另有单原始暂停点
  的 200/201/402 次清理 yield × 16/32 位分支，共六个规模组合。
- 首轮新用例在 phase-input kKind 失败，见
  `/tmp/abcd-return-suspend-red-tests.log`。六个相关 C++ 目标构建成功；
  最终 16 项定向 CTest 全通过（76.95 秒），包括 generator 全部相关
  源码目标、async_semantics、共同控制/出口、await finally suspend/reuse；
  `/tmp/abcd-return-suspend-regression-build.log`、
  `/tmp/abcd-return-suspend-regression-tests.log`。`git diff --check` 通过；
  按用户要求独立开发、自查，未调用 CC 或子代理。
- 未外推完整 generator 域：嵌套的新 RETURN route、yield*、async
  generator、其他控制组合和结构 gate 继续开放。探索“两段原始 yield
  各内含 402 次清理 yield”时，函数族树已构建，诊断草稿打印返回
  TreePrintError::kResourceLimit；尚未定位具体内存消耗，不改配额或隐藏
  该缺口，六个规模正例明确只含一段 RETURN 清理。下一步先定位这一
  具体源码贯通限制，再继续 C07/C08 功能。全量测试/语料/RSS/基准保持
  暂停，不据本轮声明真实应用覆盖或全进程 1GB 达标。

### 2026-09-30：preserving do/while 清理与恢复方式行为验收

- 在上一项共同 preserving 出口修复上增加不含提前 return/throw 的
  do/while 清理，首轮完整生成 JS 即通过；只扩展真实 opcode 文件解码/
  合成元数据 fixture 和行为 oracle，没有新增生产算法或专项 writer。
  新增 24 个完整函数族：三 profile × 三父环境 × 有无用户 catch 的
  18 个双暂停点组合，以及 200/201/402 条清理 × 16/32 位恢复分支的
  6 个组合。循环集成目标现消费 192 个完整函数族及 36 个协议反例。
- 检查暂停 1/2 次后的 NEXT/RETURN/THROW 分流、0/1/2/7 轮清理、初始
  false 条件下 do/while 的 mandatory iteration、每轮捕获 cell 更新、
  callback receiver/次数、原 catch 二次清理和抛出身份。正常 NEXT 的
  第二次 yield 不运行 RETURN 清理，THROW 只执行原 handler 路径；
  尚未启动的 generator 的 return/throw 不运行 work 或 cleanup。
- 对普通 while 和新增 do/while 均用 Object.is 核对 pending RETURN 的
  undefined/null/false/0/-0/NaN/bigint/symbol/object/同步 thenable payload，
  thenable 不被 assimilate。新增 18 个三恢复分支宽度协议反例，检查
  未初始化 accumulator、外部清理入口和私有 result/mode/state 槽。
- `cmake --build build --target loop_closure_file_test -j 4` 成功；完整
  循环集成目标首轮通过（26.19 秒），见
  `/tmp/abcd-resume-post-loop-preserve-build.log`、
  `/tmp/abcd-resume-post-loop-preserve-tests.log`。随后七项定向回归通过
  （58.32 秒）：control_plan、loop_exit、control_composition 单元及源码、
  loop_closure_file、generator_return 与 generator_return_loop 源码目标；
  `/tmp/abcd-resume-post-loop-preserve-regression-tests.log`。
  `git diff --check` 通过；仅作者自查，按用户要求未调用子代理。
- C07 下一项仍为 RETURN 清理内再次 yield：NormalizeGenerator::Resume
  当前禁止清理读取协议私有槽，外层扫描又跳过整段恢复协议，嵌套暂停
  不能只放开 opcode 或控制出口检查，需归一化嵌套 owner、活 payload 和
  THROW/RETURN dispatch 后经共同树贯通。尚未实现，不算本轮支持域。
  yield*、async generator、其他控制组合与结构 gate 继续开放；全量测试/
  语料/RSS/基准保持暂停，不外推真实应用覆盖或 1GB 达标。

### 2026-09-30：RETURN 清理后测试循环与 preserving 出口归属

- 恢复 CLI session `01a0e5c6-2009-7403-9768-aec3d50a3587` 留下的
  do/while 红例及未完成改动，在 `4ed6764` 上的隔离分支继续。
  异常控制投影从 ValueYield 的 owner-normalized return_route.completion
  精确关联原始 Return，Before/Commit 均保留类型化 preserving 属性；
  exception-loop 发现、压缩和共同 loop-control 视图传递该属性。
  不根据弧顺序、header/latch 或 PC 大小猜测哪个 return 保留 caller payload。
- 共同私有出口走廊索引将 preserving 尾路径留在循环后，其他私有 abrupt
  路径仍属于循环；不删除 Return、不修改原 CFG/handler/效果顺序。
  多个 preserving 出口保持独立，由共同区域构建器处理多出口限制。
  原有普通 terminal、正常 port、独立入口与共享 join 边界保留。
  原始 completion 索引 O(O log(Y+1)+Y log(Y+1))，走廊扩展 O(V+E+L)，
  不按循环或暂停点重复遍历图；没有新增 work 截止或专项 writer。
- 新增 48 个真实 opcode 文件解码/合成元数据函数族：三 profile × 三
  父环境 × 有无用户 catch × return/throw 的 36 个双暂停点组合，以及
  200/201/402 条清理 × 16/32 位恢复分支 × return/throw 的 12 个组合。
  实际生成 JS 检查初始条件 false 时仍执行一轮、每轮重读 cell、callback
  receiver/次数、提前覆盖及原 catch 二次清理/错误身份。循环集成目标现
  消费 168 个完整函数族和原 18 个协议反例。图级检查覆盖 preserving
  位于 header/latch 两种弧排序及双 preserving 出口，规模 1/200/201/402。
- 当前 checkout 首次构建使用固定依赖：Ruby 3.2.3、pinned ISA 和 JSON；
  16 个相关 C++ 目标构建成功。功能红例
  `/tmp/abcd-resume-post-loop-red-tests.log`（退出 8，family_tree_error 9）；
  多 preserving 边界红例 `/tmp/abcd-resume-preserving-boundary-red-tests.log`。
  最终 41/41 定向 CTest 通过（86.79 秒）：
  `/tmp/abcd-resume-post-loop-regression-build.log`、
  `/tmp/abcd-resume-post-loop-regression-tests.log`，覆盖控制/异常组合、
  loop exits、finally、捕获、phased values、await/generator 和共同树消费。
  `git diff --check` 通过；作者自查精确来源关联和投影属性传递，按用户
  要求未使用 CC/子代理。原 checkout 与用户 sample.tar.gz 保持原样。
- C07/总目标仍开放：RETURN 清理再次暂停、更多循环/多出口布局、嵌套
  finally completion、入口默认值/解构、self/super、yield*、async generator
  和字段/全局/签名/函数体整合。上述是解码 fixture 的标准 JS 行为证据，
  不是实际应用或全语料支持证明；全量测试/语料/RSS/基准继续暂停，两个
  历史 base_function 失败未重跑/修复，不宣称全套通过或 1GB 验收。

### 2026-09-30：RETURN 清理嵌套循环纵向验收

- 在上一项共同控制/值流实现上增加嵌套循环组合，首轮完整生成 JS 即
  通过；本项仅扩展真实 opcode 文件解码/合成元数据 fixture 和行为测试，
  没有新增生产算法、专项 writer 或输出许可。内层正常退出后执行外层
  callback 再回到外层 header；内层 return/throw 则终结整个清理路径。
- 新增 48 个完整函数族：三 profile × 三父环境 × 有无用户 catch ×
  return/throw 的 36 个双暂停点组合，以及 200/201/402 条内层 guard ×
  16/32 位恢复分支 × return/throw 的 12 个组合。循环集成目标现消费
  120 个完整函数族和原 18 个协议反例。验证 pending payload、覆盖结果、
  receiver、内外层 callback 次数，以及原 handler 的二次清理/错误身份。
- 外层 callback 可重新开启捕获 cell，再进入内层，实际执行 1/2/7 轮；
  同时验证外层 callback 抛错后用户 catch 是否执行、二次清理是否覆盖
  错误。检查真实生成源码完整且不含 INCOMPLETE，不用部分输出冒充成功。
- `loop_closure_file_test` 构建成功；七项相关定向测试通过（57.50 秒）：
  `/tmp/abcd-generator-return-nested-loop-verified-tests.log`。补充外层
  callback 异常断言后重新构建并重跑整个循环集成目标，通过（17.36 秒）：
  `/tmp/abcd-generator-return-nested-loop-final-build.log`、
  `/tmp/abcd-generator-return-nested-loop-final-tests.log`；
  `git diff --check` 通过。这些是解码 fixture 的标准 JS 行为证据，
  不是实际应用、任意循环布局或全语料支持证明。
- C07/总目标仍开放：RETURN 清理再次暂停、其他循环/多出口布局、嵌套
  finally completion、入口默认值/解构、self/super、yield*、async
  generator 和结构 gate。全量测试/语料/RSS/基准仍暂停，两个历史
  base_function 失败未重跑/修复；没有全套通过或 1GB 验收结论。

### 2026-09-30：RETURN 清理跨块值与循环内 completion 覆盖

- RETURN 清理不再要求每个 basic block 单独初始化 accumulator。仅在跨块
  读取或 abrupt 清理需要时，对不相交的拥有切片复用共同 normal CFG 做
  must-availability；入口仍无协议 accumulator，正常 call 后继可用其结果，
  分支合流必须所有可达前驱均已定义。每块最多处理两次，实际值/phi 和
  handler 仍由共同 phased SSA 处理，不引入 work 截止或输出准入。
- 共同 loop exit corridor 扩展支持私有 return/throw 尾路径，包括已在
  Before 发射、结束于空 Commit 的路径。一次反向索引私有线性 abrupt
  corridor；全部出口为私有 abrupt 时保留一条完整 following corridor，
  其他路径归入循环，避免将 preserving payload Return 错分为循环内尾部。
  CFG 边不变；独立入口、共享 join、兄弟/子循环、正常零弧 try port 不被
  吸收。异常循环发现图现保留原 termination 类型，与普通循环共用该算法。
- 新增 114 个完整文件解码/合成元数据函数族：18 个公共条件值进入 call，
  36 个条件选择后 return/throw，12 个 200/201/402 连续选择，48 个循环内
  条件 return/throw（含 200/201/402 条 guard、16/32 位恢复分支）。实际
  生成 JS 验证 callback receiver、顺序/次数、合流结果身份、捕获 cell
  更新、原 RETURN payload 保留/覆盖、异常及用户 catch 二次清理。
  return/loop 两个集成目标分别消费 375/72 个完整函数族；不冒充真实应用
  或任意 generator 形态覆盖。未初始化入口与协议私有寄存器反例保留。
- 普通函数新增三 profile × return/throw 的六个实际源码行为用例，验证
  0/1/2/7 次循环、提前终结与 callback 抛错。图级反例确认正常出口、
  独立入口和共享 terminal 的边界。旧 control_plan 用例曾把两个真实
  return 当成不支持的多出口；现分别检查私有 return 成功和两个普通
  continuation 仍拒绝，不删除原安全边界。
- 相关 C++ 目标构建成功；42/42 定向测试通过（59.08 秒），包括普通/
  异常控制组合、try、finally、闭包、await 和 generator 的完整源码执行。
  构建日志：`/tmp/abcd-generator-return-flow-final-build.log`、
  `/tmp/abcd-generator-return-flow-consumer-build.log`、
  `/tmp/abcd-loop-abrupt-control-build.log`；最终测试：
  `/tmp/abcd-generator-return-flow-verified-tests.log`；`git diff --check`
  通过。循环红例定位：`/tmp/abcd-generator-return-loop-override-location.log`，
  临时投影丢失 terminal 类型定位：`/tmp/abcd-loop-abrupt-new-location.log`。
- C07/总目标仍开放：嵌套/多出口清理、RETURN 清理再次暂停、入口默认值/
  解构、self/super、yield*、async generator，以及字段/全局/签名/函数体
  结构 gate。全量测试/语料/RSS/基准仍暂停，两个历史 base_function 失败
  未重跑/修复；不宣称全套通过或 1GB 验收。

### 2026-09-30：generator 注入 RETURN 的循环清理纵向验收

- 将 inline RETURN 清理的后向分支带入真实 opcode 文件解码用例，经共同
  SSA/效果/exception-loop/RegionPlan/FunctionTree/Printer 完整输出并执行。
  首轮即通过，说明现有共同循环恢复已经能消费这个组合；本项没有新增
  generator 循环算法、专项 writer、预算计费或输出许可。
- 新 `file_generator_return_loop_source_test` 消费 24 个完整函数族：三
  profile × 三父级环境 × 有无用户 catch 的 18 个双暂停点组合，以及
  200/201/402 个 loop-body 调用 × 16/32 位恢复分支的 6 个组合。真实清理
  CFG 的 JEQZ/JMP 使用 imm32，来源 PC 仍由共同路径保留；用外部 setter
  在 callback 中改变捕获 cell，验证每轮重新读取、0/1/2/7 轮的实际调用
  次数、跳过 RETURN loop 的 NEXT/THROW、callback receiver、原 catch
  二次清理、先后错误身份和初次 next 前的 return/throw 懒执行。
- 18 个协议反例覆盖三个恢复分支宽度下的未初始化 accumulator 入口、
  跳过 payload load、外部进入 loop 清理，以及私有 result/mode/state
  读取。它们检查真实控制/值边界，不按 loop 条数或 work 计数拒绝产出。
- `loop_closure_file_test` 重新构建成功，39/39 相关定向测试通过
  （39.95 秒）：`/tmp/abcd-generator-return-loop-final-build.log`、
  `/tmp/abcd-generator-return-loop-final-tests.log`；`git diff --check` 通过。
  夹具为真实 opcode 文件解码及合成元数据，不冒充真实应用验收。
- C07/总目标仍开放：循环内显式 completion 覆盖、嵌套/多出口清理、
  RETURN 清理再次暂停、跨块 accumulator 输入、入口默认值/解构、
  self/super、yield*、async generator 与结构 gate 等继续推进。全量测试、
  语料、RSS、基准仍暂停；历史两个 base_function 失败未重跑/修复。

### 2026-09-30：generator 注入 RETURN 的显式 return/throw 覆盖

- 支持 RETURN 内联清理中的实际 return/throw，以及捕获 cell 控制的条件
  覆盖。对照锁定 OH7 `FunctionBuilder::HandleCompletion` 和
  `TryContext::EmitFinalizer`：编译清理后仍发射旧 payload 的 preserving
  tail，该尾部可能已经不可达。仅在有 abrupt 清理时复用共同 normal CFG
  分析不相交的协议切片，保留可达指令；不扫描/裁掉普通正文，不把外部
  分支或 handler 进入协议内部当成合法清理入口。工作量与切片大小相关，
  没有新增 work 截止或产出准入。
- YieldReturnRoute/CompletionPlan/FunctionTree 使用 optional preserving
  completion 区分“仍有路径保留原 RETURN”与“清理路径已全部覆盖”。真实
  override 的 ReturnStmt/ThrowStmt 保留并进入普通 native finally，既不
  伪造原 payload，也不剥掉覆盖操作。共同 SSA/效果/handler、TreeFlow
  逆投影和 Printer 继续消费同一条主干。
- 实际红例暴露 preserving terminal 可位于 branch arm 或 following
  sequence 的末尾 if 内。一次建立 tail-parent 索引，仅接受 Block/If 的
  最后子节点链：该私有 terminal 在 finally 外保留审查身份，finally
  自然落下保留 caller payload；逆投影恢复原 terminal 所属块及操作数。
  循环或 try/finally 不是透明尾路径，不借此删除其 return。
- 新增 96 个完整文件解码/合成元数据函数族：三 profile × 三父级环境 ×
  有无用户 catch × 无条件/条件 × return/throw 的 72 个双暂停点组合，
  及 200/201/402 条清理 × 16/32 位恢复分支的 24 个组合。完整生成 JS
  核对覆盖结果（undefined/null/NaN/symbol/object/同步 thenable）、callback
  receiver/次数、暂停中 cell 更新、NEXT/RETURN/THROW 分流、原用户 catch
  的二次清理及错误身份。无条件覆盖后的死清理不被执行；条件 false
  保留原 payload。另 6 个反例覆盖各恢复分支宽度下外部进入已死 payload
  尾部，不靠丢弃入口冒充恢复。该集成目标现消费 309 个完整函数族及
  36 个协议反例，仍不代表真实应用或任意 generator 形态覆盖。
- 原功能红例：`/tmp/abcd-generator-return-override-red-tests.log`；嵌套
  terminal 布局定位：`/tmp/abcd-generator-return-override-placement.log`、
  `/tmp/abcd-generator-return-override-tail-placement.log`。相关十五个
  C++ 目标构建成功，38/38 定向测试通过（47.48 秒）：
  `/tmp/abcd-generator-return-override-final-build.log`、
  `/tmp/abcd-generator-return-override-final-tests.log`；`git diff --check`
  通过。未恢复全量测试/语料/RSS/基准，两个历史 base_function 失败未
  重跑/修复，不宣称全套通过或 1GB 验收。
- C07/总目标仍开放：RETURN 清理再次暂停、循环清理与嵌套 completion 的
  完整组合、跨块 accumulator 输入、入口默认值/解构、self/super、yield*、
  async generator、类/record/全局/完整签名及函数体整合仍需继续推进。

### 2026-09-30：generator 注入 RETURN 的条件清理与正常出口区分

- 在上一项内联 RETURN 路由上支持清理内部条件分支：目标只能位于该清理
  与保留 payload load 的边界内，外部进入协议内部仍不允许。按基本块
  重置 accumulator 可用性，每块必须先定义再读取；不把分发 accumulator
  跨分支/回边当作源码输入。普通寄存器合流和控制结构复用共同 CFG/SSA/
  RegionPlan，不另建 generator 控制算法或专项 printer。
- 共同 CompletionTree 消费两种已有树布局：RETURN 清理位于分支 arm，
  或 NEXT 真正提前退出后位于 following sequence。批量搬移/还原已有
  语句根，保留 RETURN terminal 身份和来源，不逐次移动完整兄弟后缀。
  FunctionTree 仅增加类型化 placement/terminal 关联，不新增输出许可。
- 实际生成 JS 抓到共同提前退出组织的语义错误：投影中的正常 try-exit
  port 也是零后继，却不发射 return；将它当 abrupt 会让 NEXT 跳过正常
  清理，调用次数 0 !== 1。共同 ControlGraphView 增加 Normal/Abrupt
  termination，由真实 return/throw、其 Before/Commit 相位和真实 loop
  transfer 构造；异常投影压缩与循环投影保留该属性。正常 port 不因此
  获得终止语义。控制单元和实际源码均覆盖这一边界，不靠调额度修复。
- 新增 24 个完整源码函数族：三 profile × 三父级环境 × 有/无用户 catch
  的 18 个双暂停点条件清理，以及 200/201/402 条条件 × 16/32 位恢复
  分支的 6 个组合。验证 RETURN 时读取暂停后的 cell 更新、false/0 跳过、
  callback receiver/次数及异常身份、NEXT/THROW 分流；清理回调修改 cell
  后，后续 guard 必须重新读取，不能复用副作用前的值。新增 9 个反例
  检查 branch-entry accumulator、越过清理边界及外部进入清理。
  file_generator_return_source_test 现消费 213 个完整文件解码/合成元数据
  函数族和 30 个协议反例，不宣称真实应用或任意清理形态覆盖。
- 功能红例 `/tmp/abcd-generator-return-conditional-red-tests.log`；布局定位
  `/tmp/abcd-generator-return-conditional-placement.log`；正常出口语义红例
  `/tmp/abcd-generator-return-conditional-behavior-tests.log`。相关十五个
  C++ 目标构建成功，38/38 定向测试通过（37.19 秒）：
  `/tmp/abcd-generator-return-conditional-final-build.log`、
  `/tmp/abcd-generator-return-conditional-final-tests.log`；`git diff --check`
  通过。构建和行为证据只覆盖所列定向范围。
- 总目标/C07 仍开放：清理再次暂停、显式 completion 覆盖、循环清理的
  完整组合验收、跨块 accumulator 输入、入口默认值/解构、self/super、
  yield*、async generator、类/record/全局/完整签名及函数体整合仍待推进。
  默认 work 截止继续关闭，无新 DFX/发布准入；全量测试/语料/RSS/基准
  仍暂停，两个历史 base_function 失败未重跑/修复，不宣称全套或 1GB 验收。

### 2026-09-30：generator 注入 RETURN 的内联清理贯通

- NormalizeGenerator 保留 OH7 HandleCompletion 中 RETURN 分支的直线内联
  清理，不假定它与 NEXT/THROW 清理等价。清理仍经共同 ValueProgram 的
  SSA、效果和 handler 分发恢复；原始协议 PC、RETURN branch/terminal 来源
  保留。私有 state/result/mode 槽读写，以及未初始化的分发 accumulator
  依赖不被误当作 native yield 的可用值。
- CompletionPlan/FunctionTree 在 yield 点构造普通 JS try/catch/finally：
  NEXT 和注入 THROW 清除局部 RETURN 标志，只有 native RETURN 执行真实
  清理根；caller payload 由 JS pending completion 保留，不伪造或读取
  不可见 payload。清理仍处于原外层保护下；清理抛错可进入原用户 catch，
  包括该原始 catch 自己再次执行清理的行为。无用户 catch 时，清理异常
  直接传播，注入 THROW 不误执行 RETURN 或正常尾部清理。
- 本轮 189 个真实 opcode/合成元数据函数族完整生成并执行 JS：162 个
  三 profile/三父级环境/两个分支 8/16/32 宽度/相同及不同回调；9 个无
  用户 catch；6 个 200/201/402 次清理与长分支；9 个双暂停点捕获 cell；
  3 个 200/201/402 暂停点。核对 callback receiver/次数、payload/error
  身份、next/return/throw、早期完成、独立迭代器、暂停中 cell 更新和
  同步 thenable。21 个私有槽/accumulator 反例检查真实协议语义边界。
  这些是文件解码夹具，不冒充真实应用或全部 OH7 编译器形态覆盖。
- 多暂停点红例暴露共同区域构建将继续路径逐层嵌套，402 点触及诊断输出
  上限，见 `/tmp/abcd-generator-return-composition-tests.log`。共同
  ControlBuilder 增加一次反向拓扑的单入口直线终止路径摘要，提前退出
  分支留在条件内，继续路径成为同级 sequence；不逐分支遍历、不改变
  受限区域 join、不复制共享节点。2/200/201/402 个两种方向的 guard
  通过控制检查，实际源码另检查 201→402 输出不发生平方膨胀。
  未提高输出、work 或内存限额，未新增 generator 专项 printer。
- 原功能红例 `/tmp/abcd-generator-return-red-tests.log`；控制紧凑性红例
  `/tmp/abcd-early-exit-red-tests.log`。临时把 native RETURN 标志置 false
  并同步其检查时，实际源码清理调用 0 !== 1，见
  `/tmp/abcd-generator-return-mutant-tests.log`；已恢复正确实现。
  相关十二个 C++ 目标构建成功，34/34 定向测试通过（23.97 秒）：
  `/tmp/abcd-generator-return-final-build.log`、
  `/tmp/abcd-generator-return-uncaught-build.log`、
  `/tmp/abcd-generator-return-final-tests.log`；`git diff --check` 通过。
- C07/总目标仍开放：带分支、再次暂停或显式覆盖 completion 的注入 RETURN
  清理、入口默认值/解构、self/super、yield*、async generator、类/record
  与完整结构 gate 继续推进。默认 work 截止保持关闭，不因计数拒绝默认
  产出；没有新增输出准入或 DFX。全量测试/语料/RSS/基准仍暂停，历史
  两个 base_function 失败未重跑/修复，不宣称全套或 1GB 验收。

### 2026-09-30：generator 恢复分支的 8/16/32 位编码贯通

- NormalizeGenerator 的 RETURN/THROW 分发复用 LiftBranchCondition 的类型化
  FalseOrZero predicate，支持锁定 OH7 ISA 的 JEQZ op_imm_8/16/32，而不是只
  接受短分支 opcode。两个目标仍必须精确跳过各自的完成路径；反向 JNEZ
  或目标错误不被等价处理。来源 PC、用户 handler 重映射及 SSA 消费不变。
- 三 profile × 三父级环境 × 两个分支各三种宽度 × 五种正文，共 405 个
  真实 opcode/合成元数据函数族完整输出并执行。覆盖 yield 恢复值、catch
  再次 yield、catch 内循环、复制帧参数 cell/逃逸 reader、清理 yield；检查
  懒执行、callback receiver/次数、next/return/throw、暂停中的外部 cell
  更新、独立参数 cell 及同步 thenable。另 36 个反例覆盖各宽度下反向条件
  和偏离完成目标的分发。反例是语义边界检查，不是 work 或覆盖数限制。
- 功能红例 `/tmp/abcd-generator-branches-red-tests.log` 在旧短分支识别处
  报 FunctionEmitError::kKind；相关十一个 C++ 目标构建成功，32/32 定向
  测试通过（23.68 秒），包括新 file_generator_branches_source_test：
  `/tmp/abcd-generator-branches-final-build.log`、
  `/tmp/abcd-generator-branches-final-tests.log`；`git diff --check` 通过。
  此证据不代表真实应用或完整 ISA/函数语义覆盖。
- C07/总目标仍开放：finally 注入 RETURN 内联清理、入口默认值/解构、方法
  self/super、yield*、async generator 和类/record 等继续推进。默认 work
  截止继续关闭；保留兼容计数接口，不为新功能增加工作量拒绝或 DFX。
  全量测试/语料/RSS/基准仍暂停，历史两个 base_function 失败未重跑/修复，
  不宣称全套或 1GB 验收。

### 2026-09-30：generator 清理块暂停与 completion 保留/覆盖

- 共同 CompletionPlan 的清理操作配对支持已归一化 ValueYield：不同暂停点的
  协议 PC/寄存器不作为语言操作身份，但继续比较 SSA 输入、效果、异常分发
  和结果对应关系；两个清理副本的来源仍保留。清理块中的 yield 可以跨暂停
  保留 pending return/throw，也允许清理自己的 return/throw 覆盖它。
  FunctionTree/共同 Printer 未增加专项输出路线或验证准入。
- 三 profile × 三父级动态环境，共 63 个真实 opcode/合成元数据函数族：
  27 个清理暂停/return/throw 正向恢复、18 个调用对象/输入不匹配组合，以及
  18 个受保护 yield 的 RETURN 路由反例。完整生成 JS 验证 callback 次数、
  receiver、payload/error 身份、next/return/throw、早期完成、独立迭代器与
  同步 thenable 不被吸收；不匹配组合仍输出并执行完整 try/catch 源码。
- 修复既有误恢复：原始 yield 的 RETURN 分支直接退出时，不把后续清理搬入
  native finally，否则 .return() 会新增可观察调用。普通配对和共享尾部两种
  反例均用实际生成 JS 验证；临时去掉对应保护分别出现调用次数 1 !== 0，
  见 `/tmp/abcd-generator-finally-effects-red-tests.log`、
  `/tmp/abcd-generator-finally-shared-red-tests.log`。此边界保留完整原控制源码，
  不是用 unsupported/INCOMPLETE 代替新增功能。
- 相关十一个 C++ 目标构建成功，31/31 定向测试通过（20.06 秒）：
  `/tmp/abcd-generator-finally-final-build.log`、
  `/tmp/abcd-generator-finally-final-tests.log`；`git diff --check` 通过。
  初始功能红例见 `/tmp/abcd-generator-finally-red-tests.log`。
  夹具不冒充真实应用或完整 OH7 编译器形态覆盖。
- C07/总目标仍开放：受保护 yield 注入 RETURN 时的内联 finally 清理路由尚未
  贯通，入口默认值/解构、方法 self/super、yield*、async generator、类/record
  整合等继续推进。默认 work 阈值继续关闭，不用 work 计数拒绝产出；没有新增
  DFX。全量测试/语料/RSS/基准仍暂停，历史两个 base_function 失败未重跑/修复，
  不宣称全套或 1GB 验收。

### 2026-09-30：generator 捕获参数的入口 cell 初始化

- 同一 NormalizeGenerator 前奏消费增加 LDA ABI 输入→STLEXVAR 的参数 cell
  初始化，允许与 MOV v4/v8/v16 交错。复用 LiftLexicalInstruction 的类型化
  address/value，只允许新建环境的 depth=0、有效 slot、Accumulator store；
  没有新环境的 store、外层 cell 写入、非 ABI 源或任意求值均不被延后。
  被捕获的参数、暂停后的 cell 写入和逃逸子闭包仍通过共同异常值流/树/Printer。
- 三 profile × 三父级环境的直接参数 cell 和三种编码完整参数帧组合，新增
  36 个实际文件解码函数族。完整生成 JS 验证首个 yield 的参数身份、多个
  迭代器逆序恢复后的独立写入、返回 reader 对动态 cell 的持久捕获、外部 cell
  写入不串到内部 cell、早期/暂停后的 return/throw 和同步 thenable 不被吸收。
  交错前奏在 cell 初始化后再次复制 callee，仍核对创建点的最后来源。
  新增四个反例：外层深度、越界 slot、非 ABI 源、没有新环境的 cell 写入。
- 红例 `/tmp/abcd-generator-parameter-cell-red-tests.log` 在旧实现失败于入口
  归一化；新增组合与前奏旧规格共同执行，共 129 个正向函数族及 25 个反例。
  相关十一个 C++ 目标构建成功，30/30 定向测试通过（18.63 秒）：
  `/tmp/abcd-generator-parameter-cell-final-build.log`、
  `/tmp/abcd-generator-parameter-cell-final-tests.log`；`git diff --check` 通过。
  证据使用真实指令与合成元数据，不冒充真实应用或全部参数签名覆盖。
- C07/总目标仍开放：用户默认值求值、解构及更复杂入口、finally 注入 RETURN
  清理、方法 self/super、yield*、async generator、类/record 整合等继续推进。
  默认 work 阈值继续关闭；没有新增预算准入或 DFX。全量测试/语料/RSS/基准
  仍暂停，历史两个 base_function 失败未重跑/修复，不宣称全套或 1GB 验收。

### 2026-09-30：generator 入口词法环境与 ABI 参数复制

- NormalizeGenerator 保留 OH7 PandaGen::InitializeLexEnv 的单个入口环境创建
  和 CopyFunctionArguments 的 MOV v4/v8/v16 参数复制，随后折叠同一个初始
  挂起协议。沿用异常相位值图、环境/cell、FunctionTree 与共同 Printer，
  不引入专项 writer。前奏只含 VM 私有环境分配和 ABI 输入复制；已有资源
  失败排除契约允许其延后至首次 next，无调用/getter/默认求值被随意延后。
- 对照 FunctionRegScope，支持 callee/new.target/this/参数由物理 ABI 窗口
  复制到局部槽再创建 generator。单次前奏扫描追踪创建 callee 的最后写入，
  必须来自当前函数槽；复制不能覆盖初始协议的 state/result/mode 私有槽。
  原始入口 PC 保留，用户异常表仍按实际创建点开始；正文回跳入口前奏不被
  当作普通源码循环，防止重新执行一次性 generator 初始化。
- 三 profile × 三父级环境的复制、独立词法环境、暂停后创建逃逸闭包及完整
  参数帧组合，覆盖 v4/v8/v16，共 90 个文件解码函数族。生成完整 JS 验证
  动态 cell 每迭代器独立、暂停后的外部 cell 更新、逃逸 reader 的持久性、
  copied receiver 的严格 primitive/null/undefined/bind 语义、逆序恢复、
  next/throw/return 与完成后调用。另三个 200/201/402 条 MOV 前奏用例完整
  输出并执行；总计 93 个正向函数族。使用真实指令与合成元数据，不声称应用覆盖。
- 21 个前奏反例覆盖用户调用、私有 state/result/mode 覆盖、读取非 ABI 局部、
  回跳入口及 callee 被改写为参数；后三者不会因 opcode 相似而冒充完整恢复。
  四段红例见 `/tmp/abcd-generator-prelude-red-tests.log`、
  `/tmp/abcd-generator-prelude16-red-tests.log`、
  `/tmp/abcd-generator-prelude-private-red-tests.log`、
  `/tmp/abcd-generator-prelude-frame-red-tests.log`。补私有槽检查时曾误写字段名
  导致一轮编译失败，已修正并重跑以下最终集合。
- 相关十一个 C++ 目标构建成功，30/30 定向测试通过（19.26 秒），包含
  file_generator_prelude_source_test 及 generator/async/await/finally/值流回归：
  `/tmp/abcd-generator-prelude-final-build.log`、
  `/tmp/abcd-generator-prelude-final-tests.log`；`git diff --check` 通过。
- C07 仍开放：finally 注入 RETURN 清理、带用户求值或 lexical 参数存储的
  入口前奏、方法 self/super 等仍待推进；yield*、async generator、整个类/record
  集成与其余目标未完成。默认 work 阈值继续关闭；全量测试/语料/RSS/基准
  仍暂停，两个历史 base_function 失败未重跑/修复，不宣称全套或 1GB 验收。

### 2026-09-30：generator 类方法贯通共同函数族

- MethodRoleIndex 区分 GENERATORMETHOD 与普通 METHOD；类创建上下文按锁定
  OH7 LiteralDataExtractor::DefineFunctionTemplate 处理 NONE 的 tag 细化及显式
  GENERATOR kind 的保留。竞争 template/constructor 角色仍不能混用；异步
  generator 未按同步 generator 处理。新增 GeneratorMethod callable，经原有
  NormalizeGenerator、异常相位值流、FunctionTree 和共同 Printer 输出
  `*run(...)` / `static *run(...)`，没有独立方法 body writer。
- 三 profile × 实例/静态 × 三种身份来源 × 普通/用户 catch，共 36 个真实
  opcode/合成元数据文件夹具，完整生成并执行方法源码。覆盖 name/length、
  GeneratorFunction/iterator prototype、不可构造性、类成员 descriptor、严格
  primitive/null/undefined receiver、bind、两迭代器逆序恢复、同步 thenable
  不被吸收、首次/暂停后 return/throw、catch 捕获注入异常及 callback 抛错身份。
  测试只提供类外壳，方法语法和正文由解码→共同树→Printer 生成。
- 新增 AST 方法 yield 正例/await 反例、不带实际类上下文打印的反例，以及
  generator 元数据配 async 正文的归一化反例；generator literal 角色索引
  覆盖 200/201/402 条引用。红例在旧实现失败于 ClassContextError::kRole
  （`/tmp/abcd-generator-method-red-tests.log`、`/tmp/abcd-generator-method-red-debug.log`）。
- 相关十一个 C++ 目标构建成功，29/29 定向测试通过（18.99 秒），包括
  file_generator_method_source_test：`/tmp/abcd-generator-method-final-build.log`、
  `/tmp/abcd-generator-method-final-tests.log`；`git diff --check` 通过。
  此证据不代表真实应用、整个类/record 装配或全部 generator 语义的验收。
- C07 仍开放：finally 注入 RETURN 清理、入口词法/参数前奏、方法 self/super
  和更多 frame/闭包组合仍待推进；yield*、async generator 及其余总目标未完成。
  默认 work 阈值保持关闭；全量测试/语料/RSS/基准仍暂停，历史两个
  base_function 失败未重跑/修复，未宣称全套通过或 1GB 实测达标。

### 2026-09-30：generator 自身身份与暂停帧 receiver

- 将 generator 的原生具名 self 绑定接入现有共同树；AllowsNamedSelf 统一树构建、
  结构、词法 scope 和 ABI 的 callable 约定，避免新增类型只在其中一层可用。
  元数据名称合法且无遮蔽时输出 function* name；非标识符或参数等同名时沿用
  既有私有 const 创建绑定。Printer 只读取已选定的 self form，不推断绑定关系。
- LDFUNCTION 返回实际 generator callable；LDTHIS 和原始 receiver 参数槽沿用
  已有相位 FrameReceiver 与共同 ThisExpr。三 profile × 三父级环境的四类文件
  解码组合共 36 个函数族，生成完整 JS 验证 name/length、GeneratorFunction
  原型、各创建实例独立的 prototype、不可构造性、thenable 不被吸收、严格
  primitive/null/undefined receiver、bind 调用保留原 callable、两个迭代器
  逆序恢复，以及 throw/return/完成后调用。未用 callback 或 forwarding writer
  替代 generator 本体；恢复的父级/子级源码均经共同函数族 Printer。
- 手工树补充 generator 的参数名/self 名冲突正例，并沿用误绑名称、可写 self、
  缺失名字和资源语义契约的反例。红例 `/tmp/abcd-generator-frame-red-tests.log`
  在旧实现上失败于 self binding；后续定向定位并补齐 shared scope/ABI 约定。
- 相关十个 C++ 目标构建成功，27/27 定向测试通过（16.41 秒），包括新增
  file_generator_frame_source_test 与 generator catch、async frame、await/finally、
  树及值流回归：`/tmp/abcd-generator-frame-final-build.log`、
  `/tmp/abcd-generator-frame-final-tests.log`；`git diff --check` 通过。
  仍为真实指令/合成元数据夹具，不据此声称真实应用或所有 frame/闭包组合覆盖。
- C07 仍开放：finally 注入 RETURN 清理、入口词法/参数前奏、generator 方法
  与更多 frame/闭包组合仍待推进；yield*、async generator 及其余总目标未完成。
  默认 work 阈值保持关闭；全量测试、全语料、RSS 和基准仍暂停，历史两个
  base_function 失败未重跑/修复，未宣称全套通过或 1GB 实测达标。

### 2026-09-30：generator 用户 catch 与注入 THROW 贯通

- NormalizeGenerator 保留用户异常表，复用既有边界重定位，仅移除编译器外层
  原样 rethrow。YieldRegion 记录原始 THROW/分支点；原 yield 与 THROW 必须
  选中同一异常目标，handler 不得进入被折叠的恢复协议内部。
- await/yield 通过同一首用/闭合后缀分析内核检查 result/mode 临时寄存器。
  VM THROW 先写恢复值和 mode，原生 yield 的异常边不会执行这两次写入；
  handler 只能读取已覆写的新值，或证明旧临时值从未被观察。没有另建异常 CFG、
  generator writer、发布证明或 work 准入，FunctionTree/Printer 路线不变。
- 三 profile × 三父级环境的五类实际文件解码组合共 45 个函数族，生成完整 JS
  执行：catch 返回异常原值、catch 读取共享 cell、catch 内再次 yield、catch
  覆写恢复寄存器、try 内 yield 循环。覆盖 callback 的惰性/次数/receiver、暂停
  前副作用、暂停期间外部 cell 更新、兄弟闭包实例隔离、首次 throw/return、
  try/catch 内 return/throw 和完成后调用；undefined/null/false/0/NaN/Symbol/object
  的异常身份保持。200/201/402 个受保护 yield 在首段、中段和末段注入 THROW，
  并执行 callback 中途抛错及正常结束路径；参考源码仅作测试 oracle，不进入引擎输入。
- 新增四个文件反例：catch 观察尚未覆写的 result/mode、异常域在恢复协议中
  分裂、handler 指向保留但属于协议内部的 mode 点；保留之前五个协议反例。
  红例 `/tmp/abcd-yield-catch-red.log` 在旧实现上失败于 phase 输入构建。
  扩展测试时发现两处用例问题（变量作用域、循环应以 false 而非 undefined
  退出），已修正并重跑完整定向集合。
- 相关十个 C++ 目标构建成功，26/26 定向测试通过（12.35 秒），含生成 JS
  generator 测试及共享 await/async/finally/值流回归：
  `/tmp/abcd-yield-catch-final-build.log`、`/tmp/abcd-yield-catch-final-tests.log`。
  `git diff --check` 通过。夹具使用真实指令与合成元数据，不声称真实应用验收。
- C07 仍未完成：finally 注入 RETURN 清理、入口词法/参数前奏、generator
  方法/self 等组合仍待推进；yield*、async generator 与其余目标保持开放。
  默认 work 阈值继续关闭；全量测试/语料/RSS/基准仍暂停，历史两个
  base_function 失败未在本轮修复或重跑，不宣称全套测试或 1GB 验收完成。

### 2026-09-30：同步 generator/yield 首段共同源码贯通

- 按锁定 OH7 GeneratorFunctionBuilder::Prepare/Yield 和 FunctionBuilder 的
  SuspendResumeExecution/HandleCompletion，区分初始挂起与源码 yield，检查
  RETURN/THROW/NEXT 分发、私有 state、done=false 和协议单入口；只移除外层
  原样 rethrow，不擦除用户 catch/finally 或夹带清理操作的 RETURN 路由。
- 新增类型化 YieldSite/YieldValue/ValueYield/YieldExpr，经现有异常相位值图、
  effects、环境/cell、求值安排、FunctionTree 和共同 Printer 输出 function*/yield。
  没有专项 generator writer，暂停不作为源码函数退出；恢复 mode 的正常值为 NEXT，
  THROW/RETURN 由原生 yield 执行。原始 yield PC 保留到树来源中。
- 三种 profile（12.0.2/12.0.6/13.0.1）与三类父级环境的实际文件解码夹具，验证
  捕获变量在暂停后的共享/隔离、恢复值返回、循环回边、首次 next 忽略参数、
  next/throw/return 与完成后调用、同步 yield 不吸收 thenable。补充 yield callback()
  的惰性执行、次数、receiver、异常原值和提前 return/throw 不执行 callback。
  200/201/402 个连续 yield 的生成 JS 也完整执行，重复使用恢复寄存器且中途更新 cell。
- 五个文件反例覆盖错误 state/done/RETURN mode、外部进入协议内部、RETURN
  路由夹有调用；树反例排除非 generator 的 yield 和同步 generator 的 await。
  红例 `/tmp/abcd-yield-red.log` 原先失败于创建身份；现已生成并执行完整源码。
  相关十个 C++ 目标构建成功，26/26 定向测试通过（10.40 秒）：
  `/tmp/abcd-generator-final-build.log`、`/tmp/abcd-generator-final-tests.log`。
  `git diff --check` 通过；使用真实指令与合成元数据夹具，不冒充真实应用验收。
- C07 未完成：用户 try/catch/finally、入口词法/参数前奏、generator 方法与 self
  等组合仍待推进；yield*、async generator 和其余 C06–C09/ISA/结构整合目标保持开放。
  默认 work 阈值保持关闭；未恢复全量测试、语料、RSS 或基准。历史两个
  base_function 失败未在本轮修复或重跑，不宣称全套测试或 1GB 验收完成。

### 2026-09-28：默认源码产出不再受 work 配额截断

- 按用户明确要求，关闭默认语义分析的 100 万 work 和旧方法/派生构造器路径的
  1 亿 work 阈值。默认值使用 uint64 计数器完整范围，不再配置功能性的 work
  配额；统计和显式有限预算的底层兼容接口保留。未批量删除已有检查或另建 DFX。
  计数器表示范围不是本阶段承诺的无限执行能力；内存、合法输入和输出写入检查不变。
- CreationExpansion 对默认无限额度不再逐次减半/扣减，避免仅因兄弟函数数量
  耗尽 work。显式有限预算的旧调用仍保持原约定。同步移除四个测试中对默认额度
  的乘法，避免将完整计数范围拿来放大而回绕。
- 402 层共享 finally 的完整源码路径先在旧默认下复现 `FunctionTreeError::kWorkLimit`
  （红例 `/tmp/abcd-work-output-red.log`），修改后完整输出并执行正常/异常/清理
  行为对照；不再只验收其控制计划。另覆盖默认预算下 201 个兄弟函数的展开。
- 相关十四个 C++ 目标构建成功，29/29 定向测试通过（20.84 秒），日志
  `/tmp/abcd-work-output-final-build.log`、`/tmp/abcd-work-output-final-tests.log`；
  `git diff --check` 通过。设计入口与主计划已同步明确默认输出策略。
- 本轮未扩大语义支持范围：generator 接入仅完成上游协议检查，因本次用户要求
  转向修复产出策略，未实现或声称 yield 已贯通。全量测试/语料/RSS/基准继续暂停，
  既有两个 base_function 失败未重跑或修复，1GB 长期约束未据此宣称实测达标。

### 2026-09-28：async arrow 自身 VM arguments 快照

- 扩展已有 `InvocationForm::kArrowMethod` 到 async arrow：共享树构建选择
  async 方法承载，Printer 输出计算属性 async method，保留名称、length、
  不可构造性和 AsyncFunction 原型；不增加转发函数或额外 await/Promise 回合。
  原箭头 ABI 对动态 receiver/new.target 的排除仍生效，外层值仍经显式捕获。
- 文件解码夹具在 await 后执行 GETUNMAPPEDARGS，经过 ReadFunctionFamily、
  BuildFileFamilyTrees 和 PrintFamilyDraft，使用已有 `kV7CurrentSlots` 宿主绑定。
  三种 profile 的生成 JS 验证零实参、显式 undefined、额外实参、多个调用逆序
  恢复、拒绝值身份、拒绝不执行快照、thenable 与微任务顺序。反例覆盖缺失宿主
  绑定、伪造直接箭头 invocation、伪造普通 async function callable。
- 红例 `/tmp/abcd-arrow-arguments-red.log` 失败于共享树构建；实现后生成完整源码。
  相关十个 C++ 目标构建与 25/25 定向测试通过（13.82 秒），包含既有
  `argument_snapshot_source_test` 和新增 `file_async_arguments_source_test`；日志
  `/tmp/abcd-async-arguments-final-build.log`、`/tmp/abcd-async-arguments-final-tests.log`。
  增加反例时曾遗漏内部 tree_structure 头文件，已修正并完整重跑该集合。
- 此功能要求现有宿主快照 capability；未提供时仍不能声称完整输出。夹具元数据
  是合成的，未据此声称应用覆盖、全部 arguments 场景或 C06 完成。generator、
  async generator/yield* 和其他通用组合仍待推进。默认 work/内存值及 DFX 不变；
  全量测试/语料/RSS/基准仍暂停，历史两个 base_function 失败未重跑或修复。

### 2026-09-28：async arrow 贯通共同函数族与捕获恢复

- 按 OH7 `PandaGen::SetFunctionKind` 的 ASYNC_NC_FUNCTION 身份恢复
  `CallableKind::kAsyncArrow`，复用文件级 await/completion 归一化、值分析、
  FunctionTree 和共同 Printer，输出真正的 `async (...) => ...`。
  名称推断、创建绑定式 self 沿用箭头约定；不赋予动态 receiver 或具名函数 self。
- 三种 profile（12.0.2/12.0.6/13.0.1）的真实解码夹具覆盖每轮/每 catch/普通
  环境捕获、跨 await 共享与隔离、恢复后创建子闭包、用户 catch、finally 内 await
  及 pending completion。额外外层 LDTHIS→cell→async arrow 用例，改变 bind receiver
  并逆序恢复多个挂起调用，确认保留各自的外层 this（含 null/undefined/原始值）。
- 红例 `/tmp/abcd-async-arrow-red.log` 原先失败于创建身份拒绝；现在生成并执行源码。
  相关十个 C++ 目标构建成功，23/23 定向测试通过（11.80 秒），日志
  `/tmp/abcd-async-arrow-final-build.log`、`/tmp/abcd-async-arrow-final-tests.log`。
  新增 `file_async_arrow_source_test` 和 `file_async_arrow_finally_source_test`；
  function_tree_test 覆盖 async arrow 创建 self 与动态 receiver/具名 self 反例。
- 范围仍是受控 ABC 夹具，不代表真实应用或 C06 完成。async arrow 的自身 VM
  arguments snapshot carrier 尚未支持，不能误输出为外层 arguments；generator、
  async generator/yield* 等缺口仍在。work/内存默认值未改，未增加 DFX/输出准入设施。
  全量测试/语料/RSS/基准仍暂停；此前两个 base_function 失败未在本轮修复或重跑。

### 2026-09-28：实例与静态 async 类方法进入共同输出路径

- 文件化 async 方法先在 ClassTreeContext 的 kKind 拒绝。锁定 runtime 的
  LiteralDataExtractor::DefineFunctionTemplate 保留普通 METHOD tag 上显式的
  ASYNC kind；上下文现记录 kAsyncMethod，仍要求唯一 ordinary 类成员来源，
  排除 sendable/constructor/generator 等不相容种类。
- FilePhasedFunction 对 async method 复用普通 async 的 completion/await
  归一化、原始 PC 和相位值流；接收者与 ABI 继续消费同一模型。共同
  PrintClassFamilyDraft 在真实类根输出 `async run(...)` / `static async run(...)`，
  并核对树/context callable 一致。不调用旧 async writer，不扩充发布 receipt。
- 三 profile × prototype/static 共六个真实 Code/异常表夹具完整输出并执行：
  await 后返回 this，检查原始值 receiver、拒绝/then getter throw、thenable
  receiver、微任务顺序、逆序恢复的并发调用隔离、name/length、不可构造性和
  类方法属性描述符。外围 metadata/类测试外壳为合成，不是整类构造器或整应用验收。
- 无类上下文的打印、同步上下文配 async 树、generator/async-generator/async-arrow
  方法种类的反例拒绝且不写伪源码。首次测试编译缺少 coroutine 头文件，已补齐；
  行为红例定位见 `/tmp/abcd-async-method-debug.log`（kKind）。
- 相关十个目标构建成功，21/21 定向测试通过（12.38 秒），新增
  `file_async_method_source_test`。日志 `/tmp/abcd-async-method-final-build.log`、
  `/tmp/abcd-async-method-final-tests.log`；`git diff --check` 通过。
  work/内存默认值未变，全量测试/语料/RSS/基准仍暂停；此前两项 base_function
  旧路径失败未修复、本轮未重跑。旧严格发布入口未据此宣称全面支持 async method。
- C06 尚未完成：async arrow、复杂 method/handler/闭包组合、super/home-object
  及真实应用仍待推进；C07–C09、全 ISA 和 record/类/签名整合目标保持开放。

### 2026-09-28：async 自身身份与跨 await 接收者

- 新文件化 `await input; return <LDFUNCTION/LDTHIS/receiver-slot>` 用例定位两处
  缺口：async self 被共同树绑定/作用域/ABI 的同步种类限制拒绝，LDTHIS 在相位
  值分析中未获得 frame.thisObj 初始化依据。接收者 ABI 寄存器路线原已可用。
- 共同 FunctionTree 的命名/私有自身绑定和对应 Scope/ABI 检查接纳普通 async
  function；Printer 原有 async 语法直接复用。ExceptionValueFlow 复用现有
  FrameReceiver 参数，默认仍 Unknown；PhasedBody 从已解析 callable 提供依据，
  遇到 SuperRange 撤销。箭头、派生构造器和未贯通 async method 不据此放行。
  暂停保存/恢复身份依据锁定 runtime 的 SaveFrameToContext 与 interpreter-inl.cpp。
- 四类 × 三 profile × 三父作用域共 36 个文件化函数族完整生成并执行 JS：命名
  self、非标识符运行时名字的私有 self、LDTHIS 和 receiver slot。对照原生 async
  的自身/receiver 身份、原始值 this、thenable/拒绝与微任务顺序、name/length、
  不可构造性；两个调用逆序恢复仍保持独立 this。fixture metadata 为合成，非整应用验收。
- 补充默认/显式 Unknown receiver 不能使 LDTHIS 结果通过输入检查的反例，以及
  async self 名称冲突和不可变绑定检查。初次扩充测试遗漏 NOP 来源 PC 36、
  新名字的 UTF-16 长度，并遇到旧测试明确拒绝 async self 的断言，均已据事实
  修正测试；未修改无关生产路径来适配夹具。
- 相关十个目标构建成功，20/20 定向测试通过（13.39 秒），新增
  `file_async_frame_source_test`。日志 `/tmp/abcd-async-frame-final-build.log`、
  `/tmp/abcd-async-frame-final-tests.log`；初始失败见
  `/tmp/abcd-async-self-red.log`、`/tmp/abcd-async-this-red.log`。
  `git diff --check` 通过。未调 work/内存默认值、未加 writer/DFX；此前两个
  base_function 旧路径失败未修复、本轮未重跑，全量测试/语料/RSS/基准仍暂停。
- C06 未完成：async method/arrow、更多跨暂停值和 handler 组合、真实应用验收
  仍需推进；generator/async generator/yield* 与原结构整合目标保持开放。

### 2026-09-28：finally await 重用 resume 临时寄存器

- 将文件化 finally 第二次 await 改为重用首个 await 的 result/mode，先在原始
  PC 8 的 catch-input 检查复现拒绝。问题是后缀 may-read 集合把后续协议中读取
  新定义的操作也计为对旧输入值的读取；不是 work 额度耗尽。
- 分析现区分精确 await 协议的局部定义/使用，保留其它位置的全部读取。额外
  索引普通分支及 handler 到 continuation 的外部入口；该入口可能绕过新定义，
  不能排除其读取。仍复用既有协议与压缩范围索引，不逐 await 遍历 handler，
  不新增 writer/发布前置包装，也未改默认限额。
- 三 profile × 三父作用域的重用布局经真实 Code 解码、共同 AST/Printer 完整
  输出，执行同一原生 async oracle 对照矩阵：原拒绝与清理拒绝覆盖、thenable、
  微任务、pending cleanup 及跨两次暂停的共享 cell。新增
  `file_await_finally_reuse_source_test`；原不同寄存器布局继续通过。
- 分析正例覆盖 1/200/201/402 await/分支加重用的 handler await；反例覆盖普通
  跳转/异常入口绕过定义、把旧 result 作为下一 await 输入、固定/range 读取、
  未知效果/协程及外跳。协议外的更一般重定义/活跃性仍可能保守拒绝，不能把
  此项称为任意多暂停点寄存器重用均已支持。
- 相关九个目标构建成功，18/18 定向测试通过（12.51 秒）；完整命令集合/结果
  见 `/tmp/abcd-await-reuse-final-tests.log`，构建日志
  `/tmp/abcd-await-reuse-final-build.log`；初始失败和定位见
  `/tmp/abcd-await-reuse-red.log`、`/tmp/abcd-await-reuse-debug.log`。
  `git diff --check` 通过。此前两个 base_function 旧路径失败未修复、本轮未重跑；
  全量测试/语料/RSS/基准仍暂停。C06、更广 ISA 和结构整合目标仍未完成。

### 2026-09-28：共享 finally 内再次 await 保留 pending completion

- 新文件化双 await 夹具先在原始 PC 8 的 catch-input 分析拒绝：第二个协议已被
  FindAwaitRegions 识别，但后缀分析仍把 suspend/resume 当未知效果。现在用逆向
  单调游标定位既有协议，仅其精确 await/suspend/resume/mode PC 消费类型化
  coroutine reads；未识别协程、arguments 隐式读取等未知效果继续拒绝。
  不引入另一套暂停分析或 async writer，不改变默认 work/内存额度。
- 三 profile × iteration/catch/normal 三父作用域，共 9 个文件化函数族完整输出
  `try { await input; } finally { await cleanup(cell); }` 对应的共同 AST 源码。
  实际 JS 对照原生 oracle，覆盖原拒绝保留、清理拒绝覆盖、then getter 抛出、
  thenable receiver/调用顺序、微任务顺序、清理一次和第二次暂停期间的 cell 改写。
  pending cleanup 不提前结束函数；原始 await PC 8/66 和返回 PC 118 保留。
- 分析定向例包含 1/200/201/402 个 await/分支加 handler await，以及暂停后读旧
  result/mode、range 读取、未知协程、未知效果、普通/异常外跳反例。后缀证明仍
  是保守 no-read 条件，不声称解决所有临时寄存器重用或任意 CFG 活跃性。
- 相关构建与 17/17 定向测试通过（11.54 秒）；命令目标为
  `loop_closure_file_test control_composition_test async_semantics_test class_tree_context_test
  creation_tree_context_test phased_value_import_test phased_value_flow_test tree_structure_test
  function_tree_test`，CTest 集合在 `/tmp/abcd-finally-suspend-final-tests.log`，
  构建日志 `/tmp/abcd-finally-suspend-final-build.log`；新增
  `file_await_finally_suspend_source_test`。红例及调试日志为
  `/tmp/abcd-finally-suspend-red.log`、`/tmp/abcd-finally-suspend-debug.log`。
- C06 仍未完成：多暂停点寄存器重用、更复杂 handler/live-value、async
  method/arrow/self/receiver 和真实应用验收仍待推进；当前外围 metadata 为合成
  夹具。此前两项 base_function 旧路径失败未修复、本轮未重跑；全量测试、语料、
  RSS 和基准仍暂停。整体开发目标继续，不能用本项通过替代整体完成。

### 2026-09-28：文件级 try/await 与共享 finally 贯通

- 真实 opcode/双异常表新例先在 await catch-input 检查拒绝（原始 await PC 8）：
  finally 中的分支使旧首用检查返回未知，虽然 resume result/mode 此后均无读取。
  增加控制闭合后缀的保守 may-read 判定，复用压缩范围索引，无每 await 的 handler 遍历；
  所有普通/异常目标必须留在后缀，任何读取或未知语义效果不能据此放行。
- 反例发现仅凭 raw register effects 会漏掉无 frame 的 arguments 隐式读取，已收紧为
  完整 semantic lift 的效果；固定/range 读取、未知效果、正常/异常外跳均拒绝。
  1/200/201/402 分支、200/201/402 await 与分支组合及闭合循环通过定向检查。
- 三 profile × 三父作用域共 9 个文件化函数族生成共同 AST 的 await/finally；
  实际 JS 对照原生 oracle 检查 fulfill/reject、then getter throw、thenable receiver、
  清理一次、清理抛出覆盖原拒绝、清理期间的捕获 cell 改写和微任务顺序。
  pending Promise 不提前进入 finally；原始 PC 8/43/63/88 保留。未增加 async writer。
- 最终相关构建和 16/16 定向测试通过，日志 `/tmp/abcd-await-finally-final-build.log`、
  `/tmp/abcd-await-finally-final-tests.log`；新增 `file_await_finally_source_test`。
  额外运行的 `base_function_test`、`base_function_js_test` 失败，分别位于
  `base_function_test.cc:260` 的 for-in 文本断言和 `:58` 的旧 fixture 发射断言。
  临时恢复本轮修改前的 `await_catch.cc` 并重新构建后，两者仍相同失败；对照日志
  `/tmp/abcd-await-finally-baseline-tests.log`，当前版本失败日志
  `/tmp/abcd-await-finally-legacy-tests.log`。未修改无关旧路径，也不声称全项目通过。
- C06 未完成：finally 本身再次 await、多暂停点与复杂 handler/live-value 组合、
  async method/arrow/self/receiver 及真实应用验收仍待推进。此次外围 metadata 为合成
  夹具；全量测试、语料、RSS、基准未恢复，work/内存默认额度未调整。

### 2026-09-28：文件级用户 catch 接收 await 拒绝

- 在已接通的文件级 async 主干上，增加用户 try/catch 与函数级 completion catch 共存的
  真实双异常表夹具；三 profile × 三父作用域共 9 个函数族。归一化仅移除隐式 reject
  尾部，用户 catch 仍由共同区域/AST 消费；无需新增生产逻辑或专用 writer。
- 实际生成源码对照原生 async oracle：普通值/Promise fulfill 后读捕获 cell；Promise
  reject、then getter throw、thenable reject 进入用户 catch 并返回原抛值，检查
  receiver、任意 payload 身份与微任务事件顺序；保留原始返回点 PC 39/42。
- `cmake --build build --target loop_closure_file_test control_composition_test -j4` 和相关
  6/6 定向测试通过；日志 `/tmp/abcd-file-await-catch-final-build.log`、
  `/tmp/abcd-file-await-catch-tests.log`。此提交仅增加共同主干的纵向回归证据。
- 仍未完成 C06：await 与 finally、多个暂停点、更复杂用户 handler/live-value 组合、
  async method/arrow/self/receiver 和真实应用仍待推进。全量测试、语料、RSS、基准未恢复。

### 2026-09-28：文件级 async 函数与跨 await 闭包进入共同主干

- 普通 async 创建上下文不再在 kind 入口被拒绝；文件读取调用既有 completion/await
  归一化，送入共同 ExceptionFlow、ValueProgram、FunctionTree 和 Printer，未调用旧 async writer。
  保留原始解码 body 供参数调试信息消费；工作 body 用于控制分析。
- 共同相位导入保留原始 PC；AST 沿用既有 `original` 字段。await 后 DEFINEFUNC 的
  occurrence 查找和子函数 owner 对照使用原始 PC，控制/capture 计划继续使用工作 PC。
  新闭包用例先在 occurrence 查找失败，修复后原始创建点 35 与完整子函数同时保留。
- 文件化真实 opcode/异常表覆盖三 profile × 三父作用域 × 两种子函数，共 18 个函数族：
  await 后直接读捕获 cell，或创建并返回继续共享 cell 的闭包。外围 metadata/header 为
  合成夹具，不是完整应用 ABC 验收。生成 JS 实际验证暂停期间兄弟闭包改写、不同
  iteration/catch 实例隔离、恢复后继续共享、任意拒绝值身份、thenable receiver/调用顺序，
  同时检查 async 名称、length 和不可构造性。错误私有 await 状态和来源篡改继续拒绝。
- 最终构建与相关 15/15 定向测试通过，日志：`/tmp/abcd-file-await-final-build.log`、
  `/tmp/abcd-file-await-final-tests.log`；新增 CTest 为 `file_await_capture_source_test`。
  初次回归发现工具仍报告工作创建 PC，已修正并重跑；新增反例缺头文件的编译错误也已修正。
- C06 尚未完成：含 await 的用户 try/catch/finally、多个暂停点、async method/arrow、
  async 自引用/receiver 与真实应用组合仍待推进。C05 其余组合缺口保留。
  work/内存默认额度未调整，未新增准入封装；全量测试、语料、RSS、基准仍暂停。

### 2026-09-28：首个 await 协议贯通共同 ValueProgram/FunctionTree

- 真实 opcode async enter/await/suspend/resume/resolve/reject 夹具经既有 owner
  归一化，曾在 `ImportPhasedValues` 返回 `kUnsupportedOperation`（归一化 PC 2）。
  新增 ValueAwait 接收已归一化 AwaitValue，保留协议来源与原有 suspension/user-code/
  heap/exception 效果；不是接纳未证明的原始状态机，也不允许纯操作删除。
- 共同表达式构建生成 AwaitExpr；操作数对应和结构检查消费该节点，限定 async 上下文；
  共同 Printer 支持 async function 与 await，不调用旧 async writer。
- 三 profile 的完整生成源码已执行：普通值、Promise fulfill/reject、then getter throw、
  thenable receiver、重复 settle 及微任务事件顺序与原生 async oracle 对照。
  同步上下文放入 await、删除实际 await 的反例拒绝。
- 构建和源码/控制相关 17/17、IR 相关 8/8 通过：
  `/tmp/abcd-await-common-final-tests.log`、`/tmp/abcd-await-ir-tests.log`；
  对应 build 日志为 `/tmp/abcd-await-common-final-build.log`、`/tmp/abcd-await-ir-build.log`。
- C06 尚未完成：文件级现代 async 入口未接入此次归一化，try/catch/finally 含 await、
  跨暂停 live binding/闭包、命名 async 自引用等组合继续开发；旧路径不计为新主干完成。
  C05 其余组合仍保留缺口。全量测试、全语料、RSS、基准均未恢复，限额未调整。

### 2026-09-28：内层 try 的循环转移汇入外层共享 finally

- 完成状态摘要保留尚未被本作用域消化的循环目标；共同控制结构已约束这些目标为
  活跃祖先循环链，摘要保留最外层目标，避免向每层 try 复制目标集合。
  循环深度来自既有 LoopForest，线性计算，不另做循环发现。
- TryExitUnit 接往已绑定的 LoopTransfer 时继续汇总；拥有目标循环的外层作用域
  消化内部转移。候选 finally 仍要求没有未消化的循环目标，不能吞掉真正逃逸的跳转。
- 三 profile 真实 opcode/双异常表新例先因未恢复 finally 失败，修正后实际生成
  `while` 内 `try` 的 break/continue 和外层 finally。Node 对照条件退出、内部 break、
  重复 continue、条件/主体抛出以及清理覆盖，检查原异常身份与调用顺序。
- 构建和相关 15/15 定向测试通过：`/tmp/abcd-shared-transfer-final-build.log`、
  `/tmp/abcd-shared-transfer-final-tests.log`。首轮源码测试有变量重名的脚本语法错误，
  已修正并完整重跑定向集合。work/内存额度未变，暂停项未恢复。
- 尚不是 C05/整体完成证据；更深层多循环目标组合与完整应用验证继续保留为缺口。

### 2026-09-28：共享 finally 中的 catch 循环行为贯通

- 新增三个 profile 的真实 opcode/双异常表夹具：内层 catch 保存原抛值，循环
  调用回调；正常离开或回调抛出都经过外层共享清理。现有共同恢复已支持该形状，
  本轮仅补纵向回归，没有增加生产分支、writer 或准入设施。
- 实际生成 JS 验证捕获值跨回边身份、未进入 catch 时零次回调、立即退出及多轮回边、
  首轮/末轮 catch 抛出、cleanup 覆盖和后继抛出，均检查 receiver/完整调用顺序。
- 构建与相关 4/4 定向测试通过：`/tmp/abcd-shared-catch-loop-build.log`、
  `/tmp/abcd-shared-catch-loop-tests.log`。这是解码夹具证据，不是完整应用验收；
  全量测试、语料、RSS、基准仍暂停。跨嵌套 try 的外层跳转等 C05 组合仍待推进。

### 2026-09-28：共享 finally 保护域内循环

- SharedScope 消费既有 LoopControl/LoopTransfer；记录本次 protected/handler 入口
  拥有的循环，内部 break/continue 不再误判为逃逸。指向外层循环的跳转仍不能据此
  折叠为 shared finally；没有引入另一套循环发现算法或专项 printer。
- 真实 opcode 解码夹具在三个 profile 上生成循环与 finally 的完整源码。
  Node 对照立即退出、1/4 次回边、首轮/末轮抛出、清理覆盖异常、后继抛出，
  验证异常身份、receiver、顺序以及清理仅一次。此证据不是完整应用 ABC 验收。
- 新用例先因未恢复 finally 失败：`/tmp/abcd-shared-loop-red.log`；修正后相关构建
  和 15/15 定向测试通过：`/tmp/abcd-shared-loop-final-build.log`、
  `/tmp/abcd-shared-loop-tests.log`。work/内存限额未改，全量测试/语料/RSS/基准仍暂停。
- C05 尚未完成：共享协议的 catch 内循环、跨嵌套 try 的外层跳转等组合继续推进。

### 2026-09-28：避免同次树构建重复检查异常控制计划

- 定位 200 层共享 finally 的默认 work 失败：构建入口已检查控制计划，
  finally 恢复前和最终语义检查又重建/核对同一投影。单次重复消耗 222,129 work。
- 同步构建内部复用未修改的控制输入检查；独立入口默认仍完整检查。
  AST 结构、求值计划、phi copies、操作数与来源检查保留，不新增 verified token，
  不提高默认 work 或内存配额。此改动只消除这条异常构建路径的重复控制检查，
  并不声称所有预算容量问题已解决。
- 默认额度下 200/201 层现能输出完整源码，并由 Node 对照任意抛值身份、cleanup
  覆盖、receiver 和调用次序。402 层仍只有已有 plan 证据，不宣称完整源码通过。
- 红例 `/tmp/abcd-work-reuse-red.log`；相关目标构建和 15/15 定向测试通过：
  `/tmp/abcd-work-reuse-final-build.log`、`/tmp/abcd-work-reuse-tests.log`。
  补充篡改控制投影反例后，相关 3/3 再通过：`/tmp/abcd-work-reuse-negative-tests.log`。
  全量测试、全语料、RSS、基准未运行。下一步继续共享 finally 与循环组合，整体目标未完成。

### 2026-09-28：嵌套 catch/rethrow 与共享 finally 贯通

- 对照 pinned `TryStatement::CompileTryFinally/CompileTryCatchFinally`，支持共享清理
  外层保护域内的 catch/rethrow，以及执行 catch 回调后正常汇合或再次抛出。
- 纯跳转/Hole 传输的控制归属支持跨多层保护域，使用祖先索引，保留原始 SSA、边和
  语义作用域。完成状态自底向上汇总，区分被祖先 handler 接住的异常和真正逃逸；
  内层直接 return 绕过清理的输入不会被误恢复成 finally。
- 三 profile 的嵌套完整源码经 Node 验证：原异常身份、catch 新异常、cleanup 覆盖、
  receiver、调用次序与后继执行。4/8/16 层完整源码也通过行为对照。
- 200/201/402 层通过控制与 CompletionPlan 检查；200 层完整树构建触及既有
  `kWorkLimit`，未据此宣称语义不支持，也未放大/关闭现有限额或扩建预算设施。
- 文件级函数族加入两张真实编码异常表和共享 finally，三 profile × 三种父作用域的
  捕获 cell 读写/实例隔离通过。外围 header/index 仍为合成元数据，非完整应用验收。
- 本轮构建和相关 15/15 测试通过：`/tmp/abcd-nested-shared-final-build.log`、
  `/tmp/abcd-nested-shared-final-tests.log`。全套测试、全语料、RSS、基准仍暂停。
  共享协议中循环及跨区域 abrupt completion 的更多组合仍待贯通，C05/整体目标未完成。

### 2026-09-28：共享清理区域恢复为 finally

- 补充文件级函数族纵向验证：真实临时文件 Code entity/异常表 → 现代 phased method →
  闭包族 → 共同源码入口。三 profile × per-iteration/per-catch/normal 父形状，
  共享 finally 的清理读取捕获 cell；逃逸后执行、兄弟 setter 写入和不同动态实例隔离均通过。
  缺失捕获 cell 的失败用例仍保留。外围 header/index 为合成测试元数据，不是完整应用 ABC。
  `loop_closure_file_test`、`loop_closure_source_test`、`completion_baseline_source_test`
  3/3 通过：`/tmp/abcd-shared-finally-file-final-tests.log`。
- 在上述 Hole 源码贯通基础上，CompletionPlan 关联 catch/Hole 的 phi 与唯一重抛分支；
  确认 try/catch 汇合后，将共享清理投影为普通 TryStmt.finalizer，继续使用共同 printer。
  清理仅一份，原异常暂存、比较与后继重抛仍保留；未做源码级临时变量美化。
- 变换不改变 ValueProgram、动态 cell 或原操作身份。共享序列采用批量替换，
  避免按区域重复搬移整个后缀；未运行性能基准，不宣称实测性能或 RSS 达标。
- 三 profile 的单段及 2/3 段连续协议生成源码通过 Node 对照：调用顺序、异常身份、
  清理异常覆盖、后继异常及停止位置；提前 return 绕过清理的反例保持展开语义。
- 相关 15 项测试通过：`/tmp/abcd-shared-finally-focused-tests.log`；新增反例后的核心
  4 项再次通过：`/tmp/abcd-shared-finally-final-tests.log`。原 `--compiler-finally`
  形态用例已转绿。没有恢复全量测试、全语料、RSS 或基准。
- 剩余边界：共享协议的嵌套保护域、跨区域提前退出暂不做此 finally 投影；
  这不替代完整 es2abc 产物、C05 全组合、C06–C09、全 ISA 及结构 gate 验收。

### 2026-09-28：共享 Hole-finally 协议贯通到源码

- 将闭合的 Hole 复制/phi、身份比较、受保护重抛协议接入共同 evaluation、binding、
  FunctionTree 和现有 printer；现代 phased method 入口允许原始 Hole 传输，源码层仍拒绝逃逸。
- 每次调用创建私有 const 对象身份，比较降为严格相等/不等；保留原始抛出值，
  不把 Hole 替换为 undefined，不触发用户对象隐式转换。
- 控制投影只扩展到纯 Hole 传输路径；普通复制路径保持原处理，避免已有 finalizer 回归。
- 三个历史 profile 的真实 opcode 解码用例已输出完整 JS；覆盖正常返回、throw undefined、
  对象/符号/NaN、cleanup 与后继异常及调用顺序。此用例手工构造 pinned 编译协议，
  不是完整 es2abc 产物验收。源码暂为 try/catch + 共享清理 + 条件重抛，尚未恢复为 finally。
- 相关 15 项定向测试通过（`/tmp/abcd-hole-source-focused-tests.log`）；不运行全量语料、
  全套测试、RSS 或基准。原 `--compiler-finally` 的 finally 形态要求仍是后续缺口。


2026-09-28 最新用户调整：[FunctionTree 与部分输出设计](function-tree-output.md) 优先。
投入以反编译功能为主；目标是 FunctionTree 直接进入共同 printer，允许函数前
INCOMPLETE 与区域缺口标记。不要求新增 VerifiedFunction/不可变准入包装，新增
budget/work 计费、通用验证器、发布 receipt 和全面映射审计暂缓；已有设施保留。
语义恢复和定向行为测试继续，部分输出不算完整支持。下述完整 gate 保留为最终
目标，不再阻止已恢复片段输出。C00a 已开始实现，具体已测范围见本次证据；
真实解码直线前缀部分构建和单根草稿 CLI 已有定向证据，更广区域保留尚未贯通，
不按子项宣布 C00a 完成。

执行目标已按用户要求调整为两阶段：

1. **优先覆盖 pinned `isa.yaml` 全部 groups 的通用反编译**：按文件中的 19 个 group
   顺序推进全部 332 个编码格式的解析、语义 IR、AST、源码输出和测试。LD/ST 方案
   纳入其中，跨作用域、词法环境、lambda、generator、async/async generator、多应用
   和历史 profile 要求不变。完整顺序见 [全 ISA 优先级](isa-priority.md)。
2. **随后完成原 T00–T12 剩余目标**：保留全部原验收要求，不缩减范围、不以第一阶段
   的局部成功替代原结构 gate 或整体完成。

各指令必需的 T07/T08/T09/T10/T11 能力在第一阶段按依赖提前实现，复用同一主干；
不等第二阶段才处理异常、捕获、receiver 或协程，也不创建平行专项路线。
独立开发，不使用 CC 或子代理；不恢复定期审视、RSS/基准或调度器开发。
源码证据规则版本为 58，默认源码契约为 V3。原 T07 仍未完成，但不再作为独立于全 ISA 覆盖的首要执行目标。
T00–T12 整体未完成；实例字段、全局变量、完整签名与函数体整合的原始 gate 未完成。
用户批准访问恢复方案并要求测试覆盖，现恢复相关单元/集成及受控生成 JS 执行。
全量测试、基准及 RSS 未恢复；下表均不代表阶段验收通过。

| 任务 | 实际状态 | 主要未完成项 |
|---|---|---|
| T00–T02 | 身份/路线、值 IR、复制与死值简化已有实现和生产构建记录 | 跨 profile、反例、预算执行验收 |
| T03、T06、T04 | 真实方法片段已走共享源码树；参数/默认值、普通调用已有实现 | 完整函数族、签名迁移及跨应用行为验收 |
| T05 | 正常边复制、自然循环、MethodEntry 初始化已接入树和独立检查 | 真实循环方法源码验收、执行验证 |
| T07 | 字面量、cell、逐创建点捕获、模块读取配对、共享语法解析已实现；新增真实文件函数族输入读取 | 全部子函数到 AST/源码的整体贯通，捕获写入与作用域闭合 |
| T08 | 新主干 Receiver/ClassTree 尚未完成 | 构造器并入通用 callable、类与实例字段整合 |
| T09–T10 | 新主干异常/async 尚未完成 | 异常相位、completion、恢复 owner；P3 子函数依赖这些能力 |
| T11 | 模块读取和别名已有前置构件，不是完整 record 支持 | 初始化/TDZ/循环依赖、模块及全局写入、整份 record 发布 |
| T12 | 语料与分类基础已有历史记录 | 全样本新路线审计、跨版本/留出集、进程 1G 实测 |

## 本次证据

- **共享 finally 仍未生成源码**：新增直接消费上述 ValueProgram 的 Hole 使用
  分析，实际识别三个 profile 复现中 PC 39 的比较和 PC 46 的受保护重抛。
  只接受由 ValueHole/复制/phi 形成的封闭内部值流，比较左侧必须确定为 Hole；
  重抛需沿单入口路径由非 Hole 分支保护。返回逃逸、比较极性反转、算术消费
  和损坏 use 链分别有反例。复制链压缩和路径缓存均为迭代 O(V+O+U+B+E)，
  不逐 throw 遍历整图。该分析目前由真实解码集成夹具消费，**尚未接入 AST
  消除，不能作为新的源码能力计数**。相关构建与定向 4/4 通过，日志
  `/tmp/abcd-hole-protocol-build.log`、`/tmp/abcd-hole-protocol-tests.log`。
  下一步是把已识别协议转换为共同树中的 completion/cleanup，不新增输出 token。
- **共享 finally 仍在实现中，新增的是值图贯通，不是源码支持**：共同 IR
  现以 `ValueHole` 保留 VM 哨兵、原 SSA 域和 PC。显式 Hole 传输模式只允许
  Language/Hole，不接纳 Environment/OtherInternal；默认源码输入检查仍拒绝
  Hole。相等比较正常结果细化为语言布尔值，未改操作数域、效果或异常边。
  `--compiler-finally-import` 在三个 profile 均已导入含共享 cleanup、Hole/语言值
  phi 和受条件控制重抛的完整 ValueProgram；尚未消除协议，源码求值继续拒绝。
  该导入回归已加入既有 control_composition_test；原 `--compiler-finally` 源码
  RED 入口仍保留，不能当作已通过。相关 10/10、词法关联 3/3 定向测试通过，
  见 `/tmp/abcd-hole-import-focused-tests.log`、`/tmp/abcd-hole-lexical-tests.log`；
  构建日志同名前缀 `*-build.log`。同时修正既有寄存器值测试中 LDA_STR 的
  过期 Language 预期为已经实现的 String。下一步必须做封闭哨兵协议的使用
  分析及源码消除，不能将传输模式用于放行 Hole 源码。未运行全量测试/语料/RSS。
- C05 新增仅含 break/continue 的 finally：原先裸跳转由共同控制树收进
  try 主体尾部，没有 wrapper 后继，因而未恢复 finalizer。现用类型化的
  body-tail 放置保留原 ControlId，将无副作用的尾跳转移入共同 FunctionTree
  的 finally；没有 cleanup opcode 时显式保存空锚点，不伪造 OpId。
  三 profile 共 12 个完整解码夹具包含同目标覆盖与两侧不同跳转反例，实际
  执行生成 JS，检查零/一/多次循环、分支跳过、原异常（含 undefined）取消、
  循环外异常不被吞掉及精确调用次序。相关构建与定向 15/15 通过，日志
  `/tmp/abcd-bare-transfer-final-build.log`、`/tmp/abcd-bare-transfer-final-tests.log`。
  当前仅支持直线主体的裸尾跳转；共享 Hole cleanup 协议仍未贯通，不因本项
  通过宣布 C05 完成。未运行全量测试、语料、RSS 或基准。
- **进行中，尚未支持**：按 pinned es2panda `TryContext::InitFinalizer`、
  `TryStatement::CompileFinally`、`PandaGen::EmitRethrow` 新增共享 cleanup 的
  Hole/undefined guard 解码复现入口 `control_composition_test --compiler-finally`。
  该入口是明确的 RED 复现，未加入通过型 CTest；不是完整 es2abc 文件验收。
  最初首个 profile 的解码、ExceptionFlow、SSA、值传播成功，
  VerifyExceptionInputs 在 PC 8 的 Hole 寄存器保存处返回 kInternalValue
  （node 10、SSA value 12），尚未进入 FunctionTree。证据日志
  `/tmp/abcd-compiler-finalizer-red.log`、`/tmp/abcd-compiler-finalizer-input-debug.log`。
  不将 Hole 改成 undefined、不放宽一般内部值输出检查；需在共同路径中
  识别并消除封闭的 pending-exception 哨兵协议。已有相关 4/4 回归通过，
  `/tmp/abcd-compiler-finalizer-existing-tests.log`；本条不代表新增源码支持。
- C05 新增 finally 内 break/continue 覆盖原异常：两侧 cleanup 的操作配对后，
  要求终结跳转的种类及 LoopId 均一致；原 TryExit 到循环跳转的路由继续保留。
  completion 使用独立 ControlId 对，不把合成跳转伪装成 opcode；既有 TreeFlow
  记录实际合成语句，FunctionTree 将两侧真实跳转放入 finally，Printer 无改动。
  覆盖型 break 暴露 try 摘要双出口归属缺口，共同出口扩展现允许单入口分支，
  多入口汇合仍停止；普通控制图有分支成功与共享汇合拒绝反例。
  三 profile 的单循环组扩为 18 个完整夹具，实际执行正常/异常、break/continue、
  覆盖及两侧跳转不一致反例，验证原异常被取消、cleanup 异常优先与循环次数。
  关联构建和定向 15/15 通过，日志 `/tmp/abcd-finalizer-jump-override-final-build.log`
  与 `/tmp/abcd-finalizer-jump-override-final-tests.log`。仅有跳转、没有 cleanup 操作的
  形式及更广嵌套/捕获/phi-copy 组合仍待覆盖；未运行全量测试、语料或 RSS。
- C05 已贯通 cleanup 后带外层标签的 break/continue：双重循环夹具先复现
  kLoopExits，退出路径归属由最外层限定扩展为 child-before-parent 顺序处理，
  Euler 区间只允许吸收无归属/祖先归属的线性路径，禁止进入同层或子循环。
  仍为共同循环层；每节点最多改变一次归属，不按嵌套深度反复扫描同一路径。
  三 profile 共 6 个完整解码夹具生成真实 finally 与外层标签，执行两层零/一/多次
  循环、跳过分支、run/cleanup 异常组合，检查目标、精确顺序和异常取消跳转。
  普通控制图另覆盖嵌套 1/200/201/402 节点线性退出路径；关联构建与定向 15/15
  通过，日志 `/tmp/abcd-finalizer-outer-final-build.log` 与
  `/tmp/abcd-finalizer-outer-final-tests.log`。尚不支持有分支的退出子图，也不代表
  finally 内覆盖 break/continue 或所有动态捕获组合完成。未运行全量语料、测试、RSS。
- C05 已贯通 cleanup 后 break/continue 的循环组合：continue 原路径已能恢复；
  break 夹具先在共享循环结构化报 kLoopExits。原因是自然循环不含只通向出口的
  try/cleanup 路径。新增共同线性退出路径归属步骤，由正常/异常投影共用，原图边
  与自然循环发现不变；单入口退出代码保留在循环内，cleanup 后发射真实跳转。
  三 profile 共 6 个完整解码夹具执行生成 JS，检查零/一/多次迭代、分支跳过、
  get/run/cleanup/after 次数与顺序，以及 cleanup 异常取消跳转并覆盖原异常。
  相关目标构建及定向 15/15 通过；新增普通控制图 1/200/201/402 节点路径、
  分支退出反例后，相关 5/5 再次通过。日志 `/tmp/abcd-finalizer-transfer-final-build.log`、
  `/tmp/abcd-finalizer-transfer-final-tests.log`、`/tmp/abcd-finalizer-transfer-control-tests.log`。
  当前归属扩展仅限最外层循环的单入口线性退出路径，遇到汇合、入口、其他循环、
  分支或终结停止；不是任意多出口/嵌套 pending break/continue 或 finally 内覆盖跳转
  的完整支持。未运行全量测试、语料、RSS 或基准。
- C05 新增保留型 pending throw，并贯通双层正常/return/throw 组合：以异常
  cleanup 末尾是否重抛原捕获值区分“保留 completion”与“cleanup 覆盖”；保留
  的正常抛出值必须在 cleanup 前可用。树构建沿同一叶子载荷/快照规则处理
  ReturnStmt 和 ThrowStmt，不把 throw 当 return，也不重复执行 cleanup。
  双层 return 用例现有实现已通过；throw 用例先复现只恢复一个 finalizer，修复后
  三 profile 共 9 个双层夹具均生成两个 finalizer。执行生成 JS 检查两层 cleanup
  修改外部状态后仍保留旧返回/抛出值，及全部 get/inner/outer/after 抛出组合、
  undefined/null/-0/NaN/Symbol/object 身份和精确调用顺序。
  关联构建成功、定向 15/15 通过；日志 `/tmp/abcd-nested-pending-final-build.log`
  与 `/tmp/abcd-nested-pending-final-tests.log`。尚非任意嵌套 abrupt completion
  支持；break/continue、复杂 cleanup 与 hole/guard 继续保留缺口。
  未运行全量测试、语料、RSS 或基准。
- C05 已贯通双层正常落下型 finally：内层 cleanup 在块末完成后进入外层区域，
  不要求同块还有后续语句。原始用例仅恢复外层一个 finalizer；修复后共同
  FunctionTree 中存在两个嵌套 finalizer，既有流/来源检查通过，Printer 未改。
  三个 profile 的解码输入经实际源码执行，覆盖 get/inner/outer/after 的全部
  抛出组合及 undefined/Symbol/object 原异常身份，验证两层 cleanup 的顺序、
  外层覆盖内层异常和 after 跳过行为。关联构建与定向 15/15 通过；日志
  `/tmp/abcd-nested-finalizer-final-build.log` 和
  `/tmp/abcd-nested-finalizer-final-tests.log`。
  交换异常表记录的尝试在前置 ExceptionFlow 构建被拒，未计入支持范围；
  本步不是任意嵌套 return/break/continue completion 完成，也未处理 hole/guard。
  未运行全量测试、语料、RSS 或基准。
- C05 正常 completion 已支持 cleanup 后接 if/return：正常路径在分支边界
  保留直线前缀，只要该前缀完整包含与异常路径配对的 cleanup，就由共同控制树
  继续消费后继，不再要求全部后继是直线。无新增分支算法或 printer 分支。
  三 profile 的落下组扩为 9 个夹具；正例先复现 finalizer=0，恢复后执行两侧
  分支与全部抛出组合。反例把分支放到 cleanup 之前，确保跳过 cleanup 的路径
  不误合并成无条件 finally，展开输出的行为也实际执行验证。
  关联目标构建成功、定向 15/15 通过；日志
  `/tmp/abcd-finalizer-successor-final-build.log` 与
  `/tmp/abcd-finalizer-successor-final-tests.log`。嵌套 finally、跨区域 completion
  及 hole/guard 仍未闭合；未运行全量测试、语料或 RSS。
- C05 新增正常落下的 finally：异常路径 cleanup + 原值 rethrow 与正常路径
  cleanup 前缀配对；正常路径后续操作保留在 finally 外，不伪造 pending return。
  FunctionTree 及既有流检查消费同一 NormalCompletion，Printer 无专项逻辑。
  新解码夹具先复现 finalizer=0，三个 profile 生成完整源码并执行 get/cleanup/after
  的全部抛出组合，检查 cleanup 覆盖、原异常身份、after 执行次数与顺序。
  相关目标构建成功，定向 15/15 通过；日志
  `/tmp/abcd-finalizer-fallthrough-final-build.log` 与
  `/tmp/abcd-finalizer-fallthrough-final-tests.log`。
  后续分支支持见上条；嵌套 finally 和跨区域 break/continue 尚未闭合。
  未运行全量测试、语料、RSS 或基准。
- C05 参数载荷边界补充实测：现有共同恢复路径已支持 finally 直接 return/throw
  函数参数，无需新增生产分支。三个 profile 的覆盖组扩为 24 个夹具，生成 JS
  执行覆盖 undefined/null/-0/NaN/bigint/Symbol/object，检查保护调用正常/抛出时
  均保留参数身份且不调用 cleanup。定向构建成功、相关 4/4 通过；日志
  `/tmp/abcd-finalizer-payload-build.log` 与 `/tmp/abcd-finalizer-payload-tests.log`。
  此项是既有能力的行为回归，不是新增 SSA 合流支持；异常路径不同值版本、
  嵌套 finally、跨区域 completion 仍待贯通。未恢复全量测试、语料或 RSS。
- C05 补齐仅含终结动作的覆盖型 finalizer：`RETURNUNDEFINED` 直接作为
  cleanup 动作；`LDUNDEFINED` + throw 的实体字面量复制随终结动作一起配对，
  不遗留在保护体外。捕获值/SSA 快照继续原有路径，不整体移动复制操作。
  新用例先复现 finalizer=0，修复后三个 profile 的覆盖组扩为 18 个夹具，
  实际执行生成 JS 检查原异常被覆盖、结果为 undefined 且无 cleanup 回调。
  关联构建及定向 15/15 通过，日志 `/tmp/abcd-finalizer-terminal-final-build.log`
  与 `/tmp/abcd-finalizer-terminal-final-tests.log`。未运行全量测试、语料或 RSS；
  更一般的 SSA 合流载荷、嵌套 finally、跨区域 completion 与 hole/guard 形式仍有缺口。
- C05 新增覆盖型 finalizer：正常与异常 cleanup 路径的 return/return 或
  throw/throw 终结动作在类型、载荷、效果与 handler 一致时，一并投影到真实
  finally 内；不虚构被覆盖的原始 return，也不将 cleanup 求值提前到保护体。
  沿用 FunctionTree 与共同 printer，原操作身份及来源检查保留；无新增输出准入包装。
  三个 profile 共 12 个解码夹具覆盖 return/throw 与不同载荷反例；生成 JS 执行
  检查 undefined/null/-0/NaN/bigint/Symbol/object 身份、get/cleanup 抛出覆盖及
  调用顺序。异常路径返回原捕获值而正常路径返回 cleanup 结果时不误折叠。
  关联目标构建成功，定向回归 15/15；日志 `/tmp/abcd-finalizer-override-final-build.log`
  与 `/tmp/abcd-finalizer-override-final-tests.log`。未运行全套测试、全量语料或 RSS。
  仅含终结动作的 cleanup 后续进展见上条；嵌套 finally、跨区域 break/continue
  及编译器 hole/guard 仍未闭合，不能据此宣布 C05 或完整反编译目标完成。
- C05 已贯通 finalizer 保护体中的嵌套 try/catch：沿既有异常区域 source_order
  内到外汇总各自 body/handler 出口，父区域消费子区域汇总，不反复遍历完整
  后代子树，也不改变 encoded TryId 的异常优先级。当前只接纳内部正常汇合或
  抛出，内部真实 return/跨区域 abrupt transfer 仍阻止不合法的外层合并。
  三个 profiles 的嵌套 catch 正例均输出完整 finalizer，生成 JS 核对捕获值
  undefined/object/Symbol 的身份、handle/after 异常传播及 cleanup 覆盖；
  内部 handler 直接 return 的反例保留展开树，执行确认跳过 after 和 cleanup。
  六个新增 opcode fixture，相关 **15/15** 定向测试通过，日志
  `/tmp/abcd-finalizer-catch-final-build.log`、`/tmp/abcd-finalizer-catch-final-tests.log`；
  原始 RED 见 `/tmp/abcd-finalizer-catch-red.log`。这是嵌套 try/catch 的组合，
  **不是嵌套 finally/覆盖 completion 的完成证明**。全量语料、RSS、基准仍暂停。
- C05 已接入 protected body 内的循环及循环携带返回值：直接消费共同
  LoopControl/LoopTransfer，允许目标仍在本保护域内的跳转，不新增循环发现算法；
  指向域外循环的 transfer 仍不作为普通 completion 合并。三个 profiles 各有
  普通循环和 `x = step(x)` phi 携带值的真实 opcode fixture，均输出完整 finalizer。
  phi 返回值曾因 cleanup 后的 SSA 快照阻止树变换；现在只允许无副作用的私有
  单定义快照移到 pending return 前，拒绝读取 cleanup 结果、可变 join/staging
  写入及调用/getter。对应序列投影仍核对原操作、边复制和 PC，未新增发布门禁。
  执行对照覆盖零次/一次/多次迭代、step 参数与最后成功值、get/next/step/cleanup
  抛错和覆盖、cleanup 恰好一次；把快照改成调用的结构合法反例被 TreeFlow 拒绝。
  定向构建成功，相关 **15/15** 测试通过，日志
  `/tmp/abcd-finalizer-loop-final-build.log`、`/tmp/abcd-finalizer-loop-final-tests.log`。
  原 RED 为 `completion candidates 0`，carried 用例还曾是 finalizer 数量 0，
  调试证据见 `/tmp/abcd-finalizer-loop-debug.log`。**不等于跨保护域 break/continue、
  嵌套 finally 或通用 C05 完成**；未恢复全量语料、RSS、基准。
- C05 已进一步贯通 protected body 中的单层/嵌套 if 汇合：completion 遍历
  现在消费共同 RegionPlan 的 enclosing continuation，识别分支末尾的隐式
  join，不再误判为无后继。没有增加图算法或按 PC 邻接猜边。
  三个 profiles 的实际 opcode fixture 均生成完整 finalizer 源码；执行对照
  核对 get、可选分支调用、共同调用、cleanup 的顺序，以及四处抛错时的覆盖
  和 pending 值身份。另有分支真实 return 绕过 cleanup 的反例，保留展开树，
  执行确认该路径不调用 cleanup。共 9 个分支 fixture，相关 composition、
  source、outer-exit、completion 行为测试 **4/4 通过**。
  RED 为 `completion candidates 0`，日志 `/tmp/abcd-finalizer-branch-red.log`；
  构建和最终测试见 `/tmp/abcd-finalizer-branch-build.log`、
  `/tmp/abcd-finalizer-branch-tests.log`。这不是嵌套 finally/覆盖 completion 的
  完成证明，hole/guard、循环 transfer 和动态环境 cleanup 缺口仍保持开放。
- C05 首条真实 finalizer 源码通路已接入：共同 TreeBuilder 消费 CompletionPlan，
  将已固定的 return/rethrow 放入外层 try，在真正的 `TryStmt.finalizer` 中执行
  原 cleanup。每次进入块时初始化私有布尔状态，catch 标记异常路径；当前保留
  正常/异常两份 cleanup 的 SSA 节点和 PC，不提前去重，也不把 cleanup 放回
  原 catch 的保护范围。Printer 未增加控制推断或专项 writer，直接打印普通 AST。
  TreeFlow 以临时语句序列投影核对原路径，原操作、边复制、表达式和来源继续
  使用共同检查；侧表仅关联 TryId 与 enclosure，不是输出许可或发布 token。
  不满足 payload 可用性、直线 cleanup、语句放置等条件时保留展开源码。
  `--finally-return` 已由 finalizer 数量 0 的 RED 转为三个 profile 的完整源码；
  两次依赖调用的 cleanup 链也输出 finalizer。生成 JS 对照覆盖 pending
  undefined/null/-0/NaN/BigInt/Symbol/object 身份、正常/抛出、第一/第二次
  cleanup 抛错覆盖、调用顺序和 receiver；私有状态名避让输入名字表的已有拼写。
  错误状态初始化/写入、cleanup 分支对调、缺失/重复/越界关联均被拒绝。
  定向构建成功，相关 **15/15** CTest 通过（含原异常循环、外层跳转、闭包及部分
  文件输出），日志 `/tmp/abcd-finalizer-final-build.log`、
  `/tmp/abcd-finalizer-final-tests.log`。未运行全量测试、语料、RSS 或基准。
  **C05 未完成**：cleanup 去重、hole/guard 编译器脚手架、break/continue、
  completion 覆盖/嵌套与动态环境 cleanup 仍需继续；200/201/402 仍仅有前一轮
  completion 分析级证据，不能据此声称长链完整源码通过。
- C05 首项 completion 分析已有生产实现 `completion_plan.h/.cc`：类型区分
  Normal（empty 与 JS 值分开）、Return、Throw、Break、Continue；当前实际识别
  return/rethrow 两条直线 cleanup 路径。比较 typed operations、参数/receiver
  角色、SSA 值及内部结果对应、副作用和异常 handler，确认 pending return 在
  cleanup 前可用、异常出口保持原 catch payload。复制身份解析有缓存，不能因
  catch snapshot 在表达式层未被别名消除而漏掉它的值身份；原图/复制绑定未改写。
  该提交当时仍是供树消费的分析结果，尚未接入 finalizer AST；后续接入见上条。
  三个 profiles 的单调用/依赖结果的 cleanup 链识别通过；不同 callee、cleanup
  才产生返回值、异常路径多一次调用、错误重抛 payload 均不合并，展开源码行为
  同时对照通过。已有循环/异常组合也调用该分析，未支持形态保留原控制方案。
  构建成功，composition / source、exception_outer、completion_baseline **4/4 通过**，
  日志 `/tmp/abcd-completion-plan-build.log`、`/tmp/abcd-completion-plan-final-tests.log`。
  200/201/402 cleanup 项仅验证分析阶段；尝试 200 项完整树构建在末尾语义检查
  触发原有默认 work 限制，未调高生产预算、未计为源码通过，定位日志
  `/tmp/abcd-completion-scale-debug.log`。最终恢复 RED 仍是 finalizer 数量 0，见
  `/tmp/abcd-completion-plan-recovery-red.log`。下一项是将 completion 对应关系接到
  共同树构建/TreeFlow/origin，而非删除检查；嵌套、覆盖、break/continue、环境
  协议 cleanup、上游 hole/guard 编译路径均未由这项分析完成。

- C05 已固定第一项真实解码 RED 与行为基线：`control_composition_test --finally-return`
  要求 `try { return get(); } finally { cleanup(); }` 的 finalizer AST，目前明确失败于
  `missing recovered finalizer: 0`，**finally 恢复尚未实现**。相同字节码的
  `--finally-expanded` 可完整输出等价的展开 try/catch + cleanup/return；新注册的
  `completion_baseline_source_test` 仅验证展开基线，不计为 finally 支持。
  三个 profiles 的返回/抛出 payload（含 undefined、NaN、-0、Symbol、对象），
  cleanup 修改外部绑定、cleanup 抛错覆盖 pending completion、清理恰好一次均已
  执行验证。fixture 使用真实 opcode 和异常表，属于语义 lowering fixture；没有
  声称逐字节复现上游包含 hole/guard 的完整 finally 编译产物。
  对照 pinned `ets_frontend/es2panda/ir/statements/tryStatement.cpp` 的
  `CompileFinally` 与 `compiler/core/dynamicContext.cpp` 的 `EmitFinalizer`；后续
  需从 CFG/SSA/effects 证明 cleanup 合并及 pending completion，再接共同树流，
  不能在 Printer 折叠文本或只删除 finalizer 检查。
  构建成功，composition / source、exception_outer、completion_baseline **4/4 通过**；
  RED 日志 `/tmp/abcd-finally-return-red.log`，基线日志
  `/tmp/abcd-completion-baseline-{build,tests}.log`。

- W4 catch 创建闭包后直接退出外层循环已有文件级行为证据：新增
  `loop_closure_file_test --outer-catch` 的 18 个函数族（3 profiles × 父级外层
  continue/break × 普通/异常/多级转发读写子函数）。Node 延后读取各次 catch
  参数，setter 修改后检查跨实例隔离；收集回调抛错必须直接向外传播，已保存
  闭包仍可调用；补充缺失捕获槽负例。仍是实际 Code entity reader + 合成 metadata
  fixture，不宣称完整应用语料验收。定向构建成功，相关 **14/14 CTest 通过**，
  `/tmp/abcd-catch-capture-final-build.log`、`/tmp/abcd-catch-capture-final-tests.log`。
  C04 本轮外层出口/标签/异常闭包组合已贯通；更广普通多 continuation、源码
  范围交叠规范化、真实应用切片，以及 C05 finally / 后续协程仍是明确缺口。

- C04 文件读取/捕获子函数组合已补充：`loop_closure_file_test --outer-transfer`
  生成 18 个函数族（3 profiles × child 外层 break/continue × 3 种父级环境），
  Code entity/异常表由实际文件 reader 读取，metadata 是合成 fixture，不是完整
  应用 ABC 解析验收。共同 family 草稿输出全部完整；子函数含双层循环和 catch
  外层跳转，并在结束时读取捕获变量。Node 在父函数返回后调用，子调用中经
  setter 修改父级 cell 后再抛错/跳转，检查结果身份与其他迭代/catch cell 隔离。
  增补该子函数缺失捕获槽的负例，仍拒绝虚构存储。定向构建成功，closure file /
  source、composition / source、exception_outer **5/5 通过**；日志
  `/tmp/abcd-outer-family-final-{build,tests}.log`。不以这些 fixture 代替真实应用
  切片或全语料验收；父级 catch 创建闭包后直接跳出外层循环的补充见上。

- C04 继续覆盖跨两层 try 的外层跳转、catch 自身循环中的外层跳转：新增
  `control_composition_test --nested-outer`，三个 profiles × 两种区域组合 ×
  break/continue，共 12 个完整生成函数。Node 比较内层失败、外层 body 失败、
  外层 catch 观察器再抛错，以及 catch-loop predicate/body 失败的路径、调用顺序
  与向外传播；已有区域/循环组合实现无需再增加专项处理。补齐测试端 JNEZ
  所需的 number 文本常量，未把测试输入缺失伪装成引擎修复。
  定向构建成功，`control_composition_test` / source、`exception_outer_source_test`
  **3/3 通过**。下一步补充带这些跳转的文件读取/函数族/捕获组合，任意普通多出口
  及 finally 仍未完成。

- C04 异常区域的外层循环跳转已完成本轮纵向贯通：此前提交 `3641ae0` 的
  `--outer-transfer` / `--outer-break` 在 `kMultipleExits` 失败，现在真实解码后经
  共同循环规划、FunctionTree、Printer 输出 catch 内的 `continue $loop0` /
  `break $loop0`。try 摘要保留全部出口用于既有自然循环发现；独立 body/handler
  入口继承包围循环，实际出口承担跳转，摘要分叉不生成虚假的源码 if。
  原始 phase 边放置 phi copies，随后出口弧仅传递跳转和来源，不重复求值/复制。
  普通与异常仍共用循环结构化算法、树构建、TreeFlow、cell 生命周期和打印器。
  新增 `exception_outer_source_test` 执行完整生成 JS：三个 dynamic profiles，
  普通/携值两种外层跳转，以及同一 try 的正常/continue/break 三出口；比较调用
  顺序、受保护调用失败、未受保护调用异常传播和跨迭代值身份。无 INCOMPLETE
  输出参加等价对照。循环父归属缺失、循环表清空的变异继续被检查拒绝。
  本轮构建上述相关 targets 成功，定向 CTest **14/14 通过**：
  `exception_plan_test`、`exception_copies_test`、`exception_tree_source_test`、
  `control_composition_test` / source、`exception_outer_source_test`、
  `loop_exit_test` / source、`tree_structure_test`、`function_tree_test`、
  `control_plan_test`、`partial_file_test`、`loop_closure_file_test` / source。
  日志 `/tmp/abcd-exception-final-build.log`、`/tmp/abcd-exception-final-tests.log`。
  **边界**：仍只接受唯一普通 continuation，外层跳转之外的任意多出口、finally、
  协程尚未由这项交付完成；文件级新组合仍需继续，多层 try/内层循环补充见上。
  全量测试、语料、RSS/基准未运行，不宣称 C04 或整体目标完成。

- C04 普通嵌套循环的外层 `break/continue` 已贯通真实 opcode → 共同控制规划 →
  FunctionTree → Printer。先分别复现 `kLoopExits`：内层普通出口与祖先 header/
  外层退出点混合导致拒绝。现在 header 边保留 continue 目标；其它离开边按真正
  跨出的最外层 loop 归属，保留内层正常出口。祖先索引采用 binary lifting，出口
  分类 O(E log L)，不逐边线性爬父链；控制检查用 Euler 区间检查祖先关系。
  `AssignTreeLabels` 在树构建阶段迭代遍历，生成函数内 `LabelId → StmtId` 表，
  仅跨层跳转使用标签；Printer 只读已分配标签。错误目标、缺失/错配标签仍拒绝，
  不能把 outer continue 静默打印成 inner continue。普通循环携带值的入口、内回边、
  外回边和外层退出沿原边安排 phi copies，未改用临时状态机或专项 writer。
  `loop_exit_test` 覆盖 3 profiles × break/continue × 有/无携带值，共 12 个真实
  解码函数；Node 对实际完整输出检查不跳转、总跳转、第二轮跳转、内层更新后跳转
  的调用轨迹、旧值输入和返回值身份。tree_structure/control_plan 的 200/201/4096
  深层检查及错误目标反例通过。重建 loop_exit、tree_structure、control_plan、
  function_tree、control_composition、loop_closure_file、partial_file 七个目标后，
  对应测试加 loop_exit/control_composition/loop_closure 的 source 测试 **10/10 通过**。
  这是普通嵌套循环证据，不是 try/catch 多出口支持：异常区域仍有单出口限制，
  C04 的 try/catch 跨层跳转、更一般多出口和 C05–C09 继续推进。全量/RSS 仍暂停。

- C04 纯转发写入已增加纵向行为证据：在 `e77e2f2` 的共同通路上，getter 与
  setter 两条独立中间链均不直接读写 cell；孙级 writer 只写祖先槽，另一孙级
  reader 读取同一迭代的槽。另测 writer 先写再 throw、中间 catch 返回后写入仍可见。
  无需新增生产特例。3 profiles × 3 父环境的矩阵从 54 扩展到 **72 组**；重建
  `loop_closure_file_test`，精确 CTest `^loop_closure_(file|source)_test$` **2/2 通过**。
  新增孙级 writer 写 Hole 的拒绝反例。此项补齐下面所列“纯转发写入中间链”缺口，
  不代表更深组合、C04 外层退出/多出口或完整计划完成。

- C04 纯环境转发已贯通真实文件函数族 → 共同 FunctionTree/printer：完整发现后，
  `PlanFamilyCells` 按 occurrence 逆序传播后代入口槽需求，按真实创建点环境重定位；
  每个成员一次合并 cell 存储，再正序重新绑定初始化/捕获。默认参数残余投影后重做
  同一规划；求值、树和函数族写入检查使用相同需求。中间函数可以没有任何词法读写，
  不插入假指令，不把 cell 值变成快照，不修改 immutable phased 输入。
  `--forwarding` 从原 `kOuterCapture` 失败变为完整源码；根部无来源的 incoming cell
  仍拒绝，原始捕获前缀不可由转发表删除或改写。初次回归发现根部拒绝边界变化，
  修复后重新构建并通过下述全部定向测试。
  文件解码/完整输出矩阵为 54 组（3 profiles × 3 父环境 × 6 子函数形态），新增
  普通和 phased 纯转发、中间额外 push 后 depth 1 捕获；Node 执行引擎实际完整输出，
  检查祖父循环/捕获环境结束后的多次读取、兄弟 setter 更新、迭代隔离、异常离开。
  `cmake --build build --target loop_closure_file_test function_input_test evaluation_plan_test
  phased_cell_test cell_plan_test cell_tree_test capture_tree_test partial_file_test
  creation_tree_context_test -j 4` 成功；对应目标加 `loop_closure_source_test` 的精确
  CTest 选择 **10/10 通过**。保留 missing-cell、未初始化、Hole 写入、缺需求及根部
  未绑定反例；cell 规划既有 0/1/2/200/201 槽检查通过。此证据是合成 metadata 的
  真实 Code 解码和标准 JS 行为，不是整份 ABC parser/真实应用/全量/RSS 验收。
  更深组合及 C04 外层退出/多出口仍需推进；未恢复 DFX。

- C04 嵌套异常函数族：修复 phased 文件输入没有向 `ProveExceptionLocals` 传递
  已有 Language 闭包写入条件，导致创建孙函数后的本地 cell 读取一律拒绝的问题。
  文件族仍逐成员检查所有写入，独立 `PhasedBody` 默认 Unknown；不把共享 cell
  变成快照。新增三层真实 Code fixture：异常中间函数读取外层值、初始化自身 cell、
  创建并调用孙函数、随后读取本地 cell。先复现值分析拒绝，再通过共同 AST/printer
  生成完整源码。三 profile、三父函数形态和三子函数形态共 27 个输出通过 Node
  行为测试；孙函数写入 Hole 的负例仍返回 `SemanticProofError::kInternalValue`。
  重建后定向 7/7 通过：class_tree_context、loop_closure_file/source、
  phased_capture_plan、phased_cell、phased_initialization、creation_tree_context。
  纯环境转发（中间函数没有直接 cell 访问）尚未打通，不能据此宣布任意多层捕获支持。

- C04 异常闭包子函数已接入共同文件族：`CaptureInput`/`CaptureChild` 借用普通或
  phased 的真实输入，不伪造 normal SSA；保留异常读取的条件前提及原始读写 occurrence，
  经共同 cell rebasing、捕获别名、异常 TreeFlow 和 family writes 检查进入同一 Printer。
  移除文件族对子函数 try_count 的一律拒绝，正常父函数和异常父函数均可创建异常子函数。
  `loop_closure_file_test` 的三种 profile、三种父函数形态、两种子函数形态共 18 个
  完整生成函数通过 Node 行为对照：异常 getter 返回捕获值，异常 setter 在 catch 内
  更新共享 cell，退出后读写仍保持同轮共享/跨轮隔离。缺失读取槽和 catch-only 写入槽
  均保持 `CaptureCellError::kInput` 拒绝。反例曾误把槽位编码为捕获深度，现按 ISA
  双 IMM4 布局修正，未放宽生产检查。重建后相关 11/11 定向测试通过。
  本项仍是 file-backed Code + 合成元数据证据，不是完整应用/record 验收；不可达
  handler、更多层级捕获与外层多出口仍待推进，C04 和总目标未完成。全量/RSS/DFX 未恢复。

- C03/C04 逃逸闭包纵向证据：新增 `loop_closure_file_test` 和
  `loop_closure_source_test`，真实文件 Code/DEFINEFUNC + 合成元数据，经
  `InspectFamilyValues` → 共同函数族 AST → printer 生成完整源码。三种 profile
  同时覆盖每轮分配和仅 catch 分配的 getter/setter，返回后逆序重复读取、同轮
  写入共享、跨轮隔离，以及 collector throw 后保存的闭包仍可读写。
  0/1/2/5 轮及正常/全异常/交替异常通过；新测试直接通过现有实现，本轮没有
  为 fixture 新增生产特例，也不将其声称为全文件元数据解析或真实应用验收。
  重建相关目标后定向 6/6 通过：loop_closure_file/source、phased_capture_plan、
  control_composition/source、partial_file；全量/RSS/基准继续暂停。下一功能缺口是含
  try/catch 的闭包子函数：文件族目前仍显式拒绝非根 phased member；C04 未完成。

- C03/C04 cell 组合切片：`control_composition_test` 新增真实指令的每轮 newlexenv、
  try/catch 写入合流读取/poplexenv，以及每次 catch 独立分配/初始化/读取/poplexenv。
  复用生产 `ProveExceptionLocals` 的 load facts（不手工把 lexical load 结果标为
  Language）；共同树断言 cell 声明归属 loop block/catch scope，并以提升到
  body scope 的反例确认 TreeFlow 拒绝。三种历史 profile 的完整生成 JS 验证
  0/1/2/5 轮、正常/异常/交替路径、值身份和顺序，以及观察回调 throw 向外传播。
  另修复循环闭包绑定前置：`ProveExceptionLocals` 不再因为存在回边就清空全部
  capture initialization intervals；完整相位图的 allocation→completed store→
  creation 支配事实仍有效。它仅证明本次初始化，动态实例仍由 cell/AST 作用域
  消费点处理，未改为函数级 cell。phased_capture_plan_test 增加每轮 pop/reallocate
  的正常/handler 创建指令，先复现已初始化捕获被拒绝，再通过；本轮 store 前
  抛异常的未初始化 handler 捕获、Hole store、未知子函数写入等反例保持拒绝。
  最新重建后 **8/8 通过**：control_composition_test、control_composition_source_test、
  phased_capture_plan_test、phased_cell_test、cell_tree_test、phased_initialization_test、
  phased_environment_test、exception_plan_test。尚未完成逃逸闭包的完整函数族源码
  行为验收（循环结束后读取/写入各动态实例），不能以这些 cell/绑定用例替代；
  下一步接 file-backed family 的循环/catch 捕获。全量测试/语料/RSS/基准仍暂停。

- C03 循环携带值/异常旧值纵向切片：新增 `control_composition_test` 与
  `control_composition_source_test`，三个历史动态 profile 均用实际 opcode 解码、
  异常 SSA、共同 FunctionTree/Printer 生成完整源码。覆盖 try 内循环中条件调用、
  更新调用、成功写入后的调用三个 throw 点，以及循环内 try 更新失败后
  `observe(oldValue, exception)` 再进入下一轮。标准 JS 执行覆盖 0/1/2/5 轮、
  各种 primitive/object 初始值、全成功/全失败/交替失败、普通值调用的 undefined
  receiver、参数/异常/返回对象身份、调用顺序及 handler 自身 throw 向外传播。
  新失败案例定位并修复 `expression_copies.cc`：自赋值 phi copy 消除后 schedule
  可以为空；仍保留空边放置区间，但不再生成没有 AST 消费者的 EdgeOrigin。
  原行为导致完整构建报 `FunctionTreeError::kSemantics`，本次没有放宽对应检查。
  显式断言 fixture 包含空 schedule 和另一 fixture 的 throw-staging slots，
  防止退化成只测试普通控制结构。临时定位输出已移除。
  最新构建后定向 **8/8 通过**：control_composition_test、control_composition_source_test、
  exception_plan_test、exception_copies_test、exception_tree_source_test、phi_copies_test、
  phi_call_test、partial_build_test。仍未替代完整 W1/W2 的数字循环/数组日志与
  多出口 continue、per-iteration/per-catch cell、真实应用切片验收；后续继续这些
  功能和共同 RegionPlan 收敛。不新增 DFX，未恢复全量/RSS/基准。

- C03 首个纵向组合切片：真实动态 opcode（12.0.2/12.0.6/13.0.1）经异常 SSA、
  ValueProgram、共同循环区域、共同 TreeBuilder/Printer，现已完整输出并执行
  **try 内循环、catch 内循环、循环包含 try/catch** 三类基础用例。异常投影遇到
  cycle 后复用既有自然循环构建；既有正常区域检查也迁移到相同视图/多入口，
  没有另建异常循环算法或新的输出准入。try/handler 起点重定向到 loop wrapper，
  TreeFlow 正确处理异常投影后追加的 loop 节点；projection arc 经原边映射才可
  成为 AST EdgeOrigin，try wrapper 的合成后继保留 TryOrigin。
  handler 回到本 try 的 protected entry 视为退出当前 catch 再重入 wrapper，
  protected body 内回到自身入口仍是 try 内循环，不移动异常保护范围。
  `exception_tree_source_test` 执行引擎生成的完整 JS：getter 第 1/2/5 次正常退出、
  中途 throw 离开 try-loop、handler 内 throw 向外传播、catch 后下一轮重试的
  调用次数与返回对象身份均符合预期，且拒绝 INCOMPLETE 测试输入。
  RED 曾复现原 `kCycle` 和 handler 重入 `kCrossEntry`；修复后通过。
  来源测试加入 Commit 相位/原 JMP PC 检查，临时恢复错误的投影边编号直用行为
  后测试失败，撤销该临时变异后通过。另修正仅异常路径未分配 loop TreeFlow
  控制记录所致的崩溃（崩溃栈确认 FlowCheck::Loop）；没有跳过语义检查。
  最新重建与定向 CTest **11/11 通过**：exception_plan_test、exception_copies_test、
  exception_tree_source_test、control_view_test、control_plan_test、loop_forest_test、
  try_tree_test、try_tree_source_test、partial_build_test、cell_tree_test、phased_cell_test。
  **这不是 C00b/C01/C02/C03 整体完成**：完整 W1/W2 的循环携带旧值/日志/call、
  per-iteration/per-catch cell 组合、真实应用切片、计划字段 ID 完整分离及共同
  RegionPlan 收敛仍待推进；已有 cell 回归不等于新增循环捕获组合的行为验收。
  未恢复全量测试、全语料、RSS 或基准，未新增 DFX。

- C01 循环区域构建子项：原有 `LoopControlBuilder` 也消费共同视图和多入口，
  循环收缩邻接改用 `ControlVertex/ControlLink`，移除用于容纳合成节点的临时
  `ValueProgram`。原普通入口保留适配；投影两独立循环岛能生成各自 loop wrapper、
  body 与 transfer，真实输入弧保持前缀映射，支配专用虚拟根不进入区域计划。
  新多入口区域断言先因缺少接口失败；实现后重建并运行 control_view_test、
  control_plan_test、loop_forest_test、exception_plan_test **4/4 通过**。
  仍未解除异常路径的循环拒绝：异常映射、phi/cell/TreeFlow 和树消费需一起迁移，
  不能仅因循环内核已能构建就声称 C03 支持。下一步以真实解码组合 fixture 驱动这些消费点。

- C01 共同视图接入：无环区域发现及已有骨架检查改为消费 `ControlGraphView`，
  异常投影保存独立 `ControlVertex/ControlLink`，不再借用语义 `WorkBlock/WorkEdge`
  装载投影邻接。`ControlPlan.view` 区分普通/投影视图；相同数字 ID/revision 的
  投影计划不能交给正常 CFG 消费入口。未新增源码输出准入或 DFX 框架。
  新 `ProjectedSkeleton` 先因尚无图视图入口失败，再实现并通过；重建后
  `ctest --test-dir build -R '^(control_view_test|control_plan_test|exception_plan_test)$' --output-on-failure`
  **3/3 通过**。既有真实 opcode 异常树构建测试继续通过；这不是循环/try 组合
  支持完成。下一步迁移现有循环区域构建到同一视图，接多入口及异常树消费；
  `ControlNode.block` 等旧计划字段的完整 ID 分离仍待共同 RegionPlan 迁移。

- C01 最小循环视图子项：新增借用型 `ControlGraphView`，正常 ValueProgram 与
  区域投影经同一循环发现内核；ControlNodeId/ControlArcId 不可隐式转为工作图 ID。
  LoopForest 显式携带 normal/projection 视图类别及 IR/CFG revision。多个独立
  主体/handler 入口经支配分析专用虚拟根连接，虚拟节点/弧不进入语义图或输出
  loop membership；保留现有 Lengauer–Tarjan 与循环发现算法，不另写异常循环器。
  control_view_test 覆盖两个独立循环岛、缺失/重复/非法入口与正常适配；与
  loop_forest_test、control_plan_test、exception_plan_test 定向 4/4 通过。
  这是 C03 所需前置，不代表 loop/try 已贯通：异常投影/骨架仍需迁移，C00b
  组合 fixture、共同区域消费和实际生成 JS 验收仍未完成；C00a 剩余缺口不撤销。

- C00a 第四子项：file-backed 函数族可保留没有捕获 cell 的不完整叶子及完整兄弟。
  子函数仍必须匹配真实创建表达式、文件/方法/IR 身份和空捕获集合；不伪造子函数
  TreeFlow，现有非空捕获验证不放宽。即使有部分成员，仍检查跨成员名字/作用域
  enclosure；不完整函数族不产生完整 writes 事实。Printer 传播到祖先状态，
  兄弟保持完整。partial_file_test 的三成员实际 Code/创建指令 fixture 先观察到
  member 1 整体拒绝，再验证父/叶 incomplete、兄弟完整且 `return 7` 保留。
  partial_file_test、partial_build_test、partial_tree_test、function_input_test、
  creation_tree_context_test、family_enclosure_test、真实 family_source_cli_test
  定向 7/7 通过。非空捕获的部分函数、控制/导入失败保留、record 级输出仍未完成。

- C00a 第三子项：`--family-source-draft` 通过原 `BuildFileFamilyTrees` 请求
  部分构建，当前限无默认参数投影、无子函数的正常直线根函数。保留真实上下文、
  名字/参数和已恢复语句；部分结果不制造完整 TreeFlow、ABI/source census 或
  family writes 证明。输出新增 draft_incomplete 和逐成员 incomplete；已有
  --conditional-family 仍不输出源码，严格准备路径仍默认完整模式。
  文件级 partial_file_test 覆盖实际 Code 实体/创建指令读取和生产草稿入口，
  metadata index 为 fixture（不是完整文件 metadata parser 验收）。CLI 已在
  macOS 构建；同步修复该工具局部 constexpr 的 AppleClang 编译问题。
  partial_file_test、creation_tree_context_test、partial_build_test、partial_tree_test、
  真实 pasteboard 的 family_source_cli_test 共 5/5 通过；后者验证既有四成员
  完整函数体草稿未退化，不是新增部分恢复覆盖证明。全量/全语料/RSS/基准未运行。
  C00a 仍缺带不完整后代的实际 family 保留、控制/导入失败保留和 record 状态贯通。

- C00a 第二子项：共同 expression forest / FunctionTree builder 新增显式
  `RecoveryMode::kAllowPartial`，保留单个正常直线区域中已闭合的表达式根；
  遇未支持 lowering 回退到最后闭合边界，将整个未完成内联表达式及后缀标为缺口。
  不修改 ValueProgram、不截断 CFG、不造终结器；常量/所有权错误不转成 unsupported。
  默认完整接口不变；分支、异常、phi、cell、捕获/子函数仍未开放这一前缀策略。
  新增 partial_build_test 使用真实动态 12.0.6 opcode 解码，覆盖 debugger/调用保留、
  inc 缺口、嵌套内联回退、原始 PC、完整返回、分支拒绝与损坏常量表拒绝。
  定向 Debug 构建成功，partial_build_test、partial_tree_test、function_tree_test、
  tree_structure_test、try_tree_test、function_input_test、call_plan_test、phi_call_test
  共 8/8 通过。`partial_build_test --emit-complete` 的引擎输出经 Node 实际执行返回 7；
  部分源码没有作为等价行为测试输入。尚无文件级 CLI/record 部分恢复验收，
  尚未覆盖导入/控制/求值分析阶段的失败保留，C00a 继续，不宣称全 opcode 支持。

- C00a 第一子项：新增直接 `PrintFunctionTree(const FunctionTree&, ...)`，
  复用现有迭代 printer，不依赖 VerifiedFunction。`RecoveryIssue`/`UnrecoveredStmt`
  保留类型化原因及可用 PC，只输出注释缺口，不生成 return/throw/undefined 占位。
  共同 family printer 将不完整状态传到所有语法祖先，不污染兄弟函数；嵌套函数
  注释不引入 return/throw 后的自动分号。节点/范围错误仍在写入前失败，旧语义
  验证入口拒绝将部分树当作完整函数。生成位置继续按实际输出记录。
  新增 partial_tree_test；观察到祖先传播、return 换行两个回归先失败后修复。
  Debug 构建成功，定向 CTest：partial_tree_test、function_tree_test、
  tree_structure_test、try_tree_test、function_input_test 共 5/5 通过。
  本子项只证明树打印与既有构建回归；尚未让未支持的真实 ABC 保留部分树，
  尚未接 CLI/record 状态，也未验证部分输出的执行等价（不作该声明）。
  下一步继续共同构建器的可靠片段保留和真实解码 fixture，不转向 DFX。

- macOS 定向构建前置适配：`AbcFile` 按平台读取 `stat` 的纳秒级 mtime/ctime，
  不放宽原输入变更检查；消除 AppleClang 对局部 constexpr 无捕获引用的编译错误。
  本地使用锁定 Ruby 3.2.3、JSON release tag 和已锁定 ISA 完成 Debug/Ninja 配置；
  依赖构建产物仅在忽略的 `build/` 下。`abc_file_test`、`acyclic_body_test` 定向通过。
  没有运行全量测试、全语料、RSS 或基准；这是平台构建修复，不代表恢复能力增加。

- mac 本地语料根调整为 `/Users/sun/abcd/samples`，忽略根目录 `/samples/`；
  同步当前命令示例、语料配置和开发约定。历史验证报告与 abc-index.json 中的
  分类证据保留原样，不因路径迁移重跑扫描、测试或 RSS；样本内容不修改。

- 新增三个官方参考源码 submodule，位于 `references/openharmony/arkcompiler/`：
  runtime_core、ets_runtime、ets_frontend，固定 OpenHarmony-v7.0-Release tag，
  提交分别为 `3e147bb7`、`f04900cf`、`c754cf63`，与既有 release lock 一致。
  核验 tag object、tag 对应提交、HEAD、gitlink、remote 和子仓库工作树；README
  补充初始化命令与 ISA 路径配置。仅接入参考源码，未执行构建、功能测试或 RSS。

- 按本轮讨论更新设计与 C00–C09：C00a 先实现直接树打印和可靠的部分结果保留，
  C00b 固定控制组合基线；C01/C02 缩小到 C03 循环/catch 所需结构，通用独立区域
  验证和预算配套后移。随后保持外层跳转/异常闭包、finally、await、generator、
  async generator/yield* 顺序。同步更新设计入口、AST/验收合同、旧总计划和 ISA
  优先级入口，历史审计结果不改写。本轮只做文档差异和链接检查，不运行构建、
  CTest、生成 JS、全语料、RSS 或基准；没有新增实现或功能通过声明。
  下一步为 C00a，不先做 VerifiedFunction 清理或 DFX 框架建设。

- 按用户要求完成一次性[控制组合审计](control-composition-audit.md)，基线为
  `1c7e44d` / Rules 58。修正“仅缺区域树”的旧表述：控制投影、树构建派发、
  cell 生命周期、独立验证及 AST 跳转/协程契约须共同迁移。
  [演进计划](../../superpowers/plans/2026-09-28-unified-control-regions.md) 明确
  C00–C09 的类型/算法/文件/组合用例/验收门槛；先交付循环与 catch 双向嵌套，
  再扩展跨区域出口、finally、await、generator 和委托协议。不改生产代码，
  不新增功能支持声明。重新构建并运行 control_plan、loop_forest、exception_plan
  三项定向 CTest，3/3 通过，其中含既有预期拒绝；未运行全语料、RSS 或基准。
  下一步为计划 C00/C01，不通过直接解除 `kCycle` 或复制异常循环实现推进。

- Rules 58 接通函数族的原生模块发布：file-backed 成员保留实际 record，
  准备阶段验证同输入 `ModuleEnclosure`，共享 destination 检查逐成员核验模块
  身份/槽位/名称，再保留既有跨成员词法遮蔽检查。重新嵌入时重验，printer
  保留每成员的模块绑定来源；仅有同名 import 列表不构成发布许可。
  模块 fixture 覆盖缺少上下文、名字冒充、目标遮蔽、错误 owner/record、缺失
  record、旧契约、零预算与位置迁移。`recover`、`queryDegree` 的五个成员
  已通过共享不可变发布及原生 ESM 行为测试，235 条原始指令映射通过检查。
  15 项定向 CTest 和六次真实样本脚本运行通过（新旧发布路径均覆盖），包括
  `updateWindowInfo` 调用期间 live import 重绑定；沿用既有显式 work 预算。
  三个 CLI 已重建。未运行全语料、RSS 或基准；不宣布整个 record、异常子函数
  或循环/finally/协程完成。下一步按已批准顺序推进循环与异常区域组合。

- Rules 57 将同快照 lookup 传入 `PreparedFamily` 的共享构建器，让无模块依赖
  的异常 root 沿同一不可变准备、目标检查、printer、原始 PC 核验和 receipt
  链路发布。未取消 strictness/callable/module/destination/closure-write 门槛，
  也未允许异常子函数或跳过原始创建点。文件解码 fixture 的共享/单函数源码
  对照、位置迁移、错误 placement/旧契约/零预算及模块无上下文拒绝均通过。
  14 项定向 CTest（含两个真实异常函数族 CLI 行为检查）、三个真实异常样本
  脚本通过。模块函数族后续接入见上条；异常子函数、循环/finally/协程组合仍未完成；
  未运行全语料、RSS 或基准，不将方法 receipt 当成整个 record 完成凭据。

- 常规 `abc_semantic --family-source-draft` 现先解析实际 root occurrence，再
  调用共享函数族 reader/tree factory；向异常输入传递真实 owner 和同快照 lookup。
  `recover`、`queryDegree` 可经 CLI 生成条件源码，新增 CTest 直接执行返回源码，
  检查 235 条原始指令映射、五个成员归属及非发布状态；`--conditional-family`
  保持不输出源码。8 项相关 CLI/真实切片测试通过。不是新的发布准入，Rules 56
  不变；后续异常 PreparedFamily 和原生模块约束接入见上条。

- 共享 `FileFamily` DFS / `BuildFileFamilyTrees` 已接入类型化 normal/phase
  所有权：异常父函数与普通闭包后代使用同一函数族构建、AST、ABI、来源索引、
  printer、enclosure 与传递写入验证，不再依赖工具手工拼装才能贯通。创建点
  与可移动 child context 分离，默认参数投影后仍从原始边重验捕获；异常 root
  校验实际 occurrence，含异常表的子函数目前明确拒绝 `kUnsupportedFlow`。
  `recover`、`queryDegree` 直接执行共享工厂生成的条件源码，覆盖分支、副作用、
  catch、延后回调和可变闭包；新旧路径文本一致，145 + 90 条原始指令另经共享
  printer 的 AST 事件范围检查。缺失 root owner、lookup、incoming edge 或
  phase 边标记均类型化拒绝。44 项定向 CTest、三个真实样本脚本通过，
  `abc_semantic`、`abc_structure`、`exception_candidates` 构建成功；未运行
  全语料、RSS 或基准，未据此声明进程 1GB 上限已验证。
  这不是正式模块发布：无模块异常 `PreparedFamily` 后续接入见上条，模块
  初始化证明尚未完成；诊断 CLI 的 root context 已接入。
  Rules 56 不变；循环/异常双向嵌套、finally、协程组合验收仍未完成。

- 新增生产层 `FilePhasedFunction::Read`：从实际文件读取 body/call-type ABI，
  对照已解析 occurrence，统一持有只读 body、原始/工作异常分析和每方法一次
  的条件捕获作用域。模块绑定必须来自同一 file/header/index 快照及实际 record；
  不保留外部 context/module 描述符指针。真实函数族工具已改用该 owner，
  不再自行拼装父 body/词法证明；原 235-PC 来源和条件源码执行检查继续通过。
  新增 200/201/402 容器扩容/移动后完整原始控制视图校验、精确/不足 recorded
  work、零 bytes、错误 ABI/方法/header、缺失 code、异常表元数据错配和模块
  快照混用反例。42 项定向 CTest、3 份真实样本脚本通过，三个 CLI 构建成功。
  此增量本身不是完整函数族发布；后续 DFS/tree factory 接入见上条，
  仍须关闭 immutable publication/destination/module 门槛。未运行全语料、
  RSS 或基准；Rules 56、循环/finally/协程及全 ISA 目标不变。

- 普通/异常 `BuildInstructionSources` 现共用完整的原始指令来源索引，异常
  复制消除与别名仍映射到原始 AST 边界锚点，不伪造语句或从 JS 文本反推位置。
  真实 `recover`、`queryDegree` 两个混合函数族的五个成员均保留 ABI 和来源
  census，并经共享 class-family printer 输出条件草稿；145 + 90 条原始指令
  均通过 AST ID→生成事件范围/锚点检查。文件解码用例另覆盖分支/catch、成员
  调用、正常/catch 存储、live/dead copies、错误 PC/revision、遗漏/重复放置、
  phase 签名迁移拒绝及精确/不足预算。受控执行覆盖状态分支、重复 receiver
  读取、getter/调用异常、日志效果顺序、回调参数/length、同步及延后抛出、
  父函数返回后的可变 receiver 捕获及多次调用的独立绑定。
  42 项定向 CTest、3 份真实样本脚本通过，三个 CLI 构建成功；未运行全语料、
  RSS 或基准。受控宿主绑定不是原生模块初始化证明，条件草稿不是正式发布。
  混合文件函数族的持有/准备/不可变发布仍须接通；不改 Rules 56，不宣布
  循环/finally/协程组合或全部 ISA 完成。

- `VerifyFamilyWrites` 现共用类型化 normal/phase 成员视图；既有 `FileFamily`
  无复制适配到同一实现，普通默认签名写入及 primitive object 证明保持不变。
  异常父函数与普通后代依次校验原始/工作捕获、实际 AST 绑定、全部词法写入，
  最后关闭原始创建点覆盖。异常侧直接扫描原始类型化指令，避免无局部 cell 时
  `ExceptionLocals` 提前返回漏掉回调；没有依据工作 IR 删除创建点义务。
  真实 `recover→7810/8118`、`queryDegree→7978` 已通过完整传递写入 gate。
  新增同时删除两份成员列表、遗漏/重复/自身/错类型边、Unknown 写入前提、
  精确及不足 byte/work 预算反例；普通双接口结果/成本一致，三版本及
  200/201/402 存储用例亦走共用 gate。42 项定向 CTest、3 份真实样本脚本
  通过，三个 CLI 构建成功；未运行全语料、RSS 或基准。
  这仍是借用既有输入的验证，不是不可变发布凭据。混合文件函数族所有权、
  ABI/PC census、destination/module 发布和组合函数族执行验收尚未完成；
  异常 child 仍明确拒绝，Rules 56 及独立源码拒绝不变。当前仍在第 1 项
  的函数族整合，未宣布循环/异常、finally 或协程组合完成。

- 普通/异常函数现共用 `VerifyLexicalStores`：检查原始 SSA 写入值域、
  原始来源、工作 cell 和实际 AST 赋值目标；异常侧只使用原始 Before 读，
  不用 Commit 或 Catch 值冒充写入操作数。普通函数族 `VerifyFamilyWrites`
  已调用该实现，保留捕获/barrier 全覆盖和默认签名证明 gate。
  `recover`、`queryDegree` 及三个实际 child 的逐方法写入检查均通过，
  后者父函数有一个词法写入；正常/捕获存储及错误目标、cell 类型、PC、
  owner/revision、精确/不足预算有回归。文件解码用例另覆盖正常与 catch 写入。
  这只是共享的成员写入义务，不是完整异常函数族写入证书；统一文件所有权、
  全部创建屏障及传递闭合仍需整合。未新增源码发布准入，Rules 56 不变。
  本轮 42 项定向 CTest（含四成员真实函数族默认签名迁移）、3 份真实样本
  脚本通过，三个 CLI 构建成功；未运行全语料、RSS 或基准。

- Rules 56 改进通用跨相位表达式回退：保留成功的树，在失败树的原分区内
  恢复后只重排一次，每条携带效果的新边要求 catch 路由及槽来源等价。
  独立验证器仍检查最终根、效果顺序、环境 epoch、唯一使用和复制边界。
  纯结果表达式不再导致合法成员调用子树全部退回临时值；没有方法名特判。
  `8257 recover→7810/8118` 现与 `8404 queryDegree→7978` 一样通过共享
  父/子 AST、捕获、函数族布局和 lexical enclosure；这仍不是完整源码发布，
  统一文件函数族所有权、传递写入及完整发布证据未闭合。
  文件解码的 `try { return typeof obj.run(7); } catch (e) { return e; }`
  已走完整不可变准备、共享 AST 和 printer；执行测试覆盖 27 组任意 JS 值、
  getter/调用抛出、receiver/参数及效果顺序，并检查全部 10 个原始 PC 映射。
  本轮 40 项定向 CTest、3 份真实样本脚本通过，三个 CLI 构建成功。
  全语料、RSS、基准和调度器工作没有恢复；全 ISA 及后续组合目标仍未完成。

- 跨相位表达式现接纳有已验证绑定的函数创建，仍作为有序事件，保留唯一使用、
  连续效果、无复制路径、最终根 catch 快照及环境 epoch 检查。三版本覆盖
  getter→回调创建→成员调用、200/201/402/1608 长路径，以及跨 push/pop、
  handler 和伪造移动的拒绝。没有改用可被覆盖的 `.call` 或重读 getter。
  真实 `8404 queryDegree→7978` 已经共享父/子 AST、实际父 cell 声明绑定、
  `BuildFunctionFamily` 和 `VerifyFamilyEnclosure`；捕获目标篡改会被拒绝。
  这仍是组合诊断，不是源码发布：统一 phase/normal 文件函数族所有权、ABI/PC
  完整发布证据、传递写入证明和生成 JS 执行验收尚待完成。该前置增量中，
  `8257 recover` 因 Op 9→12 被带到无派发证据的纯根 Op 22 而拒绝；
  此缺口现由上述 Rules 56 通用回退处理，没有放宽 catch 校验或特判方法。
  该增量使用 Rules 55，全 ISA 目标不变。
  本轮 40 项定向 CTest、3 份真实样本脚本通过，三个 CLI 重建成功；
  未运行全语料、RSS 或基准测试。

- 异常捕获已接入共享工作 `CaptureCells` 与求值 `FunctionBinding`。
  普通/异常路径共用按单元规划、逐原始使用独立校验；原始环境通过分配来源
  映射到工作 OpId，再与工作环境的 depth/slot 反查比较，不直接比较环境 ID。
  真实 8257→7810/8118、8404→7978 获得 0/0/1 个父单元目标，两个父函数的
  共享异常求值计划及独立校验已通过。它们仍是**条件求值**，不是完整函数族
  或源码发布：当时尚缺实际 child AST、父级声明绑定、enclosure 和传递写入；
  前三项现已在上述真实组合诊断中验证，正式文件函数族及写入 gate 仍未完成。
  独立 `PhasedMethod` 保持原拒绝，Rules 55 不变。后续完成上述 AST/family
  衔接后，再按原顺序推进循环/异常、finally 和协程，不缩减全 ISA 目标。
  本轮 40 项定向 CTest、3 份真实样本脚本通过，三个 CLI 构建成功。
  新增原始/工作环境 ID 不同、200/201/402 组捕获读写、目标与 revision
  篡改、原始分配来源错配、精确/不足预算反例；未运行全语料或 RSS 验证。

- 普通/异常创建现通过类型化 `ScopeFacts` 共用 `CapturePlan`，异常侧固定
  原始 Before，并按完整异常图的完成写入支配区间查询初始化；不伪造普通 CFG。
  计划显式保留条件 `ClosureWrites::kLanguage`，不能把可变绑定当值快照；
  父级自身的内部值写入仍拒绝，完整子函数族的写入义务没有免除。
  新增 `BindPhasedFunctionInputs`，核对真实 child owner、同文件/record、
  创建 PC、目标和原始/工作创建点。真实 8257→7810/8118、8404→7978 已分别
  绑定 0/0/1 个捕获使用，父级发布仍拒绝；这不是完整闭包源码验收。
  40 项定向 CTest、3 份真实样本脚本通过，三个 CLI 重建成功。新增三版本、
  调用前后写入与 catch 创建、内部 hole 写入、条件前提/绑定篡改、只读保留、
  外层域义务、200/201/4096 次使用、精确/不足预算及真实创建点错配反例。
  工作 cell 映射与捕获求值现已接通，实际树与完整 family 写入验证仍待完成。
  Rules 55 不变；循环/异常组合、finally、协程和全 ISA 目标均未完成。

- Rules 55 将相位 cell 接入共享 `FunctionTree`：复用作用域树/Euler 区间，
  新增有类型的 try 主体/catch 容器，并通过投影 unit 找回真实工作块。
  声明仍留在分配点，越出作用域的访问拒绝；仅接受已认证的显式 throw
  空 Commit 不可达节点。AST 独立校验继续检查实际声明位置、origin 和绑定。
  `PhasedBody` 现接入原始完成写入支配证明产生的词法读取前提，并验证全部写入。
  文件级 ABC 样例已通过 preparation→共享树→printer→JS，覆盖 catch 写回、
  任意 JS 异常值身份、调用 receiver/实参、效果顺序、重入绑定隔离及全部 10 个 PC。
  三版本的越界作用域、声明 scope 篡改及预算边界已有定向验证。
  32 项定向 CTest 与 3 份真实样本脚本通过，三个 CLI 重建成功；生命周期
  新增 200/201/402 组 cell 校验，越界作用域也验证源码树入口明确拒绝。
  这不是完整 child 捕获族、循环/异常组合、finally 或协程验收；后续仍按批准
  的五步顺序推进。全量语料、RSS/基准和调度器未恢复。

- 相位 cell 初始化已接入共享异常求值计划：分配/写入在成功 Commit 更新，
  读取在 Before 查询；独立校验使用完整异常图的支配关系。调用后才执行的
  赋值不能证明 catch 读取已初始化，即使伪造 Language 域或写入 witness 也拒绝。
  原普通 API 仍不接受相位图；传入 cell 和闭包写入仍是 owner 义务。
  Rules 54 同时补齐缓冲对象方法的隐式捕获检查，未证明纯数据的 buffer 不能
  免除闭包写入证明。依据 pinned `RuntimeCreateObjectHavingMethod` 和
  `ObjectFactory::CloneObjectLiteral` 设置词法环境的行为。
  31 项定向 CTest、真实创建/cell 回归、两份既有生成 JS 行为脚本通过，三个
  CLI 重建成功。新增测试覆盖三版本、显式 throw、200/201/402 组分配、伪造
  见证、预算边界和共享求值组合，但不是新增可执行函数族验收。
  当时 `FunctionTree` 对相位 cell 保留显式拒绝；Rules 55 已通过独立生命周期
  与实际声明校验替换此限制，并接通原始词法读取前提。实际 child 捕获族仍待
  闭合，不能由局部 cell 成功推断完成。未运行全量语料、
  RSS 或基准；全 ISA、循环/finally/协程与原结构 gate 仍未完成。

- 新增 `BuildPhasedCellPlan`/`VerifyPhasedCellPlan`，与普通路径共用存储键、
  分配点索引和访问 census，但使用异常环境校验。分配失败进入 catch 后的
  传入 cell，不会与正常路径中新建环境的同 depth/slot 合并。普通初始化和
  生命周期入口仍不接受相位计划，未绕过成功 Commit 或捕获写入证明。
  `PhasedBody` 现按环境/词法/创建操作需要，持有工作环境与 cell 计划并累计
  同一预算；真实 8257/8404 诊断改读生产保留结果，分别有 0/1 个 cell。
  三版本、200/201/402 分配组、损坏证据及预算边界已覆盖，30 项定向 CTest、
  真实创建/cell 回归和两份既有生成 JS 行为脚本通过。Rules 53 不变；
  父函数族仍不可执行，成功覆盖计数不增加。下一步是成功写入初始化见证、
  相位作用域生命周期及实际 child 捕获闭合，之后继续共享树与组合验收。
  未运行全量语料、RSS、基准；全 ISA 与原结构 gate 仍未完成。

- 共享 `ValueEnvironments` 新增显式异常相位入口表与独立构建/校验入口：
  词法访问在 Before 绑定，环境入栈/出栈仅在成功 Commit 改变状态；分配失败
  到 catch 保留旧环境，显式 throw 的不可达 Commit 保持无状态。操作视图仍用
  OpId，不修改原 CFG/SSA/handler，也不借用普通流程的初始化证明。
  三个历史 profile、200/201/402 组操作、篡改状态/绑定/分配点/版本以及精确和
  不足预算已覆盖。真实 Immersive 8257/8404 的工作 IR 也通过环境分析，8404
  的一个环境分配点和一个词法绑定已纳入固定回归。父函数族仍不可执行；
  这是捕获 cell 接入的前置构件，不是完整函数族、循环/finally/协程验收。
  下一步仍须桥接实际 child 的捕获初始化/写入证明与共享 FunctionTree。
  Rules 53 不变；29 项定向 CTest、真实创建/环境诊断及两份既有生成 JS
  行为脚本通过。未运行全量语料、RSS 或基准；不声称满足整体 1G 实测验收。

- 新增 `BindExceptionCreation` 与 `BindPhasedCreations`：前者从原异常图的
  **Before 节点**读取环境，后者将不可变父体的实际创建操作对应到区域池目标、
  原 PC/指令与独立 `CreationTreeContext`。普通与异常创建共用目标/捕获种类解析，
  但绝不把按相位节点索引的环境表冒充普通指令表。父体保留身份和 method stamp；
  结果中的环境 ID 仍归原父体所有，不证明初始化、捕获写入或 child AST。
  已定位真实边：8257@95→7810、8257@118→8118、8404@52→7978；父函数仍拒绝
  求值/源码发布。下一步对接这些实际 child 的条件输入、捕获 cell 与完整函数族
  树，不能仅将目标 ID 填入 `FunctionBinding`。Rules 53 与成功覆盖计数不变。
  实际 child 的条件输入检查显示：7810/8118 无捕获且各有一个模块配对，7978
  有一个捕获单元；三者无后代创建义务。该诊断仍返回“条件性、不可执行”，
  不能替代有创建位置 owner 的 child 验证，也不能跳过模块/捕获闭合。
  验证：三个 CLI 与相关目标重建成功，25 项定向 CTest 全部通过；真实创建目标
  回归与两份既有 Immersive 方法行为脚本均通过。未运行全量语料/RSS/基准。

- 异常函数族前置阶段现拆为不可变 `PhasedBody` 与消费它的
  `PhasedMethod::Evaluate`。原 `Build` 入口仍组合两步；CFG/SSA/环境/值证明、
  模块配对和异常控制只构建一次，移动时不复制图。总预算包含保留的 body 存储和
  已消耗工作量，不因分阶段而重置。此结构改动不放宽源码语义，Rules 53 不变。
  真实 8257 的 PC 95/118、8404 的 PC 52 创建点现可在求值之前取得；求值仍因
  缺少真实子函数绑定拒绝，诊断坚持 `source_complete=false`、`executable=false`。
  这是解除父体发现与子函数绑定之间的构建顺序问题，不是函数族源码完成。
  下一步将这些原始创建点经区域池和 occurrence context 解析到真实 child，
  对接捕获证明、完整 child AST 与 family 验证；不得填空 `FunctionBinding`、
  忽略失败子函数或将异常图伪装成 normal CFG。循环/异常、finally/协程仍待推进。
  验证：三个 CLI 及相关测试目标重建，24 项定向 CTest 全部通过；真实创建点
  回归 `exception_children.mjs` 与两份既有 Immersive 行为脚本通过。未运行全量
  语料/RSS/基准；当前两份含创建点的方法仍未产生源码，不增加成功覆盖计数。

- Rules 53 将已证明的模块 load/TDZ guard 配对纳入共享相位效果子树。两条原始
  指令仍保留各自效果、异常边、输入来源与 PC，只有内部 guard 依赖不再算作
  第二个源码值使用。效果区间覆盖 load 与 guard 两个事件；单次使用、唯一正常
  路径、无中间边复制、最终 throw owner 与 catch 快照一致的约束不变。
  三 profile 新增模块读取/getter/catch 组合、跨 handler 拒绝和伪造计划拒绝，
  原生词法 TDZ、live binding、任意 JS 返回/抛值及 200/201/402 项也有定向用例。
  真实 Immersive 8566 `updateWindowInfo` 已经由不可变准备、共享树和统一 printer
  完整输出方法片段；`immersive_window.mjs` 逐正常路径可观察操作注入异常，比较效果顺序、
  字段写入、receiver、日志、返回/抛值，并验证 getter 重绑定模块后参数读取新值
  和全部 330 条指令的生成位置。不是整个 record 完成，也不读取 sidecar 源码。
  **预算边界**：默认工作量下返回 `kWorkLimit`；该验收显式使用诊断工具
  `--emit --work 4000000`。只是测试调用方提供工作预算，未放宽生产默认值或内存
  限制。后续需处理重复独立证明的成本，不能把该方法计入默认配置的成功覆盖。
  下一步继续 8257/8404 的异常函数族创建归属，以及共享循环/异常区域整合；
  finally/协程与五步整体仍未验收。
  验证：三个 CLI 和相关测试目标重建成功；24 项定向 CTest 全部通过；
  `immersive_exception.mjs`、`immersive_window.mjs` 均使用最新工具通过。
  8566 默认预算拒绝、非法 `--work` 输入拒绝也已复核；未运行全量语料/RSS/基准。

- Rules 52 将同一 `LiteralPlan` 的新对象 own-data 初始化证明扩展到异常相位。
  原正常 CFG 入口不变；相位入口验证原始 availability、复制身份与唯一正常
  fallthrough 路径，逐 use 核验对象在最后一次定义前未被观察，RHS 必须在分配
  前已可用。保留原 CFG/SSA/异常边；不把任意 own-write 当普通属性赋值。
  求值器先形成复制身份，再构建初始化计划；共享 AST/printer 使用原 `ObjectExpr`
  与 `DataProperty`，写入 PC 映射到属性范围。文件化 getter/新对象/own-write/catch
  方法已通过不可变源码出口，运行时检查任意返回/抛值、继承 setter 不被调用、
  数据属性描述符与逐指令 PC。三 profile 覆盖窄/宽 STOWNBYNAME、DEFINEFIELD/
  DEFINEPROPERTYBYNAME、复制链和 200/201/402/804 个字段、预算及伪造计划反例。
  中途观察、catch 观察、晚 RHS、初始化中分支仍拒绝。大字段 AST 用例单独显式
  提高测试工作量预算，生产默认预算没有放宽，未声称整条流水线线性。
  真实 Immersive 8566 从 literal/evaluation 拒绝推进到 call-plan：PC 331 的 callee
  与 PC 363 的调用之间有 PC 337/339 模块读取/guard，尚缺统一的效果子树组合。
  8257/8404 的拒绝已确定是创建子函数缺失 FunctionBinding 归属，而非 catch 本身。
  下一步补模块 guarded-read 的表达式组合，并保留原配对异常/TDZ 义务；随后
  继续异常函数族、循环/异常、finally 与协程。第 1 步及五步整体仍未验收。
  验证：`abc_structure`、`abc_semantic`、`exception_candidates` 及相关测试目标
  重建成功；24 项定向 CTest 全部通过。`immersive_exception.mjs` 使用最新工具
  对真实 8284 方法再次通过行为与 PC 映射回归。未运行全量语料、RSS 或基准。

- Rules 51 新增不可变 `ModuleEnclosure`：同一 ABC/record 的外部 import
  slot、原始 local 名称和 owner 绑定到原生 ESM prelude；共享异常方法准备及
  打印时重新验证实际树别名和目标作用域。名字列表本身不构成发布许可；
  本地模块槽、可变别名、捕获和函数族模块发布仍拒绝。全局名称检查同时覆盖
  未被当前方法读取的导入，避免把 import 遮蔽误认为 VM 全局查找。
  文件化方法现经 `VerifiedFunction -> FunctionEmission` 输出，位置元数据
  记录 module binding 要求；没有开放 raw tree printer，也没有复制导入值。
  原生 ESM 组合用例验证循环依赖初始化前的 ReferenceError 被 catch 捕获，
  初始化后的读取随导出更新，原始 load/guard PC 映射均保留。
  真实 Immersive 8284 `release` 已完成方法片段输出及 JS 执行对照：11 种故障
  路径乘 9 种任意 JS 值，检查 getter、Proxy 调用、catch 日志属性/字符串转换
  顺序、返回/抛出及全部 53 个指令位置。整个 record 明确 `record_complete:false`。
  8458 也可输出，但未作行为验收；8257/8404 仍拒绝于 evaluation，不能据此
  宣布含分支真实应用函数的验收已完成。下一步补齐这类共性求值缺口，再按
  已批准顺序推进循环/异常、typed completion/finally 与 await/yield。
  不恢复全量/RSS/基准或调度器；五步组合验收及全部 ISA 目标仍未完成。
  `abc_structure`、`abc_semantic`、`exception_candidates` 与相关目标构建通过；
  21/21 定向 CTest 通过（原 18 项加 module_enclosure、module_enclosure_source、
  tree_globals），真实样本脚本单独通过。1G 进程峰值仍未实测。

- 相位表达式已从“属性跨纯路径”推广为同一共享表达式森林：有效 use 索引、
  唯一正常路径和边复制约束不变，构建时选择效果后缀，独立验证时按源操作数
  顺序核验连续区间。最终执行根通过逆序索引确定；包括 `typeof` 等纯包装
  中的 getter，每个被移动效果都核验最终根的异常 handler 与 catch 暂存值。
  证明不足的候选树单遍恢复原快照，不反复尝试、重排效果或引入 `.call`。
  文件化实参 getter、纯包装和嵌套字符串加法经共享树与不可变发布路径输出；
  组合测试核对多 getter/转换顺序、任意抛值、Proxy、非 callable 的参数先求值
  行为及原始 PC 区间。未证明的普通数值 ADD 仍由值域检查拒绝，未放宽 NaN
  内部编码边界。三 profile 验证正序/逆序实参、不同 handler、catch 快照、
  重复 use、预算拒绝及 1/200/201/402/1608 复制链和纯包装链的线性工作量。
  真实 Immersive 8284 已到共享 `FunctionTree`，模块别名树检查也通过；仍是
  `executable:false/source_complete:false` 的条件化诊断，不是 record 发布。
  下一步闭合其 module enclosure/publication 义务并完成真实函数源码验收，
  然后继续循环/异常、typed completion/finally、await/yield 的既定步骤。
  原第 1 步、五步组合验收和全部 ISA 目标均未完成；不恢复全量/RSS/调度器。
  `abc_structure`、`abc_semantic`、`exception_candidates` 及相关测试目标构建
  通过；18/18 定向回归通过（原 17 项及 phased_arguments_source）。1G 进程
  峰值仍未实测，定向组合用例不能替代全样本与完整功能验收。

- 共享异常求值新增跨相位成员引用证明，不修改原 CFG、SSA、环境或异常相位。
  唯一 fallthrough 链、效果/边复制前缀及有效 use 索引证明：属性结果仅被该
  callee 使用，中间只有纯操作，没有分支/汇合/跳转/catch 入口或正常边复制，
  两端异常 dispatch 与所有 catch 暂存来源相同。调用绑定和真实 AST 操作数
  检查继续独立核验 receiver；没有引入 `.call`/`.apply` 或样本名特判。
  文件化成员调用、分支、catch 及 catch 局部变量快照经共享 FunctionTree、
  VerifiedFunction 和 printer 输出可执行源码；运行对照核对任意 JS 抛值、
  Proxy 调用、receiver/实参、效果顺序及全部原始 PC 映射。
  三个 profile 覆盖命名/计算属性、1/200/201/402/1608 纯路径，以及效果、
  分支/跳转、重复 callee、错误 receiver、不同 catch 快照、伪造 consumer
  和精确 work/资源拒绝。畸形调用形状在读取 callee 前拒绝。
  真实 Immersive 8284 的首个成员引用已由 block 18 root 变为调用 block 28
  的 inline；后一个引用 block 46→100 因参数 getter 的可观察效果仍为 root。
  下一步需要推广实参表达式的效果顺序与逐抛出点快照证明，再完成真实应用
  方法的发布验收；module gate 不放宽。第 1 步、循环/异常整合、finally、
  协程与完整五步组合验收均未完成。不恢复全量、RSS、基准或调度器任务。
  `abc_structure`、`abc_semantic`、诊断及相关测试目标构建通过；17/17 定向
  回归通过，包含新增 phase_reference 和原 16 项。新增证明工作量通过几何
  增长断言；这不是进程峰值测量，1G 上限仍未实测。

- 共享异常求值新增复制身份表，原 IR/CFG/相位、模块 guard 和边复制计划不变。
  参数/receiver 与已求值结果的无效果复制采用路径压缩，逐节点独立校验身份等式，
  绑定、表达式、catch/并行复制 RHS、ABI census 与树语义检查消费同一投影。
  phi/catch 的首个快照仍保留，不能直接别名到可变 phi 或 handler 局部符号；
  域更窄的副本也不能被更宽的来源替换。复制 PC 保留原词法边界锚点。
  文件化分支/调用/catch 夹具加入实际使用的参数 MOV 链后，源码与无该链的
  基准一致，不再新增复制临时量。真实 Immersive 8284 已能解析 callee 的属性
  来源，但属性仍是 block 18 的 root、调用在 block 28，明确卡在跨相位
  reference/order 证明；没有移动 getter、使用 `.call` 或放宽 module 发布 gate。
  下一步闭合跨相位调用引用、求值顺序及异常快照的共同证明。规则升至 48，
  第 1 步、五步计划和全 ISA 覆盖仍未完成。
  三个 profile 验证 1/200/201/402/1608 的死链与活跃参数复制链，覆盖域细化、
  错误根/非扁平身份、catch/phi 边界和精确 work 拒绝；组合执行核对任意抛值、
  模块 TDZ、效果顺序和 PC 锚点。CLI/诊断及相关目标构建通过，16/16 定向回归
  通过：phased_liveness、class/creation_tree_context、function_tree、binding/
  evaluation/call_plan、phased_module_guards/source、module_evaluation/aliases、
  exception_plan/copies/tree_source、phased_method_source、source_route。
  未运行全量、RSS、基准或调度器任务；1G 峰值约束仍未实测。

- 异常相位求值新增纯操作活跃性投影，沿用正常简化的纯操作白名单，不改原始
  IR/CFG/Before/Commit/Catch。反向标记所有不可省略事件与全部 phi 输入，独立
  校验省略集合的纯性与使用闭包；catch 暂存输入仍保留。共享绑定、表达式树、
  Flow/Operands/Origins 与 ABI census 消费同一核验结果，省略 PC 在原词法位置
  保留边界 anchor，不伪造表达式或临时声明。文件化分支/调用/catch 夹具加入
  callable/new-target 无用复制后，输出与原基准源码一致，仍校验任意 JS 抛值、
  效果顺序、参数长度及 PC 映射。真正返回隐藏 ABI 值仍拒绝，篡改 getter、
  return、被使用的复制或 catch 快照为省略也拒绝。三个 profile 覆盖
  1/200/201/402/1608 链及精确 work 边界；大树采用显式测试预算，不改生产默认值。
  真实 Immersive 8284 越过 `kUnsupportedEntry`，现确切于 `CallPlanError::kReference`
  拒绝：callee 的复制链尚未归并到属性读取。下一步处理共享复制/receiver 证明，
  不跳过调用语义校验。规则升至 47；死 phi、跨相位表达式归并和五步计划仍未完成。
  CLI、诊断与相关目标构建通过，15/15 定向回归通过：phased_liveness、
  class/creation_tree_context、function_tree、binding/evaluation/call_plan、
  phased_module_guards/source、module_evaluation/aliases、exception_plan/copies/
  tree_source、phased_method_source。NOP 已经由失败用例验证并补齐其省略顺序处理。
  未运行全量、RSS、基准或调度器任务；1G 峰值约束仍未实测。

- 同文件模块别名绑定器已同时接受正常与异常相位输入，复用 slot、名称、guard
  和 record 身份校验。PhasedMethod 可借用调用方的 record 上下文，持有已核验的
  别名并交给共享求值；文件化类方法夹具贯通模块表、语法解析、FunctionTree，
  再独立核对树中的模块身份。覆盖 local/external、1/2/200/201、错误名称/身份、
  精确 work 和资源拒绝；原正常路径仍覆盖 4096 组。预算反例按实际 typed
  ModuleRead/ModuleGuard/ModuleAlias work-limit 判定，不把任意失败当通过。
  选定 Immersive 8284 在显式提供同文件绑定时已通过 phase/evaluation/syntax，
  现于共享绑定层 `kUnsupportedEntry` 拒绝：ABI 入口值仍有寄存器复制引用。
  此处尚未抵达 receiver 调用检查；下一步是共享相位复制/活跃性处理，不跳过
  入口值验证。Pasteboard 5165 仍在 values 拒绝。独立发布默认路径未接入 record
  上下文，模块初始化/外层环境发布 gate 保持关闭，规则仍为 46；并非整方法或
  record 可执行发布。诊断工具始终标记 source_complete/executable 为 false。
  CLI、诊断及相关单元目标构建通过，9/9 定向回归通过：module_aliases、
  class_tree_context、phased_module_guards/source、module_evaluation、
  exception_plan/copies/tree_source、phased_method_source。未运行全量、RSS 或基准。

- 模块读取/TDZ 配对已接入共享相位求值、FunctionTree 和 printer 的条件性路径。
  原始 load 与 guard 保留，验证唯一 Before/Commit/Before 通路和相同异常去向；
  效果校验以确切 guard OpId 消费第二个事件，不泛化跳过 guard。合并还要求
  两条 raising edge 的每个 catch 暂存槽具有相同源值，且中间没有正常 phi 复制。
  以边/槽索引逐项核对，保留原有 staging 与 Catch 提交，不按模块逐次扫描图。
  三个 profile 的 local/external、宽窄编码、caller/catch 和 1/200/201/402 组配对
  已覆盖；组合执行使用真正的 JS 词法 TDZ，检查第一次失败时的旧参数、第二次
  失败时第一次读取的值、后续 live-binding 更新及任意 JS 值。篡改 guard phi 源、
  异常去向、guard placement 和 work 边界均拒绝；两个原 PC 映射到同一组合来源。
  **尚无文件模块发布新增覆盖**：测试模块符号是条件性绑定，不能替代同文件元数据。
  Immersive 8284 仍在 evaluation 拒绝，下一步将真实模块别名/名称证明接入相位 owner，
  然后闭合 receiver 调用；规则仍为 46，第 1 步、group 1 和全 ISA 目标均未完成。
  CLI 及相关目标构建通过，8/8 定向回归通过：phased_module_guards/source、
  module_evaluation、exception_plan/copies/tree_source、class_tree_context、phased_method_source。
  普通模块 4096 组夹具在进入求值前耗尽默认 SSA work，现显式提供测试预算并贯穿
  各阶段；保留全部规模/语义/精确 work 断言，未调整生产默认预算。未运行全量、RSS 或基准。

- 异常控制投影新增无效果跳转链的结构归属传播，采用 O(B+E+R) 的 Kahn 工作队列，
  不逐出口追踪跳转链。仅处理无操作数/结果/效果/异常、单后继的 ValueJump 相位；
  所有前驱必须同属一个 try 分部，原归属必须是其直接父作用域。入口、Catch、
  混合分部汇合、getter/调用/环境等操作均不移动；原始语义区域、CFG、边复制及
  跳转 PC 保留。不同真实终点、跨多层退出、循环与 completion 仍不是本项支持。
  三个 profile 的解码分支/catch 用例通过共享树并执行，包含 gap jump、共享出口、
  1/200/201/402 跳转链和 try 外 getter 的拒绝反例。文件化 forwarder 夹具也进入
  VerifiedFunction/ABI/FunctionEmission，比较返回/抛出值、getter 次数并检查全部
  PC 与跳转 anchor。规则升至 46；这不是把 try 的原始异常保护范围扩大。
  真实 Immersive 8284 已从 MultipleExits 推进到模块求值拒绝，尚未输出完整源码；
  下一步接通模块相位求值及 receiver 证明，真实方法验收后继续循环/异常组合。
  CLI 和相关目标构建通过，6/6 定向回归通过：exception_plan/copies、
  class_tree_context、source_route、exception_tree_source、phased_method_source。
  未运行全量语料、RSS 或基准；不能据此宣称一般多出口、循环或 finally 已完成。

- 异常模块配对已从原始异常分析接到工作 IR 对应校验和 `PhasedMethod`：
  `Build/VerifyPhasedModuleGuards` 重新核对未修改的导入图及原始异常去向，产出
  与正常路径相同的 `ModuleGuards`。仅接纳无旁路入口、load/guard 异常去向一致的
  配对；保留两个操作、原始 PC 和效果，不删除 guard，也不授权移动异常快照。
  owner 仅在该配对证明成立时使用条件性的模块 Language 事实，随后再次核对
  源码配对对应关系。模块绑定、相位求值和 AST 融合仍未接通，不能发布源码。
  三个历史 profile 的 local/external、宽/窄编码、caller/catch 去向及
  1/200/201/402 配对测试覆盖修订、PC、相位、效果、槽、配对覆盖篡改、不同
  handler 边界和精确 work/零 bytes。文件化模块夹具从值域拒绝推进到求值拒绝。
  真实 Immersive 8284 已越过 PC 47，调试确认现在由异常投影的
  `ExceptionControlError::kMultipleExits` 拒绝；pasteboard 5165 仍在值域阶段拒绝。
  这不是新增完整函数覆盖：规则仍为 45，第 1 步和 group 1 均未完成。
  相关目标与 CLI 构建通过，8/8 定向回归通过（module_guards、phased_module_guards、
  class_tree_context、phased_method_source、exception_copies/plan/tree_source、
  phased_value_import）。未运行全量、RSS 或基准。下一步整合多出口异常区域，
  同时保持模块绑定/求值和 receiver 调用义务，禁止靠预设初始化或外部辅助值绕过。

- 文件方法入口新增 `PhasedMethod`，持有异常 CFG/SSA、环境、值域、输入证明、
  区域投影及求值计划；借用视图随所有者移动仍有效。含 try 的方法进入现有
  `VerifiedFunction` 后半段，共用同文件常量解析、FunctionTree 独立语义检查、
  callable ABI、全局绑定验证和不可变发布，不新增 writer。源码规则升至 45。
  文件化解码夹具已实际生成并执行包含 getter、普通调用、分支和 catch 的类方法；
  比较任意 JS 返回/抛出值、效果顺序、普通调用 receiver，验证 9 条指令的 PC 映射。
  ABI 不把 catch 绑定计作形参；验证 owner 移动、精确 work/零 bytes、错误 try 元数据，
  并拒绝未排除 VM 资源耗尽差异的 V1 契约。默认参数与异常相位尚未整合。
  `abc_semantic` 及相关目标构建通过，16/16 定向回归通过，包含新
  `phased_method_source_test`、phased_value_import、source_route 及上轮 13 项。
  元数据候选工具检查了 pasteboard、Immersive、MultiCommunityApplication 三个 ABC；
  实际尝试 pasteboard 5165 与 Immersive 8284 仍为 unsupported，无 legacy fallback。
  调试确认 Immersive 8284 在 PC 47 的模块 TDZ 检查缺值域证明；随后还需闭合
  跨相位显式 receiver 调用。不能用夹具通过替代真实应用验收，第 1 步仍未完成。
  下一步先整合这些输入证明及调用，再按序完成循环跨区、completion/finally、
  await/yield。函数族/record 异常发布亦未完成。未运行全量、RSS 或基准。

- 共享 FunctionTree 已接入异常投影、handler 作用域 catch 参数、抛出前暂存与
  Catch 入口提交，复用共享 printer。三种 profile 的解码指令用例已执行生成 JS，
  验证分支、getter/调用效果顺序、旧局部值与嵌套 catch。
  独立 TreeFlow/操作数/来源检查现已接入构建出口，不重新生成预期 AST：核对
  实际 catch 绑定、try 归属、Before/Commit 分支位置、原正常边、抛出前暂存与
  Catch 提交、暂存槽独占和原始 PC。错位复制、phi/slot 混用、错误 RHS/catch 值、
  篡改 PC、scope 与修订均有负例，包含语法合法但语义错误的篡改。
  200/201/402 层区域已走到共享树及独立语义检查；分支/return/throw 的语句位置
  和 jump anchor 同时验证。仍是 `ExceptionFunctionDraft`，缺真实文件常量、ABI、
  函数族绑定与发布验收，不计作第 1 步完成。后续依次为循环跨区、typed completion/finally、
  await/yield 协议与组合验收；规则版本仍为 44。
  本轮 `abc_semantic` 及相关测试目标构建成功，13/13 定向回归通过：
  exception_plan/copies/tree_source、tree_structure、try_tree/source、function_tree、
  control_plan、creation/class_tree_context、argument_snapshot/source、constant_group。
  未运行全量语料、RSS 或基准。

- T09 异常控制层已复用共享 postdominator/区域发现算法，详见
  [异常控制投影](exception-control.md)。ControlTopology 只借用拓扑，不复制或清空
  ValueProgram 的 phases；ExceptionControl 显式区分原 phase、try wrapper 和区域
  正常退出单元，投影边保留原 EdgeId，异常分派仍由 ExceptionPlan 保存。
  Catch 相位按所属 handler 定位，不误落入同 PC 开始的内层 protected scope。
  区域祖先采用 DFS 区间，入口采用排序索引；每条边只检查最内层出口，子区域摘要
  按逆序传播一次，避免按边反复扫描祖先。独立骨架校验核对边、分支、根和节点
  独占所有权，不重跑 postdominator。仅保留 explicit throw 的空 disconnected Commit；
  其他不可达块仍拒绝。普通控制入口继续拒绝 phased graph。
  三个 profile 的解码用例覆盖 protected diamond/汇合、共享起点嵌套、handler 起点
  嵌套 try、旧局部值、200/201/402 层区域及篡改 handler/出口/单元/边/修订/不可达
  Commit、精确 work/零 bytes 边界。两个 getter/两个调用的异常复制用例同时消费
  控制投影和共享表达式，核对 staged op/raising edge/Catch 提交的相同身份。
  13/13 定向回归通过：exception_plan/copies、control_plan、function_tree、try_tree、
  try_tree_source、creation/class_tree_context、argument_snapshot_source、constant_group
  和三个既有应用切片；abc_semantic 构建通过。新入口先编译失败后实现接通。
  **已接入 FunctionTree 和条件性语义校验，尚未接入源码发布**。当前仅接纳无环、
  每个 try 单一正常出口的区域；多出口和投影循环均 typed reject，不是最终支持
  边界。下一步接通真实文件 callable/ABI 与发布身份，再完成循环/异常混合区域、
  completion/finally、async/generator 挂起恢复。规则仍为 44，全 ISA/group 1 未完成。
  未运行全量、RSS 或基准。

- T09 共享源码 AST 新增 TryStmt/CatchClause，支持 try/catch、try/finally 和
  try/catch/finally；所有区域必须独占 BlockStmt。新增明确的 catch 参数 binding/
  declaration/scope 类型，只有所属 handler 才激活参数，退出后恢复外层同名绑定。
  protected body、finally 和相邻语句不能看到该参数；同层重复声明拒绝，嵌套块
  可遮蔽，catch 参数可以赋值但不能伪装为普通 DeclarationStmt。遍历沿用有界显式
  栈和按名字分组的作用域索引，不改 CFG/SSA，也不按样本或构造器新增 writer。
  共享 printer 输出三种结构，并记录所有区域语句及表达式的生成范围。新增
  try_tree_test/try_tree_source_test 覆盖任意 JS 抛出值（含 Proxy、Symbol、NaN、-0）、
  catch 赋值与外层参数隔离、finally 执行次数和 return/throw/break/continue completion、
  非法作用域/复用/缺少区域、1/200/201/402 层嵌套以及精确 work/bytes 边界。
  function_tree_test 确认语法合法的未证明 TryStmt 仍被语义发布检查拒绝。
  13/13 定向回归通过：上述两项、tree_structure、function_tree、exception_copies、
  family_enclosure、creation/class_tree_context、argument_snapshot_source、constant_group，
  以及 pasteboard/Immersive/call 三个既有源码切片；abc_semantic 构建通过。
  新 AST 规格先在缺少类型时编译失败，再接通并执行。未运行全量、RSS 或基准。
  **新增 JS 用例是合成 AST 的 printer 执行验证，不是 ABC 异常端到端验收。**
  下一步把 ExceptionPlan/异常复制接入共享控制树与 FunctionTree，并独立校验 handler
  范围、参数载荷、两阶段赋值和 PC→生成位置；catch 闭包捕获、finally completion
  识别和 async owner 仍未完成。规则仍为 44，group 1 与全 ISA 目标保持未完成。

- T09 绑定与表达式层已消费异常求值计划：CatchParameterBinding 保留独立 Catch
  块/值身份，CatchSlotBinding 分配实际私有 SymbolId，不复用参数、phi、快照或
  cell；异常 bundle 不再分配不会执行的抽象 cycle temporary。普通/构造器沿用
  同一绑定算法，新 phased 入口不解除普通入口的拒绝条件。
  暂存与提交现在成为共享 AssignmentExpr/SymbolExpr，分别记录抛出操作前的根
  范围和 Catch 提交根范围，不冒充普通边复制，也不重用 getter/调用表达式节点。
  正常/异常复制的 AST 构建统一提取到 expression_copies，原表达式文件相应缩小。
  Catch 没有伪造的指令 PC；提交沿其唯一出边记录合成来源，原操作保留 OriginalSite。
  三个 profile 的两个 getter/两个 CALLARG0 解码用例已到共享表达式 AST，分别保留
  旧值/新值、原始 PC 和 value-call 引用形式。显式 receiver 的跨相位 member-call
  融合尚未实现，既有引用检查保留，不用 `.call` 或重复 getter 绕过。
  新规格覆盖私有符号不别名、0/1/2/200/201/402 phi、根/RHS 独立所有权、两阶段赋值
  源/目标与边来源、normal/phased 入口隔离以及绑定/表达式的精确 work 和资源边界。
  22/22 定向回归通过：exception_copies/plan、phi_copies、call_plan、evaluation_plan、
  function_tree、control_plan、phased_value_flow/import、throw_value、constant_group、
  三个既有应用源码切片、creation/class_tree_context、source_route、function_input、
  async_semantics、exception_arguments/dispatch、argument_snapshot_source；abc_semantic
  构建通过。新增绑定、调用和表达式正例先运行失败，再接通。未运行全量、RSS 或基准。
  **新证据仍不是完整异常源码**：仅表达式 AST，尚无 TryCatchStmt、handler 声明作用域、
  异常控制树及最终语义/生成位置证明。下一步必须完成这些集成与实际 JS 执行，
  不能把 catch 参数当成 body 局部变量。源码规则仍为 44，全 ISA 与 group 1 目标未完成。

- T09 异常 phi 已接入同一套并行复制与求值算法：新增显式 phased 入口，保留
  Before/Commit/Catch 可用性检查，不把异常图降格为普通图。EvaluationPlan 持有
  ExceptionCopies，独立验证缺失/伪造计划；normal 入口及普通 BindingPlan 仍拒绝它。
  抛出前仅写调用私有 CatchSlot，只有进入 Catch 才更新正式 phi 位置；正常返回
  不修改这些 phi。各抛出边为同一 catch 复用按 phi 身份分配的槽，后一次暂存覆盖
  前一次的槽值；绑定契约禁止与参数、cell、phi 或 cycle temporary 共用位置。普通边和方法
  入口仍使用既有复制调度。共享算法不跨相位融合 getter/调用等求值。
  三个 profile 的解码测试贯通异常 SSA→值 IR→区域计划→复制→求值；同一 catch
  从两个 getter 进入时，分别携带旧局部值与更新后的局部值。另覆盖 0/1/2/200/201/402
  catch phi、入口/普通/Catch 出边混合、正常返回前不写 phi、暂存更新、错误槽/落点/
  提交/修订、正常结果泄漏到 catch，以及构建和校验的精确 work 与资源边界。
  21/21 定向回归通过：exception_copies/plan、phi_copies、evaluation_plan、
  function_tree、control_plan、phased_value_flow/import、throw_value、constant_group、
  三个既有应用源码切片、creation/class_tree_context、source_route、function_input、
  async_semantics、exception_arguments/dispatch 和 argument_snapshot_source。
  abc_semantic 构建通过；新增阶段分别先运行失败再通过。未运行全量、RSS 或基准。
  **没有新增异常 JS 输出覆盖**：当前新证据到求值计划为止，生成 JS 测试是既有路径
  回归。下一步必须把 CatchBinding/CatchSlot 绑定和两阶段赋值接入共享 AST/控制树，
  验证实际 try/catch 执行和 PC 位置；不得直接把抽象 exceptional EdgeCopies 放在
  抛出前执行。规则仍为 44，group 1、finally、async owner 及全 ISA 目标均未完成。

- T09 新增通用值 IR 的 ExceptionPlan 与独立对照验证：重新核对原始相位导入、
  保护区/handler 范围及优先级，按原 TryId 关联 Before 入口和 CatchDefinition。
  普通边、抛出边、Catch→handler 边采用不同类型；catch 载荷不能冒充函数体局部值。
  计划只描述异常区域和边，不改变 CFG/SSA，不清空 phases，不授予源码发布能力。
  构建与校验共用有界预算；一次索引 phase/definition，避免逐 handler 扫值表。
  嵌套测试暴露并修正了原区域校验对“同一入口开始的多层 try”的误拒绝；入口祖先
  按外到内顺序预计算，不在每条边上遍历祖先。普通跳入 catch 仍明确拒绝。
  新测试先在未实现计划处运行失败，再覆盖三个 profile、旧局部值、异常再抛出、
  200/201/402 前导指令与嵌套区域、错误载荷/入口/范围顺序/边分类/修订/PC 及精确预算。
  14/14 定向回归通过：exception_plan、throw_value、phased_value_import/flow、
  control_plan、function_tree、evaluation_plan、async_semantics、exception_arguments、
  exception_dispatch、constant_group 及 pasteboard/Immersive/call 三个现有源码切片；
  abc_semantic 构建通过。未运行全量测试、RSS 或基准。
  **尚未增加 try/catch 源码输出覆盖**，规则仍为 44；下一步将本计划接入共同的
  求值/绑定/控制树：异常边输入在抛出前暂存，正式 phi 写入在 Catch 提交，catch 参数进入真实 handler
  scope，再接共享 TryCatch AST、printer 与位置验证。finally/async owner 仍未完成，
  不以区域计划测试替代 group 1 或全 ISA 的端到端验收。

- 显式 THROW_PREF_NONE 已端到端接入普通无 handler 主干：ValueThrow/kThrownValue
  保留原值，ThrowStmt 进入通用 AST、独立结构/语义验证、printer 和 PC→语句位置映射。
  文件来源的三个 profile 新增 39 个生成函数，覆盖常量/严格 this 抛出、回调副作用、
  较早异常优先、undefined/NaN/-0/bigint/symbol/Proxy 等原值身份与无隐式转换。
  源码规则升到 44，V3 契约不变；未新增专用 writer。
  异常主干同步接入显式 throw，保留其结构性不可达、无值/无效果 Commit；不制造
  正常落空边，Catch/向调用方传播及异常 payload 仍独立核验。200/201/402 前导指令、
  内部 Hole、错误完成/载荷、AST 被换成 return/表达式、非法表达式 ID 均有测试。
  20/20 定向回归通过：throw_value、phased_value_import、phased_value_flow、
  value_flow/value_import/value_simplify、function_tree、evaluation_plan、control_plan、
  source_route、constant_group、async_semantics、exception_arguments、function_input、
  creation_tree_context、class_tree_context、argument_snapshot_source 及三个应用源码切片。
  abc_semantic 构建通过；新增导入与文件源码正例均先运行失败，再通过。
  未恢复全量测试、RSS 或基准，1G 上限未在本轮实测。
  **尚未完成 try/catch/finally、通用不可达指令保留、async owner 与 group 1**；
  本项是依赖提前实现，也不代表 group 8 全部格式已覆盖。

- 已新增真实异常 SSA 到通用值 IR 的 ImportPhasedValues 和独立 VerifyImportedPhases。
  对照原始 PC、相位身份、输入 ABI、操作/效果、Commit/Catch 定义、SSA 每次读取及
  phi 入边；异常出口保留选中的 TryId/HandlerId/handler PC/Catch 块，区别向调用方
  传播。普通导入拒绝该 handler 元数据，普通改写不能篡改它；未新增专项源码 writer。
  输入是同一未变更 flow/SSA/env/value/proof/frame 的借用集合；预算覆盖输出图与
  验证 scratch，输入保留存储由调用方另行准入。顺序读取 SSA phi 输入避免逐 phi 扫图。
  初始快照正例在返回“不支持”的占位实现处运行失败，随后通过；不是只增加接口。
  13/13 定向回归通过：phased_value_import、phased_value_flow、value_flow、
  value_simplify、value_import、exception_arguments、async_semantics、control_plan、
  evaluation_plan、function_tree，以及 pasteboard/Immersive/call 三个既有源码切片
  执行测试；abc_semantic 构建通过。新增用例含三个 profile 的 0/1/200/201/402 参数、
  handler 汇合 phi、词法环境前导、catch 读取旧局部值、元数据反例及精确 work 边界。
  未运行全量、RSS 或基准；未声称 1G 上限已实测。
  **仍不算异常源码贯通**：通用不可达相位、归一化恢复 owner、环境/捕获与
  源码作用域对照、异常区域 AST/completion 和源文件发布仍待完成，group 1 未完成。

- T09 相位可用性已纳入同一个 ValueProgram：Before 保存操作与效果，正常结果
  显式指向 Commit，CatchDefinition 独立定义异常值，保留异常边及不同于指令 PC
  的相位身份。共享支配关系/稀疏 phi 检查阻止正常结果泄漏到抛出路径；普通导入
  对照、简化、改写、求值和控制规划明确拒绝相位图，不静默丢弃相位。
  新测试覆盖 Catch/正常结果隔离、异常边 phi、正常跳入 handler 的汇合、畸形
  Commit/边/handler、精确预算及 200/201/402 阶段对直线和循环图。
  最新验证 12/12：phased_value_flow、value_flow、value_simplify、value_import、
  exception_arguments、async_semantics、control_plan、evaluation_plan、function_tree，
  以及 pasteboard/Immersive/call 三个既有生成源码切片执行测试；abc_semantic 构建通过。
  初始新测试在缺失相位 API 处编译失败，并非运行期红测；未运行全量/RSS 验证。
  **这不是 T09 或 group 1 完成**：初始独立导入与对照见上；通用不可达相位、
  环境/捕获衔接、异常区域 AST 和 async owner 仍未接通，不另建源码 writer。

- 已补齐异常图中的 GETUNMAPPEDARGS 帧衔接：BuildExceptionFlow 接收并保留布局，
  复用普通语义提升的隐式参数窗口；异常源码调用方传入真实 FrameLayout，但不
  自动伪造调用归属。VerifyExceptionInputs 可接收显式 ArgumentFrameFact 并在
  成功结果中保留，验证窗口/效果、Before 读取顺序与槽、对应 Commit 的正常结果；
  缺失事实（包括零参数）或事实与布局不符拒绝，内部值不能借事实变成 JS 值。
  正常与异常路径复用同一快照效果检查，O(N+U+S log V) 工作、O(1) 额外验证存储。
  新增 0/1/200/201/402 参数、正常结果与 Catch 异常值分离、InstructionSsa 不把
  Catch 映射为普通指令定义、词法环境成功 pop／异常保留、畸形布局与效果、错误
  Commit、内部槽值和精确 work 边界测试。新正例先在原异常提升处失败。
  8/8 定向回归通过：exception_arguments、argument_snapshot、async_semantics、
  function_input、creation_tree_context、class_tree_context、source_route、
  argument_snapshot_source（原 51 组源码执行）；相关构建与 abc_semantic 成功。
  未运行全语料、RSS、基准或 Ark VM 差分。源码规则仍为 43；这不是异常 AST、
  完整 async 或 group 1 完成。下一步仍为 T09 相位化值 IR 导入，再接统一异常 AST；
  真实创建 owner 必须在源码边界独立闭合，不能把布局匹配当作 owner 身份证明。
- 用户已明确同意恢复后的源码文字不同，同时要求执行行为、作用域、参数、返回值
  和副作用保持正确。本轮落实 [源码契约 V3](../source-contract-v3.md)，只增加文字
  差异许可，不改变 V2 的其他前提，不豁免 ABI、捕获、异常、宿主快照工厂或求值
  顺序检查。V3 成为引擎默认，规则升至 43；收据和位置产物记录版本。
  V3 的箭头实参载体不再需要外加文字许可；V2 仍须显式许可，撤销后输出仍拒绝。
  新正例先在旧默认门禁失败，随后 6/6 定向回归通过：creation_tree_context、
  class_tree_context、source_route、tree_globals、family_enclosure 和
  argument_snapshot_source（51 个普通/箭头/类方法源码变体执行及 PC 映射）。
  三版本收据往返、未知版本拒绝和 V2 其他策略不变均覆盖；相关目标与 abc_semantic
  构建成功。没有改动 CFG、scope 或函数体变换，也未运行全语料、RSS 或 Ark VM 差分。
  下方“文字前提待确认”是历史记录，现已由本项取代。
  group 1 与完整 async 仍未完成；继续 T09/T10 时已定位异常图未传 FrameLayout、
  无法组合 GETUNMAPPEDARGS 的缺口，须连同零参数快照的 owner 门禁一起接入，
  不能仅因没有显式寄存器读取就授予源码权限。本轮先落实用户新确认，尚未修改该接口。
- group 1 的 async 前置切片：AsyncEnter / AsyncSettle 已进入共享语义 IR，
  复用 coroutine 操作数解析、正常 SSA 与 Before/Commit/Catch 异常 SSA。
  保留原 PC、状态与结算值、正常结果提交以及抛出前的分配/堆/用户代码效果；
  不移除原字节码，不新增 writer。验证 enter 失败逃逸、resolve 异常保留状态
  寄存器、Catch 定义异常 acc，普通跳转进入 handler 则绕过 Catch 并产生正确 phi。
  新增现代编码、畸形操作数、历史编码拒绝、预算边界及 200/201/402 分配点测试。
  原始 async 操作仍被 typed value/source 门禁拒绝，尚无 owner 证明、async AST
  或可执行源码交付；规则维持 42，group 1 未完成。后续依赖与验收见
  [async phases](async-phases.md)，继续 T09 相位值导入、T10 owner 和共享 AST。
  新正例先在旧语义提升处失败；7/7 定向回归通过（async_semantics、constant_object、
  value_operands、value_import、source_route、function_input、constant_group），
  对应构建与 abc_semantic 成功。扩展测试中修正了零预算在 CFG 层先拒绝的预期，
  以及旧 value_operands 测试仍将已支持 LDGLOBAL 视为 unsupported 的过期断言；
  未放宽生产门禁。未运行完整 async 源码、Ark VM 差分、全语料、RSS 或基准。
  用户对源码文本反射问题表示未理解，已用变量改名导致 toString 文本变化解释；
  仍未取得接受前提的确认，相关默认发布策略不变。
- 规则 42 新增箭头自身调用载体：保留 kArrow 身份，以类型化 InvocationForm
  选择带原名、不可构造的简洁方法表达式；共享 AST/结构/ABI/printer 接入，不新增
  body writer。动态 receiver、LDTHIS、new.target 的既有拒绝不放宽，捕获仍使用
  原 owner 的 cell/symbol；LDFUNCTION 自引用复用私有创建绑定。
  新路径默认不发布：除宿主快照工厂，还须显式 CallableTextPolicy::kRecovered，
  缺失或撤销时 kCallableText 拒绝，位置产物记录承载形式与文本策略。原因是
  Function.prototype.toString 可观察重建源码文本；已向用户确认整个引擎的等价
  范围，当前尚未收到答复，未暗中扩展 V2 或增加 CLI 开关。该问题亦影响其他
  重建源码，方法承载实现不证明整个引擎的源码反射等价。
  51 个文件级合成源码变体执行通过，包括箭头分支/循环/自引用、bind 后不可构造、
  缺失/撤销文本策略拒绝及 PC 映射；伪造 callable/承载形式、名称和 new.target
  输入亦拒绝。新正例先在旧 AST 门禁失败；本轮 17/17 定向回归通过，相关目标与
  `abc_semantic` 构建成功。未做 Ark VM 差分、全语料、RSS 或基准；group 1 未完成。
- 规则 41 补齐值默认参数到函数族的衔接：复用 DefaultBody/DefaultValueProof，
  在环境重建与闭包边重绑定之前投影，不增加类方法专用 writer。原默认分支迁入
  签名后成为剩余 IR 的 Nop，原始 PC 仍由签名证明持有；保留剩余指令索引，
  不复制原始值对应关系。默认值合流后的值可作为快照条件输入，原始入参逃逸、
  缺调用帧事实、错误角色/槽数、其他协议消费者仍拒绝。已有类方法默认值正例
  原先在函数族阶段失败，现接通；同时修正元数据与编码头不一致反例的预期，
  该反例实际在解码门禁以 kBody 拒绝，而非走到参数门禁。
  新增普通/整数默认值/负数计算默认值三组类方法快照源码执行，合计 23 个文件级
  合成变体；验证方法 name/length/不可构造性、遗漏与显式 undefined、非 undefined
  值、额外尾部及宿主绑定要求。混合默认值、rest、箭头和协程仍未整体接通。
  `class_tree_context_test`、`default_candidates_test`、`argument_snapshot_source_test`
  及创建/环境/求值/ABI/路线相关 17/17 定向回归通过；`abc_semantic` 与对应测试
  目标构建成功。未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照首个宿主契约源码切片贯通：ArgumentSnapshotExpr 经共享 forest、结构/
  流/操作数检查、ABI 及 printer 输出。严格普通函数的原生 arguments 只作为
  未暴露的实际数量/额外尾部载体，声明槽仍按实际数量条件读取；返回值必须由
  显式 HostArgumentSnapshotBinding 工厂按 v7 契约新建，不能直接返回 arguments
  或数组。绑定名称、类型化 ABI 与契约版本写入位置产物；缺绑定、遮蔽、V1
  资源契约、错 owner、错槽对应均拒绝。箭头仍拒绝，未虚构其自身调用载体。
  文件级普通函数包含读、改写参数、重复快照、分支汇合和有限循环；源码执行与 PC 映射
  测试使用独立测试工厂。实际应用宿主适配、多应用与更多 callable/默认值/rest
  组合仍待验证，未将 GETUNMAPPEDARGS 或 group 1 标为整体完成。
  源码运行覆盖 20 个文件级合成变体（0/1/2/200/201 声明参数），每个再测试不同实际
  实参数量；包含分支 false/0 与 undefined/null/BigInt 的区别、构造调用、对象身份、
  属性描述符/顺序、公共数组迭代器改写及原始 PC→生成范围。封存后方法 receipt
  为 Complete，但明确依赖输出中的宿主工厂契约，不等于整份 record 或全组完成。
  新增循环覆盖 2/200/201 声明参数：两次迭代分别保存快照到参数，退出后再次读取；
  执行验证各轮槽值、嵌套对象身份与跨调用隔离，实际零参数也必须调用工厂三次。
  复用既有 CFG/phi/循环 AST，无生产代码或规则版本改动，不扩大 callable 支持声明。
  本轮循环相关 8/8 定向回归通过（上述快照/创建/求值测试及 control_plan、phi_copies、
  function_tree、tree_structure）。其中既有 control/phi 4096 规模夹具最初触发
  kWorkLimit，已改为显式有界 O(n log n) 测试配额，并保留精确 work / work-1
  成功与拒绝检查；未放宽生产默认限制，也未启动性能基准或 RSS 测量。
  新正例先在旧 AST 门禁失败；`argument_snapshot_source_test`、
  `creation_tree_context_test`、`argument_snapshot_test`、`argument_evaluation_test`、
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `tree_globals_test`、`call_plan_test`、`evaluation_plan_test`、
  `default_candidates_test`、`function_input_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 15/15 通过；相关目标和
  `abc_semantic` 构建成功。未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照进入共享表达式规划：新增类型化 ConditionalArgument 输入模式，保留
  协议读取/原序号/语言值域及当前调用前提，不将条件读取改成必定求值。
  槽值的生产表达式禁止内联到快照里，包括携带 getter 的纯复制节点；独立调度
  检查器验证该边界，并保留快照自身的有序分配事件。CFG、作用域和 phi 路线不变。
  普通/arrow 文件夹具推进到求值计划，AST 仍拒绝；实际 count/tail 的源码绑定、
  精确对象工厂和统一 printer 尚待实现，本次仍不计完整指令源码覆盖。
  新正例先在旧求值计划门禁失败，再通过实现；覆盖三 profile、0/1/200/201/65536、
  getter 后写参数、重复快照、分支 phi、循环、伪造内联、丢失归属、错效果/序号/
  读取目的/内部值域和预算。`argument_evaluation_test`、`creation_tree_context_test`、
  `evaluation_plan_test`、`argument_snapshot_test`、`value_simplify_test`、
  `default_candidates_test`、`function_input_test`、`constant_group_test`、
  `value_created_family_test`、`source_route_test`、`function_tree_test`、
  `call_plan_test` 共 12/12 通过；相关目标及 `abc_semantic` 重建成功。
  未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参快照进入共享值 IR：ValueArgumentSnapshot 保留声明槽数量及条件协议读取，
  ValueProgram 保留已核对的当前调用 ArgumentFrameFact；不是将声明数量当实际数量。
  导入重新核对原指令/效果/SSA；对应检查验证槽数量、归属、PC、读序号和异常出口。
  简化器复制并保留归属，改写检查拒绝丢失/变更该事实；快照即使效果字段被清空也
  不可作为纯操作删除。文件级普通/arrow 输入已到达值 IR，求值计划仍显式拒绝，
  运行时 count/tail、精确对象工厂、AST 和源码绑定仍未完成，不计指令源码覆盖。
  新测试覆盖三 profile、0/1/200/201/65536 参数、重复分配与参数槽改写，
  错归属/槽数量/读取目的/序号/异常效果、预算耗尽及简化保留。
  文件级正例先在旧值导入处失败；新增改写检查又捕获 Clone 遗漏归属，补齐后
  `argument_snapshot_test`、`creation_tree_context_test`、`function_input_test`、
  `snapshot_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test`、`value_simplify_test`、
  `default_candidates_test` 共 10/10 通过；相关目标及 `abc_semantic` 构建成功。
  未运行 RSS、基准、全语料或 Ark VM 差分。
- 实参输入已绑定精确 callable owner：FunctionInput 在核对文件身份、目标和帧后
  提供 ArgumentFrameFact，并保留原创建点 stamp；同目标不同创建点不可互换。
  独立检查原 opcode/PC、规范帧、范围、正常/异常效果及 SSA 读写对应关系，
  随后继续逐值域验证，Hole 不因潜在缺省实参而放行。源码规则版本 37。
  分析层覆盖三 profile、0/1/200/201/65536、改写/分支/循环、错 IR/SSA 和预算；
  文件级普通/arrow 覆盖 0/1/200/201 参数、缺 owner、错身份/目标/帧和不同创建点。
  证明保留实际数量/尾部属于当前调用的前提，不将声明数量变成实际数量，
  不把箭头函数绑定到外层 arguments。ValueOperation、运行时 count/tail 绑定、
  AST 与源码仍待实现；带 owner 的文件级输入目前在值 IR 阶段拒绝，不计完整输出。
  同时修复该文件级夹具的旧 API 预算参数、写死的创建 PC 和角色冲突取值错误。
  新文件级 owner 正例先在旧 kArgumentOwner 门禁失败，再通过实现；
  `creation_tree_context_test`、`function_input_test`、`argument_snapshot_test`、
  `snapshot_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 8/8 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参快照的共享 OperandView 已接通：常量大小的窗口视图按 32-bit 序号
  描述每个当前参数槽，标记为 Argument 协议依赖，不授权无条件 JS 求值。
  独立校验读范围、acc 写入、allocation 和异常前效果；错槽、错数量、
  缺范围、额外固定读、错写入及删改效果均拒绝。相同 ABC 经 IR/SSA 后再核对
  操作数对应关系；覆盖 0/1/200/201/65536，以及 28-bit 最大窗口的 O(1) 访问。
  实际数量/尾部所有者、ValueOperation、AST 尚未接通，源码仍以 kArgumentOwner
  拒绝。未改变源码发布范围，规则版本仍为 36，不计为该指令端到端完成。
  `snapshot_operands_test` 先因不支持快照失败，实现后与 `argument_snapshot_test`、
  `call_operands_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 7/7 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- GETUNMAPPEDARGS 已接入共同语义提升和正常 CFG/SSA：ArgumentSnapshot 保留
  当前声明槽的隐式 may-read、每次执行的 allocation 及异常前效果，acc 仅在
  正常返回定义。FunctionInput 传入真实 FrameLayout；无帧信息、错位/不匹配
  布局继续拒绝。32-bit RegisterRange 与 16-bit CallRange 分离，保留资源预算。
  源码验证以 kArgumentOwner 显式拒绝，尚未提供实际数量/尾部所有者证明、
  值 IR adapter 和 AST；零声明参数也不能越过该边界。规则版本 36。
  新测试覆盖三个 profile、0/1/200/201/65536 槽、当前槽覆写、连续快照、
  分支合流和循环回边，以及巨大窗口的预算拒绝；不是生成源码完成证据。
  `argument_snapshot_test`、`argument_window_test`、`call_operands_test`、
  `register_ssa_test`、`register_uses_test`、`register_effect_test`、
  `constructor_call_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 11/11 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参窗口新增 ArgumentWindow：复用 BuildFrameLayout 验证隐藏 ABI 槽顺序、
  28-bit 计数和参数边界，以 ActualArgumentCount 区分当前槽前缀与额外实参尾部。
  计算 O(1)，不按声明或实际实参数量分配；声明数量不作为实际数量的默认值。
  rest 证明已消费该校验，错置 first_parameter 的反例先失败后修复。
  测试覆盖全部调用标志组合、零参数、200/201/65536、最大运行时字段、
  数量溢出及各隐藏槽错置。规则版本 35；仍待 callable owner、语义 IR/SSA
  快照依赖和源码接入，不计为 GETUNMAPPEDARGS 完成。
  `argument_window_test`、`rest_arguments_test`、`frame_layout_test`、
  `source_route_test`、`constant_group_test`、`value_created_family_test` 共 6/6
  通过；对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- 实参快照前置：共享 SSA、值 IR、操作数对应关系及 guarded-read 查询的读取
  序号扩为 32 位，不再把编码范围的 16-bit 宽度错误套到整个输入列表。
  65535 项范围加 callee/receiver 的边界用例先因旧上限拒绝，新增用例验证
  每个 occurrence 保持顺序、重复槽保持同值但不同 occurrence、末项不截断。
  编码 CallRange 仍为 16 位，参数窗口的独立范围模型仍待接入；不计为
  GETUNMAPPEDARGS 完成，也不改变 CFG、作用域或副作用顺序。规则版本 34。
  `register_ssa_test`、`register_uses_test`、`call_operands_test`、
  `guarded_reads_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 8/8 通过；相关目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- GETUNMAPPEDARGS 的运行时核对发现 rest 签名证明边界缺口：该指令没有显式
  寄存器操作数，但按实际实参数量复制当前物理实参槽，会隐式读取原 rest 槽。
  ProveRestPrologue 原本会错误放行这种后续读取，新增反例先失败，现已拒绝。
  未把指令错误提升为零输入常量，也未直接输出 native arguments/rest 数组。
  [实参快照设计](argument-snapshots.md) 记录新对象身份、caller/callee 描述符、
  帧数量/尾部输入、SSA 当前槽依赖、16-bit occurrence 边界和 callable 约束。
  规则版本 33；本项是接通该指令的前置修正，不计为 GETUNMAPPEDARGS 源码完成。
  `rest_arguments_test`、`source_route_test`、`constant_group_test`、
  `value_created_family_test` 共 4/4 通过；相关目标及 `abc_semantic` 构建成功。
  未运行 RSS/基准/全语料或 Ark VM 差分。
- LDSYMBOL 已贯通 ValueSymbolConstructor -> SymbolConstructorExpr -> 统一
  printer，并通过显式 HostSymbolBinding 记录原始构造器的不可变宿主绑定。
  V1/V2 均不自动假定公开 Symbol 属性稳定；缺能力、名字遮蔽或与 realm
  绑定同名时拒绝。普通全局读取也不能被新绑定捕获；换目的地重新验证。
  PrintedFunction 和位置 JSON 保存绑定要求及契约，PC 映射到真实标识符区间。
  三个 profile 的普通/arrow、直接/回调共 12 个文件级变体，在公开 Symbol
  被替换和抛异常 getter 两种环境下执行；保留身份、Symbol 调用、不可构造性、
  回调异常与重入。两个窄/宽闭包变体验证词法捕获、key 异常和 setter 重入；
  六个缺能力反例继续拒绝。作用域单测覆盖 V1/V2、非法名、外部/参数/模块名
  冲突、祖先/兄弟块、能力冲突和 200/201/4096 work 上界；没有测量 RSS/延迟。
  规则版本 32；下面规则 31 的源码拒绝状态是历史前置。本项不代表整个第一组
  或 record 发布完成，V1 命名创建及协程/derived owner 仍受独立前提限制。
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `evaluation_plan_test`、`tree_globals_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`computed_store_test`、`value_created_family_test` 共 10/10 通过；
  对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDSYMBOL 新增 SymbolConstructorLoad -> ValueSymbolConstructor，保留 pinned
  GlobalEnv::SymbolFunction 身份，不将其误写成 Symbol/globalThis.Symbol 查找。
  无寄存器输入，只定义 acc，结果 Defined、无普通异常或可观察效果。
  三个 profile 新增导入、值域、畸形操作数及 IR 身份替换测试；测试先失败于
  语义提升，再实现。源码端仍拒绝，六个文件级直接/回调反例防止误发 Complete。
  规则版本 31；这是可信 intrinsic 绑定/AST 的前置，不计为 LDSYMBOL 源码完成。
  未扩大 V2 的 globalThis 前提，后续步骤见
  [Symbol Constructor Dependency](constant-group.md#symbol-constructor-dependency)。
  `value_import_test`、`evaluation_plan_test`、`source_route_test`、
  `constant_group_test`、`value_created_family_test` 共 5/5 通过；对应目标及
  `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDGLOBAL 已接通 ValueGlobalObject -> RealmObjectExpr -> 统一 printer：
  在实际函数族目的地解析稳定 globalThis 或显式宿主绑定，复核祖先作用域与
  外部绑定后才发布。每次换目的地重查，缺上下文或遮蔽在写入前拒绝。
  PrintedFunction 保存实际 policy/name，位置 JSON 序列化该前提与契约版本；
  独立语义检查拒绝替换为普通全局名称或 this。规则版本 30，未改变 CFG/SSA/cell。
  三个 profile、普通/arrow、直接/回调、稳定/宿主绑定共 24 个文件级变体
  生成并执行 JS，检查身份、异常、重入、PC 区间及 JSON 前提；两个窄/宽
  闭包样本验证 realm 捕获、错误 receiver、key 异常及 setter 重入。
  V1 缺宿主/显式宿主、非法名字、局部/外部遮蔽与祖先/兄弟 scope 由单测覆盖；
  V1 完整命名创建仍受原资源异常契约限制，不冒充执行正例。以下规则 29 的
  源码拒绝状态为历史前置。本项不代表整个第一组或 record 发布完成。
  `tree_structure_test`、`family_enclosure_test`、`function_tree_test`、
  `evaluation_plan_test`、`tree_globals_test`、`value_import_test`、`source_route_test`、
  `constant_group_test`、`computed_store_test`、`value_created_family_test` 共 10/10 通过；
  对应目标和 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
- LDGLOBAL 的新主干入口已补齐：既有 GlobalObjectLoad 导入独立
  ValueGlobalObject，无寄存器输入、只定义 acc、结果 Defined；不复用普通
  ValueGlobalRead 或 globalThis 字符串语义。依据 pinned interpreter 的
  GetGlobalObject 初始化及 LDGLOBAL handler，未引入新的宿主前提。
  三个 profile 新增导入、值域、零操作数、错误 IR 身份替换与畸形编码测试；
  测试先失败于值 IR 导入，再实现。源码求值/AST 检查暂时明确拒绝，六个
  文件级直接/回调反例防止误发 Complete。该项是 realm AST/绑定证明的前置，
  `value_import_test`、`evaluation_plan_test`、`constant_group_test`、
  `source_route_test` 共 4/4 通过，对应目标与 `abc_semantic` 构建成功。
  规则版本 29；未运行 RSS/基准/全语料或 Ark VM 差分。
  不计为 LDGLOBAL 端到端完成；后续执行步骤见
  [Realm Object Dependency](constant-group.md#realm-object-dependency)。
- LDFUNCTION 扩展到普通函数/arrow 的创建期私有不可变绑定：类型化 SelfForm
  区分命名函数表达式与 CreationBinding，共用现有 IR、作用域验证和 printer。
  空/非法 runtime name、参数/全局名字冲突、含子函数的 owner 使用私有 const；
  创建后才允许调用，不读取可变 function.name，不改变 CFG/SSA/cell 归属。
  选择器每个 owner 扫描一次参数、捕获、模块和表达式，计入字符比较与预算；
  新增分配沿用既有资源耗尽排除契约，未增加宿主假设。规则版本 28。
  三个 profile 的 60 个名称/callable/直接或回调变体生成并执行 JS；另有 12 个
  同名全局 getter 变体、2 个子函数全局名字与父函数同名的窄/宽编码函数族，
  验证异常顺序、身份、重入、name 描述符、length、可构造性及 PC 映射。
  参数同名与非法 form/evidence/const/contract 由 AST 单测覆盖，不冒充文件级证据。
  `function_tree_test`、`binding_plan_test`、`tree_structure_test`、`tree_globals_test`、
  `family_enclosure_test`、`evaluation_plan_test`、`constant_group_test`、
  `computed_store_test`、`source_route_test`、`value_created_family_test` 共 10/10 通过；
  对应目标及 `abc_semantic` 构建成功。未运行 RSS/基准/全语料或 Ark VM 差分。
  缺失名字元数据、上游边界外 Unicode、method/async/derived 身份恢复仍未完成；
  本项取代下方规则 27 时空/非法名字、普通 arrow 与命名冲突仍拒绝的历史状态。
- LDFUNCTION 普通命名函数已端到端贯通：BindingPlan 为实际 owner 分配一个
  FunctionSelfBinding，位于参数作用域之外、捕获/模块作用域之内；表达式森林
  降为 SymbolExpr，结构/flow/operand/ABI 独立核验，统一 printer 输出命名函数
  表达式。没有函数体 self let，没有更改 CFG/基本块或环境 cell 的归属。
  ABI 分开统计 frame_function_reads/self_references，明确报告 FrameIdentity；
  创建点继续核对 runtime name，原始 PC 映射到精确 self 标识符区间。规则版本 27。
  三个 profile 的 12 个直接/回调/覆盖 ABI 副本变体执行通过；另有 4 个名称边界
  正例与 6 个非法/空名或 raw arrow 拒绝用例。运行检查外部变量重绑定、修改
  function.name、construct 返回、回调异常及重入；两种宽度的闭包写入样本验证
  父函数身份捕获、多 activation、错误 this/显式参数、key 异常与 setter 重入。
  AST mutation 拒绝参数替换/遮蔽、缺失或可写 self、错误 scope、非法名字及
  不支持的 callable。新增端到端测试先失败于 PrepareCreated，再实现并通过。
  `function_tree_test`、`binding_plan_test`、`tree_structure_test`、`tree_globals_test`、
  `family_enclosure_test`、`evaluation_plan_test`、`constant_group_test`、
  `computed_store_test`、`source_route_test`、`value_created_family_test` 共 10/10 通过；
  对应目标与 `abc_semantic` 构建成功。旧 self 拒绝断言已改为精确非法绑定诊断。
  未运行 RSS/基准/全语料或 Ark VM 差分；空/非法名、现有标识符边界外的 Unicode、
  参数/全局命名冲突及 arrow/method/async/derived 的完整身份恢复仍未完成。
- LDFUNCTION 已接入公共 FrameFunctionLoad -> ValueFrameFunction：按 pinned
  GetFunction 读取独立 frame.function，无寄存器输入，只写 acc，正常结果为
  Defined 语言值；不把可写 ABI function 副本或函数名称当作运行时身份。
  三个 profile 验证无 ABI slot、覆盖副本、malformed 操作数及 IR 身份篡改；
  先观察到缺少语义提升的失败，再实现并通过。该前置提交中 self scope 尚未接线，
  源码求值与 AST 明确拒绝该操作，六个文件级直接/回调变体验证不误发 Complete。
  `value_import_test`、`constant_group_test`、`evaluation_plan_test`、
  `source_route_test` 共 4/4 通过；对应目标及 `abc_semantic` 构建成功。
  规则版本 26；未运行 RSS/基准/全语料或 Ark VM 差分。本项不计为 LDFUNCTION
  源码完成；后续 self scope、创建点身份、名称/遮蔽、统一 printer 和端到端执行
  验收顺序已写入 [第一组设计](constant-group.md#current-function-identity)。
- LDTHIS 已对初始化的普通 callable owner 贯通 FrameThisLoad、值域证明、
  ValueFrameThis、ThisExpr、独立语义/ABI 检查与统一 printer；不依赖可写 ABI
  receiver 副本。FrameReceiver 前提随输入证明保存，发布时复核 owner；无 owner、
  raw arrow、derived、async 或存在 SuperRange 的方法不自动获取该前提。
  三个历史 profile 共 12 个直接/回调/覆盖 ABI 副本变体实际生成并执行 JS，
  检查 call/construct、primitive/null/undefined/Proxy receiver、异常身份及 PC 映射。
  另有 3 个 raw arrow 拒绝反例和 narrow/wide 父 receiver 词法捕获用例，覆盖
  多次 activation、错误显式参数、不同子调用 receiver、抛异常 key 和 setter 重入。
  ABI 单独记录 frame_receiver_reads；源码证据规则升至 25，旧 receipt 失效。
  `source_route_test`、`constant_group_test`、`computed_store_test`、
  `value_import_test`、`function_tree_test`、`function_input_test`、
  `value_operands_test`、`value_simplify_test`、`evaluation_plan_test` 共 9/9 通过；
  对应目标与 `abc_semantic` 构建成功。未运行 RSS/基准/全语料，未作 Ark VM 差分；
  第一组整体未完成，derived/arrow/async receiver 相位证明仍待实现。
- receiver owner 前置接线完成：PreparedFamily 在根函数输入验证前传入真实
  FunctionContext；子函数在读取前先解析实际 creation occurrence，随后走相同入口。
  FunctionInput 保存不可变 creator/PC/class property/callable/strictness/frame
  标识，输入前拒绝错误 identity/target/frame，构建源码树时再次核对实际 owner。
  此前置提交保留 ownerless 诊断入口，不自动增强 receiver 值域；源码接线见上项。
  同目标两个创建点不能串用的正反例、父子 owner 对应及现有闭包输出均通过；
  `function_input_test`、`constant_group_test`、`computed_store_test` 共 3/3 通过。
  fixture、输入测试及 `abc_semantic` 构建成功；补齐旧输入测试缺少的 algorithm 包含。
- LDTHIS 新增独立 FrameThisLoad 语义操作：读取 frame.thisObj，不读取/别名化
  可写 ABI receiver 寄存器；保留 acc 写入、无普通语言效果和 Unknown 值域。
  三个历史 profile 验证 ABI 副本覆盖不能证明 frame receiver 初始化，缺少 owner
  证明时返回它仍以 InternalValue 拒绝；malformed 操作数也有反例。
  `value_import_test`、`constant_group_test` 2/2 通过，`abc_semantic` 和 fixture
  构建成功。该提交仅完成语义前置，不计入 LDTHIS 源码完成量；后续 owner 输入
  与值 IR/ThisExpr 接线见上两项。
  具体集成顺序已写入 [第一组证据](constant-group.md)，不新增构造器专项 writer。
- DEBUGGER 已贯穿公共 SemanticOperation、ValueOperation、求值计划、独立
  DebuggerStmt、语义/来源校验与统一 printer。它保留 acc、无结果，有独立有序
  观察效果，不允许按 NOP 或纯操作删除；没有增加专项 body writer。
  三个历史 profile 的 36 个函数变体新增两次 debugger，Node inspector 实际验证
  暂停/回调/暂停及异常跳过第二次暂停；原始 PC 对应精确的 `debugger;` 区间。
  malformed 操作数、伪造 AST 语句、getter 跨观察点移动均有回归。
  `abc_semantic` 与相关测试目标构建成功；value_operands、function_tree、
  evaluation_plan、value_simplify、value_import、constant_group、computed_store
  共 7/7 通过。不是 Ark VM 调试协议/寄存器视图等价证明，第一组仍未全部完成。
- 第一组普通常量端到端证据已补齐：LDNAN/LDINFINITY/LDUNDEFINED/LDNULL/
  LDTRUE/LDFALSE，三个历史 profile、直接返回/跨回调保留两种形态，共 36 个
  文件方法 fixture，经 PreparedFamily/FamilyEmission 到源码执行；检查 PC 0
  生成区间、名字遮蔽、参数/receiver、调用次数、异常身份和原值返回。
  另有 6 个 LDHOLE 返回/传给宿主的拒绝反例。未修改生产语义，未扩大接受域。
  `constant_group_fixture` 构建成功，`constant_group_test`、`constant_object_test`、
  `isa_catalog_test`、`computed_store_test` 共 4/4 通过。不是 Ark VM 差分或真实
  应用验收，第一组尚未完成；边界与剩余项见 [第一组证据](constant-group.md)。
- 全 ISA 计划已启动：新增类型化 `IsaGroup`/`IsaSpec`，固定 YAML 的 19 groups、
  332 格式均进入不可变清单；显式生成边界拒绝未知/缺失/顺序变化的 group，
  运行时 Opcode 二分查找、不按字符串分派、不分配内存。清单不授予源码发布能力。
  新增全格式解码、acc 标志、全部截断长度、归组数量和未知 opcode 测试；
  独立 YAML 副本的未知 group 生成反例通过。第一组原有 18 条专项语义测试
  现与清单交叉检查，防止漏项或重复项冒充覆盖。
  `isa_catalog_test`、`constant_object_test` 构建成功；两项新 catalog 测试、
  两项 access catalog 测试、`constant_object_test`、`instruction_test` 共 6/6 通过。
  下一步是第一组逐指令的通用 IR/AST/源码证据与缺口，不是宣布 332 格式已反编译。
- computed Set 集成测试增加窄/宽格式的捕获箭头函数：父函数创建词法环境、
  子函数读取并更新捕获 cell 后写计算属性，共用 PreparedFamily/FamilyEmission。
  独立源码 oracle 比较多次调用、独立激活、key 抛出和 setter 重入的事件顺序、
  异常身份及最终值；检查子函数原 PC 14 的生成区间、箭头 prototype 和 arity。
  修正 fixture 提前 unlink 导致 ctime 改变的问题，保留生产文件身份检查。
  `computed_store_fixture` 构建成功，`computed_store_test` 1/1 通过；
  不代表循环环境、兄弟闭包、generator/async 或完整异常路径已覆盖。
- L0 新增 pinned ISA 访问清单：146 个编码格式、21 个 typed family，包含 wide、
  deprecated、runtime 前缀；同时纳入 MOV/FLDAI、环境建立及 named definition 依赖。
  生成阶段校验显式边界映射的遗漏、重复和失效项，运行时不按指令名称字符串分派。
  清单及查找不分配内存，查找 O(log N)。这是编码归属，不是 lift/AST 支持证明；
  profile/实际 route 的分阶段证据接线仍待完成，不把 146 项写成已支持反编译。
  独立 ISA 副本加入未知 LD 时生成失败，原输入未改动；细节见模板说明。
  `access_catalog_test`、`access_catalog_guard_test`、`instruction_test`：3/3 通过；
  `abcd_analysis` 与两个 C++ 测试目标构建成功。未运行性能或 RSS 测试。
- 已开始执行 [访问恢复方案](access-recovery.md)，本次为 L1 computed Set，不是整个
  LD/ST 家族完成。`STOBJBYVALUE` 窄/宽格式进入同一 `ValuePropertyWrite`，保留三个
  输入、原始 PC、效果和 acc 不变语义；共用 AssignmentExpr/ComputedKey 与 printer。
  求值 shape 增加 typed ReferenceBoundary，限制 RHS 跨引用/key 转换的内联；独立
  verifier 复核边界、三个操作数及 strict 语义。未改 CFG、未增加专用 writer。
  受控字节码 fixture 经 PreparedFamily/FamilyEmission 生成可执行 JS，检查原 PC 4
  的源码区间；Node oracle 覆盖 RHS/key/setter 顺序、Symbol、null、三类抛出、只读
  Proxy 和 setter 同步重入。它不是 pinned Ark VM 差分证据，不验证 VM 错误消息一致性。
  恢复测试时修正了此前模块存储规格中的 variant 比较编译错误及错误的常量传播假定。
  L0 全量 typed 覆盖目录、模块发布、环境/捕获组合、generator/async 仍未完成；
  RHS 保留的快照是当前语义约束，不以删尽临时变量作为完成标准。
  `value_import_test`、`value_operands_test`、`value_simplify_test`、
  `evaluation_plan_test`、`function_tree_test`、`computed_store_test`：6/6 通过。
  生产 `abc_semantic` 构建退出 0；既有 pasteboard 7449/Immersive 21082 草稿仍为
  1434/160 字节且 incomplete。未做 Ark VM 差分、全量回归或 RSS 验证。
- 通用值 IR 新增 `ValueModuleWrite`，窄/宽 STMODULEVAR 共用 typed 模型，
  保留模块槽、单个 stored-value 操作数、原始 PC、正常/抛出前模块写效果及异常出口。
  不产生虚假的 accumulator 结果；复制/死值简化禁止删除模块存储，求值计划保持顺序。
  源码 AST 尚未支持模块写，构树和独立对应检查均明确拒绝，不误输出普通局部赋值。
  生产双目标构建退出 0。ABC-only 入口 7283 当前在 PC 101 拒绝，已越过 PC 11
  模块存储；7449 四成员和 Immersive 21082 单成员草稿仍生成，7477 record 片段及
  PC 194/11 初始化来源仍保留。窄/宽边界、accumulator 保持、输入篡改和简化保留
  规格已写但未编译、未运行。实际 record 绑定、声明/更新 AST 与 TDZ 证明仍待接入，
  本项不是 T07/T11 完成，也未解除 7449 的模块发布 guard。
- 模块存储输出现保留类型化初始化状态：TDZ、位置未知、原始首次存储 PC。
  仅成功写出首次声明后提交位置，后续更新不覆盖，不把 PC 0 当作缺失。
  两条 record writer 均传入实际指令位置；`BuildRecordSource` 成功时将模块表
  移入源码产物的不可变 owner，并由 record-slice 流式输出，失败不发布。
  ABC-only record 6897/entry 7283 的 slot 0/1 初始化分别为 PC 194/11，
  独立读取原指令确认均为 STMODULEVAR；7477、7586 片段区间未变。
  生产双目标构建退出 0；新增规格未运行。这是声明来源，不是模块读取等价证明，
  7449 的发布 guard 保留，实际名字绑定、TDZ 异常和 live-write 义务仍需闭合。
- 函数族作用域检查新增模块引用：模块 scope 中的 alias 不再能掩盖父函数局部变量
  或其 TDZ；仍按实际创建位置区分祖先与兄弟 block。跨成员核对 typed 模块目标与
  名字的一致性，使用排序索引和 Euler 区间，无逐引用祖先扫描，不改变 CFG。
  这只是作用域证明；destination 对缺失模块执行上下文显式拒绝，发布 guard 保留。
  生产双目标构建退出 0；ABC-only 7449 四成员草稿仍为 1434 字节，Immersive
  21082 草稿仍为 160 字节，7477 record 片段仍能生成。新增规格未编译、未运行。
  已核对干净的 pinned v7 runtime 模块读取路径。7089 的本地导出 `PageA` 与
  record 实际 `$local0` 绑定、原存储点处的 `let` 初始化需要连接，不能照抄 metadata
  名字。具体下一步见 [模块别名作用域与发布边界](family-enclosure.md)。
- 普通函数创建点已接入真实 record 装配：`FunctionCreationSelection` 按 caller/PC
  选择，同一调用不能同时选择类方法；输出必须保留对应函数族片段，否则整次拒绝。
  适配层复核实际创建操作数、借用 record 的完整绑定上下文，不新增 body writer，
  不允许选中路径失败后回退；类型化拒绝保留到 record 返回值。
  ABC-only 7117、7477 分别在 [975,1018)、[1139,1202) 输出，实际创建点为
  7089/PC 55、7089/PC 102；7586 类片段仍为 [2307,2464)。所有 record 仍明确不完整。
  7449 的真实阻点已由调试确认：成员 1（7089）的 `FamilyTreeError::kModules`。
  仅提供 globals 不能解决；模块 alias 的 TDZ/循环初始化/作用域发布义务仍未闭合，
  不能把诊断 metadata alias 当成执行证明。新增适配层/record 规格未编译、未运行。
  生产双目标构建退出 0；未运行测试、生成 JS、基准或 RSS。
- 普通/箭头函数的 `--value-source` 不再在准备后固定拒绝输出，现经过
  `PreparedFamily::PrepareCreated` 与同一个 `FamilyEmission`。结果显式要求
  strict enclosing destination；缺失 global/capture 证明仍拒绝，不插入伪造上下文。
  ABC-only 7117、7477 已输出函数表达式片段，各保留 5 条指令归属、record=6897；
  7449 仍拒绝且无源码，根收据保持 Decoded，族内失败单独报告，不虚报阶段。
  普通表达式与 record 片段共用逐成员位置/收据序列化。CLI 回归规格已写、未运行。
  生产 `abc_semantic` / `abc_structure` 构建退出 0；测试、JS 执行和 RSS 仍暂停。
  这不代表完整 P3、多应用运行验收或 record/module 完成。
- selected-method 生产入口现已切换到 `PreparedFamily` / `FamilyEmission`，
  核对实际创建点的目标、种类、length、placement 和名称；拒绝后不回退旧函数体。
  函数创建索引每次选择只构建一次并计费；原始 record 身份随逐成员收据保留。
  类型化失败投影保留嵌套资源/工作限制和最深原始指令 PC，不解析错误文本。
  生产双目标构建退出 0。ABC-only 7559 的 [2638,2924) 已实际走函数族 emitter，
  record=6897，单成员、43 条指令归属；7586 为 [2307,2464)、21 条归属，
  输出字节及收据与独立入口一致。独立 7559 入口缺上下文而拒绝，不能作为对照。
  Immersive 21082 仍在外围 record 装配 PC 159 拒绝；不冒充整份 record 成功。
  新增失败映射与更新后的 record 规格未编译、未运行；整份 record 仍不完整。
- record 的 `FunctionSegment` 现可持有单函数或完整函数族，共享原有搬运、捕获
  位置重定位、内存计费及保留检查。整族只移动一层外部区间，内部 AST/收据/位置不拆散。
  record-slice 序列化已支持逐成员收据和原始指令位置表。
  此前该运输层提交尚未切换 selected-method；现已由上项生产入口接入。
  新旧头文件接合暴露的类型重名已修正为 `ReceiverEntryBinding`，未改变 receiver 语义。
  双 payload 的重放、捕获前缀、预算与 200/201 规格已更新，仍未编译、未运行。
  生产双目标构建退出 0；ABC-only 7559 仍为 [2638,2924)、规则 24 的单函数
  record 片段，Immersive 21082 类成员草稿仍为 160 字节、不可执行、不完整。
- 新增[拥有型函数族输出](family-emission.md)：准备接口保存逐 occurrence 的
  TreeVerified/Incomplete 收据；`FamilyEmission` 在实际输出、目标语法/作用域和
  全指令位置检查成功后才产生逐方法 Printed/Complete 收据，并持有树与位置表。
  类成员和函数表达式目的位置采用枚举，不允许互换。证据规则升为 24。
  这是代码实现，尚未运行新路径测试或生成 JS；CLI 草稿和整份 record 不自动升级。
  记录装配整合、异常/async、构造器与原始结构 gate 仍未完成。
  生产 `abc_semantic`/`abc_structure` 构建退出 0。ABC-only 7559 的既有方法片段
  仍为 [2638,2924)、Printed/Complete，规则 24；7449 的四成员草稿仍为
  1434 字节、不可执行、不完整。这不构成新 `FamilyEmission` 的执行验收。
- 类方法与普通函数已共用 `PreparedFamily::Prepare`，仅创建上下文不同。
  共享 printer 支持实际类方法根节点，保留 prototype/static、名称和参数范围；
  后代仍是普通/箭头函数表达式，不生成另一套 body writer。
  ABC-only Immersive 21082 现输出 `setInitiallyProvidedValue(params)` 类成员草稿，
  21 条原始指令有归属；pasteboard 7449 仍为四成员、1434 字节函数表达式草稿。
  生产构建退出 0；两份产物仍不可执行、不完整。新增类路径规格未编译、未运行。
  构造器、类整体、异常及 async 的未完成条件没有放宽。
- 新增[原始指令源码归属表](instruction-sources.md)：整族构建逐条原始指令核对
  AST 节点、锚点、默认参数签名或已证明纯消除的归属；拒绝缺失、重复及 PC/版本错配。
  线性索引随不可变准备结果保留，草稿 JSON 流式输出 `instruction_sources`。
  纯消除使用 `range:null`，不伪造生成位置。ABC-only 的 7449 四成员覆盖表长度
  为 36/66/5/5，根签名 6 项，草稿仍 1434 字节；Immersive 21082 仍可构建。
  两者仍不完整、不可执行、退出 1。生产构建退出 0；新增规格未编译、未运行。
- 新增 [不可变函数族准备](prepared-family.md)：`PreparedFamily::PrepareCreated`
  从实际 ABC 创建点重新贯通完整 family 输入、默认值、AST、ABI、捕获/写入与
  外部作用域检查，再保存 `unique_ptr<const Storage>`；不接受任意树或统计作为证明。
  `PrintDraft` 在输出前重新检查严格模式与目标作用域，调用现有共享 printer。
  仍输出草稿，不授予 executable/complete；CLI、module 初始化及整份 record 发布未升级。
  简单/cell/嵌套写入、移动所有权、输入文件释放后输出与预算规格已写但未编译、未运行。
  生产构建退出 0；测试、JS 执行和 RSS 保持暂停。
- 新增 `VerifyFamilyDestination`：复用函数族逐创建点 scope 检查，同时核对调用方
  证明完整的外部 `GlobalScope`。缺少 context 只允许没有 global read 的函数族；
  外部 record/import/parameter/class/self/realm 绑定不能遮蔽后代全局名。
  有序外部索引借用并按比较长度计费，不按子函数复制或扫描整个 record。
  新接口尚未接入不可变发布 owner，诊断构建仍只做内部 scope 检查，不扩大覆盖率。
  外部绑定类别、缺失上下文、目标作用域变化与预算规格已写，未编译、未运行。
  本次生产 `abc_semantic`/`abc_structure` 构建退出 0；不据此声称运行验收通过。
- 单函数与文件函数族现共享箭头函数 receiver ABI 拒绝规则：动态 receiver entry
  或 owner-local `ThisExpr` 不能直接生成词法 `this`；外层 this 仍须显式捕获绑定。
  文件函数族保留各成员实际 `FunctionAbi`，不再检查后丢弃；它仍是可变构建产品，
  不是可转移的发布证据。新增普通/箭头 receiver 与 ABI 保留规格未编译、未运行。
  生产 `abc_semantic`/`abc_structure` 构建退出 0；ABC-only 的 pasteboard 7449
  四成员草稿、Immersive 21082 单成员条件树均仍可构建，退出 1，
  `source_complete=false`、`executable=false`。未执行生成 JS 或 RSS 测量。
- 整族写入域检查已接入：逐个原始 lexical store 核对 SSA Language 值域及残余
  AST cell 写入位置；仅允许已有默认参数证明迁移的 store 不再出现在函数体。
  所有创建屏障都必须由实际子函数或原始值对象证据覆盖，并重新核对捕获边。
  7449 统计为 3 个原始 store、1 个签名迁移、3 条函数创建边、2 个原始值对象；
  该样本没有捕获 store，不能代表捕获写入的运行验收。新增“子函数写父 cell”
  文件级 fixture 与缺失写入/子函数、预算规格均未编译、未运行。
- 文件函数族构建现新增内部 scope 遮蔽检查。结构遍历记录每个表达式的 scope，
  子树在真实 FunctionExpr 创建点接入父 scope，而不是把父函数全部局部变量
  都视为可见。Euler 区间与名字区间合并保留兄弟 block 边界，并计入 TDZ 绑定。
  它不改变 CFG/basic block，不补造外部 record/module 环境，也不证明捕获写入。
  本次 ABC-only 检查：7449 四成员草稿仍为 1434 字节；Immersive 21082 的
  单成员条件 AST 仍可构建。scope/深层祖先/200/201/预算规格未编译、未运行。
- 新增 `--family-source-draft`，保留 `--conditional-family` 不输出源码的契约。
  7449 的四成员 AST 已通过同一个迭代 printer 输出嵌套函数草稿；事件栈携带
  owner 和缩进深度，各成员保留表达式/语句/参数/属性/锚点的整份草稿绝对 UTF-8 区间。
  草稿不是发布收据，`source_complete=false`、`executable=false`；外层
  strict/global/module 绑定及传递前提仍未闭合。仍有 phi/效果快照临时变量。
  CLI 现通过 `draft_positions` 逐 owner 流式导出完整的现有 origin DAG、共享
  part 表、节点区间、零宽锚点、操作归属及签名迁移表。与单函数路径使用同一个
  序列化实现，不展开传递 PC 集合，也不从 JS 反推来源。源码缓冲有独立 1 MiB
  诊断上限。这些表是草稿的来源信息，不证明全指令覆盖或可执行语义等价。
  当前 ABC-only 输出共 1434 字节，四成员范围为 [0,1433)、[325,1368)、
  [1025,1079)、[1255,1329)。G2 的 7559 record 片段仍为 [2638,2924)、
  Printed/complete、规则 23；只检查了产物，未执行生成 JS。
  四成员 origin 数为 [42,84,5,5]；根签名中的 PC [16,20,21,24,26,31]
  均关联到实际生成的 `$arg0 = null` 参数范围 [29,41)。新增 CLI 映射规格未运行。
- 运行时名字已从实际创建上下文传入共享 AST。7449 四成员分别保留
  `PageABuilder`、空名、空名、`paramsLambda`；统一 printer 用计算属性的
  NamedEvaluation 命名，不新增 self 绑定、不依赖可变全局 intrinsic。
  该包装使用已批准 V2 的资源耗尽差异排除；V1 在输出前拒绝。更自然的具名表达式
  仍需证明不改变名称解析；当前没有将包装美化冒充作用域证明。
- `cmake --build build --target abc_semantic abc_structure -j 2`：退出 0。
- 新增 `--conditional-family`：只读取真实 ABC 的函数创建族、逐创建点绑定与捕获，
  不输出源码、不授予 executable/complete 收据；诊断模式退出 1 是预期行为。
- pasteboard `modules.abc` 方法 7366：`family_prepared=false`。原 PC 24 拒绝已定位并修正：
  捕获屏障包含缓冲对象，不等于函数边。共享对象解析确认 PC 24、180 只含原始值，
  保留对象证据及原始屏障，随后进入创建 PC 219 的子函数 7421；在其 PC 0 的异常
  区域输入处拒绝。完整函数族仍未成功，下一层为既有 T09/T10 依赖。
- Immersive `modules.abc` 方法 21082：输入及条件 AST 准备成功，单成员、21 个操作、
  10 个表达式、无子函数；pasteboard 7559 条件 AST 为单成员、22 个表达式。
  两者 `source_complete=false`、`executable=false`，不是闭包或跨应用源码成功证据。
- 上次 G2 方法 7559 的 record 内片段为 Printed/complete、区间 [2638,2924)、规则 23；
  **仅方法片段，整个 record 仍不完整**。本次未执行生成 JS。
- 新增测试规格未编译、未运行。内存预算代码不能代替整个进程的 1G RSS 证明。
- PageABuilder 7449 的真实函数族有四成员（7449、7089、7117、7477），
  三条创建边。此次接入发现函数创建的一律固定策略阻断了合法成员调用的求值组合；
  现允许顺序保持且不跨环境变化的内联，并已通过 cell 默认值证明和共享残余投影
  解决 length=0/1 不匹配。四成员条件 AST 已全部构建，表达式数为 [22,51,1,1]。
  实施约束见 [函数族默认参数迁移](family-defaults.md)，尚未发布完整函数族源码。
- 已接入入口 cell 默认值候选发现，7449 报告 `cell_default_candidates=1`。
  独立 `CellDefaultProof` 已接入，7449 报告 `cell_defaults_proved=true`。
  原 `kLength` 检查保留，现通过真实默认签名迁移满足；投影后的捕获已全族重新绑定。
  回归规格未运行，conditional AST 不等同 executable 收据。

## 设计对照审查

本次审查覆盖共享语法解析、模块/捕获别名和文件函数族读取的近期工作。

1. **方向一致**：沿用 typed IR、输入身份/修订、真实创建 PC、作用域和效果检查；
   没有引入专用 body writer、生成文本反推语义或配对源码输入。
2. **存在集成滞后风险**：前置构件增长，但真实完整函数族尚未贯通；不能继续以
   “又增加一个表或接口”代替 T07 的端到端交付。该项保持开放，下一轮必须复查。
3. **CFG/scope 边界仍须验证**：按创建 occurrence 区分 owner，不能按 method 合并
   可变状态；接入 AST 时必须重新验证工作修订、捕获映射、默认参数投影与效果顺序。
4. **接受域未扩大**：未新增宿主假设；不解除 Proxy-super 拒绝；完整 P3 的
   异常/async 依赖不得通过省略子函数、伪造函数体或旧路径补正文绕过。
5. **验证缺口保留**：当前没有 Validated 能力，也没有全样本完成或内存达标结论。

## 下一步交付顺序

1. 已修正缓冲对象与函数创建混淆；含方法/访问器的对象继续类型化拒绝，
   不能以无子函数论处。新增 0/1/2/200/201 重复池对象和方法值负例规格尚未运行。
2. 7449 四成员已串入共享 AST，后续在此基础上闭合 callable、外层作用域和发布义务；
   继续保留跨应用扩展及未支持模块/默认值形式，不跳过失败子函数。
3. 嵌套源码草稿、运行时名字、owner 事件区间与 origin DAG 已贯通导出；下一步
   闭合外层环境与不可变发布 owner。整族写入值域检查已接入，但不能据此标量化
   可变捕获或消除调用后的重新读取；继续处理有独立证明的冗余变量搬运。
4. 扩展跨应用证据；完整 P3 的 T09/T10 依赖明确保留，T07 不因简化样本通过而宣告完成。

定期设计审视已按用户要求取消；以上审查保留为历史记录，不安排下一次定期审查。
