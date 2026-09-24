#include "abcd/analysis/register_value_flow.h"

#include <algorithm>
#include <memory_resource>
#include <stdexcept>

#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/semantic_ir.h"
#include "idf_scratch.h"
#include "lexical_value_index.h"
#include "register_value_rule.h"

namespace abcd::analysis {
namespace {
inline constexpr std::size_t kScalarInputs = 2;
struct ValueNodeId {
  std::uint32_t value = 0;
};
struct ValueDependency {
  ValueNodeId source;
  ValueNodeId target;
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

std::variant<RegisterValueFlow, ValueFlowFailure>
RegisterValueFlow::BuildWithLexicals(const abc::MethodBody& body,
                                     const RegisterSsa& ssa,
                                     std::uint64_t frame_size,
                                     std::span<const EntryValueFact> entries,
                                     const LexicalValueContext& lexicals,
                                     IdfQuota& quota, IdfBudget budget) {
  return Analyze(body, ssa, frame_size, entries, &lexicals, {}, quota,
                 budget);
}

std::variant<RegisterValueFlow, ValueFlowFailure> RegisterValueFlow::Build(
    const abc::MethodBody& body, const RegisterSsa& ssa,
    std::uint64_t frame_size, std::span<const EntryValueFact> entries,
    IdfQuota& quota, IdfBudget budget) {
  return Analyze(body, ssa, frame_size, entries, nullptr, {}, quota,
                 budget);
}

std::variant<RegisterValueFlow, ValueFlowFailure>
RegisterValueFlow::BuildWithLanguageLoads(
    const abc::MethodBody& body, const RegisterSsa& ssa,
    std::uint64_t frame_size, std::span<const EntryValueFact> entries,
    std::span<const LanguageLoadFact> loads, IdfQuota& quota,
    IdfBudget budget, std::span<const SemanticInstruction> semantics) {
  return Analyze(body, ssa, frame_size, entries, nullptr, loads, quota,
                 budget, semantics);
}

std::variant<RegisterValueFlow, ValueFlowFailure> RegisterValueFlow::Analyze(
    const abc::MethodBody& body, const RegisterSsa& ssa,
    std::uint64_t frame_size, std::span<const EntryValueFact> entries,
    const LexicalValueContext* lexicals,
    std::span<const LanguageLoadFact> loads, IdfQuota& quota,
    IdfBudget budget, std::span<const SemanticInstruction> semantics) {
  using Kind = detail::ValueRuleKind;
  using Error = ValueFlowError;
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    if (!semantics.empty() && semantics.size() != body.instructions.size())
      return ValueFlowFailure{Error::kInvalidSemantics, {}, {}};
    for (std::size_t i = 0; i < semantics.size(); ++i) {
      charge();
      if (i > UINT32_MAX || semantics[i].pc != body.instructions[i].offset)
        return ValueFlowFailure{Error::kInvalidSemantics, {}, {}};
      const InstructionId id{static_cast<std::uint32_t>(i)};
      const auto write = ssa.Write(id);
      const auto& slot = semantics[i].effects.normal_registers.write;
      // Unreachable sites have no renamed value. For every published value,
      // the typed rule must describe precisely the slot defined by this SSA.
      if (write && (!slot || write->value >= ssa.Values().size() ||
                    !SameSlot(*slot, ssa.Values()[write->value].slot)))
        return ValueFlowFailure{Error::kInvalidSemantics, id, {}};
    }
    detail::IdfScratch memory{budget.bytes};
    LexicalIndex cells{body, ssa, frame_size, quota, &memory};
    if (lexicals) cells.Collect(*lexicals);
    const auto value_count = ssa.Values().size();
    // Keep graph edges as compact as register-only SSA IDs. A graph beyond
    // this representable range cannot fit the engine's process memory bound.
    if (value_count > UINT32_MAX ||
        cells.cells.size() > UINT32_MAX - value_count)
      throw std::bad_alloc{};
    const auto count = value_count + cells.cells.size();
    charge(count);
    std::pmr::vector<ValueNode> nodes(count, {}, &memory);
    std::pmr::vector<ValueKinds> facts(count, {}, &memory);
    std::pmr::vector<std::uint8_t> marked(count, 0, &memory);
    std::pmr::vector<std::uint8_t> trusted{&memory};
    if (!loads.empty()) trusted.resize(body.instructions.size());
    for (const auto load : loads) {
      charge();
      const auto id = load.instruction;
      if (id.value >= body.instructions.size())
        return ValueFlowFailure{Error::kInvalidLoadFact, id, {}};
      if (trusted[id.value])
        return ValueFlowFailure{Error::kDuplicateLoadFact, id, {}};
      bool supported;
      if (semantics.empty()) {
        const auto& decoded = body.instructions[id.value].decoded;
        supported = std::holds_alternative<LexicalLoad>(
                        LiftLexicalInstruction(decoded, frame_size)) ||
                    std::holds_alternative<ModuleLoad>(LiftModuleLoad(decoded));
      } else {
        const auto& operation = semantics[id.value].operation;
        supported = std::holds_alternative<LexicalLoad>(operation) ||
                    std::holds_alternative<ModuleLoad>(operation);
      }
      const auto value = ssa.Write(id);
      if (!supported || !value || value->value >= value_count ||
          !std::holds_alternative<InstructionValue>(
              ssa.Values()[value->value].origin) ||
          std::get<InstructionValue>(ssa.Values()[value->value].origin)
                  .instruction != id)
        return ValueFlowFailure{Error::kInvalidLoadFact, id, {}};
      trusted[id.value] = 1;
    }
    std::pmr::vector<ValueDependency> edges{&memory};
    for (std::size_t i = 0; i < value_count; ++i) {
      charge();
      const auto& value = ssa.Values()[i];
      const SsaValueId target{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        facts[i] = ValueKinds::Unknown();
      } else if (std::holds_alternative<PhiValue>(value.origin)) {
        nodes[i].kind = Kind::kJoin;
        for (const auto& input : ssa.Inputs(target)) {
          charge();
          if (input.value.value >= value_count ||
              !SameSlot(ssa.Values()[input.value.value].slot, value.slot))
            return ValueFlowFailure{Error::kInvalidSsa, {}, {}};
          edges.push_back({{input.value.value}, {target.value}});
        }
      } else {
        const auto instruction =
            std::get<InstructionValue>(value.origin).instruction;
        if (instruction.value >= body.instructions.size() ||
            ssa.Write(instruction) != target)
          return ValueFlowFailure{Error::kInvalidSsa, instruction, {}};
        if (!trusted.empty() && trusted[instruction.value]) {
          facts[i] = ValueKinds::Language();
          continue;
        }
        const auto lifted = semantics.empty()
            ? detail::DescribeRegisterValue(
                  body.instructions[instruction.value].decoded, frame_size)
            : detail::DescribeRegisterValue(semantics[instruction.value]);
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
          if (read.value.value >= value_count)
            return ValueFlowFailure{Error::kInvalidSsa, instruction, {}};
          nodes[i].inputs[nodes[i].input_count++] = read.value;
          edges.push_back({{read.value.value}, {target.value}});
        }
      }
    }
    // Flow-insensitive CELL categories, not last-store forwarding. Every fresh
    // dynamic instance starts Hole, including a repeatedly executed allocation.
    // Joining all stores preserves that possibility; never seed JS undefined.
    for (std::size_t i = value_count; i < count; ++i) {
      charge();
      nodes[i].kind = Kind::kJoin;
      facts[i] = ValueKinds::Hole();
    }
    for (const auto access : cells.accesses) {
      charge();
      const ValueNodeId cell{
          static_cast<std::uint32_t>(value_count + access.cell.value)};
      const ValueNodeId value{access.value.value};
      switch (access.kind) {
        case LexicalIndex::AccessKind::kLoad:
          nodes[value.value].kind = Kind::kJoin;
          facts[value.value] = {};
          edges.push_back({cell, value});
          break;
        case LexicalIndex::AccessKind::kStore:
          edges.push_back({value, cell});
          break;
      }
    }
    for (const auto& entry : entries) {
      charge();
      const auto id = entry.value.value;
      if (id >= value_count || entry.kinds.IsEmpty() ||
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
    std::pmr::vector<ValueNodeId> targets(edges.size(), {}, &memory);
    for (const auto edge : edges) {
      charge();
      targets[cursor[edge.source.value]++] = edge.target;
    }
    RegisterValueFlow result;
    result.work.dependencies = edges.size();
    result.work.cells = cells.cells.size();
    std::pmr::vector<ValueNodeId> pending{&memory};
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      marked[i] = !facts[i].IsEmpty();
      if (marked[i]) {
        ++result.work.changes;
        pending.push_back({static_cast<std::uint32_t>(i)});
      }
    }
    // Incremental join propagation is essential: rescanning all inputs of a
    // high-fan-in Phi after each changed source would be quadratic. A fact
    // gains four VM atoms and can lose a scalar refinement once;
    // operation nodes have at most two fixed inputs.
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
        if (nodes[target].kind == Kind::kLanguageOperation ||
            nodes[target].kind == Kind::kStringAdd ||
            nodes[target].kind == Kind::kNonNaNNegate) {
          bool ready = true, language = true, string = false;
          bool non_nan_number = true;
          for (std::size_t i = 0; i < nodes[target].input_count; ++i) {
            const auto input = facts[nodes[target].inputs[i].value];
            ready &= !input.IsEmpty();
            language &= input.IsLanguage();
            string |= input.IsString();
            non_nan_number &= input.IsNonNaNNumber();
          }
          if (!ready) {
            propagated = {};
          } else if (!language) {
            propagated = ValueKinds::Unknown();
          } else if (nodes[target].kind == Kind::kStringAdd) {
            propagated = string ? ValueKinds::String()
                                : ValueKinds::Unknown();
          } else if (nodes[target].kind == Kind::kNonNaNNegate) {
            propagated = non_nan_number ? ValueKinds::NonNaNNumber()
                                        : ValueKinds::Unknown();
          } else {
            propagated = ValueKinds::Language();
          }
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
    charge(value_count);
    if (value_count > memory.Available() / sizeof(ValueKinds))
      throw std::bad_alloc{};
    if (value_count != 0) {
      result.facts_ = std::make_unique<ValueKinds[]>(value_count);
      std::copy_n(facts.begin(), value_count, result.facts_.get());
    }
    result.count_ = value_count;
    memory.Observe(result.AllocatedBytes());
    result.work.peak_bytes = memory.Peak();
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const ValueFlowFailure& failure) {
    return failure;
  } catch (const detail::IdfWorkLimit&) {
    return ValueFlowFailure{Error::kWorkLimit, {}, {}};
  } catch (const std::bad_alloc&) {
    return ValueFlowFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) {
    return ValueFlowFailure{Error::kResourceLimit, {}, {}};
  }
}
}  // namespace abcd::analysis
