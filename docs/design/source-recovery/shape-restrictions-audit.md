# 按用户要求进行的恢复条件审计

规则228补充：原“已知值必须指向原TreeLiteral表达式”的表示限制退出
读取替换路径。统一PrimitiveId指向准备阶段一次建立的typed值池；
共同表达式事实、定义合流、计划和打印器使用同一身份域。隐式词法
undefined是模型已有定义的值，不需识别空声明附近的操作序列或循环
形状。显式字面量同样经此池，数值kind/bits与UTF-16内容身份保持。

Constants仍只提交读取替换，按值比较提案变化；独立清理决定初始化/
写入/效果。池在pass执行前建立并保持不可变，分析只读；revision-owned
事实提交前整体失效。打印器只按已证明值和优先级输出，不增加AST节点
或运行语义优化，原读取range保持。TDZ、未知写入、捕获cell和源码Local
义务没有放宽。零literal输入、复制循环、条件/空值选择、效果与反例、
arena反排及200/201预算的实际证据见status。全局依赖/原IR/完整生命期
仍是缺口，不能由这个表示限制退休替代其验收。


规则227补充：Constants的单写入计数、唯一覆盖定义及本地复制队列
退出常量决策路径。共同FlowConstants按所有保留的成功定义合流值；
同值分支和有常量来源的复制环不再要求一个定义或特殊循环形状。
别名传播的唯一动态值覆盖证明仍保留，不能把它与常量值一致混同。

只有共同活跃性证明无读取的词法undefined初始定义可以排除；其他
保留写入均参与。++x的正常结果可能在没有额外GetValue时被消费，
更新必须作为未知新值，不能因无普通读取而当成自复制/旧常量。
外部可观察绑定、入口活跃值、未知/冲突/无来源复制保持未知。
这些是值传播义务，不是同块、邻接、opcode序列或图形状准入。

Constants只提交读取替换；原选中写入删除及声明退役退出此pass，
交给独立DeadDefinitions/InitialValues/Bindings在新版本事实上决定。
常量提交先触发清理再运行Aliases；后续别名/身份/效果有提交才重复
清理，已清理且未变化的版本不重复分析。依赖来自RunPass版本变化，
不靠样本大小或资源失败回退；原预算保持。
共同值分析进入revision-owned ProjectionFacts，提交前整体失效，
禁止提交中读取分析；不新增手工reset或重复语义猜测。已知undefined
缺少原literal节点时仍有打印表示缺口，未描述成语言限制。全局依赖
收敛/原IR对应/完整生命期仍开放。独立小图、布局变换、源码执行、
反例与200/201预算证据见status。


规则226补充：Constants不再从原RHS的TreeLiteral/SymbolExpr语法形状
选择值，改读共同表达式正常结果的primitive literal与binding事实。
赋值/sequence转交完成值，条件合流比较值，逻辑选择依据语言ToBoolean/
nullish；这是typed AST语义，不是操作序列或producer白名单。
同一选择同时提供复制的binding结果，后序Aliases消费前序常量提交后的
新版本事实；未增加新pass、固定轮次或逐组合生产分支。

值传播不授权删除RHS、绑定完成或异常前的undefined定义。必要条件求值、
成员setter、TDZ和catch读保持独立义务；作用域/捕获/覆盖证明未删除。
数值kind与原bits保持；同值UTF-16表项按内容一次建立身份，不按TextId
编号匹配。跨数值编码等价、算术folding与可执行边精化仍是独立精度
边界，不以布局条件掩盖。通用声明/异常/循环/复制等价测试及错误变体
验证原行为；退出的原RHS语法分支和最终证据见status。


规则225补充：RootLifetime局部查询已删除。快照目的引用、存储引用
替换、undefined完成重映射和别名传播统一消费RecoveryFlow的
BindingInScope；源声明投影范围与当前引用owner由共同入口发布一次，
不再由各优化器直接读取declaration_sites/plan/intervals拼装判断。
区间只表达词法祖先关系，不是字节码布局、同块或邻接准入。

Original表示没有额外收窄，仍需普通名字解析；Projected必须有当前
引用范围事实；Unmodeled不能授权访问。原生声明缺少范围投影时显式
标记Unmodeled，重建时清空旧事实。作用域事实随整个ProjectionFacts
在BeginCommit前失效，禁止作为跨版本缓存或正向初始化证明运输。
初始化完成/异常顺序、动态捕获cell及原名字解析义务未删除。
LegacyConfined未扩展；完整生命期/unwind、原IR对应及全局pass收敛
仍是能力缺口。独立父链对照、范围外反例、arena反排、200/201及
几何规模、重建清空与独立源码执行的实际证据见status。


规则224补充：InitialValues独立消费当前需求/效果，裁剪死初值时保留
必要RHS和正常完成后的词法初始化。绑定删除、存储合并、声明移动
分别消费显式完成与求值；不把值无需求当作删除生命期的证明。
声明候选/放置的原DeclarationStmt.initializer判断退出生产决策路径，
改读共同FlowBinding的初始化kind及已恢复声明身份。投影原生声明在
初值裁剪后仍优先于旧placeholder，不能由定义arena顺序推断身份。
共享undefined写回的词法解析/生命期证明同样枚举当前定义位置，
替代仅检查原声明slot的路径；通用义务保持，没有增加第二道布局准入。
值/复制及快照提案持有当前初始化身份，移除绑定时保留该位置独立RHS，
并保留此前pass已留下的DeclarationValue求值；删除声明不授权删效果。
恢复旧的无条件省略行为后，初始化裁剪/声明移动/返回快照组合的独立
执行对照能发现第一调用被删除，修复不靠图形状或组合生产分支。

最终循环声明语法移至值/绑定清理之后，消费已提交计划，显式清除
无剩余声明的旧suffix；被移除的声明若有必要RHS仍保留求值。
原生for语法容器仍是打印表示边界，不是语义准入。未加样本/名称、
操作序列、同块、邻接、固定图或次数匹配。初始化支配、真实值需求、
异常顺序、名字解析及RootLifetime义务保持。旧legacy捕获/完整生命期、
原IR对应及全局pass收敛仍为能力缺口，没有新增局部模式掩盖它们。
布局变换、TDZ/调用删除反例、独立执行及既定record证据见status。

规则215补充：显式绑定引用可证明的快照不再以整个环境身份不同作为
第二道准入。共同摘要区分已建模引用与尚需保留的隐式环境依赖；当前
读写按目的scope批量解析，同时保留投影声明生命期。全局/闭包等依赖
未精化时沿用原证明，属于能力缺口；不把AST分类当作ISA操作白名单。
V1资源可观察合同的环境激活资源事件尚未进入流图，继续保留原环境
边界；语言合同的放宽不改写该旧合同。

规则214补充：共享引用图记录当前owner，保留site的原始来源身份。
范围及存储合并的词法检查消费目的owner，替代快照之后的原ExprId位置
查询；所属语句只是发生位置，不是同块准入。初始化/干涉/名字解析/
声明生命期证明保持，完整异常与捕获生命期仍是能力缺口。

规则213补充：非逃逸声明/别名根不再消费原始全部引用的范围统计，
改用共同当前引用集合计算词法范围；无效果读取、死写入删除后自然
退出约束。真实旧值读取、异常观察及未建模签名仍受原证明约束。
捕获 LegacyConfined 保留且不扩展；共同范围查询限现有快照移动前
消费者，未声称原始所有权能够描述任意移动后的位置。

规则212补充：非逃逸声明候选退出原始首次赋值定位，改从当前定义、
归一化语句根和共同支配前序选择，并核验全部保留读写。声明语法容器
沿用现有整语句/原生for支持，不把任意嵌套写入拆成新声明，这是已知
表示缺口，不增加同块/编号/操作序列准入。实际声明位置独立于原始
扫描记录发布，后续生命周期查询和清理使用同一位置状态；捕获仍
保留原legacy候选，完整共同绑定生命期尚未完成。

规则211补充：非逃逸绑定的声明初始化已移除“第一次语法观察必须
是候选赋值”的准入。独立效果清理之后，使用当前 RecoveryFlow 的
共同支配关系证明候选正常写入先于每个保留读取/写入；原 RHS 和异常
路径不移动，词法范围仍独立证明。正常写入不等于可删除私有写入，
该变换保留值写入，分别消费正常完成事实与完成顺序。捕获单元尚未
完整进入共同观察模型，原首次观察检查只为该未迁移范围保留且未扩展。
候选位置扫描和完整生命周期仍是能力缺口，不宣称一般声明选择完成。

规则171补充：移除直接FunctionExpr/ClassExpr快照的一律保留；普通
有序转发证明不变，命名预检改为实际目的ownership的一次遍历。replacement
代表GetValue，不能凭目的变量/属性拼写给移来的函数新增name；原assignment
等内部命名目的仍保留。严格资源合同暂保留直接creation，是尚未证明新增
命名包装分配可忽略的覆盖限制，不是function/class不能跨块的语言不变量。
无额外名字/同块/指令/样本条件，class-self证明沿规则170的实际投影继续。

规则170补充：消除“canonical表达式归属就是最终打印归属”的隐含假设。
class-self作用域证明消费共同快照/store/alias/literal投影：被移走的RHS
在实际读取目的下检查，省略store不再重复遍历，alias检查最终绑定。
这是一般词法遮蔽义务，不限制global、参数、copy数量或heritage形状；
没有新增同块/邻接/固定序列条件，也不关闭快照转发。独立canonical
词法分析仍可用，printer统一选择projected分析。每个owner规划一次、
索引与遍历受共同预算约束；不按class候选逐次重放子树。完整函数/类
NamedEvaluation的转发仍待实际目的建模，本次只修复新增class-self环境。

规则169补充：statement快照转发依据首次完成、唯一实际读取、控制/
异常/迭代activation、读取及效果观察序和词法环境签名。相同环境允许
跨空块，已消失的交错copy声明不构成虚假屏障；新native declaration
的作用域仍纳入签名。源Local/捕获、额外读取、控制转移、TDZ遮蔽和
catch旧值按同一语义分析保留，不以same-block或固定编译布局判定。
直接function/class RHS保留是NamedEvaluation目的尚未投影的覆盖缺口，
不是操作码准入名单；不宣称一般非干扰环境交换已证明。phase branch
的throw义务未删除。正反arena/200/201/复制/空块及真实源码验证见status。


规则168补充：重复写入消除证明的是结果身份与无hook的可写PutValue，
不是`store/update/store`模式。共同表达式后序事实通过sequence和路径
join组合，原RHS的TDZ/异常/转换/暂停仍执行；无同块、相邻、opcode、
固定层数或样本条件。capture/module/self缺少此处可写性证明时保留，
明确属于实现覆盖缺口。20种组合/两种绑定/反向arena/六规模与独立
行为及真实循环表示已验证；一般跨语句值搬移与SSA合并不在本次范围。


规则166补充：normal branch缺少throw dispatch是语义义务，但不能因此
保留已经证明纯的比较后缀。第一遍只提出forest；包含effect的控制root
由共同拒绝分区整体重排一次，throwing producer恢复快照，pure suffix
重入条件。最终独立验证仍检查branch child无effect、效果顺序、唯一
use与catch副本，未将一个compiler序列作为准入。既有PhaseCorridors
路径覆盖保持；不宣称一般effectful condition可跨控制搬移。三profile/
复制/寄存器布局/长纯wrapper及handler/effect/coercion/multiple-use
对照与实际源码行为见status.md。生产不读取任何测试中的AST变异规则。

规则165补充：取消分阶段表达式消费者对SourceEvaluation::kSuspending的
一律拒绝。await/yield的operand在暂停前求值，可消费共同effect suffix；
暂停本身仍不是可搬移的Expression。仍由共同唯一def-use、availability、
效果区间拼接、最终root catch/异常快照证明准入，不加样本/同块/邻接条件。
该变更不扩大既有PhaseCorridors路径覆盖，也不声称一般跨分支/循环暂停
搬移已支持；分支、额外观察、效果间隔和catch差异继续保留实际快照。
48个两种暂停/六规模/四边界forest覆盖200/201及线性work；独立verifier
拒绝伪造跨effect/catch/multiple-use融合和移动暂停节点。真实ABC输出
与暂停/恢复行为证据见status.md规则165。

规则164补充：源Local不再仅因非CompilerTemporary而保留空声明。
共同首次完成/全部观察/词法包含证明恢复let initializer，真实let/名字/
存储保留，capture创建计入观察且不推断子函数只读。逃逸cell额外需要
动态环境身份；原ScopeId相同是当前共同模型的充分证明，保护外层循环
共享cell与本来每轮创建的cell。不同scope的activation等价尚缺证明，
登记为实现覆盖域，不描述为语言不变量或CFG同块约束。nonescaping
Local可跨嵌套scope恢复，不加邻接/PC/register/opcode/样本选择。
module alias保留外部重入观察；私有常量/复制删除仍只处理私有存储。
一个creation可有任意受资源预算约束的capture边，观察容量按实际边计。
早期/RHS capture、child mutation、loop共享/每轮cell、finally旧值、
async/generator、正反arena及200/201规模与实际record证据见status.md。

规则163补充：原完成census的postorder与typed expression/operator
穷尽解释汇总无hook/throw/write/allocation/suspension的完成证明。
已初始化读取用共同声明先后、lexical/execution包含或入口body语义；
mutable并不阻止无hook读取。未知global/member/coercion和TDZ保持，
不增加same-block、opcode/producer名单、寄存器/PC、copy数量或样本
条件。保守first-observation包含仍是精确支配证明的实现缺口，不称
为语言必要条件，不拒绝整函数。comma纯left投影后保留right原发生点、
parent grammar和GetValue类别，typeof/receiver/NamedEvaluation保持。
匿名function/class在命名目的下保留值包裹是语法语义义务，不是字节码
形状准入；原range映射到normalized result，transport原检查不改。
一般默认表达式被既有结构检查拒绝，测试记录为负例，不以删除准入
取得覆盖。十九组规模/arena变体、独立behavior/mutation和原decoder
共同验证。一般dead statement/right pruning及更多初始化精度仍开放。

规则162补充：表达式child ownership以语言求值顺序扫描，RHS完成后
记录identifier store；typed statement/short-circuit/choice程序的执行
区间和first-completion/全部观察证明供共同constant/copy图使用。不以
独立ExpressionStmt、生产者/opcode名单、同块/邻接、PC/register或
copy数量作为恢复准入。原声明/效果/RHS完成/NamedEvaluation保持。
嵌入纯copy替换为原RHS结果，该RHS仍是实际读取，必须参与同一root
名称解析；被完全省略语句的RHS才可消费原conditional-retention豁免。
条件store后的区间外读取、earlier/self读取、未知capture/module写、
handler/loop退出保留原snapshot。执行区间的保守包含不称为语言必要
条件；跨区间更精细reaching definitions和纯comma省略是实现缺口。
普通lexical block不创建执行屏障。RootLifetime使用共同原生声明区间
和读取的lexical/projection owner，证明替换后仍处于实际打印scope。
特别是prospective for let比canonical symbol scope短，loop外复制
必须保留；这是打印后可用性义务，不是同块/声明邻接的额外准入。
旧规则161直接store表示缺口已由共同完成分析替代，没有扩展局部模式。

规则161补充：声明RHS完成（或无initializer let初始化undefined）成为
共同初始化事件；原生声明先于全部观察、无owned或未知外部写且观察
位于声明activation时，可供私有复制共用。共用Confined词法包含证明
和TreeScopeCheck身份解析，目标继续用原first-store/观察分析；没有
同块/邻接、producer名单、固定寄存器/PC或复制数匹配。真实let/const/
scope/RHS不更改，不扩大live module或未知capture域；初始化的效果、
NamedEvaluation、分配与异常/await/yield位置次数保持。声明前/自读
TDZ、后续写、capture、退出观察和循环每次进入均走相同生产模型。
先于全部观察是现有保守可用性证明范围，不称为语言必要条件；更精细
支配/SSA活跃期仍有缺口，不能靠图形匹配扩大，原函数和复制继续输出。
空声明后实际单写的source存储也复用FirstStored及Confined；保留原声明/
store而共享其初始化后的观察生命周期。拒绝把已省略constant store当
作存储根是在保护存储存在性，不追加layout准入。条件store未支配外部
观察时保留复制，是当前共同可用性证明的保守域；不会拒绝整个函数。
直接ExpressionStmt投影尚未贯通嵌在实参中的AssignmentExpr，是表示
缺口；不得用成员写入/调用等producer名单扩展准入。实际成员写入
解码用例消费共同evaluator产生的早期快照，生产没有新增field特例。

规则160补充：入口参数、handler catch、函数self以typed BindingKind
初始化语义进入既有不可变复制图；scope/first-store/完整观察及共同
词法解析继续证明目标可用性、身份、次数和生命周期，不追加同块、
producer名单、PC/register或链长准入。Strictness/CallableKind/参数
默认值和rest共同判定mapped arguments的语言写通道，非严格simple
普通函数保留旧值。这是语言义务，不能以编译器布局或零本地写替代。
Local/Capture/Module不从零写获得隐式初始化。canonical capture目前
只表示Local/Capture cell，原调用槽Parameter不是已支持capture域；
有效family测试使用提升的cell，不放宽绑定规则让无效树通过。native
arguments源码写探针仅属测试层，不声称字节码恢复完成。source重写、
真实遮蔽、catch退出/finally、条件首写和子函数修改cell保持旧值存储。

规则159补充：不可变复制共享一个保留producer的存储，消费共同首次
store/完整观察/词法包含/capture证明，不按producer/opcode、PC/register、
同块/邻接或链长准入。所有分配、效果与暂停保留原执行点和执行次数。
共同TreeScopeCheck批量解析实际引用的ScopeId/根SymbolId，保护真实
绑定身份；内层同名遮蔽和兄弟同名用同一接口处理，没有全函数唯一名
或固定深度条件，也不更名绕过义务。被删除copy的RHS不是实际读取，
只有该目标必须保留时，遮蔽才沿依赖链保留必要源存储。每边/节点一次，
正反symbol/scope/AST、200/201/400/800链及深作用域共用生产路径。
当前保守解析canonical名称；被删除同名绑定造成的额外保留、参数/
catch/self隐式初始化和真实source/capture/module不可变性是表示缺口，
不称为语言不变量，不能以零本地写入或样本布局代替可用性/外部写证明。
可变phi/SSA干涉与每轮capture未完成，外部/退出/finally旧值保持。

规则158补充：原始值常量传播消费共同首次完成store、全部观察词法包含
和capture/escape证明，不在证明后追加同块/邻接/寄存器/PC/producer列表
或复制链长度条件。单写是值不可变义务；TreeLiteral是已检查的类型化
原始值域，SymbolExpr是复制依赖，不按指令名字白名单承认表达式。
有界SymbolId依赖worklist支持任意数量和arena顺序；真实Local、可变/
捕获/未知值、对象分配和调用/getter保持存储与身份，不盲目删除检查。
字面量使用实际读取的parent grammar和ExprId，loop逗号/空前缀由实际
非省略项决定，原boundary/anchor保持。数值/构造契约不改变。长原始
字符串的输出增长、一般alias/SSA干涉和原变量归属仍是表示/质量缺口；
不能用新样本布局匹配填补。有效旧实现先RED，独立JS旧值、handler、
await/yield、member receiver及分配身份反例均用同一生产方案验证。

规则157补充：私有loop进入绑定复用首次完成store和全部观察包含证明，
候选for生命周期由已有LoopStmt/进入程序owner记录，退出后/handler/
finally旧值和capture保持。ForDeclaration的可声明后缀是语言语法投影，
不是固定数量、same-block、producer/opcode或编译器布局准入；多声明、
跨透明block、arena重编号和outer labeled continue均使用共同分析。
前序效果保留原进入顺序/handler，原label仍归loop，不把效果塞进RHS
改变NamedEvaluation。compound root/非声明交错尚未投影到声明列表，
明确保持表达式表示，不称为语言不变量或为此拒绝整个函数。

规则156补充：共同LoopConditionSyntax.body已明确继续执行而保持语句的
原发生点，printer的LoopBody按此打印。规则155错误把它保守包含在条件
表达式程序中，现结束该外层投影context并恢复native词法归属，再应用
体内自己的loop/nullish投影。该LoopStmt/StmtId身份是语法发生点的归属
义务，不是新增block/arm/PC/register布局条件，不重新识别compiler图形。
全部旧值/RHS/外层/for更新观察及capture/module义务保持，不消除SSA。
有效树先在初始化范围断言RED，实际解码循环不要求固定retained/following
body形状；三profile、分配/COPY/split、正反AST及实际record另验。

规则155补充：规则154的“空声明和初始化必须同一native容器”是保守的
表示范围，现由共同作用域证明替代，不再作为额外条件。一次迭代DFS
记录每个词法容器的子树区间，一次ownership census记录SymbolId全部
读取/写入容器的上下界；初始化的直接赋值完成是首次观察，且所有观察
均在该子树内，才缩短不可捕获私有存储的语法生命周期。RHS旧值读取、
外层/兄弟handler或finally观察、module/class/closure escape及未知区域
继续保留，不将它们当成布局条件。没有新恢复拒绝、局部CFG模式、固定
深度/读写数量或sample/name匹配。嵌套初始化/loop重复进入和反例用
同一实现；一般SSA投影、用户原变量和每轮capture仍未完成。
scope census还使用既有loop初始化/条件/更新和statement-nullish的打印
归属。被投影到for更新表达式的读取属于loop owner的scope；不能用
canonical体内包含关系证明其可见性。普通多写绑定的反例在禁用打印
scope映射的有效probe中RED，正常实现保留外层存储；这是词法可见性
义务，经共同投影owner证明，不是新root/same-block/序列准入条件。

规则154补充：私有结果初始化声明由canonical AST词法归属、直接语句的
顺序支配、全ownership读写census和真实capture/module escape决定。
目标的首次观察为赋值完成；RHS/提前读取、嵌套先写和后续update分别
保留原表示或mutable let。只改声明语法，不移动操作/合并SSA存储。
同一native声明容器是可见性义务，不是CFG同块/编译器布局限制；
真实三profile decoded COPY/split和相反AST arena编号均通过。
嵌套调用保存、loop/nullish表达式程序、cell/formal、capture/per-iteration
环境与未知区域是保留的表示缺口，不能通过更多局部模式扩展。本次没有
source/sample/名字、PC/register、数量、邻接或producer/opcode白名单。

规则153补充：规则152记录的retained callee失败已追到条件参数transport
的额外root条件。普通phase evaluator已按storage lifetime内联首次phi
读取，OperandSelections仍拒绝非root ValueCopy，导致PhaseCallReference
不能取得归属。现移除root/inline准入限制；纯语义、原输入/result/
canonical使用及共同Prefix/Scope、receiver和异常义务保持，不增加
sample/name、PC/register、局部序列、same-block或复制数量匹配。

既有三个通用调用测试先RED，实际10构造器record和Push普通callback
再通过，证明不限于最初暴露问题的应用。CallPlan仍要求实际AST拥有
成员Reference，未把暂存裸callee错误打印成成员或新增.call/Reflect
前提。其他不能取得owned表达式的retained Reference限制仍登记为
表示缺口；不因本次修复声称全部SSA transport/引用恢复已完成。

规则152补充：数值更新只能复用同一producer的实际私有结果snapshot。
这是存储身份/不可观察暂存义务，按typed producer/ValueId和新鲜树的
publication重建证明；不按same-block、PC/register、名字、样本、操作
序列或固定selection数量判断。原input/phi/cell/其他producer保持独立。
共同operand可用性证明结果不能提前读取，capture census排除转换中观察，
实际写入census排除额外修改。ToNumeric外层publication仍返回旧数值。

新增构造器配对验收同时暴露call_plan.cc的既有retained Reference缺口：
MemberCall仅接受inline/nested property，而实际callee是跨region的根。
关闭本次storage复用仍同样失败，已定位普通arrow函数而非super。
该表示条件不能声称语言不变量；不能直接删除后打印错误receiver/重复
getter，后续须以共同Reference/value/effect/exception模型替换。详情及
21/22实测结果见status.md；没有将失败测试改成忽略或INCOMPLETE成功。

规则151补充：初始化根由共同normal完成代数、statement/expression
ownership和SymbolId写入索引选择，不增加same-block、opcode序列、
PC/register、生产名字、样本、邻接或固定根数量的条件。loop条件读取
且loop内写入的绑定用于表示选择；它不是语言要求，也不把仅读取的
边界值准备强行放入for。候选定义索引保留重复写入和全部中间效果，
不假称reaching definition，不合并storage或改变执行次数。

进入程序支持跨任意透明block；原表达式、来源、scope和handler不变。
控制相关进入、声明/绑定、handler和label边界的保守限制是当前表示
缺口，不是VM不变量；TDZ反例直接验证不能移到声明初始化之后。
只新增表达式initializer，不推断for let/每轮环境，未恢复更新保持
body原位。144真实三profile COPY/register/split/handler变体与156个
arena/200/201/801深块/800依赖符号、只读gate、scope/handler/TDZ/
await/yield及请求反例定向验证，事实见status。阶段一完整风格仍开放。

规则150补充：共同completion摘要区分owned break/continue；继续程序由
条件实际读取的绑定、符号定义依赖和原操作序形成，不依据++/opcode/
寄存器/PC、生产名字、样本路径、局部序列或固定guard/COPY数量选择。
全部continue目标在所选程序中的覆盖证明执行次数；break/return/throw
保留body。覆盖计数没有固定阈值，是各个原transfer身份的完整覆盖，
不在另一份完成路径证明之后新增布局条件。

只跨无绑定/label的语言block，Expr/Block/If按同一完成代数组合；原
scope/TDZ、handler及每轮环境保护保持。Try/声明、一般分离继续程序和
member alias状态更新仍是实现表示边界，不能叫VM禁止for。原初始化
不移动，canonical AST及source identity不改变；不能按当前PasteBoard
最后一个循环新增branch/update图形匹配。three-profile COPY/register/
split、arena重排、条件update、部分写入throw、await/yield和scope/handler
反例的定向证据见status.md。风格仍有初始化、phi及每轮绑定缺口。

规则149补充：首次ValueCopy只因输入是SSA ValueId就保留snapshot，是
表示限制，不能在共同availability/unique-use/no-write/effect/exception
事实成立之后另作准入。现在用EvaluationLiveness的typed存储lifetime替代
这一律限制，phase planner/verifier共享原PhiCopies epoch与效果/handler
证明；首次读取在exact alias图保留，不以alias搬运回放producer。

raw CatchDefinition只具有handler scope，当前phase层的移读scope证据
未齐，继续materialize并明确为保守表示缺口。旧unique-fallthrough
corridor/one-op phase block限制未扩展，也不能宣称语言禁止跨该布局。
不存在新样本名、PC/register、opcode序列、same-block、相邻、producer
名单或固定COPY数条件。72个真实三profile register/copy/split/handler
变体与独立原throw/参数返回identity反例验证本次边界；native for和
一般跨branch/scope的读取仍沿共同模型推进，不盲删语义校验。


规则148补充：loop一直输出while(true)是共同syntax表示限制。新增
LoopSyntax依据已验证树的正常完成/owned break目标作组合摘要，不按
opcode、register/PC、名字、same-block、相邻、producer或固定guard数量
分派。Block/If是所有源码的语言所有权结构，不是某个编译器CFG形状；
任意表达式前缀/嵌套decision和透明块复用同一完成代数。continuing arm
保留原block/scope，不新增body图匹配或copy深度分支。

绑定/label的检查保护真实scope、TDZ、每轮环境及transfer目标。现guard
表达式不能表示声明或try/return/continue完成，是明确的保守表示边界；
不把它描述成语言/VM不变量，也不直接删除原identity、effect/exception/
value-flow证明。退出前effect同样保留，不能用纯break外观丢弃它。36个
三profile实际解码copy/register/split变体、arena重排、复杂退出、raw throw、
finally、await/yield及TDZ反例有独立定向证据；for/SSA binding仍未完成。

规则147补充：旧member path在中间CallExpr处不传播，是后缀链的表示限制。
共同postfix path现在沿原MemberExpr.base／CallExpr.callee依赖自底向上
保留同一首个reference/access，成员／值调用与调用返回值可继续组合。
不新增操作名单、同块、邻接、序列、producer集合、寄存器／PC、selection
数量或样本分派；原三域guard／形成／TDZ／identity／effect／异常义务保持。

CallReference仍区分member receiver与detached value invocation。把成员
值调用改成native member chain会错误恢复receiver，故该表示继续原条件，
不是盲目删校验；调用返回值的值调用则没有Reference，可原生继续。首个
optional eval必须间接；后续链接不得加?.掩盖异常或加括号结束短路。
任意节点顺序、200/201/402、三profile真实解码／201复制拆块及独立
getter/参数/receiver/异常/eval反例证据见status.md。不同reference及未融合
formation/alias仍是表示缺口，不作为完整函数的新拒绝条件。

规则145补充：nullish投影要求terminal member的直接base是稳定reference，
是复合member spine的表示缺口，不是语言约束。现在共同MemberExpr.base／
CallExpr.callee依赖调度自底向上保存首个access及SymbolId／This owner，
一次摘要即可投影任意长度的原member chain。named／computed keys与参数
保留原ownership，未增加producer名单、操作序列、selection数量、同块、
邻接、寄存器／PC、graph shape或样本分派。原三域guard／形成次序／TDZ／
identity／effects／exception证明不变。printer只在首个access加入?.，后续
member mandatory，并保留真实callee receiver与原节点range；中途加?.或
括号的错误mutation必须被检出。逆序arena暴露的callee依赖缺失由共同
工作队列修正，不能按depth或arena顺序特判。分离形成、一般alias transport
和跨reference整体chain仍是表示缺口，不当作完整函数恢复的拒绝条件；
详细200/201/402、三profile decoded及实际record证据见status.md。

规则143补充：移除TreeGlobals的整函数局部名称union准入，接入原共同
structure遍历的实际active lexical scope。global、realm、Symbol和arguments
工厂读取共用spelling索引；不按兄弟块数量、布局、声明邻接或名字名单
选择恢复。全局不被真实destination/local binding遮蔽仍是语义义务；当前
scope的TDZ声明仍拒绝。损坏scope/ownership不能通过无冲突名字取得许可。
0/1/200/201/402/804个兄弟块与前后读取、有效body遮蔽和损坏AST已有独立
测试，实际decoded global/family回归保留。固定语料未新增complete；旧G2
来源映射切片的括号oracle失败与暂缓状态见status，不伪装为通过。


规则142补充：原可写class初始化一律打印let/class表达式是表示限制。现在
原共同ClassNames通过lexical scope、实际symbol/module/capture target与
heritage遍历证明新增self scope不会改变真实引用后，printer支持原生class
声明。runtime name与目的绑定一致是必要的命名语义边界；kLet/kConst区分
防止扩大外层写入许可，不是按类名或源码匹配恢复。constructor/member/
initializer完整性仍由原分析负责，未知body继续采用表达式和明确partial。
没有新增同块、邻接、producer名单、固定数量、PC/register或图形状条件。
直接initializer是声明语法的适用上下文；未融合的创建时点snapshot/alias
仍是共同值流的表示限制，复制/拆块变体继续恢复并通过独立行为对照。

原ClassExpr/DeclarationStmt和来源ID保持，class body/super/accessor等复用
同一printer；default expression、const binding和真实可写外层类引用保留。
实际解码的base/derived、复制/拆块、TDZ/namespace、mutable outer storage、
动态父类、原异常及错误self scope/hoisting/名称/参数变体的证据见status。
未把消除let carrier等同完整fields/globals/signatures/body结构gate。

规则141补充：精确Boolean曾只由当前producer/literal的局部类型识别，原
Boolean/null refinement不能区分null，COPY与phi后的布尔事实随之丢失。
现在由共同ValueKinds/sparse值流保留Boolean，混合null/内部值继续弱化；
源码只查询事实，不增加copy深度、同块、相邻或分支数量准入。结果事实
不能删除getter/效果/异常/执行次数证明，也不能认证未证明的输入环。

SelectionOrigin曾只允许logical使用CombinedOrigin，透明Boolean转换与
conditional组合时被拒绝。这是表示限制，不是语言义务；现统一交由原
来源correspondence核对成员与unique owner。该校验另区分原scalar predicate
执行表达式和已由新鲜TreeFlow证明的selection contextual branch标注：
context只能属于那个selection，不能计作原predicate覆盖。三profile的
复合条件/Boolean-null/COPY/split/SCC与实际错误origin、arm、getter次数
反例验收；生产不按pasteboard、方法名或错误文本处理这个缺口。

规则140补充：直接FunctionExpr／ClassExpr曾因缺少default NamedEvaluation
表示而整体拒绝，本次由共同typed命名上下文及expression grammar取代该
限制。owned函数body与明确runtime-name证据属于身份／可观察名称义务；
缺失证据不是空name。已有slot／scope／lifetime／partial／捕获检查和class
bodies／名称作用域证明保留。与default名称相等是必要文本语义边界，不是
函数名选择恢复算法；其他名称使用原envelope，严格V1额外分配合同不放宽。
不增加opcode序列、PC/register、同块、图形、producer或固定selection准入。
括号保留原创建时机及class内部scope，错误hoisting／外层binding变体独立
检出。三profile直接解码及原模块split/register/loop回归、200/201/400
family规模和语义反例见status.md；private class alias与一般绑定写入／TDZ
投影仍需共同证明，不能按元数据类名或一次store盲目替换。

规则139补充：RecordTree移除后续arm/loop module assignment要求初次声明
位于物理block 0的限制。共同ControlPlan仍拥有native声明位置；每slot保存
其真实首次声明OpId，需要时只运行一次当前ValueProgram的VerifyValueFlow。
同块operation order／跨块dominator证明原声明已完成，source tree-flow仍核对
具体scope、module target和store来源。没有额外入口块、邻接、opcode序列、
固定register/PC、producer/count或样本条件；也不通过移除scope/TDZ、身份、
effects/exception证明来扩大覆盖。条件首次初始化和此前write仍属于未贯通
的native declaration/lifetime表示，不能误称语言不允许；catch需要自己的
successful-Commit证明。本次三profile实际汇合、loop、register重分配及
1/200/201/400/800 split的54 ESM/57 mutation/6 refusal覆盖边界，完整record
独立验收与阶段一仍需各自证据，详见status.md。

规则138补充：SourceTree 移除“所有 formal 都有 debug 名称才保留任意名称”
的表示限制。ParameterOffsets / RecoveredParameters 逐 ABI ordinal 使用 typed
absence，真实原始 SSA/Before/Commit/Catch、copy/phi/SCC 仍证明名字关联；
不按默认 prologue、寄存器/PC 布局、同块或样本补名字。单 ordinal 的范围
冲突、非法名字及跨 formal duplicate 只撤销对应候选，不能后续复活；读取/
file-change/resource 错误保持。legacy 全集合限制仍隔离在它的 text adapter。
实际 source formal/可写 cell/global/module/capture 身份保持独立，旧共同
NameReservations/enclosure 避免新增名字遮住真实 descendant callee。24 个
新增 decoded family case 加不利 shadow mutation、ordinal 缺失/拆块/混合
phi/200/201/402/804及真实 NavPageMap 方法行为覆盖这些边界；不扩大为
所有 source signature/type 或 UI/platform 语义完成。

2026-10-06 文件名边界补充（结构输出，非新增方法恢复规则）：contextual
source-path planner移除entry==package、entry不得含slash及ASCII-only三项
表示限制。现由既有ABC SourceIdentity/PackageSource adapter、全文件排序
namespace规划和共同路径编码负责；入口、包名和版本分别保持文本身份，
Unicode scalar按UTF-8可逆转义。多入口/版本数量只决定输出目录区分，不
参与方法语义准入、producer/selection匹配或样本分派。实际文件名证据
唯一性、成员范围/owner、read/resource失败、路径长度和碰撞义务保留。
不支持的filename wire format/suffix/relative spelling继续descriptor fallback，
明确属于表示缺口；不把source identity当作字段、类型或函数正文的证据。
record顺序反转、真实字符串读取、Unicode/percent、200/201/400/800及不利
namespace碰撞覆盖这些边界；固定五ABC六模式的所有源码/位置映射内容
按entity逐一不变。细节和复杂度见[record-structure.md](../record-structure.md)。

规则135补充：ValueRestArguments / ValueFrameFunction 的结果复用真实 native
rest/self binding，仍保留原 operation/evaluation/origin。指令 adapter 定义
原生值身份，TreeOperands 独立检查实际 formal/self；没有追加同块、邻接、
固定 producer/selection 数量、图形或样本条件。TreeFlow 的一次 source
assignment/update census 证明复用 binding 未被写入；captures 仍由共同
cell ownership 指向独立可写存储，不把 source spelling 当作存储身份。
ABI 保留原 rest 物理标量不可读、一次分配、source write 为零；移除恰好
一次 source reference 的旧表示限制。printer 与来源传输共用 initialized
parameter/self 纯读取省略：只允许实际 ExpressionStmt 的两个节点处于
同一 owner 内零宽区间，不放宽 TDZ/cell/module/global/getter/phi/exception
存储检查。普通根、nested eager snapshot、跨块、分支/COPY/leading formal
及插入 getter 回归证明布局变化不需新分支；错误 rest/self 和 formal write
仍拒绝。没有扩大既有 predicate snapshot 等 legacy fast path 的布局限制。

规则134补充：captured参数名仅由真实allocated cell元数据提示及共同
ValueIdentities的ParameterEntry身份关联；copy/phi/SCC和block split共用
同一分析，不匹配first-store、immutable、固定寄存器/PC、同块或样本。
名字冲突仅保留合成拼写，不拒绝恢复；cell提示不证明原formal源码名。
NameId intern只代表文本，formal和cell保持不同binding；共同tree使用
已有formal排序索引避免同名，原storage/capture/TDZ、reference identity、
free-name shadow、effect/exception义务保持。incoming cell的另一activation
归属来自共同CellPlan，不把图布局当作归属证据。ordinary/phased、copy、
等源/混合phi、copy cycle、later write及200/201增长覆盖这些边界。

规则133补充：真实debug formal的命名adapter显式区分legacy writer与
共同SourceTree域。已有source identifier规则允许$及旧私有prefix名字，
不把writer namespace当成语言不变量；reserved/非法名字仍拒绝。真实
entry来源、scope/ABI、duplicates与资源义务保持，descendant global/module
冲突使用既有共同引用命名规划。80个新增真实metadata fixture覆盖
straight/split等价变化及不利名字/真实shadow；不增加opcode、PC/register、
body producer、同块或样本匹配。legacy默认限制保留在隔离adapter，
完整签名及Unicode等表示边界仍是工程覆盖缺口。

规则132补充：family在命名前一次准备共同syntax/module aliases，以live
ValueGlobalRead和实际module alias建立name/owner排序索引。真实incoming
父子边与preorder occurrence区间限定查询；metadata局部/formal偏好及
合成名字都避开当前子树的引用，不能用同名提示遮蔽原global/module。
这个callable子树粒度是保守命名选择，不是输出准入；实际block/creation
scope、capture、TDZ、effect/exception与write校验仍由原共同分析证明。
不增加retry次数、同块、producer、寄存器/PC或样本形状条件。tree的
标识符校验不再套用legacy `$`/ClassValue/MethodSelf私有namespace拒绝；
语法与真实shadow继续检查。legacy限制仍隔离在旧writer/旧命名adapter，
不宣称整个工程已移除。外部wrapper/seeded record root保留独立scope义务。


规则131补充：参数debug名不再要求entry MOV prefix或固定五条captured-this
前导。实际range start观察复用original CFG/dispatch、typed语义和RegisterSsa；
entry来源经透明copy/phi一致性建立，Before/Commit/Catch异常时间保留。
未知语义/原始suspension只沿可达控制/handler边传播未知frame事实，不能
给下游值授予身份；不把后段未知当成入口name的全函数布局限制。
命名提示的上游优先级由实际def-use SCC建立，不用PC/register/copy距离；
独立root冲突仍保留合成名，单一copy cycle不会成为自己的冲突ancestor。
没有样本分派、producer名单、同块或局部指令序列；未知后的观察与不完整
formal debug集合仍是表示覆盖缺口，不能作为语言不变量。

规则130补充：普通/分阶段callable的syntax adapter接入既有scope literal
命名器，提示始终按真实allocation/slot绑定；cell ownership、capture lifetime、
初始化、family free-name visibility由原共同分析证明。没有新增opcode序列、
同块、register/PC、producer名单或样本分派。reserved或当前name arena冲突
保留合成名；普通family的跨代free-name shadow仍由原enclosure拒绝，其
generated-name retry尚未接入，是表示缺口，不能描述成语言不变量。
元数据局部存储名不证明formal名，也不证明const或own class field语义。

规则127补充：optional call 复用既有 nullish 域解释、reference formation
及原 CallExpr.reference。成员调用保留实际 receiver，值调用保留存储的
callable 和间接 eval，参数/key 仍属于原 call。准入说明的是语言短路、
结果及 reference 身份义务，没有新增 opcode/PC/register、固定 guard
数量、同块、名字或样本匹配。条件之外的调用继续执行，不被扩大短路
范围；不同 base、缺少 null/undefined 证明或有 fallback 效果时保留
原 conditional，不拒绝完整反编译。既有 effect/exception/flow/origin
验证保留。更长链是表示覆盖缺口，不能作为语言限制。
用户要求当前修复结束后，先解决实际 INCOMPLETE/UNRECOVERED 的语义
支持缺口，后续 optional-chain 表示优化后移；不恢复周期审计/RSS。

规则126补充：未融合的闭合条件族现在在原分支写入phi binding一次，
后续任意观察保留该绑定，不把single eager use当成所有条件表达式的准入
规则。单使用点融合的execution/catch检查保持独立。原位置candidate
共用root-use、EffectOrder、真实phi输入及external observation义务；
根predicate snapshot原位保留，child prefix留在选中的原控制臂。
整个族先绑定真实source control，再授予prefix归属；bottom-up及parent
闭合检查去掉缺失child/root的候选。不会因try边界绑定失败提前吞掉getter。
独立ExpressionScopes使用原try layout核对ordered phi/prefix的实际AST
protected/handler范围。原位置带pre-throw copies的prefix仍缺少staged
表达式表示，不能把这个限制描述成catch语言规则。
Phi输入索引复用已经检查的flat alias，而不是把STA/MOV当成另一次观察；
真实UseId、可用性、全路径必达与异常图保持。字符串和其他生产者共用
同一描述及证明，没有字符串、PC/register、样本或固定深度分派。
多次观察已覆盖闭合条件族；general bundles/reconvergent tails/constructor
及其他未完成表示仍开放。新能力没有恢复暂停的周期审计或RSS测试。

规则125补充：phased nested-arm不再要求原phi只有两个输入。共同正常
CFG后序路径索引保留原EdgeId，使用已有SelectionChild/partial SelectionRegion
表达共享phi的子树；原叶边及child各有唯一表达式父归属，完整root才能
消费原phi。child没有伪造input或中间phi。共同root-use/EffectOrder/catch
检查全部子树成功后才发布group执行归属；失败时保留原始语句和异常范围。
原inline/module roots统一使用functional forest，不再用OpId大小推断父。
TreeBuilder从control adapter取得真实Commit分支，不用空phase的最后操作。
identity predicate的combined origin按已经验证的条件节点核对，来源成员
独立验证保持。没有固定嵌套数量、同块、指令序列或样本分派。
此路径仍使用现有单表达式phi edge bundle；多次观察的phi materialization、
共享reconvergent tail、general staged arrays及旧member/joint corridor仍为
表示缺口，不将这些限制描述成语言不变量或以新局部模板扩大旧fast path。

规则124补充：嵌套phased selection的实际syntax consumer与execution owner
分别保留。inner phi经纯comparison进入outer decision时，不再要求纯
comparison具有throw dispatch；实际catch签名来自共同inline root或已经
检查成功的外层selection。按实际consumer及operand order分组，以source
root依赖从外向内检查，各group/依赖只处理一次。未通过的parent、cycle
不会授予owner；这表示表达式放置证明尚未闭合，不是新的布局不变量。
原phi/transport、arm执行、receiver、effects/exception staging和最终
interval义务继续由各共同分析拥有。fresh verifier独立重算execution，
不从plan字段自行授予成功；对象字面量复用已有forest、对象归属及printer。
无optional-chain字节码模板、opcode序列、PC/register布局、producer名单
或固定selection数量条件。原candidate discovery/member prefix/joint suffix
仍有corridor表示缺口，不因本项移除必要异常检查或宣称全部条件均支持。

规则123补充：direct phased条件的prefix改由真实root-use方程与共同
EffectOrder证明。reverse-use Kahn求解要求所有外部consumer归属同一
terminal owner，允许owner内部多次使用；callee仍由Reference拥有。
pure execution anchor只把执行点交给共同前驱/必达分析，不伪造效果。
完整原CFG Commit支配关系、真实catch签名和独立重算仍保留；条件前缀
成功后不追加single-entry corridor、same-block、相邻、opcode序列或
producer名单。原forest的sequence/snapshot保留getter一次读取及raw
VM predicate，不通过重复getter改善外观。register aliases/copies/拆块、
200/201/402和getter后return/纯循环/debugger反例覆盖执行次数及顺序。
pure-only snapshot保持原绑定位置，不为搬进条件额外生成赋值序列；其
后续绑定消除仍是缺口。suspension/constructor-frame等协议仍由共同
DescribeEvaluation的eager expression能力区分，不靠opcode producer名单。
nested selection predicate、member prefix及joint suffix仍有旧表示缺口，
不把这些能力限制解释成语言不变量，也不以新的局部匹配扩大旧fast path。

规则122补充：已获得共同表达式/条件归属的phased字段RHS，现在通过
functional source forest及投影EffectOrder组合整个对象初始化。原
LiteralPlan仍证明身份、完整own定义和无提前观察；原allocation root/
snapshot保留。共同全路径前驱/必达链成功后，不增加同块、相邻、局部
opcode序列、producer名单或selection数量条件。完整CFG支配关系证明
external leaf在allocation可用，未归属的pure late snapshot仍在原位；
原始catch签名、执行次数及receiver义务保留。fresh verifier重建融合前
的消费者并独立重算对象能力，复用共同ObjectExpr/TreeFlow，无新writer。
三profile、register alias/copy/拆块、200/201/402、barrier/late leaf与
ownership篡改，以及83个独立重复成员调用行为覆盖正反例。
effectful predicate prefix、general staged array及旧nested/corridor仍是
实现缺口，不能描述成语言不变量；固定五ABC输出字节本轮未改变。

规则121补充：phased LiteralPath原未消费共同EffectOrder，已在分配处
可用的RHS仍受直线fallthrough/corridor限制。现共同分析接受normal/phased
SSA，效果方程仅沿正常边，成功Commit可用性由原始完整图支配关系证明；
catch entry独立开始效果历史，真实Return不被合成Commit末端遮蔽。
对象/数组共同复用exact predecessor与must-reach链，证明成立后不追加
同块、邻接或corridor准入。绕过、getter、可能不终止的纯循环及catch读取
部分对象仍不能获得融合能力；原def-use、异常与catch-copy义务保留。
late/conditional RHS、nesting corridor及空纯分支打印仍是实现缺口，
不能将本轮交付称为全部phased对象或源码风格验收完成。

规则120补充：phased selection原限定成员调用argument，普通eager消费方
即使共同控制/唯一使用/效果/异常义务成立也只保留phi snapshot。现共用
PhaseSelectionOperand，typed owner区分直接消费及真实成员Reference；
消费能力来自DescribeEvaluation，保留NeedsSnapshot、receiver及predicate
边界。原join-to-consumer逐选择Inert遍历改用既有normal-interval效果索引，
O(1)查询，不按同块、adjacency、样本或选择数量准入。复制/拆块及中间getter
反例通过实际字节码和独立JS检查；已有candidate/corridor发现限制与producer
能力名单仍是实现缺口，整个staged-to-native object还未完成。

规则119补充：异常表达式调度原要求消费方有result并属于Expression
操作子集，普通字段写入因而不能承接共同区间已证明的RHS。现消费能力
取自DescribeEvaluation的eager输入与source能力；Reference/key边界、
predicate snapshot、唯一canonical使用、效果后缀及catch copies仍检查。
native literal RHS保留其allocation区间归属，staged定义是语句根，
不再一律固定其RHS。builder/fresh verifier共同重建这一区别。
resultless consumer是最终执行owner，缺少匹配catch证据时立即保留快照，
避免一次失败外层融合改变已合法的内部Reference归属。此证据缺口不作为
语言禁止语句内嵌表达式的理由。现存producer能力名单、phase corridor
及整个staged-to-native重建仍开放，不靠更多局部布局扩大它们。

规则118补充：普通selected object的ObjectRoute在共同Domain已证明
分配/字段/消费的效果区间后，仍额外要求allocation.block等于首个条件
header。现删除这项重复布局准入，沿用已有直线前缀及fresh Verify方程。
对象使用统计、依赖producer和forest receiver共同解析已验证的alias
身份；复制不算额外观察，但复制后的实际逃逸/额外消费仍进入统一统计。
三profile真实解码的1/2/200/201/402复制拆块规格恢复完整嵌套对象表达式；
带同样前缀的debugger barrier/共享对象反例仍拒绝融合，Node独立行为
对照共5,600场景通过。没有增加样本、寄存器、PC或指令序列匹配。
phased staged-to-native、非直线前缀，以及ObjectRoute现存物理字段顺序/
join候选限制仍是开放的实现缺口，不把它们描述为语言不变量。

最新替换状态见本页规则125及各项补充。历史候选不是当前实现的证明；
未替换项继续开放，不能将局部交付称为全量审计闭合。

规则114补充：ClassSelfOrder的初始化观察证明原仅扫描String/Copy，
这是封闭表达式分析的实现缺口，不是语言不允许复合字段初始化。现由
共同ClosedValues使用source evaluation capability、真实效果/异常、
语言输入和exact copy/phi SCC身份，稀疏传播上下文无关值；不追加
producer名单、同块、相邻、selection数量或源码/样本匹配。同一身份
phi可以传递，多个不同选择值不能凭臂内纯净获得同一封闭结果；参数
类型不等于闭合，frame/lexical/global/heap/捕获仍有真实观察义务。
ClosedClassFields保留共同EffectOrder的单次字段链和真实receiver/
DefineField/owner/self-cell证明，控制角色复用共同shape。原normal CFG
证书的phased缺口及有真实效果/异常RHS的表达式观察证明仍未闭合。
实际生成的复杂字段使用原FunctionTree/ClassStaticFields；36份三profile
类源码、540独立行为、反向/拆块/复制/身份phi/重复输入及反例已验证。

共同typeof printer原给全部操作数加逗号。现只为GlobalNameExpr保留
强制取值，防止未解析名称异常被typeof吞掉；符号TDZ和其他值表达式
沿用原语义及优先级。此分派依赖语言AST reference类型，不涉及原指令
或CFG形状。固定五输入的既有typeof表达式简化，无完整record降级；
独立配对静态类/Content局部检查不等于全部record或阶段一完成。

规则113补充：普通source evaluation原假定输入已经简化，将原始
ValueCopy按表达式合并来源，破坏首次保存赋值的计算owner。这是模型
缺口，不是语言对复制数量、块布局或指令相邻性的要求。普通与异常
路径现复用既有copy/phi SCC身份事实、canonical observation census、
PhiCopyUse投影及原始anchor；共同Use构建器与保存区间查同一身份。
计算与传输不再争用一个赋值来源，snapshot/operand/origin校验保持。
只有实际身份等价方程允许投影，多个锚点、无锚点、scope与内部值义务
继续保留。原始/简化、三profile、长复制链、寄存器交换、拆块、效果
barrier及真实汇合的完整树/行为验证，另有身份/consumer/target/来源
与phi projection篡改反例。原来的同参数phi测试另保留为空schedule，
真实并行复制测试改用不同参数/receiver身份；不把强制生成临时搬运
作为语义要求。未改默认数值合同，未宣称全量限制审计或阶段一完成。

规则112补充：普通conditional恢复原先重复限定ToBoolean/NOT和严格
相等，而共同BooleanEvaluation已经拥有全部比较结果事实。现用共同
Boolean结果证明VM false/zero分支与语言truthiness相符，不按比较名单
定义源码条件表达式，也不把任意Number视为Boolean。既有def-use、
selection arm归属、效果/异常和来源验证保持。

普通logical Domain原遗漏header之前的直线执行块；纯control transfer
又被保守OrderedEvaluation计为一个事件，导致等价拆块后callee仍是
跨region临时量。现一次索引普通控制边的直线执行关系，将该前缀纳入
同一Build/fresh Verify效果方程；纯control anchor依据共同shape和原
无效果/无异常事实不推进语言事件时钟。原来取得的callee、receiver及
条件参数形成一个原生Reference调用；CallPlan检查保持，未使用.call。
单incoming/单ordinary successor是直线执行次数的充分证明，不是语言
不变量或额外Reference准入条件。一般非线性前缀、跨控制literal和
历史开放项仍是实现覆盖缺口，不能把本次称为全部图形限制消除。

已初始化cell guard原在快照census中抢占首次使用，又在selected
subtree中成为额外producer限制。现两处复用IsInitializedCellGuard，
fresh verification检查实际初始化/read/revision/owner和原anchor；
不为guard添加指令模式，不将cell读取视为纯操作。三个profile、十种
比较、复制、1至4块拆分、取反、cell快照及200/201/402真实完整树和
独立Node行为均验证，consumer/初始化事实篡改拒绝；实际FollowView
先ABC生成再读sidecar，注册回调6,426场景通过。固定五输入无完整
记录降级，四份已有源码的条件/快照表示变化逐份检查；完整record、
构造器/UI及阶段一全体验收仍未完成。

规则111补充：普通LiteralPath原未接收共同CellEvaluation，因而把一个
已证明初始化的lexical read后的guard当作额外消费者和效果，拒绝合法
的对象初始化。普通构建及fresh验证现显式传入同一cell事实，每次独立
核验revision、实际环境/写入/读取和初始化owner；use与效果区间的
两处census复用IsInitializedCellGuard，不增加ValueGuard生产白名单、
opcode序列、相邻操作、样本、寄存器或PC匹配。缺少或伪造证明仍拒绝，
Language值域本身不能授权移除guard。捕获owner的条件义务仍须家庭
分析闭合；这份初始化证明不保证闭包不可写。

解码回归包括三profile、复制、对象分配前纯块拆分、嵌套对象及2/200/
201/402规模，实际树保持原生ObjectExpr。getter的次数、receiver、
顺序、原异常、payload身份和新对象/属性描述符独立对照。旧异常fixture
将kPhiInput误解为纯producer的断言在修改前HEAD也失败；现由既有
fresh树验证及带getter/异常的Node对照检查原执行位置，不删除效果校验。
异常LiteralPath的既有语义保持。普通字面量初始化跨控制流的完整泛化、
剩余FollowView成员调用、raw-copy来源及历史开放项仍待共同模型恢复，
不能把本次贯通称为所有布局限制或阶段一验收完成。

规则110补充：实际共享条件遇到已经证明初始化的cell guard仍受
EmitDecision自己的inline/module-guard名单拦截。predicate及normal
linear prefix现复用同一EmitBlock，不追加cell-guard特例；语句根和
环境声明使用原共同staging。fresh flow先由IndexPlacements证明
实际cell/receiver/class/hole等anchor语义，再验证其原位置，不增加
第二个producer准入。初始化事实、anchor位置/来源篡改均拒绝，copy/
拆块/反向比较/条件内局部作用域无需新匹配。原snapshot来源校验未
放宽；五个实际tree失败、input-only omission及历史开放项仍待恢复。
此项是已定位重复条件的替换，不宣称全量审计或阶段一完成。

规则109补充：语言算术是显式SourceContract前提，经共同typed数值规则
贯通normal/phased SSA和linear分析，不以样本、数值碰巧安全、opcode
序列、PC/寄存器、同块或producer名单选择恢复。默认解释器表示前提
保持，语言输入、receiver、效果/异常、scope和来源义务仍由原分析负责。
普通二元表达式/copy/块拆分及复合优先级使用相同树和printer，无第二套
布局准入。遇到raw copy transport的snapshot来源模型缺口时没有删除
origin校验；两次局部放宽已全部撤回，后续必须共同区分计算来源与传输
wrapper来源。类成员部分输出目前只传播input gap，不能传播tree gap，
新模式的7个typed tree失败因此暂时没有部分源码；这两项是实现缺口，
不能描述为语言禁止，也不能计作阶段一或全量条件审计完成。

规则108补充：普通 ControlDecision 路径中的语句根现进入共同 FunctionTree
语句 staging，以块内私有条件变量及同层 if 保留执行条件和顺序，不再
把“必须是 inline expression”作为这一表示的要求。选择依据是共同
EvaluationKind::kRoot，不按 debugger 或其他 opcode 生产名单、样本、
PC、固定 guard 数量、寄存器或同块。共享/最终 fallback 保持原 owner。
独立 AST 检查承担保存身份、极性、私有访问及语句顺序的真实义务；
copy/拆块/反向比较无需新生产分支。原异常相位的 statement-only 前缀、
catch staging、一般语义输入失败与历史未替换项仍开放。本项不是全量
审计或阶段一验收闭合，规则106所述普通前缀的12个拒绝用例已替换为
完整源码及行为验证，不能将原 typed 拒绝记录继续当作完成证据。

规则107补充：既有nullish抽象解释新增AssignmentExpr/SequenceExpr的
语言转移规则及按路径的有序reference formation。typed SymbolTarget
写入与赋值结果是同一RHS值；用原ExprId保留整个assignment/RHS，不按
局部assignment形状、getter/call名单、两个guard、寄存器或同块选择。
每个可能路径的形成 occurrence及其先前读取必须相容；不能提前到旧值
读取之前、丢失另一个赋值或把条件形成改为必执行。这些检查承担实际
执行/身份义务。不同guard数量、反向arena、copy/块拆分及任意RHS组合
不需要新的生产分支；已证明SSA/效果/来源不再额外受布局条件拦截。
当前native base引用一个保留的原ExprId，分离的多次形成效果暂未合并；
这是表示能力缺口，原条件源码仍完整输出，不作为语言禁止或整函数拒绝。
不同副作用fallback occurrence、一般alias、optional call/整链和历史
未替换项继续开放；此次不宣称全量条件审计或阶段一验收闭合。

规则106补充：已proved retained SSA的fallback改由共同四域方程解释
strict null/undefined比较、NOT、truthiness及shared-arm条件合并，原
saving UseId/assignment/phi/effect/exception与instruction origins保留。
没有根据赋值语法、getter producer种类、两个guard、寄存器或块布局选择。
TreeOperands原unary/copy/branch名单在fresh语义证明之后重复拦截binary
比较，现删除该额外producer条件；保存身份、逻辑运算和边选择检查保持，
三项实际树篡改仍拒绝。decoded copy/拆块/反向比较/Boolean中间结果与
完整HotListItemView配对分别验证，未借配对源码生成。
条件间debugger暴露既有statement-only决策前缀kOwnership缺口，12例
没有完整源码，不计为行为成功；没有删除barrier或为它新增布局匹配。
其他alias、optional链/调用和历史未替换控制条件继续开放。

规则105补充：PlanNullishSyntax共用三个值域及typed结果身份恢复??，
不要求固定guard数量、producer列表、CFG同块或literal池编号。fallback
保留原AST子树，调用/getter/sequence仅在nullish路径执行。重复UTF-16
literal内容以一次排序规范化，false/0及正负零不混同。不同binding的
谓词不能因两个分支同值或最终truth已知就消失：TDZ读取仍可能抛出。
新增两项TDZ反例在实现中先复现错误投影后修正；独立行为测试要求保留
异常。首次effectful读取/赋值前缀的共同快照与alias还未实现，保留展开式；
此缺口不作为语言不变量，不用新增局部assignment/opcode匹配扩大支持。

规则104补充：原生optional member使用null/undefined/other域的共同
抽象解释，按SymbolId或ThisExpr.owner关联原稳定读取。允许严格比较、
NOT、AND/OR及条件组合，不要求固定guard数量、顺序、CFG块或表达式
arena顺序。成员base/key与外部call/member边界保留；纯条件证明永不
执行的opaque arm，来源事件沿完整AST所有权记录，不扩展字节码匹配。
opaque效果分支不能仅凭长得像optional而删除；不同binding、缺少null
路径、非undefined fallback和truthiness均不能替代nullish语义。
1/200/201/402组合、反向arena、计算键/异常/外部value-call与member
分别验证，实际CommentItem配对通过。尚未实现原生optional call叶子、
effectful base快照/alias和整链压缩；完整family仍核对子函数owner发布，
不因为不可达就伪造子函数完成。这些实现缺口不作为语言不变量宣称。

规则103补充：构造器尾部receiver完成复用共同ImplicitTailCompletion，
按typed callable、ThisExpr.owner和真实AST body位置选择原生完成表示。
不增加ABC的同块、PC、寄存器、样本或opcode序列准入。实际body尾部
是安全语法投影的边界；嵌套try中的this读取可能抛出并进入catch，不能
盲删。普通函数receiver返回、提前返回、别名/属性/compound/call均保留；
更广的冗余完成消除仍需控制及异常归属证明，未宣称本次已实现。
24组语法fixture/8,640行为场景与实际HomeModel配对分别验证；语法fixture
不冒充原ABC的完整语义发布。其他历史恢复形状限制仍待共同模型替换。

2026-10-03，基线 87176c10。此为本次明确请求的审计，不恢复定期审视。
目标是查出把编译器局部形状当作恢复准入条件的实现，并规定后续通用替换方式。
本次覆盖 src/include/tools 的样本身份文本搜索，并人工检查调用引用、条件表达式、
谓词快照、rest/派生构造器和异常类集成的条件。不是所有生产分支已逐行审计。

## 结论

本节及下表记录 2026-10-03 的历史基线；最新核对见文末
“2026-10-04：当前引用校验与扩展审计”。已替换项不能继续当作当前缺陷报告。

此次文本搜索没有发现以 pasteboard、WindowUtil、Immersive、PushMessage、
EntryAbility、Widget、hilog、JSON 或 sample 路径进行生产语义分派的实现。
仅有 fixture/sample 的说明性注释。文本搜索只能支持这一有限结论，不能证明
没有隐蔽特例。以下确实存在的是以局部布局或子集形状限制恢复能力的实现。
它们不能被称为语言语义要求，也不能继续靠追加形状匹配扩充。

## 初次审计发现（历史基线）

下表保留首次审计的发现；不是当前仍有这些条件的声明。CallPlan同块、
predicate producer/strict-comparison、单selection及computed phi Commit
邻接门槛已分别替换，当前剩余项和关闭证据见后续规则66至88。

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

## 规则 69：异常相位的保留值与输入边归属

异常选择发现现在先解析已经验证的复制别名，再使用共同保留值分类，将
无producer且无隐藏效果的分支作为原 binding 的一次读取。首次phi/catch
materialization没有被别名消除。调用前缀的使用普查核对真实phi owner和输入
edge：当前选择输入属于同一参数表达式，外部phi及较早的其他参数不能读取
尚未执行的移动前缀。

纯、不会抛出、结果为Language且只含literal输入的共同 eager表达式，现可
跨CFG分块安排到唯一原消费者。判断来自共享DescribeEvaluation与效果/值域，
不新增producer opcode白名单；有状态输入、getter及first materialization
继续保留控制/顺序义务。单selection、有限arm/copy布局等旧边界仍开放，未
扩大其局部模式列表。record5374已从failure进入incomplete，constructor缺口
与阶段一完整验收仍保留。定向副作用/错误分支值反例及独立行为对照支持本次
接受域，不能据此宣布七类审计事项全部解决。


## 规则 70：输入失败归属于实际成员函数族

部分class草稿原先只允许成员根自身的输入域缺口，defined member没有相同
表示。该边界会因失败位于不同嵌套深度、class buffer或DEFINEMETHOD安装而
丢弃其他独立成员。现在函数族reader记录请求root及实际DFS失败创建边，
共同ClassMemberOmission按真实所属成员记录缺口；没有局部序列/深度匹配。

这不是扩大完整语义接受域。只允许原输入InternalValue证明失败，省略整个
成员族；输入损坏、资源超限和后续证明失败保持拒绝。定义成员依然逐个核对
原始身份/创建/定义位置，所有缺口使class/record保持incomplete，没有假的
callback正文或内部写入/ABI/source收据。1/2/3层、三profile和200/201回归，
完整组合的访问器反例及独立方法行为对照支持这个表示接入。15个record现
可保留其他成员，未宣称数值语义、遗漏函数体或七类既有审计事项已经完成。

## 2026-10-04：当前引用校验与扩展审计

本次按用户要求复核，基线 79324555。工作区另有未完成的 SuperForward 接入；
以下不将其作为已交付能力。人工核对调用引用、normal/phased 选择、谓词快照、
rest、派生构造器、类成员与初始化器、literal 输入和 legacy 模块/数组/super
整理；另搜索 src/include/tools 的样本身份文本和块/邻接条件。未逐行审计所有
生产分支，也未运行全套、扩展语料或 RSS。搜索命中需结合用途判断；真实名称、
ISA 解码、枚举穷尽性和操作数元数校验不是样本分派或布局特例。

### 同块校验的来源和现状

历史提交 c9c8a93b（2026-09-26，调用引用与 receiver 证明）在 VerifyCalls 中
要求 property.block == call.block 且 property.order < call.order，同时要求
property 为 callee 的内联 producer。它用块内次序限制早期表达式恢复范围，
防止 getter 重读、receiver 改变和求值乱序；这不是 VM 或语言对成员调用的
限制。历史代码本身不足以证明其因某个样本而加入，不能据此推断作者动机。

766afe0c 已删除同块条件及 CrossPhaseInline/CrossLogicalInline 的例外枚举。
当前 src/source/call_plan.cc::VerifyCalls 先独立调用 VerifyEvaluationPlan 或
VerifyPhasedEvaluationPlan，再核对原 property result、callee/base/receiver
身份和实际 consumer。callee 在分支前、参数在汇合后的关系本身不再拒绝。
仍要求 property 被当前表达式内联拥有：这是现有 MemberCall 的表示边界，
不是所有合法显式 receiver 调用的定义，不能新增更多 placement 枚举来填洞。

### 当前待替换项

| 优先级 | 当前源码位置 | 观察到的实现边界 | 必须保留的义务及共同替换方向 |
|---|---|---|---|
| P1 | src/source/call_plan.cc::VerifyCalls，约215行 | callee property 只接受当前表达式的 inline/nested-literal consumer | 保留原 callee/base/key 与 receiver；用共同 Reference 和保留值表示承载共享/已存储引用，证明一次读取和调用时序 |
| P1 | src/source/phase_selection_calls.cc::Prefix；src/source/phased_selections.cc；src/source/phi_expression_inputs.cc | 规则87已移除单选择门槛，88替换Commit邻接，89投影冗余phi/copy身份；单入候选和多份真实parallel-copy RHS共同安排仍开放 | 共同Reference联合安排同一调用的全部选择；继续贯通控制依赖/区域及真实edge-copy时序，不能扩大local opcode或tail白名单 |
| P1 | src/source/predicate_snapshots.cc::Candidates，约20/38行 | 最早同块eager occurrence可接收snapshot assignment；规则82已去除strict-comparison/branch限定 | producer与consumer白名单已删；尚需跨块snapshot所有权/支配/控制依赖与效果区间，保留一次求值和所有使用身份 |
| P1 | src/analysis/rest_arguments.cc::ProveRestPrologue；src/source/rest_binding.cc::FindRestLocal | 固定起始 ABI MOV、寄存器布局及紧邻 rest/STA；旧路径仍调用 | normal ValueTree 已用 ProveRestOwnership；剩余 legacy/构造器使用必须迁移共同 frame、SSA、参数写入和动态数量证明，不能扩充 prologue 模板 |
| P1 | src/analysis/derived_constructor.cc；spread_field_constructor.cc；range_derived_constructor.cc，约38/79行 | legacy直线状态序列及copy opcode列表仍在；共同ValueTree的回边/guard存在门槛已由规则84/86移除 | 共同CFG/SSA单次调用、receiver、guard输入与cell事实已贯通；剩余多super site、捕获/异常与导入父协议不以legacy布局定义，不能假定host/import构造器类型 |
| P1 | src/source/class_definitions.cc，约83/96行；class_initializers.cc，约58/70行 | 规则74/75已替换defined-member同块条件；class initializer owner/method/call仍受同块局部次序限制 | 已定义成员复用真实HomeObject、occurrence和normal-CFG效果顺序；初始化器及phased捕获环境仍需共同证明，不能追加局部序列 |
| P2 | src/source/logical_regions.cc::FindSelectionExpressions 等 | branch/ToBoolean 邻接、有限 arm/phi 布局 | 用通用控制依赖与区域表示产生候选，typed predicate/phi 定义语义；二元分支或操作数元数本身不是缺陷 |
| P2 | src/source/class_tree.cc::CheckClassParentScope，约20行 | phased class 不接受 initializer/defined members 或父 captures/modules | 作用域/模块/创建环境接口尚未贯通；禁止把“无捕获/模块”视为 class 语言要求；部分成员缺口不等于完整环境证明 |
| P2 | src/source/literal_plan.cc::EarlyValue，约236行 | 规则81已以共同availability替换phased phi/allocation同块；working-IR导入对应、首次OwnDefinition候选仍开放 | 保留snapshot/edge-copy版本与定义/观察顺序；剩余候选要贯通共同ownership，不追加block/producer类型匹配 |
| P2 | src/source/super_arguments.cc::ProveSuperArguments，约67行 | legacy super 每个参数必须来自调用前紧邻的 load/save 对，且只支持单 super 区域 | 直接通过真实 def-use 和有序参数依赖整理，保留原参数快照、this 初始化和效果；不扩充寄存器排列或 load/save 模式 |
| P2 | src/source/array_literals.cc::FoldArrayExpressions/FoldArrayLiterals；class_assembly.cc | legacy 数组/类整理按同块连续语句与操作列表推进 | 共同 LiteralPlan/ClassTree 是迁移方向；保留新对象身份、定义/逃逸/观测、属性描述符和捕获时机，不追加 legacy 白名单 |
| P2 | src/source/module_binding.cc，约73行 | 未证明初始化的模块 load 要求下一条指令就是 Hole guard | 需用原 module binding、Hole 状态、guard 输入的 SSA 身份及控制/异常效果证明；TDZ 行为必须保留，邻接仅是旧实现边界 |

以上是恢复能力限制，不是已证明的错误放行；同块比较也可能是在实现一个已
声明为局部范围的优化，不能只凭搜索命中全部删除。特别是 literal phi 的
edge-copy、模块 TDZ、getter、super 和 class 捕获会在粗暴放宽后改变语义。
通用证明负责正确性；表示接口负责表达证明后的结果。两者已有完整证据后，
不得再加样本/块形状等第二重准入条件。AGENTS.md 已明确该规则及变形/负例
验证要求。本次没有扩展任何生产模式，也未声称这些开放项已全部修复。

### 本次验证

现有 build 二进制执行 5/5 定向测试通过（15.11秒）：logical_regions_test、
expression_snapshot_source_test、pasteboard_partial_callback_source_test、
retained_selection_source_test、forwarded_selection_source_test。日志：
/tmp/abcd-reference-shape-audit-tests.log。通用表达式快照另直接执行270组场景；
pasteboard 配对脚本覆盖240组原 callback 场景并要求 record 保持 incomplete。
验证的是已有构建的行为，没有重新构建未完成的工作区 SuperForward 修改，
也不证明所有 phased 多选择、共享引用或阶段一完整语义已覆盖。

## 规则 71：原调用帧转发采用整个函数的语义证明

前述审计时未完成的 SuperForward 现已贯通共同 IR、frame fact、evaluation、
SourceTree、ABI 与 class printer。当前 normal 路径调用 ProveImplicitConstructor，
不调用 ProveForwardingDerivedConstructor 的直线 prologue 模板。原 CFG 上的
转发次数与终止传播、共同 SSA/copy/phi 身份和物理 frame 写入分析负责证明；
不限定原始转发站点数量、寄存器布局、同块或邻接。多个分支分别转发、汇合
返回 super phi 的 fixture 经共同 FunctionTree/ABI 通过。

该证明允许将真正等价的整个函数表示为语言原生的隐式派生构造器。源类省略
显式 constructor，保留原始 IR/树来源与 typed implicit fragment。它不是将
forward 指令局部替换成 super(...args)：后者观察 iterator，前者直接转发原
调用参数。绑定/Proxy parent、动态父构造器和 newTarget 行为通过独立对照；
不得以这种转发的 IsConstructor runtime 规则放宽另一个 super spread 协议。

以下仍是未完成的共同证明范围：循环的终止、异常相位和额外可观察效果；
调用前物理参数/newTarget 写入后恢复尚未通过版本化 frame 读证明，当前拒绝；
printer 尚未承载此形式的额外捕获/模块环境。这些不是语言禁止条件，不允许
增加局部恢复写入序列、producer白名单或新的布局例外去补。range/spread 与
legacy 构造器的既有模板仍开放，前表其余项目也未全部关闭。

15/15 定向回归32.10秒通过，含寄存器复制、0/1/200/201间隔、分支转发及
跳过/重复转发、替换constructor、修改frame、效果、错误返回和回边负例。
185组构造行为及70组异步方法 paired对照通过。固定五输入从78 complete /
26 incomplete /2 failure到80/26/0，原78份源码逐字未变；此为组件交付，
不代替阶段一与 object creators 全组验收。细节见 implicit-constructors.md。

## 规则 72：语法分组不承担恢复准入

共同打印上下文已区分 member/call/new 的语言 grammar，替换旧 Member printer
按 Symbol/This/Member 列表决定括号的写法。new 的 MemberExpression 上下文
向 member base 传播，任意深度的工厂调用均在原调用处正确分组；没有新增
graph形状或producer白名单去取得恢复许可。callee、receiver和求值顺序仍
由原共同分析决定，所有被接受的 AST 都由同一个打印器处理。

值调用的 `(0, callee)` 是语言 Reference/GetValue 与 indirect-eval 表示义务，
不能因 callee 恰好是 identifier 而删除；这种语法规则与同块/邻接准入不同。
数字成员base及new目标中的call分组也是语言grammar，不能靠删括号提升外观。
14种组合、2538组行为对照及200/201长链验证支持这次表示变更；前述12项
能力审计中的开放语义/表示缺口没有因此全部关闭。

## 2026-10-04：当前提交复核与 literal 嵌套补充

复核基线 66e48615，工作区干净。再次核对历史 c9c8a93b、删除同块条件的
766afe0c 和当前 VerifyCalls：同块限制没有回归；inline-owned callee 表示
边界仍在。src/include/tools 的 pasteboard、WindowUtil、Immersive、
PushMessage、EntryAbility、Widget、abc_with_code 文本搜索未命中；这仍只
证明所搜身份文本没有被用于生产分派，不证明不存在其他隐蔽特例。

补充一类此前表格未独立列出的实现边界，使当前清单为13类：

| 优先级 | 当前源码位置 | 实现边界 | 通用替换方向与真实义务 |
|---|---|---|---|
| P1 | src/source/literal_plan.cc::LiteralExpression/LiteralUnary，约247/265行 | literal 嵌套单独列举 unary、binary、call、construct、read、closure 等可接受表达式；共同 DescribeEvaluation 已有表达式能力描述，两处能力需分别维护 | 共同接口描述 AST lowering 的输入求值次数、eager/conditional/suspension 行为和 Reference 消费方式；结合 literal 所有权、效果/异常区间、创建环境和逃逸证明决定嵌套，不再通过追加 operation 类型扩大准入 |

不能仅将这份列表替换为 DescribeEvaluation(...).expression：该属性目前不
证明一次求值、暂停语义或安全的原生表示。FalseOrZero/TrueOrNonzeroNumber
打印会重复读取 snapshot，不能把有副作用子树直接代入；await/yield 也不能
靠表达式标签忽略暂停。binary 的 use_count == 2 是两个输入操作数的元数，
不是“只允许两个使用者”的恢复限制；Language domain、closure HomeObject、
模块 Hole guard 和全局 lookup 的语义区别仍须逐项证明。

前表 class 同块项还覆盖 ProjectClassDefinitions 的第二层检查：约64行的
claim 限定 creation/definition 同块，约93/107行限定 class/prototype 的
外部观察者与最后安装同块且在其后。只改 FindClassDefinitions 的候选发现
不能修复跨块投影。共同替换必须同时证明安装的执行条件/次数、所有观察者
看见完整类、HomeObject/捕获身份与 intervening effects；不能以 block ID 或
operation ID 的数值顺序推断 dominance/lifetime。

本次仅更新审计及状态，没有扩大生产接受域或删除真实语义义务。当前现有
build 重跑5/5定向测试通过（15.10秒）：logical_regions_test、
pasteboard_partial_callback_source_test、retained_selection_source_test、
expression_snapshot_source_test、forwarded_selection_source_test。日志
/tmp/abcd-reference-shape-current-tests.log。未重新构建、未运行全套/扩展
语料/RSS；pasteboard callback 通过不等于整个 record 已完整恢复。

## 规则 73：literal 复用共同源码求值能力

前节新增的 LiteralExpression/LiteralUnary 独立operation准入列表已替换。
现有 EvaluationShape 同时描述 SourceEvaluation：直接表达式、纯值、重复
snapshot输入、暂停、调用帧协议及无源码降低能力。literal从共同能力和
输入/结果语义发现候选，原 use/effect/catch/escape/environment 证明决定
实际归属，producer种类不再作为第二份可扩充白名单。

共同类型分派是描述指令/降低语义的边界，不是移动原列表后继续逐个给
literal授权。原来遗漏的this、当前函数身份和纯binding copy自动进入该
机制；单次使用与共享使用仍不同。重复输入谓词复用 PredicateSnapshot 和
BooleanEvaluation：原存储输入可重复读，Boolean降低可单次消费子树；不能
把getter子树复制进展开式。独立verifier重查嵌套输入角色/purpose及snapshot
义务。pure copy/string的额外效果、模块TDZ、HomeObject、异常和super协议
仍需各自真实证明。没有把“表达式”标签当作暂停或运行时等价证明。

真实DEFINEFUNC owner的编码用例经共同输入/树/ABI恢复此前拒绝的嵌套对象，
有复制/NOP、1/2/200/201/402变化及效果/共享/所有权负例。272组独立行为
对照通过；24项相关定向检查通过，细节及失败修复证据见status.md。五输入
及106份源码无回退/内容变化，阶段一依然80 complete/26 incomplete。
这关闭的是第13类独立列表事项；前表12类中尚存的布局/表示限制未因此关闭，
literal的phased phi存储版本和跨块区间也没有完整贯通。

## 规则 74：已完成类成员安装的后续观察

ProjectClassDefinitions 的 class/prototype 外部观察者同块门槛已替换。
共同 VerifyValueFlow 在当前原始图上证明SSA定义支配直接使用及phi实际
输入边。现有安装全部位于创建块，正常离开该块前必已完成全部安装；
故支配的其他块使用无需再匹配布局。创建块内仍按真实operation order
证明晚于最后安装，不能把未来定义或更早逃逸当作合法观察。phi按incoming
edge的source位置读取，不按merge块或PC大小推断时序。保留外部使用的
prototype读取与身份，不隐藏已安装方法或duplicated getter。

这不是删除所有class同块条件：FindClassDefinitions的候选范围与projection
claim仍限制creation/method/installation同块，原效果区间、HomeObject、
捕获和安装次数义务未变。跨块安装需要共同全路径效果/控制证明，此项仍
开放；也未扩大class initializer和phased接口的局部模板。禁止以新增
特殊分块、寄存器或方法名称匹配代替后续共同实现。

编码fixture覆盖后续分块、phi及循环观察和1/200/201/402安装；旧代码的
分块用例拒绝，当前通过共同evaluation独立复验。144组独立原生对照核对
类/方法身份、命名、描述符、receiver、Proxy效果/任意抛出值，配对源码
不进入引擎。共享SSA证明的实际资源费用也纳入投影预算。10项不同相关
检查通过；另两项legacy失败在优先链接原实现后仍复现，完整证据见status.md。
五输入仍80 complete/26 incomplete，106份源码内容未变，未达阶段一验收。

## 规则 75：跨块类成员安装不再以同块布局准入

FindClassDefinitions 中 creation/installation、method/installation 的同块
条件及 ProjectClassDefinitions 的同块 claim 已删除。共同 EffectOrder 从
真实 normal CFG/SSA 和 typed effect/exception/environment 语义构造效果
前驱；无效果依赖 SCC 透明传播同一前驱身份。不同路径上不同的效果即使
数量一致仍形成 opaque join。没有枚举允许的分块、opcode序列、寄存器、PC
或graph形状；pure diamond/loop 自动使用同一机制。

投影要求私有 class 的所有被移入成员效果组成从原创建出发的单一前驱链，
每个前驱支配后继，唯一尾部为最后安装。外部 class/prototype 观察仍须
晚于最后安装；phi使用按真实输入边检查。HomeObject/函数身份、捕获环境、
原方法单次消费和descriptor义务未放宽。条件跳过安装、循环重复安装、
环境/debugger干预继续拒绝；不声称证明全路径终止或完整异常图。

原 bytecode/OpId 的升序只保留为索引边界，不再决定 native 成员覆盖顺序。
DefinitionOrder 取共同 CFG reverse postorder，并在上述链/支配证明成立后
用于语法排序。物理布局反转的端到端 getter/data-method 覆盖用例检查真实
执行顺序；伪造顺序由 fresh candidate 对应检查拒绝。

这关闭了第6类中的 defined-member 同块门槛，未关闭其 class initializer、
phased scope/capture 接口和活跃 accessor-result alias 缺口。前表其他开放
项也未因这次替换消失。13项相关检查、432组observer及96组capture原生
对照通过；固定五输入与106份源码无回退，完整证据与剩余gate见status.md。

## 规则 76：名称推断是语法上下文，不是恢复准入

直接 identifier declaration/assignment 的 ClassExpr 现在复用该语言位置
自带的 NamedEvaluation。实际 binding spelling 等于原 runtime class name
时可去掉计算属性包装；不一致时保留包装，不拒绝已恢复 AST，不扩大效果
移动许可。这是生成文本/名称边界上的正确性规则，不能用类似字符串比较
反过来按函数/应用名称选择生产语义。成员属性赋值、非直接 RHS和其他目的地
不会继承这个 identifier 上下文；也不凭原 class name 引入 immutable self。

实际编码首次声明、后续赋值、名称不匹配和模块绑定重赋值的63组独立对照
通过；6个 named-class-self 合法语法 mutant 改变行为，证明名字相同不足以
证明 lexical binding 对应。它改善固定输入的源码表示，未关闭其他开放准入
限制或26份不完整模块的正文缺口。


## 规则 78：父函数初始化不再限定入口块

审计后续发现 FunctionInput 的 creation-time 初始化只收集入口块 store；
这也是旧实现范围，不是语言要求。现以已有 sparse SSA 的完整 IDF、Boolean
must meet 和支配遍历证明正常 CFG 的初始化状态，并在真实 function/class
创建处保存共享持久化集合。不同分支、汇合或循环不再需要新的形状匹配。
Language store、真实 owner/创建身份、TDZ、异常和效果义务保留；早于初始化
的创建仍缺 escape/invocation 证明。正常 parent 的结果供 normal/phased
child 共同使用，phased parent 的成功 Commit 分析仍未贯通。源码首次声明
和父函数 raw read 的独立边界没有被这项替换隐藏。详见 module-reads.md。


## 规则 79：同函数读取与当前图复用共同初始化分析

SemanticValidation 的 raw local read 已不再只认 entry-block store。共用
analysis::BuildModuleInitialization 的 all-path 候选，完整输入验证再逐项
核对所有store的Language操作数，单侧写入、零次循环和Hole/read-store循环
不能自证。ModuleReadFact::kStored 的入口store backlink变为可选来源，不是
准入条件。source ModuleGuards 用同一个算法重算 CURRENT ValueProgram 的
初始化状态，并检查实际write/use/domain和原始身份/effects；删除其stored-read
current_store.block==0 / 同块order条件。工作图移除/移动store不能靠旧输入证明
放行。phased parent、guard成功Commit与首次source声明归属仍是开放项；
没有增加branch形状、producer列表或sample身份匹配。详见module-reads.md。

## 规则 81：literal 的早期输入改用实际分配点可用性

删除 `literal_plan.cc::EarlyValue` 的 phased `phi.block == allocation.block`
和 producer 块身份分派。普通及 phased 路径保留 fresh ValueFlow 产生的支配树，
在真实 allocation 的 block/order 上复用共同可用性查询，包括成功 Commit 与
phi/catch entry；查询不提供值域、效果搬移或源存储版本权限。原来的 native
interval/unique phase corridor、no-escape 和 catch-copy 证明仍独立成立。
不再因为一个已可用的 phi 来自另一块而拒绝，也不把晚于分配的 phi 当早期叶。

原始编码的 phi materialization 之前就可输出；本次 RED 来自同一编码消除
纯复制后的等价候选 IR。候选证明和原始编码到独立源码树的验证分别记录，
不能把修改过的候选直接当原始 import 证据：当前 `VerifyImportedPhases`
对这种 working-IR 编辑的对应关系尚未贯通。该边界未添加新的本地匹配例外。
首次 own-definition 跨选择的原生表示仍开放；不得用普通 Set 替换
DefineOwnProperty。细节与验证层见 literal-initialization.md。

## 规则 82：共同 eager 使用点保存与引用复核

再次核对发现 `tree_operands.cc::Receiver` 仍有第二重
`property.block==call.block` 或 CrossPhaseInline/CrossLogicalInline 枚举门槛。
`VerifyTreeFlow` 在操作数复核前已新鲜独立验证共同 normal/phased EvaluationPlan。
该额外布局门槛现已删除；实际 property result、consumer、base/receiver、私有
存储和原始使用点对应继续校验，没有 callee 重读或 `.call/.apply` 替代。

`predicate_snapshots.cc::Candidates` 的“strict comparison 内联进 branch”候选
条件已删除。共同 DescribeEvaluation 的 eager 语义、reference/repeated-input
边界和已有完整效果区间证明决定保存位置。普通函数无需有 phi 才运行该机制。
源码森林与操作数校验携带真实 UseId，覆盖 scalar/call/module store；native
logical/conditional 使用原始谓词 occurrence，原 logical-LHS 证明查询原森林。
逻辑 keep edge 必须绑定真实已验证的左值；其他已保存值仍走普通 private-slot
校验。不能因 producer 是 snapshot 就推断任意 phi 输入属于那个逻辑左值。

这不是关闭所有表达式限制。当前 use-site interval 仍为 normal 局部证明，
跨块可继续保留原始独立保存；phased 保存需要共同 Commit/throw/catch 版本
模型。单 selection 的 phased call、inline-owned callee 表示、literal 首次
own-definition、初始化器/捕获投影与剩余 legacy 构造模板仍开放。不能继续
增加 producer 白名单、selection 数量枚举、邻接或块布局例外。具体契约见
expression-snapshots.md；验证与固定五输入证据见 status.md。

## 规则 84：构造初始化不以向后 PC 或 guard 布局准入

后续核对 `range_derived_constructor.cc::ProveInitialization` 发现三项额外
布局条件：任何 branch_target<=PC 一律拒绝、至少出现一个 super guard、
super 必须支配所有 guard。这些不是初始化不变量。range/spread 现在共享
真实 normal CFG 的 successor 可达性，只有通过 call block 的循环才拒绝
该单次调用表示；其他循环和向后物理布局不要求新的恢复分支。guard 按
其实际 SSA 输入逐项证明，未初始化输入可以在 super 前检查，no-op 不
要求 super 在前；没有 guard 时也不伪造 guard 或输入事实。

正常 return 的原始支配、super 来源、current constructor、每个初始化/
未初始化 guard 的真实值、文件/owner/current-IR 对应及完整 guard coverage
均保留。重复调用负例在新的 CFG 证明处拒绝；错误 working guard 输入和
原始错误来源继续拒绝。新增遍历 O(B+E) 时间/O(B) scratch，并计费真实
初始化和边访问；不按每项 guard 重新遍历。算法及预算见 algorithms.md。

这是构造初始化证明的替换，不能当作原生构造/整类输出许可。单 super
site 接口、多路径 invocation frame、异常控制、捕获/receiver 初始化、
parent brand、spread-array 展开及 legacy 直线模板仍有独立边界。多个
互斥 super site 尚未表示；不能按数量、opcode 序列或固定图形补充模板。
旧审计表的 rest/构造模板类事项及 phased call/reference 事项未全部关闭。

720个三profile真实解码用例、1/200/201/402/804块的CFG链、预算边界与
40个原始input/current-IR投影用例通过；实际source路径的构造/捕获/Proxy/
异常与配对回调另有独立Node对照。首轮行为mutation暴露了旧文本替换未
实际改变嵌套保存赋值的问题，已让负例保留super效果而仅破坏receiver身份。
旧writer assignments失败单独记录，没有计入通过。最新验证与五输入
状态见 status.md；未进行逐行全量生产审计、full-suite、扩展语料或RSS。

## 规则 85：语法恒等变换与恢复准入分开，补充发布层门槛

当前再次核对 CallPlan 与 tree_operands::Receiver，同块比较及
CrossPhaseInline/CrossLogicalInline 例外没有回归；共同求值独立验证与实际
callee/base/receiver 对应仍在。phase_selection_calls 的 phi_arguments 和
root_selections 各为1门槛仍在，不能视为已由这些修复关闭。

新共同 printer 用 typed Value/Truthiness/Discarded 和 polarity 组合 NOT 与
BooleanLiteral 条件分支。这种源语言布尔恒等式仅改变已恢复树的表示，不
决定哪个指令、布局或样本可以恢复；条件 GetValue、getter/throw 和 nullish
左侧精确值保留。不得将这类 AST 语法变换误用为 producer/CFG 准入模板。
独立行为与来源范围验证见 operator-expressions.md、status.md。

本次补充确认另一项发布边界：prepared_family.cc 约216行的 root callable
检查，在实际 ClassContext 下仍要求显式 DerivedConstructor 的
receiver_guards 非空。规则84的共同初始化分析已不以 guard 存在与否准入，
这里不能再用 guard 数量代替初始化/receiver 生命周期证明。暂未修改这个
独立发布接口；后续需消费原 owner、实际初始化证明、完整树/ABI、捕获和
constructor 上下文，避免直接删除后让未初始化 receiver 获得发布许可。
隐式构造路径有独立协议证明，不能据其成功宣称此显式根接口已经贯通。

当前审计仍有 inline-owned Reference、phased 多selection、initializer/
capture/模块相位、首次 own-definition 和 legacy 构造/rest 等开放项。本轮
27项相关检查通过，并重新导出固定五输入；未逐行审计所有生产条件，也未
恢复暂停的全套、扩展语料、性能或RSS验证。全部审计事项尚未关闭。

## 规则 86：显式派生根不再以 guard 存在准入

前节补充的 PreparedFamily::Prepare 发布门槛已删除。真实 ClassContext
和 root placement 保留；FamilyTrees 对实际同 owner 输入先证明构造初始化
协议，再构建/验证 SourceTree、ABI、来源及全族捕获/写入，发布层不再重复
要求 receiver_guards 非空。RED 在完整构建后命中 kCallable；无 guard 的
正例经同一通路进入完整 family emission，没有另加局部序列或布局分支。

未新增“无 guard 即 initialized”的假设。原 guard 的实际输入及覆盖、cell
初始化与 descendant preservation 仍独立证明。没有可省略的 root guard 时，
可写 cell 不一定使寄存器初始化协议失败，但不能获得 immutable receiver
事实；真实 child initialized guard 无证据时继续拒绝。不得将这两种义务
合成新的“必须只读捕获”或“必须出现某种 guard”模板。

测试真实序列化的三profile owner/constructor/child，包含复制规模、分支
捕获、字面量初始化、getter和原始/current-IR变造。夹具省略 guard 后曾
遗漏必要 accumulator receiver load，现修正输入而未增加生产模式。旧负例
要求“协议全部拒绝”的假设也已分解为实际 receiver 事实/guard 省略义务。
具体构建及失败修复证据见 status.md；当前开放的 phased 多selection、
inline-owned Reference、initializer/捕获/模块相位与legacy模板仍未关闭。


## 规则 87：联合选择参数与索引祖先 Reference

删除phase_selection_calls的phi_arguments == 1和root_selections == 1
准入，不再以选择数量避免共用callee的重复扫描。实际argument UseId决定
源码参数次序，同一call联合检查从callee Commit经各selection header/join
到实际call的分段依赖；整组失败保留原快照，不能部分移走callee。prefix
只由相应decision拥有，原phi输入/transport/branch身份分别验证。普通
branch也登记decision ownership，避免把其真实Boolean输入误判为外部使用。

多条selection记录可以对应一个成员Reference，phase_references只保存
其同一实际callee-use。Build/Verify/ExpressionForest/TreeFlow仍使用原
ValueProgram与共同ConditionalExpr/MemberCall及printer，没有样本名、
固定参数个数、局部opcode串或新增placement例外。callee与branch的物理
OpId先后门槛也由真实CFG分段路径取代，outer callee与child的次序复用
引用区间；receiver/getter、原use、正常复制、catch scope/staging仍保留。

祖先Reference按实际inline consumer关系连接。预索引正常corridor的
效果归属区间和catch一致性，路径终点进入前停止，终点的其他前驱不会
新增single-entry门槛。每个祖先关系只查询一次、存储其联合引用一次，
避免多个selection/兄弟参数重扫长前缀；详见algorithms.md。三profile
129组解码正/负例覆盖1/2/3/50/100/200/201参数、前缀getter、外层成员调用、
复制变化、反向参数、debugger屏障、伪造argument/callee/缺项及配额边界；
另有44496组生成源码行为对照和9项有效mutation。

当前开放项包括MemberCall的inline-owned表示边界、单入prefix/control
candidate、PhiExpressionProducer的computed Commit邻接与literal-only
inert tail、class initializer同块、phased scope/capture/module接口以及
legacy rest/super/array/class/module模板。初次双getter arm夹具因现有
computed arm后跟pure jump限制没有产生候选，已记录为缺口，没有追加
opcode/tail匹配。确认为有实现准入限制，不据此断言其历史作者因样本加入。
禁止继续用样本/布局匹配扩大这些路径；真实语义校验继续保留。具体新鲜
构建/测试/固定五输入证据见status.md，审计仍非全仓生产分支逐行关闭。


## 规则 88：phi 表达式以全路径效果证明替代 Commit 邻接

删除PhiExpressionProducer的computed producer必须紧邻原phi输入边及
仅primitive literal可跨inert tail的限制。IndexPhiExpressionInputs先一次
普查实际UseId观察，再从原normal copy边反向求必须到达的同一EdgeId。
普通与phased共用此索引；正常块查询producer自有literal/guard区间结束
后的尾部，相位图查询实际Commit。checked module guard和native literal
内部定义仍属于同一表达式，不能被当作额外观察或样本模板。

所有正常分支必须到达同一原phi边；返回/外来效果/环境改变/中间copy或
无限循环使证明失败。单次原观察、原throw staging和最终TreeFlow的原
protected/handler scope核对保留。typed EvaluationShape记录normal control
意义，路径分析不按opcode串、producer名单、前驱个数或图形模板准入。
索引是线性工作/空间，不逐参数或逐phi重走尾部，见algorithms.md。

216组三profile实际解码用例及88992组完整生成源码对照通过，覆盖两侧
getter、多参数、prefix/outer Reference、throw/noncallable及18项有效
mutation。另有normal图纯branch/merge和1/200/201/402块尾部，divergence、
早退、debugger、额外观察与中间phi-copy拒绝。固定五输入无complete回退，
真实VoIP方法的一处双分支getter改成ConditionalExpr，独立配对服务300组
行为对照通过；不是HarmonyOS主机运行验收，阶段一仍未完成。

本轮诊断还确认：true arm插入冗余MOV自身会新增一个真实SSA phi输入，
现有IndexPhiExpressionInputs及FindPhasedPhiSelections仍以bundle.count /
schedule.steps为1准入。此例也被初始夹具触发，不能把它的拒绝全归因于
Commit邻接；夹具移除这个独立变化后双getter路径通过。多parallel-copy
RHS需要按实际值身份/存活/复制顺序共同安排，不能删除count门槛就放行。
该缺口、单入candidate/prefix、inline-owned Reference、class initializer
同块、phased scope/capture/module接口及legacy模板仍开放。禁止用更多
样本/opcode/寄存器/PC/数量匹配修补；不得断言已完成全仓逐分支审计。


## 规则 89：冗余 phi/copy 不再充当正常边屏障

arm 中 MOV 自身会产生额外 SSA phi，原 bundle.count == 1 把实际值不变也
当成多份写入；这属于表示/分析缺口，不能解释成成员调用的语言限制。
共同 AnalyzeValueIdentities 复用迭代 Tarjan，按原 PhiDefinition/纯 ValueCopy
方程证明同一外部 ValueId 锚点，不看寄存器、PC、块布局或 producer 名单。
多个不同外部值、无外部锚点、域不相容不会折叠；catch binding 和真正的
mutable phi 首次快照仍独立。原 phased SSA、全部输入 UseId 和 catch staging
保留，PhiCopies 独立复算身份并验证原输入全集，只投影 executable schedule。

正常边屏障及唯一 expression_input 改用实际保留的 schedule；共同 canonical
use census 保留穿过身份 phi/copy 的最终观察，不把搬运计为额外求值，也不
遗漏共享 getter/receiver。BindingPlan、选择恢复、Reference corridor 和树
flow 消费同一已验证投影。删除的是冗余身份造成的 raw-count 限制；多份真正
不同值的 parallel-copy RHS 顺序仍未贯通，不能宣称单份 RHS 边界全部关闭。

旧 phased-selection-refusal 测试假定普通 conditional module-property 参数
必拒绝；现在它经过完整 draft、树语义/flow 和原 guard/staging 验证生成
原生 MemberCall，测试改为正例。独立 Node 检查 getter 替换、receiver、模块
绑定变化、throw/catch、非 callable、TDZ 与未取分支；不经 .call/.apply。
补测直接 module 值作为选择参数仍命中 kReference，保留为 phased module
参数/guard 接口缺口；没有新增生产 direct/property 分支或放宽 TDZ 义务。

435组三profile解码矩阵覆盖 base/callee arm 自复制、getter/prefix/outer
Reference、多参数及1/2/200/201/402复制变化。不同 receiver、debugger、
反向参数及变造 alias/projection 拒绝仍测；完整生成源码另有177984组行为
对照和36项有效 mutation。构建、最终模块测试及固定五输入证据见 status.md。
inline-owned Reference、single-entry candidate/prefix、class initializer
同块、phased scope/capture/module 和 legacy 模板仍未全部替换。本次沿既有
审计发现推进共同实现，不声称已经逐行关闭全仓全部恢复条件。


## 规则 90：模块读取不再按 producer 类型强制保留为 root

补测直接模块值作为conditional参数时，IndexPhiExpressionInputs已经由
共同原UseId、原边和all-path尾部证明可以表达；但builder/verifier又为
所有phased ValueModuleRead设置pinned。此前inline模块读取另有类型例外，
phi输入却被第二重准入拒绝。这是早期实现范围限制，不是TDZ/模块语言要求。

删除全局module pin及对应inline例外，共同观察/效果/原边/Reference证明
消费这一producer。没有增加direct/property或placement名单。模块绑定、
原guard输入、原Before/Commit/catch对应、catch slot来源与最终树中的guard/
read共同表达式仍独立验证。原始SSA与staging不改写，不猜initialized状态。
多观察、额外debugger效果和不同catch状态不因pin删除而获得移动权限。

三profile72组实际模块参数正例及18组额外效果拒绝通过；9252组native
行为对照包含模块绑定变化、getter替换、receiver、noncallable、caught
completion、TDZ与未取分支。phased and/or目前保留高层ConditionalExpr，
原生logical表示仍是风格缺口。固定五输入无complete回退且源码字节不变。
具体新鲜构建和28项相关验证见status.md；这关闭直接模块选择参数的pin
缺口，没有关闭模块guard邻接、scope/capture、多真实copy安排、单入口
candidate、initializer同块或legacy模板，也未做全仓逐生产分支审计。


## 规则 91：原生 logical 是共同真值/保存使用的表示投影

此次改进不增加CFG识别模式。既有SelectionRegion仍拥有原两臂、phi边及
异常/control通路；共同真值方程只证明其保留分支返回的正是被测试SSA值。
inline consumer森林、纯效果、完整predicate极性和canonical身份分别证明
可省略的Boolean计算，没有opcode串、同块、固定深度或条件数量准入。
raw VM zero/nonzero对null/undefined/NaN不同于JS truthiness，不能贸然替换。
有外部观察的Boolean结果仍materialize，不因为&&/||表示而省略其观察。

首次保存也是语义义务。首轮实现从slot取值漏掉saving use，固定五输入
抓到DetailPage complete回退。现在原UseId沿方程保留，源树消费原保存
赋值，原phi边读取其已求值结果；scope、Reference、TDZ及异常staging
没有因表示更简洁而放宽。120组三profile解码、10162组模块行为、1390组
实际输出相关方法的配对对照与固定五输入支持此交付，具体证据见status.md。

当前再次核对的开放限制如下；不能用其数量/布局条件代替语言不变量：

| 位置 | 当前实现边界 | 后续共同替换 |
|---|---|---|
| call_plan.cc:215 | MemberCall的property仍要求inline-owned或已有nested-literal接口 | 真正保存的Reference及有序参数安排，保留callee/base/key/receiver身份，不重读getter |
| phased_selections.cc:25/63/129 | 唯一实际copy RHS、arm/condition单入口corridor | 同一依赖/控制/效果模型安排全部真实RHS并证明区域归属，不枚举数量或前驱形状 |
| class_initializers.cc / class_initializer_projection.cc | 同块/order/OpId门槛与效果数量比较已由规则92替换；当前ClassTree仍只有一个initializer接口 | 多次真实初始化区段须由共同顺序/owner模型表达，不能增加count模板 |
| module_guards.cc:79 | guarded read仍要求block/order/OpId邻接 | 以原guard事实、精确输入及相位/TDZ对应证明所属读取，保留原抛出顺序 |
| class_tree.cc::CheckClassParentScope | phased parent接口仍排除initializer、defined-member、captures和modules | 贯通真实创建scope/捕获/module/异常对应，不能直接删掉父作用域义务 |
| class_self.cc::FindClassSelfStores | 同块/order和效果数量已由规则93的共同EffectOrder身份/支配/必达替换；实际唯一首次store及初始化器正文接口范围仍有限 | 多次写入与一般初始化器须由共同cell/观察/执行顺序模型表达，不能增加数量模板 |
| semantic_locals.cc / scope_facts.cc | 规则93以真实拓扑排序替换物理forward-edge准入；既有一般cell初始化仍依赖原指令排序，循环保留局部路径 | activation-sensitive初始化数据流证明每次分配到读取的全路径store；不能以PC大小或同块替代 |
| literal_class_fields.cc::Build | 规则95替换单块/无边和物理return次序；规则96以SSA定义memo替换String/copy物理扫描；literal RHS与无参数接口仍有限 | 一般RHS依赖、phi/条件字段、phased执行/this与cell观察证明，不能继续扩大局部列表 |
| literal_plan.h::IsReceiverField；tree_structure.cc | 规则94共用实际ReceiverEntry与已证明初始化FrameThis的来源接口；删除直接ThisExpr结构门槛，保留原读与跨块绑定身份 | 多次/保留frame读取的native字段投影仍需共同观察/顺序证明；不将frame read等同ABI快照，不推断derived/arrow初始化 |
| legacy rest/super/array/class/module整理 | 既有直线/局部序列模板未全部迁移 | typed CFG/SSA/协议状态和共同FunctionTree通路；不继续扩大模板集合 |

CallPlan同块及CrossPhaseInline/CrossLogicalInline例外没有回归，模块producer
pin、selection数量及computed Commit邻接门槛已经替换。上表的边界仍开放，
这不是全仓每个生产分支逐行审计闭合，也不能推断原作者因某个样本加入。
禁止追加样本/名字/布局匹配的规则继续有效。真实身份、effect、scope、TDZ、
异常和资源校验继续有效，不能把“避免特例匹配”理解为删除语义证明。

### 2026-10-04：规则92，class initializer删掉同块及数量式效果走廊

FindClassInitializerCalls以fresh共同SSA可用性验证实际home/callee/receiver。
ProjectClassMembers消费共同EffectOrder，原前驱身份加新增after的全路径
下一效果必达事实，取代同块、物理OpId先后及prefix效果数量比较。
纯分块/菱形和逆物理布局无需新增生产匹配；绕过call、重复call、可能
不终止及任意夹杂效果不能挪进class syntax。phi观察按其原incoming edge
检查是否由call支配，真实self cell写入保留，未替代self初始化证明。

零length、零实参、method不逃逸、未使用call结果及单initializer接口仍
是现有静态字段表示的前提/接口范围，不宣称所有ordinary method call都
是static initializer。实际body、DefineOwnProperty、receiver及scope还须
由独立共同树通路证明；多参数/多initializer应通过相应接口与分析扩展。
本次额外核查并登记class_self及literal_class_fields的局部限制，未扩大旧
fast path，也不宣称全仓每个条件已逐行迁移完成。验证及剩余gate见status。

### 2026-10-04：规则93，class self 私有初始化与捕获的跨块证明

FindClassSelfStores复用共同EffectOrder：allocation支配creation，creation
支配store，store支配initializer call；allocation→creation→store使用实际
效果前驱身份及全路径下一效果必达，不比较数量、同块或物理OpId。
所有原cell读取须由call支配。提前读/发布、夹杂效果或不终止路径仍拒绝，
不是靠删除语义检查扩大覆盖。原self store继续留在父FunctionTree。

ClassSelfOrder以真实同文件初始化器正文验证闭合私有区间，再把实际class
capture occurrence绑定进arena/revision受约束的证书。ScopeFacts消费这个
Language-only事实；Defined、未知child writes和一般cell初始化仍有各自
证明义务，不能把该证书用于别的capture或TDZ豁免。FamilyWrites从实际
已准备ClassExpr的initializer context/body登记原method barrier，与普通
定义成员共用owner索引；缺正文、错call或错body owner仍拒绝。

逆物理布局的完整用例发现ProveLocalAccesses把target block编号较小当作
真实环。现以已有OrderAcyclicBlocks的Kahn顺序区分布局与实际cycle，再
使用原支配证明。该局部迁移尚未替换一般cell的PC顺序和循环activation
限制，已列入开放项。没有生产opcode串、样本身份或新图形枚举分支。
验证与阶段一边界见status.md；不把此项交付称为全仓审计闭合。

### 2026-10-04：规则94，字段receiver来源与保留读取分离

IsReceiverOperand按实际entry/operation origin与已证明Language域识别
receiver来源，IsReceiverField与LiteralClassFields共用查询。FrameThis与
ReceiverEntry保持各自SSA身份和原事件，不因为都是Language就互相替换。
真实owner/context建立frame初始化事实；相同原字节脱离owner仍拒绝，
derived/arrow/未知this没有获得新的初始化许可。没有新增LDTHIS局部序列。

完整字段源树的分块用例进一步发现结构检查要求receiver直接是ThisExpr。
这是internal AST的表示限制，不是字段语义要求；现允许共同BindingPlan
保留原读取，TreeOperands独立核对原base use、存储身份及descriptor语义。
native static printer仍需单独关闭implicit receiver与原存储/event省略义务；
尚未证明的snapshot树保持不可发布，而不是把symbol重新打印成this。

新真实file/frame initializer用例18组、独立native对照270组，组件正反例
及200/201/402 work证据见status.md。一般literal字段正文仍要求单块及
literal RHS；没有扩大这条旧快速路径的布局模板。当前重新核对CallPlan
同块条件未回归；inline-owned Reference、module_guards邻接和phased
单RHS/入口仍是开放接口边界，不宣称全仓逐行审计闭合。

### 2026-10-04：规则95，字段正文的共同执行链与正常返回

LiteralClassFields不再要求正文只有一个块或没有边，也不以物理扫描中
return先出现来拒绝后续字段。共同EffectOrder新增必达首效果与正常返回
endpoint，字段证书沿原exact-next效果链恢复顺序，检查前驱身份/支配及
全部原字段归属一次。纯菱形/逆布局/空入口不需要生产分支；绕过字段、
重复执行、可能无限循环或夹杂效果仍没有class self私有区间许可。

不同return站点可以同意正常完成；返回值不由endpoint合并推断，消费方
仍检查实际undefined输入。normal return与未证明终止不再都用空after
表示。共享分析保留原操作及图，没有据sample/PC/寄存器猜测顺序。

72组完整真实文件与1080组native对照、1/200/201/402组件矩阵见status。
正文条件字段的native表示、一般RHS表达式与局部String/copy事实、phased
字段及保留receiver的存储省略仍未闭合；没有把这次通用效果替换称为
全部static initializer或阶段一完成，也不继续扩充旧literal-only模板。

## 规则96：SSA字面量来源与共同无依赖纯值表示

本次从最新固定五输入的26个incomplete核对实际拒绝指令。普通函数中的
参数/属性/宿主调用算术没有可安全补齐的数值域事实；未知父类super spread
协议也未被布局放行。这些不是同块门槛，不借配对源码猜类型或父类品牌。

实际替换了LiteralClassFields按物理遍历确认String/copy的局部事实。现在
每个值从当前SSA定义追溯、缓存，不因块被分割、逆向排列或复制链变长
新增恢复分支。首/后效果、receiver来源、可用性、descriptor及normal
return合同保持。一般表达式、phi字面量及phased字段仍开放。

完整文件回归又暴露两个表示缺口：普通流无依赖纯值跨块时强制局部存储，
以及森林依赖物理构造顺序。现以共同typed SourceEvaluation纯值合同、
无输入/效果/异常/隐式上下文和原Language值作为证明前提，记录确切唯一
消费UseId，独立重查census及所有权，再从原constant/origin预构造一次。
这是无依赖语言值的语义证明，不是字符串opcode、特定寄存器、PC、块形状
或样本选择。logical selection、phased reference和纯值所有权分别校验。

额外登记expression_forest.cc::CanRetainPrefix的实现边界：仍只保留单块
normal正文前缀，拒绝异常/phi/复杂作用域等前缀。不能称作语言语义条件，
后续应以共同控制/效果/完成与缺口归属替换，不追加局部布局模板。
CallPlan的非inline callee引用存储、其他旧rest/派生协议快路径、一般
字段/表达式和作用域接口仍未全部闭合；本轮不宣称完成逐分支全库审计。

验证与固定五输入结果见status.md当前规则96；原生产样本身份文本搜索的
有限结论不升级为全库不存在特例的证明。AGENTS.md第1a节的禁止规则继续有效。

## 规则97：固定自身属性读取依据语义投影，不再依赖私有类 corridor

实际完整ViewModel模块的丢弃prototype读取此前只能随方法安装或初始化
corridor投影，否则强制类carrier和computed命名包装。它不是一般成员
读取的getter/receiver义务：固定runtime的原生class已安装不可写、不可
配置的自身prototype，读取取得已存在对象。新增共同copy/phi SCC身份
证明原ClassCreation，常量边界核对真实prototype键，最终ClassTree检查
对应原创建。因此未使用读取不要求同块、无分支、未逃逸或效果邻接。

具名键与计算键共用typed ClassPrototypeRead事实；计算键记录实际String
producer及原TextId，不把String域或任意调用结果当作值为prototype。
混合身份、未知键、普通对象或活跃结果保持原读取，优化精度不足不拒绝
整个函数。现阶段不同String producer经phi汇合但文本相同的键尚未取得
共同常量值事实，仍保留原读；不得为此追加phi形状或数量模板。

这项固定自身属性证明不取消实际方法安装、初始化移动、callee Reference、
scope/TDZ或异常/效果所有权义务。原ClassTree保留字段名/方法名的
profile/native合法性检查，不能把任何缓冲区对象无条件称作原生class。
新增源键与ClassTree身份变造反例验证最终条件证明边界。23个完整记录
及12个仍标记incomplete的输出表示简化；覆盖与局限见status.md规则97。

再次核对CallPlan没有同块及手工cross-inline例外回归，但inline-owned
Reference接口、module_guards邻接、phased selections corridor和单块
CanRetainPrefix等开放项仍在。此前审计表的共同替换方向保持；这次不是
全仓每个生产分支逐行审计闭合，不能宣称不存在其他布局边界。
### 2026-10-04：规则99，构造器捕获按控制流证明存储先后

`LocalSsa` 原先以 branch_target <= PC 拒绝所有向低地址跳转，并以
first_capture 的指令编号切断读取。`DerivedProjection` 又以原指令编号
选择最后非receiver写入及首个receiver初始化；`FamilyTrees::ReceiverGuards`
在已证明支配后仍要求 store < creation。以上地址条件不能定义执行先后。

共同 `OrderAcyclicBlocks` 现在证明可达正常图无环；局部SSA保留每个真实
site的类型化DAG rank，并沿同一图传播未证明只读的capture暴露状态。
消费者查询实际读取的PreservesRead，未保留capture见证不再是时间边界。
初始化候选与DefinedCaptures的最后写入使用这个共同顺序；跨块receiver
初始化由原支配区间证明，仅同块仍比较真实指令顺序。正常/异常效果、
原工作图操作和地址身份、环境逃逸及传递捕获写入证明继续成立。

DAG rank仅是充分的不可后执行证明，不是支配证明、执行历史等同或操作
重排许可。当前receiver suffix接口仍不能表示所有互不支配写入的精确
创建点cell快照；真实循环/异常环境也需activation-sensitive转移。保持
显式实现缺口，不能把rank或无环范围描述成语言准入条件。

回归使用两种相同执行、不同地址的完整decoded构造器：closure块放在
super store之前，以及初始undefined store放在super/closure/return之后。
普通/转发arrow、有无guard、1/200/201/402规模贯通同一FunctionTree和
printer。可写/逃逸捕获、同块早创建、分支绕过初始化、错误guard及实际
store/origin反馈继续有负例；Node独立对照即时setter、Proxy替换receiver、
getter/callee替换、noncallable和任意抛出值。

本次调用方审查另定位两个未闭合范围：`semantic_locals.cc` 的初始store
选择及跨块load仍混用原instruction顺序；`scope_facts.cc::Rebase` 在已
取得初始化/guard支配区间后仍有跨块PC比较。这些可能拒绝合法布局，
需要共同初始化快照/实际block身份替代，未在本次宣称清理完成。

## 2026-10-04：尾部语法投影与真实完成义务（规则 100）

共同 PureTailCompletion 将 module 的纯尾部完成规则复用于原生 callable。
只检查实际 function body 的末尾 direct return 及 typed undefined operand；
不查看 opcode 序列、PC、寄存器、CFG 布局、样本或源码字符串。更早/嵌套
return 和 finally 完成覆盖必须保留。这个 AST 所有权边界是语法等价的
前提，不是拒绝反编译的结构匹配；不满足时继续打印原 return。

async generator 有操作数 return 的隐式 Await 不能省略。原独立值/队列
检查没有区分该微任务边界；新增对照复现 completion 提前，再按共同
CallableKind 保留原返回。bare return 没有该 implicit Await。source
occurrence 仍落在真实结束边界，其他范围验证义务不放宽。

实际旧 unknown-parent 构造器仍走 pinned RuntimeSuperCallSpread：先
IsJSFunction，再私有 array 参数读取和 JSFunction::Construct。原生默认
构造器仅要求 IsConstructor，允许 Proxy/bound constructor，语义范围不同；
打印 super(...args) 又引入用户可观察 iterator。这些是尚未承载的真实 VM
义务，不能因源码常见写法或未知 SDK 父类名称删除。已有 invocation-frame
forward-all-args 隐式构造器证明保持。尚无样本运行证明该边界实际触发。


## 2026-10-04：以共同退出语义替换 preserving RETURN 的尾部匹配（规则 101）

RecoverFinalizers/CompletionTreeView 原 TailParents/TailWithin 仅承认尾部
Block/If 路径，拒绝已证明 preserving completion 的循环/暂停清理；另把
native If.alternate 固定当 RETURN，无法接受共同谓词反转后的源码。

改为实际 AST 的 Euler 祖先区间和原位、显式的 labelled block break。循环、
try/finally 不视作透明结构；break 终止原执行位置后的清理，途经的 finally
照常执行。RETURN arm 由原适配器的 fallthrough 与新鲜 TreeFlow polarity
确定，逆投影也重验该事实。原 completion 节点、operand、origin 和既有
控制/副作用/异常校验保留；不是删除校验或加入 loop/try 的局部白名单。

Block labels 只能供实际祖先 break 使用；continue-to-block、错目标、缺标签、
function body label 和 try/catch/finally 必需块上的非法标签拒绝。实际暂停
和 loop/nested-loop/post-loop override fixture 贯通共同引擎与 Node 行为对照，
覆盖三 profile、多种分支宽度和 200/201/402 规模，不用拒绝作为正例。

仍开放：既有 yield 与 guard 的同一源码序列包装、following placement、
completion_plan 按原 PC 查找 cloned occurrence 的精确身份，以及此前
semantic_locals/scope_facts 初始化顺序的跨块 PC 比较。它们是尚待公共模型
替换的实现限制。本轮定位的 402 select-many 失败在 FamilyEnclosure 的
TreeStructure 资源准入；累计 tree/scratch admission 不等同真实 RSS。
未抬高阈值、删除检查、修改正例预期或开展暂缓的 DFX。

## 2026-10-04：普通 cell 初始化不依赖跨块地址顺序（规则 102）

semantic_locals 原按物理顺序选每个 cell 的首个 store，且在支配证明后
仍比较 allocation/store/load 的 instruction 编号。scope_facts 对已支配
creation 的初始化 store 和 Defined guard 也比较跨块编号。这些是布局
限制，不是初始化或 Defined 的语义定义。

现由共同 LocalAccessPlan 保留每个 cell 的 disjoint dominator regions，
读取与捕获调用同一 InitializationBefore。store 支配、allocation 身份及
全部写入值域义务保留。同块编号仍表示真实指令顺序；跨块不以 PC 选择
执行先后。未保留捕获的暴露沿实际 DAG 前驱传播，不能按更早的物理
creation 误判兄弟分支，也不能漏掉地址更晚但执行更早的 creation。

解码回归覆盖两种反向布局、三个历史 profile、1/200/201/402 读取和独立
初始化区域，以及分支绕过 store、未知捕获暴露和晚 guard 负例。区域不是
reaching-value 快照；所有路径初始化的合流、循环/异常 activation transfer、
旧 deferred/module prefix 及既有 coroutine 包装限制仍是明确覆盖缺口。
这是此前定位的初始化条件修复，不是全仓形状条件审计已经闭合。

## 2026-10-05：undefined 静态字段声明是语言表示规则（规则 115）

共同printer对FieldDefinitionExpr中确切的UndefinedLiteral省略初始化
表达式，输出原生字段声明。没有按字节码生产者或样本名称判断值，也不
省略字段本身：无初始化声明仍在原位置定义自身undefined数据属性。
receiver及静态字段投影的既有义务保持；表达式来源是字段名后的零宽
事件。非字面量RHS不因运行结果可能为undefined而被省略或移位。

真实VoIP配对验收检查继承getter/setter不会被触发、字段和方法descriptor/
name/length，以及受控Promise中的共享字段重读、请求交错、参数getter
改写callee和原异常身份。样本名称/源码文本仅用于独立测试定位及有效JS篡改，
不进入生产恢复。1/200/201/402字段和非字面量RHS检查共同打印与来源。

仍开放：已有staged buffered literal跨选择时的机械赋值表示，以及普通/
异常native initializer interval的布局限制。它们不能因字段声明缩短就
被当作已解决，也不能靠增加更多局部形状或删除效果/异常义务来扩展。

## 2026-10-05：普通可用 RHS 的字面量初始化采用共同效果顺序（规则 116）

普通 LiteralPath 原 Same/Before 仅承认同块，字段按物理 OpId 排序；
普通 use census 也只检查 allocation 所在块的直接使用。这些不能定义
所有权、执行次数或未完成对象的观察，且纯复制/同身份 phi 不是真正观察。

现为普通路径一次构建共同 EvaluationAliases 与 EffectOrder。字段 RHS
调用 actual-allocation ValueAvailableAt；forward must-reach successor 与
exact predecessor 构成同一 allocation 的全路径单次写链。字段数量只
约束遍历界限，不能替代身份/执行次数证明。已证明 capability 后不再附加
同块、PC 顺序、邻接、producer 名单或 selection 数量条件。字段依共同
reverse-postorder 排序，normal/phased 共用 canonical-use 索引检查真实
观察和 phi edge use，保留私有值域、own descriptor、数组索引及异常义务。

单元测试原两处把纯 unused ValueCopy 当作观察，现改为真正的 heap read；
纯参数复制作为合法 transport 正例。实际解码的绕过写入、重复写入、纯
循环、提前读取及 DEBUGGER barrier 仍拒绝；三 profile 的四种布局、
1/2/200/201/402 字段和重复复制/分支实际输出完整函数并独立执行。

仍开放：late-owned RHS 的普通 local native interval、phased unique corridor
及条件/分阶段初始化恢复为原生对象表达式。保留这些旧 fast path 的明确
前提，不增加局部模式；不能把当前 available-RHS capability 宣称为所有
复杂初始化均已恢复。本轮固定五 ABC 源码字节未改变，阶段一尚未完成。

## 2026-10-05：条件极性与等值互补属于语言表达规则（规则 117）

共同 printer 原条件表达式保留 NOT 包装及反向分支，BooleanOperand 又
在等值结果外保留逻辑取反/双重转换。现以 typed UnaryExpr NOT 的极性
归一化 conditional，奇数极性交换原 consequent/alternate，偶数极性
保持原分支；条件及被选分支仍在原次序执行。等值运算符的互补由
TreeEqualityComplement 的 exhaustive enum 映射定义，不是值 IR producer
准入名单，更不根据字节码、样本、名称、PC 或源码文本决定恢复。

关系比较因 NaN 没有相同的互补关系，保持 NOT；不以数学直觉把 < 改成
>=。转换异常、getter 次数/顺序、返回值及对象身份照旧，重排分支的
原 ExprId 保留实际 source range。被省略 NOT 的 range 仅覆盖条件，
不会越过问号落到任一分支。优先级查询消费实际输出运算符，保留指数、
一元及嵌套比较的必要括号；原 FunctionTree 与共同语义/来源验证保持。

这不解决跨语句 snapshot 临时变量、staged literal 机械写入、未恢复
构造器、完整签名/字段或所有 record 的独立风格验收。源码投影不能被
当作这些恢复义务已经闭合，也不能授权数值默认合同变化。


### 2026-10-06：显式语言数值语义恢复普通 NEG（规则128）

实际WindowUtil getCalDegree在显式二元模式下因raw parameter NEG结果无法
证明而UNRECOVERED。用户授权新增独立language_numeric_v1；默认V3及旧
二元模式含义不变。共同scalar adapter/normal/phased sparse solver/linear
lexicals按原输入事实授予Number/BigInt正常结果，不以先前乘法、TS注解、
参数拼写或样本作为原参数后续转换的依据。legacy NEG的forest与fresh
operand checker消费同一typed premise，旧模式仍检查minimum-tag witness。
既有Unary/Binary/Update AST及printer负责嵌套/优先级；NaN原AST位模式
保留，只有该显式契约下的文本边界忽略payload，private编码继续拒绝。
没有新增same-block、adjacency、固定选项数量、producer名单或局部字节码
模板；arity=1/2是原typed操作语义。复制/拆块、普通/异常、负例与200/201/
402覆盖属于新功能检查。未知super父品牌及私有数组义务没有被数值模式
放宽；整体record与平台验收仍须独立完成。


定向numeric_update_tree_source_test另暴露普通临时初始化实际语义错误：
后一参数内部更新承载原producer赋值，前一参数先读其尚未初始化的binding。
共同snapshot planner现在按inline forest的真实eager use顺序挑选occurrence，
并在全部provisional parent assignment形成后重建实际读取/初始化顺序。
失败时保留原Root语句，bounded dependency queue同时保留被提前执行的输入
赋值；每operation/use最多访问常数次，不反复全图retry。fresh checker核对
相同availability义务，并拒绝把赋值移到第二次读取的篡改。旧snapshot同块
准入仍是明确实现缺口，未新增或扩大；没有INC/call名单、参数数量模板或
样本分派。三个profile、普通/异常、201copies/拆块及旧值/更新值/callback/
原异常对照覆盖该语义。没有把INCOMPLETE/refusal算为这项正例。


### 2026-10-06：显式语言构造模式恢复普通 SuperSpread（规则129）

用户独立允许native super语义，包括动态Proxy/bound父类、new.target、
iterator及原异常；默认和既有两个数值契约保留。未知父品牌是原pinned
语义义务，新模式是显式验收选择，不能由语言数值授权推导或由样本假定。

新增条件分别证明：当前源契约允许native spread；位置属于derived
constructor语法；输入/结果数和角色符合typed SuperSpread；original
FileFunction/work graph、方法和完整route identity一致；原constructor
输入通过既有稀疏DerivedValueStates的copies/cells/phis确为当前调用身份；
原第二输入对应spread child。没有同块、邻接、固定PC/register、producer
名单、参数数或样本名字准入。class context、receiver初始化/guards、
闭包保留及效果/异常顺序仍由原family/evaluation/projection负责。

开发途中对native super父类读取时机作了错误判断，曾增加提前读取要求。
独立Node probe证明range/spread都在参数求值后读取父类，已完整撤销这项
额外snapshot条件和对应拒绝断言；原已提交的共同初始化顺序修复保留。
getter参数现在是正常组合正例，不要求机械提取临时变量。复制/拆块、
200/201/402增长与foreign constructor、缺失owner、stale契约负例共同检查。

实例字段表示、implicit constructor、继承setter、配对implicit构造器的
iterator污染及全record验收尚开放。不得把完整函数树或局部constructor
对照扩大成这些行为的证明，也不得增补布局匹配来掩盖其表示缺口。


## 规则 136：class 命名 carrier 的通用替换

规则76保留的“非同名 identifier 上下文一律计算属性包装”是表示限制。
ClassNames 现通过一次 typed lexical traversal 判定新增 class-self 环境是否
遮住实际存储。安全 class 使用原生名称；同名可写 module/capture、heritage
TDZ、未知正文及不可表达标识符保留原表示，不使 AST 新增拒绝。没有以类名/
record/sample、固定 producer/selection 数、同块、邻接或字节码序列选择恢复。
名字比较仅是必要的语言词法边界，不按名字分派语义。

普通 source ownership、owner/symbol identity 与范围检查继续存在；它们防止
读取错误存储或非法数组索引，非图形状准入条件。共同 visitor 进入 heritage、
每个 prepared family、accessor/static initializer 与 FunctionExpr 所在位置，
capture alias 跟随真实 target。未知正文只撤回原生命名许可，保留 INCOMPLETE/
UNRECOVERED 输出。算法/复杂度/预算依据见 function-tree-output.md 规则136。

新增测试包括倒置 expression arena、闭包 capture、同名局部与模块别名、
实际 capture 写入、保留字/非ASCII、未知正文、限额和200/201/400/800规模。
既有63组 class binding/module reassignment 对照及 named-self mutation继续
通过；新增无名称冲突的 Box 改用原生名，原外部绑定重赋值行为检查保留。
两个 Logger 与 WidgetCard/其余实际record沿 ABC-only 路径恢复后独立验收，
没有把包装消失当作完整阶段验收。full corpus、RSS/benchmark和周期审计
仍暂停；其他恢复准入/结构gate缺口继续由status记录。

## 规则 137：native receiver 保存与 ABI census 的共同替换

原 source snapshot 机械保存 super/current-this 结果是表示限制，不是值身份
要求。EvaluationShape 的 typed receiver capability、BindingPlan 与实际根
TreeFlow 共同复用 native invocation storage；所有 initializing event、实参、
原 provenance、作用域、控制、效果和异常义务保留。TreeOperands 对每个
消费者独立核对 actual producer、owner 与 saving occurrence，错误参数替换
或删除 super 不能通过 fresh semantic mutation。VM derived entry undefined
始终独立；不存在“函数有 super 即允许所有 this”的 whitelist。

derived ABI 入口读数与源码 receiver 引用数的等式拒绝了合法 super 结果。
该实现 admission 已删除，精确身份由原共同树对应校验承担；不是删除真实
receiver 不变量。普通 ABI census、owner、frame 与 callable 要求继续保留。
同样，静态字段 printer 不再用全 arena field-count==child-count 准入：共用
DiscardedInvocationRead 消除已初始化普通 this 的无副作用语句，原字段语法
及来源仍检查。derived/arrow 的 this 不沿此投影省略。

新增 current/initialized×COPY×拆块×getter 与分支组合；actual ABC Widget
构造器含 current-parent Proxy、bound target、foreign object return、替代
new.target、参数默认值、字段状态、任意 throw identity及有效JS mutations。
原语言构造模式仍独立、显式，默认 VM/profile 语义不变；未用样本路径、
类名、图形状、固定 producer/selection 数或寄存器布局做 production 分派。
完整字段表示/implicit ctor/平台生命周期和阶段一独立验收仍是实际缺口。

## 规则146：丢弃结果的空值语句观察

原三域有限解释和reference formation复用到ExpressionStmt/Block/If，按原
控制语义合成无效果或同一原效果occurrence。typed ExprId/StmtId投影复用
member/call分类、printer及来源计划。null和undefined域不能丢失原效果，
other域必须保持原receiver、key/argument/callee、形成次序和执行次数；
opaque效果后的域相关guard必须有新的值流证据，不能沿用旧reference事实。
这些检查证明身份/效果/异常/完成，不是两个guard、同块、邻接、producer
名单或固定selection数的附加条件；逆序arena与decoded COPY/块拆分共用。

当前投影保留一个原效果表达式，可包含任意已有复合表达式；不能删除第二个
独立效果或把live phi写入当作无效果。声明、abrupt、loop、handler、debugger
和gap尚无丢弃表达式摘要，原完整或显式部分语句继续输出。这些是实现能力
范围，不是语言不变量，也不能扩成新局部pattern。一般多效果sequence、
分离形成和跨reference链需要共同模型扩展；完整phase gate仍开放。

80共同树/32,000行为、六语义反例/typed预算边界和336 actual decoded函数/
75,264行为验证此边界。初始live结果join正例失败后保留作表示反例，并另验
实际void观察，未删除真实写入或来源检查。fixed-five/six-mode导出只改变
MiniGame和ShoppingFullScreen原void guard的共同投影，样本只作独立测试；
具体范围、失败/成功和暂停事项见status.md。


## 规则172：以共同活跃性证明私有存储合并

binding_interference 从已检查 FunctionTree 的求值/控制完成意义建立图，
copy 只提出待证变量对；最终读取经替换链缓存解析，不限于原直接叶。
干涉由真实 read/write、异常及暂停恢复路径决定；
没有 ++ 模板、同块、邻接、PC/寄存器布局、样本名或固定 selection 数。
转换抛出在写入前，故 catch 只读转换前旧值时可安全合并；更新后另有可抛
调用/await，catch 观察旧值时必须保留。yield RETURN 的 finally 读取也保留。

活跃性不能证明词法身份：每个改名后的 occurrence 由共同作用域解析检查，
原声明与原生 for-let 初始化/生命周期分别证明。删 empty declaration 必须
证明初始 undefined 不被读取；source local、capture、outside-body 不参与。
输出 Name 与 class-self 自由引用使用同一 typed storage map，原 IR/证书不改。
最终惯性 store 的省略按既有完成事实，先更新 loop-entry 投影再生成声明。

保守 finally continuation 并集、一次 disjoint pair 合并、跨捕获/参数环境及
严格资源合同的命名包装仍是明确实现缺口，不是语言不变量。测试包含逆序
arena、0/1/200/201/400/800 empty blocks、独立多 copy 对、shadow/capture/
初始 undefined、catch/await/generator return/throw；独立错误合并 mutations
必须是可解析 JS 且行为不同。实际 pasteboard 两个循环直接更新原循环变量。


## 规则173：共同流模型的过渡责任边界

干涉消费者已删除整个FunctionTree/statement/expression构造分支，新的
RecoveryFlow承担typed定义/读取/完成模型适配。小图独立oracle通过，
真实输出保持。不是把Coalesce重命名：该函数仍保留的CompletedStore/
初始化/生命周期准入逐项列于unified-variables.md，尚未宣称退出。
LegacyFlowProjection携带旧事实是明确过渡缺口，不是新semantic premise。
后续只允许扩充共同模型/分析精度并替换旧决策，不扩展初始化/快照/循环
各自的局部条件。复杂finally保守图不授予binding lifetime变化权限。

### 规则177：发布范围不再独立匹配省略语法

FamilyEmission删除PlanNullishSyntax、DiscardedInvocationRead、
ImplicitTailCompletion的第二次恢复判断。省略合法性仍属于原恢复分析；
打印事件显式记录GeneratedExtent，发布只验证同一产物的坐标类别和
函数归属。默认跨度要求非空，显式边界要求相等；这证明来源传输完整性，
不以空范围反推语义省略，也不增加同块/邻接/图形状准入。
循环头跳过尾部省略根的遍历漏洞通过继续消费原节点修复，未新增恢复模式。
测试端仍独立重算恢复计划、检查尾部/可选链范围关系并执行生成源码；
消费者信任恢复事实不代表语义验收已经完成。具体证据见status规则177。

## Rules180: dead-write decisions use shared liveness

The new definition-elision consumer does not use statement adjacency, same-block
availability, first-read syntax, sample identity or producer operation lists.
RecoveryFlow records native declaration initialization and successful write effects;
FlowLiveness supplies whether each definition reaches an observation. The proposal
pass only selects dead inert assignments and preserves their RHS. Compiler-private
nonescaping identity is an observability premise, not a spelling heuristic.
Initialization/escape facts remain the documented legacy adapter gap.

Legacy resource-observable contracts do not run this new source transformation:
existing naming preservation may need an allocation envelope, whose resource
failure equivalence is not established. This is an explicit capability limit,
not a language property; no RHS shape matching was added to narrow that check.
Coalesce::CompletedStore remains unchanged and still requires replacement by
shared initialization/lifetime analysis. No new exception was added to it.

## Rules181: discarded-value demand is semantic, not statement adjacency

A discarded region denotes an ExpressionStmt's ignored result, not a recognized
copy sequence or basic-block shape. Its effect fact is composed by RecoveryFlow
from the existing intrinsic/initialization/write semantics. The elimination pass
consumes typed regions and demand facts from the shared backward solver. It
does not inspect expression syntax or materialize every reaching-definition edge. Control statements and their condition reads stay outside the
removable regions; source declaration initialization forces retention. Both an
entirely unused cycle and a demanded chain use the same worklist rules. Storage
and scope admissions are unchanged; no CompletedStore exception was added.

## Rules182: declaration retention is independent of value demand (superseded by rules222)

PlanBindingElisions uses FlowBinding exposure plus retained semantic occurrences
after a definition-elision proposal. It never uses variable spelling, first use,
adjacency, block identity or a producer pattern. The kUndefined definition denotes
the existing language initialization event, not a pattern of source statements.
Any retained read, assignment, update or other definition requires the binding;
capture/external exposure preserves identity even without a local read. A dead
undefined value is therefore not permission to remove its still-used lifetime.
Both proposals are prepared before the shared projection changes. CompletedStore
is unchanged; general lifetime movement and live-undefined storage sharing remain
explicitly unconverted obligations.

## Rules183: all flow occurrences consume the committed storage map

Storage identity is an explicit projection input, separate from immutable aliases.
The semantic adapter applies it uniformly to reads, assignments and both update
halves; it never dispatches on sample, block or neighboring operation shape.
The dependent cleanup stage is triggered by actual producer changes, not counts,
function names or layout. Old initialization, name and lifetime proofs remain
unchanged. The adverse test is an ordinary write through an eliminated binding
followed by a read through its retained slot; pre-storage analysis loses that
dependency. Both flow facts and independently executed source exercise it.

## Rules184: live undefined no longer bars storage sharing

The Coalesce CompletedStore admission is removed, not renamed or moved to a new
helper. An independent initialization plan consumes shared liveness and the
proposed map: preserve each live undefined at its original site, omit dead ones.
Shared interference checks whether that write clobbers another live value, and
completion dominance proves the destination initialization precedes the reset.
The existing lexical visibility/activation proof is reused for the explicit write.
No same-block, adjacency, first-use/store, operation-sequence or sample test is
introduced. Both repeated-loop initialization and straight execution use the same
semantic plan. Original copy/constant CompletedStore and initialization/scope census
are still explicit migration gaps, not claimed as retired by this change.

## Rules185: reference order comes from the projected flow

Coalesce no longer checks statement traversal controls/region intervals or the
original expression completion order to infer initialization. It also no longer
walks all original expression syntax to rediscover reference kinds. RecoveryFlow
emits actual binding read/write occurrences once, and shared FlowInitializations
answers their completion-order obligation using all completion edges. A reference
removed by an earlier pass cannot block recovery merely because its old arena
position preceded a native declaration. A still-executed reference before that
declaration fails the common proof. Name resolution and binding activation remain
independent requirements, including for unreachable retained syntax. No producer
list, sequence, adjacency, same-block or sample admission replaces the old checks.


## Rules186: expression facts and inert-statement proposals

The separate stored-result/evaluation traversals are replaced by one const
AnalyzeExpressions pass on the current projection. Inert-statement scanning now
returns a proposal before any omitted flag changes. Consumers do not reuse either
result after committing changes. No new recovery admission or opcode/shape/sample
condition is added. The existing InitializedRead scope/census obligation remains
explicitly legacy; this refactor does not claim to replace it with dominance.


## Rules187: Constants retires CompletedStore

Constant propagation no longer requires the stored occurrence to be the first
census observation or all observations to fit its original expression region.
Shared all-read liveness and read-origin coverage prove that the proposed static
definition supplies every reachable read, with no live undefined/other definition
or missing entry value. Primitive literal identity across repeated executions is
separate from this coverage proof. Abrupt exits before a join therefore compose
without a branch-specific production path. The existing conditional-missing-store,
throwing/finally and loop tests continue to preserve competing values.

No new same-block, adjacency, producer-list or sample test is added. Constants
keeps its typed primitive-literal lattice and private single-write representation;
Aliases/ForwardSnapshots still carry the old CompletedStore obligation pending
separate value/lifetime migration. Literal proposals are committed only after the
shared-analysis and sparse-propagation read phase finishes.


## Rules188: Aliases retires CompletedStore and initialized_first

Alias recovery uses the same common definition coverage as constant propagation,
freshly computed after constants commit. Its availability decisions no longer
call CompletedStore/PrivateCopy or declaration initialized_first. Both retained
source and destination copy must cover their own reads; missing entry/undefined
and competing definitions remain semantic failures. Dynamic producer re-execution
cannot bypass a covered copy on a path to its read. Source spelling and activation
are still checked separately, and captured/mapped-argument state stays observable.
An abrupt alternative and common use therefore need no branch-specific recovery
condition. No same-block, adjacency, fixed shape or sample rule is added.

Confined/RootLifetime and original alias reference enumeration remain documented
migration gaps. CompletedStore now remains only in ForwardSnapshots and must not
be expanded as a replacement for common value/effect/lifetime analysis.


## Rules189: final CompletedStore consumer removed

Snapshot value coverage now uses the same shared definition analysis as constants
and aliases, with read counts from current FlowReference events. CompletedStore
and PrivateCopy are deleted rather than renamed or moved. Their dead store-region,
declaration-control and expression-completion-order metadata is removed too.
Dead-read cleanup precedes snapshot planning; proposal computation and application
are separate. No old arena occurrence survives as a second value-availability
admission after it has been removed from the current flow.

The existing motion epoch/control-region and lexical environment checks remain
explicit migration gaps. They still prove/bound effect order, execution count and
name activation and are not broadened with new cases. No sample, same-block,
adjacency or new graph-shape condition replaces CompletedStore.

RunPass now owns invalidation of the local graph/liveness pair after any changing
pass; unchanged passes share immutable facts. This is projection dependency
management, not a new recovery admission. Pre-snapshot dead-read cleanup and
post-storage cleanup have explicit fact dependencies. No extra pass is scheduled
merely because snapshot syntax changed: its RHS occurrences remain single and
its own read/store/declaration are removed together.


## Rules190: alias name checks stop recounting syntax reads

Alias reference queries use FlowReference binding/site identities from the same
current flow as definition coverage. The original SymbolExpr scan and post-query
syntax cast are removed. RootLifetime and indexed lexical lookup remain distinct
activation/name obligations. Compound typeof and inner-name shadow tests do not
introduce operator-specific production branches. Canonical activation and the
remaining legacy initialization/motion conditions are still migration gaps.


## Rules191: initialized-first/region admission removed from effects

InitializedRead no longer decides whether a binding read or write completes
normally. The shared completion graph supplies per-occurrence initialization
proofs, so an earlier TDZ observation does not force all later reads to remain
effectful. The old initialized_first, definition_region and per-binding read
region fields, and unused region interval storage, are deleted. Region identities
remain only for the existing snapshot-motion limitation; no new shape is added.

A conservative graph precedes effect refinement; absent signature-program events
retain evaluation. Function-self body initialization is explicit. Typed pass
contracts preserve only proved positive occurrence facts through supported
transformations; event-indexed analyses always invalidate after plan changes.
Canonical activation, default-parameter program modeling and legacy declaration/
motion lifetime proofs remain distinct gaps. CSR graph input and prepaid buffer
reuse change representation/ownership, not semantic admission.


### Rules192: commit lifecycle, not a recovery admission pattern

Variable pass changes now enter a shared commit phase before the first write.
Dependent facts are invalidated there; the scheduler observes the projection
revision instead of trusting pass-returned changed flags. Analysis access during
commit is an ownership failure, never a same-block/adjacency/opcode/name test.
It prevents consumption of a partially mutated projection. No semantic recovery
predicate was added. Existing value, exception and lifetime obligations remain.


### Rules193: remove printer-owned discarded-operand optimization

Comma-left elision now consumes shared completion facts in a dedicated pass.
Its read occurrences leave the projected flow before dependent dead-definition
and snapshot decisions. The old printer test of ExpressionEvaluation is removed;
printing only consumes replacements and transports omitted source ranges.
Sequence grammar defines a discarded value in the language. No opcode sequence,
block adjacency, register layout or sample identity is used. Required coercion,
TDZ, calls and suspension are excluded by shared effects, not by a whitelist of
recognized left operand syntax. Global pass convergence remains open.


### Rules194: retire original-syntax snapshot observations

The snapshot epoch counter now consumes reads, expression writes and intrinsic
effects from the current RecoveryFlow. Original IntrinsicEvaluation queries,
statement-omission lookups and literal exceptions leave that consumer. The old
expression-statement census and current-statement field are deleted. Discarded
operands have no events and cannot block forwarding merely because their syntax
remains in the canonical arena. No sample, adjacency or opcode rule is added.
Legacy order/control/region/environment motion obligations remain; this bounded
migration does not claim that common activation analysis has replaced them.


### Rules195: remove snapshot epoch and control-prefix admission

Snapshot execution order now uses universal next-observation propagation on all
RecoveryFlow completion edges. The prior equal-epoch and equal-control-prefix
checks and counters actually leave the decision path. A transparent split/join
can be crossed because all paths prove the same next read; exits, intervening
observations and possible nonterminating transparent paths fail that semantic
obligation. There is no path walk per candidate or recognized control shape.
Region identity remains a documented implementation restriction for matching
handlers/activations until shared context facts replace it, not a language rule.


### Rules196: remove snapshot same-region admission

Expression-region equality and its syntax-census state are deleted. Common
bidirectional observation pairing proves successful-write/read correspondence
without replay or bypass; common reference exception contexts preserve handler
and cleanup destinations even for a nonthrowing destination read. A once-only
loop-entry case now succeeds without a loop-pattern branch, while repeated-read
and changed-handler mutants are independently rejected. Lexical identity and
lifetime proofs remain in their documented owners. Full unwind/activation and
canonical-IR precision remain gaps, not new local admission conditions.


### Rules197: separate loop grammar planning from publication

Loop declaration recovery now consumes immutable projection facts and emits a
complete typed delta before any mutation. Its expression-prefix/declarator-suffix
rule comes from ForDeclaration grammar, not a recognized bytecode sequence.
Initialization, binding identity and lifetime remain upstream obligations. No
new loop-count, adjacency, name, same-block or sample restriction was introduced.
The old interleaved decision/publication traversal has been removed.


### Rules198: remove direct-SymbolExpr copy admission

Aliases consumes common normal-result binding facts instead of testing its RHS
for SymbolExpr. Effects are not discarded with the eliminated storage: only a
shared inert proof permits removing the whole statement. Retention dependencies
use semantic discarded ownership and a sparse many-edge graph instead of one
direct RHS occurrence/one blocked source. Existing coverage, escape, mapped
arguments, lexical identity and lifetime checks stay in their documented owners.


### Rules199: ignored result recovery follows shared value use

Discarded sequence results are recovered from FlowValueUse and inert completion
facts. The consumer does not require a top-level expression statement, a fixed
wrapper depth or an adjacent producer. The semantic builder owns value transport
through sequences, choices, logical control and replacements. Observed results,
branch values, coercion/throw effects and unmodeled signature uses remain.


### Rules200: inert statements do not require an upstream replacement

Statement omission now consumes current ExpressionFacts for every expression
statement. The previous requirement that its root already appear in the
replacement plan is removed: that was pass-history admission, not a semantic
obligation. The statement discards its result; initialization and effect facts
prove absence of required evaluation. Declarations remain separate, including
undefined initialization, and TDZ reads/coercion/calls remain required.
The pass runs before demand and snapshot analysis and after later projection
changes, with centralized invalidation at commit. No printer optimization,
sample dispatch, same-block condition, or expression-operation whitelist was
added. Tests include ordinary locals without any compiler-temporary candidate,
reverse arenas, retained exceptions/effects, independent execution and geometric
work scaling. Global pass convergence and original BindingUses migration remain
open; this change removes one concrete dependence on prior rewrite history.


### Rules201: snapshot write cardinality follows the current projection

ForwardSnapshots no longer requires the original BindingUses write count to be
one. It counts current FlowReference writes alongside reads, including typed
statement-site writes, and checks cardinality with current definition coverage,
execution pairing and exception context. Eliminated redundant writes therefore
stop blocking forwarding; retained evaluations still constrain motion. Original
store/declaration identities remain transport and lifetime inputs: this change
does not yet select arbitrary successor definitions or claim all BindingUses
consumers have migrated. No new sequence, adjacency or same-block admission is
introduced. Redundant self-assignments with and without effects, reversed statement
arenas and geometric input sizes exercise this pass interaction. Counting adds
O(B) storage and O(B + R) work for bindings B and current references R.


### Rules202: snapshot producers come from current covered definitions

ForwardSnapshots selects the covered FlowDefinition and its RHS from the current
graph, instead of selecting BindingUses.store and mapping coverage back onto
that original first write. The old BindingUses.root_store index and its Scan
update are deleted. The owning discarded evaluation supplies the commit target.
Current read/write counts, bidirectional motion, exception and environment proofs
remain independent obligations. Original first-store history no longer excludes
a retained successor after dead-definition cleanup.
The existing whole-assignment-statement projection boundary remains explicit: a
nested write needs occurrence-level removal that preserves its enclosing effects.
This is a transport limitation, not a language invariant, and no new local
sequence/layout admission was added. Declaration and environment recovery still
use legacy provenance; this does not complete binding-lifetime migration or
global pass convergence. Tests compose discarded initial reads and overwritten
values, effectful dead writes and observable earlier values with reverse arenas
and geometric sizes. Selection scans B bindings, D definitions and R references
in O(B+D+R) work with O(B) additional storage.


### Rules203: statement ownership consumes normalized expression identity

ExpressionFacts now publishes the terminal identity of each current replacement
chain in the existing postorder analysis. Snapshot transport compares this
identity with its covered definition, rather than comparing the original syntax
root. No comma/operation-sequence special case was added to snapshot recovery;
all consumers see the same current expression normalization. Required enclosing
effects still prevent whole-statement deletion. Arbitrary nested occurrence
removal remains an explicit transport gap, not a semantic invariant.
The transfer plan now records the moved RHS identity instead of a boolean. The
printer maps all other omitted origins to the statement boundary and preserves
the RHS destination span. This is origin transport, not printer-owned semantic
optimization. Tests cover both inert sides, retained effects, reverse statement
arenas, 200/201 and geometric chains, independently executed sources and mutants.
The shared root table costs O(E) work/storage for E original expressions; it
avoids a replacement-chain walk for each candidate.

The new TDZ adversary also exposed a missing write-completion obligation after
Rules202 removed original first-store admission: read coverage and RHS motion
alone do not authorize deleting PutValue. Assignment forwarding now requires
the shared FlowWriteEffect::kInert proof. Native lexical initialization remains
a distinct definition kind. This is an exception/initialization proof, not a
declaration-order, same-block or layout test. The independently executed
`store; return read; let binding;` case must retain its ReferenceError after RHS
effects, and must propagate an earlier RHS failure unchanged.

### 2026-10-08：规则216，初始化引导事实与当前图事实分离

- 当前流图会细化不可能的异常边；引导图的肯定初始化证明可以传递，
  但其否定结论不能代替当前图的精确支配分析。事件编号不变不是分析
  结果完全相同的证明。
- CurrentFlow不再把引导FlowInitializations发布为最终图缓存；
  CurrentInitializations按需分析最终图。未增加任何样本、同块、邻接或
  操作序列条件，未删除效果/词法生命期义务，也未新增迭代精度循环。
- 共同图测试分别保留真实异常和移除不可能异常，并覆盖0/1/200/201/
  400/800个中间节点；旧正证明仍安全，新增精度不等于完整生命周期。


### 2026-10-08：规则217，显式全局名字替代整环境准入

- GlobalNameExpr进入共同FlowNameReference，使用当前owner及NameId批量
  查询目的作用域，替代该表达式无条件要求环境一致的旧限制。词法解析
  共用TreeScopeCheck索引；真正遮蔽（含TDZ）仍阻止移动，无关flag不阻止。
- 组合的双重not携带同一名字观察，原生全局getter必须保持一次读取、
  抛异常和返回关系。名字查询不授权省略效果，原双向motion/异常证明保持。
- 闭包/类/隐式helper/observable资源等尚未表达的依赖仍保留旧边界，
  不能从这次显式名字迁移推出全部跨环境移动已支持。


### 2026-10-08：规则218，this所有权与局部环境分离

- ThisExpr的FunctionOwnerId由共同树验证；同一准备内的快照不能进入
  子函数树。该所有权事实进入共同词法依赖摘要，替代this遇到任意局部
  声明就要求整环境一致的旧限制，组合表达式保留该依赖。
- 这不证明this已初始化：派生构造器/arrow原效果保留，双向motion和
  异常路由继续控制移动；方法getter及receiver身份不能替换为参数读取。
- 目的flag及同名input局部声明均作通用正例，含receiver变化、getter
  异常/次数和真实派生构造器super前执行比较；未添加生产形状匹配。


### 2026-10-08：规则219，存储提交后的复制传播与当前owner

Aliases移除expression_scopes_及按原ExprId推导RootLifetime的决策路径。
词法可见性与收窄声明生命期消费共同FlowReference.owner，覆盖证明仍由
当前flow产生；快照移动不再让旧scope决定后续值替换。owner存在是私有
body读取的内部表示不变量，不是同块、邻接或原布局准入条件。

Storage及独立identity/inert清理后显式调度同一Aliases算法；该依赖让
合并后只有一个值定义的slot重新进入不可变复制分析。没有新的源码/样本
名字、opcode序列、图形或固定选择数量匹配。两阶段别名与直接slot按
约定O(1)组合，之后不再存储合并；每次提交统一失效整个ProjectionFacts，
别名提交也清除旧初始化传输证明。源码/capture写回、效果、活跃undefined
及映射arguments义务保持独立。后续声明语法只消费已证明的计划。

正例包含callback观察的往返复制；反例包含先取得snapshot再进入参数
同名TDZ scope的读取。变体反向statement arena并增加0/1/200/201/400/800
空块，独立行为及错误替换验证见status。未宣称全局pass不动点、完整绑定
生命期或original IR correspondence已经完成，不继续扩展局部模式。


### 2026-10-08：规则220，组间完整干涉替代两两配对限制

Coalesce移除paired bitmap及“一个symbol最多进入一对”的决策路径。
共同live-in/成功定义产生全部temporary干涉边，包含无affinity的端点；
独立PlanStorageCoalescing维护组间冲突邻接并收缩安全affinity。每组
所有成员必须两两无干涉，不能把相邻两对无干涉误当作传递证明。

代表选择复用共同spelling分组的唯一性偏好、native声明和当前初始化
dominator位置。名字用于源码词法身份与打印质量，既不定义值等价，也
不作为准入名单；同名绑定仍可合并。逐读取/写入的实际scope、初始化
及RootLifetime证明和独立活跃undefined写回保持。无样本、opcode序列、
同块、邻接、寄存器号或固定链长准入条件。图收缩只提案，后续pass在
提交统一失效后消费direct slot map，打印器仍不优化语义。

小图稠密oracle验证所有成员的跨组冲突；长链/链首链尾非affinity冲突/
反向symbol与statement arena/1/2/200/201/400/800及错误变换覆盖组合。
已有同名shadow长链验最多一个copy声明，防止代表选择退化。完整绑定
生命期、finally精度、原IR对应及全局pass收敛仍未宣布完成。


### 2026-10-08：规则221，直接初始化消费共同定义与引用

覆盖值/复制/快照不再要求所有定义都具有AssignmentExpr。原生词法初始化
在RecoveryFlow中提供RHS、copy、StmtId写引用、当前owner与异常路由，
消费者使用共同coverage、liveness、completion及motion事实。源码变量/
捕获仍保留身份；CompilerTemporary才允许移除存储。没有新增同块、邻接、
opcode序列、固定链长、样本名称或图形状准入规则。

独立DeclarationValue投影把移除绑定与保留RHS/写回分开；合并后identity
写入由StoredResults消除，效果由InertStatements处理，打印器不重判语义。
原绑定与目的slot均须在实际引用处完成初始化，防止掩盖TDZ读或写。
这是共同语义义务，不是按直接初始化语法扩展的禁止合并补丁。const的
不可写性质复用MutableBinding；原生初始化不重复变为另一个初始化语法。
所有新提交统一失效当前投影分析，后序pass重建；原IR/完整生命期及
全局pass收敛缺口保持。布局、分支/循环、异常和反例证据见status。


### 2026-10-08：规则222，绑定保留独立消费当前投影

原PlanBindingElisions依赖尚未提交的DefinitionElisions并与其联合提交，
现已退出生产路径。DeadDefinitions只删除无需求的赋值/非初始化求值；
独立Bindings在其提交之后读取当前flow及版本缓存的FlowDemand。需求
事实与其他ProjectionFacts统一失效，提交阶段禁止查询，提案只持有ID。

私有词法初始化的成功完成不再冒充必须保留的RHS效果；初始化region
通过typed owner连接绑定。保留的读取/写入和source/capture身份作为根；
若保留绑定还需要打印其无效果初始化，owner到RHS读取绑定的依赖由
共同事件一次发布并经稀疏闭包传播。未使用绑定可删除，必要RHS保持
原位置、GetValue、次数、异常路由；TDZ读取仍保留。仍活跃的初始化
值暂不单独裁剪，公开为精度缺口，而非增加新的语法准入条件。

未新增名字、样本、操作序列、同块、邻接或固定图形状判断。声明原节点
及已提交词法初始化消费同一义务。原始IR对应、完整生命期/finally/
capture及全局pass收敛继续开放。独立小图闭包、200/201几何增长、
复制/arena变换、异常/循环组合及实际record证据见status。


### 2026-10-08：规则223，死写入不依赖表达式节点表示

旧死定义提交从原tree提取AssignmentExpr，提案也只接受ExprId。
存储合并后的原生声明已成为语句级slot赋值，因表示差异而保留死写入。
现在共同FlowDefinition::WriteSite发布ExprId/StmtId位置，提案拥有RHS，
提交只消费typed site/value；原AssignmentExpr提取与ExprId-only路径移除。

共同需求和效果同时决定assignment/UndefinedStore的值删除；词法
初始化、update转换/写回和可观察写入义务保留。必要RHS GetValue保持
原位置、次数及异常路由。未加名字、样本、同块、邻接、固定图或操作
序列判断；semantic definition kind是初始化/赋值的语言语义区分。
当前活跃初始化值裁剪、完整生命期/finally/capture、原IR对应及全局
pass收敛仍有缺口。布局变换、异常旧值及错误写入/调用反例见status。


### 2026-10-08：规则229，正常值依赖闭包与后代准备结果共享

复制专用SCC和“表达式正常结果必须先由一轮后序事实变为已知”的精度
限制退出。共同source adapter一次发布typed正常值方程，当前投影只传递
已提交的替换、primitive与storage身份。稀疏单调求解器同时闭合绑定和
条件/逻辑/sequence结果，不新增整函数固定轮次或组合形状识别。更新、
未知计算与无来源循环保持unknown；终端/绑定读取压缩不丢弃操作数。

已知值只许可读取替换；写入、RHS次数/效果/异常、初始化、TDZ、作用域
和捕获由原共同分析独立证明。版本失效与Constants→独立清理→Aliases
→storage后清理→循环/声明语法的依赖保持；不在提交中查询旧事实。

类命名阶段准备的后代SourceVariables由同一打印owner保留，成员打印
消费exact tree的不可变计划及相同外部context的命名事实。旧重复pass
执行在该owner供应路径退出；相等副本、其他context与后续修订不能借用。
这是事实所有权/名称解析义务，不是块布局准入。目标global/module/
capture和结构验证保持，静态fields的独立准备仍记录为重复开销缺口。

未新增样本、opcode序列、相邻、同块、producer-list、链长或图形状分支；
没有修改预算或用资源失败切换语义。直接/借用方程独立对照、布局变换、
异常/更新反例及完整源码执行见status。原IR对应、完整生命期/capture/
finally、per-read值精化与全局pass收敛仍开放。


### 2026-10-08: rules230, declared pass dependencies and cleanup checkpoints

The former producer-specific cleanup booleans have left the decision path.
RecoverySchedule requires declared current facts, disallows analysis during an
atomic commit and enforces values -> storage -> grammar stage order. Cleanup
completion is published only by the ordered independent definition/initial-value/
binding passes. Dirty projections cannot enter downstream storage/grammar;
no-op passes avoid repeating already completed cleanup. Only the existing
snapshot-motion obligation transports an established checkpoint.

These are compiler ownership/order contracts, not recovery admissions based on
samples, opcodes, register/PC layouts, same blocks, adjacency or graph shape.
They neither infer value identity nor justify deleting a write. Availability,
identity, effects, exception order and scope proofs remain with shared semantic
analyses. Contract negatives cover stale facts, undeclared dependencies, nested
pass/commit reads, missing cleanup owners and wrong stages; generated-source and
actual-record verification remain independent. Per-read precision and global
pass convergence have not been implemented or claimed.

### 2026-10-08: rules231, actual read values from common completion facts

The binding-wide-only value limit has retired for completed private primitive
reads. Shared incoming liveness facts, successful definition values and actual
read references now close with the existing expression equations. Initialization
proof belongs to the flow bootstrap; every completion predecessor contributes.
Unknown/missing/entry/update values and observable cells remain conservative.
Independent stronger binding invariants are frozen before contextual refinement;
conservative unreachable incoming unknowns cannot weaken that separate proof.

These conditions establish value identity and completed GetValue. They are not
same-block, opcode-list, adjacency, selection-count or compiler-shape admissions.
Each repeated ExprId joins its actual references. Constants proposes read values
only; fresh ordered dead-definition/initial-value/binding passes separately decide
storage and writes, followed by unchanged alias/interference/motion obligations.
The printer consumes committed primitive values before aliases and retains source
range/precedence. It does not infer semantics from output or names.

Earlier tests that required initial undefined/known-zero storage now assert its
absence where independent cleanup is proved. Their independent execution checks
remain, and incorrect initial-value substitutions remain adverse cases even when
the old assignment is gone. TDZ, failed writes, unknown branches/updates, escaping
and source-local bindings remain counterexamples. Layout/encoding permutations,
small independent read graphs and composition/source verification are recorded in
status.md. No original SSA, arbitrary nonprimitive identity, complete binding
activation/capture/finally or global pass fixed-point claim is made.

### 2026-10-08: rules232, normalized placement and lexical slot preference

The original early Declarations call has retired. Sole placement follows normal
value substitution and the current independent cleanup checkpoint, before aliases/
storage; its commit requires fresh cleanup before coverage. Successful-definition
dominance over every completion edge and retained lexical extent remain its proof.
Required original RHS effects retain their occurrence. No second placement round
or source-form admission is introduced. Placement still carries completion in the
current model; moving it past all storage lost useful coverage and was retired.

Newly native loop declarations exposed a representative preference bias. Common
RecoveryFlow regions/occurrences now supply binding reference extents; contracted
groups union them once. Prefer a retained representative covering actual group
references over one that cannot; missing positions cannot prove narrowed scope.
If both cover or both fail, preserve existing spelling/native/completion ranks.
No group admission changes. Every mapping still passes completion/name/source
containment/initial-value proofs. An unconditional wider-scope preference regressed
real record loop spelling and was retired. Complete feasible choice among all
group members remains an explicit gap; this bounded choice compares two retained
candidates. No new AST scope inference or per-variable graph walk is added.

Repeated-loop merging assertions stay, rather than accepting added temporaries.
Tests reverse IDs/arenas and grow 200/201/geometrically. Independent execution
preserves early values/effects and TDZ/branch/catch/loop/source-cell counterexamples.
Loop harness explicit undefined no longer falls through its default, and mutants
remove the actual producer. status.md records failures and final evidence. Full
lifetime/capture/finally/original IR and global convergence remain open.

### 2026-10-08: rules233, final-group scope and initialization preference

The union-time two-winner rule is removed. Sparse interference contraction closes
membership/reference extents first; a final linear candidate scan considers every
original group member. Earlier spelling preference cannot discard a candidate
needed by later observations. Conflict membership remains greedy; no new group
admission or fixed shape is introduced.

Existing current all-completion initialization dominance now supplies candidate
availability alongside shared lexical extent. CompletionRegion and SourceRegion
are distinct types; reachable reference extents and initialization subtrees come
from the same immutable FlowInitializations, without another semantic inference.
Scope plus initialization coverage ranks candidates before spelling. Unreachable
references omit only execution demand, not lexical proof. Existing per-mapping
completion/name/lifetime/initial-value/cell obligations still decide legality.
Selection proposes storage only; neither RHS effects nor initialization are
deleted by its preference. The pass contract/version/commit boundaries remain.

An independent all-candidate oracle covers 45,056 small group cases; reversed
IDs/affinities, missing locations and 200/201/geometric cases target candidate
loss. Native original initialization and producer/update/writeback compositions
exercise generated source with independent execution and adverse mutations.
No sample, operation sequence, same-block, adjacency or fixed graph admission is
added. Exhaustive feasibility across name/cell/value obligations, original IR/SSA,
full activation/capture/finally and global convergence remain explicit gaps.

### 2026-10-08: rules234, occurrence transport replaces statement admission

Snapshot forwarding no longer requires the source write to equal the root of a
whole ExpressionStmt or to belong to a discarded expression statement. Common
FlowValueUse supplies discarded assignment-result proof; a typed transport removes
only that source occurrence. Sibling effects and enclosing observed results retain
independent ownership. Common successful-definition/read coverage, motion pairing,
exception identity, initialized inert PutValue and name/lifetime proofs remain.

The old source-statement-to-candidate scope table and whole-statement reference
queries retire. One common trial graph supplies actual destination expression
owners and optional typed transport roots. Nested replacements share their outer
proposal's dependence; unrelated moves can share a statement without sharing a
rejection. Batch name/reference proofs cover only transported RHSs. Rejecting an
outer resource environment cannot leave an inner move validated in its hypothetical
scope. There is no destination-statement grouping admission or per-variable graph
walk. Commit only removes initialization/declaration actions with the elided
binding identity; enclosing native initialization remains.

Tests include independent-positive/same-return rejection precision, dependent
loop/resource/TDZ negatives, return prefixes, observed assignment values and
nested native initialization, with independent execution and adverse mutations.
Arena reversal, sibling composition and 200/201/geometric cases remain. Current
resource/creation and whole-native-declaration printer limits are not language
invariants and are not expanded here. Original IR/SSA/lifetime/capture/finally,
full feasible storage selection and global convergence remain open.

### 2026-10-08: rules235, canonical and projected scope share one fact

Retired precision restriction: only ProjectedBindingScope supplied a storage
candidate region, while OriginalBindingScope always passed containment and relied
on name resolution after the representative had already been selected. Original
lexical regions now come from the same single source-adapter traversal; candidate
selection and final lifetime queries use the common BindingRegion/BindingInScope.
No same-block, adjacency, instruction sequence, sample name or fixed scope shape
admission is added. An inner canonical slot cannot gain visibility from a unique
spelling, and a harmless sibling shadow need not prevent valid outer-slot recovery.

Independent root/member initialization, actual-reference names, initial-value
liveness/effects and cell identity remain separate mandatory proofs. Pass order,
revision invalidation, proposal/commit ownership and grammar barriers retain their
contracts. Source cases and independent ancestry oracles cover inner/outside uses,
sibling shadows, reversed IDs/arenas, missing facts, adverse update/scope mutations
and 200/201/geometric sizes. Unlocated original scopes preserve the existing identity
contract, not a new lifetime theorem. Full activation/capture/finally, original
IR/SSA, exhaustive feasible representative choice and global convergence remain gaps.

### 2026-10-08: rules236, shared actual name visibility before representative choice

Retired decision path: Coalesce independently constructs TreeScopeCheck and uses
global unique-spelling counts before actual names are validated after selecting a
representative. Unique spelling now comes from the shared canonical spelling facts,
and direct shadow regions query actual current references of each finalized group.
Global uniqueness remains only a style tie-break; it cannot approximate visibility.
A candidate with a shadow containing a group use ranks below a name covering all
uses. A shadow hole containing no use does not block it merely because it intersects
the bounding extent. No sample/sequence/same-block/adjacency/fixed-shape admission.

Storage grouping still owns interference only. Final name resolution, completion,
lexical lifetime, cell identity and initial-value/effect proofs remain independent;
missing regions are an explicit preference precision gap. Canonical elided shadows
remain conservative because final name resolution still uses the checked canonical
scope tree. No new whole-function round or incremental partial-acceptance retry.
Tests use two independently shadowed spellings, active versus harmless scopes,
actual-use holes, ancestry/brute-force oracles, wrong-name/update mutants, reverse
arenas/IDs and 200/201/geometric/exact-short inputs. Original IR/SSA, rewritten
binding visibility, full activation/capture/finally and global convergence remain.

### 2026-10-09 rules237: current names after binding retirement

Obligation: a proposed source name resolves to the retained binding at each actual
occurrence. The common current graph now publishes retained-name presence from
binding kind/exposure plus all retained definitions/references. Canonical nearest
same-spelling identities are projected across retired private temporaries with a
sparse forest traversal. Current storage/snapshot/alias name checks share this mask;
old all-original-symbol activation exits those decisions. No samples, operation
sequences, block adjacency or fixed source/CFG shapes decide retention. Canonical
forest ownership validation checks semantic provenance, not compiler-layout admission.

The fact cannot delete a lexical initialization, merge observable cells or prove
TDZ/completion/effect order. Existing independent proofs remain. Source locals and
captures are retained even without a current read; unknown shadow identity remains
conservative. Independent ancestor masks, ID/fact/arena permutations, retained-local
wrong-merge/update mutants and 200/201/geometric/exact-short budgets cover this path.
Rewritten native-for visibility, relocated scopes and simultaneous prospective
storage retirement remain documented precision gaps, not language invariants.

### 2026-10-09 rules238: projected lexical name resolution

Obligation: each retained occurrence resolves its proposed spelling to the intended
current binding. The common name index sweeps current retained original/projected
lexical intervals using canonical typed spelling identity. All alias/storage and
snapshot symbol/free-name decisions consume it; original statement scope queries
and repeated TreeScopeCheck instances exit those optimizer paths. Current shadows
also come from this sweep, so narrower native declarations/native-for lifetimes no
longer cast their entire original shadow. Independent init/cell/value/effect and
completion/lifetime proofs remain; name resolution is not a deletion decision.

Explicit enclosing environments preserve self/module/capture/parameter parent
identity; missing local scope is unmodeled. Co-located conflicts/unknown facts
return typed gaps. Nested/disjoint intervals are a source lexical-scope invariant,
not an ABC or CFG shape admission. No sample, sequence, same-block or adjacency
matcher is introduced. Independent ancestry/mask/projection oracles, native-for
outside/inside-read source cases, actual syntax assertions, wrong-merge/update
mutants and layout/scaling/budget/reuse negatives cover the migration. Prospective
simultaneous storage retirement/renaming remains a precision gap, never a language
invariant. Full activation/capture/finally/original IR/SSA/convergence remain open.

### 2026-10-09 rules239: simultaneous copy-binding retirement

Obligation: every retained rewritten read must resolve to its fixed intended root
under the final retirement proposal. AnalyzeSourceNameRetirement starts with all
eligible copies absent and monotonically activates bindings required by name or
independent lifetime demands. Inert copy RHS reads are conditional on their owning
destination surviving; observable/embedded RHS reads stay required. The common
worklist combines lexical shadow activation with these conditional dependencies.
Aliases' optimizer-local old-namespace rejection and blocked-copy graph retire.
Value/cell/effect/completion/TDZ proofs remain separate and unchanged.

Nearest retained lexical parent and coincident-region peer identities are common
semantic facts. Unknown scopes or ambiguous declarations are typed capability gaps.
No sample/name/opcode sequence, same-block, adjacency or fixed CFG layout admission
is introduced. Fixed retained roots are the normalized proposal contract, not an
instruction pattern. Source and small independent graph oracles test simultaneous
retirement, newly retained shadows, conditional demands, legal preceding storage
recovery, TDZ/incorrect-root counterexamples and layout/budget/scaling stability.
Prospective storage-group renaming/retirement, full activation/capture/finally and
original IR/SSA correspondence remain documented gaps.

### 2026-10-09 rules240: joint storage namespace

Obligation: all remapped semantic reads/writes and live-undefined writes resolve to
their fixed representative in the final accepted storage proposal. Coalesce uses
common retirement closure rather than two original-namespace ResolveNames batches
and optimizer-local blocked-mask filtering. Root/original completion, lexical lifetime,
undefined liveness and interference remain independently required and proved on the
same unchanged flow. Rejecting a mapping leaves other member-local initialization
actions valid; storage sharing never makes its RHS reads disappear.

Representative completion/lifetime feasibility now precedes current-shadow preference,
which may become obsolete when another group retires the shadow. This is proof-order
repair, not a new compiler-layout matcher. Two writable-group positives, source-local
and interference negatives, independent execution/mutants/declaration counts, reversed
arenas/block composition and tiny rank/subset/throw/resume oracles cover the change.
No sample/opcode sequence, adjacency, same-block or fixed-shape condition is introduced.
Arbitrary renaming/relocation/re-ranking after subset rejection, full activation/capture/
finally/original IR/SSA/convergence remain explicit precision gaps.


### 2026-10-09 rules241: snapshot consumer dependency refresh

Obligation: a consumer following storage/alias changes uses the current retained
namespace and observations, with ordered cleanup complete. A revision-owned completed
snapshot attempt requests one fresh application at the stable storage boundary;
no-op producers do not request it and consumer edits do not create a self-loop.
This is a scheduling dependency, never source/bytecode admission. Old fact revisions
remain invalid even when the consumer checkpoint advances. Clean and dirty partial
cleanup groups, active-pass and wrong-stage queries are rejected.

The refresh reuses existing common availability, count, motion, exception, completion,
name and lifetime analyses. Storage can remove an inner lexical shadow and enable
ordinary expression recovery; source-local or genuinely interfering values keep that
shadow and retain snapshots to avoid TDZ. Independent execution/semantic mutants,
reversed statement arenas and block composition cover both. No sample/name/opcode
sequence, same-block, adjacency or fixed CFG shape condition is added. Lazy proof
construction changes cost only. Arbitrary incremental convergence, full activation/
capture/finally and original IR/SSA remain explicit gaps.


### 2026-10-09 rules242: definition-independent stable read values

Obligation: each substituted read has the same normal value under every retained
successful definition and resolves to the intended stable entry binding at its
actual destination. The common flat solver joins typed opaque identities alongside
primitives. Mapped arguments, unknown captures/signature observations and retained
writes defeat grounding; unknown/conflicting/unseeded/live-undefined inputs defeat
agreement. These are identity/value/capability facts, not producer shapes.

BindingValues only rewrites reads. Independent definition/initial-value/binding
cleanup owns deletion and preserves RHS effects, throws and lexical completion.
Common name and lifetime resolution validates all destination reads. Single-write
alias storage retirement remains its own contract; repeated-copy read recovery no
longer depends on that representation. No sample/name/opcode sequence, same-block,
adjacency or fixed CFG shape matcher is added. The explicit ReadValues stage prevents
later alias/storage/snapshot mutation of established destinations; it is scheduling,
not bytecode admission. The original shadowed-snapshot style test remains unchanged.

Source branch/loop/effect positives, initial-undefined observation, different/written
root and source-local/shadow/mapped-argument negatives, independent execution/mutants,
origin/expression oracles and layout/budget/scaling tests cover the path. Opaque roots
for arbitrary local/catch/captured values, per-occurrence symbolic precision, final
namespace allocation and global convergence remain documented implementation gaps.


### rules243: read values consume canonical storage slots

- Removed duplicate Storage interpretation and original-symbol-first read lookup
  from RecoveryFlow/BindingInitializers. One typed BindingProjection composes
  committed stages; the number of stages is a planner contract, not a bytecode or
  graph-shape admission. No new recovery candidate condition is introduced.
- Common current slot references, including every original member destination,
  retain the existing scope/name proof. A read substitution does not change
  assignment or update identity, delete RHS effects, or prove initialization.
- Shared repeated branch values and effectful copies recover direct reads; written
  parameter and inner shadow counterexamples retain necessary storage. Reversed
  arenas may choose a different valid retained representative; tests require
  equivalent behavior and the same necessary variable count. Existing regression
  expectations remain unchanged. Independent execution/mutants and 200/201/
  geometric/exact-short cases exercise production composition.
- The existing ReadValues stage excludes later alias/storage/motion mutation;
  first-commit fact invalidation and owned proposals are unchanged. Arbitrary
  symbolic local/catch/capture values and common global convergence remain gaps.


### rules244: current normal-result facts replace direct-read copy inference

- Current production RecoveryFlow consumes the shared exact-revision normal binding
  results. Assignment/initializer copy facts no longer require direct SymbolExpr
  RHS or a normalized chain ending at that shape. Explicit unknown results stay
  unknown; a second syntax inference does not authorize a copy.
- Interference still requires equal current source value at a successful commit,
  then all live conflicts, initialization/completion/name/lifetime proofs. Prefix
  results can agree with current storage; postfix old values cannot be guessed as
  the current source. Shared storage does not remove effectful RHS evaluation.
- Hypothetical motion and legacy projections without normal facts retain the old
  direct-read fallback only as a documented precision gap. They do not consume a
  current vector belonging to another projection. Extend this with common trial
  value analysis, never more local producer matches.
- Positive source composition, distinct live values/mixed roots/postfix/catch
  negatives, native declarations, independent execution/mutants and 200/201/
  geometric/reversed/exact-short tests verify semantics and output quality.
  Dead inner writes are not artificially retained for a test; observation makes
  the retained-write copy contract concrete. Overall migration stays incomplete.

- One existing scoped-copy assertion required all original upstream aliases to stay
  absent. Composed affinity legitimately retires an inert condition, removing the
  reference that originally demanded one private copy. Updated checks preserve the
  actually retained TDZ-shadowed value binding and add a mixed-arm counterexample
  retaining both upstream copies. Independent truthy/falsy value identity/error
  comparisons and shadow mutants cover both arena orders; no production restriction
  was added to preserve the old representation.


### rules245: contextual read values consume shared invocation paths

- BindingValues consumes common per-occurrence symbolic facts, independently of
  all-definition invariants; no assignment/branch/catch producer match decides it.
  Successful definition, completed read and immutable root proofs precede source
  scope/name queries. Each invariant/occurrence proposal must pass independently.
- Shared liveness publishes invocation reachability once. Contextual joins ignore
  predecessors with no invocation path; conservative liveness/global stability and
  lexical checks still cover retained syntax. Abrupt catch normal exits exposed this
  missing fact, not a catch-specific admission. Snapshot motion's current path exits
  its own reachability traversal and consumes the shared mask.
- Common BindingProjection interprets actual read occurrence before slot invariant;
  flow, expression analysis, class names and printing consume it. Writes/update
  halves retain storage. Independent cleanup remains the deletion owner.
- Tests cover mixed histories/live undefined/effectful failure, written/mapped/shadow/
  local negatives, independent graph reach/value oracles and output execution/mutants.
  Forced merges can become equivalent after source recovery; test the original and
  recovered programs and require an observed counterexample instead of forcing the
  old snapshot spelling to survive. No production shape restriction was added.
- Unmodeled local/capture/catch grounding, original IR/SSA/full activation/finally,
  trial projection precision and common global convergence remain declared gaps.
### Rules246: initialization proof revision ownership

Initialized-read evidence owns the source projection revision. Every consumer
requires the current analysis revision; first commit transfers or revokes it
through the typed pass preservation contract. These checks prove internal fact
ownership and prevent stale analysis reuse; they do not admit/reject recovery
based on opcode, sample, block, adjacency or graph layout. The unversioned cache
and bare preservation-boolean decision path are retired. Existing initialization,
scope, effect and lifetime obligations remain; no source precision is claimed
from the metadata change. Independent phase/stale/future/no-op/batch/pass-category
and geometric protocol counterexamples complement actual generated-source tests.


### Rules247: mutable-entry reads use common available copies

- ReadValues may replace a completed private read when common gen/kill facts prove
  it equals the current entry cell on every invocation path. Global immutability
  remains a separate proof; a later write no longer rejects an earlier safe read.
- Normal-result copy definitions generate relations; every successful source write
  and unknown destination write kills them. Exception edges bypass failed stores.
  No assignment/update/branch/loop opcode sequence or producer layout is matched.
- Entry-cell adapter evidence, initialization and actual scope/name obligations
  remain independent. Arbitrary local/catch activation is explicitly unmodeled;
  scope membership alone is not a substitute for cell identity across reentry.
- Exact-revision copies live in ProjectionFacts and are declared pass inputs.
  Owned occurrence proposals precede first-commit invalidation. Cleanup owns
  writes/effects/declarations; the printer only consumes the committed plan.
- Independent concrete graph exploration, reverse arena/geometric layouts,
  source/unknown-write semantic counterexamples and generated-source execution
  validate the change. The added test's initial direct-callee assumption missed
  the existing `(0, report)` value-call representation; AST target resolution was
  corrected without changing production output or admission.


### Rules248: private copy chains use semantic dependencies

- Normal copy facts query current source state at successful commit through common
  sparse liveness. Queries are not actual source reads or observable demand roots.
- Transitive generation inherits entry equality through private cells; exposed or
  unmodeled roots stay unknown. Root writes kill, intermediate writes change only
  their own state, and failed RHS paths skip commits. No producer sequence, same
  block, adjacency, copy depth, graph shape or selection-count admission appears.
- Query/result/proposal ownership uses the existing exact projection revision;
  no additional pass round or backward stage is introduced. Names, initialization,
  effects, scope and deletion retain independent owners.
- Independent two-cell graph states, split edges/reversed arenas/geometric chains,
  output execution and semantic mutants validate the change. Wrong sharing must
  join two values that really coexist: when a proved read replacement deletes an
  intermediate cell, tests use the original program for that counterexample and
  actual output for wrong entry replacement/read deletion. Production need not
  retain an unused declaration to make a mutant differ.


### 规则249：控制结构清理消费共同语句观测

旧PlanInertStatements仅重判ExpressionStmt/declaration_values，已退出决策。
新的kStatements基于同一当前flow的效果、成功定义、暂停和builder-owned
完成/progress/ownership汇总。每个非body语句使用同一Inert义务；body仅为
必需语法容器。不存在empty-if、同块、邻接、分支数或producer形状准入。
retained定义保护初始化/绑定生命期；不得由private write effect绕过独立
需求删除。loop可能不终止、finally保守完成为显式分析精度缺口。
独立oracle反向布局和源码反例验证，打印器只消费计划/保留来源边界。
两项历史快照形状断言随合法纯读取消失而更新；保留独立错误TDZ移动及
实际输出更新/返回删除反例，禁止在生产为维持旧断言保留无用控制。


### 规则250：有限正常区域复用共同完成图证据

旧无条件repetition/abrupt-jump保守标记在有共同SCC/normal-exit证据时退出。
normal continuation和repeat point由source语言adapter发布，所有完成边参与
SCC，local jump按目标点与ownership出口匹配；不是break布局、循环body为空、
同块/邻接/PC/producer名单准入。nearest exit索引与差分累计一次完成，不新增
pass整轮/回退阶段或每区域图扫描。return/throw/resume/cleanup由边穷尽汇总，
缺元数据和复杂finally仍保守。普通有限循环与反向/拆边/组合反例共同验证；
保留defs/init/lifetime和effects归属独立。Scope可见性不足以证明local activation
root，本轮未以它扩展复制根准入；该能力缺口继续保留。


### 2026-10-09 rules251: mandatory entry evaluation

Effectful conditions with inert owned regions previously retained mechanical control
syntax. StatementValues now consumes common mandatory entry/residual facts, preserving
the original evaluation and removing only proven inert residual control. It does not
match empty-if, fixed body shape, opcode, block adjacency or producer lists. Adapter
entry provenance states execution/scope/handler ownership once; complete child effects,
definitions, abrupt/suspend/cycle and cleanup obligations stay independent. Required
entry evaluation is not deleted by whole-region inertness. Legacy unsupported branch
program synthesis/fullfinally remains a capability gap, not a new admission pattern.
See status.md and unified-variables.md for implementation and selected evidence.


### 2026-10-09 rules252: original normal values and checked occurrences

Normal-domain recovery consumes ValueKinds from the already checked SSA/result
and operand correspondence. It never selects recovery from opcode names, source
spelling, PC, block adjacency, snapshot producer kinds or selection counts. The
exhaustive typed source operator table proves absence of primitive coercion hooks
and language exceptions; initialization/GetValue, child effects, resource contract
and binding identity remain separate. Original raw effects are not blindly treated
as the normalized source event's effects. Original input absence is conservative,
not a new strict publication wrapper. Canonical tree changes replace correspondence.
Tests permute SSA IDs and expression/statement arenas, introduce copies and split
blocks; unknown-value coercion and removed-conversion execution mutants reject
false elimination. Full reference/phase projection and binding activations stay
open; no new local admission or pass round is introduced.

## Rules253: public property reference phases

Semantic obligations: retain base/key values once; acquire Reference before RHS;
receiver check before key conversion/Get/Set; source result commits only after
successful access; preserve exceptional handler/cleanup and current owner.
The common source adapter publishes these phases for every legal MemberExpr/
MemberTarget, including nested/projected positions. No sample, layout, block,
adjacency or sequence admission is added. Shared consumers see the graph rather
than reconstructing phases. Primitive domain refinements are typed language
facts; original input ranges preserve provenance but do not prove cell identity
or a one-to-one raw effect projection. Old generic children/single-effect path
exits member/target handling. ECMA-2622024 early-key assumption was found invalid
for current source by independent execution and corrected to2025; its initial
RED result is not valid evidence. Full original profile/normalized phase mapping
remains open. See source-property-references.md for prerequisites and tests.


规则255主干迁移：原始operand域按原ExprId索引并不证明当前源码slot的
读取域。该独立旧消费退出决策路径，正常值域由共同成功定义、all-read
活跃事实、分支/循环/异常边及源码正常结果方程产生。原始参数域在已校验
参数对应上进入入口定义；不是新的生产者白名单、同块或初始化邻接条件。
成功写入与抛异常路径分离；无法确定的值及外部cell保持未知，域证明不授权
删除生命期/初始化/效果。当前与试算同入口，外部旧域span被拒绝。独立
小图、布局变换、实际源码执行及错误写回/删初始值/删转换反例见status。
完整原始effect/protocol与local/catch activation仍为实现缺口，未新增局部
模式弥补，也未把这些缺口说成语言不变量。五项主干尚未关闭。


## Rules256: checked source commits and conditional original domains

Obligation: the current successful source write still denotes the original checked
result/contribution, and its operand and execution dependencies retain that proof.
TreeFlow snapshot/edge/stage/catch owners publish correspondence once. Shared
normal analysis invalidates it through current values and all completion edges;
source-plan statement deltas include omitted linked boundaries. Missing boundary
correspondence disables raw path precision and preserves source-value solving;
it is not a graph-size admission condition. Actual SSA IDs
remain static provenance, never dynamic value equality or binding-cell identity.

No sample/producer whitelist, same-block, adjacency, copy count or fixed graph
admission is added. Missing correspondence/environment/activation precision uses
current conservative source semantics. Raw occurrence hints remain provenance;
the old direct hint consumer remains outside decisions. The stricter checked
success domain is a precision source, not authority to remove initialization,
effects or binding lifetime. No new pass order/round or manual cache preservation.
Finite-domain fixtures permute raw IDs, add copies and split blocks; producer and
path edits must invalidate the domain. Existing arena/normal/exception/loop and
independent source execution tests remain in the bounded scope. Full original
protocol/effect mapping, local/catch activation, complete trial deltas and global
convergence are still open; the five backbone deliveries are not closed here.


## Rules257 canonical binding activation boundary

CatchClause now emits InitializeBinding after its handler EnterScope; the former
pre-activation definition order is removed for every handler. Scope lifetimes come
from verified syntax ownership and language allocation/departure events, never
sample names, opcode sequences, adjacency, producer lists or block equality.
ScopeId identifies an allocation site whose visits create fresh units; it does not
prove captured-cell or value equality across visits.

Shared gen/kill replaces crossing canonical enter/normal-leave boundaries in
liveness/demand, reaching definitions, coverage, constants, copies and normal
values. Coverage retains the unavailable boundary source instead of inventing an
all-read proof. The common initialization provider checks allocation-before-init;
its completion result cannot independently authorize a lifetime move. Reversed
physical event order, independent execution-state counterexamples and copy/block
source variants exercise the obligations. No optimizer-local sequence condition
or new global pass round is introduced.

Historical rules164 same-ScopeId capture assumptions are not superseded by this
component: complete current projected activation/capture equivalence is still an
open migration obligation. Abrupt unwind and native for per-iteration environments
also remain explicit model gaps. Do not use this canonical allocation index as a
second same-scope admission rule or expand older local checks around new examples.


## Rules258: canonical abrupt departure and unknown transfer

Obligation: a binding value read still belongs to an active canonical allocation,
and a value does not cross a definite departure/reallocation. Shared typed event
contexts and one lexical forest index prove this on every completion edge. Current
projected owners supply expression contexts; omitted blocks allocate no environment.
No sample, opcode sequence, same-block, adjacency, selection count, physical scope
numbering or jump-layout admission is introduced.

Retired decisions: point-only reset propagation in liveness/demand and reaching
definitions, and initialization dominance certifying reads after canonical departure.
Coverage/constants/copies/normal domains consume the same edge-transfer facts rather
than adding optimizer-specific scope predicates. Unknown cleanup preserves may-demand
and definitions while withholding substitution proofs; it is not silently classified
as ended. Completion/value-only inputs without unit metadata remain valid for their
old demand/order contracts and receive no cell identity certificate.

Independent ancestry forests, adverse storage proposals across break/throw, retained
facts after graph destruction, unknown-transfer counterexamples, copy/block/source
execution and production mutants exercise these obligations. Existing capture/name
safeguards stay in force. Historical same-ScopeId capture assumptions have not been
replaced; static containment is no proof of captured-cell identity across visits.
Declaration narrowing/native-for lifetime deltas and precise finally/closure contexts
remain explicit gaps. All five backbone delivery loops remain open; no new pass round
or paused verification expansion occurs.


## Rules259: projected declaration allocation ownership

Obligation: current source declaration allocation denotes its current lexical unit,
rather than the original binding's wider scope. The shared lexical census publishes
explicit region allocation provenance; current/trial flow consumes it once per
binding. A ProjectedBindingScope no longer defaults to original Symbol.scope lifetime.
Name containment, successful initialization, value transfer and capture exposure stay
independent. Canonical original declarations retain their own proven allocations.

Missing block allocation precision is unmodeled, preserving may-demand while
withholding initialization/substitution proof. Native-loop regions retain their
preexisting completion/value contract explicitly as LegacyLoopAllocation, without
canonical/dynamic cell identity. Placement/confinement/capture guards are unchanged;
this is retained legacy capability, not a new loop/update admission expansion. No position or region-ID cast supplies
an allocation, and no same-block/adjacency/sample/opcode/selection rule is added.
Independent legal-original/adverse-projection examples, positive current-block reads,
outside reads, reverse region/scope numbering, exact/short admission and generated
narrowed-source execution/scaling test the obligation. Original-unit-fallback and
missing-correspondence production mutants fail. Complete native-for per-iteration
copy/capture and finally lifetime deltas remain open; historical capture restrictions
are not replaced. All five backbone deliveries remain open.


## Rules260: actual loop entry declaration ownership

Obligation: declaration/effect ownership in the shared model must describe the
actual native for declaration suffix and its outlined expression prefix. A complete
immutable grammar product, distinct from changed commit actions, supplies that
ownership to current/trial preparation and the final grammar consumer. Retained
declarations and current omission are source grammar facts, not opcode, operation
whitelist, adjacency, same-block, fixed selection count or compiler-layout gates.

Retired paths: treating every prospective initializer root as a loop declaration
allocation; publishing InitializeBinding for a demoted prefix assignment; resolving
prefix reads against the prospective loop source region. Actual enclosing positions
are indexed in the shared lexical traversal and used by the common adapter, including
descendants of transparent blocks. A missing position stays unmodeled. Native-loop
value capability remains explicit and grants no dynamic cell identity. No original
outer-unit fallback is restored for an actual projected declaration.

Independent common-flow checks require the prefix binding to resolve after the loop,
a future native loop binding to be unavailable in the outlined prefix, and prefix
assignment/native initialization definition kinds to stay distinct. Reversed source
region IDs, current/trial omission, no-op grammar products and exact/short scaling
checks accompany actual emitted-source gates. Four production mutants exercise
prospective prefix scope, initialization, binding identity and delta-only facts.
Full environment allocation/copy phases and closure/finally precision remain open;
old capture/confinement prerequisites are unchanged. All five backbone loops remain
open and no paused suite/corpus/resource/agent scope resumes.

Additional cross-pass obligation: deleting a copied slot must not silently relocate
an earlier outlined binding into the final native declaration suffix. Placement
uses the complete staged initializer proposal and the same grammar provider before
any commit. Prefix candidates retain their original declaration/assignment; future
omission cannot reintroduce their loop initializer annotation. The owned proposal
mask is grammar output, not a same-block, operation-sequence or numeric admission.
The initial adapter-only implementation violated this obligation; its generated
source and independent ReferenceError/trace RED are preserved. Corrected source
passes the original behavior reference and deliberate late-prefix-promotion fails.
The old unit requirement to retain a copied slot is replaced by scope-correct
source representation and independent execution, never by weakening the reference.


### Rules261 native initial allocation

Obligation: all actual native header BoundNames are allocated before any RHS,
and declaration completion must follow allocation. The complete grammar provider
owns retained group entries/member identities; the common current/trial adapter
and flow initialization/gen-kill analyses consume them. No same-block, adjacency,
opcode/update whitelist, count threshold or graph-shape admission is added.

An allocation entry is the first retained declarator in the language declaration
evaluation order, not a CFG/arena-neighbor match. Omitted roots cannot allocate;
nonmember names in the same lexical region do not acquire its proof. The preliminary
region-wide native allocation publication failed the peer-binding counterexample
and was replaced by actual binding membership before final validation.

Canonical and native allocations use the common completion-order check. Native
entry now resets incoming values in all shared clients; the original outer
allocation cannot certify a newly declared header binding. Legacy loop scalar
capability still provides no copied-unit/captured-cell certificate. Complete
iteration/capture/finally ownership remains a documented implementation gap.


### Rules262 source loop completion graph

Obligation: source condition/increment are outside body activation; the continuing
arm executes once inside it; normal/continue reaches increment and break/return/
throw leaves through the proper completion route. FlowBuilder consumes the same
immutable completion program as printing. Current/trial/bootstrap/refinement use
that input, and common scopes/dominance/definitions/effects consume actual phases.
The raw body-scope graph no longer decides phase placement on modeled loops.

Range bounds and owned transfer-target checks validate the existing typed program,
not an additional recovery admission. No operation whitelist, same-block,
adjacency, selection count, register/PC or sample condition is added. The old
grammar expressibility/bound/labeled-block gaps remain unchanged and documented
as producer capability gaps. Phase events alone grant no copied-cell identity.
Captures, precise finally and iteration copy transport remain mainline work; all
five backbone loops stay open. Model mutations reject scope misplacement, skipped
increment, duplicated continuing arms and update execution in both phases.

The final representation also publishes typed required grammar positions, separate
from effects. Inert elision consumes the shared role for all mandatory statement
slots; its old callable-root exception is removed. This does not impose a new
block-layout admission: the actual source grammar requires the position even
when no computation remains. Required slots carry no fabricated effect that
would retain their ancestors. The initial phase-only graph omitted the empty
body yet printing emitted its moved predicate; existing source range checks
rejected the conflict. No range/reference assertions were weakened.

### Rules263 native copied scalar inputs

Obligation: the first condition observes copied entry values; normal/continue
copies the previous iteration values before increment; abrupt exit skips next
copy. Actual BoundNames and the already shared source completion program supply
membership and routes. All old inputs are retained before scalar definitions;
common liveness/demand/value/type consumers use the explicit captured occurrence.
The old current-binding-at-commit assumption is not used for these implicit copies.

No source name, sample, opcode sequence, same-block, adjacency, fixed shape/count,
region-peer membership or register layout admission is added. Owned member/phase
and input bounds validate semantic adapters. Existing grammar coverage restrictions
remain producer gaps. Current/trial share the producer through the same flow fact
and projection revision. A captured earlier value cannot be turned into a current
mutable-root alias without the common effect/write proof; precision remains
conservative rather than adding a local safe-shape branch.

Value transport is not dynamic cell identity. Native bindings still have the
explicit completion-only iteration capability; actual fresh units, reset-aware
initialization and captured-cell proofs must be delivered together next. All five
backbone loops stay OPEN. Component counterexamples/mutants do not substitute for
original IR correspondence, full lifetime trials, convergence or final acceptance.


### 2026-10-10: rules264, unit resets and completion leave local dominance

Obligation: a reachable read or proposed storage action needs successful
initialization in its CURRENT unit, through every completion path. One static
lexical declaration dominating a read does not prove this after reentry/reset.
The old FlowInitializations::Completed dominance-plus-local-scope decision is
removed. The common reset-aware must solver owns this fact; retained declaration
anchors own source placement only. Storage candidates explicitly request common
facts for their representatives instead of inferring completion independently.

Actual native first/repeat programs consume BoundNames, captured old inputs and
source phase ownership. Typed replacement/normal-exit events and the shared
native forest define resets and ancestry. Unknown contexts retain may demand
and definitions; they do not masquerade as unit exits. Conservative cleanup
only guarantees the function-body ancestry, with nested units unknown. Implicit
scalar epochs are not captured-cell equality or VM heap-allocation proofs.

The dominating-gen/no-unit-kill certificate is a sufficient theorem for the SAME
must equations. Uncertified states use their full solver. Heavy paths, offline
Fenwick queries and straight basic-block contraction control analysis costs;
none admits source based on adjacency, same-block placement, sample identity,
operation lists, fixed selection counts or compiler graph shapes. No resource
budget increase or per-binding whole-graph walk is introduced.

Remaining gaps: source loop programs not represented by legacy LoopSyntax retain
CompletionOnly precision. Native source entry/movement corridors are partially
unknown; complex cleanup, captured units, complete trial deltas and IR/SSA
correspondence are not closed. Nested native model tests supply an outer phase
program explicitly, rather than claiming syntax-planner recovery. Eight production
mutants distinguish missing replacement/init, old dominance, ignored unit cuts,
unknown transport, cleanup descendants, lost native parent and false native exit.
Independent oracle and bounded source/record evidence are recorded in status.md.
All five mainline deliverables remain OPEN; other optimizations remain paused.

### Rules265 declaration trial authority

Retired: independently supplied replacement/transport arrays in expression input;
optional initializer/omission fallbacks in grammar; BuildProjectedFlow borrowing
current initializer annotations or declaration sites implicitly; native old
declaration retirement deferred to final grammar. Apply now takes all semantic
plan facets from the one candidate. No mixed bootstrap/current initialization
proof survives the declaration trial.

Obligations: first successful assignment must dominate retained references and
remain in a valid lexical extent; the actual proposed graph must initialize each
ordinary moved read in its current unit; source lookup must resolve moved bindings
and preserve existing resolved references/free names. The common flow, gen/kill
and source-name analyses own these obligations. Initial placement anchors,
captured-cell legacy admission and grammar suffix representation limitations
remain explicitly transitional, not newly expanded shape patterns. Unknown
ordinary-unit initialization retains the entire original batch. Validating a
subset requires its own common trial, not a selection-count or block-layout rule.

Snapshot commit retirement and component retention still need complete candidate
representation; this component does not certify every motion proposal or all
ordinary/catch/reentry units. All five backbone deliveries remain OPEN.

The 800-pair frozen source failure was typed resource exhaustion, not a new
semantic rejection. A pass-boundary RetainFacts policy consumes RequiredFacts
dependency closure and destroys unconsumed derived owners before admission
release. No size threshold, sample dispatch, budget increase or weakened proof
was introduced. The common bootstrap pair remains available; an evicted derived
fact can be recomputed at the same revision. Dependency-driven fixed-point
convergence remains a separate OPEN delivery.


### Rules266: full snapshot transaction and common restoration closure

ForwardSnapshots no longer checks names against a partial replacement/transport/
omission trial then separately retires declarations during commit. Complete owned
candidate facets now feed the same producer and Publish transfers those exact
facets. The old invalid/blocked selector loop is replaced by PlanSnapshotRetention
and indexed SourceNameRetirement, including restored shadows and nested transport
component dependencies. Actual selected names are verified on the final full
transaction; a failed invariant is typed, never a second shape-based admission.

Read coverage, discarded result, bidirectional observation count, successful
private PutValue and exceptional route identity retain their documented semantic
obligations. Existing captured-environment/resource census remains transitional
and is not expanded. New namespace-graph tests separate restoration from motion
eligibility; source tests cover combined behavior. No sample/layout, opcode list,
same-block, adjacency, or fixed selection-count branch was introduced. Raw IR,
ordinary units, other candidate consumers and convergence remain mainline work.


### Rules267: original inline range proof validity

Inline original ranges now use actual source evaluation entry and normal completion
plus shared operand/completion dependencies. Live-result and correspondence checks
state ownership/availability obligations; they do not require a source temporary,
same block, adjacency, producer list or fixed graph layout. No raw opcode variant
is interpreted again, and raw effect flags do not bypass current language effects.
Changed projections invalidate through the common graph, including deleted intrinsic
events. Source arenas are re-verified after permutation, not merely relabelled on
stale annotations. Missing intervals retain conservative current domains.

Three production mutants disable inline consumption, its path dependency or
missing-expression-boundary fallback and fail
the semantic checks. Independent source execution covers unchanged normal results
and altered comparison behavior. The full phase/exception correspondence, ordinary
units, remaining trials and global pass convergence remain OPEN mainline work.


### Rules268: catch payloads and observable scoped state

Catch correspondence comes from the verified TreeFlow, not a matched opcode
sequence or handler layout. Entry/normal completion owns an explicit thrown value;
implicit failure cannot borrow that operation's normal value. Shared joins,
scoped unit transfers and required evaluation invalidation prove current domains.
Original catch ownership and subset checks preserve validity; there is no extra
same-block, adjacency, sample/name or fixed graph-size admission. Write
observability stays independent. Initialization refinement precedes its liveness/
domain consumers and cannot mutate the recovery plan. Full raw phase/action and
closure/finally precision remain implementation gaps, not language restrictions.


### Rules269: effects and write observability are independent facts

The original exhaustive source semantic table owns typed normal/before-throw
effects; no second visitor or production opcode pattern decides mutation. The
normal-value solver consumes UserCode on the actual completion edge, alongside
existing successful definitions and unit transfers. Required allocation, guards
and declarative writes are not callback evidence. Their retention, completion,
identity and scope obligations remain independent. Unknown/unmodeled paths stay
conservative; no sample/name, same-block, adjacency, fixed graph or size admission
was added. Raw phase/protocol correspondence remains an implementation gap.
