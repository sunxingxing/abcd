# 统一控制区域演进实施计划

> **For agentic workers:** Use `executing-plans` inline, task by task. 当前项目不使用
> CC 或子代理；不得因执行本文恢复代理、定期审视、RSS/基准或引擎调度器开发。
> 每个阶段先核验设计与失败用例，再实施、定向验证、更新状态并提交。

**Goal:** 优先交付可用反编译功能：允许标记后的部分源码输出，将循环、异常、
completion 与协程逐步纳入共同源码通路；覆盖全部样本而非专用应用。

**Architecture:** 保留 ValueProgram、相位 SSA/环境分析、FunctionTree 和 printer。
以类型化图视图和 RegionPlan 替换两种控制布局的下游消费约定；先打通循环/catch
组合，再闭合 finally 与 suspension，不新增专项 body writer。最终 printer 直接消费
FunctionTree；不新增 VerifiedFunction 准入包装，DFX 配套不作为功能前置。

**Tech Stack:** C++20、现有 CMake/CTest、受控 Node.js 行为对照、锁定 Ark ISA/profile。
不引入新的运行时依赖。

**Spec:** [FunctionTree 与功能优先的部分输出](../../design/source-recovery/function-tree-output.md)。
2026-09-28 讨论后的修订优先于本计划旧版本的全验证/预算/发布门槛；这是待实施
计划，不表示现有严格入口已改变。既有设施保留兼容，不集中拆除。

基线：`1c7e44d2a23e14b91fb91ab98244f3dec355f6ba`，Rules 58 / V3。
[审计证据](../../design/source-recovery/control-composition-audit.md) 为本计划输入。
本文细化原 T05/T06/T07/T09/T10 的依赖，不取代
[全 ISA 优先级](../../design/source-recovery/isa-priority.md) 或放宽原结构 gate。
本文新增名字是**拟议内部契约**，不是已存在 API；不能据本文宣布功能已实现。

## 1. 不变式和范围

1. 原始解码 CFG/操作/PC、handler dispatch、输入身份不可被结构化输出改写。
   在工作图上做变换须保留 revision 和原始来源，失效相关证明。
2. 正常返回提交值只沿正常路径可用。异常路径继承实际 throw 时点的值、效果、
   环境和 handler；不能一律使用块末状态或操作前的全部堆状态。
   例如 getter 在抛出前已经发生的写入不能回滚。
3. `WorkBlockId`、投影节点 ID、`RegionId`、AST `StmtId`、`ScopeId` 不可互换。
   catch 参数属于 catch scope；结构嵌套不授权新词法可见性。
4. phi copies 绑定原始转移及 phase；循环入口和回边分开；每次动态转移只执行一次。
5. 禁止为匹配 AST 扩大 try 保护范围、移动 getter/call、合并不同异常值，
   或把 effectful gap 当无效果跳转走廊。
6. finally 的恢复建立在 completion 证明上，不由相似指令、源码文本或方法名猜测。
   suspension 与 completion 分离；yield/await 暂停不等于执行 finally。
7. ABI、callable、参数默认值、cell/capture、module/enclosure 的语义不得伪造。
   不能恢复的部分明确标注；不以 V3 发布证明或全 PC census 阻止其他片段输出。
   构造器也是同一通路的 callable 变体，不另建控制系统。
8. 每个新接受域都要增加实际解码到源码的测试。typed refusal 明确记录未支持项，
   不算完整支持，但允许共同 AST 输出已恢复部分。保留用户拒绝的 Proxy-super 等边界。
9. 暂缓新增 budget/work 计费、资源失败注入、通用验证包装、发布 receipt 和全面
   来源审计；现有保护保留。进程 1,000,000,000 字节为长期约束，不恢复 RSS/基准，
   不宣称实测达标。必要的语义检查和定向功能测试继续。

## 2. 数据与算法契约

### 2.1 图视图及身份

新增内部 `control_view.h/.cc`，按当前组合功能需要提供只读邻接视图和原始映射：

| 视图 | 用途 | 禁止事项 |
|---|---|---|
| Normal | 已确认正常转移的支配/循环事实 | 不能证明异常路径上的值可用性 |
| Phased | Before/Commit/Catch、dispatch、环境/SSA 校验 | 不能直接把所有异常边当普通分支来结构化 |
| Quotient | 已验证区域的节点及 typed exit ports | 不得把多个端口伪装成一个出口或布尔分支 |
| Resumption | 后续阶段的暂停点和恢复方式 | 不得把恢复输入当普通调用返回 |

新 `ControlViewId`、`ControlNodeId`、`ControlArcId` 使用项目强 ID 风格。
视图身份关联 file/method owner、value/cfg revision、profile、view kind 和 projection
revision。索引、支配结果、循环事实均携带该身份，拒绝跨视图使用。
算法须显式请求视图，无默认“忽略异常边”参数。

投影节点引用原始块或区域；投影弧引用原始边/操作级异常出口/已验证区域端口，
使用 variant，不用字符串标签。合成 continuation 无原始 PC，来源引用独立保存。
不制造假的 ValueProgram，不复制原始值表，不依赖 `blocks.size() + try_id` 编号。

### 2.2 RegionPlan

新增 `region_plan.h/.cc`，采用 arena、连续子节点范围、显式 parent、出口表和来源表。
拟议节点语义：

```text
Region = Sequence(children)
       | Leaf(original_block_or_phase_range)
       | If(predicate_site, true_child, false_child, optional_join)
       | Loop(header, body, continue_port, exit_ports)
       | Try(body, optional_handler, optional_finalizer_proof)

Exit = Normal(target_port)
     | Return(value_reference)
     | Throw(exception_reference, dispatch_reference)
     | Break(loop_region)
     | Continue(loop_region)
```

这是一份类型规格，不允许用上述文本作为运行时语义。C++ 使用具名结构和 variant；
没有有效 value/target 的变体不设占位字段。`If` 的 predicate 保留原求值位置。
原 handler 的 TryId 与新结构 RegionId 有显式关联，不能根据新 parent 计算 dispatch。
`Try` 的最终语法是同一个 TryStmt；TryCatch、TryFinally 可由字段组合表达，
不为了名称统一创造重复节点。

每个出口保存原始转移证据和状态引用：值/phi 输入、effect、environment、origin。
多出口是边界端口集合，不意味着复制完整状态向量到每个祖先。
Leaf 只拥有要发射一次的操作范围；其他节点拥有结构和证据。
不可达 Commit 只按现有显式 throw 证明记入 census，不泛化为删除任意不可达代码。

### 2.3 组合算法和接受域

复用 [算法约束](../../design/algorithms.md) 的支配/循环、稀疏流和区间索引基础。
循环分析的邻接适配必须与 ValueProgram 存储解耦；不复制一份 ExceptionLoop。

实施分为两个互补的组合操作：

1. **区域内部结构化**：对一个 try 的 body/handler 内部子图识别分支和循环。
   以该语义入口分析可达性，对离开该区域的边记录出口，不删除或提前解释成 return。
2. **区域收缩后结构化**：把已验证 try 视为有端口的原子节点，在外围发现 loop/if。
   handler 的 continue 端口可以回到外层 loop；其异常可达性来自原 dispatch 证明，
   不是给 handler 随便增加一条普通入口边。

使用有依赖的工作队列、区域归属索引和边界摘要；不为本阶段新增逐项计费设施。
不能对每个 try 全图扫描、为每个变量重跑图算法或展开每层祖先的全部后代。
普通自然循环发现与商图循环发现使用同一内核；捕获值可用性仍由完整相位图验证。

必须专门验证两类情况：try 包含 loop；loop 包含 try 且 catch 有回边。
源异常区间与循环集合部分交叠时，不能硬塞成父子。先验证是否能通过不移动效果的
边界分裂/端口重定向得到合法嵌套，所有操作 handler 和求值次数必须不变；不能证明
时记录 typed unsupported overlap，并保留原图证据；不可约图暂不完整恢复，
通过缺口策略输出其他可靠片段，不伪装支持。
**本阶段不承诺全部合法 ABC 都可直接还原结构化 JS**；该覆盖缺口必须保留在清单。

循环发现不强依赖退出可达性。无终止 SCC 必须独立识别，不伪造可执行退出边来让
postdominator 算法成立。结构分析使用的虚拟出口只能作为证据节点，不能产生源码。

复杂度分开报告：原图 V/E、相位 P、区域 R、边界端口 X、phi 输入 H、输出来源 M。
索引与结构遍历目标 O(V+E+P+R+X+H+M)，排序/区间及支配步骤注明自身成本；
不要承诺整个恢复过程严格线性。实际稠密边界按 X 分析，不隐藏在 R 内。
避免嵌套收缩反复扫描；保留复杂度分析，不以新增 work 计数器和预算规格阻塞功能。

### 2.4 功能检查与后移的独立验证

通用 `region_verify.cc` 及全面证书框架后移，不作为 C03 的前置交付。
以下与控制组合直接相关的检查随构建/现有 TreeFlow 和定向测试接入；某区域无法
满足时成为恢复缺口，不阻止其他部分输出。将来扩展独立验证器时，不以重跑 builder
并比相等为唯一依据，不共享决定归属和出口的核心推断过程。

- 从原图及 handler 表逐项核对正常/异常转移的 coverage；不能只遍历计划中的边。
- 校验单入口或已证明的入口集合、兄弟无非法交叠、子区域 owner、全部出口目标。
- throw 指向原 handler 或方法外；normal result 不进入 Before/Catch 的不可用位置。
- phi copy 在真实入口/回边/异常转移上各自匹配，缺失/重复均拒绝。
- Try catch 参数绑定、作用域、cell 动态实例、捕获创建点与原证明一致。
- normal/throw/return/break/continue 的路径与效果顺序经原转移证明逐项匹配。
- 所有推导都有 revision/owner/profile；同数值不同身份的 ID 不能互换。

保留错 target、漏 handler、copy 放错边等当前功能负例；全套篡改计划/随机图验证
框架后移。生产路径不能依赖逐节点 DFS。

### 2.5 AST、scope 和映射

FunctionTree 仍是源码层唯一 AST。基本块不等于 BlockStmt，结构 region 不等于 scope。
直接输出入口不要求 VerifiedFunction、不可变工厂或发布 receipt；基本节点安全检查
继续。复用已有 printer 增加函数前 INCOMPLETE 和区域 UnrecoveredStmt 注释，缺口
状态沿包含关系传播。不能构造完整表达式时扩大缺口，不制造值或拼接未知后继。
新增 `region_tree.cc` 仅负责区域到现有节点的发射，复用表达式、绑定、origin 和 printer。
迁移 `function_tree.cc`、`tree_flow.cc`、`cell_lifetime.cc` 的全部相关消费点，
取消“函数有异常就优先走异常 unit”的发射规则。

outer break/continue 用 AST `StmtId` 指向实际包围循环；增加类型化 label 分配表，
仅当最近循环不是目标时输出标签。校验 continue 目标必须是祖先循环；label 不能
进入别的函数，名字分配与普通变量/标签域规则分别处理。不要把 RegionId 塞进 StmtId。

每个节点/操作来源进入既有 origin DAG；合并 finally 的多个原始副本用共享来源集合，
不丢弃各原始 PC，也不为每个节点展开所有 PC。映射允许多个 PC 对应同一语法区间，
沿用已有映射能力和回归，全面原指令 census 不作为本阶段输出门槛。
UTF-8 区间与函数族 owner 保留；没有恢复的区域记录可用原 PC，不伪造完整映射。

表达式内联和临时变量消除继续经过既有 effect/evaluation 证明。除普通依赖外，
移动前后还须保持可能抛出操作的 handler、环境和求值次数；不能跨 try 边界移动调用
再凭“正常结果相同”接受。非抛出的纯操作也须检查输入在目标 phase 是否可用。
跨 await/yield 的保存值不能因普通单使用点规则而重新求值；调用可能修改的捕获值
不能当不可变 SSA 常量替代。结构化若只建视图，不重写原 SSA；若确实修改工作图，
必须更新 revision 并重算受影响分析，不能继续复用旧证明。

### 2.6 Completion 与 suspension

新增 `completion_plan.h/.cc`：区分 Normal（含 empty 与实际 JS 值）、Return、Throw、
Break、Continue。empty 是类型化不存在，不是 undefined；payload 用现有值引用。
进入 finalizer 前固定待完成 payload 和 target。finalizer 正常完成恢复待完成结果，
非正常完成覆盖它；嵌套按实际退出顺序逐层执行。

finally 候选从原 CFG、SSA 值、效果和退出转移识别；重复片段只能提供候选。
必须证明所有可达入口与出口、catch 的失败、返回值求值时点、嵌套覆盖、每次离开
所需 finalizer 的执行次数。无法证明的模式拒绝，不因打印得像 finally 就接受。

新增 `suspension_plan.h/.cc`：暂停状态、保存值/环境、resume next/throw/return 和
pending completion 分开。共同 ValueProgram 增加类型化操作，FunctionTree 增加
AwaitExpr/YieldExpr；只有协议证明通过才能省略编译器状态机。沿用旧分析中可证明
的事实，但不能绕开共同 IR/树流验证去调用旧 writer。

generator `.return()`、`.throw()`、未启动/已完成状态、finally 中 yield，以及
async 的 rejection 注入位置必须显式建模。async generator 的请求队列和 yield*
委托是独立协议证明，不可从普通 generator 支持自动推导。

增加 ValueOperation/TreeExpression 变体时，逐项更新 importer、uses/def、effect、
exception availability、liveness、simplify/DCE、capture、tree flow、结构校验、
printer 和 origin census 的穷尽处理。不能用默认分支将新操作当纯值或忽略。
每次新增操作先做最小 decoded round-trip，再开启组合恢复。

## 3. 文件责任表

以下新文件均为计划创建；生产内部头文件放 src/source，不扩大公开 API。

| 文件 | 责任 |
|---|---|
| `src/source/control_view.h/.cc` | 图视图身份、强 ID、邻接和原始来源映射 |
| `src/source/region_plan.h/.cc` | 组合节点、端口、归属、构建 orchestration |
| `src/source/region_verify.cc` | 后移：通用独立 coverage/转移验证，不阻塞组合功能 |
| `src/source/region_tree.cc` | 统一控制节点向既有 AST 发射 |
| `src/source/completion_plan.h/.cc` | 待完成方式与 finalizer 转移证明 |
| `src/source/finally_recovery.cc` | 候选识别、等价性证明、共享 finalizer 来源 |
| `src/source/suspension_plan.h/.cc` | 暂停/恢复、owner、值/环境保存证明 |
| `src/source/delegation_plan.h/.cc` | yield* 的迭代委托协议证明 |
| `src/source/tree_labels.cc` | AST 目标对应标签及其验证，printer 只读使用 |
| `tests/unit/source/{control_view,region_plan,completion_plan,suspension_plan,delegation_plan}_test.cc` | 对应功能单元及必要负例；通用 region_verify_test 后移 |
| `include/abcd/source/source_tree.h`, `tree_printer.h` | 缺口类型/附属状态、直接 FunctionTree 打印接口 |
| `src/source/function_tree.h/.cc`, `tree_printer.cc`, `family_print.h` | 保留部分构建结果、复用统一打印实现、传播不完整状态 |
| `tools/source_inspection.cc`, `tools/value_source_inspect.cc` | 将部分结果与函数前标记接入现有源码命令，不新增专项 writer |
| `tests/unit/source/control_composition_test.cc` | 解码 fixture，经全部共享层输出与映射 |
| `tests/integration/control_composition.mjs` | 组合行为 oracle；不被引擎读取 |

修改而非复制：`control_plan`/`loop_forest`/`loop_control`、`exception_topology`、
`function_tree`/`tree_flow`/`cell_lifetime`、`family_file`、`phased_method`、
`include/abcd/analysis/value_ir.h`、`include/abcd/source/source_tree.h`、
`tree_structure`/`tree_printer`/`tree_correspondence_origins` 和相关 CMake 注册。
某模块超出单一责任时按语义拆分，不建立通用 Manager/Utils。

## 4. 实施任务与停止条件

依赖链：`C00a -> C00b -> C01 -> C02 -> C03 -> C04 -> C05 -> C06 -> C07 -> C08 -> C09`。
每个 C 是可单独验收的工作包，不是假定几分钟能完成的微任务。
C00a 先交付部分输出；C01/C02 限于 C03 必要结构，可随纵向功能内聚提交。
不等待通用验证框架、全映射审计或预算配套；完整组合支持仍以 C03 行为测试为准。

### C00a：FunctionTree 直接打印与部分源码

文件：上表 source_tree/tree_printer/function_tree/family_print 与源码 CLI 消费点；
测试复用 `tests/unit/source/function_tree_test.cc`，增加本计划的 composition fixture。

接口：拟议 `UnrecoveredStmt{RecoveryIssueId}` 和函数恢复状态/问题表见设计；构建器
返回已恢复 FunctionTree 及问题，而非遇到普通 unsupported 就丢弃整个函数。
为 `PrintFunctionTree` 增加直接消费 `const FunctionTree&` 及必要输出上下文的入口，
底层复用现有事件 printer；已有 VerifiedFunction 入口保留适配，不先做删除重构。

- [ ] 先加入完整函数、已恢复前缀+缺口、依赖未知值的后续语句、未知控制后继、
  不完整子函数以及损坏 AST ID 的规格；确认当前入口无法提供所需部分输出。
- [ ] 实现最小缺口节点、构建结果保留与状态传播；不伪造占位表达式/返回值，
  操作数不全时扩大到语句或区域缺口，控制关系不明时不连接后续代码。
- [ ] 直接树入口输出函数前 INCOMPLETE、缺口处原因/可用 PC；名字或签名未知时
  明示缺失。子函数/父函数/record 状态一致，其他成员继续输出。
- [ ] CLI 复用上述结果；不要求发布收据，不增加完整验证准入，不把内部错误
  降级为普通 unsupported。已有严格输出能力和完整函数文本保持回归覆盖。
- [ ] 运行实际注册的 function_tree/打印/CLI 定向目标，部分结果只检查片段、标记、
  缺口边界和状态，不执行为等价程序。记录真实命令、更新 status 并提交。

验收：真实解码输入遇到不支持区域仍能输出可靠片段及标记；完整样例继续通过行为
测试；未恢复依赖不会作为正常语句泄漏。该交付不宣称循环/catch 已恢复。

### C00b：冻结基线与组合证据

文件：新增 `tests/unit/source/control_composition_test.cc`、
`tests/integration/control_composition.mjs`，修改 CMakeLists.txt 和 status.md。

- [ ] 保存现有正常/异常真实函数族的行为与映射基线；不把未执行历史规格算通过。
- [ ] fixture 通过真实 opcode/profile 解码，而非只手工制造 AST。
- [ ] 先加入下节 W1/W2，保留旧异常循环拒绝原因，新入口期望带标记的部分结果；
  不允许直接输出参考源码。C03 实施前启用完整源码/行为成功断言并确认 red。
  不能用“仍然不完整”或“仍预期拒绝”让完整组合验收变绿。
- [ ] 注册 `control_composition_test` 与 `control_composition_js`；C++ emitter 的 `--emit`
  输出可导入 ES module，导出测试函数集合和映射证据，复用现有测试 emitter 模式。
- [ ] 每份真实切片记录内容 hash、profile、record/method identity、原始 PC 范围、
  原 opcode 清单、期望执行分支。至少两个不同应用；找不到目标形态时报告缺口，
  不能用两个同源副本充数。应用源码只属于 oracle，不进入反编译输入。

验收：原有三项控制测试保持通过；W1/W2 的失败原因可复现且指向控制组合门槛。
提交：`test: pin decoded control composition baselines`。

### C01：明确视图和编号，不扩大接受域

文件：control_view、control_plan、control_skeleton、loop_forest、exception_topology；
新增 control_view_test，更新现有 control/exception tests。

- [ ] 先测试错 revision、错 view、同数值不同 ID 域、catch-only 入口、空/非终止图。
- [ ] 引入投影 ID 与显式原始引用；替换块数偏移及 WorkBlockId 借用。
- [ ] 抽取循环内核的只读邻接入口；正常路径适配该入口，算法和接受域不变。
- [ ] 保留 phase/catch 的原始事实；单元断言每条投影弧能回溯原边或有证明的合成端口。
- [ ] 验证既有 JS 行为和已有映射回归；不新增预算字段、计费框架或全 PC census 门槛。

验收：没有任何新循环/try 成功声明；ID/view 混用负例全部拒绝。
提交：`refactor: separate control projection identities`。

### C02：满足首个组合功能的共享 RegionPlan

文件：region_plan；迁移普通 control adapter；新增 region_plan_test。
不在此任务建设通用 region_verify/证书框架。

- [ ] 先编写手工合法计划及篡改计划：漏一条 handler 边、出口换 target、重复 Leaf、
  catch 消费正常结果、交换入口/回边 copy、跨 owner/revision。
- [ ] 实现 Sequence/Leaf/If/Loop/Try 及端口；普通和现有无环异常都能适配同一规格。
- [ ] 接入 C03 必要的边/handler/target/copy 一致性检查，失败成为该区域缺口；
  复用现有检查，不以完整独立验证器为前置。
- [ ] 验证非终止 SCC、空 continuation、return/throw-only 区域及多个异常站点。
- [ ] 保留算法复杂度说明和适用的 200/201 结构回归；计费/scaling 测量后移，
  不运行 RSS 或时间基准。

验收：当前接受域可无损表示，必要负例能定位缺口；原 control 检查保留迁移对照。
提交：`feat: build shared control regions for loop catch composition`。

### C03：首个完整控制组合交付，循环与 catch 双向嵌套

文件：region_plan、region_tree、function_tree、tree_flow、cell_lifetime、phased_method；
control_composition fixture/oracle，真实切片注册。

- [ ] 将 C00b 的 W1/W2 成功断言启用并确认失败；补 W3 的 handler 含循环。
- [ ] 按 2.3 的内部结构化与区域收缩实现组合，所有边界以端口保留。
- [ ] TreeBuilder 按 Region 节点派发；迁移 TreeFlow、CellLifetimes、入口 origins、
  AST 容量计算和 copy 放置。不能让旧异常模式吞掉 Loop 节点。
- [ ] 同一 decoded fixture 记录 IR/RegionPlan/AST/打印位置的 identity，实际执行 JS。
- [ ] 覆盖每个 mayThrow 站点成功/失败、0/1/多次迭代、catch 返回与 continue、
  失败操作旧值、抛出前副作用保留；运行两个应用的可用真实切片。

验收：W1/W2/W3 及真实切片的结果/效果/mapping 相符；ScopeId 和 per-entry cell
不因区域合并改变。仅接口单测通过不得宣布本阶段完成。
提交：`feat: compose loops and catch through shared regions`。

### C04：跨区域出口、外层标签与异常闭包

文件：tree_labels、source_tree、tree_structure、tree_printer、family_file、cell_lifetime；
扩充 control_composition，新增标签和 closure 负例到现有 source 测试。

- [ ] 先测试内层 try/catch 中 break/continue 外层 loop、多个正常出口、非法 cross-entry。
- [ ] AST ancestor 目标与 label table 接入统一 printer，验证目标/标签一致性。
- [ ] 源区域部分交叠只在完整证明后规范化；不可约及未证明交叠保留 typed refusal。
- [ ] 带异常的子函数经同一文件族 reader/tree factory；逐 occurrence 传入捕获与
  phase 环境，删除 root-only 限制前补错误 incoming capture/owner 的负例。
- [ ] W4 捕获每次 catch 的参数并延后调用；另测 per-iteration cell、同名遮蔽、
  外层可变 cell 在回调前后写入、在异常子函数中再次创建闭包。

验收：目标不是最近 loop 的跳转行为正确，闭包返回每次独立绑定且外层写入可见；
普通与异常子函数使用同一 AST/printer；失败成员标记并传播不完整，不丢弃其他结果。
提交可拆为 labels、exit ports、exception family 三个经验证的内聚提交。

### C05：typed completion 与 finally

文件：completion_plan、finally_recovery、region_plan、tree_flow、origin 对应；
新增 completion_plan_test，扩充 decoded composition。

- [ ] 先测试 W5/W6：待返回值先求值、finally 修改绑定、finally return/throw 覆盖，
  break/continue 穿过一个及两个 finalizer；计数遗漏或重复均失败。
- [ ] 实现 typed completion 与 pending 状态，不复用 opcode 的 ExceptionalCompletion
  充当 JS completion，不用数值 sentinel 编码 return/break。
- [ ] 从 CFG/SSA/effects 证明 finalizer 候选与全部进入/离开路径；覆盖副本差一个
  getter、catch 内抛出、外部 effectful corridor 和两个不同值恰好打印相同的负例。
- [ ] 统一树流验证支持有 finalizer 的 TryStmt，保留执行次序与多 PC 来源证明。
- [ ] 普通 completion/嵌套 finally/无退出循环全部经过共同 AST，无专用 writer。

验收：所有 completion 组合返回/抛出身份、效果序列、finally 次数及映射正确；
无证明的编译器模式拒绝。不要为了恢复自然源码无条件合并效果相似的重复块。
提交：先 completion 证明，再 finally 源码接入，分别验证后提交。

### C06：同模型 await

文件：suspension_plan、value_ir 及其 importer/verifier、source_tree、tree_flow/printer；
旧 async_completion 仅复用经过复核的事实；新增 suspension_plan_test。

- [ ] W7 先验证 resolve/reject，try/catch/finally 含 await、catch 内再次 await、
  pending return 被 finalizer await 后恢复或覆盖。
- [ ] 补正常/异常恢复的 ValueOp、resume 输入、effect/env 状态及原 PC 对应。
- [ ] 证明同步前缀、promise 建立边界、thenable 的 getter/call 副作用和微任务顺序；
  V3 的资源耗尽排除不授权吞掉用户 getter 的 throw。
- [ ] AwaitExpr 由共享表达式构建及 flow 检查生成，reject 在正确位置进入 handler。
- [ ] 跨 await 的可变捕获/模块 live binding 在恢复后重新读取，不能缓存成旧值。

验收：结果/异常身份、效果与微任务事件序列、closure 及 mapping 相符；
禁止把旧 async writer 输出包装成新通路成功。
提交：`feat: recover await through shared suspension proof`。

### C07：同模型 generator/yield

文件：suspension_plan、value_ir/importer、source_tree、tree_flow/printer；相同测试入口。

- [ ] W8 先测 next(value)、throw(value)、return(value)、完成后/首次恢复以及重入。
- [ ] 实现暂停点活值/环境及 resume kind；yield 保持活跃 try 和 pending completion。
- [ ] 证明生成器 callable 属性与现有 ABI/函数族约束一致，不能改成普通可构造函数。
- [ ] finally 内 yield 后继续/throw/return；延后捕获绑定仍指向正确环境实例。

验收：每次迭代 value/done、对象身份、异常、日志和 finally 次数相同；
暂停不提前执行 finalizer，生成器恢复后完整 PC mapping 保留。
提交：`feat: recover generator resumes through shared regions`。

### C08：async generator 与 yield* 协议

文件：delegation_plan、suspension_plan、source_tree/flow/printer；新增 delegation_plan_test。

- [ ] W9 测并发排队 next/return/throw、await rejection、finally yield 与排队顺序。
- [ ] yield* 覆盖同步及异步委托、缺失 throw/return、非对象迭代结果、done getter
  抛出、return getter/call 抛出、IteratorClose 与既有 throw 的优先级。
- [ ] 协议候选只由 typed operations、CFG 和已锁定 profile 的规范识别；不同协议
  不共享未证明的 cleanup 优先级。普通 for-of closing 和 yield* 不按文本归一。
- [ ] 子协议未通过时保持 typed unsupported，不能降级普通 yield 后算支持。

验收：委托调用次数/顺序、结果、抛出身份、排队、finally 次数与来源全部核验。
提交分为 async-generator 和 delegation 两个功能单元，不一次扩大全部接受域。

### C09：退役重复控制消费并更新覆盖清单

文件：旧 control/exception adapter、function_tree、tree_flow、cell_lifetime、
family_file、各目标 CMake、status.md、全 ISA/corpus 的既有覆盖报告。

- [ ] 搜索生产消费者，消除按普通/异常选择独立控制发射的逻辑。
  异常 dispatch/SSA/env 的专门分析继续保留；不是要求删除所有 Exception 类型。
- [ ] 旧构建路径仅保留有明确差分价值的测试入口；有生产调用则尚未完成退役。
- [ ] 每个支持项记录解码格式、profile、IR、区域、AST、完整/部分输出与组合测试，不按 opcode
  已识别就标记反编译成功。19 groups/332 formats 的总目标保持。
- [ ] 对两个实际 corpus 根按现有持久分类索引选取多应用定向切片；静态 ABC 重用
  有效 hash/检测版本，不把未知版本或解码错误归类 static。
- [ ] 留出应用不参与模式调优；全语料/RSS 暂停期间只能报告定向验证，不能结案 T12。

验收：新接受域共用区域/AST/printer；基线接受域不退化；所有拒绝可定位到原 PC
和 typed reason；原结构 gate 和未完成组清单仍诚实可见。

## 5. 可执行行为规格

以下 JS 只存测试 oracle，不作为引擎输入。fixture 必须独立用对应 profile 的 ABC
指令和异常表表达这些行为；不得让 emitter 直接返回这些字符串。

```js
// W1: loop 包含 try；失败赋值保留 x 的旧值，副作用不回滚。
function loopCatch(n, step, log) {
  let x = 1;
  for (let i = 0; i < n; ++i) {
    try { x = step(i, x); }
    catch (e) { log.push([i, x, e]); continue; }
    log.push([i, x]);
  }
  return x;
}

// W2: try 包含 loop，throw 离开整个 loop，而非每轮重新进入。
function tryLoop(n, step, log) {
  let i = 0;
  try { while (i < n) { step(i); ++i; } }
  catch (e) { log.push([i, e]); }
  return i;
}

// W3: handler 内循环不是原 try 的 protected body。
function catchLoop(run, step, log) {
  try { run(); }
  catch (e) { for (let i = 0; i < 3; ++i) log.push(step(i, e)); }
}

// W4: outer continue 及每次 catch 参数捕获。
function captures(raise, callbacks, log) {
  outer: for (let i = 0; i < 3; ++i) {
    for (let j = 0; j < 2; ++j) {
      try { raise(i, j); }
      catch (e) { callbacks.push(() => e); continue outer; }
      log.push([i, j]);
    }
  }
}

// W5: return 的 payload 先固定；cleanup 可以抛出并覆盖。
function returnFinally(get, cleanup) {
  let x = 1;
  try { return get(x); }
  finally { x = 2; cleanup(x); }
}

// W6: 内层 finalizer 的 return 被外层 finally 观察/覆盖。
function nestedFinally(first, last, log) {
  try {
    try { return first(); }
    finally { log.push('inner'); return 2; }
  } finally { log.push('outer'); last(); }
}

// W7: rejected await -> catch -> awaiting finalizer。
async function awaiting(run, recover, cleanup, log) {
  try { return await run(); }
  catch (e) { return await recover(e); }
  finally { log.push('enter'); await cleanup(); log.push('leave'); }
}

// W8: yield 不执行 finalizer；return 可在 finalizer 中再次暂停。
function* yielding(log) {
  try { yield 1; return 2; }
  finally { log.push('enter'); yield 3; log.push('leave'); }
}

// W9: 委托协议、请求队列、await finalizer。
async function* delegating(input, cleanup, log) {
  try { return yield* input; }
  finally { log.push('enter'); await cleanup(); log.push('leave'); }
}
```

关键断言示例：W8 的 `.next()` 为 `{value:1, done:false}` 且 log 为空；随后
`.return(9)` 为 `{value:3, done:false}` 且 log 为 `['enter']`；再 `.next()` 为
`{value:9, done:true}` 且 log 为 `['enter','leave']`。另起实例覆盖 `.throw(token)`
和 finally 再被 return 覆盖，不混用同一实例的恢复状态。

通用比较：正常值/抛出值用 `Object.is` 或同一 token 身份检查，覆盖 undefined、null、
false、-0、NaN、bigint、symbol、对象；不能 JSON.stringify 后比较而丢失身份。
日志比较 getter/call/set、receiver、参数序列；对每个 mayThrow 站点单独注入失败。
source 语法检查不是行为验收。Node oracle 只覆盖标准 JS 子集；宿主 API 用显式测试
契约，Ark-specific 差异仍须核对 pinned runtime，不能以 Node 结果替代。

## 6. 测试命令与每阶段交付

当前基线命令：

```sh
cmake --build build --target control_plan_test loop_forest_test exception_plan_test -j 2
ctest --test-dir build -R '^(control_plan_test|loop_forest_test|exception_plan_test)$' --output-on-failure
```

C00b 注册新目标后，每次控制组合交付运行：

```sh
cmake -S . -B build
cmake --build build --target control_composition_test -j 2
ctest --test-dir build -R '^(control_composition_test|control_composition_js)$' --output-on-failure
```

各阶段新增单元目标与文件同名，例如 C02：

```sh
cmake --build build --target region_plan_test -j 2
ctest --test-dir build -R '^region_plan_test$' --output-on-failure
```

其他阶段严格替换为已注册的 `control_view_test`、`completion_plan_test`、
`suspension_plan_test`、`delegation_plan_test`，不得使用未注册 target 假定测试存在。
完整正例期望源码成功并对照行为；不支持输入期望部分输出与明确缺口，内部错误
仍需报告失败。沿用现有映射回归，不扩展全面映射门槛。功能前 red，功能后
green；若只因 fixture 格式错误而 red，不是功能失败证据。

每次提交附：设计不变式、实际修改文件、red/green 命令及退出码、真实样本身份、
结果/效果/捕获/finally 的相关证据、实际输出与剩余缺口、下一依赖；不要求新增
work/bytes 计费、发布 receipt 或全面 mapping 审计。
同步更新 `docs/design/source-recovery/status.md`。Rules 或契约仅在证据/准入语义变化
时按现有规则更新，不能把 Rules 增长当完成度。

## 7. 设计与完成门槛

实施前检查：不复制循环/异常 writer；图视图与 ID 域明确；原始 dispatch 不变；
scope/cell/copy/effect 消费点迁移完整；算法没有隐含平方扫描；证明不循环自证。
恢复不成立的区域保留缺口，不解除语义限制；其他可靠片段允许输出。

功能完成要求真实解码 -> ValueProgram -> RegionPlan -> FunctionTree -> printer
贯通，对应完整样例的行为 oracle 通过；不要求新的发布义务、预算或全来源审计闭合。
C00a 的部分输出以片段/标记/状态验收；后续功能未恢复部分继续输出缺口，但不能
把部分结果宣布为整个函数/record 或该控制组合完整成功。

回退策略：新接受域无法可靠恢复则输出有标记的 incomplete；既有经验证路径可在
迁移期间作受版本控制的差分基线。不得 fallback 到缺少效果/异常证明的 writer。
统一完成后删除重复的生产消费入口，不长期保留三套控制系统。

本计划的“支持组合”指列明接受域内的组合闭合，不是任意恶意/不可约 ABC 都可恢复。
无法规范化的区域交叠、未知协程协议和历史版本差异必须继续单列；不降低最初
通用引擎目标，也不把本阶段边界悄悄变成整个项目的最终范围。
