# 统一方案审计与聚合（2026-10-28）

起因：用户要求停止 Codex，由 cc 独立完成剩余工作，并持续检查三件事：
是否过度设计；方案是否统一贯通；是否存在针对样本的一对一处理。
本文是这次审计的结论，也是后续每次改动都要对照的基线。

## 1. 样本特例检查（结论：未发现）

在 `src/`、`include/`、`tools/` 中检索了以下内容，均无命中：

- 样本名、记录名或方法名，例如 DataDriver、TaskpoolAsyncLevel、hypium、
  getSuiteStress、asyncRunSpecs、CameraService；
- cohort 中的方法号、记录号，例如 60551、42717、42798、16784、58223、42397；
- 用 pc 或偏移字面量做比较准入（`pc == 123` 一类）。

近期新增的机制都只按以下依据准入：

- es2abc 降级（lowering）的结构事实，例如 iterator close 模板、单趟循环、
  per-iteration env copy；
- 带类型的失败证据。

准入从不依据文本、pc 或名字。
每个新机制都配有自建的 JS fixture 和 Node 差分测试，而不是只拿 cohort 样本验证。

## 2. 过度设计检查（结论：存在，需要聚合）

问题不在单个机制是否正确（每个都有独立验证器），而在组织方式。

### 2.1 "拒绝触发重试"的链式叠加

`phased_method.cc` / `family_tree.cc` 中，目前有 5 条只在特定类型化拒绝之后
才启用的重试路线：

| 触发拒绝 | 重试机制 | 引入提交 |
|---|---|---|
| `ExceptionInputError::kInternalValue` | guarded catch payload | 7daabca8 |
| `EnvironmentFlowError::kAmbiguousJoin` | environment renewal | ec359fc5 |
| `ExceptionControlError::kMultipleExits` | completion ports | ccbe614e |
| completion port `kTarget` → missing natural loop | single-pass scope | 09b75ea6 |
| AST census orphan + continuation mismatch | iterator continuation（family 级重建） | 808ac232 |

另有一个进行中的 projected exception snapshots（未提交；它是第 6 条，
由 `CallReferenceError::kCrossRegionValue` 触发）。

这种组织方式有三个问题：

1. **同一构造的结果依赖失败顺序。** 一个 `for…of` 走哪条路线，取决于
   legacy 路线先在哪个阶段失败，而不是取决于它是什么构造。
   失败顺序变了（例如修好上游某个 gap），下游的准入就可能随之改变。
2. **证据类型膨胀。** 每条路线各有 certificate、Error 枚举、verifier、
   failure JSON 字段和 retry 预算。iterator 一族已有 49 个源文件、约 8.3k 行。
   `for-of-round80..83.md`、`for-of-*.md`、`close-*.md`、`completion-*.md`、
   `iterator-*.md` 这些设计文档描述的是同一个对象的不同切片。
3. **"降级保护"本身成了补丁。** 09b75ea6 不得不加一条
   "后续失败复用早先的 omission 选择"的规则，
   原因正是重试链让一个记录可能在更深的阶段失败。

### 2.2 本质上是同一个概念的机制

- **迭代器协议**（es2abc：`ForOfStatement::Compile`、`Iterator::Close`、
  `DestructuringIterator`、`IteratorContext`）：
  - for-of acquisition / iteration proof；
  - close entries / boundary / completion / loop body；
  - completion ports、iterator continuation、single-pass scope；
  - array pattern、protocol rebase、protocol topology / enclosure /
    region ownership。

  它们共同描述同一个对象：一次迭代器激活，包含获取、advance、done 边、
  body、各类出口（break/continue/return/throw）以及 close 路由。
  natural loop 与否、耗尽后的 continuation、嵌套 close 链，
  都只是这个对象的属性，不是独立机制。
- **异常拷贝 / 暂存**：uniform exception staging、guarded catch payload，
  以及进行中的 projected snapshots，都在回答同一个问题：
  "异常边上的寄存器拷贝谁拥有、是否可执行"。
  projected snapshots 的根因正是两个 owner 对同一批拷贝给出了不同解释
  （一个看原始拷贝计数，一个看已认证的 protocol 消费）。
- **环境**：environment renewal 是独立概念，保留，不需要聚合。

## 3. 统一贯通的目标结构

单条管线保持不变：

```text
ABC → 解码 → CFG/异常分派 → SSA → value IR → 区域/控制规划 → FunctionTree → 打印
```

在此之上，每类编译器降级只对应 **一个模型、一个 owner、一个 verifier**：

| 概念 | 唯一 owner（目标） | 由它导出的属性（不再单独成机制） |
|---|---|---|
| 迭代器激活 | `IteratorActivation`：一次性识别 es2abc 迭代器模板（for-of 与数组解构共用） | 获取/advance/done；body 区域；出口端口及其 pending completion；close 路由与优先级；是否存在可达 repeat 边（natural/单趟）；耗尽 continuation；嵌套父激活 |
| 异常拷贝 | `ExceptionCopyModel`：每条原始异常边上的拷贝，分为"可执行暂存 / 被协议消费 / 被 catch 绑定" | uniform staging、guarded payload 域、跨区域调用引用 |
| 词法环境 | `EnvironmentModel`（含 renewal token 层） | per-iteration 绑定、捕获代际 |

原则如下：

- **模型先建，放置后做。** 识别是无条件的（按构造识别，而不是等失败），
  控制规划只消费模型。
- **一个对象，一份证据。** 不再为同一对象的不同出口或形态
  另建 certificate 和 Error 枚举。
- **Legacy-first 只作迁移手段，不作架构。** 字节一致仍是验收门槛，
  但新路线一旦覆盖旧路线的全部已完成输出，就删除旧路线，
  而不是永久保留"先旧后新"的重试链。

## 4. 执行计划（cc 独立）

1. **完成第 620 条（o32 service），不新增机制。**
   pc 2208 的根因是异常拷贝的解释不一致，所以修复放在现有的
   `exception_copy_emission` owner 里：让 selection discovery 读取已认证的
   protocol 消费，而不是原始计数。不新建 `projected_exception_snapshots`
   这个 owner 和 retry。Codex 的未完成 WIP 已归档到
   `build/review/codex/snap-wip/`，仅供参考。
2. **聚合迭代器协议。** 把 completion ports、iterator continuation、
   single-pass scope 合并为 `IteratorActivation` 的属性，
   并改为无条件识别。验收：
   - cohort 完成数不降；
   - 所有已完成记录字节一致；若有文本变化，逐条证明语义等价后再接受；
   - fixture 差分全部通过；
   - 净删代码。
3. **聚合异常拷贝。** guarded payload 与 uniform staging 统一由
   `ExceptionCopyModel` 解释。
4. **文档收敛。** 把 for-of-round80..83 等切片文档合并为
   iterator-activation.md 一篇，旧文档移入 history/。
5. 回到 ISA 账本：61 个 Unmeasured、21 个 SupportedUnverified。

每次改动都在 status.md 中注明：属于哪个统一 owner、复用了什么、删除了什么。
