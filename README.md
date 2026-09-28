# ABC Decompiler

C++20 项目，目标是从动态 ABC 恢复语义可信的 JS/TS/ArkTS，并为每个 record
分别输出文件。参考版本固定为官方 `OpenHarmony-v7.0-Release`，旧版 ABC 仍在范围内。

**当前已有受证明的局部源码输出，但尚不能完整反编译动态语料。**
读取、分类、元数据索引、方法体解码、普通控制流及异常派发映射已覆盖本地动态语料；
record 结构和部分函数体可输出源码。解码、方法体源码准入与整个 record 的行为等价
是不同验收层级，不能互相替代。

## 鸿蒙参考源码

三个官方参考仓库以 submodule 放在 `references/openharmony/arkcompiler/` 下：
`runtime_core`、`ets_runtime`、`ets_frontend`。均固定到 `OpenHarmony-v7.0-Release`
tag 对应提交，与 [版本锁定文件](docs/reference/openharmony-v7.0-release.lock.json) 一致；
不跟踪移动分支。首次获取或切换本项目提交后执行：

```sh
git submodule update --init --depth 1
```

这些是参考源码，不代表已构建完整鸿蒙工具链。使用本地 submodule 的 ISA 时，可传入
`-DABCD_ISA_ROOT="$PWD/references/openharmony/arkcompiler/runtime_core/isa"`；
其他构建依赖仍按下文配置。

## 构建与测试

环境：Linux/POSIX、CMake 3.20+、C++20 编译器、Ruby 3.2.3、OpenSSL 3、zlib；集成测试还使用
util-linux 的 `prlimit` 验证继承的内存限制。
nlohmann/json 使用已固定的本地参考仓，并在配置时验证头文件 SHA-256，见
[依赖说明](docs/reference/dependencies.md)。默认路径为相邻的
`../arkruntime/third_party/json`，可通过 `-DABCD_JSON_ROOT=...` 指定相同版本的位置。
ISA 枚举及格式表通过固定版本的上游 Ruby API 生成，默认参考路径为
`../arkruntime/arkcompiler/runtime_core/isa`，可通过 `-DABCD_ISA_ROOT=...` 指定。

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
```

## 分类清单

```sh
build/release/abc_scan tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc \
  paired /Users/sun/abcd/samples/abc_with_code
```

实际语料目录为 `~/sample/`，不是 `~/samples/`。扫描器只读取 ABC，不读取配套源码。
首次扫描分类并写入清单；之后流式重算文件指纹，复用相同 SHA-256、检测规则版本
的结果。静态 ABC 的证据已写入 [abc-index.json](tests/corpus/abc-index.json)。
未知、损坏和静态分类分开记录，不以解析失败推断静态。

选中的 corpus ID 会以当前目录快照替换，其他 ID 的记录保留；路径更新、内容变化、
规则版本变化均不会盲目沿用旧路径记录。锁冲突会明确失败，不覆盖另一扫描会话。
替换前失败保留旧清单；替换完成后若目录同步失败，返回 `kCommittedNotDurable`，
表示新清单已可见但持久性未确认，不承诺回滚。

CLI 使用 1,000,000,000 字节的虚拟地址空间上限，并报告峰值 RSS；不会放宽更低的
继承限制。当前扫描器无子进程。后续完整反编译仍须独立通过同一内存上限验收。

## Record 与方法索引

```sh
build/release/abc_index tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc \
  paired /Users/sun/abcd/samples/abc_with_code
```

索引器核对清单中的文件哈希及检测规则版本，直接跳过已确认的静态文件；内容变化
会报错，需先重新运行分类扫描。输出 JSON 包含每个文件的索引结果、record/方法
数量、方法体字节数和进程峰值 RSS，不写入或改动输入 ABC。

每次只构建一个文件的元数据索引；函数指令保留在文件中，索引只保存位置与长度。
区域引用通过有序区间查询，方法归属从 class 内联成员序列和区域引用交叉校验。
此工具同样施加 1G 进程地址空间限制，尚未承担函数语义恢复或源码生成。

## 方法解码与普通控制流

```sh
build/release/abc_decode tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc paired /Users/sun/abcd/samples/abc_with_code
build/release/abc_flow tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc paired /Users/sun/abcd/samples/abc_with_code
```

解码保留强类型操作数、原始数值位模式、分支目标以及有序异常范围与处理器。
构图保留不可达代码和条件分支的两个结果，生成器恢复边与普通顺序边分别表示。
异常范围边界会分块，但普通图不含异常派发边，不可用于直接证明完整函数语义。

验证工具顺序处理文件和方法，同时只保留一个方法体及其图；输出 JSON 流式记录
每个文件的计数和所有方法失败位置，不截断诊断。单方法解码和额外构图预算各为
64 MiB，进程总地址空间仍限制在 1,000,000,000 字节。同步验证工具不是未来默认的
异步、并行、懒加载函数反编译接口。

## 异常派发映射

```sh
build/release/abc_exceptions tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc paired /Users/sun/abcd/samples/abc_with_code
```

该阶段按固定版本动态解释器的规则，选择文件顺序中第一个覆盖当前 PC 且有 catch
的 try，再选择其中第一个处理器；不按嵌套深度或 catch 类型重排。半开区间映射
与普通图分开保存，只回答异常已经发生时的本地目标。没有本地目标表示向调用者传播。
覆盖指令数不表示可抛异常的指令数；异常发生前的精确寄存器状态仍需后续分析。

映射的额外预算默认 64 MiB，所有分析仍受相同 1G 进程总限制。该验证器不读配套源码，
不生成占位函数，也不把静态跳过计作动态反编译成功。

## 常量引用验证

```sh
build/release/abc_references tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc paired /Users/sun/abcd/samples/abc_with_code
```

该阶段在方法解码、普通图和异常派发验证后，按实体角色和池索引去重解析引用，
并读取指令直接引用的字符串和字面量缓冲区。方法引用目前仅验证实体范围，
嵌套字面量及其内部引用不递归加载；通过检查不表示调用目标或源码语义已验证。
计数按方法累计，同一实体跨方法出现会重复计数；失败方法可能贡献部分已验证计数。
字符串和字面量逐个读取并释放，不缓存全语料。进程仍受同一 1G 地址空间限制。

## 模块元数据验证

```sh
build/release/abc_modules tests/corpus/abc-index.json \
  batch /Users/sun/abcd/samples/abc paired /Users/sun/abcd/samples/abc_with_code
```

该工具只运行字段、封装和模块元数据检查，不重复方法体分析。merged 文件按运行时
规则选择 record，检查模块请求、导入导出表及其字符串引用。bundle 文件按特殊类
发现单个运行时 record；含模块字段时须在 MANIFEST 前传入
`--bundle-descriptor VALUE`，VALUE 必须是运行时构造器收到的描述符，不能猜成
宿主路径。该值应用于本次命令的所有 bundle 文件；不同描述符应分批运行。
缺失所需描述符会返回非零状态，不作为成功或静态跳过。请求阶段表、JSON 内容语法、
跨模块绑定和源码行为等价尚未验证。

## 普通控制流支配树

普通控制流支配树可用 `abc_dominators` 验证，参数与 `abc_flow` 相同。
输出直接支配树构建数量、可达块数、算法操作计数及额外分配峰值；暂不包含异常边，
不能用该结果证明异常路径上的 SSA 或源码语义正确。

## Sanitizer

```sh
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DABCD_BUILD_TOOLS=OFF \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/asan -j 4
ctest --test-dir build/asan --output-on-failure
```

ASan 需要巨大的虚拟 shadow 映射，因此该配置验证库和扫描工作流，不启动具有
1G 地址空间限制的 CLI。实际 CLI 的内存限制及峰值由普通构建单独验证。

## 进度与约束

- [项目规则](AGENTS.md)
- [总体设计](docs/superpowers/specs/2026-09-22-abc-decompiler-design.md)
- [语料分类验证记录](docs/validation/2026-09-22-corpus-ingestion.md)
- [Record 索引验证记录](docs/validation/2026-09-22-record-index.md)：402 个动态文件，
  281,067 个 record、5,486,254 个方法；索引阶段峰值 RSS 约 151MB。
- [方法解码验证记录](docs/validation/2026-09-22-method-decode.md)
- [普通控制流验证记录](docs/validation/2026-09-22-normal-flow.md)：全部动态方法通过
  编码与普通构图验证，尚不代表源码语义正确。
- [异常派发验证记录](docs/validation/2026-09-22-exception-dispatch.md)：所有动态方法
  通过派发映射及处理器基本块边界检查，峰值 RSS 约 150 MB。
- [直接引用验证记录](docs/validation/2026-09-22-direct-references.md)：全部动态方法
  通过引用范围及直接字符串/字面量解析检查，峰值 RSS 150,278,144 字节。
- [构造调用源码验证](docs/design/constructor-calls.md)：最新 900 文件探测中，402 个动态
  ABC 的 5,486,254 个方法有 1,500,536 个方法体准入源码输出；498 个已分类静态文件
  跳过，报告峰值 RSS 178,135,040 字节。该数字不代表完整 record 数或行为等价。
- 当前未过关：实例字段、全局声明、完整源码签名与函数体整合；精确异常状态、一般
  控制流结构化、历史版本源码语义和默认有界异步并行懒加载方法调度器也未完成。
  ISA 按上游 group 顺序推进；`constant object loaders` 已有类型化语义，但只有经证明
  的子集准入源码，下一组是 `iterator instructions`。

不得将汇编、占位函数或配套源码复制品作为最终结果。
