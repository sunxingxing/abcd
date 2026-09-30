#include "../../src/source/class_initializer_file.h"
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
      if (const auto* error = std::get_if<FunctionTreeError>(&built)) {
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
