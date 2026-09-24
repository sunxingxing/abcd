# HomeModel Record Vertical Check

The paired `Immersive/modules.abc` record 16474 now has a complete-record
oracle, separate from the older class-site fixture. It exports the record
through `abc_structure --source`, loads the generated ESM with a controlled
`HomeConstants` import, and checks constructor and method lengths, own
`tabBarList` initialization, item values, lexical arrow capture, repeated
initialization, and observable dependency getter order. The paired TS source
is evaluation material only; it is not an engine input.

Base-constructor source emission now renders a named write as
`this["field"] = value` only when a straight-line copy proof tracks the
write's object register back to the original ABI `this` value. It does not
hoist initialization or declare a class field: doing so can change setter,
Proxy, and construction ordering. Branches, exception regions, unknown
register effects, and exhausted proof scratch keep the prior register-based
strict write. A unit regression overwrites the ABI `this` slot with a
parameter and requires that fallback. If the optional direct-write form
exhausts downstream body-analysis scratch, source preflight releases its
proof storage and retries the original form. A tight-budget regression
checks this preserves prior admission.

This verifies one real record and a narrow instance-write form, not the
instance-field/global/signature/body integration gate across the corpus.
The focused constructor/record tests and full CTest run passed (166/166).
An external `/usr/bin/time` run of `abc_structure --source` on this ABC
measured 10,880 KiB peak RSS. The proof pre-scan and alias walk are linear
in instructions and registers; writer membership is logarithmic per named
write. Tests cover 200, 201, and 402 repeated writes. This single-file
measurement does not certify the full-corpus 1,000,000,000-byte process bound.
`WidgetCard` remains refused at its derived constructor (123 instructions,
including default branches, `SUPERCALLTHISRANGE`, guarded `this` binding,
effectful field creation calls, and later class methods). Its existing
forwarding/spread proofs cannot be relaxed to admit that body.
