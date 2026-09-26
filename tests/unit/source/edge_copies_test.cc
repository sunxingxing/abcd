#include "../../../src/source/edge_copies.h"

#include <bit>
#include <array>
#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
EdgeCopies Plan(std::span<const ParallelCopy> copies) {
  auto built = BuildEdgeCopies(copies);
  Check(std::holds_alternative<EdgeCopies>(built));
  auto plan = std::get<EdgeCopies>(std::move(built));
  Check(std::holds_alternative<std::uint64_t>(VerifyEdgeCopies(copies, plan)));
  return plan;
}
void Swap() {
  const std::array copies{ParallelCopy{{0}, analysis::ValueId{1}}, ParallelCopy{{1}, analysis::ValueId{0}}};
  const auto plan = Plan(copies);
  Check(plan.temporaries == 1 && plan.steps.size() == 3);
  Check(std::holds_alternative<CopyTempId>(plan.steps.front().destination));
  EdgeCopies destructive;
  destructive.steps = {{analysis::ValueId{0}, analysis::ValueOperand{analysis::ValueId{1}}},
                       {analysis::ValueId{1}, analysis::ValueOperand{analysis::ValueId{0}}}};
  Check(std::get<CopyError>(VerifyEdgeCopies(copies, destructive)) == CopyError::kValue);
  auto forged = plan;
  forged.steps.front().destination = analysis::ValueId{99};
  Check(std::get<CopyError>(VerifyEdgeCopies(copies, forged)) == CopyError::kDestination);
  forged = plan;
  forged.steps.front().source = CopyTempId{0};
  Check(std::get<CopyError>(VerifyEdgeCopies(copies, forged)) == CopyError::kTemporary);
  analysis::SemanticBudget budget;
  budget.work = plan.work;
  Check(std::holds_alternative<EdgeCopies>(BuildEdgeCopies(copies, budget)));
  --budget.work;
  Check(std::get<CopyError>(BuildEdgeCopies(copies, budget)) == CopyError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  Check(std::get<CopyError>(BuildEdgeCopies(copies, budget)) == CopyError::kResourceLimit);
}
void Shapes() {
  const std::array copies{
      ParallelCopy{{0}, analysis::ValueId{1}}, ParallelCopy{{1}, analysis::ValueId{0}},
      ParallelCopy{{2}, analysis::ValueId{0}}, ParallelCopy{{3}, analysis::ValueId{2}},
      ParallelCopy{{4}, analysis::ValueId{4}}, ParallelCopy{{5}, analysis::ValueId{4}},
      ParallelCopy{{6}, analysis::IntegerLiteral{-1}},
      ParallelCopy{{7}, analysis::Float64Bits{std::bit_cast<std::uint64_t>(-0.0)}},
      ParallelCopy{{8}, analysis::SpecialValue::kUndefined}};
  const auto plan = Plan(copies);
  Check(plan.temporaries == 1 && plan.steps.size() == copies.size());
  auto duplicate = copies;
  duplicate[1].destination = duplicate[0].destination;
  Check(std::get<CopyError>(BuildEdgeCopies(duplicate)) == CopyError::kDuplicateDestination);
  Check(Plan({}).steps.empty());
}
void SmallExhaustive() {
  // Small slow oracle domain: every three-location graph, including fanout,
  // chains, self edges, cycles, external values and literal RHS. Not production.
  const std::array<analysis::ValueOperand, 5> sources{analysis::ValueId{0}, analysis::ValueId{1},
      analysis::ValueId{2}, analysis::ValueId{3}, analysis::IntegerLiteral{7}};
  for (const auto& a : sources) for (const auto& b : sources) for (const auto& c : sources) {
    const std::array copies{ParallelCopy{{0}, a}, ParallelCopy{{1}, b}, ParallelCopy{{2}, c}};
    static_cast<void>(Plan(copies));
  }
}
enum class Shape { kChain, kCycle, kFanout, kDisjointSwaps };
void Scaling() {
  for (const std::uint32_t count : {200U, 201U, 4096U}) {
    for (const auto shape : {Shape::kChain, Shape::kCycle, Shape::kFanout, Shape::kDisjointSwaps}) {
      std::vector<ParallelCopy> copies;
      for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t source = i + 1;
        switch (shape) {
          case Shape::kChain: break;
          case Shape::kCycle: source %= count; break;
          case Shape::kFanout: source = count; break;
          case Shape::kDisjointSwaps: source = i ^ 1U; break;
        }
        copies.push_back({{i}, analysis::ValueId{source}});
      }
      const auto plan = Plan(copies);
      constexpr std::uint64_t kWorkPerCopyLogBound = 64;
      Check(plan.work <= kWorkPerCopyLogBound * count * std::bit_width(count));
      Check(plan.temporaries == (shape == Shape::kCycle ? 1U : shape == Shape::kDisjointSwaps ? count / 2 : 0U));
    }
  }
}
}  // namespace
int main() { Swap(); Shapes(); SmallExhaustive(); Scaling(); }
