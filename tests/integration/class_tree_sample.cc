#include "../../src/source/class_tree.h"
#include "../../src/source/family_route_failure.h"
#include "../../src/source/evaluation_plan.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "abcd/corpus/abc_file.h"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <string_view>
namespace {
void Check(bool value) { if (!value) std::abort(); }
std::uint32_t Number(const char* value) {
  std::string_view text{value}; std::uint32_t result;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  Check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()); return result;
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  Check(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--tree"));
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto hash = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  const auto creator = lookup.Find({Number(argv[2])}); Check(creator.has_value());
  RouteInputIdentity identity; identity.profile = header.profile;
  identity.abc_version = header.version; identity.file_size = header.file_size;
  auto caller = ReadFunctionValues(file, header, index, *creator, identity, InputCaptureMode::kClosed);
  if (!std::holds_alternative<FileFunction>(caller)) { std::cerr << "caller failed\n"; return 1; }
  const auto& function = std::get<FileFunction>(caller);
  const auto ordinal = Number(argv[3]); Check(ordinal < function.values.classes.classes.size());
  const auto operation = function.values.classes.classes[ordinal].operation;
  // The unchanged full record graph must also schedule this creation. A
  // detached class body alone does not exercise heritage or surrounding order.
  const auto evaluated = BuildEvaluationPlan(function.values.graph);
  if (const auto* error = std::get_if<EvaluationError>(&evaluated)) {
    std::cerr << "caller evaluation failure " << static_cast<unsigned>(*error) << '\n'; return 1;
  }
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(function.values.graph, evaluation)));
  Check(evaluation.operations[operation.value].kind == EvaluationKind::kRoot ||
      evaluation.operations[operation.value].kind == EvaluationKind::kInline);
  auto layout = BuildClassLayout(file, header, index, lookup, roles, classes, function, operation, identity);
  if (!std::holds_alternative<ClassTree>(layout)) {
    std::cerr << "class layout cannot be built before its bodies\n"; return 1;
  }
  std::ostringstream pending;
  Check(std::get<TreePrintError>(PrintClassTree(std::get<ClassTree>(layout), pending)) ==
      TreePrintError::kUnsupportedContext);
  Check(pending.str().empty());
  const auto prepare = [&]() -> ClassTree {
    auto built = BuildClassTree(file, header, index, lookup, roles, functions, classes, function, operation, identity);
    if (const auto* failure = std::get_if<ClassTreeFailure>(&built)) {
      std::cerr << "class tree failure " << failure->index();
      if (const auto* family = std::get_if<FamilyPrepareFailure>(failure)) {
        const auto reason = FamilyRouteFailure(*family);
        std::cerr << " family " << family->cause.index() << " stage " << static_cast<unsigned>(reason.reason);
      }
      std::cerr << '\n'; std::exit(1);
    }
    return std::get<ClassTree>(std::move(built));
  };
  const auto& layout_tree = std::get<ClassTree>(layout);
  if (argc == 5) {
    // A synthetic creation-only callable exercises AST composition using the
    // actual ABC-derived definition. It is not the original complete record.
    analysis::ValueProgram graph;
    const auto effects = analysis::LanguageEffect::kAllocation | analysis::LanguageEffect::kUserCode;
    const auto creation = function.values.graph.ops[operation.value].operation;
    graph.values = {{analysis::OpResult{{0}}, analysis::ValueKinds::Hole(), analysis::UseId{0}},
        {analysis::OpResult{{1}}, analysis::ValueKinds::Language(), analysis::UseId{1}}};
    graph.origins = {{{0}, {0}, {}}, {{1}, layout_tree.creation.pc, {}}, {{2}, {layout_tree.creation.pc.value + 1}, {}}};
    graph.ops = {
        {analysis::ValueHole{}, {0}, 0, {0}, 0, analysis::ValueId{0}, {}, {}, analysis::ExceptionalCompletion::kImpossible, {0}},
        {creation, {0}, 1, {0}, 1, analysis::ValueId{1}, effects, effects, analysis::ExceptionalCompletion::kMayThrow, {1}},
        {analysis::ValueReturn{}, {0}, 2, {1}, 1, {}, {}, {}, analysis::ExceptionalCompletion::kImpossible, {2}}};
    graph.uses = {{analysis::OpId{1}, analysis::OperandRole::kHeritage, analysis::ReadPurpose::kProtocol, analysis::ValueId{0}},
        {analysis::OpId{2}, analysis::OperandRole::kReturnValue, analysis::ReadPurpose::kOperand, analysis::ValueId{1}}};
    graph.blocks = {{{0}, 3, {0}, 0}};
    std::vector<ExpressionConstant> constants(3);
    auto definition = std::make_shared<ClassTree>(std::get<ClassTree>(std::move(layout)));
    constants[1] = definition;
    const auto plan = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = index.methods[creator->value].offset;
    syntax.owner = {1}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    auto constructed = BuildFunctionTree(graph, control, plan, syntax, {constants});
    if (const auto* error = std::get_if<FunctionTreeFailure>(&constructed)) {
      std::cerr << "creation AST failure " << static_cast<unsigned>(*error) << '\n'; return 1;
    }
    const auto& root = std::get<ConstructedFunction>(constructed).tree;
    Check(!root.hole);
    auto wrong_owner = root;
    ++wrong_owner.method.value;
    std::ostringstream rejected;
    Check(std::get<TreePrintError>(PrintFunctionTree(wrong_owner, rejected)) == TreePrintError::kUnsupportedContext);
    auto missing = constants;
    missing[1] = std::monostate{};
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, plan, syntax, {missing})));
    auto wrong_creation = graph;
    ++std::get<analysis::ValueClassCreation>(wrong_creation.ops[1].operation).constructor.value;
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(wrong_creation, control, plan, syntax, {constants})));
    std::ostringstream unprepared;
    Check(std::get<TreePrintError>(PrintFunctionTree(root, unprepared)) == TreePrintError::kUnsupportedContext);
    // Prepare only after the parent AST, then fill its original ClassExpr.
    // No placeholder body is printed, and final semantics see actual bodies.
    *definition = prepare();
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, plan, root, {constants})));
    auto missing_member = std::move(definition->members.back());
    definition->members.pop_back();
    std::ostringstream missing_body;
    Check(std::get<TreePrintError>(PrintClassTree(*definition, missing_body)) == TreePrintError::kUnsupportedContext);
    Check(missing_body.str().empty());
    definition->members.push_back(std::move(missing_member));
    auto constructor = std::move(definition->constructor);
    definition->constructor.reset();
    std::ostringstream missing_constructor;
    Check(std::get<TreePrintError>(PrintClassTree(*definition, missing_constructor)) == TreePrintError::kUnsupportedContext);
    Check(missing_constructor.str().empty());
    definition->constructor = std::move(constructor);
    definition->callable = CallableKind::kDerivedConstructor;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, plan, root, {constants})));
    definition->callable = CallableKind::kBaseConstructor;
    const auto printed = PrintFunctionTree(root, std::cout);
    if (const auto* error = std::get_if<TreePrintError>(&printed)) {
      std::cerr << "creation AST print failure " << static_cast<unsigned>(*error) << '\n'; return 1;
    }
    const auto& positions = std::get<PrintedFunction>(printed);
    Check(positions.class_members.size() == 3);
    for (const auto& member : positions.class_members) {
      Check(member.enclosing_offset < positions.bytes);
      for (const auto& range : member.expressions)
        Check(range.begin <= range.end && member.enclosing_offset + range.end <= positions.bytes);
      for (const auto& range : member.statements)
        Check(range.begin <= range.end && member.enclosing_offset + range.end <= positions.bytes);
    }
    return 0;
  }
  const auto tree = prepare();
  std::ostringstream output;
  const auto printed = PrintClassTree(tree, output);
  if (!std::holds_alternative<PrintedClassTree>(printed)) {
    std::cerr << "class print failure " << printed.index() << '\n'; return 1;
  }
  const auto& positions = std::get<PrintedClassTree>(printed);
  Check(positions.bytes == output.str().size() && positions.members.size() == tree.members.size() + 1);
  std::size_t last = 0;
  for (const auto& member : positions.members) {
    Check(member.offset >= last && member.offset + member.positions.bytes <= positions.bytes);
    last = member.offset + member.positions.bytes;
  }
  std::ostringstream limited;
  const auto small = PrintClassTree(tree, limited, {1});
  Check(std::get<TreePrintError>(small) == TreePrintError::kOutputLimit);
  std::cout << output.str();
}
