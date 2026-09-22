# Whole-File Method Roles

Debug build succeeded; final full CTest: 104/104 passed, 27.18 seconds.
ASan/UBSan method_roles_test: passed, 0.06 seconds.

MethodRoleIndex covers all typed literal operands and ordinary/sendable class
constructor targets in every decoded method. Runtime references are
LiteralDataExtractor::DefineFunctionTemplate, DefineMethodInLiteral and
compiler/jit_compilation_env.cpp's constructor MethodLiteral update.

Tests exercise ordinary/other/conflicting tags, repeated references, constructor
conflicts, legacy rejection, missing targets, exact/short shared budgets, moved
source queries, and 200/201/400/800/1600 repeated references and literal pairs.
Review identified and closed the moved-from null dereference and broadened
the proof to include constructor/JIT role contamination. No blocking finding
remained after reviewing the expanded scanner.

Five real paired inputs completed the role scan without failures:

| Input | Ordinary-Only Targets | Conflicts | Peak RSS KiB |
| --- | ---: | ---: | ---: |
| Immersive | 162 | 0 | 9216 |
| MultiCommunityApplication | 94 | 0 | 9596 |
| pasteboard | 38 | 0 | 8960 |
| push modules | 76 | 0 | 8960 |
| push widgets | 4 | 0 | 8448 |

374 ordinary-only targets are role evidence, NOT 374 recovered methods: initial
attributes and other source admission conditions still apply. The existing
nine class expressions and real CreateFormData field/setter oracle passed again
using build/classes-roles-*.mjs. No new class/member source coverage is claimed.
These small process RSS measurements do not prove the final whole-engine 1 GB
ceiling. No samples or static classification cache changed.
