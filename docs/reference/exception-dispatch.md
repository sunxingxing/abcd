# Dynamic Exception Dispatch

Reference: ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9` and
runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`, both pinned to
OpenHarmony-v7.0-Release in the reference lock. These sources use Apache-2.0.

## Selection Rule

`ecmascript/method.cpp`, `Method::FindCatchBlock`, enumerates try entries in
encoded order, with coverage `start <= pc && pc < start + length`. It selects
the first encoded catch of the first covering try with a nonempty catch list.
An empty list allows the scan to continue. Catch type and handler length are
not tested here. Sorting by nesting depth, trying catch types in sequence, or
preferring the narrowest range would change the pinned dynamic VM's behavior.

`libpandafile/code_data_accessor-inl.h` preserves encoded try/catch order and
stops enumeration when the callback returns false. The interpreter's EXCEPTION
handler in `ecmascript/interpreter/interpreter-inl.cpp` walks frames until a
handler is found, sets the accumulator to the thrown value, clears the pending
exception and resumes at the selected PC. Absence of a local target means
propagation to the caller, not a synthetic local catch.

## Representation and Bounds

`ExceptionDispatch` owns sorted non-overlapping byte-PC spans, each retaining
the selected strong `TryId`, `HandlerId` and handler PC. The source MethodBody
retains every encoded handler and is not reordered or modified. Adjacent spans
merge only when the selected target, including source try identity, is equal.

The builder validates instruction/range boundaries, sorts start/end events,
and uses STL heap operations with encoded try index as priority. Lazy expiration
removes each active entry at most once. With I instructions, R tries and H
handlers, build time is O(I+(R+H)log(I+1)+Rlog(R+1)); auxiliary/output storage is
O(R), and lookup is O(log(R+1)). There is no instruction-by-handler matrix.
Exact vector reserves and their actual capacities are charged to an additional
64 MiB default budget; the caller still owns input and whole-process admission.

The validation CLI additionally checks span endpoints and selected handler PCs
against ordinary basic-block boundaries using binary search. Protected-block
queries cost O(B log(R+1)), plus O(R log(B+1)) target/boundary validation.

## Semantic Limits

The map answers where an already-raised exception goes. It does not assert
that an instruction throws, nor model the register/effect state at that point.
Do not attach one block-exit state to every exceptional transfer: the throw may
occur before later writes in the block. Exception-aware SSA and source recovery
must use instruction-specific abrupt-completion states.

The pinned dynamic rule is selected for recognized dynamic profiles. Static and
unknown profiles are rejected. Header recognition and successful corpus mapping
do not independently certify historical runtime semantics, source-equivalent
try/catch/finally reconstruction, or host/native exception behavior.
