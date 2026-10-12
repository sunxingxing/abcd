#include "exception_family_probe.h"

#include "../../src/source/syntax_resolution.h"
#include "../../src/source/lexical_stores.h"
#include "../../src/source/function_abi.h"
#include "../../src/source/family_print.h"
#include "../support/instruction_positions.h"

#include <sstream>

namespace abcd::test {
ExceptionFamilyProbe ProbeExceptionFamily(const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const source::RecordModuleContext& modules, source::ModuleOwnerId module_owner,
    const source::FunctionContext& context, const source::FilePhasedFunction& input,
    const source::EvaluationPlan& evaluation,
    std::vector<ExceptionChild> children, const abc::MethodLookup& lookup,
    analysis::SemanticBudget budget) {
  using namespace source;
  const auto& body = input.Body();
  const auto& parent = input.Phases();
  const auto& locals = input.Locals();
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
  if (!tree) { result.tree_error = std::get<FunctionTreeFailure>(built); return result; }
  result.stage = FamilyProbeStage::kParentFlow;
  auto flowed = VerifyExceptionTreeFlow(parent.Graph(), parent.TreeInput(body), evaluation, tree->tree, budget);
  auto* flow = std::get_if<TreeFlow>(&flowed);
  if (!flow) return result;
  if (parent.Modules() && !std::holds_alternative<std::uint64_t>(
      VerifyModuleAliasTree(*parent.Modules(), evaluation, tree->tree, budget))) return result;
  result.parent_tree = true;
  const auto stores = VerifyLexicalStores(PhasedStoreInput{parent, body}, parent.Graph(), parent.Cells(),
      {tree->tree, *flow}, {}, budget);
  const auto* parent_stores = std::get_if<LexicalStores>(&stores);
  if (!parent_stores) return result;
  ++result.checked_store_methods; result.checked_lexical_stores += parent_stores->stores;
  auto parent_abi = BuildPhasedFunctionAbi(parent.Graph(), evaluation, tree->tree,
      context.Frame(), context.Length(), budget);
  auto* abi = std::get_if<ConstructedAbi>(&parent_abi);
  if (!abi) return result;
  ++result.abi_methods;
  auto parent_sources = BuildInstructionSources(parent, body, tree->tree, budget);
  auto* sources = std::get_if<InstructionSources>(&parent_sources);
  if (!sources) return result;
  ++result.source_methods;
  auto exact_sources = budget; exact_sources.bytes = sources->allocated_bytes; exact_sources.work = sources->work;
  if (!std::holds_alternative<InstructionSources>(BuildInstructionSources(parent, body, tree->tree, exact_sources))) return result;
  --exact_sources.work;
  if (std::get<InstructionSourceError>(BuildInstructionSources(parent, body, tree->tree, exact_sources)) !=
      InstructionSourceError::kWorkLimit) return result;
  ++exact_sources.work; --exact_sources.bytes;
  if (std::get<InstructionSourceError>(BuildInstructionSources(parent, body, tree->tree, exact_sources)) !=
      InstructionSourceError::kResourceLimit) return result;
  if (tree->tree.operations.empty()) return result;
  const auto placement = tree->tree.operations.back();
  tree->tree.operations.pop_back();
  if (std::get<InstructionSourceError>(BuildInstructionSources(parent, body, tree->tree, budget)) !=
      InstructionSourceError::kCoverage) return result;
  tree->tree.operations.push_back(placement); tree->tree.operations.push_back(placement);
  if (std::get<InstructionSourceError>(BuildInstructionSources(parent, body, tree->tree, budget)) !=
      InstructionSourceError::kPlacement) return result;
  tree->tree.operations.pop_back();
  ++tree->tree.method.value;
  if (std::get<InstructionSourceError>(BuildInstructionSources(parent, body, tree->tree, budget)) !=
      InstructionSourceError::kIdentity) return result;
  --tree->tree.method.value;
  result.rejected_source_forgery = std::holds_alternative<InstructionSources>(
      BuildInstructionSources(parent, body, tree->tree, budget));
  if (!result.rejected_source_forgery) return result;
  std::vector<FamilyMember> members;
  members.reserve(children.size() + 1);
  members.push_back({std::move(tree->tree), std::move(*flow), {}, std::move(abi->abi), std::move(*sources)});
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
    if (!child_tree) { result.tree_error = std::get<FunctionTreeFailure>(child_built); return result; }
    result.stage = FamilyProbeStage::kChildFlow;
    auto child_flowed = VerifyTreeFlow(graph, *control, *child_evaluation, child_tree->tree, budget);
    auto* child_flow = std::get_if<TreeFlow>(&child_flowed);
    if (!child_flow) return result;
    const auto checked_stores = VerifyLexicalStores(NormalStoreInput{child.input}, graph,
        child.values.cells ? &*child.values.cells : nullptr, {child_tree->tree, *child_flow}, {}, budget);
    const auto* child_stores = std::get_if<LexicalStores>(&checked_stores);
    if (!child_stores) return result;
    ++result.checked_store_methods; result.checked_lexical_stores += child_stores->stores;
    result.stage = FamilyProbeStage::kChildAliases;
    if (aliases && !std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(
        *aliases, *child_evaluation, child_tree->tree, budget))) return result;
    auto child_abi = BuildFunctionAbi(graph, child_tree->tree, child.context.Frame(), child.context.Length(), budget);
    auto* ready_abi = std::get_if<ConstructedAbi>(&child_abi);
    if (!ready_abi) return result;
    ++result.abi_methods;
    auto child_sources = BuildInstructionSources(child.input, graph, child_tree->tree, budget);
    auto* ready_sources = std::get_if<InstructionSources>(&child_sources);
    if (!ready_sources) return result;
    ++result.source_methods;
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
    members.push_back({std::move(child_tree->tree), std::move(*child_flow), std::move(child.captures),
        std::move(ready_abi->abi), std::move(*ready_sources)});
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
  result.stage = FamilyProbeStage::kWrites;
  std::vector<FamilyWriteInput> inputs;
  inputs.reserve(children.size() + 1);
  inputs.push_back({PhasedFamilyInput{parent, body, locals, lookup}, {}, {}});
  for (const auto& child : children) inputs.push_back({CaptureFunction{child.input, child.values},
      FamilyWriteEdge{FileFamily::kRoot, PhasedWriteEdge{child.creation, child.context}}, {}});
  const auto verify = [&](analysis::SemanticBudget limit) { return VerifyFamilyWrites(index, inputs, *layout, limit); };
  const auto checked = verify(budget);
  const auto* writes = std::get_if<FamilyWrites>(&checked);
  if (!writes || writes->facts.stores != result.checked_lexical_stores ||
      writes->facts.creations != children.size()) return result;
  result.family_writes = true; result.write_creations = writes->facts.creations;
  auto exact = budget; exact.bytes = writes->allocated_bytes; exact.work = writes->work;
  if (!std::holds_alternative<FamilyWrites>(verify(exact))) return result;
  const auto fails = [&](analysis::SemanticBudget limit, FamilyWriteError error) {
    const auto failed = verify(limit);
    const auto* failure = std::get_if<FamilyWriteFailure>(&failed);
    return failure && failure->kind == error;
  };
  --exact.work;
  if (!fails(exact, FamilyWriteError::kWorkLimit)) return result;
  ++exact.work; --exact.bytes;
  if (!fails(exact, FamilyWriteError::kResourceLimit)) return result;
  auto unknown = locals; unknown.writes = analysis::ClosureWrites::kUnknown;
  inputs[0].function.emplace<PhasedFamilyInput>(PhasedFamilyInput{parent, body, unknown, lookup});
  if (!fails(budget, FamilyWriteError::kDomain)) return result;
  inputs[0].function.emplace<PhasedFamilyInput>(PhasedFamilyInput{parent, body, locals, lookup});
  if (!children.empty()) {
    // Remove descendants from both censuses: matching counts alone must not
    // hide their original creation barriers, even with zero captured cells.
    auto omitted = *layout;
    omitted.members.resize(1); omitted.parents.resize(1);
    const auto missing = VerifyFamilyWrites(index, std::span<const FamilyWriteInput>(inputs).first(1), omitted, budget);
    const auto* missing_error = std::get_if<FamilyWriteFailure>(&missing);
    if (!missing_error || missing_error->kind != FamilyWriteError::kCoverage ||
        missing_error->member != FileFamily::kRoot ||
        missing_error->pc != children.front().creation.binding.offset) return result;
    const auto saved = *inputs[1].incoming;
    inputs[1].incoming.reset();
    if (!fails(budget, FamilyWriteError::kCapture)) return result;
    inputs[1].incoming.emplace(saved);
    inputs[1].incoming->parent = FamilyId{1};
    if (!fails(budget, FamilyWriteError::kCapture)) return result;
    inputs[1].incoming.emplace(saved);
    inputs[1].incoming.emplace(FamilyWriteEdge{FileFamily::kRoot,
        NormalWriteEdge{children[0].creation.instruction, children[0].creation.binding}});
    if (!fails(budget, FamilyWriteError::kCapture)) return result;
    inputs[1].incoming.emplace(saved);
    if (children.size() > 1) {
      const auto second = *inputs[2].incoming;
      inputs[2].incoming.emplace(saved);
      if (!fails(budget, FamilyWriteError::kCoverage)) return result;
      inputs[2].incoming.emplace(second);
    }
  }
  result.rejected_write_forgery = std::holds_alternative<FamilyWrites>(verify(budget));
  if (!result.rejected_write_forgery) return result;
  result.stage = FamilyProbeStage::kPrint;
  if (!context.ClassContext()) return result;
  std::ostringstream output;
  const auto printed = PrintClassFamilyDraft(*layout, *context.ClassContext(), output, {}, budget);
  const auto* positions = std::get_if<PrintedFamilyDraft>(&printed);
  if (!positions || positions->members.size() != layout->members.size() ||
      positions->functions.size() != layout->members.size()) return result;
  for (std::size_t i = 0; i < layout->members.size(); ++i) {
    const auto& member = layout->members[i];
    if (!member.sources || !member.abi) return result;
    CheckInstructionPositions(member.tree, *member.sources, positions->members[i], positions->functions[i]);
    result.mapped_instructions += member.sources->input_sites;
  }
  result.draft = output.str();
  result.stage = FamilyProbeStage::kDone;
  return result;
}
}  // namespace abcd::test
