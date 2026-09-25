#include "class_parent_read.h"

#include "private_copy.h"

namespace abcd::source {
namespace {
// Pinned global read, private accumulator save, then DEFINECLASSWITHBUFFER.
constexpr std::size_t kParentPreparation = 2;
bool HasOrigin(const SourceStatement& node) {
  return node.fused_read || node.fused_spill || node.fused_reload ||
         node.previous_origin || node.class_origin || node.literal_origin;
}
}  // namespace

void FoldClassParentReads(const AstMethod& method, SourceAst& ast,
                          std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (auto i = std::size_t(block.first_instruction) + kParentPreparation; i < end; ++i) {
      auto& node = ast.statements[i];
      auto* assignment = std::get_if<SourceAssignment>(&node.operation);
      auto* created = assignment ? std::get_if<ClassExpression>(&assignment->value) : nullptr;
      if (!created || created->heritage != analysis::HeritageKind::kUnknown ||
          created->parent_read || HasOrigin(node)) continue;
      const auto source = i - kParentPreparation;
      auto& head = ast.statements[source];
      const auto* load = std::get_if<SourceAssignment>(&head.operation);
      const auto* global = load ? std::get_if<GlobalExpression>(&load->value) : nullptr;
      const auto* target = load ? std::get_if<abc::RegisterId>(&load->target) : nullptr;
      const auto* original = std::get_if<analysis::GlobalAccess>(
          &method.instructions[source].operation);
      if (!global || !target || *target != created->creation.parent ||
          load->result != AssignmentResult::kStored || load->object_field ||
          HasOrigin(head) || !original || original->operation != analysis::GlobalOp::kReadName ||
          original->name != global->name ||
          !IsRetargetedCopyAnchor(ast.statements[source + 1], head)) continue;
      const auto saved = RetargetedOutput(method, ast, source, i, *target, uses);
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i)});
      if (!saved || saved->value >= uses.size() || uses[saved->value] != 1 ||
          reads.size() != 1 || reads.front().value != *saved) continue;
      // Only a pure private save separates the original lookup and class.
      // Exact uses include phis. Do not generalize across calls/getters or
      // infer that the resulting superclass can never become a Proxy.
      created->parent_read = *global;
      head.operation = analysis::NoOperation{};
      node.fused_read = head.pc;
      node.read_distance = kParentPreparation;
      node.fused_spill = ast.statements[source + 1].pc;
      node.spill_distance = i - (source + 1);
    }
  }
}
}  // namespace abcd::source
