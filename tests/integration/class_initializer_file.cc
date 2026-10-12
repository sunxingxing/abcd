#include "../../src/source/class_initializer_file.h"
#include "../../src/source/derived_projection.h"
#include "../../src/source/class_tree.h"
#include "../../src/source/call_plan.h"
#include "../../src/source/logical_regions.h"
#include "../../src/source/tree_checks.h"
#include "../../src/source/default_body.h"
#include "../../src/source/prepared_family.h"
#include "abcd/corpus/abc_file.h"
#include "../../src/source/tree_constants.h"
#include "../../src/source/syntax_resolution.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
#include "../../src/source/module_aliases.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/source_route.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <source_location>
namespace {
void Check(bool value, const std::source_location& where = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); }
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  Check(argc == 2 || argc == 3);
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  // Real initialRender callback: builder's bind call occurs after allocation.
  // Conditional capture admission is evaluation evidence, not publication of
  // a detached closure or completion of the enclosing rendering family.
  {
    const auto position = lookup.Find({9809}); Check(position.has_value());
    auto read = ReadFunctionValues(file, header, index, *position, identity, InputCaptureMode::kConditional);
    Check(std::holds_alternative<FileFunction>(read));
    const auto& function = std::get<FileFunction>(read);
    const auto owner = std::find_if(index.records.begin(), index.records.end(),
        [&](const auto& record) { return record.offset == index.methods[position->value].owner_offset; });
    Check(owner != index.records.end());
    const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
    const auto modules = std::get<RecordModuleContext>(ReadRecordModuleContext(file, header, index, *owner, packaging));
    const auto aliases = std::get<ModuleAliases>(BindModuleAliases(file, header, index, function.input,
        function.values, modules, {owner->offset.value}));
    auto planned = BuildEvaluationPlan(function.values.graph, {}, IncomingCells::kConditional, {}, aliases.evaluations,
        {}, {}, analysis::ClosureWrites::kLanguage);
    if (const auto* error = std::get_if<EvaluationError>(&planned))
      std::cerr << "literal callback evaluation failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<EvaluationPlan>(planned));
    const auto& evaluation = std::get<EvaluationPlan>(planned);
    Check(std::holds_alternative<CallPlan>(BuildCallPlan(function.values.graph, evaluation)));
    bool nested_call = false;
    for (std::size_t i = 0; i < function.values.graph.ops.size(); ++i) {
      const auto& op = function.values.graph.ops[i];
      if (function.values.graph.origins[op.origin.value].pc.value == 180) {
        Check(std::holds_alternative<analysis::ValueInvoke>(op.operation));
        Check(evaluation.operations[i].kind == EvaluationKind::kNestedLiteral);
        nested_call = true;
      }
    }
    Check(nested_call);
  }
  // A real derived body now imports through the common value pipeline. Check
  // the original-body initialization proof before any source projection.
  {
    const auto position = lookup.Find({9865}); Check(position.has_value());
    auto loaded = ReadFunctionValues(file, header, index, *position, identity, InputCaptureMode::kClosed);
    Check(std::holds_alternative<FileFunction>(loaded));
    const auto& constructor = std::get<FileFunction>(loaded);
    const auto proof = analysis::ProveRangeDerivedConstructor(constructor.body, constructor.frame, {}, header.profile);
    if (const auto* error = std::get_if<analysis::DerivedConstructorError>(&proof))
      std::cerr << "derived protocol failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<analysis::RangeDerivedConstructor>(proof));
    Check(std::get<analysis::RangeDerivedConstructor>(proof).super_pc.value == 79);
    std::size_t checks = 0, calls = 0;
    const auto& graph = constructor.values.graph;
    for (std::size_t i = 0; i < constructor.input.Input().instructions.size(); ++i) {
      const auto& original = constructor.input.Input().instructions[i];
      const auto* check = std::get_if<analysis::SuperCheck>(&original.operation);
      const auto* call = std::get_if<analysis::SuperRange>(&original.operation);
      if (!check && !call) continue;
      Check(i < graph.input.sites.size() && graph.input.sites[i].has_value());
      const auto operation = *graph.input.sites[i];
      Check(operation.value < graph.ops.size());
      const auto& op = graph.ops[operation.value];
      Check(op.life == analysis::ValueLife::kLive);
      Check(op.normal == original.effects.normal && op.before_throw == original.effects.before_throw &&
          op.exceptional == original.effects.exceptional);
      Check(graph.origins[op.origin.value].pc == original.pc);
      if (check) {
        ++checks;
        Check(std::get<analysis::ValueSuperCheck>(op.operation).kind == check->kind);
        Check(!op.result && op.use_count == 1);
        const auto& use = graph.uses[op.first_use.value];
        Check(use.role == analysis::OperandRole::kGuardValue && use.purpose == analysis::ReadPurpose::kProtocol);
      } else {
        ++calls;
        const auto& imported = std::get<analysis::ValueSuperCall>(op.operation);
        Check(imported.arguments == call->count && imported.cache_slot == call->cache_slot);
        Check(op.result.has_value() && op.use_count == call->count);
      }
    }
    Check(calls == 1 && checks > 1);
    // In-memory mutation only: after the parent call the first guard still
    // checks the OLD receiver. Requiring an initialized receiver here is wrong.
    auto changed = constructor.body;
    bool mutated = false;
    for (auto& site : changed.instructions) {
      const auto lifted = analysis::LiftSuperInstruction(site.decoded, constructor.frame.registers);
      const auto* check = std::get_if<analysis::SuperCheck>(&lifted);
      if (!check || check->kind != analysis::SuperCheckKind::kRequireUninitialized) continue;
      site.decoded.operands[0].raw_bits = 0; mutated = true; break;
    }
    Check(mutated);
    const auto rejected = analysis::ProveRangeDerivedConstructor(changed, constructor.frame, {}, header.profile);
    Check(std::get<analysis::DerivedConstructorError>(rejected) == analysis::DerivedConstructorError::kGuard);

  }
  // Actual post-class getter/setter definitions, separate from buffer members.
  {
    const auto position = lookup.Find({10009}); Check(position.has_value());
    auto caller = ReadFunctionValues(file, header, index, *position, identity, InputCaptureMode::kClosed);
    Check(std::holds_alternative<FileFunction>(caller));
    const auto& parent = std::get<FileFunction>(caller);
    const auto& graph = parent.values.graph;
    Check(parent.values.definitions.sites.size() == 9);
    // Derive the callable owner from the actual DEFINECLASS occurrence. The
    // same body read in isolation cannot supply a fabricated class owner.
    const auto creation = parent.values.definitions.sites.front().creation;
    auto resolved_constructor = ClassTreeContext::ResolveConstructor(file, header, index, lookup,
        roles, parent, creation, identity);
    if (const auto* failure = std::get_if<ClassContextFailure>(&resolved_constructor))
      std::cerr << "constructor context failure " << failure->error.index() << '\n';
    Check(std::holds_alternative<ClassTreeContext>(resolved_constructor));
    auto context = FunctionContext{std::get<ClassTreeContext>(std::move(resolved_constructor))};
    Check(context.Callable() == CallableKind::kDerivedConstructor);
    Check(context.Target().value == 9865);
    const auto constructor_position = lookup.Find(context.Target()); Check(constructor_position.has_value());
    auto owned = ReadFunctionValues(file, header, index, *constructor_position, identity,
        InputCaptureMode::kClosed, {}, nullptr, &context);
    Check(std::holds_alternative<FileFunction>(owned));
    Check(std::get<FileFunction>(owned).input.MatchesOwner(context));
    Check(std::get<FileFunction>(owned).input.Proof().receiver == analysis::FrameReceiver::kUnknown);
    auto& derived = std::get<FileFunction>(owned);
    const auto projected_constructor = ProjectDerivedConstructor(derived, context);
    if (const auto* error = std::get_if<DerivedProjectionError>(&projected_constructor))
      std::cerr << "derived projection failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<DerivedProjection>(projected_constructor));
    const auto& protocol = std::get<DerivedProjection>(projected_constructor);
    const auto scheduled_constructor = BuildEvaluationPlan(derived.values.graph);
    if (const auto* error = std::get_if<EvaluationError>(&scheduled_constructor))
      std::cerr << "derived evaluation failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<EvaluationPlan>(scheduled_constructor));
    Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(derived.values.graph,
        std::get<EvaluationPlan>(scheduled_constructor))));

    Check(protocol.revision == derived.values.graph.revision && !protocol.guards.empty());
    auto& guard = derived.values.graph.ops[protocol.guards.front().value];
    auto& guard_input = derived.values.graph.uses[guard.first_use.value];
    const auto saved = guard_input.operand;
    guard_input.operand = analysis::SpecialValue::kNull;
    Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(derived, context)) == DerivedProjectionError::kOperand);
    guard_input.operand = saved;
    Check(std::holds_alternative<DerivedProjection>(ProjectDerivedConstructor(derived, context)));
    auto& current_call = std::get<analysis::ValueSuperCall>(derived.values.graph.ops[protocol.call.value].operation);
    ++current_call.arguments;
    Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(derived, context)) == DerivedProjectionError::kOrigin);
    --current_call.arguments;
    auto& current_guard = std::get<analysis::ValueSuperCheck>(guard.operation);
    const auto saved_kind = current_guard.kind;
    current_guard.kind = analysis::SuperCheckKind::kNoop;
    Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(derived, context)) == DerivedProjectionError::kOrigin);
    current_guard.kind = saved_kind;
    Check(std::holds_alternative<DerivedProjectionError>(ProjectDerivedConstructor(derived, context, {0})));
    Check(std::holds_alternative<DerivedProjectionError>(ProjectDerivedConstructor(derived, context, {64 * 1024 * 1024, 0})));



    // Family construction projects defaults before deriving source bindings.
    // Rebase the constructor protocol against that residual graph, not a
    // cached proof for the pre-signature revision.
    const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(derived.values.graph, derived.frame.parameters));
    const auto candidates = std::get<DefaultCandidates>(FindDefaultCandidates(derived.values.graph, parameters, context.Length()));
    Check(!candidates.values.empty());
    auto residual_result = DefaultBody::Build(derived.values.graph, parameters, candidates, context.Length());
    Check(std::holds_alternative<DefaultBody>(residual_result));
    const auto& residual = std::get<DefaultBody>(residual_result);
    derived.values.graph = residual.Graph();
    const auto rebased = ProjectDerivedConstructor(derived, context);
    if (const auto* error = std::get_if<DerivedProjectionError>(&rebased))
      std::cerr << "residual derived projection failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<DerivedProjection>(rebased));
    Check(std::get<DerivedProjection>(rebased).revision == derived.values.graph.revision);
    Check(protocol.revision != derived.values.graph.revision);
    Check(VerifyDerivedProjection(derived, context, protocol) == DerivedProjectionError::kRevision);
    const auto& current_protocol = std::get<DerivedProjection>(rebased);
    Check(!VerifyDerivedProjection(derived, context, current_protocol));
    auto missing_guard = current_protocol; missing_guard.guards.pop_back();
    Check(VerifyDerivedProjection(derived, context, missing_guard) == DerivedProjectionError::kCoverage);
    auto wrong_call = current_protocol; wrong_call.call = wrong_call.guards.front();
    Check(VerifyDerivedProjection(derived, context, wrong_call) == DerivedProjectionError::kCoverage);

    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(derived.values.graph)));
    auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, context,
        derived.body, derived.values.graph, {0}));
    auto constants = syntax.View(); constants.receiver_guards = current_protocol.guards;
    const auto control = std::get<ControlPlan>(BuildControlPlan(derived.values.graph));
    const auto constructor_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(derived.values.graph, {}, IncomingCells::kReject, {}, {}, {}, {}, analysis::ClosureWrites::kUnknown, {}, current_protocol.guards));
    const auto built_tree = BuildFunctionTree(derived.values.graph, control, constructor_evaluation,
        std::move(syntax.syntax), constants);
    if (const auto* error = std::get_if<FunctionTreeFailure>(&built_tree))
      std::cerr << "derived tree failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<ConstructedFunction>(built_tree));
    const auto& constructor_tree = std::get<ConstructedFunction>(built_tree).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(derived.values.graph, control,
        constructor_evaluation, constructor_tree, constants)));
    auto constructor_family_input = ReadFunctionFamily(file, header, index, lookup, roles, sites,
        context.Position(), identity, {}, nullptr, &context);
    Check(std::holds_alternative<FileFamily>(constructor_family_input));
    auto constructor_family = BuildFileFamilyTrees(file, header, index,
        std::get<FileFamily>(std::move(constructor_family_input)), context, {}, nullptr, {}, &lookup);
    if (const auto* error = std::get_if<FamilyTreeFailure>(&constructor_family)) {
      std::cerr << "derived family failure " << error->cause.index() << '\n';
      std::visit([](const auto& cause) { if constexpr (std::is_enum_v<std::decay_t<decltype(cause)>>)
        std::cerr << "derived family enum " << static_cast<unsigned>(cause) << '\n'; }, error->cause);
    }
    Check(std::holds_alternative<ConstructedFamily>(constructor_family));
    GlobalScope constructor_globals{};
    const FamilyDestination constructor_destination{Strictness::kStrict, &constructor_globals, FunctionFragment::kClassMember};
    auto prepared_constructor = PreparedFamily::PrepareConstructor(file, header, index, lookup, roles, sites,
        parent, creation, identity, constructor_destination);
    if (const auto* error = std::get_if<FamilyPrepareFailure>(&prepared_constructor))
      std::cerr << "derived prepare failure " << error->cause.index() << '\n';
    Check(std::holds_alternative<PreparedFamily>(prepared_constructor));
    std::ostringstream constructor_source;
    const auto printed_constructor = std::get<PreparedFamily>(prepared_constructor).PrintDraft(
        constructor_destination, constructor_source);
    if (const auto* error = std::get_if<TreePrintError>(&printed_constructor))
      std::cerr << "derived print failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<PrintedFamilyDraft>(printed_constructor));
    // Fragment success must not permit a whole class to silently lose extends.
    // This deliberately minimal container tests the class printer's boundary;
    // it is not evidence that the actual class's remaining members recovered.
    ClassTree no_heritage;
    no_heritage.identity = identity; no_heritage.creation = *context.ConstructorSite();
    no_heritage.name = u"Index"; no_heritage.callable = context.Callable();
    no_heritage.constructor = std::move(std::get<PreparedFamily>(prepared_constructor));
    std::ostringstream invalid_class;
    const auto rejected_class = PrintClassTree(no_heritage, invalid_class, {}, {}, &constructor_globals);
    Check(std::get<TreePrintError>(rejected_class) == TreePrintError::kUnsupportedContext);
    Check(invalid_class.str().empty());
    // A creation-only fixture isolates outer heritage ownership using the real
    // derived constructor. It deliberately omits the class's other members.
    analysis::ValueProgram creation_graph;
    const auto effects = analysis::LanguageEffect::kAllocation | analysis::LanguageEffect::kUserCode;
    creation_graph.values = {
        {analysis::EntryDefinition{analysis::ParameterEntry{0}}, analysis::ValueKinds::Language(), analysis::UseId{0}},
        {analysis::OpResult{{0}}, analysis::ValueKinds::Language(), analysis::UseId{1}}};
    creation_graph.origins = {{{0}, no_heritage.creation.pc, {}}, {{1}, {no_heritage.creation.pc.value + 1}, {}}};
    creation_graph.ops = {
        {parent.values.graph.ops[creation.value].operation, {0}, 0, {0}, 1, analysis::ValueId{1},
            effects, effects, analysis::ExceptionalCompletion::kMayThrow, {0}},
        {analysis::ValueReturn{}, {0}, 1, {1}, 1, {}, {}, {}, analysis::ExceptionalCompletion::kImpossible, {1}}};
    creation_graph.uses = {
        {analysis::OpId{0}, analysis::OperandRole::kHeritage, analysis::ReadPurpose::kProtocol, analysis::ValueId{0}},
        {analysis::OpId{1}, analysis::OperandRole::kReturnValue, analysis::ReadPurpose::kOperand, analysis::ValueId{1}}};
    creation_graph.blocks = {{{0}, 2, {0}, 0}};
    std::vector<ExpressionConstant> creation_constants(2);
    creation_constants[0] = std::make_shared<ClassTree>(std::move(no_heritage));
    const auto creation_plan = std::get<EvaluationPlan>(BuildEvaluationPlan(creation_graph));
    const auto creation_control = std::get<ControlPlan>(BuildControlPlan(creation_graph));
    FunctionSyntax creation_syntax{};
    creation_syntax.identity = identity; creation_syntax.method = context.ConstructorSite()->creator;
    creation_syntax.owner = {1}; creation_syntax.callable = CallableKind::kFunction;
    creation_syntax.strictness = Strictness::kStrict;
    creation_syntax.names = {u"Parent"}; creation_syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
    auto created = BuildFunctionTree(creation_graph, creation_control, creation_plan, creation_syntax, {creation_constants, 1});
    Check(std::holds_alternative<ConstructedFunction>(created));
    const auto& creation_tree = std::get<ConstructedFunction>(created).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(creation_graph, creation_control,
        creation_plan, creation_tree, {creation_constants, 1})));
    auto missing_heritage = creation_tree;
    auto wrong_heritage = creation_tree;
    bool found_heritage = false;
    for (std::size_t i = 0; i < creation_tree.expressions.size(); ++i) {
      const auto* klass = std::get_if<ClassExpr>(&creation_tree.expressions[i].expression);
      if (!klass) continue;
      Check(klass->heritage.has_value()); found_heritage = true;
      std::get<ClassExpr>(missing_heritage.expressions[i].expression).heritage.reset();
      wrong_heritage.expressions[klass->heritage->value].expression = TreeLiteral{UndefinedLiteral{}};
    }
    Check(found_heritage);
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(creation_graph, creation_control,
        creation_plan, missing_heritage, {creation_constants, 1})));
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(creation_graph, creation_control,
        creation_plan, wrong_heritage, {creation_constants, 1})));
    std::ostringstream class_source;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(creation_tree, class_source, {}, {}, &constructor_globals)));
    Check(class_source.str().find("extends (Parent)") != std::string::npos);
    if (argc == 3 && std::string_view(argv[2]) == "--extends") {
      std::cout << class_source.str(); return 0;
    }

    if (argc == 3 && std::string_view(argv[2]) == "--derived") {
      std::cout << constructor_source.str(); return 0;
    }

    // The real unused default survives as an elided original literal, not a
    // live phi. Prove and migrate that exact PC while retaining closure setup.
    {
      const auto nav_position = lookup.Find({10428}); Check(nav_position.has_value());
      auto read = ReadFunctionValues(file, header, index, *nav_position, identity, InputCaptureMode::kClosed);
      Check(std::holds_alternative<FileFunction>(read));
      const auto& nav = std::get<FileFunction>(read);
      const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(nav.values.graph, nav.frame.parameters));
      const auto candidates = std::get<DefaultCandidates>(FindDefaultCandidates(nav.values.graph, parameters, 1));
      Check(candidates.values.size() == 1 && !candidates.values[0].merged);
      Check(std::get<analysis::SpecialValue>(candidates.values[0].fallback) == analysis::SpecialValue::kNull);
      const auto initializer = candidates.values[0].initializer;
      Check(nav.values.graph.origins[nav.values.graph.ops[initializer.value].origin.value].pc.value == 28);
      auto body = DefaultBody::Build(nav.values.graph, parameters, candidates, 1);
      Check(std::holds_alternative<DefaultBody>(body));
      const auto& residual = std::get<DefaultBody>(body).Graph();
      Check(residual.ops[0].life == analysis::ValueLife::kLive);
      Check(residual.ops[0].operation == nav.values.graph.ops[0].operation);
      Check(residual.origins[residual.ops[initializer.value].origin.value].disposition ==
          analysis::OriginDisposition::kRetained);
      const auto owner = std::find_if(index.records.begin(), index.records.end(),
          [&](const auto& record) { return record.offset == index.methods[nav_position->value].owner_offset; });
      Check(owner != index.records.end());
      const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
      auto modules = std::get<RecordModuleContext>(ReadRecordModuleContext(file, header, index, *owner, packaging));
      auto enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(file, header, modules, identity, {owner->offset.value}));
      GlobalScope scope{}; scope.module_enclosure = &enclosure;
      const FamilyDestination destination{Strictness::kStrict, &scope, FunctionFragment::kClassMember};
      auto prepared = PreparedFamily::PrepareDefinedMember(file, header, index, lookup, roles, sites,
          parent, parent.values.definitions.sites[5].definition, abc::ClassMethodKind::kOrdinary, identity, destination);
      Check(std::holds_alternative<PreparedFamily>(prepared));
      const auto& family = std::get<PreparedFamily>(prepared);
      Check(family.Family().members[family.Family().root.value].tree.method.value == 10428);
      std::ostringstream source;
      Check(std::holds_alternative<PrintedFamilyDraft>(family.PrintDraft(destination, source)));
      if (argc == 3 && std::string_view(argv[2]) == "--navmap") {
        std::cout << source.str(); return 0;
      }
      // Preserve the actual initialRender occurrence and complete child binding
      // inventory when checking the guarded capture in its descendant 10400.
      auto render_context = ClassTreeContext::ResolveDefinedMember(file, header, index, lookup,
          roles, sites, parent, parent.values.definitions.sites[6].definition,
          abc::ClassMethodKind::kOrdinary, identity);
      Check(std::holds_alternative<ClassTreeContext>(render_context));
      const FunctionContext render_owner{std::get<ClassTreeContext>(std::move(render_context))};
      auto render_family = ReadFunctionFamily(file, header, index, lookup, roles, sites,
          render_owner.Position(), identity, {}, &modules, &render_owner, {owner->offset.value});
      Check(std::holds_alternative<FileFamily>(render_family));
      bool guarded_child = false, logical_child = false;
      for (const auto& child : std::get<FileFamily>(render_family).members) {
        if (child.function.Method().value == 10510) {
          logical_child = true;
          const auto& graph = child.function.Normal().values.graph;
          const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
          const auto regions = FindLogicalRegions(graph, control);
          Check(regions.size() == 1);
          const auto& region = regions.front();
          Check(region.operation == TreeLogical::kOr);
          Check(graph.origins[graph.ops[region.boolean.value].origin.value].pc.value == 72);
          Check(graph.origins[graph.ops[region.branch.value].origin.value].pc.value == 75);
          const auto* left = std::get_if<analysis::OpResult>(&graph.values[region.left.value].origin);
          Check(left && graph.origins[graph.ops[left->op.value].origin.value].pc.value == 66);
          const auto right = std::get<analysis::ValueId>(region.right);
          const auto allocation = std::get<analysis::OpResult>(graph.values[right.value].origin).op;
          Check(std::holds_alternative<analysis::ValueArrayAllocation>(graph.ops[allocation.value].operation));
          Check(graph.origins[graph.ops[allocation.value].origin.value].pc.value == 77);
          const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kConditional,
              child.children, {}, {}, {}, analysis::ClosureWrites::kLanguage));
          Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph, evaluation)));
          Check(evaluation.logical_values[region.merged.value] == region.branch);
          Check(evaluation.operations[region.branch.value].kind == EvaluationKind::kLogicalValue);
          Check(evaluation.operations[left->op.value].kind == EvaluationKind::kLogicalLeft);
          Check(!evaluation.operations[left->op.value].temporary);
          Check(evaluation.operations[allocation.value].kind == EvaluationKind::kPhiInput);
          Check(!evaluation.operations[allocation.value].temporary);
          TreeCheckBudget logical_budget({});
          const auto expressions = FindSelectionExpressions(graph, control, evaluation, logical_budget);
          Check(expressions.size() == 1 && expressions.front().operation == TreeLogical::kOr);
        }
        if (child.function.Method().value != 10400) continue;
        guarded_child = true;
        const auto& function = child.function.Normal();
        const auto evaluation = BuildEvaluationPlan(function.values.graph, {}, IncomingCells::kConditional,
            child.children, {}, {}, {}, analysis::ClosureWrites::kLanguage);
        Check(std::holds_alternative<EvaluationPlan>(evaluation));
        const auto& planned = std::get<EvaluationPlan>(evaluation);
        Check(std::holds_alternative<CallPlan>(BuildCallPlan(function.values.graph, planned)));
        bool projected = false;
        for (std::size_t i = 0; i < function.values.graph.ops.size(); ++i) {
          const auto& op = function.values.graph.ops[i];
          if (function.values.graph.origins[op.origin.value].pc.value != 26) continue;
          Check(planned.operations[i].kind == EvaluationKind::kCellGuard);
          projected = true;
        }
        Check(projected);
        Check(std::holds_alternative<EvaluationError>(BuildEvaluationPlan(function.values.graph, {},
            IncomingCells::kReject, child.children, {}, {}, {}, analysis::ClosureWrites::kLanguage)));
      }
      Check(guarded_child && logical_child);
      if (argc == 3 && std::string_view(argv[2]) == "--render") {
        auto render_prepared = PreparedFamily::PrepareDefinedMember(file, header, index, lookup, roles, sites,
            parent, parent.values.definitions.sites[6].definition, abc::ClassMethodKind::kOrdinary, identity, destination);
        if (const auto* failure = std::get_if<FamilyPrepareFailure>(&render_prepared)) {
          std::cerr << "render prepare cause " << failure->cause.index();
          if (const auto* tree = std::get_if<FamilyTreeFailure>(&failure->cause)) {
            std::cerr << " member " << tree->member.value << " cause " << tree->cause.index();
            if (const auto* error = std::get_if<FunctionTreeFailure>(&tree->cause)) std::cerr << " tree " << static_cast<unsigned>(*error);
          }
          std::cerr << '\n'; return 1;
        }
        Check(std::holds_alternative<PrintedFamilyDraft>(std::get<PreparedFamily>(render_prepared).PrintDraft(destination, std::cout)));
        return 0;
      }
    }


    const auto projected = ProjectClassDefinitions(graph, parent.values.definitions.sites);
    if (const auto* error = std::get_if<ClassDefinitionError>(&projected))
      std::cerr << "accessor corridor failure " << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<ClassDefinitionProjection>(projected));
    const auto& projection = std::get<ClassDefinitionProjection>(projected);
    for (const auto& accessor : parent.values.definitions.sites) {
      Check(projection.owners[accessor.definition.value] == accessor.creation);
      if (accessor.prototype) Check(projection.owners[accessor.prototype->value] == accessor.creation);
    }
    // Member ownership cannot discard the two ordinary function creations
    // in this actual page. Its complete outer closure family is still open.
    std::vector<unsigned> unbound;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueFunctionCreation>(op.operation) &&
          !projection.owners[i]) unbound.push_back(graph.origins[op.origin.value].pc.value);
    }
    Check(unbound == std::vector<unsigned>({77, 285}));
    auto read_family = ReadFunctionFamily(file, header, index, lookup, roles, sites, *position,
        identity, {}, nullptr, nullptr, {}, FamilyClassMode::kRetainRoot);
    if (const auto* error = std::get_if<FamilyFileFailure>(&read_family)) {
      std::cerr << "record family failure " << error->cause.index();
      if (error->method) std::cerr << " method " << error->method->value;
      if (error->creation_pc) std::cerr << " pc " << error->creation_pc->value;
      std::cerr << '\n';
    }
    Check(std::holds_alternative<FileFamily>(read_family));
    const auto& entry_family = std::get<FileFamily>(read_family);
    Check(entry_family.members.front().children.size() == 2);
    Check(entry_family.members.front().classes.size() == 10);
    Check(entry_family.members.front().function.Cells() != nullptr);
    const auto& entry_values = entry_family.members.front().function.Normal().values;
    const auto& entry_graph = entry_values.graph;
    std::vector<unsigned> bound_pcs;
    for (const auto& binding : entry_family.members.front().children) {
      bound_pcs.push_back(entry_graph.origins[entry_graph.ops[binding.creation.value].origin.value].pc.value);
      Check(binding.child.value < entry_family.members.size());
      const auto& child = entry_family.members[binding.child.value];
      Check(child.incoming && child.incoming->parent == FileFamily::kRoot);
      Check(child.function.Method().value == (bound_pcs.back() == 77 ? 9698U : 9726U));
      if (bound_pcs.back() == 285) {
        Check(!binding.captures.empty());
        Check(binding.captures.size() == child.incoming->cells.targets.size());
        for (const auto& target : child.incoming->cells.targets)
          Check(std::holds_alternative<ParentCell>(target.target));
      } else Check(binding.captures.empty());
    }
    Check(bound_pcs == unbound);
    for (const auto& obligation : entry_family.members.front().classes) {
      Check(entry_graph.origins[entry_graph.ops[obligation.creation.value].origin.value].pc.value == 98);
      Check(entry_graph.input.sites[obligation.site.value] == obligation.operation);
    }
    // Retention is explicit and does not change ordinary family acceptance.
    Check(std::holds_alternative<FamilyFileFailure>(ReadFunctionFamily(file, header, index, lookup,
        roles, sites, *position, identity)));
    const auto bound_evaluation = BuildEvaluationPlan(entry_values.graph, {}, IncomingCells::kReject,
        entry_family.members.front().children, {}, entry_family.members.front().function.Cells()->demands,
        entry_values.initializers.calls, analysis::ClosureWrites::kUnknown, entry_values.definitions.sites);
    Check(std::holds_alternative<EvaluationPlan>(bound_evaluation));
    const auto evaluation = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {},
        analysis::ClosureWrites::kUnknown, parent.values.definitions.sites);
    Check(std::get<EvaluationError>(evaluation) == EvaluationError::kOwnership);
    const unsigned pcs[] = {137, 160, 183, 206};
    std::ostringstream definitions;
    definitions << "class Accessors {\n";
    GlobalScope globals{};
    const FamilyDestination destination{Strictness::kStrict, &globals, FunctionFragment::kClassMember};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto& accessor = parent.values.definitions.sites[i];
      Check(accessor.prototype.has_value());
      Check(accessor.Method(abc::ClassMethodKind::kGetter).has_value() == (i % 2 == 0));
      Check(accessor.Method(abc::ClassMethodKind::kSetter).has_value() == (i % 2 != 0));
      Check(graph.origins[graph.ops[accessor.creation.value].origin.value].pc.value == 98);
      Check(graph.origins[graph.ops[accessor.definition.value].origin.value].pc.value == pcs[i]);
      const auto kind = i % 2 ? abc::ClassMethodKind::kSetter : abc::ClassMethodKind::kGetter;
      auto prepared = PreparedFamily::PrepareDefinedMember(file, header, index, lookup, roles, sites,
          parent, accessor.definition, kind, identity, destination);
      if (const auto* error = std::get_if<FamilyPrepareFailure>(&prepared)) {
        std::cerr << "accessor prepare failure " << i << " cause " << error->cause.index() << '\n';
        if (const auto* tree = std::get_if<FamilyTreeFailure>(&error->cause)) {
          std::cerr << "member " << tree->member.value << " cause " << tree->cause.index() << '\n';
          std::visit([](const auto& e) { if constexpr (std::is_enum_v<std::decay_t<decltype(e)>>) std::cerr << "error " << static_cast<unsigned>(e) << '\n'; }, tree->cause);
        }
      }
      Check(std::holds_alternative<PreparedFamily>(prepared));
      const auto& family = std::get<PreparedFamily>(prepared);
      const auto emitted = family.PrintDraft(destination, definitions, {}, {}, 1);
      Check(std::holds_alternative<PrintedFamilyDraft>(emitted));
      auto wrong_identity = identity; ++wrong_identity.file_size;
      Check(std::holds_alternative<ClassContextFailure>(ClassTreeContext::ResolveDefinedMember(
          file, header, index, lookup, roles, sites, parent, accessor.definition, kind, wrong_identity)));
      const auto absent = i % 2 ? abc::ClassMethodKind::kGetter : abc::ClassMethodKind::kSetter;
      Check(std::holds_alternative<ClassContextFailure>(ClassTreeContext::ResolveDefinedMember(
          file, header, index, lookup, roles, sites, parent, accessor.definition, absent, identity)));

    }
    const unsigned installed_pcs[] = {218, 230, 242, 254, 266};
    for (std::size_t i = 4; i < 9; ++i) {
      const auto& site = parent.values.definitions.sites[i];
      Check(site.Method(abc::ClassMethodKind::kOrdinary).has_value());
      Check(graph.origins[graph.ops[site.definition.value].origin.value].pc.value == installed_pcs[i - 4]);
      Check(site.prototype.has_value() == (i != 8));
    }
    auto ordinary = PreparedFamily::PrepareDefinedMember(file, header, index, lookup, roles, sites,
        parent, parent.values.definitions.sites.back().definition, abc::ClassMethodKind::kOrdinary, identity, destination);
    Check(std::holds_alternative<PreparedFamily>(ordinary));
    std::ostringstream static_method; static_method << "class Member {\n";
    Check(std::holds_alternative<PrintedFamilyDraft>(std::get<PreparedFamily>(ordinary).PrintDraft(destination, static_method)));
    static_method << "}\nMember;\n";
    if (argc == 3 && std::string_view(argv[2]) == "--static-method") { std::cout << static_method.str(); return 0; }
    definitions << "}\nAccessors;\n";
    if (argc == 3 && std::string_view(argv[2]) == "--accessors") { std::cout << definitions.str(); return 0; }
  }
  for (const auto entry : {7312U, 16391U}) {
    const auto position = lookup.Find({entry}); Check(position.has_value());
    const auto& method = index.methods[position->value];
    const auto owner = std::find_if(index.records.begin(), index.records.end(),
        [&](const auto& record) { return record.offset == method.owner_offset; });
    Check(owner != index.records.end());
    const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
    auto modules = std::get<RecordModuleContext>(ReadRecordModuleContext(file, header, index, *owner, packaging));
    auto enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(file, header, modules, identity, {owner->offset.value}));
    GlobalScope scope{}; scope.module_enclosure = &enclosure;
    auto caller = ReadFunctionValues(file, header, index, *position, identity, InputCaptureMode::kClosed);
    Check(std::holds_alternative<FileFunction>(caller));
    const auto& parent = std::get<FileFunction>(caller);
    Check(parent.values.initializers.calls.size() == 1);
    const auto& call = parent.values.initializers.calls.front();
    auto loaded = ReadClassInitializerFunction(file, header, index, lookup, roles, sites, parent, call, {}, &modules);
    if (const auto* failure = std::get_if<ClassInitializerReadFailure>(&loaded)) {
      std::cerr << "initializer read failure " << failure->index();
      if (const auto* body = std::get_if<FunctionFileFailure>(failure)) {
        std::cerr << " body " << body->index();
        if (const auto* input = std::get_if<FunctionInputFailure>(body)) {
          std::cerr << " input " << input->cause.index();
        }
        if (const auto* values = std::get_if<FunctionValueFailure>(body))
          std::cerr << " stage " << static_cast<unsigned>(values->cause.reason);
      }
      std::cerr << '\n'; return 1;
    }
    const auto& initializer = std::get<ClassInitializerFunction>(loaded);
    Check(initializer.context.Callable() == CallableKind::kMethod);
    Check(initializer.function.input.MatchesOwner(initializer.context));
    Check(initializer.function.frame.parameters == 0);
    const auto* site = initializer.context.CreationSite(); Check(site);
    const auto occurrence = sites.FindOccurrence(site->target, site->creator, site->pc); Check(occurrence.has_value());
    const auto ordinary = CreationTreeContext::Resolve(file, header, index, lookup, roles, sites, *occurrence, identity);
    Check(std::get<CreationContextError>(std::get<CreationContextFailure>(ordinary).error) == CreationContextError::kKind);
    auto wrong = call; ++wrong.receiver.value;
    Check(std::holds_alternative<ClassInitializerReadFailure>(
        ReadClassInitializerFunction(file, header, index, lookup, roles, sites, parent, wrong)));
    std::size_t fields = 0, object_fields = 0;
    const auto& graph = initializer.function.values.graph;
    const auto constants = ResolveTreeConstants(file, header, index, site->target, graph);
    Check(std::holds_alternative<TreeConstants>(constants));
    const auto& resolved = std::get<TreeConstants>(constants);
    std::size_t arrays = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !array || !array->literal) continue;
      const auto* constant = std::get_if<AggregateConstant>(&resolved.operations[i]);
      Check(constant && constant->kind == AggregateKind::kArray);
      if (entry == 7312) Check(constant->count == 4);
      ++arrays;
    }
    if (entry == 7312) Check(arrays == 1);
    for (const auto& op : graph.ops) {
      if (op.life != analysis::ValueLife::kLive) continue;
      if (const auto* field = std::get_if<analysis::ValueOwnWrite>(&op.operation)) {
        Check(field->kind == analysis::OwnWriteKind::kDefineField);
        Check(op.use_count == 2);
        const auto& base = graph.uses[op.first_use.value];
        Check(base.role == analysis::OperandRole::kBase);
        const auto* value = std::get_if<analysis::ValueId>(&base.operand); Check(value);
        const auto* origin = std::get_if<analysis::EntryDefinition>(&graph.values[value->value].origin);
        if (origin && std::holds_alternative<analysis::ReceiverEntry>(*origin)) {
          ++fields;
        } else {
          const auto* produced = std::get_if<analysis::OpResult>(&graph.values[value->value].origin);
          Check(produced && produced->op.value < graph.ops.size());
          Check(std::holds_alternative<analysis::ValueObjectAllocation>(graph.ops[produced->op.value].operation));
          ++object_fields;
        }
      }
    }
    Check(fields > 0 && (entry != 7312 || fields == 3));
    {
      auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index,
          initializer.context, initializer.function.body, graph, {1}));
      auto bound = std::get<ModuleAliases>(BindModuleAliases(file, header, index, initializer.function.input,
          initializer.function.values, modules, {owner->offset.value},
          {static_cast<std::uint32_t>(syntax.syntax.names.size())}));
      syntax.syntax.modules = bound.aliases;
      for (const auto& name : bound.names) syntax.syntax.names.push_back(name);
      const auto constants_view = syntax.View();
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, bound.evaluations);
      if (const auto* error = std::get_if<EvaluationError>(&evaluated)) {
        const auto literal = BuildLiteralPlan(graph);
        if (const auto* error = std::get_if<LiteralError>(&literal)) std::cerr << "literal failure=" << static_cast<unsigned>(*error) << '\n';
        std::cerr << "evaluation failure=" << static_cast<unsigned>(*error) << '\n'; return 1;
      }
      const auto& evaluation = std::get<EvaluationPlan>(evaluated);
      const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax.syntax), constants_view);
      if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) {
        std::cerr << "tree failure=" << static_cast<unsigned>(*error) << '\n'; return 1;
      }
      const auto& tree = std::get<ConstructedFunction>(built).tree;
      std::ostringstream denied;
      Check(std::get<TreePrintError>(PrintFunctionTree(tree, denied)) == TreePrintError::kUnsupportedContext);
      auto wrong_tree = tree;
      for (auto& expression : wrong_tree.expressions) {
        if (const auto* field = std::get_if<FieldDefinitionExpr>(&expression.expression)) {
          expression.expression = AssignmentExpr{MemberTarget{field->receiver, field->name}, field->value};
          break;
        }
      }
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control,
          evaluation, wrong_tree, constants_view)));
      std::ostringstream wrong_output;
      Check(std::get<TreePrintError>(PrintStaticFields(wrong_tree, wrong_output)) == TreePrintError::kUnsupportedContext);
      Check(wrong_output.str().empty());
      std::ostringstream printed;
      if (!tree.modules.empty()) {
        std::ostringstream missing, wrong_record, shadowed;
        Check(std::get<TreePrintError>(PrintStaticFields(tree, missing)) == TreePrintError::kUnsupportedContext);
        Check(std::get<TreePrintError>(PrintStaticFields(tree, wrong_record, {}, {}, 0, &scope,
            abc::EntityOffset{owner->offset.value + 1})) == TreePrintError::kUnsupportedContext);
        auto shadow = scope; shadow.class_binding = "BreakpointType";
        Check(std::get<TreePrintError>(PrintStaticFields(tree, shadowed, {}, {}, 0, &shadow,
            owner->offset)) == TreePrintError::kUnsupportedContext);
        Check(missing.str().empty() && wrong_record.str().empty() && shadowed.str().empty());
      }
      const auto result = PrintStaticFields(tree, printed, {}, {}, 0, &scope, owner->offset);
      if (const auto* error = std::get_if<TreePrintError>(&result)) {
        std::cerr << "print failure=" << static_cast<unsigned>(*error) << '\n'; return 1;
      }
      Check(tree.modules.empty() || std::get<PrintedFunction>(result).module_binding.has_value());
      if (argc == 3) { std::cout << printed.str(); return 0; }
    }
    std::cout << "entry=" << entry << " initializer=" << site->target.value << " own_fields=" << fields << " buffered_arrays=" << arrays << " object_fields=" << object_fields << '\n';
  }
}
