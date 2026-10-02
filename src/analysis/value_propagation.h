#ifndef ABCD_ANALYSIS_VALUE_PROPAGATION_H_
#define ABCD_ANALYSIS_VALUE_PROPAGATION_H_

#include <span>
#include "abcd/analysis/register_value_flow.h"
#include "idf_scratch.h"
#include "register_value_rule.h"

namespace abcd::analysis::detail {
struct ValueNodeId {
  std::uint32_t value = 0;
};
struct ValueDependency {
  ValueNodeId source;
  ValueNodeId target;
};
struct ValueNode {
  ValueRuleKind kind = detail::ValueRuleKind::kFixed;
  std::array<SsaValueId, kScalarInputs> inputs{};
  std::uint8_t input_count = 0;
};

// Shared finite-height solver. Callers validate node/edge indices and build
// typed transfer rules; this routine never reinterprets bytecode or source.
template <class Charge>
void PropagateValues(std::span<const ValueNode> nodes,
                     std::span<const ValueDependency> edges,
                     std::span<ValueKinds> facts, std::span<std::uint8_t> marked,
                     IdfScratch& memory, Charge&& charge, ValueFlowWork& work) {
  using Kind = ValueRuleKind;
  const auto count = nodes.size();
  std::pmr::vector<std::size_t> begin(count + 1, 0, &memory);
  std::pmr::vector<std::size_t> cursor(count, 0, &memory);
  for (const auto edge : edges) {
    charge();
    ++begin[std::size_t(edge.source.value) + 1];
  }
  for (std::size_t i = 1; i < begin.size(); ++i) {
    charge();
    begin[i] += begin[i - 1];
    cursor[i - 1] = begin[i - 1];
  }
  std::pmr::vector<ValueNodeId> targets(edges.size(), {}, &memory);
  for (const auto edge : edges) {
    charge();
    targets[cursor[edge.source.value]++] = edge.target;
  }
  work.dependencies = edges.size();
  std::pmr::vector<ValueNodeId> pending{&memory};
  for (std::size_t i = 0; i < count; ++i) {
    charge();
    marked[i] = !facts[i].IsEmpty();
    if (marked[i]) {
      ++work.changes;
      pending.push_back({static_cast<std::uint32_t>(i)});
    }
  }
  // Incremental join propagation is essential: rescanning all inputs of a
  // high-fan-in Phi after each changed source would be quadratic. A fact
  // gains four VM atoms and can weaken its scalar refinement four times
  // (FiniteNonzeroNumber -> FiniteNumber -> NonNaNNumber -> Defined -> Language);
  // The int32-tag exclusion can weaken once independently.
  // Operation nodes have at most two fixed inputs.
  for (std::size_t head = 0; head < pending.size(); ++head) {
    charge();
    ++work.queue_visits;
    const auto source = pending[head].value;
    const auto incoming = facts[source];
    marked[source] = 0;
    for (std::size_t edge = begin[source];
         edge < begin[std::size_t(source) + 1]; ++edge) {
      charge();
      ++work.edge_visits;
      const auto target = targets[edge].value;
      ValueKinds propagated = incoming;
      if (nodes[target].kind == Kind::kLanguageOperation ||
          nodes[target].kind == Kind::kEquality ||
          nodes[target].kind == Kind::kAdd ||
          nodes[target].kind == Kind::kSubtract ||
          nodes[target].kind == Kind::kNonNaNUnary ||
          nodes[target].kind == Kind::kNumericIdentity ||
          nodes[target].kind == Kind::kNonNaNMultiply ||
          nodes[target].kind == Kind::kNonNaNDivide) {
        bool ready = true, language = true, equality_inputs = true;
        bool non_nan_number = true;
        for (std::size_t i = 0; i < nodes[target].input_count; ++i) {
          const auto input = facts[nodes[target].inputs[i].value];
          ready &= !input.IsEmpty();
          language &= input.IsLanguage();
          equality_inputs &= !input.Contains(VmValueKind::kEnvironment) &&
                             !input.Contains(VmValueKind::kOtherInternal);
          non_nan_number &= input.IsNonNaNNumber();
        }
        if (!ready) {
          propagated = {};
        } else if (nodes[target].kind == Kind::kEquality && equality_inputs) {
          propagated = ValueKinds::Language();
        } else if (!language) {
          propagated = ValueKinds::Unknown();
        } else if (nodes[target].kind == Kind::kAdd || nodes[target].kind == Kind::kSubtract) {
          propagated = nodes[target].input_count == kScalarInputs
              ? (nodes[target].kind == Kind::kAdd ? AddValues : SubtractNumbers)(
                    facts[nodes[target].inputs[0].value], facts[nodes[target].inputs[1].value])
              : ValueKinds::Unknown();
        } else if (nodes[target].kind == Kind::kNumericIdentity) {
          propagated = nodes[target].input_count == 1 && non_nan_number
              ? facts[nodes[target].inputs[0].value] : ValueKinds::Unknown();
        } else if (nodes[target].kind == Kind::kNonNaNUnary) {
          propagated = non_nan_number ? ValueKinds::NonNaNNumber()
                                      : ValueKinds::Unknown();
        } else if (nodes[target].kind == Kind::kNonNaNMultiply) {
          propagated = nodes[target].input_count == kScalarInputs
              ? MultiplyNumbers(facts[nodes[target].inputs[0].value],
                                facts[nodes[target].inputs[1].value])
              : ValueKinds::Unknown();
        } else if (nodes[target].kind == Kind::kNonNaNDivide) {
          propagated = nodes[target].input_count == kScalarInputs
              ? DivideNumbers(facts[nodes[target].inputs[0].value],
                              facts[nodes[target].inputs[1].value])
              : ValueKinds::Unknown();
        } else {
          propagated = ValueKinds::Language();
        }
      }
      const auto joined = facts[target].Join(propagated);
      if (joined == facts[target]) continue;
      facts[target] = joined;
      ++work.changes;
      if (!marked[target]) {
        marked[target] = 1;
        pending.push_back({target});
      }
    }
  }
}
}  // namespace abcd::analysis::detail
#endif
