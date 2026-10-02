# 项目进展与设计审查

更新时间：2026-10-02。设计基线见 [实施计划](../../superpowers/plans/2026-09-25-unified-source-recovery.md)。
本页是当前状态入口；[progress.md](progress.md) 保留历史证据，不覆盖本页结论。

## 更新规则

- 每个完整子任务提交同步更新本页：实际交付、构建证据、验证状态、缺口、下一步。
- 用户已取消定期设计审视，不再按提交次数或阶段切换安排审查；遇到具体架构风险时说明。
- 工作期间持续简短汇报；无运行中的会话时不承诺后台定时刷新。
- 不按文件数、提交数或接口数量宣布完成；必须以原计划的端到端验收为准。

## 当前阶段

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
