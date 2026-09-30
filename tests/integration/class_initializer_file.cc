#include "../../src/source/class_initializer_file.h"
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
