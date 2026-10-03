#include "../../../src/source/function_values.h"
#include "../../../src/source/class_tree.h"
#include <unistd.h>
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_edits.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/value_import.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>
#include <bit>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Form { kEmpty, kBuffered, kComputed, kShared, kParameter, kObject, kLateConstructor, kLateBuffered, kUnknownConstructor };
std::uint8_t Op(abc::Opcode code) { return static_cast<std::uint8_t>(code); }
abc::MethodBody Body(bool wide, std::uint32_t count, std::uint32_t arguments,
                     Form form, bool caught, bool member, std::vector<std::uint8_t>* encoded = nullptr) {
  // Locals: array, retained constructor, retained construction result.
  // Parameters: constructor/provider and element getter/provider.
  constexpr std::uint8_t kArray = 0, kConstructor = 1, kSaved = 2, kProvider = 6, kElements = 7;
  std::vector<std::uint8_t> code{Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), kSaved,
      Op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8), 0, 0, 0, 1, 0, 2, 0, kSaved,
      Op(abc::Opcode::kSTA_V8), kConstructor};
  const auto constructor = [&] {
    code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kProvider,
        Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
    if (form == Form::kUnknownConstructor) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kConstructor});
  };
  for (std::uint32_t c = 0; c < count; ++c) {
    if (member && form != Form::kLateConstructor && form != Form::kLateBuffered) constructor();
    if (form == Form::kParameter) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kElements});
    else if (form == Form::kObject) code.push_back(Op(abc::Opcode::kCREATEEMPTYOBJECT));
    else if (form == Form::kBuffered || form == Form::kLateBuffered) {
      code.insert(code.end(), {Op(abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16), 0, 2, 0});
      if (form == Form::kLateBuffered) {
        code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kArray}); constructor();
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kArray});
      }
    }
    else {
      code.insert(code.end(), {Op(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0,
          Op(abc::Opcode::kSTA_V8), kArray});
      if (form == Form::kComputed || form == Form::kLateConstructor)
        for (std::uint32_t a = 0; a < arguments; ++a) {
          code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kElements,
              Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 4, 0,
              Op(abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16), 0, kArray,
              static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(a >> 8)});
        }
      if (member && form == Form::kLateConstructor) constructor();
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kArray});
    }
    if (form == Form::kShared) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kArray});
    code.insert(code.end(), {Op(wide ? abc::Opcode::kNEWOBJAPPLY_IMM16_V8 : abc::Opcode::kNEWOBJAPPLY_IMM8_V8), 7});
    if (wide) code.push_back(1);
    code.push_back(form == Form::kUnknownConstructor && !member ? kProvider : kConstructor);
    if (form == Form::kShared) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kSaved,
        Op(abc::Opcode::kLDA_V8), kArray});
  }
  code.push_back(Op(abc::Opcode::kRETURN));
  const auto handler = static_cast<std::uint32_t>(code.size());
  if (caught) code.push_back(Op(abc::Opcode::kRETURN));
  if (encoded) *encoded = code;
  auto body = test::Body(code);
  if (caught) {
    abc::TryRegion region; region.offset = {14}; region.length = handler - region.offset.value; region.handler_count = 1;
    abc::CatchHandler catch_handler; catch_handler.offset = {handler}; catch_handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(catch_handler);
  }
  return body;
}
// Class bodies are decoded and prepared from an unchanged file. The main
// low-level phase fixture adds its explicit handler graph; the class creation
// precedes that region and shares the normal file's exact original site.
struct ClassFile {
  static constexpr std::uint32_t kPool = 64, kClass = 160, kArray = 192,
      kCaller = 256, kCtor = 300, kName = 1536, kValue = 1552,
      kCtorCode = 2048, kCallerCode = 4096;
  std::unique_ptr<corpus::AbcFile> file;
  abc::Header header;
  abc::MetadataIndex index;
  std::string path;
  ClassFile(std::span<const std::uint8_t> code, abc::ProfileId profile, std::uint32_t arguments) {
    std::vector<std::uint8_t> bytes(kCallerCode + code.size() + 16);
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const std::uint32_t pool[]{kCtor, kClass, kArray, kName, kValue};
    for (std::size_t p = 0; p < std::size(pool); ++p) word(kPool + p * sizeof(std::uint32_t), pool[p]);
    word(kClass, 2); bytes[kClass + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kClass + 5, 0);
    word(kArray, arguments * 2);
    for (std::uint32_t a = 0; a < arguments; ++a) {
      bytes[kArray + 4 + a * 5] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
      word(kArray + 5 + a * 5, a + 7);
    }
    const auto text = [&](std::size_t at, std::string_view value) {
      bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
      std::copy(value.begin(), value.end(), bytes.begin() + at);
    };
    text(kName, "ctor"); text(kValue, "value");
    const auto write_body = [&](std::uint32_t at, std::uint32_t locals, std::uint32_t args,
                                std::span<const std::uint8_t> payload) {
      auto cursor = at;
      const auto uleb = [&](std::uint32_t value) {
        do {const auto byte = static_cast<std::uint8_t>(value & 0x7f); value >>= 7;
          bytes[cursor++] = byte | (value ? 0x80 : 0); } while (value);
      };
      uleb(locals); uleb(args); uleb(static_cast<std::uint32_t>(payload.size())); uleb(0);
      std::copy(payload.begin(), payload.end(), bytes.begin() + cursor);
      return abc::CodeInfo{{at}, {cursor}, locals, args, static_cast<std::uint32_t>(payload.size()), 0};
    };
    const std::uint8_t ctor[]{Op(abc::Opcode::kLDA_V8), 3, Op(abc::Opcode::kRETURN)};
    index.methods.resize(2);
    index.methods[0].offset = {kCaller}; index.methods[0].owner_offset = {128};
    index.methods[0].name = "factory"; index.methods[0].name_utf16_length = 7;
    index.methods[0].code = write_body(kCallerCode, 3, 5, code);
    index.methods[1].offset = {kCtor}; index.methods[1].owner_offset = {128};
    index.methods[1].name = "Known"; index.methods[1].name_utf16_length = 5;
    index.methods[1].code = write_body(kCtorCode, 0, 5, ctor);
    index.regions.push_back({kCaller, kCallerCode, {}, {std::size(pool), kPool}, {}, {}});
    char temporary[] = "/tmp/abcd-apply-class-XXXXXX";
    const auto fd = mkstemp(temporary); test::Check(fd >= 0);
    test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); test::Check(close(fd) == 0);
    path = temporary; file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
    header.kind = abc::AbcKind::kDynamic; header.profile = profile; header.file_size = bytes.size(); header.layout = abc::DynamicLayout{};
    header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
        profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0};
  }
  ~ClassFile() {test::Check(unlink(path.c_str()) == 0);}
};
void Run(abc::ProfileId profile, bool wide, bool phased, std::uint32_t count,
         std::uint32_t arguments, Form form, bool member, bool print = false) {
  const auto body = Body(wide, count, arguments, form, phased, member);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 5, {true, true, true, true}));
  std::vector<std::uint8_t> encoded;
  const auto normal_body = Body(wide, count, arguments, form, false, member, &encoded);
  const ClassFile class_file(encoded, profile, arguments);
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(class_file.file->Fingerprint()), class_file.header);
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(class_file.index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(*class_file.file, class_file.header, class_file.index, lookup));
  const auto functions = std::get<FunctionCreationSites>(FunctionCreationSites::Build(*class_file.file, class_file.header, class_file.index, lookup));
  const auto classes = std::get<ClassMethodSites>(ClassMethodSites::Build(*class_file.file, class_file.header, class_file.index, lookup));
  const auto creator_result = ReadFunctionValues(*class_file.file, class_file.header, class_file.index, {0}, identity, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FileFunction>(creator_result));
  const auto& creator = std::get<FileFunction>(creator_result);
  test::Check(creator.values.classes.classes.size() == 1);
  const auto class_operation = creator.values.classes.classes.front().operation;
  auto class_result = BuildClassTree(*class_file.file, class_file.header, class_file.index, lookup, roles, functions, classes, creator, class_operation, identity);
  test::Check(std::holds_alternative<ClassTree>(class_result));
  const auto definition = std::make_shared<ClassTree>(std::get<ClassTree>(std::move(class_result)));
  if (count <= 2) {
    const auto scope = std::get<ClassParentScope>(CheckClassParentScope(*definition));
    test::Check(scope.independent && scope.work != 0);
    test::Check(std::get<ClassTreeError>(CheckClassParentScope(*definition, scope.work - 1)) == ClassTreeError::kWorkLimit);
    test::Check(std::get<ClassParentScope>(CheckClassParentScope(*definition, scope.work)).independent);
    auto constructor = std::move(definition->constructor); definition->constructor.reset();
    test::Check(!std::get<ClassParentScope>(CheckClassParentScope(*definition)).independent);
    definition->constructor = std::move(constructor);
  }
  ValueProgram graph;
  std::optional<ExceptionFlow> phases;
  std::optional<ExceptionSsa> ssa;
  std::optional<EnvironmentFlow> env;
  std::optional<ExceptionValueFlow> domains;
  std::optional<ExceptionInputProof> proof;
  std::optional<PhasedValueInput> input;
  if (phased) {
    phases = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
    IdfQuota work{kDefaultSemanticWork};
    ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(*phases, frame.registers, work));
    std::vector<EntryValueFact> entries;
    for (std::size_t i = 0; i < ssa->Values().size(); ++i) {
      const auto& value = ssa->Values()[i]; const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
        entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
    }
    env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(*phases));
    domains = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(*phases, *ssa, entries,
        work, {}, PendingExceptionValues::kLanguage));
    proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(*phases, *ssa, *env, *domains, kDefaultSemanticWork, nullptr, ExceptionInputDomain::kHoleTransport));
    input.emplace(PhasedValueInput{*phases, *ssa, *env, *domains, *proof, frame});
    graph = std::get<ValueProgram>(ImportPhasedValues(*input));
    test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, *input)));
  } else {
    const auto original = std::get<FunctionInput>(FunctionInput::Build(identity, {128}, {256}, body,
        frame, InputCaptureMode::kClosed));
    graph = std::get<FunctionValues>(BuildFunctionValues(original, body, frame)).graph;
  }
  std::vector<OpId> calls;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (const auto* apply = std::get_if<ValueConstructApply>(&graph.ops[i].operation)) {
    const auto& op = graph.ops[i]; calls.push_back({static_cast<std::uint32_t>(i)});
    test::Check(op.life == ValueLife::kLive && op.use_count == 2 && op.result &&
        apply->profile_slot == (wide ? 263 : 7) &&
        op.normal == (LanguageEffect::kAllocation | LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite | LanguageEffect::kUserCode) &&
        op.before_throw == op.normal && op.exceptional == ExceptionalCompletion::kMayThrow);
    auto stripped = op; stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible; test::Check(!CanErasePureOp(stripped));
  }
  test::Check(calls.size() == count);
  const auto scheduled = phased ? BuildPhasedEvaluationPlan(graph) : BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto evaluation = std::get<EvaluationPlan>(scheduled);
  const auto planned = phased ? BuildPhasedCallPlan(graph, evaluation) : BuildCallPlan(graph, evaluation);
  if (form == Form::kShared || form == Form::kParameter || form == Form::kObject) {
    test::Check(std::get<CallPlanError>(planned) == CallPlanError::kArrayArguments); return;
  }
  if (form == Form::kUnknownConstructor) {
    test::Check(std::get<CallPlanError>(planned) == CallPlanError::kConstructorIdentity); return;
  }
  test::Check(std::holds_alternative<CallPlan>(planned));
  const auto& plan = std::get<CallPlan>(planned);
  if (count <= 2) {
    auto wrong = plan; std::get<ConstructCall>(wrong.operations[calls.front().value]).array.reset();
    const auto checked = phased ? VerifyPhasedCallPlan(graph, evaluation, wrong) : VerifyCallPlan(graph, evaluation, wrong);
    test::Check(std::holds_alternative<CallPlanError>(checked));
    wrong = plan; std::get<ConstructCall>(wrong.operations[calls.front().value]).constructor.reset();
    const auto missing_constructor = phased ? VerifyPhasedCallPlan(graph, evaluation, wrong) : VerifyCallPlan(graph, evaluation, wrong);
    test::Check(std::get<CallPlanError>(missing_constructor) == CallPlanError::kConstructorIdentity);
  }
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {256};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"Ctor", u"input", u"ctor", u"value"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  for (std::uint32_t a = 0; a < arguments; ++a) {
    const auto index = std::to_string(a); syntax.names.emplace_back(index.begin(), index.end());
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  std::vector<ConstantProperty> properties;
  if (form == Form::kBuffered || form == Form::kLateBuffered) for (std::uint32_t a = 0; a < arguments; ++a)
    properties.push_back({NameId{a + 4}, abcd::source::IntegerLiteral{static_cast<std::int32_t>(a + 7)}});
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value == 3 ? 2U : 3U};
    if (std::holds_alternative<ValueClassCreation>(graph.ops[i].operation)) constants[i] = definition;
    if (const auto* write = std::get_if<ValueIndexedWrite>(&graph.ops[i].operation)) constants[i] = NameId{write->index + 4};
    if (const auto* array = std::get_if<ValueArrayAllocation>(&graph.ops[i].operation); array && array->literal)
      constants[i] = AggregateConstant{0, arguments, AggregateKind::kArray};
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  FunctionTree tree;
  std::optional<ControlPlan> control;
  std::optional<ExceptionPlan> regions;
  std::optional<ExceptionControl> exception_control;
  std::optional<ExceptionTreeInput> context;
  if (phased) {
    regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, *input));
    exception_control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, *input, *regions));
    context.emplace(ExceptionTreeInput{body, *input, *regions, *exception_control});
    const auto built = BuildExceptionFunctionDraft(graph, *context, evaluation, syntax, resolved, {});
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built)); tree = std::get<ExceptionFunctionDraft>(built).tree;
  } else {
    control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto built = BuildFunctionTree(graph, *control, evaluation, syntax, resolved);
    test::Check(std::holds_alternative<ConstructedFunction>(built)); tree = std::get<ConstructedFunction>(built).tree;
  }
  const auto verify = [&](const FunctionTree& candidate) {
    return phased ? VerifyExceptionTreeSemantics(graph, *context, evaluation, candidate, resolved, {})
        : VerifyTreeSemantics(graph, *control, evaluation, candidate, resolved);
  };
  test::Check(std::holds_alternative<TreeSemantics>(verify(tree)) && tree.recovery_issues.empty());
  test::Check(std::count_if(tree.expressions.begin(), tree.expressions.end(), [](const auto& node) {
    const auto* construct = std::get_if<ConstructExpr>(&node.expression);
    return construct != nullptr;
  }) == count);
  if (count <= 2) {
    auto wrong = tree;
    for (auto& node : wrong.expressions) if (auto* construct = std::get_if<ConstructExpr>(&node.expression))
      construct->arguments = ArgumentRange{0, 0};
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    wrong = tree; wrong.identity.contract = SourceContract::kLanguageAndHostV1;
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    if (arguments >= 2 && form != Form::kEmpty) {
      wrong = tree; std::swap(wrong.properties[0].value, wrong.properties[1].value);
      test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    }
    if (form == Form::kLateConstructor || form == Form::kLateBuffered) {
      wrong = tree;
      for (auto& node : wrong.expressions) if (const auto* construct = std::get_if<ConstructExpr>(&node.expression)) {
        const auto& range = std::get<ArgumentRange>(construct->arguments);
        if (!range.count) continue;
        const auto& member = std::get<MemberExpr>(wrong.expressions[wrong.arguments[range.first].value].expression);
        const auto key = std::get<ComputedKey>(member.key).expression;
        std::get<TreeLiteral>(wrong.expressions[key.value].expression) = NumberLiteral{std::bit_cast<std::uint64_t>(1.0)};
      }
      test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    }
  }
  std::ostringstream output;
  const auto printed = PrintFunctionTree(tree, output);
  test::Check(std::holds_alternative<PrintedFunction>(printed));
  test::Check(output.str().find("...") == std::string::npos && output.str().find("Reflect.") == std::string::npos);
  const auto& positions = std::get<PrintedFunction>(printed);
  for (const auto& node : tree.expressions) if (const auto* construct = std::get_if<ConstructExpr>(&node.expression)) {
    if (const auto* arguments = std::get_if<ArrayArguments>(&construct->arguments)) {
      const auto span = positions.expressions[arguments->array.value];
      test::Check(span.begin <= span.end && span.end <= output.str().size());
      if (form == Form::kEmpty) test::Check(span.begin == span.end);
    }
  }
  if (print) std::cout << output.str();
}
void Admission(abc::ProfileId profile, bool wide) {
  const auto body = Body(wide, 1, 0, Form::kEmpty, false, false);
  const auto& site = body.instructions[body.instructions.size() - 2];
  const auto lifted = LiftSemanticInstruction(site, profile, 5);
  test::Check(std::holds_alternative<SemanticInstruction>(lifted));
  const auto& semantic = std::get<SemanticInstruction>(lifted);
  test::Check(std::get<ApplyConstructorCall>(semantic.operation).constructor == abc::RegisterId{1});
  test::Check(semantic.effects.normal_registers.read_count == 2 &&
      std::holds_alternative<Accumulator>(semantic.effects.normal_registers.reads[1]));
  auto malformed = site; malformed.decoded.operands[1].raw_bits = 5;
  test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, 5)).kind == SemanticError::kMalformedInstruction);
  malformed = site; malformed.decoded.operands[0].raw_bits = wide ? UINT16_MAX + 1ULL : UINT8_MAX + 1ULL;
  test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, 5)).kind == SemanticError::kMalformedInstruction);
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    if (option == "--unknown-refusal" || option == "--unknown-getter-refusal") {
      Run(abc::ProfileId::kDynamic1206, false, false, 1, 0, Form::kUnknownConstructor, option == "--unknown-getter-refusal"); return 0;
    }
    const bool caught = option == "--caught-source" || option == "--late-caught-source";
    const bool late = option == "--late-source" || option == "--late-caught-source";
    const auto form = option == "--empty-source" ? Form::kEmpty : option == "--buffer-source" ? Form::kBuffered :
        late ? Form::kLateConstructor : Form::kComputed;
    Run(abc::ProfileId::kDynamic1206, false, caught, 1, form == Form::kEmpty ? 0 : 2, form,
        form != Form::kEmpty && form != Form::kBuffered, true); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool wide : {false, true}) {
      Admission(profile, wide);
      for (const bool phased : {false, true}) {
        Run(profile, wide, phased, 1, 0, Form::kUnknownConstructor, false);
        Run(profile, wide, phased, 1, 0, Form::kUnknownConstructor, true);
        for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
          Run(profile, wide, phased, count, 0, Form::kEmpty, false);
          Run(profile, wide, phased, count, 2, Form::kBuffered, false);
          Run(profile, wide, phased, count, 2, Form::kComputed, true);
          Run(profile, wide, phased, count, 2, Form::kLateConstructor, true);
          Run(profile, wide, phased, count, 2, Form::kLateBuffered, true);
        }
        for (const auto arguments : {1U, 200U, 201U}) {
          Run(profile, wide, phased, 1, arguments, Form::kComputed, true);
          Run(profile, wide, phased, 1, arguments, Form::kLateConstructor, true);
          Run(profile, wide, phased, 1, arguments, Form::kLateBuffered, true);
        }
        for (const auto form : {Form::kShared, Form::kParameter, Form::kObject, Form::kLateConstructor})
          Run(profile, wide, phased, 1, 2, form, form == Form::kLateConstructor);
      }
    }
}
