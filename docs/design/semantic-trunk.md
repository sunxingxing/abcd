# Semantic Source Trunk

The first end-to-end target is the real `BreakpointType.getValue` method
(entity 19360, Dynamic1202, in MultiCommunityApplication). It branches, reads an external
module binding, performs a Hole guard, and reads properties whose getters or
proxies can execute user code. Neither adjacent source files nor
`sourceMap.map` are engine inputs.

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
   afterward. The first artifact is a complete executable function plus a
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

Current work is staged in coherent commits. Compilation and direct sample
inspection are allowed; the user's request to stop tests and RSS tracking
remains in force until changed. The phase is not complete until the one real
function crosses every layer and the output/map can be checked.
