# First `for-in` Function Recovery

Revision: `a678564`. Reference: pinned `OpenHarmony-v7.0-Release` identities in
`docs/reference/openharmony-v7.0-release.lock.json`.

The v7 frontend `ets_frontend/es2panda/ir/statements/forInStatement.cpp`
emits `GETPROPITERATOR` inside a setup try, a transparent catch/rethrow,
then `GETNEXTPROPNAME` and an undefined exit check. The runtime's for-in
iterator returns undefined when exhausted. The new whole-body matcher accepts
only the complete first-key-return form, with exact branches, try/handler
bounds, register relationships, and a source-register ABI parameter. The
function writer emits native `for (const $forInKey in input)` and a fallthrough
return. It does not reconstruct general loops or suppress exceptional edges.

The source admission gate is `kDynamic1301` with caller-proven method constants.
Historical ABCs still decode and retain typed iterator effects, but this
version-sensitive source rewrite refuses them until separately validated.
No dependency was added. The matcher uses fixed-size scratch and at most 16
instructions, so its time and memory costs are constant per candidate body.

`/home/rabbit/sample/abc/4655acad3603c111523c7c96488f06bf.abc` has a
matching dynamic method at entity `16986016` in record
`L&@douyin/google-protobuf/google-protobuf&3.21.2;`. Its frame has 2 locals,
one user parameter at register 5, and a 16-instruction body with one setup
try and one untyped rethrow handler. The test-only `iterator_candidates` tool
reports `first_for_in=true` for that method. This is real bytecode evidence,
not an assertion that the containing record is source-ready.

Verification after the change: full build succeeded; CTest passed 154/154.
The Node oracle covers own keys, inherited keys, empty enumeration, and Proxy
errors. The selected-file diagnostic used 138,204 KiB peak RSS. No full-corpus
RSS measurement was made for this revision; the 1,000,000,000-byte process
limit remains a release gate rather than a claim established by this run.
