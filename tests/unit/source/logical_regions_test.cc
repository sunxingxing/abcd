#include "../../../src/source/logical_regions.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

using namespace abcd;
using namespace abcd::source;
using abcd::test::Check;
namespace {
analysis::ValueProgram Diamond(bool negate, unsigned count = 1, bool direct = false) {
  // return arg || [] (or arg && []), retaining the compiler's empty keep arm.
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    const std::initializer_list<std::uint8_t> diamond{0x60, 2, 0x61, 1, 0xfb, 0x13, 0,
        static_cast<std::uint8_t>(negate ? 0x4f : 0x51), 6,
        0x05, 0, static_cast<std::uint8_t>(direct ? 0x61 : 0x4d),
        static_cast<std::uint8_t>(direct ? 1 : 4), 0x60, 1, 0x61, 2};
    bytes.insert(bytes.end(), diamond.begin(), diamond.end());
  }
  bytes.insert(bytes.end(), {0x60, 2, 0x64});
  const auto body = test::Body(bytes);
  using namespace analysis;
  const auto method_result = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 3);
  Check(std::holds_alternative<SemanticMethod>(method_result));
  const auto& method = std::get<SemanticMethod>(method_result);
  const auto proof_result = VerifySemanticSource(body, method, 2, 1);
  Check(std::holds_alternative<SemanticProof>(proof_result));
  const auto& proof = std::get<SemanticProof>(proof_result);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 1, {false, false, false, false}));
  auto imported = ImportNormalValues(body, method, proof, frame);
  Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  return graph;
}
}
void Negatives() {
  using namespace analysis;
  auto graph = Diamond(false);
  auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto region = FindLogicalRegions(graph, control).front();
  auto wrong = graph;
  std::get<ValueUnary>(wrong.ops[region.boolean.value].operation).op = UnaryOperator::kTypeOf;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // A nonempty keep arm must not be discarded just because its phi matches.
  Check(region.keep.has_value());
  auto& keep = wrong.ops[wrong.blocks[region.keep->value].first_op.value];
  keep.operation = ValueDebugger{}; keep.result.reset(); keep.use_count = 0; keep.life = ValueLife::kLive;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // An available but different value is a conditional, not logical selection.
  const ValueId other{static_cast<std::uint32_t>(wrong.values.size())};
  wrong.values.push_back({EntryDefinition{ParameterEntry{1}}, ValueKinds::Language(), {}});
  for (auto& use : wrong.uses) {
    const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
    if (use.owner == decltype(use.owner){region.merged} && edge && *edge == region.keep_input)
      use.operand = other;
  }
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  wrong.values[region.left.value].domain = ValueKinds::Hole();
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph;
  // A second observer of the predicate prevents absorbing its evaluation.
  for (auto& op : wrong.ops) if (std::holds_alternative<ValueReturn>(op.operation))
    wrong.uses[op.first_use.value].operand = *wrong.ops[region.boolean.value].result;
  Check(FindLogicalRegions(wrong, control).empty());
  wrong = graph; ++wrong.revision.value;
  Check(FindLogicalRegions(wrong, control).empty());
}
int main() {
  for (const bool negate : {false, true}) for (const bool direct : {false, true}) {
    for (const unsigned count : {1U, 200U, 201U, 402U}) {
      const auto graph = Diamond(negate, count, direct);
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto regions = FindLogicalRegions(graph, control);
      Check(regions.size() == count);
      for (const auto& region : regions) {
        Check(region.operation == (negate ? TreeLogical::kAnd : TreeLogical::kOr));
        Check(region.keep.has_value() != direct);
        Check(graph.values[region.merged.value].life == analysis::ValueLife::kLive);
      }
    }
  }
  Negatives();
}
