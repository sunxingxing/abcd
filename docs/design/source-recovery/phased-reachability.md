# Phased import reachability (Round 36)

The projection is general graph reachability, not a return/NOP or compiler
layout recognizer. From ExceptionFlow node 0, traverse all normal and exception
successors using a charged bounded queue. Before, Commit, and Catch nodes are
classified independently. A Catch reachable only by an exception arc is live;
an always-throw operation can have a live Before and a dead Commit.

Working block, operation, original instruction and phase IDs stay stable.
Dead Before nodes retain removed NOP slots and original site/PC evidence, but
no effects, reads, result, or exceptional exit. All edges out of dead phases
are absent from the working graph. Exception SSA already renames reachable
nodes only; the importer checks its definitions and phi input edges against
the projected relationships, rather than inventing versions for dead sites.

`VerifyImportedPhases` independently traverses immutable ExceptionFlow and
checks every retained/dead phase, edge, original site, operand, definition,
exceptional exit and phi input. Environment absence is checked against graph
reachability; it is never authority to erase an operation. Value-flow checking
independently recomputes working-graph dominance/reachability. Unreachable
phases contain neither executable operations nor outgoing edges.

Exception layout checks impose dispatch and lexical-entry obligations only on
reachable original nodes. Control retains provenance-only disconnected phases
in `inactive_phases`; instruction correspondence records them as
`UnreachableInstruction`, not purity elisions. Child discovery ignores dead
creation operations. Every reachable handler and its catch payload survives.
Source structuring still requires a binding for each represented try wrapper:
an entirely unreachable handler can import but may still receive a typed
`kRegion` refusal from exception control. Removing empty try wrappers is not
implemented by inventing catch payloads.

Import failures retain `PhasedMethodError` and `ValueImportError` in the bounded
cause chain. Manifest/receipt fields distinguish failed `method`, original
`pc`, normalized `working_pc`, and `creation_method`/`creation_pc`. A site-less
failure stays site-less; a parent's creation PC never fills in a child PC.
Resource/work failures retain their prior classifications.

The new traversal/index work is O(V+E), with buffers charged before allocation.
Existing independent dominance/phi checks retain their original complexity.
Verifier scratch is reported and charged by the importing owner. Tests locate
the composed exact byte admission boundary (upstream dominators may admit a
larger ceiling than their consumed charge) and exercise 200/201/400/800 sizes.

Tests: `phased_value_import_test`, `exception_plan_test`,
`family_route_failure_test`, `source_route_test`, and
`phased_reachability_cli_test`. The latter uses a freshly compiled ABC and Node
source oracle for returns, exceptions and finally traces, including mutation
detection; see the [fixture limitations](../../../tests/integration/fixtures/phased_reachability/README.md).

The eleven authorized record probes (LanguageSourceV1, outputs only in
`/private/tmp/round36-final-records`) all pass the previous import gate:

| Input / record | Current result |
|---|---|
| o4 / 4901 CertChainModel | complete, Node syntax check passes |
| o6 / 5058 AudioStreamController | FunctionTreeError::kControl |
| o19 / 18254 AudioOutputDeviceChange | ScopeError::kInitialization, method 20458 |
| o19 / 28925 AudioOutputDeviceSwitcher | PhasedMethodError::kControl, method 29461 |
| o21 / 9820 Resource | complete, Node syntax check passes |
| o22 / 34064 ProductSpecPage | complete, Node syntax check passes |
| o24 / 37520 CameraService | FunctionTreeError::kControl |
| o26 / 4515 Index | ScopeError::kInitialization, method 8331 |
| o32 / 38062 assertThrowError | complete, Node syntax check passes |
| o34 / 10794 renderer | ScopeError::kInitialization, method 13644 |
| o39 / 21472 WebSaveImage | FunctionEmitError::kKind, method 22136 |

Initialization leaves retain the full CaptureCellError::kInput →
CaptureError::kScope → ScopeError::kInitialization chain. These probes are not
an exploration/holdout evaluation; the four complete records have syntax
checks, not paired application execution proofs.

## Verification (uncommitted Round 36)

- Keep-going regression build succeeds; fixed source-regression runs serially,
  65/65. Focused import/throw/exception-plan/receipt/CLI tests: 7/7.
- Serial phase/exception/source slice plus the requested existing failures:
  293/324. Seven literal and two generator failures were recorded both before
  and after this change, with unchanged failing status. The other 21 failures
  (excluding community graph) were reproduced using a separate build of exact
  HEAD `03b1681b` under `/private/tmp/round36-head-baseline`.
- Community graph failed in two different worker cases, then passed a third
  serial run (145.08 seconds) with a temporary child-process error diagnostic.
  The first two runs hid the child exit reason; timeout is not established.
- Logs: `/private/tmp/round36-final-build.log`, `round36-fixed.log`,
  `round36-focused-final.log`, `round36-targeted.log`, `round36-baseline.log`,
  `round36-head-tests.log`, and `round36-community-diagnostic.log`.
  No exploration, holdout, full-suite or RSS benchmark was run.
