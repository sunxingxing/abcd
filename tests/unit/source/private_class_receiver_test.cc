#include "../../../src/source/private_class_receiver.h"
#include "../../../src/analysis/value_edits.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
ValueProgram Graph(unsigned count, bool split, bool copied) {
  ValueProgram g;
  g.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language(), {}});
  const auto add = [&](ValueOperation operation, bool result,
      std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs,
      LanguageEffect effect = LanguageEffect::kNone) {
    const OpId id{static_cast<std::uint32_t>(g.ops.size())};
    const WorkBlockId block{split ? id.value : 0};
    const OriginId origin{id.value};
    std::optional<ValueId> value;
    if (result) {
      value = ValueId{static_cast<std::uint32_t>(g.values.size())};
      g.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    }
    const auto first = static_cast<std::uint32_t>(g.uses.size());
    for (const auto& [role, operand] : inputs) {
      const UseId at{static_cast<std::uint32_t>(g.uses.size())};
      OperandUse use{id, role, ReadPurpose::kOperand, operand};
      if (const auto* v = std::get_if<ValueId>(&operand)) {
        use.next = g.values[v->value].first_use;
        if (use.next) g.uses[use.next->value].previous = at;
        g.values[v->value].first_use = at;
      }
      g.uses.push_back(use);
    }
    g.ops.push_back({operation, block, split ? 0 : id.value, {first},
        static_cast<std::uint32_t>(inputs.size()), value, effect, effect,
        effect == LanguageEffect::kNone ? ExceptionalCompletion::kImpossible : ExceptionalCompletion::kMayThrow,
        origin});
    g.origins.push_back({{id.value}, {id.value}, {}});
    if (effect != LanguageEffect::kNone) g.exceptional_exits.push_back({id, origin, effect});
    if (split) {
      g.blocks.push_back({id, 1, {static_cast<std::uint32_t>(g.edges.size())}, 1});
      g.edges.push_back({block, {block.value+1}, FlowEdgeKind::kFallthrough, 0});
    }
    return value;
  };
  const auto base = copied ? *add(ValueCopy{}, true, {{OperandRole::kInput, ValueId{0}}}) : ValueId{0};
  for (unsigned i = 0; i < count; ++i) {
    auto callee = *add(ValueGlobalRead{GlobalLookup::kName, {i}}, true, {}, LanguageEffect::kUserCode);
    auto value = *add(ValueConstruct{0}, true, {{OperandRole::kConstructor, callee}}, LanguageEffect::kUserCode);
    add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
        {{OperandRole::kBase, base}, {OperandRole::kStoredValue, value}},
        LanguageEffect::kHeapWrite | LanguageEffect::kUserCode);
  }
  add(ValueReturn{}, false, {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
  if (split) { g.blocks.back().edge_count = 0; g.edges.pop_back(); }
  else g.blocks.push_back({{0}, static_cast<std::uint32_t>(g.ops.size()), {0}, 0});
  return g;
}
void Run(unsigned count, bool split, bool copied = false) {
  auto graph = Graph(count, split, copied);
  const auto built = BuildPrivateClassReceiver(graph);
  if (const auto* e = std::get_if<PrivateClassReceiverError>(&built)) std::cerr << "private error " << unsigned(*e) << '\n';
  test::Check(std::holds_alternative<PrivateClassReceiverProof>(built));
  const auto& proof = std::get<PrivateClassReceiverProof>(built);
  test::Check(proof.evidence.fields.size() == count && proof.evidence.exceptions.size() == count*3);
  test::Check(std::holds_alternative<PrivateClassReceiverProof>(VerifyPrivateClassReceiver(graph, proof.evidence)));
  auto forged = proof.evidence; forged.fields.pop_back();
  test::Check(std::get<PrivateClassReceiverError>(VerifyPrivateClassReceiver(graph, forged)) == PrivateClassReceiverError::kInput);
  forged = proof.evidence; std::swap(forged.effects[0], forged.effects[1]);
  test::Check(std::holds_alternative<PrivateClassReceiverError>(VerifyPrivateClassReceiver(graph, forged)));
  forged = proof.evidence; forged.exceptions.pop_back();
  test::Check(std::holds_alternative<PrivateClassReceiverError>(VerifyPrivateClassReceiver(graph, forged)));
  const auto field = proof.evidence.fields.front().value;
  auto wrong = graph;
  test::Check(!ReplaceUse(wrong, {wrong.ops[field].first_use.value+1}, ValueId{0}));
  test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)) == PrivateClassReceiverError::kObservation);
  wrong = graph; wrong.ops[field].operation = ValueNamedWrite{{0}};
  test::Check(std::holds_alternative<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)));
  wrong = graph; wrong.exceptional_exits.pop_back();
  test::Check(std::holds_alternative<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)));
  wrong = graph;
  test::Check(!ReplaceUse(wrong, wrong.ops[field-1].first_use, ValueId{0}));
  test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)) == PrivateClassReceiverError::kObservation);
  wrong = graph;
  wrong.exceptional_exits.front().handler = ValueHandler{{{0}, {0}, {0}}, {0}};
  test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)) == PrivateClassReceiverError::kInput);
  if (split) {
    wrong = graph;
    wrong.edges[wrong.edges.size()-1].target = {1};
    test::Check(std::holds_alternative<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)));
  }
  // Hidden capture/reflective execution can observe a private cell without a
  // receiver operand. Suspending the initializer needs a separate protocol.
  for (const auto effect : {LanguageEffect::kEnvironmentRead, LanguageEffect::kDebugger, LanguageEffect::kSuspension}) {
    wrong = graph;
    wrong.ops[field-2].normal = wrong.ops[field-2].normal | effect;
    wrong.ops[field-2].before_throw = wrong.ops[field-2].normal;
    wrong.exceptional_exits.front().before_throw = wrong.ops[field-2].normal;
    test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(wrong)) == PrivateClassReceiverError::kObservation);
  }
  SemanticBudget exact; exact.work = proof.work;
  test::Check(std::holds_alternative<PrivateClassReceiverProof>(BuildPrivateClassReceiver(graph, exact)));
  --exact.work;
  test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(graph, exact)) == PrivateClassReceiverError::kWorkLimit);
  std::size_t lo = 0, hi = SemanticBudget{}.bytes;
  while (lo < hi) {
    const auto mid = lo + (hi-lo)/2;
    auto attempt = BuildPrivateClassReceiver(graph, {mid});
    if (std::holds_alternative<PrivateClassReceiverProof>(attempt)) hi = mid;
    else { test::Check(std::get<PrivateClassReceiverError>(attempt) == PrivateClassReceiverError::kResourceLimit); lo = mid+1; }
  }
  test::Check(std::holds_alternative<PrivateClassReceiverProof>(BuildPrivateClassReceiver(graph, {lo})));
  test::Check(std::get<PrivateClassReceiverError>(BuildPrivateClassReceiver(graph, {lo-1})) == PrivateClassReceiverError::kResourceLimit);
  std::cout << count << " split=" << split << " copied=" << copied << " bytes=" << lo << " work=" << proof.work << '\n';
}
}
int main() { for (auto n : {200U, 201U, 400U, 800U}) { Run(n, false); Run(n, true); Run(n, true, true); } }
