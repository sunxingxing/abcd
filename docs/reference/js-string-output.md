# JavaScript String Output

`source::WriteStringLiteral` emits a double-quoted JavaScript expression from
UTF-16 units. Printable ASCII is retained except for double quote and backslash;
all other units use fixed-width lowercase `\uXXXX` escapes. This deliberately
preserves lone surrogates, embedded NUL and surrogate pairs without Unicode
normalization or dependence on the output file's text encoding. A NUL followed
by a digit cannot become a legacy octal escape. This is not HTML escaping.

The first linear pass computes exact output size with checked budget arithmetic
before writing. The second pass writes through a fixed 1024-byte buffer. Output
limit failure leaves the destination untouched; IO failure may leave partial
source. The record writer must eventually use temporary files and atomic
publication. Caller-owned input/output storage still belongs to the process
memory budget; the emitter does not buffer the entire generated expression.

Unit tests cover delimiters, controls, line separators, lone surrogates, pairs,
200/201/1024/8192 units, output limits and failed streams. The integration test
emits all 65,536 UTF-16 units into one JS string and evaluates it with Node,
checking its length and every charCodeAt result. Node is a test-only external
oracle, not an engine dependency; CMake warns if it cannot register this test.
The local oracle version is v24.15.0.
Integration verification: Debug CTest 22/22 and ASan/UBSan CTest 17/17 passed,
including the JS roundtrip in both configurations.

This emitter is the string-expression leaf of future typed source generation,
not record decompilation. It does not decide identifier legality, reconstruct
bindings or expressions, or prove source equivalence for any ABC method.
