# ISA coverage round 3

Baseline cbc9389b: 117 SupportedVerified / 62 SupportedUnverified /
125 Unmeasured / 28 NonExecutable. No fixture names, source text, or ledger
labels participate in production admission.

V7 records the four user-approved predicates independently; V6 is preserved.
Only NativeNewTarget is consumed in this increment. Tagged templates, object
spread and object rest still need their whole-expression/whole-construction
certificates; enabling a predicate does not bypass those obligations. See
[language-source.md](language-source.md#languagesourcev7-native-ark-forms-user-approval-2026-10-22).

## Owner-bound frame values

An explicit LDNEWTARGET is a ValueNewTarget invocation read, not undefined and
not a synthetic ExprId. ABI new-target entries are a separate source origin.
Both project to an owner-bound NewTargetExpr only under V7. Independent tree
and ABI verification checks the original origin, owner and non-arrow callable;
the printer rejects module-body syntax. Existing successful trees remain
unchanged. LDTHIS and LDFUNCTION need no new premise: constructed exported
functions exercise their existing frame-identity recovery.

The test-only ABC builder now supports additional real method headers, function
kind bits and local exports. All method headers precede code, matching the SDK
reader's contiguous member scan. Layout arithmetic is bounded and storage
charged before allocation. Existing one-method fixture bytes must stay identical.
SDK ark_disasm and the engine decoder independently read every fixture.

## Implicit this properties

Eight typed variants use the current invocation's receiver, never the accumulator
as receiver: LDTHISBYNAME/STTHISBYNAME/LDTHISBYVALUE/STTHISBYVALUE, both widths.
Pinned interpreter-inl.cpp handlers start at 7056/7109/7162/7224/7286/7345/
7404/7476. Get operations commit the accumulator only on success. Stores preserve
it and perform strict Set, not DefineOwnProperty; slow paths use StObjByName /
StObjByValue and the strict SetProperty machinery. Typed operand descriptions
carry a computed key and stored value without fabricating a receiver SSA value.
Source uses the existing MemberExpr/MemberTarget with an owner-bound ThisExpr.
The independent verifier checks implicit owner, field name/key, value order,
strict stores and original exception event. Arrow/derived-constructor contexts
remain refused. Computed key conversion is checked by the common Get/PutValue event model;
RuntimeLdObjByValue/RuntimeStObjByValue (runtime_stubs-inl.h:640/664) checks
receiver coercibility before converting the key. Source assignment similarly
evaluates its RHS before PutValue and performs strict Set. This does not borrow
the different super-reference semantics.

The pinned frontend does not emit these implicit-this forms in the current
ordinary module probes. Constructed executable method layouts are therefore
used, with separate hand-written JavaScript oracles. SDK recompile emits normal
this-entry/property instructions, not the identical width/opcode; strict
round-trip mismatches are retained rather than relabeled as passes.

## Remaining independent gaps

* Super stores: RuntimeStSuperByValue (runtime_stubs-inl.h:726) obtains the home
  object's prototype at store time after RHS evaluation. Native super assignment
  obtains the base before the RHS. `super.p = changeHomePrototype()` can call a
  different setter. A future proof may admit RHS intervals incapable of changing
  that prototype, or preserve a precomputed RHS; arbitrary RHS needs a separate
  decision. This round does not widen super-store admission.
* RegExp: raw flags g/i/m/s/u/y/d are seven pinned bits (regexp_parser.h).
  Faithful literal printing also needs a bounded ECMAScript pattern validator,
  including escaping `/` and line terminators. Invalid ABC patterns must remain
  runtime errors, not become source parse errors. std::regex is not that validator.
* SETOBJECTWITHPROTO rejects shared object prototypes in the pinned stub; the
  ordinary-source projection needs a domain proof. No sendable-domain premise
  was added.
* NEWOBJAPPLY is not covered by the approved APPLY-only call-spread premise.
  LDSYMBOL/global-var bypass forms, computed-key name inference, generator result
  objects, legacy environments, wide rest/default ABI and intrinsic throw forms
  keep their existing obligations. No refusal counts as support.

## Measured checkpoint (working tree against cbc9389b)

Ledger: **117/62/125/28 → 132/62/110/28**, in Verified / Unverified /
Unmeasured / NonExecutable order. All 332 variants observed over 896 ABC inputs;
15 variants promoted, no variant or profile moved backward. Each new promotion
has independent Node occurrence evidence in profiles 4 (12.0.6) and 6 (24.0.0).
No claim transfers to historical profiles 3 or 5.

| Variant | Before | After | Verified profiles |
|---|---|---|---|
| `LDTHISBYNAME_IMM8_ID16` | Unmeasured | SupportedVerified | 4,6 |
| `STTHISBYNAME_IMM8_ID16` | Unmeasured | SupportedVerified | 4,6 |
| `LDTHISBYVALUE_IMM8` | Unmeasured | SupportedVerified | 4,6 |
| `STTHISBYVALUE_IMM8_V8` | Unmeasured | SupportedVerified | 4,6 |
| `LDNEWTARGET` | Unmeasured | SupportedVerified | 4,6 |
| `LDTHIS` | Unmeasured | SupportedVerified | 4,6 |
| `LDTHISBYNAME_IMM16_ID16` | Unmeasured | SupportedVerified | 4,6 |
| `STTHISBYNAME_IMM16_ID16` | Unmeasured | SupportedVerified | 4,6 |
| `LDTHISBYVALUE_IMM16` | Unmeasured | SupportedVerified | 4,6 |
| `STTHISBYVALUE_IMM16_V8` | Unmeasured | SupportedVerified | 4,6 |
| `LDFUNCTION` | Unmeasured | SupportedVerified | 4,6 |
| `CREATEITERRESULTOBJ_V8_V8` | Unmeasured | SupportedVerified | 4,6 |
| `STOWNBYNAME_IMM16_ID16_V8` | Unmeasured | SupportedVerified | 4,6 |
| `WIDE_STOWNBYINDEX_PREF_V8_IMM32` | Unmeasured | SupportedVerified | 4,6 |
| `WIDE_COPYRESTARGS_PREF_IMM16` | Unmeasured | SupportedVerified | 4,6 |

32 complete fixtures: 30 constructed modules and two SDK default/O0 modules;
244 Node cases, source-key/owner/arrow/premise mutations and exact byte/work
boundaries. Each exported output passes node --check. The strict occurrence
writer imports 241 new Node occurrences and 31 matched SDK round-trip events;
210 unmatched events retain null round-trip evidence. Ark runtime remains null.

`grouping_bytecode.py`, both optimization modes: **30 exact records / 2 different
/ 0 unverified**. The two differences are the two-profile LDFUNCTION fixtures:
the JS oracle loads its unchanged module binding (`ldlocalmodulevar 0`), while
the recovered named function expression reads its immutable self binding
(`lda a0`). The latter directly matches the constructed LDFUNCTION/GetFunction
meaning. No instruction/register/branch/handler normalization was used. All
other compiled reference/recovered files are identical. The parenthesis-only
token checker rejects all 32 recovery pairs (declaration/name changes), as
expected; these rejects are not semantic passes. Recompiling an implicit-this
operation can use an ordinary ABI-this load plus Get/Set; explicit LDNEWTARGET
can become an ABI new-target load. The occurrence writer does not invent matches
across those different event sequences.

The 16 remaining lift-ok request now has ten gates: LDGLOBALVAR, LDSYMBOL,
STOWNBYVALUEWITHNAMESET (two widths), NEWOBJAPPLY (two widths),
STOWNBYVALUE_IMM16, DEPRECATED_LDLEXENV, THROW_UNDEFINEDIFHOLE, and
THROW_IFSUPERNOTCORRECTCALL_IMM16. The first two bypass source-observable global
bindings; computed name-setting retains its numeric-key name divergence;
NEWOBJAPPLY is outside the APPLY-only premise; the other rows retain their
existing context, declaration or intrinsic-guard proofs. All have the existing
constructed observations under tests/fixtures/isa/current or deprecated.
A new computed-own-store probe still reaches EvaluationShapeError::kUnsupported
and a general two-register Hole guard reaches the same shape boundary; neither
was counted as supported.

Tagged-template, object-spread and object-rest predicates are versioned but
their construction/event certificates are **not implemented** in this partial
round. Object rest additionally needs to account for the pinned helper unwrapping
JSProxy at runtime_stubs-inl.h:2641-2646; proxy input must be excluded by proof or
addressed separately, since that difference was not in the user-approved scope.
The remaining super stores, RegExp/prototype recovery and intrinsic throw-source
forms are also unfinished. An intrinsic throw cannot be replaced by a mutable
global Error-constructor call without proving the original source operation.


## Acceptance

Full build passes. Source regression 65/65, all scale 11/11, ISA 14/14,
focused owner/frame/route 5/5, property-receiver 1/1. Final touched sweep
166/171; remaining failures are the documented family_source_cli, tree_structure,
try_tree, class_tree_source and try_tree_source. An old unknown-op receiver
witness used LDNEWTARGET. Since that operation is now modeled, the test now
checks that it preserves retained register aliases, and keeps the original
unknown barrier assertion using a valid, still unsupported GETTEMPLATEOBJECT.
No behavioral or mutation requirement was removed.

Fresh OLD/NEW cohorts use wtbase cbc9389b and the actual measured binaries.
605/620 source and 542/620 previous complete, no new completions, regressions or
text drift. All 80 manifests are identical apart from source_contract labels
at the root and record entries (V6 to V7 only under --language-source).
`grep -rn language_source_v6 tests tools src` is empty. Historical V6 serialization
in include/ remains intentionally supported. Existing printed-spelling tests
were not relaxed: only version-label assertions and the raw-unsupported witness
changed. The printer adds one new leaf and existing complete text is unchanged.

At depth/width 800: new-target tree 1,127,155 bytes / 162,643 work;
implicit-this tree 1,240,855 / 177,053; split blocks 1,511,300 / 214,776.
Tests check each admitted exact boundary and immediate smaller refusal.
Darwin wait4 over all 80 sequential recovery runs reports a peak of 40,583,168
bytes. No Ark VM execution is claimed. Evidence and large JSON remain untracked
under build/review/isa-r3-work/ and build/review/isa-r3/. No commit/push/stash.
