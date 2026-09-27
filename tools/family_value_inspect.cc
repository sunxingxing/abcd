#include "family_value_inspect.h"

#include <nlohmann/json.hpp>
#include <type_traits>

#include "../src/source/family_file.h"
#include "../src/source/family_tree.h"

namespace {
using namespace abcd;
using namespace abcd::source;
template <class Error> unsigned Code(const Error& error) {
  if constexpr (std::is_enum_v<Error>) return static_cast<unsigned>(error);
  else return static_cast<unsigned>(error.kind);
}
void Failure(nlohmann::json& report, const FamilyFileFailure& failure) {
  report["family_failure"] = failure.cause.index();
  if (failure.method) report["failed_method"] = failure.method->value;
  if (failure.parent) report["creator"] = failure.parent->value;
  if (failure.creation_pc) report["creation_pc"] = failure.creation_pc->value;
  std::visit([&](const auto& error) {
    using T = std::decay_t<decltype(error)>;
    if constexpr (std::is_same_v<T, FamilyFileError> || std::is_same_v<T, CaptureCellError>) {
      report["family_error"] = Code(error);
    } else if constexpr (std::is_same_v<T, CreationContextFailure>) {
      report["context_failure"] = error.error.index();
      std::visit([&](const auto& reason) { report["context_error"] = Code(reason); }, error.error);
    } else if constexpr (std::is_same_v<T, CaptureEvaluationFailure>) {
      report["capture_failure"] = error.index();
      std::visit([&](const auto& reason) { report["capture_error"] = Code(reason); }, error);
    } else if constexpr (std::is_same_v<T, BodyEmitFailure>) {
      report["object_failure"] = Code(error.kind);
      if (error.object_error) report["object_error"] = Code(*error.object_error);
      if (error.metadata_error) report["metadata_error"] = Code(*error.metadata_error);
      if (error.literal_error) report["literal_error"] = Code(*error.literal_error);
    } else {
      static_assert(std::is_same_v<T, FunctionFileFailure>);
      report["method_failure"] = error.index();
      std::visit([&](const auto& reason) {
        using R = std::decay_t<decltype(reason)>;
        if constexpr (std::is_same_v<R, FunctionInputFailure>) {
          report["input_failure"] = reason.cause.index();
          std::visit([&](const auto& input) {
            report["input_error"] = Code(input);
            if constexpr (!std::is_same_v<std::decay_t<decltype(input)>, FunctionInputError>) report["pc"] = input.pc.value;
          }, reason.cause);
        } else if constexpr (std::is_same_v<R, FunctionValueFailure>) {
          report["value_error"] = Code(reason.cause.reason);
          if (reason.cause.pc) report["pc"] = reason.cause.pc->value;
        } else if constexpr (std::is_same_v<R, ModuleReadFailure>) {
          report["module_read_failure"] = reason.reason.index();
          std::visit([&](const auto& module) { report["module_read_error"] = Code(module); }, reason.reason);
          if (reason.pc) report["pc"] = reason.pc->value;
        } else report["method_error"] = Code(reason);
      }, error);
    }
  }, failure.cause);
}
}  // namespace
nlohmann::json InspectFamilyValues(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodLookup& lookup, abcd::abc::MethodPosition root,
    abcd::source::RouteInputIdentity identity, const abcd::source::RecordModuleContext* modules) {
  nlohmann::json report{{"family_prepared", false}};
  analysis::SemanticBudget budget;
  const auto indexed_roles = analysis::MethodRoleIndex::Build(file, header, index, lookup, {budget.bytes});
  const auto* roles = std::get_if<analysis::MethodRoleIndex>(&indexed_roles);
  if (!roles) {
    const auto& error = std::get<analysis::MethodRoleFailure>(indexed_roles).error;
    report["roles_failure"] = error.index();
    std::visit([&](const auto& reason) { report["roles_error"] = Code(reason); }, error);
    return report;
  }
  if (roles->AllocatedBytes() > budget.bytes) {
    report["family_error"] = Code(FamilyFileError::kResourceLimit); return report;
  }
  budget.bytes -= roles->AllocatedBytes();
  const auto indexed_sites = analysis::FunctionCreationSites::Build(file, header, index, lookup, budget);
  const auto* sites = std::get_if<analysis::FunctionCreationSites>(&indexed_sites);
  if (!sites) {
    const auto& error = std::get<analysis::FunctionSiteFailure>(indexed_sites).error;
    report["sites_failure"] = error.index();
    std::visit([&](const auto& reason) { report["sites_error"] = Code(reason); }, error);
    return report;
  }
  if (sites->AllocatedBytes() > budget.bytes || sites->Work() > budget.work) {
    report["family_error"] = Code(sites->AllocatedBytes() > budget.bytes
        ? FamilyFileError::kResourceLimit : FamilyFileError::kWorkLimit); return report;
  }
  budget.bytes -= sites->AllocatedBytes(); budget.work -= sites->Work();
  auto prepared = ReadFunctionFamily(file, header, index, lookup, *roles, *sites, root, identity, budget, modules);
  auto* family = std::get_if<FileFamily>(&prepared);
  if (!family) { Failure(report, std::get<FamilyFileFailure>(prepared)); return report; }
  report["family_prepared"] = true;
  report["family_members"] = nlohmann::json::array();
  for (std::size_t i = 0; i < family->members.size(); ++i) {
    const auto& member = family->members[i];
    nlohmann::json entry{{"owner", i}, {"method", member.function.input.Method().value},
        {"operations", member.function.values.graph.ops.size()},
        {"capture_occurrences", member.function.input.Captures().size()},
        {"child_obligations", member.function.input.Children().size()}};
    entry["primitive_objects"] = member.objects.size();
    if (member.context) entry["callable"] = static_cast<unsigned>(member.context->Callable());
    if (member.incoming) {
      entry["parent"] = member.incoming->parent.value;
      entry["creation_pc"] = member.incoming->creation.offset.value;
      entry["bound_cells"] = member.incoming->cells.targets.size();
    }
    entry["children"] = nlohmann::json::array();
    for (const auto& child : member.children)
      entry["children"].push_back({{"operation", child.creation.value}, {"owner", child.child.value}, {"captures", child.captures.size()}});
    report["family_members"].push_back(std::move(entry));
  }
  report["family_tree_prepared"] = false;
  if (family->allocated_bytes > budget.bytes || family->work > budget.work) return report;
  budget.bytes -= family->allocated_bytes; budget.work -= family->work;
  std::optional<FunctionContext> context;
  const auto occurrences = sites->Find(index.methods[root.value].offset);
  if (occurrences.count == 1) {
    auto resolved = CreationTreeContext::Resolve(file, header, index, lookup, *roles, *sites,
        occurrences.first, identity, budget);
    if (auto* created = std::get_if<CreationTreeContext>(&resolved)) context.emplace(std::move(*created));
  } else if (occurrences.count == 0) {
    auto indexed = analysis::ClassMethodSites::Build(file, header, index, lookup, budget);
    if (const auto* classes = std::get_if<analysis::ClassMethodSites>(&indexed)) {
      if (classes->AllocatedBytes() > budget.bytes || classes->Work() > budget.work) return report;
      budget.bytes -= classes->AllocatedBytes(); budget.work -= classes->Work();
      const auto range = classes->Find(index.methods[root.value].offset);
      if (range.count == 1) {
        auto resolved = ClassTreeContext::Resolve(file, header, index, lookup, *roles, *classes,
            range.first, identity, budget);
        if (auto* method = std::get_if<ClassTreeContext>(&resolved)) context.emplace(std::move(*method));
      }
    }
  }
  // Ambiguous/unproved root owners are not guessed from method attributes.
  report["family_root_context"] = context.has_value();
  if (!context) return report;
  if (context->AllocatedBytes() > budget.bytes || context->Work() > budget.work) return report;
  budget.bytes -= context->AllocatedBytes(); budget.work -= context->Work();
  const auto constructed = BuildFileFamilyTrees(file, header, index, std::move(*family), *context, budget, modules);
  if (const auto* tree = std::get_if<ConstructedFamily>(&constructed)) {
    report["family_tree_prepared"] = true;
    report["family_tree_members"] = tree->members.size();
    report["family_tree_expressions"] = nlohmann::json::array();
    for (const auto& member : tree->members)
      report["family_tree_expressions"].push_back(member.tree.expressions.size());
  } else {
    const auto& failure = std::get<FamilyTreeFailure>(constructed);
    report["family_tree_member"] = failure.member.value;
    report["family_tree_failure"] = failure.cause.index();
    if (failure.cell_default_candidates)
      report["cell_default_candidates"] = *failure.cell_default_candidates;
    report["cell_defaults_proved"] = failure.cell_defaults_proved;
    std::visit([&](const auto& error) {
      using T = std::decay_t<decltype(error)>;
      if constexpr (std::is_same_v<T, SyntaxResolutionFailure>) {
        report["syntax_failure"] = error.index();
        std::visit([&](const auto& reason) { report["syntax_error"] = Code(reason); }, error);
      } else if constexpr (std::is_same_v<T, ModuleAliasFailure>) {
        report["module_alias_failure"] = error.cause.index();
      } else report["family_tree_error"] = Code(error);
    }, failure.cause);
  }
  return report;
}
