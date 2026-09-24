# Rest Parameters

Reference: pinned `OpenHarmony-v7.0-Release` identities in
`docs/reference/openharmony-v7.0-release.lock.json`. The es2panda frontend
`compiler/core/function.cpp` emits `CopyRestArgs` for a rest formal with the
zero-based formal index. The runtime's
`ecmascript/interpreter/interpreter-inl.cpp` handlers at `COPYRESTARGS_IMM8`
and `WIDE_COPYRESTARGS_PREF_IMM16` pass that index to `GetNumArgs`; the runtime
stub allocates a new array containing actual arguments from that index.

`LiftRestArguments` validates the narrow and wide immediate encodings. A
source `...parameter` is admitted only when source length is exactly one
less than the ABI parameter count, extra-argument handling is enabled, the
v7 entrance copies every ABI slot to consecutive locals, and the copy is
immediately followed by `COPYRESTARGS` and a store over the copied rest slot.
The original ABI rest slot must not be read later, no second `COPYRESTARGS`
may allocate another array, and no branch or exception region may reenter
the entrance. This proof is O(instructions) and uses fixed scratch.
Unsupported profiles and unproved shapes remain refused. Getter/setter
syntax rejects rest formals because both are invalid ECMAScript declarations.

The rest formal itself supplies the fresh array; emitting another copy would
change identity. Ordinary function expressions use an outer strict IIFE
because ECMAScript forbids an explicit strict directive inside a function
with a non-simple parameter list. Class methods are intrinsically strict.

`rest_arguments_test` covers both widths, malformed operands, effect and
value transfer, malformed prologues, and 200/201-instruction inputs.
`base_function_test` and the Node oracle cover the generated signature,
fresh rest-array values, function length, absence of extra-argument handling,
fixed formals preceding rest, and later reads of the original ABI slot. The
real `Logger` record now emits
source and its paired-corpus Node oracle verifies the module export, instance
fields, method lengths, hilog receiver, and arguments. This does not establish
general whole-record semantic equivalence.

Final Release source-body probe, with a 1,000,000,000-byte address-space cap,
processed 900 paths: 402 dynamic and 498 cached static skips. It decoded
5,486,254 methods with zero file/method failures. Method-body readiness stayed
1,500,536; the standalone probe intentionally has no proven source rest
binding, so this feature is admitted only through the complete function
writer. The measured whole-process peak RSS was 176,857,088 bytes; wall time
was 144.57 seconds. Command:

```sh
prlimit --as=1000000000 -- /usr/bin/time -f 'wall_s=%e peak_rss_kib=%M' \
  build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/rest-source-probe-release.json
```
