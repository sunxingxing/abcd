# Native source property reference phases

The common source adapter uses ECMAScript 2025 ordinary public property
references. This describes emitted native source; it does not redefine original
Ark instruction effects or authorize moving an original operand. Source runtime
identity and the original ISA/profile remain separate prerequisites.

Primary references:

- [EvaluatePropertyAccessWithExpressionKey, 13.3.3](https://tc39.es/ecma262/2025/multipage/ecmascript-language-expressions.html#sec-evaluatepropertyaccesswithexpressionkey)
- [GetValue and PutValue, 6.2.5.5–6](https://tc39.es/ecma262/2025/multipage/ecmascript-data-types-and-values.html#sec-getvalue)
- [Assignment evaluation, 13.15.2](https://tc39.es/ecma262/2025/multipage/ecmascript-language-expressions.html#sec-assignment-operators-runtime-semantics-evaluation)

Acquisition evaluates the base and key expressions once, retaining their values
in a Reference. It does not call a getter, check the base for null, or convert an
object key. GetValue/PutValue first convert the receiver to an object, then
convert an unresolved key, then invoke Get/Set. A simple property assignment
evaluates its RHS after acquisition and before PutValue. A method callee performs
GetValue before arguments and retains its Reference receiver.

The adapter publishes acquisition, receiver, key and access points, successful
continuation, exceptional route, source owner and checked original operand
ranges. These ranges preserve original use identity; they are not a certificate
that each source phase equals one original operation's raw flags. Multiple
normalized operations, signature/protocol roots and original effect composition
remain part of the open original correspondence task.

The effect table excludes user hooks for known primitive key domains, retains
resource-observable conversions/boxing, and otherwise stays conservative. It
does not prove Get/Set pure, binding initialization or value/cell equality.
Source passes and trial projections receive the same phase graph; they must not
independently infer these facts from adjacent syntax.

## Version difference and verification

The [2024 algorithm](https://tc39.es/ecma262/2024/multipage/ecmascript-language-expressions.html#sec-evaluatepropertyaccesswithexpressionkey)
converted the key during acquisition. Applying that order to current native
source is incorrect. The initial rules253 fixture assumed that order; its RED
result is invalid current-profile evidence. A separate assertion against the
corrected inert-acquisition obligation failed at property check99 before the
phase fix. Do not silently use a 2024 source runtime with this adapter; an older
reference interpretation requires an explicit source semantic adapter.

Current Node v25.7.0 independently confirms deferred conversion and receiver
failure before key hooks. `property_flow_test` checks the shared graph, motion,
definition demand, refinement, reverse arena, budgets and geometric cost.
`property_flow_source_test` independently constructs reference source and
executes generated output against coercion/throw/proxy/getter/setter cases;
early conversion, premature receiver checks and premature GetValue are mutants.
`function_tree_test` checks original base/key UseId and role correspondence.
These are bounded source proofs, not a fresh Ark-platform differential run.
