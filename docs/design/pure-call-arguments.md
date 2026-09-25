# Pure Arguments Through the Shared Call AST

Receiver calls with pure argument preparation previously selected a late
MemberCallExpression, while effectful arguments used SourceStagedCall. The
late form kept VM argument and receiver commits even when they had no other
observers. Normal-SSA calls with an already-proven callee spill/reload and at
least one argument now use SourceStagedCall as well.

All original admission checks remain: exact callee definitions and single uses,
property/receiver SSA identity, ordered property/spill/reload/call positions,
block ownership and noncrossing nested call intervals. Getter evaluation stays
at the original property PC, followed by argument evaluation and then invocation
with the original receiver. Pure argument expressions gain no license to move
across the getter. Phased exception calls retain their existing route-selected
form; zero-argument and unsaved calls retain their prior path.

This reuses the common call AST, SSA dead-copy elimination, ordered argument
proof, slot audit and printer. The explicit original call receiver read is
deducted exactly once under the existing native Reference certificate; all
other/phi uses stay live. Original property, callee save/reload, argument and
call PCs retain their mapping machinery. There is no new semantic string,
sample-specific path, AST variant, dependency or storage beyond the existing
budgeted call interval stack. Added selection work is constant per call.

Tests, JS execution and RSS remain paused. Deferred regressions include getter
exceptions, mutable receiver slots, shared/phi callee uses, nested and crossing
intervals, argument-register reuse, output origins and phased-call stability.

## Production Evidence

Both production CLIs build. ABC-only export `build/pasteboard-pure-arguments-01`
recovers calls such as ViewPU.create($r6), Button.fontSize((16)) and
NavDestination.title("PageA") through the existing native argument printer.
Setter/lifecycle calls also lose private receiver/argument transports where
the existing SSA proofs allow it. All six pages change; source totals become
4,637 lines / 95,072 bytes, down 378 lines / 4,003 bytes. Method coverage is
unchanged, and these counts do not prove executable or equivalent source.

Partial maps retain 195 methods / 9,745 spans; statuses remain eight incomplete,
two host constructors refused at PC 14 and nine metadata-only records. PageA
method 7366 receiver load PC 49 maps to `Button` at bytes [6447,6453), while
argument save PC 68 maps to `(16)` at [6463,6467). Existing call-origin machinery
retains getter/call spans and eliminated transport anchors.

Read-only review found no concrete defect. More calls now enter existing staged
depth/output admission, so deeply nested inputs may still be conservatively
refused under those bounds. No test, generated JS execution or RSS run was made.
