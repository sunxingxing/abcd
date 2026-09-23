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
