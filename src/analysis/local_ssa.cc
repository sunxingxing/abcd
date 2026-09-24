#include "abcd/analysis/local_ssa.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <tuple>

#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/linear_lexicals.h"

namespace abcd::analysis {
namespace {
auto Key(LocalCell cell) {
  return std::tuple(cell.environment.value, cell.slot.value);
}
}  // namespace

std::variant<LocalSsa, LocalSsaError> BuildLocalSsa(
    const abc::MethodBody& body, const NormalFlow& graph,
    const RegisterSsa& registers, std::uint64_t frame_size,
    abc::ProfileId profile, IdfBudget budget,
    std::uint64_t work) {
  using Error = LocalSsaError;
  if (!body.tries.empty() || !body.handlers.empty()) return Error::kControlFlow;
  for (const auto& site : body.instructions)
    if (site.branch_target && site.branch_target->value <= site.offset.value)
      return Error::kControlFlow;
  try {
    auto remaining_work = work;
    const auto charge = [&](std::uint64_t amount = 1) {
      if (amount > remaining_work) throw Error::kResourceLimit;
      remaining_work -= amount;
    };
    const auto environments = AnalyzeEnvironments(body, graph, frame_size, {budget.bytes});
    const auto* bound = std::get_if<EnvironmentFlow>(&environments);
    if (!bound) {
      return std::get<EnvironmentFlowFailure>(environments).kind ==
                     EnvironmentFlowError::kResourceLimit
                 ? Error::kResourceLimit : Error::kEnvironment;
    }
    if (bound->allocated_bytes > budget.bytes) return Error::kResourceLimit;
    budget.bytes -= bound->allocated_bytes;
    charge(bound->instruction_visits);
    if (bound->bindings.size() > budget.bytes / sizeof(LocalCell))
      return Error::kResourceLimit;
    std::vector<LocalCell> cells;
    cells.reserve(bound->bindings.size());
    if (cells.capacity() > budget.bytes / sizeof(LocalCell)) return Error::kResourceLimit;
    budget.bytes -= cells.capacity() * sizeof(LocalCell);
    for (const auto& site : bound->bindings) {
      charge();
      if (const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment))
        cells.push_back({*owner, site.binding.slot});
    }
    std::sort(cells.begin(), cells.end(), [&](auto a, auto b) {
      charge();
      return Key(a) < Key(b);
    });
    cells.erase(std::unique(cells.begin(), cells.end(), [&](auto a, auto b) {
      charge();
      return Key(a) == Key(b);
    }), cells.end());
    if (cells.size() > UINT32_MAX ||
        body.instructions.size() > UINT32_MAX ||
        body.instructions.size() > budget.bytes / sizeof(RegisterSite))
      return Error::kResourceLimit;
    std::vector<RegisterSite> sites(body.instructions.size());
    if (sites.capacity() > budget.bytes / sizeof(RegisterSite)) return Error::kResourceLimit;
    budget.bytes -= sites.capacity() * sizeof(RegisterSite);
    if (registers.Values().size() > UINT32_MAX ||
        registers.Values().size() > budget.bytes) return Error::kResourceLimit;
    std::vector<std::uint8_t> environment_values(registers.Values().size());
    if (environment_values.capacity() > budget.bytes) return Error::kResourceLimit;
    budget.bytes -= environment_values.capacity();
    std::optional<InstructionId> first_capture;
    std::size_t binding = 0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
      charge();
      const auto& instruction = body.instructions[i];
      sites[i].pc = instruction.offset;
      // No implicit bytecode rewriting: this effect program only describes
      // accesses to cell bindings, never the instruction's VM register effect.
      const auto lifted = LiftSemanticInstruction(instruction, profile, frame_size);
      const auto* semantic = std::get_if<SemanticInstruction>(&lifted);
      if (!semantic) return Error::kInstruction;
      const auto& operation = semantic->operation;
      if (std::holds_alternative<CurrentEnvironment>(operation))
        return Error::kEnvironment;
      if (std::holds_alternative<PushEnvironment>(operation)) {
        const auto value = registers.Write(InstructionId{static_cast<std::uint32_t>(i)});
        if (value) {
          if (value->value >= environment_values.size()) return Error::kInstruction;
          environment_values[value->value] = 1;
        } else if (bound->before[i]) {
          return Error::kInstruction;
        }
      }
      // Literal buffers can install method templates capturing the current
      // environment too. Without decoded literal evidence, both object and
      // array buffers are barriers, even when this particular buffer is plain.
      if (!first_capture &&
          (std::holds_alternative<FunctionCreation>(operation) ||
           std::holds_alternative<BufferedObjectCreation>(operation) ||
           std::holds_alternative<BufferedArrayCreation>(operation)))
        first_capture = InstructionId{static_cast<std::uint32_t>(i)};
      if (binding >= bound->bindings.size() || bound->bindings[binding].instruction != i)
        continue;
      const auto& site = bound->bindings[binding++];
      const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment);
      if (!owner) continue;
      const LocalCell cell{*owner, site.binding.slot};
      const auto found = std::lower_bound(cells.begin(), cells.end(), cell,
          [&](auto a, auto b) { charge(); return Key(a) < Key(b); });
      if (found == cells.end() || Key(*found) != Key(cell)) return Error::kEnvironment;
      const abc::RegisterId slot{static_cast<std::uint32_t>(found - cells.begin())};
      if (std::holds_alternative<LexicalLoad>(operation)) {
        sites[i].effect.reads[0] = slot;
        sites[i].effect.read_count = 1;
      } else if (std::holds_alternative<LexicalStore>(operation)) {
        sites[i].effect.write = slot;
      } else {
        return Error::kInstruction;
      }
    }
    // Reject even a private copy of a raw environment identity; admitting
    // copy chains would require a separate escape proof. Phi inputs count as
    // uses even when the phi result is otherwise dead.
    for (const auto& read : registers.AllReads()) {
      charge();
      if (read.value.value >= environment_values.size()) return Error::kInstruction;
      if (environment_values[read.value.value]) return Error::kEnvironment;
    }
    for (std::size_t i = 0; i < registers.Values().size(); ++i) {
      charge();
      if (!std::holds_alternative<PhiValue>(registers.Values()[i].origin)) continue;
      for (const auto& input : registers.Inputs(SsaValueId{static_cast<std::uint32_t>(i)})) {
        charge();
        if (input.value.value >= environment_values.size()) return Error::kInstruction;
        if (environment_values[input.value.value]) return Error::kEnvironment;
      }
    }
    IdfQuota quota{remaining_work};
    auto built = RegisterSsa::BuildSites(sites, graph, cells.size(), quota, budget);
    auto* values = std::get_if<RegisterSsa>(&built);
    if (!values) {
      if (const auto* error = std::get_if<RegisterUseFailure>(&built))
        return error->kind == RegisterUseError::kResourceLimit ||
                       error->kind == RegisterUseError::kWorkLimit
                   ? Error::kResourceLimit : Error::kInstruction;
      if (const auto* error = std::get_if<IdfError>(&built))
        return *error == IdfError::kResourceLimit || *error == IdfError::kWorkLimit
                   ? Error::kResourceLimit : Error::kControlFlow;
      if (const auto* error = std::get_if<DominatorError>(&built))
        return *error == DominatorError::kResourceLimit
                   ? Error::kResourceLimit : Error::kControlFlow;
      return Error::kResourceLimit;  // SsaError has only byte/work exhaustion.
    }
    charge(quota.Consumed());
    return LocalSsa{std::move(*values), first_capture, work - remaining_work};
  } catch (LocalSsaError error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
