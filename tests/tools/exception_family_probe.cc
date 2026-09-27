#include "exception_family_probe.h"

#include "../../src/source/syntax_resolution.h"

namespace abcd::test {
ExceptionFamilyProbe ProbeExceptionFamily(const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const source::RecordModuleContext& modules, source::ModuleOwnerId module_owner,
    const source::FunctionContext& context, const abc::MethodBody& body,
    const source::PhasedBody& parent, const source::EvaluationPlan& evaluation,
    std::vector<ExceptionChild> children, analysis::SemanticBudget budget) {
  using namespace source;
  ExceptionFamilyProbe result;
  constexpr FunctionOwnerId kRoot{0};
  auto resolved = ResolveFunctionSyntax(file, header, index, context, body, parent.Graph(), kRoot, budget);
  auto* syntax = std::get_if<ResolvedSyntax>(&resolved);
  if (!syntax) return result;
  if (const auto* aliases = parent.Modules()) {
    const auto first = static_cast<std::uint32_t>(syntax->syntax.names.size());
    syntax->syntax.names.insert(syntax->syntax.names.end(), aliases->names.begin(), aliases->names.end());
    syntax->syntax.modules = aliases->aliases;
    for (auto& alias : syntax->syntax.modules) alias.name.value += first;
  }
  result.stage = FamilyProbeStage::kParentTree;
  const auto constants = syntax->View();
  auto built = BuildExceptionFunctionDraft(parent.Graph(), parent.TreeInput(body), evaluation,
      std::move(syntax->syntax), constants, budget);
  auto* tree = std::get_if<ExceptionFunctionDraft>(&built);
  if (!tree) { result.tree_error = std::get<FunctionTreeError>(built); return result; }
  result.stage = FamilyProbeStage::kParentFlow;
  auto flowed = VerifyExceptionTreeFlow(parent.Graph(), parent.TreeInput(body), evaluation, tree->tree, budget);
  auto* flow = std::get_if<TreeFlow>(&flowed);
  if (!flow) return result;
  if (parent.Modules() && !std::holds_alternative<std::uint64_t>(
      VerifyModuleAliasTree(*parent.Modules(), evaluation, tree->tree, budget))) return result;
  result.parent_tree = true;
  std::vector<FamilyMember> members;
  members.reserve(children.size() + 1);
  members.push_back({std::move(tree->tree), std::move(*flow)});
  for (auto& child : children) {
    const FunctionOwnerId owner{static_cast<std::uint32_t>(members.size())};
    const auto& graph = child.values.graph;
    result.stage = FamilyProbeStage::kChildSyntax;
    auto resolved_child = ResolveFunctionSyntax(file, header, index, child.context, child.body, graph, owner, budget);
    auto* child_syntax = std::get_if<ResolvedSyntax>(&resolved_child);
    if (!child_syntax) return result;
    std::optional<ModuleAliases> aliases;
    if (!child.values.modules.pairs.empty()) {
      result.stage = FamilyProbeStage::kChildModules;
      auto bound = BindModuleAliases(file, header, index, child.input, child.values, modules, module_owner,
          {static_cast<std::uint32_t>(child_syntax->syntax.names.size())}, budget);
      auto* ready = std::get_if<ModuleAliases>(&bound);
      if (!ready) return result;
      aliases.emplace(std::move(*ready));
      child_syntax->syntax.names.insert(child_syntax->syntax.names.end(), aliases->names.begin(), aliases->names.end());
      child_syntax->syntax.modules = aliases->aliases;
    }
    result.stage = FamilyProbeStage::kChildControl;
    auto controlled = BuildControlPlan(graph, budget);
    const auto* control = std::get_if<ControlPlan>(&controlled);
    if (!control) return result;
    result.stage = FamilyProbeStage::kChildEvaluation;
    auto evaluated = BuildEvaluationPlan(graph, budget, IncomingCells::kConditional, {},
        aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{});
    const auto* child_evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!child_evaluation) return result;
    const IncomingCaptureTree incoming{child.captures, {members.front().tree, members.front().flow}};
    result.stage = FamilyProbeStage::kChildTree;
    const auto child_constants = child_syntax->View();
    auto child_built = BuildFunctionTree(graph, *control, *child_evaluation,
        std::move(child_syntax->syntax), child_constants, budget, &incoming);
    auto* child_tree = std::get_if<ConstructedFunction>(&child_built);
    if (!child_tree) { result.tree_error = std::get<FunctionTreeError>(child_built); return result; }
    result.stage = FamilyProbeStage::kChildFlow;
    auto child_flowed = VerifyTreeFlow(graph, *control, *child_evaluation, child_tree->tree, budget);
    auto* child_flow = std::get_if<TreeFlow>(&child_flowed);
    if (!child_flow) return result;
    result.stage = FamilyProbeStage::kChildAliases;
    if (aliases && !std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(
        *aliases, *child_evaluation, child_tree->tree, budget))) return result;
    if (!child_tree->tree.captures.empty()) {
      auto& alias = child_tree->tree.captures.front();
      const auto saved = alias.target;
      alias.target.owner = owner;
      const auto forged = VerifyCaptureTree(child.captures, incoming.parent,
          {child_tree->tree, *child_flow}, budget);
      alias.target = saved;
      result.rejected_capture_forgery = std::holds_alternative<CaptureTreeError>(forged) &&
          std::get<CaptureTreeError>(forged) == CaptureTreeError::kTarget;
      if (!result.rejected_capture_forgery) return result;
      if (!std::holds_alternative<std::uint64_t>(VerifyCaptureTree(child.captures, incoming.parent,
          {child_tree->tree, *child_flow}, budget))) return result;
    }
    members.push_back({std::move(child_tree->tree), std::move(*child_flow), std::move(child.captures)});
    ++result.child_trees;
  }
  result.stage = FamilyProbeStage::kLayout;
  auto family = BuildFunctionFamily(std::move(members), kRoot, budget);
  const auto* layout = std::get_if<ConstructedFamily>(&family);
  if (!layout) return result;
  result.layout = true;
  result.stage = FamilyProbeStage::kEnclosure;
  if (!std::holds_alternative<FamilyEnclosure>(VerifyFamilyEnclosure(*layout, budget))) return result;
  result.enclosure = true;
  result.stage = FamilyProbeStage::kDone;
  return result;
}
}  // namespace abcd::test
