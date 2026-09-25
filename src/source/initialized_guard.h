#ifndef ABCD_SOURCE_INITIALIZED_GUARD_H_
#define ABCD_SOURCE_INITIALIZED_GUARD_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Consume BuildSourceAst's constructor/Defined-value proof, never infer a new
// initialization premise. Callers must verify exact SSA reads and ownership.
inline bool IsElidedInitializedGuard(const AstMethod& method, const SourceAst& ast,
                                     std::size_t position,
                                     std::optional<abc::CodeOffset> previous = {}) {
  const auto& node = ast.statements[position];
  const auto& instruction = method.instructions[position];
  const auto* check = std::get_if<analysis::SuperCheck>(&instruction.operation);
  if (!check || method.ssa.IsPhased() || ast.regions.catches ||
      !std::holds_alternative<analysis::NoOperation>(node.operation) ||
      node.fused_read || node.fused_spill || node.fused_reload ||
      node.previous_origin != previous || node.class_origin || node.literal_origin ||
      instruction.effects.normal_registers.write) return false;
  switch (check->kind) {
    case analysis::SuperCheckKind::kRequireInitialized:
    case analysis::SuperCheckKind::kNoop: return true;
    case analysis::SuperCheckKind::kRequireUninitialized: return false;
  }
  return false;
}
}  // namespace abcd::source
#endif
