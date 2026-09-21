# Instruction Decoding Evidence

Reference: runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`.
This records inputs for the next implementation stage, not an existing decoder.

## Generated Encodings

- `isa/isa.yaml` is the authoritative dynamic ISA input. It names version
  24.0.0.0, maps API versions to historical container versions, and explicitly
  rejects 11.0.0.0/11.0.1.0. Container recognition alone does not certify opcode
  availability or semantics for every historical profile.
- `isa/isapi.rb:105-187` expands each format of an instruction into its own
  opcode identity and resolves operand width/bit offsets from the format. Reuse
  this proven generator API rather than hand-maintaining numeric opcode tables.
- Prefixed opcode identity is `(secondary_opcode << 8) | prefix_byte`, with the
  prefix byte first in the file (`Instruction::opcode_idx`). Prefixes include
  throw, wide, deprecated and callruntime. Do not swap the byte order.
- `libpandafile/templates/bytecode_instruction_enum_gen.h.erb` shows generation
  of typed Format/Opcode enums. The inline template derives instruction sizes
  and typed operand extraction. Generated project tables belong in the build
  directory, with pinned input hashes and an explicit generation boundary.
- Generation may interpret YAML identifiers/signatures; production dispatch
  must use enums and typed operand roles, never strings or printed assembly.

## References and Exceptions

- `libpandafile/file.h:328-344`: `ResolveOffsetByIndex` uses the regional
  **method index table**, also used by `ResolveMethodIndex`. Its entries are
  not exclusively method entities. Preserve operand-specific meaning when
  resolving string/literal/method operands through the shared table.
- `code_data_accessor.cpp` parses four ULEBs before instruction bytes. Try
  metadata follows exactly `code_size` bytes. Catch/try traversal is defined in
  that file and `code_data_accessor-inl.h:20-54`; bounded decoding must replace
  unchecked pointer access, and verify instruction/handler boundaries.
- Method body requests must retain the file owner, select an explicit profile,
  and reserve memory before loading/decode. Record indexing currently only
  saves offsets/counts; it does not establish an asynchronous task executor.

The next milestone must decode real method instructions and exception metadata,
then feed semantic recovery. Instruction coverage alone is not source fidelity;
the final result still requires verified JS/TS/ArkTS files per record.
