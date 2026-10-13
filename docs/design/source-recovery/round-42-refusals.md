# Round 42 拒绝重计表（2026-10-14）

基线 `1c8dee55` / `explore40`、`explore40s`；结果来自本轮工作区构建。
旧 flags 为 `--language-numeric --language-constructors`；source 为 `--language-source`。
两组各 620 个模块 record、387 个非模块 record。旧 flags complete **479**、拒绝 **141**；source complete **511**、拒绝 **109**。
逐 record status、failure domain、外层 error/class/reason 零变化；479/511 个已发布文件逐字节相同。

完整机器可读证据：`/private/tmp/r42-final-cohort/results.json`。这是首拒绝分布，不是解除该门槛后的完成数预测。

## 完整首拒绝链分布

| 原因链（现有枚举值） | 旧 flags | source |
|---|---:|---:|
| `FunctionTreeError=1 > ExpressionError=4 > CallPlanError=4 > CallReferenceError=1 > EvaluationKind=1 > TemporaryReason=4` | 13 | 14 |
| `FamilyTreeError=5 > DeferredCellError=0` | 9 | 10 |
| `FunctionTreeError=0 > TreeObligation=3` | 8 | 9 |
| `PhasedMethodError=0 > SemanticError=1` | 8 | 8 |
| `EvaluationError=5` | 7 | 8 |
| `SemanticError=1` | 6 | 7 |
| `TreePrintError=3` | 6 | 6 |
| `CreationContextError=3` | 5 | 6 |
| `CaptureCellError=3 > CaptureError=1 > ScopeError=3` | 18 | 5 |
| `FamilyFileError=1` | 5 | 5 |
| `FunctionAbiError=7` | 4 | 4 |
| `FunctionEmitError=1` | 4 | 4 |
| `SemanticProofError=3` | 4 | 4 |
| `EvaluationError=2` | 2 | 3 |
| `ControlError=3 > ControlOwnershipError=0` | 1 | 3 |
| `FunctionTreeError=5 > TreeObligation=6` | 0 | 3 |
| `FunctionTreeError=0 > TreeObligation=2` | 2 | 2 |
| `DerivedProjectionError=1` | 15 | 1 |
| `BodyEmitError=7 > ObjectBufferError=0` | 1 | 1 |
| `ClassInitializerError=1` | 1 | 1 |
| `ClassSelfOrderError=2` | 1 | 1 |
| `FamilyEnclosureError=1` | 1 | 1 |
| `FunctionTreeError=9 > TreeSemanticError=0 > TreeFlowError=4 > TreeObligation=10` | 1 | 1 |
| `RecordModuleKindError=1` | 1 | 1 |
| `FamilyWriteError=3` | 0 | 1 |
| `ModuleEnclosureError=10` | 13 | 0 |
| `ArrayInitializationError=0` | 5 | 0 |

`TreeObligation`: 2=DecisionEdgeCopies，3=DecisionPrefix，6=ExpressionOwnership，10=Semantics。
`DeferredCellError=0` 为 ExternalRoot；`RecordModuleKindError=1` 为 Shared。
`TreeFlowError=4` 为 Placement；`SemanticError=1` 为 UnsupportedInstruction。

## 树阶段逐 record

`O`=两套 flags 都出现；`S`=仅 source 首拒绝出现。PC 栏为原始/工作 PC；`—` 表示检查没有指令身份。

| 组 | Record | Method | 首拒绝义务 / 叶子 | PC 原始/工作 | 实际局部身份 |
|---|---|---:|---|---|---|
| O | o1/3011 | 3322 | `FunctionTreeError=0 > TreeObligation=3` | —/— | block=16, control=16 |
| O | o1/12874 | 13317 | `FunctionTreeError=0 > TreeObligation=3` | —/— | block=16, control=16 |
| O | o2/10579 | 11217 | `FunctionTreeError=9 > TreeSemanticError=0 > TreeFlowError=4 > TreeObligation=10` | —/— | 尚无局部 witness；不推测 PC |
| O | o6/5058 | 5417 | `FunctionTreeError=0 > TreeObligation=3` | 33/30 | block=28, operation=14 |
| O | o18/28264 | 29676 | `FunctionTreeError=0 > TreeObligation=3` | 97/94 | block=80, operation=40 |
| O | o18/31094 | 32347 | `FunctionTreeError=0 > TreeObligation=3` | —/— | block=3, control=3 |
| O | o18/43021 | 44615 | `FunctionTreeError=0 > TreeObligation=3` | —/— | block=3, control=3 |
| S | o19/12741 | 13192 | `FunctionTreeError=5 > TreeObligation=6` | 88/85 | block=66, operation=33; expected Op=31 |
| S | o19/28925 | 29433 | `FunctionTreeError=5 > TreeObligation=6` | 88/85 | block=66, operation=33; expected Op=31 |
| O | o20/10622 | 11730 | `FunctionTreeError=0 > TreeObligation=2` | 139/139 | block=6, control=6, edge=10, operation=54 |
| O | o24/37520 | 38116 | `FunctionTreeError=0 > TreeObligation=3` | —/— | control=1396, synthetic={'kind': 2, 'source': 8} |
| O | o25/33281 | 33743 | `FunctionTreeError=0 > TreeObligation=2` | 421/421 | block=7, control=7, edge=12, operation=160 |
| S | o32/49116 | 49302 | `FunctionTreeError=0 > TreeObligation=3` | 347/342 | block=264, operation=132 |
| S | o34/10794 | 11425 | `FunctionTreeError=5 > TreeObligation=6` | 87/84 | block=66, operation=33; expected Op=31 |
| O | o38/6590 | 7794 | `FunctionTreeError=0 > TreeObligation=3` | —/— | block=0, control=0 |

语义失败 o2/10579 已揭示 `TreeSemanticError::kFlow > TreeFlowError::kPlacement`；当前仍为方法级定位，尚无局部 witness；保留无 PC，不推测具体操作。
没有把上述 decision prefix 的相同枚举视为已经证明同一根因；block/control/synthetic identity 供下一轮继续追踪。

## Deferred 逐 record

全部为 ExternalRoot，旧 flags 9、source 10；没有 PhasedParent/Placement/Binding 首拒绝。
下表 requested=Guarded；occurrence 均为 0，slot=0/depth=0。allocation/store PC 均未归一化。

| Record | Creator | Child / occurrence PC | Cell | 创建 PC | Allocation PC | Store PC | flags |
|---|---:|---|---:|---:|---:|---:|---|
| o5/28303 | 28528 | 28988 / 22 | 0 | 13 | 6 | 28 | O |
| o8/10284 | 11497 | 12104 / 43 | 12 | 189 | 182 | 204 | O |
| o8/22863 | 23012 | 23067 / 123 | 0 | 13 | 6 | 28 | O |
| o8/67870 | 68031 | 68113 / 19 | 1 | 28 | 21 | 43 | O |
| o15/7013 | 7170 | 7225 / 129 | 0 | 13 | 6 | 28 | O |
| o15/37098 | 37717 | 38016 / 45 | 12 | 195 | 188 | 210 | S |
| o15/44200 | 44369 | 44451 / 19 | 1 | 28 | 21 | 43 | O |
| o28/10155 | 11093 | 11420 / 45 | 13 | 199 | 192 | 214 | O |
| o28/21809 | 21963 | 22018 / 129 | 0 | 13 | 6 | 28 | O |
| o28/67233 | 67399 | 67481 / 19 | 1 | 28 | 21 | 43 | O |

## Phased input 与入口模块

两套 flags 都是 8 个 `PhasedMethodError::kInput > SemanticError::kUnsupportedInstruction`：

| Record | Method | PC 原始/工作 | 实际指令（原始解码） |
|---|---:|---|---|
| o20/6952 | 7171 | 93/93 | GETITERATOR_IMM8 |
| o32/29268 | 29450 | 90/90 | GETPROPITERATOR |
| o32/42397 | 42548 | 23/23 | GETPROPITERATOR |
| o32/43537 | 44469 | 28/28 | GETPROPITERATOR |
| o32/46008 | 47128 | 173/173 | GETITERATOR_IMM8 |
| o32/53454 | 54047 | 109/109 | DELOBJPROP_V8 |
| o37/20175 | 20512 | 294/294 | GETITERATOR_IMM8 |
| o38/25106 | 25655 | 317/317 | GETITERATOR_IMM8 |

因此按实际指令分为 iterator 4、property enumeration 3、delete 1；尚未解除准入。
两组 o14/24892 LruCache 的首拒绝都是 `RecordProgramError::kModule > RecordModuleKindError::kShared`：
`isSharedModule=1` 的入口限制，不是一个尚未定位的函数体错误，故无方法或 PC。

## SemanticProof PC 归属修正

| Record | 旧 method/PC | 真实 method/原始 PC/工作 PC | 验证 |
|---|---|---|---|
| o0/4660 | 4850/12 | 5040/12/12 | `--instructions` 中 12 为真实指令边界 |
| o20/8166 | 8392/13 | 8556/13/13 | `--instructions` 中 13 为真实指令边界 |

错误发生于类 initializer 的 FunctionInput；以前外层 FamilyFile owner 覆盖了该叶子的实际方法。
现在叶子携带 owner。没有把内部偏移四舍五入到附近指令，也没有以父创建 PC 冒充子函数失败 PC。
