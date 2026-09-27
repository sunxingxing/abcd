#include "family_tree.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
class FamilyTrees {
 public:
  FamilyTrees(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, analysis::SemanticBudget budget,
      const RecordModuleContext* modules, ModuleOwnerId owner)
      : file_(file), header_(header), index_(index), initial_(budget), left_(budget),
        modules_(modules), module_owner_(owner) {}
  ConstructedFamily Build(FileFamily& family, const FunctionContext& root) {
    Admit(1, sizeof(FamilyTrees));
    Require(!family.members.empty(), FamilyTreeError::kContext);
    Require(family.members.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
    ProjectDefaults(family, root);
    Rebind(family);
    std::vector<FamilyMember> trees;
    Reserve(trees, family.members.size());
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset();
      defaults_proved_ = false;
      if (defaults_[i]) {
        default_candidates_ = defaults_[i]->Signature().Defaults().size();
        defaults_proved_ = true;
      }
      Step(1);
      auto& member = family.members[i];
      std::optional<FunctionContext> child_context;
      if (i != 0) {
        Require(member.context.has_value() && member.incoming.has_value(), FamilyTreeError::kContext);
        child_context.emplace(std::move(*member.context));
      } else Require(!member.incoming && !member.context, FamilyTreeError::kContext);
      const auto& context = child_context ? *child_context : root;
      const auto& function = member.function;
      Require(!function.input.HasOwner() || function.input.MatchesOwner(context), FamilyTreeError::kContext);
      const auto& graph = function.values.graph;
      Require(context.Identity() == function.input.Identity() &&
          context.Target() == function.input.Method(), FamilyTreeError::kContext);
      auto syntax = Take<ResolvedSyntax, SyntaxResolutionFailure>(ResolveFunctionSyntax(
          file_, header_, index_, context, function.body, graph, {member_.value}, left_));
      Account(syntax);
      std::optional<ModuleAliases> aliases;
      if (!function.values.modules.pairs.empty()) {
        Require(modules_ != nullptr, FamilyTreeError::kModules);
        Require(syntax.syntax.names.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
        aliases = Take<ModuleAliases, ModuleAliasFailure>(BindModuleAliases(file_, header_, index_,
            function.input, function.values, *modules_, module_owner_,
            {static_cast<std::uint32_t>(syntax.syntax.names.size())}, left_));
        Account(*aliases);
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
        for (const auto& alias : aliases->aliases) { Step(1); syntax.syntax.modules.push_back(alias); }
      }
      auto control = Take<ControlPlan, ControlError>(BuildControlPlan(graph, left_));
      Account(control);
      auto evaluation = Take<EvaluationPlan, EvaluationError>(BuildEvaluationPlan(graph, left_,
          i == 0 ? IncomingCells::kReject : IncomingCells::kConditional, member.children,
          aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{}));
      Account(evaluation);
      std::optional<IncomingCaptureTree> incoming;
      if (member.incoming) {
        const auto parent = member.incoming->parent.value;
        Require(parent < trees.size(), FamilyTreeError::kParent);
        incoming.emplace(IncomingCaptureTree{member.incoming->cells, {trees[parent].tree, trees[parent].flow}});
      }
      const auto constants = syntax.View();
      auto constructed = Take<ConstructedFunction, FunctionTreeError>(BuildFunctionTree(graph, control,
          evaluation, std::move(syntax.syntax), constants, left_, incoming ? &*incoming : nullptr));
      Account(constructed);
      // Naming evidence is creation-specific and must survive arena growth and
      // capture/module alias insertion. It is not a lexical self declaration.
      if (context.CreationSite()) {
        const auto name = constructed.tree.inferred_name;
        Require(name && name->value < constructed.tree.names.size(), FamilyTreeError::kContext);
        Step(context.Name().size());
        Require(!constructed.tree.self && constructed.tree.names[name->value] == context.Name(),
            FamilyTreeError::kContext);
      } else Require(!constructed.tree.inferred_name, FamilyTreeError::kContext);
      if (defaults_[i]) {
        auto signature = Take<DefaultSignatureProof, DefaultSignatureError>(
            BuildDefaultSignature(*defaults_[i], constructed.tree, left_));
        Account(signature);
        auto semantics = Take<TreeSemantics, TreeSemanticError>(VerifyTreeSemantics(
            graph, control, evaluation, constructed.tree, constants, left_));
        Account(semantics);
      }
      auto flow = Take<TreeFlow, TreeFlowError>(VerifyTreeFlow(graph, control, evaluation, constructed.tree, left_));
      Account(flow);
      if (aliases) Step(Take<std::uint64_t, ModuleAliasTreeError>(
          VerifyModuleAliasTree(*aliases, evaluation, constructed.tree, left_)));
      auto abi = Take<ConstructedAbi, FunctionAbiError>(BuildFunctionAbi(graph, constructed.tree,
          context.Frame(), context.Length(), left_));
      Account(abi);
      auto sources = Take<InstructionSources, InstructionSourceError>(BuildInstructionSources(
          function.input, graph, constructed.tree, left_));
      Account(sources);
      std::optional<CaptureCells> cells;
      if (member.incoming) cells.emplace(std::move(member.incoming->cells));
      trees.push_back({std::move(constructed.tree), std::move(flow), std::move(cells),
                       std::move(abi.abi), std::move(sources)});
    }
    auto result = Take<ConstructedFamily, FamilyError>(BuildFunctionFamily(std::move(trees), {FileFamily::kRoot.value}, left_));
    Account(result);
    const auto enclosure = VerifyFamilyEnclosure(result, left_);
    if (const auto* failure = std::get_if<FamilyEnclosureFailure>(&enclosure)) {
      member_ = failure->member; Fail(*failure);
    }
    Account(std::get<FamilyEnclosure>(enclosure));
    const auto writes = VerifyFamilyWrites(index_, family, result, left_);
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
  void ProjectDefaults(FileFamily& family, const FunctionContext& root) {
    Reserve(defaults_, family.members.size()); defaults_.resize(family.members.size());
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      Step(1); member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset(); defaults_proved_ = false;
      auto& member = family.members[i];
      Require(i == 0 || member.context.has_value(), FamilyTreeError::kContext);
      const auto length = i == 0 ? root.Length() : member.context->Length();
      const auto& frame = i == 0 ? root.Frame() : member.context->Frame();
      auto& values = member.function.values;
      if (length >= frame.parameters || !values.cells) continue;
      auto parameters = Take<ParameterPlan, ParameterPlanError>(BuildParameterPlan(values.graph, frame.parameters, left_));
      Account(parameters);
      auto candidates = Take<CellDefaults, CellDefaultError>(FindCellDefaults(values.graph, *values.cells, parameters, length, left_));
      Account(candidates); default_candidates_ = candidates.candidates.size();
      if (candidates.candidates.empty()) continue;
      Require(values.environments.has_value(), FamilyTreeError::kContext);
      auto body = Take<DefaultBody, DefaultBodyError>(DefaultBody::BuildCell(values.graph,
          *values.environments, *values.cells, parameters, candidates, length, left_));
      Admit(body.AllocatedBytes(), 1); Step(body.Work()); defaults_proved_ = true;
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
      auto modules = Take<ModuleGuards, ModuleGuardError>(BuildModuleGuards(member.function.input, values.graph, left_));
      Account(modules); values.modules = std::move(modules);
    }
  }
  void Rebind(FileFamily& family) {
    // Every residual revision is fixed before any creation edge is rebound.
    // This prevents a parent's cached binding from retaining a child's old
    // revision when that child's own defaults are projected later.
    for (auto& member : family.members) { Step(1); member.children.clear(); }
    for (std::size_t i = 1; i < family.members.size(); ++i) {
      Step(1); member_ = {static_cast<std::uint32_t>(i)};
      auto& child = family.members[i];
      Require(child.incoming.has_value() && child.incoming->parent.value < i, FamilyTreeError::kParent);
      auto& edge = *child.incoming;
      auto& parent = family.members[edge.parent.value];
      const CaptureFunction parent_work{parent.function.input, parent.function.values};
      const CaptureFunction child_work{child.function.input, child.function.values};
      auto cells = Take<CaptureCells, CaptureCellError>(BindCaptureCells(index_, parent_work, child_work,
          edge.site, edge.creation, left_));
      Account(cells);
      auto evaluation = Take<CapturedFunctionBinding, CaptureEvaluationFailure>(BindCaptureEvaluation(index_,
          parent_work, child_work, edge.site, edge.creation, cells, {edge.parent.value}, {member_.value}, left_));
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
  FamilyId member_{0};
  std::optional<std::size_t> default_candidates_;
  bool defaults_proved_ = false;
  std::vector<std::optional<DefaultBody>> defaults_;
};
}  // namespace
std::variant<ConstructedFamily, FamilyTreeFailure> BuildFileFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, const FunctionContext& root,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner) {
  try { return FamilyTrees(file, header, index, budget, modules, module_owner).Build(family, root);
  } catch (const FamilyTreeFailure& error) { return error;
  } catch (const std::bad_alloc&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit};
  } catch (const std::length_error&) { return FamilyTreeFailure{{0}, FamilyTreeError::kResourceLimit}; }
}
}  // namespace abcd::source
