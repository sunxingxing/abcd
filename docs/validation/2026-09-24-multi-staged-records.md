# Multiple Staged Named Calls

The function-wide matcher now admits disjoint staged named calls in complete
straight-line, handler-free methods. It uses bounded forward and reverse
register-access timelines instead of scanning a method suffix for each call.
Work and retained/scratch allocations are charged to the method budgets.
Overlapping intervals or unproved register effects reject the batch.

Synthetic C++ tests exercise two calls, overlap and register-effect refusals,
work/byte exhaustion, and 200/201/402-call scaling. A Node oracle checks
getter order, receiver identity, argument evaluation and result use.

On the paired Immersive ABC, complete source-ready records increased from 8
to 10: `IconInfoModel` (entity 16752) and `SectionModel` (entity 17544) now
export full record files. The paired-record Node oracle loads those files as
ES modules and checks imports, exports, class names, instance fields, map
callbacks, accessors and both sections of `initData`. It checks bytecode
behavior even where the paired source passes six arguments to a seven-parameter
constructor: the final declared field remains absent.

Verification: the full Debug build and 167/167 CTest cases passed. A fresh
`abc_structure --source` run on Immersive wrote 10/45 records and measured
11,136 KiB external peak RSS (11,403,264 bytes); its own `ru_maxrss` report
was 11,272,192 bytes. Under a 1,000,000,000-byte address-space limit, the
paired+batch `abc_source_probe` finished with 498 cached static skips and
173,792 KiB external peak RSS (177,963,008 bytes). The working corpus index
copy remained byte-identical to the tracked index. The probe measures
source-body admission, not full-corpus record output or equivalence.

This does not claim general control-flow, exception-region or arbitrary
receiver-call recovery. The arrow IIFE still introduces a JS frame and
closure, so stack-observing behavior and allocation-failure timing are not
proved.
