#include "family_value_inspect.h"
#include "tree_position_json.h"

#include <nlohmann/json.hpp>
#include <type_traits>
#include <memory>
#include <ostream>
#include <streambuf>

#include "../src/source/family_file.h"
#include "../src/source/family_tree.h"
#include "../src/source/family_print.h"

namespace {
using namespace abcd;
using namespace abcd::source;
// Diagnostic output is bounded independently of the engine's publication path.
// A failed print is discarded in full; no partial source enters the report.
constexpr std::size_t kDraftBytes = 1024 * 1024;
class DraftBuffer final : public std::streambuf {
 public:
  DraftBuffer() : bytes_(std::make_unique<char[]>(kDraftBytes)) {
    setp(bytes_.get(), bytes_.get() + kDraftBytes);
  }
  std::string_view Text() const { return {pbase(), static_cast<std::size_t>(pptr() - pbase())}; }
 private:
  std::unique_ptr<char[]> bytes_;
};
void Draft(nlohmann::json& report, const ConstructedFamily& family,
           const FunctionContext& context, analysis::SemanticBudget budget, std::ostream& positions) {
  report["draft_prepared"] = false;
  // Reserve both the fixed stream storage and the final JSON string copy.
  constexpr std::size_t kOutputStorage = 2 * kDraftBytes;
  if (family.allocated_bytes > budget.bytes || kOutputStorage > budget.bytes - family.allocated_bytes) {
    report["draft_error"] = static_cast<unsigned>(TreePrintError::kResourceLimit); return;
  }
  if (family.work > budget.work) {
    report["draft_error"] = static_cast<unsigned>(TreePrintError::kWorkLimit); return;
  }
  budget.bytes -= family.allocated_bytes + kOutputStorage; budget.work -= family.work;
  DraftBuffer buffer;
  std::ostream output(&buffer);
  const auto* member = context.ClassContext();
  const auto printed = member ? PrintClassFamilyDraft(family, *member, output, {kDraftBytes}, budget)
                              : PrintFamilyDraft(family, output, {kDraftBytes}, budget);
  const auto* draft = std::get_if<PrintedFamilyDraft>(&printed);
  if (!draft) {
    report["draft_error"] = static_cast<unsigned>(std::get<TreePrintError>(printed)); return;
  }
  report["draft_prepared"] = true;
  report["draft_source"] = buffer.Text();
  // Stream the shared DAG once per owner. Expanding combined origins per node
  // can be quadratic; building a second JSON DOM needlessly duplicates arenas.
  positions << "\"draft_positions\":[";
  for (std::size_t i = 0; i < draft->members.size(); ++i) {
    const auto& member = draft->members[i];
    if (i) positions << ',';
    positions << "{\"owner\":" << family.members[i].tree.owner.value
              << ",\"method\":" << member.method.value
              << ",\"begin\":" << draft->functions[i].begin
              << ",\"end\":" << draft->functions[i].end << ",\"positions\":";
    const auto& sources = family.members[i].sources;
    abcd::tools::WriteTreePositions(positions, family.members[i].tree, member,
        sources ? &*sources : nullptr);
    positions << '}';
  }
  positions << "],";
  report["draft_coordinates"] = "absolute-utf8-bytes";
  report["draft_root_fragment"] = member ? "class-member" : "function-expression";
  report["draft_publication_pending"] = {"root-enclosure", "global-module-bindings", "transitive-premises"};
  std::size_t named = 0;
  for (const auto& member : family.members) named += member.tree.inferred_name.has_value();
  report["draft_named_members"] = named;
  if (named + (member ? 1U : 0U) != family.members.size()) report["draft_publication_pending"].push_back("callable-names");
}
template <class Error> unsigned Code(const Error& error) {
  if constexpr (std::is_enum_v<Error>) return static_cast<unsigned>(error);
  else if constexpr (requires { error.kind; }) return static_cast<unsigned>(error.kind);
  else if constexpr (requires { error.error; }) return std::visit([](const auto& nested) { return Code(nested); }, error.error);
  else if constexpr (requires { error.cause; }) return std::visit([](const auto& nested) { return Code(nested); }, error.cause);
  else return std::visit([](const auto& nested) { return Code(nested); }, error);
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
    } else if constexpr (std::is_same_v<T, PhasedFileFailure>) {
      report["phase_input_failure"] = error.index(); report["phase_input_error"] = Code(error);
    } else if constexpr (std::is_same_v<T, PhasedCreationFailure>) {
      report["phase_creation_failure"] = error.cause.index(); report["phase_creation_error"] = Code(error);
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
    abcd::source::RouteInputIdentity identity, const abcd::source::RecordModuleContext* modules,
    std::ostream& output, FamilyInspectMode mode) {
  nlohmann::json report{{"family_prepared", false}};
  if (root.value >= index.methods.size()) {
    report["family_error"] = Code(FamilyFileError::kMethod); return report;
  }
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
  // Resolve the actual occurrence before decoding: phase inputs and receiver
  // proofs need its ABI/owner stamp. Ambiguous owners remain diagnostic-only.
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
  report["family_root_context"] = context.has_value();
  if (context) {
    if (context->AllocatedBytes() > budget.bytes || context->Work() > budget.work) return report;
    budget.bytes -= context->AllocatedBytes(); budget.work -= context->Work();
  }
  auto prepared = ReadFunctionFamily(file, header, index, lookup, *roles, *sites, root, identity,
      budget, modules, context ? &*context : nullptr);
  auto* family = std::get_if<FileFamily>(&prepared);
  if (!family) { Failure(report, std::get<FamilyFileFailure>(prepared)); return report; }
  report["family_prepared"] = true;
  report["family_members"] = nlohmann::json::array();
  for (std::size_t i = 0; i < family->members.size(); ++i) {
    const auto& member = family->members[i];
    nlohmann::json entry{{"owner", i}, {"method", member.function.Method().value},
        {"operations", member.function.Graph().ops.size()},
        {"child_obligations", member.function.Children().size()}};
    if (!member.function.Phase()) entry["capture_occurrences"] = member.function.Normal().input.Captures().size();
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
  // Ambiguous/unproved root owners are not guessed from method attributes.
  if (!context) return report;
  const auto constructed = BuildFileFamilyTrees(file, header, index, std::move(*family), *context, budget, modules, {}, &lookup);
  if (const auto* tree = std::get_if<ConstructedFamily>(&constructed)) {
    report["family_tree_prepared"] = true;
    report["family_tree_members"] = tree->members.size();
    if (tree->writes) report["family_writes"] = {
        {"stores", tree->writes->stores}, {"captured_stores", tree->writes->captured_stores},
        {"signature_stores", tree->writes->signature_stores}, {"creations", tree->writes->creations},
        {"primitive_objects", tree->writes->primitive_objects}};
    report["family_tree_expressions"] = nlohmann::json::array();
    report["family_tree_signatures"] = nlohmann::json::array();
    for (const auto& member : tree->members) {
      report["family_tree_expressions"].push_back(member.tree.expressions.size());
      nlohmann::json signature{{"method", member.tree.method.value},
          {"parameters", member.tree.parameters.size()}, {"defaults", nlohmann::json::array()},
          {"signature_pcs", nlohmann::json::array()}};
      for (std::size_t i = 0; i < member.tree.parameters.size(); ++i)
        if (member.tree.parameters[i].initializer) signature["defaults"].push_back(i);
      for (const auto& placement : member.tree.signature) {
        if (const auto* original = std::get_if<OriginalSite>(&member.tree.origins[placement.origin.value]))
          signature["signature_pcs"].push_back(original->pc.value);
      }
      report["family_tree_signatures"].push_back(std::move(signature));
    }
    if (mode == FamilyInspectMode::kSourceDraft) Draft(report, *tree, *context, budget, output);
  } else {
    const auto& failure = std::get<FamilyTreeFailure>(constructed);
    report["family_tree_member"] = failure.member.value;
    report["family_tree_failure"] = failure.cause.index();
    if (failure.cell_default_candidates)
      report["cell_default_candidates"] = *failure.cell_default_candidates;
    report["cell_defaults_proved"] = failure.cell_defaults_proved;
    std::visit([&](const auto& error) {
      using T = std::decay_t<decltype(error)>;
      if constexpr (std::is_same_v<T, CaptureEvaluationFailure>) {
        report["capture_failure"] = error.index();
        std::visit([&](const auto& reason) { report["capture_error"] = Code(reason); }, error);
      } else if constexpr (std::is_same_v<T, SyntaxResolutionFailure>) {
        report["syntax_failure"] = error.index();
        std::visit([&](const auto& reason) { report["syntax_error"] = Code(reason); }, error);
      } else if constexpr (std::is_same_v<T, ModuleAliasFailure>) {
        report["module_alias_failure"] = error.cause.index();
      } else if constexpr (std::is_same_v<T, FamilyEnclosureFailure>) {
        report["family_tree_error"] = Code(error);
        if (error.name) report["enclosure_name"] = error.name->value;
      } else if constexpr (std::is_same_v<T, FamilyWriteFailure>) {
        report["family_tree_error"] = Code(error);
        if (error.pc) report["pc"] = error.pc->value;
      } else report["family_tree_error"] = Code(error);
    }, failure.cause);
  }
  return report;
}
