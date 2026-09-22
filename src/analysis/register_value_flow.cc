#include "abcd/analysis/register_value_flow.h"

#include <algorithm>
#include <memory_resource>
#include <stdexcept>

#include "idf_scratch.h"
#include "register_value_rule.h"

namespace abcd::analysis {
namespace {
inline constexpr std::size_t kScalarInputs = 2;
struct ValueDependency {
  SsaValueId source;
  SsaValueId target;
};
struct ValueNode {
  detail::ValueRuleKind kind = detail::ValueRuleKind::kFixed;
  std::array<SsaValueId, kScalarInputs> inputs{};
  std::uint8_t input_count = 0;
};
bool SameSlot(ScalarSlot a, ScalarSlot b) {
  const auto* left = std::get_if<abc::RegisterId>(&a);
  const auto* right = std::get_if<abc::RegisterId>(&b);
  return left && right ? left->value == right->value : !left && !right;
}
}  // namespace

std::variant<RegisterValueFlow, ValueFlowFailure> RegisterValueFlow::Build(
    const abc::MethodBody& body, const RegisterSsa& ssa,
    std::uint64_t frame_size, std::span<const EntryValueFact> entries,
    IdfQuota& quota, IdfBudget budget) {
  using Kind = detail::ValueRuleKind;
  using Error = ValueFlowError;
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    detail::IdfScratch memory{budget.bytes};
    const auto count = ssa.Values().size();
    charge(count);
    std::pmr::vector<ValueNode> nodes(count, {}, &memory);
    std::pmr::vector<ValueKinds> facts(count, {}, &memory);
    std::pmr::vector<std::uint8_t> marked(count, 0, &memory);
    std::pmr::vector<ValueDependency> edges{&memory};
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      const auto& value = ssa.Values()[i];
      const SsaValueId target{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        facts[i] = ValueKinds::Unknown();
      } else if (std::holds_alternative<PhiValue>(value.origin)) {
        nodes[i].kind = Kind::kJoin;
        for (const auto& input : ssa.Inputs(target)) {
          charge();
          if (input.value.value >= count ||
              !SameSlot(ssa.Values()[input.value.value].slot, value.slot))
            return ValueFlowFailure{Error::kInvalidSsa, {}, {}};
          edges.push_back({input.value, target});
        }
      } else {
        const auto instruction =
            std::get<InstructionValue>(value.origin).instruction;
        if (instruction.value >= body.instructions.size() ||
            ssa.Write(instruction) != target)
          return ValueFlowFailure{Error::kInvalidSsa, instruction, {}};
        const auto lifted = detail::DescribeRegisterValue(
            body.instructions[instruction.value].decoded, frame_size);
        if (const auto* error = std::get_if<ScalarError>(&lifted))
          return ValueFlowFailure{Error::kInstruction, instruction, *error};
        const auto rule = std::get<detail::ValueRule>(lifted);
        nodes[i].kind = rule.kind;
        facts[i] = rule.seed;
        if (rule.kind == Kind::kFixed) continue;
        const auto reads = ssa.Reads(instruction);
        if (reads.empty() || reads.size() > kScalarInputs ||
            (rule.kind == Kind::kJoin && reads.size() != 1))
          return ValueFlowFailure{Error::kInvalidSsa, instruction, {}};
        for (const auto& read : reads) {
          charge();
          if (read.value.value >= count)
            return ValueFlowFailure{Error::kInvalidSsa, instruction, {}};
          nodes[i].inputs[nodes[i].input_count++] = read.value;
          edges.push_back({read.value, target});
        }
      }
    }
    for (const auto& entry : entries) {
      charge();
      const auto id = entry.value.value;
      if (id >= count || entry.kinds.IsEmpty() ||
          !std::holds_alternative<MethodEntry>(ssa.Values()[id].origin))
        return ValueFlowFailure{Error::kInvalidEntryFact, {}, {}};
      if (marked[id])
        return ValueFlowFailure{Error::kDuplicateEntryFact, {}, {}};
      marked[id] = 1;
      facts[id] = entry.kinds;
    }
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
    std::pmr::vector<SsaValueId> targets(edges.size(), {}, &memory);
    for (const auto edge : edges) {
      charge();
      targets[cursor[edge.source.value]++] = edge.target;
    }
    RegisterValueFlow result;
    result.work.dependencies = edges.size();
    std::pmr::vector<SsaValueId> pending{&memory};
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      marked[i] = !facts[i].IsEmpty();
      if (marked[i]) {
        ++result.work.changes;
        pending.push_back({static_cast<std::uint32_t>(i)});
      }
    }
    // Incremental join propagation is essential: rescanning all inputs of a
    // high-fan-in Phi after each changed source would be quadratic. Each source
    // grows at most four times; operation nodes have <=2 fixed inputs.
    for (std::size_t head = 0; head < pending.size(); ++head) {
      charge();
      ++result.work.queue_visits;
      const auto source = pending[head].value;
      const auto incoming = facts[source];
      marked[source] = 0;
      for (std::size_t edge = begin[source];
           edge < begin[std::size_t(source) + 1]; ++edge) {
        charge();
        ++result.work.edge_visits;
        const auto target = targets[edge].value;
        ValueKinds propagated = incoming;
        if (nodes[target].kind == Kind::kLanguageOperation) {
          bool ready = true, language = true;
          for (std::size_t i = 0; i < nodes[target].input_count; ++i) {
            const auto input = facts[nodes[target].inputs[i].value];
            ready &= !input.IsEmpty();
            language &= input.IsLanguage();
          }
          propagated = !ready     ? ValueKinds{}
                       : language ? ValueKinds::Language()
                                  : ValueKinds::Unknown();
        }
        const auto joined = facts[target].Join(propagated);
        if (joined == facts[target]) continue;
        facts[target] = joined;
        ++result.work.changes;
        if (!marked[target]) {
          marked[target] = 1;
          pending.push_back({target});
        }
      }
    }
    charge(count);
    if (count > memory.Available() / sizeof(ValueKinds)) throw std::bad_alloc{};
    if (count != 0) {
      result.facts_ = std::make_unique<ValueKinds[]>(count);
      std::copy(facts.begin(), facts.end(), result.facts_.get());
    }
    result.count_ = count;
    memory.Observe(result.AllocatedBytes());
    result.work.peak_bytes = memory.Peak();
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const detail::IdfWorkLimit&) {
    return ValueFlowFailure{Error::kWorkLimit, {}, {}};
  } catch (const std::bad_alloc&) {
    return ValueFlowFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) {
    return ValueFlowFailure{Error::kResourceLimit, {}, {}};
  }
}
}  // namespace abcd::analysis
