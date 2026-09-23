# Global Access Semantics

Global access is not a source declaration. Keep name reads/writes, Var-family
reads/writes and constant/mutable record initialization as distinct typed
operations. Modern encodings use an IC immediate and regional string index;
validate both widths before resolving a name. Deprecated encodings remain
explicitly unsupported by this lift, not silently interpreted as modern ones.

Reference: pinned v7.0 interpreter handlers TRYLDGLOBALBYNAME,
TRYSTGLOBALBYNAME, LDGLOBALVAR, STGLOBALVAR, STCONSTTOGLOBALRECORD and
STTOGLOBALRECORD. Reads replace accumulator, writes consume and preserve it on
normal completion. All can throw; neither is pure. Explicit registers and the
caller's frame thisObj survive normal completion, allowing receiver identity
to pass through these operations without widening shared SSA/escape support.

Do not simplify Var-family accesses to object-only lookup. In ic_runtime.cpp,
LoadMiss and StoreMiss also consult global records for non-TRY IC kinds; the
interpreter's no-profile fallback differs. Source binding resolution must retain
this distinction and establish compilation/runtime preconditions before choosing
syntax. Mutable record initialization alone does not prove original `let`
instead of another mutable declaration. No declaration hoisting is permitted
without lifetime/order and scope evidence.

Record output preserves operation, name and pc per site. Invalid pool indices or
strings remain local errors; input I/O/change and output failures abort staged
publication. It shares the existing decoded method and uses O(instructions)
additional time, one bounded string at a time. Future source integration must
connect these operations to actual global binding identity and initialization,
not deduplicate solely by name across unrelated records or realms.

## Bounded Name-Read Source

TRYLDGLOBALBYNAME narrow/wide now emits a native free identifier only with a
caller-proven complete, sorted ASCII enclosing-binding set. The name must be a
strict-safe ASCII identifier, not reserved, and must not collide with the
generated class binding or named-function self binding. Accumulator/register
bindings must use the private `$` namespace when this operation occurs. These
checks are per method, not a scan of all registers for every global access.
All other typed global operations remain unsupported by source emission.

The complete caller scope is also borrowed by a nested DEFINEFUNC source
writer, including enclosing source-formal bindings. Its own recovered formals
are added before body validation. A missing scope still rejects the read, and
an enclosing or inner-formal collision still rejects it. This is a synchronous
proof transfer, not a new cross-method global binding inference.

Under those premises, the pinned v7.0 runtime's record-first lookup, own/global
object fallback, inherited property lookup and missing-name ReferenceError
match free-identifier evaluation. This does not infer or emit a source global
declaration. A record exporter must build the complete enclosing-binding set,
including other generated factory names and module aliases, before using this
primitive. A missing proof rejects the site. The paired BreakpointType oracle
exercises record precedence, inherited setter behavior and missing globals.
