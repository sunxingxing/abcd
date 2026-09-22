# Ordinary Members In Class Expressions

## Scope

Connect the previously separate native ordinary method writer and whole-file
role index to WriteCreatedClass. Every class member must pass target, kind,
name, descriptor, signature and body checks. Nonempty classes capture stable
method-self values before escape; whole-class preflight prevents semantic or
configured-budget failures from publishing a prefix. This is class-expression
integration, not complete record or global-variable recovery.

## Verification

- RED: created_members_test reached its successful-class assertion and failed
  against the old empty-buffer-only writer. Role-index fixture checks passed.
- Debug build: successful, warnings treated as errors.
- Full Debug CTest: 108/108 passed, 29.23 seconds.
- After adding empty-key coverage, rebuilt and reran all four new member tests:
  4/4 passed, 1.87 seconds.
- ASan/UBSan: created_class, created_members and created_members_scale C++/Node
  tests, 6/6 passed, 8.70 seconds.
- Independent read-only review found no blocking issues; checked pinned fast
  and dictionary ordinary-property descriptors and immutable self capture.
- git diff --check passed. One worktree; no sample/cache changes.

Native Node oracles cover constructor/prototype ownership, method names and
lengths, nonconstructibility, W=true/E=false/C=true descriptors, distinct static
and prototype methods, strict self identity after replacement/deletion, class
name collision with MethodSelf0 and ClassValue, and property definition order.
C++ cases cover quoted/empty/__proto__ keys, reserved and element keys,
duplicate keys within a placement, nonordinary roles/templates, missing proof,
name mismatch, initial async attributes, missing body, late unsupported body,
and exact/one-byte-short output and scratch budgets. Failure output is empty.

Scale cases contain 200, 201, 400, 800, 1022, 1023 and 1600 methods; 1022/1023
straddle pinned prototype fast/dictionary capacity including constructor.
The complete non-sanitized scale executable measured 7048 KiB peak RSS and
0.69 seconds elapsed. This is a small synthetic measurement, not final
whole-process/corpus memory certification or a timing proof of complexity.

## Paired Samples

Fixture invocation remains class_sample_fixture INPUT.abc, now supplying the
whole-file role index. RLIMIT_AS is 1,000,000,000 bytes. Engine input contains
only ABC; sidecar sources are not used to synthesize output.

| Sample | Sites | Accepted | Peak RSS KiB | Seconds |
| --- | ---: | ---: | ---: | ---: |
| Immersive | 41 | 2 | 9216 | 0.08 |
| MultiCommunityApplication | 32 | 5 | 9728 | 0.15 |
| pasteboard | 11 | 1 | 8960 | 0.04 |
| push modules | 16 | 1 | 8832 | 0.03 |
| push widgets | 1 | 0 | 8448 | 0.00 |

Node class_samples.mjs passed with expected count nine, including the existing
CreateFormData assignment/setter oracle. An initial invocation with an unproven
expected count of twelve failed (actual nine); no coverage increase is claimed.
Outputs are untracked build/classes-members-*.mjs validation artifacts.

There are still 92 rejected sites. Newly admitted member buffers expose later
body/signature rejection rather than becoming automatically recoverable. For
example, Immersive caller 16948 pc 9 reports unsupported instruction at body
pc 10; caller 7531 pc 9 reports control flow at body pc 27. The test fixture
now logs typed lexical/function rejection details. These diagnostics guide the
remaining instance initialization/signature/body work; they are not permission
to omit operations or mark the current source gate complete.
