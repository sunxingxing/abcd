#ifndef ABCD_SOURCE_RECORD_MODULE_CHOICE_H_
#define ABCD_SOURCE_RECORD_MODULE_CHOICE_H_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"
#include "abcd/analysis/module_load.h"

namespace abcd::source {
struct RecordModuleChoice {
  analysis::ModuleSlot slot;
  abc::RegisterId prior;
  abc::RegisterId destination;
  abc::CodeOffset store_pc;
  std::size_t after;
};

// Matches the v7 local-export `prior || (slot = {})` diamond. Only the
// matched branch edges enter its interior; the caller rejects all backedges
// and proves prior is a snapshot of this already-initialized slot.
[[nodiscard]] std::optional<RecordModuleChoice> MatchRecordModuleChoice(
    const abc::MethodBody& body, std::size_t first,
    std::uint64_t frame_size);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_MODULE_CHOICE_H_
