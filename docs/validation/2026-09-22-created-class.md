# Created Class Source Validation

The adapter resolves actual ABC constructor targets, buffers, names, ABI and
bodies and emits native class expressions. These are creation-site expressions,
not complete record files or whole application reconstructions.

Five paired ABCs were read without sidecar source. class_sample_fixture writes
admitted expressions to build/classes-*.mjs and reports every rejected site:

| Input | Sites | Emitted | Peak RSS KiB |
| --- | ---: | ---: | ---: |
| Immersive | 41 | 2 | 9216 |
| MultiCommunityApplication | 32 | 5 | 9472 |
| pasteboard | 11 | 1 | 8832 |
| push modules | 16 | 1 | 8960 |
| push widgets | 1 | 0 | 8448 |

Node executed all nine recovered expressions, checked fresh identity, native
class invocation restrictions, prototype chains and constructor ownership.
CreateFormData additionally exercises real recovered instance assignments to
formId, text_key and image_key: initial empty-string values, writable/enumerable/
configurable own properties, and ordered inherited setter calls rather than
field-definition semantics. The checked-in class_samples.mjs oracle reproduces
these checks; it is validation-only and never an engine input.

Synthetic tests verify parameter length/name, return substitution, descriptors,
unknown/null/mismatched-PC rejection, nonempty-buffer rejection, missing code,
invalid pool/name, exact/short output and scratch budgets. Initial kinds are
explicitly refined by class creation; sendable remains rejected. Shared runtime
name extraction retains the ordinary-function tests.

Full Debug CTest: 101/101 passed, 28.71 seconds. ASan/UBSan created_function_test
and created_class_test: 2/2 passed, 23.88 seconds. Initial-kind policy assertions
were added afterward and separately rerun before commit.

Remaining 92 sites are rejected, never replaced with fake class bodies. Member
buffers, derived classes, captures, complex signatures, module/global binding
and subsequent initialization are not complete. Small fixture process RSS is
not certification of the final concurrent engine's 1 GB bound. No input sample
or static classification cache was modified.
