# Historical Derived Class Admission

The pinned `OpenHarmony-v7.0-Release` interpreter routes both modern
`DEFINECLASSWITHBUFFER` formats through `CreateClassWithBuffer`. Its
`RuntimeSetClassInheritanceRelationship` applies the same parent constructor
and prototype checks without branching on ABC container version. For an
import/global parent snapshot already proved by the record interpreter,
`WriteCreatedClass` now admits both dynamic 12.0.6.0 and 13.0.1.0 profiles.
Unknown profiles remain refused; this change does not infer a parent from
record metadata or skip constructor/method-body proof.

Synthetic imported- and global-parent fixtures run under both profiles.
Node checks their prototype chains, forwarded arguments and constructor
lengths. The real pasteboard `EntryAbility` record (entity 4767) still
refuses at class creation pc 14, now because constructor method 5083 is not
the pure forwarding template. Its decoded body uses `COPYRESTARGS 0`,
`SUPERCALLSPREAD`, two `this` guards, and a later `STOBJBYNAME` field write.
Publishing `super(...arguments)` alone would omit `curBp` initialization.
The `EntryBackupAbility` class has a distinct remaining class-member refusal.
No record was newly counted as decompiled by this profile admission.
The pasteboard source export measured 442,613,760 bytes peak RSS, under the
1,000,000,000-byte process limit. Full Debug CTest passed 159/159, including
the historical imported/global parent Node oracle. This sample result is not
a full-corpus record-output memory certification.
