# Arrow Self Wrappers

## Selection

FunctionWriter now prepares its common body/template before emitting the
callable header. A syntax-only header lambda preserves the existing forms for
methods, constructors and ordinary functions. It does not analyze or emit a
second body. Final output limits continue to cover header, declarations, body
and suffix; preparation is bounded by the same method/output allowances.

An arrow may omit the `const $arrow` IIFE only when the common printer supplies
precise slot metadata, the metadata matches the frame, and the incoming
function-object register is absent or unused. A present slot is bounds-checked
before inspection. Unknown/legacy bodies and observed self registers retain
the wrapper. SelfBindingUse reports the same certificate to class owners.

The direct form remains a computed data-property arrow followed by a lookup
of that exact literal key. This preserves SetFunctionName, including the empty
name, without a mutable intrinsic or assignment-dependent inferred `$r` name.
The arrow's parameter list, async form, body, capture tokens and owner-proven
`.length` remain unchanged. The removed IIFE is itself an arrow, so it does not
supply a different this, arguments or new.target binding to the inner arrow.
Its fresh object/data-property operations do not invoke application code.

Only simple parameter lists qualify. Default/rest parameters cannot carry a
body strict directive; their original enclosing strict wrapper remains even
if self is unused. Ordinary anonymous function wrappers are unchanged.

## Positions and Bounds

Header selection occurs before rebasing body positions into the output sink.
Nested captured templates retain the existing prefix-shift replay protocol.
No PC or semantic decision is recovered from generated text, and no input
sidecar source/map is consumed. The change adds constant-time selection and
no new heap structure, dependency or external algorithm.

## Evidence and Limits

Production abc_semantic and abc_structure builds succeeded. ABC-only export
`build/pasteboard-arrow-wrappers-01` removes 95 self-free arrow IIFEs across six
pages, reducing output from 7,112 lines / 127,462 bytes to 6,827 / 123,187.
For example PageA's empty callback is now directly `({[""]: () => { ... }})[""]`
with its strict body retained; paramsLambda retains its actual function name.
CommonConstants and Logger output sizes are unchanged.

Statuses remain eight incomplete records, two constructors refused at PC 14,
and nine metadata records. Partial maps retain 195 methods / 9,745 spans.
Final rebuild and export `build/pasteboard-arrow-wrappers-02` succeeded; its
sources directory is byte-identical to the first export. Independent read-only
review found no concrete correctness issue.
Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include observed function identity, empty/named arrows, async,
default/rest parameters, lexical this/arguments/new.target, nested captures,
legacy fallback and tight output limits. Full equivalence and peak-memory
compliance are not claimed.
