#include "abcd/analysis/semantic_validation.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "semantic_locals.h"
#include "argument_inputs.h"

namespace abcd::analysis {
namespace {
SemanticProofFailure Fail(SemanticProofError kind,
                          abc::CodeOffset pc = {}) {
  return {kind, pc};
}
bool AcceptsProtocolHole(const SemanticOperation& operation, const RegisterValue& value) {
  if (const auto* copy = std::get_if<ScalarAssignment>(&operation)) {
    const auto* input = std::get_if<ScalarValue>(&copy->expression);
    return copy->completion == CompletionEffect::kNormal && input &&
        (std::holds_alternative<Accumulator>(*input) ||
         std::holds_alternative<abc::RegisterId>(*input));
  }
  if (const auto* creation = std::get_if<ClassCreation>(&operation)) {
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    return reg && *reg == creation->parent;
  }
  // The guard inspects the VM sentinel without exposing it as a JS value.
  if (std::holds_alternative<SuperCheck>(operation))
    return std::holds_alternative<Accumulator>(value.slot);
  return false;
}
}  // namespace

SemanticProofResult VerifySemanticSource(const abc::MethodBody& body,
                                         const SemanticMethod& method,
                                         std::uint64_t frame_size,
                                         std::uint32_t first_abi_register,
                                         SemanticBudget budget,
                                         CapturedValues captures,
                                         std::span<const LanguageLoadFact> initialized_modules,
                                         ClosureWrites closure_writes,
                                         std::span<const CapturePreservation> preserved,
                                         CallResultValues calls, FrameReceiver receiver,
                                         const ArgumentFrameFact* arguments) {
  try {
  if (first_abi_register > frame_size ||
      method.instructions.size() != body.instructions.size() ||
      method.allocated_bytes > budget.bytes)
    return Fail(SemanticProofError::kResourceLimit);
  auto remaining = budget.bytes - method.allocated_bytes;
  auto environments = AnalyzeEnvironments(
      body, method.flow, frame_size, {remaining});
  auto* bound = std::get_if<EnvironmentFlow>(&environments);
  if (!bound) {
    const auto& failure = std::get<EnvironmentFlowFailure>(environments);
    return Fail(failure.kind == EnvironmentFlowError::kResourceLimit
                    ? SemanticProofError::kResourceLimit
                    : SemanticProofError::kEnvironment,
                failure.offset);
  }
  if (bound->allocated_bytes > remaining)
    return Fail(SemanticProofError::kResourceLimit);
  remaining -= bound->allocated_bytes;
  // Normal SSA renames only entry-reachable instructions. The existing lexical
  // flow retains that same per-site domain; dead decoded tails still require
  // PC correspondence and must not acquire SSA reads/writes. Do not invent
  // definitions merely to satisfy a check over physical bytecode positions.
  if (bound->before.size() != body.instructions.size())
    return Fail(SemanticProofError::kSsaMismatch);
  const auto reads = method.ssa.AllReads();
  std::size_t read_cursor = 0;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const InstructionId site{static_cast<std::uint32_t>(i)};
    const bool reachable = bound->before[i].has_value();
    if (method.instructions[i].pc != body.instructions[i].offset ||
        method.ssa.Write(site).has_value() !=
            (reachable && method.instructions[i].effects.normal_registers.write.has_value()))
      return Fail(SemanticProofError::kSsaMismatch, body.instructions[i].offset);
    while (read_cursor < reads.size() && reads[read_cursor].instruction == site) {
      if (!reachable) return Fail(SemanticProofError::kSsaMismatch, body.instructions[i].offset);
      ++read_cursor;
    }
    if (read_cursor < reads.size() && reads[read_cursor].instruction < site)
      return Fail(SemanticProofError::kSsaMismatch, body.instructions[i].offset);
  }
  if (read_cursor != reads.size()) return Fail(SemanticProofError::kSsaMismatch);
  const auto argument_check = VerifyArgumentInputs(body, method, frame_size,
      first_abi_register, arguments, bound->before, budget);
  if (const auto* failure = std::get_if<SemanticProofFailure>(&argument_check)) return *failure;
  const auto& argument_inputs = std::get<ArgumentInputCheck>(argument_check);
  budget.work -= argument_inputs.work;

  auto local_result = ProveLocalAccesses(method, *bound, {remaining, budget.work},
                                        closure_writes, preserved);
  auto* locals = std::get_if<LocalAccessPlan>(&local_result);
  if (!locals) return std::get<SemanticProofFailure>(local_result);
  if (locals->AllocatedBytes() > remaining)
    return Fail(SemanticProofError::kResourceLimit);
  remaining -= locals->AllocatedBytes();
  constexpr auto kLoadProofBytes = sizeof(std::uint8_t) +
      sizeof(LanguageLoadFact) + sizeof(ModuleReadFact) + sizeof(CapturedReadFact);
  if (method.instructions.size() > remaining / kLoadProofBytes)
    return Fail(SemanticProofError::kResourceLimit);
  std::vector<std::uint8_t> language_loads(method.instructions.size());
  std::vector<ModuleReadFact> modules;
  std::vector<LanguageLoadFact> loads;
  std::vector<CapturedReadFact> captured_reads;
  modules.reserve(method.instructions.size());
  loads.reserve(method.instructions.size());
  captured_reads.reserve(method.instructions.size());
  const auto proof_bytes = language_loads.capacity() +
      modules.capacity() * sizeof(ModuleReadFact) +
      loads.capacity() * sizeof(LanguageLoadFact) +
      captured_reads.capacity() * sizeof(CapturedReadFact);
  if (proof_bytes > remaining) return Fail(SemanticProofError::kResourceLimit);
  remaining -= proof_bytes;
  for (const auto& access : locals->accesses)
    if (access.kind == LocalAccessKind::kLoad)
      language_loads[access.instruction.value] = 1;
  if (captures == CapturedValues::kLanguage) {
    for (const auto& site : bound->bindings) {
      const auto* depth = std::get_if<CapturedDepth>(&site.binding.environment);
      if (!depth || !std::holds_alternative<LexicalLoad>(
                        method.instructions[site.instruction].operation))
        continue;
      language_loads[site.instruction] = 1;
      captured_reads.push_back({InstructionId{site.instruction},
                                {*depth, site.binding.slot}});
    }
  }
  std::optional<InstructionId> previous_module;
  for (const auto& premise : initialized_modules) {
    const auto i = premise.instruction.value;
    if (i >= method.instructions.size() ||
        (previous_module && *previous_module >= premise.instruction))
      return Fail(SemanticProofError::kModulePremise);
    const auto* load = std::get_if<ModuleLoad>(&method.instructions[i].operation);
    if (!load || load->space != ModuleSpace::kLocal)
      return Fail(SemanticProofError::kModulePremise, method.instructions[i].pc);
    previous_module = premise.instruction;
    language_loads[i] = 1;
  }
  {
    struct EntryStore { ModuleSlot slot; InstructionId instruction; };
    if (method.flow.blocks.empty()) return Fail(SemanticProofError::kSsaMismatch);
    const auto& entry = method.flow.blocks.front();
    if (entry.first_instruction != 0 || entry.instruction_count > method.instructions.size())
      return Fail(SemanticProofError::kSsaMismatch);
    std::size_t count = 0;
    for (std::size_t i = 0; i < entry.instruction_count; ++i)
      count += std::holds_alternative<ModuleStore>(method.instructions[i].operation);
    if (count > remaining / sizeof(EntryStore)) return Fail(SemanticProofError::kResourceLimit);
    std::vector<EntryStore> stores;
    stores.reserve(count);
    if (stores.capacity() > remaining / sizeof(EntryStore)) return Fail(SemanticProofError::kResourceLimit);
    for (std::size_t i = 0; i < entry.instruction_count; ++i)
      if (const auto* store = std::get_if<ModuleStore>(&method.instructions[i].operation))
        stores.push_back({store->slot, {static_cast<std::uint32_t>(i)}});
    std::sort(stores.begin(), stores.end(), [](const auto& a, const auto& b) {
      return a.slot < b.slot || (a.slot == b.slot && a.instruction < b.instruction);
    });
    stores.erase(std::unique(stores.begin(), stores.end(), [](const auto& a, const auto& b) {
      return a.slot == b.slot;
    }), stores.end());
    // Completed entry stores dominate every later normal block. This is only
    // a candidate premise until the full value solver/read check below proves
    // every store's operand Language. A prior raw read remains unknown, so a
    // read/store cycle cannot certify its own first initialization. Live reads
    // after calls may change values, hence no scalar refinement/forwarding.
    std::size_t block = 0;
    for (std::size_t i = 0; i < method.instructions.size(); ++i) {
      while (block + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block + 1].first_instruction <= i) ++block;
      const auto* load = std::get_if<ModuleLoad>(&method.instructions[i].operation);
      if (!load || !bound->before[i]) continue;
      const InstructionId site{static_cast<std::uint32_t>(i)};
      if (language_loads[i]) {
        modules.push_back({site, ModuleReadProof::kInitialized});
        continue;
      }
      const auto& region = method.flow.blocks[block];
      const auto* guard = i + 1 < std::size_t(region.first_instruction) + region.instruction_count
          ? std::get_if<HoleGuard>(&method.instructions[i + 1].operation) : nullptr;
      if (guard && std::holds_alternative<Accumulator>(guard->value) &&
          std::holds_alternative<HoleNameSlot>(guard->name)) {
        // Preserve the existing whole load/guard pair; an initialization
        // witness does not authorize orphaning or deleting an encoded guard.
        language_loads[i] = 1;
        modules.push_back({site, ModuleReadProof::kGuarded});
        continue;
      }
      if (load->space != ModuleSpace::kLocal) continue;
      const auto found = std::lower_bound(stores.begin(), stores.end(), load->slot,
          [](const auto& store, auto slot) { return store.slot < slot; });
      if (found == stores.end() || found->slot != load->slot || found->instruction >= site) continue;
      language_loads[i] = 1;
      modules.push_back({site, ModuleReadProof::kStored, found->instruction});
    }
    // The entry index is gone before entry facts and the value-flow solver
    // allocate scratch. Witness IDs remain in the existing read ledger.
  }
  for (std::size_t i = 0; i < language_loads.size(); ++i)
    if (language_loads[i])
      loads.push_back({InstructionId{static_cast<std::uint32_t>(i)}});
  if (method.ssa.Values().size() > remaining / sizeof(EntryValueFact))
    return Fail(SemanticProofError::kResourceLimit);
    std::vector<EntryValueFact> entries;
    entries.reserve(method.ssa.Values().size());
    if (entries.capacity() > remaining / sizeof(EntryValueFact))
      return Fail(SemanticProofError::kResourceLimit);
    for (std::size_t i = 0; i < method.ssa.Values().size(); ++i) {
      const auto& value = method.ssa.Values()[i];
      if (std::holds_alternative<MethodEntry>(value.origin) &&
          std::holds_alternative<abc::RegisterId>(value.slot) &&
          std::get<abc::RegisterId>(value.slot).value >= first_abi_register)
        entries.push_back({SsaValueId{static_cast<std::uint32_t>(i)},
                           ValueKinds::Language()});
    }
    remaining -= entries.capacity() * sizeof(EntryValueFact);
    IdfQuota quota{budget.work - locals->work_units};
    auto values = RegisterValueFlow::BuildWithLanguageLoads(
        body, method.ssa, frame_size, entries, loads, quota, {remaining},
        method.instructions, calls, receiver);
    auto* analyzed = std::get_if<RegisterValueFlow>(&values);
    if (!analyzed) {
      const auto& failure = std::get<ValueFlowFailure>(values);
      const auto pc = failure.instruction &&
                              failure.instruction->value < body.instructions.size()
                          ? body.instructions[failure.instruction->value].offset
                          : abc::CodeOffset{};
      return Fail(failure.kind == ValueFlowError::kResourceLimit ||
                          failure.kind == ValueFlowError::kWorkLimit
                      ? SemanticProofError::kResourceLimit
                      : SemanticProofError::kRegisterValue,
                  pc);
    }
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      for (const auto& read : method.ssa.Reads(
               InstructionId{static_cast<std::uint32_t>(i)})) {
        const auto kinds = analyzed->At(read.value);
        // Hole is not JS undefined. Permit only exact marker transport and
        // DEFINECLASS's absent-heritage operand or the explicit super guard.
        // Observable uses remain rejected; source lowering must consume each
        // protocol with its own proof, not turn Hole into a JS literal.
        if (kinds && *kinds == ValueKinds::Hole() &&
            AcceptsProtocolHole(method.instructions[i].operation,
                                method.ssa.Values()[read.value.value])) continue;
        if (!kinds || !kinds->IsLanguage())
          return SemanticProofFailure{SemanticProofError::kInternalValue, body.instructions[i].offset,
              budget.bytes - remaining + analyzed->AllocatedBytes(),
              argument_inputs.work + bound->instruction_visits + locals->work_units + quota.Consumed() +
                  body.instructions.size() + method.ssa.AllReads().size()};
      }
    }
    const auto bytes = method.allocated_bytes +
                       bound->allocated_bytes + locals->AllocatedBytes() +
                       analyzed->AllocatedBytes() +
                       modules.capacity() * sizeof(ModuleReadFact) +
                       captured_reads.capacity() * sizeof(CapturedReadFact);
    return SemanticProof{std::move(*bound),
                         std::move(*analyzed),
                         bytes, std::move(modules), std::move(captured_reads),
                         std::move(*locals), closure_writes, {}, receiver, argument_inputs.fact};
  } catch (const std::bad_alloc&) {
    return Fail(SemanticProofError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SemanticProofError::kResourceLimit);
  }
}
}  // namespace abcd::analysis
