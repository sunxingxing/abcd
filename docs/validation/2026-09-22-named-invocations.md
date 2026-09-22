# Named Invocation Checkpoint

The user redirected priority to record paths, module/class declarations and
symbol/signature recovery before further body work. This checkpoint preserves
the already implemented fixed CALLTHIS named-invocation increment; no further
function-body expansion or full-corpus run is part of this checkpoint.

- Initial named-call C++ success assertion failed before implementation.
- Debug build succeeds; targeted named-call C++/Node, accumulator, acyclic,
  block-loop and source-probe CLI tests passed 6/6 in 4.08 seconds.
- Tests cover fixed arities, LDA/STA provenance, malformed/register operands,
  wrong receiver, property/call block entries, handler refusal, block loop,
  getter/Proxy order, raw primitive this, exception identity and output quotas.
- Scaling covers 200/201/4096/65536 repeated invocations.
- Independent code/runtime review found no concrete issue under the documented
  private-temporary, handler-free abrupt-exit contract; reviewer did not run tests.
- No full-suite, sanitizer or corpus coverage result is claimed for this change.

This is not a record-output deliverable; see the subsequent structure-first
implementation priority.
