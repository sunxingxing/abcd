# 统一变量恢复：有限交付清单

本清单落实 2026-10-11 用户纠偏，替代实施计划中开放式的“继续补齐剩余语义”。
核对基线是 `d684c698`（规则296），不是侧边对话的规则294快照。
本轮范围仍为**普通函数临时变量和循环携带变量**；复杂 finally、闭包环境重构、
暂停协议内部精度保持原表示。保留不是免验收：必须证明变换不会借用缺失的事实。
原阶段一和结构 gate 没有被缩减；它们与本次变量里程碑分别验收。

五项主干状态：原始接入未关闭；普通绑定身份未关闭；试算在现有模型能力内关闭；
Pass 收敛未关闭；统一变量终验未关闭。没有可信的完成百分比或剩余工时估计。
新增规则、测试、提交只作为下列清单的证据，不是交付单位。

## 一、原始接入的穷尽边界

分类含义：

- **贯通**：本轮必须提供原始输入到当前/候选共同模型的对应，不能用保守拒绝结项。
- **保留**：共同模型必须保留执行、效果、异常、单元可见性；内部精度可缺失。
  缺失的原始证书不能参与值域、移动、存储或删除证明。保留义务本身必须验收。
- **后续精度**：仅指在保留义务通过后提升恢复质量，不是本轮语义安全的免责条款。

下面覆盖 `ValueOperation` 当前全部 alternatives，另列不属于操作 variant 的
入口、合流、异常边、源码声明。适配只发生在现有 IR/Tree 验证边界；恢复 pass
不得重新解释 opcode，也不得依据这里列出的类别建立新的操作白名单。

| 编号 | 原始/源码语义类别 | 分类 | 现有证据与有限的剩余义务 |
|---|---|---|---|
| R1 | 参数、receiver、函数自身、入口定义；SSA use/result；phi/catch/saved contribution；`ValueCopy` | 贯通 | 已保留 values/uses/definitions/输入来源；逐类验证当前/候选成功点、合流边身份及缺失证书失效；建立覆盖账本，不能只凭结构存在判通过 |
| R2 | `ValueUnary`、`ValueBinary`、`ValueString`、`ValueNop` | 贯通 | 已有值域/效果/原始结果对应；确认隐式转换失败、更新读取—转换—成功写回、removed/pure-elided 的边界由共同事实处理 |
| R3 | `ValueNamedRead`、`ValueNamedWrite`、`ValueOwnWrite`、`ValueIndexedWrite`、`ValueDefineAccessor`、`ValuePropertyRead`、`ValuePropertyWrite` | 贯通 | 原始效果与原生动作已接入；验收 reference/receiver/属性键准备与实际动作分离、异常前后写入，不能把 enclosing expression 当物理动作 |
| R4 | `ValueGlobalRead`、`ValueModuleRead`、`ValueModuleWrite`、`ValueFrameThis`、`ValueFrameFunction`、`ValueGlobalObject`、`ValueSymbolConstructor`、`ValueRestArguments`、`ValueArgumentSnapshot` | 贯通 | 核对外部/入口读取、mapped arguments、module 写与 ordinary binding 的不同身份；不能将缺失的入口稳定性当作常量 |
| R5 | `ValueArrayAllocation`、`ValueObjectAllocation`、`ValueInvoke`、`ValueConstruct`、`ValueConstructApply` | 贯通 | 保留分配、调用、构造的原生动作与效果；验收实参/receiver顺序、成功结果与抛出路线，资源失败只按显式合同排除 |
| R6 | `ValueLexicalLoad`、`ValueLexicalStore`、`ValueEnvironmentPush`、`ValueEnvironmentPop`、`ValueCurrentEnvironment` | 贯通 | 295的失败原因和296的成功环境状态是已完成子义务；**尚缺原始动态环境到源码 activation 的对应及投射后的身份保持**，不得用 EnvironmentId 或同 ScopeId 代替 |
| R7 | `ValueBranch`、`ValueJump`、`ValueReturn`、`ValueThrow`、`ValueGuard`、`ValueDebugger`；正常/异常控制边 | 贯通 | return/throw和有pure test的控制相位已接入；**无pure equation控制、无值跳转、guard/debugger的动作/边对应仍须逐类清零**；同一表达式锚点不等于同一完成边 |
| R8 | 源码 `let x;`/初始化/赋值、catch参数、作用域进入/离开/重复进入、for每轮绑定 | 贯通 | 源定义和scope事件已有；**local/catch动态单元、TDZ与初始化的区分、变换后对应**仍未关闭，联合B项验收 |
| R9 | `ValueFunctionCreation`、`ValueClassCreation`与已有incoming capture | 保留；创建动作仍属本轮贯通 | 已有创建/捕获可见性与incoming身份；验收普通变量变换不改变闭包观察到的单元或创建次数；重新布局逃逸环境属于后续精度 |
| R10 | `ValueHole`、`ValueIterResult`、`ValueAwait`、`ValueYield`、`ValueSuperCall`、`ValueSuperSpread`、`ValueSuperCheck`；复杂finally完成路由 | 保留 | 由现有协议验证和typed completion保持原表示；**须用组合反例验证缺失原始相位时共同分析撤回跨界变换依据**。展开内部暂停/恢复或协议、重构finally属于后续精度，不能成为普通变量关闭前的无限前置任务 |

`R9/R10` 的“保留”是处置边界，不声称整个保留合同已经验收。
现有 `FindOriginalPhase`/handler coverage 的空值撤证只证明它们自己的消费者；
还要验证其它共同分析和所有候选也受同一边界约束。

**R项关闭条件**：R1–R8 所有本轮义务有原始输入→当前→候选→源码的证据；
R9/R10 的保留合同有组合正例和跨界反例；没有未归类的操作/入口/完成事件。
缺失类别只能成为显式清单项，不能静默改为“后续”或反复引入无边界的“剩余语义”。
新增 ISA variant 时先更新归属和影响，再决定是否影响本轮。

## 二、连续交付包，而非子证明终点

| 交付包 | 必须一起关闭的义务 | 退出的旧决策 | 完成证据 |
|---|---|---|---|
| A 原始语义与普通单元 | R6/R8；B1原始allocation成功与失败；B2 local/catch进入—初始化—重复进入；B3 raw/source动态单元对应；B4候选保持；B5名字与捕获观察 | `FindFlowOriginalCellReference`中以相同scope充当动态单元证明的临时限制；其它保留限制逐个注明义务 | 解码输入、多次作用域进入、循环catch、声明undefined活跃、异常写回、arena/寄存器/分块变换；独立token单元轨迹对照；生成源码独立执行 |
| B 原始对应余项 | R1–R5/R7的覆盖账本清零，R9/R10保留合同 | 被共同事实替代的原始阶段、效果、控制重复推断 | 每类正例、错误证书反例、边/效果组合；缺失能力保留原表示不伪造精度 |
| C 全过程Pass收敛 | P1变更集、P2失效与队列、P3顺序、P4终止、P5成本、P6闭环验证 | `Initializers::Run`手工重复列举局部pass、一次snapshot refresh、全局revision承担全部稳定判断 | 见[收敛合同](unified-variable-pass-convergence.md)；不能以分析求解稳定替代 |
| D 统一变量终验 | A/B/C、现有试算合同、旧判断退出、真实record | 用新旧双重准入保留旧形状限制的路径 | 原固定名单/真实record及原计划规定输入；被暂停范围不能算通过，需保持未验收状态 |

实现可按职责拆成多个提交，但 A/B/C 各自直到右列证据齐备才是一个交付闭环。
每次只开展一个交付包的生产实现。先A，再B，再C，最后D；C的设计与验收方案
本轮独立落实，不等待零散的版本检查“自然长出”收敛。
保存的动态单元WIP只有构建证据，未经测试，也未证明raw/source前提；不纳入已交付清单。

## 三、验收成本与工作组织

- 定向开发：验证本交付包新增语义、负例和相关输出；失败后修根因。
- 整包验收：包的必需义务全部完成并冻结源码后，跑一次固定名单和固定record。
  子证明提交只登记其定向证据，不为每个子证明重复整包20分钟验证。
- 共享生成：同一轮CTest中，C++完整断言+生成一次；独立JS解释执行消费这次输出。
  CTest fixture每轮重建；二进制身份、参数、完整内容摘要绑定；失败禁止消费旧产物。
  额外scale/chains/units模式、200/201与几何增长、语义变异、原截止时间和预算保持。
- 共享机制使用现有CMake/CTest的`FIXTURES_SETUP`/`FIXTURES_REQUIRED`合同
  （本机`cmake --help-property FIXTURES_REQUIRED`，自CMake 3.7提供）；无新增依赖。
  保留Standalone运行入口；维护在tests/support/source，序列化/校验成本与输出字节数
  和二进制字节数线性相关。不得使用`-FA`/`-FS`跳过本轮生成再声称完成固定验收。
- 本轮实测：原65项、Debug、`-j2`，1190.97s→678.53s；十组源码执行
  summary不变，固定record10份源码逐字节一致。证据在
  `build/stage-one/shared-fixtures-final/latency-comparison.json`；无新增语义覆盖。
- 验收优化只报告固定名单墙钟时间。不得把共享fixture的收益计作生产引擎加速，
  也不得将单位测试work/bytes称作整个进程RSS。全量、扩展语料、RSS/DFX/agents仍暂停。
- 真实record长期输出不变时明确报告“防回归证据，未新增恢复能力”，不累计成进度。

## 四、进度报告格式与变更控制

每轮报告只含：本次关闭的主干/有限子义务；已消除的确定阻塞；新增阻塞及依据；
仍开放的交付包和下一完整闭环；构建/定向/整包证据的实际层级。
不按规则号、代码量或测试数量估百分比；没有穷尽且估算过的剩余任务时不报完成时间。

允许新增阻塞，但必须指明影响哪个现有编号、具体反例或实现缺口、是否阻止安全保留，
以及为何不属于已允许延后的精度增强。不允许通过重命名清单项改变关闭标准。
