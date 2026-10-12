# 旧路线删除：测试迁移台账（2026-10-12）

背景：用户要求删除 legacy/shared 旧方案，只保留 pass 通用贯通的 value-tree 路线。
生产代码的删除已完成，共删除 96 个目标文件，范围按符号级 `-dead_strip` 可达性确定。
删除前后，新旧二进制对固定五份 ABC 的 value-tree 产物逐字节一致，唯一差异是删除了
`legacy_writer_used` 字段。

下表列出的测试依赖已删除的旧代码，删除或裁剪须由用户授权。本表既是授权清单，
也是覆盖损失记录。

## A. 整体删除：被测对象就是已删除的旧模块

| 测试目标 | 被测的旧代码 |
|---|---|
| acyclic_body_test、block_loop_body_test | 旧 `WriteAcyclicBody`/`WriteBlockLoopBody` |
| linear_body_test、linear_constants_test、scalar_source_fixture | 旧 `WriteLinearBody` |
| local_lexical_body_test、module_import_body_test、named_scope_test、named_call_test | 旧 `WriteLocalLexicalBody`、`ValidateNamedScopes` |
| base_function_test、base_constructor_test | 旧 `WriteBaseFunction`/`WriteBaseConstructor` |
| class_method_test、class_accessor_test | 旧方法体重载的 `WriteClassMethod(body…)` |
| created_class_test、created_function_test、created_members_test、created_members_scale_test、derived_class_test、record_accessor_test、class_sample_fixture | 旧 `WriteCreatedClass`/`WriteCreatedFunction` |
| define_computed_fields_test、define_static_sample_test | 旧 `RecoverStaticFields` |
| record_import_test、record_object_test、record_literal_test、module_exports_sample_test | 旧 `WriteRecordProgram`/`WriteRecordLiteral` |
| guarded_global_call_sample_test、historical_guarded_global_sample_test、historical_async_member_sample_test | 旧 guarded-global writer 与旧类成员 writer |
| spread_field_constructor_test、spread_field_constructor_sample_test | 旧 spread-field 构造器 writer |
| guarded_reads_test、lexical_value_flow_test、first_for_in_test、capture_propagation_test | 只被旧 writer 消费的分析模块（已删除） |

上述目标的 `.mjs`/`.cmake` 消费者及 CMake 注册一并删除。

## B. 裁剪：被测对象仍保留，只删除借道旧 writer 的部分

| 测试 | 保留 | 删除 |
|---|---|---|
| ordinary_call_test、constructor_call_test、iter_result_test | 分析层 `Lift*` 的正反断言 | 用 `WriteLinearBody`/`WriteAcyclicBody` 渲染并比对文本的断言，以及 `--emit` 输出 |
| exception_arguments_test | `exception_inputs` 的断言 | 依赖已删除 `instruction_ssa` 的夹具部分 |
| object_buffer_test | 能直接验证 object buffer 的断言 | 经旧 body writer 的渲染断言 |
| source_route_test | value-tree 回执校验与序列化断言 | Legacy 回执、`RoutePolicy`、`legacy_writer_used` 用例 |
| record_module_context_test | 模块上下文断言 | 旧 `kLegacyAllowed` 和“缺身份即拒绝”用例（身份现为必需的引用参数） |
| class_tree_context_test、creation_tree_context_test（含 `tests/support/function_segments.h`） | value-tree 类/创建上下文断言 | 旧 `ClassSource` 片段传输和 selection 混合产物用例 |
| source_route_cli.cmake、language_arithmetic_cli.cmake | value-tree CLI 断言 | `--shared-source`/`--class-source`/`--exception-*`/`--record-value-slice` 用例 |
| value_record_slice.mjs、value_created_family.mjs、value_rest_methods.mjs、value_global_call_slice.mjs | 无（这些测试只验证 `--record-value-slice`） | 整体删除（slice 输出的外层是旧路线容器） |

## C. 迁移：改用 value-tree 路线

`structure_source_cli.cmake`、`constructor_record.cmake`、`historical_record_source.cmake`、
`record_constructor.cmake` 调用的是 `abc_structure --source`，改为 `--value-source`。
如果 value-tree 路线不支持其中某项断言，就把该断言改为对类型化失败的断言，并记入
本表的覆盖损失。

## 已完成且不需授权的适配（断言数量不变）

- 72 处 `legacy_writer_used === false` 改为断言该字段不存在。3 个 CMake 检查改为字段缺失判定。
- C++ 测试中的 `!receipt.legacy_writer_used` 改为 `receipt.route == SourceRoute::kValueTree`。
- 测试中 `BuildRecordSource` 的调用改为新签名（身份改为必需参数）。
- `index_cli.cmake`：Linux 保留原有的 prlimit 断言；Darwin 新增断言“限制未建立且如实报告”。

## 执行结果

用户已授权，台账已全部执行，并有两处偏差：

- `value_rest_methods.mjs` 只裁剪了 record-slice 段，保留了 value-tree draft 与行为对照，没有整体删除。
- 额外删除了 `accumulator_flow_test`、`environment_escape_test`，以及
  `language_arithmetic`、`language_numeric`、`named_write`、`derived_constructor` 测试中针对已删分析模块的断言，
  被测模块均已随旧路线删除。

执行后的结果：全量构建 0 错误；固定 65 项 65/65 通过；212 项定向验证中，剩余失败均在
`f0028f54` 基线上以相同原因失败，详见 [status.md](status.md)。
