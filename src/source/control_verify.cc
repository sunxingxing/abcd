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
struct LoopRange { std::size_t begin = 0; std::size_t end = 0; };
struct LoopVisit { LoopId loop; bool leave; };
using Targets = std::array<std::optional<ControlId>, analysis::kMaxNormalSuccessors>;

class ControlCheck {
 public:
  ControlCheck(ControlGraphView graph, std::span<const ControlNodeId> entries,
      const ControlPlan& plan, ControlQuota& quota)
      : graph_(graph), entries_(entries), plan_(plan), quota_(quota) {}
  void Run() {
    Index();
    Require(!entries_.empty(), Error::kInvalidGraph);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      const auto entry = entries_[i];
      Require(entry.value < graph_.NodeCount(), Error::kInvalidGraph);
      const auto loop = headers_[entry.value];
      const auto root = loop ? plan_.loops[loop->value].wrapper : ControlId{entry.value};
      Require(i != 0 || plan_.entry == root, Error::kInvalidGraph);
      Push({root, {}, {}});
    }
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
          Require(Encloses(transfer->loop, task.scope), Error::kCrossEntry);
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
    Require(plan_.revision == graph_.Revision() && plan_.cfg_revision == graph_.CfgRevision(), Error::kRevision);
    Require(plan_.view == graph_.Kind(), Error::kInvalidGraph);
    const auto blocks = graph_.NodeCount();
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(blocks != 0 && blocks < kMaxId && graph_.ArcCount() < kMaxId &&
        graph_.ArcCount() <= std::numeric_limits<std::size_t>::max() - plan_.loops.size() &&
        plan_.nodes.size() < kMaxId &&
        plan_.nodes.size() >= blocks && plan_.loops.size() <= blocks &&
        plan_.loops.size() <= plan_.nodes.size() - blocks &&
        plan_.nodes.size() - blocks - plan_.loops.size() <= graph_.ArcCount() + plan_.loops.size() &&
        plan_.edges.size() == graph_.ArcCount() && plan_.entry.value < plan_.nodes.size(), Error::kInvalidGraph);
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
      const auto& block = graph_.Node({static_cast<std::uint32_t>(b)});
      Require(plan_.nodes[b].block.value == b && std::holds_alternative<std::monostate>(plan_.nodes[b].synthetic) &&
          block.first_arc.value == next_edge && block.arc_count <= analysis::kMaxNormalSuccessors &&
          block.arc_count <= graph_.ArcCount() - next_edge, Error::kInvalidGraph);
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto& edge = graph_.Arc({static_cast<std::uint32_t>(next_edge++)});
        Require(edge.source.value == b && edge.successor == e && edge.target.value < blocks &&
            (edge.kind == analysis::FlowEdgeKind::kTaken || edge.kind == analysis::FlowEdgeKind::kFallthrough), Error::kInvalidGraph);
        if (block.arc_count == analysis::kMaxNormalSuccessors)
          Require(edge.kind == (e == 0 ? analysis::FlowEdgeKind::kTaken : analysis::FlowEdgeKind::kFallthrough), Error::kInvalidGraph);
      }
    }
    Require(next_edge == graph_.ArcCount(), Error::kInvalidGraph);
    for (std::size_t i = 0; i < plan_.loops.size(); ++i) {
      quota_.Work();
      const auto& loop = plan_.loops[i];
      Require(loop.header.value < blocks && !headers_[loop.header.value] &&
          loop.body.value == loop.header.value && loop.wrapper.value == blocks + i &&
          (!loop.parent || (loop.parent->value < plan_.loops.size() && loop.parent->value > i)) &&
          (!loop.exit || (loop.exit->value < blocks && *loop.exit != loop.header)), Error::kInvalidGraph);
      headers_[loop.header.value] = LoopId{static_cast<std::uint32_t>(i)};
    }
    IndexLoopParents();
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
          Require(edge->value < graph_.ArcCount() && node.block.value == graph_.Arc({edge->value}).source.value, Error::kInvalidGraph);
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
  void IndexLoopParents() {
    const auto count = plan_.loops.size();
    constexpr std::size_t kVisitEvents = 2; // Enter and leave each loop once.
    Require(count <= std::numeric_limits<std::size_t>::max() / kVisitEvents, Error::kResourceLimit);
    std::vector<std::optional<LoopId>> heads, siblings;
    std::vector<LoopVisit> pending;
    quota_.Size(heads, count + 1); quota_.Size(siblings, count);
    quota_.Size(loop_ranges_, count); quota_.Size(pending, count * kVisitEvents);
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work(); const auto parent = plan_.loops[i].parent;
      const auto at = parent ? parent->value : count;
      siblings[i] = heads[at]; heads[at] = LoopId{static_cast<std::uint32_t>(i)};
    }
    std::size_t size = 0, clock = 0;
    for (auto root = heads[count]; root; root = siblings[root->value]) pending[size++] = {*root, false};
    while (size) {
      quota_.Work(); const auto visit = pending[--size];
      auto& range = loop_ranges_[visit.loop.value];
      if (visit.leave) { range.end = clock; continue; }
      range.begin = clock++; pending[size++] = {visit.loop, true};
      for (auto child = heads[visit.loop.value]; child; child = siblings[child->value]) {
        quota_.Work(); Require(size < pending.size(), Error::kResourceLimit);
        pending[size++] = {*child, false};
      }
    }
  }
  bool Encloses(LoopId target, std::optional<LoopId> scope) const {
    if (!scope) return false;
    const auto range = loop_ranges_[target.value];
    const auto point = loop_ranges_[scope->value].begin;
    return range.begin <= point && point < range.end;
  }
  std::uint8_t Degree(ControlId id) const {
    if (id.value < graph_.NodeCount()) return graph_.Node({id.value}).arc_count;
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
    Require(Encloses(transfer->loop, scope) && transfer->source == source, Error::kCrossEntry);
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
    if (source.value < graph_.NodeCount()) {
      const analysis::EdgeId id{graph_.Node({source.value}).first_arc.value + ordinal};
      const auto& edge = graph_.Arc({id.value});
      const auto& planned = plan_.edges[id.value];
      Require(planned.edge == id && planned.owner == source && planned.placement == placement &&
          Destination(target, scope, id).value == edge.target.value, Error::kInvalidGraph);
    } else {
      const auto* wrapper = std::get_if<LoopControl>(&plan_.nodes[source.value].synthetic);
      Require(wrapper && plan_.loops[wrapper->loop.value].exit, Error::kInvalidGraph);
      Require(Destination(target, scope, wrapper->loop) == *plan_.loops[wrapper->loop.value].exit, Error::kInvalidGraph);
    }
  }
  ControlGraphView graph_;
  std::span<const ControlNodeId> entries_;
  const ControlPlan& plan_;
  ControlQuota& quota_;
  std::vector<std::uint8_t> visited_, entered_, continued_, exited_;
  std::vector<std::optional<LoopId>> scopes_, headers_;
  std::vector<Targets> targets_;
  std::vector<RegionCheck> tasks_;
  std::vector<LoopRange> loop_ranges_;
  std::size_t pending_ = 0;
};
}  // namespace

void VerifyControlRegions(ControlGraphView graph, std::span<const ControlNodeId> entries,
    const ControlPlan& plan, ControlQuota& quota) {
  quota.Admit(1, sizeof(ControlCheck));
  ControlCheck(graph, entries, plan, quota).Run();
}

std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan, analysis::SemanticBudget budget) {
  try {
    Require(graph.phases.empty() && plan.view == ControlViewKind::kNormal, Error::kInvalidGraph);
    ControlQuota quota(budget);
    constexpr ControlNodeId root{0};
    VerifyControlRegions(ControlGraphView{graph}, {&root, 1}, plan, quota);
    return budget.work - quota.RemainingWork();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
