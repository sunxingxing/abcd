# Historical Async Class Members

Pinned OpenHarmony v7 `interpreter-inl.cpp` routes
`ASYNCFUNCTIONENTER`, `ASYNCFUNCTIONRESOLVE_V8` and
`ASYNCFUNCTIONREJECT_V8` through the same runtime handlers for decoded
dynamic methods. The 12.0.6.0 container and method-attribute profile decode
these opcodes and the async kind explicitly. `ResolveClassMember` now admits
12.0.6.0 async ordinary members, but only the existing exact no-await
`ProveAsyncCompletion` shape may reach source emission. Dynamic 11.0.2.0
still refuses at the member boundary. The transformation remains linear in
the method body and keeps its existing memory admission.

In the paired pasteboard ABC, the real class buffer at entry entity 5409,
PC 14 owns member entities 5437 (`onBackup`, length 0) and 5465
(`onRestore`, length 1). Both pass the Promise/exception wrapper proof and
`WriteClassMethod` using their own record's module table. The
generated methods were executed by Node with a logger stub: tests assert
signature length, call receiver and arguments, getter-before-argument order,
`JSON.stringify` side effects, and Promise rejection on a cyclic argument.
The paired TS files are not engine inputs. The entire
`EntryBackupAbility` record remains unpublished: constructor entity 5493
uses `COPYRESTARGS`/`SUPERCALLSPREAD`, and its import parent has no lifetime
JSFunction-brand invariant required for faithful native `super(...rest)`
output. Its 18-instruction constructor was inspected directly; the class
writer refuses before visiting members.

Fresh Debug build and 163/163 CTest cases passed. The real-member fixture
peaked at 9,600 KiB RSS (9,830,400 bytes). `abc_structure --source` on
pasteboard exited 0 with a 10,752 KiB process peak (11,010,048 bytes);
manifest record 5246 remains `source_status=3`, `source_error=7` at PC 14
with no source path. This is a single-file memory measurement, not a new
full-corpus memory certification.
