# big.abc 完整反编译基线（2026-10-28）

输入为 `samples/big.abc`（114,709,204 字节，sha256 `d22cdea7…bb242`），属于大型
支付宝应用：alipayportal 加 @alipay 系列 HAR，共 13,974 条记录。

- 复现命令：
  `python3 tests/tools/source_baseline.py build/regression/abc_structure samples/big.abc OUT SUMMARY.json`
- 机器可读结果：`tests/corpus/big-abc-baseline.json`。

## 基线（commit 91701e09，source-regression 构建，单进程）

| 指标 | 值 |
|---|---:|
| 墙钟时间 | 42.7 s |
| 峰值 RSS | 159.7 MB（低于 1 GB 上限） |
| 记录数 | 13,974 |
| 完整（status 0） | 1,176 |
| 部分（status 4，缺失成员已标明） | 83 |
| 拒绝（status 3） | 12,660 |
| 非执行（module context / no module） | 55 |
| 完整率（占可执行记录） | 8.45% |
| 已发布源码 | 完整 1.38 MB，部分 0.19 MB |
| 语法错误 | 0（共 1,259 个文件） |

语法检查由 `tests/tools/check_source_syntax.mjs` 完成：`.mjs` 按 ES module 检查；
`.ets` 先把 `import lazy` 改为 `import` 并擦除类型，再按 ES module 检查。
这样处理是因为用户已批准的 ArkTS 原生形式（`import lazy`、Sendable 类的带类型
字段）Node 无法直接解析。最初直接用 `node --check` 报出的 209 个"错误"全部属于
这两种合法形式。

## 当前（commit c2a0319d：phased 模块入口 + code_size 0 handler）

| 指标 | 值 |
|---|---:|
| 墙钟时间 | 50.0 s |
| 峰值 RSS | 159.8 MB |
| 完整 / 部分 / 拒绝 | 1,460 / 83 / 12,376 |
| 完整率（占可执行记录） | 10.49% |
| 已发布源码 | 完整 2.15 MB，部分 0.19 MB |
| 语法错误 | 0 |

`CleanupError::kHandler` 已不再出现。模块入口进入 phased 路线后，原先被它遮住的
函数级原因成为主要拒绝原因：

| 原因 | 记录数 |
|---|---:|
| `ExceptionInputError:1` | 4,419 |
| `CreationContextError:3`（phased 创建者内带静态字段的类） | 3,212 |
| `FamilyCreationCheck:13` | 1,222 |
| `SemanticProofError:3 > InternalValueReason:6` | 606 |
| `EvaluationShapeError:0` | 490 |
| `DefaultBodyError:2` | 409 |
| `FamilyTreeError:2` | 362 |
| `FamilyCreationCheck:17` | 320 |
| `AwaitRegionError:1` | 312 |
| `CatchRegionError:3`（normal 流进入 catch，如优化后的 for-of） | 203 |

下面两节记录的是最初基线（commit 91701e09）。

## 拒绝原因分布与根因

| 原因 | 记录数 | 根因 |
|---|---:|---|
| `CleanupError::kHandler` | 11,175（88%） | 模块入口 `func_main_0` 含真实的顶层 `try/catch`（主要是 `_qf_globals_*` 注入的 `try { … } catch (e) { G["get$e$…"] = e; }`，以及空 `catch {}`）。模块根没有 owner，无法进入已支持 try/catch 的 phased 路线，只能走 cleanup-entry 路线；该路线只接受"poplexenv 后 rethrow"的环境清理 handler，因此整条记录被拒。 |
| `SemanticProofError:3 > InternalValueReason:6` | 286 | 待查 |
| `DefaultBodyError:2` | 285 | 待查 |
| `CatchRegionError:1` | 259 | 待查 |
| `ExceptionInputError:1` | 205 | 待查 |
| `TreeObligation::kDecisionPrefix` | 189 | 待查（受保护的条件放置） |
| 其他 | 约 260 | 长尾 |

## 输出质量检查（1,176 个完整文件）

没有发现语义错误。逐项结论：

- `static X = X;` 是用同名模块变量初始化静态字段，属于合法语义。
- `"" + a + ""` 是对 ADD2 字符串拼接的忠实还原。

以下属于可读性问题（语义等价，计划清理）：

- 未使用的帧别名 `let $localN = new.target;` 和 `let $localN = this;`，几乎每个函数都有。
- 循环体末尾冗余的 `continue;`：206 处，分布在 56 个文件。
- 函数尾部出现 `return void 0;`（共 204 处），可以直接写成 `return;`。

## 计划

1. 让模块根进入 phased 路线，使带真实 `try/catch` 的模块入口得到统一处理（预计覆盖
   约 1.1 万条记录）。这是对现有 owner 的扩展，不新增并行机制。
2. 补充可读性清理，并用 cohort 做字节对比，确保只改变上述冗余形式。
3. 逐一处理长尾原因。
