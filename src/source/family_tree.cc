#include "family_tree.h"
#include "family_cells.h"
#include "derived_projection.h"
#include "abcd/analysis/dominators.h"

#include <limits>
#include <bit>
#include <new>
#include <stdexcept>
#include "phased_capture_evaluation.h"

namespace abcd::source {
namespace {
class FamilyTrees {
 public:
  FamilyTrees(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, analysis::SemanticBudget budget,
      const RecordModuleContext* modules, ModuleOwnerId owner, const abc::MethodLookup* lookup,
      RecoveryMode recovery)
      : file_(file), header_(header), index_(index), initial_(budget), left_(budget),
        modules_(modules), module_owner_(owner), lookup_(lookup), recovery_(recovery) {}
  ConstructedFamily Build(FileFamily& family, const FunctionContext* root, std::optional<FamilyMember> entry = {},
      const ExternalFamilyCapture* external = nullptr) {
    Admit(1, sizeof(FamilyTrees));
    Require(!family.members.empty() && (root != nullptr) != entry.has_value(), FamilyTreeError::kContext);
    Require(!family.root_capture || (external && root && root->ClassContext() && lookup_), FamilyTreeError::kContext);
    Require(family.members.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
    Require(family.members.size() - 1 <= std::numeric_limits<std::uint32_t>::max() - family.first_owner.value,
        FamilyTreeError::kResourceLimit);
    // Only a supplied record root has already composed its ClassTree bodies.
    // Their original barriers are checked against those bodies by FamilyWrites;
    // ordinary child families cannot silently inherit this permission.
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      Step(1);
      Require(family.members[i].classes.empty() || (entry && i == FileFamily::kRoot.value), FamilyTreeError::kContext);
    }
    ProjectDefaults(family, root);
    Rebind(family);
    if (family.root_capture) {
      auto cells = Take<CaptureCells, CaptureCellError>(BindClassRootCaptureCells(file_, header_, index_, *lookup_,
          external->creator, family.members.front().function.Capture(), *root, left_));
      Account(cells);
      Step(Take<std::uint64_t, CaptureCellError>(VerifyClassRootCaptureCells(file_, header_, index_, *lookup_,
          external->creator, family.members.front().function.Capture(), *root, cells, left_)));
      family.root_capture = std::move(cells);
    }
    Reserve(syntaxes_, family.members.size()); syntaxes_.resize(family.members.size());
    Reserve(normal_aliases_, family.members.size()); normal_aliases_.resize(family.members.size());
    for (std::size_t i = entry ? 1 : 0; i < family.members.size(); ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      auto& member = family.members[i];
      Require(i ? member.context.has_value() && member.incoming.has_value() :
          root && !member.incoming && !member.context, FamilyTreeError::kContext);
      const auto& context = i ? *member.context : *root;
      const auto& function = member.function;
      const auto* phase = function.Phase();
      const auto& graph = function.Graph();
      auto syntax = Take<ResolvedSyntax, SyntaxResolutionFailure>(ResolveFunctionSyntax(
          file_, header_, index_, context, phase ? phase->OriginalBody() : function.Body(), graph, family.Owner(member_), left_, function.Defaults() ? function.Defaults()->Strings() : std::span<const SignatureString>{}, function.Cells()));
      Account(syntax);
      auto& normal_aliases = normal_aliases_[i];
      const ModuleAliases* aliases = phase ? phase->Phases().Modules() : nullptr;
      if (aliases || (!phase && (!function.Normal().values.modules.pairs.empty() || std::any_of(graph.ops.begin(), graph.ops.end(),
          [&](const auto& op) { Step(1); return op.life == analysis::ValueLife::kLive &&
              std::holds_alternative<analysis::ValueModuleWrite>(op.operation); })))) {
        Require(modules_ != nullptr, FamilyTreeError::kModules);
        Require(syntax.syntax.names.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
        const auto first_name = static_cast<std::uint32_t>(syntax.syntax.names.size());
        if (!phase) {
          normal_aliases = Take<ModuleAliases, ModuleAliasFailure>(BindModuleAliases(file_, header_, index_,
              function.Normal().input, function.Normal().values, *modules_, module_owner_, {first_name}, left_, ModuleAccess::kMutable));
          Account(*normal_aliases); aliases = &*normal_aliases;
        }
        Require(aliases && aliases->record == modules_->record() && aliases->owner == module_owner_ &&
            aliases->first_name.value <= first_name, FamilyTreeError::kModules);
        Require(aliases->names.size() <= std::numeric_limits<std::uint32_t>::max() - syntax.syntax.names.size(),
            FamilyTreeError::kResourceLimit);
        Reserve(syntax.syntax.names, syntax.syntax.names.size() + aliases->names.size());
        // Keep aliases unchanged for independent actual-tree verification.
        for (const auto& name : aliases->names) {
          Step(name.size()); Admit(name.size() + 1, sizeof(char16_t));
          syntax.syntax.names.push_back(name);
          Admit(syntax.syntax.names.back().capacity() - name.size(), sizeof(char16_t));
        }
        Reserve(syntax.syntax.modules, aliases->aliases.size());
        for (auto alias : aliases->aliases) {
          Step(1); alias.name.value += first_name - aliases->first_name.value;
          syntax.syntax.modules.push_back(alias);
        }
      }
      if (aliases) for (const auto& write : aliases->writes) {
        Step(1); Require(write.operation.value < syntax.operations.size() && !write.binding.initialize,
            FamilyTreeError::kModules);
        syntax.operations[write.operation.value] = write.binding;
      }
      syntaxes_[i] = std::move(syntax);
    }
    ReferenceNames(family, entry.has_value());
    Reserve(receiver_cells_, family.members.size()); receiver_cells_.resize(family.members.size());
    Reserve(receiver_scopes_, family.members.size()); receiver_scopes_.resize(family.members.size());
    std::vector<FamilyMember> trees;
    bool incomplete = false;
    Reserve(trees, family.members.size());
    if (entry) {
      const auto& function = family.members.front().function;
      Require(!function.Phase() && !family.members.front().incoming && !entry->incoming &&
          entry->tree.owner == family.Owner(FileFamily::kRoot) &&
          entry->tree.identity == function.Identity() && entry->tree.method == function.Method() &&
          entry->record == function.Record(), FamilyTreeError::kContext);
      trees.push_back(std::move(*entry));
    }
    for (std::size_t i = entry ? 1 : 0; i < family.members.size(); ++i) {
      member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset();
      defaults_proved_ = false;
      if (family.members[i].function.Defaults() && family.members[i].function.Defaults()->Storage() == DefaultStorage::kCell) {
        default_candidates_ = family.members[i].function.Defaults()->Signature().Defaults().size();
        defaults_proved_ = true;
      }
      Step(1);
      auto& member = family.members[i];
      if (i != 0) {
        Require(member.context.has_value() && member.incoming.has_value(), FamilyTreeError::kContext);
      } else Require(!member.incoming && !member.context, FamilyTreeError::kContext);
      const auto& context = i ? *member.context : *root;
      const auto& function = member.function;
      const auto* phase = function.Phase();
      if (!phase) {
        const auto& input = function.Normal().input;
        Require(!input.HasOwner() || input.MatchesOwner(context), FamilyTreeError::kContext);
        Require(input.Proof().receiver == analysis::FrameReceiver::kUnknown || input.MatchesOwner(context), FamilyTreeError::kContext);
      } else Require(lookup_ && phase->MatchesOwner(context), FamilyTreeError::kContext);
      const auto& graph = function.Graph();
      Require(context.Identity() == function.Identity() && context.Target() == function.Method(), FamilyTreeError::kContext);
      auto syntax = std::move(syntaxes_[i]);
      const ModuleAliases* aliases = phase ? phase->Phases().Modules() :
          (normal_aliases_[i] ? &*normal_aliases_[i] : nullptr);
      std::optional<DerivedProjection> derived;
      if (context.Callable() == CallableKind::kDerivedConstructor) {
        Require(!phase, FamilyTreeError::kContext);
        // Reuse the existing same-file transitive preservation proof. A child
        // write or unsupported descendant never becomes an immutable cell.
        std::optional<DerivedCaptureInput> captures;
        if (lookup_) captures.emplace(DerivedCaptureInput{file_, header_, index_, *lookup_});
        auto projection = ProjectDerivedConstructor(function.Normal(), context, left_, captures ? &*captures : nullptr);
        if (const auto* error = std::get_if<DerivedProjectionError>(&projection))
          throw FamilyTreeFailure{member_, *error};
        derived.emplace(std::get<DerivedProjection>(std::move(projection)));
        Admit(sizeof(DerivedProjection) + derived->guards.capacity() * sizeof(analysis::OpId) +
            derived->receiver_cells.capacity() * sizeof(CellId) +
            derived->receiver_initializations.capacity() * sizeof(ReceiverInitialization), 1);
      }
      auto receiver_guards = ReceiverGuards(family, i, derived ? &*derived : nullptr);
      const auto guards = derived ? std::span<const analysis::OpId>(derived->guards) :
          std::span<const analysis::OpId>(receiver_guards);
      std::optional<ControlPlan> normal_control;
      if (!phase) { normal_control.emplace(Take<ControlPlan, ControlError>(BuildControlPlan(graph, left_))); Account(*normal_control); }
      const auto& control = phase ? phase->Phases().Control() : *normal_control;
      const auto module_evaluations = aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{};
      const auto entries = member.function.Cells() ? std::span<const IncomingCell>(member.function.Cells()->entry_demands) : std::span<const IncomingCell>{};
      const auto demands = member.function.Cells() ? std::span<const CellDemand>(member.function.Cells()->demands) : std::span<const CellDemand>{};
      // Preserve the phased file input's conditional write domain. The complete
      // family below still discharges every original store and capture barrier.
      auto evaluation = Take<EvaluationPlan, EvaluationError>(phase ?
          BuildPhasedEvaluationPlan(graph, left_, i == 0 && !family.root_capture ? IncomingCells::kReject : IncomingCells::kConditional,
              member.children, module_evaluations, demands, {}, phase->Locals().writes) :
          BuildEvaluationPlan(graph, left_, i == 0 && !family.root_capture ? IncomingCells::kReject : IncomingCells::kConditional,
              member.children, module_evaluations, demands, {}, analysis::ClosureWrites::kLanguage, {},
              guards, entries));
      Account(evaluation);
      std::optional<IncomingCaptureTree> incoming;
      if (member.incoming) {
        const auto parent = member.incoming->parent.value;
        Require(parent < trees.size(), FamilyTreeError::kParent);
        incoming.emplace(IncomingCaptureTree{member.incoming->cells, {trees[parent].tree, trees[parent].flow}});
      } else if (i == 0 && family.root_capture) {
        incoming.emplace(IncomingCaptureTree{*family.root_capture, external->source.tree,
            ClassCaptureAnchor{external->source.creation, *root}});
      }
      auto constants = syntax.View();
      const NameReservations reserved{references_, family.Owner(member_), subtree_end_[i] - i};
      constants.name_reservations = &reserved;
      constants.receiver_guards = guards;
      if (derived) constants.derived_constructor = &function.Normal();
      if (derived && derived->implicit) constants.implicit_constructor = &function.Normal();
      auto constructed = [&]() -> ConstructedFunction {
        if (phase) {
          auto draft = Take<ExceptionFunctionDraft, FunctionTreeError>(BuildExceptionFunctionDraft(graph,
              phase->Phases().TreeInput(phase->Body()), evaluation, std::move(syntax.syntax), constants, left_,
              incoming ? &*incoming : nullptr));
          return {std::move(draft.tree), draft.allocated_bytes, draft.work, std::move(draft.flow)};
        }
        return Take<ConstructedFunction, FunctionTreeError>(BuildFunctionTree(graph, control,
            evaluation, std::move(syntax.syntax), constants, left_, incoming ? &*incoming : nullptr,
            member.children.empty() && !family.members[i].function.Defaults() ? recovery_ : RecoveryMode::kCompleteOnly));
      }();
      Account(constructed);
      // Naming evidence is creation-specific and must survive arena growth and
      // capture/module alias insertion. The runtime name stays creation-proven
      // even when a private self binding has a distinct synthetic spelling.
      if (context.CreationSite() && !IsMethodCallable(context.Callable())) {
        const auto name = constructed.tree.inferred_name;
        Require(name && name->value < constructed.tree.names.size(), FamilyTreeError::kContext);
        Step(context.Name().size());
        Require(constructed.tree.names[name->value] == context.Name(),
            FamilyTreeError::kContext);
      } else Require(!constructed.tree.inferred_name, FamilyTreeError::kContext);
      if (!constructed.tree.recovery_issues.empty()) {
        // The tree carries a readable omission, not whole-input flow/ABI/source
        // evidence. Keep the original file identity and do not forge those products.
        Require(member.children.empty() && !family.members[i].function.Defaults() &&
            (!member.incoming || member.incoming->cells.targets.empty()), FamilyTreeError::kContext);
        incomplete = true;
        std::optional<CaptureCells> cells;
        if (member.incoming) cells.emplace(std::move(member.incoming->cells));
        trees.push_back({std::move(constructed.tree), {}, std::move(cells), {}, {}, function.Record()});
        continue;
      }
      if (family.members[i].function.Defaults()) {
        const DefaultSignatureContext capture{control,evaluation,&constants};
        auto signature = Take<DefaultSignatureProof, DefaultSignatureError>(
            BuildDefaultSignature(*family.members[i].function.Defaults(), constructed.tree, left_, &capture));
        Account(signature);
        auto semantics = Take<TreeSemantics, TreeSemanticError>(VerifyTreeSemantics(
            graph, control, evaluation, constructed.tree, constants, left_));
        Account(semantics);
        constructed.flow = std::move(semantics.flow);
      }
      // No tree mutation follows the final semantic check. Reuse its actual
      // correspondence; a default-signature rewrite above replaces the proof.
      Require(constructed.flow.has_value(), FamilyTreeError::kContext);
      auto flow = std::move(*constructed.flow);
      if (aliases) Step(Take<std::uint64_t, ModuleAliasTreeError>(
          VerifyModuleAliasTree(*aliases, evaluation, constructed.tree, left_)));
      auto abi = Take<ConstructedAbi, FunctionAbiError>(phase ? BuildPhasedFunctionAbi(graph, evaluation, constructed.tree,
          context.Frame(), context.Length(), left_) : BuildFunctionAbi(graph, constructed.tree, context.Frame(), context.Length(), left_));
      Account(abi);
      auto sources = Take<InstructionSources, InstructionSourceError>(phase ?
          BuildInstructionSources(phase->Phases(), phase->Body(), constructed.tree, left_) :
          BuildInstructionSources(function.Normal().input, graph, constructed.tree, left_,
              function.Normal().cleanup ? &*function.Normal().cleanup : nullptr));
      Account(sources);
      std::optional<CaptureCells> cells;
      if (member.incoming) cells.emplace(std::move(member.incoming->cells));
      trees.push_back({std::move(constructed.tree), std::move(flow), std::move(cells),
                       std::move(abi.abi), std::move(sources), function.Record()});
    }
    auto result = Take<ConstructedFamily, FamilyError>(BuildFunctionFamily(std::move(trees), family.Owner(FileFamily::kRoot), left_));
    Account(result);
    result.root_capture = std::move(family.root_capture);
    family.root_capture.reset();
    const auto enclosure = VerifyFamilyEnclosure(result, left_);
    if (const auto* failure = std::get_if<FamilyEnclosureFailure>(&enclosure)) {
      member_ = failure->member; Fail(*failure);
    }
    Account(std::get<FamilyEnclosure>(enclosure));
    if (incomplete) {
      result.allocated_bytes = initial_.bytes - left_.bytes;
      result.work = initial_.work - left_.work;
      return result;
    }
    std::optional<ClassRootWriteInput> root_writes;
    if (result.root_capture) root_writes.emplace(ClassRootWriteInput{
        file_, header_, *lookup_, external->creator, *root, external->source});
    const auto writes = VerifyFamilyWrites(index_, family, result, left_, lookup_, root_writes ? &*root_writes : nullptr);
    if (const auto* failure = std::get_if<FamilyWriteFailure>(&writes)) {
      member_ = failure->member; Fail(*failure);
    }
    const auto& checked_writes = std::get<FamilyWrites>(writes);
    Account(checked_writes); result.writes = checked_writes.facts;
    result.allocated_bytes = initial_.bytes - left_.bytes;
    result.work = initial_.work - left_.work;
    return result;
  }
 private:
  void ReferenceNames(const FileFamily& family, bool entry) {
    Reserve(subtree_end_, family.members.size()); subtree_end_.resize(family.members.size());
    for (std::size_t i = 0; i < family.members.size(); ++i) { Step(1); subtree_end_[i] = i + 1; }
    // FileFamily owns preorder occurrences and explicit incoming parent edges.
    // Reverse subtree maxima use those identities, never PCs or CFG edge order.
    for (std::size_t i = family.members.size(); i-- > 1;) {
      Step(1); Require(family.members[i].incoming.has_value(), FamilyTreeError::kParent);
      const auto parent = family.members[i].incoming->parent.value;
      Require(parent < i, FamilyTreeError::kParent);
      subtree_end_[parent] = std::max(subtree_end_[parent], subtree_end_[i]);
    }
    std::size_t count = 0;
    for (std::size_t i = entry ? 1 : 0; i < family.members.size(); ++i) {
      for (const auto& op : family.members[i].function.Graph().ops) {
        Step(1); if (op.life == analysis::ValueLife::kLive &&
            std::holds_alternative<analysis::ValueGlobalRead>(op.operation)) ++count;
      }
      Step(syntaxes_[i].syntax.modules.size());
      Require(syntaxes_[i].syntax.modules.size() <= std::numeric_limits<std::size_t>::max() - count,
          FamilyTreeError::kResourceLimit); count += syntaxes_[i].syntax.modules.size();
    }
    Reserve(references_, count);
    for (std::size_t i = entry ? 1 : 0; i < family.members.size(); ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& syntax = syntaxes_[i];
      const auto& graph = family.members[i].function.Graph();
      const auto append = [&](NameId id) {
        Require(id.value < syntax.syntax.names.size(), FamilyTreeError::kContext);
        const auto& name = syntax.syntax.names[id.value]; Step(name.size());
        Admit(name.size() + 1, sizeof(char16_t));
        references_.push_back({name, family.Owner(member_)});
        Admit(references_.back().name.capacity() - name.size(), sizeof(char16_t));
      };
      for (std::size_t op = 0; op < graph.ops.size(); ++op) {
        Step(1); if (graph.ops[op].life != analysis::ValueLife::kLive ||
            !std::holds_alternative<analysis::ValueGlobalRead>(graph.ops[op].operation)) continue;
        Require(op < syntax.operations.size() && std::holds_alternative<NameId>(syntax.operations[op]),
            FamilyTreeError::kContext); append(std::get<NameId>(syntax.operations[op]));
      }
      for (const auto& alias : syntax.syntax.modules) { Step(1); append(alias.name); }
    }
    std::sort(references_.begin(), references_.end(), [&](const auto& a, const auto& b) {
      Step(std::min(a.name.size(), b.name.size()) + 1);
      return a.name < b.name || (a.name == b.name && a.owner < b.owner);
    });
    const auto end = std::unique(references_.begin(), references_.end(), [&](const auto& a, const auto& b) {
      Step(std::min(a.name.size(), b.name.size()) + 1); return a.owner == b.owner && a.name == b.name;
    });
    references_.erase(end, references_.end());
  }
  std::vector<ResolvedSyntax> syntaxes_;
  std::vector<std::optional<ModuleAliases>> normal_aliases_;
  std::vector<NameReference> references_;
  std::vector<std::size_t> subtree_end_;
  struct ReceiverScope {
    ReceiverInitialization initialized;
    analysis::BlockId block;
    std::uint32_t begin, end;
  };
  struct ReceiverScopes {
    std::vector<std::uint32_t> entries;
    std::vector<ReceiverScope> cells;
  };
  std::vector<analysis::OpId> ReceiverGuards(const FileFamily& family, std::size_t owner,
      const DerivedProjection* derived) {
    const auto& member = family.members[owner];
    const auto* cells = member.function.Cells();
    if (!cells || member.function.Phase()) return {};
    auto& receivers = receiver_cells_[owner];
    Reserve(receivers, cells->cells.size()); receivers.resize(cells->cells.size());
    if (derived) for (const auto cell : derived->receiver_cells) {
      Step(1); Require(cell.value < receivers.size(), FamilyTreeError::kContext); receivers[cell.value] = true;
    }
    if (derived && !derived->receiver_initializations.empty()) {
      const auto& input = member.function.Normal().input.Input();
      auto built = analysis::BuildNormalDominators(input.flow, {}, {left_.bytes, left_.work});
      if (const auto* error = std::get_if<analysis::DominatorError>(&built))
        Fail(*error == analysis::DominatorError::kResourceLimit ? FamilyTreeError::kResourceLimit :
            *error == analysis::DominatorError::kWorkLimit ? FamilyTreeError::kWorkLimit : FamilyTreeError::kContext);
      auto dom = std::get<analysis::DominatorTree>(std::move(built));
      // Dominator scratch is local to this parent, not retained per creation.
      Admit(dom.work.allocated_bytes, 1); Step(dom.work.steps);
      auto& scopes = receiver_scopes_[owner];
      Reserve(scopes.entries, dom.nodes.size());
      for (const auto& node : dom.nodes) { Step(1); scopes.entries.push_back(node.begin); }
      Reserve(scopes.cells, derived->receiver_initializations.size());
      for (const auto initialized : derived->receiver_initializations) {
        Step(1 + std::bit_width(input.flow.blocks.size()));
        auto block = std::upper_bound(input.flow.blocks.begin(), input.flow.blocks.end(), initialized.store.value,
            [](auto site, const auto& block) { return site < block.first_instruction; });
        Require(block != input.flow.blocks.begin(), FamilyTreeError::kContext); --block;
        const auto& node = dom.nodes[block - input.flow.blocks.begin()];
        Require(node.begin != 0, FamilyTreeError::kContext);
        scopes.cells.push_back({initialized, {static_cast<std::uint32_t>(block - input.flow.blocks.begin())}, node.begin, node.end});
      }
    }
    // Rebind freshly checks original creation initialization and current cell
    // identity. The derived protocol proves stores are super, either throughout
    // the cell's initialized lifetime or after a store dominating this creation,
    // and all descendants preserve incoming cells. Thus
    // these edges convey a receiver value, not generic Language/constness.
    if (member.incoming) {
      const auto& edge = *member.incoming;
      Require(edge.parent.value < owner, FamilyTreeError::kParent);
      const auto& parent = receiver_cells_[edge.parent.value];
      const auto& scopes = receiver_scopes_[edge.parent.value];
      std::optional<std::uint32_t> entry;
      std::optional<analysis::BlockId> creation_block;
      if (!scopes.cells.empty()) {
        const auto& blocks = family.members[edge.parent.value].function.Normal().input.Input().flow.blocks;
        Step(1 + std::bit_width(blocks.size()));
        auto block = std::upper_bound(blocks.begin(), blocks.end(), edge.site.value,
            [](auto site, const auto& block) { return site < block.first_instruction; });
        Require(block != blocks.begin(), FamilyTreeError::kContext); --block;
        Require(static_cast<std::size_t>(block - blocks.begin()) < scopes.entries.size(), FamilyTreeError::kContext);
        entry = scopes.entries[block - blocks.begin()];
        creation_block = analysis::BlockId{static_cast<std::uint32_t>(block - blocks.begin())};
      }
      for (const auto& target : edge.cells.targets) {
        Step(1); Require(target.child.value < receivers.size(), FamilyTreeError::kContext);
        const auto* source = std::get_if<ParentCell>(&target.target);
        if (source && source->cell.value < parent.size()) receivers[target.child.value] = parent[source->cell.value];
        if (source && entry && !receivers[target.child.value]) {
          Step(1 + std::bit_width(scopes.cells.size()));
          const auto scope = std::lower_bound(scopes.cells.begin(), scopes.cells.end(), source->cell,
              [](const auto& scope, auto cell) { return scope.initialized.cell < cell; });
          if (scope != scopes.cells.end() && scope->initialized.cell == source->cell &&
              (scope->block != *creation_block || scope->initialized.store < edge.site) &&
              scope->begin <= *entry && *entry < scope->end)
            receivers[target.child.value] = true;
        }
      }
    }
    if (derived) return {};
    const auto& graph = member.function.Graph();
    std::size_t count = 0;
    for (const auto& op : graph.ops) { Step(1); count += std::holds_alternative<analysis::ValueSuperCheck>(op.operation); }
    std::vector<analysis::OpId> guards; Reserve(guards, count);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      Step(1); const auto& op = graph.ops[i];
      const auto* check = std::get_if<analysis::ValueSuperCheck>(&op.operation);
      if (!check || check->kind != analysis::SuperCheckKind::kRequireInitialized ||
          op.life != analysis::ValueLife::kLive || op.use_count != 1 || op.first_use.value >= graph.uses.size()) continue;
      const auto* value = std::get_if<analysis::ValueId>(&graph.uses[op.first_use.value].operand);
      if (!value || value->value >= graph.values.size()) continue;
      const auto& definition = graph.values[value->value];
      const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
      if (!result || !definition.domain.IsLanguage() || result->op.value >= cells->operations.size()) continue;
      const auto cell = cells->operations[result->op.value];
      if (!cell || cell->value >= receivers.size() || !receivers[cell->value]) continue;
      const auto& load = graph.ops[result->op.value];
      Require(load.life == analysis::ValueLife::kLive && load.result == *value &&
          std::holds_alternative<analysis::ValueLexicalLoad>(load.operation), FamilyTreeError::kContext);
      Require(op.origin.value < graph.origins.size(), FamilyTreeError::kContext);
      const auto& origin = graph.origins[op.origin.value];
      const auto& input = member.function.Normal().input.Input();
      Require(origin.site.value < input.instructions.size(), FamilyTreeError::kContext);
      const auto& original = input.instructions[origin.site.value];
      const auto* original_check = std::get_if<analysis::SuperCheck>(&original.operation);
      Require(original_check && original_check->kind == check->kind && origin.pc == original.pc &&
          origin.original == original.origin && origin.disposition == analysis::OriginDisposition::kRetained &&
          op.normal == original.effects.normal && op.before_throw == original.effects.before_throw &&
          op.exceptional == original.effects.exceptional, FamilyTreeError::kContext);
      guards.push_back({static_cast<std::uint32_t>(i)});
    }
    return guards;
  }
  void ProjectDefaults(FileFamily& family, const FunctionContext* root) {
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      Step(1); member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset(); defaults_proved_ = false;
      auto& member = family.members[i];
      Require(i == 0 || member.context.has_value(), FamilyTreeError::kContext);
      // A module entry has no callable signature to project. Its supplied tree
      // retains the original graph; children still use the same default pass.
      if (i == 0 && !root) continue;
      const auto length = i == 0 ? root->Length() : member.context->Length();
      const auto& frame = i == 0 ? root->Frame() : member.context->Frame();
      if (member.function.Phase()) continue;
      auto& values = member.function.Normal().values;
      if (length >= frame.parameters) continue;
      auto parameters = Take<ParameterPlan, ParameterPlanError>(BuildParameterPlan(values.graph, frame.parameters, left_));
      Account(parameters);
      auto direct = Take<DefaultCandidates, DefaultCandidateError>(
          FindDefaultCandidates(values.graph, parameters, length, left_));
      Account(direct);
      std::optional<DefaultBody> projected;
      if (direct.capture) {
        Require(values.cells && values.environments, FamilyTreeError::kContext);
        projected.emplace(Take<DefaultBody, DefaultBodyError>(DefaultBody::BuildCaptured(values.graph,
            *values.environments, *values.cells, parameters, direct, length, left_)));
      } else if (!direct.values.empty()) {
        projected.emplace(Take<DefaultBody, DefaultBodyError>(
            DefaultBody::Build(values.graph, parameters, direct, length, left_)));
      } else {
        if (!values.cells) continue;
        auto candidates = Take<CellDefaults, CellDefaultError>(FindCellDefaults(values.graph, *values.cells, parameters, length, left_));
        Account(candidates); default_candidates_ = candidates.candidates.size();
        if (candidates.candidates.empty()) continue;
        Require(values.environments.has_value(), FamilyTreeError::kContext);
        projected.emplace(Take<DefaultBody, DefaultBodyError>(DefaultBody::BuildCell(values.graph,
            *values.environments, *values.cells, parameters, candidates, length, left_)));
        defaults_proved_ = true;
      }
      auto& body = *projected;
      Admit(body.AllocatedBytes(), 1); Step(body.Work());
      // Retain the sealed residual owner for signature checks. The mutable
      // working copy is separately admitted before allocation and is never
      // passed off as the original input-correspondence graph.
      Admit(body.AllocatedBytes(), 1);
      const auto& residual = body.Graph();
      Step(residual.blocks.size()); Step(residual.edges.size()); Step(residual.ops.size());
      Step(residual.values.size()); Step(residual.uses.size()); Step(residual.origins.size());
      Step(residual.exceptional_exits.size()); Step(residual.input.sites.size()); Step(residual.input.values.size());
      values.graph = body.Graph();
      family.members[i].function.Defaults().emplace(std::move(body));
      auto environments = Take<analysis::ValueEnvironments, analysis::ValueEnvironmentError>(
          analysis::BuildValueEnvironments(values.graph, left_));
      Account(environments); values.environments = std::move(environments);
      auto cells = Take<CellPlan, CellError>(BuildCellPlan(values.graph, *values.environments, left_));
      Account(cells); values.cells = std::move(cells);
      auto modules = Take<ModuleGuards, ModuleGuardError>(BuildModuleGuards(member.function.Normal().input, values.graph, left_));
      Account(modules); values.modules = std::move(modules);
    }
  }
  void Rebind(FileFamily& family) {
    // Every residual revision is fixed before any creation edge is rebound.
    // This prevents a parent's cached binding from retaining a child's old
    // revision when that child's own defaults are projected later.
    Account(Take<FamilyCellCost, CaptureCellError>(PlanFamilyCells(family, left_)));
    for (auto& member : family.members) { Step(1); member.children.clear(); }
    for (std::size_t i = 1; i < family.members.size(); ++i) {
      Step(1); member_ = {static_cast<std::uint32_t>(i)};
      auto& child = family.members[i];
      Require(child.incoming.has_value() && child.incoming->parent.value < i, FamilyTreeError::kParent);
      auto& edge = *child.incoming;
      auto& parent = family.members[edge.parent.value];
      const auto child_work = child.function.Capture();
      const auto* phase = parent.function.Phase();
      Require((phase != nullptr) == edge.phase_operation.has_value(), FamilyTreeError::kParent);
      std::optional<PhasedCreationSite> occurrence;
      if (phase) {
        Require(lookup_ && child.context, FamilyTreeError::kContext);
        occurrence.emplace(PhasedCreationSite{*edge.phase_operation, edge.site, edge.creation});
      }
      auto cells = Take<CaptureCells, CaptureCellError>(phase ? BindPhasedCaptureCells(index_, *lookup_, phase->Phases(),
          phase->Body(), phase->Locals(), *occurrence, child_work, *child.context, left_, parent.function.Cells()) :
          BindCaptureCells(index_, parent.function.NormalCapture(), child_work,
              edge.site, edge.creation, left_));
      Account(cells);
      auto evaluation = Take<CapturedFunctionBinding, CaptureEvaluationFailure>(phase ?
          BindPhasedCaptureEvaluation(index_, *lookup_, phase->Phases(), phase->Body(), phase->Locals(), *occurrence,
              child_work, *child.context, cells, family.Owner(edge.parent), family.Owner(member_), left_, parent.function.Cells()) :
          BindCaptureEvaluation(index_, parent.function.NormalCapture(),
              child_work, edge.site, edge.creation, cells, family.Owner(edge.parent), family.Owner(member_), left_));
      Account(evaluation);
      Require(parent.children.size() < parent.children.capacity(), FamilyTreeError::kParent);
      Require(parent.children.empty() || parent.children.back().creation < evaluation.binding.creation, FamilyTreeError::kParent);
      parent.children.push_back(std::move(evaluation.binding));
      edge.cells = std::move(cells);
    }
  }
  template<class T> [[noreturn]] void Fail(T error) const {
    throw FamilyTreeFailure{member_, std::move(error), default_candidates_, defaults_proved_};
  }
  void Require(bool ok, FamilyTreeError error) const { if (!ok) Fail(error); }
  void Step(std::uint64_t work) {
    Require(work <= left_.work, FamilyTreeError::kWorkLimit); left_.work -= work;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, FamilyTreeError::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  template<class T> void Account(const T& value) { Admit(value.allocated_bytes, 1); Step(value.work); }
  template<class T, class E> T Take(std::variant<T, E> result) {
    if (const auto* error = std::get_if<E>(&result)) Fail(*error);
    return std::get<T>(std::move(result));
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  analysis::SemanticBudget initial_, left_;
  const RecordModuleContext* modules_;
  ModuleOwnerId module_owner_;
  const abc::MethodLookup* lookup_;
  RecoveryMode recovery_;
  FamilyId member_{0};
  std::optional<std::size_t> default_candidates_;
  bool defaults_proved_ = false;
  std::vector<std::vector<std::uint8_t>> receiver_cells_;
  std::vector<ReceiverScopes> receiver_scopes_;
};
}  // namespace
std::variant<ConstructedFamily, FamilyTreeFailure> BuildFileFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, const FunctionContext& root,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner,
    const abc::MethodLookup* lookup, RecoveryMode recovery, const ExternalFamilyCapture* external) {
  try { return FamilyTrees(file, header, index, budget, modules, module_owner, lookup, recovery).Build(family, &root, {}, external);
  } catch (const FamilyTreeFailure& error) { return error;
  } catch (const std::bad_alloc&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit};
  } catch (const std::length_error&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit}; }
}
std::variant<ConstructedFamily, FamilyTreeFailure> BuildRecordFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, FamilyMember entry,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner,
    const abc::MethodLookup* lookup) {
  try { return FamilyTrees(file, header, index, budget, modules, module_owner, lookup,
      RecoveryMode::kCompleteOnly).Build(family, nullptr, std::move(entry));
  } catch (const FamilyTreeFailure& error) { return error;
  } catch (const std::bad_alloc&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit};
  } catch (const std::length_error&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit}; }
}
}  // namespace abcd::source
