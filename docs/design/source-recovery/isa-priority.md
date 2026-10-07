# 全 ISA 优先执行顺序

2026-10-07 rules163保持：四个shopping ViewModel的真实十record连接
及独立公开方法/构造表示验收完成，records28887/29119/29382/29619
受控whole-record语义/style由pending转accepted。22正常/2560原异常/
416构造assignment/八mutation/四等价表示通过，旧model/ViewModel证据
保留，生产覆盖数量无变化。当前显式语言模式的历史106/0不是全阶段
验收；继续其余record、fields/globals/signature/body原gate与通用SSA/
每轮capture，Goal active及暂停范围保持。范围与证据见status.md。

2026-10-07规则163：同一postorder完成/初始化分析消除pure comma left，
typed operator语义穷尽解释，GetValue/typeof/receiver/NamedEvaluation
保持，无样本/布局条件。1706完整源/457128独立行为/270 mutation及
1296命名场景/54 name mutation通过；定向29/30后修复测试解析启动
flag，3/3补验。fresh pasteboard仍10complete，十mjs/声明/comma数与
规则162一致，无新增actual coverage。继续优先剩余普通INCOMPLETE/
UNRECOVERED、结构/signature/body及全paired语义/style；一般default、
精确reaching definitions/SSA/每轮capture与unknown capture/module
仍有缺口，Goal active及暂停范围保持。证据见status.md。

2026-10-07 规则162：共同表达式完成census消除嵌在实参等位置的纯
复制存储，保留原结果/效果和读取；执行区间/词法名字/原生声明打印
生命周期由共同证明覆盖，不添加独立语句或样本形状准入。1490有效
树/1478完整源、435930独立行为/252 mutation，72三profile实际cell
解码全部共享Local。最终生产30/30定向147.90秒，||/??绑定2/2补测
35.71秒，证据见status.md。
fresh pasteboard仍10complete/0partial，十mjs/声明数与规则161一致，
无新增actual coverage。下一步共同pure sequence投影、精确定义/一般
SSA及原变量/每轮capture，继续优先普通INCOMPLETE/UNRECOVERED；未知
capture/module、全body/style、原gate和暂停范围保持。

2026-10-07 规则161：声明完成及保留source首次store复用共同不可变
复制证明，原let/const/初始化/赋值位置不动，不回放RHS。1226有效树/
1214完整源、376758独立行为/224 mutation通过，包括72三profile实际
lexical cell解码，36成员写入验证共享Local。首次定向30项29通过/1
旧源码const断言失败，修复后binding 2/2通过，其余29项最终生产通过。
fresh pasteboard仍10complete/0partial，十mjs/声明数与规则160一致，
无新增actual coverage。下一步实参嵌套赋值的共同完成/观察投影、一般
SSA干涉/原变量/每轮capture，优先普通INCOMPLETE/UNRECOVERED；未知
capture/module、全body/style、原gate及暂停范围保持。证据见status.md。

2026-10-07 规则160：共同复制图加入无写/无escape的隐式入口Parameter、
CatchParameter和FunctionSelf根；typed语言调用语义保护mapped
arguments，target沿共同首次store/生命周期及词法解析恢复，无布局/
样本匹配。966有效树/954完整源、335362独立行为/166 mutation，最终
30/30定向137.87秒；0/1/200/201/400/800与正反arena、旧值/暂停/cell/
receiver通过。fresh pasteboard仍10complete/0partial，四源七个catch
复制消除，总声明147→140；无新增record coverage。下一步真实source
初始化/不可变性、capture/module及一般SSA干涉/原变量/每轮capture，
全body/style、原gate及暂停范围保持；实际证据见status.md。

2026-10-07 规则159：共同不可变/生命周期证明和TreeScopeCheck批量词法
解析使私有复制共用保留producer存储，分配/效果/暂停不回放。768有效
树/756完整源，328132独立行为/146 mutation，最终30/30定向132.26秒。
任意复制链、正反symbol/scope/AST、深作用域及保留级联/旧值保持；
fresh pasteboard10complete/0partial及十源解析保持，实际源码/声明数
与规则158相同，无新增actual coverage。下一步隐式入口绑定及一般
SSA干涉/真实变量/每轮capture；全body/style、原gate及暂停范围保持。

2026-10-07 规则158：已证明不变的私有原始值和任意复制链由共同绑定
证明/有界依赖worklist传播，不添加样本/布局/opcode列表条件。512完整
源码317848独立行为/124 mutation，mutable/captured/旧值和对象分配身份
保持，最终30/30定向通过122.88秒；fresh pasteboard常量种子直接false/0，
空声明19→16、总声明66→63，
三个for let与10complete/0partial保持。一般alias/SSA干涉、原变量与
每轮capture、全部body/style及结构gate仍开放，Goal active、暂停范围
保持，构建/定向及实际证据见status.md。

2026-10-07 规则157：私有loop进入绑定按共同首次store/全部观察包含
证明恢复native for let，前序效果/handler/label和真实capture保持。
276完整源码13892独立行为/56 mutation、最终30/30定向109.71秒，
新结构检查顺序另轮2/2通过；fresh pasteboard三个for均有let，空声明
22→19，初始化44→47，10complete/0partial保持。未合并SSA/phi/cell、
猜原变量或恢复用户每轮capture，全body/style及阶段一仍开放。
原结构gate和暂停范围保持，具体gap/probe/actual证据见status.md。

2026-10-07 规则156：按共同condition/body打印归属恢复原生循环体的私有
初始化，取消继承的条件表达式context，全部旧值/更新/capture义务保持。
300完整源码308180行为/92 mutation、最终27/27定向通过；fresh实际
pasteboard空声明39→22、初始化27→44，10complete/0partial及三个for保持。
继续一般SSA存储与实际变量绑定、for let/每轮capture和全body/style；
原结构gate、阶段一Goal与暂停范围保持，证据见status.md。

2026-10-07 规则155：私有初始化以共同词法子树区间和全部观察位置证明
作用域包含，去掉空声明/赋值同容器限制，复用loop/nullish打印owner
保护for更新读取。212源码25940独立行为/56 mutation、最终25/25及
optional另轮2/2通过；fresh pasteboard空声明50→39，10complete/0partial
及三个for保持。一般SSA存储、真实变量及每轮capture、全部body/style
仍未完成，原结构gate、阶段一Goal和暂停范围保持，证据见status.md。

2026-10-07 规则154：私有结果的初始化声明进入共同printer，按native
词法归属/顺序支配、全ownership读写和capture census保留原位置语义，
不按opcode/CFG同块/样本或复制数量选择，不合并SSA/cell/formal。
144源码最终20820行为/18 mutation、主运行23/23及补强后2/2通过；actual record对照
保持。fresh pasteboard空声明65→50、初始化1→16、三个for及10complete
保持。后续继续普通INCOMPLETE/UNRECOVERED、一般SSA存储投影、实际
变量归属及native binding/每轮capture；此次不完成原结构/全style gate。
阶段一Goal和暂停范围保持，完整证据见status.md。

2026-10-07 规则153：普通调用的条件参数复制已按共同owner证明恢复，
去掉root-placement额外限制，值/Reference/receiver、效果及异常义务
保持，不扩展局部形状或样本匹配。十个实际构造record完整输出且
2450 paired/120 native super/三mutation通过；Push322 lifecycle、
1860 delivery和八connected场景另验通过。fresh Push15complete、
pasteboard10complete，均0partial；旧writer variant断言修正另验2/2。
主运行25项24通过/1旧断言失败，453.55秒，其余24通过及修正后2/2
分别保留；joint源码286848/解码435和phased调用78624场景通过。
下一步继续SSA存储投影、native binding/每轮捕获和一般继续程序，
优先普通INCOMPLETE/UNRECOVERED覆盖。默认模式、阶段一Goal、原结构
gate及暂停范围保持；全body/style仍未完成，完整证据见status.md。

2026-10-07 规则152：numeric update复用自身私有结果snapshot，原输入/
phi/cell分离和ToNumeric旧值publication保持。24新增任意值解码与
原108解码/四语法树共3584行为场景通过；22定向实测21通过/1失败。
构造器paired阻挡在普通arrow的retained callee，关闭本次复用仍失败；
下一步优先共同Reference/value/effect/exception调用表示，不补样本匹配。
198 decoded构造器通过，当前十record配对未完成。fresh pasteboard
update私有变量3→1，19 records/10complete及三个for保持。原结构gate、
阶段一Goal和暂停范围保持；完整证据见status.md。

2026-10-07 规则151：共同进入程序、normal完成代数与SymbolId写入索引
恢复for表达式初始化，支持跨透明block且保留所有中间效果、原scope/
handler/来源，不新增样本/布局/固定数量匹配。144真实三profile解码
4176独立比较/504有效错误mutation，156独立树10372比较/32 mutation
通过；包含801深块、800符号定义链、TDZ/handler、只读边界、await拒绝、
初始化yield的throw/return。最终22项定向22/22，完整证据见status.md。

fresh pasteboard19 records，10complete/0partial/9noModule；三个实际for
均有initializer，两个有update，第三个更新仍在原continue路径。私有
$value49/$phi10保留；for let/每轮binding、一般继续程序、SSA更新及完整
body/style尚未完成。原gate和阶段一Goal保持active；actual受控平台
对照不代替设备SDK，固定五ABC106/0未重跑，暂停范围保持。

2026-10-07 规则150：共同完成摘要/continue目标覆盖、条件绑定读取与
唯一statement定义依赖恢复for更新程序，原canonical AST/来源保持。
不匹配++、opcode/寄存器/PC、样本/名字、固定guard/COPY数量或局部序列。
108个scope/handler/条件更新/await/yield/arena/200/201/深块树完成2418
独立行为及14有效错误mutation；三profile72 decoded仍2088比较/216
mutation通过。20项定向20/20，最后深块/命名扩展相关七项另轮7/7，
完整失败修正和验证范围见status.md，不以测试范围代替全阶段。

fresh pasteboard仍10complete/0partial/9noModule；两个实际嵌套遍历已有
for(;condition;update)，第三个保持while。初始化、每轮native binding、
分离继续程序/SSA更新和完整body/style仍未完成，原结构gate及阶段一
Goal保持。actual ProgressBar/富文本受控矩阵通过；原五ABC106/0未重跑，
全套/扩展语料/RSS/DFX/agents暂停保持，paired正文不进引擎。

2026-10-07 规则149：移除首次phi ValueCopy因SSA输入而一律保留snapshot
的限制。共同EvaluationLiveness提供typed存储lifetime，phase planner/verifier
复用原availability/unique-use/PhiCopies epoch/effect/catch证明。catch
参数移读与旧corridor的限制仍是表示缺口，没有新增样本或布局匹配。
72个真实三profile locals/copy/split/handler变体、2088独立行为比较通过，
最后216个condition/type/参数identity错误mutation通过。专用lifetime接口
最终18项定向76.80秒通过，包含该216 mutation版本；首轮及独立加强
验收、失败修正分别保留，完整证据见status.md。

fresh pasteboard ABC-only仍10complete/0partial/9noModule；PasteBoard私有
求值声明60→49、phi仍10个，三个有条件while保持。实际ProgressBar/
富文本的完整受控行为矩阵保持通过，默认数值/构造合同保持。native for
初始化/更新、每轮binding、全body/style和阶段一原gate仍开放；固定五
ABC106/0未重跑，暂停的全套/扩展语料/RSS/DFX/agents未恢复。

2026-10-07 规则148：共同LoopSyntax从已验证FunctionTree的正常完成/
owned break目标恢复原生while条件，原表达式/phi/效果/异常、scope与
continue/finally目标保持。36个三profile真实解码copy/register/split与
96个arena/规模/复杂退出/TDZ/finally/await/yield树，独立4472次行为比较
及14错误条件mutation通过；最终15项定向回归70.18秒全部通过。

fresh pasteboard ABC-only导出10complete/0partial/9noModule；实际三个
循环均有原生while条件，富文本与ProgressBar完整受控矩阵保持通过。
原五ABC106/0没有重跑。SSA carrier、for初始化/更新、每轮native binding
及全部body/style未完成；原字段/全局/signature/body与阶段一gate保持。
作用域声明/try等guard表达式的保守边界详见function-tree-output.md及
shape audit；不按opcode、同块、名字、PC/register或固定selection分派。
全部构建、失败修正、原throw/receiver/job/request和暂停项证据见status.md。

2026-10-07 富文本续作：实际PasteBoard与CommonConstants、ShareDirect、
PageA/B连接，原嵌套遍历、async/图片/controller/clipboard/navigation/body
独立行为验收通过，view协议与ProgressBar共用。buffer身份盲点经有效
mutation先RED再修复，生产/rules147保持。具体矩阵见status.md。
表示仍有三个for对应while(true)/phi搬运的缺口，下一步沿共同loop
control/evaluation/binding还原高层循环，禁止样本或局部layout投影；
不以本次行为/计数替代body/style和阶段一原gate。

2026-10-07 ProgressBar续作：沿共同source路径补齐原仅copy callback
验证的实际constructor/模块初始化/render/progress/async/timer/teardown。
真实CommonConstants连接，外部文件/clipboard仅受控协议；Push与本记录
复用native请求追踪，原body不替换。身份/默认参数/动态Proxy父类的独立
证据与旧copy style断言修正见status.md；production/rules147保持。
继续实际record和原fields/globals/signatures/body gate，组件测试通过不
代替完整pasteboard图、平台或阶段一验收；暂停项保持。

2026-10-07 Push组合续作：沿共同ABC-only source路径补齐十五模块真实
页面/Ability/VoIP Service/storage/form/image组合独立验收，七种view实际
render；保留已有单record和Ability+Service验收。SDK父类身份验证盲点
经错误父类mutation先RED再修复；native Promise/derived链完整结算。
最终范围见status.md；production/rules147保持，无样本／布局新条件。
继续原fields/globals/signatures/body gate与其他record语义/表示验收，
本次受控平台和组合通过不代替阶段一或设备完成。

2026-10-07 规则147：既有显式numeric+constructors恢复后的共同高层表示
继续推进。旧member chain在中间调用丢失摘要，由同一postfix path沿原
成员／callee依赖传播取代，支持`x?.f().a`、`fn?.().a`与返回函数继续调用；
首个guard之后保持mandatory及原receiver／参数／异常／来源。detached
member值调用保留原conditional，不能为了外观重绑receiver；不同reference
和未融合alias仍是表示缺口。三profile真实解码及独立行为、定向实际
Window／导航验证范围见status.md；不将本组件或106/0统计代替阶段一。

2026-10-07 Window组合续作：真实WindowUtil／WindowInfo接入三十三模块
导航图与实际storage/child更新，不再只执行模块初始化及controlled window
替身。实际listener/Promise/body及高层class/signature独立验收，原单record
验收保留；alias按真实identity处理。测试区域identity盲点与coverage报告
修正、最终定向结果见status.md；未增加production特例或更改默认合同。

2026-10-07 当前核对：规则146的固定五ABC在显式numeric+constructors组合
已无INCOMPLETE/UNRECOVERED源码标记，默认模式的数值/构造合同保持。
继续优先这些原缺口的完整函数、类、初始化及record独立语义/表示验收，
不以106/0统计、部分注释包装或?.细节替代完成。Content14411新增实际
whole-record/closure/字段/signature验收；本轮将真实minigame barrel及三
child接入十四模块导航图，推进实际跨record constructor/field/capture/body
整合；本轮继续接入真实shopping依赖链，三十三模块图验证Home及其
子类/model/capture/body，沿用户功能优先继续独立验收。来源映射/DFX
仍不作为功能前置；其他记录与原结构gate仍开放。

2026-10-07 后续：继续实际MultiCommunity四十五模块图与二十view的
constructor/field/watch/storage/closure/body组合验收，未新增生产样本、
同块或图形条件。实际render coverage和完整导出alias/descriptor另验；
受控宿主与完整平台/所有body表示仍区分，最终证据见status.md。

2026-10-07 组合续作：同一四十五模块图中真实Ability callback调用原
BreakpointConstants、写共享storage并驱动视图，Logger原rest body也执行。
保留已有单record验收，补齐跨record组合证据；等价captured-this/词法this
表示必须允许，不能通过源码匹配定义恢复。最终范围与证据见status.md。

2026-10-06 当前用户要求：完成正在进行的 `?.` 调用修复后，优先解决
阶段一实际输出中的 `INCOMPLETE / UNRECOVERED` 功能缺口，使未完整恢复
的函数、类、初始化和 record 获得完整反编译及独立验收。后续 optional
chain 等表示细节优化排在这些缺口之后。不得仅改善缺口注释、输出外壳
或增加接口来代替支持缺失语义；通用贯通、无样本定制和语义约束保持。

2026-10-06 本轮授权：新增显式language_numeric_v1，按JS/ArkTS语言数值
语义验收一元及二元运算；默认V3和既有二元模式保持。WindowUtil NEG
缺口已贯通，证据见status及language-numeric.md；剩余普通SuperSpread
构造器继续优先。这个数值前提不授权新的父品牌/host lifetime假设。

2026-10-06 追加授权：独立显式语言构造模式，原生JS/ArkTS super规则
验收。`--value-source --language-constructors`与两个数值selector分别组合；
默认模式不变，不引入父类终生保持JSFunction的host假设。普通SuperSpread
进入同一AST和身份分析，详见[语言构造语义](language-constructors.md)。
构造器组件恢复后继续实例字段/全局/完整签名/函数体整合和整record独立
语义、风格验收；不能把source_complete统计替代这些阶段一门槛。

2026-10-06 规则132：普通/分阶段family命名先查询真实子树的global/module
引用索引，冲突提示与合成名均避让；不以名字/布局定义恢复语义。scope/
捕获/异常/副作用校验保持。共享tree标识符不再受legacy私有namespace
限制，详见[cell命名](cell-names.md)。固定五ABC六模式结果无回退且源码
不变；48 decoded family独立行为/mutation证据不能代替整record验收。

2026-10-06 规则130：普通方法/闭包的命名提示已接入共用syntax resolver，
复用record cell命名与实际allocation/capture分析。完整模式29份源码表示
改善，source_complete数量保持。签名中的真实formal证据、setter-capable
实例赋值与整record独立语义/风格验收仍按原结构gate继续，不先扩展语料。

2026-10-06 规则131：正式移除参数命名的MOV-prefix/固定captured-this序列，
改用debug boundary的真实entry copy/phi与异常路径事实；详见
[参数命名](parameter-names.md)。完整签名仍需真实formal/default/rest证据与
独立验收，实例赋值、全局/闭包及整个record的结构gate不由此单项关闭。

2026-10-03 用户要求：先补 `object creaters` 的全部 26 个编码格式，
使用既有通用贯通路径及公共接口。完整清单、现有条件与缺口见
[对象创建指令](object-creators.md)。这调整立即执行的优先级，不缩减阶段一 Goal。

2026-09-28 补充：按 [功能优先与部分输出设计](function-tree-output.md) 执行。
保留下列 ISA 覆盖目标与顺序，先打通所需控制组合；新增 DFX 不作为功能前置，
未支持区域允许标记后部分输出，不能计为该格式已完整反编译。

2026-09-27 用户最新要求：优先覆盖 pinned OpenHarmony-v7.0-Release
`runtime_core/isa/isa.yaml` 中所有 groups 的反编译，再继续此前目标。
本要求取代“仅优先 LD/ST”的执行顺序，不取消已批准的通用访问设计或原 T00–T12 验收。

## 固定顺序

按当前固定 YAML 的原始标题/顺序，共 19 groups、332 编码格式：

| 顺序 | 原始 group title | 格式数 |
|---|---|---:|
| 1 | constant object loaders | 18 |
| 2 | iterator instructions | 12 |
| 3 | object creaters | 26 |
| 4 | binary operations | 18 |
| 5 | unary operations | 16 |
| 6 | comparation instructions | 4 |
| 7 | call runtime functions | 26 |
| 8 | throw instructions | 12 |
| 9 | call instructions | 31 |
| 10 | definition instuctions | 8 |
| 11 | object visitors | 113 |
| 12 | Load accumulator from string constant pool | 1 |
| 13 | jump operations | 37 |
| 14 | Dynamic move register-to-register | 3 |
| 15 | Dynamic load accumulator from register | 1 |
| 16 | Dynamic store accumulator | 1 |
| 17 | Dynamic load accumulator from immediate | 2 |
| 18 | dynamic return | 2 |
| 19 | no operation | 1 |

计数是格式，不是完成量，也不是历史 profile 的语义支持声明。

## 实施约束

- 先扩展全 ISA group/格式清单，再从 group 1 开始核对和补齐已有能力。
- 每组分别保留 decode、typed semantic lift、value IR、AST、源码及行为验证证据。
- 每组必须有端到端正例及反例；编码测试通过、清单有条目或保守拒绝不等于完成反编译。
- 原因明确的 runtime 差异仍需正确建模/证明，不为形式上的全覆盖输出错误 JS。
- 顺序是任务优先级，不是禁止先实现依赖：iterator/async、throw、closure、模块等
  需要的 CFG、异常相位、环境和恢复 owner 必须及时实现，不能无限等待后续 group。
- 不按组新增 writer；全部进入同一通用 IR、区域树、AST、printer 和 PC 来源链。
- 保留 LD/ST 设计的复杂场景矩阵、多应用/历史 profile、构造器共用 callable 要求。
- 全 groups 功能覆盖完成后，对照此前 LD/ST 与 T00–T12 目标补齐剩余工作和验收；
  不重复开发已完成依赖，不缩减原结构 gate 或完整函数/record 要求。
- 独立实施、不使用 CC/子代理；针对性功能测试继续，定期审视、RSS/基准及调度器仍不启动。
