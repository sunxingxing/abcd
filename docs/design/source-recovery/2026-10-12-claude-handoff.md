# Claude 开发交接（2026-10-12）

## 0. Claude 接手后的更新（以本节为准）

- **接续位置已变更**：codex 工作树 `codex/resume-generator-post-loop`（`f0028f54`）已覆盖主检出
  `/Users/sun/abcd` 的 `feat/abc-foundation`，并已快进推送到 `origin/feat/abc-foundation`。
  后续开发在主检出进行，codex 工作树不再是事实来源。
  - 主检出原有的 8 个未推送文档提交另存为本地分支 `backup/main-checkout-pre-overwrite-20261012`；
    原有的循环相关未提交改动另存为 stash：`main-checkout loop WIP before overwrite …`。
  - 第 5 节的 `cell.reset` 实验另存为本地分支 `wip/community-reset-experiment`（`1565478f`），
    已从工作区撤回。它是未验证的宿主假设，不合并。
- **审计共识**：Claude 与 Codex（gpt-6-astra，xhigh）两轮交叉审计的问题清单、修复顺序和
  应停止的做法见 [2026-10-12-audit-consensus.md](2026-10-12-audit-consensus.md)。要点是：
  目标为通用引擎，固定五份降为回归集；留出探针显示已准入版本只有 27% 的模块 record
  完整恢复，24.0 版本为 0。
- **已完成**：
  - S0：共识、WIP 处置，以及 `source-cohorts.json` 的角色更新。冻结留出集仍待选取。
  - S1：删除 legacy/shared 旧方案，只保留 value-tree 路线，并按 Codex 第三轮审查清除了
    声明与分支级残留。
  - S5 的 CLI 部分：所有 CLI 共用 `process_limit`，失败时输出结构化报告。
  - S3 的一部分：只有能证明来自 record 自身预算的失败（`kRecordBudget`、`kOutputLimit`）
    才隔离到该 record。底层阶段折叠的 `kResourceLimit` 仍为致命，原因见 status.md。

  证据与已知限制见 [status.md](status.md)，测试去向见 [旧路线测试台账](2026-10-12-legacy-test-ledger.md)。
- **下一步**：按共识依次做 S2（错误链与能力报告）→ S3 余项（结构导出、路径和模块上下文的
  失败隔离，按叶子原因判定致命性）→ S4（profile 能力表与 24.0）。之后是 C5/C3/C2/C4/C8，并冻结新的留出集。第 4–6 节中补 Community
  平台模拟的计划按共识降级为回归维护，不再是主干的下一步。
- 构建注意：主检出的 `build/regression` 用 preset 配置，需要额外指定
  `-DRuby_EXECUTABLE=$PWD/build/deps/ruby/bin/ruby`（Ruby 必须是 3.2.3）。

以下第 1–7 节为 codex 交出时的原文，保留作历史依据。

本文按用户要求记录当前工作树、目标、已验收边界、未完成工作和接续步骤。
**阶段一整体尚未完成；不要从 complete 标志、提交数、函数数量或测试场景数量推算完成百分比。**
本次交接只更新文档，不继续开发，也不把未验收修改提交为实现成果。

## 1. 接续位置与第一步

- 工作树：`/Users/sun/.codex/worktrees/25aa/abcd`
- 分支：`codex/resume-generator-post-loop`
- 交接前最新实现/测试提交：`925e5d92`，`test: exercise public push view APIs and callback behavior`。
- 生产恢复语义仍为 rules311；`git diff b50fe2c2 HEAD -- src include tools` 为空。
  后续提交主要加强独立验收，不能称为生产恢复能力增加。
- **有且仅有一处未提交代码修改**：`tests/integration/community_graph_state.mjs` 的 `cell.reset`。
  它是实验，不是通过验收的修复，详见第 5 节。请保留并先判断，不要直接提交或静默丢弃。
- 交接时没有运行中的测试/覆盖探针；无需等后台任务。阶段一 Goal 未标记完成。
- 原 CLI 历史 session：`01a0e5c6-2009-7403-9768-aec3d50a3587`；当前事实以本工作树为准。
- 本工作树没有 `.codegraph/`，无需建立索引。

先执行：

```sh
cd /Users/sun/.codex/worktrees/25aa/abcd
git status --short
git log -5 --oneline
git diff -- tests/integration/community_graph_state.mjs
```

推荐阅读顺序：本文件 → [当前状态](status.md) 顶部 →
[阶段一验收历史](acceptance.md) → [统一变量有限交付清单](unified-variable-deliveries.md)
及 [统一变量终验账本](unified-variable-acceptance.md)。历史段落的“下一步”可能已被后续记录关闭，
不能将旧缺口全部重新打开。`build/stage-one/pass-convergence-next.md` 已改为指向本文件的本地入口。

## 2. 用户目标与必须保留的设计约束

阶段一目标：沿现有通用 ABC → IR/SSA/CFG → 效果、异常、绑定生命期 → 恢复计划 → AST/打印器
链路，完成 `/Users/sun/abcd/samples/abc_with_code` 中固定五份 ABC 的反编译。
要求可观察语义正确，源码风格、表达方式与参考源码相当，不要求文本逐字一致。
配对源码只作独立验收 oracle；必须先完成 ABC-only 导出，再读取参考源码，不能喂给引擎。

用户已允许独立、显式的语言数值及语言构造语义模式。当前完整输出验收使用
`language_numeric_constructors_v1`，不偷偷修改默认合同，也不承诺复现 Ark 内部 NaN 标签等细节。
默认合同结果仍单列；统一变量 D 终验记录为默认 80 complete / 26 incomplete / 46 no-module，
显式数值与构造合同为 106 complete / 46 no-module。

关键约束：

- 不新增样本名、路径、函数名、opcode 序列、固定寄存器/PC、同块、邻接或固定图形状的生产准入。
  样本可以暴露问题；生产修复必须落在共同语义模型和通用分析中。
- 不盲删正确性检查。每条检查说明证明义务；被共同分析替代后退出决策路径。
- 值传播、存储合并、死定义删除、声明恢复各司其职；共享事实，候选与当前模型使用同一求解。
  版本失效、依赖队列、阶段顺序和终止度量明确；打印器不再独立做语义优化。
- 主干优先；不要先优化 `?.` 等表示细节，也不要把复杂 finally、闭包重构、暂停内部精度
  无限制加入已限定的普通变量里程碑前置条件。
- 保留现有暂停边界：不恢复全套测试、扩展 895 语料、RSS/benchmark/DFX；不新建工作树、
  启动 agents 或 push。固定五输入和相关定向/固定 65 项验证可继续。
  输入 ABC 不可修改；不要降低预算、删反例或缩名单以获得绿色结果。
- 尊重仓库 `AGENTS.md`、算法成本和内存合同。按完整交付报告进度，及时提交连贯且已验证的修改。

## 3. 已验收结果：不要重复做，也不要扩大结论

### 固定回归三分钟目标已达到

`b50fe2c2` 引入独立 `source-regression`：Debug + `-O2 -g -UNDEBUG`，断言和两个工作槽保持。
原固定 65 项从 655.79s 降至 97.30s；随后加强身份对照的完整复验为 **65/65，86.53s**，
测试名逐项一致、无失败/禁用/跳过。首次冷构建 285.82s 单独计时；不能宣称冷构建也小于三分钟。
详细报告：[2026-10-11-source-regression.md](../../validation/2026-10-11-source-regression.md)。

**额外整图专项不等于固定 65 项。** Community 最近受影响完整专项约 203.10s，
Window/navigation 约 223.05s；不应把这些额外矩阵也说成已经小于三分钟。
最新固定 65 项证据早于本次未验收 reset 修改，不能覆盖该 WIP。

### 用户要求的四点纠偏及统一变量里程碑已按有限范围落实

有限清单、按交付连续推进、独立收敛设计及按义务报告已写入统一变量账本。
原始接入 R1–R10、普通 local/catch 动态单元身份、现有模型内共同试算、依赖驱动多轮 pass、
普通临时与循环变量终验分别在 A/B/试算/C/D 的明确范围关闭；不是整个反编译器语义模型完备。
主要证据提交包括 `fd86a761`（B）、`d1ee1ea9`（C/rules311）、`a85ee780`（D）。
复杂协议保留义务通过不等于其内部恢复精度已扩展。

已有退出项包括 CompletedStore 消费路径、same-ScopeId 身份准入、已初始化 guard 的直接 load
限制、rest 的 normal-only 限制和旧手工重复 pass/snapshot 刷新。
旧 SourceAst PrivateCopyOutput 在 value_tree 路径之外，不能声称全仓已删除。
详细前提、暂留边界和旧判断退出证据以账本为准，不重新开启开放式“补齐全部语义”。

### 固定输入的结构与独立执行证据

`63c3134d` 独立读取原始 ABC 的 class index/record 方法与字段计数，核对全部 152 本地 record：
2111 方法、626 record 元数据字段（不是源码字段）。46 个 NoModule 原始方法数均为 0，module 为空。
106 源码的逐 record 方法计数成立：106 模块入口 + 101 构造器 + 754 类方法 + 1141 其他函数
+ 9 静态初始化单元 = 2111。签名和模块接口义务已验收；计数不证明正文等价。

| 固定输入（相对配对根） | 源码数 | 已有行为/表示证据归属 |
|---|---:|---|
| Immersive/modules.abc | 35 | 33 模块 Window/navigation，EntryAbility 完整生命周期，Backup |
| MultiCommunityApplication/modules.abc | 45 | Community ability 图及 Entry/Backup/静态专项；当前正文盲点见后文 |
| pasteboard/modules.abc | 10 | 富文本 5 + Progress 2，共享 CommonConstants 1；另 Home/Logger/Entry/Backup 4 |
| push-kit-sample-code-clientdemo-arkts/modules.abc | 15 | Push 全图、公开 API 和 Widget 专项 |
| push-kit-sample-code-clientdemo-arkts/widgets.abc | 1 | 独立 src_widget 参考的 Widget 专项，不能拿 modules/src 代替 |

模块接口验收覆盖请求次序、具名/default/namespace 依赖、重导出、公开绑定来源、别名单元和
live export 与 default 快照；183 个真实有效 JS 模块变异拒绝。全部签名保留 149 + 751 变异。
后续绑定写入、装饰器和正文时序仍由独立执行负责。

`215548e4` 加强九份静态记录身份/原型/可扩展性图，`7ca70c38` 将联合实例、单元、存储和
namespace 身份图接入 Community/Push/富文本/Progress。不把逐值相等当作对象身份相同。

最新 `925e5d92` 补齐 Push 原先未实际调用的 CustomButton、部分 accessor、getEntryName。
旧 oracle 会漏掉 CustomButton 错误宽度；新增 24 个公开 API 场景真实调用 getter/setter、
读回/恢复、builder 和其注册 callback。最终 **111 场景、430 故障、86 换绑、14 变异、2 等价表示**
通过 8.37s，原场景和反例保持。这只关闭该公开 API 缺口，阶段一仍未完成。

## 4. 当前正在做什么，为什么还没关闭阶段一

当前任务是固定 106 源码的有限正文/初始化/风格核定，不是继续扩展生产语义模型。
已证明“模块求值、view render、结构计数通过”仍可能漏掉真实公开函数正文：Push 已有具体漏检反例。
因此正在检查 Community 的同类实际缺口，不能仅凭所有 record 分配了测试就宣布整体等价。

临时诊断在 `build/stage-one/body-audit/`（ignored、本工作树本地文件，不是提交中的正式验收）：

- `probe.mjs`：Node inspector 精确覆盖探针，读取已有 `instance-identities/*_worker.json`。
  普通基线曾显示 Push 37、Community 145 个函数未进入，只是线索。
- `matrix-probe.mjs`：单进程跑 Community 96 普通 + 104 ability 场景，**512MB 堆 OOM，exit 134**。
  `community-matrix.log` 保留失败；不要提高堆上限或把它当通过。
- `entry-probe.mjs`：改用每场景独立子进程，在生成源码副本的函数入口插入计数。
  200 场景完成，`community-entry.log` 输出 **unexecuted 141 of 886**。
  `community-unexecuted.json`、`community-case-N.json`、`remaining.mjs`、`community-remaining.txt`
  保存方法归属和正文线索。该诊断用插桩源码且不含完整故障矩阵，不能冒充正式语义验收。

具体线索（尚未逐项完成修复/验收）：

1. 多个公开 setter/getter、MainPage.getEntryName、DetailTitleView.checkFoldable、FollowView.updateShow
   和 PictureDetail.aboutToDisappear 没有在上述正常矩阵中实际进入。
2. 实际生成源码通过 `ListItemGroup.create({header: itemHeader.bind(this, groupTitle)})` 提交 header。
   当前测试平台 `uiMethod` 没有执行该 header builder，导致 HotListView.itemHeader 及其闭包未执行。
   这是测试宿主覆盖缺口线索，不是已经证明生产恢复错误。
3. callback 回放目前只取一次 `[...callbacks]` 快照，后续响应式更新新注册的回调可能没进入。
   不能因此无限回放，也不能调用隐藏/禁用 click 或制造真实流程不存在的行为。
4. 未执行并不自动等于缺陷：空函数、无引用且不公开的内部构造器无需为了覆盖率强行暴露；
   错误回调另有故障矩阵，正常矩阵未执行不能直接判其无证据。

## 5. 唯一未提交 WIP 与明确失败的复现

文件：`tests/integration/community_graph_state.mjs`，`cell.reset` 当前 diff：

```js
reset(value) {
  const old = entry.value; entry.value = value;
  if (!Object.is(old, value)) changed(entry);
  return token;
},
```

已提交版本仅赋值并返回 token。实验意图是让单向参数 reset 在值变化时触发已注册 watch 和依赖更新。
现有 `cell.set`、`changed`、`notify`、`declareWatch` 已有共同机制；不应另建样本专用通知路径。
**这项修改尚未通过完整矩阵，也没有证据说明它独自补齐 checkFoldable。**
它是受控平台模型的假设，未核对正式 ArkUI reset 语义，不能称为平台规范事实。

反例将真实 DetailTitleView.checkFoldable 改为有效 JS 空正文。配置为：

```js
{ability: true, width: 700, density: 1, resizeWidths: [840, 700],
 storage: {isShowInput: true, isFoldHorizontal: true}}
```

生成脚本 `body-audit/community-watch-red.mjs`；输入 `community-watch-mutant.json`。
WIP 前 `community-watch-red.log`、WIP 后 `community-watch-green.log` 都失败，后者名字不代表通过：
`undetected valid-JS mutation one-way reset lost watch write`，`false !== true`，exit 1。
意味着 oracle 仍未检出该错误变异，不能说恢复引擎有同样错误。

复现（只运行该反例，不启动全矩阵）：

```sh
/Users/sun/.nvm/versions/node/v25.7.0/bin/node \
  --max-old-space-size=512 --expose-gc --experimental-vm-modules \
  tests/integration/community_graph_worker.mjs \
  < build/stage-one/body-audit/community-watch-mutant.json
```

待验证的时序解释：`platform.resize(drain)` 在 destination 子 view 创建前发生，后续
`updateStateVars` 可能 reset 为相同值，所以新增 changed 仍未触发 watch。不要把此推测当结论。

## 6. 后续按完整闭环推进

1. **先处理 reset WIP 和反例。** 核对实际响应式合同、view 创建与参数更新时间。
   决定保留/调整/撤回该实验，并记录原因；不把红色反例或未跑矩阵当已验收交付。
2. **补有限的真实 Community API/参数变化场景。** 可参考刚完成的 Push：实际 getter/setter
   调用及读回/恢复；完整参数对象中改变 currentBreakpoint（sm/md/lg），调用真实 updateStateVars。
   保持类型有效，不给所有对象字段盲塞原始值、不替换真实方法正文。将新场景与旧故障事件序号隔离。
   检查 static getEntryName 与实际生命周期 aboutToDisappear。
3. **接通现有 header builder 路径。** 在受控 UI 平台执行真实 ListItemGroup header，保留 bound this、
   默认参数和 groupTitle 捕获。若要扩展 callback 回放，先定义有界的真实生命周期，不新增无限队列。
4. **以反例证明补上了什么，再完整验收受影响范围。** 保留原场景、故障、换绑和变异；新增有效 JS
   反例先证旧模型漏检，再证新模型拒绝。共享 Community 状态模型改变后，两种相关 Community 矩阵
   都可能受影响，不能只跑新 case 就提交为通过。未变动的长矩阵无需每个小步反复重跑。
5. **回到固定 106 记录的终验账本。** 对初始化、公开行为、身份、异常和表示各项写明已覆盖/具体剩余义务。
   仅对发现的具体缺口补实现或测试。没有明显漏分配 record，不再增加无穷的未来平台/ISA 前置条件。
   如发现生产错误，落在共同模型修复并做布局等价/语义反例；最后以实际义务关闭阶段一，不能靠数量宣布。

本交接没有承诺整体完成时间或百分比。普通变量里程碑、固定 65 项速度、全阶段验收是三件事。

## 7. 工具入口、代码导航和验证证据

日常构建/固定名单（已有配置）：

```sh
cmake --build --preset source-regression
ctest --preset source-regression
```

额外定向矩阵示例（执行前确认改动确实需要它）：

```sh
ctest --test-dir build/regression --output-on-failure -R '^push_graph_source_test$'
ctest --test-dir build/regression --output-on-failure -R '^community_(ability_)?graph_source_test$'
```

`build/regression/abc_structure`、`abc_semantic` 为当前工具。CMake 的测试注册保存准确参数与输入路径。
手工 Node 使用上述 v25.7.0；内部 Acorn 需要 `--expose-internals`，VM module 需要
`--experimental-vm-modules`。不要在普通未优化 Debug 的 build 目录测完又当作优化 preset 的结果。

重点文件：

- `tests/integration/community_graph.mjs`：场景、变异、执行入口。
- `community_graph_state.mjs`：view/cell/watch/UI 队列/回放，当前 WIP 所在。
- `community_graph_worker.mjs`：reference/recovered 独立执行与对照。
- `push_graph.mjs`、`push_graph_state.mjs`、`push_graph_worker.mjs`：最新公开 API 闭环参考。
- `graph_trace.mjs`：真实 namespace 与实例共同身份图。
- `paired_signatures.mjs`、`record_inventory.mjs`、`module_surface.mjs`：五输入结构/接口核对。
- `tests/source-regression.json`、`CMakePresets.json`、`CMakeLists.txt`：固定名单、构建与注册。
- `tests/corpus/abc-index.json`：输入身份与分类；输入不要修改。

除另标外，以下证据路径相对本工作树，均为本地 ignored 产物，换机器不会随 Git 自动传输：

| 验证范围 | 最新证据 | 结论/限制 |
|---|---|---|
| 固定 65 | `build/stage-one/instance-identities/fixed65.log`、`fixed65-lasttest.log`；`build/regression/regression-current.xml` | 65/65，86.53s；不是 reset WIP 验收 |
| Push 公开 API | `build/stage-one/body-audit/push-api-tests.log`、`push-api-lasttest.log`；`build/regression/push-public-api.xml` | 完整 Push 8.37s 通过；`push-api-red.log` 保留旧漏检 |
| 原始计数/接口/签名 | `build/stage-one/record-inventory/{build-final.log,final-tests.log,final-lasttest.log}` | 构建及 6/6，4.23s |
| 联合实例身份 | `build/stage-one/instance-identities/{tests.log,lasttest.log}`；`build/regression/instance-identities.xml` | 6/6，231.41s；Community 203.10s，Push 8.14s，富文本 12.41s，Progress 6.78s |
| 静态身份图 | `build/stage-one/static-initialization/{tests.log,lasttest.log}` | 3/3，0.99s |
| 嵌套签名 | `build/stage-one/nested-signatures/{tests-2.log,lasttest-2.log,fixture-final.log}` | 共同 3/3 及 fixture 补验；签名不代替正文 |
| Immersive Ability | `build/stage-one/immersive-ability/final-tests.log` | 3/3，5.90s；实际五模块生命周期 |
| Community 当前实验 | `build/stage-one/body-audit/community-watch-green.log` | **失败，未检出变异；不能称绿色** |

历史失败保留，不拼成首次全绿：`window-final/tests.log` 初次 3/4（Widget 失败），
后续 `widget-lasttest.log` 才通过；`window-final/remaining-records.log` 初次 4/5
（retained_call_nullish 失败），`retained-final.log` 和 `immersive-ability/final-tests.log` 后续通过。
`community-matrix.log` 为 OOM，不能列入通过证据。

如果本地 ignored 文件丢失，从跟踪的测试入口重新生成所需证据，不从本文编造结果。
本次交接未重跑构建或回归；仅核对 Git、文档、现存日志和无运行中测试的状态。
