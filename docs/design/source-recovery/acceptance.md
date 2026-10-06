# 验收、证据与实施交接

2026-10-06 Widget补充：push modules.abc record12347和widgets.abc record564
的受控whole-record语义与高层表示由pending转accepted。两个ABC-only默认V3
导出全部结束、既有paired SHA/size核对后再读取独立WidgetCard.ts；真实模块
prelude/polyfill、storage/class/启动/结束/launch及完整constructor、六个方法、
三对访问器和static方法执行。没有参考body替换、source fragment或平台receipt。

6,392组完整对照包括4,100组实际可达原异常、820组component/subscriber全局
换绑；初始化、访问器、purge/delete/rerender与保留callback/click用foreign this
在删除和注册失败后重放，比较callee/receiver、捕获、字段/cell部分状态、顺序及
原throw身份。finalize缺失/继承/不可调用/不可扩展、父对象return、正反队列、
changing getter、image scalar、默认和0/1/200/201等实参边界另验。72组setter/
new.target/部分写入是独立pinned STOBJBYNAME层；Node TS initialized DefineField
不能作此层oracle。24个有效JS mutation检测错误defaults/super/capture/字段/
生命周期/void返回和DefineField。旧176/864/60/6子场景证据继续保留。

表示接受原生class/constructor/super/default/accessor/arrow/member/literal与
必要的private class/storage creation carrier及receiver alias、等价if极性；
不要求逐字恢复，也不把语义等价的arrow this替换视作错误。源码type/private/
const、其他record、完整框架/设备及原结构gate仍须自己的证据。测试曾因累计
VM context触发Node OOM；改为128项一批顺序子进程后全场景通过，inventory/
总数核对不允许漏项。这不是生产RSS/benchmark证明或恢复暂停工作的授权。
最终20/20定向回归与后续Widget复验、失败保留日志及剩余门槛见status.md。


2026-10-06补充：规则142的原生class声明复用共同ClassNames词法证明，
保持可写外层绑定、TDZ、真实runtime name及原创建/heritage求值时机。
const、同名外层binding引用、未知body和未融合snapshot继续原表达式表示。
三profile实际解码的54份完整模块有468组独立行为对照及6个有效JS mutation；
两个主Ability、Backup/Push、三个模型、ViewModel、CardArray/Breakpoint等
原整record受控验收继续通过。固定六模式各54/68/68/62/78/78份源码表示
改善，不增加完整record数量，不替代其余记录或原结构gate。证据见status.md。

状态：历史完整验收规格。2026-09-28 按 [部分输出设计](function-tree-output.md) 调整：
相关定向单元/集成和受控完整生成 JS 行为测试已恢复；全量测试、基准和 RSS 仍暂停。
部分源码允许带标记输出，不要求先取得 Validated/Published 状态；新增预算与发布审计
工作后移。下文完整验收门槛不阻塞部分输出，生产构建或目测仍不代表行为验证通过。
使用 [总体入口](README.md)、[IR 合同](value-ir.md)、[AST 合同](source-tree.md) 与
[实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md) 和 [全样本合同](corpus.md)。

2026-10-06 Boolean贯通补充：规则141的三profile实际解码覆盖Boolean/null
phi、0/200/201/402 COPY拆块与复合null/undefined条件；原sparse值流另验
200/201/402/804和循环SCC。完整生成函数对照真实getter返回、原throw和
状态变化，错误CombinedOrigin/缺失scalar来源/交换arm必须仍拒绝。
精确Boolean只影响透明转换的表示；contextual selection来源不能代替
原predicate执行归属，effects/catch/receiver合同不变。固定五ABC／六模式
30次ABC-only导出与规则140源码/位置映射相同，完整/partial仍80/26、95/11、
96/10、88/18、105/1、106/0。此通用能力不关闭原结构gate或其余record的
独立whole-record验收；默认及显式数值/构造语义合同不变。证据见status.md。

2026-10-06主Ability补充：规则140保持，新增完整pasteboard4767与
MultiCommunity40957 EntryAbility的受控whole-record独立验收，状态由pending
转accepted。生成前按paired index核对ABC SHA/size，ABC-only显式numeric+
constructors完成全部输出后才读paired TS；七个实际prototype method和所有
嵌套callback参与执行。2,710组对照覆盖阈值／coercion、两个实例的正反队列、
注册失败后保留callback、完整事件逐项fault/live import换绑、JSON返回／
原throw及真实catch error accessor的第二次异常；void宿主结果不得返回或
await，callback的foreign this不能改变捕获receiver。16项pinned assignment
另验0/1/200/201参数身份、inherited curBp setter/throw，明确区分Native TS
DefineField oracle边界。13个有效JS mutation与最终16项相关回归通过。
表示允许保守mutable class binding、显式等价constructor、conditional及局部
snapshot；这关闭两个指定record的完整生命周期证据缺口，不能扩为完整
base/platform模块图、其余record、源码type/const或全部结构gate。见status.md。

2026-10-06默认callable补充：规则140的真实三profile解码函数／类，加共同
树的6种callable／4种原名称，独立210组ESM行为及4个有效JS mutation通过。
对照实际生成源码的TDZ／求值阶段创建、重复evaluate身份、运行时name／arity、
构造边界、async／generator／原throw、live import及具名class外层scope；
不能以export文本或解析通过证明hoisting／name正确。owned／missing-name／
partial／capture反例和200/201/400族规模、25项相关回归保持；此前直接
FunctionExpr／ClassExpr的表示限制由共同命名上下文替代，其他原生命周期
义务不放宽。当前固定五ABC／六模式30次输出／位置映射均与规则139基线
相同，完整数量未增加；这是通用能力的独立fixture验收，不能替代其余
实际record的whole-record表示／语义及原结构gate。具体证据见status.md。

2026-10-06 VoIP主流程补充：当前默认ValueTree实际ABC-only生成完整
record11989后，再读取paired TS；输入SHA/size按既有index核对。新增366组
受控完整main/callback/close调度，覆盖两来电的raw/pixel/incoming/event
请求正反释放、共享callId与每请求descriptor身份、暂停时live logger换绑、
原拒绝及catch getter异常、延迟image-info回调、on保留listener后throw导致
主流程失败的原callback重放，以及close丢弃其实际启动promise的边界。
host逐项释放/等待真实操作，禁止参考方法代替原body；4个新增有效JS
mutation检出错误profile/事件/尺寸/off名。原322直接场景、768 event调度
及3 mutation保留，最终9项定向回归通过。比较链、private class alias与
snapshot暂存的整record风格仍需自身验收；这关闭上述并发/回调行为证据
缺口，不能把完整输出或受控host等同平台/全部表示/原结构gate完成。

2026-10-06备份record补充：新增backup_records_source_test先完成三个实际
ABC-only --language-constructors记录生成和既有paired SHA/size核对，再读
配对TS；Immersive9308、pasteboard5246、push6659的完整模块实际初始化、
构造并调用原两个async方法。1,224组构造及4,068组方法场景、13个有效JS
mutation通过，受控whole-record语义与高层表示由pending转accepted。
覆盖动态父类/new.target/两realm fallback/原异常与参数身份，namespace/
class/method元信息，live import/callee替换、JSON/toJSON异常、Promise/
nested thenable/原拒绝身份和并发microtask顺序；不额外读取borrowed receiver
或await日志返回值。表示允许保守let、显式等价constructor及await局部暂存。
原default partial方法测试独立保留；paired普通iterator对照不代表implicit
constructor与受污染explicit spread普遍等价，原native-super独立契约保持。
最终10项定向回归和证据见status.md；不能扩为平台、其余record、源级const/
类型或全部结构gate完成，生产规则仍139。

2026-10-06补充：`paired_model_record_source_test`实际执行ABC-only
`--value-source`的完整IconInfoModel/SectionModel/RecommendModel记录，按
既有paired index验证ABC hash/size及ValueTree身份，生成结束后再读paired TS。
2481配对场景覆盖module live更新（包括参数求值期间）、构造callee在参数
效果之前取得、列表/receiver、延迟callback及构造失败后保留的原callback、
稀疏输入、第二/最后一次callback原异常、部分状态与新鲜身份。生成记录另验
12组model/item assignment/setter/new.target、112组0/1/200/201实参及各
字段setter异常/部分写入，39组原参数身份/字段descriptor；
Node保留的未初始化TS字段不能充当Ark属性赋值own key的oracle。
原生constructor/map/arrow/push/new、参数列表与条件属性行为另验，九种
先解析的有效JS mutation均检出。三个指定record的受控whole-record语义
与高层表示验收accepted，保留必要receiver alias及mutable class binding；
不能扩为平台/全部record/全部类型或原结构gate完成。共享位置测试另补齐
既有discarded invocation read和implicit tail completion的精确零宽边界
契约；无生产语义变化，最终27/27定向回归和日志见status.md。

2026-10-06补充：CardArray 与 BreakpointType 的独立整record表示验收
从pending转为accepted；源码均由当前规则137的ABC-only ValueTree实际生成。
可保留的局部差异是mutable class binding、等价最终if极性、不可变receiver
capture alias与pinned instance assignment。两项分别验证高层imports/exports、
class、constructor/signature、字段初始化与真实method/callback表达；不以
source_complete、Node语法或单个regex代替独立行为测试。

CardArray保留24组基础场景及1 mutation；新增11组live import/callee、push
取得后替换、reverse/repeat、reentrant constructor、非callable/constructible、
retained arrow行为与196组任意throw identity/部分数组状态。失败构造留下的
实际closure在host fault恢复后仍可调用，失败模块不因此重启/修复。84组
STOBJBYNAME赋值层单独检查inherited setter、foreign new.target、两个realm
的fallback、求值顺序/原异常/返回身份；不能把Node的initialized DefineField
当作这一层oracle。5个新增有效JS mutation拒绝，含错误参数、普通函数代替
arrow、push→unshift、DefineField代替assignment和错误constructor return。

CardArray/实际CardItem两模块直接连接，只有base/Index是控制依赖；7组
连接场景/336组异常注入比较实际类/receiver、图片length或index coercion
期间live export替换、CommentItem换绑、空图片表、延迟comment callback、
module TDZ、own-field顺序/descriptor、fresh array/instance及部分状态。
peer TS只擦除4个无initializer的type-only field；initialized fields和实际
constructor/closure保留。3个peer语义mutation被独立trace/state对照检出；
不能据此自动完成CardItem的全部new.target/setter或独立表示验收。

BreakpointType原360组lookup/getter/live import及constructor setter/3 mutation
保留；新增24组constructor/参数身份与240组prototype/setter异常，含NaN/-0/
Symbol、禁止coercion对象、两个realm的new.target fallback与部分写入状态。
3个constructor mutation拒绝错误值、错误对象return及DefineField替代assignment。
独立reference只擦除原有3项uninitialized type-only字段，不进入engine。
这些是两个指定完整record与实际peer连接的受控验收；其余record、结构集成
门槛、完整应用/平台、RSS与全部阶段一验收仍须对应实际证据。

2026-10-06文件名补充：实际固定五ABC/六模式30次ABC-only导出通过，
每模式106份源码的filename均来自unique内嵌包身份，关闭所测67份entry与
package不同的descriptor fallback缺口。所有正文及位置映射按entity与规则137
native-receiver输出逐一不变；16项定向回归通过。此证据验证输出路径与无
正文回退，不扩为未初始化TS字段、global/签名或其余record的独立行为完成。

2026-10-06逐formal命名补充：规则138的真实稀疏/非法/重复 sibling三参数
family新增24组，独立JS共152 decoded families、3268行为及88有效mutation；
实际global/module callee不能被新formal/cell提示遮住。实际完整Index record
通过host注册取得真实factory，创建原class后调用真实NavPageMap，配对方法
的756组同步/延迟callback、receiver、arity、效果/异常对照通过。Content原
176组检查及legacy NavPageMap的另756组保留。依赖host/framework为控制
对象，不以该方法的通过宣称整record、构造生命周期或平台验收完成。
最终18项定向与五ABC六模式导出通过；default/constructor各7份、四种语言
数值模式各14份源码表示改善，完整数量未变。完整source签名/结构gate仍须
自身全范围证据，逐参数有名不证明类型、const或源级字段。

2026-10-06 CardItem补充：当前规则138实际ABC-only完整record的独立
whole-record表示验收由pending转accepted。保留原生class binding、参数
签名、字段赋值、取模索引、forEach/arrow/new及imports/export；允许不可变
receiver capture alias和pinned assignment。146组普通/数值/live import/
callee行为与544组原异常身份、356组inherited setter/new.target/两realm
fallback/部分写入检查、6个有效JS mutation通过。Node initialized TS字段
与固定Ark赋值不同，setter层仍以独立pinned效果序列验收。

再将实际CardItem与实际CommentItem完整ESM连接；受控barrel仅reexport
实际类并提供两个输入列表。152组条件字段/重复getter/reverse/deferred/
200/201项及276组原异常/部分内容/失败后callback重放、3个有效mutation
通过。两个reference保留完整constructor/closure与initialized字段，只
擦除明确无initializer的TS字段/interface声明，并在全部ABC-only生成后
读取；没有把配对来源或host构造器接入engine。这关闭CardItem所测独立
受控验收，不自动关闭完整base模块图、平台或全部结构gate。实际三个record
重新生成及相关10/10回归证据见status.md，生产规则/完整数量未改变。

2026-10-06 TabBar补充：实际record41409 TabBarModel和44106
TabBarViewModel的受控whole-record独立验收由pending转accepted。两固定
ABC导出在任何paired TS读取前结束，输入/manifest hash及ValueTree核对；
同一module graph实际执行两个完整生成记录。两次连接验29个字面量节点的
值、identity graph、own key/descriptor/extensibility/realm prototype，
inherited setter与被替换的全局helper不能影响原生字面量初始化。实际两个
ViewModel实例共享导出数组，独立field替换、borrowed receiver、module
TDZ/namespace/重复evaluate等保留；196组live import及52组pinned字段赋值/
new.target/任意异常、6个有效JS mutation通过，原六个viewmodel证据保留。

该表示接受原生array/object/class/import/export/getter与等价空constructor，
允许当前保守let绑定；不能据此称已恢复源级const/private/type信息。Module
slot access与完整嵌套绑定写入证明是不同事实；Node initialized TS field
DefineField仍不作为固定Ark setter语义oracle。这是两个实际record及其
直接模块链的受控验收，不能关闭整个应用或原完整结构gate。相关10/10
定向回归与剩余边界见status.md。

2026-10-06规则139模块lifetime补充：native声明后的arm/loop assignment
改用共同VerifyValueFlow的fresh dominator与operation order证明已初始化，
移除物理入口块限制，原root declaration ownership/scope/module/capture/
TDZ/origin保持。三profile真实join/loop/register重分配和1/200/201/400/
800 pure split共54独立ESM场景、57 valid-syntax mutation通过，6个提前
write/条件首次初始化refusal保留。这些后两类仍是native declaration表示
缺口，不是语言禁止条件初始化的结论；catch不由normal flow授权。

最终21/21定向回归通过，当前固定五ABC六模式30次导出/hash/106份语法
检查通过；各模式source bytes及212份位置JSON与此前partial-formals输出
完全相同，完整数量不变，无legacy/完整回退。本次证明通用布局变化不需
新恢复匹配，不证明任何剩余整record或阶段一已完成，细节见status.md。

## 1. 四种状态，不混报完成

| 状态 | 意义 | 可否称 gate 通过 |
|---|---|---|
| Designed | 类型/算法/规格已审阅 | 否 |
| Implemented | 代码和回归规格已提交 | 否 |
| BuildChecked | 生产目标构建成功，静态/产物核对有记录 | 否 |
| Validated | 对应 verifier、回归、差分、负例、预算验证实际执行且通过 | 是，仅限对应范围 |

不能把“之前旧路径测试通过”当新路径验证；暂停时不要编造 expected 输出为实际输出。
测试状态是工程交接状态；正式产物本身另有路线和完整性状态。

## 2. 最小路线与证据模型

新增类型化合同（拟实现，不是当前 CLI 能力）：

```text
RoutePolicy = LegacyAllowed | RequireValueTree
SourceRoute = Legacy | ValueTree
SourceStage = Decoded | InputVerified | ValuesImported | ValuesVerified
            | ControlFrozen | BindingsFrozen | TreeVerified | Printed | Published
Coverage = Complete | Incomplete | Unsupported
FailureClass = Unsupported | Unproved | InvalidInput | InvariantViolation
             | WorkLimit | MemoryLimit | OutputFailure
```

`RequireValueTree` 对选定方法及其必需子方法闭包生效。任一子方法失败会导致父级
义务无法闭合，不可把旧子函数正文塞入新函数树算完成。LegacyAllowed 可以输出旧路径，
但 route 必须为 Legacy，migration coverage 不计成功；record 总状态不能覆盖子方法失败。
资源/内部不变量失败不得静默改路径；旧行为若有此问题须在接入任务显式修复。

每方法 receipt 至少包含：输入 hash、record/method 实体、profile 三身份、source contract、
engine/rules revision、normalization owner、实际 route、最高成功 stage、typed failure
与 PC、IR/CFG revision、是否调用旧 writer、子方法依赖及结果、source/maps 产物身份。
证据由各阶段成功时签发，不能在 serializer 中统一填 `TreeVerified`。
未执行验证的工程状态不能伪装为运行时 stage 已验证。

Origin evidence 记录 retained effects、control、elisions、signature placements、coverage
缺口分类；不以 span 总数或源码行数作为正确性指标。receipt/schema 版本有显式 enum
序列化映射，未知标签/版本不能解析为默认成功。

## 3. 冻结启动输入与全语料范围

以下 P1–P6 仅是启动/固定回归子集。总目标覆盖两个 corpus 根的全部输入，
跨应用/profile/留出集与统计分母遵循 corpus.md。T00 同时冻结全语料分组，不得
到 pasteboard 完成后才决定要不要支持其他应用。

复用 `tests/corpus/pasteboard.json`，不把输出目录中的生成源码作为新引擎输入：

- 输入：`/Users/sun/abcd/samples/abc_with_code/pasteboard/modules.abc`。
- SHA-256：`854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e`。
- 19 records、219 methods。身份变化时重新核对任务选择，不能只按名称沿用实体号。
- 另一份路径 `/Users/sun/abcd/samples/abc/pasteboard.abc` 按同一内容身份处理，不重复算样本。
- 不读配对源码、sourcesMap.map 或 IDE sidecar；旧输出只作对照参考，不作 oracle。

| Slice | 真实实体 | 检查重点 | 边界 |
|---|---|---|---|
| P1 | PageA record 6897 / method 7586 `setInitiallyProvidedValue` | 参数、if、两次属性读、pathStack 写 PC40 | 没有显式 call；G1 首个贯通 |
| P2 | PageA 6897 / method 7559 `aboutToBeDeleted` | callee/receiver/内外层调用顺序；既有记录 PC66/74/77/81 | 没有分支；G2 调用伴随样本 |
| P3 | PageA 6897 / method 7366 | 同方法分支、调用、对象/闭包创建及 owner 闭合 | 先由 ABC 冻结指令/能力清单；不是已通过样本 |
| P4 | Home 5521 / constructor 5851 | 默认参数、this、捕获、局部快照、真实函数体 | 高复杂样本，不替代普通函数验证 |
| P5 | PasteBoard 8383 / constructor 9277 | 与 P4 不同结构的构造器，禁止样本特判 | 所有用途逐项验证 |
| P6 | 全 pasteboard | record 组合、异常/async、类型化拒绝及完整分类 | 不是承诺所有方法都成功 |

实体/PC 来自现有 ABC 派生产物与专项设计；实施 T00 再以 ABC-only 清单冻结，
不把表内 PC 当算法常量写进生产代码。7366 缺什么能力就纳入对应任务，不跳过难点
后宣称已满足“同一个真实函数包含分支及副作用调用”的原目标。

## 4. 验收矩阵

每个编号同时需要正例和拒绝/错误反例；这里是必需断言，不是可选灵感。
生产 verifier 不能依赖 tests/support 的慢 oracle。

### 4.1 IR 与证明

| ID | 输入/操作 | 必须断言 | 归属 |
|---|---|---|---|
| V01 | read ordinal 与语法 role 顺序不同、range call、同值多次参数 | role/value 对应正确；每 occurrence 有 use；错 ordinal 拒绝 | T01 |
| V02 | 输入局部从未初始化、method/profile 不匹配 | UnknownInput 不变 undefined；身份不匹配拒绝 | T01 |
| V03 | copy 链、多个不同寄存器、同值双 use | 同次 pass 清除所有合法 alias；use 双向一致 | T02 |
| V04 | dead pure、unused getter/call、可能 coercion throw | 只删纯无异常操作；效果 retained 且顺序不变 | T02 |
| V05 | trivial/self-only phi、同 predecessor 两条边、MethodEntry phi | 有外部值才可化简；IncomingKey 区分；未支持 live phi 拒绝 | T02/T05 |
| V06 | 使用旧 revision 的分析、变换中途资源失败 | stale proof 拒绝；原图/已提交图不半改 | T02 |
| V07 | may-throw result 在 catch 使用、正常跳 handler | catch 不看到 Commit result；普通跳不生成 CatchValue | T09 |
| V08 | 修改 cell 的子闭包、不同 creation env、循环捕获 | language-domain 不当 immutable；逐创建 rebase 正确 | T07 |
| V09 | effect 缺失、重复、跨 handler/分支/suspend 移动 | verifier/plan 拒绝，不只比较最终值 | T03/T04/T09 |
| V10 | 极深树、use fanout、容量溢出、注入预算失败 | 有界退出 typed resource error；不返回部分成功 | T01–T12 |
| V11 | buffered object/array、hole、own define 与 setter | 分配身份/descriptor/原型/次序正确；不能用普通 store 替代 define | T07 |

### 4.2 源码、控制与绑定

| ID | 场景 | 必须断言 | 归属 |
|---|---|---|---|
| S01 | `r=param; use(r)`，参数后来改写 | 无无用 alias 声明；旧值 use 不错换当前 param | T03/T06 |
| S02 | `p===undefined` 前导，p=-1/p=undefined/rest/arguments | 签名绑定、length 与参数环境不变；无证明保留前导 | T06 |
| S03 | 两次 getter、一次 getter 多次 use、getter 中 mutation | 次数/顺序/异常截止点与输入对应 | T03/T04 |
| S04 | property callee、nested args、getter 修改方法、direct eval | reference/receiver/参数次序保真；不引入可篡改 `.call` | T04 |
| S05 | diamond、早 return、循环 phi、两值 swap、critical edge | 每边复制正确；loop back/continue 顺序；循环 temp 必需 | T05 |
| S06 | shadow、TDZ、catch param、每迭代 closure、未知 debug 名 | Symbol 身份/声明 scope 正确；不猜原名 | T06/T07/T09 |
| S07 | 派生 this 在 super 前/后、箭头 this、替换 receiver | 同一构建器；未初始化读取拒绝/保留；不可假定捕获恒定 | T08 |
| S08 | 所有 AST expression/statement 类型和错误组合 | 无 RawCode/寄存器/phi；target/label/await/super 合法 | T03–T10 |
| S09 | NaN/-0、优先级、escape、深表达式、非 ASCII | 语法及数值保真；offset 编码明确；大小有界 | T03/T12 |
| S10 | module live binding、global 属性、class setter、computed keys | 不能快照 import、伪造 global let、把赋值改 field define | T08/T11 |
| S11 | try/catch/finally 中 return/break/throw 覆盖 | completion kind/value/target 保留；finally 执行次数正确 | T09 |
| S12 | await 正常/拒绝/恢复，async lexical prelude | handler/恢复 owner 保留，前导资源前提有版本 | T10 |

### 4.3 路线、来源与发布

| ID | 场景 | 必须断言 | 归属 |
|---|---|---|---|
| A01 | 新路线各阶段故意返回失败；旧 writer 可成功 | RequireValueTree 保留原失败，无旧正文替换 | T00/T03 |
| A02 | 父新树、子旧路径或 child capture 失败 | 父不能 TreeVerified/Complete，逐 child receipt 可追踪 | T07/T11 |
| A03 | standalone 和 record 中同方法 | identity/profile/contract/实际 route 一致，外壳不掩盖方法路径 | T03/T11 |
| A04 | copy 消除、默认签名、融合表达式、嵌套方法 | 原 PC disposition 完整；有效 byte span；不假零宽覆盖 | T02/T06/T11 |
| A05 | record 一个方法不支持、输出写到一半失败 | 不发布 executable/complete；staging 原子清理/提交 | T11 |
| A06 | 两根目录全 corpus、重复输入、static/unknown/malformed | 全输入/实体分类无漏计；19/219 仅固定子集；static 不计源码成功 | T00/T12 |
| A07 | 200/201 与几何规模、峰值账本/RSS | 不出现隐藏逐值整图扫描；资源诊断可信；全进程 cap | T12 |
| A08 | 多应用/profile、未参与调优的应用留出、复杂度分层 | 不按样本/命名特判；旧 profile 不套 v7；开发和留出结果分别记录 | T00–T12/Cnn |

## 5. 验证层级与 oracle 边界

1. 结构验证：typed import、def-use、dominance、边归属、owner/capture、树及 provenance。
2. 小型手构造 IR/ABC 回归：覆盖上述反例，测试源码与 production 分离。
3. 效果轨迹 oracle：记录 getter/setter/call/throw 顺序和值身份；分别断言正常、条件跳过、
   任意 JS 值抛出。轨迹类型在 C++ 内用 enum，序列化字符串仅边界使用。
4. JS 语法/运行：Node parse check 只证明语法；运行 fixture 比较可观察结果与轨迹。
   ArkUI 宿主 mock 只能证明指定 mock 契约下行为，不证明真实宿主所有状态。
5. 新旧输出差分：用于找回归，旧路径不是真值。两者一致仍需独立 IR/语义 oracle。
6. 若有匹配版本且可执行的 Ark runtime，加入原 ABC 与生成 JS 的宿主轨迹对照；
   缺 runtime 时明确列残余风险，不能以 JS 自己比自己代替原 ABC 行为验证。
7. 资源与规模验证：操作计数+几何输入+全进程 peak RSS。预算单测不是 RSS 证明。

不得把完整应用源码放进引擎输入或把生成源码与原文本逐字匹配当唯一正确性标准。
同一 fixture 的规范轨迹不能由被测 optimizer 自动生成，否则会共同复制错误。

## 6. 命令与产物约定

目前可用的生产构建命令（此文并未执行）：

```bash
cmake --build build --target abc_structure abc_semantic -j2
```

预计成功为退出 0。仅证明编译/链接；如果当前 build 不存在或配置漂移，先按仓库
现有配置文档核对，不能臆造 CMake options。生产构建也需报告真实 commit/命令/退出码。

现有 ABC-only 导出命令：

```bash
build/abc_structure /Users/sun/abcd/samples/abc_with_code/pasteboard/modules.abc out/pasteboard-next --shared-source
```

该命令目前不能强制新 ValueTree 路线，也不能证明迁移完成。新选项 `--value-source`
拟在 T00/T11 实现为 RequireValueTree 全 record 模式；未实现前禁止假装可运行。
方法级筛选由 `abc_semantic` 的新类型化 selector 接入，具体 CLI 拼写在 T00 固定于
工具 usage 和回归，设计不要求新增第二个独立 CLI。不得让名称匹配决定引擎语义。

待用户恢复测试且实施计划创建目标后，按任务列出的准确 target/name 构建并运行，例如：

```bash
cmake --build build --target value_import_test -j2
ctest --test-dir build -R '^value_import_test$' --output-on-failure
```

目标尚未创建时失败是缺实现，不是验收运行。每项期望为退出 0 且对应反例触发规定的
typed rejection。不要预填“all passed”。产物写新目录，不覆盖用户当前查看版本。

## 7. Agent 交接模板

分配任务时复制并填实以下字段；没有明确任务 ID/范围不启动实施。

```text
Task: Txx（本计划名称）
Baseline: commit + clean/dirty state + preserved unrelated changes
Read first: AGENTS.md + source-recovery/README.md + 本任务合同章节
Own files: 明确列举；共享 CMake/入口修改由集成者串行完成
Predecessors: 已合入提交 + 对应 receipt/test 状态
Deliver: 类型/API、算法、正反例、调用点、文档更新、一个或数个小提交
Forbidden: 新宿主假设、源码字符串消除、samples 特判、旧路径掩盖、越界重构
Validation authorization: 当前仍暂停测试/JS/RSS；允许项单独列
Acceptance IDs: Vxx/Sxx/Axx + 真实样本实体
Complexity: I/B/E/U/P/A/K 维度、计费操作、资源失败点
Review: 语义审查者 + 集成负责人
Stop/return: 需要新契约、缺前置证据、共享接口冲突时提交问题和最小反例
```

实施后交回：修改文件/提交 hash、实际命令与退出码、未执行验证、typed blocker、
已消除与保留临时量的理由、真实路线和来源证据、下一任务可用接口。
禁止把这份计划的复选框全勾选而不附证据。一次只领取一个有明确依赖闭合的任务。

复杂导入/调用/异常/捕获由强语义能力模型实施或审查；机械 enum 序列化、文档链接、
既定 fixture 配线可交经济模型；主入口集成用标准编码模型并接受语义审查。
不要把互相依赖的 IR/AST API 同时交给多个 agent 自由设计。模型选择服从实际可用工具。
最多 8 个 worktree；任务完后确认干净及提交保留再清理，不强删未完工作。

## 8. 偏离处理与退出标准

新增 unsupported case 先定位为 input/profile、导入、owner、控制、表达式或发布能力，
按任务归属补通用规则；不能因为只在 Pasteboard 出现就写 method ID/name 特判。
若需改变语义契约、AST 表示或证明边界，先给最小反例与方案影响，再修订本设计；
不静默扩大前提。术语/文件名小调整可在同一提交记录映射，避免平行重复类型。

完成 G5 必须有两个 corpus 全量分类、跨应用/profile/留出结果和所有宣称成功方法的闭合证据；
不能把 pasteboard 的 19/219 当全语料分母。仍拒绝的方法列出 typed 原因、
PC、缺少的最小能力以及是否属于明确不支持范围。不得以“临时量归零”“mapped methods
数量增加”“有 `.mjs` 文件”作为完成定义。G5 是审计完成，不是所有动态样本反编译完成；
剩余缺口按 Cnn 持续关闭，G6 才是全目标动态范围完成。原结构 gate 另逐项核对，
不能被新 IR 工作取代。
