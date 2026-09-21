# ABC Decompiler

C++20 项目，目标是从动态 ABC 恢复语义可信的 JS/TS/ArkTS，并为每个 record
分别输出文件。参考版本固定为官方 `OpenHarmony-v7.0-Release`，旧版 ABC 仍在范围内。

**当前只完成读取和语料分类基础，不具备源码反编译能力。** 分类成功、头部识别、
静态跳过均不代表方法解码成功或行为等价。

## 构建与测试

环境：Linux/POSIX、CMake 3.20+、C++20 编译器、OpenSSL 3、zlib。
nlohmann/json 使用已固定的本地参考仓，并在配置时验证头文件 SHA-256，见
[依赖说明](docs/reference/dependencies.md)。默认路径为相邻的
`../arkruntime/third_party/json`，可通过 `-DABCD_JSON_ROOT=...` 指定相同版本的位置。

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
```

## 分类清单

```sh
build/release/abc_scan tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc \
  paired /home/rabbit/sample/abc_with_code
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
- 下一步：动态 record/方法元数据索引、按需方法体解码、异步有界调度与语义恢复。

尚未输出任何反编译源码。不得将汇编、占位函数或配套源码复制品作为最终结果。
