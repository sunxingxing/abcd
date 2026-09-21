# Literal Array Reader

Reference: OpenHarmony-v7.0-Release runtime_core
`3e147bb76e0b622cc2e4399ef954b030c0639691`,
`libpandafile/literal_data_accessor.h` and `literal_data_accessor-inl.h`
(Apache-2.0). Consumer constraints are recorded in constant-references.md.

ReadLiteralArray reads a direct entity offset with the existing bounded
positional cursor. A U32 count represents tag/value units, so canonical tagged
arrays require an even count. Each supported tag determines its payload width.
The output retains ordered entries and the end offset. LiteralBits preserves
the complete numeric payload, including negative-zero and NaN encodings, the
raw byte of BOOL and NULL, and FLOAT's U32 representation. No floating-point
conversion or interpretation is performed here.

String, method, nested-array and implements references use EntityOffset rather
than LiteralBits. Their ranges are checked but their target payloads are not
loaded or certified. Self references and cycles do not cause recursion. Method
kind tags and METHODAFFILIATE remain separate ordered entries; the consumer must
pair and validate them before constructing a source-level function or property.

Packed ARRAY_* tags return kPackedArray, TAGVALUE returns kUnsupportedTag, and
unassigned tags return kUnknownTag. These failures do not classify the input
file as static. Static/unknown profiles are rejected independently. Historical
profiles use this pinned representation policy; tests do not prove historical
runtime equivalence.

The reader checks count feasibility against remaining bytes and admits exact
entry capacity before reading payloads. Time and retained storage are O(N) for
N entries; nested payloads are lazy. Allocation failures and budget exhaustion
are typed errors, and the caller must admit the reader's allowance within the
whole-process memory limit. Existing stat-based unchanged checks surround reads
and retain the immutability limitations documented in constant-references.md.

Tests cover every supported tag layout, ordered method/affiliate entries,
negative-zero bits, noncanonical true bytes, invalid references, unknown and
unsupported tags, every truncation of a mixed fixture, budgets, profile
rejection, and 200/201/1024/8192 entries. Full-corpus literal validation and
opcode-specific object/array/class reconstruction remain unfinished.

Integration verification: Debug CTest 23/23 and ASan/UBSan CTest 18/18 passed.
