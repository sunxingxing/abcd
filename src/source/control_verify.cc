#include "control_skeleton.h"

#include <array>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct RegionCheck {
  ControlId first;
  std::optional<ControlId> stop;
  std::optional<LoopId> scope;
};
using Targets = std::array<std::optional<ControlId>, analysis::kMaxNormalSuccessors>;

class ControlCheck {
 public:
  ControlCheck(const analysis::ValueProgram& graph, const ControlPlan& plan, ControlQuota& quota)
      : graph_(graph), plan_(plan), quota_(quota) {}
  void Run() {
    Index();
    Require(!std::holds_alternative<LoopTransfer>(plan_.nodes[plan_.entry.value].synthetic) &&
        ConcreteEntry(plan_.entry).value == 0, Error::kInvalidGraph);
    Push({plan_.entry, {}, {}});
    std::size_t covered = 0;
    while (pending_ != 0) {
      quota_.Work();
      const auto task = tasks_[--pending_];
      std::optional<ControlId> cursor = task.first;
      while (cursor) {
        quota_.Work();
        const auto id = *cursor;
        Require(id.value < plan_.nodes.size() && !visited_[id.value] && task.stop != id, Error::kCrossEntry);
        visited_[id.value] = 1; scopes_[id.value] = task.scope; ++covered;
        const auto& node = plan_.nodes[id.value];
        if (node.next) Require(node.next->value < plan_.nodes.size(), Error::kInvalidGraph);
        const auto degree = Degree(id);
        if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic)) {
          const auto& loop = plan_.loops[wrapper->loop.value];
          Require(!entered_[wrapper->loop.value] && loop.parent == task.scope, Error::kCrossEntry);
          entered_[wrapper->loop.value] = 1;
          Push({loop.body, {}, wrapper->loop});
        } else if (const auto* transfer = std::get_if<LoopTransfer>(&node.synthetic)) {
          Require(task.scope == transfer->loop, Error::kCrossEntry);
        }
        if (degree == analysis::kMaxNormalSuccessors) {
          Require(node.arms.has_value(), Error::kInvalidGraph);
          const auto join = node.next ? node.next : task.stop;
          for (std::uint8_t e = 0; e < degree; ++e) {
            quota_.Work();
            const auto child = e == 0 ? node.arms->taken : node.arms->fallthrough;
            if (child) {
              Require(child->value < plan_.nodes.size() && child != join, Error::kCrossEntry);
              Push({*child, join, task.scope});
            } else Require(join.has_value(), Error::kInvalidGraph);
            Arc(id, e, child ? *child : *join, task.scope,
                e == 0 ? EdgePlacement::kTaken : EdgePlacement::kFallthrough);
          }
        } else if (degree == 1) {
          Require(!node.arms && (node.next || task.stop), Error::kInvalidGraph);
          Arc(id, 0, node.next ? *node.next : *task.stop, task.scope,
              node.next ? EdgePlacement::kNext : EdgePlacement::kJoin);
        } else Require(!node.arms && !node.next, Error::kInvalidGraph);
        cursor = node.next;
      }
    }
    Require(covered == plan_.nodes.size(), Error::kUncoveredBlock);
    for (std::size_t i = 0; i < targets_.size(); ++i) {
      quota_.Work();
      for (const auto target : targets_[i]) {
        quota_.Work();
        if (target) Require(scopes_[i] == scopes_[target->value], Error::kCrossEntry);
      }
    }
    for (std::size_t i = 0; i < plan_.loops.size(); ++i) {
      quota_.Work();
      Require(entered_[i] && continued_[i] && (exited_[i] != 0) == plan_.loops[i].exit.has_value(), Error::kInvalidGraph);
    }
  }
 private:
  void Index() {
    Require(plan_.revision == graph_.revision && plan_.cfg_revision == graph_.cfg_revision, Error::kRevision);
    const auto blocks = graph_.blocks.size();
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(blocks != 0 && blocks < kMaxId && graph_.edges.size() < kMaxId &&
        graph_.edges.size() <= std::numeric_limits<std::size_t>::max() - plan_.loops.size() &&
        plan_.nodes.size() < kMaxId &&
        plan_.nodes.size() >= blocks && plan_.loops.size() <= blocks &&
        plan_.loops.size() <= plan_.nodes.size() - blocks &&
        plan_.nodes.size() - blocks - plan_.loops.size() <= graph_.edges.size() + plan_.loops.size() &&
        plan_.edges.size() == graph_.edges.size() && plan_.entry.value < plan_.nodes.size(), Error::kInvalidGraph);
    quota_.Size(visited_, plan_.nodes.size());
    quota_.Size(scopes_, plan_.nodes.size());
    quota_.Size(targets_, plan_.nodes.size());
    quota_.Size(tasks_, plan_.nodes.size());
    quota_.Size(entered_, plan_.loops.size());
    quota_.Size(continued_, plan_.loops.size());
    quota_.Size(exited_, plan_.loops.size());
    quota_.Size(headers_, blocks);
    std::size_t next_edge = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
      quota_.Work();
      const auto& block = graph_.blocks[b];
      Require(plan_.nodes[b].block.value == b && std::holds_alternative<std::monostate>(plan_.nodes[b].synthetic) &&
          block.first_edge.value == next_edge && block.edge_count <= analysis::kMaxNormalSuccessors &&
          block.edge_count <= graph_.edges.size() - next_edge, Error::kInvalidGraph);
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota_.Work();
        const auto& edge = graph_.edges[next_edge++];
        Require(edge.source.value == b && edge.successor == e && edge.target.value < blocks &&
            (edge.kind == analysis::FlowEdgeKind::kTaken || edge.kind == analysis::FlowEdgeKind::kFallthrough), Error::kInvalidGraph);
        if (block.edge_count == analysis::kMaxNormalSuccessors)
          Require(edge.kind == (e == 0 ? analysis::FlowEdgeKind::kTaken : analysis::FlowEdgeKind::kFallthrough), Error::kInvalidGraph);
      }
    }
    Require(next_edge == graph_.edges.size(), Error::kInvalidGraph);
    for (std::size_t i = 0; i < plan_.loops.size(); ++i) {
      quota_.Work();
      const auto& loop = plan_.loops[i];
      Require(loop.header.value < blocks && !headers_[loop.header.value] &&
          loop.body.value == loop.header.value && loop.wrapper.value == blocks + i &&
          (!loop.parent || (loop.parent->value < plan_.loops.size() && loop.parent->value != i)) &&
          (!loop.exit || (loop.exit->value < blocks && *loop.exit != loop.header)), Error::kInvalidGraph);
      headers_[loop.header.value] = LoopId{static_cast<std::uint32_t>(i)};
    }
    for (std::size_t i = blocks; i < plan_.nodes.size(); ++i) {
      quota_.Work();
      const auto& node = plan_.nodes[i];
      Require((i < blocks + plan_.loops.size()) ==
          std::holds_alternative<LoopControl>(node.synthetic), Error::kInvalidGraph);
      if (const auto* wrapper = std::get_if<LoopControl>(&node.synthetic)) {
        Require(wrapper->loop.value < plan_.loops.size(), Error::kInvalidGraph);
        const auto& loop = plan_.loops[wrapper->loop.value];
        Require(loop.wrapper.value == i && node.block == loop.header, Error::kInvalidGraph);
      } else {
        const auto* transfer = std::get_if<LoopTransfer>(&node.synthetic);
        Require(transfer && transfer->loop.value < plan_.loops.size(), Error::kInvalidGraph);
        if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer->source))
          Require(edge->value < graph_.edges.size() && node.block == graph_.edges[edge->value].source, Error::kInvalidGraph);
        else {
          const auto loop = std::get<LoopId>(transfer->source);
          Require(loop.value < plan_.loops.size() && node.block == plan_.loops[loop.value].header, Error::kInvalidGraph);
        }
      }
    }
  }
  void Push(RegionCheck task) {
    Require(pending_ < tasks_.size(), Error::kResourceLimit); tasks_[pending_++] = task;
  }
  std::uint8_t Degree(ControlId id) const {
    if (id.value < graph_.blocks.size()) return graph_.blocks[id.value].edge_count;
    if (const auto* loop = std::get_if<LoopControl>(&plan_.nodes[id.value].synthetic))
      return plan_.loops[loop->loop.value].exit ? 1 : 0;
    return 0;
  }
  analysis::WorkBlockId ConcreteEntry(ControlId id) const {
    const auto& node = plan_.nodes[id.value];
    if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) return plan_.loops[loop->loop.value].header;
    Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kInvalidGraph);
    return node.block;
  }
  analysis::WorkBlockId Destination(ControlId id, std::optional<LoopId> scope,
      const ControlTransferSource& source) {
    Require(id.value < plan_.nodes.size(), Error::kInvalidGraph);
    const auto* transfer = std::get_if<LoopTransfer>(&plan_.nodes[id.value].synthetic);
    if (!transfer) return ConcreteEntry(id);
    Require(scope == transfer->loop && transfer->source == source, Error::kCrossEntry);
    const auto& loop = plan_.loops[transfer->loop.value];
    switch (transfer->kind) {
      case LoopTransferKind::kContinue:
        continued_[transfer->loop.value] = 1; return loop.header;
      case LoopTransferKind::kBreak:
        Require(loop.exit.has_value(), Error::kInvalidGraph);
        exited_[transfer->loop.value] = 1; return *loop.exit;
    }
    throw Error::kInvalidGraph;
  }
  void Arc(ControlId source, std::uint8_t ordinal, ControlId target,
      std::optional<LoopId> scope, EdgePlacement placement) {
    Require(target.value < plan_.nodes.size(), Error::kInvalidGraph);
    targets_[source.value][ordinal] = target;
    if (source.value < graph_.blocks.size()) {
      const analysis::EdgeId id{graph_.blocks[source.value].first_edge.value + ordinal};
      const auto& edge = graph_.edges[id.value];
      const auto& planned = plan_.edges[id.value];
      Require(planned.edge == id && planned.owner == source && planned.placement == placement &&
          Destination(target, scope, id) == edge.target, Error::kInvalidGraph);
    } else {
      const auto* wrapper = std::get_if<LoopControl>(&plan_.nodes[source.value].synthetic);
      Require(wrapper && plan_.loops[wrapper->loop.value].exit, Error::kInvalidGraph);
      Require(Destination(target, scope, wrapper->loop) == *plan_.loops[wrapper->loop.value].exit, Error::kInvalidGraph);
    }
  }
  const analysis::ValueProgram& graph_;
  const ControlPlan& plan_;
  ControlQuota& quota_;
  std::vector<std::uint8_t> visited_, entered_, continued_, exited_;
  std::vector<std::optional<LoopId>> scopes_, headers_;
  std::vector<Targets> targets_;
  std::vector<RegionCheck> tasks_;
  std::size_t pending_ = 0;
};
}  // namespace

std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan, analysis::SemanticBudget budget) {
  try {
    Require(graph.phases.empty(), Error::kInvalidGraph);
    ControlQuota quota(budget);
    quota.Admit(1, sizeof(ControlCheck));
    ControlCheck(graph, plan, quota).Run();
    return budget.work - quota.RemainingWork();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
