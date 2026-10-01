#include "cell_lifetime.h"
#include "exception_tree.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = CellLifetimeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct Task { ControlId control; CellScopeId scope; };
struct Interval { std::uint32_t begin; std::uint32_t end; };
struct Visit { CellScopeId scope; bool exit; };
class Planner {
 public:
  Planner(const analysis::ValueProgram& graph, const CellPlan& cells, const ControlPlan& control,
      analysis::SemanticBudget budget, const ExceptionTreeInput* exceptions = nullptr)
      : graph_(graph), cells_(cells), control_(control), initial_(budget), left_(budget), exceptions_(exceptions) {}
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width; }
  template <class T> void Reserve(std::vector<T>& items, std::size_t n) {
    Admit(n, sizeof(T)); items.reserve(n); Admit(items.capacity() - n, sizeof(T));
  }
  template <class T> void Size(std::vector<T>& items, std::size_t n) { Reserve(items, n); Step(n); items.resize(n); }
  CellLifetimes Build(const analysis::ValueEnvironments& environments) {
    const auto verify_cells = exceptions_ ? VerifyPhasedCellPlan : VerifyCellPlan;
    const auto checked_cells = verify_cells(graph_, environments, cells_, left_);
    if (const auto* error = std::get_if<CellError>(&checked_cells)) {
      if (*error == CellError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CellError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kCells;
    }
    Step(std::get<std::uint64_t>(checked_cells));
    if (exceptions_) {
      const auto checked = VerifyExceptionControl(exceptions_->body, graph_, exceptions_->input,
          exceptions_->regions, exceptions_->control, left_);
      if (const auto* error = std::get_if<ExceptionControlError>(&checked)) {
        if (*error == ExceptionControlError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ExceptionControlError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      Step(std::get<std::uint64_t>(checked));
    } else {
      const auto checked_control = VerifyControlPlan(graph_, control_, left_);
      if (const auto* error = std::get_if<ControlError>(&checked_control)) {
        if (*error == ControlError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ControlError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      Step(std::get<std::uint64_t>(checked_control));
    }
    constexpr std::size_t kArms = 2;
    Require(control_.nodes.size() <= (std::numeric_limits<std::uint32_t>::max() - 1) / kArms, Error::kResourceLimit);
    const auto capacity = control_.nodes.size() * kArms + 1;
    Admit(1, sizeof(CellLifetimes));
    CellLifetimes result{graph_.revision, graph_.cfg_revision};
    Reserve(result.scopes, capacity); Size(result.blocks, graph_.blocks.size());
    Size(result.cells, cells_.cells.size());
    std::vector<Task> tasks; Reserve(tasks, capacity);
    std::vector<std::uint8_t> seen; Size(seen, control_.nodes.size());
    result.scopes.push_back({{}, CellBodyScope{}});
    tasks.push_back({control_.entry, {0}});
    const auto child = [&](CellScopeId parent, auto origin, ControlId control) {
      Require(result.scopes.size() < result.scopes.capacity() && tasks.size() < tasks.capacity(), Error::kResourceLimit);
      const CellScopeId id{static_cast<std::uint32_t>(result.scopes.size())};
      result.scopes.push_back({parent, origin}); tasks.push_back({control, id});
    };
    while (!tasks.empty()) {
      Step(); const auto task = tasks.back(); tasks.pop_back();
      Require(task.control.value < seen.size() && !seen[task.control.value], Error::kControl);
      seen[task.control.value] = 1;
      const auto& node = control_.nodes[task.control.value];
      if (node.next) {
        Require(tasks.size() < tasks.capacity(), Error::kResourceLimit);
        tasks.push_back({*node.next, task.scope});
      }
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        child(task.scope, CellLoopScope{loop->loop}, control_.loops[loop->loop.value].body);
      } else if (std::holds_alternative<std::monostate>(node.synthetic)) {
        if (exceptions_) {
          // Skeleton block IDs name projected units, never original work blocks.
          const auto& unit = exceptions_->control.units[node.block.value];
          if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
            const auto& roots = exceptions_->control.regions[region->region.value];
            child(task.scope, CellTryScope{region->region, CellTryPart::kBody}, roots.body);
            child(task.scope, CellTryScope{region->region, CellTryPart::kHandler}, roots.handler);
            continue;
          }
          if (std::holds_alternative<TryExitUnit>(unit)) continue;
          result.blocks[std::get<PhaseControlUnit>(unit).block.value] = task.scope;
        } else {
          result.blocks[node.block.value] = task.scope;
        }
        // A conditional expression adds no lexical container. Walk every
        // original evaluation node in its checked path, for either graph view;
        // no declarations or environment changes are authorized by this map.
        auto decision = node.decision;
        ControlId predicate = task.control;
        bool selected_taken = node.arms && node.arms->taken.has_value();
        while (decision) {
          Step(); auto at = decision->evaluate;
          const auto last = decision->predicate.value_or(at);
          while (true) {
            Step(); Require(at.value < seen.size() && !seen[at.value], Error::kControl);
            seen[at.value] = 1;
            const auto& condition = control_.nodes[at.value];
            Require(std::holds_alternative<std::monostate>(condition.synthetic), Error::kControl);
            auto block = condition.block;
            if (exceptions_) {
              Require(at.value < exceptions_->control.units.size(), Error::kControl);
              const auto* phase = std::get_if<PhaseControlUnit>(&exceptions_->control.units[at.value]);
              Require(phase, Error::kControl); block = phase->block;
            }
            result.blocks[block.value] = task.scope;
            if (at == last) break;
            Require(condition.next && !condition.arms && !condition.decision, Error::kControl);
            at = *condition.next;
          }
          predicate = last; selected_taken = decision->shared_on_taken;
          decision = control_.nodes[last.value].decision;
        }
        if (node.decision) {
          const auto& last = control_.nodes[predicate.value];
          const auto fallback = selected_taken ? last.arms->fallthrough : last.arms->taken;
          if (fallback) child(task.scope, CellArmScope{predicate, selected_taken ?
              analysis::FlowEdgeKind::kFallthrough : analysis::FlowEdgeKind::kTaken}, *fallback);
        }
        if (node.arms) {
          if (node.arms->taken) child(task.scope, CellArmScope{task.control, analysis::FlowEdgeKind::kTaken}, *node.arms->taken);
          if (node.arms->fallthrough) child(task.scope, CellArmScope{task.control, analysis::FlowEdgeKind::kFallthrough}, *node.arms->fallthrough);
        }
      }
    }
    std::vector<std::uint8_t> inactive;
    if (exceptions_) {
      Size(inactive, graph_.blocks.size());
      // Fresh control verification certifies exactly these empty disconnected
      // explicit-throw Commit nodes. No reachable declaration/access is skipped.
      for (const auto block : exceptions_->control.inactive_commits) {
        Step(); inactive[block.value] = 1;
      }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
      Step();
      if (seen[i]) continue;
      Require(exceptions_ && std::holds_alternative<std::monostate>(control_.nodes[i].synthetic), Error::kControl);
      const auto* phase = std::get_if<PhaseControlUnit>(&exceptions_->control.units[control_.nodes[i].block.value]);
      Require(phase && inactive[phase->block.value], Error::kControl);
    }
    // Euler intervals permit one constant-time enclosure check per access.
    // Never walk all parents separately for every cell or lexical load.
    std::vector<std::optional<CellScopeId>> heads, siblings;
    std::vector<Interval> intervals;
    std::vector<Visit> pending;
    Size(heads, result.scopes.size()); Size(siblings, result.scopes.size());
    Size(intervals, result.scopes.size()); Reserve(pending, result.scopes.size() * kArms);
    for (std::size_t i = 1; i < result.scopes.size(); ++i) {
      Step(); const auto parent = *result.scopes[i].parent;
      siblings[i] = heads[parent.value]; heads[parent.value] = CellScopeId{static_cast<std::uint32_t>(i)};
    }
    pending.push_back({{0}, false}); std::uint32_t clock = 0;
    while (!pending.empty()) {
      Step(); const auto visit = pending.back(); pending.pop_back();
      if (visit.exit) { intervals[visit.scope.value].end = clock; continue; }
      intervals[visit.scope.value].begin = clock++;
      pending.push_back({visit.scope, true});
      for (auto child = heads[visit.scope.value]; child; child = siblings[child->value]) {
        Step(); pending.push_back({*child, false});
      }
    }
    for (std::size_t c = 0; c < cells_.cells.size(); ++c) {
      Step(); const auto& cell = cells_.cells[c];
      const auto* allocation = std::get_if<AllocatedCell>(&cell.storage);
      if (!allocation) { result.cells[c] = IncomingLifetime{}; continue; }
      const auto scope = result.blocks[graph_.ops[allocation->allocation.value].block.value];
      result.cells[c] = scope; const auto owner = intervals[scope.value];
      for (std::uint32_t i = 0; i < cell.count; ++i) {
        Step(); const auto op = cells_.accesses[cell.first + i].operation;
        const auto use = intervals[result.blocks[graph_.ops[op.value].block.value].value].begin;
        Require(owner.begin <= use && use < owner.end, Error::kScopeEscape);
      }
    }
    result.allocated_bytes = sizeof(result) + result.scopes.capacity() * sizeof(CellScope) +
        result.blocks.capacity() * sizeof(CellScopeId) + result.cells.capacity() * sizeof(decltype(result.cells)::value_type);
    result.work = initial_.work - left_.work; return result;
  }
 private:
  const analysis::ValueProgram& graph_;
  const CellPlan& cells_;
  const ControlPlan& control_;
  analysis::SemanticBudget initial_, left_;
  const ExceptionTreeInput* exceptions_;
};
}  // namespace
std::variant<CellLifetimes, CellLifetimeError> BuildCellLifetimes(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ControlPlan& control, analysis::SemanticBudget budget) {
  try { return Planner(graph, cells, control, budget).Build(environments);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<CellLifetimes, CellLifetimeError> BuildPhasedCellLifetimes(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ExceptionTreeInput& exceptions, analysis::SemanticBudget budget) {
  try { return Planner(graph, cells, exceptions.control.skeleton, budget, &exceptions).Build(environments);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
