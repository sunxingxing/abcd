#ifndef ABCD_TOOLS_TREE_POSITION_JSON_H_
#define ABCD_TOOLS_TREE_POSITION_JSON_H_

#include <iosfwd>

#include "abcd/source/tree_printer.h"

namespace abcd::source { struct InstructionSources; class FamilyEmission; struct RecordModuleInitialization; }
namespace abcd::tools {
// Serialize the unchanged tree's origin DAG and printer events in linear space
// and time. Coordinates belong to the supplied print, including absolute family
// draft coordinates. Never expand transitive PC sets or infer origins from JS.
// The caller supplies matching immutable tree/events; this grants no receipt.
void WriteTreePositions(std::ostream& output, const source::FunctionTree& tree,
                        const source::PrintedFunction& printed,
                        const source::InstructionSources* sources = nullptr);
// Only sealed emissions supply completed member receipts. All member ranges
// remain relative to the entire family, not to each child's start.
[[nodiscard]] bool WriteFamilyMembers(std::ostream& output,
                                     const source::FamilyEmission& emission);
[[nodiscard]] bool WriteModuleInitialization(std::ostream& output,
    const source::RecordModuleInitialization& initialization);
}  // namespace abcd::tools
#endif
