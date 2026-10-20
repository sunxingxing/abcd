#include "class_inner_scope.h"

#include <new>
#include <stdexcept>

#include "family_file.h"
#include <algorithm>
#include <bit>
#include <tuple>
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
bool ClassInnerScopeProof::SelfStorage(IncomingCell address) const {
  const auto& requirements = Scope().requirements;
  const auto key = std::tuple{owner_.family, owner_.member,
                              address.depth.value, address.slot.value};
  const auto at = std::lower_bound(requirements.begin(), requirements.end(), key,
      [](const auto& r, const auto& k) {
        return std::tuple{r.owner.family, r.owner.member,
                         r.incoming.depth.value, r.incoming.slot.value} < k;
      });
  return at != requirements.end() && at->owner == owner_ &&
         at->incoming.depth == address.depth && at->incoming.slot.value == address.slot.value &&
         at->kind == ClassSelfRequirementKind::kClassInner;
}
bool ClassInnerScopeProof::SameClass(const ClassInnerScopeProof& other) const {
  return identity_ == other.identity_ && creator_ == other.creator_ &&
         Scope().binding == other.Scope().binding && &Scope() == &other.Scope();
}
std::variant<std::shared_ptr<const ClassInnerScopeProof>, TreeStructureError>
ClassInnerScopeProof::Select(std::shared_ptr<const ClassInnerScopeProof> root,
    std::uint32_t family, std::uint32_t member,
    const analysis::ValueProgram& graph, TreeCheckBudget& q) {
  const auto need = [](bool ok) { RequireTree(ok, TreeStructureError::kBinding); };
  try {
    need(bool(root));
    // One root seal per creation; selecting from a selected view cannot grow
    // a recursive owner chain or duplicate the whole certificate per member.
    if (const auto* parent = std::get_if<std::shared_ptr<const ClassInnerScopeProof>>(&root->scope_))
      root = *parent;
    const auto& scope = root->Scope();
    const auto key = std::pair{family, member};
    const auto at = std::lower_bound(scope.owners.begin(), scope.owners.end(), key,
        [&](const auto& owner, auto k) { q.Step(); return std::pair{owner.family, owner.member} < k; });
    need(at != scope.owners.end() && std::pair{at->family, at->member} == key &&
         at->revision == graph.revision && at->control_revision == graph.cfg_revision);
    ClassInnerScopeProof view(root);
    view.identity_ = root->identity_;
    view.creator_ = root->creator_;
    view.creator_revision_ = root->creator_revision_;
    view.creator_control_ = root->creator_control_;
    view.owner_ = *at;
    view.site_count_ = graph.input.sites.size();
    auto first = std::lower_bound(scope.references.begin(), scope.references.end(), key,
        [&](const auto& ref, auto k) { q.Step(); return std::pair{ref.owner.family, ref.owner.member} < k; });
    auto last = first;
    while (last != scope.references.end() && std::pair{last->owner.family, last->owner.member} == key) { q.Step(); ++last; }
    q.Array(view.reads_, static_cast<std::size_t>(last - first));
    std::vector<const analysis::ValueExceptionalExit*> exits;
    q.Array(exits, graph.ops.size());
    for (const auto& exit : graph.exceptional_exits) {
      q.Step();
      need(exit.op.value < exits.size() && !exits[exit.op.value]);
      exits[exit.op.value] = &exit;
    }
    std::size_t n = 0;
    for (auto ref = first; ref != last; ++ref) {
      q.Step();
      need(ref->owner == *at && ref->read.value < graph.ops.size() && ref->guard.value < graph.ops.size());
      const auto& read = graph.ops[ref->read.value];
      const auto& guard = graph.ops[ref->guard.value];
      need(std::holds_alternative<analysis::ValueLexicalLoad>(read.operation) &&
           std::holds_alternative<analysis::ValueGuard>(guard.operation) &&
           read.result == ref->value && guard.use_count == 1 &&
           guard.first_use.value < graph.uses.size());
      const auto* value = std::get_if<analysis::ValueId>(&graph.uses[guard.first_use.value].operand);
      need(value && *value == ref->value &&
           read.life == analysis::ValueLife::kLive &&
           guard.life == analysis::ValueLife::kLive &&
           read.origin.value < graph.origins.size() &&
           guard.origin.value < graph.origins.size());
      const auto* exit = exits[ref->guard.value];
      need(exit && exit->handler == ref->exception &&
           exit->origin == guard.origin && exit->before_throw == guard.before_throw &&
           guard.exceptional == analysis::ExceptionalCompletion::kMayThrow);
      view.reads_[n++] = {{graph.origins[read.origin.value].site.value},
                         {graph.origins[guard.origin.value].site.value}};
    }
    auto sealed = std::allocate_shared<ClassInnerScopeProof>(
        RecoveryAllocator<ClassInnerScopeProof>{q}, std::move(view));
    return std::shared_ptr<const ClassInnerScopeProof>(std::move(sealed));
  } catch (TreeStructureError error) { return error;
  } catch (const std::bad_alloc&) { return TreeStructureError::kResourceLimit;
  } catch (const std::length_error&) { return TreeStructureError::kResourceLimit; }
}
void ClassInnerScopeProof::Project(FunctionTree& tree,
    const analysis::ValueProgram& graph, const CellPlan& cells,
    const TreeFlow& flow, std::u16string_view name, TreeCheckBudget& q) const {
  const auto need = [](bool ok) { RequireTree(ok, TreeStructureError::kBinding); };
  need(tree.method == owner_.method && tree.identity == identity_ &&
       graph.revision == owner_.revision && graph.cfg_revision == owner_.control_revision &&
       !tree.class_inner && !tree.class_scope);
  // Resolve by ORIGINAL incoming address; CellId is used only to query this
  // unchanged plan's checked source binding, never across owners.
  std::optional<SymbolId> binding;
  for (std::uint32_t c = 0; c < cells.cells.size(); ++c) {
    q.Step();
    const auto* address = std::get_if<IncomingCell>(&cells.cells[c].storage);
    if (!address) continue;
    q.Step(std::bit_width(Scope().requirements.size()) + 1);
    if (!SelfStorage(*address)) continue;
    need(!binding && c < flow.cells.size() && flow.cells[c]);
    binding = *flow.cells[c];
    for (std::uint32_t a = 0; a < cells.cells[c].count; ++a) {
      q.Step();
      need(cells.accesses[cells.cells[c].first + a].kind == CellAccessKind::kLoad);
    }
  }
  const auto owner = std::pair{owner_.family, owner_.member};
  const auto& requirements = Scope().requirements;
  auto required = std::lower_bound(requirements.begin(), requirements.end(), owner,
      [&](const auto& r, auto key) { q.Step(); return std::pair{r.owner.family,r.owner.member}<key; });
  bool expected = false;
  while (required != requirements.end() &&
         std::pair{required->owner.family,required->owner.member} == owner) {
    q.Step(); expected |= required->kind == ClassSelfRequirementKind::kClassInner; ++required;
  }
  need(expected == binding.has_value());
  if (!binding) return;
  need(binding->value < tree.symbols.size() &&
       tree.symbols[binding->value].kind == BindingKind::kCapture);
  const auto id = NameId{static_cast<std::uint32_t>(tree.names.size())};
  q.Admit(tree.names.size() + 1, sizeof(std::u16string));
  tree.names.reserve(tree.names.size() + 1);
  q.Admit(tree.names.capacity() - tree.names.size() - 1, sizeof(std::u16string));
  q.Admit(name.size() + 1, sizeof(char16_t));
  tree.names.emplace_back(name);
  q.Admit(tree.names.back().capacity() - name.size(), sizeof(char16_t));
  q.Admit(tree.scopes.size() + 1, sizeof(TreeScope));
  tree.scopes.reserve(tree.scopes.size() + 1);
  q.Admit(tree.scopes.capacity() - tree.scopes.size() - 1, sizeof(TreeScope));
  const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
  const auto enclosed = tree.self ? tree.symbols[tree.self->value].scope : tree.parameter_scope;
  tree.scopes.push_back({ScopeKind::kClassInner, tree.scopes[enclosed.value].parent});
  tree.scopes[enclosed.value].parent = scope;
  tree.class_inner = FunctionTree::ClassScope{Scope().binding, scope, id};
  auto& symbol = tree.symbols[binding->value];
  symbol.kind = BindingKind::kClassInner;
  symbol.scope = scope;
  symbol.name = id;
  symbol.declaration = DeclarationKind::kConst;
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
