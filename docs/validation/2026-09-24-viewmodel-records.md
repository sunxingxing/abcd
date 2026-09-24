# ViewModel Class Records

Immersive `IconInfoViewModel` (record 29119) and `SectionViewModel` (29619)
were previously reported as `RecordProgramError::kClass`. The class-site
fixture localized the underlying failure to `LinearLexicalError::kInstruction`
at the first nonterminal `CALLTHIS0` in a member body; class-buffer parsing
and constructor source were not the failing stages. The generated method
calls an imported model, stores its result, then calls `slice(0, count)`.

The function-wide named-call matcher now admits the exact immediate
zero-argument spill/reload shape, with existing register liveness and budget
proofs. The later `slice` call retains its deferred-argument source form.
Synthetic tests cover source output, dead-callee proof, alias refusal, getter
count, receiver identity and mutable `call`/`apply` avoidance.

Both class-site fixtures are now 1/1 ready and `abc_structure --source`
writes both complete record files. Immersive source-ready records rose from
10/45 to 12/45. A Node ESM oracle links the generated record files against
controlled imported models and checks class names, constructor/method lengths,
instance field ownership, category/section delegation, `slice` receiver,
argument order and call sequence. The paired TS is validation context only,
not an engine input.

The full Debug build and 168/168 CTest cases passed. A fresh external
`/usr/bin/time` run of `abc_structure --source` on Immersive measured
11,264 KiB peak RSS (11,534,336 bytes); the CLI reported 11,272,192 bytes.
This single-file result does not certify full-corpus record output memory.
The source-body corpus probe from the preceding staged-call change is not
evidence for this newly admitted class-body path. This change does not admit
general class bodies, branching methods or exception regions.
