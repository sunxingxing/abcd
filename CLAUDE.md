# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**`AGENTS.md` holds the binding project rules: goals, naming/typing, the 1 GB memory cap,
the O(n²) limit, the method-task executor, tests, corpus handling and git workflow. Read it
first. This file does not repeat it.** Where documents conflict, the latest user instruction
wins, then AGENTS.md and the specialist contracts under `docs/`, then older design notes.
README and most design docs are in Chinese.

## What this is

A C++20 decompiler for OpenHarmony/ArkCompiler **dynamic** ABC bytecode. It aims to produce
semantically faithful JS/TS/ArkTS, one output file per record. The reference version is pinned
to `OpenHarmony-v7.0-Release` (`docs/reference/openharmony-v7.0-release.lock.json`), and older
ABC formats are still in scope through explicit profiles. Static ABC is classified and
skipped. The engine can already emit partial source that is proven correct, but it cannot yet
decompile whole files. For current state and open gates, read
`docs/design/source-recovery/status.md`.

## Build and test

Dependencies: CMake 3.20+, Ruby **exactly 3.2.3** (`find_package(... EXACT)`; it generates the ISA tables from upstream `isa.yaml`),
OpenSSL 3, zlib, a pinned nlohmann/json (its header SHA-256 is checked at configure time; see
`docs/reference/dependencies.md`), and Node (for the `.mjs` integration tests).

```sh
git submodule update --init --depth 1        # references/openharmony/arkcompiler/{runtime_core,ets_runtime,ets_frontend}
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DABCD_ISA_ROOT="$PWD/references/openharmony/arkcompiler/runtime_core/isa" \
  -DABCD_JSON_ROOT="$PWD/build/deps/json" \
  -DABCD_PAIRED_CORPUS_ROOT="$PWD/samples/abc_with_code"
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure
```

`build/deps/json` is the local pinned copy; without `ABCD_JSON_ROOT` the default is
`../arkruntime/third_party/json`.

- The existing local dev tree is `build/` itself (Ninja, Debug, with ISA/JSON/paired-corpus
  paths already configured). To rebuild and run a single test there:
  `cmake --build build --target loop_exit_test && ctest --test-dir build -R '^loop_exit_test$' --output-on-failure`.
- Each unit test is its own executable and CTest test, registered by hand in the single
  top-level `CMakeLists.txt`. When you add a test, add `add_executable` + `add_test` there.
- `tests/integration/*.mjs` tests run a C++ test binary under Node and check the generated JS.
  They are registered only when `ABCD_NODE_EXECUTABLE` is found. Tests that need real samples
  are registered only if files under `ABCD_PAIRED_CORPUS_ROOT` exist.
- `-DABCD_BUILD_TOOLS=OFF` skips the CLI tools. Use it for ASan/UBSan builds, because the CLIs
  set a 1 GB address-space limit that the sanitizer shadow memory cannot fit in (README
  "Sanitizer" has the full command).
- Build logs and other scratch output go in build directories and stay untracked.
- Formatting: `.clang-format` (Google style, 80 columns). There is no lint target.

## Corpus CLI tools (`tools/`, built into the build dir)

They all take the same arguments, a manifest followed by labelled corpus roots:

```sh
build/release/abc_scan tests/corpus/abc-index.json \
  batch samples/abc paired samples/abc_with_code
```

`abc_scan` classifies files and writes the manifest. The other tools (`abc_index`,
`abc_decode`, `abc_flow`, `abc_exceptions`, `abc_references`, `abc_modules`,
`abc_dominators`, …) check the manifest's content hash and detector revision, and they skip
files already classified as static. Each one validates a single pipeline stage across the
corpus and streams JSON that includes peak RSS. `abc_modules` needs
`--bundle-descriptor VALUE` for bundle files that have module fields. These synchronous
validators are not the intended asynchronous method-task interface.

Source output has exactly one route, the pass-based value-tree pipeline; the legacy and shared
writers were removed on 2026-10-12. `abc_structure INPUT.abc OUT_DIR [--value-source]` exports
structure only, or structure plus value-tree source per record (`--source`/`--shared-source`
are rejected). `abc_semantic INPUT.abc METHOD [--function-source|--record-source|...]`
inspects one method on the same route. All CLIs share `tools/process_limit.h`: Linux applies a
1 GB `RLIMIT_AS`; macOS reports `process_limit.established: false` instead of claiming it.

## Architecture

The pipeline is split into layered static libraries. A lower layer never depends on a higher
one:

1. **`abcd_abc`** (`src/abc`, `include/abcd/abc`): the container/format layer. It covers the
   header, the metadata index (records, methods, region refs), string/literal entities,
   debug info and scopes, module info, and instruction decoding into strongly typed operands.
   The ISA enums and format tables are generated from the pinned upstream ISA via Ruby into
   the build tree. Never hand-write opcodes.
2. **`abcd_file` / `abcd_corpus`** (`src/corpus`): file mapping, static/dynamic detection,
   and the persistent `tests/corpus/abc-index.json` manifest (bounded hashing, atomic
   replace, locking).
3. **`abcd_analysis`** (`src/analysis`, about 100 headers): per-method semantics. Normal CFG
   and dominators are kept separate from exception dispatch (first matching try in file
   order, then first handler, as the pinned interpreter does). On top of that sit
   register/accumulator/environment flow, SSA and phi placement, and typed semantic
   operations per ISA group (calls, property reads/writes, module loads/stores, class/function
   creation, iterators, coroutines/await, …) that feed a value IR (`value_ir.h`,
   `value_import.h`, `value_simplify.h`).
4. **`abcd_source`** (`src/source`, the largest layer): the single value-tree route. Value IR →
   binding and evaluation plans → control regions (loops, exits, try trees) → `FunctionTree`
   families → `TreePrinter`. The record entry is `BuildRecordSource` → `BuildRecordTree`. It covers
   function/class/method emission, captures, module imports/exports, and partial output that
   is explicitly marked incomplete. The design contracts are in `docs/design/source-recovery/`
   (README, `value-ir.md`, `source-tree.md`, `acceptance.md`, `function-tree-output.md`).
5. **`abcd_structure`** (`src/structure`): record-level output, covering record paths and file
   names, module/class/global/scope writers, and source identity. One file per record.

Each semantic step has its own design note in `docs/design/*.md` (one feature per file) and a
format/runtime citation in `docs/reference/*.md`. Find the matching note before changing
behavior. Algorithm choices must follow `docs/design/algorithms.md`, and method scheduling
must follow `docs/design/method-tasks.md`.

## Repository conventions

- `references/openharmony/...` are pinned upstream submodules, used read-only as the
  authority for format and interpreter semantics.
- `samples/` is gitignored local corpus data: `abc/` (batch, roughly 900 files, includes
  static), `abc_with_code/` (paired with source), and `applications_app_samples/` (DevEco
  builds). Never modify input ABCs. Paired source is only for evaluation and must never reach
  the engine.
- `tests/support` and `tests/tools` are test-only helpers. Production code (`src/`,
  `include/`, `tools/`) must not depend on them.
- After each coherent commit, update `docs/design/source-recovery/status.md` with what was
  delivered, build and verification evidence, and the remaining gates. History goes in
  `progress.md`.
