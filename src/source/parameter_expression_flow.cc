#include <algorithm>

#include "expression_observation.h"
#include "parameter_expression_tree.h"
#include "parameter_recovery.h"
#include "tree_checks.h"
namespace abcd::source {
namespace {
using namespace analysis;
void Need(bool ok) { RequireTree(ok, TreeStructureError::kOwnership); }
template <class T>
void Need(const std::optional<T>& v) {
  Need(v.has_value());
}
struct Check {
  const FunctionTree& tree;
  const RecoveryFlow& flow;
  TreeCheckBudget& q;
  const ParameterExpressionSource& source;
  std::vector<std::optional<std::size_t>> ports, guards;
  std::vector<std::optional<FlowPoint>> definitions;
  std::vector<FlowParameterExpressionEvent> events;
  std::vector<std::uint8_t> visited;
  std::vector<std::size_t> ranks;
  void Edges(
      FlowPoint point,
      std::initializer_list<std::pair<FlowPoint, FlowCompletion>> expected) {
    Need(point.value < flow.events.size());
    std::size_t count = 0;
    unsigned seen = 0;
    for (auto i = flow.events[point.value].outgoing; i;
         i = flow.edges[*i].outgoing) {
      q.Step();
      Need(*i < flow.edges.size());
      const auto& e = flow.edges[*i];
      unsigned n = 0;
      bool match = false;
      for (const auto& [target, kind] : expected) {
        if (e.to == target && e.completion == kind) {
          Need(!(seen & (1U << n)));
          seen |= 1U << n;
          match = true;
        }
        ++n;
      }
      Need(match);
      ++count;
    }
    Need(count == expected.size());
  }
  void Run() {
    VerifyParameterExpressionTree(tree, q);
    Need(flow.original &&
         flow.original->parameter_expressions == tree.parameter_expressions);
    const auto& c = source.Certificate();
    const auto& g = source.Input();
    q.Array(ports, c.formals);
    q.Array(guards, g.ops.size());
    q.Array(definitions, c.formals);
    q.Array(visited, flow.events.size());
    q.Array(ranks, flow.events.size());
    std::size_t rank = 0;
    for (std::size_t i = 0; i < flow.parameter_defaults.size(); ++i) {
      q.Step();
      const auto& p = flow.parameter_defaults[i];
      Need(p.parameter.value < ports.size() && !ports[p.parameter.value]);
      ports[p.parameter.value] = i;
    }
    Need(flow.parameter_defaults.size() == c.defaults.size());
    std::vector<std::optional<std::uint32_t>> ordinals;
    q.Array(ordinals, tree.symbols.size());
    for (std::uint32_t i = 0; i < tree.parameters.size(); ++i) {
      q.Step();
      ordinals[tree.parameters[i].symbol.value] = i;
    }
    for (const auto& d : flow.definitions) {
      q.Step();
      if (d.kind != FlowDefinitionKind::kParameter) continue;
      Need(d.binding.value < ordinals.size() && ordinals[d.binding.value]);
      const auto p = *ordinals[d.binding.value];
      Need(!definitions[p]);
      definitions[p] = d.point;
    }
    for (std::size_t i = 0; i < flow.parameter_guards.size(); ++i) {
      q.Step();
      const auto& guard = flow.parameter_guards[i];
      Need(guard.operation.value < guards.size() &&
           !guards[guard.operation.value]);
      guards[guard.operation.value] = i;
    }
    std::size_t count = 0;
    for (const auto& e : c.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      if (!std::holds_alternative<ValueJump>(op.operation)) ++count;
    }
    q.Array(events, count);
    std::size_t event = 0;
    for (const auto& d : c.defaults) {
      q.Step();
      Need(ports[d.parameter]);
      const auto& p = flow.parameter_defaults[*ports[d.parameter]];
      Need(p.initializer == *source.Roots()[d.parameter] &&
           p.caller_exception == flow.terminal &&
           definitions[d.parameter] == p.initialized);
      if (d.input) {
        // A direct earlier-parameter read is an original phi-input use, not a
        // fabricated value operation. Recheck its actual native read event.
        const auto* read = std::get_if<SymbolExpr>(
            &tree.expressions[p.initializer.value].expression);
        Need(read && p.initializer.value < flow.actions.size() &&
             flow.actions[p.initializer.value]);
        const auto& action = *flow.actions[p.initializer.value];
        Need(action.before.value < flow.events.size() &&
             flow.events[action.before.value].read == read->symbol &&
             action.exceptions.thrown == flow.terminal &&
             !action.exceptions.finalizer);
      }
      Edges(p.argument, {{p.decision, FlowCompletion::kNormal}});
      Edges(p.decision,
            {{p.provided, FlowCompletion::kNotTaken},
             {flow.expressions[p.initializer.value], FlowCompletion::kTaken}});
      Edges(p.provided, {{p.initialized, FlowCompletion::kNormal}});
      Edges(p.missing, {{p.initialized, FlowCompletion::kNormal}});
      if (d.parameter) {
        Need(definitions[d.parameter - 1]);
        Edges(*definitions[d.parameter - 1],
              {{p.argument, FlowCompletion::kNormal}});
      } else
        Edges(flow.entry, {{p.argument, FlowCompletion::kNormal}});
      if (d.parameter + 1 < c.formals) {
        const auto next =
            ports[d.parameter + 1]
                ? flow.parameter_defaults[*ports[d.parameter + 1]].argument
                : *definitions[d.parameter + 1];
        Edges(p.initialized, {{next, FlowCompletion::kNormal}});
      } else
        Edges(p.initialized,
              {{flow.statements[tree.body.value], FlowCompletion::kNormal}});
      // Each initializer is a disjoint normal chain with only caller exits.
      // Walk all chains once in total; no per-event dominance/CFG rescan.
      auto point = flow.expressions[p.initializer.value];
      while (point != p.missing) {
        q.Step();
        Need(point.value < flow.events.size() && !visited[point.value]);
        visited[point.value] = 1;
        ranks[point.value] = ++rank;
        const auto& e = flow.events[point.value];
        Need(e.entry_evaluation && !e.owner && !e.definition);
        std::optional<FlowPoint> next;
        for (auto at = e.outgoing; at; at = flow.edges[*at].outgoing) {
          q.Step();
          const auto& edge = flow.edges[*at];
          if (edge.completion == FlowCompletion::kNormal) {
            Need(!next);
            next = edge.to;
          } else
            Need(edge.completion == FlowCompletion::kThrow &&
                 edge.to == flow.terminal);
        }
        Need(next.has_value());
        point = *next;
      }
      ranks[p.missing.value] = ++rank;
    }
    std::vector<std::optional<FlowPoint>> read_guards;
    q.Array(read_guards, tree.expressions.size());
    for (const auto& g : flow.parameter_guards) {
      q.Step();
      Need(g.expression.value < read_guards.size() &&
           !read_guards[g.expression.value]);
      read_guards[g.expression.value] = g.before;
    }
    std::size_t previous_commit = 0;
    for (const auto& e : c.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      const auto& p = flow.parameter_defaults[*ports[e.parameter]];
      if (std::holds_alternative<ValueJump>(op.operation)) continue;
      FlowParameterExpressionEvent port{
          {e.parameter}, e.operation, ParameterEventStage::kTest,
          p.argument,    p.decision,  {}};
      if (const auto expr = source.Operations()[e.operation.value]) {
        Need(expr->value < flow.actions.size() && flow.actions[expr->value]);
        const auto& action = *flow.actions[expr->value];
        Need(action.exceptions.thrown == flow.terminal &&
             !action.exceptions.finalizer);
        port.stage = ParameterEventStage::kInitializer;
        port.before = action.before;
        port.commit = action.normal;
        if (std::holds_alternative<ValueGuard>(op.operation)) {
          Need(guards[e.operation.value]);
          const auto& guard = flow.parameter_guards[*guards[e.operation.value]];
          Need(guard.expression == *expr && guard.commit == action.normal);
          port.stage = ParameterEventStage::kGuard;
          port.before = guard.before;
          port.commit = guard.commit;
        } else if (read_guards[expr->value])
          port.commit = *read_guards[expr->value];
        Need(port.before.value < visited.size() && visited[port.before.value]);
        if (const auto* read = std::get_if<SymbolExpr>(
                &tree.expressions[expr->value].expression)) {
          Need(action.before.value < flow.events.size() &&
               flow.events[action.before.value].read == read->symbol);
        }
        Need(port.commit.value < ranks.size() && ranks[port.before.value] &&
             ranks[port.before.value] >= previous_commit &&
             ranks[port.commit.value] >= ranks[port.before.value]);
        previous_commit = ranks[port.commit.value];
        if (op.exceptional != ExceptionalCompletion::kImpossible) {
          bool caller = false;
          for (auto at = flow.events[port.before.value].outgoing; at;
               at = flow.edges[*at].outgoing) {
            q.Step();
            const auto& edge = flow.edges[*at];
            if (edge.completion == FlowCompletion::kThrow) {
              Need(edge.to == flow.terminal);
              caller = true;
            }
          }
          if (caller) {
            port.exception = flow.terminal;
            port.exception_kind = ParameterExceptionPort::kCaller;
          } else {
            // The source value-domain analysis may discharge a conservative
            // VM conversion exit (e.g. NEG of an integer literal). It cannot
            // discharge a getter/global/guard, whose own exit must be present.
            Need(std::holds_alternative<ValueUnary>(op.operation) &&
                 NormalValueEvaluation(
                     tree.expressions[expr->value].expression,
                     flow.normal_kinds,
                     ResourceFailures(tree.identity.contract)) ==
                     ExpressionEvaluation::kInert);
            port.exception_kind = ParameterExceptionPort::kDomainImpossible;
          }
        }
      } else {
        const auto& d =
            *std::lower_bound(c.defaults.begin(), c.defaults.end(), e.parameter,
                              [&](const auto& a, auto p) {
                                q.Step();
                                return a.parameter < p;
                              });
        Need(e.operation == d.comparison || e.operation == d.branch ||
             e.operation == d.undefined);
      }
      events[event++] = port;
    }
    Need(event == events.size());
  }
};
}  // namespace
void BuildParameterExpressionPorts(const FunctionTree& tree, RecoveryFlow& flow,
                                   TreeCheckBudget& q) {
  if (!tree.parameter_expressions) return;
  Check check{tree, flow, q, *tree.parameter_expressions};
  check.Run();
  flow.parameter_expression_events = std::move(check.events);
}
void VerifyParameterExpressionFlow(const FunctionTree& tree,
                                   const RecoveryFlow& flow,
                                   TreeCheckBudget& q) {
  Need(tree.parameter_expressions != nullptr);
  Check check{tree, flow, q, *tree.parameter_expressions};
  check.Run();
  Need(check.events == flow.parameter_expression_events);
}
}  // namespace abcd::source
