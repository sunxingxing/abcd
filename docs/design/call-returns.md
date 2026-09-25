# Direct Returns From the Common Call AST

SourceStagedCall can retain an optional original return instruction ID. This is
a typed completion certificate, not source text and not a second body writer.
FoldCallReturns runs inside the existing return-expression pass and scans each
normal block once. It admits a call owner in that same block only when the call
is immediately followed by the terminal accumulator return. Exact SSA counts
must prove that return is the call result's sole consumer, including phi uses.
Phased methods, catches, merged origins and non-stored call results are excluded.

Property lookup, argument evaluation and invocation remain inside their existing
Reference owner. The result is returned after invocation instead of committed
to an unobservable accumulator. No instruction or exception route is crossed;
no new superclass, host or resource-exhaustion assumption is made. The original
return becomes a NoOperation with previous_origin pointing to the call PC.
The printer validates that coordinate and anchor before writing `return`, and
the existing mapping chain gives the return the emitted call span. Returned
calls cannot be reclassified as unused results.
Record/module emission still rejects this return form, exactly like an ordinary
SourceReturnExpression; a function completion must not become top-level JS.

The ordinary slot-aware receiver and argument paths remain in use, without
hidden reads or synthesized bindings. Optional inline storage is covered by
sizeof(SourceStatement) budgeting. Work is O(I log(I + U)) for I instructions
and U indexed SSA reads, constant scratch, no dependency. Calls in forward
argument regions or separated from their return retain their existing output.

Tests, JS execution and RSS remain paused. Deferred regressions include live
result/phi uses, getter and argument exceptions, malformed return coordinates,
nested argument owners, slot declarations and original return PC propagation.
Nested call-as-argument expression recovery and full equivalence remain open.

## Production Evidence

Read-only review identified a missing record-output restriction in the initial
implementation; the returned staged branch now rejects record_exports_ before
emitting return, matching SourceReturnExpression. Follow-up review confirmed the
fix. Both production tools were rebuilt after that change, and ABC-only export
`build/pasteboard-call-returns-01` succeeded.

Ten getters across the six pages now directly return their call result. Totals
become 4,327 lines / 91,852 bytes, down ten lines / 130 bytes. PageA getter 7228
property PC 20, call PC 28 and return PC 31 share bytes [4257,4278), the actual
`return ($acc).get();` statement including newline. The receiver's preceding
property expression still remains separate; it is not claimed as a recovered
nested expression.

Partial maps retain 195 methods / 9,745 spans. Statuses remain eight incomplete,
two refused host constructors at PC 14 and nine metadata-only records. Tests,
generated JS execution and RSS were not run; full equivalence remains unproven.
