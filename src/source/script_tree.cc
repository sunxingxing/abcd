#include "script_tree.h"

#include <algorithm>
#include <new>

#include "abcd/source/counting_sink.h"
#include "creator_classes.h"
#include "family_print.h"
#include "family_route_failure.h"
#include "family_tree.h"
#include "record_print_sink.h"
#include "script_scope.h"
#include "tree_globals.h"
namespace abcd::source {
RecordSourceResult BuildScriptTree(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    const RecordModuleContext& context, abc::MethodPosition entry,
    RouteInputIdentity identity, RecordProgramBudget budget,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites* functions,
    const analysis::ClassMethodSites* classes) {
  using E = RecordProgramError;
  const auto fail = [](E error) { return RecordProgramFailure{error, {}}; };
  const auto script_fail = [&](ScriptScopeError error) {
    auto failure =
        fail(error == ScriptScopeError::kResourceLimit ? E::kResourceLimit
                                                       : E::kValue);
    AppendFailureCause(failure.causes, error);
    return failure;
  };
  try {
    const auto* attributes = context.Attributes(file, record.offset);
    if (!GlobalScripts(identity.contract))
      return script_fail(ScriptScopeError::kContract);
    if (!attributes || attributes->module ||
        attributes->common_js.value_or(false) ||
        attributes->shared.value_or(false) || attributes->json_content ||
        context.ForOwner(file, record.offset))
      return script_fail(ScriptScopeError::kOwner);
    if (entry.value >= index.methods.size() ||
        index.methods[entry.value].owner_offset != record.offset ||
        !functions || !classes)
      return script_fail(ScriptScopeError::kEntry);
    const auto class_failure = [&](const CreatorClassFailure& error) {
      auto result = fail(E::kClass);
      CollectFailureCauses(error.cause, result.causes);
      return result;
    };
    const auto retain = [&](std::size_t bytes) {
      if (bytes > budget.bytes)
        throw script_fail(ScriptScopeError::kResourceLimit);
      budget.bytes -= bytes;
    };
    auto read = ReadFunctionFamily(
        file, header, index, lookup, roles, *functions, entry, identity,
        {budget.bytes}, &context, nullptr, {record.offset.value},
        FamilyClassMode::kRetainRoot, nullptr, {}, nullptr, classes);
    if (const auto* error = std::get_if<FamilyFileFailure>(&read))
      return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
    auto family = std::get<FileFamily>(std::move(read));
    retain(family.allocated_bytes);
    if (family.members.empty() || family.members.front().function.Phase())
      return fail(E::kBody);
    auto& function = family.members.front().function.Normal();
    const auto& graph = function.values.graph;
    if (function.frame.parameters) return fail(E::kFrame);
    auto controlled = BuildControlPlan(graph, {budget.bytes});
    if (const auto* error = std::get_if<ControlError>(&controlled)) {
      auto failure = fail(E::kBody);
      AppendFailureCause(failure.causes, *error);
      return failure;
    }
    auto control = std::get<ControlPlan>(std::move(controlled));
    retain(control.allocated_bytes);
    auto resolved = ResolveTreeConstants(file, header, index,
                                         index.methods[entry.value].offset,
                                         graph, {budget.bytes});
    if (!std::holds_alternative<TreeConstants>(resolved)) return fail(E::kRead);
    auto constants = std::get<TreeConstants>(std::move(resolved));
    retain(constants.allocated_bytes);
    auto derived = BuildScriptScope(graph, constants, record.offset,
                                    index.methods[entry.value].offset,
                                    identity.contract, {budget.bytes});
    if (const auto* error = std::get_if<ScriptScopeError>(&derived))
      return script_fail(*error);
    auto declarations = std::get<ScriptScope>(std::move(derived));
    retain(declarations.allocated_bytes);
    auto checked =
        VerifyScriptScope(graph, constants, declarations, record.offset,
                          identity.contract, {budget.bytes});
    if (const auto* error = std::get_if<ScriptScopeError>(&checked))
      return script_fail(*error);
    auto prototypes_result = FindClassPrototypeReads(graph, {budget.bytes});
    if (!std::holds_alternative<ClassPrototypeReads>(prototypes_result))
      return fail(E::kClass);
    auto prototypes =
        std::get<ClassPrototypeReads>(std::move(prototypes_result));
    retain(prototypes.allocated_bytes);
    std::erase_if(prototypes.reads, [&](const auto& site) {
      return !HasClassPrototypeKey(site, constants.View(), constants.names,
                                   constants.texts);
    });
    GlobalScope globals{};
    FunctionSyntax syntax{};
    syntax.identity = identity;
    syntax.method = index.methods[entry.value].offset;
    syntax.owner = {0};
    syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict;
    syntax.script_record = record.offset;
    CreatorClassInput class_input{
        file,     header,
        index,    lookup,
        roles,    *functions,
        *classes, function,
        identity, {index.methods[entry.value].offset, family.first_owner, {0}}};
    CreatorClasses creator;
    if (!function.values.classes.classes.empty()) {
      if (!classes) return fail(E::kClass);
      auto discovered = DiscoverCreatorClasses(class_input, {budget.bytes});
      if (const auto* error = std::get_if<CreatorClassFailure>(&discovered))
        return class_failure(*error);
      creator = std::get<CreatorClasses>(std::move(discovered));
      retain(creator.allocated_bytes);
      for (const auto& task : creator.tasks)
        constants.operations[task.operation.value] = task.tree;
      auto prepared = PrepareCreatorClasses(
          class_input, creator, &globals,
          {static_cast<std::uint32_t>(family.members.size())},
          RecoveryMode::kCompleteOnly, {budget.bytes});
      if (const auto* error = std::get_if<CreatorClassFailure>(&prepared))
        return class_failure(*error);
      retain(std::get<CreatorClassCost>(prepared).allocated_bytes);
    }
    const auto* cells = family.members.front().function.Cells();
    const auto demands = cells ? std::span<const CellDemand>(cells->demands)
                               : std::span<const CellDemand>{};
    auto evaluated = BuildEvaluationPlan(
        graph, {budget.bytes}, IncomingCells::kReject,
        family.members.front().children, {}, demands,
        function.values.initializers.calls,
        function.values.classes.classes.empty()
            ? analysis::ClosureWrites::kUnknown
            : analysis::ClosureWrites::kLanguage,
        function.values.definitions.sites, {}, {},
        function.self_order ? &*function.self_order : nullptr, prototypes.reads,
        {identity.contract, constants.View(), constants.names});
    if (const auto* error = std::get_if<EvaluationFailure>(&evaluated))
      return RouteRefusal{FamilyRouteFailure(*error)};
    auto evaluation = std::get<EvaluationPlan>(std::move(evaluated));
    retain(evaluation.allocated_bytes);
    retain(constants.names.size() * sizeof(std::u16string) +
           constants.texts.size() * sizeof(std::u16string));
    for (const auto& name : constants.names) {
      if (name.size() >= budget.bytes / (2 * sizeof(char16_t)))
        throw script_fail(ScriptScopeError::kResourceLimit);
      retain((name.size() + 1) * 2 * sizeof(char16_t));
    }
    for (const auto& text : constants.texts) {
      if (text.size() >= budget.bytes / (2 * sizeof(char16_t)))
        throw script_fail(ScriptScopeError::kResourceLimit);
      retain((text.size() + 1) * 2 * sizeof(char16_t));
    }
    syntax.names = constants.names;
    syntax.texts = constants.texts;
    auto built =
        BuildFunctionTree(graph, control, evaluation, std::move(syntax),
                          constants.View(), {budget.bytes});
    if (const auto* error = std::get_if<FunctionTreeFailure>(&built))
      return RouteRefusal{FamilyRouteFailure(*error)};
    auto root = std::get<ConstructedFunction>(std::move(built));
    retain(root.allocated_bytes);
    auto sealed = ScriptScopeProof::Verify(
        graph, constants, declarations, root.tree, control, evaluation,
        std::span(index.methods)
            .subspan(record.first_method, record.method_count),
        {budget.bytes});
    if (const auto* error = std::get_if<ScriptScopeError>(&sealed))
      return script_fail(*error);
    root.tree.script_scope =
        std::get<std::shared_ptr<const ScriptScopeProof>>(std::move(sealed));
    retain(root.tree.script_scope->AllocatedBytes());
    globals.script_scope = root.tree.script_scope.get();
    if (!creator.tasks.empty()) {
      if (!root.flow) return fail(E::kValue);
      CaptureTree parent{root.tree, *root.flow};
      auto finished = FinishCreatorClasses(
          class_input, creator, &globals,
          {static_cast<std::uint32_t>(family.members.size())}, &parent,
          RecoveryMode::kCompleteOnly, {budget.bytes});
      if (const auto* error = std::get_if<CreatorClassFailure>(&finished))
        return class_failure(*error);
      retain(std::get<CreatorClassCost>(finished).allocated_bytes);
    }
    if (!creator.tasks.empty()) {
      auto verified = VerifyTreeSemantics(graph, control, evaluation, root.tree,
                                          constants.View(), {budget.bytes});
      if (const auto* error = std::get_if<TreeSemanticError>(&verified)) {
        auto result = fail(E::kValue);
        AppendFailureCause(result.causes, *error);
        return result;
      }
      auto proof = std::get<TreeSemantics>(std::move(verified));
      retain(proof.allocated_bytes);
      root.flow = std::move(proof.flow);
    }
    if (!root.flow) return fail(E::kValue);
    family.next_class_owner = creator.tasks.empty()
                                  ? family.members.size()
                                  : creator.tasks.back().owners->end;
    FamilyMember seed{std::move(root.tree), std::move(*root.flow), {}, {}, {},
                      record.offset};
    auto assembled = BuildRecordFamilyTrees(
        file, header, index, std::move(family), std::move(seed), {budget.bytes},
        &context, {record.offset.value}, &lookup, &globals);
    if (const auto* error = std::get_if<FamilyTreeFailure>(&assembled))
      return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
    auto tree = std::get<ConstructedFamily>(std::move(assembled));
    retain(tree.allocated_bytes);
    auto destination = VerifyFamilyDestination(tree, &globals, {budget.bytes});
    if (const auto* error = std::get_if<FamilyEnclosureFailure>(&destination))
      return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
    const auto write = [&](std::ostream& output) {
      auto printed = PrintScriptFamilyDraft(tree, output, budget.output,
                                            {budget.bytes}, &globals);
      if (const auto* error = std::get_if<TreePrintFailure>(&printed))
        throw *error;
      return std::get<PrintedFamilyDraft>(std::move(printed));
    };
    CountingSink count;
    std::ostream counting(&count);
    auto measure = write(counting);
    retain(measure.allocated_bytes);
    PrintMemory memory(budget.bytes);
    RecordPrintSink sink(memory, count.Bytes());
    std::ostream output(&sink);
    auto printed = write(output);
    if (printed.bytes != measure.bytes || file.CheckUnchanged())
      return fail(E::kFileChanged);
    PrintedSource source;
    source.contract = identity.contract;
    source.text = sink.Finish();
    for (const auto& member : printed.members)
      source.incomplete |= member.incomplete;
    return source;
  } catch (const RecordProgramFailure& failure) {
    return failure;
  } catch (const TreePrintFailure& failure) {
    auto result =
        fail(failure.kind == TreePrintError::kResourceLimit ? E::kResourceLimit
                                                            : E::kValue);
    AppendFailureCause(result.causes, failure.kind);
    result.printing = failure;
    return result;
  } catch (const std::bad_alloc&) {
    return fail(E::kAllocationFailure);
  } catch (const std::length_error&) {
    return fail(E::kAllocationFailure);
  }
}
}  // namespace abcd::source
