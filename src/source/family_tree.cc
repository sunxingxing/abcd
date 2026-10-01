#include "family_tree.h"
#include "family_cells.h"
#include "derived_projection.h"

#include <limits>
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
  ConstructedFamily Build(FileFamily& family, const FunctionContext* root, std::optional<FamilyMember> entry = {}) {
    Admit(1, sizeof(FamilyTrees));
    Require(!family.members.empty() && (root != nullptr) != entry.has_value(), FamilyTreeError::kContext);
    Require(family.members.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
    // Only a supplied record root has already composed its ClassTree bodies.
    // Their original barriers are checked against those bodies by FamilyWrites;
    // ordinary child families cannot silently inherit this permission.
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      Step(1);
      Require(family.members[i].classes.empty() || (entry && i == FileFamily::kRoot.value), FamilyTreeError::kContext);
    }
    ProjectDefaults(family, root);
    Rebind(family);
    std::vector<FamilyMember> trees;
    bool incomplete = false;
    Reserve(trees, family.members.size());
    if (entry) {
      const auto& function = family.members.front().function;
      Require(!function.Phase() && !family.members.front().incoming && !entry->incoming &&
          entry->tree.owner.value == FileFamily::kRoot.value &&
          entry->tree.identity == function.Identity() && entry->tree.method == function.Method() &&
          entry->record == function.Record(), FamilyTreeError::kContext);
      trees.push_back(std::move(*entry));
    }
    for (std::size_t i = entry ? 1 : 0; i < family.members.size(); ++i) {
      member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset();
      defaults_proved_ = false;
      if (defaults_[i] && defaults_[i]->Storage() == DefaultStorage::kCell) {
        default_candidates_ = defaults_[i]->Signature().Defaults().size();
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
      auto syntax = Take<ResolvedSyntax, SyntaxResolutionFailure>(ResolveFunctionSyntax(
          file_, header_, index_, context, phase ? phase->OriginalBody() : function.Body(), graph, {member_.value}, left_));
      Account(syntax);
      std::optional<ModuleAliases> normal_aliases;
      const ModuleAliases* aliases = phase ? phase->Phases().Modules() : nullptr;
      if (aliases || (!phase && !function.Normal().values.modules.pairs.empty())) {
        Require(modules_ != nullptr, FamilyTreeError::kModules);
        Require(syntax.syntax.names.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
        const auto first_name = static_cast<std::uint32_t>(syntax.syntax.names.size());
        if (!phase) {
          normal_aliases = Take<ModuleAliases, ModuleAliasFailure>(BindModuleAliases(file_, header_, index_,
              function.Normal().input, function.Normal().values, *modules_, module_owner_, {first_name}, left_));
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
        Admit(sizeof(DerivedProjection) + derived->guards.capacity() * sizeof(analysis::OpId), 1);
      }
      std::optional<ControlPlan> normal_control;
      if (!phase) { normal_control.emplace(Take<ControlPlan, ControlError>(BuildControlPlan(graph, left_))); Account(*normal_control); }
      const auto& control = phase ? phase->Phases().Control() : *normal_control;
      const auto module_evaluations = aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{};
      const auto demands = member.function.Cells() ? std::span<const CellDemand>(member.function.Cells()->demands) : std::span<const CellDemand>{};
      // Preserve the phased file input's conditional write domain. The complete
      // family below still discharges every original store and capture barrier.
      auto evaluation = Take<EvaluationPlan, EvaluationError>(phase ?
          BuildPhasedEvaluationPlan(graph, left_, i == 0 ? IncomingCells::kReject : IncomingCells::kConditional,
              member.children, module_evaluations, demands, {}, phase->Locals().writes) :
          BuildEvaluationPlan(graph, left_, i == 0 ? IncomingCells::kReject : IncomingCells::kConditional,
              member.children, module_evaluations, demands, {}, analysis::ClosureWrites::kLanguage, {},
              derived ? std::span<const analysis::OpId>(derived->guards) : std::span<const analysis::OpId>{}));
      Account(evaluation);
      std::optional<IncomingCaptureTree> incoming;
      if (member.incoming) {
        const auto parent = member.incoming->parent.value;
        Require(parent < trees.size(), FamilyTreeError::kParent);
        incoming.emplace(IncomingCaptureTree{member.incoming->cells, {trees[parent].tree, trees[parent].flow}});
      }
      auto constants = syntax.View();
      if (derived) constants.constructor_guards = derived->guards;
      auto constructed = [&]() -> ConstructedFunction {
        if (phase) {
          auto draft = Take<ExceptionFunctionDraft, FunctionTreeError>(BuildExceptionFunctionDraft(graph,
              phase->Phases().TreeInput(phase->Body()), evaluation, std::move(syntax.syntax), constants, left_,
              incoming ? &*incoming : nullptr));
          return {std::move(draft.tree), draft.allocated_bytes, draft.work};
        }
        return Take<ConstructedFunction, FunctionTreeError>(BuildFunctionTree(graph, control,
            evaluation, std::move(syntax.syntax), constants, left_, incoming ? &*incoming : nullptr,
            member.children.empty() && !defaults_[i] ? recovery_ : RecoveryMode::kCompleteOnly));
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
        Require(member.children.empty() && !defaults_[i] &&
            (!member.incoming || member.incoming->cells.targets.empty()), FamilyTreeError::kContext);
        incomplete = true;
        std::optional<CaptureCells> cells;
        if (member.incoming) cells.emplace(std::move(member.incoming->cells));
        trees.push_back({std::move(constructed.tree), {}, std::move(cells), {}, {}, function.Record()});
        continue;
      }
      if (defaults_[i]) {
        auto signature = Take<DefaultSignatureProof, DefaultSignatureError>(
            BuildDefaultSignature(*defaults_[i], constructed.tree, left_));
        Account(signature);
        auto semantics = Take<TreeSemantics, TreeSemanticError>(VerifyTreeSemantics(
            graph, control, evaluation, constructed.tree, constants, left_));
        Account(semantics);
      }
      auto flow = Take<TreeFlow, TreeFlowError>(phase ? VerifyExceptionTreeFlow(graph,
          phase->Phases().TreeInput(phase->Body()), evaluation, constructed.tree, left_) :
          VerifyTreeFlow(graph, control, evaluation, constructed.tree, left_));
      Account(flow);
      if (aliases) Step(Take<std::uint64_t, ModuleAliasTreeError>(
          VerifyModuleAliasTree(*aliases, evaluation, constructed.tree, left_)));
      auto abi = Take<ConstructedAbi, FunctionAbiError>(phase ? BuildPhasedFunctionAbi(graph, evaluation, constructed.tree,
          context.Frame(), context.Length(), left_) : BuildFunctionAbi(graph, constructed.tree, context.Frame(), context.Length(), left_));
      Account(abi);
      auto sources = Take<InstructionSources, InstructionSourceError>(phase ?
          BuildInstructionSources(phase->Phases(), phase->Body(), constructed.tree, left_) :
          BuildInstructionSources(function.Normal().input, graph, constructed.tree, left_));
      Account(sources);
      std::optional<CaptureCells> cells;
      if (member.incoming) cells.emplace(std::move(member.incoming->cells));
      trees.push_back({std::move(constructed.tree), std::move(flow), std::move(cells),
                       std::move(abi.abi), std::move(sources), function.Record()});
    }
    auto result = Take<ConstructedFamily, FamilyError>(BuildFunctionFamily(std::move(trees), {FileFamily::kRoot.value}, left_));
    Account(result);
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
    const auto writes = VerifyFamilyWrites(index_, family, result, left_, lookup_);
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
  void ProjectDefaults(FileFamily& family, const FunctionContext* root) {
    Reserve(defaults_, family.members.size()); defaults_.resize(family.members.size());
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
      if (!direct.values.empty()) {
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
      defaults_[i].emplace(std::move(body));
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
              child_work, *child.context, cells, {edge.parent.value}, {member_.value}, left_, parent.function.Cells()) :
          BindCaptureEvaluation(index_, parent.function.NormalCapture(),
              child_work, edge.site, edge.creation, cells, {edge.parent.value}, {member_.value}, left_));
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
  std::vector<std::optional<DefaultBody>> defaults_;
};
}  // namespace
std::variant<ConstructedFamily, FamilyTreeFailure> BuildFileFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, const FunctionContext& root,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner,
    const abc::MethodLookup* lookup, RecoveryMode recovery) {
  try { return FamilyTrees(file, header, index, budget, modules, module_owner, lookup, recovery).Build(family, &root);
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
