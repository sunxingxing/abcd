#include "class_inner_scope.h"

#include <new>
#include <stdexcept>

#include "family_file.h"
#include "recovery_scratch.h"
namespace abcd::source {
std::variant<std::shared_ptr<const ClassInnerScopeProof>,
             ClassSelfInitializationFailure>
ClassInnerScopeProof::Build(const ClassSelfInitializationInput& input,
                            const ClassSelfInitialization& raw,
                            analysis::SemanticBudget budget) {
  using Error = ClassSelfInitializationError;
  ClassSelfInitializationFailure failure{Error::kCorrespondence};
  failure.creator = raw.creator;
  failure.initializer = raw.initializer;
  try {
    auto built = ClassSelfScopeProof::Build(input, raw, budget);
    if (const auto* error = std::get_if<ClassSelfInitializationFailure>(&built))
      return *error;
    auto proof = std::get<ClassSelfScopeProof>(std::move(built));
    TreeCheckBudget q(budget);
    q.Admit(proof.Cost().bytes, 1);
    q.Step(proof.Cost().work);
    const auto checked =
        VerifyClassSelfScope(input, raw, proof.Evidence(), q.Remaining());
    if (const auto* error =
            std::get_if<ClassSelfInitializationFailure>(&checked))
      return *error;
    const auto cost = std::get<ClassSelfInitializationCost>(checked);
    q.Admit(cost.bytes, 1);
    q.Step(cost.work);
    ClassInnerScopeProof result(std::move(proof));
    result.identity_ = raw.identity;
    result.creator_ = raw.creator;
    result.creator_revision_ = raw.revision;
    result.creator_control_ = raw.control_revision;
    RequireTree(!input.families.empty() && !result.Scope().owners.empty(),
                TreeStructureError::kOwnership);
    result.owner_ = result.Scope().owners.front();
    const auto& graph =
        input.families.front().family.members.front().function.Graph();
    result.site_count_ = graph.input.sites.size();
    q.Array(result.reads_, result.Scope().references.size());
    std::size_t n = 0;
    for (const auto& ref : result.Scope().references) {
      q.Step();
      if (ref.owner.family || ref.owner.member) continue;
      RequireTree(ref.read.value < graph.ops.size() &&
                      ref.guard.value < graph.ops.size(),
                  TreeStructureError::kOwnership);
      result.reads_[n++] = {
          {graph.origins[graph.ops[ref.read.value].origin.value].site.value},
          {graph.origins[graph.ops[ref.guard.value].origin.value].site.value}};
    }
    result.reads_.resize(n);
    auto sealed = std::allocate_shared<ClassInnerScopeProof>(
        RecoveryAllocator<ClassInnerScopeProof>{q}, std::move(result));
    sealed->cost_ = {q.Bytes(), q.Work()};
    return std::shared_ptr<const ClassInnerScopeProof>(std::move(sealed));
  } catch (TreeStructureError e) {
    failure.kind = e == TreeStructureError::kWorkLimit ? Error::kWorkLimit
                   : e == TreeStructureError::kResourceLimit
                       ? Error::kResourceLimit
                       : Error::kCorrespondence;
  } catch (const std::bad_alloc&) {
    failure.kind = Error::kResourceLimit;
  } catch (const std::length_error&) {
    failure.kind = Error::kResourceLimit;
  }
  return failure;
}
void ClassInnerScopeProof::Verify(const FunctionTree& tree,
                                  TreeCheckBudget& q) const {
  const auto need = [](bool ok) {
    RequireTree(ok, TreeStructureError::kBinding);
  };
  need(tree.class_scope.get() == this && tree.class_inner &&
       tree.capture_requirements.empty() && tree.identity == identity_ &&
       tree.method == owner_.method && tree.value_revision == owner_.revision &&
       tree.cfg_revision == owner_.control_revision &&
       tree.class_inner->binding == Scope().binding);
  std::size_t bindings = 0;
  for (const auto& symbol : tree.symbols) {
    q.Step();
    if (symbol.kind == BindingKind::kClassInner) {
      ++bindings;
      need(symbol.scope == tree.class_inner->scope &&
           symbol.name == tree.class_inner->name &&
           symbol.declaration == DeclarationKind::kConst);
    }
  }
  need(bindings == 1);
  struct Origin {
    std::optional<std::size_t> read;
    bool load = false, guard = false, mixed = false;
  };
  std::vector<Origin> sites, origins;
  q.Array(sites, site_count_);
  q.Array(origins, tree.origins.size());
  std::vector<std::uint32_t> seen;
  q.Array(seen, reads_.size());
  for (std::size_t r = 0; r < reads_.size(); ++r) {
    for (const auto site : {reads_[r].read, reads_[r].guard}) {
      q.Step();
      need(site.value < sites.size() &&
           (!sites[site.value].read || sites[site.value].read == r));
      sites[site.value].read = r;
    }
    sites[reads_[r].read.value].load = true;
    sites[reads_[r].guard.value].guard = true;
  }
  for (std::size_t i = 0; i < tree.origins.size(); ++i) {
    q.Step();
    const auto& origin = tree.origins[i];
    if (const auto* s = std::get_if<OriginalSite>(&origin)) {
      if (s->site.value < sites.size()) origins[i] = sites[s->site.value];
    } else if (const auto* c = std::get_if<CombinedOrigin>(&origin)) {
      need(c->first_part <= tree.origin_parts.size() &&
           c->part_count <= tree.origin_parts.size() - c->first_part);
      for (std::size_t p = c->first_part; p < c->first_part + c->part_count;
           ++p) {
        q.Step();
        const auto part = tree.origin_parts[p];
        need(part.value < i);
        const auto& x = origins[part.value];
        origins[i].mixed |= x.mixed;
        origins[i].load |= x.load;
        origins[i].guard |= x.guard;
        if (x.read) {
          if (origins[i].read && origins[i].read != x.read)
            origins[i].mixed = true;
          else
            origins[i].read = x.read;
        }
      }
    }
  }
  for (const auto& expression : tree.expressions) {
    q.Step();
    if (const auto* value = std::get_if<SymbolExpr>(&expression.expression)) {
      need(value->symbol.value < tree.symbols.size());
      if (tree.symbols[value->symbol.value].kind != BindingKind::kClassInner)
        continue;
      need(expression.origin.value < origins.size());
      const auto& origin = origins[expression.origin.value];
      // Original expression origins may retain the read OR its guard. The
      // immutable certificate identifies the pair; TreeFlow/operand checking
      // separately reconstructs both physical operations and the throw exit.
      need(origin.read && (origin.load || origin.guard) && !origin.mixed &&
           ++seen[*origin.read] == 1);
    }
    if (const auto* assignment =
            std::get_if<AssignmentExpr>(&expression.expression))
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target))
        need(target->symbol.value < tree.symbols.size() &&
             tree.symbols[target->symbol.value].kind !=
                 BindingKind::kClassInner);
    if (const auto* update = std::get_if<UpdateExpr>(&expression.expression))
      need(update->symbol.value < tree.symbols.size() &&
           tree.symbols[update->symbol.value].kind != BindingKind::kClassInner);
  }
  for (const auto count : seen) {
    q.Step();
    need(count == 1);
  }
}
void VerifyClassInnerScope(const FunctionTree& tree, TreeCheckBudget& q) {
  if (tree.class_inner) {
    RequireTree(bool(tree.class_scope), TreeStructureError::kBinding);
    tree.class_scope->Verify(tree, q);
  } else
    RequireTree(!tree.class_scope, TreeStructureError::kBinding);
}
}  // namespace abcd::source
