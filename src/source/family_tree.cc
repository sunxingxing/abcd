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
    Require(!family.members.empty(), FamilyTreeError::kContext);
    Require(family.members.size() < std::numeric_limits<std::uint32_t>::max(), FamilyTreeError::kResourceLimit);
    std::vector<FamilyMember> trees;
    Reserve(trees, family.members.size());
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      member_ = {static_cast<std::uint32_t>(i)};
      default_candidates_.reset();
      defaults_proved_ = false;
      Step(1);
      auto& member = family.members[i];
      std::optional<FunctionContext> child_context;
      if (i != 0) {
        Require(member.context.has_value() && member.incoming.has_value(), FamilyTreeError::kContext);
        child_context.emplace(std::move(*member.context));
      } else Require(!member.incoming && !member.context, FamilyTreeError::kContext);
      const auto& context = child_context ? *child_context : root;
      const auto& function = member.function;
      const auto& graph = function.values.graph;
      Require(context.Identity() == function.input.Identity() &&
          context.Target() == function.input.Method(), FamilyTreeError::kContext);
      if (context.Length() < context.Frame().parameters && function.values.cells) {
        auto parameters = Take<ParameterPlan, ParameterPlanError>(BuildParameterPlan(
            graph, context.Frame().parameters, left_));
        Account(parameters);
        auto defaults = Take<CellDefaults, CellDefaultError>(FindCellDefaults(
            graph, *function.values.cells, parameters, context.Length(), left_));
        Account(defaults);
        default_candidates_ = defaults.candidates.size();
        if (!defaults.candidates.empty()) {
          Require(function.values.environments.has_value(), FamilyTreeError::kContext);
          auto proof = Take<CellDefaultProof, CellDefaultProofError>(CellDefaultProof::Build(graph,
              *function.values.environments, *function.values.cells, parameters, defaults, context.Length(), left_));
          Admit(proof.AllocatedBytes(), 1); Step(proof.Work());
          defaults_proved_ = true;
        }
        // Keep the ordinary tree/ABI gate intact until independent cell escape
        // proof, residual projection and whole-family rebinding are connected.
      }
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
      auto flow = Take<TreeFlow, TreeFlowError>(VerifyTreeFlow(graph, control, evaluation, constructed.tree, left_));
      Account(flow);
      if (aliases) Step(Take<std::uint64_t, ModuleAliasTreeError>(
          VerifyModuleAliasTree(*aliases, evaluation, constructed.tree, left_)));
      auto abi = Take<ConstructedAbi, FunctionAbiError>(BuildFunctionAbi(graph, constructed.tree,
          context.Frame(), context.Length(), left_));
      Account(abi);
      std::optional<CaptureCells> cells;
      if (member.incoming) cells.emplace(std::move(member.incoming->cells));
      trees.push_back({std::move(constructed.tree), std::move(flow), std::move(cells)});
    }
    auto result = Take<ConstructedFamily, FamilyError>(BuildFunctionFamily(std::move(trees), {FileFamily::kRoot.value}, left_));
    Account(result);
    result.allocated_bytes = initial_.bytes - left_.bytes;
    result.work = initial_.work - left_.work;
    return result;
  }
 private:
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
