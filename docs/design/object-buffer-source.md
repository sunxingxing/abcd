# Data Object Buffer Source

Pinned v7.0 `CREATEEMPTYOBJECT` constructs through the realm's intrinsic
Object constructor and has no operands or pool reference. Native `({})`
preserves its ordinary prototype, fresh identity and empty own-property set;
it does not require method constants. Empty and buffered creation are separate
typed lift results, so an absent literal is never invented.

Pinned OpenHarmony v7.0 modern `CREATEOBJECTWITHBUFFER` resolves a regional
literal-array slot, obtains an object-literal template and clones it for each
execution. `LiteralDataExtractor::ExtractObjectDatas` pairs keys and values;
`RuntimeCreateObjectHavingMethod` clones the template and rebases method
environments. The deprecated opcode has a different constant-pool contract and
is not admitted by this rule.

`LiftObjectCreation` validates the modern operand roles and widths independently
of ISA access flags. The source writer admits only string keys with null,
boolean, unsigned integer, double or string data values. It rejects methods,
accessors, unsupported value tags, malformed literals and duplicate decoded
keys. Duplicates are rejected because the runtime template builder's insertion
behavior is not proved equivalent to JavaScript's last-definition semantics.
Computed string keys in `({[key]: value})` preserve an own `__proto__` data
property and canonical array-index keys. Each evaluation creates a fresh object
with ordinary writable, enumerable and configurable data properties. No value
expression is moved across an observable operation.

`ReadLiteralArray` and all retained key/order buffers share a bounded scratch
allowance; each value string is read transiently within the residual allowance.
The local lexical writer reserves its graph, environment, closure and plan
storage before passing this allowance. Acyclic and single-block loop writers
likewise pass only the allowance left after graph and accumulator-flow storage.
Duplicate detection sorts indices, so
time is O(P log P + string bytes + emitted bytes) for P properties, with O(P +
string bytes) retained memory. Production never compares every key pair.
Preflight of the complete method prevents publication on semantic or configured
budget failure. IO failure or file mutation between passes still requires
atomic final-file publication.

Tests cover empty objects, typed instruction effects, primitive values, narrow and wide
operands, duplicate/method rejection, `__proto__`, fresh identity, own
descriptors, 200/201/402 properties, and exhausted object scratch allowance.
This is not object-method source recovery, historical
opcode certification or a record exporter.

## Fresh Property Definitions

Pinned v7 `DEFINEPROPERTYBYNAME` calls `RuntimeDefineField`, which creates a
writable, enumerable, configurable own data property and preserves the
accumulator. Ordinary assignment would invoke an inherited setter; calling
`Object.defineProperty` would depend on a mutable global intrinsic. The
straight-line lexical writer therefore admits only an empty or buffered object that is
immediately stored in one register, then has the accumulator overwritten
without reading that object. Any receiver read, copy, write, unknown effect or
new object candidate ends the proof. Each admitted definition except the last
stores its already-evaluated accumulator in a private temporary. At the final
definition, one `({...object, [key0]: value0, ...})` replaces the still
unobserved object. Both eligible objects have only own data properties, so spread
cannot invoke a getter; computed keys preserve `__proto__` and index-key
semantics. The caller's bindings may not collide with the generated temporary
prefix. Unproved definitions remain explicit source failures.

The proof scans I instructions twice, retaining O(P) field sites for P
definitions. Source size and generated runtime object copying are
O(I + P + initial properties), not quadratic in P. The field-site storage is
charged to the lexical writer's scratch allowance before object-buffer reads.
Tests cover 200/201/402 definitions, one spread per chain, separate chains,
intervening calls and throws, field-plan budget exhaustion, name collision,
key order, `__proto__`, accumulator preservation and alias rejection.
This remains a guarded subset, not complete class-field or object-initializer
recovery.

## Method entries (object-literal method shorthand)

`ExtractObjectDatas` reads a key followed by either one data entry or a
`METHOD`/`GENERATORMETHOD`/`ASYNCGENERATORMETHOD` entry plus its
`METHOD_AFFILIATE` length; only the affiliate produces the property value.
`DefineMethodInLiteral` installs the tag's kind only on a `NONE` method
literal (otherwise the recorded kind, e.g. `ASYNC_FUNCTION`, wins), and
`CreateJSFunctionInLiteral` uses the no-prototype function class for
`NORMAL_FUNCTION`. `CloneObjectLiteral` clones each method per evaluation
and sets the creating lexical environment and the fresh literal as home
object. Native shorthand `key(params) { … }` has exactly these properties:
non-constructor, no `prototype`, name from the key, home object = literal.

- `FunctionCreationSites` also indexes each method entry of a modern
  `CREATEOBJECTWITHBUFFER` as an occurrence (target, creator, original PC,
  key ordinal, tag). Its creation is `kMethod` with
  `HomeObjectInput::kLiteralObject`, the affiliate length (≤ 255) and no
  pool slot. `FindLiteral(creator, pc)` returns the occurrences of one site
  in key order in O(log S). Unreadable or malformed literals are not indexed;
  the object reader refuses them at the creation.
- `BindLiteralCreation`/`BindExceptionLiteralCreation` bind an occurrence at
  its own working instruction with the ordinary environment before it (no
  sendable branch). `CreationContextMode::kObjectMethod` requires the method's
  single literal role to match its tag and maps `METHOD` + `NONE` →
  `kMethod`, `METHOD` + `ASYNC` → `kAsyncMethod`, generator tag →
  `kGeneratorMethod`; async generator methods stay refused.
- The family reader keeps an object site current until every indexed method
  has been read as its own child; the buffer's method keys must equal the
  indexed occurrences exactly. `FunctionBinding` keys are (operation, key
  ordinal); evaluation, tree-semantics and capture checks consume them in
  that order. Family writes mark the site `kMethodObject` and require one
  child edge per method entry before the barrier is consumed.
- The tree binds each method key to the child's `FunctionExpr`
  (`DataProperty::method`). Printing requires the child's runtime name to
  equal the key and prints `[async ][*]key(params) { … }`; a later own
  definition replacing a method key is refused. A method callable outside a
  class root is printable only through this shorthand path.

Accessor placeholders (`ACCESSOR` + `DEFINEGETTERSETTERBYVALUE`) remain
refused by the data-value reader. Evidence: fixture `object_methods` (Node
differential: captures through try, receivers, key order, names, lengths,
non-constructability, async and generator kinds).
