# 反编译算法与性能约束

本专项设计落实根目录 [AGENTS.md](../../AGENTS.md) 的算法、类型、性能与测试规则。
属于方案审阅内容，尚未实现或测量。

## 输入规模与成本

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
| 循环嵌套 | T05 自然循环采用支配树驱动反向发现、子循环压缩与 union-by-rank；非自然区域扩展候选为 Ramalingam/Havlak | 使用紧凑父子森林，不复制每层成员；不可约环拒绝，源码循环语义仍需独立验证。选择与复杂度见 [循环发现](source-recovery/loop-discovery.md)。 |
| 循环单入口退出分支归属 | 自然循环之后，按唯一入边和工作队列扩展词法归属；按 child-before-parent 处理，Euler 区间判断祖先，正常/异常投影共用 | O(V+E+L) 时间和空间，每个新归属节点的出边只入队一次，不逐层重走路径。仅多出口、单入、无归属或祖先归属的路径；允许单入口分支及 try 摘要分发，多入口汇合、独立入口、同层/子循环和终结停止。原图边及自然循环发现结果不改。沿用本项目单入口区域/邻接表原则，自有实现，无外部代码或新依赖；单层/嵌套 200/201/402 与分支正反例为定向正确性用例，不是性能实测。 |
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
Evidence: literal_plan_test and decoded logical_regions source behavior tests;
this is not a peak-RSS or benchmark claim.

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
