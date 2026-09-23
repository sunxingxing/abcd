# Empty Object Source (2026-09-23)

Pinned v7.0 `CREATEEMPTYOBJECT` calls `RuntimeCreateEmptyObject`, which
constructs an ordinary object from the realm's intrinsic Object constructor.
The new typed lift distinguishes this operandless creation from
`CREATEOBJECTWITHBUFFER`; `({})` needs no literal pool or method constants.
The same fresh-object proof admits unescaped own field definitions after
either creation kind.

The C++ fixture and Node oracle verify fresh identity, ordinary prototype,
empty own-key set, and a subsequent own data field. The complete Debug CTest
run passed 115/115 tests. On the five paired ABCs, `abc_source_probe` reported
479/2111 source-ready method bodies, up from 445/2111 before this change;
all 34 former `CREATEEMPTYOBJECT` first obstacles disappeared. A repeated
paired run produced the same counts. The last run's whole-process peak RSS
was 11,964,416 bytes (`/usr/bin/time`: 11,940 KiB). This is not a full-corpus
memory-bound result or proof of complete record decompilation.

Command:

```sh
build/decode/abc_source_probe tests/corpus/abc-index.json \
  paired /home/rabbit/sample/abc_with_code
```
