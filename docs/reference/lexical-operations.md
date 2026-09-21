# Lexical Environment Operations

The typed `LiftLexicalInstruction` boundary covers sixteen encodings: three
modern loads, three modern stores, three deprecated register stores, four
environment allocations, two pop encodings and deprecated current-environment
load. It does not emit closure source or traverse runtime environment objects.

Pinned `ecmascript/interpreter/interpreter-inl.cpp` reference behavior:

- NEWLEXENV and named variants near 4583-4639 allocate a child environment,
  install it as current and write its internal identity to the accumulator.
  Named variants retain a literal-array pool slot, not a direct entity offset.
- Deprecated LDLEXENV loads current environment identity into the accumulator.
- POPLEXENV variants near 4648-4660 install the parent without changing acc.
- LDLEXVAR near 6067-6116 follows exactly `level` parents, then loads `slot` to
  acc. Level zero selects the current environment.
- Modern STLEXVAR near 6118-6170 writes acc into the addressed slot and leaves
  acc unchanged. Deprecated variants near 6172-6231 use an explicit register
  as the value source, also preserving acc.

`FastRuntimeStub::NewLexicalEnv` connects the parent and global environment.
Both `ObjectFactory::NewLexicalEnv` and `InlineNewLexicalEnv` initialize fresh
slot storage to Hole. Therefore neither undefined-filled JS arrays nor eager
ordinary-variable reads are generally equivalent. A load does not itself
perform a JS TDZ check; explicit hole-check instructions must be incorporated
before source declarations and accesses can be recovered faithfully.

IR alternatives encode loads/stores/creation/pop/current separately. Addresses
use typed depth and slot fields; store sources distinguish accumulator from
register. Allocation counts and optional scope slots are retained without
allocating per-slot IR. Bounds checks validate encoded operand ranges and
register windows, not the dynamically selected environment's size or ancestry.

Future closure binding must account for control-flow joins, loop-created fresh
environments, captured environments surviving pop, and shared/sendable context.
Do not rename `(depth, slot)` globally as if it were a unique variable identity.
Do not expand a 65,535-depth address by walking a chain per use: environment
resolution needs indexed ancestry or another bounded scalable representation.
This lifting primitive itself is constant time/storage independent of depth,
slot count and method size.

Tests decode every supported encoding, distinguish modern/deprecated sources,
check packed-nibble ordering, optional scope references, register bounds,
malformed operands, 200/201 and maximum-width counts. These structural tests
do not establish historical runtime equivalence, TDZ/source behavior, captured
binding identities or complete per-record decompilation.
