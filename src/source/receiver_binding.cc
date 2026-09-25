#include "receiver_binding.h"

#include <algorithm>
#include "entry_copy_elision.h"
#include "copy_origins.h"
#include "private_copy.h"
#include "register_commits.h"

namespace abcd::source {
namespace {
bool ErasedPrivateCopy(const analysis::SemanticInstruction& instruction,
                       const SourceStatement& node) {
  if (!std::holds_alternative<analysis::NoOperation>(node.operation) ||
      node.fused_read || node.fused_spill || node.fused_reload ||
      node.class_origin || node.literal_origin || node.constructor_arguments ||
      !IsPureCopyStep(instruction)) return false;
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
  const auto* value = copy ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
  return value && copy->completion == analysis::CompletionEffect::kNormal &&
      (std::holds_alternative<analysis::Accumulator>(*value) ||
       std::holds_alternative<abc::RegisterId>(*value));
}
}  // namespace
void RecoverReceiverBinding(const AstMethod& method, SourceAst& ast,
    std::span<const std::size_t> uses, const CopyOrigins& copies,
    const analysis::RangeDerivedConstructor& initialized) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region ||
      ast.statements.size() != method.instructions.size() || !CanElideEntryCopies(ast)) return;
  const auto found = std::lower_bound(method.instructions.begin(), method.instructions.end(),
      initialized.super_pc, [](const auto& instruction, auto pc) { return instruction.pc < pc; });
  if (found == method.instructions.end() || found->pc != initialized.super_pc) return;
  const auto producer = static_cast<std::size_t>(found - method.instructions.begin());
  auto& node = ast.statements[producer];
  auto* assignment = std::get_if<SourceAssignment>(&node.operation);
  const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
  const auto* call = assignment ? std::get_if<analysis::SuperRange>(&assignment->value) : nullptr;
  const auto* original = std::get_if<analysis::SuperRange>(&found->operation);
  if (!target || !call || !original || call->first != original->first ||
      call->count != original->count || assignment->result != AssignmentResult::kStored ||
      assignment->object_field || node.constructor_arguments || node.fused_read ||
      node.fused_spill || node.fused_reload || node.previous_origin ||
      node.class_origin || node.literal_origin) return;
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(producer)});
  if (!output) return;
  for (std::size_t v = 0; v < method.ssa.ValueCount(); ++v) {
    const analysis::SsaValueId value{static_cast<std::uint32_t>(v)};
    const auto slot = method.ssa.Slot(value);
    const auto* reg = slot ? std::get_if<abc::RegisterId>(&*slot) : nullptr;
    if (!reg || *reg != *target || copies.At(value) == *output) continue;
    // Entry values and erased private copies may exist but cannot be observed.
    // Other writes/phis need SSA-level naming for the slot's separate lifetimes.
    if (method.ssa.IsPhi(value)) return;
    const auto definition = method.ssa.Definition(value);
    if (definition && !ErasedPrivateCopy(method.instructions[definition->value],
                                         ast.statements[definition->value])) return;
  }
  for (std::size_t i = 0; i < method.instructions.size(); ++i) {
    for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(i)})) {
      const auto slot = method.ssa.Slot(read.value);
      const auto* reg = slot ? std::get_if<abc::RegisterId>(&*slot) : nullptr;
      if (!reg || *reg != *target || copies.At(read.value) == *output) continue;
      // The old VM receiver snapshot can survive in original SSA solely as
      // input to a discharged uninitialized-this check. DCE certified its
      // private transport dead; do not treat arbitrary empty AST nodes as
      // proof of a dead read or admit a fused/owned load here.
      const auto dead = method.ssa.Write({static_cast<std::uint32_t>(i)});
      if (!dead || dead->value >= uses.size() || uses[dead->value] != 0 ||
          !ErasedPrivateCopy(method.instructions[i], ast.statements[i]) ||
          i + 1 >= method.instructions.size()) return;
      const auto* guard = std::get_if<analysis::SuperCheck>(&method.instructions[i + 1].operation);
      const auto guarded = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!guard || guard->kind != analysis::SuperCheckKind::kRequireUninitialized ||
          guarded.size() != 1 || guarded.front().value != *dead ||
          !std::holds_alternative<analysis::NoOperation>(ast.statements[i + 1].operation)) return;
    }
  }
  bool stable = true;
  VisitRegisterCommits(ast, [&](const SourceStatement& statement, abc::RegisterId reg) {
    if (reg == *target && &statement != &node) stable = false;
  });
  if (!stable) return;
  // Native super initializes the very same receiver object, even when the
  // base constructor returns another object. Do not replace it by allocation
  // or erase the call. The proof excludes pre-initialization reads and reuse.
  ast.receiver_binding = *target;
  assignment->result = AssignmentResult::kUnused;
}
}  // namespace abcd::source
