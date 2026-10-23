#include "environment_cleanup.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = CleanupError;
void Require(bool condition, Error error) { if (!condition) throw error; }
template<class T, class... U> T Take(std::variant<U...>&& result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  return std::visit([](const auto& error) -> T {
    if constexpr (!std::is_same_v<T,std::decay_t<decltype(error)>>) throw CleanupFailure{error};
    else throw CleanupFailure{Error::kHandler};
  },result);
}
class Quota {
 public:
  explicit Quota(SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= left_.work,Error::kWorkLimit); left_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width,Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count,sizeof(T)); values.reserve(count); Admit(values.capacity()-count,sizeof(T));
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Reserve(values,count); Step(count); values.resize(count);
  }
  SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work-left_.work; }
 private:
  SemanticBudget initial_, left_;
};
}  // namespace

std::variant<EnvironmentCleanup, CleanupFailure> EnvironmentCleanup::Build(
    const abc::MethodBody& body, abc::ProfileId profile, const abc::FrameLayout& frame,
    SemanticBudget budget) {
  try {
    Quota quota(budget); quota.Step(); quota.Admit(1,sizeof(EnvironmentCleanup));
    Require(!body.tries.empty() && !body.handlers.empty(),Error::kNoRegions);
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(body.instructions.size() <= kMaxId && body.handlers.size() <= kMaxId,Error::kResourceLimit);
    auto flow = Take<ExceptionFlow>(BuildExceptionFlow(body,profile,frame.registers,
        {quota.Remaining().bytes,quota.Remaining().work},{},&frame));
    quota.Admit(flow.allocated_bytes,1); quota.Step(flow.work_units);
    // The upstream environment adapter is byte-bounded, as in phased input
    // preparation. Charge its reported visits plus this pass's indexed work.
    auto environments = Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow,{quota.Remaining().bytes}));
    quota.Admit(environments.allocated_bytes,1); quota.Step(environments.instruction_visits);
    std::vector<std::optional<ExceptionNodeId>> before;
    std::vector<std::optional<HandlerId>> owners;
    std::vector<std::optional<InstructionId>> allocations;
    std::vector<std::uint8_t> referenced;
    quota.Array(before,flow.instructions.size()); quota.Array(owners,flow.instructions.size());
    quota.Array(allocations,environments.environments.Size()); quota.Array(referenced,body.handlers.size());
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      quota.Step(); const auto& node = flow.nodes[i];
      if (node.phase != ExceptionPhase::kBefore) continue;
      Require(node.instruction.value < before.size() && !before[node.instruction.value],Error::kHandler);
      before[node.instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(i)};
    }
    for (const auto& allocation : environments.allocations) {
      quota.Step();
      Require(allocation.environment.value < allocations.size() && allocation.instruction < flow.nodes.size(),Error::kOwnership);
      const auto& node = flow.nodes[allocation.instruction];
      Require(node.phase == ExceptionPhase::kCommit &&
          std::holds_alternative<PushEnvironment>(flow.instructions[node.instruction.value].operation),Error::kOwnership);
      allocations[allocation.environment.value] = node.instruction;
    }
    for (const auto& region : body.tries) {
      quota.Step();
      Require(region.handler_count == 1 && region.first_handler < referenced.size(),Error::kHandler);
      referenced[region.first_handler] = 1;
    }
    const auto boundary = [&](std::uint64_t pc) {
      return std::lower_bound(flow.instructions.begin(),flow.instructions.end(),pc,
          [&](const auto& instruction, auto value) { quota.Step(); return instruction.pc.value < value; });
    };
    EnvironmentCleanup result;
    quota.Reserve(result.handlers_,body.handlers.size()); quota.Reserve(result.pops_,body.instructions.size());
    quota.Reserve(result.moves_,body.instructions.size());
    for (std::size_t h = 0; h < body.handlers.size(); ++h) {
      quota.Step(); const auto& handler = body.handlers[h];
      Require(referenced[h] && !handler.type,Error::kHandler);
      const auto start = boundary(handler.offset.value);
      Require(start != flow.instructions.end() && start->pc == handler.offset,Error::kHandler);
      const auto first = static_cast<std::size_t>(start-flow.instructions.begin());
      std::size_t last;
      if (handler.length != 0) {
        const auto end = boundary(std::uint64_t(handler.offset.value) + handler.length);
        Require(start != end,Error::kHandler);
        last = static_cast<std::size_t>(end-flow.instructions.begin())-1;
      } else {
        // es2abc may record code_size 0; the pinned interpreter uses only the
        // start. The handler then extends linearly to its first THROW.
        last = first;
        while (last < flow.instructions.size() &&
               !std::holds_alternative<ThrowOperation>(flow.instructions[last].operation)) { quota.Step(); ++last; }
        Require(last < flow.instructions.size(),Error::kHandler);
      }
      std::vector<std::uint64_t> stored;
      quota.Reserve(stored, last - first + 1);
      const auto first_pop = static_cast<std::uint32_t>(result.pops_.size());
      for (auto i = first; i <= last; ++i) {
        quota.Step(); const auto& instruction = flow.instructions[i];
        Require(!owners[i] && before[i],Error::kHandler); owners[i] = HandlerId{static_cast<std::uint32_t>(h)};
        const auto& node = flow.nodes[before[i]->value];
        if (i == last) {
          const auto* rethrow = std::get_if<ThrowOperation>(&instruction.operation);
          Require(rethrow && rethrow->kind==ThrowKind::kValue && std::holds_alternative<Accumulator>(rethrow->value) && node.exception &&
              !node.exception->target && instruction.effects.exceptional == ExceptionalCompletion::kAlwaysThrow,
              Error::kRethrow);
          continue;
        }
        if (const auto* move = std::get_if<ScalarAssignment>(&instruction.operation)) {
          // `sta vR` saves the pending exception; `lda vR` restores it from a
          // register saved here. Neither throws nor has effects; THROW leaves
          // the method below, so the registers are dead afterwards.
          const auto* from = std::get_if<ScalarValue>(&move->expression);
          Require(from && move->completion == CompletionEffect::kNormal &&
              instruction.effects.exceptional == ExceptionalCompletion::kImpossible &&
              instruction.effects.normal == LanguageEffect::kNone &&
              instruction.effects.before_throw == LanguageEffect::kNone,Error::kHandler);
          if (const auto* reg = std::get_if<abc::RegisterId>(&move->destination)) {
            Require(std::holds_alternative<Accumulator>(*from),Error::kHandler);
            stored.push_back(reg->value);
          } else {
            const auto* source = std::get_if<abc::RegisterId>(&*from);
            Require(source && std::find(stored.begin(),stored.end(),source->value) != stored.end(),Error::kHandler);
          }
          result.moves_.push_back(InstructionId{static_cast<std::uint32_t>(i)});
          continue;
        }
        Require(std::holds_alternative<PopEnvironment>(instruction.operation) &&
            !instruction.effects.normal_registers.write &&
            instruction.effects.exceptional == ExceptionalCompletion::kImpossible &&
            instruction.effects.before_throw == LanguageEffect::kNone &&
            instruction.effects.normal == (LanguageEffect::kEnvironmentRead | LanguageEffect::kEnvironmentWrite),Error::kHandler);
        const auto& state = environments.before[before[i]->value];
        const auto* local = state ? std::get_if<EnvironmentId>(&*state) : nullptr;
        Require(local && local->value < allocations.size() && allocations[local->value],Error::kOwnership);
        result.pops_.push_back({InstructionId{static_cast<std::uint32_t>(i)},*allocations[local->value]});
      }
      Require(result.pops_.size() > first_pop,Error::kHandler);
      result.handlers_.push_back({HandlerId{static_cast<std::uint32_t>(h)},
          InstructionId{static_cast<std::uint32_t>(last)},first_pop,
          static_cast<std::uint32_t>(result.pops_.size()-first_pop)});
    }
    // Catch installs the pending exception in acc. Only linear POPLEXENV nodes
    // may precede THROW acc; a normal jump into the same bytes bypasses Catch
    // and must not be mistaken for a transparent exceptional path.
    for (const auto& node : flow.nodes) {
      quota.Step();
      for (std::uint8_t i = 0; i < node.successor_count; ++i) {
        quota.Step(); const auto& arc = node.successors[i]; const auto& target = flow.nodes[arc.target.value];
        const auto owner = owners[target.instruction.value];
        if (!owner || target.phase == ExceptionPhase::kCatch) continue;
        Require(owners[node.instruction.value] == owner && arc.kind != ExceptionArcKind::kException,Error::kEntry);
      }
    }
    result.bytes_ = sizeof(EnvironmentCleanup) + result.handlers_.capacity()*sizeof(CleanupHandler) + result.pops_.capacity()*sizeof(CleanupPop) +
        result.moves_.capacity()*sizeof(InstructionId);
    result.work_ = quota.Work(); return result;
  } catch (const CleanupFailure& failure) { return failure;
  } catch (Error error) { return CleanupFailure{error};
  } catch (const std::bad_alloc&) { return CleanupFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return CleanupFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
