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
      result->allocated_bytes += quota.initial.bytes - quota.left.bytes;
      result->work += quota.initial.work - quota.left.work;
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
}  // namespace abcd::analysis
