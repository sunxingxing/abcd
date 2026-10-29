#include "caller_exit.h"

#include <algorithm>
#include <bit>

namespace abcd::source {
using namespace analysis;
bool CallerExits::operator()(OpId op, SemanticBudget& budget) {
  const auto fail = [](ForInError kind) { return ForInFailure{kind}; };
  const auto step = [&](std::uint64_t work) {
    if (work > budget.work) throw fail(ForInError::kWorkLimit);
    budget.work -= work;
  };
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > budget.bytes / width) throw fail(ForInError::kResourceLimit);
    budget.bytes -= count * width;
  };
  step(std::bit_width(graph_.exceptional_exits.size()) + 1);
  const auto exit = std::lower_bound(graph_.exceptional_exits.begin(), graph_.exceptional_exits.end(), op,
      [](const auto& item, OpId wanted) { return item.op < wanted; });
  if (exit == graph_.exceptional_exits.end() || exit->op != op || !exit->handler) return true;
  if (!forwarding_) {
    auto built = AnalyzeExceptionForwarding(graph_, budget);
    if (const auto* failure = std::get_if<ForInFailure>(&built)) throw *failure;
    forwarding_ = std::get<ExceptionForwarding>(std::move(built));
    admit(forwarding_->allocated_bytes, 1); step(forwarding_->work);
    admit(graph_.blocks.size(), sizeof(std::optional<ValueId>)); catches_.resize(graph_.blocks.size());
    for (std::uint32_t v = 0; v < graph_.values.size(); ++v) {
      step(1);
      if (const auto* entry = std::get_if<CatchDefinition>(&graph_.values[v].origin);
          entry && entry->block.value < catches_.size()) catches_[entry->block.value] = ValueId{v};
    }
  }
  const auto entry = exit->handler->entry.value;
  if (entry >= catches_.size() || !catches_[entry]) return false;
  const auto& summary = forwarding_->blocks[entry];
  return summary && !summary->destination && summary->payload == *catches_[entry];
}
}  // namespace abcd::source
