# Register Def/Use Validation

## Scope

`LiftRegisterEffect` derives register/accumulator may-reads and definite
normal-completion writes from the existing reviewed semantic lifts, not ISA
may-access metadata. Range-call arguments remain compact until indexing.
`RegisterUses` owns sorted sparse binding/block lists for definitions and
upward uses; it connects decoded instructions to `LiveIn` and `Place`.

Build complexity is O(B + E + I + U log U), U expanded register accesses.
There is no frame-sized allocation or per-binding CFG walk in this stage.
Downstream repeated liveness/IDF queries retain their separately documented
output-sensitive costs and shared method quota; this does not prove a complete
multi-variable SSA pipeline complexity bound.

## Evidence

- Initial test targets failed because the two new public headers did not exist.
  After implementation both targets pass.
- Full Debug build and CTest: **68/68 passed**, 13.44 seconds.
- AddressSanitizer + UndefinedBehaviorSanitizer Debug targets:
  `register_effect_test` and `register_uses_test`, **2/2 passed**, 2.05 seconds.
- Effects cover scalar assignments, named/computed/indexed properties,
  ordinary calls, lexical operations, Hole guards, creation home-object inputs,
  constants, branches, returns and throws. Tests invert ISA access flags and
  verify they cannot change semantic def/use results. Malformed, register-range
  and unsupported failures remain distinct.
- Decoded diamond and loop-carried bindings produce expected live-in and Phi
  placement. Self-copy, legacy lexical-store register input, conditional Hole
  diagnostic name, range count zero/255 and implicit registers through v509
  are covered.
- 1,000 independent generated MOV-sequence oracles verify per-block first-use
  semantics without reusing effect extraction in the oracle.
- Scaling cases contain 200, 201, 4,096 and 65,536 distinct registers. Exact
  instruction/access counts and linear retained payload are checked. A frame
  size of UINT64_MAX has the same payload/peak as the small equivalent frame.
- Exceptions, resumes, invalid CFG shape/edges, reachable unknown instructions,
  normal-unreachable unknown instructions, moved-from access and destruction of
  input body/CFG are covered.
- Exact/one-byte-short memory budgets include live scratch plus owned outputs.
  Exact/shared/short work quotas and interruption during the sort-dominated
  scaling cases return typed failures without publishing partial results.
- Independent read-only review found no correctness issues; `git diff --check`
  passes. One worktree, no sample modifications.

After builds, CTest and sanitizer processes finished, a separate run used:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/register_uses_test
```

Exit 0, elapsed **0.69 seconds**, maximum RSS **37,696 KiB = 38,600,704 bytes**.
The test launches no child processes. This is a synthetic component test under
a 1,000,000,000-byte address-space limit, not a full-corpus or complete-engine
memory acceptance result. PMR payload counts exclude allocator overhead and
stack; measured RSS includes them for this run.

## Remaining Boundaries

Body/CFG identity and validated decoding are caller preconditions; structural
checks are not a cryptographic identity proof. Input body/CFG memory and retained
results during later queries remain charged to the method owner. Exception
regions and resume edges are refused. Register effects do not establish heap
purity, JS language-value types, Hole/TDZ equivalence, lexical-cell alias safety
or source equivalence. SSA renaming, source generation, actual function wrappers
and the asynchronous/lazy function service remain separate work.

This increment is not wired into the corpus source probe. No full corpus rerun,
source-coverage increase or complete record source-file recovery is claimed.
