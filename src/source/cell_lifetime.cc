#include "cell_lifetime.h"
#include "exception_tree.h"
#include "for_of_emission.h"
#include "switch_region.h"

#include <limits>
#include <bit>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = CellLifetimeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct Task { ControlId control; CellScopeId scope; std::optional<ControlId> stop; };
struct Interval { std::uint32_t begin; std::uint32_t end; };
struct Visit { CellScopeId scope; bool exit; };
class Planner {
 public:
  Planner(const analysis::ValueProgram& graph, const CellPlan& cells, const ControlPlan& control,
      analysis::SemanticBudget budget, const ExceptionTreeInput* exceptions = nullptr,
      const ForOfEvaluationPlan* iteration = nullptr, const ForOfEmissionView* view = nullptr)
      : graph_(graph), cells_(cells), control_(control), initial_(budget), left_(budget),
        exceptions_(exceptions), iteration_(iteration), view_(view) {}
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width; }
  template <class T> void Reserve(std::vector<T>& items, std::size_t n) {
    Admit(n, sizeof(T)); items.reserve(n); Admit(items.capacity() - n, sizeof(T));
  }
  template <class T> void Size(std::vector<T>& items, std::size_t n) { Reserve(items, n); Step(n); items.resize(n); }
  CellLifetimes Build(const analysis::ValueEnvironments& environments,
      std::span<const std::optional<analysis::OpId>> deferred = {}) {
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
          exceptions_->regions, exceptions_->control, left_,
          exceptions_->for_of ? &exceptions_->for_of->ownership : nullptr);
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
    std::vector<std::optional<IteratorProtocolId>> decisions;
    if (exceptions_ && exceptions_->for_of) {
      Require(iteration_ && view_, Error::kControl);
      // Recompute identity, ownership, ports and the emission projection from
      // immutable original inputs. The builder's consumed flags are no proof.
      const auto checked = VerifyForOfEmissionView(graph_, *exceptions_->for_of,
          *iteration_, *view_, left_);
      if (const auto* error = std::get_if<ForOfIterationFailure>(&checked)) {
        if (error->kind == ForOfIterationError::kResourceLimit) throw Error::kResourceLimit;
        if (error->kind == ForOfIterationError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      Step(std::get<std::uint64_t>(checked));
      Size(decisions, control_.nodes.size());
      for (std::uint32_t i = 0; i < view_->loops.size(); ++i) {
        Step(); const auto at = view_->loops[i].decision.value;
        Require(at < decisions.size() && !decisions[at], Error::kControl);
        decisions[at] = view_->loops[i].protocol;
      }
    } else Require(!iteration_ && !view_, Error::kControl);
    constexpr std::size_t kArms = 2;
    Require(control_.nodes.size() <= (std::numeric_limits<std::uint32_t>::max() - 1) / kArms, Error::kResourceLimit);
    const auto capacity = control_.nodes.size() * kArms + 1;
    Admit(1, sizeof(CellLifetimes));
    CellLifetimes result{graph_.revision, graph_.cfg_revision};
    Reserve(result.scopes, capacity); Size(result.blocks, graph_.blocks.size());
    Size(result.cells, cells_.cells.size());
    std::vector<Task> tasks; Reserve(tasks, capacity);
    std::vector<std::uint8_t> seen; Size(seen, control_.nodes.size());
    std::vector<std::optional<std::uint32_t>> switch_entries,switch_tests;
    if(control_.switches) {
      Size(switch_entries,graph_.blocks.size());Size(switch_tests,graph_.blocks.size());
      for(std::uint32_t r=0;r<control_.switches->regions.size();++r){Step();switch_entries[control_.switches->regions[r].entry.value]=r;}
      for(std::uint32_t c=0;c<control_.switches->clauses.size();++c)if(const auto& t=control_.switches->clauses[c].test){Step();switch_tests[t->block.value]=c;}
      for(std::uint32_t p=0;p<control_.switches->ports.size();++p){Step();const auto kind=control_.switches->ports[p].kind;
        if(kind==SwitchPortKind::kMatch||kind==SwitchPortKind::kMiss)seen[graph_.blocks.size()+p]=1;}
    }
    result.scopes.push_back({{}, CellBodyScope{}});
    tasks.push_back({control_.entry, {0}});
    const auto child = [&](CellScopeId parent, auto origin, ControlId control) {
      Require(result.scopes.size() < result.scopes.capacity() && tasks.size() < tasks.capacity(), Error::kResourceLimit);
      const CellScopeId id{static_cast<std::uint32_t>(result.scopes.size())};
      result.scopes.push_back({parent, origin}); tasks.push_back({control, id});
    };
    while (!tasks.empty()) {
      Step(); const auto task = tasks.back(); tasks.pop_back();
      if (task.stop == task.control) continue;
      Require(task.control.value < seen.size() && !seen[task.control.value], Error::kControl);
      seen[task.control.value] = 1;
      const auto& node = control_.nodes[task.control.value];
      if(control_.switches && task.control.value<switch_entries.size() && switch_entries[task.control.value]) {
        const auto& proof=*control_.switches;const auto r=*switch_entries[task.control.value];const auto& region=proof.regions[r];
        const CellScopeId scope{static_cast<std::uint32_t>(result.scopes.size())};
        Require(result.scopes.size()<result.scopes.capacity(),Error::kResourceLimit);
        result.scopes.push_back({task.scope,CellSwitchScope{region.entry}});
        if(region.follow){Require(tasks.size()<tasks.capacity(),Error::kResourceLimit);tasks.push_back({ControlId{region.follow->value},task.scope,task.stop});}
        auto at=region.entry;
        while(true){Step();if(at!=region.entry){Require(!seen[at.value],Error::kControl);seen[at.value]=1;}
          result.blocks[at.value]=at==region.entry?task.scope:scope;
          const auto t=switch_tests[at.value];const auto edge=t?proof.clauses[*t].test->miss:graph_.blocks[at.value].first_edge;
          const auto next=graph_.edges[edge.value].target;
          if(!proof.blocks[next.value]||proof.blocks[next.value]->region!=r||proof.blocks[next.value]->clause)break;at=next;
        }
        for(std::uint32_t c=region.first_clause;c<region.first_clause+region.clause_count;++c){Step();
          if(const auto body=proof.clauses[c].body)child(scope,CellSwitchCaseScope{region.entry,c},ControlId{body->value});
        }
        continue;
      }
      if(std::holds_alternative<SwitchPort>(node.synthetic))continue;
      if (node.next) {
        Require(tasks.size() < tasks.capacity(), Error::kResourceLimit);
        tasks.push_back({*node.next, task.scope, task.stop});
      }
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        child(task.scope, CellLoopScope{loop->loop}, control_.loops[loop->loop.value].body);
      } else if (std::holds_alternative<std::monostate>(node.synthetic)) {
        if (exceptions_) {
          // Skeleton block IDs name projected units, never original work blocks.
          const auto& unit = exceptions_->control.units[node.block.value];
          if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
            const auto& roots = *exceptions_->control.regions[region->region.value];
            child(task.scope, CellTryScope{region->region, CellTryPart::kBody}, roots.body);
            child(task.scope, CellTryScope{region->region, CellTryPart::kHandler}, roots.handler);
            continue;
          }
          if (std::holds_alternative<TryExitUnit>(unit)) continue;
          if (const auto* close = std::get_if<ProtocolCloseUnit>(&unit)) {
            CloseUnit({node.block.value}, *close);
            continue;
          }
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
          if (decision->prefix == DecisionPrefix::kRegion) {
            Require(at != last && tasks.size() < tasks.capacity(), Error::kControl);
            tasks.push_back({at, task.scope, last}); at = last;
          }
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
        if (!decisions.empty() && decisions[task.control.value]) {
          Require(node.arms && !node.decision, Error::kControl);
          // The not-done edge initializes the native binding inside the same
          // original LoopId scope. Exhaustion adds no lexical arm container.
          // Walk both proven edges (including their transfers), never remap
          // unseen blocks to the function scope or erase body control identity.
          for (const auto arm : {node.arms->taken, node.arms->fallthrough}) if (arm) {
            Require(tasks.size() < tasks.capacity(), Error::kResourceLimit);
            tasks.push_back({*arm, task.scope, task.stop});
          }
          continue;
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
      // Fresh control verification certifies disconnected provenance-only
      // phases. No reachable declaration/access is skipped.
      for (const auto block : exceptions_->control.inactive_phases) {
        Step(); inactive[block.value] = 1;
      }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
      Step();
      if (seen[i]) continue;
      Require(exceptions_ && std::holds_alternative<std::monostate>(control_.nodes[i].synthetic), Error::kControl);
      const auto at = control_.nodes[i].block.value;
      const auto& unit = exceptions_->control.units[at];
      if (const auto* close = std::get_if<ProtocolCloseUnit>(&unit)) {
        // Exception-only close entries are verified native completion ports,
        // not source declarations or lexical containers. Leave them unmapped.
        CloseUnit({at}, *close); continue;
      }
      const auto* phase = std::get_if<PhaseControlUnit>(&unit);
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
    for (const auto& access : cells_.accesses) {
      Step(); Require(result.blocks[graph_.ops[access.operation.value].block.value].has_value(),
                      Error::kScopeEscape);
    }
    std::vector<std::uint32_t> loop_depth;
    if (!deferred.empty()) {
      Size(loop_depth, result.scopes.size());
      for (std::size_t i = 0; i < result.scopes.size(); ++i) {
        Step(); const auto& scope = result.scopes[i];
        loop_depth[i] = (scope.parent ? loop_depth[scope.parent->value] : 0) +
            std::holds_alternative<CellLoopScope>(scope.origin);
      }
    }
    for (std::size_t c = 0; c < cells_.cells.size(); ++c) {
      Step(); const auto& cell = cells_.cells[c];
      const auto* allocation = std::get_if<AllocatedCell>(&cell.storage);
      if (!allocation) { result.cells[c] = IncomingLifetime{}; continue; }
      const auto scope = result.blocks[graph_.ops[allocation->allocation.value].block.value];
      Require(scope.has_value(), Error::kScopeEscape);
      auto declared = *scope;
      if (!deferred.empty()) {
        Require(deferred.size() == cells_.cells.size(), Error::kCells);
        if (const auto store = deferred[c]) {
          Require(store->value < graph_.ops.size() && cells_.operations[store->value] == CellId{static_cast<std::uint32_t>(c)}, Error::kCells);
          const auto at = result.blocks[graph_.ops[store->value].block.value];
          Require(at.has_value(), Error::kScopeEscape);
          const auto interval = intervals[at->value];
          Require(intervals[scope->value].begin <= interval.begin && interval.end <= intervals[scope->value].end, Error::kScopeEscape);
          // A smaller source scope may start later only once per original
          // environment activation. Crossing a loop would create fresh cells
          // for an old allocation and is never admitted by containment alone.
          Require(loop_depth[at->value] == loop_depth[scope->value], Error::kScopeEscape);
          declared = *at;
        }
      }
      result.cells[c] = declared; const auto owner = intervals[declared.value];
      for (std::uint32_t i = 0; i < cell.count; ++i) {
        Step(); const auto op = cells_.accesses[cell.first + i].operation;
        const auto scope = result.blocks[graph_.ops[op.value].block.value];
        Require(scope.has_value(), Error::kScopeEscape);
        const auto use = intervals[scope->value].begin;
        Require(owner.begin <= use && use < owner.end, Error::kScopeEscape);
      }
    }
    if (!deferred.empty()) for (const auto& demand : cells_.demands) {
      Step(2 * std::numeric_limits<std::uint32_t>::digits + std::bit_width(cells_.cells.size()) + 1);
      const auto cell = FindDemandCell(graph_, environments, cells_, demand);
      Require(cell.has_value(), Error::kCells);
      if (!deferred[cell->value]) continue;
      const auto& declared = std::get<CellScopeId>(result.cells[cell->value]);
      const auto at = result.blocks[graph_.ops[demand.creation.value].block.value];
      Require(at.has_value(), Error::kScopeEscape);
      const auto owner = intervals[declared.value], use = intervals[at->value];
      Require(owner.begin <= use.begin && use.end <= owner.end, Error::kScopeEscape);
    }
    result.allocated_bytes = sizeof(result) + result.scopes.capacity() * sizeof(CellScope) +
        result.blocks.capacity() * sizeof(decltype(result.blocks)::value_type) + result.cells.capacity() * sizeof(decltype(result.cells)::value_type);
    result.work = initial_.work - left_.work; return result;
  }
 private:
  void CloseUnit(ControlNodeId unit, const ProtocolCloseUnit& close) {
    Require(view_ && unit.value < view_->closes.size() &&
        view_->closes[unit.value], Error::kControl);
    const auto& entry = *view_->closes[unit.value];
    Require(entry.protocol == close.protocol && entry.entry == close.entry &&
        entry.pending == close.pending && entry.port_count != 0 &&
        entry.first_port <= view_->ports.size() &&
        entry.port_count <= view_->ports.size() - entry.first_port, Error::kControl);
    // The independently verified ports preserve all outcomes and payloads.
    // Only the ordinary continuation has a source scope; never assign one to
    // the implicit close subgraph or turn it into a transparent rethrow.
    for (std::uint32_t p = 0; p < entry.port_count; ++p) {
      Step(); Require(view_->ports[entry.first_port + p].unit == unit, Error::kControl);
    }
  }
  const analysis::ValueProgram& graph_;
  const CellPlan& cells_;
  const ControlPlan& control_;
  analysis::SemanticBudget initial_, left_;
  const ExceptionTreeInput* exceptions_;
  const ForOfEvaluationPlan* iteration_;
  const ForOfEmissionView* view_;
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
    const CellPlan& cells, const ExceptionTreeInput& exceptions, analysis::SemanticBudget budget,
    const ForOfEvaluationPlan* iteration, const ForOfEmissionView* view,
    std::span<const std::optional<analysis::OpId>> deferred) {
  try { return Planner(graph, cells, exceptions.control.skeleton, budget, &exceptions,
                       iteration, view).Build(environments, deferred);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
