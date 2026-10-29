#include "abcd/analysis/module_initialization.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::analysis {
namespace {
using Error = ModuleInitializationError;
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget)
      : initial(budget), left(budget) {}
  void Require(bool ok) const {
    if (!ok) throw Error::kInput;
  }
  void Step(std::uint64_t n = 1) {
    if (n > left.work) throw Error::kWorkLimit;
    left.work -= n;
  }
  void Admit(std::size_t n, std::size_t width = 1) {
    if (n > left.bytes / width) throw Error::kResourceLimit;
    left.bytes -= n * width;
  }
  template <class T>
  void Reserve(std::vector<T>& items, std::size_t n) {
    Admit(n, sizeof(T));
    items.reserve(n);
    Admit(items.capacity() - n, sizeof(T));
  }
  template <class T>
  void Size(std::vector<T>& items, std::size_t n) {
    Reserve(items, n);
    Step(n);
    items.resize(n);
  }
  void Insert(ModuleSlotSet& set, analysis::ModuleSlot slot) {
    Step(ModuleSlotSet::kPathWork);
    auto bytes = left.bytes;
    set = set.With(slot, bytes);
    Admit(left.bytes - bytes);
  }
  analysis::SemanticBudget initial, left;
};
}  // namespace
std::variant<ModuleInitialization, ModuleInitializationError>
BuildModuleInitialization(std::span<const ModuleStateSite> program,
                          const NormalFlow& flow, const ModuleSlotSet& entry,
                          analysis::SemanticBudget budget) {
  using namespace analysis;
  try {
    Quota quota(budget);
    quota.Admit(sizeof(ModuleInitialization));
    quota.Require(program.size() <= UINT32_MAX && !flow.blocks.empty() &&
                  flow.blocks.size() < UINT32_MAX);
    auto dominated =
        BuildNormalDominators(flow, {}, {quota.left.bytes, quota.left.work});
    if (const auto* error = std::get_if<DominatorError>(&dominated)) {
      if (*error == DominatorError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == DominatorError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kInput;
    }
    const auto& tree = std::get<DominatorTree>(dominated);
    quota.Admit(tree.work.allocated_bytes);
    quota.Step(tree.work.steps);
    std::vector<std::uint8_t> reachable_site;
    quota.Size(reachable_site, program.size());
    for (std::size_t block = 0; block < flow.blocks.size(); ++block) {
      quota.Step();
      const auto& region = flow.blocks[block];
      quota.Require(region.first_instruction <= program.size() &&
                    region.instruction_count <=
                        program.size() - region.first_instruction);
      if (!tree.nodes[block].begin) continue;
      for (std::size_t site = region.first_instruction;
           site <
           std::size_t(region.first_instruction) + region.instruction_count;
           ++site) {
        quota.Step();
        reachable_site[site] = 1;
      }
    }
    ModuleInitialization result;
    std::vector<ModuleSlot> slots;
    quota.Reserve(slots, program.size());
    std::size_t creations = 0;
    for (std::size_t i = 0; i < program.size(); ++i) {
      quota.Step();
      if (!reachable_site[i]) continue;
      const auto& site = program[i];
      if (site.store) slots.push_back(*site.store);
      creations += site.snapshot;
    }
    quota.Reserve(result.states, creations);
    if (!creations)
      return ModuleInitialization{{},
                                  quota.initial.bytes - quota.left.bytes,
                                  quota.initial.work - quota.left.work};
    std::sort(slots.begin(), slots.end(), [&](auto a, auto b) {
      quota.Step();
      return a < b;
    });
    slots.erase(std::unique(slots.begin(), slots.end(),
                            [&](auto a, auto b) {
                              quota.Step();
                              return a == b;
                            }),
                slots.end());
    if (slots.empty()) {
      for (std::size_t i = 0; i < program.size(); ++i) {
        quota.Step();
        if (!reachable_site[i]) continue;
        const auto& site = program[i];
        if (site.snapshot)
          result.states.push_back({{static_cast<std::uint32_t>(i)}, entry});
      }
      result.allocated_bytes = quota.initial.bytes - quota.left.bytes;
      result.work = quota.initial.work - quota.left.work;
      return result;
    }
    std::vector<RegisterSite> effects;
    quota.Size(effects, program.size());
    for (std::size_t i = 0; i < effects.size(); ++i) {
      quota.Step();
      effects[i].pc = program[i].pc;
      if (!reachable_site[i]) continue;
      if (const auto store = program[i].store) {
        const auto found = std::lower_bound(slots.begin(), slots.end(), *store,
                                            [&](auto a, auto b) {
                                              quota.Step();
                                              return a < b;
                                            });
        quota.Require(found != slots.end() && *found == *store);
        effects[i].effect.write =
            abc::RegisterId{static_cast<std::uint32_t>(found - slots.begin())};
      }
    }
    IdfQuota ssa_work(quota.left.work);
    auto built =
        RegisterSsa::BuildSites(effects, flow, slots.size(), ssa_work,
                                {quota.left.bytes}, SsaPlacement::kComplete);
    quota.Step(ssa_work.Consumed());
    if (!std::holds_alternative<RegisterSsa>(built))
      std::visit(
          [&](const auto& error) {
            using T = std::decay_t<decltype(error)>;
            if constexpr (std::is_same_v<T, RegisterUseFailure>) {
              if (error.kind == RegisterUseError::kWorkLimit)
                throw Error::kWorkLimit;
              if (error.kind == RegisterUseError::kResourceLimit)
                throw Error::kResourceLimit;
            } else if constexpr (!std::is_same_v<T, RegisterSsa>) {
              if (error == T::kWorkLimit) throw Error::kWorkLimit;
              if (error == T::kResourceLimit) throw Error::kResourceLimit;
            }
            throw Error::kInput;
          },
          built);
    const auto& ssa = std::get<RegisterSsa>(built);
    quota.Admit(ssa.work.peak_bytes);
    const auto values = ssa.Values();
    const auto slot_of = [&](SsaValueId id) {
      quota.Step();
      quota.Require(id.value < values.size());
      const auto* key = std::get_if<abc::RegisterId>(&values[id.value].slot);
      quota.Require(key && key->value < slots.size());
      return slots[key->value];
    };
    std::vector<std::uint8_t> uninitialized;
    quota.Size(uninitialized, values.size());
    std::vector<SsaValueId> queue;
    quota.Reserve(queue, values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
      quota.Step();
      if (!std::holds_alternative<MethodEntry>(values[i].origin)) continue;
      const SsaValueId id{static_cast<std::uint32_t>(i)};
      quota.Step(ModuleSlotSet::kPathWork);
      if (!entry.Contains(slot_of(id))) {
        uninitialized[i] = 1;
        queue.push_back(id);
      }
    }
    std::vector<std::size_t> offsets;
    quota.Size(offsets, values.size() + 1);
    for (const auto& edge : ssa.AllInputs()) {
      quota.Step();
      quota.Require(
          edge.value.value < values.size() && edge.phi.value < values.size() &&
          std::holds_alternative<PhiValue>(values[edge.phi.value].origin) &&
          slot_of(edge.phi) == slot_of(edge.value));
      ++offsets[std::size_t(edge.value.value) + 1];
    }
    for (std::size_t i = 1; i < offsets.size(); ++i) {
      quota.Step();
      offsets[i] += offsets[i - 1];
    }
    std::vector<std::size_t> cursor;
    quota.Reserve(cursor, offsets.size());
    cursor = offsets;
    std::vector<SsaValueId> dependents;
    quota.Size(dependents, ssa.AllInputs().size());
    for (const auto& edge : ssa.AllInputs()) {
      quota.Step();
      dependents[cursor[edge.value.value]++] = edge.phi;
    }
    // Meet is Boolean AND. Propagate false once over sparse phi edges; loop
    // cycles retain true only when no entry/uninitialized path reaches them.
    for (std::size_t head = 0; head < queue.size(); ++head) {
      quota.Step();
      const auto id = queue[head].value;
      for (auto i = offsets[id]; i < offsets[id + 1]; ++i) {
        quota.Step();
        const auto target = dependents[i].value;
        if (!uninitialized[target]) {
          uninitialized[target] = 1;
          queue.push_back(dependents[i]);
        }
      }
    }
    struct InitializedPhi {
      BlockId block;
      ModuleSlot slot;
    };
    std::vector<InitializedPhi> phis;
    quota.Reserve(phis, values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
      quota.Step();
      const auto* phi = std::get_if<PhiValue>(&values[i].origin);
      if (phi && !uninitialized[i]) {
        quota.Require(phi->block.value < tree.nodes.size());
        phis.push_back({phi->block, slot_of({static_cast<std::uint32_t>(i)})});
      }
    }
    std::sort(phis.begin(), phis.end(), [&](const auto& a, const auto& b) {
      quota.Step();
      return a.block < b.block;
    });
    std::vector<std::size_t> phi_offsets;
    quota.Size(phi_offsets, tree.nodes.size() + 1);
    for (const auto& phi : phis) {
      quota.Step();
      ++phi_offsets[std::size_t(phi.block.value) + 1];
    }
    for (std::size_t i = 1; i < phi_offsets.size(); ++i) {
      quota.Step();
      phi_offsets[i] += phi_offsets[i - 1];
    }
    std::vector<BlockId> order;
    quota.Size(order, tree.nodes.size());
    std::size_t reachable = 0;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
      quota.Step();
      if (const auto begin = tree.nodes[i].begin) {
        quota.Require(begin <= order.size());
        order[begin - 1] = {static_cast<std::uint32_t>(i)};
        ++reachable;
      }
    }
    std::vector<ModuleSlotSet> completed;
    quota.Size(completed, tree.nodes.size());
    for (std::size_t rank = 0; rank < reachable; ++rank) {
      quota.Step();
      const auto id = order[rank];
      const auto parent = tree.nodes[id.value].parent;
      auto state = parent ? completed[parent->value] : entry;
      for (auto i = phi_offsets[id.value]; i < phi_offsets[id.value + 1]; ++i)
        quota.Insert(state, phis[i].slot);
      const auto& block = flow.blocks[id.value];
      quota.Require(block.first_instruction <= program.size() &&
                    block.instruction_count <=
                        program.size() - block.first_instruction);
      for (std::size_t i = block.first_instruction;
           i < std::size_t(block.first_instruction) + block.instruction_count;
           ++i) {
        quota.Step();
        quota.Require(reachable_site[i]);
        const auto& site = program[i];
        if (site.snapshot)
          result.states.push_back({{static_cast<std::uint32_t>(i)}, state});
        if (site.store) quota.Insert(state, *site.store);
      }
      completed[id.value] = std::move(state);
    }
    quota.Require(result.states.size() == creations);
    std::sort(result.states.begin(), result.states.end(),
              [&](const auto& a, const auto& b) {
                quota.Step();
                return a.instruction < b.instruction;
              });
    result.allocated_bytes = quota.initial.bytes - quota.left.bytes;
    result.work = quota.initial.work - quota.left.work;
    return result;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
std::variant<ModuleInitialization, ModuleInitializationError>
BuildModuleInitialization(const SemanticMethod& input,
                          const ModuleSlotSet& entry, SemanticBudget budget) {
  try {
    Quota quota(budget);
    std::vector<ModuleStateSite> sites;
    quota.Size(sites, input.instructions.size());
    for (std::size_t i = 0; i < sites.size(); ++i) {
      quota.Step();
      const auto& instruction = input.instructions[i];
      sites[i].pc = instruction.pc;
      if (const auto* store = std::get_if<ModuleStore>(&instruction.operation))
        sites[i].store = store->slot;
      const auto* load = std::get_if<ModuleLoad>(&instruction.operation);
      sites[i].snapshot =
          (load && load->space == ModuleSpace::kLocal) ||
          std::holds_alternative<FunctionCreation>(instruction.operation) ||
          std::holds_alternative<ClassCreation>(instruction.operation);
    }
    auto built =
        BuildModuleInitialization(sites, input.flow, entry, quota.left);
    if (auto* result = std::get_if<ModuleInitialization>(&built)) {
      quota.Step(result->work);
      // A created function cannot run before the first instruction that can
      // execute user code. Hoisted declarations are emitted as a straight
      // prelude (`definefunc; stmodulevar` ...): stores later in that prelude
      // happen before any call of the earlier function. Extend each function
      // creation's snapshot with the module stores that follow it in its block
      // until anything other than a function creation, module store, pure
      // scalar move or no-op. Each instruction is visited once per creation
      // prelude; blocks are walked in order, so total work is O(I + B).
      std::size_t block = 0;
      for (auto& state : result->states) {
        quota.Step();
        const auto at = state.instruction.value;
        if (at >= input.instructions.size() ||
            !std::holds_alternative<FunctionCreation>(input.instructions[at].operation)) continue;
        while (block + 1 < input.flow.blocks.size() && input.flow.blocks[block + 1].first_instruction <= at) {
          quota.Step(); ++block;
        }
        if (block >= input.flow.blocks.size()) continue;
        const auto& range = input.flow.blocks[block];
        const std::size_t end = std::size_t(range.first_instruction) + range.instruction_count;
        for (auto k = std::size_t(at) + 1; k < end && k < input.instructions.size(); ++k) {
          quota.Step();
          const auto& operation = input.instructions[k].operation;
          if (const auto* store = std::get_if<ModuleStore>(&operation)) {
            quota.Insert(state.slots, store->slot);
            continue;
          }
          const auto* scalar = std::get_if<ScalarAssignment>(&operation);
          if (std::holds_alternative<FunctionCreation>(operation) || std::holds_alternative<NoOperation>(operation) ||
              (scalar && scalar->completion == CompletionEffect::kNormal)) continue;
          break;
        }
      }
      result->allocated_bytes = quota.initial.bytes - quota.left.bytes + result->allocated_bytes;
      result->work = quota.initial.work - quota.left.work;
    }
    return built;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
namespace {
// ExceptionFlow nodes as one-node blocks over all arcs: a store takes effect
// at its Commit, and a snapshot site observes the state at a Before node.
template <class Snapshot>
std::variant<ModuleInitialization, ModuleInitializationError> ExceptionStates(
    const ExceptionFlow& flow, const ModuleSlotSet& entry, SemanticBudget& budget, Snapshot snapshot) {
  using Error = ModuleInitializationError;
  const auto count = flow.nodes.size();
  if (!count || count >= UINT32_MAX) return Error::kInput;
  if (count > budget.work) return Error::kWorkLimit;
  budget.work -= count;
  const std::size_t width = sizeof(ModuleStateSite) + sizeof(FlowBlock);
  if (count > budget.bytes / width) return Error::kResourceLimit;
  budget.bytes -= count * width;
  std::vector<ModuleStateSite> sites(count);
  NormalFlow normal;
  normal.blocks.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& node = flow.nodes[i];
    if (node.instruction.value >= flow.instructions.size() || node.successor_count > kMaxNormalSuccessors)
      return Error::kInput;
    const auto& instruction = flow.instructions[node.instruction.value];
    auto& block = normal.blocks[i];
    block.first_instruction = static_cast<std::uint32_t>(i);
    block.instruction_count = 1;
    block.successor_count = node.successor_count;
    for (std::uint8_t e = 0; e < node.successor_count; ++e) {
      const auto target = node.successors[e].target.value;
      if (target >= count) return Error::kInput;
      block.successors[e] = {BlockId{target}, node.successors[e].kind == ExceptionArcKind::kFallthrough
          ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken};
    }
    sites[i].pc = instruction.pc;
    if (node.phase == ExceptionPhase::kBefore) sites[i].snapshot = snapshot(instruction.operation);
    if (node.phase == ExceptionPhase::kCommit)
      if (const auto* store = std::get_if<ModuleStore>(&instruction.operation)) sites[i].store = store->slot;
  }
  return BuildModuleInitialization(sites, normal, entry, budget);
}
}  // namespace
std::variant<ModuleInitialization, ModuleInitializationError>
ExceptionCreationModuleStates(const ExceptionFlow& flow, const ModuleSlotSet& entry, SemanticBudget budget) {
  using Error = ModuleInitializationError;
  try {
    const auto initial = budget;
    auto built = ExceptionStates(flow, entry, budget, [](const auto& operation) {
      return std::holds_alternative<FunctionCreation>(operation) || std::holds_alternative<ClassCreation>(operation) ||
          std::holds_alternative<BufferedObjectCreation>(operation);
    });
    if (const auto* error = std::get_if<Error>(&built)) return *error;
    auto result = std::get<ModuleInitialization>(std::move(built));
    // Node ids become flow instruction ids (one Before node per instruction).
    std::vector<std::optional<ExceptionNodeId>> before(flow.instructions.size());
    for (std::size_t n = 0; n < flow.nodes.size(); ++n)
      if (flow.nodes[n].phase == ExceptionPhase::kBefore && flow.nodes[n].instruction.value < before.size())
        before[flow.nodes[n].instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(n)};
    for (auto& state : result.states) {
      if (state.instruction.value >= flow.nodes.size()) return Error::kInput;
      state.instruction = flow.nodes[state.instruction.value].instruction;
      // Hoisting prelude, as for normal methods: a created function cannot run
      // before the straight run of creations/module stores/moves that follows
      // it; none of these branch, and none may throw to a local handler.
      if (!std::holds_alternative<FunctionCreation>(flow.instructions[state.instruction.value].operation)) continue;
      for (auto k = std::size_t(state.instruction.value) + 1; k < flow.instructions.size(); ++k) {
        if (!budget.work) return Error::kWorkLimit;
        --budget.work;
        const auto& operation = flow.instructions[k].operation;
        const auto* store = std::get_if<ModuleStore>(&operation);
        const auto* scalar = std::get_if<ScalarAssignment>(&operation);
        if (!store && !std::holds_alternative<FunctionCreation>(operation) &&
            !std::holds_alternative<NoOperation>(operation) &&
            !(scalar && scalar->completion == CompletionEffect::kNormal)) break;
        if (!before[k] || (flow.nodes[before[k]->value].exception && flow.nodes[before[k]->value].exception->target)) break;
        if (store) {
          auto bytes = budget.bytes;
          state.slots = state.slots.With(store->slot, bytes);
          budget.bytes = bytes;
        }
      }
    }
    std::sort(result.states.begin(), result.states.end(),
        [](const auto& a, const auto& b) { return a.instruction < b.instruction; });
    result.allocated_bytes += initial.bytes - budget.bytes;
    result.work += initial.work - budget.work + result.states.size() * std::bit_width(result.states.size());
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::vector<LanguageLoadFact>, ModuleInitializationError>
ProveEntryModuleLoads(const ExceptionFlow& flow, SemanticBudget budget) {
  using Error = ModuleInitializationError;
  try {
    const auto count = flow.nodes.size();
    auto built = ExceptionStates(flow, {}, budget, [](const auto& operation) {
      const auto* load = std::get_if<ModuleLoad>(&operation);
      return load && load->space == ModuleSpace::kLocal && !load->request_namespace;
    });
    if (const auto* error = std::get_if<Error>(&built)) return *error;
    const auto& initialized = std::get<ModuleInitialization>(built);
    std::vector<LanguageLoadFact> facts;
    if (initialized.states.size() > budget.bytes / sizeof(LanguageLoadFact)) return Error::kResourceLimit;
    facts.reserve(initialized.states.size());
    for (const auto& state : initialized.states) {
      if (state.instruction.value >= count) return Error::kInput;
      const auto instruction = flow.nodes[state.instruction.value].instruction.value;
      const auto* load = std::get_if<ModuleLoad>(&flow.instructions[instruction].operation);
      if (!load || !state.slots.Contains(load->slot)) continue;
      if (GuardedAfterCopies(instruction, flow.instructions.size(),
          [&](std::size_t k) { return AccumulatorCopy(std::get_if<ScalarAssignment>(&flow.instructions[k].operation)); },
          [&](std::size_t k) { return AccumulatorGuard(std::get_if<HoleGuard>(&flow.instructions[k].operation)); })) continue;
      facts.push_back({InstructionId{instruction}});
    }
    std::sort(facts.begin(), facts.end(), [](auto a, auto b) { return a.instruction < b.instruction; });
    facts.erase(std::unique(facts.begin(), facts.end(),
        [](auto a, auto b) { return a.instruction == b.instruction; }), facts.end());
    return facts;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
