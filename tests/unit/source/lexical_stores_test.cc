#include "../../../src/source/lexical_stores.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/family_writes.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::source;
void Run(abc::ProfileId profile, bool captured, std::size_t count) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kFirstCell = 0, kCellCount = 1, kParameter = 0;
  std::vector<std::uint8_t> bytes;
  if (!captured) bytes = {op(abc::Opcode::kNEWLEXENV_IMM8), kCellCount};
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
  for (std::size_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kFirstCell});
  bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {false, false, false, false}));
  RouteInputIdentity identity; identity.profile = profile;
  constexpr abc::EntityOffset kMethod{300}, kRecord{100};
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, kMethod, kRecord,
      body, frame, InputCaptureMode::kConditional));
  auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto control = std::get<ControlPlan>(BuildControlPlan(values.graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(values.graph, {}, IncomingCells::kConditional));
  FunctionSyntax syntax; syntax.identity = identity; syntax.method = kMethod; syntax.owner = {1};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  if (captured) {
    // A conditional fragment, not a resolved file-family or a source receipt.
    syntax.names.push_back(u"captured");
    syntax.captures.push_back({CellId{0}, {FunctionOwnerId{0}, SymbolId{0}}, NameId{0}});
  }
  std::vector<ExpressionConstant> constants(values.graph.ops.size());
  const ExpressionConstants view{constants, syntax.names.size(), 0};
  auto tree = std::get<ConstructedFunction>(BuildFunctionTree(values.graph, control, evaluation,
      std::move(syntax), view)).tree;
  auto flow = std::get<TreeFlow>(VerifyTreeFlow(values.graph, control, evaluation, tree));
  const auto verify = [&](analysis::SemanticBudget budget = {}) {
    return VerifyLexicalStores(NormalStoreInput{input}, values.graph, &*values.cells, {tree, flow}, {}, budget);
  };
  const auto checked = std::get<LexicalStores>(verify());
  test::Check(checked.stores == count && checked.captured == (captured ? count : 0) && !checked.signature);
  auto budget = analysis::SemanticBudget{}; budget.work = checked.work;
  test::Check(std::holds_alternative<LexicalStores>(verify(budget)));
  --budget.work;
  test::Check(std::get<LexicalStoreFailure>(verify(budget)).kind == LexicalStoreError::kWorkLimit);
  budget = {}; budget.bytes = checked.allocated_bytes;
  test::Check(std::holds_alternative<LexicalStores>(verify(budget)));
  --budget.bytes;
  test::Check(std::get<LexicalStoreFailure>(verify(budget)).kind == LexicalStoreError::kResourceLimit);
  ++tree.value_revision.value;
  test::Check(std::get<LexicalStoreFailure>(verify()).kind == LexicalStoreError::kOwner);
  --tree.value_revision.value;
  ++tree.method.value;
  test::Check(std::get<LexicalStoreFailure>(verify()).kind == LexicalStoreError::kOwner);
  --tree.method.value;
  for (std::size_t i = 0; i < values.graph.ops.size(); ++i) {
    auto& operation = values.graph.ops[i];
    if (!std::holds_alternative<analysis::ValueLexicalStore>(operation.operation)) continue;
    const auto expected_pc = values.graph.origins[operation.origin.value].pc;
    auto& expression = tree.expressions[flow.expressions[i]->value];
    auto& assignment = std::get<AssignmentExpr>(expression.expression);
    const auto saved = assignment.target;
    assignment.target = SymbolTarget{SymbolId{static_cast<std::uint32_t>(tree.symbols.size())}};
    const auto error = std::get<LexicalStoreFailure>(verify());
    test::Check(error.kind == LexicalStoreError::kStore && error.pc == expected_pc);
    assignment.target = saved;
    const auto cell = values.cells->operations[i]; values.cells->operations[i].reset();
    test::Check(std::get<LexicalStoreFailure>(verify()).kind == LexicalStoreError::kStore);
    values.cells->operations[i] = cell;
    const auto storage = values.cells->cells[cell->value].storage;
    values.cells->cells[cell->value].storage = captured ? CellStorage{AllocatedCell{{0}, {0}}}
                                                     : CellStorage{IncomingCell{{0}, {0}}};
    test::Check(std::get<LexicalStoreFailure>(verify()).kind == LexicalStoreError::kStore);
    values.cells->cells[cell->value].storage = storage;
    ++values.graph.origins[operation.origin.value].pc.value;
    test::Check(std::get<LexicalStoreFailure>(verify()).kind == LexicalStoreError::kStore);
    values.graph.origins[operation.origin.value].pc = expected_pc;
    test::Check(std::holds_alternative<LexicalStores>(verify()));
    break;
  }
  if (!captured) {
    // Exercise the shared family gate at the same 200/201/geometric sizes;
    // this synthetic layout is not file ownership or publication evidence.
    std::vector<FamilyMember> members;
    members.push_back({tree, flow}); members.front().tree.owner = {0};
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily(std::move(members), {0}));
    const FamilyWriteInput source{CaptureFunction{input, values}, {}, {}};
    const abc::MetadataIndex index;
    const auto checked_family = std::get<FamilyWrites>(VerifyFamilyWrites(index,
        std::span<const FamilyWriteInput>(&source, 1), family));
    test::Check(checked_family.facts.stores == count && !checked_family.facts.captured_stores &&
        !checked_family.facts.creations && !checked_family.facts.signature_stores);
  }
}
}  // namespace
int main() {
  using namespace abcd;
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301})
    for (const bool captured : {false, true}) Run(profile, captured, 1);
  for (const std::size_t count : {200, 201, 402})
    for (const bool captured : {false, true}) Run(abc::ProfileId::kDynamic1301, captured, count);
}
