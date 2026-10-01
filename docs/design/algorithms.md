# 反编译算法与性能约束

本专项设计落实根目录 [AGENTS.md](../../AGENTS.md) 的算法、类型、性能与测试规则。
属于方案审阅内容，尚未实现或测量。

## 输入规模与成本

### 正常入口的模块初始化与条件更新（2026-10-02）

`VerifySemanticSource` 为同一个正常函数的入口块收集模块 store，按 typed
slot/instruction 排序去重，保留每槽最早 store。单调块游标遍历原指令，
仅为可达、位于 store 之后的本地模块 load 附加 `kStored` 见证。它是候选：
完整值求解和所有 SSA operand 检查必须证明首个及后续 store 都写入 Language；
首个 store 之前的 raw load 仍 unknown，不能循环自证。已有 load/guard 对仍
整体保留，外部模块和异常/恢复路径不使用此见证。读取只获得 Language 域，
不会转发初值或保持常量、非 undefined、对象身份等精化。

令 S 为入口 store 数、M 为模块 load 数，新增时间
`O(I + S log(S+1) + M log(S+1))`，暂存 `O(S)`；索引在值求解 scratch 分配前
释放。见证进入现有 read ledger，空间计入已有 admission。源码对应检查逐 read
核对原 store/PC、当前 entry block/顺序、slot、live 状态及 Language operand；
沿用 SSA read 查询的 `O(log(U+1))` 成本，不逐 read 扫描整函数。

Record 声明仍按根控制序列放置。额外每槽一个字节记录是否首次声明于入口块，
另一次线性操作扫描将其 arm/loop store 规划为对原绑定的赋值；禁止条件首次
初始化或先分支写入后根声明。新增 `O(O+K)` 时间、`O(K)` 空间，O 为工作操作
数、K 为本地模块槽数，不为每次写入单独走支配图。其他支配位置的首次声明
仍是能力缺口。本项目自有实现，复用已有 normal reachability/SSA/源码绑定，
无新增依赖或外部代码。三 profile、窄/宽 slot、1/200/201/400 项、Hole/cycle/
分支/未来 store 反例及实际 decoded ESM 执行为定向证据；不是 RSS/性能实测。

用 `I` 表示指令数、`B` 表示基本块数、`E` 表示边数、`R` 表示异常范围数、
`U` 表示实际 def/use 数、`P` 表示产生的 phi 及其输入数，分别报告其成本。
集合规模超过 `kPairwiseLimit = 200` 时，避免平方及以上算法。
阈值只用于识别和阻止不合适的路径，不作为“小样本可以随意退化”的理由。

`O(B + E)` 不能写成无条件的 `O(B)`；显式输入若本身包含大量边，读取这些边
有不可消除的成本。也不能重新定义 n，把全体配对算法包装成“按输出线性”。
对真正必须产生的大量事实使用紧凑表示、懒枚举和明确预算；耗尽预算返回资源诊断。

## 初始算法选择

| 问题 | 选择 | 成本与约束 |
|---|---|---|
| 字节码解码 | 生成式 opcode/prefix 表 + 有界顺序读取 | `O(I)`；无需逐指令遍历全部 opcode 名称 |
| PC 到指令/块 | 一次建立有序 PC 索引，二分查找；连续 PC 表仅在空间预算允许时使用 | 构建 `O(I)`，查询 `O(log I)`；不能每次从头扫指令 |
| 实体与引用解析 | 强类型 ID 索引；可用紧凑数组或排序索引 | 直接索引 `O(1)` 或查询 `O(log n)`；不能依赖名称两两匹配 |
| 异常范围关联 | 有序边界扫描 + 按运行时优先级组织的活动范围索引 | 目标 `O((I+R) log(R+1) + K)`，K 为实际生成的关联；避免每条指令扫描全部 handler |
| CFG 与可达性 | 邻接表 + DFS/BFS，显式栈处理深图 | `O(B+E)`；构图与遍历不使用稠密 B×B 矩阵 |
| SCC | Tarjan 的线性 DFS 算法 | `O(B+E)` 时间、`O(B)` 辅助空间；结果按稳定 ID 输出 |
| 支配树/后支配树 | Lengauer-Tarjan，首选简单且有明确上界的版本 | `O((B+E) log B)` 级别；通过 DFS 区间判断支配关系，避免集合交迭式全图迭代 |
| DAG 区域续接树 | 逆拓扑摘要与 binary-lifted LCA；一条私有 abrupt 路径不约束普通续接，两条 abrupt 路径仍保留真实共同祖先 | O((B+E) log B) 时间、O(B log B+E) 空间。需已核对的有向无环邻接；虚拟出口仅作分析。不是所有终结路径的后支配证明，独立原边/归属验证必须核对每次转移。复用本项目 DAG 与提前终结结构化实现，无外部代码、新依赖或许可证；共享退出、嵌套 continue/return、普通/相位生成源码和篡改计划为回归，不能据此宣称任意不可约图支持。 |
| 循环嵌套 | T05 自然循环采用支配树驱动反向发现、子循环压缩与 union-by-rank；非自然区域扩展候选为 Ramalingam/Havlak | 使用紧凑父子森林，不复制每层成员；不可约环拒绝，源码循环语义仍需独立验证。选择与复杂度见 [循环发现](source-recovery/loop-discovery.md)。 |
| 循环退出分支归属 | 自然循环之后，按原始前驱计数和一致 owner 的工作队列扩展词法归属；child-before-parent、Euler 祖先区间，正常/异常投影共用 | O(V+E+L) 时间和空间。内部汇合须全部前驱到达同一 owner；外部前驱、独立入口、混合 owner、同层/子循环及正常零边端口停止。保留普通或 typed preserving 续接，允许私有 abrupt 分支；每个新归属节点的出边只入队一次，不逐层重走。原始邻接和自然循环不改。采用常规 Kahn 就绪传播及现有区域原则，自有实现，无外部代码、许可证或新依赖。1/200/201/400/800、200/201/402 嵌套及普通/相位完整源码行为对照验证正确性与计数增长；RSS/性能实测仍暂停。 |
| SSA phi 放置 | 活跃性剪枝的 IDF，参考 Sreedhar-Gao 路线 | 按真实定义/使用选择计算；不先构造所有块的完整 dominance frontier |
| 同步 generator 的直接恢复协议 | 单调指令扫描识别初始挂起与 yield 的 RETURN/THROW/NEXT 分发，保留结果/mode 定义；按有序原始 PC 重定位正常分支及用户异常表 | O(I+(B+H) log(I+1)) 时间、O(I+Y+H) 空间，B 为保留分支数、H 为 handler 数、Y 为 yield 数；共享 catch-input 分析成本另见下一行。外部进入协议内部、状态寄存器逃逸、非 false 的 done、带清理的 RETURN 路由不能按此协议恢复。仅去掉外层原样 rethrow，保留用户 catch，yield 与原 THROW 必须选中同一异常目标。依据锁定 OH7 ets_frontend/es2panda GeneratorFunctionBuilder/FunctionBuilder，自有实现，无新增外部代码或依赖。三 profile、try 内循环、catch 内 yield、200/201/402 连续受保护 yield 为完整生成 JS 的定向证据，非性能/RSS 实测；finally 的 RETURN 清理仍待贯通。 |
| await/yield 异常路径的恢复临时值 | 共用首用覆盖检查 + 控制闭合后缀的 may-read 集合 | 后缀保守包含全部指令，累计最小跳转/异常目标；目标不出后缀且无输入值读取时允许分支/循环。已识别恢复协议的局部 result/mode 读取由协议内新定义支配；二分标记外部 continuation 分支/handler 入口后才排除此类读取，不以 write 擦除其它后缀 read。两种协议通过类型化证书使用同一压缩寄存器范围线段树内核，调度建成后 O((I+A+H) log(A+1)+I log(R+1)) 时间、O(A) 空间，A 为对应暂停点数、H 为 handler 数、R 为 dispatch span 数。未知效果和外跳不能证明输入值无用；yield 只为已检查的 iterator-result 创建计入其显式读取，不使用通用 raw register effects 代替语义。协议依据固定版本 ets_frontend/es2panda FunctionBuilder::Await/HandleCompletion，自有实现，无新增外部代码/依赖；1/200/201/402 暂停点、临时值重用/覆写、固定/range 读取和正常/异常外跳为定向证据，非完整 CFG 活跃性或性能实测。 |
| def/use 与重命名 | 稀疏 use 链、支配树遍历、每绑定的版本栈 | 按实际访问和 `U+P` 计费；避免对每个寄存器重新清空/扫描所有块 |
| 去 SSA 并行复制 | 依赖索引、就绪队列和复制环临时值 | 按复制节点与依赖边处理；不反复两两搜索未完成赋值 |
| 异常相位纯操作省略 | 从效果/控制和全部 phi 输入反向标记依赖，独立检查省略集合闭包 | O(I+U) 工作、O(I) 辅助空间；不改原图、不删除 phi，PC 保留边界锚点。沿用本项目 MarkLive 策略，无新增外部代码/依赖；见 [异常投影](source-recovery/exception-control.md)。 |
| 相位复制身份投影 | 函数图路径压缩及逐节点等式核验 | O(V+I) 工作/空间；O(1) 解析每个 use，禁止逐 use 重走复制链。phi/catch 的首个快照保留，原边复制计划不变；沿用本项目 ValueSimplify 策略，无新增外部代码/依赖。 |
| 跨相位表达式 | 唯一 fallthrough 坐标、效果区间、有效 use census、最终根逆序索引与 catch 槽索引 | O(B+E+I+V+U+P+S) 工作/空间；独立按操作数顺序核验连续效果区间，每个被移动效果与最终根比较异常路由和槽来源。每个 producer 最多使用一次，槽比较按其总复制量 P 计费；无逐表达式图遍历或逐节点父链回溯。拒绝树恢复快照后，仅在原分区内再做一次要求逐边 catch 等价的有界调度；接受树不变，不进行不动点重试。沿用表达式森林及前缀/use-def 算法，无新增外部依赖。 |
| 逻辑值选择恢复 | 单入口结构化菱形与SSA phi的短路模式识别 | O(B+I+V+U)工作/辅助空间；块摘要与phi输入索引一次构建，按真实ToBoolean/分支极性匹配保留值，只组合单一RHS表达式子树。左值合并仅投影predicate/keep两用途，检查互不重叠header效果区间；有候选时额外计入一次现有ControlPlan构造/校验成本，禁止逐区域重建控制图。复用项目ControlPlan/求值森林，自行实现常规SSA短路恢复，无新增外部代码、许可证或依赖；作用域、原PC及控制边在共同树验证中保留，1/200/201/402规格和生成JS行为对照见logical_regions_test。 |
| 逻辑值与调用组合 | 表达式DAG连续效果区间组合；条件RHS作为受控原子事件 | 在现有ControlPlan成本之外O(B+I+V+U)扫描/辅助空间；单phi单join用途，当前选择不重叠header/join，原局部森林和RHS继续验证。逆序选择，独立正序区间方程验证；callee/receiver语义仍由共同CallPlan及TreeOperands证明。自行实现，复用已有SSA/控制算法，无新增依赖；logical_regions_test的1/200/201/402规模、708通用JS场景和initial_render_source_test的720配对源码场景提供验证。 |
| 跨线性段的共享条件 arm | 逆拓扑单入口谓词摘要、既有 follow-tree LCA、一次控制归属遍历 | 摘要/归属 O(B+E)，候选汇合 LCA 最多 O(B log B)，新增 O(B) 存储；共同原边 verifier 逐路径检查唯一访问、原弧、scope 及 fallback，不重跑发现。普通/投影共用，独立入口与共享前驱阻止吸收，原图不变。相位 AST 仅折叠已有归属的条件子树，无 phi copies 或独立 staging/效果语句；无 dispatch 的分支只接纳零效果子树。共同 LogicalExpr/IfStmt/printer，末端其他路径作为 alternate 保留。复用自有 DAG/SSA 算法，无新依赖/许可证；1/200/201/400 图和三 profile 完整生成源码对照、有效 mutation 提供定向证据，不是 RSS/性能实测。 |
| 表达式与 AST 输出 | 类型化 AST、大小预算、分段输出缓冲 | 按实际输出量处理；避免重复拼接整个历史字符串 |
| 字面量外部使用投影 | 正常流已验证 LiteralPlan 的接收者 use 从逃逸值 census 中排除；每 producer 缓存一次投影 | O(I+U) 工作、O(I) 辅助空间；不逐 consumer 重走 producer 链。唯一外部使用沿用原效果区间及独立求值顺序验证后才能内联，多处使用、观察屏障及跨区域仍保留快照；异常相位初始化仍固定。自有 use-chain 实现，无新增依赖；对象/数组 1/200/201/402 项及共享身份运行时对照为定向证据，非性能/RSS 实测。 |
| 模块导入发布归属 | record 级不可变 slot 表与有序名称索引，方法级校验实际别名 | 构建 O(E log E) 次名称比较及 O(T) 解码、O(E+T) 存储；比较按实际文本长度计费。方法使用 slot O(1) 定位、外层索引二分查重，O(A log(S+1)) 次比较，不逐方法复制全 import 表。E 为导入数、T 为名称总长度、A 为当前别名数、S 为外层绑定数。原生 ESM 保留 live/TDZ；复用现有 import writer，无新增依赖或外部代码。 |
| 相位对象初始化 | 唯一正常路径坐标、平坦复制身份与 canonical-use census；复用 LiteralPlan 分组 | 在原始 flow/alias 验证之外，O(B+E+I+V+U+F log F) 时间、O(B+I+V+F) 空间。F 为 own-data 定义数；一次索引所有对象使用，禁止每个字段重走 CFG 或复制链。RHS 提前可用、对象未被提前观察；跨路径和 catch 观察保守拒绝。沿用本项目初始化/唯一路径证明，无新增依赖。 |
| 缓冲对象占位覆写 | 按实际 UTF-16 键排序，二分定位最后一个同名缓冲位置；按 own-write 顺序单调选择输出位置 | 每对象 P 个缓冲属性、D 个写入，O((P+D) log(P+1)) 次文本比较及 O(P+D) 辅助空间；名称长度另计并纳入工作预算。沿用 LiteralPlan 的初始化前不可观察及 RHS 顺序证明，缓冲值必须是 primitive；不改变键插入顺序、不丢弃 RHS。无法单调安放的写入保留追加表示。独立树验证检查真实键、唯一写入映射、单调位置及最后同名项。自有实现与标准库排序/二分，无新增依赖或外部代码；2/200/201/402 项与真实 Constants 为定向正确性证据，非性能或 RSS 实测。 |

上述选择的适用前提必须写进实现与测试。算法名本身不保证具体代码达到相应复杂度。
尤其不能因为单次 IDF 计算高效，就宣称多变量 SSA 总成本必然线性。
跨变量重复访问、活跃性计算、phi 膨胀与嵌套循环扫描都要单独计数和设预算。

结构化按区域索引和 worklist 更新。优先可验证的自然结构；复杂不可约图超过
结构化工作预算时可保留正确的内部低层表示，不退回无限迭代或指数级代码复制。
内部 IR 不是最终源码交付；输出必须是保留语义的 JS/TS/ArkTS，不能以汇编替代。
无法可信输出时记录未完成，不把它计作动态样本反编译成功。
优化只处理失效的分析结果，不能每次局部变换都无条件重建整张图。

## 数据布局与测量

工作 IR 的异常环境分析复用 `AnalyzeEnvironmentSites` 有界工作队列及固定
32 位深度祖先索引。额外相位适配、OpId 投影与独立状态检查为
O(B+E+I+L*d)，空间 O(B+I+A*d)，其中 L 为词法访问/出栈数、A 为分配点数、
d 为 uint32 深度域位数；另计既有 fresh flow/支配验证成本。不为每个变量或
创建点重新遍历图，不丢弃异常边。复用本项目实现，无新增依赖/许可负担；
200/201/402 用例只验证正确性及工作预算，不声称延迟/RSS 测量通过。

相位 cell 计划复用普通 cell 的有序存储键与逐访问 census：除环境校验外，
构建 O(I+A+L log(L+1))、独立校验 O(I+A+L+C)，空间 O(I+A+L+C)。
L 为词法访问数、C 为实际存储单元数；只按实际访问建单元，不展开环境全部槽。
`PhasedBody` 按需持有结果并累计预算，不按每个 child 重新分配父图或做全图扫描。

相位 cell 初始化沿用 Lengauer-Tarjan 支配树和单次支配前序 undo-state 遍历：
异常边全部保留，环境分配与写入在 Commit 更新，读取在 Before 查询。
除既有环境/cell 校验成本外，时间 O((B+E) log B+I+C)，空间 O(B+E+I+C)。
独立验证按 witness 逐项做常数时间支配区间检查，不重跑初始化选择、不逐变量
搜索 CFG。只证明单个完成写入支配的初始化，分支合流初始化仍拒绝；不证明
源码生命周期或任意闭包写入安全。无新增外部代码/依赖，新增相位索引计入预算。

异常 cell 生命周期复用普通作用域树与 Euler 区间：除既有 cell/control 校验，
时间 O(U+B+C+L)、空间 O(U+B+C)，U 为投影控制单元、B 为工作块、C 为 cell、
L 为访问数。try 主体/handler 是有类型的子作用域，逐访问常数时间检查声明
包围关系，不逐变量重扫 CFG，不跨区域提升声明。无新依赖；预算和三版本
反例验证不代替 RSS/基准，也不声称循环或捕获生命周期已完成。

异常创建归属沿用原 `BindCreation` 区域池查询与 `CreationTreeContext` 解析，
只增加 O(I) 的 typed creation census 和 O(C log S) 的创建位置索引查询；
C 为活跃创建数、S 为已建创建位置索引大小。结果空间为 O(C+N)，N 为 context
名字存储。元数据/注解读取成本仍属于既有目标/context 解析预算，不把整个路径
笼统声称为 O(I)。原始 Before 环境用强类型节点 ID 直接索引，不为每个创建点
重扫异常图或复制环境表；200/201/4096 同目标创建点为定向回归，不是 RSS 证明。

异常捕获输入复用 `CapturePlan`，每个创建点借用其原始 Before 节点、环境索引
和每父函数一次的 `ExceptionLocals`。Q 个捕获使用的查询复杂度为
O(Q * (d + log(C+1) + log(S+1)))，额外空间 O(Q)；d 为固定 uint32 祖先深度
位数，C 为 cell 数，S 为完成写入支配区间数。另计既有初始化证明成本，
不为每个 capture 复制或重扫父图。条件 Language 写入只改变前提，不删除
异常边、不声称不可变；源码前仍须独立关闭整个函数族的写入义务。

工作捕获目标复用普通/异常共用的单元规划与逐使用校验。父/子环境、cell、
来源索引各准备一次；视图只借用，不复制 CFG。除原始捕获证明外，K 个 child
传入单元、Q 个捕获使用、P 个父单元的时间为
O((K+Q) * (d + log(P+1)) + log(C+1))，C 为 child 单元总数，d 为固定祖先域位数；
额外结果空间 O(K)，原始使用证明另计 O(Q)。通过有序存储查询及直接 origin
索引连接两个环境命名空间，不逐变量重扫图；预算拒绝不丢弃绑定或改成值快照。

共享词法写入校验使用原始 Before 索引、SSA 读区间查询和直接 IR/AST 索引：
O(N+I+O+A+S log(R+1)) 时间、O(I) 辅助空间，分别为相位节点、原始指令、
工作操作、访问、写入和 SSA 读数量。`RegisterSsa::Reads` 的两次二分查询
显式计费；不为每个写入扫描图。输入/已验证树为调用者所有，额外索引计入预算。

普通/异常共用函数族写入 gate 额外使用 O(F+I) 时间和空间建立成员偏移与原始
创建点标记，F 为成员数，I 为原始指令总量；逐成员词法写入及逐边捕获证明的
成本另按上述公式累加，不将它们隐去声称整条链路线性。异常创建点直接扫描原始
类型化指令，包括无局部 cell 的函数；不逐创建点扫描父图。FileFamily 适配和
混合输入均借用现有存储，不复制 IR；偏移、标记、signature 标记与成员检查
scratch 保守累计计入同一个 byte/work 预算，失败不跳过后代或发布不完整源码。

原始指令来源清单共用 normal/phase 稠密索引，时间 O(I+O+P)、辅助空间 O(I+O)，
I 为原始指令、O 为工作操作、P 为 AST 操作及签名放置数。phase owner 只借用
原始指令 span，不按 PC 数值大小分配，不为每条指令扫描相位图或生成文本。
原始指令到 AST ID 的清单与 printer 事件位置相连，复制消除仍保留边界锚点。

正常输入的可达域沿用 `AnalyzeEnvironments` 的逐指令 before 表，与正常 SSA 的
entry-reachable 重命名域一致。保留全部物理指令的 OpId/原 PC/typed operation；
不可达槽为 Removed + Retained origin，没有 SSA 结果、use、异常出口或源码语句。
工作 CFG 只含可达块；不可达存储范围并入前一个可达块的 arena 区间，原始到工作
块映射同时用于正常边、phi 定义和 phi incoming。不是将死块出口接到活块，也不
改变原解码图。导入与独立 correspondence 新增 O(B+I+E+V+U) 遍历及 O(B) 映射，
保留既有 operand/use/可用性验证的成本。连续死块区间各扫描一次，不逐活块扫全图。
语义 read-domain 检查为 O(I+R) 顺序扫描；既有逐指令 SSA Write 查询仍为
O(I log W)，不声称整条证明或反编译严格线性。I/R/W 是指令/read/write 数。
correspondence 的 use-chain scratch 先释放，块映射与 OperandView 共用后一阶段，
峰值为两阶段的较大值，使用原始块数计入已存在的内存额度。
共同源码清单为死槽输出 UnreachableInstruction，原始来源仍为 OriginalSite；
printer 没有语法事件，JSON 边界标记 unreachable/null range，区别于纯消除和恢复
缺口。phased 输入规则保持独立，不借普通可达域删除 handler/resume。
这是项目既有可达性/SSA 与 arena tombstone 表示的接线，无新增外部依赖或许可。
三 profile 的中间死前驱/phi remap、1/200/201/400 死尾、不可达 argument snapshot
及真正解码到共同树/位置的正反例见 semantic_reachability_test 和
source_reachability_test；配对 BreakpointType 的效果轨迹单独验证实际生成代码。
这些测试不构成 RSS、Ark VM 或整应用风格证明。

文件异常方法 owner 在既有 decode/phase/local 分析之外做常数次元数据定位、
ABI 比较、vector capacity 统计和 O(I) 原始创建屏障 census；不复制 body/IR，
不按创建点重复计算捕获作用域。共享 FileFamily 显式 DFS 保留 normal/phase
类型化所有权，异常创建 context 每方法解析一次；按原始指令 ID 二分查找已排序
创建表，边查找总计 O(C log(C+1))，额外存储 O(C)，不逐边扫描父图。
移动为 O(1)，后续函数族容器扩容仅移动 envelope，原始/工作图和 body 地址稳定。
方法读取扣除 instruction bytes 及各分析记录 work；元数据/容器解析仍仅由既有
byte budget 约束，不将该计数声称为所有底层解析操作的总成本或 RSS 证明。

函数族模块 destination 在每成员一次的结构检查之后复用原生模块校验；按槽位
直接定位绑定，外层名字在已排序索引中二分，不逐 alias 扫描父函数或模块表。
文件 record 身份为每成员常数空间，跨成员遮蔽继续复用 Euler 区间和排序索引；
新增名字比较及查找 work 累计扣减原预算，不缓存外部 scope 指针。

热路径优先连续存储、稠密强类型 ID、邻接索引、函数级 arena；减少逐节点分配、
无界递归和指针追逐。字符串在边界解析并按需驻留；指令语义不通过字符串分派。
哈希结构须考虑不可信输入的退化情况，不能把平均复杂度写成最坏情况保证。

从 P1 开始记录各 pass 的指令/边访问次数、分配量、峰值内存、首次结果延迟和
批量吞吐。测试覆盖 200 与 201 的边界，并以几何增长规模验证曲线。
操作计数用于发现渐进退化，墙钟基准用于比较真实成本；不能仅凭某台机器耗时
较短就认为平方算法合规，也不能用一次耗时抖动证明复杂度退化。

基准、慢速小图 oracle 和对抗性图生成器分别放在 `tests/bench/`、
`tests/support/` 与相应测试目录，不能随生产库发布或进入默认生产路径。

## 算法证据

- 支配树基于 [Lengauer-Tarjan 原论文](https://www.cs.princeton.edu/courses/archive/fall05/cos528/handouts/A%20Fast%20Algorithm.pdf)。
- SCC 基于 [Tarjan 的 DFS 线性图算法论文](https://epubs.siam.org/doi/abs/10.1137/0201010?journalCode=smjcat)。
- phi 放置参考 [LLVM 的 pruned IDF 实现说明与源码](https://llvm.org/doxygen/GenericIteratedDominanceFrontier_8h_source.html)，借鉴算法不等于引入 LLVM 依赖。
- 循环算法采用 [Ramalingam 的修正工作](https://research.ibm.com/publications/identifying-loops-in-almost-linear-time)：该文指出原 Havlak 算法最坏情况可达平方，并给出近线性修正，因此不能直接给未经核对的 Havlak 实现贴上近线性标签。

实现时进一步固定所参考的源码版本或论文版本，并保留许可证和复杂度论证。

### 2026-10-01：共享原始常量出口的受限节点拆分

实现位于 value_simplify.cc 的 SpecializePureTails，采用 CFG tail duplication
的受限形式；这是本项目实现，无新增第三方依赖或许可证变化。目的为解除编译器
null/undefined短路判断共用常量出口造成的cross-entry，不能泛化为复制任意基本块。
前提：已验证的normal ValueProgram；非入口、无phi，活操作是无效果jump及至多一个
language primitive copy；已消除的常量不需要重新制造值。常量结果若存在，其所有
用途均为唯一出边上的phi输入。纯尾部链用一次迭代三色遍历检查，保留进入纯跳转环
的路径，避免重复简化不断复制环。每条原路径仍只
执行一次相同常量，没有复制读取、分配、coercion、环境或异常效果。保留原块的一条
入边保证其可达；副本的出边phi同时重映射常量结果，其他输入由SSA流验证保证可用。

设B/E/O/V/U为原图块、边、操作、值和用途数，D为新增私有出口数，P为输出新增phi
输入数。建立入边及出边phi索引，各常量结果的用途链互不重叠，额外时间与存储为
O(B+E+O+V+U+D+P)，另有原有用途/SSA流验证成本。P可为出度所需的稠密输出，明确
按实际输出容量计费；不对每条入边重扫全部phi或全图。整批仍在独立candidate上，
失败不修改原图。原来的别名/效果对应验证先执行，新增变换由局部类型前提及精确
副本构造约束，再核验最终用途和流；不声称只凭CFG可用性证明了任意图改写正确。
2/3/200/201串接可选访问及942组共同逻辑行为通过定向测试；没有恢复RSS/基准。

### Staged buffered object initialization (2026-10-01)

For normal acyclic ValueProgram graphs, preserve allocation and each own write at
its original site. One Kahn topological pass selects the last write; the existing
Lengauer–Tarjan normal dominator implementation proves that every non-initializer
use occurs after that write. Scan each allocation use chain once, not the whole
CFG per object. Reject cyclic graphs and pre-initialization observations.

Let B/E be blocks/edges, U relevant uses, D definitions and P buffer properties.
The additional flow work is O(B + E), plus existing dominator construction and
O(U + D) use checks. Per-object spelling indexes cost O(P log P + D log P)
comparisons, with string comparison length charged to the work budget; memory is
O(B + E + P) beyond existing plans. No pairwise field scan is used. Both expression
construction and operand verification require each target key to exist as a
writable own data property in the original buffer. This permits native assignment
without inherited-setter behavior. No external dependency or license is added.

### Mixed nested expression construction (2026-10-01)

ExpressionForest::NestedTrees uses this project's implementation of Kahn's
indegree worklist algorithm over verified literal syntax ownership. Read/call
operand dependencies point backward in IR, while aggregate/property ownership
can point forward. A forward pass followed by a reverse aggregate pass cannot
construct arbitrary alternating trees. Each nested node has one parent edge;
an initializer's syntax parent is its exact allocation, not the own-write op.
Use a pending-child count and a bounded ready list, release the parent after
constructing its child, and refuse any unprocessed cycle. Never change runtime
operand/property order to obtain a topological construction order.

For O IR operations and N nested nodes, dependency construction costs O(O+N)
time and O(O) scratch storage, with N <= O. Normal literal use/interval checks
and per-object buffer-property assembly retain their separate costs; the bound
does not claim the whole decompiler is linear. Initializations, scans, capacity
and child releases are charged to the existing byte/work grant. No external
code, license or dependency is introduced. Keep the unique-parent and interval
prerequisites when maintaining this pass. Width and mixed nesting depth tests
cover 200/201/400/800; no full RSS or benchmark work was resumed, so this is
algorithmic and focused test evidence rather than process performance acceptance.
Evidence: literal_plan_test and decoded logical_regions source behavior tests;
this is not a peak-RSS or benchmark claim.

### Guarded module inputs in phased object initializers (2026-10-02)

Pass the already budgeted symmetric module peer index into the common literal
builder and independent verifier. Evaluation construction and verification each
create their own index with the exact live-read census, read/guard effects,
unique Commit corridor and matching dispatch. Literal ownership absorbs only
the proved TDZ guard dependency, preserves single-consumer operand/property
order, and compares both throwing sites against the outer allocation's dispatch
and staged catch sources. The source tree still maps both original PCs to one
native module expression. Original module binding/record identity remains a
separate publication prerequisite. Standalone literal calls without the index
cannot fold late module reads.

No new index, graph traversal per property or external dependency is introduced.
The existing peer validation adds O(I) work; interval guard checks add O(I) plus
their original catch-copy volume, using the existing corridor/catch index and
budget. Common field grouping retains its separately documented F log F cost.
Decoded three-profile object/getter/member-call fixtures cover 2/200/201/400
fields, geometric work growth, TDZ, mutable bindings, receiver/order, arbitrary
throws and saved catch-value identity. Missing/moved guards, slot changes,
ownership/definition mutations and work-budget boundaries are rejected.
The following extension addresses allocations without an exceptional exit.
No peak-RSS or performance acceptance is claimed.

### Native numeric update storage (2026-10-02)

ValueUnary increment/decrement owns an additional exclusive compiler symbol,
not the input SSA binding. Binding allocation counts updates in the existing
operation pass, then creates an OpId-indexed symbol table and dense UpdateBinding
entries in one pass. For O operations and U retained updates, the added work is
O(O), storage O(O + U); the table is absent when U is zero. Existing byte/ID
admission includes the table, symbols, names and AST nodes.

The common AST represents one input assignment followed by native prefix
increment/decrement in a two-child comma expression. TreeFlow derives exclusive
storage from actual node targets and checks all symbol references in one pass;
for S symbols and X expression nodes the additional check is O(O + S + X)
work and O(S) scratch, or a single O(X) discovery pass when no update exists.
It never searches the CFG per update or walks all uses per symbol. Existing
operand/origin checking proves the original input, new result and occurrence.
Non-NaN Number identity ToNumeric checks are O(1). General conversion/domain
admission and source-reference update folding are not claimed by this rule.
Full draft work scaling is checked at 200/201/400 updates in normal/phased
three-profile fixtures; RSS and benchmark measurements remain paused.

### Phased initialization under a nonthrowing allocation (2026-10-02)

Keep the allocation's creation/identity event even when it cannot throw. Its
first actual throwing initializer supplies the interval's dispatch signature;
every subsequent read, guard, call and definition must match it. Such an interval
currently requires zero catch-copy staging at every throwing site: the existing
statement placement cannot execute those copies before a native initializer
emitted at the earlier allocation. Throwing allocations retain their own catch
signature and existing staging contract. The CFG and exceptional exits stay
unchanged; no handler is assigned to a nonthrowing operation.

At the existing independent TreeFlow boundary, reuse TreeStructure's actual AST
expression scopes and the checked original try/body/handler mapping. Seed native
try/catch block scopes and memoize the nearest CatchScope along the already
verified scope-parent forest. Compare each native allocation, property definition,
nested operand and paired guard against its original instruction scope. This also
checks an initializer fused into a later call, rather than assuming its AST
still executes at the allocation. Never walk scope parents once per field or
expand the original protected range to fit the generated syntax.

This adds O(S+R+I+F) work and O(S) scratch beyond the existing scope and literal
checks, where S is AST scopes, R original try regions, I operations and F
definitions. The catch-anchor choice adds O(I) work and O(1) state to the existing
interval stack. Existing byte/work grants cover scratch; there is no new report,
IR, dependency or writer. Decoded three-profile dense arrays cover 2/200/201/400
elements through full tree construction, TDZ, live-binding replacement,
getter/member receiver/order, arbitrary throws, own descriptors and fresh identity.
Cross-protected-boundary and required-staging fixtures remain refusals. These are
focused functional/algorithmic checks, not RSS or benchmark acceptance.

### Native conditional value selection (2026-10-01)

Reuse the checked normal control tree and existing evaluation forest. Index one
result phi per join and its incoming uses in linear passes. Select only two
straight arms, each exclusively owned by one phi-input evaluation subtree, and
boolean-producing predicates (ToBoolean, LogicalNot, strict equality/inequality).
Preserve raw VM false-or-zero tests outside this domain. A conditional AST owns
three distinct expression occurrences; the independent flow check pairs its arms
with the original incoming edges, and operand checking ties its predicate to the
original branch input. The printer uses the existing bounded iterative action stack.

Selection discovery costs O(operations + values + uses + blocks + control nodes),
with disjoint arm scans under the verified control-tree ownership contract.
Scratch storage is linear in those dimensions. Unique join consumers reuse the
existing forward/backward event interval equations for ordered member references;
conditional arm effects remain confined to their original choice. No effects or
allocations are duplicated. Nested/multi-input joins are not yet contracted.
The implementation is project-local, adds no dependency, and is covered by decoded
2/3/200/201-region cases, malformed branch/predicate cases, and independent JS
behavior comparisons. These runs are not a benchmark or peak-memory measurement.

### Partial selection at a shared phi (2026-10-01)

For a verified normal control tree, index the sole join phi's incoming value by
EdgeId. A nested two-arm selection may consume a subset of that phi's edges,
writing the original phi destination at its original branch position. Retain all
other incoming assignments. Only a selection covering the complete phi with a
local join can participate in cross-region expression fusion; the independent
forward interval verifier derives this restriction again from the original CFG.

Arm discovery traverses disjoint control-tree paths and admits only pure jump
prefixes before the final owned expression. Construction and verification both
retain the original jump anchors. Indexing costs O(values + uses + edges);
path/leaf scans cost O(operations + control nodes) under unique control ownership.
The additional edge index is O(edges), with no per-phi graph walk or new dependency.
Decoded 2/3/200/201-region fixtures, distinct outer/inner return values, a forged
whole-phi fusion and an effectful corridor exercise coverage and ordering. This
extends the earlier two-input domain without yet composing nested selections
into one high-level expression; it is not a runtime resource measurement.

### Nested selection trees and join consumers (2026-10-01)

Process the verified control forest in iterative postorder. A typed child branch
can replace an arm only when it targets the same phi, has one containing parent,
and its header consists entirely of the predicate's owned inline subtree. Count
leaf incoming edges bottom-up, preserving partial coverage until a root covers
all inputs and owns the join. Reuse the existing event interval equations only
for that root; nested children never independently substitute the shared phi.

Expression construction, placement emission and independent flow verification use
reusable bounded worklists. Primitive-copy phi leaves with no SSA operand may be
materialized into AST before their array position, retaining their original arm
and origin; all nonconstant dependencies must precede the join consumer. This is
AST scheduling, not effect motion or shared runtime evaluation. Each subtree and
leaf is built once, including after cloned constant tails were appended past a
join. No runtime helper or external dependency is introduced.

Discovery and worklists add O(control nodes + operations + values + uses + edges)
time and space under the checked unique control ownership contract. No recursive
C++ descent or per-variable reachability pass is used. Decoded 2/3/200/201/402-deep
trees, distinct short-circuit results, malformed child origins/arms, effectful
headers and ordered member getter tests cover the extension. This supersedes the
previous nested-selection exclusion; object initializer fusion remains separate.
No peak-RSS or benchmark claim is made.

### Selected module reads and consecutive consumers (2026-10-01)

Normal selection arms also accept an exact module read/guard pair owned by their
single inline/phi subtree. One common checked peer index adds O(I+M) time/O(I)
temporary space, rather than rescanning module obligations for every arm. The
phi producer excludes only the original guard use and event; arbitrary throwing
suffixes and multiple source uses still refuse. Both original events retain
their effects and origins in the independent evaluation and tree-flow checks.

Selection occupancy distinguishes a header from a consumed join prefix. A join
consumer must precede a later header's predicate before that same block can be
reused. The existing domain equations check all events and exclusive dependency
consumption again; a join cannot acquire multiple owners. Each block participates
at most once as a header and once as a join, retaining linear scans and O(B)
occupancy storage. This is an in-house extension of the existing interval
algorithm with no dependency or license change. Decoded conditional/and/or
module arms at 1/2/200/201/400/800 widths and independent consecutive-call traces
exercise the extension; no RSS or benchmark measurement is claimed.

### Selected buffered object arguments (2026-10-01)

A normal staged buffer with ordered own definitions and one external use can enclose
already proved conditional selections. The backwards event-interval builder
combines allocation, conditional RHS and definition, then consumes the whole
interval in the join. The independent forwards checker proves contiguous child
intervals and unique consumption, including an earlier member callee. Original
local placements are still checked with the allocation's multiple-use snapshot
and the definition at its original site. Constants and TreeFlow retain the
existing writable-own-key, property-order and source-origin obligations.

Allocation/field and header/consumer indexes are built once from LiteralPlan
and complete selections. Only an object's first definition starts its route;
adjacent header/join selections follow the original definition order, and each
object use chain is traversed once. Contracted blocks are visited once per route,
including shared joins. This adds O(B+I+D+U+V) work and auxiliary space to the
existing selection-domain passes. Literal
property construction keeps its separately documented sorting/text costs.
Budgets include both the persistent projection and temporary indexes. This is
an internal extension of the existing event-interval proof, without a new
library or dependency. It composes multiple adjacent selections whose results initialize ordered
fields, directly or through a same-block expression tree with a sole pinned RHS
root. A gap between selection domains still needs a separate proof; no arbitrary
CFG traversal is implied.

Evidence: decoded early/late member call and direct-return fixtures, getter and
method replacement/throw behavior comparisons, intervening debugger and shared
object rejections, and corrupted projection/property-origin negatives. Existing
200/201/deep selection specifications remain exercised; no RSS or performance
measurement is claimed.

The 2026-10-01 multi-field extension uses a reusable, budgeted domain index and
route vector, not per-object graph-sized allocations. Source-order root iteration
prevents a later selection from consuming a shared block before its containing
object route. Independent forward verification rebuilds the route, verifies all
selected phi consumers, each property definition and final object consumption.
Distinct/repeated keys and 2/3/16/64/128/200/201/256 definitions are decoded
correctness fixtures. Repeated keys retain every RHS and its original origin;
intervening debugger and missing/early definition claims remain rejected.

The field-expression extension indexes initializer consumers in reverse value
order: seed each staged definition, then propagate through its single-use pinned
RHS and existing local inline operand edges. Every operand is visited once,
adding O(I+U) work and O(I) space, all budgeted. This avoids walking the same
expression tree separately for every selection. A selected field RHS restores
its original effect snapshot for local verification; independent domain
verification checks its actual inline consumer and contiguous event interval.
String-add fixtures include user conversion effects and abrupt completion,
1/2/16/64/200/201 expression depth and 201 fields with suffix operations.

### 组合证明的表达式依赖范围

快照验证从选定比较消费者反向标记原inline子树；逻辑组合Domain从最终消费者、
分支和对象定义反向标记依赖。每条既有收缩路线增加一次O(I+U)扫描，复用O(I)
标记与清理列表；不增加逐值图遍历。独立正序区间方程仍计入全部原始效果事件，
只对参与额外组合的树合并子区间；无关表达式保留既有局部验证。全局consumer
计数及精确用途检查不变。自行实现，无新增依赖；前置/后置字面量调用及跨debugger
伪造快照反例见logical_regions_test，行为对照见logical_regions.mjs。

### 未暴露环境的延迟初始化

ProveLocalAccesses沿既有指令/环境绑定扫描记录新环境的连续无观察者前缀，按cell
保存其首个store和allocation。额外O(I+C)工作/O(C)存储，纳入原预算；闭包捕获
复用原二分cell/store索引并常数查询此前缀，不逐变量扫描指令或控制图。控制边、
加载、外部存储及可能观察的操作终止前缀。此实现复用项目环境身份/SSA/完整族
Language写入合同，无外部依赖；1/2/200/201闭包及独立cell行为用例见
creation_tree_context_test、capture_plan_test和deferred_capture.mjs。

### Conditional retained roots in shared decisions (2026-10-02)

The existing checked ControlDecision path owns each later predicate's original
single-entry linear corridor. In phased trees, retain expression roots as a
right-associated native comma prefix inside that predicate's logical RHS.
Snapshot assignments publish only after their operand completes normally;
phase evaluation is unchanged and a branch still owns no throwing subtree.
Require the original protected/handler scope to match the decision header,
no normal edge copies and no nonempty catch staging. Statements, environment
changes, declarations and unsupported root shapes remain refusals.

Let C/O/R denote control nodes, operations and retained RHS roots. One scan
reserves O(C+O) expression/scratch capacity, with byte and strong-ID admission;
no reserve(size+1) per root. Each corridor operation and root is consumed once,
so extra construction/checking work is O(C+O+R), space O(C+O+R); the independent
flow consumer parses actual comma operands in original order without a subtree
walk per root. Existing exception/control/operand/origin checks retain their
own costs. This is a local implementation of structural short-circuit recovery
using the existing common AST and printer, with no new dependency or license.
Tests cover 2/200/201/400 predicates and complete draft work growth, both
polarities and protected/unprotected calls and coercing comparisons. No RSS or
benchmark run is implied by these bounded regression measurements.

### Anonymous module binding spelling (2026-10-02)

For one metadata inventory, let E be import/local-export rows, U distinct name
entities, B decoded character volume, and O retained source-name volume.
Sort/deduplicate entity IDs, decode each entity once, intern decoded text by
sorting name indices, and resolve rows by binary search. Adjacent rows compare
content-class IDs, including equal text at distinct entities, without scanning
the long name again. Let K be characters compared/charged by content sorting
and interning; its comparison-model bound is O(U log(U+1) * Lmax), with Lmax
the longest name. Keep that cost explicit rather than claiming strict linear
text sorting. The prefix scan visits decoded characters once. Work is
O(E log(E+1) + B + K + O), storage O(E + U + B + O), including admitted transient
arenas. Prefix and final names reserve once before appending. This is the
adapter's per-invocation cost, not a claim that all per-function metadata
resolution across a file has been cached. Existing module-reader and guard
validation costs remain separate.

The 200/201/400 repeated-long-name fixture first observed work
81182/81989/322182, then 2212/2222/4212 after entity deduplication. Each case has
one growing long string, repeated aliases, distinct equal entities and
nonadjacent repeated slots; input/output dimensions are linear in the case
size. These bounded work measurements are not latency, benchmark or RSS proof.
See source-recovery/module-binding-names.md for pinned sentinel provenance,
source identity, independent behavioral checks and remaining scope. A further
self-review regression alternating two entities with the same growing long
text found 42526/43545/164838 work before content interning. The final fixture
keeps this case so entity-only deduplication cannot pass the scaling assertion.


Native anonymous-default syntax uses the already-checked sole default export
slot; there is at most one candidate per record because public export names are
unique. Inventory/declaration selection is O(A+S+M), for alias rows, root
statements and module aliases. Eligibility traverses T distinct immutable trees,
C distinct class definitions and E ownership edges, inspecting N total syntax,
alias and capture entries. Ordered pointer sets give O(N+E+(T+C)log(T+C+1)) work
and O(T+C) scratch. Pointer comparisons and scanned entries consume work;
PMR allocation requests (including discarded queue capacities) consume a bounded
scratch allowance. Adapter selection and printer revalidation make two passes,
not one traversal per module variable. Existing structure/enclosure/printing
costs remain separate. This is local C++20 code using the existing standard
library, not a new IR, output writer, dependency or class-analysis cache.

The 200/201/400 fixture has root-check work 608/611/1208 and wide-family work
5420/5453/12352. These counters check bounded growth across 200/201 and geometric
sizes; they are not whole-process latency, allocator or RSS evidence. Native
module semantics are exercised independently through Node ESM tests under their
stated execution premises; pinned frontend provenance remains in
source-recovery/module-binding-names.md.
