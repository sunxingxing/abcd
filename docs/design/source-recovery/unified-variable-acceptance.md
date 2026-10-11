# 统一变量恢复终验：消费者与证据账本

2026-10-11，生产规则311/d1ee1ea9。D终验已按原普通函数临时变量与循环携带变量范围关闭。
A的B1–B5、B的R1–R10及试算共同模型边界不变；复杂闭包重构、finally展开和暂停内部精度继续保留原表示。

| 责任 | 当前唯一事实入口和消费者 | 退出的重复决策及边界 |
|---|---|---|
| 原始语义、当前和试算 | Initializers::BuildProjectedFlow → PrepareRecoveryProjection；CurrentProjection只发表完整联合产品；trial使用相同入口 | 不再独立运行修改计划的Evaluations；未完成的联合产品不能成为pass可借用的中间事实。原始对应R1–R10及普通单元B1–B5沿用已验收清单 |
| 读取值传播 | CurrentProjection表达式/初始化产品，CurrentConstants/CurrentCopies → Constants/BindingValues；ExprId拥有读取替换 | 生产不再新建整槽read_bindings；初始undefined是否活跃由共同定义/需求决定，不由变量名或初始化形状决定 |
| 存储合并 | CurrentLiveness/CurrentInitializations、当前绑定存在性、名字退休与共同scope单元 → Coalesce/Aliases | CompletedStore无生产消费者；原始复制仅生成偏好，当前干涉和初始化仍负责证明；合并不复活已删除的store或已退休绑定 |
| 删除写入/效果 | CurrentDemand、CurrentProjection表达式效果、CurrentStatements → DeadDefinitions/InitialValues/Bindings/StoredResults/Inert… | 原始声明不能替代当前成功定义；删除写入仍保留必要RHS求值/异常，删除计算与共享存储分别发表 |
| 声明与循环表示 | 当前FlowBinding.declaration、CurrentInitializations、RetainedExtents、SourceNameRetirement；trial复用共同投射；native loop统一entry/repeat单元适配 | 当前声明替代原始placeholder决定退休位置。Confined证明保留引用的源码词法extent，不是同块准入；LegacyConfined仅保留逃逸闭包重构能力缺口 |
| 打印和类名准备 | PrepareSourceVariables → 不可变SourceVariables；class_names和tree_printer按相同tree/original借用 | printer不再单独构造变量优化计划；独立打印入口也调用相同准备入口。语法选择及既有协议打印仍保留，不等于额外变量优化 |
| 多轮调度 | RecoveryChanges实际journal → 域epoch/RecoveryWorklist →阶段回边；RecoveryProgress消费原始义务/存储类 | 手工重复列表、SnapshotRefreshNeeded/专用刷新barrier退出。无关域不失效稳定尝试，借用对象仍由完整revision保护 |

旧SourceAst路径的PrivateCopyOutput仍被predicate_copies/result_copies/store_receivers使用，
本次没有声称全仓删除；abc_structure的value_tree模式在规划阶段排除legacy emission，
导出manifest及route测试须确认legacy_writer_used=false。迁移范围为现行贯通ValueTree变量恢复。

D分别使用下列证据：原65名单对六类语义/布局/反例/源码执行的结果，10项组合保留结果，
200/201及几何成本，实际PasteBoard/ProgressBar配对行为以及固定五ABC两合同的源码和路由。
完整record标记和语法成功不证明所有平台行为；阶段一完整行为/表示质量和结构gate继续单独开放。


## 六类必需语义及性质证据

| 义务 | 同一生产版本的实际验收入口 |
|---|---|
| 普通临时复制和多次赋值 | binding_initializer、binding_interference、definition_elision的生成与独立JS；删除复制后仍保留历史偏好，但由当前干涉证明合并 |
| 分支合流和循环携带值 | binding_interference的分支/循环/旧值链，loop_initializer/loop_transition；每个规模保留源码声明数与独立行为要求 |
| 声明undefined被读取 | declaration_value、local_initializer及共同initialization/demand反例；初始化删除与存储共享分别证明 |
| 抛异常前后写入 | original_flow、numeric_update、stored_result；提前写回/丢转换/删除活跃初始化的合法JS变异必须被发现 |
| 嵌套名字与重复单元 | recovery_flow的独立名字/初始化/单元oracle；flow_units另有24份实际解码loop/catch、47520比较、72变异 |
| 组合与布局稳定性 | 原65的寄存器/COPY/拆块/arena变化；10项协议/构造/迭代/控制保留；共同小图oracle、200/201/400/800以及精确/差一预算 |

C的最终冻结记录覆盖原32目标、65项（655.79s）及10项保留（56.54s）；D没有重新
跑一遍相同生产版本的固定65。D之后仅修改一个CTest的解析器选项、两个VoIP测试
定位方式：对象声明initializer无需冗余括号，await和userProfile错误变异按实际语法
节点选择，不依赖消失的临时变量名。生产文件、其余验收代码和测试命令没有变化。

补充8项是明确的增量结果：第一组7项通过，VoIP测试在旧定位失败后单独1项通过
1.38s，不能写成第一次8/8通过。VoIP的322场景、768受控调度、366主流程调度及
3+4错误变异全部通过。window数值/record/optional三个检查、FollowView6426场景、
普通literal两个record的3888回调场景和flow_units两项通过。既有失败日志保留。

原65中的真实PasteBoard回调通过6995次配对比较和2个错误变异，保留1个成功标志及
2个循环索引；ProgressBar/CommonConstants通过88场景、570故障边界、114次callee
替换、15个错误变异及2个等价变换。它们有明确的宿主模拟范围，未声称HarmonyOS运行。

## 既定五输入与表示证据

输入均来自原abc-index的五个paired条目，前后SHA/size一致；引擎只接收ABC。
两合同分别导出152 records，全部value_tree且legacy_writer_used=false，无原完整
record退回部分。默认合同80完整/26部分/46无模块；显式language_numeric_constructors_v1
为106完整/46无模块。两种合同各106份完整/部分源码均通过语法检查。

对照迁移前optional-statements-default-pass-1和optional-postfix-numeric-constructors-pass-1，
分别61/68份源码变化；类方法顺序、kind、static/computed、async/generator和参数语法
布局均保持。声明计数默认536→378、显式合同651→468；3个机械while变为for。
这些是本次普通变量表示迁移证据，不是全源码逐方法语义证明，也不是C单个提交的新收益。
固定pasteboard相对C前的最近基线仍逐字相同；不得把两种比较基线混为一谈。

D证据入口：build/stage-one/unified-variable-package-d/package-acceptance.json；
result.json、layouts.json、source-diffs.patch、final-paired-junit.xml、voip-junit.xml。
C原65/10项及成本入口：build/stage-one/pass-convergence-package-c-acceptance/package-acceptance.json。
D源码摘要中相对C只差上述三个测试配置/文件，生产实现完全一致；不借历史通过掩盖新改动。

## 关闭与剩余任务

本普通变量迁移的五项主干均按各自有限范围关闭：共同原始对应、普通单元、当前/候选
联合分析、依赖Pass收敛、消费者/旧判断/端到端终验。没有按项数计算工作量比例。
全套、扩展语料、RSS/benchmark、DFX和代理仍暂停；这些没有伪记为已通过。

阶段一Goal保持active：106个complete标志不代替全部配对源码的独立行为和表示质量
验收；实例字段、全局、完整签名与函数体整合仍按结构gate逐项核验。复杂闭包环境重构、
finally展开、暂停内部精度属于预先列明的后续精度，不回头扩大本里程碑的关闭前提。
