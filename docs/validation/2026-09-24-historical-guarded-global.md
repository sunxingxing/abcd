# Historical Guarded Global Prelude

The paired `push-kit-sample-code-clientdemo-arkts/widgets.abc` is dynamic
12.0.6.0. Its `WidgetCard` record entity 564 has entry method 1118. The
existing typed `MatchGuardedGlobalCall` proves the complete diamond from
instruction index 3 through 32, including repeated pool slots, register
uses, branch targets and later transient-register liveness. Only the outer
record and inner prelude profile gates changed to admit this exact shape;
ordinary global-property reads remain refused.

The real-ABC integration fixture calls the matcher and renderer, then runs
the generated source through the existing Node oracle. With the key absent,
it checks prototype/set/prototype/call ordering, the call's `this` receiver,
argument receiver, key and fresh function; with the key present, it checks
one prototype read and no call. A throwing second prototype getter must
prevent the call. The record interpreter now progresses past
the guard and refuses at the later class creation at entry PC 107
(`RecordProgramError::kClass`). No `WidgetCard` source file is published.

The focused C++ and Node tests and all 165 CTests pass. A clean-process
`/usr/bin/time` run of `abc_structure --source` on this 13 KiB ABC measured
10,624 KiB peak RSS.
This is not a full-corpus memory certification.
