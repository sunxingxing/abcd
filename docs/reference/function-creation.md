# Function Creation Inputs

`LiftFunctionCreation` models the four current DEFINEFUNC/DEFINEMETHOD encodings
with 8/16-bit inline-cache operands. This is typed creation IR, not source
closure emission. Each result records the operation kind, caller-regional
method slot, observable function length and home-object input. Cache operands
are range-checked but do not become language-level semantics.

Pinned reference evidence:

- `interpreter/interpreter-inl.cpp` near 5080-5176 passes current lexical
  environment and module to both operations and writes the created function
  into the accumulator. DEFINEFUNC supplies the current function's home object;
  DEFINEMETHOD reads the pre-instruction accumulator as its home object.
- `stubs/runtime_stubs-inl.h`, `RuntimeDefinefunc` near 2485-2531, allocates a
  normal or sendable function depending on the referenced method. The normal
  path installs the supplied module, lexical environment and home object. The
  sendable path generates a sendable module, propagates its sendable environment
  when applicable and installs the runtime's empty shared-function environment;
  it does not perform the normal lexical-environment/home-object assignments.
- `RuntimeDefineMethod` near 2651-2661 calls the method factory, sets length,
  lexical environment and module. `ObjectFactory::NewJSFunctionForDefineMethod`
  near 5663-5692 selects async/generator/async-generator classes specially and
  otherwise uses a function class without a prototype, then sets home object.

Both runtime stubs explicitly set length from the instruction. That byte must
not be replaced with `FrameLayout::parameters`: source default/rest parameters
and observable function length require separate recovery. This IR does not
claim that the encoded length alone reconstructs the original formal syntax.

Consumers must resolve method slots using the creating method's pool, confirm
method membership, combine target attributes with creation kind and reconstruct
lexical state. A function method can be reused by multiple dynamic closure
instances; caching analysis by method must not deduplicate runtime function
objects or merge distinct captured environments. Home-object links must remain
distinct from lexical this and an ordinary enumerable object property.

The lifting operation is constant time and allocation-free. Tests decode all
four encodings, cover method slots 0/200/201/65535 and lengths 0/1/200/201/255,
and reject operand count/kind/range errors and unsupported opcodes. Historical
creation encodings, class-buffer construction, target binding, exception/effect
analysis and generated closure semantics remain pending. No corpus-wide source
or runtime differential validation is implied by these structural tests.
