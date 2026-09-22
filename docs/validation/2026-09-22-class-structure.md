# Ordinary Class Structure

## Evidence Recovered

Ordinary modern DEFINECLASSWITHBUFFER sites now include constructor entity,
observable constructor length, parent input register, literal entity, ordered
prototype/static properties, member target and owner record, template kind,
initial method kind and member function length. Targets require actual method
membership through a once-per-file MethodLookup. Duplicate keys are preserved.

This is creation-site evidence, not proof of final class/export identity,
runtime execution or instance field declarations. Method length is not a source
signature parameter count. Template and initial kinds remain separate because
the runtime may refine or retain method kinds. Deprecated/sendable class sites
are explicitly unsupported rather than silently interpreted as ordinary ones.

## Verification

- Warnings-as-errors Debug build; final full CTest 91/91, 27.48 s.
- ASan/UBSan class_buffer_test, class_creation_test, class_writer_test,
  module_writer_test: 4/4, 0.31 s.
- Failing builds observed before both new APIs; structure integration assertion
  failed on previous artifacts before class_sites was added.
- Fixtures cover method/affiliate folding, static/prototype split, empty class,
  duplicate keys, getter/setter/generator kinds, scalar literals, malformed
  variants and tags, affiliate range, exact/short budgets, both cache widths,
  malformed operand widths, and 200/201/8192-member inputs.
- `git diff --check` clean.
- Independent review found lazy member string failures escaping site isolation.
  A failing bad-key/bad-value test with a second valid class site reproduced
  this; bounded preflight now reports only that site's error and preserves the
  valid site. Read/FileChanged remain fatal. Re-review found no new issue.

## Paired Export

Outputs in `build/structure-classes-final/`: five ABCs, 152 records, 2111 methods,
101 class sites, 371 prototype and 3 static properties. All 374 properties in
these class buffers are methods; all target/owner references validate against
the exported method inventory. No scan or site failure. This does not imply
these classes lack instance fields; constructor stores remain to be analyzed.

TypeScript 5.9.3 declaration syntax and JSON/path/count checks pass. Debug
coverage remains 2111 programs, 8924 local records and 59306 positions.
No sidecar source is read as engine input.

External `/usr/bin/time` under CLI RLIMIT_AS 1,000,000,000 bytes:

| Input | Peak RSS (KiB) | Seconds |
| --- | ---: | ---: |
| Immersive/modules.abc | 8960 | 0.48 |
| MultiCommunityApplication/modules.abc | 9216 | 0.75 |
| pasteboard/modules.abc | 8704 | 0.17 |
| push modules.abc | 8576 | 0.18 |
| push widgets.abc | 8704 | 0.02 |

Only paired inputs measured; not full-corpus/final-engine memory certification.
Static classification manifest unchanged. Next: constructor instance writes,
class identity flow, globals/module bindings, source signatures and body
integration. Declaration-only output is not completion of executable records.
