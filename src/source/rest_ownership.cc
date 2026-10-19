#include "rest_ownership.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/abc/register_access.h"
#include "abcd/analysis/argument_window.h"
#include "abcd/analysis/value_environment.h"
namespace abcd::source {
std::variant<RestOwnership, RestOwnershipError> ProveRestOwnership(
    const analysis::ValueProgram& graph, const abc::MethodBody& body,
    const abc::FrameLayout& frame, std::uint16_t first,
    analysis::SemanticBudget budget, RestOwnershipFailure* failure,
    const analysis::ValueEnvironments* environments) {
  using namespace analysis; using Error = RestOwnershipError;
  if (failure) *failure = {};
  try {
    const auto initial = budget;
    const auto step = [&](std::uint64_t count = 1) {if (count > budget.work) throw Error::kWorkLimit; budget.work -= count;};
    const auto admit = [&](std::size_t count, std::size_t width) {if (count > budget.bytes / width) throw Error::kResourceLimit; budget.bytes -= count * width;};
    const auto window_result = ArgumentWindow::Build(frame);
    const auto* window = std::get_if<ArgumentWindow>(&window_result);
    if (!window || window->Count() != std::uint32_t{first} + 1) return Error::kFrame;
    const auto refuse = [&](Error kind, std::optional<OpId> operation = {}) {
      if (failure) {
        failure->kind = kind;
        failure->operation = operation;
        if (operation && operation->value < graph.ops.size()) {
          const auto origin = graph.ops[operation->value].origin;
          if (origin.value < graph.origins.size())
            failure->pc = graph.origins[origin.value].pc;
        }
      }
      throw kind;
    };
    std::optional<OpId> allocation;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i]; if (op.life != ValueLife::kLive) continue;
      if (std::holds_alternative<ValueArgumentSnapshot>(op.operation)) return Error::kArgumentUse;
      if (const auto* rest = std::get_if<ValueRestArguments>(&op.operation)) {
        if (allocation || rest->first_argument != first) return Error::kAllocation;
        allocation = OpId{static_cast<std::uint32_t>(i)};
      }
    }
    if (!allocation) return Error::kAllocation;
    const auto& rest = graph.ops[allocation->value];
    // Reverse reachability indexes all possible work before the allocation.
    // Phased IR uses the same graph, with the allocation in its Before block.
    // Commit/handler edges therefore participate without flattening completion.
    // The same traversal exposes a cycle through its block; native rest cannot
    // replace distinct arrays allocated on repeated loop iterations.
    admit(graph.blocks.size(), sizeof(std::vector<WorkBlockId>));
    std::vector<std::vector<WorkBlockId>> predecessors(graph.blocks.size());
    std::vector<std::uint32_t> counts;
    admit(graph.blocks.size(), sizeof(std::uint32_t)); counts.resize(graph.blocks.size());
    for (const auto& edge : graph.edges) {step(); ++counts[edge.target.value];}
    for (std::size_t b = 0; b < counts.size(); ++b) {
      step(); admit(counts[b], sizeof(WorkBlockId)); predecessors[b].reserve(counts[b]);
      admit(predecessors[b].capacity() - counts[b], sizeof(WorkBlockId));
    }
    for (const auto& edge : graph.edges) {step(); predecessors[edge.target.value].push_back(edge.source);}
    admit(graph.blocks.size(), sizeof(std::uint8_t)); std::vector<std::uint8_t> before(graph.blocks.size());
    admit(graph.blocks.size(), sizeof(WorkBlockId)); std::vector<WorkBlockId> pending; pending.reserve(graph.blocks.size());
    admit(pending.capacity() - graph.blocks.size(), sizeof(WorkBlockId));
    before[rest.block.value] = 1; pending.push_back(rest.block);
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) for (const auto prior : predecessors[pending[cursor].value]) {
      step(); if (prior == rest.block) return Error::kAllocation;
      if (!before[prior.value]) {before[prior.value] = 1; pending.push_back(prior);}
    }
    const auto prefix = [&](const ValueOp& op) {
      return op.life == ValueLife::kLive && before[op.block.value] &&
             (op.block != rest.block || op.order < rest.order);
    };
    bool private_prefix = false;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      if (!prefix(op)) continue;
      private_prefix |= op.normal != LanguageEffect::kNone ||
                        op.before_throw != LanguageEffect::kNone ||
                        op.exceptional != ExceptionalCompletion::kImpossible;
    }
    std::vector<RestDeclarationEvent> declarations;
    if (private_prefix) {
      // A new path only: preserve the legacy proof/cost for pure transports.
      // Closing this cut excludes early publication/return through side paths.
      for (const auto& edge : graph.edges) {
        step();
        if (edge.source != rest.block && before[edge.source.value] &&
            !before[edge.target.value])
          refuse(Error::kOrder, graph.blocks[edge.source.value].first_op);
      }
      // Borrow the same immutable topology already verified by the input owner;
      // do not allocate/redecode a second environment tree per rest proof.
      if (!environments || environments->revision != graph.revision ||
          environments->cfg_revision != graph.cfg_revision)
        refuse(Error::kOrder, allocation);
      admit(environments->flow.environments.Size(),
            sizeof(std::optional<OpId>));
      std::vector<std::optional<OpId>> allocated(
          environments->flow.environments.Size());
      for (const auto& entry : environments->flow.allocations) {
        step();
        if (entry.environment.value >= allocated.size() ||
            entry.instruction >= graph.ops.size())
          refuse(Error::kOrder, allocation);
        if (prefix(graph.ops[entry.instruction]))
          allocated[entry.environment.value] = OpId{entry.instruction};
      }
      admit(graph.ops.size(), sizeof(RestDeclarationEvent));
      declarations.reserve(graph.ops.size());
      admit(declarations.capacity() - graph.ops.size(),
            sizeof(RestDeclarationEvent));
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step();
        const auto& op = graph.ops[i];
        if (!prefix(op)) continue;
        const OpId id{static_cast<std::uint32_t>(i)};
        if (op.normal == LanguageEffect::kNone &&
            op.before_throw == LanguageEffect::kNone &&
            op.exceptional == ExceptionalCompletion::kImpossible)
          continue;
        if (std::holds_alternative<ValueEnvironmentPush>(op.operation)) {
          // RuntimeNewLexicalEnv installs private Hole-filled storage; only
          // resource failure is crossed (already excluded by the source owner).
          if (op.exception_cause != ExceptionCause::kResourceExhaustion ||
              op.use_count != 0)
            refuse(Error::kOrder, id);
          declarations.push_back({id, RestDeclarationRole::kEnvironment});
          continue;
        }
        if (const auto* function =
                std::get_if<ValueFunctionCreation>(&op.operation)) {
          // interpreter-inl.cpp DEFINEFUNC / RuntimeDefinefunc: immutable pool
          // lookup, allocation and env/module/home attachment, no invocation.
          // Target-kind/capture admission remains the real family owner's job.
          constexpr auto effects =
              LanguageEffect::kEnvironmentRead | LanguageEffect::kModuleRead |
              LanguageEffect::kHeapRead | LanguageEffect::kAllocation;
          if (function->kind != CreationKind::kFunction ||
              function->home_object != HomeObjectInput::kCurrentFunction ||
              op.normal != effects || op.before_throw != effects ||
              op.exceptional != ExceptionalCompletion::kMayThrow)
            refuse(Error::kOrder, id);
          declarations.push_back({id, RestDeclarationRole::kFunction});
          continue;
        }
        if (const auto* store = std::get_if<ValueLexicalStore>(&op.operation)) {
          constexpr auto effects =
              LanguageEffect::kEnvironmentRead | LanguageEffect::kLexicalWrite;
          if (op.normal != effects ||
              op.before_throw != LanguageEffect::kNone ||
              op.exceptional != ExceptionalCompletion::kImpossible ||
              op.use_count != 1 || i >= environments->flow.before.size() ||
              !environments->flow.before[i])
            refuse(Error::kOrder, id);
          step(std::numeric_limits<std::uint32_t>::digits);
          const auto resolved = environments->flow.environments.Resolve(
              *environments->flow.before[i], store->address);
          const auto* binding = std::get_if<LexicalBinding>(&resolved);
          const auto* local =
              binding ? std::get_if<EnvironmentId>(&binding->environment)
                      : nullptr;
          if (!local || local->value >= allocated.size() ||
              !allocated[local->value])
            refuse(Error::kOrder, id);
          const auto& use = graph.uses[op.first_use.value];
          const auto* value = std::get_if<ValueId>(&use.operand);
          if (use.role != OperandRole::kStoredValue ||
              (value && !graph.values[value->value].domain.IsLanguage()))
            refuse(Error::kOrder, id);
          declarations.push_back({id, RestDeclarationRole::kCellStore});
          continue;
        }
        // Includes calls, lexical reads, guards, argument snapshots, external
        // writes and classes with effectful heritage. No whitelist of layouts.
        refuse(Error::kOrder, id);
      }
    }
    // Original physical slots are implicit COPYRESTARGS inputs. Check writes
    // against original instruction coordinates, rather than printed bindings.
    const auto physical = *window->At(first);
    if (graph.input.sites.size() != body.instructions.size()) return Error::kFrame;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      step(); const auto& instruction = body.instructions[i];
      const auto mapped = graph.input.sites[i];
      if (!mapped || mapped->value >= graph.ops.size()) continue;
      const auto& op = graph.ops[mapped->value];
      if (!before[op.block.value] || (op.block == rest.block && op.order >= rest.order)) continue;
      const auto accesses = abc::GetRegisterAccesses(instruction.decoded, frame.registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&accesses); if (!known) return Error::kFrame;
      for (std::size_t i = 0; i < known->count; ++i) {
        step(); const auto& range = known->windows[i];
        if (range.access != abc::OperandAccess::kRead &&
            physical.value >= range.first.value &&
            physical.value - range.first.value < range.count)
          refuse(Error::kArgumentUse, *mapped);
      }
    }
    // Follow the last declared scalar's transparent def-use transports once.
    // It may be copied and overwritten, but cannot become an observable array
    // value merely because the source signature now binds ...rest.
    admit(graph.values.size(), sizeof(std::uint8_t)); std::vector<std::uint8_t> tainted(graph.values.size());
    admit(graph.values.size(), sizeof(ValueId)); std::vector<ValueId> values; values.reserve(graph.values.size());
    admit(values.capacity() - graph.values.size(), sizeof(ValueId));
    const auto mark = [&](ValueId id) {if (!tainted[id.value]) {tainted[id.value] = 1; values.push_back(id);}};
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      step(); const auto* entry = std::get_if<EntryDefinition>(&graph.values[i].origin);
      const auto* parameter = entry ? std::get_if<ParameterEntry>(entry) : nullptr;
      if (parameter && parameter->index == first) mark({static_cast<std::uint32_t>(i)});
    }
    for (std::size_t cursor = 0; cursor < values.size(); ++cursor) {
      for (auto use = graph.values[values[cursor].value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value]; if (occurrence.life != ValueLife::kLive) continue;
        if (const auto* phi = std::get_if<ValueId>(&occurrence.owner)) {mark(*phi); continue;}
        const auto* owner = std::get_if<OpId>(&occurrence.owner); if (!owner) return Error::kArgumentUse;
        const auto& op = graph.ops[owner->value]; if (op.life != ValueLife::kLive) continue;
        if (!std::holds_alternative<ValueCopy>(op.operation) || !op.result)
          refuse(Error::kArgumentUse, *owner);
        mark(*op.result);
      }
    }
    return RestOwnership{initial.bytes - budget.bytes,
                         initial.work - budget.work, std::move(declarations)};
  } catch (const RestOwnershipError error) {
    if (failure) failure->kind = error;
    return error;
  } catch (const std::bad_alloc&) {
    return RestOwnershipError::kResourceLimit;
  } catch (const std::length_error&) {
    return RestOwnershipError::kResourceLimit;
  }
}
std::variant<RestOwnership, RestOwnershipError> VerifyRestOwnership(
    const analysis::ValueProgram& graph, const abc::MethodBody& body,
    const abc::FrameLayout& frame, std::uint16_t first,
    const RestOwnership& candidate, analysis::SemanticBudget budget,
    const analysis::ValueEnvironments* environments) {
  auto actual = ProveRestOwnership(graph, body, frame, first, budget, nullptr,
                                   environments);
  if (auto* proof = std::get_if<RestOwnership>(&actual)) {
    if (proof->declarations.size() != candidate.declarations.size())
      return RestOwnershipError::kOrder;
    const auto count = proof->declarations.size();
    if (count > budget.work - proof->work)
      return RestOwnershipError::kWorkLimit;
    proof->work += count;
    for (std::size_t i = 0; i < count; ++i)
      if (proof->declarations[i] != candidate.declarations[i])
        return RestOwnershipError::kOrder;
  }
  return actual;
}
}
