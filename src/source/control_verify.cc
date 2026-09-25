#include "control_plan.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct RegionCheck { ControlId first; std::optional<analysis::WorkBlockId> stop; };
}  // namespace

std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan,
    analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto count = graph.blocks.size();
    Require(count != 0 && count < std::numeric_limits<std::uint32_t>::max() &&
        plan.nodes.size() == count && plan.edges.size() == graph.edges.size() &&
        plan.entry.value < count, Error::kInvalidGraph);
    auto work = budget.work;
    const auto step = [&] {
      Require(work != 0, Error::kWorkLimit);
      --work;
    };
    constexpr auto kBytesPerBlock = sizeof(std::uint8_t) + sizeof(RegionCheck);
    Require(count <= budget.bytes / kBytesPerBlock, Error::kResourceLimit);
    Require(count <= work, Error::kWorkLimit);
    work -= count;
    std::vector<std::uint8_t> visited(count);
    std::vector<RegionCheck> tasks(count);
    Require(visited.capacity() <= count && tasks.capacity() <= count, Error::kResourceLimit);
    // The plan's stable ControlId is deliberately the block index. Checking
    // this once makes all later region/edge checks constant-time indexed reads.
    std::size_t next_edge = 0;
    for (std::size_t i = 0; i < count; ++i) {
      step();
      const auto& block = graph.blocks[i];
      Require(plan.nodes[i].block.value == i && block.first_edge.value == next_edge &&
          block.edge_count <= analysis::kMaxNormalSuccessors &&
          block.edge_count <= graph.edges.size() - next_edge, Error::kInvalidGraph);
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        step();
        const auto& edge = graph.edges[next_edge++];
        Require(edge.source.value == i && edge.successor == e && edge.target.value < count,
                Error::kInvalidGraph);
      }
    }
    Require(next_edge == graph.edges.size() && plan.entry.value == 0, Error::kInvalidGraph);
    tasks[0] = {plan.entry, {}};
    std::size_t pending = 1;
    std::size_t covered = 0;
    const auto edge_matches = [&](std::uint32_t id, ControlId owner, EdgePlacement placement) {
      const auto& edge = plan.edges[id];
      Require(edge.edge.value == id && edge.owner == owner && edge.placement == placement,
              Error::kInvalidGraph);
    };
    while (pending != 0) {
      step();
      const auto task = tasks[--pending];
      std::optional<ControlId> cursor = task.first;
      while (cursor) {
        step();
        Require(cursor->value < count && !visited[cursor->value], Error::kCrossEntry);
        const auto& node = plan.nodes[cursor->value];
        Require(!task.stop || node.block != *task.stop, Error::kCrossEntry);
        visited[cursor->value] = 1;
        ++covered;
        if (node.next) Require(node.next->value < count, Error::kInvalidGraph);
        const auto& block = graph.blocks[node.block.value];
        if (node.arms) {
          Require(block.edge_count == analysis::kMaxNormalSuccessors, Error::kInvalidGraph);
          const auto join = node.next ? std::optional{plan.nodes[node.next->value].block} : task.stop;
          for (std::uint8_t e = 0; e < block.edge_count; ++e) {
            step();
            const auto id = block.first_edge.value + e;
            const auto& edge = graph.edges[id];
            const bool taken = e == 0;
            Require(edge.kind == (taken ? analysis::FlowEdgeKind::kTaken : analysis::FlowEdgeKind::kFallthrough),
                    Error::kInvalidGraph);
            edge_matches(id, *cursor, taken ? EdgePlacement::kTaken : EdgePlacement::kFallthrough);
            const auto child = taken ? node.arms->taken : node.arms->fallthrough;
            if (child) {
              Require(child->value < count && plan.nodes[child->value].block == edge.target,
                      Error::kInvalidGraph);
              Require(pending < tasks.size(), Error::kResourceLimit);
              tasks[pending++] = {*child, join};
            } else Require(join && edge.target == *join, Error::kInvalidGraph);
          }
        } else if (block.edge_count == 1) {
          const auto id = block.first_edge.value;
          const auto& edge = graph.edges[id];
          Require(edge.kind == analysis::FlowEdgeKind::kTaken ||
                  edge.kind == analysis::FlowEdgeKind::kFallthrough, Error::kInvalidGraph);
          if (node.next) {
            Require(plan.nodes[node.next->value].block == edge.target, Error::kInvalidGraph);
            edge_matches(id, *cursor, EdgePlacement::kNext);
          } else {
            Require(task.stop && edge.target == *task.stop, Error::kInvalidGraph);
            edge_matches(id, *cursor, EdgePlacement::kJoin);
          }
        } else Require(block.edge_count == 0 && !node.next, Error::kInvalidGraph);
        cursor = node.next;
      }
    }
    Require(covered == count, Error::kUncoveredBlock);
    return budget.work - work;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
