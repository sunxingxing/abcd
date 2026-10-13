#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
enum class BinaryCase { kStringAdd, kEqual, kLess, kBitOr, kUnknownAdd };
std::uint64_t Run(abc::ProfileId profile, std::uint32_t count, bool print = false, BinaryCase kind = BinaryCase::kStringAdd, bool barrier = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto key = i + 2;
    bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kSTA_V8), 1});
    if (kind == BinaryCase::kStringAdd) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
    auto binary = abc::Opcode::kADD2_IMM8_V8;
    switch (kind) {
      case BinaryCase::kStringAdd: case BinaryCase::kUnknownAdd: break;
      case BinaryCase::kEqual: binary = abc::Opcode::kSTRICTEQ_IMM8_V8; break;
      case BinaryCase::kLess: binary = abc::Opcode::kLESS_IMM8_V8; break;
      case BinaryCase::kBitOr: binary = abc::Opcode::kOR2_IMM8_V8; break;
    }
    bytes.insert(bytes.end(), {op(binary), 0, 1});
    if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, static_cast<std::uint8_t>(key),
        static_cast<std::uint8_t>(key >> kByteBits), 0});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  if (kind == BinaryCase::kUnknownAdd) {
    test::Check(!std::holds_alternative<FunctionInput>(prepared)); return 0;
  }
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto planned = BuildEvaluationPlan(graph);
  if (barrier) {
    test::Check(std::holds_alternative<EvaluationFailure>(planned)); return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
  if (count == 2) {
    Run(profile, 1, false, kind, true);
    const auto binary = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) {
      return std::holds_alternative<ValueBinary>(op.operation);
    });
    test::Check(binary != graph.ops.end());
    auto shared = graph;
    for (const auto& op : graph.ops) if (std::holds_alternative<ValueReturn>(op.operation))
      test::Check(!ReplaceUse(shared, op.first_use, *binary->result));
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
    test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared)));
    auto forged = evaluation;
    forged.literals->nested[static_cast<std::size_t>(binary - graph.ops.begin())].reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"Lookup", u"left"}; syntax.texts = {u"suffix"};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) constants[i] = NameId{0};
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    if (std::holds_alternative<ValueBinary>(graph.ops[i].operation))
      test::Check(evaluation.operations[i].kind == EvaluationKind::kNestedLiteral);
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  const GlobalScope globals{}; std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &globals)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
enum class OrderedLayout { kContiguous, kSplit, kDiamond, kReverse };
enum class OrderedHazard { kBypass, kRepeat, kPureLoop, kObserve, kBarrier };
void OrderedRefusals(abc::ProfileId profile) {
  const auto wire = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{}));
  const auto condition = static_cast<std::uint8_t>(frame.first_parameter.value);
  // Forward and backward must-reach identities must both agree. Merely seeing
  // one field instruction is no proof of its execution count or availability.
  for (const auto hazard : {OrderedHazard::kBypass,OrderedHazard::kRepeat,OrderedHazard::kPureLoop,
      OrderedHazard::kObserve,OrderedHazard::kBarrier}) {
    std::vector<std::uint8_t> bytes{wire(abc::Opcode::kCREATEEMPTYOBJECT),wire(abc::Opcode::kSTA_V8),0};
    if (hazard == OrderedHazard::kBypass)
      bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),condition,wire(abc::Opcode::kJEQZ_IMM8),12});
    if (hazard == OrderedHazard::kPureLoop)
      bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),condition,wire(abc::Opcode::kJEQZ_IMM8),static_cast<std::uint8_t>(-2)});
    if (hazard == OrderedHazard::kObserve)
      bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),0,wire(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,0,0});
    if (hazard == OrderedHazard::kBarrier)
      bytes.insert(bytes.end(), {wire(abc::Opcode::kJMP_IMM8),2,wire(abc::Opcode::kDEBUGGER)});
    bytes.insert(bytes.end(), {wire(abc::Opcode::kLDAI_IMM32),1,0,0,0,
      wire(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8),0,0,0,0});
    if (hazard == OrderedHazard::kRepeat)
      bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),condition,wire(abc::Opcode::kJEQZ_IMM8),static_cast<std::uint8_t>(-12)});
    bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),0,wire(abc::Opcode::kRETURN)});
    const auto body = test::Body(bytes);
    RouteInputIdentity identity; identity.profile = profile;
    const auto prepared = FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInput>(prepared));
    const auto& input = std::get<FunctionInput>(prepared);
    const auto values = std::get<FunctionValues>(BuildFunctionValues(input,body,frame));
    test::Check(std::holds_alternative<EvaluationFailure>(BuildEvaluationPlan(values.graph)));
  }
}
std::uint64_t Ordered(abc::ProfileId profile, std::uint32_t count, OrderedLayout layout, bool print = false) {
  const auto wire = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  constexpr unsigned kByteBits = 8;
  constexpr std::uint32_t kLocals = 2;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(kLocals,1,{}));
  std::vector<std::uint8_t> bytes{wire(abc::Opcode::kCREATEEMPTYOBJECT), wire(abc::Opcode::kSTA_V8),0};
  // Register copies vary allocation transport independently of block layout.
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8),0,wire(abc::Opcode::kSTA_V8),1,
      wire(abc::Opcode::kLDA_V8),1,wire(abc::Opcode::kSTA_V8),0});
  const auto field = [&](std::uint32_t key) {
    bytes.push_back(wire(abc::Opcode::kLDAI_IMM32));
    for (unsigned byte = 0; byte < sizeof(key); ++byte) bytes.push_back(static_cast<std::uint8_t>(key >> (byte*kByteBits)));
    bytes.insert(bytes.end(),{wire(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8),0,
      static_cast<std::uint8_t>(key),static_cast<std::uint8_t>(key >> kByteBits),0});
  };
  const auto jump = [&]() {
    const auto pc = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {wire(abc::Opcode::kJMP_IMM32),0,0,0,0}); return pc;
  };
  const auto patch = [&](std::uint32_t at, std::uint32_t target) {
    const auto delta = static_cast<std::uint32_t>(static_cast<std::int32_t>(target)-static_cast<std::int32_t>(at));
    for (unsigned byte = 0; byte < sizeof(delta); ++byte) bytes[at+1+byte] = static_cast<std::uint8_t>(delta >> (byte*kByteBits));
  };
  if (layout == OrderedLayout::kReverse) {
    const auto entry = jump(), returned = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(),{wire(abc::Opcode::kLDA_V8),0,wire(abc::Opcode::kRETURN)});
    std::uint32_t next = returned;
    for (std::uint32_t i = count; i-- > 0;) {
      const auto current = static_cast<std::uint32_t>(bytes.size());
      field(i); const auto transfer = jump(); patch(transfer,next); next = current;
    }
    patch(entry,next);
  } else {
    for (std::uint32_t i = 0; i < count; ++i) {
      if (layout == OrderedLayout::kSplit)
        bytes.insert(bytes.end(),{wire(abc::Opcode::kJMP_IMM8),2});
      if (layout == OrderedLayout::kDiamond)
        bytes.insert(bytes.end(),{wire(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(frame.first_parameter.value),
          wire(abc::Opcode::kJEQZ_IMM8),4,wire(abc::Opcode::kJMP_IMM8),4,wire(abc::Opcode::kJMP_IMM8),2});
      field(i);
    }
    bytes.insert(bytes.end(),{wire(abc::Opcode::kLDA_V8),0,wire(abc::Opcode::kRETURN)});
  }
  const auto body = test::Body(bytes);
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input,body,frame));
  const auto& graph = values.graph;
  const auto evaluated = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  test::Check(evaluation.literals && evaluation.literals->objects.size() == 1 &&
    evaluation.literals->objects[0].form == LiteralForm::kExpression);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph,evaluation)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method();
  syntax.owner = {0}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.push_back({NameId{0},NameEvidence::kDebugRange}); syntax.names = {u"condition"};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"}+std::to_string(i);
    syntax.names.push_back(i ? std::u16string(name.begin(),name.end()) : u"__proto__");
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* write = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{write->name.value+1};
  const ExpressionConstants resolved{constants,syntax.names.size(),0};
  const auto built = BuildFunctionTree(graph,control,evaluation,syntax,resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.properties.size() == count);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,tree,resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input,graph,tree)));
  std::ostringstream output; const GlobalScope globals{};
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {}, &globals)));
  if (print) std::cout << output.str() << ",\n";
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--ordered-sources") {
    std::cout << "[\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
      for (const auto layout : {OrderedLayout::kContiguous,OrderedLayout::kSplit,OrderedLayout::kDiamond,OrderedLayout::kReverse})
        for (const auto count : {1U,2U,200U,201U,402U}) Ordered(profile,count,layout,true);
    std::cout << "]\n"; return 0;
  }
  if (argc == 2) {
    const auto option = std::string_view(argv[1]);
    test::Check(option == "--source" || option == "--equal-source" || option == "--less-source" || option == "--bit-source");
    const auto kind = option == "--equal-source" ? BinaryCase::kEqual : option == "--less-source" ?
        BinaryCase::kLess : option == "--bit-source" ? BinaryCase::kBitOr : BinaryCase::kStringAdd;
    Run(abc::ProfileId::kDynamic1206, 2, true, kind); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    OrderedRefusals(profile);
    for (const auto layout : {OrderedLayout::kContiguous,OrderedLayout::kSplit,OrderedLayout::kDiamond,OrderedLayout::kReverse}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U,2U,200U,201U,402U}) {
        const auto work = Ordered(profile,count,layout);
        if (count == 402) test::Check(work < previous*3);
        previous = work;
      }
    }
    for (const auto kind : {BinaryCase::kEqual, BinaryCase::kLess, BinaryCase::kBitOr, BinaryCase::kUnknownAdd})
      Run(profile, 2, false, kind);
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, count);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
