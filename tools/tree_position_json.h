#ifndef ABCD_TOOLS_TREE_POSITION_JSON_H_
#define ABCD_TOOLS_TREE_POSITION_JSON_H_

#include <iosfwd>

#include "abcd/source/tree_printer.h"

namespace abcd::source { struct InstructionSources; }
namespace abcd::tools {
// Serialize the unchanged tree's origin DAG and printer events in linear space
// and time. Coordinates belong to the supplied print, including absolute family
// draft coordinates. Never expand transitive PC sets or infer origins from JS.
// The caller supplies matching immutable tree/events; this grants no receipt.
void WriteTreePositions(std::ostream& output, const source::FunctionTree& tree,
                        const source::PrintedFunction& printed,
                        const source::InstructionSources* sources = nullptr);
}  // namespace abcd::tools
#endif
