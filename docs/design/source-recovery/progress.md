# Implementation Progress

Development is performed directly by Codex. The external Claude controller
was stopped at the user's request; it must not be restarted for this plan.
Scope remains T00 through T12 in the dependency order from the implementation
plan. No task scheduler, additional host assumptions or sidecar source inputs.

## T00

Implemented route admission, identity-bound receipts, central validation,
checked serialization, parsing and obligation closure. Evidence rules are now
version 2; schema remains 2 and working revisions are explicitly absent.
Invalid receipts cannot be serialized as successful evidence. CLI publication
handles typed serialization errors. Holdout hashes exclude all development
path aliases; G1/G2/G3 independent candidates are explicitly unfrozen until
method-level inspection establishes their capabilities.

Regression specifications cover CLI option ordering, receipt contradictions,
missing fields, numerical narrowing, direct body/template/entry/record guards,
and a legacy-success versus required-route-refusal function. These tests have
NOT been compiled or executed under the current test pause.

Production build evidence after `e23d304`:

```text
cmake --build build --target abc_structure abc_semantic -j2
exit 0, both tools built
```

ABC-only inspection on 2026-09-26, without paired source:

```text
build/abc_semantic /home/rabbit/sample/abc_with_code/pasteboard/modules.abc 7586 --value-source
exit 1: route=value_tree, stage=decoded, coverage=unsupported
reason=route_unavailable, legacy_writer_used=false, evidence_rules=2
file_size=112024, record=6897, work_revision=null
```

This is expected admission refusal, not recovered source. Behavioral regression
and resource validation remain unverified. Existing review findings have been
addressed in implementation/specifications; this does not assert tests passed.

## T01 In Progress

Normal operand mapping, owned work graph, SSA import, use-chain checking and
independent input correspondence traversal are implemented. The method CLI now
calls BuildValueStage instead of refusing every decoded method at admission.
It builds fresh input analysis/proof without legacy AST/printer participation.
Receipt schema/evidence rules 3 require a working revision from ValuesImported
onward; pre-import and legacy products keep typed absence.

ABC-only inspection of pasteboard method 7586 now reports:

```text
stage=values_imported, coverage=incomplete, work_revision=0
operations=21, values=29, uses=23, blocks=3, edges=3, exceptional_exits=3
legacy_writer_used=false, failure=null, source_complete=false, executable=false
exit 1 (no complete source yet)
```

This supersedes its T00-only route-unavailable observation above. Independent
application/profile candidates, entry/phi adversarial cases and stronger
input/owner verification remain outstanding; no full T01 validation claim.
Regression specifications and generated-JS/resource execution remain paused.

### Remaining Input Work

Use immutable SemanticMethod/SSA and its source proof as the input layer.
Import a separate owned ValueProgram with typed operation operands, explicit
read occurrences and source correspondence. Retain entry-phi inputs and
per-operation exceptional exits. Reject handler/resume inputs at the normal
import boundary. Do not invoke the legacy SourceAst or a legacy printer to
fill missing stages. Admission progress must produce truthful intermediate
receipts, not complete-source claims. T02 through T12 remain outstanding.
