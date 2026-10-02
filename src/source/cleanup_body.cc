#include "cleanup_body.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<CleanupBody, CleanupFailure> CleanupBody::Build(const abc::MethodBody& body,
    abc::ProfileId profile, const abc::FrameLayout& frame, analysis::SemanticBudget budget) {
  using Error = CleanupError;
  try {
    auto remaining = budget;
    const auto work = [&](std::uint64_t count) {
      if (count > remaining.work) throw Error::kWorkLimit;
      remaining.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > remaining.bytes / width) throw Error::kResourceLimit;
      remaining.bytes -= count * width;
    };
    const auto copy = [&]<class T>(std::vector<T>& to, const std::vector<T>& from) {
      work(from.size()); admit(from.size(),sizeof(T)); to.reserve(from.size());
      admit(to.capacity()-from.size(),sizeof(T)); to.assign(from.begin(),from.end());
    };
    admit(1,sizeof(CleanupBody));
    abc::MethodBody original, normal;
    copy(original.instructions,body.instructions); copy(original.tries,body.tries); copy(original.handlers,body.handlers);
    copy(normal.instructions,body.instructions);
    std::vector<std::optional<CleanupInstructionKind>> kinds;
    work(body.instructions.size()); admit(body.instructions.size(),sizeof(decltype(kinds)::value_type));
    kinds.resize(body.instructions.size());
    admit(kinds.capacity()-body.instructions.size(),sizeof(decltype(kinds)::value_type));
    const auto retained = budget.bytes-remaining.bytes;
    auto built = EnvironmentCleanup::Build(original,profile,frame,remaining);
    auto* proof = std::get_if<EnvironmentCleanup>(&built);
    if (!proof) return std::get<CleanupFailure>(std::move(built));
    work(proof->Work()); admit(proof->AllocatedBytes(),1);
    for (const auto& pop : proof->Pops()) {
      work(1);
      if (pop.instruction.value >= kinds.size() || kinds[pop.instruction.value]) throw Error::kHandler;
      kinds[pop.instruction.value] = CleanupInstructionKind::kPop;
    }
    for (const auto& handler : proof->Handlers()) {
      work(1);
      if (handler.rethrow.value >= kinds.size() || kinds[handler.rethrow.value]) throw Error::kHandler;
      kinds[handler.rethrow.value] = CleanupInstructionKind::kRethrow;
    }
    const auto bytes = retained+proof->AllocatedBytes();
    return CleanupBody{std::move(original),std::move(normal),std::move(*proof),std::move(kinds),bytes,budget.work-remaining.work};
  } catch (Error error) { return CleanupFailure{error};
  } catch (const std::bad_alloc&) { return CleanupFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return CleanupFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
