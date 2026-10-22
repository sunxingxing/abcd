# Strict super stores (2026-10-22)

The four STSUPERBYNAME/STSUPERBYVALUE variants now retain
`PropertyLookup::kSuper` in the existing named/computed write IR. Receiver,
key, stored value, strict Set effects, normal completion and exception exits
remain original occurrences. Neither an own definition nor a normal object
assignment is substituted. The source AssignmentExpr carries the lookup mode;
MemberMutationExpr remains within its previous arena width.

Pinned evidence, relative to `references/openharmony/arkcompiler/ets_runtime/ecmascript`:

- interpreter/interpreter-inl.cpp:2773–2790 and 6763–6780 load the receiver/key
  registers and accumulator value and invoke StSuperByValue with GetFunction(sp).
- The named variants at 3183–3207 and 6835–6858 resolve the string-pool key and
  invoke the same runtime with the explicit receiver and current function.
- stubs/runtime_stubs-inl.h:725–743 gets the current function's HomeObject,
  resolves GetSuperBase, requires coercibility, applies ToPropertyKey, and calls
  SetProperty(superBase,key,value,receiver,true). The final true is strict
  failure behavior. Interpreter SAVE_ACC/RESTORE_ACC preserves the original
  value; only successful source assignment produces its value.

## Reference and order obligations

The existing class home-object certificate and receiver SSA correspondence are
rechecked in TreeFlow. The target prints `super.name`/`super[key]`, never
`this.name` or a dynamic prototype property. Class structure independently
requires a method/constructor owner and strict context. RecoveryFlow carries
the super lookup through its receiver/key/access phases on every projection.

There is a real runtime timing difference. Ark resolves GetSuperBase and
ToPropertyKey during the store, after RHS evaluation; ECMA-262 AssignmentExpression
resolves the reference before evaluating RHS. `super.x = mutateHomePrototype()`
can therefore observe different prototypes. Computed object keys can also
expose different conversion/exception ordering. No premise for this is assumed.

`SuperStoreRhsSafety` derives one transitive expression-effects index per tree,
using a prepaid reverse-child CSR and Kahn worklist, O(N+E) time/space. It proves
that RHS evaluation has no observable effect or language exception; initialized
formal reads are inert, other unproven binding reads retain their guard. A
separate primitive-key-domain proof makes ToPropertyKey timing unobservable.
The immutable graph's lookup, original receiver, key and stored value are
matched independently. A failed RHS proof returns `kSuperStoreOrder`; unknown
computed domains return `kSuperKeyDomain`; absent home/receiver evidence retains
their existing typed refusals. Original exception correspondence remains common
flow's obligation. Scope and effect-summary mutations cannot authorize printing.

Object-literal methods remain a typed home-object/creation-context refusal;
the current class home-object proof is not extended by assuming object identity.
