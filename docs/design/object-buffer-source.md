# Data Object Buffer Source

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

Tests cover typed instruction effects, primitive values, narrow and wide
operands, duplicate/method rejection, `__proto__`, fresh identity, own
descriptors, 200/201/402 properties, and exhausted object scratch allowance.
This is not object-method source recovery, historical
opcode certification or a record exporter.

## Fresh Property Definitions

Pinned v7 `DEFINEPROPERTYBYNAME` calls `RuntimeDefineField`, which creates a
writable, enumerable, configurable own data property and preserves the
accumulator. Ordinary assignment would invoke an inherited setter; calling
`Object.defineProperty` would depend on a mutable global intrinsic. The
straight-line lexical writer therefore admits only a buffered object that is
immediately stored in one register, then has the accumulator overwritten
without reading that object. Any receiver read, copy, write, unknown effect or
new object candidate ends the proof. For an admitted definition it replaces
the as-yet-unobserved object with `({...object, [key]: value})`. The buffered
object has only own data properties, so spread cannot invoke a getter; computed
keys preserve `__proto__` and index-key semantics. Unproved definitions remain
explicit source failures. The proof tracks constant state and costs O(I) for I
instructions; it never compares every property pair.

Repeated distinct definitions make the generated program copy growing objects
repeatedly (up to quadratic runtime work). This is a known output-performance
gap, not a claim of complete class-field or object-initializer recovery. A
future linear-runtime emitter must defer definitions and prove all retained
field values and object aliases before one final object construction.
