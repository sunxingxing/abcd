# Range super in the common source pipeline

## Representation and admission

The existing typed `SuperRange` operand model now enters `SemanticOperation`
for narrow and wide range-super instructions. Its argument window and optional
inline-cache slot remain separate; the cache slot is never a source argument.
The normal register effect writes the accumulator. Heap reads/writes, user code
and allocation are possible before either completion, and exceptions retain
the pre-instruction register state. Normal completion yields a Language value,
matching the pre-existing decoded register-value rule.

Reference: the pinned `OpenHarmony-v7.0-Release` runtime's
`ecmascript/interpreter/interpreter-inl.cpp`,
`SUPERCALLTHISRANGE_IMM8_IMM8_V8` and
`WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8`. These handlers obtain the current
function and newTarget from call context; this change does not reinterpret
them as register arguments or as an ordinary constructor call.

The AST uses a typed range-super expression and the shared printer emits
`super(arg0, ...)`, preserving the original instruction's generated span.
This requires all of the following:

- The callable owner selects an actual derived constructor and supplies the
  corresponding private ABI register bindings. Its initial VM this slot is
  undefined, not an early access to native JS this.
- The existing whole-body `ProveRangeDerivedConstructor` succeeds again at
  the common body boundary. Only one range call is admitted, with no handlers
  or backedges; dominance and SSA origins prove the existing VM checks safe.
- The AST receives the unchanged same-body/frame certificate and matches its
  super PC. Only then may it omit those checks, retaining PC anchors.
- The printer requires the derived-constructor context. A detached function
  fragment, a Language-only capture premise, or a phased AST does not license
  native super or guard removal.

The constructor wrapper now attempts the common body pipeline before its
existing legacy fallback. Forwarding and exact spread paths remain unchanged;
this change does not weaken their parent-brand or lifetime requirements.
No new dependencies or algorithms are introduced. The additional proof is
sequential, uses the existing byte/work limits, and releases its scratch before
the semantic method is built. Printing takes O(number of explicit arguments).

## Observed progress and remaining work

Both CLI targets built after the change. ABC-only inspection artifacts are
`build/pasteboard-5851-super-range.jsonl`,
`build/pasteboard-semantic-super-range.jsonl`, and
`build/pasteboard-source-super-range/manifest.json`.

Five pasteboard methods now pass IR construction that previously stopped at
range super: 5851, 7201, 7944, 9277 and 11444. In the ordinary detached survey,
Home constructor 5851 now reaches a proof refusal at PC412; the other four reach
AST refusal at PC79 because no derived-owner certificate is supplied there.
These are intermediate-stage advances, not completed source methods.

The complete record manifest is unchanged from the record-lexical checkpoint.
Home still refuses at entry PC381 when flushing the class: its constructor
stores this in a lexical cell, which the existing register-only super-origin
proof does not handle. It also creates a capturing child before later reads.
The next proof must model reaching lexical stores and possible closure writes;
neither lexical initialization alone nor a generic Language fact proves that
those values are the constructed this object.

Tests, runtime equivalence checks and RSS measurement remain paused. No positive
runtime equivalence claim is made for the newly connected constructor path.
When resumed, regression coverage must include narrow/wide and zero-argument
range forms, argument order, exception-before-commit, super PC mapping, ordinary
function/phased refusal, and malformed or mismatched constructor certificates.
