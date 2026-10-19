# Constructed extended branches (ISA increment 1b)

The 28 extended-branch ABCs are **constructed refusal fixtures**, not compiled
source and not executable semantic examples. They are generated from typed
instruction encodings and labels by `tests/support/abc/builder.py`, with a module
record, one method, a module literal, index region, code and Adler-32 checksum.
No existing ABC bytes are copied or patched. Each has `ldundefined`, its chosen
branch (v0 for register variants), and `returnundefined`. The resolved branch
target is the return. imm8 and imm16 variants have separate files.

Pinned-runtime evidence ID 0 means
`RuntimeEvidence::kPinnedExtendedBranches`; see
`docs/design/source-recovery/isa-branches-runtime-evidence.md`. These opcodes have
FATAL unimplemented interpreter handlers, NOP assembly stubs and no lowering.
They must decode but must not become source equality expressions. They are never
executed in Node or Ark. A successful disassembly is only an encoding oracle.

Regenerate deterministically from the repository root:

```sh
python3 tests/isa/constructed_branches_test.py --generate
python3 tests/isa/constructed_branches_test.py
```

The test compares every committed ABC byte with a fresh build, verifies its
SHA-256 against `tests/isa/coverage_manifest.json`, cross-checks SDK `ark_disasm`,
checks the raw production lifter and the production record refusal, and checks
`abc_semantic --record-source`. Both the opcode and evidence ID must survive.
The registry also pins the evidence document's SHA-256. `NonExecutable` is not
support; all runtime and Node oracle columns for these 28 remain empty.

## Positive controls

`jeqz_imm8.abc` and `jnez_imm8.abc` are built from the same writer, with a real
constant pool holding `input` and `observed`. They perform global input read,
ToBoolean, a branch, and a conditional global observation. Their `.js` sources
are independently compiled into `*-es2abc.abc`:

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --output=tests/fixtures/isa/branches/jeqz_imm8-es2abc.abc tests/fixtures/isa/branches/jeqz_imm8.js
"$ES2ABC" --module --merge-abc --opt-level=0 --output=tests/fixtures/isa/branches/jnez_imm8-es2abc.abc tests/fixtures/isa/branches/jnez_imm8.js
```

Compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
SDK disassembler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/ark_disasm`.

The engine exports both constructed and compiled control records. The Node test
compares both outputs with the original source using separate VM contexts:
false/true, zero/-zero/NaN/numbers, null/undefined, strings, BigInts, objects with
throwing valueOf, and input/observation getter exceptions. It checks complete
traces and exception constructors. It never transfers this evidence to the 28
refused variants. There is no finally lowering in these controls.

## Builder boundary

The writer currently supports one merged ECMAScript module, one method, ASCII
strings, borrowed encoded instructions, resolved relative branch labels, a
string pool, and catch-all regions. It does not claim support for arbitrary
metadata, annotations, non-ASCII MUTF-8, exports, or another container profile.
The layout and writing passes share the same actions; fixed-width references
allow forward references without patching offsets after writing. Checked uint,
ULEB and signed branch ranges reject overflow. Before allocation, the budget
reserves label/entity scratch and both mutable and immutable output storage.
The input description is borrowed. Unit tests cover exact budgets and 200/201/
400/800 code sizes, backward deltas, optional catch, bad labels, duplicate labels,
branch overflow, negative frame counts and invalid strings. Inputs are never
rewritten by the engine. Production has no dependency on this builder.

## Positive-control SHA-256

* `jeqz_imm8-es2abc.abc`: `f5daa657cd9c65a07bfba7160354f17b99870d1ea2e3e55ceeea547485132e0e`
* `jeqz_imm8.abc`: `49f14056b4f71e158ab8ffa6d7d7614c678e9a9344ab46c2fed617f61ff8632d`
* `jeqz_imm8.js`: `6944e2d552e21bcbd332fd1348c213733b8929a2e776c99f5ae141e15c090bb4`
* `jnez_imm8-es2abc.abc`: `fb675c2d0d86a229af15696f830588f48ee0cd67b15371a5b20e6771e3aabb69`
* `jnez_imm8.abc`: `d24eba2deae868a1677e454f60f584c3ea9410fc0dd8de9710a98d2a61659a53`
* `jnez_imm8.js`: `e0ea389d50b68787b38838b460674785780202f788621e41b3b437d3267640ee`
