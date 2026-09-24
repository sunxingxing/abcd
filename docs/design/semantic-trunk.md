# Semantic Source Trunk

The ongoing experimental corpus is now [all Pasteboard records](pasteboard-trunk.md).
The earlier CommentItem slice below remains a baseline, not the corpus boundary.

The first end-to-end target is the real `CommentItem` constructor (entity
20651, Dynamic1202, in MultiCommunityApplication). It has branches and named
property reads/writes that can invoke getters, setters or proxies. The
`BreakpointType.getValue` method (entity 19360) also lifts into the IR, but
its guarded external module load needs a later module/Hole source proof.
Neither adjacent source files nor `sourceMap.map` are engine inputs.

1. Decode and validate the method under its explicit profile. Lift each
   supported instruction to a typed semantic operation retaining its original
   PC, normal register effect, and exceptional effect. A missing rule rejects
   the method; it never becomes an opaque source statement.
2. Build the existing normal CFG and register SSA over the same decoded body.
   Validate environment and effect preconditions before source recovery.
   Exceptional dispatch joins the model only when represented explicitly;
   normal SSA must not be used as proof across an exception edge.
3. Recover an acyclic region tree for this method. Lower regions to a typed
   source AST, preserving each operation's origin. The printer records output
   byte offsets and line/column while writing, rather than searching JS text
   afterward. The first artifact is an executable class constructor plus a
   method-entity/PC-to-generated-span map.
4. Route this method through the new writer only after comparison against the
   existing verified path. Keep the old writer as a fast path and differential
   baseline. Gradually move record, class, and module writers to the same AST
   and printer. Do not turn failures into executable-looking placeholders.

The initial semantic IR is deliberately strict: it only admits operations
with typed interpreter contracts. Normal and exceptional effect summaries are
separate, and a may-throw operation never publishes its register result on
the exceptional path. Its vectors, CFG, SSA, AST and position map all share a
caller-admitted memory budget. Index-based traversals avoid pairwise scans.
Initial profile admission is explicit for Dynamic1202, Dynamic1206 and
Dynamic1301; individual operations still need their semantic contract.

`abc_semantic INPUT.abc 20651 --class-source` currently exercises this entire
method path and emits JSON with `source` and `positions`. The CLI's class form
is an explicit caller assertion, not inferred from the method name or opaque
strings. Production record/class ownership must supply a proven source form
before this path replaces the corresponding legacy writer. Unsupported module
loads, Hole guards, exceptional regions and closures still fail before source
publication; the `getValue` method remains an IR-only diagnostic case.

Current work is staged in coherent commits. Compilation and direct sample
inspection are allowed; the user's request to stop tests and RSS tracking
remains in force until changed. The phase is not complete until the one real
function crosses every layer and the output/map can be checked.
