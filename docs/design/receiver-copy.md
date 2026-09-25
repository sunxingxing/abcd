# Entry Receiver Copy

The initial single-receiver implementation below has been superseded by the
shared bounded entry-binding table in `entry-bindings.md`. Its receiver proof
boundaries remain; immutable formal copies now share the same SSA scans.

The common printer can now recover the first direct ABI receiver copy in a
normal method's pure entry register-copy prefix. This builds on the owner's
typed read-only native-this binding, not the register's spelling or debug name.
Arrows and derived constructors do not carry that binding. Phased exception
owners and catch regions are excluded from this initial certificate.

The original register SSA must contain exactly the copy definition for its
destination, apart from the method-entry value. Any other write or phi refuses
the substitution. All SSA reads of the destination must consume the copy
definition, excluding initialization-before-use mistakes. Failure does not
retry other candidates and repeat the SSA traversal. Work is O(I + V + U), for
entry-prefix instructions, SSA values and SSA reads, with O(1) scratch.

Only typed scalar read expressions are rebound to the immutable ABI receiver
before expression emission. Scalar, unary, binary and property-read operands
are handled exhaustively. Assignment, array-commit and array-literal targets
use a distinct printer path that does not substitute the copy. This separates
read semantics from register identity without introducing semantic strings.

The original register declaration and initialization remain: capture and other
identifier-only consumers still need them. The optimization does not authorize
declaration deletion, new class fields or broader variable coalescing. Existing
instruction/AST provenance is unchanged; final coordinates continue to come
from the printer rather than generated-text reconstruction.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include writes and phis, entry-value reads, repeated loop entry,
ordinary async normalization, phased exception owners, target emission,
captured identifier consumers, and geometric SSA scaling. Read-only review
found no concrete correctness issue. Runtime equivalence is not yet verified.

Both production targets built successfully. ABC-only export
`build/pasteboard-receiver-copy-01` recovers direct `this` field reads/writes
and returns in CommonConstants/BreakpointType; assignment targets remain
register identifiers. Output inspection found no standalone `this =` statement.
Eight incomplete app records, two host-constructor refusals at PC14 and nine
metadata records remain. Method maps retain 195 entities / 9,745 spans, without
claiming complete instruction coverage.
