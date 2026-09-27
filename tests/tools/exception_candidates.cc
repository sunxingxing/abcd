#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/analysis/value_environment.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/function_emission.h"
#include "../../tools/tree_position_json.h"
#include "../../src/source/phased_method.h"
#include "../../src/source/phased_creations.h"
#include "../../src/source/phased_capture_input.h"
#include "../../src/source/phased_capture_cells.h"
#include "../../src/source/phased_capture_evaluation.h"
#include "../../src/source/module_aliases.h"
#include "../../src/source/syntax_resolution.h"
#include "exception_family_probe.h"

namespace {
int Inspect(const abcd::corpus::AbcFile& file, const abcd::abc::Header& header,
    const abcd::abc::MetadataIndex& index, const abcd::abc::MethodLookup& lookup,
    const abcd::analysis::ClassMethodSites& sites, const abcd::corpus::FileFingerprint& hash,
    abcd::abc::EntityOffset entity, bool emit, abcd::analysis::SemanticBudget budget) {
  using namespace abcd;
  using namespace abcd::source;
  const auto position = lookup.Find(entity);
  if (!position || sites.Find(entity).count != 1) return 1;
  const auto& method = index.methods[position->value];
  const auto owner = std::find_if(index.records.begin(), index.records.end(),
      [&](const auto& record) { return record.offset == method.owner_offset; });
  if (owner == index.records.end() || !method.code) return 1;
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  const auto* packed = std::get_if<abc::FilePackaging>(&packaging);
  if (!packed) return 1;
  const auto modules = ReadRecordModuleContext(file, header, index, *owner, *packed);
  const auto* record = std::get_if<RecordModuleContext>(&modules);
  if (!record) return 1;
  const auto roles = analysis::MethodRoleIndex::Build(file, header, index, lookup);
  const auto* indexed_roles = std::get_if<analysis::MethodRoleIndex>(&roles);
  if (!indexed_roles) return 1;
  auto selected = ClassTreeContext::Resolve(file, header, index, lookup, *indexed_roles,
      sites, sites.Find(entity).first, MakeRouteInputIdentity(hash, header));
  const auto* ready = std::get_if<ClassTreeContext>(&selected);
  if (!ready) return 1;
  const FunctionContext context{std::move(std::get<ClassTreeContext>(selected))};
  const auto decoded = abc::DecodeMethodBody(file, header.profile, *method.code);
  const auto* body = std::get_if<abc::MethodBody>(&decoded);
  if (!body) return 1;
  // A single explicitly selected method, not record publication or a corpus
  // scan. Its module owner is local to this diagnostic invocation.
  constexpr ModuleOwnerId kSelectedRecord{0};
  if (emit) {
    const auto identity = MakeRouteInputIdentity(hash, header);
    auto planned = ModuleEnclosure::Build(file, header, *record, identity, kSelectedRecord);
    const auto* enclosure = std::get_if<ModuleEnclosure>(&planned);
    if (!enclosure) { std::cerr << "module enclosure error " << static_cast<unsigned>(std::get<ModuleEnclosureError>(planned)) << '\n'; return 1; }
    GlobalScope scope{}; scope.module_enclosure = enclosure;
    auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup, *indexed_roles,
        sites, sites.Find(entity).first, identity, budget, &scope);
    if (const auto* error = std::get_if<FunctionPrepareFailure>(&prepared)) {
      std::cerr << "preparation error " << static_cast<unsigned>(error->kind) << '\n'; return 1;
    }
    std::ostringstream output, imports, mapping;
    if (!std::holds_alternative<std::size_t>(enclosure->WriteImports(file, header, imports))) return 1;
    auto printed = FunctionEmission::Write(std::get<VerifiedFunction>(std::move(prepared)), output, {}, {}, budget, &scope);
    const auto* emission = std::get_if<FunctionEmission>(&printed);
    if (!emission) { std::cerr << "print error " << static_cast<unsigned>(std::get<TreePrintError>(printed)) << '\n'; return 1; }
    tools::WriteTreePositions(mapping, emission->Function().Tree(), emission->Positions());
    std::cout << nlohmann::json{{"method", entity.value}, {"record_complete", false},
        {"fragment_complete", emission->Receipt().coverage == Coverage::kComplete},
        {"work_limit", budget.work},
        {"source", output.str()}, {"imports", imports.str()},
        {"positions", nlohmann::json::parse(mapping.str())}} << '\n';
    return file.CheckUnchanged() ? 1 : 0;
  }
  const PhasedModuleBinding binding{file, header, index, *record, kSelectedRecord};
  nlohmann::json report{{"method", entity.value}, {"source_complete", false}, {"executable", false}};
  auto staged = PhasedBody::Build(context, *body, budget, &binding);
  if (const auto* error = std::get_if<PhasedMethodError>(&staged)) {
    report["phase_error"] = static_cast<unsigned>(*error);
    std::cout << report << '\n';
    return file.CheckUnchanged() ? 1 : 0;
  }
  report["phase_body_ready"] = true;
  const auto* cells = std::get<PhasedBody>(staged).Cells();
  report["phase_cells_ready"] = cells != nullptr;
  if (cells) report["cells"] = cells->cells.size();
  // Retained production topology, not a separately budgeted diagnostic proof.
  if (const auto* ready = std::get<PhasedBody>(staged).CellEnvironments()) {
    report["phase_environments_ready"] = true;
    report["environment_allocations"] = ready->flow.allocations.size();
    report["lexical_bindings"] = ready->flow.bindings.size();
  }
  report["creations"] = nlohmann::json::array();
  auto creation_sites = analysis::FunctionCreationSites::Build(file, header, index, lookup);
  const auto* occurrences = std::get_if<analysis::FunctionCreationSites>(&creation_sites);
  if (!occurrences) return 1;
  auto bound_creations = BindPhasedCreations(file, header, index, lookup, *indexed_roles,
      *occurrences, std::get<PhasedBody>(staged), *body);
  auto* children = std::get_if<PhasedCreations>(&bound_creations);
  if (!children) {
    report["creation_error"] = std::get<PhasedCreationFailure>(bound_creations).cause.index();
    std::cout << report << '\n'; return file.CheckUnchanged() ? 1 : 0;
  }
  const auto original = std::get<PhasedBody>(staged).TreeInput(*body).input;
  auto capture_locals = analysis::ProveExceptionLocals(original.flow, original.environments,
      budget, {}, analysis::ClosureWrites::kLanguage);
  const auto* local_scopes = std::get_if<analysis::ExceptionLocals>(&capture_locals);
  std::vector<FunctionBinding> function_bindings;
  std::vector<test::ExceptionChild> tree_children;
  function_bindings.reserve(children->sites.size());
  for (auto& child : children->sites) {
    report["creations"].push_back({{"operation", child.operation.value}, {"pc", child.binding.offset.value},
        {"instruction", child.instruction.value},
        {"target", child.context.Site().target.value}});
    if (!local_scopes) continue;
    FunctionContext child_owner{std::move(child.context)};
    const auto& child_method = index.methods[child_owner.Position().value];
    if (!child_method.code) continue;
    auto decoded_child = abc::DecodeMethodBody(file, header.profile, *child_method.code);
    auto* child_body = std::get_if<abc::MethodBody>(&decoded_child);
    if (!child_body) continue;
    auto child_input = FunctionInput::Build(context.Identity(), child_owner.Target(),
        child_method.owner_offset, *child_body, child_owner.Frame(), InputCaptureMode::kConditional,
        budget, &child_owner);
    auto* ready_child = std::get_if<FunctionInput>(&child_input);
    if (!ready_child) continue;
    const auto captures = BindPhasedFunctionInputs(index, lookup, std::get<PhasedBody>(staged), *body,
        *local_scopes, child, *ready_child, child_owner, budget);
    if (const auto* plan = std::get_if<CapturePlan>(&captures)) {
      auto exact = budget; exact.work = plan->work;
      if (!std::holds_alternative<CapturePlan>(BindPhasedFunctionInputs(index, lookup,
          std::get<PhasedBody>(staged), *body, *local_scopes, child, *ready_child, child_owner, exact))) return 1;
      --exact.work;
      const auto limited = BindPhasedFunctionInputs(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, *ready_child, child_owner, exact);
      if (!std::holds_alternative<CaptureError>(limited) ||
          std::get<CaptureError>(limited) != CaptureError::kWorkLimit) return 1;
      exact = budget; exact.bytes = 0;
      const auto exhausted = BindPhasedFunctionInputs(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, *ready_child, child_owner, exact);
      if (!std::holds_alternative<CaptureError>(exhausted) ||
          std::get<CaptureError>(exhausted) != CaptureError::kResourceLimit) return 1;
      const auto saved = child.instruction;
      child.instruction = analysis::InstructionId{static_cast<std::uint32_t>(original.flow.instructions.size())};
      const auto invalid = BindPhasedFunctionInputs(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, *ready_child, child_owner, budget);
      child.instruction = saved;
      if (!std::holds_alternative<CaptureError>(invalid) ||
          std::get<CaptureError>(invalid) != CaptureError::kCreation) return 1;
      const auto offset = child.binding.offset;
      ++child.binding.offset.value;
      const auto wrong_owner = BindPhasedFunctionInputs(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, *ready_child, child_owner, budget);
      child.binding.offset = offset;
      if (!std::holds_alternative<InputBindingError>(wrong_owner) ||
          std::get<InputBindingError>(wrong_owner) != InputBindingError::kTarget) return 1;
      report["creations"].back()["capture_inputs_ready"] = true;
      report["creations"].back()["capture_occurrences"] = plan->bindings.size();
      report["creations"].back()["conditional_writes"] = plan->writes == analysis::ClosureWrites::kLanguage;
      auto child_values = BuildFunctionValues(*ready_child, *child_body, child_owner.Frame(), budget);
      auto* values = std::get_if<FunctionValues>(&child_values);
      if (!values) continue;
      auto cells = BindPhasedCaptureCells(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, {*ready_child, *values}, child_owner, budget);
      auto* mapped = std::get_if<CaptureCells>(&cells);
      if (!mapped) continue;
      exact = budget; exact.work = mapped->work;
      if (!std::holds_alternative<CaptureCells>(BindPhasedCaptureCells(index, lookup, std::get<PhasedBody>(staged),
          *body, *local_scopes, child, {*ready_child, *values}, child_owner, exact))) return 1;
      --exact.work;
      const auto cells_limited = BindPhasedCaptureCells(index, lookup, std::get<PhasedBody>(staged),
          *body, *local_scopes, child, {*ready_child, *values}, child_owner, exact);
      if (!std::holds_alternative<CaptureCellError>(cells_limited) ||
          std::get<CaptureCellError>(cells_limited) != CaptureCellError::kWorkLimit) return 1;
      const auto verify = [&] { return VerifyPhasedCaptureCells(index, lookup, std::get<PhasedBody>(staged),
          *body, *local_scopes, child, {*ready_child, *values}, child_owner, *mapped, budget); };
      if (!std::holds_alternative<std::uint64_t>(verify())) return 1;
      const auto revision = mapped->parent_revision; ++mapped->parent_revision.value;
      if (std::get<CaptureCellError>(verify()) != CaptureCellError::kRevision) return 1;
      mapped->parent_revision = revision;
      for (auto& target : mapped->targets) {
        const auto saved_target = target.target;
        target.target = OuterCell{{analysis::CapturedDepth{0}, analysis::LexicalSlot{0}}};
        if (std::get<CaptureCellError>(verify()) != CaptureCellError::kBinding) return 1;
        target.target = saved_target;
      }
      if (!std::holds_alternative<std::uint64_t>(verify())) return 1;
      report["creations"].back()["capture_cells_ready"] = true;
      report["creations"].back()["parent_cell_targets"] = std::count_if(mapped->targets.begin(), mapped->targets.end(),
          [](const auto& target) { return std::holds_alternative<ParentCell>(target.target); });
      constexpr FunctionOwnerId kParentOwner{0};
      const FunctionOwnerId child_id{static_cast<std::uint32_t>(report["creations"].size())};
      auto evaluated = BindPhasedCaptureEvaluation(index, lookup, std::get<PhasedBody>(staged), *body,
          *local_scopes, child, {*ready_child, *values}, child_owner, *mapped, kParentOwner, child_id, budget);
      if (auto* ready_binding = std::get_if<CapturedFunctionBinding>(&evaluated)) {
        if (ready_binding->binding.creation != child.operation || ready_binding->binding.captures.size() != mapped->targets.size()) return 1;
        exact = budget; exact.work = ready_binding->work;
        if (!std::holds_alternative<CapturedFunctionBinding>(BindPhasedCaptureEvaluation(index, lookup,
            std::get<PhasedBody>(staged), *body, *local_scopes, child, {*ready_child, *values}, child_owner,
            *mapped, kParentOwner, child_id, exact))) return 1;
        --exact.work;
        const auto evaluation_limited = BindPhasedCaptureEvaluation(index, lookup, std::get<PhasedBody>(staged),
            *body, *local_scopes, child, {*ready_child, *values}, child_owner, *mapped, kParentOwner, child_id, exact);
        const auto* work_failure = std::get_if<CaptureEvaluationFailure>(&evaluation_limited);
        if (!work_failure || !std::visit([](auto error) { return error == decltype(error)::kWorkLimit; }, *work_failure)) return 1;
        const auto same_owner = BindPhasedCaptureEvaluation(index, lookup, std::get<PhasedBody>(staged), *body,
            *local_scopes, child, {*ready_child, *values}, child_owner, *mapped, kParentOwner, kParentOwner, budget);
        const auto* failure = std::get_if<CaptureEvaluationFailure>(&same_owner);
        if (!failure || !std::holds_alternative<CaptureEvaluationError>(*failure) ||
            std::get<CaptureEvaluationError>(*failure) != CaptureEvaluationError::kOwner) return 1;
        report["creations"].back()["capture_evaluation_ready"] = true;
        function_bindings.push_back(std::move(ready_binding->binding));
        tree_children.push_back({std::move(child_owner), std::move(*child_body),
            std::move(*ready_child), std::move(*values), std::move(*mapped), child});
      }
    }
  }
  // Conditional common evaluation, never a family/source receipt. The probe
  // below checks actual trees/enclosure; transitive writes still gate publication.
  const auto& parent_body = std::get<PhasedBody>(staged);
  if (!function_bindings.empty() && function_bindings.size() == children->sites.size() &&
      parent_body.AllocatedBytes() <= budget.bytes && parent_body.Work() <= budget.work) {
    const auto* aliases = parent_body.Modules();
    const auto evaluation = BuildPhasedEvaluationPlan(parent_body.Graph(),
        {budget.bytes - parent_body.AllocatedBytes(), budget.work - parent_body.Work()},
        IncomingCells::kReject, function_bindings,
        aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{});
    if (const auto* ready = std::get_if<EvaluationPlan>(&evaluation)) {
      report["conditional_evaluation_ready"] = std::holds_alternative<std::uint64_t>(
          VerifyPhasedEvaluationPlan(parent_body.Graph(), *ready, budget));
      const auto probe = test::ProbeExceptionFamily(file, header, index, *record, kSelectedRecord,
          context, *body, parent_body, *ready, std::move(tree_children), lookup, *local_scopes, budget);
      report["conditional_parent_tree_ready"] = probe.parent_tree;
      report["conditional_child_trees"] = probe.child_trees;
      report["conditional_family_layout_ready"] = probe.layout;
      report["conditional_family_enclosure_ready"] = probe.enclosure;
      report["rejected_capture_forgery"] = probe.rejected_capture_forgery;
      report["checked_store_methods"] = probe.checked_store_methods;
      report["checked_lexical_stores"] = probe.checked_lexical_stores;
      report["family_writes_ready"] = probe.family_writes;
      report["family_write_creations"] = probe.write_creations;
      report["rejected_family_write_forgery"] = probe.rejected_write_forgery;
      report["family_abi_methods"] = probe.abi_methods;
      report["family_source_methods"] = probe.source_methods;
      report["rejected_source_forgery"] = probe.rejected_source_forgery;
      report["family_mapped_instructions"] = probe.mapped_instructions;
      if (probe.draft) report["conditional_family_source"] = *probe.draft;
      report["family_probe_stage"] = static_cast<unsigned>(probe.stage);
      if (probe.tree_error) report["family_probe_tree_error"] = static_cast<unsigned>(*probe.tree_error);
    }
  }
  const auto built = PhasedMethod::Evaluate(std::get<PhasedBody>(std::move(staged)), budget);
  if (const auto* error = std::get_if<PhasedMethodError>(&built)) {
    report["phase_error"] = static_cast<unsigned>(*error);
  } else {
    const auto& phased = std::get<PhasedMethod>(built);
    report["phased_ready"] = true;
    report["module_aliases"] = phased.Modules() ? phased.Modules()->aliases.size() : 0;
    auto resolved = ResolveFunctionSyntax(file, header, index, context, *body, phased.Graph(), {0});
    if (auto* syntax = std::get_if<ResolvedSyntax>(&resolved)) {
      if (phased.Modules()) {
        const auto first = static_cast<std::uint32_t>(syntax->syntax.names.size());
        syntax->syntax.names.insert(syntax->syntax.names.end(), phased.Modules()->names.begin(), phased.Modules()->names.end());
        syntax->syntax.modules = phased.Modules()->aliases;
        for (auto& alias : syntax->syntax.modules) alias.name.value += first;
      }
      const auto constants = syntax->View();
      const auto tree = BuildExceptionFunctionDraft(phased.Graph(), phased.TreeInput(*body),
          phased.Evaluation(), std::move(syntax->syntax), constants, budget);
      if (const auto* error = std::get_if<FunctionTreeError>(&tree)) report["tree_error"] = static_cast<unsigned>(*error);
      else {
        const auto& draft = std::get<ExceptionFunctionDraft>(tree);
        if (phased.Modules()) {
          const auto verified = VerifyModuleAliasTree(*phased.Modules(), phased.Evaluation(), draft.tree);
          if (const auto* error = std::get_if<ModuleAliasTreeError>(&verified))
            report["module_tree_error"] = static_cast<unsigned>(*error);
          else report["tree_ready"] = true;
        } else report["tree_ready"] = true;
      }
    } else report["syntax_error"] = std::get<SyntaxResolutionFailure>(resolved).index();
  }
  std::cout << report << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
}  // namespace

// Candidate discovery or one explicit method's phase diagnostic. Not a corpus
// decompilation/support census. No source/sidecar inputs are read.
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc < 2 || argc > 6 || argc == 5 ||
      (argc >= 4 && std::string_view(argv[3]) != "--emit")) return 1;
  analysis::SemanticBudget budget;
  if (argc == 6) {
    if (std::string_view(argv[4]) != "--work") return 1;
    const std::string_view text{argv[5]};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), budget.work);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !budget.work) return 1;
  }
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto* owner = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owner) return 1;
  const auto& file = **owner;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return 1;
  const auto parsed = abc::ParseHeader(std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->kind != abc::AbcKind::kDynamic || header->checksum != hash->adler32) return 1;
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return 1;
  const auto lookup = abc::MethodLookup::Build(index->methods);
  const auto* methods = std::get_if<abc::MethodLookup>(&lookup);
  if (!methods) return 1;
  const auto bound = analysis::ClassMethodSites::Build(file, *header, *index, *methods);
  const auto* sites = std::get_if<analysis::ClassMethodSites>(&bound);
  if (!sites) return 1;
  if (argc >= 3) {
    const std::string_view text{argv[2]};
    abc::EntityOffset entity;
    const auto parsed_id = std::from_chars(text.data(), text.data() + text.size(), entity.value);
    if (parsed_id.ec != std::errc{} || parsed_id.ptr != text.data() + text.size()) return 1;
    return Inspect(file, *header, *index, *methods, *sites, *hash, entity, argc >= 4, budget);
  }
  std::vector<const abc::MethodInfo*> candidates;
  for (const auto& method : index->methods)
    if (method.code && method.code->try_count && sites->Find(method.offset).count == 1)
      candidates.push_back(&method);
  std::sort(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
    return left->code->instruction_bytes < right->code->instruction_bytes;
  });
  constexpr std::size_t kCandidateLimit = 20;
  if (candidates.size() > kCandidateLimit) candidates.resize(kCandidateLimit);
  for (const auto* method : candidates)
    std::cout << nlohmann::json{{"entity", method->offset.value}, {"name", method->name},
        {"bytes", method->code->instruction_bytes}, {"tries", method->code->try_count}} << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
