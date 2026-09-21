# Dynamic Function ABI Evidence

Reference: the runtime and runtime_core commits pinned in
`openharmony-v7.0-release.lock.json`. This is source inspection, not an
execution oracle or proof of historical runtime equivalence.

## Annotation Selection

`ecmascript/jspandafile/method_literal.cpp`, `MethodLiteral::Initialize`,
enumerates ordinary method annotations, not runtime or parameter annotations.
It matches descriptor `L_ESCallTypeAnnotation;` and element name `callType`.
Within each matching annotation the first matching element wins; subsequent
matching annotations can overwrite that value. Missing annotations leave the
initial unsigned all-ones value, whose low four bits are all enabled.

`MethodLiteral` in `method_literal.h` defines these bits:

| Bit | Meaning |
| --- | --- |
| 0 | This binding slot present |
| 1 | New-target slot present |
| 2 | Extra-argument handling enabled |
| 3 | Function-object slot present |

The runtime masks the call type before storing it. Extra-argument handling is
not another mandatory prefix slot. The code item's argument count includes
present function, new-target, and this slots. Subtract their count to obtain
the declared user argument count; reject underflow before unsigned arithmetic.

Upstream compares names using a byte string view with the reported UTF-16
length. A reader must explicitly decide whether to reproduce this boundary
behavior or reject noncanonical spellings; silently assuming decoded Unicode
comparison is equivalent is not justified.

## Register Initialization

The entry path in `ecmascript/interpreter/interpreter-inl.cpp` near lines
700-770 pushes arguments, then this, new-target, function, then local registers
on a descending stack. Thus ascending bytecode register indices contain:

1. Declared local registers, initially undefined.
2. Function-object slot, if enabled.
3. New-target slot, if enabled.
4. This slot, if enabled.
5. Declared user arguments in source argument order.

Absent hidden slots consume no register index. Missing user arguments receive
undefined. Extra-argument handling affects storage beyond the declared frame
when actual and declared counts differ, including an actual-count value; it
must not increase the statically declared bytecode register window.

The entry accumulator is Hole, not undefined. Any function emitter that simply
initializes every slot to undefined loses this distinction. Model uninitialized
accumulator state explicitly and prove reads safe before emitting expressions.
The frame also retains function and this independently of optional register
copies. A missing this register does not prove that the function has no this
binding. These observations concern this interpreter entry path, not every
optimized or generator-resume entry path.

## Container Layout and Implementation Constraints

`libpandafile/annotation_data_accessor.cpp` resolves the annotation's class
index using the region containing the annotation, not the owning method.
Elements are fixed-width name-reference/value pairs followed by a separate
type-tag array. Names are string entity references. Runtime call-type reading
uses the scalar value without consulting its type tag.

`ReadMethod` in `src/abc/metadata_index.cc` validates annotation reference
ranges and retains the first ordinary annotation tag's file offset and count
in `MethodAnnotations`. Ordered method tags make these entries contiguous;
runtime, parameter and type annotations are excluded. The fixed-size range
does not allocate per annotation and is included in the existing method-vector
capacity budget. Tests cover empty, mixed categories, and 1/200/201/4096 entries.
Annotation payloads still require lazy reads before the index can recover ABI
flags; the range alone is not annotation-payload validation.

`abc/frame_layout.h` now exposes `CallFlags`, `DecodeCallFlags`, and
`BuildFrameLayout`. The allocation-free layout calculation covers all sixteen
flag combinations, missing-annotation defaults, high-bit masking, hidden-slot
order, argument underflow, and the runtime's two 28-bit count fields. It rejects
counts that the runtime would truncate. This standalone calculation is not yet
connected to annotation reading or function source emission.

Required checks and tests for the complete annotation integration:

- Typed call flags and optional register IDs, never semantic string dispatch.
- Missing annotation default, all sixteen low-bit combinations, duplicate
  annotations and duplicate element order.
- Annotation-region lookup distinct from method-region lookup; invalid names,
  bounds, truncated elements and tag arrays.
- Argument-count underflow and register-index overflow without wraparound.
- Extra-argument flag independent of mandatory-prefix length.
- Local undefined versus accumulator Hole initialization.
- Historical profiles reported separately until equivalence is established.
- Bounded lazy reads and whole-process memory accounting; no eager annotation
  object graph or unbounded per-method cache.

This evidence does not yet produce recovered functions or per-record source.
