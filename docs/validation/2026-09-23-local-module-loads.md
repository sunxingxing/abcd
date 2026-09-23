# Local module load validation (2026-09-23)

`LDLOCALMODULEVAR` with an adjacent named Hole guard now uses the same-record
live `$localN` binding when the record writer certifies its slot. Unit tests
cover missing context, missing guard, out-of-range slots, generated-name
collision and mixed external/local sequences at 200/201/400/800 loads.
The Node oracle checks reads after a local binding update and after an
external import update. Full CTest: 146/146 passed.

The paired Immersive `modules.abc` record export still emits 5/45 executable
records. The remaining refusals include 15 records at an entry
`TRYLDGLOBALBYNAME`/conditional prelude and a `HomeConstants` static
initializer with 849 instructions containing nested literals, an external
module read and a constructor call. Neither path is treated as a constant-only
initializer. A direct `abc_structure --source` run exited 0 in 0.60 s with
external peak RSS 10,752 KiB. The unchanged record count is not evidence
against the tested live-binding behavior; it shows that earlier record-level
barriers still dominate this sample.
