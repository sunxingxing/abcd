# 反编译算法与性能约束

本专项设计落实根目录 [AGENTS.md](../../AGENTS.md) 的算法、类型、性能与测试规则。
属于方案审阅内容，尚未实现或测量。

## 输入规模与成本

### 共同变量恢复流的首个消费者（2026-10-07，规则173）

规则172的source-side图构造抽出为RecoveryFlow，独立BindingInterference
只读取图的read/definition/copy/edge，不解释源码AST。参数、empty声明和
成功赋值是typed定义；update按读取→求值→成功写回分点，完成边区分
truth/nullish分支、throw/return/jump和暂停恢复。LegacyFlowProjection
仍为过渡输入，不能用其normal_stores反向宣称canonical初始化已证明。

图构造和替换链缓存O(N+E)，显式definitions/双向edge索引/来源点表增加
O(N+E)保留存储；整个liveness成本仍按下节A/L/P，不声称整体线性。
参数定义表示默认值程序之后的body entry。normal scope enter/leave
已显式，abrupt scope unwind、capture identity、原IR证据复用与def-use
共同分析仍未闭合；不能据当前scope点表移动绑定生命周期。

独立三点/两绑定oracle用小型稠密固定点扫描，不复用生产稀疏工作表；
7种read/write/copy action的全部三点组合×512有向图（含自环/不可达）
共175616图，对照copy-affinity干涉。模型事件测试另验初始化、分支、
显式throw、await/yield resume与成功写回，原800规模案例沿新消费者。
1/200/201/400/800对独立存储work为1313/298418/298625/622162/1305730，
400→800为2.10倍。实际RSS和全套仍暂停；此处不是完整架构验收。

### 私有存储干涉与 copy coalescing（2026-10-07，规则172）

来源为经典后向 may-liveness 与 copy-affinity coalescing；行业参照
[LLVM Code Generator 的 register allocation/coalescing](https://llvm.org/docs/CodeGenerator.html#register-allocation)。
这里是自有 FunctionTree 实现，未复制 LLVM 代码、未引入第三方依赖；
项目许可适用，维护在 binding_interference/binding_initializer 与共同 printer。
前提是已检查的 typed FunctionTree 和当前 immutable binding projection。

按唯一语法 ownership 迭代构造原求值次序图。普通结果、抛出、循环回边、
分支及 break/continue/return 分开；可抛求值与成功后的写入分别建点。
已初始化声明式读取和可写 PutValue 复用共同 normal-completion 事实。
await 拒绝和 yield throw/return 保留；finally 当前采用 pending continuation
的保守并集，可能多拒绝合并，不删真实 cleanup 路径。参数默认值不在此
body 图内；候选排除 outside-body/capture，不能据此改写参数环境。

读点种下 (point, symbol) 活跃事实，共享一个反向工作表。写点杀死本
symbol 的活跃传播；对另一活跃 symbol 的写入形成干涉，直接 copy 的
同值源例外。仅查原 copy-affinity 对，排序/二分，不构造全变量对矩阵。
替换链缓存解析最终读取，逐边一次，不按链长匹配。
图 N 点/E 边，A 个 affinity，L 个唯一活跃事实，P 个实际传播的事实-边
关联：时间 O(N+E+A log A+(L+P) log(L+1)+P log(A+1))，空间
O(N+E+A+L)。稠密活跃时 L/P 本身可大，不能称整体线性。PMR allocation
和比较/传播计入既有 byte/work budget；累积 allocation 保守计费，耗尽
返回 typed 错误。无逐变量全 CFG 重走、无 200/201 算法切换。

一次 disjoint matching 只合并已证明的变量对，避免未经证明的传递干涉；
一般多变量组仍是精度缺口。还必须证明被删声明的初始 undefined 已死、
保留声明已初始化、每个映射引用词法解析一致、原生 for-let lifetime
不变。严格 resource 合同保留可能改变直接 function/class 命名包装分配
的存储。这些独立语言/资源义务不由活跃性代替。

144 份树变化与 5 个规模函数执行通过；独立行为/反例见 status。
1/200/201/400/800 对独立存储的 work 为 1157/278362/278469/582106/
1225674，400→800 为 2.11 倍；empty-block 与逆序 arena 另验。
这是定向 work 计数，非暂停的全语料 RSS 或 benchmark 验收。

规则174将后向求解移至FlowLiveness，干涉仅消费其事实，不再拥有第二个
工作队列。FlowDefinitions使用经典前向gen/kill到达定义，在同一不可变
图上按live-in裁剪；每个定义从成功写回点的后继出发，读发生在同点写入
之前，新的同绑定定义杀死旧传播。空def-use不证明已初始化。实现自有，
无新增依赖或复制外部代码。维护点为flow_liveness/flow_definitions。
设D为定义数，R为唯一(point,definition)事实，Q为传播边访问，U为输出
def-use边，额外时间O(N+D+(R+Q) log(L+1)+(R+Q) log(R+1)+U log(U+1))，
额外空间O(R+U+D)，不隐藏稠密定义传播成本；所有容器和传播受共同预算。
175616个三点图用独立稠密固定点同时核对live-in、live definitions、
def-use和干涉。200/201/400/800/1600定向work分别6485/6495/15248/
35155/79750，正反arena均验证。尚未用此分析替代初始化/死定义决策。

### 循环进入程序与 for initializer（2026-10-07，规则151）

LoopInitializer按原statement ownership顺序流式遍历，loop进入/退出
的写入序号与SymbolId最后写入索引共同选定携带状态，不逐loop遍历
body或匹配opcode/CFG/寄存器/PC。透明block中的进入根按原序组成
disjoint程序；候选定义索引向左扩展依赖并保留全部中间操作及重复
写入。每个根/表达式只扫描常数次，loop条件不包含被保留的body。
N为statement/expression/children/symbol/scope数量：新增O(N)时间、
O(N)预算化存储，总LoopSyntax仍O(N + R log N)，R为transition依赖读。
printer另按输出字节及anchor索引计费；深层缩进产生的输出规模不能
归为输入线性。内存/工作耗尽复用原typed错误，不丢弃语义。

语言来源固定[ECMAScript 2024 ForLoopEvaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-for-statement-runtime-semantics-forloopevaluation)；
实现为项目自有ownership/completion/定义索引，遵循项目许可，无新增
第三方代码或依赖。维护在loop_initializer/loop_syntax/common printer。
仅表达式initializer，不新建每轮lexical binding；scope/handler/label/
声明和间接heap写入仍有表示边界。156树的普通prefix、801深块和
800符号依赖链分别验400→800 work小于3倍，typed字节耗尽、原来源
范围及独立行为另验；真实144 decoded和actual record证据见status。
这些是定向work/行为证据，不替代暂停的全语料RSS/benchmark。

### 循环继续程序与 for update（2026-10-07，规则150）

复用共同LoopCompletionMeaning与原唯一AST ownership，在条件实际读取
的SymbolId上种下更新目标；一次definition census与statement Euler区间
索引建立依赖，扩展有序程序时保留全部中间操作。继续目标全覆盖、原
scope/handler/完成义务决定表达式能否移动，不以++、opcode或CFG形状
猜测for。N为statements/expressions/children/symbols/scopes，R为依赖读：
O(N + R log N)时间、O(N)存储；所有buffer经共同budget申请，定义区间
查询二分，不反复逐变量走CFG。源码、原来源与typed耗尽保持。

语言来源固定[ECMAScript 2024 ForBodyEvaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-forbodyevaluation)
及LoopContinues；分析代码是本项目实现，遵循项目许可，无新第三方实现/
依赖。现空initializer不引入每轮lexical binding；member location alias、
分离继续程序、跨handler/binding和initializer仍有表示缺口，维护责任在
loop_syntax/loop_transition/common printer。108份独立树/72实际解码与
400/800 work检查、typed耗尽及实际record定向证据见source-recovery/status。
该测试work计数不是全语料RSS/benchmark；暂停的测量未恢复。

### 精确 Boolean 值域与选择来源归属（2026-10-06）

规则141沿原有限高度格与sparse依赖worklist传播Boolean⊂Boolean/null⊂
Defined⊂Language；normal-result仍依赖已证明输入。COPY、phi和循环SCC
沿既有依赖处理，不按值重走CFG或复制链。ValueKinds仅增加原byte enum
alternative，无新增每值字段/数组；join与BooleanEvaluation均O(1)。格高度
增加一个常数层级，原工作计费不以VmValueKind四类别数当迭代上限。

TreeFlow复用原conditional/logical ownership；共同origin pass的原每site
owner表增加typed selection context，实际scalar执行归属仍独立覆盖。
填表与查询沿原一次operation扫描及CombinedOrigin DAG遍历，不为每个
conditional重走来源。O(O+R+X+A)附加/既有工作，O(R) owner空间，R为原
origin数、X为语法expression数、A为origin parts数；实际OriginOwner宽度
由原TreeCheckBudget::Array计入capacity/内存。DAG unique combined owner
及backward引用仍验证，不允许context覆盖原predicate或共享执行root。

来源为项目既有typed ISA/profile转换、SSA固定点与FunctionTree来源模型；
无新依赖、外部代码或许可变更。200/201/402/804 COPY/split值流与三profile
native源码、复合条件/stateful getter/错误来源检查是功能及有界工作证据，
不冒充仍暂停的延迟benchmark或完整进程RSS。固定五ABC／六模式fresh
导出、源码/位置映射比较与实际测试证据见status.md。

### 直接 callable 默认导出的命名投影（2026-10-06）

规则140复用原module默认候选与完整tree/class inventory；没有增加恢复图
匹配、CFG遍历、专用writer或第三方依赖。直接函数先一次扫描F个family
member，证明实际owned body及明确creation-name证据，成本O(F)、O(1)
新增空间；每record只有一个候选，不按binding反复扫family。原inventory
成本O(N+E+(T+C)log(T+C+1))及O(T+C)计费scratch保持，下文同算法维度。
每个member查询计work，旧PMR allocation、tree/class去重及资源异常保持。

printer新增固定大小typed名称alternative，现有直接语法ownership扫描
把候选目的名称设为default。实际runtime-name／self／class-scope proof、
原native syntax／envelope选择和preflight复用原算法；表达式括号仅增加
常数输出动作，不重新解析文本或移动创建。来源为项目已有命名／生命周期
实现与ESM表达式NamedEvaluation合同，没有新依赖或许可变化。

200/201/400个wide-family的新增owned查询相对原inventory保持线性附加
工作；三profile实际解码及独立210 ESM场景／4 mutation检查名称、TDZ与
外层scope，原相关模块split/register回归通过。完整测试和固定五ABC／六
模式当前导出见status.md；功能work计数不代替仍暂停的RSS／延迟benchmark。

### Native 模块声明的后续写入可用性（2026-10-06）

规则139的RecordTree按共同ControlPlan的根source序列记录每个module slot
实际首次声明OpId；后续非根序列write由新鲜VerifyValueFlow的正常dominator
和operation order证明声明已完成。删除physical block 0准入，不移动store/
RHS或扩大scope；tree flow、module/capture身份、TDZ及原来源验证仍独立。
conditional first initialization/native declaration placement和catch保持其
明确表示边界，不能借normal dominance输出错误的初始化时机。

来源是已有值流校验及simple Lengauer-Tarjan dominator实现；没有新第三方
代码、依赖或许可变化。S/B/E/O/V/U/P分别为module槽、块、边、操作、值、
用途、phi输入数；本次新增索引/遍历O(S+B+O)，每个额外write查询O(1)。
只在有额外write时运行一次共同availability，成本O(O+V+U+P log(P+1)+
(B+E) log(B+1))，不按槽/write反复遍历CFG。typed absence/OpId向量实际
capacity、availability临时数组和dominator保守peak沿原budget计入。

三profile的join/loop/register重分配、1/200/201/400/800 split已以真实
ABC解码及独立ESM结果/变造/拒绝验证；本次不引入200项阈值，也不新增
默认work准入。完整进程RSS与独立性能benchmark仍暂停，不据这些功能
fixtures宣称实测1GB/延迟达标。实际固定五输入回归证据见status.md。

### 固定自身 class prototype 读取的 SSA 身份（2026-10-04）

规则97先新鲜验证availability/uses，再复用AnalyzeValueIdentities的
迭代copy/phi SCC，按实际base、key SSA定义形成ClassPrototypeRead。
键名属于常量文本边界：NamedRead的NameId或计算String producer的TextId
须确为prototype。独立投影重建图事实，最终源语义重查键与实际ClassTree。
固定v7 runtime f04900cf的class_info_extractor.cpp在创建时安装不可写、
不可配置的自身prototype并设置ProtoOrHClass；JSFunction::PrototypeGetter
读取已有对象。没有引用身份/逃逸/corridor的额外准入；未使用读取可消除，
活跃结果和非原生类身份保持。其他getter、scope、初始化效果和异常不放宽。

来源为既有SSA SCC身份算法与固定VM的类属性合同，项目自有实现，无新增
第三方依赖/复制代码或许可证变化。I/V/U/B/E为操作/值/使用/块/边数；
在原availability/dominance成本外，分析O(I+V+U)时间/空间，排序后的条件
事实与候选以一次merge O(I)核对，源边界按原操作索引O(1)查询；prototype
义务对已排序条件事实的查询O(log I)，整体不超过O(I log I)。不按读取或
类重走CFG/copy链。发现和独立投影的共同flow/SCC scratch、结果capacity、
计划保留facts及合并扩容均计预算，暂存与retained成本保守计入。

180组三profile/15情形/1、200、201、402解码用例，精确work和几何增长、
完整文件原生行为/源常量变造及六个实际配对模块提供证据，见status.md。
计算键没有共同常量值事实时保留原读取；一般RHS、phased类和保存的callee
Reference接口仍开放。完整进程RSS/延迟基准继续暂停。

### 字面量来源与无依赖纯表达式所有权（2026-10-04）

规则96的LiteralClassFields按实际SSA OpResult追溯字符串及透明copy的
字面量来源，以Unknown/Visiting/Absent/Proven缓存每个值，迭代展开而不
递归。每个定义最多展开、回填一次，物理OpId/块顺序不决定事实。入口、
receiver、phi、未知值、效果来源或无基础的循环均不能凭String域变成
字面量；可用性、效果链和返回义务仍由原共同证明承担。一般表达式及phi
字面量来源尚未覆盖，此为已有literal证书的来源替换，不是完整RHS恢复。

共同EvaluationShape的SourceEvaluation::kPure、无显式依赖、eager输入、
Language结果及无正常/异常效果组成closed pure value事实；隐式frame读、
分配及可能抛出的操作不满足该合同。普通流保留原单次消费UseId到
EvaluationPlan.closed_expressions，独立校验原可用性、实际use census、
consumer、语言域和源语义。该所有权与logical selection、phased效果/
catch所有权分开；不能把任意跨块inline当作条件选择。Forest预构造这些
无依赖节点一次，使用原constants/origin，再在原物理位置跳过重复构造。
不重放getter/this，不授予一般依赖表达式的移动或临时变量消除权限。

来源为SSA def-use、memoization和既有typed求值形状，项目自有实现；无
第三方代码、依赖或许可证变化。I/V/U为操作/值/用途数，literal查询总
时间O(I+V)、暂存O(V)；closed所有权建立/校验/预构造均O(I)，原use
census为O(U)。索引仅在需要时分配O(I)，计入EvaluationPlan retained
bytes；literal状态/路径及所有额外work均计预算。1/200/201/402、正向/
逆序复制链和完整文件源码回归见status.md，不代替暂停的全语料RSS测量。

### 字段初始化正文的共同必达效果与完成（2026-10-04）

规则95复用EffectOrder的逆向must worklist：Endpoint为原OpId、NormalReturn
或未证明。首有序效果切断依赖；真实return作为正常完成边界；后继必须
同意同一原效果或正常返回。无出口证据/纯循环保留失败，不能把“有出口”
当作必终止。相同正常完成不合并其返回值或原PC，消费方另检查实际输入。
入口endpoint来自同一方程，包括空入口块，不能借第一个物理op取代。

LiteralClassFields一次分类原字段及既有literal事实，再沿必达首/后效果链
检查原前驱身份与dominance，最后证明无后续效果且必正常返回。每个字段
只能消费一次；所有原字段必须归属此链。字段结果按实际效果身份顺序，
不再按物理OpId/PC排列。this/owner、literal RHS、return值及descriptor
各自保留原证明；一般RHS/条件字段和phased原生投影仍有覆盖缺口。

来源为既有SSA/dominance、效果依赖与must数据流，项目自有实现，无新
依赖、第三方代码或许可证变化。I/V/B/E/F为操作/值/块/边/字段数，索引
新增时间O(I+B+E)、空间O(I+B)；证书在共同分析外O(I+V+F)时间/空间，
查询O(1)，不按字段重走CFG。预算计入endpoint/normal-return及字段状态
数组，继承共享索引整体成本。1/200/201/402、空入口/逆序/纯菱形/不同
return及divergence反例与完整native对照提供证据；没有恢复RSS/基准。

### 字段 receiver 的共同来源查询（2026-10-04）

规则94在fresh verified/simplified ValueProgram上以实际ValueDefinition
origin及Language域查询ReceiverEntry或ValueFrameThis。来源依据既有
typed ABI/frame与callable owner初始化合同；没有外部代码、新依赖或许可
变化。该查询不求两个SSA值的等价、不回放frame read、不沿每个字段
重走CFG/copy链；原copy canonicalization、availability、效果和域证明
仍分别成立。每个查询O(1)时间/空间，原LiteralClassFields单次扫描和
线性scratch成本不变，新增frame读取计入既有flow/work预算。

FieldDefinitionExpr内的receiver使用共同binding表示保存的原读取；树
结构不要求直接ThisExpr，独立operand/flow/origin证明实际输入。native
static syntax的implicit receiver上下文另在打印边界关闭；跨块/多字段
保留读尚缺省略storage/event的投影，不能以AST形状代替这项证明。
1/2/200/201/402字段证书精确work与几何增长、跨块源树正反例、三profile
实际file/context输出及270组独立native对照提供本项证据。literal-only
单块快速路径仍未迁移，完整进程RSS/延迟基准继续暂停。

### 类初始化调用的共同效果必达索引（2026-10-04）

规则92的调用识别以fresh VerifyValueFlow证明class home、method callee及
receiver实际使用处的SSA可用性；不再要求creation/method/call同块或比较
物理OpId。语法投影复用BuildEffectOrder的原效果前驱身份和dominance，
新增after索引回答所有路径是否必然到达同一下一效果。每块首个有序效果
为边界；反向邻接/worklist求must-reach，各后继必须同意同一原OpId。
正常退出失败，未收敛纯循环失败，不同效果历史失败。反向块内扫描把
结果分配给实际操作，不能把不同效果的相同数量视为同一历史。

初始化方法、调用、已证明的self store及有独立名称义务的prototype读取
构成creation到call的唯一原效果链；其余class观察须由call支配，phi在
实际incoming edge上检查。self store保持真实cell写入。调用可能绕过、
重复执行、永远到不了或有任意interposed effect时，不准提前进class syntax。
这不是固定块形状或指令模板；名称、源文件和opcode序列不参与准入。

来源为既有共同SSA/dominance、效果依赖及must数据流方程；项目自有实现，
没有新增依赖或复制第三方代码。令I/U/B/E为操作/使用/块/边，新增after
时间O(I+B+E)、空间O(I+B)，复用已有反向邻接；整体继承共同SSA与
效果索引成本及预算。每个原use和效果至多归属一条class链，不按class
重新走CFG。解码覆盖200/201及402规模、分块/菱形控制；完整源树另覆盖
逆物理布局。full-suite、完整进程RSS及延迟基准继续暂停。

### 保留值选择的共同真值投影（2026-10-04）

规则91的ProjectSelectionLogicals在已验证SelectionRegion之后运行，原控制/
phi/异常模型不变。按原inline consumer森林迭代路径压缩求根；按typed
ToBoolean、LogicalNot和有Boolean输入证明的VM谓词形成一元方程，单次
邻接表/worklist求canonical保留值和极性。规则106将同一方程扩展为
null、undefined、其他假值、其他真值四个不相交域；严格null/undefined
比较解释原常量和操作数身份，允许交换操作数和取反，不匹配指令序列。
方程另记录中间结果是否为Boolean，Boolean再与null/undefined比较恒假，
不能把原输入的null域误授予该结果。branch自身predicate和shared-arm
polarity也参与方程，不能只看Boolean producer。不同观察归属/其他效果或
不等价VM谓词保留原ConditionalExpr；此为安全代数表示，不是恢复准入模板。
各scalar判断按共同shared-arm控制合并可达域，恰好保留truthy/falsy/
non-nullish原值时分别构建既有OR/AND/Nullish LogicalExpr。

原first input UseId随方程保留，Source Use消费原saving assignment。省略
的是单次branch-owned纯truth计算，未省略getter、外部Boolean观察或模块
TDZ。每个原truth/branch来源合入同一logical根；fresh TreeFlow复算事实，
原边与operand/origin检查确认实际保存、精确值和调用顺序。不存在从输出
文本反推恢复或根据样本答案替换producer的路径。
规则106删除operand检查在fresh truth证明后重复要求unary/copy/branch的
producer名单。实际saving UseId、selected phi、效果/异常和来源检查保留，
改成OR、交换两支或替换保存值的实际树仍被拒绝。

来源为既有typed VM/profile语义与ECMA-262 ToBoolean/logical evaluation的
等价关系；项目自有实现，没有复制外部代码或新增依赖/许可证。令I为操作，
V为值，U为原使用，B为块，C为已选择的scalar条件；新增时间/空间为
O(I+V+B+C)，使用查询为O(1)，继承共同SSA/census成本O(I+V+U)。每个
inline链有唯一consumer根，所选truth链互不共享执行所有者；每个condition
block的纯度缓存只求一次。不按每个producer/selection重新走整个图，不
复制每个链后缀；所有数组/队列/来源及输出事实计入现有byte/work预算。

完整解码/draft覆盖三profile的1/2/200/201 truth链、NOT、重复分块测试、
raw VM谓词和外部观察；50/100/200/201/400重复条件的draft work为
514547/977270/1902979/1912237/3754389。该工作量检查及源码/native/配对
回归不是暂停的完整进程RSS/延迟/分配基准。single-entry候选、多真实copy
RHS及parent scope等既有覆盖缺口不由这项代数投影宣称完成。
规则106另有三profile、1/200/201/402次严格比较的真实解码夹具，含
反向操作数、NOT、寄存器copy及纯块拆分、缺少undefined、不同返回值、
Boolean中间结果及成员call producer。96个完整生成函数独立验证150,528次
行为；另外12个
debugger条件前缀仅记录既有FunctionTree kOwnership缺口，未生成源码，
不计为行为通过。这类statement-only前缀需共同控制/语句表示，不能删除
barrier或额外匹配其局部布局来产生逻辑表达式。

### 布尔选择与 NOT 的共同打印组合（2026-10-04）

规则85在已恢复 FunctionTree 的打印 action 中携带 Value/Truthiness/Discarded
观察与 Boolean polarity。迭代消费任意长度的 NOT 和两侧 BooleanLiteral 的
ConditionalExpr；不同布尔结果转为最少必要 NOT，相同结果通过 comma 保留
原条件 GetValue。Discarded 只省去无用户钩子的布尔转换与未观察的结果，
&&/|| 左侧仍消费 truthiness，?? 左侧仍消费精确值。原输入验证、效果/异常、
Reference、scope 和 AST 身份没有改变，不提供 opcode/CFG/样本恢复许可。

来源为 ECMA-262 ToBoolean、logical NOT、conditional 和 comma evaluation；
项目自有实现，无复制外部代码、新增依赖或第三方许可证要求。共同 grammar
处理被暴露的子表达式和剩余 unary，原节点位置范围随事件栈结束。N个访问的
AST节点、L个输出字节的时间 O(N+L)，action 与位置空间 O(N)；每次 polarity
action 消费一层节点，不回溯重扫，不递归打印，literal 查询为常数时间。

44种选择/观察组合、1/2/200/201/402/804深度的工作量与范围检查，以及
270080组getter/Proxy/结果身份/短路/异常独立对照验证此边界。夹具是 printer
隔离层；真实解码及配对行为另行验证。没有恢复暂停的性能/RSS或扩展语料
测量，既有算术域、构造协议和阶段一语义/源码风格缺口仍在。

### 派生构造初始化的单次调用证明（2026-10-04）

range/spread 共用 `ProveInitialization`，不以 branch target 的 PC 大小、
guard 数量或 guard 是否在 super 后决定恢复。`ProveSingleInvocation`
从真实 call block 沿 normal CFG successor 做一次迭代可达性遍历；存在
返回 call block 的路径才表示调用可能重复。其他块的循环和向后物理布局
不提供也不破坏初始化证明。正常 return 的支配与每项 guard 的实际 SSA
origin 仍独立成立；原始/current-IR 对应、receiver 和捕获另行验证。

标准图可达性算法，项目自有实现，无外部代码或新增依赖。B 块、E 边的
新增时间 O(B+E)，scratch O(B)，每块最多入队一次。分配容量和初始化/
边访问计入现有预算，scratch 结束即释放；不按每个 guard 重新遍历图。
原支配/稀疏 SSA 成本不变。这是正确性及预算边界证据，不是暂停的延迟、
分配或 RSS 实测。正常图不证明缺失的异常/resume 路径。

当前接口仍表示一个原始 super site，要求它支配正常 return；多个互斥
super site、异常区域、escaping cell、未证明的 parent brand/参数展开
和 legacy 线性构造模板仍有缺口。不得将上述临时表示边界扩充为本地
序列或 CFG 形状列表。验证层与剩余阶段一 gate 见 source-recovery/status.md。

### class 的 identifier 名称推断上下文（2026-10-04）

共同 printer 的表达式 action 携带 optional NameId；直接变量初始化与
identifier 赋值填入实际 symbol spelling，其余表达式不继承。ClassExpr
比较该 spelling 与已恢复 runtime name，匹配时使用匿名 class 语法自带的
NamedEvaluation，不匹配时保留 computed-property 包装。没有改变 AST 的
binding、effects、捕获或执行位置，不按样本/方法身份调度恢复。

语义来源是 [ECMA-262 NamedEvaluation](https://tc39.es/ecma262/multipage/syntax-directed-operations.html#sec-runtime-semantics-namedevaluation)
及 identifier assignment/class definition evaluation，自有打印实现，无新增
依赖或外部代码。匿名语法不添加 immutable class self；原外部绑定在重赋值
后仍由构造器/方法读取。比较总成本为实际候选名称字符数，action 增量空间
每个待打印表达式 O(1)，复用迭代 action stack 及位置映射。非直接 RHS、
成员属性、名称被重命名和非identifier runtime name仍走原正确的包装路径。

三profile编码模块63组行为对照与6个合法语法 mutant验证此边界；固定五输入
38份完整及15份部分源码改善，106份语法解析通过，另有13项相关定向检查。
这是正确性/表示证据，没有恢复暂停的性能/RSS实测。剩余phase/scope/capture
能力边界仍由原共同分析决定，不能以打印成功替代阶段一验收。

### 共同效果前驱与跨块类成员投影（2026-10-04）

`BuildEffectOrder` 消费真实 normal ValueIR。共同 `VerifyValueFlow` 检查图、
定义/使用与 phi 输入边，并返回本次已构造的支配树；调用者不另建支配树。
按 typed language effect、exception、environment 和 debugger 语义标记
有序效果，记录每块最后效果。incoming CSR 一次建成；无效果源块到后继的
依赖图用迭代 Kosaraju 两次 DFS 收缩 SCC，再按凝聚图依赖顺序传播效果
前驱身份。多个不同身份合并成 opaque join，效果数量相同不构成同一历史。
effects 是依赖叶，pure loop 不需要枚举特殊循环形状或递归调用栈。

算法来源为标准 SCC 凝聚与 SSA 支配性质，自有实现，没有复制外部代码、
新增依赖或第三方许可证要求。前提是已验证 reachable normal graph；不
接受 phased 图，不宣称完整异常图或全路径终止证明。私有 class 投影要求
所有被拥有的效果构成从实际创建出发的单一链，前驱支配后继、最后安装是
唯一尾部，外部观察晚于它；原 HomeObject、捕获与 descriptor 对应保留。
这允许把未被观察的成员创建投影到 class syntax；pure loop 上不额外假定
终止。循环内重复安装或条件遗漏不能满足该效果身份/支配链。

全 CFG reverse postorder 给 `DefinitionOrder`，只在上述证明后用于 syntax
排序。物理 OpId 只作索引，禁止用其数值推断移动许可。B块、E边、U使用、
N操作、V值的共同构造时间 O((B+E) log B + U log U + N+V)，存储
O(B+E+U+N+V)；投影线性普查，C个成员排序 O(C log C)。候选发现和独立
投影各构造一次并按真实费用保守记账，没有逐 class CFG 分析或二次普查。

三profile、1/200/201/402安装、纯分块/diamond/loop、物理顺序反转及
效果/遗漏/重复/伪造顺序反例是定向正确性证据。432组observer和96组capture
行为对照通过；没有延迟/分配/RSS性能实测，暂停的完整语料与性能检查未恢复。
维护风险是 phased/initializer/活跃安装结果 alias 对应尚未接入，不能靠
新增局部模式放行。日志、固定五输入和验收边界见 source-recovery/status.md。

### 类布局先行与实际 parent 树组合（2026-10-02）

`BuildClassLayout` 从实际 ClassCreation、构造器 context 和 literal buffer
解析 callable、名称、按 wire 顺序的成员 occurrence、定义位置和静态初始化
调用。布局没有 PreparedFamily 或伪造正文，只供共同 ClassExpr 建立 parent
AST。record 获得真实符号与作用域后，在其完整 native enclosure 中准备
constructor、buffer 成员、定义成员和静态初始化，原 ClassExpr 指针保持稳定。
赋值期间只有当前 builder 拥有可变引用，没有并发 reader；失败丢弃整个未打印
record。成功后对完整常量和实际构造器 callable 再作一次 TreeSemantics 检查。

布局检查只证明原操作/投影位置，不能证明缺席的正文。完成后的 operand
检查保留实际函数创建身份和 body 对应；printer、默认导出分析和 family
write barrier 需要全部正文。缺 constructor、缺 buffer 成员、缺定义成员
任一 descriptor half，或缺原静态初始化时，不能打印完整类。

令 L 为 literal entries、M 为 buffer methods、D 为定义 sites、C 为文件
occurrence index 大小。布局使用已有 DecodeClassBuffer 和精确二分 occurrence
查询；既有 literal 读取/解码成本另计，布局新增 O(M log(C+1) + D) 时间、
O(M+D) 保留元数据。literal 解码 scratch 单独 admission。record 当前先解析布局，body factory 再解析一次
同一冻结布局，重复次数为常数，不是按每个成员重扫 buffer；原方法族准备
成本另计。record 的最终语义检查仅一次，沿原操作/AST/控制索引遍历，保留
原检查自身的复杂度；没有逐 class 重建 parent 图或 tree。资源 charge 包含
布局、完整正文及验证 scratch，仍是保守 admission，不是 RSS 实测。

采用项目自有 typed occurrence 和 staged AST 构建，无新增依赖或许可证。
实际 ABC 的 IconInfo 配对执行、三 profile 的 decoded 定义成员及原有静态
字段/record 测试用于正确性验证。此改动建立外部 class capture 绑定所需
parent 树；外部 incoming cell、owner 分离和独立 capture 证明仍需继续实施。

### 普通流的原生成员调用链（2026-10-02）

复用异常流已有的 `IsMemberReceiver`：仅当原 ValueInvoke 的显式 receiver
与原属性引用的 base 相同，才将该 VM receiver 从源码使用计数中投影掉。
属性读取仍消费 base，getter、callee、独立观察者和原始效果均保留。
正常求值计划与独立 forest 遍历消费同一规则；原 CallPlan 和 TreeOperands
继续证明引用取得位置、稳定 receiver、操作数及效果顺序。DEBUGGER 隔断
引用融合，错误 receiver 被拒绝；真实额外使用仍需要快照，不能因本规则
重复求值或删除临时变量。

每条生产者 use chain 互不重叠，新增检查为每 use 常数个 typed/arena
索引及已压平 alias 查询，总新增 O(U) 时间、O(1) scratch；U 为操作数
使用数。现有求值和控制索引的成本另计，不为每次调用回溯全部链或图。
共同 AST 按原来的唯一求值所有权构建嵌套调用，没有新 writer 或依赖。
本项目自有实现；三 profile、1/200/201/400 层实际解码链、额外观察者、
副作用隔断和错 receiver 回归，以及独立 JS 和真实配对 ViewModel 为
定向正确性证据，不是全应用验收或 RSS/性能实测。

### 共享分支谓词与原生条件调用参数（2026-10-02）

正常 selection 使用已验证 ControlDecision 的单入口、共享 arm 与原边归属，
保留完整条件链。一次标记被父 decision 拥有的 predicate 节点，仅最大根
形成 selection；flat condition/block 列表保留每个原分支及其共享边极性，
不复制 CFG。条件路径要求局部闭合 inline forest、已有 guarded-module 对
或纯别名/跳转；保留 root、phi、异常相位和独立入口不能按该路径融合。
既有 selection 的逆序选择与正序效果区间方程，将整个已独立检查的条件
链视作一个有条件执行的事件，继续核对 getter/callee、参数及消费顺序。

每个 decision path 的节点由一个最大根拥有，新增 `O(N+D+I)` 工作、
`O(N+D)` 存储，N 为控制节点数、D 为实际条件路径条目数，I 为这些
互不重叠路径的操作数。scratch 索引按 N 一次分配，保留列表按实际 D
admission，源码 AST 每个条件增加常数个节点；无逐分支 CFG 搜索或
祖先集合复制。原 control/SSA/evaluation prerequisites 成本另计。
ExpressionForest 的 selection-built 索引区分原标量谓词和最终选值，
不能以“原分支已有表达式”跳过完整 conditional 构造。

TreeFlow 独立从实际 conditional/OR 语法恢复各原标量判断、极性及路径
归属；原分支的 scalar predicate 与选值表达式分别保存。operand/origin
对应检查使用原 scalar，选值用途继续检查完整 conditional，根 origin
必须为原始分支。callee 仍经共同 MemberCall/稳定 receiver 验证，不调用
可变 `.call` 属性。采用已有控制所有权和表达式区间算法，本项目自有
实现，无新增依赖、外部代码或许可证。三 profile、1/200/201/400 次调用、
DEBUGGER 路径拒绝、类型化语法/来源变异、真实构造器函数族及 Node
执行为定向证据；不是 RSS/性能实测或完整 WindowUtil 配对验收。

### 经类与函数创建链共享模块初始化依据（2026-10-02）

正常 `FunctionInput` 将已证明 Language 的首次入口 store 按原始指令排序，
建立不可变二叉 trie 前缀。槽宽 W 取自 typed `ModuleSlot` 的 wire 类型，
每次插入仅复制 W+1 个路径节点，其他分支共享。类缓冲成员保留真实
`ClassCreation` 操作数；构造器、成员及嵌套函数按原始创建点取得父级前缀。
依据只有 initialized membership，不包含初值、对象身份或非 undefined 精化。
分支 store、未来 store、不可达创建及其他 record/file/创建操作数不能提供依据。

令 S 为首次入口 store 数、M 为 raw local load 数，前缀新增
`O(S log(S+1)+SW)` 时间和 `O(SW+S)` 自有存储。创建点二分查询
`O(log(S+1))`，membership 查询 `O(W)`；透明祖先仅共享一个根引用，
不逐 child 复制全部槽、不逐 read 追溯祖先。既有 slot/store 二分索引和
SSA read 检查成本另计。工作预算逐次计入 W+1 路径及二分上界；
`allocate_shared` 的实际 rebind 控制块分配计入创建方 admission，节点
借用方仅计自身引用/索引。allocation allocator 无状态，调用期 TLS quota
由作用域恢复；释放不访问 quota，可在另一线程释放共享节点。

完整函数族发布逐边复核同一初始化前缀根及真实创建身份；类根即使没有
模块读取也必须核对外部创建边，不能通过透明根洗掉条件前提。原始所有
store 的 Language 域仍由既有输入及函数族检查封闭。仅用于正常流；
phase/resume 不继承该前提。采用常规持久化二叉 trie 与标准库共享所有权，
本项目自有实现，无新增外部代码、依赖或许可证。1/200/201/400/800、
最大槽位、预算耗尽/恢复、双线程共享与实际 decoded live-binding 源码
执行为定向正确性证据，未进行 RSS 或性能实测。

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
| 模块初始化到真实读取/创建点 | 复用完整 sparse IDF、Boolean must meet、支配遍历与持久化 ModuleSlot 集合 | 不用入口块形状或 S×C 伪造读取矩阵；FALSE 沿原 phi 输入的反向索引传播一次，零次循环不自行证明。既有 SSA/支配构造与排序成本外 O(I+V+P+B+C)，S 为 store slot 数、V/P 为实际 SSA/phi 输出，插入为固定 slot 位宽。输出可能稠密，显式计数并以工作/字节预算拒绝，不声称总成本线性。复用现有 IDF/dominator 实现，无新增依赖或许可证负担；同函数 raw read 与正常 parent 的创建 root 共用；source在当前正常 ValueProgram重算，所有store输入独立校验Language。正常/异常child共用parent root，phased parent未接入。200/201/256/512/1024 独立维度与零次/必执行循环、三profile native ESM 是正确性/有界工作证据，非延迟/RSS实测。 |
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

ValueUnary increment/decrement owns private compiler storage,
never the input SSA binding. Binding allocation counts updates in the existing
operation pass, then creates an OpId-indexed symbol table and dense UpdateBinding
entries in one pass. Rules 152 reuse the exact producer's materialized
SnapshotBinding when it owns the result ValueId; otherwise an exclusive
UpdateBinding remains. Alias identity alone does not authorize reuse.
For O operations and U retained updates, the added work is
O(O), storage O(O + U); the table is absent when U is zero. Existing byte/ID
admission includes the table, symbols, names and AST nodes.

The common AST represents one input assignment followed by native prefix
increment/decrement in a two-child comma expression. TreeFlow independently
reconstructs actual result publication and permits only that producer's
save/update/publication writes. Shared SSA reads retain the common availability
and operand proof; captures of shared or exclusive update storage are forbidden.
Postfix ToNumeric keeps its outer publication of the old numeric value.
For S symbols, X expression nodes and C capture entries the additional check is
O(O + S + X + C)
work and O(S) scratch, or a single O(X) discovery pass when no update exists.
It never searches the CFG per update or walks all uses per symbol. Existing
operand/origin checking proves the original input, new result and occurrence.
Non-NaN Number identity ToNumeric checks are O(1). General conversion/domain
admission uses the separate explicit language numeric contract where selected;
source-reference update folding is not claimed by this storage rule.
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
Fresh same-block native literals can also own an arm's phi RHS. One indexed
pass over the checked LiteralPlan records each allocation's last definition;
phi census excludes only its actual own-definition base uses. Its complete
unobserved initialization interval stays inside that edge expression, including
nested allocations/getters, while any independent suffix event remains a
barrier. Conditional discovery consumes that interval and waits for its final
definition before constructing the arm. This adds O(operations + definitions)
scratch/work per index; eligible edge suffixes and producer use chains remain
disjoint. It reuses the existing literal ownership/effect verifier and native
AST; no invocation intrinsic, IIFE, dependency or license change is introduced.
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

### Forwarded selection phis (2026-10-02)

A complete child selection can also feed a distinct parent phi through the
child's single jump-only join. Keep both CFG/SSA identities and record the exact
forwarding UseId. A single indexed live-use census proves that the child result
has no other consumer. The join must contain no live operation except pure
jumps; it cannot carry another control subtree or continuation. Parent coverage
counts this forwarding input once, rather than counting the child's leaves as
additional parent inputs. This is ordinary SSA value-selection composition,
implemented locally with the existing control postorder and no new dependency.

The common evaluation plan owns the child's phi expression at that exact edge
use. Expression construction and placement visit the child and its bridge once;
flow checking keeps parent and child copy targets separate. Operand checking
matches the actual copy destination, incoming edge, and child expression, so an
edge cannot acquire a conditional value merely because its phi was fused
elsewhere. Shared results, effectful joins, and stale/forged consumers remain
outside this composition. Normal-only handling does not extend catch domains.

The added census and checks are O(values + uses + control nodes + operations)
time and space. No per-phi graph walk, CFG rewrite, or text-based recovery is
introduced. Three-profile decoded member-call cases cover 1/2/200/201/402
consecutive selections, both getter/argument orders, effectful bridges, and
forged consumers/conditional arms. Native JavaScript is the independent behavior
oracle; this does not establish full paired-record or platform acceptance.

### Mixed ordinary and selected literal fields (2026-10-02)

Extend the existing selected-object route with ordinary definitions before,
between, and after its selections. Index each selected RHS's containing field
and each object's first selection once. Only that first selection may visit the
object's full definition/use list; later candidates cannot repeatedly rescan it.
Walk fields in their original definition order, advancing from a selection's
header to its join only at the indexed selected field. Ordinary fields must stay
in the current block. Multiple selections feeding the same field and unrelated
control gaps remain outside this route.

Reuse the forward/backward event-interval checks for the whole object and its
external consumer. A previously checked local literal is one expression event:
its nested children and non-staged initializer operations are not counted again.
Its LiteralPlan still independently proves ownership, input snapshots, and
internal order. Staged outer definitions remain separate events after changing
their placement to initializer, so builder and verifier count the same sequence.
This composes existing literal proofs; it does not erase arbitrary allocation
effects or permit observation of an unfinished object.

The additional indexes and passes cost O(operations + uses + fields + selections)
time and O(operations) space. The implementation is a local extension of the
existing expression interval algorithm, with no new dependency or license.
Three-profile decoded tests exercise both many objects and many interleaved
definitions in one object at 1/2/200/201/402; at 402 the existing evaluation work
is below three times the 201 case. Native syntax independently checks repeated
keys, allocation identity, throwing getters/constructors, receiver, new.target,
Proxy construction and callee replacement. These are bounded correctness/scaling
checks, not peak-RSS, benchmark, or full application acceptance.

### Staged object ownership trees (2026-10-02)

Index the sole external consumer of each staged object, excluding only its own
definition-base occurrences. A child may join a parent's group only when both
objects have one eligible external consumer and the child's actual consumer is
the stored RHS of a parent definition. Require forward allocation/definition/use
order. Parent allocations precede children, so one forward pass resolves every
root without repeated ancestor walks. Shared or otherwise ineligible parents
do not acquire child ownership.

Prefix-summed ranges group allocations and merge field definitions in original
operation order. Each root's first selection visits that group once. The existing
selection route retains the original blocks and each conditional arm; the event
domain maintains a separate interval for every allocation, extending it at that
object's own definitions and consuming it at the exact parent field or final
external use. Constructor, getter, branch, and unrelated-effect events retain
their existing checks. The AST still owns one ObjectExpr per original allocation
and records each object's own last definition, not a shared synthetic object.

Indexing is O(I+U+F+L) time and O(I+F+L) space for operations, uses, staged fields,
and literal objects; arrays share the existing budget. It extends the project's
typed expression-ownership/event-interval algorithm, with no external dependency,
runtime wrapper, graph rewrite, or license change. Three-profile decoded tests
cover nested depths up to 201 within actual V8 operand limits, 200/201/402 separate
trees and siblings under one parent, shared-child rejection, effect gaps, and
forged child consumers/last definitions. Existing evaluation work at 402 remains
below three times the 201 case. Native syntax checks identity, prototypes,
descriptors, branch effects and member references; paired DetailPage callbacks
are separately bounded evidence. No RSS, benchmark, or whole-platform claim.

### Creation binding lookup across AST schedules (2026-10-02)

Literal dependency construction may visit a later closure before an earlier
ordinary root. Resolve FunctionBinding by its original creation OpId with binary
search in the sorted unique map already checked by evaluation verification.
Do not interpret the AST build sequence as instruction or binding order. Retain
the exact child owner, capture list and complete constructed-function census;
the existing placement and scope checks still prove runtime order and lifetime.

For F creations this lookup adds O(F log(F+1)) comparisons and constant lookup
storage, reusing the existing vector and standard-library lower_bound. There is
no copied map, new dependency, or runtime helper. File-backed decoded families
exercise three profiles and 1/2/200/201/402 groups of three mixed closures. The
largest synthetic family has 1,207 members and uses an explicit 128 MiB cumulative
preparation allowance; the standalone 64 MiB default reports resource exhaustion
for that fixture. Production limits are unchanged. Whole-family work at 402
groups stays below three times the 201 case, without making an RSS claim.
Native behavior tests independently cover closure names/identity, distinct and
shared captures, writes after return/throw, and separate factory invocations.
Malformed creation order, duplicate IDs and self owners remain rejected at the
evaluation/expression boundary; the family entry itself rebinds from its inputs.

### Guarded module reads in statement decisions (2026-10-02)

Normal shared-arm decisions retain an already verified module guard's placement
at the same ExprId as its read. The RHS remains conditional; no separate guard
statement or unconditional module access is introduced. TreeFlow's existing
ModulePairs check independently requires the exact read/guard expression match
before decision traversal consumes that pair without an additional statement.
Phased staging/handler admission is unchanged.

This is constant work per existing operation, with no new index, allocation,
dependency, or runtime helper. Three-profile decoded AND/OR statement decisions
cover direct module values and property reads at 1/2/200/201/402 repetitions.
Missing/removed guards and forged guard-expression placements remain rejected.
Independent native syntax tests compare short circuiting, getter/call order,
exceptions and lexical TDZ, while paired getPaddingTop tests exercise a mutable
module export under a bounded host contract. Neither substitutes for Ark VM or
full platform acceptance.

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

Source owner allocation separates file-family occurrence indices from AST owners.
The project-local affine map `first_owner + occurrence` takes O(1) time/space per
lookup after a checked uint32 range. Discovery, ordinary/phased rebinding and
write verification retain their existing walks; no owner-to-method search or new
graph pass is introduced. Class/record composition advances a uint64 cursor once
per family and sums the actual member counts once, O(F) time and O(1) additional
space for F embedded families. Exhaustion is a typed resource refusal, never an
owner wrap or alias. The standard-library representation adds no dependency or
license requirement. Tests cover repeated creations at 200/201/400/800, shifted
nested captures and the maximum-owner boundary; these are functional checks,
not whole-process RSS or benchmark evidence.

Class capture-point dispatch extends the existing project-local capture binder
with a two-alternative typed site. Matching the original class instruction and
working class operation takes O(1) work; owner-stamp equality is bounded by the
fixed occurrence fields. No class buffer or metadata table is rescanned per
cell. Sparse target storage and original/work environment queries retain the
capture-binding bounds documented in source-recovery/capture-binding.md,
including zero-access forwarding obligations from the family propagation pass.
Normal and original Before-phase matching share the same initialization/domain
rules. Decoded positive/negative fixtures exercise all three class-root forms
and genuine descendant forwarding. This adds no dependency, allocator scheme,
writer or whole-process memory/latency claim.

External class-root discovery reuses the completed `PlanFamilyCells` pass, then
performs one binary PC lookup in the verified original creator instruction
sequence, O(log(I+1)) for I instructions. Defined members additionally use the
existing indexed `BindCreation` queries; constructor/buffer members use their
stamped original class context. One shared binding/independent check is retained
per root, O(K+U) storage for K original/forwarded obligations and U incoming
cells, with the indexed rebasing costs above. There is no second descendant
discovery or per-capture method scan. External evidence is not permission for
closed-root AST or transitive-write publication.

Class-root source anchoring uses the actual parent TreeFlow expression index.
Constructor and buffer occurrence checks take O(1); defined-member checks use
the ordered ClassDefinitionTree inventory in O(log(D+1)) for D definitions.
Buffer contexts retain the immutable ClassSiteId so there is no per-member scan
of all methods or properties. The common alias binder/independent verifier keep
O(K+S) work/storage for K incoming cells and S copied/compared UTF-16 units,
plus the single anchor lookup. No new capture-name lookup or cell scalarization
is introduced. Normal records prepare the parent flow once before all class
members, retaining its existing linear indexes and conservatively charging its
verification output/scratch. Post-default root binding uses the same indexed
original-point resolver as discovery and checks fresh revisions/storage. This
is project-local C++20/standard-library code with no dependency or writer, and
does not claim whole-process RSS/latency or final scope/write publication.

Class-root integration retains the owned rebased CaptureCells in the constructed
family and independently rechecks original initialization, current aliases and
all Language stores/descendant barriers in the shared FamilyWrites traversal.
The final parent structure traversal resolves root captures with the existing
active-spelling scope index, O(1) per qualified symbol. External reads in all
prepared class-member descendants query that parent's sorted spelling index in
O(log(S+1)) comparisons for S parent symbols; compared UTF-16 lengths are counted.
Each child builds a symbol-indexed module-target table once, O(N+M) time/storage
for N symbols and M module aliases. There is no per-read alias scan or per-member
parent-tree replay. Beyond the existing parent checks this is O(C+N+M+E log(S+1))
work, plus spelling units, for C root captures and E external expressions, with
O(S+max(N)) additional scratch; budget charging remains conservative across
released child tables. Printer passes the actual parent ExprId as syntax only;
class-operation identity is stamped by layout construction, not inferred there.
Project-local C++20 code, existing active-scope/standard sorting algorithms and
licenses; no new dependency or whole-process performance/RSS claim.

Literal-expression eligibility also admits ValueGlobalRead with kName and zero
operands, using the existing GlobalNameExpr lowering and scope checks. kVar is
not name syntax and remains refused. The eligibility test is O(1); it adds no
index, graph walk, snapshot or new dependency. Existing linear reverse ownership
propagation, canonical use census and laminar interval traversal still check
single consumption, unobserved allocation, source operand/property effect order
and all folded exceptional sites' dispatch/catch copies. Decoded 1202/1206/1301
fixtures cover 200/201/402 fields, buffered/unbuffered objects and direct/member
reads; measured verifier work at 402 remains below three times the 201 result.
This is work-counter scaling evidence, not whole-process RSS or latency proof.

Decision evaluation prefixes may contain closed conditional-value regions. The
existing follow tree supplies nearest matching predicates: queries are indexed
by evaluation entry and shared target, and one iterative DFS maintains target
slots with per-frame rollback. This adds O(V+E) work/storage after the existing
follow/LCA construction, rather than walking ancestors for every branch. Region
tasks stop before the ending predicate while retaining its explicit continuation;
that predicate and the effectful shared body each retain one owner. Independent
control verification traverses the bounded prefix and checks original arcs.

Selection discovery memoizes arm tails in O(N) control-node work. A containment
forest orders closed prefix selections before their owner in O(I+R log(R+1))
work and O(I+R) scratch for I operations and R selections. Reverse local-expression
indexes connect adjacent selectors and propagate their final object-field owner.
Each attempted selector chain is marked to avoid repeated failed suffix walks;
existing interval and object-group checks still enforce single consumption and
original effect order. PhiInput suffixes count as consumers, and a selected
logical LHS has exactly its predicate occurrence and matching keep-edge copy.
These are project-local C++20 extensions to the existing algorithms, with no new
dependency. Three-profile decoded fixtures exercise 1/2/200/201/402 compositions;
402 evaluation work remains below three times 201. This is bounded work-counter
evidence, not physical RSS or latency measurement.


### Conditional primitive normal-result facts (2026-10-04)

The existing finite-height sparse value solver stores one typed normal-result
fact per operation node, separately from literal seeds. Boolean/null and typeof
refinements become available only after their input obligations hold. Ordinary
and phased adapters build this same node representation; linear lexical flow
calls the same fixed-width coercion/arithmetic transfer helpers. There is no
per-value CFG scan, interval solver, producer-layout admission or new dependency.
Costs remain O(values + dependency edges) times the fixed lattice height; actual
ValueNode size is admitted by existing scratch accounting. Boolean/null joins
add a fixed refinement branch, not input-dependent lattice height. Numeric
conversions/arithmetic inspect at most two operands. The source verifier checks
legacy negation using the same literal/domain semantics, without a separate
producer list. Source fixtures include 200/201/402 phi dimensions and bounded
work tests. These are correctness/accounting evidence, not RSS or latency
measurements; full corpus/RSS/benchmarks remain paused. Provenance and coverage
are recorded in source-recovery/numeric-facts.md.


### Joint phased call selections and ancestor references (2026-10-04)

Let I/B/E/V/U be original operations, phase blocks, edges, values and uses;
S selected-argument candidates; A actual ancestor-call relations; Q original
exception-staging copy entries compared. The existing ValueProgram, canonical
operand/def-use index, phase evaluation and SelectionRegion proof stay primary.
Group candidates by actual call and argument UseId, in O(S log(S+1)) ordering
work. A call's original reference encloses every selected argument. Each
argument owns its own decision, original phi edges, optional pure transport,
and ordered prefix; walk successive Commit/header/join segments once, then
require the final normal path to reach the call without a foreign event/copy.
Reject the whole group on failure. Do not limit selection or argument counts.

Index normal corridors once in O(B+E+I+Q) work and O(B+I) storage, recording
positions before each block, homogeneous effect-owner runs, original exception
scope consistency, and the outgoing interval before an endpoint is entered.
The endpoint may have other predecessors. Root identity comes from the existing
checked inline forest, not opcode/name matching. Query a reference prefix in
O(1) plus the original catch comparison: every intervening effect must belong
to its child reference's root, without normal or catch copies. Pure closed
cycles remain unindexed; this retains the earlier linear-corridor coverage gap.
Memoize each actual inline consumer/ancestor relation once, storing the union
of accepted references once across selected records. There is no graph walk
per argument or copied ancestor list per selection.

Additional storage is O(I+B+E+V+U+S+A); candidate ordering and existing binary
lookups add O(S log(S+1)), while indexing, disjoint prefix census and ancestor
queries add O(I+B+E+V+U+A+Q). Discovery, alias/phi, control, source-tree and
scope validation retain their separate costs; this is not a whole-pipeline
complexity claim. New index arrays/capacity growth, planner scans and candidate sorting
comparisons are charged to the shared byte/work quota. Existing AST lookup
and consumer validation retain their own charging granularity. Provenance is the existing source evaluation,
normal-CFG/SSA and phase corridor model, with no external code/dependency or
new license. The original single-entry candidate/path restrictions remain
explicit coverage gaps, not language or VM invariants.

joint_selection_call_test covers three profiles, 1/2/3/50/100/200/201 arguments,
1/2/200/201/402 copies, outer member calls and rejection/mutation boundaries.
Recorded evaluation work for 50/100/200/201 arguments is
538235/1062180/2110010/2120488; the test checks geometric growth. It also checks
exact verifier work admission, one-step-short failure, and zero-byte refusal.
These are bounded regression/work-accounting checks; paused benchmarks,
full-corpus peak RSS and aggregate process-memory validation have not resumed.


### All-path inert phi-expression tails (2026-10-04, rules 88)

IndexPhiExpressionInputs consumes freshly checked PhiCopies and checked native
literal/module indexes. Let I/B/E/U/P be original operations, blocks, edges,
uses and phi bundles. Census each live original observation once, excluding only
already verified expression-owned module guards and literal definitions. The
common typed EvaluationShape marks normal control separately from language
expression ownership. An inert operation has no normal/pre-throw language
effect, no exceptional completion or environment transition; memory/protocol
reads do not become inert just because their effect flags are empty.

For every inert block, index its non-copy normal successors and reverse edges.
Seed endpoints at the actual normal phi-copy edge. A backwards worklist proves
that every successor must reach the same edge: foreign effects, catch entries,
terminal paths, competing copy edges and unresolved cycles fail the proof.
Each block is queued once and every indexed edge is processed at most once.
Query a phased producer at its actual Commit; query a normal producer after its
checked owned interval. Neither query requires a particular jump tail, unique
predecessor, adjacency or producer whitelist. Original exception staging stays
at its site, and the eventual source tree independently checks original scope,
operands and effect order. The index is not publication permission by itself.

Work and additional scratch are O(I+B+E+U+P), charged through the existing shared
byte/work budget. The normal query scans only a block's bounded original edge
range; the phased guard fallback likewise inspects that original operation's
outgoing phases. No per-phi value-use or graph scan remains. Single-copy bundle
and literal-ownership adapters retain documented coverage gaps; their existing
costs and discovery/source-tree costs are separate. Provenance is the existing
ValueProgram/evaluation semantics and backwards must-reach data flow, with no
external implementation or new dependency/license.

Regression coverage includes pure branching/merging, early return, divergence,
foreign debugger effects, repeated observations, intervening copies and
1/200/201/402-block chains. Joint member-call evaluation work at
50/100/200/201 arguments is 574601/1133946/2252576/2263762; geometric checks pass.
These checks do not claim latency or full-process RSS; those remain paused.


### Read-only phi/copy identities and observation projection (2026-10-04, rules 89)

AnalyzeValueIdentities reuses the iterative Tarjan implementation underlying
SimplifyPhis. The ordinary mutating pass still operates on phi equations only;
source projection also includes live, pure ValueCopy equations. Index their
actual incoming OperandUse dependencies once. An SCC aliases its members only
when all external dependencies resolve to one canonical ValueId anchor and that
anchor's value domain is admitted by every member. Multiple anchors, literal
operands without an identity, unanchored cycles and domain mismatch retain their
original identities. No producer replay or structural compiler-pattern premise.

For V original values and U uses, this index takes O(V+U) work and storage.
Each vertex/dependency is visited a constant number of times; dependency-order
SCC completion produces flat canonical roots without repeated global scans.
Normal simplification rewrites checked occurrences afterwards. Read-only source
projection preserves original phased SSA and every original occurrence. Existing
language parameter/receiver/operation-result bindings can anchor source aliases;
unresolved mutable phi and catch bindings retain first-snapshot/staging ownership.

PhiCopies retains complete original copies/UseIds and a typed per-copy projection.
Build and Verify independently compute identities; the shared parallel-copy
algorithm schedules only remaining writes. expression_input names the original
UseId when exactly one executable copy survives. Original exceptional staging
and scope verification remain mandatory. The O(V+U) identity passes are bounded
constant passes, not one graph traversal per phi, edge, argument or producer.
Genuine multiple-copy RHS expression ordering remains a coverage gap.

IndexEvaluationUses indexes canonical producer observations in O(I+U log(G+1))
work and O(I) result storage, for I operations and G checked receiver guards.
Already proved alias/identity-phi transport is not an observation; actual final
uses still are, including uses transported across branches. Module/literal/class/
cell/receiver projections retain their independent proofs. Builder and verifier
recompute this census instead of repeatedly walking raw producer use chains.
All new scratch/capacity and index work use existing byte/work quotas. Retained
projection bytes are included in the plan's allocation report.

Provenance is the existing SSA, value-domain, iterative Tarjan and parallel-copy
implementation, with no new external dependency, license or maintenance surface.
Tests cover anchored/unanchored distinction, mixed anchors, copy/phi SCCs,
1/200/201/402 values and exact original-occurrence preservation. Joint recovery
covers three profiles and 435 cases. Recorded evaluation work at 50/100/200/201
arguments is 625937/1234982/2453012/2465192, with geometric checks and exact quota
negative cases. These are scoped work-accounting checks, not whole-pipeline
latency, allocation measurements or full-process RSS; those checks remain paused.


### Module phi-expression admission (2026-10-04, rules 90)

Module reads consume the same checked observation and original incoming-edge
proof as other producers. Remove unconditional phased module root pinning and
its inline-only type exception. IndexModuleEvaluations, VerifyModulePhaseOrder
and VerifyModuleCatchCopies still prove the original binding/guard, events and
catch staging; final source-tree checks retain original operands and scopes.
This introduces no algorithm, dependency, scratch index or repeated scan. It
reuses rules 88/89 tail, identity and observation analyses with their existing
cost bounds. Multiple observations and foreign effects do not become movable.
Tests exercise three profiles, 72 positive decoded module-selection cases,
18 debugger barriers and 9252 independently evaluated native scenarios. Full
process RSS and performance benchmarks remain paused and unmeasured.

### 类 self 私有初始化和原捕获 occurrence（2026-10-04，规则93）

FindClassSelfStores共享BuildEffectOrder的SSA/dominance、效果前驱身份和
全路径下一效果必达索引，证明实际allocation→creation→store私有区间。
任意分块、纯diamond及逆物理布局不构成第二准入条件。store保留真实cell
身份；call支配全部cell读取。早期观察、路径绕过、夹杂效果和divergence
不能改变初始化时机。候选仍要求原fresh base class和现有初始化器正文
接口，不宣称任意有继承/调用/多次写入的initializer已恢复。

ClassSelfOrder逐真实初始化器验证同文件/context/ABI及LiteralClassFields，
建立只对原class创建occurrence有效的Language capture事实。证书绑定原
input及working arena/revision，ScopeFacts不使用任意PC区间猜测跨块归属。
FamilyWrites用一个operation-owner索引核对已准备ClassExpr中的真实方法
context和body，闭合original class/method barriers；不是可转交的打印许可。

现有ProveLocalAccesses的无环初始化路径改用共同OrderAcyclicBlocks判断
实际cycle。来源为已有自有Kahn O(B+E)实现，无新依赖/许可证。排序scratch
O(B)，在当前remaining memory内使用并在支配构造前释放；失败cycle也
保守charge三次block/edge访问上界。真实循环仍需activation-sensitive
初始化transfer；原一般cell的PC扫描限制尚未全部替换。

共同效果构造复杂度沿用前节；self候选/访问和capture-site索引线性扫描，
cell查找O(log C)，没有逐class CFG遍历。1/200/201/402定向work检查是
算法增长回归，不是已暂停的全流程峰值RSS或性能基准。

### 原生函数名称的语法上下文（2026-10-04，规则98）

共同TreePrinter在既有结构/函数所有权验证后预计算每个子函数的命名选择。
来源是ECMAScript的IsAnonymousFunctionDefinition/NamedEvaluation：
[identifier赋值](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-assignment-operators-runtime-semantics-evaluation)
及[对象数据属性](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-object-initializer-runtime-semantics-propertydefinitionevaluation)。
这只核对输出语言语法，不引入新ISA/profile或依赖；原创建元数据、self、
invocation、capture和发布证明保持，在线规范草案不替代锁定的Ark行为。

先线性扫描所有表达式，赋予无推断上下文，再扫描直接声明/赋值/属性/字段
的函数操作数。设表达式X、属性P、语句S、函数成员F、名称比较字符总量L，
新增工作O(X+P+S+(X+P+S)log(F+1)+L)，新增scratch O(F)字节。
复用既有排序owner索引；所有扫描、比较及native选择数组均计费。不逐绑定
重走子树、def-use或CFG，不从源码片段匹配命名许可。预输出资源校验仅在
最终需要包装时触发；独立fragment无目的语法证明，保持原命名策略。

1/200/201/402成员整体printer work253/35949/36132/73353；精确work
成功、少一单位拒绝。648组名称/调用行为及可变外部捕获对照保持名称属性
而不引入内部self；实际配对回调另外检查词法receiver。全流程RSS和性能
基准继续暂停；计费scratch及work不是进程峰值或吞吐测量。
## Acyclic local storage order and capture exposure

Local cell SSA now reuses `OrderAcyclicBlocks` (Kahn, O(B+E) time/O(B)
storage) instead of comparing branch target PCs. This proves single activation
for this existing normal, handler-free adapter. It is a coverage boundary;
cyclic/exception environments require a richer transfer model. No dependency
or license changes: the shared in-repository ordering implementation is retained.

The adapter assigns optional typed ranks to reachable original instruction
sites and propagates a Boolean exposure fact across each block/edge once.
Unpreserved callable/class/buffer creation invalidates subsequent reads on
every path where it occurs. Preserved creation leaves the fact unchanged.
The additional cost is O(N+B+E) time/O(N+B) storage over existing environment
binding and sparse SSA costs; capacities are admitted and retained read/rank
arrays are included in `LocalSsa::AllocatedBytes`. This is not a dense cell by
site matrix, a per-cell graph walk, or a whole-process RSS result.

Receiver suffix and defined-capture consumers summarize writes by the shared
rank in one indexed access pass. Lower rank establishes that a store cannot
execute after a higher-ranked site in this DAG. Candidate receiver stores still
owe actual capture dominance; rank proves neither dominance nor equivalence of
incomparable histories. Finer creation-point cell snapshots remain necessary
for general incomparable writes. Family queries keep typed original block IDs:
cross-block availability uses dominator intervals, while a same-block query
uses actual site order. The existing indexed lookups remain logarithmic.

Complete decoded constructor families test two reverse layouts, both guard
modes and 1/200/201/402 repeated reads/member calls. Native source behavior
tests separately cover receiver/effects/errors. Scaling assertions compare
charged work across the geometric cases; physical memory and full-corpus
benchmarks remain paused.


### Native callable tail completion (2026-10-04, rules 100)

The shared syntax projection inspects only the last direct child of the actual
function body. A bare return or typed undefined literal can complete through
native fallthrough without changing the verified FunctionTree, control plans or
instruction ownership. Earlier/nested returns, symbol reads, compound operands,
calls, suspension expressions and finally returns keep their original syntax.
This is a language completion rule, not a bytecode sequence or CFG admission
pattern. Static-field and implicit-constructor projections retain their separate
position conventions.

Provenance: ECMA-262 2024 [ReturnStatement evaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-return-statement-runtime-semantics-evaluation),
[GeneratorStart](https://tc39.es/ecma262/2024/multipage/control-abstraction-objects.html#sec-generatorstart),
[AsyncGeneratorStart](https://tc39.es/ecma262/2024/multipage/control-abstraction-objects.html#sec-asyncgeneratorstart)
and [AsyncBlockStart](https://tc39.es/ecma262/2024/multipage/control-abstraction-objects.html#sec-asyncblockstart).
An async-generator return operand has an implicit Await even when it is
undefined. That return must remain; its bare return has no such operand.
Checking only eventual values or iterator request order misses this job boundary.
Independent microtask markers distinguish completion before/after the marker.

`ImplicitTailCompletion` (formerly `PureTailCompletion`) takes O(1) work/space per callable and reuses existing flat
tree tables. The printer writes the original return/literal at one zero-width
byte boundary before its closing block; emission checks that boundary within the
real owner/body and after the prior sibling. Serialization reports
`tail_completion`, retaining the existing `module_completion` label for module
roots. No fabricated expression range or deleted instruction origin is needed.
No dependency or external implementation is added. Tests cover six native
callable kinds, retained effects/control/finalizers, asynchronous job order and
queued iterator requests; actual paired HomeModel verifies source style and
observable callback behavior independently.


### Constructor receiver tail completion (2026-10-04, rules 103)

Extend the same constant-time projection with the typed base/derived callable
kind and a direct ThisExpr owned by that constructor. No CFG walk, bytecode
pattern, inferred function name or additional admission premise is introduced.
The original AST return, operand and instruction-source occurrences remain;
printer, emission checks and position serialization share the same query.

Provenance: ECMA-262 2024
[Construct](https://tc39.es/ecma262/2024/multipage/ordinary-and-exotic-objects-behaviours.html#sec-ecmascript-function-objects-construct-argumentslist-newtarget).
Explicit return of the bound receiver and native fallthrough return the same
object, including a replacement object returned by super and alternate newTarget.
Uninitialized derived this throws in both forms. Reads inside try/catch/finally
must stay there; dropping them could change handler/finalizer behavior. Earlier
returns, alias/property reads, compound operands and ordinary callable receiver
returns remain. More general redundant-completion elimination needs control and
exception ownership proof; this syntax projection does not assert that gap closed.

No dependency, imported implementation, cache or license change. Maintenance
requires retaining callable/owner identity, original provenance and exception
boundaries. Native syntax oracles cover 24 base/derived cases and 8,640 identity,
effect and exception scenarios, independently of actual paired decoded
constructors and full HomeModel record behavior/style checks. Physical RSS and
larger corpus benchmarks remain paused.

## Generator preserving completion syntax ownership (rules 101)

`StatementChild` exhaustively enumerates typed AST children for both actual
block storage and completion sequence views. An iterative Euler traversal builds
statement entry/exit intervals once per projection. With S statements and C child
occurrences, construction is O(S+C) time/space; ownership queries are O(1).
Compared with walking each terminal's parent chain, nested independent completion
sites do not add repeated ancestor walks. Label assignment uses the same child
interface, O(S+C) work/space, with no recursive C++ stack or function-boundary
crossing. Existing byte/work admission covers arrays and owned label capacity.

The source primitive is a labelled block with a typed break at the original
preserving RETURN site. RETURN arm selection uses common branch polarity.
Origins and original terminals remain for the inverse SSA/control audit.
Prerequisites: pinned resume semantics, fresh flow, valid syntax ownership and
independently checked finalizer routing. Existing same-sequence yield/guard and
following placement restrictions remain coverage gaps; this change does not
claim arbitrary coroutine layout support.

Semantic provenance: ECMA-262 2024
[14.13.4 LabelledEvaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-labelled-statements-runtime-semantics-labelled-evaluation)
absorbs the matching break; [14.15.3 try evaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-statements-and-declarations.html#sec-try-statement-runtime-semantics-evaluation)
executes finally and preserves the prior completion when it completes normally.
The traversal and label implementation are repository-owned, with no imported
code, new dependency or additional license obligation. Maintenance requires
exhaustive updates when adding statement variants. Directed decoded generator
and block-target tests exercise 200/201/402, effects, identity and exceptions.
The 402 return-selection fixture still exhausts the existing cumulative 64MiB
admission during family enclosure; this counter is not a measured process peak.
Full RSS/throughput and accounting redesign remain paused.

## Normal cell initialization regions and exposure (rules 102)

Reuse the repository's simple Lengauer-Tarjan dominators, Euler intervals and
Kahn DAG order. Allocation must dominate each initializing store; instruction
coordinates order sites only in the same actual block. Sort candidates by cell,
dominator entry and same-block store coordinate, then retain outermost disjoint
regions. A binary interval lookup proves initialization at a read/creation, not
the cell's reaching value. Every store keeps its separate Language obligation.
Sibling stores do not prove initialization of their join without an all-path
transfer proof. Cycles retain the existing activation-local admission boundary.

An entry-reachable DAG pass assigns typed InstructionRank coordinates and
propagates the maximum unpreserved capture rank along actual successors. The
state before each instruction excludes that instruction's own capture. Once
allocation/initialization ownership is independently proved, comparing the
incoming capture rank with the dominating allocation rank detects a possible
exposure since allocation. Unrelated sibling captures never enter that path;
captures before a fresh allocation refer to the earlier environment. A rank
comparison alone is not dominance, path membership or a reordering license.

For I instructions, B blocks, E edges and A lexical accesses, cost including the
existing dominator build is O((B+E) log(2+B) + A log(2+A+B) + I) time and
O(I+B+E+A) storage. Queries cost O(log(2+A)); exposure after the common pass is
O(1). No per-cell or per-creation CFG walk is added. Existing capacity and work
admission covers retained arrays and conservative scratch; it is not process
peak RSS. Physical RSS and larger corpus benchmarks remain paused.

Algorithm provenance is the same dominator/Kahn implementation documented above;
no external code, dependency, license change or version-unpinned adapter is
introduced. Maintenance requires preserving the original block/site ownership,
Language-store checks and exposure transfer when extending cell lifetimes.
Decoded fixtures exercise 1/200/201/402 disjoint regions and reversed block
layouts; generated common-tree source has independent identity/effect tests.

## Native nullish syntax (rules 104, 105)

PlanNullishSyntax (formerly PlanOptionalAccess) interprets typed meanings over
null/undefined/other, tracking a stable SymbolId or ThisExpr owner, pure guard
truth and original expression outcomes. A typed variant retains an original
optional member, or a value/fallback pair for native ?? through the existing
LogicalExpr writer. Provenance is ECMA-262 2024 13.3.9 and 13.13.1, Kahn
scheduling and the project's finite abstract interpretation; no external code,
new dependency, licensing change or unpinned profile adapter is introduced.

Each expression has one syntax owner. At most three semantic dependencies per
node allow a bottom-up queue independent of arena order, followed by one
outermost projection pass. Opaque fallback calls, members, sequences and foreign
conditional subtrees remain original occurrences; different effectful nodes are
never equated from spelling. A foreign predicate is retained even when its
outcomes agree, because its read may throw (TDZ). A conditional is an erasable
predicate only if every possibly evaluated arm is pure in that same reference
domain. Known final truth after OR/AND does not imply all earlier reads are
exception-free. No sample, fixed guard count, operation sequence or CFG layout
is a proof premise. Stable-reference initialization/effects remain owned by the
original verified tree and common engine analyses.

Equal literals and stable references use typed value identity. Number payloads
remain exact, including signed zero; Boolean false is not integer zero. Text
IDs are canonicalized once by sorting borrowed UTF-16 contents, then selection
joins compare IDs rather than rescanning long strings per candidate. For N
expressions, A argument slots, P properties, M text entries and C total UTF-16
comparison work, time is O(N+A+P+M log(2+M)+C), storage O(N+A+P+M). C is charged
conservatively by compared minimum lengths; it is not hidden in a constant-time
string comparison claim. The three-domain state has fixed size. Trees without
a selection, stable reference or nullish literal have an allocation-free O(N)
pre-scan. No per-candidate subtree replay or per-variable CFG walk is added.

A second queue streams full omitted syntax ownership, including variadic calls
and aggregate properties. Each omitted expression is queued once. Retained
value/fallback or member/base/key ownership stays independently printed. Ranges
are finalized linearly at selected base.end; family emission and streamed JSON
share that same typed plan. Existing conservative byte/work accounting covers
plans and scratch, without a new default quota or process RSS claim.

Maintenance must preserve nullish-only fallback execution, receiver/identity,
TDZ and effect/exception order, optional-chain termination and mixed logical
operator grammar. Remaining representation gaps include effectful first-read
snapshots/aliases, independently repeated effectful fallback occurrences,
optional call leaves, whole-chain compaction and omitted child-function owners.
Constructed syntax fixtures exercise 1/200/201/402 guard composition, reverse
arena order, equal/different texts, signed zero, foreign-binding TDZ, call/getter
exceptions, opaque fallback sequences and outside value calls. Actual decoded
HotListItemView and retained-selection methods receive separate paired behavior
and source-style checks; fixtures are not decoded-body publication evidence.


## Ordered reference formation in nullish syntax (rule 107)

The existing ECMA-262 2024 assignment/sequence semantics supply the transfer:
a SymbolTarget assignment returns its stored RHS value. Preserve that entire
original ExprId, including arbitrary RHS effects, scope checks and exceptions.
Each null/undefined/other domain carries a fixed-size formation/read/order
summary. Sequential composition and short-circuit/conditional joins establish
that every possible path retains the same occurrence before its guarded reads.
Moving it ahead of an old-value read, dropping a distinct write or making a
conditional formation mandatory cannot authorize native optional access.

This extends the existing finite interpretation and Kahn ownership traversal,
not a bytecode/assignment sequence matcher. It adds no producer whitelist,
selection count, register/PC/block condition, dependency or copied third-party
code. Assignment RHS ownership remains opaque to the guard proof and fully
retained by the original AST. OptionalMemberSyntax explicitly retains its base
and original member/key; all discharged reads retain zero-width event ranges at
that base.end. Shared printer/emission/serializer consume the same plan.

For N expressions, A arguments, P properties, M text entries and C compared
UTF-16 work, complexity stays O(N+A+P+M log(2+M)+C) time and O(N+A+P+M) space.
The new per-domain state is constant size and counted by the existing budget;
no per-candidate graph or expression replay is introduced. 1/200/201/402 guard
fixtures, reverse arenas and 201-to-402 geometric work checks cover scaling.
They are not process RSS, latency benchmarks or full-corpus evidence.

The current native base refers to one retained original expression. Combining
separated formation effects, general alias transport, independent effectful
fallback occurrences, optional call/whole-chain syntax and omitted child owners
remain representation gaps. Original condition source continues to print for
unprojected cases; this is not an admission limit on complete function recovery.
Maintenance must preserve execution count, receiver/key order, target checks,
TDZ, exception identity and original producer/instruction provenance.

## Original member spines in nullish syntax (rule 145)

The shared finite interpretation proves the same original null/undefined guards
and ordered formation as above. A bottom-up MemberPath records the stable
SymbolId/This owner and first guarded MemberExpr. MemberExpr.base and
CallExpr.callee join the existing Kahn dependency queue; classification must
wait for the callee's summary even when the expression arena is reversed. No
sample identity, opcode/register sequence, CFG shape, same-block condition or
producer whitelist is added. Keys and arguments remain original opaque owned
expressions. A terminal member's direct stable base was a representation gap,
not a language requirement.

Typed optional member/call projections retain the original terminal member,
first access and original forming base. One iterative printer action emits
each original member in order, putting ?. only on that first access. Later
mandatory members preserve TypeError; an intermediate group would end the
chain and an extra optional link would suppress that exception. Original
member-call receiver, key coercion/getter/argument effects, execution count and
raw throw identity remain unchanged. Outside members/calls still end the
projection. All retained member nodes keep nonzero origin ranges; omitted
guards and reads keep their existing zero-width boundary at retained base.end.
Printer, family emission and position serializer share one plan, without AST
copies, recursive chain traversal or a separate source writer.

For N expressions this adds O(N) summary storage, charged by TreeCheckBudget,
and O(N) dependency/summary work. Including original arguments/properties/text
interning, planner time remains O(N+A+P+M log(2+M)+C) and storage O(N+A+P+M),
with C the previously documented UTF-16 comparison work. Printing visits each
retained spine node once plus its actual source output; pending origin/key
actions are charged to the existing stack budget. There is no per-candidate
spine walk. This is the project's ECMA-262 2024 Optional Chains interpretation
and Kahn implementation; no copied dependency or license change is introduced.

96 common-tree fixtures exercise depth 1/2/3/200/201/402, computed/named keys,
forming assignments, member calls and reversed arenas. 201-to-402 work checks,
57,500 independent behavior cases, origin serialization and four valid-JS
mutations cover the new boundary. Three profiles supply 276 actually decoded
functions (five new compound/copy/split forms) and 61,824 behavior cases. Actual
ShoppingAvoid inner padding chains now print natively and the ten-module
navigation graph passes, but separated formations/general aliases, whole
chains crossing distinct references and optional void statement projection
remain representation gaps. These checks are not full-corpus RSS, latency
benchmarks or all-record behavior/style acceptance. The older remaining-gap
lists above describe their historical rule boundaries; optional call leaves
have since been delivered and this rule covers their member spines.


## Conditional statement roots in shared decisions (rules 108)

The existing ControlDecision ownership determines the original predicate path,
shared arm and final fallback. Normal evaluation roots select a statement
representation rather than introducing an opcode or compiler-layout matcher.
One private block-local cell stores each original oriented predicate. Flat
conditional blocks execute subsequent prefixes only while that cell is false;
the shared/fallback arms remain single occurrences. Deep source nesting is not
proportional to predicate count. Phased statement prefixes still need their
catch-copy/scope representation; the existing phased comma path is retained.

The construction scan and emission each visit an owned decision path at most
once. Fresh flow verifies ordered roots, original empty transfers and branch
polarity, then checks all private-cell accesses in one AST/capture scan. For I
operations, B owned controls, T syntax nodes and A captures, added work and
storage are O(I+B+T+A); there is no per-variable CFG walk or repeated tree
copying. Arrays reserve linear bounds once. Explicit slot bounds count the
predicate save, guard read/complement and final read, plus statements; these
are representation capacity bounds, not VM constants or acceptance thresholds.

Decoded 200/201/402 fixtures exercise geometric work through the existing
counters. They do not constitute process RSS, a timing benchmark, full-corpus
validation, or completion of the pending fields/signatures/body gate.


### Shared exact-import identity projection (2026-10-05, rules 113)

Normal and phased evaluation now use the same read-only copy/phi SCC identity
index described under rules 89. Mutation by SimplifyValues is an optimization;
exact imported normal graphs retain every original instruction and use. Source
aliases keep zero-width transport anchors while the canonical producer owns
its computation, private snapshot assignment and all real observations.
PhiCopies independently derives the same identity projection in either flow,
retaining the complete incoming census and scheduling actual writes only.

The planner, eager snapshot intervals and expression Use lookup resolve the
same flat canonical identity. No per-use copy-chain walk or origin-membership
exception is added. TreeFlow, operands and correspondence still independently
verify the saving occurrence, private symbol, effect order and original sites.

Provenance, licensing and maintenance are unchanged: project-owned iterative
Tarjan identity analysis, functional-graph compression, observation indexing
and parallel-copy scheduling; no new dependency. Each identity pass remains
O(V+U) time/storage. They are bounded constant passes in build and fresh verify;
existing copy scheduling/sorting costs are separate. Allocated index capacity
and work remain in the existing grants and plan reports. Three-profile exact
and simplified 200/201/402 copy variants check evaluation-work scaling; existing
phi tests retain 200/201/4096 identity and actual parallel-copy cases. These
scoped checks do not measure physical RSS or full-corpus latency.


### Closed initializer values (2026-10-05, rules 114)

ClosedValues is a project-owned sparse Boolean data-flow analysis over verified
SSA. Provenance is the existing source evaluation/effect adapter and read-only
iterative Tarjan copy/phi identity index, with a monotone all-operands transfer.
No new dependency or licensing obligation is introduced. Callers first verify
availability/uses and imported instruction semantics; results are queried on
that unchanged graph. Closure means context independence without observable
effects or abrupt completion, not permission to replay or move a source tree.

The identity index gives flat canonical roots. Eligible native expression
results have reverse dependencies keyed by each actual UseId and a pending
operand count. Immediate closed primitives and dependency-free expressions seed
a bounded queue; each result is enqueued once and each input occurrence is
visited once. Repeated occurrences of the same operand discharge separately.
Copy/phi aliases query the same root; ungrounded cycles and distinct-choice
phis receive no seed. Work and storage are O(V+U+O), including identity indexing;
there is no recursive expression walk or per-variable CFG search.

ClosedClassFields retains the separate shared normal EffectOrder cost and
all-path exactly-once field chain. It consumes the common closure facts instead
of a String/Copy producer scan. Actual field trees and expression parentheses
still come from the existing FunctionTree printer. Effects, exceptions, phased
field initialization and general control/temporary syntax remain coverage gaps;
coarse instruction effects are not reclassified merely because operands look
like constants. Existing 200/201/402 scoped work checks are preserved and
extended to composed expressions, without reviving RSS or timing benchmarks.

## Phased predicate execution prefixes (source rule 123)

The source root-use graph reuses the project's Kahn readiness propagation.
Only eager expression capabilities from DescribeEvaluation participate;
suspension, constructor-frame protocols and unsupported source forms retain
their own owners. Every external use is indexed once. A root resolves after
all consumers agree on one terminal owner; competing owners and unresolved
cycles remain snapshots. Multiple uses within one owner are permitted.

Ordered predicate evaluation and its pure dependencies become caller-selected
execution anchors in the existing EffectOrder. Its sparse predecessor and
must-reach equations prove prefix/decision count and order, including pure
branch subdivision, early returns and potentially nonterminating pure loops.
Original full-flow Commit dominance proves availability, while the existing
catch/Reference/operand analyses retain exception and receiver obligations.
Anchors are analysis endpoints, not invented language effects. Pure-only
materializations retain their binding placement, avoiding gratuitous sequences.

For O operations, U uses and P selected prefix roots, the dependency index is
O(O+U) work/storage and grouped sorting O(P log P). Checking adds O(P+U) work,
plus the previously documented EffectOrder/dominance and catch-copy costs.
Capacities/work share the existing budget. There is no per-decision CFG walk,
new dependency, external code or licensing change. Provenance is the existing
self-maintained Kahn/effect-order implementation documented in this file.
Three-profile decoded 1/200/201/402 cases and 769 independent JS observations
check count/order, aliases, exceptions and descriptors; this is targeted
semantic/work-growth evidence, not a physical RSS or full-corpus measurement.


## Private binding initialization syntax (source rules 154/155/156/157)

The already structurally checked FunctionTree supplies ownership and lexical
containers. An iterative statement walk streams expression ownership, recording
the first potential observation and total writes in SymbolId-indexed slots.
An empty private declaration can initialize at a direct assignment only if its
completed store is the first observation. Rules 155 replaces the same-container
representation restriction with scope confinement: iterative DFS numbers native
block entries and subtree ends; every binding records the minimum and maximum
container entry of its observations. Two interval comparisons prove all reads
and writes belong to the initializer's subtree, even when the original empty
declaration belongs to an ancestor. Sibling handler/finalizer and outer reads
retain original storage. Existing loop initializer/condition/transition and
statement-nullish projection owners supply their printed lexical context;
projected references are resolved after the walk, including owners occurring
after the original roots. Outermost ownership propagates to descendants; loop
expression ownership takes precedence over its nested nullish syntax independent
of arena numbering. Canonical-body containment alone is insufficient when a
read is printed in a for update outside that body. One bounded sparse reference
array stores at most one context observation per expression (an update has one
context and one write). Rules 156 indexes the existing condition's owned
continuing-body occurrence: that statement is printed by LoopBody outside the
condition expression. On entering it, the enclosing loop-expression context ends
and native lexical ownership resumes before applying any projections within it.
This uses the actual LoopConditionSyntax.body/LoopBody ownership, not an arm
number, declaration count, graph shape or compiler pattern. One additional
StmtId-indexed optional owner array costs O(S) work/storage; nested native scopes
reuse the same DFS intervals. All earlier/RHS/nested accesses are included. Captured/module
locations retain their declarations, and one static write permits private
const syntax. This does not infer original const metadata, move expressions,
coalesce SSA storage or require a bytecode block layout. Existing loop/nullish
expression programs remain in their original grammar, except the separately
proven loop-entry ForDeclaration projection in rules 157.

Rules 157 records an entry-bearing LoopStmt as a prospective lexical interval
for native `for (let ...)`. Entry/test/update references resolve to that owner;
body references retain native scopes. The existing first completed direct store,
complete observation containment and capture/escape census prove private entry
initialization, including repeated outer-loop entry. Outside/catch/finally reads,
RHS old-value reads and real source/cell/formal bindings keep original storage.
One disjoint scan per entry program selects a representable declarator suffix;
earlier ordered effects execute once before the loop in their existing handler,
so NamedEvaluation and the loop label retain their original destinations.
Arbitrary declarator counts and transparent block distances use the same proof.
Compound completion roots/interleaved non-declarators remain representation gaps,
not compiler-layout admission conditions. SSA storage and captured per-iteration
environments are not coalesced. One StmtId-indexed optional entry offset adds
O(S) bounded storage; the interval/census and disjoint entry scan remain linear.
Language provenance is the pinned ECMA-262 2024 ForLoopEvaluation and
ForBodyEvaluation documented in function-tree-output.md; no new dependency.

Rules 158 adds standard immutable constant propagation on the same eligibility
proof, not a separate layout recognizer. A single-write eligible private binding
has either a typed primitive TreeLiteral definition, a SymbolExpr dependency or
an unknown value. Source literals seed a bounded SymbolId queue; indexed head/next
adjacency visits each copy edge once. There is at most one incoming copy per
eligible target, and each known target is queued once. Cycles without a literal
seed remain unknown. Mutable/captured/source storage, allocations and effects
retain their original producer. No arithmetic folding or expression replay is
introduced. This is a project-owned sparse worklist implementation of ordinary
constant propagation; it introduces no external code, license or dependency.

For B symbols and D eligible copy edges (D <= B), propagation is O(B+D) time and
O(B) bounded storage. The plan stores original literal ExprIds, not copied text.
One O(I) prefix count over disjoint loop-entry roots supports constant-time
nonempty range queries; ordered printer cursors omit pure stores without empty
commas/declaration lists and retain every original boundary. Real read ExprIds
retain their own ranges, and substituted literals use the existing parent grammar
grouping. Each omitted store/RHS has a zero range at its original boundary.
Printing cost includes actual output bytes; repeated long literals can grow output
and remain subject to the existing output budget, not a hidden constant cost.
Unknown alias coalescing, interference and source-name recovery are still open.
Tests cover chains and every typed primitive variant, 200/201/400/800 growth,
arena renumbering, mutable/old-value/exception/suspension cases, number-member
receiver identity and retained object/array allocation identity.

Rules 159 adds immutable copy propagation for unknown runtime values while
retaining their one producer. The same first-store, observation-containment and
capture proof admits private single-write roots and copies. Reusing the indexed
copy adjacency and bounded queue resolves each chain once; mutable/captured/
external or unavailable roots remain storage. Allocation, call, getter and
suspension producers are never moved, cloned or evaluated at a later read.

Name resolution is shared with TreeScopeCheck: ResolveTreeReferences buckets
queries by canonical ScopeId and traverses indexed scope adjacency iteratively.
Its same spelling-group/activation index tests root SymbolId identity in O(1)
per query, without ancestor walks. Existing projection-owner/lifetime containment
establishes visibility of the shortened printed root lifetime transitively along
copies; canonical activation protects the remaining root spelling. A query on a
candidate copy RHS is conditional: only retaining that destination makes its
RHS a real read. Unavailable ordinary reads seed a second bounded queue; each
required copy propagates retention to its source only when that RHS cannot
resolve the root. Each binding is retained once, each dependency visited once.
Branches still permitted to share storage are not disabled by an unrelated
required copy. Original read IDs/ranges and every omitted store/RHS boundary
remain separate, with no canonical AST mutation or renamed source variable.

For N lexical scopes, Q queried reads, B symbols, M module aliases and total
compared spelling units T, shared name indexing adds O(B log(B+1) +
M log(M+1) + N + Q + T) work and O(B+M+N+Q) bounded storage. The ownership,
dependency and retention walks remain O(S+E+K+B+C). T includes validation and
actual UTF-16 comparison units; this cost is not described as constant-time
string comparison or purely linear analysis. Printing includes output bytes.
Standard copy propagation and the existing scoped activation index are
project-owned implementations with no new dependency or external code/license.
Tests cover 200/201/400/800 chains/depth, reverse symbol/scope/expression/statement
IDs, sibling/inner shadowing, conditional RHS disappearance and retention
cascades, zero/multiple iterations, finally/old values, suspension, function
receiver and fresh object/array identity. The Node oracle uses the same bounded
4MB stack as adjacent deep-loop tests, rather than treating a reference compiler
stack failure as successful engine/source validation. Physical RSS stays paused.
External/source immutability remains a gap;
retained canonical same-name declarations can conservatively prevent elision.

Rules 160 seeds this same copy graph with implicitly initialized bindings:
parameters on body entry, a catch binding on handler activation, and the named
function's self binding at function creation. No owned write or unknown capture
escape may alter the root; default-initializer writes count. Mapped arguments
add an implicit formal write channel only for a non-strict, non-arrow function
with a simple parameter list. These language facts follow ECMA-262 2024
FunctionDeclarationInstantiation and are represented by the existing typed
Strictness, CallableKind and Parameter fields. The pinned primary definition is
[ECMA-262 2024, 10.2.11 steps 16-22](https://tc39.es/ecma262/2024/multipage/ordinary-and-exotic-objects-behaviours.html#sec-functiondeclarationinstantiation);
the current Parameter domain contains identifiers, optional defaults and rest,
not destructuring patterns. Sloppy mapped formals preserve
their copied old values. Local/Capture/Module zero-write counts alone establish
neither initialization nor external immutability, so they are not new roots.

This bootstrap scans parameters and symbols once, O(P+B) additional work,
without new arrays, per-root CFG walks or a second admission/layout condition.
The existing scoped visibility, first-store and observation confinement prove
target availability, identity, activation lifetime and execution count. Tests
include catch/finally escape, conditional first stores, source rewrite, shadowing,
async/generator suspension, self identity, mapped/unmapped formals and value
receiver, using reversed arenas and 0/1/200/201/400/800 chains. The arguments
coupling probe inserts the same write into generated and independent reference
JS only in the test layer; it is not bytecode-domain recovery evidence. Captured
mutation uses the currently canonical Local/Capture cell domain in a valid
printed family; raw Parameter capture admission remains a representation gap.

Rules 161 records native declaration completion in the same binding-use census:
evaluate the original initializer before recording completion, or initialize an
uninitialized let to undefined at its original statement. A declaration preceding
all observations, with no owned write or unknown capture/module escape, becomes
an immutable retained copy root after the same lexical-interval confinement
proof. Empty declaration plus a later direct source store uses FirstStored, the
identical first-completed-store proof as private projections. A single retained
store can be stable within its observation activation without changing the
original source declaration or store; constant-elided stores cannot provide
retained storage. Confined and FirstStored are shared with private first-store
projection. Target availability
and execution count still come from that first-store/observation proof, while the
existing scope index preserves actual root identity. Source declaration kind,
scope, NamedEvaluation and the initializer's allocation/effects/exception and
await/yield completion remain unchanged. Source lets are never inferred const,
and initializers are neither copied nor evaluated at each substituted read.

The initialization model is ECMA-262 2024 lexical binding evaluation, implemented
in project-owned code without an additional dependency or copied external code.
An optional declaration ID and one completion flag extend each existing binding
record; sizeof(BindingUses) is already charged by its bounded array admission.
The added census/root work is O(S+B), with O(B) metadata and the existing copy and
scope-index costs. No per-binding CFG or ancestor traversal is introduced. Tests
exercise 0/1/200/201/400/800 chains, reversed IDs, stable let/const/undefined,
declaration-before-TDZ observations, later writes, caught/captured old values,
activation exits, per-iteration effectful initialization, suspension in declaration
RHS, original failures and fresh allocation identity. A declaration with any
earlier observation retains all storage under this conservative first-completion
proof; narrowing to independently dominated observations is a general liveness
gap, not a language invariant or a reason to reject the complete function.
Rules 161 projected direct ExpressionStmt assignments; rules 162 extend the
same census to completed identifier stores in all owned expressions. Seventy-two
actual lexical-cell decoder variants across the three profiles verify the
complete pipeline; both call arguments and member-write cases share the retained
Local root and independently verify key
conversion, Proxy setter receiver, state, exception and effect order. Those
members exercise the existing evaluator's early snapshots; no member-specific
production admission was added.

Rules 162 visits the shared immediate-child ownership in source evaluation order
(reverse pushed children for the existing iterative LIFO stack). Identifier
PutValue is observed only after its RHS completes. Statement control children,
logical RHS and conditional arms have nested execution intervals;
first completed store plus observation confinement proves availability within
that activation, including repeated loop entries. Earlier reads/self reads,
conditional stores observed outside their arm, writes, unknown escapes and
handler exits preserve snapshots. These are semantic execution regions, not CFG
layout/adjacency/opcode matches. The conservative proof does not merge multiple
definitions or establish availability beyond the containing execution region;
more precise reaching definitions remain an implementation gap.
An ordinary lexical block does not create an execution boundary: it runs in
sequence, while common scope resolution still protects its bindings. Each actual
read also records its original lexical/projection owner. RootLifetime checks the
same native-declaration interval at substituted reads, including a prospective
for-let owner, because its printed lifetime may be narrower than the canonical
SymbolId's scope. Removing a copy must not make an outside-loop read reference a
new loop-scoped root. Owner resolution is a single post-census indexed pass;
there are no ancestor scans or declaration-layout admission matches.

The same constant/copy worklists consume completed-store ExprIds. A redundant
direct statement store disappears; an embedded store projects to its original
pure TreeLiteral/SymbolExpr RHS at the original expression occurrence, preserving
the assignment result and parent grammar. The embedded RHS remains an actual
read and therefore participates in common scoped root resolution. It cannot use
the conditional-retention exemption for deleted statement-copy RHSs. Source
stores, effectful producers and NamedEvaluation remain in their original place.
Original expression ranges cover replacement results and their original RHS;
deleted declarations/direct stores retain existing zero ranges and anchors.
Rules 163 also project redundant inert left operands of comma through the
completion summary below. General dead statement elimination and more precise
per-occurrence initialization remain representation gaps, never whole-function
rejections.

This project-owned extension follows ECMA-262 2024 expression evaluation,
assignment, short-circuit and conditional completion rules, with no new runtime
dependency or copied implementation. Added region metadata and replacement slots
are bounded O(S+E+B); the shared census costs O(S+E+K+B+C), and existing copy/scope
index costs remain as documented. Each child slot is visited/reversed a constant
number of times; no per-symbol graph or ancestor walk is added. Tests exercise
0/1/200/201/400/800 chains, reversed symbol/scope/expression/statement arenas,
argument order, conditional execution, old values, async/generator completion,
fresh identity, constant results, callee receiver and embedded-RHS shadowing.

Rules 163 add a postorder ExpressionEvaluation summary to this same census.
IntrinsicEvaluation exhaustively interprets the typed language AST and its
unary/binary enums: no bytecode name, producer list, sample or graph shape is
consulted. Literals, ToBoolean/typeof on values, strict equality and value
selection have no intrinsic hook; child completion still contributes its throw,
effect and suspension. Unknown coercions, binding/global/member lookup, writes,
allocation, calls and suspension retain evaluation. Invocation body bindings
and own ordinary this are initialized; derived/lexical this remain unproved.
Original declarations completing before all observations need both the existing
lexical confinement and declaration execution interval. Reads need initialization,
not immutability: reading a later-mutated initialized binding invokes no user
hook. A parameter initializer does not receive the body-entry proof. General
default expressions remain explicitly unsupported by existing structural checks.
Store/literal/alias projections consume their existing completed-copy proofs.

The printer removes an inert comma left operand and retains the original right
occurrence and parent grammar. Its value-result category survives replacement
chains: typeof unresolved globals still obtains GetValue, value calls remain
indirect/receiverless, and function/class creation in name-sensitive destinations
keeps a comma envelope rather than introducing NamedEvaluation or self scope.
Discarded/truthiness observation propagates through an unprojected sequence's
right. This is expression algebra after general availability/completion proofs,
not a recognized compiler sequence. No new transport admission or zero-width
exception is added: omitted inert subtrees map to the nonzero normalized result
span, retaining original IDs/origins/ownership and all existing position checks.
They are disjoint owned subtrees, visited once by iterative printer events.

The project-owned implementation follows ECMA-262 2024 expression evaluation,
GetValue, comma, unary/binary and NamedEvaluation rules. No new dependency or
copied third-party algorithm is introduced; existing pinned profiles and source
contracts remain intact. Postorder completion IDs and summaries add budgeted
O(E) space and O(E+K) work; each child is visited a constant number of times,
independent of arena numbering. Allocation/resource and unknown initialization
are conservative representation gaps, not language invariants. Tests use
0/1/200/201/400/800 chains, reversed arenas, getters/coercions, unresolved/global
accessors, TDZ, mutable invocation reads, await jobs/rejection, yield requests,
value calls, named creation, valid-JS adverse mutations and original ranges.
General dead statements/right-operand pruning and more precise dominated-read
proofs are still open. Work scaling is not a physical RSS/latency measurement.

For S statements, E expressions, K owned child/argument/property slots, B symbols
and C owned class-member capture occurrences, ownership/initialization cost is
O(S+E+K+B+C), with
O(S+E+B) scratch/output indexes before the separately accounted rules 159 scope
index above. Dense class capture occurrences are counted
explicitly; there is no per-binding CFG or per-loop ownership traversal.
The bounded iterative stacks avoid recursive depth limits. Project-owned code
adds no runtime dependency; existing AST/NamedEvaluation/loop grammar remains
the semantic basis. Tests cover 200/201 and 400/800 growth, reversed arenas,
three ISA profiles, COPY/split variants, effects/exceptions/await/yield and
source ranges. Work counters do not certify physical RSS; that gate stays paused.
Additional tests exercise nested lexical containers, protected bodies and old
values observed in finally, zero/multiple loop entries, 200/201/400/800 nested
blocks, arena renumbering and exact/under work budgets. Only uncaptured private
lifetimes shrink; the adverse mutable-private/for-update fixture and valid-JS
scope-shortening mutations keep projected reads in their actual visible storage.
This is not original user binding or per-iteration capture
recovery. Standard DFS subtree intervals introduce no dependency or license.
Retained-body tests cover native locals, outside-loop old-value reads, 0/1/2/3
entries, 200/201/400/800 nested blocks and reversed arenas. Three-profile actual
decoded loops vary local-register allocation, 0/1/200/201 copies and block splits;
their native representation and arbitrary-value behavior are checked independently.
Decoded control may use a retained arm or a following native body; tests do not
require one implementation layout. Real paired-record evidence remains separate
from these conditional fixtures and from full-corpus/device acceptance.

## Nested phased selection execution owners (source rule 124)

Source rule 153 removes a root-placement admission condition on pure SSA
transport copies. A copy already owned by the common evaluator still carries
the same selected input/result to its canonical consumer; the original
identity, unique use, prefix/scope, effect and catch checks remain. Root versus
inline is not an additional semantic proof. Each candidate uses the existing
indexed canonical-use facts in O(1); no traversal, allocation or retry loop is
added, and the grouping/ownership bounds below are unchanged. Regression
evidence includes three-profile register variations and 0/1/200/201 source
copy variants; this is not a physical RSS or performance acceptance claim.

The existing inline roots identify the actual source dependency between
selection operand groups. Sort S candidates by actual consumer/operand; index
each branch's group once, then connect the consumer root to its enclosing group.
A bounded parent-before-child queue checks every group once. Successful enclosing
checks propagate one final execution owner; cycles and failed parents propagate
no ownership. Source input and phi identities are not rewritten into that owner.
The independently reconstructed checker uses original catch/staging signatures
and the existing arm/Reference/interval obligations. The common forest and
literal ownership consume the checked source placements.

Grouping costs O(S log S), dependency indexing/queue O(O+S) work/storage for O
operations, plus the existing candidate discovery, interval and catch checks.
There is no repeated whole-selection scan, per-variable CFG traversal, recursive
ownership walk, external dependency or new licence obligation. This uses the
project's existing adjacency/worklist design (Kahn/topological dependency
processing); it changes execution scope, not the original CFG or exception arcs.
200/201/geometric depth and transport tests check scoped work growth; physical
RSS and full-corpus performance remain unverified and paused.

## Phased nested control-arm paths (source rule 125)

Original normal edges are processed in iterative DFS postorder. Each block has
one path summary, each branch combines at most its two actual CFG successors,
and original phi occurrences/child regions acquire unique syntax parents.
Disjoint bottom-up leaf counts identify complete phi coverage; unresolved
cycles or competing observations do not form closed expression families.
The shared functional inline/module forest resolves every parent once and
rejects cycles independently of operation array order. Predicate ownership and
execution remain the existing root-use and anchored EffectOrder analyses.

For B blocks, E edges, O operations, V values, U uses and S regions, path
indexing adds O(B+E+O+V+U+S) work/storage, plus the existing predicate-prefix
sorting/effect/dominance analysis. Operand grouping remains O(S log S); bounded
child queues check each region once before publishing its group's execution
owner. AST construction uses existing iterative selection worklists. No recursive
source walk, per-phi graph scan, external dependency or copied upstream code is
introduced. Phi bundles and later control/evaluation/exception checks retain
their existing cost and representation obligations. Scoped scaling checks do
not establish physical RSS or full-corpus performance, which remain paused.

## Original-site phased selections and alias-aware phi inputs (source rule 126)

Original-site bindings reuse the existing root-use, anchored effect-order and
phi ownership indexes. Each closed family and control child enters one bounded
queue. Actual control binding is followed by bottom-up child closure and a
parent-before-child keep pass: O(O+S) additional indexing/work/storage for O
operations and S selection regions, plus existing scope/interval queries.
No candidate owns a prefix before complete source-control binding. Existing
AST scope indexing independently compares original try membership.

The phi input census resolves already verified flat aliases in O(1) per use;
transport operations are not additional evaluations. The all-path tail index
and actual phi UseId stay unchanged. This adds O(U+P) work, no graph walk per
alias, phi, or variable, and no external dependency/licensing change. It follows
the project's existing SSA identity projection and Kahn/must-reach analyses.
Scoped 200/201/geometric checks cover original binding families as well as
fusion. Their existing discovery/prefix/flow costs remain part of total work;
physical RSS and full-corpus benchmarks remain paused and unverified.

### Native class declarations (2026-10-06, rules 142)

TreePrinter reuses the existing ClassNames lexical-scope proof for a direct
class initializer in writable declaration storage. The printed destination
must preserve the actual runtime name; the added immutable self scope must
shadow no real module/capture/global/heritage reference. Const destinations,
unknown bodies and unsafe names retain their original expression form. The
original AST, initializer ID, evaluation time, captures and descendant printer
remain the owners of semantics and source events. This uses ECMAScript 2024
[ClassDefinitionEvaluation](https://tc39.es/ecma262/2024/multipage/ecmascript-language-functions-and-classes.html#sec-runtime-semantics-classdefinitionevaluation)
and BindingClassDeclarationEvaluation, implemented locally with no new library
or copied third-party implementation; existing project licensing is unchanged.

For D declarations, C class candidates and total compared spelling length L,
the additional syntax decision costs O(D log(C+1) + L). ClassNames is planned
once, its existing ordered lookup is reused, and no declaration walks a CFG or
member body again. The new form is a fixed enum alternative in the existing
printer action stack; it adds no class/variable-sized inventory. Existing
allocation, output and work accounting remains in the shared printer. Tests
cover decoded copies/splits and the existing 200/201/400/800 lexical-plan
scaling; these are correctness/work-growth evidence, not measured process RSS.

### Lexical publication of globals (2026-10-06, rules 143)

TreeGlobals reuses the existing iterative TreeStructure traversal and
TreeScopeCheck active spelling index at the actual expression scope. It no
longer sorts a union of all local spellings and treats every matching local as
an ancestor. The same indexed lookup serves class-family outer references,
ordinary globals and selected host capability bindings. Scope entry activates
all bindings, so TDZ is preserved; leaving a sibling scope restores the prior
active binding. Complete outer destination indexes remain independently checked.
Invalid syntax ownership/scopes return typed structure failure, rather than
being admitted by a name-only check. This is the project's existing lexical
scope-stack and sorted-name algorithm, implemented locally with no new external
dependency or copied implementation; existing provenance/licensing is unchanged.

For N tree arena entries, B local bindings, R free reads, E outer destination
bindings and compared spelling length L, this adds one constant-factor common
publication traversal: O(N + B log(B+1) + R log(B+1) + R log(E+1) + L) work with
O(N+B) scratch. Character comparisons and actual vector capacities retain the
existing work/byte accounting. There is no graph/ancestor walk per variable;
publication checks are fresh, rather than cached transferable certificates.
The full structure report includes existing tree storage accounting, so this
is a conservative admission cost, not measured process RSS. Scope-bearing
0/1/200/201/402/804 tests verify geometric work growth. Full-corpus RSS and
benchmarks remain paused; fixed paired exports prove no output regression,
not a latency or peak-memory improvement.

## Nested literal presentation (rule 144)

The common TreePrinter scans each emitted aggregate's owned properties once to
choose compact or nested layout, then uses its existing iterative action stack.
Typed AssignmentExpression grammar permits ungrouped object/array elements;
other destinations retain their previous grouping. No recovery admission, IR
ownership, allocation, effect or exception proof changes. ECMA-262 Object/Array
Initializer and Expression Statement grammar supplies the prerequisites; this
local extension adds no dependency or license change.

For P emitted properties and L output bytes the extra work is O(P+L), with
constant-sized additions to existing action state. There is no repeated width
measurement, copied subtree or recursive host stack. Deep indentation can make
L grow faster than tree depth; every byte remains charged to existing output
and work limits. Existing 200/201/402 literal-buffer/array fixtures, original
span checks and independent decoded/paired JS compare semantics and resource
failure behavior. Full-corpus RSS/benchmarks remain paused; bounded exports and
test counters do not establish process memory or latency improvement. Details:
[literal initialization](source-recovery/literal-initialization.md).

## Discarded nullish statement observations (rule 146)

The existing null/undefined/other abstract meanings and ordered Formation feed
ExpressionStmt, Block and If summaries. A statement result retains an original
effect ExprId or absence; incompatible references, additional effects and abrupt
completions retain original syntax. An opaque effect invalidates later
domain-dependent reference facts. Native syntax never moves a formation ahead
of a preceding read or erases another write. Live result-join assignments need
a general sequence/value-flow representation and are not removed as void.

One Kahn owner/pending queue evaluates statement summaries independently of
arena order. One top-down ownership pass selects outer projections and marks
omitted statements; one expression queue marks their original owned subtrees,
stopping at retained base/call/member roots. The existing ProjectOptional,
MemberPath and iterative printer serve both typed ExprId and StmtId roots.
No per-candidate subtree traversal, graph-shape matcher, second writer or AST
copy is introduced. The existing source-event boundary includes original block
anchors and statement/property ranges; all new arrays/capacities are charged.

For N expressions, S statements, H statement-child slots, A argument slots,
P properties and M text entries, planner time is O(N+S+H+A+P+M log(2+M)+C),
where C is the existing UTF-16 comparison cost; storage is O(N+S+P+M). Each
summary has the same constant three domains. Statements and opaque expression
subtrees are visited a bounded number of times. Printing follows actual output
and original owned nodes; original stack/buffer admission remains. This uses
the project's existing finite interpretation/Kahn implementation and pinned
ECMA-262 2024 statement-completion/Optional Chains semantics; no new external
code, dependency or license change. Declarations/abrupt/loop/handler/debugger/gap
absence of summaries is a documented representation limit, not a language rule.

80 common-tree fixtures cover 1/200/201/402 guards, reversed statement arenas,
forming assignments, member spines, computed keys and value calls. 201-to-402
work growth, exact memory/work exhaustion, six adverse cases and original
source-range serialization are checked. Independent JS covers 32,000 cases
and two valid mutations. Three ISA profiles supply 336 actually decoded
functions and 75,264 behavior cases, including preserved live join storage.
These are bounded correctness/work checks; paused full-process RSS and latency
benchmarks remain unverified. Fresh fixed-five/six-mode evidence is in status.md.

### SourceVariables shared preparation (rules178)

The preparation coordinator runs existing nullish, loop and binding recovery once
for each current printing owner. Printed class-name analysis borrows those exact
immutable results. For M supplied owners and T inventoried trees, the lookup adds
O(M log(M+1) + T log(M+1)) comparisons and O(M+T) descriptors, charged to the
existing budget. Tree-object identity selects associated facts, never recovery
behavior; semantic traversal does not follow pointer sort order. Unsupplied nested
class bodies use the same coordinator once within the name-analysis invocation.
Cross-printer-instance reuse remains open. No dependency or external code added.
Tests cover projected class-name semantics and reduced shared-consumer work at
0/1/200/201/400/800 copies with reversed statement arenas and exact/short budgets.

### Projection-local effect facts (rules179)

Stored-result elision observes a fixed plan, then commits its replacement delta.
Effect analysis is const and returns an owned vector. Storage interference and
inert-statement deletion derive facts at consumption; final public summaries are
published only after all plan edits. No consumer depends on a manually refreshed
output cache. This removes the stale-effect interval after snapshot forwarding.
For E expressions, each analysis costs O(E + child edges) time and O(E) storage;
the current finite sequence adds a constant number of analyses, not per-variable
walks. Every fresh allocation and traversal is budgeted, including overlap with
proposal vectors. A future cache must carry full projection invalidation; this
change deliberately has no cross-mutation cache. Initialization still uses the
legacy census and simplification scheduling is not yet a fixed-point solver.

### Independent definition elision (rules180)

Project-owned dead-store elimination follows the standard backward-liveness rule:
a successful scalar write can be removed only if its value is dead and the write
itself is unobservable. It consumes the existing sparse FlowLiveness facts; it
does not solve another dataflow problem or infer liveness from syntax. For D
flow definitions the proposal scan is O(D) work and O(D) maximum output capacity.
The source adapter adds O(E + S) initialization/observability descriptors for E
expressions and S symbols. Graph construction and shared liveness retain their
existing N/E/L bounds and budgets. The following storage pass rebuilds after the
delta; no per-variable graph walk, quadratic threshold, dependency or external
implementation is introduced. RHS computations, normal-result identity and
exception order remain at their original occurrence. Native initialization and
observable writes are not ordinary dead stores.

Verification extends the 175,616 tiny-graph independent oracle to deleted-write
sets; source execution tests compose overwrites, branch joins, catch observations,
returned assignment values, native declarations and live undefined initialization.
Reversed expression arenas, 200/201/400/800 effect sequences and exact/short budgets
exercise layout independence and bounded work. This is not a process RSS claim.

### Demand closure for discarded evaluations (rules181)

Provenance: the standard mark-from-observable-roots dead-code elimination family,
using the dead-until-demanded approach described by
[LLVM ADCE](https://llvm.org/docs/Passes.html#adce-aggressive-dead-code-elimination).
This is independent project-owned code, not copied LLVM code or an LLVM dependency;
no external licensing or versioned runtime is introduced. All control transfers and
condition reads remain. Maintenance belongs to the existing source adapter, shared
liveness solver and definition-elision consumer.

RecoveryFlow records typed discarded-value regions and composes their operation
effects once. Private successful writes can be inert; required binding initialization, observable
writes, getters/coercion, calls, throws and suspension force retention. Existing
initialization/escape facts remain an explicit migration prerequisite and gap.

AnalyzeFlowLiveness and AnalyzeFlowDemand share one sparse backward gen/kill and
exception-edge solver. The former seeds every read for storage interference; the
latter seeds observable effects and reads outside discarded regions. On reaching
a definition it retains that definition's producing region, activating its reads.
Each region activates once. The monotone worklist closes chains and cycles before
one write-and-statement delta is committed. Separate result types prevent passing
demand-only facts to a consumer that needs all-read liveness.

For N events, E edges, D definitions, G discarded regions and F reached
(point,binding) facts, demand adds O(N+G+D) indexes/output to the existing sparse
backward worklist. Each unique fact is processed once; ordered-set operations cost
O(log(F+1)), and incoming edges are visited per reached fact. F is bounded by the
all-read solver's fact set on this same graph. No read/definition cross product,
per-variable graph walk or repeated whole-flow deletion rounds are introduced.
Dense liveness remains possible and is budgeted, not hidden by a size threshold.

Measured design choice: explicitly materializing all reaching-definition read
edges for this consumer exceeded the existing resource budget on the 402-selection
nullish regression. On-demand discovery through the shared solver preserves the
same demand semantics without that product. FlowDefinitions remains available and
independently verified; it is not a production prerequisite of this transformation.

Tests add 98,304 tiny demand graphs checked against an independent dense fixed
point, including effects, external reads, cycles and multiple definitions per
region. Source chains/cycles at 2/200/201/400/800 retain demanded copies and remove
undemanded ones, with reversed arenas, an independent array-transport execution
oracle, exact/short budgets and geometric work assertions. This is not process RSS
or full-corpus evidence. Whole-pipeline convergence and binding lifetime recovery
remain open.

### Unused private binding retention (rules182, superseded by rules222)

Project-owned occurrence indexing applies the standard unused-binding rule:
an unobservable empty lexical binding with no remaining references may be removed.
No external implementation, dependency or license is added. RecoveryFlow owns
binding exposure, reads and definitions; the new planner consumes the proposed
write/evaluation deletions against that same immutable graph. Removed statements
and writes are indexed once; retained events mark required bindings, then unused
private undefined declarations are proposed separately. Capture/escape facts are
still supplied by the explicit legacy adapter; initialized declarations and
general lifetime movement remain outside this pass's proof.

For N flow events, D definitions, B bindings, X expression slots, S statement
slots and K proposed elisions, work is O(N+D+B+X+S+K), storage O(B+X+S+D),
including zero-filled indexes and output capacity. There is no per-binding graph
walk, repeated demand closure or extra graph construction. The two immutable
proposal stages commit together; the subsequent storage consumer rebuilds its
facts. Allocation and work accounting include all simultaneously live proposals.

Rules222 replace this pending-delta path with the separate current-projection
binding pass described below.

### Committed storage projection and dependent cleanup (rules183)

The existing disjoint storage plan is applied in O(1) per semantic occurrence,
after immutable alias resolution, just as in the source consumer. No new graph
solver, dependency or external code is introduced. Each producing pass reports
whether it changed the projection; declaration projection contributes its actual
changes too. The final cleanup only runs when these producers changed its input.
It rebuilds the flow and reuses the shared demand solver and separate elision
planners. Its costs are the existing N/E/reached-fact bounds, paid once at this
stage, rather than a graph traversal per binding or per mutation. This finite
dependency schedule does not establish a global fixed point for repeated storage
coalescing; that remains an explicit milestone gap. Mutable state is still local
to a function and no analysis cache survives a projection mutation.

### Preserving initialization values during storage sharing (rules184)

PlanStorageInitializations independently consumes the current RecoveryFlow,
all-read liveness and proposed storage map. For B bindings, D definitions and S
statements it indexes active declaration points and proposes preserved undefined
writes in O(B+D+S) work/storage. A dead initial value is omitted; a live value stays
at its occurrence. Shared interference already accounts for these writes, rather
than relying on a first-read/store pattern. Source name/activation constraints
remain the documented source planner obligation. The printer only renders actions.

When preserved writes need initialization-order proof, AnalyzeFlowDominance reuses
the project's existing simple Lengauer–Tarjan implementation (provenance above;
src/analysis/dominators.cc). The adapter includes every RecoveryFlow completion
edge. For N events/E edges it builds N+E vertices: each original event points into
a binary fanout chain over its outgoing edges. Paths between original events are
unchanged, so original-event dominance is preserved. It costs O(N+E) adapter work
and memory, followed by O((N+E) log(N+E)) dominance work and O(N+E) memory. No
per-binding traversal, quadratic threshold or external dependency is introduced.
The byte quota includes graph storage and the upstream solver's simultaneous
scratch; work includes its reported steps. Empty/no-proof-needed proposals avoid
the solver by semantic demand, not input size or graph shape.

Independent remove-vertex reachability checks all dominance pairs on 512 small
graphs including exception/resume edges and fanouts above two. Tests exercise
preserved initialization, bypassing its destination declaration, loop reactivation,
200/201/geometric layouts, arena permutations, and exact/short quotas. Execution
oracles and missing-reset mutants distinguish correct sharing from lost undefined.
These are bounded algorithm/correctness checks, not full-corpus RSS measurements.

### Shared binding-reference initialization facts (rules185)

The source adapter emits one typed reference per binding read/write occurrence,
including both update halves, in O(R) additional work/storage. Source locations
are metadata for name resolution; execution order comes from FlowPoint. No
consumer reconstructs those facts from expression arena ordering. Coalesce's
original syntax scan and control/region/completion-order initialization decisions
are removed from this path.

AnalyzeFlowInitializations indexes active initialization definitions in O(B+D)
work/storage and computes completion dominance once for actual storage proposals.
It shares that immutable result between ordinary reference checks and preserved
undefined planning. Each order query is O(1); lexical resolution retains its
existing batched indexing. Overall dominance costs remain the rules184 N/E bounds,
not a graph traversal per reference or binding. A function without a proposed
sharing action skips this analysis by lack of consumers, not a size threshold.
No new dependency/algorithm implementation is introduced; provenance remains the
existing LT implementation and remove-vertex reachability oracle described above.

Unreachable references impose no runtime order condition, while source name and
activation checks remain. This is not general TDZ/lifetime reconstruction:
effects still use the documented legacy initialization adapter. Layout/scaling,
shared-analysis budget and independent source behavior tests cover the migrated
reference-order decisions without claiming process RSS or whole-phase completion.


### Shared projected expression facts (rules186)

AnalyzeExpressions computes result-binding, normal-store and composed evaluation
facts in one semantic postorder, O(X+C) work and O(X) storage for X expressions
and C owned child incidences. Replacement occurrences forward all applicable
facts; no per-expression subtree scan is added. CurrentFlow and final publication
no longer independently build stored-result and evaluation snapshots. Consumers
request a fresh local result after preceding mutations; no cached dependency or
invalidation claim is introduced.

Inert-statement proposals require O(S) work/storage, followed by O(K) application
for K actual omissions. This extra bounded proposal array buys an immutable read
phase; it is accounted by the existing semantic budget. A consumer needing only
one fact still computes the combined result: this is a consistency tradeoff, not
a claimed overall latency or memory improvement. Existing exact/short quota and
200/201/geometric tests cover the changed accounting. No new algorithm dependency,
external implementation or license is introduced; full-process RSS remains paused.


### Read-origin coverage for constant propagation (rules187)

AnalyzeFlowReadCoverage consumes shared all-read liveness, avoiding the eager
read-to-definition relation. It scans D definitions to index each binding's sole
observed origin/conflict, L live facts for missing entry definitions, then D
candidate definitions: O(B+D+L) work and O(B+D) memory. It does not walk the graph
per binding or materialize a dense read/definition product. The preceding sparse
liveness cost is unchanged and charged explicitly; no quadratic threshold is
introduced. No reachable read may observe another definition or missing entry
value. Unreachable competing definitions are conservatively retained.

Constants builds the current graph only when a primitive literal seed exists,
then proposes literal/copy substitutions on the existing indexed dependency
worklist before one commit. The seed scan is O(B); there is no result without a
seed in this lattice. Coverage alone does not equate dynamic executions: only
identical primitive literals propagate here. No new dependency or external code
is used. Independent tiny forward-dataflow tests check coverage, and source tests
retain 200/201/geometric and arena-remapping checks. Whole-process RSS is not
claimed by these bounded checks.


### Shared coverage adapter for aliases (rules188)

Constants and Aliases use one AnalyzeBindingReadCoverage implementation. Besides
rules187's graph/liveness/coverage costs, mapping definition IDs to the planner's
store/declaration IDs takes O(B+D) work and O(B) storage. The alias consumer runs
on the projection after literal commit; this intentionally rebuilds rather than
caching potentially stale facts. Its O(B) candidate scan skips analysis only when
no private single-write symbol-copy consumer exists. Existing indexed copy
propagation and batched lexical lookup costs are unchanged; there is no per-copy
graph walk or full read/definition relation.

This applies the ordinary single-definition copy-propagation obligation to the
common flow, with separate binding lifetime/name checks and escaping/mapped-args
exclusions. No producer is replayed. No dependency or external implementation is
added. Geometric copy chains, arena/symbol/scope remapping, independent behavior
and existing exact/short budgets remain gates; extra analysis costs are charged,
and no latency/RSS improvement is claimed.


### Snapshot read coverage and batch application (rules189)

The binding coverage adapter borrows a const span of RecoveryFlow references
within one pass; it does not copy them or rebuild references from syntax.
Snapshot reads are counted in O(R+B) work/storage from those current events. The existing bounded motion
census and environment indexing remain O(X+S+B). Proposals occupy O(B) storage
and apply in O(K) work for K actual forwards. All allocations/work use the current
semantic budget, including the fresh graph/liveness/coverage analysis.

Demand cleanup precedes snapshot analysis. Forwarding preserves RHS read counts
and removes its own single read/store/declaration, so demand cleanup need not
repeat until a later storage/declaration change. RunPass owns invalidation of
the cached graph and liveness after any changing pass; no-op passes reuse const
facts. Allocations/work are charged when built, never refunded or charged again
for borrowing. At most one graph and one all-read liveness result are retained,
with storage O(N+E+B+D+R+L), where L is the number of sparse live facts. Other
pass scratch remains charged as before; unchanged graph reuse removes duplicate
builds, not semantic events. There is no per-variable graph walk. Old
unused declaration-control/completion-order/store-region metadata is removed.
No dependency/external algorithm is added and no net speed or RSS improvement is
claimed. Independent source execution, repeated-work mutants, exact/short quotas
and 200/201/geometric cases remain validation obligations.


### Alias reference queries from semantic events (rules190)

Alias name/lifetime queries now iterate R current FlowReference events rather
than X original expressions. O(R) bounded query/occurrence storage retains typed
binding and expression identities; the O(X) copy-RHS index and O(B) blocked-source
queue are unchanged. Existing batched lexical resolution and lifetime lookup are
reused. No graph traversal per alias, read/definition Cartesian product, new
dependency or recovery pattern is introduced. The reference span is borrowed
only until the local pass returns; RunPass invalidates its owner on commit.
Costs remain charged to the existing budget. Composition and name-shadow
counterexamples, layout permutations and 200/201/400/800 remain validation gates;
this is a shared-fact migration, not a claim of whole-corpus speed or completion.


### Initialization bootstrap and completion adjacency (rules191)

The existing simple Lengauer-Tarjan implementation now accepts both NormalFlow
and general CSR adjacency through small typed views. The LT algorithm, iterative
path compression, predecessor indexing and budgets are shared. Source completion
graphs no longer copy NormalBlock records or introduce one synthetic node per
edge: CSR costs O(N+E) storage and LT costs O((N+E) log N) work/O(N+E) storage.
All completion edges are supplied; no fanout or exception pattern is filtered.
Independent removal/reachability oracles exercise both views, including arbitrary
fanout for the source graph, malformed CSR boundaries, and geometric sizes.
No dependency, external code or licensing change is introduced.

Initialization bootstrap composes conservative effects in O(X), builds one
completion graph, derives shared initialization proofs, then composes refined
effects in O(X). An O(X+N+E+D) refinement removes only impossible throw edges
and updates effects in place. Its event identities are stable, so positive
dominance facts survive this edge-removal refinement; demand/liveness are later
products. Unknown occurrences and negative initialization results remain unknown.
Typed pass contracts distinguish preserved occurrence proofs from invalidated
event-indexed analyses. This does not claim a whole-pipeline fixed point.

Published graph vectors transfer ownership rather than copying PMR buffers.
Their geometric capacity growth charges the new allocation and any excess,
including old/new overlap. A later pass may transfer those already-charged
buffers back for rebuilding under the same preparation budget. Only capacity
survives invalidation; arrays are cleared/repopulated and metadata is overwritten.
Scratch still uses RecoveryScratch. No budget is increased or refunded. Fresh
and reused graph equivalence plus existing exact/short quotas check the contract;
no net latency or process-RSS improvement is claimed.


### Universal next observation for source motion (rules195)

AnalyzeFlowMotion is an original repository implementation of backward universal
terminal propagation using a Kahn-style dependency countdown. No external code,
package or runtime dependency is introduced. Its input is the current RecoveryFlow
with all completion edges, semantic reads, definitions and intrinsic effects.
Observations and terminal sinks seed their own identity. A transparent predecessor
resolves only after every outgoing edge has delivered a resolved successor; equal
identities join, disagreement propagates a resolved unknown. Transparent cycles
never resolve, preserving the possibility of an infinite execution before a read.
An output query joins successor summaries, excluding the queried point itself.

For N events and E edges, three bounded edge traversals and a queue admitting each
event at most once cost O(N+E) work and O(N) storage. Every allocation and traversal
is charged through the existing budget. Snapshot candidates query the result in
O(1), with one linear map from definitions/references to candidate occurrences.
The result proves must-execute order, not handler or lexical-activation equivalence.
It deliberately loses precision on loops whose termination would need a value
proof; it does not impose an arbitrary loop size or graph-shape admission limit.

An independent recursive path enumerator checks all 512 directed three-node
graphs with all eight observation subsets (4,096 configurations), including
mixed normal/throw/resume edges. Separate chain and cycle families check
200/201/400/800/1600 scaling, exact budget and one-unit-short behavior. The
production solver is iterative; the deliberately small oracle is test-only.


### Bidirectional observation pairing (rules196)

The rules195 solver now runs over both adjacency directions using the same
implementation and scratch arrays. A forward reachability pass identifies
invocation-reachable predecessors for backward history; entry is its explicit
history seed. Disconnected structural exits cannot invent an earlier activation.
The forward result keeps its previous meaning for all graph points. Backward
results are absent for unreachable points. Both directions include all completion
kinds and preserve unresolved transparent-cycle conservatism.

For a source write S and read R, next[S]=R proves every successful write reaches
that read before another observation. previous[R]=S separately excludes a later
iteration reading again without another source write. These are execution-count
and ordering obligations, not a region-shape assumption. Reachability plus two
bounded propagation passes remain O(N+E) work/O(N) space; scratch is reused and
the second output vector is explicitly charged. The independent 4,096 graph
oracle now enumerates reverse reachable histories too, with entry as the boundary.
Linear/cyclic geometric families and exact/short budgets cover both results.


### Immutable loop declaration proposals (rules197)

PlanLoopDeclarations applies the ECMAScript ForDeclaration grammar to already
proven disjoint loop entry streams. It consumes existing declaration identities
and omission/projection facts; it does not infer binding lifetimes. One reverse
scan finds each representable suffix; one forward scan emits typed edits. With
S statement-indexed loop slots and R disjoint initializer roots, time is O(S+R),
proposal storage O(L+R) for L loops. The caller's declaration identity view costs
O(B) for B bindings. There is no per-loop tree or per-binding graph walk.

Planning takes const references and commit consumes only the completed delta.
A second plan on the applied state is empty. Multi-loop/reversed-order tests
cover 1/200/201/400/800 loops with exact and short resource/work budgets. This
is a grammar projection split, not a convergence proof for the whole pipeline.


### Compound copy results and retention dependencies (rules198)

Copy affinity consumes shared normal-completion result identities, rather than
matching the original RHS syntax. The sparse binding-copy graph still has at
most one value source per candidate. Visibility retention is a distinct graph:
each discarded reference may require another source if its containing copy must
remain. FlowEvent discarded ownership supplies the destination; adjacency lists
store all such edges and a once-enqueued worklist propagates retention. With B
bindings and R references, added work/storage are O(B+R); source reference
resolution and common flow analyses retain their separately documented costs.
No per-candidate subtree walk or repeated whole-plan fixed-point scan is added.

Copy elimination preserves required RHS evaluation at its occurrence. Readonly
source identity is independent of writable destination identity. This follows
the language's normal completion and declarative-binding semantics; calls,
throw paths and capture activation remain represented in existing shared facts.


### Expression value-use transport (rules199)

The existing source-side flow builder transports a typed value-use fact along
its owned expression occurrences. It records O(X) expression-indexed storage
and O(X) additional work, X the expression count; existing ownership prevents
replaying an occurrence through different contexts. Unvisited signature roots
remain unmodeled. Buffer reuse resets all facts, and effect refinement leaves
value use unchanged. Sequence-right elision queries this fact in constant time
per candidate and batches edits. No new ancestor scans or iterative whole-tree
fixed-point rounds are added.


### Complete interference graph and storage-group contraction (rules220)

The implementation in binding_interference/storage_coalescing is project-owned
classical greedy copy coalescing by interference-graph contraction, with union by
size, path compression and ordered conflict sets. It introduces no dependency or
third-party source; the project license applies. This is not optimal coloring.
Source initialization, scope, lifetime and effects remain separate prerequisites
at the caller, so graph contraction alone never authorizes source rewriting.

Let B be binding count, D definitions, L live-in facts, K copy observations,
A unique affinity edges, M live-fact/predecessor definition conflict observations,
E unique conflict edges, and Q live-fact predecessor edge visits. Construction
costs O(D+L+Q + K log(K+1) + M log(M+1) + A+E), with O(K+M+A+E)
budgeted storage. Repeated affinities are charged before deduplication. Set comparisons during contraction are charged.
Every removed group at least doubles its DSU weight; each original conflict endpoint
can therefore be rewritten O(log(B+1)) times. Contraction costs
O(B + (A+B) alpha(B) + A log(B+1) + E log(B+1)^2) and O(B+E) resident storage,
in addition to cumulative charged allocation growth. Output is B direct mappings.
Dense M/E is genuine input/output cost and is not discarded. Typed resource/work
exhaustion returns no partial storage plan. The complete process RSS gate remains
paused; these counters do not establish peak-memory compliance.

Printed-slot choice is independent of balancing. Shared spelling-group counts
provide O(1) uniqueness preference after their existing indexed name sort; current
initialization dominator coordinates provide a rank, not proof of completion.
Name comparison costs remain explicit in TreeScopeCheck. All actual occurrences
still use common name/lifetime/completion checks before commit. The existing
identity/alias composition therefore keeps O(1) direct-map lookups even for a
whole group. Independent dense tiny graphs, non-affinity conflict counterexamples,
source/symbol reversal and 200/201/geometric chain work checks verify the contracts;
no 200-item production cutoff or repeated per-binding CFG walk is added.


### Native lexical initializer projection (rules221)

The existing source-side semantic adapter now publishes a lexical initializer's
copy identity and statement write reference with completion/owner/exception
context. It reuses the classical def-use, liveness, dominance, copy coalescing
and observation-motion algorithms already documented here; there is no new
third-party implementation or dependency. Project-owned code uses the project
license and retains the legacy adapter/capture/abrupt-lifetime precision limits.

For S statements, X expressions and R retained binding references, this adds
O(S) statement projection storage and O(S+R) adapter/consumer census work per
existing pass. Each native initializer contributes one definition/reference;
its RHS remains owned once. Completion checks reuse the shared dominator index
in constant time per reference. Identity and effect cleanup use existing
postorder expression facts, without a per-binding CFG/subtree walk. Existing
sparse live-fact/edge costs and dense interference output costs remain; all
new arrays/events and loops use the same byte/work budgets. Tests at
0/1/200/201/400/800 statements check geometric work (<3 for doubling) and typed
resource failure. These tests are not process peak-RSS or performance benchmarks;
the paused full-process RSS and corpus gates remain paused.


### Initialization binding dependencies (rules222)

Project-owned mark-from-roots reachability implements unused-binding retention;
no copied implementation, new dependency or licensing requirement is introduced.
Provenance is the same standard observable-root elimination family as rules181,
with lexical name retention explicitly separate from runtime value demand.
Maintenance belongs to RecoveryFlow, shared demand analysis and the independent
binding elision consumer. Native private initializer regions carry their binding
owner; initializer effects and successful completion have separate obligations.
Current projected syntax, binding exposure and FlowDemand are prerequisites.
Capture/resource contracts retain existing boundaries; arbitrary lifetime
movement and live-initializer value pruning are explicit precision gaps.

For B bindings, N events, D definitions and K initializer-read dependencies,
FindUnusedBindings scans bindings/events once, stores dependencies in indexed
adjacency, propagates a binding queue once per retained binding/edge and scans
definitions once. Work and storage are O(B+N+D+K), with K <= N; dense input is
charged honestly. FlowDemand retains its existing sparse fact-dependent cost.
The new analysis is cached in the same projection revision aggregate as all
other graph facts. A commit invalidates it wholesale; no fixed-count sweep or
per-binding subtree/CFG traversal is added. Capacity, output, adjacency, queue
and work are charged against typed semantic budgets; this is not process RSS
or full-corpus evidence. The previous pending-elision indexes and joint commit
are removed from the production decision path.

The small independent oracle uses dense Floyd-Warshall closure over all 65,536
three-binding graph/effect/root/reversed-layout cases. It is test-only; production
never selects a quadratic fallback at 200. Chain tests exercise 200/201/400/800
bindings, growth and exact/short budgets, with source execution covering retained
RHS effects, TDZ failures, overwrites and repeated loop effects. See status.md for
measured verification and record acceptance scope.


### Typed write-site dead-definition deltas (rules223)

The existing project-owned observable-root demand solver and dead-definition
planner now carry a FlowReferenceSite plus optional RHS in each proposal.
FlowDefinition::WriteSite is a constant-time query over the adapter's typed
expression/statement positions, reused for publishing references. No new solver,
dependency, external implementation or licensing requirement is introduced.
Maintenance remains with RecoveryFlow, PlanDefinitionElisions and its source
projection consumer. Required demand, inert successful writes and the supplied
validated storage projection are prerequisites; lexical initialization and
updates retain separate completion/conversion obligations.

For D definitions and G evaluation regions, delta construction still costs
O(D+G) work/storage, and commit O(number of elisions). The larger typed proposal
records are charged, including optional RHS and valueless undefined slot writes.
There is no per-binding scan, tree re-matching, additional graph traversal or
fixed-count sweep. The existing sparse demand fact cost and revision invalidation
remain unchanged. Tests cover both site kinds, all semantic definition kinds,
effects and live/dead facts, with exact/short budgets; source tests extend existing
0/1/200/201/400/800 and reversed arenas. This is no RSS or corpus claim.

### Dead initial values and independent completion projection (rules224)

This extends the existing project-owned observable-root dead-code analysis;
there is no copied implementation, new dependency or licensing obligation.
Maintenance is in RecoveryFlow, FindDeadInitialValues and source completion
projection, with the printer rendering the immutable result. Prerequisites are
the current validated private-binding projection, shared value/effect demand,
and explicit RHS owner and lexical completion. Successful initialization is
retained independently of the dead value; source/capture/resource-visible
contracts keep existing boundaries. Complete binding lifetime/unwind, original
IR correspondence and global pass convergence remain open.

For D definitions and S statements, the new proposal costs O(D) work/storage and
the occurrence-indexed completion plan O(S) storage. Binding declaration identity
publication adds one O(D) scan using supplied projection ownership. Storage
completion remapping adds one O(S) scan and owned occurrence proposal storage;
it cannot substitute original placeholder locations for recovered declarations.
Lexical resolution/lifetime proposals enumerate current undefined definitions
in O(D) space/work before the existing indexed scope resolution, replacing the
original-placeholder-only census. RHS evaluation and undefined completion each
add bounded events/edges; no per-binding graph or
subtree traversal is introduced. Existing sparse demand/liveness costs remain
fact-dependent. Value/copy/snapshot proposals carry one additional optional
initialization identity, charged in their existing owned arrays; retirement
preserves prior independent RHS projections without another traversal or sweep.
This does not claim the whole solver is linear. Final loop grammar
uses its existing indexed initializer ranges once, with explicit optional suffix
clearing after binding cleanup. It replaces the former placement before dependent
cleanup and removes that post-grammar cleanup group, rather than adding sweeps.
Every output, event and scan is charged against typed byte/work budgets.

Cartesian dead/live, private/observable, inert/observable, required/inert and
modeled/unmodeled facts test the proposal contract with exact/short budgets.
Model tests verify completion ordering/identity and independent RHS retention
through binding/store removal. Existing two initializer encodings, arena reversal,
0/1/200/201/400/800 and independent execution/mutants now include dead initial
values, required call/comma effects and pre-initialization self reads. Indexed
loop grammar clearing is exercised at 1/200/201/400/800 without a quadratic fallback.
These are bounded semantic/cost tests; full-process RSS and broad corpus remain
paused. See status.md for measured verification and record scope.


### Recovered source region publication (rules225)

This uses the standard lexical-tree DFS ancestry interval index already built by
source ownership preparation. The implementation is project-owned; no copied
implementation, dependency, license or maintenance obligation is added.
RecoveryFlow maintains the published facts and binding_initializer supplies the
checked current projection. Prerequisites are validated source ownership and
an active recovered declaration region; spelling resolution, initialization
completion, capture/activation and exception obligations remain independent.

For S statements and B bindings, preparation adds O(S) work/storage; publication
adds O(S+B) charged work/storage to the existing flow build. Each containment
query is O(1), replacing the old RootLifetime query in four consumers. Occurrence
and region arrays, binding variants and publication copies are charged against
the existing typed byte/work budgets; reusable storage is cleared on rebuild.
The existing sparse flow/demand/liveness cost is unchanged. There is no new
per-binding graph traversal, whole-pass round or quadratic production fallback.
This does not claim complete lifetime reconstruction or a linear whole solver.

The independent test-only oracle walks parent chains and enumerates pairs; its
quadratic work is deliberate and is not a production scaling measurement.
Forward/reverse identities and 200/201/400/800 cases check indexed membership,
with missing facts, invalid IDs, budget exhaustion and stale-capacity clearing.
Existing source semantic/cost cases remain the end-to-end evidence; full-process
RSS, benchmarks and broad corpus remain paused. See status.md for measured runs.


### Primitive normal-result propagation (rules226)

This is the project-owned constant/unknown value lattice over the existing checked
expression postorder, using ECMA-262 2024 assignment, sequence, conditional and
logical completion/ToBoolean rules. It is not SCCP, arithmetic folding or CFG
executable-edge elimination. Maintenance stays in ComputeExpressions and the
existing Constants/Aliases consumers; no copied implementation, new dependency
or license obligation. Prerequisites are shared definition coverage and the
independent successful-write, initialization, scope, capture and effect proofs.
Only original primitive literal nodes can supply a constant result; required RHS
execution remains at its original occurrence and handler.

For E expressions, A child edges and T text entries, publication adds O(E+A)
work and O(E) storage to the existing expression analysis. Literal joins and
ToBoolean/nullish tests are O(1). UTF-16 value interning happens once per immutable
function input: O(T log T) comparisons plus C actually compared code units,
charged individually, and two O(T) ID arrays (retained index and temporary order).
It does not rescan text per expression or projection revision. Existing sparse
copy propagation remains O(B+copy edges) after shared coverage, with no repeated
pass sweep, new per-binding traversal or pairwise production algorithm. The
full flow/demand/liveness cost remains fact-dependent, not claimed linear.

Geometric tests include 200/201 and common-prefix text pools at 400/800; exact and
short byte/work budgets exercise the whole plan. The independent JS reference
checks compound primitive and binding results with effects, setter throws,
selected/unknown branches, signed zero, NaN, TDZ and catch reads. Forward/reverse
text/arena IDs must retain recovery and behavior. A deleted-initializer or early
write must change the independent observation. These are bounded semantic/cost
checks; broad corpus, full-process RSS and benchmarks remain paused.


### Primitive definition joins over copy components (rules227)

AnalyzeFlowConstants implements the project-owned constant/unknown value join
and iterative Kosaraju SCC condensation already used by parameter_origins.cc.
The established SCC algorithm avoids per-binding walks and repeated convergence
rounds; no external implementation or new dependency/license is introduced.
Maintenance is isolated in flow_constants.cc; the checked source adapter supplies
typed successful-definition equations from common expression facts. Prerequisites
are one immutable RecoveryFlow/liveness revision, canonical primitive identities,
private storage and absence of live entry demand. All retained noninitial writes
participate, including updates whose normal result is used without a GetValue.
Dead lexical undefined can be excluded only by shared liveness. Unknown writes,
exposure, missing grounding or conflicting primitive values prevent substitution.
Writes, effects, exception timing and lexical completion remain separate proofs.

For B bindings, D definitions, C retained copy edges (C <= D) and L published
live-in facts, graph construction, two iterative graph traversals, component
joins, condensation propagation and publication take O(B+D+C+L) work and
O(B+D+C) additional storage. Adjacency is indexed, stacks are iterative and all
arrays/work are budgeted. Copy-edge capacity is exactly C, not D. Dependency and
component IDs use the common flow strong 32-bit identity domain with a typed
resource failure for an unrepresentable count, avoiding size_t-width adjacency
slots. The existing flow/liveness cost remains fact-dependent;
this bound does not claim a linear whole recovery pipeline or process RSS proof.
Primitive comparison is O(1) after rules226 immutable-input UTF-16 interning.
Constants replaces only reads; separately scheduled cleanup owns writes and
bindings. A Constants commit schedules cleanup before Aliases. A subsequent
alias/identity/effect commit requires fresh cleanup; no commit leaves the already
clean projection intact rather than charging duplicate same-version analysis.
RunPass revision movement drives these dependencies. The fresh fixed record
checks the unchanged default allowance, not an inflated resource budget.
No quadratic production fallback, new pass sweep or input-count cutoff.
This invariant is less precise than per-read SCCP/executable-edge analysis, and
known undefined lacking an original literal anchor cannot yet be printed.

The independent slow test oracle enumerates 32,768 three-binding edge/seed cases,
walks backwards origins and verifies every contributing origin has a primitive
source. Its repeated graph walks are test-only and independent of SCC ordering.
200/201/400/800 cycle cases exercise grounding, live initial undefined, exposed
bindings, live entry demand, dead unknown updates and exact/short budgets.
400/800 work must grow by less than threefold. Independent JS execution compares
equal/different branch definitions, self/mutual copies, zero/multiple loop
iterations and consumed prefix update, with reversed arenas and both declaration
encodings. Semantic mutants must differ. Status.md owns measured times/counts;
full suite, wider corpus, benchmarks and RSS remain paused.


### Immutable source primitive pool (rules228)

This extends the project-owned primitive completion/definition lattice with a
source value identity independent of AST occurrence. ECMA-262 empty lexical
initialization supplies undefined; input literal values preserve typed numeric
representation/bits and rules226 canonical UTF-16 identity. It introduces no
folding, external implementation, dependency or license obligation. Maintenance
stays in source preparation, shared primitive identity and the literal printer.
Initialization, liveness, definition join, effect and scope proofs remain distinct.

For E input expressions and P literal entries plus the implicit initialization
value, counting and indexing take O(E+P) work, O(E) private expression-ID indexing
and O(P) published typed value storage. Values do not duplicate string contents;
they retain validated TextIds into immutable input. Known value joins/truth/
nullish tests remain O(1); pool capacity and index work are budgeted. The pool
is built once per function preparation and stays immutable across pass revisions,
not rebuilt per definition, use, variable or analysis. No graph traversal, global
pass sweep or input-count threshold is added. Default allowances remain unchanged.

The source plan and ExpressionFacts keep compact optional PrimitiveIds in the
same slots that previously held optional ExprIds. Proposed substitutions compare
semantic value equality, avoiding revision changes caused only by choosing an
equal occurrence. The printer emits a proved value with the existing operator
precedence and each read's original range, without new AST nodes or independent
optimization. Independent JS execution checks no-literal-input undefined copies,
loop cycles, selected parameter values and required calls, and rejects early-TDZ,
unknown-write and wrong-value variants. Native/store encodings, reversed arenas,
200/201/400/800 and exact/short byte/work budgets remain the bounded evidence;
full suite, wider corpus, RSS/DFX and benchmarks remain paused.


### Sparse normal-value equations and preparation reuse (rules229)

Algorithm provenance: [Wegman and Zadeck, Constant Propagation with Conditional
Branches, ACM TOPLAS 13(2), 1991, pp. 181-210](https://courses.grainger.illinois.edu/CS426/fa2022/Papers/sccp.toplas91.pdf),
flat value lattice (section 2.2) and sparse dependency worklists (sections 4/5.4).
This project-owned implementation borrows those ideas, not external code. No
library, copied implementation or new dependency/license obligation is introduced.
It is not full SCCP: the source adapter supplies normal-result meaning and the
solver never changes executable CFG edges. Maintain it with that adapter and the
independent graph/source oracles. Unknown operations stay unknown.

For B bindings, E input equations, D successful definitions, C retained mutable
value-dependency incidences, and L incoming live facts, construction and solve
cost O(B+E+D+C+L). Each value advances unresolved -> primitive -> unknown at most,
with at most one additional printable-identity notification. Each expression
transfer reads at most three operands; binding joins are incremental. No repeated
scan of all definitions, graph walk per binding or whole-pass iteration is used.
A queued flag limits the pending stack to the number of mutable nodes. Terminal
primitive/unknown equations do not own mutable nodes; a binding read maps to its
binding node. The E-sized typed-absence index retains every terminal operand's
meaning, and primitive payloads borrow immutable input until result publication.

Working storage is O(B+E+D+C): immutable base equations are adapted once, current
projection borrows replacements and owns a B-sized binding snapshot, mutable
states/adjacency/worklist use B+K slots (K nonterminal expression transfers),
and edges reserve actual C. Returned values own copies and retain immutable pool
IDs. Every allocation/capacity and work step is admitted under existing budgets;
conservative cumulative charges are not process peak-RSS evidence.

Class-name preparation transfers its already charged descendant plans into the
printing owner rather than freeing/rebuilding them. A sorted exact-tree index
costs O(T log(T+1)) comparisons for T owners, then O(log(T+1)) lookup per emitted
owner. Moves/sort do not duplicate plan arrays; emission borrows them synchronously
under unchanged trees/external context. Existing destination/capture checks still
run. Retained memory lives until that printer finishes, so the owner must include
it in admission; no cross-method cache, threshold or error fallback is added.
Static field emission still separately prepares its plan; broader reuse is open.

Tests use the 32,768 independent copy oracle, 864 finite truth/selection cases in
direct and borrowed representations, 0/1/200/201/400/800 source chains, exact/short
byte/work budgets and independent JS execution with incorrect mutants. Existing
geometric work bounds and arena/copy variations remain. Fixed-record regression
was retained and resolved with compact facts/owner reuse at unchanged budgets.
Measured verification belongs in status.md; full-suite/corpus/RSS/DFX/benchmarks
remain paused.


### Revision-owned source pass scheduling (2026-10-08, rules230)

RecoverySchedule is an allocation-free finite state machine over declared pass
contracts, projection revisions, semantic stages and an ordered demand-cleanup
checkpoint. Each Start/Finish/Commit/fact request costs O(1) time and storage;
there are six cached fact kinds and a compile-time finite dependency closure.
Providers remain lazy and retain the existing budgeted analyses. No N-sized pass
queue, eager analysis, added graph scan or unconditional global round is introduced.
A committed projection dirties cleanup; the complete definition/initial-value/
binding group publishes the current checkpoint. No-op passes preserve it; only
proven snapshot motion transports it across a commit. This is conventional
compiler analysis invalidation/dependency ownership, implemented locally without
a dependency or copied upstream code.

The changed barrier can run cleanup earlier than the former Constants-specific
boolean branch, so whole-function costs are still the sum of the actual analyses
invoked, not a claim that the full pipeline is O(1) or linear. Checkpoint freshness
does not establish a global optimization fixed point. Existing 200/201/geometric
source and sparse-graph budget tests remain the meaningful scaling evidence;
controller repetition tests exercise version/checkpoint stability. Final selected
timing and unchanged-budget record evidence are in status.md. RSS/full-corpus and
benchmarks remain paused.


### Sparse per-read normal values (2026-10-08, rules231)

Extend the project-owned flat constant lattice and worklist documented at rules229
with incoming live-fact and actual read nodes. Binding invariants are solved and
frozen first; unresolved completed private reads then consume them as immutable
terminal inputs. Both solves use the same projection and share the same total
budget. The contextual solve is skipped when no such unresolved read exists.
This is value propagation without executable-edge SCCP, external code or a new
dependency. The source semantic adapter and independent oracles own maintenance.

Let B be bindings, E expression equations, D definitions, L all incoming live
facts, F the selected incoming facts, R selected read expressions, and I actual
dependency incidences (including predecessor transfers). A B-sized mask selects
bindings, and a sorted F-sized index borrows the existing liveness facts instead
of copying them. Incoming lookup costs O(log(F+1)); construction and solving cost
O(B+E+D+L+I log(F+1)), plus primitive payload comparison units as previously
documented. There is no read-by-definition relation or per-variable graph walk.
Each mutable value advances through the finite lattice; each expression has at
most three operands, and incoming/read joins notify dependents incrementally.
The independent invariant solve adds its previously documented cost, not an
unbounded pass round. All terminal operands, unknown predecessors and completion
edges keep their meanings.

Working storage is O(B+E+D+F+R+I): immutable inputs are borrowed, terminal known
equations have no mutable nodes, and the output has one owned record per known
read. The committed source plan adds an E-indexed optional PrimitiveId array.
Both analysis domains, their temporary/output arrays, actual edges, lookup steps
and proposal storage are charged under unchanged budgets. These cumulative
charges do not establish process peak RSS. The finite-set small-graph oracle may
use a deliberately slow reference solver; production remains sparse. Tests use
0/1/200/201/400/800 sources, exact/short byte/work budgets, arena permutations,
32,768 read graphs, independent execution and adverse substitutions. Selected
timing and unchanged-budget fixed-record evidence are recorded in status.md.

### Normalized placement and lexical slot preference (2026-10-08, rules232)

The existing placement analysis remains a successful-definition dominator scan
on shared RecoveryFlow and retained lexical extents. The sole pass moves after
value normalization and independent cleanup, before alias/storage coverage; the
early call is removed. Its current revision checkpoint costs O(1), and a commit
requires a fresh independent cleanup group. Total cost is the sum of invoked
analyses; no global fixed point or O(1) whole-function claim is made.

Storage coalescing reuses common projected SourceRegion and source occurrence
facts. One O(R) reference scan builds extents, O(B) fields/working arrays carry
them, and each accepted DSU union joins two intervals/missing-position flags and
compares candidate coverage in O(1). Existing conflict adjacency/DSU costs remain.
All arrays, scans and proposals remain budgeted. No library, per-variable graph
walk, read-by-definition relation or AST scope census is added. If candidates
have equal coverage, existing representative ranks stay; the full post-proposal
proof remains. This is bounded two-candidate preference, not exhaustive feasible
root search. An unconditional wider-scope heuristic was retired after record
loop spelling regressed. Total tests include both-covered native spelling.

Tests grow real unknown reads at 0/1/200/201/400/800, reverse encodings/arenas, check
exact/short budgets and compare independent execution. Scope preference stars
reverse IDs at 2/200/201/400/800 and retain repeated-loop merging. Loop callbacks
now actually observe explicit undefined; producer mutations remain adverse.
Selected timings and unchanged-budget fixed-record evidence belong in status.md.
RSS/benchmarks/full corpus remain paused.

### Final-group representative selection (2026-10-08, rules233)

The existing union-by-size interference contraction and common all-completion
dominator analysis remain the algorithms/provenance; no library or dependency is
added. Union-time source winner selection is removed. Let B be bindings and R
actual common references. After membership closes, an additional B-sized scan
with path-compressed finds ranks all original members, costing O(B alpha(B))
amortized; interval coverage/rank comparison is O(1). Working candidate and
reachable-completion arrays cost O(B). Existing weighted conflict-adjacency
rewrites and greedy membership cost are unchanged, including dense-output costs.

The source adapter reuses current FlowInitializations' dominator subtrees and
preorder positions, adding constant work to the existing O(B+R) scans. Joining
reachable reference extents is O(1) per accepted union. Lexical SourceRegion and
execution CompletionRegion are typed separately. There is no per-binding graph
walk, candidate-by-reference relation or repeated semantic solve. All arrays,
finds/scans/comparisons and proposals are charged under the existing budgets;
post-proposal proofs retain their existing costs. These charges do not prove RSS.

The final scan is exhaustive only for the modeled scope/completed-initialization
preference. Independent name/cell/initial-value checks may still retain members;
no complete feasible-root search or order-independent greedy partition is claimed.
The small oracle deliberately uses dense pair scans and enumerates all candidates
against original observations, rather than production DSU/interval summaries.
Tests cover 45,056 oracle cases, 3/200/201/400/800 reordered groups, exact/short
budgets and actual native initializer/producer source execution. Current selected
timings and fixed-record evidence belong in status.md; paused scope is unchanged.

### Expression-occurrence snapshot projection (2026-10-08, rules234)

Provenance remains the shared completion-flow/value-use and bidirectional universal
observation algorithms, plus the existing batched lexical-name index. No dependency
or library is added. Typed source occurrence transport is distinct from read value
replacement, preserving arbitrary surrounding effects. A single trial graph uses
the production RecoveryFlow adapter; it does not infer bytecode semantics again.

Let E be expressions, S statements, B bindings, K proposals and R actual reference
plus free-name occurrences. Trial plan buffers cost O(E+S), expression owner/root
arrays O(E), proposal/failure arrays O(B+K) and query buffers O(R). Adapter work
remains its existing graph/event/edge cost plus O(E) root propagation. Candidate
filtering is O(K), reference classification O(R); name resolution retains its
indexed/sort costs. A root propagates through each actually projected expression
once; no candidate-by-subtree relation, same-statement grouping or per-candidate
rebuild is introduced. Current graphs retain only owner arrays; root metadata is
trial-only and clears when recycled. All arrays, graph buffers, scans and proposals
are charged to the original byte/work budget, including simultaneous current and
trial graph storage. These charges are not process RSS evidence.

A failed root conservatively retains its dependent component without a retry
loop. Independent roots sharing a statement remain separately selectable. The
commit phase invalidates facts before writing; subsequent passes lazily rebuild
for the new revision. Store/operand/statement normalization and dead-definition/
initial-value/binding cleanup remain independent ordered passes. Printed names and
origin spans consume the same projection. Tests exercise 200/201/geometric and
reverse arenas, unique RHS evaluation, independent execution and semantic mutants;
actual selected timings are in status.md. Broader corpus/RSS/benchmarks remain paused.

### Canonical lexical-region indexing (2026-10-08, rules235)

Provenance is the existing preorder subtree interval containment index, published
by the source adapter once and consumed by common RecoveryFlow. No dependency or
license change. With Q checked source scopes, S statements and B bindings, the new
canonical scope index costs O(Q) storage, O(S) traversal work (inside the existing
traversal), and O(B) lookup work when building current Flow. BindingRegion and
BindingInScope are O(1). Storage consumes the same intervals and completed-group
reference extents, so there is no per-binding ancestor walk or extra optimizer
census. Existing interference/output costs remain unchanged; all retained arrays,
validation and graph copies use the original byte/work budget.

Canonical and recovered intervals are lexical coordinates, distinct from ALL-
completion dominator intervals. Both are candidate preferences; post-proposal
spelling, completion, cell and initial-value/effect obligations still decide
acceptance. Original bindings without supplied regions retain the legacy contract;
missing projected regions reject access. Tests compare against a small independent
ancestry oracle, reverse arenas, exercise 200/201/geometric source sizes, exact/short
budgets and independent generated-source behavior/mutants. This evidence does not
establish whole-process RSS or global pass convergence; paused checks remain paused.

### Shared shadow regions and storage observation index (2026-10-08, rules236)

Provenance is the existing canonical spelling-group activation stack, preorder
subtree intervals and sorted point/interval stabbing queries. No dependency, license
or pinned profile changes. Prerequisites are the checked scope tree and one current
RecoveryFlow; canonical spelling facts remain conservative for elided declarations,
with final actual-name proofs mandatory.

Let B be bindings, Q scopes, R actual reference positions and H direct shadow facts.
Spelling analysis reuses the existing O(B log B) name sorting (including charged
UTF-16 comparison lengths), then O(Q+B) activation/query work and storage. Every
binding has at most one nearest enclosing same-spelling binding, so H<=B; nested
shadows require no expansion to every ancestor. Optimizer-local uniqueness indexing
retires. Existing current Flow copies O(H) shadow facts and B unique flags under the
original budget. Source region lookup is O(H); references are already scanned once.

After final DSU grouping, sort R observations by group/position in O(R log R),
construct group ranges in O(R+B), then query each known shadow interval in O(log R).
Total additional name work is O(R log R+H log R+B), storage O(R+H+B), plus existing
amortized DSU find costs. With H=0 skip the observation index. Candidate ranking
still visits all original members once. No per-candidate CFG/name/ancestor scan,
whole-function retry or new pass round. All buffers/comparisons/queries are charged;
these accounting facts do not prove process RSS. Tiny dense oracles are test-only.
Tests include holes without observations, unknown facts, reverse IDs/affinities,
200/201/geometric source/group sizes, independent execution, mutants and exact/short
budgets. Actual measurements are in status.md; broader corpus/RSS remain paused.

### Current retained-name projection (rules237, 2026-10-09)

Provenance: standard sparse rooted-forest traversal and memoized nearest retained
ancestor, implemented locally without dependency/license/profile changes. Inputs
are checked canonical nearest same-spelling parent identities and one current
RecoveryFlow's binding kinds/exposure, definitions and references. Name retention
is separate from value liveness and source lifetime; legacy unidentified shadows
retain conservative meaning.

For B bindings, H canonical shadows, D definitions and R references, retention
scanning and forest projection use O(B+H+D+R) work and O(B+H) storage. Each binding
has at most one canonical parent; adjacency arrays and a bounded parent-before-child
queue visit every vertex/edge once. No per-binding ancestor walk, recursion or
repeated sweep occurs in production. Fresh output/temporary buffers and all scans
are charged to the unchanged semantic budget. Fresh graph facts replace prior masks
and shadows; this does not claim buffer-capacity reuse for the new analysis.

Current name resolution retains the existing spelling sorting and scope traversal;
mask activation adds O(B) validation and constant work per visited binding. Small
independent ancestor walks are test-only. 200/201, chain/star geometric growth and
exact/short budgets validate accounting; measured selected acceptance is in status.md.
These results do not establish full-process RSS, which remains paused.

### Current source-name range index (2026-10-09, rules238)

Provenance: standard sorting and stack sweep of nested lexical intervals, followed
by binary search in disjoint active-name ranges. Implemented locally without new
dependencies, licensing or profile changes. Prerequisites are validated canonical
spelling identities, retained binding presence, current original/projected lexical
regions and explicit enclosing-environment facts. Lexical intervals are nested or
disjoint by source scope semantics; crossing inputs are invalid adapter provenance,
not a compiler-layout admission. Unknown local scope remains a typed gap.

Let B be bindings, N NameIds, H canonical shadow edges, D definitions, R references
and Q queries. Text identity publication adds O(N log(B+1)) name comparisons to the
existing O(B log(B+1)) spelling sort; compared UTF-16 units are charged. Current
retention is O(B+D+R), canonical enclosing-layer precedence O(B+H), and current
range sorting/sweep O(B log(B+1)+B+H+N), with O(B+H+N) retained/temporary storage.
There are at most 2B disjoint ranges; each lexical interval enters/leaves once.
Current direct shadows are produced by that sweep rather than a per-binding
ancestor scan. Unknown legacy shadow identities remain conservative.

Q read-only symbol/free-name queries take O(Q log(B+1)) work and O(Q) output.
Optimizer-local repeated canonical spelling/scope checkers and the original
statement-scope query table retire. All buffers, text comparisons, sort comparisons,
queue/sweep visits and binary searches use the unchanged semantic budget. Rebuilt
indexes replace prior facts; this does not claim new buffer-capacity reuse. Tiny
slow ancestry oracles are test-only. 200/201, geometric chain/star and exact/short
budgets validate cost/accounting, not complete process RSS (still paused).

### Prospective copy-retirement name demands (2026-10-09, rules239)

Provenance: monotone Horn-style implication closure with an adjacency worklist,
sorted lexical interval/root indexes, and successor-set deletion with path
compression. Local implementation; no new dependency, license or profile. Requires
validated spelling/scope facts and a value/effect-safe retirement proposal whose
symbol lookup roots are fixed retained bindings. Namespace analysis has no write
or declaration-deletion authority. Missing facts remain explicit precision gaps.

For B bindings, N NameIds, H canonical shadow edges, Q proposed reads and D external
retention demands, baseline name indexing costs O(B log(B+1)+B+N+H), query resolution
O(Q log(B+1)), and sorting query positions O(Q log(Q+1)). Root partitions avoid
scanning deeper fixed-binding queries on each ancestor activation. Each candidate
activates once; each query fails once and adds at most one conditional edge. Binary
range lookup adds O(B log(Q+1)); successor deletion uses monotone path compression
with a conservative O(Q log(Q+1)) amortized bound (no rank-union claim). Overall
O((B+Q) log(B+Q+1)+N+H+D), memory O(B+Q+N+H+D). Each explicit demand edge is charged;
there is no per-variable graph walk, pairwise threshold or repeated full analysis.

Coincident-region occupancy and once-per-spelling precision invalidation are
indexed. All owned buffers, nested analysis costs, sorts, binary searches, worklist
visits and successor traversals consume the existing semantic budget. Small slow
repeated-resolution oracles exist only in tests. 200/201/geometric and exact/short
checks measure work/accounting; process RSS and broad corpus validation stay paused.

### Joint storage namespace and subset initialization (2026-10-09, rules240)

Provenance: reuse the shared monotone retirement closure, existing ALL-completion
dominance/liveness and interference-graph contraction; no dependency/profile change.
Prerequisites: interference-safe direct fixed-root mappings and unchanged flow,
liveness and initialization facts. Original binding completion/TDZ, root completion,
lexical lifetime and live-undefined preservation remain independent obligations.

Coalesce scans B proposed bindings, R retained semantic references and D definitions
to form unconditional name queries and independent retention demands. Initialization
planning is one O(D) scan. Each live-undefined action depends on its own original
liveness and fixed-root completion; discarding another mapping cannot change it.
No full-graph reconstruction or per-member reanalysis is required after rejection.

Q<=R+D queries and at most B+R+D demands feed the common sorted root/position index
and bounded activation worklist. Additional cost, beyond unchanged interference and
contraction, is O((B+R+D) log(B+R+D+1)+N+H), memory O(B+R+D+N+H). Buffer sums use checked
arithmetic; allocations, visits and nested analysis costs consume original budgets.
Completion/lifetime feasibility outranks current-shadow preference in representative
selection. That preference adds no admission; only the final proposal namespace
establishes occurrence resolution. Independent small rank/subset oracles and generated
source composition/mutants/layout/geometric/exact-short tests verify these contracts.
Full process RSS and expanded-corpus benchmarking remain paused.


### Stable storage-boundary snapshot refresh (2026-10-09, rules241)

Provenance: revision-stamped compiler pass dependency invalidation and lazy analysis;
local implementation, no new dependency, license, profile or acceptance mode.
Prerequisites: validated current RecoveryFlow and a complete ordered demand-cleanup
checkpoint. Snapshot forwarding retains its existing coverage, execution-count,
exception/effect-order, initialization, binding identity and lexical lifetime proofs.
Consumer checkpoints record completed attempts, never analysis cache validity.

RecoverySchedule compares two revisions in O(1) time/space at the storage boundary.
The original snapshot attempt remains before contraction; at most one additional
attempt follows storage/alias normalization. No whole-pipeline retry or input-sized
round count is introduced. A no-op producer retains the revision and avoids refresh.
The added attempt has the same charged cost as the existing forwarding analysis,
so the pipeline increases that component by a constant factor rather than repeating
it per binding. This does not claim arbitrary downstream opportunities converge.

Let B be bindings and R retained references, S statements and C lexical containers.
Existing candidate/read/write scans cost O(B+R); lexical environment construction
costs O(S+B+C) work and O(S+C) temporary storage and runs only for actual proposals.
The existing common motion solver is allocated/computed at most once per unchanged
attempt and only after a candidate reaches its existing semantic prerequisites.
All graph/index/proposal buffers and nested analysis work remain under the original
semantic budgets. State/stage/partial-cleanup negatives, independent generated-source
execution and 200/201/400/800 composition verify contracts and bounded work. This is
not a new whole-process RSS claim; full RSS and expanded-corpus runs remain paused.


### Symbolic entry identities in the flat value domain (2026-10-09, rules242)

Provenance: extend the existing Wegman/Zadeck-style sparse flat value propagation
with typed opaque entry identities; local implementation, no dependency/license/
profile change. The domain is bottom, one primitive or stable binding value, and
unknown. A node advances to a known value then unknown at most, with the existing
printable primitive notification. Grounding needs explicit adapter entry premises
and no retained write; observable/captured/mapped/unmodeled values stay unknown.
All-definition symbolic invariants remain separate from contextual primitive facts.

For B bindings, D definitions and R references, grounding adds O(B+D+R) work and
O(B) buffers. Symbolic joins compare typed IDs in O(1), sharing the existing sparse
expression/dependency/liveness graph and its worklist rather than introducing a
second solver or per-binding graph traversal. Flat state storage increases by a
constant-sized identity field. Q destination name queries use the existing common
index in O(Q log(B+1)) work and O(B+Q) proposal buffers. All buffers, nested analysis,
sorts and visits retain existing byte/work accounting and finite-budget behavior.

The new ReadValues stage runs one independent read-only consumer after storage and
snapshot choices. There is no input-sized pass round count. Read substitutions are
owned deltas; common current flow/effects and independent cleanup rebuild by revision.
A source adapter computes the mapped-argument premise once, removing its duplicate
alias census. Empty consumers avoid value analysis when there is no eligible binding.
32768 tiny independent copy-origin closures, equal/conflicting expression joins,
live/dead undefined, missing/written entry roots, reversed IDs, 200/201/400/800 and
exact/short/invalid-input tests verify results and cost. Generated source checks
return/error/event/object identity and syntax; full process RSS stays paused.


### Constant-cost binding projection composition (2026-10-09, rules243)

Provenance: local typed view over existing committed source alias/storage/read-value
plans; no new dependency or licensing change. Stage-owned direct roots determine
composition order: early alias, storage slot, final alias, then a read-only stable
value indexed by that slot. BindingProjection centralizes the operation for common
flow and source consumers. Writes and update halves stop at Storage.

Each read/storage query is O(1) work, O(1) stack and allocation-free. Existing spans
are borrowed only during the immutable consumer phase; no new arrays, graph walks,
recursive lookup, round count or 200-item admission threshold is introduced. The
ReadValues stage prohibition on later alias/storage/motion changes is essential:
it establishes the canonical slot identity used by the proposal. Revision-owned
fact invalidation and independently charged cleanup remain unchanged.

Tests combine actual shared-slot branch copies, effectful RHS, written-entry and
lexical-shadow counterexamples with reversed arenas and 0/1/200/201/400/800 block
composition. Eight common-flow alias/storage/read projections separately verify
actual read identity, assignment targets and both update halves, immutable earlier
results and exact/short budgets. Full process RSS remains paused.


### Shared normal-result copy affinities (2026-10-09, rules244)

Provenance: reuse the local normal-completion result analysis and existing sparse
copy-affinity/interference solver; no dependency or licensing change. A copy fact
states source binding value equals destination value at its successful definition.
A postfix snapshot cannot supply this fact; prefix/assignment/selection results can
only when the shared producer proves current binding identity. Affinity is not
permission to share storage, move effects, suppress exceptions or remove writes.

For E expressions and D definitions, the producer already visits E once per current
projection. The adapter borrows its vector and performs O(1) typed lookup for each
value definition, O(D) incremental work and no added buffer. Current production
avoids the old E-sized direct-copy normalization buffers/work. An additional known
copy can increase the existing affinity/output dimension A; sparse liveness facts
L and traversed predecessor edges P drive conflict output K. Existing O(D+L+A log(A+1)
+P+K log(K+1)) interference work and O(A+K) solver storage remain charged honestly.
Dense conflict output is still bounded by the existing semantic resource budget;
no per-variable graph walk, pairwise cutoff or input-sized pass retry is added.

Current revision ownership and first-commit aggregate invalidation precede any
consumer mutation. Hypothetical projections retain a documented coarse fallback
without borrowing current normal-result precision. Tests cover composed source
values, live/exception/postfix counterexamples, native initialization, 200/201/
geometric/reversed layouts, explicit unknown facts, malformed span/ID boundaries
and exact/short budgets. No new whole-process RSS claim is made.


### Contextual symbolic values and shared reachability (2026-10-09, rules245)

Provenance: reuse the existing sparse flat normal-value lattice and finite monotone
notifications (primitive or proved immutable entry identity); no new dependency or
license. Distinct primitive/binding occurrence outputs express read values only,
never completion, store deletion or global binding identity.

For N events, Q completion edges, E expressions, B bindings, R references and C proved
symbolic reads, common invocation reachability adds O(N+Q) work, O(N) retained mask and
O(N) bounded scratch. It leaves conservative live facts unchanged. The contextual
solver filters existing sparse facts/edges by the mask; publication adds O(E+C) work
and O(C) owned output to its existing lattice cost. No read-definition Cartesian
product or per-binding traversal is materialized. Snapshot motion borrows this mask,
removing its current production reachability buffer/traversal; legacy standalone
analysis keeps its explicit construction.

ReadSources uses O(E) owned plan storage and O(1) occurrence lookup before the existing
slot invariant. Actual BindingValues proposals use O(E+B+R+C) scratch/batch queries,
at most two independently proved queries per reference; common name-index costs
remain separately charged. No-proposal functions skip query, owner, blocked and
occurrence-index arrays; invariant-only proposals need no expression lookup index.
Dense sparse-fact/output dimensions remain
honestly charged under existing budgets. No new pipeline round, cutoff or quadratic
selection algorithm. Independent 65536 graph cases, 200/201/geometric source layouts
and exact/short allowances verify this boundary; full process RSS remains paused.

### Initialization evidence transport (2026-10-09, rules246)

Provenance: the existing projection revision/invalidation protocol and semantic
pass preservation contracts; no new dependency or algorithm. The retained
ExprId-indexed initialized-read mask owns its source ProjectionRevision. Every
access verifies the current analysis phase/revision; first commit explicitly
transports that revision or revokes the mask under the pass contract. Flow/effect/
name facts remain independently invalidated. The former unversioned cache and
bare preservation boolean leave production decisions.

Each access/transfer is O(1) work and adds O(1) cache metadata; mask storage and
budget charging, graph costs and lazy analysis stay unchanged. No mask copy or
per-occurrence revision array is added. No-op passes do not manufacture revisions;
one batch advances once. Independent stale/phase/non-preserving and 200/201/
geometric protocol cases plus existing generated-source execution verify the
boundary. Full process RSS remains paused, so no new peak-memory claim is made.


### Available current-cell copies (2026-10-09, rules247)

Provenance: classical available-copy gen/kill dataflow, implemented internally in
flow_copies.cc with the existing charged sparse worklist and strong graph IDs; no
new dependency or external code/license. Prerequisites are current shared normal
copy definitions, sparse liveness, invocation reachability and completed binding
reads. Adapter evidence admits entry-cell identity independently of body writes.
Each successful source write kills a relation; each successful destination copy
generates one, other destination writes kill it. All completion edges participate;
failed-before-write paths retain the old state. Incoming paths must agree on a
single current source. Immutable roots and storage interference remain separate.

Let N/B/D/R/E denote events/bindings/definitions/references/expressions, L all
supplied sparse live facts, P all incoming edges inspected across the selected
reachable subset (including constant and unreachable predecessors), and A materialized dependencies
with A <= P. Filtering and charged sorted lookup cost
O(N+B+D+R+E+L+(P+R) log(L+1)); memory is O(N+B+E+L+A+output). No copy-pair enumeration or per-binding graph walk occurs.
A flat unresolved/source/unknown fact changes at most twice; counted adjacency and
bounded queued IDs notify only dependents. Ungrounded cycles become unknown.
Dense L/P/A/output is honestly charged, not discarded by a cutoff. All arrays,
iterations and failed resource admissions consume the existing budgets.

ReadValues merges sorted invariant/available occurrence outputs in O(output)
work and owned space before separate batched scope/name queries. First commit
invalidates revision-owned results; no borrowed analysis survives into mutation.
No full-function retry, new pass round or eager reanalysis is added. Independent
221184 concrete small-graph cases, 200/201/geometric layouts, unknown-write kills
and exact/short quotas verify this boundary. Full process RSS remains paused;
these bounds and quota checks do not constitute a measured peak-memory claim.


### Transitive available copies and completion queries (2026-10-09, rules248)

Provenance: the same internal classical available-copy gen/kill solver and sparse
liveness worklist as rules247, with normal-copy dependencies; no new dependency or
external code/license. Common all-read liveness seeds at most one source-value
query per normal-copy definition, on pre-commit state. Observable demand does not
seed them; a queried value is not an executed read or effect. Successful copies
inherit proved entry equality from private source facts; exposed/missing facts and
ungrounded cycles remain unknown. Intermediate overwrites do not retroactively
modify a saved copy; entry-root writes kill the current-value relation.

Let D count definitions, Q <= D count completion queries and L the shared sparse
facts after closing actual reads and queries. The shared liveness pass adds O(D)
seed visits and charged O(Q log(L+1)) insertion work; its existing fact/edge costs
include all expanded L. No consumer performs another graph closure or per-binding
walk. Copy generation adds one indexed dependency lookup per inspected copied
definition connection, already included in rules247's P/log L dimension; arrays,
output and materialized dependency A bounds remain unchanged with the expanded L.
The flat facts still change at most twice. Dense L/P/A/output is charged honestly.

Source chain insertion, independent 524288 two-cell concrete graph states,
200/201/geometric chains with split edges/reversed arena and exact/short quotas
exercise costs and layout independence. Query-only demand and exposed-source
counterexamples verify that proof facts do not authorize observable work or external
cell assumptions. Actual source execution remains independent of engine inputs.
No new peak RSS or benchmark claim is made; those measurements remain paused.


### 共同语句观测汇总（2026-10-09，规则249）

复用RecoveryFlow的source语义适配器，发布retained父先子后ownership forest，
不是按StmtId排序。现有图事件新增当前语句owner；新的语句记录只表达正常/
abrupt/保守cleanup及有限/可能重复执行，不匹配empty-if/块布局。表达式效果、
成功定义、暂停取自共同事件。有限may-observation格逐项union，逆ownership序
bottom-up fold一次；这是标准语法所有权树属性汇总，不引入外部依赖或许可。
未新增另一套AST表达式/初始化推断。所有保留语法，包括不可达声明，均参与。

设S为语句槽数、R<=S为retained语句、N为当前flow事件。适配记录O(S+R)
存储/工作，owner增加O(N)存储。分析O(S+R+N)工作、O(S)观测与seen存储；
proposal O(S)容量、O(R)工作。打印省略子树来源遍历计入已有输出预算，不按
每个父节点重新递归分析。定义是否dead仍由独立需求分析决定，private inert
write不等于可删除。整体恢复其他分析仍按各自L/P/A等维度，不宣称整体线性。

独立小森林参考按每个节点重走祖先/后代（只用于固定4点oracle）核对全部
may-observation分量；生产只fold一次。反向StmtId/event布局、200/201/400/800
chain/star、exact/short字节与work预算验证成本。真实源码另验证调用、coercion、
TDZ、保留声明、return/throw、break、finally、私有复制清理与来源边界。
loop终止和精确handled/finally完成仍保守，不能据body为空删除无限循环。


### 完成图循环性与区域跳转汇总（2026-10-09，规则250）

标准迭代Kosaraju两次遍历当前RecoveryFlow完成图，forward/reverse CSR按实际
edge向量构造，point SCC size>1或self-edge表示可能重复。所有边种类都计入，
不按normal路径过滤。既有phi/copy Tarjan适配专用方程，本轮没有复用其语义或
新增外部依赖/许可；原语实现本地维护、以独立稠密传递闭包核对。无递归栈或
每区域图走查。设N为事件、E为所有完成边，O(N+E)工作/存储，队列/帧界限N。
CSR fill借offset当cursor，right shift恢复边界，不依赖原linked edge顺序。

语句S、retained R、owned jump J<=E。forest邻接按typed parent构造，activation
heads以normal-exit FlowPoint索引，jump query以owner索引。单次DFS给每jump
找到最近语义出口consumer；owner delta+1、consumer delta-1，bottom-up
累计后只标记尚未消费的区域。O(S+N+E+J)工作与O(S+N+J)额外存储，所有临时
数组与owned proposal计入预算；不复制每个区域的target集合、不对每jump走祖先。
全恢复的liveness/definitions成本仍按其L/P/A等维度，不声称整体线性。

2,048独立三点图覆盖512有向图、反向point和拆边，慢传递闭包核对cyclic；
78,732独立四点ownership/出口/目标/反向ID用慢祖先链核对abrupt。200/201/
400/800 chain/cycle与local/external jump链核对几何成本/exact/short预算。
missing metadata和conservative cleanup保持保守；source另外覆盖nested finite
exits、empty infinite、continue周期、effects和finally。VM timeout只是有界行为
证据，不能作为一般终止证明。数值有界循环与outer SCC内精确子区域仍未解决。


### Statement entry residual observations (rules251)

This extends the existing ownership-forest may-observation fold, without a new
external dependency or graph algorithm. The language adapter publishes mandatory
entry evaluation provenance once. Full facts retain every observation; residual
facts exclude only the queried statement's own carried evaluation, then union all
full descendant facts. Throw/resume exclusion applies to retained expression
completion only; return, local-jump differences, conservative cleanup and cycle
obligations remain. No branch-layout or event adjacency establishes provenance.

For S statements, N events, E completion edges and J owned jump queries, the existing
indexed completion fold stays O(S+N+E+J) work/storage. Residuals add O(S) storage and
one accounted O(S) seed copy; event/edge unions and both forest folds take constant
work per visited item. An O(S) owned proposal array precedes commit; no per-statement
reachability search or descendant set copying occurs. Existing cycle facts remain
lazy. The slow independent 3-node ancestor reference covers 16384 entry forests;
actual source tests cover equivalent layouts, 200/201 and geometric growth, retained
entry effects/receiver/exception/suspension and adverse semantic transformations.
These bounded work counters are not resumed process RSS or benchmark evidence.


### Retained original SSA facts and source normal domains (rules252)

Provenance: the existing verified ValueProgram/TreeFlow/operand correspondence,
ValueKinds abstract domain and the existing ECMA-262 source semantics adapter;
local C++20 implementation, no added external dependency or license. Original
operation variants are not cloned or reinterpreted. Original immutable arrays are
shared by their prepared owner; current recovery graphs borrow the input during
analysis, retaining only current source domain/continuation/reference projections.

Let O,V,U,B,E,P,A be original operation/value/use/block/edge/phase-or-exit/origin
counts, X source expressions and I checked operand occurrences (normalization may
expand an original use). Retention takes O(O+V+U+B+E+P+A+X) work/storage; collecting
inputs takes amortized O(I), then one counting scatter O(X+I) with O(X+I) scratch.
No per-read graph walk or sorted name/PC lookup. The shared_ptr control block is
charged using allocate_shared's actual rebound allocation, not a guessed overhead;
deallocation uses the standard allocator and cannot read the construction budget.
Source normal-domain transport adds O(X) storage/work per requested exact projection,
and original input ranges attach to R references in O(R). Source-only fallback has
no normal-domain array; absent facts stay conservative. Other existing graph/fact
costs remain, so this does not claim whole recovery is linear or convergence closed.

Fixtures expose real O/V/U/E dimensions and construction work/bytes. Boundary
200/201 and geometric200/400/800 copy chains exercise normal/unknown domains,
SSA renumbering, block splits and arena reversal. Exact/one-short source preparation
budgets reject insufficient work/storage. These counters are accounted complexity
evidence, not process RSS or a resumed benchmark. Full-corpus/RSS remain paused.

## Property reference stages (rules253)

The checked source adapter adds three phase points and a constant-size typed
reference per retained MemberExpr/MemberTarget. Let P be retained property
references. Construction adds O(P) work/storage to existing source-flow cost;
all edges and buffers are charged by the same budget, with geometric reuse.
Original base/key ranges attach in O(1) from the existing checked ExprId index,
not a per-property scan of operations or uses. Refinement visits events/edges
once and consumes the same key/receiver domain table. Existing shared motion
and liveness costs remain dimension-based and may depend on live facts.
Tests cover200/201/400/800 reference forests, exact/one-short budgets and reverse
source arenas. This is not a claim about total recovery convergence or peak RSS.
Provenance and version prerequisites: docs/reference/source-property-references.md.


## Shared projected normal-value domains (rules255)

Provenance: existing verified SSA parameter correspondence, existing ValueKinds
finite join lattice and ECMA-262 source normal-completion semantics. Local C++20
implementation with no new external dependency/license. It does not reinterpret
raw ISA operations or equate static definitions across dynamic activations.

Let X be source expressions, C expression dependencies, N source events, L sparse
incoming liveness facts, A predecessor edges examined for those facts, D generated
dependency edges, and V retained original values. After shared liveness, indexing
and equation construction cost O(V+X+C+N+L+A log L); a binary search resolves each
sparse fact, not a per-binding graph traversal. D is O(C+L+A+references), and the
normal-domain solve costs O(H(X+L+D)), where H is the finite ValueKinds lattice
height. Storage is O(X+N+L+D+bindings). Dense liveness still has real cost and is
admitted explicitly; no 200-item algorithm switch, dropped facts or read×definition
relation is used. Worklist membership bounds pending nodes by X+L.

Unresolved equations are seeded unknown after closure and solved again, preserving
monotone conservative results. Original operand-domain hints are not transported
through changed source writes. Parameter type proofs and actual source success
points provide facts; effects, initialization, binding identity and write deletion
remain separate. Bootstrap phase budgets include every simultaneously live output
and scratch allocation. Boundary200/201 and geometric400/800 tests, exact/short
budgets and an independent small-graph dense state oracle measure these dimensions.
This does not establish process RSS or full-pass convergence; those scopes remain
paused/open respectively.


## Checked successful-definition precision (rules256)

The existing TreeFlow proof owners publish typed original-result, incoming-phi,
parallel-cycle save, exception-stage save and catch-commit correspondence at the
actual successful source write. The ExprId-indexed table is moved into immutable
OriginalFlow; there is one allocation/owner, admitted by the original proof budget.
Recovery never infers this correspondence again from syntax or opcode sequences.

The normal-domain solver uses the same sparse graph for value dependencies and
completion dependencies. Completion nodes represent every current source event;
all completion edges propagate projection changes, including loops and exceptional
paths. They do not join a predicate's Boolean domain into a stored value. Changed
expressions, unknown/exposed cells, unsupported environments and explicit statement
projection deltas invalidate dependent raw range proofs. Omitted statements still
have linked boundary points, which carry their deltas without a current owner.
Value-only inputs without a statement-boundary index keep their source equations,
with original path contexts conservatively unproved; absence is not rejection.
A stricter original domain is consumed only at an unchanged checked success;
broad raw facts never discard precision already established by source semantics.
No raw occurrence hint is restored as a projected type certificate.

The previous dimensions V/X/C/N/L/A/D gain E completion edges and S statement
boundaries. Building/invalidating the additional context graph is O(N+E+S+D);
normal type propagation remains bounded by finite lattice height H. Memory is
O(X+N+L+D+bindings), plus O(X) retained correspondence. There is no definition x
read relation, per-definition predecessor walk, pairwise size cutoff or global
pass round. All allocations and traversals are admitted/accounted. This is a
local projection solver bound, not a proof of full-pass convergence or process RSS.

Raw numeric-update result and stored-cell value are different (notably postfix
ToNumeric lowering); no original old-result domain is assigned to the incremented
scratch store. Unmapped/native protocol roots, full original effect composition,
ordinary binding activation and declaration/lifetime candidate deltas remain
explicit precision gaps. Unknown precision falls back to current source equations,
without withdrawing otherwise valid partial source.
