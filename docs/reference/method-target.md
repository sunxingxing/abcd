# Method Target Resolution

Closure capture recovery requires a validated target method, not merely an
in-file pool offset. This increment implements that dependency under the
approved binding/capture recovery design. It does not emit closures or connect
caller environments to callee captures yet.

## Design and Implementation

`MethodLookup` builds a move-only immutable array of entity offsets and original
`MethodPosition` values. Sorting preserves the source method vector and its
record ranges. Duplicate offsets fail explicitly. `Find` uses binary search;
absence does not manufacture a foreign or local body.

Repeated linear scans would cost O(C*M) for C creation sites and M methods.
A hash table offers expected constant lookup but more allocation and less
predictable worst-case behavior. The compact sorted array was selected for
bounded payload and O(log M) queries: O(M log M) construction, O(M) payload,
standard-library sorting with logarithmic stack space. No new dependency or
third-party code is introduced. Payload is checked before its single allocation.
The default allowance is 64 MiB; caller metadata, stack and allocator overhead
remain within the separate whole-process 1,000,000,000-byte admission limit.

`ResolveMethodTarget` first requires caller membership, resolves the slot using
the caller's region via `ResolvePoolReference`, checks target membership and
decodes target attributes with the file's explicit profile. It retains the
original method position, initial function kind, sendable bit, access byte and
pool-region field. This is O(log R + log M), with constant auxiliary space.

Every argument must belong to the same unchanged file/index context. Positions
are contextual IDs, not ownership tokens. Offset/position consistency checks
catch stale selected entries but do not certify arbitrary caller mutation or
cross-file substitution. The lookup owns no metadata pointers; returned
positions still refer to the caller's method vector. Const lookup is safe to
share while neither the lookup nor its source context is moved or destroyed.

The target need not have bytecode: method membership and source-body availability
are separate properties. Missing foreign targets are explicit. `FunctionKind`
does not determine final JS syntax, and sendable attributes do not themselves
select a closure capture policy. Creation operation and runtime semantics must
also be considered. Multiple references to one target are not one runtime
function object.

## Verification Steps

1. Add `method_lookup_test`, observe the missing-header failure, implement the
   index, and run its checks over 0/1/200/201/4096/65536 entries, descending input,
   absent offsets, duplicates, exact/short/zero budgets and moved-from state.
2. Extend `pool_reference_test` with an explicit synthetic membership table;
   observe the missing target-resolver header, then implement resolution. Check
   differing targets for the same slot in two regions, foreign and non-method
   in-file offsets, all retained attribute fields, missing caller,
   out-of-range pool slot, unknown target attributes
   and changed target offset. Synthetic membership does not validate parsing.
3. Build and run full Debug CTest and focused sanitizer tests; review the patch
   before committing. Full-corpus creation-site/capture mapping remains the next
   integration, not a result of the synthetic tests.

Run focused tests with:

```sh
cmake --build build/decode --target method_lookup_test pool_reference_test -j2
ctest --test-dir build/decode -R '^(method_lookup|pool_reference)_test$' --output-on-failure
```

On 2026-09-22 the full Debug suite passed 55/55 and the focused ASan/UBSan tests
passed 2/2. The Debug lookup test completed in 0.07 seconds with peak RSS
10,633,216 bytes, including its synthetic method metadata. This measurement
is not a full-corpus or complete-decompiler memory result.
