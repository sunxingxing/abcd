# 全 ISA 优先执行顺序

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
