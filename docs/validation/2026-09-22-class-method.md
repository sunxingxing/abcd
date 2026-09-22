# Ordinary Class Method Validation

Debug build succeeded; full CTest: 103/103 passed, 27.30 seconds.

Scope: a native ordinary-method source primitive sharing the existing ABI,
parameter, local lexical and body writer. It supports instance/static placement
and escaped UTF-16 computed keys. It is not yet connected to WriteCreatedClass;
this increment claims no additional recovered real-corpus classes or records.

Node executes generated instance/static methods and checks:

- Native nonconstructibility and absence of an own prototype property.
- Name, length, writable/nonenumerable/configurable property descriptors.
- Strict this identity, undefined new.target, primitive/object returns and
  exact thrown-value identity.
- Stable self identity after the property is replaced and then deleted.
- Quoted property-key escaping and invocation through that key.

Unit tests cover unknown effective kind, unsupported initial/template kind,
sendable, invalid self/frame, mismatched length, outer capture and Hole rejection,
zero-output failures, exact/short output and scratch budgets, and parameter
counts 200/201/400/800/1600. Shared emission does not add a whole-source buffer.

Review found an important premise missing from the initial design:
LiteralDataExtractor::DefineFunctionTemplate and DefineMethodInLiteral mutate
shared MethodLiteral kind. Initial NONE is insufficient to establish ordinary
runtime behavior. The primitive now requires explicit EffectiveMethodKind
evidence, defaulting to unknown/rejected. Production admission must establish
the actual template/method kind at materialization, including object-literal
conflicts. Synthetic tests supply this known premise; no production caller
silently assumes it. Stable self binding and name/descriptor compatibility
remain explicit caller obligations.

ASan/UBSan class_method_test, base_function_test, base_constructor_test and
created_class_test: 4/4 passed, 0.28 seconds. No corpus input, static cache or
reference pin changed. Whole-engine concurrency and 1 GB compliance remain
separate unfinished requirements.
