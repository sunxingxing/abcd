# Typed Receiver Binding

ScalarNames carries an optional RegisterId certificate for a read-only ABI
receiver. The scalar expression printer emits `this` for reads of that slot;
it never places the keyword in the identifier table or parses source names.

The callable owner authorizes the certificate only for ordinary functions,
base constructors, class methods and accessors with an initialized native
receiver. Arrow functions and range-derived constructors are excluded. A
whole-body register-access scan rejects any write/read-write window containing
the receiver, including unreachable instructions. Unknown access information
refuses the certificate. Checking windows does not expand their registers.

This is O(I) time and O(1) scratch. The owner charges the scan's instruction
work before taking the constructor fallback budget snapshot. No resource
allocation or language operation moves. Existing arrow helper closures preserve
the surrounding receiver; nested callable owners construct their own bindings.

ABI register declarations remain emitted. Some capture, class-parent and legacy
paths consume the identifier table directly, so deleting these declarations
would need a separate complete use proof. Copies of the receiver into other
registers are also not automatically aliased. This is a typed expression
binding, not complete variable recovery or a proof of a source class field.

## Evidence and Limits

Both production targets rebuilt successfully. ABC-only export
`build/pasteboard-this-binding-01` replaces ABI-register reads with native `this`,
including CommonConstants and BreakpointType constructors and GetValue. A
read-only output inspection found no standalone assignment to `this`. Eight
incomplete app records, two host-constructor refusals at PC14 and nine metadata
records remain; method maps retain 195 method entities / 9,745 spans.
Read-only review reported no concrete correctness issue.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include receiver writes/read-write ranges, unknown access, arrows,
derived constructors, getters/setters, async methods, nested helper scopes,
capture identifier consumers and constructor fallback work accounting.
No complete semantic-equivalence or memory-compliance claim follows from the
build and export evidence.
