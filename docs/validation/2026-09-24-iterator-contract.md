# Iterator and Private-Property Contract Validation

Pinned reference: OpenHarmony-v7.0-Release `runtime_core/isa/isa.yaml`
`iterator instructions` group and matching
`ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` handlers. This change
adds typed lifts and normal-completion register effects for seven iterator
encodings and three private-property encodings. The two named-field DEFINE
encodings in that ISA group already use existing reviewed paths. It does not
admit any new function or record source.

- Full default-build CTest after the final change: 153/153 passed. Targeted
  iterator, private-property, register-effect, SSA, value-flow and source
  refusal tests passed. Invalid widths/kinds/counts and frame boundaries are
  covered by negative tests.
- Full source-body probe over 900 paths: 402 dynamic, 498 hash-verified static
  skips, zero failures; 5,486,254 methods decoded. Ready 1,500,536,
  rejected 3,985,718; 261,735,459 output bytes sent to the counting sink.
  Ready + rejected and the obstacle histogram both reconcile to the method
  total. These are body-admission counts, not whole-record source or behavior
  equivalence.
- The probe reported 176,902,144 bytes peak process RSS. External
  `/usr/bin/time` measured 172,756 KiB peak RSS and 114.19 seconds elapsed.
  Its 1,000,000,000-byte address-space cap remained enabled. No child workers
  were used. This is not a complete end-to-end decompilation memory proof.

The optimized probe used `build/decode-release` with `-Wall -Wextra` but not
`-Werror`. The existing Release build fails under `-Werror` on unrelated
`linear_body.cc` aggregate-initializer / `maybe-uninitialized` diagnostics and
`static_fields.cc` signedness diagnostics. The default build retains its normal
flags and full CTest passed. No production source was changed to suppress those
warnings. Generated probe JSON and timing remain untracked under `build/`.
