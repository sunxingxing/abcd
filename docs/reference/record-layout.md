# Dynamic Record and Method Layout Evidence

Reference roots: pinned runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`
and ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9`.
This is preparation for the record index, not an implemented decoder.

## Ownership and Offsets

- `libpandafile/file.h:64-92,191-197`: dynamic class index is a sequence of u32
  entity offsets. Local classes own inline field and method sequences; foreign
  entities fall in the validated half-open foreign range.
- `class_data_accessor.cpp:20-64`: class prefix is encoded string length,
  terminated MUTF-8 descriptor, u32 superclass, ULEB32 access flags, field count,
  and method count. Class tags end with NOTHING (0), followed by fields, then
  methods (`class_data_accessor-inl.h:149-184`).
- **Length correction:** the accessor stores the raw ULEB in a misleadingly named
  length field. Actual writer `BaseClassItem::Write` delegates to StringItem
  (`file_items.cpp:257`); `StringItem::Write:231` emits
  `(utf16_length << 1) | is_ascii`. Class names and string entities therefore both
  carry this encoded prefix. Never treat it as a byte count or unshifted length.
- Field header: u16 class index, u16 type index, u32 name offset, then local
  ULEB flags and FieldTag records (`field_data_accessor.cpp:20-46`). INT_VALUE
  payload is **signed** LEB128; a ULEB value decoder is not interchangeable.
  VALUE and annotation references carry fixed-width u32 payloads.
- Method header: u16 class index, u16 proto index (0xffff means absent), u32 name
  offset, ULEB flags, then local MethodTag records (`method_data_accessor.cpp:20`).
  CODE (1) carries a u32 code entity offset. SOURCE_LANG (2) is one byte; other
  defined annotation/debug tags reference u32 entities. Unknown tags cannot be
  skipped by guessing their length. Foreign methods have no local tag stream.
- Code prefix: four ULEBs for vreg count, argument count, code size and try count,
  followed by instruction bytes (`code_data_accessor.cpp:37-49`). Metadata index
  construction must not eagerly decode instruction streams.

## Regional Indexes

`File::IndexHeader` contains ten u32 fields (40 bytes), with a half-open entity
offset range and class/method/field/proto table counts and offsets. Resolve a
method's class/proto indexes in the region containing that **method entity**;
the region method table is not the authoritative record-owned method list.

Upstream `File::GetIndexHeader` (`file.h:215`) linearly searches all regions.
Our implementation must validate/sort compact nonoverlapping region intervals
and use binary search or an appropriate cached interval, not repeat that linear
search for every entity/reference when collections exceed 200 items.
Primitive field-type encodings must remain distinct from ordinary entity IDs.

`file_item_container.cpp:928-963` writes `(0xffffffff, 0xffffffff)` for an
index table whose `NeedsEmit()` is false (field/proto tables). This is a paired
absence sentinel, not a huge table count or an arbitrary out-of-range table.
Both words must agree; attempts to resolve entries through absent tables fail.
`file_item_container.h:291-297` restricts this omission to field/proto tables
for bytecode major versions >= 12; version 9/11 profiles retain real tables.
The first full-corpus indexing run found this in 399 of 402 dynamic files,
including `/home/rabbit/sample/abc/002974edf05b0ed8068861109aba3d33.abc`.
Its first region has class table `(1110, 263812)`, method table
`(65531, 268252)`, and both field/proto pairs absent. Rejecting these pairs as
ordinary bounds failures would incorrectly reject supported dynamic files.

## Runtime Records

ets_runtime `ecmascript/jspandafile/js_pandafile.cpp:250-310` builds JSRecordInfo
selectively from module fields such as isCommonjs and jsonFileContent. That map
does not replace the complete local-class inventory. `GetRecordName:475-494`
resolves the method class index and reads its descriptor.

`js_pandafile.h:461-469` strips descriptor framing to obtain record names. A
bounded decoder should first validate framing and preserve raw identity.
`panda_file_translator.cpp:45-67` chooses entry methods by runtime naming
conventions; this is not an ownership rule and must not hide other methods.

## Verification Obligations

- Use bounded readers for strings/tags/LEB values, not upstream unbounded strlen
  or direct pointer casts. Preserve UTF-16 semantics for later JS escaping.
- Correlate methods with class ownership and regional indexes independently;
  verify all references and metadata extents before trusting them.
- Sample profiles are 9.0.0.0, 11.0.2.0, 12.0.2.0, 12.0.6.0 and 13.0.1.0.
  Current-reader code alone does not certify all historical tag/opcode semantics.
- Post-12.0.6 dynamic headers reserve the literal index words; do not expand
  0xffffffff as a count. Container profiles already preserve those raw words.
- Preserve file lifetime for lazy bodies; bound aggregate metadata memory and
  count/read work. Full source, async method scheduling and behavioral tests
  remain required after this index exists.
