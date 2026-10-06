#include "../../../src/source/function_tree.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/instruction_sources.h"
#include "../../support/analysis/ssa_fixture.h"
#include "../../support/instruction_positions.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
enum class Form { kDirect, kReversedOperands, kNegated, kMissingUndefined, kDifferentResult, kCopiedSplit, kCallProducer, kComputed,
  kMemberCall, kComputedCall, kValueCall, kDifferentCallBase, kArgumentCall, kOutsideCall,
  kReversedMemberCall, kCopiedMemberCall, kNegatedValueCall, kValueArgumentCall,
  kMemberChain, kComputedChain, kMemberChainCall, kComputedChainCall, kCopiedChainCall,
  kDiscardedChainCall, kDiscardedComputedChainCall, kDiscardedCopiedChainCall, kDiscardedValueCall, kVoidViaResultJoin };
bool ChainForm(Form form) {return form>=Form::kMemberChain && form!=Form::kDiscardedValueCall;}
bool CallForm(Form form) { return form >= Form::kMemberCall && form != Form::kMemberChain && form != Form::kComputedChain; }
abc::MethodBody Body(unsigned count, Form form, abc::ProfileId profile) {
  const std::uint8_t parameter = CallForm(form) ? 3 : 2;
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto read = [&](std::uint8_t name) {
    load(parameter); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, name, 0});
  };
  const auto jump = [&](abc::Opcode opcode) {
    const auto pc = bytes.size(); emit(opcode); const auto operand = bytes.size();
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return std::pair{pc, operand};
  };
  const auto patch = [&](auto branch, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - branch.first);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[branch.second + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  read(0);
  if (form == Form::kCallProducer) {
    emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 2});
  }
  store(0);
  std::vector<std::pair<std::size_t, std::size_t>> exits;
  for (unsigned i = 0; i < count; ++i) for (const bool null : {true, false}) {
    if (!null && form == Form::kMissingUndefined) continue;
    if (form == Form::kCopiedSplit || form == Form::kCopiedMemberCall || form == Form::kCopiedChainCall || form == Form::kDiscardedCopiedChainCall) {
      load(0); store(1);
      const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size());
    }
    emit(null ? abc::Opcode::kLDNULL : abc::Opcode::kLDUNDEFINED);
    std::uint8_t slot = form == Form::kCopiedSplit || form == Form::kCopiedMemberCall || form == Form::kCopiedChainCall || form == Form::kDiscardedCopiedChainCall ? 1 : 0;
    if (form == Form::kReversedOperands || form == Form::kReversedMemberCall) { store(1); load(0); slot = 1; }
    const bool negated = form == Form::kNegated || form == Form::kNegatedValueCall;
    emit(negated ? abc::Opcode::kSTRICTEQ_IMM8_V8 : abc::Opcode::kSTRICTNOTEQ_IMM8_V8);
    bytes.insert(bytes.end(), {0, slot});
    if (negated) emit(abc::Opcode::kISFALSE);
    exits.push_back(jump(abc::Opcode::kJEQZ_IMM32));
  }
  load(form == Form::kDifferentResult || form == Form::kDifferentCallBase ? parameter : 0);
  if (form == Form::kComputed || form == Form::kComputedCall) {
    store(1);
    if (form == Form::kComputedCall) read(5);
    else { emit(abc::Opcode::kLDA_STR_ID16); bytes.insert(bytes.end(), {3, 0}); }
    emit(abc::Opcode::kLDOBJBYVALUE_IMM8_V8); bytes.insert(bytes.end(), {0, 1});
  } else if (form != Form::kValueCall && form != Form::kNegatedValueCall && form != Form::kValueArgumentCall && form != Form::kDiscardedValueCall) {
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 3, 0});
  }
  if (ChainForm(form)) {
    store(1);
    if (form == Form::kComputedChain || form == Form::kComputedChainCall || form == Form::kDiscardedComputedChainCall) {
      read(5); emit(abc::Opcode::kLDOBJBYVALUE_IMM8_V8); bytes.insert(bytes.end(), {0, 1});
    } else {
      load(1); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 3, 0});
    }
  }
  if (CallForm(form) && form != Form::kOutsideCall) {
    if (form == Form::kArgumentCall || form == Form::kValueArgumentCall) {
      store(1); read(4); store(2); load(1);
      if (form == Form::kValueArgumentCall) { emit(abc::Opcode::kCALLARG1_IMM8_V8); bytes.insert(bytes.end(), {0, 2}); }
      else { emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); bytes.insert(bytes.end(), {0, 0, 2}); }
    } else if (form == Form::kValueCall || form == Form::kNegatedValueCall || form == Form::kDiscardedValueCall) {
      emit(abc::Opcode::kCALLARG0_IMM8); bytes.push_back(0);
    } else {
      emit(abc::Opcode::kCALLTHIS0_IMM8_V8);
      bytes.insert(bytes.end(), {0, form == Form::kDifferentCallBase ? parameter : ChainForm(form) ? std::uint8_t{1} : std::uint8_t{0}});
    }
  }
  if(form==Form::kVoidViaResultJoin) emit(abc::Opcode::kLDUNDEFINED);
  const auto skip = jump(abc::Opcode::kJMP_IMM32);
  const auto fallback = bytes.size(); emit(abc::Opcode::kLDUNDEFINED);
  for (const auto exit : exits) patch(exit, fallback);
  patch(skip, bytes.size());
  if (form == Form::kOutsideCall) {
    store(1); read(4); store(2); load(1);
    emit(abc::Opcode::kCALLARG1_IMM8_V8); bytes.insert(bytes.end(), {0, 2});
  }
  const bool discarded = form>=Form::kDiscardedChainCall && form!=Form::kVoidViaResultJoin;
  emit(discarded ? abc::Opcode::kRETURNUNDEFINED : abc::Opcode::kRETURN);
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
struct Generated { std::string source; std::uint64_t work; };
Generated Source(unsigned count, Form form, abc::ProfileId profile) {
  const auto body = Body(count, form, profile);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(CallForm(form) ? 3 : 2, 1, {}));
  RouteInputIdentity identity{}; identity.profile = profile;
  const auto input_result = FunctionInput::Build(identity, {}, {}, body, frame, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FunctionInput>(input_result)); const auto& input = std::get<FunctionInput>(input_result);
  auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, input.Input(), input.Proof(), frame));
  Check(!std::holds_alternative<analysis::ValueSimplifyError>(analysis::SimplifyValues(graph, graph.revision)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation_result = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluation_result)); const auto& evaluation = std::get<EvaluationPlan>(evaluation_result);
  Check(!std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict; syntax.names = {u"value", u"probe", u"fallback", u"other", u"field", u"argument", u"key"};
  syntax.texts = {u"field"};
  syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    else if (std::holds_alternative<analysis::ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  const ExpressionConstants view{constants, 7, 1, {}, {}};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto meaning = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  if (const auto* error = std::get_if<TreeSemanticError>(&meaning)) {
    std::cerr << "profile=" << unsigned(profile) << " count=" << count << " form=" << unsigned(form)
              << " semantics=" << unsigned(*error) << '\n';
    const auto flow = VerifyTreeFlow(graph, control, evaluation, tree);
    if (const auto* failure = std::get_if<TreeFlowError>(&flow)) std::cerr << "flow=" << unsigned(*failure) << '\n';
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
    (void)PrintFamilyDraft(family, std::cerr);
  }
  Check(std::holds_alternative<TreeSemantics>(meaning));
  const auto sources = std::get<InstructionSources>(BuildInstructionSources(input, graph, tree));
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream source; const auto printed = PrintFamilyDraft(family, source);
  Check(std::holds_alternative<PrintedFamilyDraft>(printed)); const auto& positions = std::get<PrintedFamilyDraft>(printed);
  test::CheckInstructionPositions(tree, sources, positions.members.front(), positions.functions.front());
  // Live join storage still has actual branch-owned assignments. Discarded
  // observation does not authorize removing those unrelated writes.
  const bool expected = form != Form::kMissingUndefined && form != Form::kDifferentResult &&
    form != Form::kDifferentCallBase && form != Form::kVoidViaResultJoin;
  Check((source.str().find("?.") != std::string::npos) == expected);
  const auto plan = std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto projections=std::count_if(plan.projections.begin(), plan.projections.end(), [](const auto& projection) {return bool(projection);})+
    std::count_if(plan.statement_projections.begin(),plan.statement_projections.end(),[](const auto& projection) {return bool(projection);});
  Check(projections == (expected ? 1 : 0));
  if (expected && CallForm(form) && form != Form::kOutsideCall)
    Check(std::any_of(plan.projections.begin(), plan.projections.end(), [](const auto& projection) {
      return projection && std::holds_alternative<OptionalCallSyntax>(*projection);
    }) || std::any_of(plan.statement_projections.begin(),plan.statement_projections.end(),[](const auto& projection) {
      return projection && std::holds_alternative<OptionalCallSyntax>(*projection);
    }));
  return {source.str(), plan.work};
}
}
int main() {
  std::array<std::uint64_t, unsigned(Form::kVoidViaResultJoin) + 1> baseline{};
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1u, 200u, 201u, 402u}) for (unsigned form = 0; form <= unsigned(Form::kVoidViaResultJoin); ++form) {
      const auto source = Source(count, Form(form), profile);
      if (count == 201) baseline[form] = source.work;
      if (count == 402) Check(source.work < 3 * baseline[form]);
      std::cout << nlohmann::json{{"profile", unsigned(profile)}, {"count", count}, {"form", form},
          {"source", source.source}, {"work", source.work}} << '\n';
    }
}
