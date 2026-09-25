# Object Data Key Syntax

ClassifyObjectKey chooses source spelling for an already-proven object data
property. It reuses the conservative ASCII IdentifierName classifier, allowing
keywords in colon-form properties. It explicitly keeps `__proto__` computed:
both an unquoted and a quoted noncomputed colon key have prototype-setting
semantics rather than defining that own property. Constructor, prototype, get,
set and async are ordinary data keys in this grammar, unlike class/method
declarations. This decision applies to names at the source boundary, not to
instruction or IR dispatch.

Object-buffer and record-literal writers use the same classifier. Other names
retain their original computed quoted spelling and string escaping, including
empty strings, numeric-looking names, non-ASCII code units and punctuation.
No key/value order, object ownership, descriptor, duplicate-key, literal-value
or parsing admission changes. Parenthesized object expressions remain intact.

The scan and emission are linear in key code units. The existing Text writer
charges every emitted byte; no UTF conversion buffer, dependency or allocation
is added. Source positions continue to use actual emitted bytes through the
existing common printer and template rebasing, not string-based map repair.

Production abc_semantic/abc_structure builds and ABC-only export
`build/pasteboard-object-keys-01` succeeded. All six page sources gain ordinary
object keys, including resource id/type/params and page metadata. Overall source
size changes from 5,935 lines / 112,679 bytes to 5,935 / 110,055. Both common
records retain their sizes. Statuses remain eight incomplete, two constructors
refused at PC 14 and nine metadata-only records. Partial maps retain 195 methods
and 9,745 spans. Independent read-only review found no concrete issue.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include prototype-preserving `__proto__`, reserved words, empty/numeric/Unicode
keys, escaping, nested literals, output-budget failures and position rebasing.
This is source-spelling recovery, not native object-initialization folding or
proof of full executable-source equivalence.
