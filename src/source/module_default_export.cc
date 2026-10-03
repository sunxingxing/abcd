#include "module_default_export.h"

#include <memory_resource>
#include <new>
#include <set>
#include <stdexcept>

#include "class_tree.h"

namespace abcd::source {
namespace {
using Error = TreePrintError;
// Charge actual container allocation requests, including discarded capacities.
// This is conservative scratch admission, not a claim of process RSS.
class ExportScratch final : public std::pmr::memory_resource {
 public:
  explicit ExportScratch(std::size_t bytes) : remaining_(bytes), initial_(bytes) {}
  std::size_t Allocated() const { return initial_ - remaining_; }
 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > remaining_) throw Error::kResourceLimit;
    remaining_ -= bytes;
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
  std::size_t remaining_, initial_;
};
}
std::variant<DefaultExportCheck, TreePrintError> CheckModuleDefaultExport(
    const FunctionTree& root, const ConstructedFamily* family,
    ModuleDefaultExport candidate, analysis::SemanticBudget budget) {
  try {
    ExportScratch scratch(budget.bytes);
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) { if (count > work) throw Error::kWorkLimit; work -= count; };
    const auto result = [&](bool eligible) { return DefaultExportCheck{eligible, scratch.Allocated(), budget.work - work}; };
    step();
    if (family && (family->root.value >= family->members.size() ||
        &family->members[family->root.value].tree != &root)) return result(false);
    if (candidate.declaration.value >= root.statements.size() || root.body.value >= root.statements.size())
      return result(false);
    const auto* declaration = std::get_if<DeclarationStmt>(&root.statements[candidate.declaration.value].statement);
    const auto* body = std::get_if<BlockStmt>(&root.statements[root.body.value].statement);
    if (!declaration || !declaration->initializer || !body ||
        declaration->symbol.value >= root.symbols.size() ||
        declaration->initializer->value >= root.expressions.size() ||
        body->first_child > root.children.size() || body->child_count > root.children.size() - body->first_child)
      return result(false);
    const auto symbol = declaration->symbol;
    const auto& binding = root.symbols[symbol.value];
    if (binding.kind != BindingKind::kLocal || binding.evidence != NameEvidence::kSynthesized ||
        binding.scope != root.body_scope ||
        (binding.declaration != DeclarationKind::kLet && binding.declaration != DeclarationKind::kConst)) return result(false);
    bool direct = false;
    for (std::uint32_t i = 0; i < body->child_count; ++i) {
      step(); direct |= root.children[body->first_child + i] == candidate.declaration;
    }
    if (!direct) return result(false);
    const auto& initializer = root.expressions[declaration->initializer->value].expression;
    if (std::holds_alternative<FunctionExpr>(initializer) || std::holds_alternative<ClassExpr>(initializer))
      return result(false);
    std::size_t local_aliases = 0;
    for (const auto& alias : root.modules) {
      step();
      // A source symbol cannot also stand for a different runtime slot.
      if (alias.symbol == symbol && alias.target != candidate.binding) return result(false);
      if (alias.target != candidate.binding) continue;
      if (alias.symbol != symbol || !alias.local || alias.access != ModuleAccess::kMutable) return result(false);
      ++local_aliases;
    }
    if (local_aliases != 1) return result(false);
    std::pmr::vector<const FunctionTree*> pending(&scratch);
    const auto less_tree = [&](const FunctionTree* a, const FunctionTree* b) {
      step(); return std::less<const FunctionTree*>{}(a, b);
    };
    const auto less_class = [&](const ClassTree* a, const ClassTree* b) {
      step(); return std::less<const ClassTree*>{}(a, b);
    };
    std::pmr::set<const FunctionTree*, decltype(less_tree)> seen_trees(less_tree, &scratch);
    std::pmr::set<const ClassTree*, decltype(less_class)> seen_classes(less_class, &scratch);
    const auto add_tree = [&](const FunctionTree& tree) {
      step(); if (seen_trees.insert(&tree).second) pending.push_back(&tree);
    };
    const auto add_family = [&](const ConstructedFamily& nested) {
      for (const auto& member : nested.members) add_tree(member.tree);
    };
    if (family) add_family(*family); else add_tree(root);
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto& tree = *pending[cursor];
      step(); if (!tree.recovery_issues.empty()) return result(false);
      if (&tree != &root) for (const auto& alias : tree.modules) {
        step(); if (alias.target == candidate.binding) return result(false);
      }
      for (const auto& capture : tree.captures) {
        step(); if (capture.target == CapturedSymbol{root.owner, symbol}) return result(false);
      }
      if (&tree == &root) {
        for (const auto captured : tree.closure_captures) { step(); if (captured == symbol) return result(false); }
        std::size_t declarations = 0;
        for (const auto& statement : tree.statements) {
          step(); const auto* node = std::get_if<DeclarationStmt>(&statement.statement);
          if (node && node->symbol == symbol) ++declarations;
        }
        if (declarations != 1) return result(false);
      }
      for (const auto& expression : tree.expressions) {
        step();
        if (&tree == &root) {
          if (const auto* read = std::get_if<SymbolExpr>(&expression.expression); read && read->symbol == symbol) return result(false);
          if (const auto* update = std::get_if<UpdateExpr>(&expression.expression); update && update->symbol == symbol) return result(false);
          if (const auto* write = std::get_if<AssignmentExpr>(&expression.expression))
            if (const auto* target = std::get_if<SymbolTarget>(&write->target); target && target->symbol == symbol) return result(false);
        }
        if (const auto* node = std::get_if<ClassExpr>(&expression.expression)) {
          if (!node->definition) return result(false);
          if (!seen_classes.insert(node->definition.get()).second) continue;
          const auto& definition = *node->definition;
          step(definition.members.size() + definition.omissions.size() + definition.definitions.size());
          if (!definition.BodiesPrepared()) return result(false);
          add_family(definition.constructor->Family());
          for (const auto& member : definition.members) { step(); add_family(member->Family()); }
          for (const auto& site : definition.definitions) for (const auto& member : site.bodies) {
            step(); if (member) add_family(member->Family());
          }
          if (definition.initializer) add_tree(definition.initializer->tree);
        }
      }
    }
    return result(true);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
