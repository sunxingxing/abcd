#include "original_flow.h"
#include "phased_parameter_defaults.h"
#include "recovery_flow.h"

namespace abcd::source {
namespace {
void Need(bool b) { RequireTree(b, TreeStructureError::kOwnership); }
TreeLiteral Literal(const PhasedDefaultLiteral& v,
                    const ExpressionConstants* constants,
                    const PhasedParameterDefault& d) {
  return std::visit(
      [&](const auto& x) -> TreeLiteral {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, analysis::IntegerLiteral>)
          return IntegerLiteral{x.value};
        else if constexpr (std::is_same_v<T, analysis::Float64Bits>)
          return NumberLiteral{x.value};
        else if constexpr (std::is_same_v<T, abc::PoolIndex>) {
          Need(constants && d.initializer.value < constants->operations.size());
          const auto* text =
              std::get_if<TextId>(&constants->operations[d.initializer.value]);
          Need(text);
          return *text;
        } else {
          switch (x) {
            case analysis::SpecialValue::kUndefined:
              return UndefinedLiteral{};
            case analysis::SpecialValue::kNull:
              return NullLiteral{};
            case analysis::SpecialValue::kTrue:
              return BooleanLiteral{true};
            case analysis::SpecialValue::kFalse:
              return BooleanLiteral{false};
            default:
              Need(false);
              return UndefinedLiteral{};
          }
        }
      },
      v);
}
template <class T>
void Grow(std::vector<T>& v, std::size_t extra, TreeCheckBudget& q) {
  const auto n = v.size() + extra;
  if (n <= v.capacity()) return;
  q.Admit(n, sizeof(T));
  q.Step(v.size());
  v.reserve(n);
  q.Admit(v.capacity() - n, sizeof(T));
}
void Proof(const FunctionTree& tree, TreeCheckBudget& q) {
  Need(static_cast<bool>(tree.phased_defaults));
  const auto& owner = *tree.phased_defaults;
  Need(tree.identity == owner.Original().Identity() &&
       tree.method == owner.Original().Target());
  auto verified = VerifyPhasedParameterDefaults(owner.Original(), owner.Body(),
                                                owner.Frame(), owner.Evidence(),
                                                q.Remaining());
  if (const auto* e = std::get_if<PhasedParameterError>(&verified))
    throw *e == PhasedParameterError::kWorkLimit
        ? TreeStructureError::kWorkLimit
    : *e == PhasedParameterError::kResourceLimit
        ? TreeStructureError::kResourceLimit
        : TreeStructureError::kOwnership;
  q.Step(std::get<std::uint64_t>(verified));
}
}  // namespace
void BuildPhasedDefaultSignature(
    std::shared_ptr<const PhasedParameterDefaults> owner,
    const analysis::ValueProgram& graph, FunctionTree& tree,
    const ExpressionConstants& constants, TreeCheckBudget& q) {
  Need(owner && !tree.phased_defaults);
  tree.phased_defaults = std::move(owner);
  Proof(tree, q);
  const auto& c = tree.phased_defaults->Evidence();
  Need(tree.parameters.size() == c.formals);
  Grow(tree.expressions, c.defaults.size(), q);
  for (const auto& d : c.defaults) {
    q.Step();
    Need(d.ordinal < tree.parameters.size() &&
         !tree.parameters[d.ordinal].initializer &&
         d.initializer.value < graph.ops.size());
    const ExprId expression{
        static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({Literal(d.literal, &constants, d),
                                {graph.ops[d.initializer.value].origin.value}});
    tree.parameters[d.ordinal].initializer = expression;
  }
  VerifyPhasedDefaultSignature(tree, q);
}
void VerifyPhasedDefaultSignature(const FunctionTree& tree,
                                  TreeCheckBudget& q) {
  if (!tree.phased_defaults) return;
  Proof(tree, q);
  const auto& owner = *tree.phased_defaults;
  const auto& c = owner.Evidence();
  Need(tree.parameters.size() == c.formals);
  std::size_t next = 0;
  for (std::uint32_t i = 0; i < tree.parameters.size(); ++i) {
    q.Step();
    const auto& p = tree.parameters[i];
    if (next == c.defaults.size() || c.defaults[next].ordinal != i) {
      Need(!p.initializer);
      continue;
    }
    const auto& d = c.defaults[next++];
    Need(p.initializer && p.initializer->value < tree.expressions.size() &&
         !p.rest);
    const auto& expression = tree.expressions[p.initializer->value];
    const auto* actual = std::get_if<TreeLiteral>(&expression.expression);
    Need(actual);
    Need(expression.origin.value < tree.origins.size());
    const auto* origin =
        std::get_if<OriginalSite>(&tree.origins[expression.origin.value]);
    Need(origin && origin->site.value == d.initializer.value);
    std::visit(
        [&](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, analysis::IntegerLiteral>) {
            const auto* x = std::get_if<IntegerLiteral>(actual);
            Need(x && x->value == v.value);
          } else if constexpr (std::is_same_v<T, analysis::Float64Bits>) {
            const auto* x = std::get_if<NumberLiteral>(actual);
            Need(x && x->bits == v.value);
          } else if constexpr (std::is_same_v<T, abc::PoolIndex>) {
            const auto* x = std::get_if<TextId>(actual);
            Need(x && x->value < tree.texts.size());
          } else
            switch (v) {
              case analysis::SpecialValue::kUndefined:
                Need(std::holds_alternative<UndefinedLiteral>(*actual));
                break;
              case analysis::SpecialValue::kNull:
                Need(std::holds_alternative<NullLiteral>(*actual));
                break;
              case analysis::SpecialValue::kTrue:
              case analysis::SpecialValue::kFalse: {
                const auto* x = std::get_if<BooleanLiteral>(actual);
                Need(x && x->value == (v == analysis::SpecialValue::kTrue));
                break;
              }
              default:
                Need(false);
            }
        },
        d.literal);
  }
  Need(next == c.defaults.size());
}
void VerifyPhasedDefaultInput(const analysis::ValueProgram& graph,
                              const FunctionTree& tree, TreeCheckBudget& q) {
  if (!tree.phased_defaults) return;
  VerifyPhasedDefaultSignature(tree, q);
  const auto& source = tree.phased_defaults->Original().Graph();
  Need(graph.ops.size() == source.ops.size() &&
       graph.phases.size() == source.phases.size() &&
       graph.origins.size() == source.origins.size());
  std::vector<std::uint8_t> decisions;
  q.Array(decisions, graph.ops.size());
  for (const auto& d : tree.phased_defaults->Evidence().defaults) {
    q.Step();
    decisions[d.decision.value] = 1;
    const auto& op = graph.ops[d.decision.value];
    Need(op.life == analysis::ValueLife::kLive &&
         std::holds_alternative<analysis::ValueJump>(op.operation));
    const auto& before = graph.blocks[op.block.value];
    Need(before.edge_count == 1);
    const auto commit = graph.edges[before.first_edge.value].target;
    Need(graph.phases[commit.value].phase == analysis::ExceptionPhase::kCommit);
    const auto& outgoing = graph.blocks[commit.value];
    Need(outgoing.edge_count == 1);
    Need(graph.edges[outgoing.first_edge.value].target ==
         source.edges[d.provided.value].target);
    for (const auto initializer : d.initializer_ops) {
      q.Step();
      Need(
          graph.ops[initializer.value].life == analysis::ValueLife::kRemoved &&
          graph.phases[graph.ops[initializer.value].block.value].reachability ==
              analysis::PhaseReachability::kUnreachable);
    }
    for (const auto& capture : d.captures) {
      q.Step();
      Need(capture.initial_store.value < graph.ops.size() &&
           std::holds_alternative<analysis::ValueLexicalStore>(
               graph.ops[capture.initial_store.value].operation));
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    q.Step();
    const auto& op = graph.ops[i];
    const auto& raw = source.ops[i];
    Need(graph.origins[i].site == source.origins[i].site &&
         graph.origins[i].pc == source.origins[i].pc &&
         graph.origins[i].original == source.origins[i].original);
    if (op.life != analysis::ValueLife::kLive) continue;
    Need(raw.life == analysis::ValueLife::kLive && op.normal == raw.normal &&
         op.before_throw == raw.before_throw &&
         op.exceptional == raw.exceptional &&
         op.exception_cause == raw.exception_cause);
    if (!decisions[i]) Need(op.operation == raw.operation);
  }
  Need(graph.exceptional_exits.size() == source.exceptional_exits.size());
  for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
    q.Step();
    const auto& a = graph.exceptional_exits[i];
    const auto& b = source.exceptional_exits[i];
    Need(a.op == b.op && a.origin == b.origin &&
         a.before_throw == b.before_throw &&
         a.handler.has_value() == b.handler.has_value());
    if (a.handler)
      Need(a.handler->entry == b.handler->entry &&
           a.handler->dispatch == b.handler->dispatch);
  }
}
void VerifyPhasedDefaultRecovery(const FunctionTree& tree,
                                 const RecoveryFlow& flow, TreeCheckBudget& q) {
  if (!tree.phased_defaults) return;
  VerifyPhasedDefaultSignature(tree, q);
  const auto& c = tree.phased_defaults->Evidence();
  Need(flow.original &&
       flow.original->phased_defaults == tree.phased_defaults &&
       flow.parameter_defaults.size() == c.defaults.size());
  std::vector<std::optional<FlowPoint>> commits, starts;
  std::vector<std::optional<std::uint32_t>> ordinals;
  q.Array(commits, tree.parameters.size());
  q.Array(starts, tree.parameters.size());
  q.Array(ordinals, tree.symbols.size());
  for (std::uint32_t i = 0; i < tree.parameters.size(); ++i) {
    q.Step();
    const auto symbol = tree.parameters[i].symbol;
    Need(symbol.value < ordinals.size() && !ordinals[symbol.value]);
    ordinals[symbol.value] = i;
  }
  for (const auto& definition : flow.definitions) {
    q.Step();
    if (definition.kind != FlowDefinitionKind::kParameter) continue;
    Need(definition.binding.value < ordinals.size() &&
         ordinals[definition.binding.value]);
    const auto i = *ordinals[definition.binding.value];
    Need(!commits[i]);
    commits[i] = starts[i] = definition.point;
  }
  for (const auto& port : flow.parameter_defaults) {
    q.Step();
    Need(port.parameter.value < starts.size());
    starts[port.parameter.value] = port.argument;
  }
  const auto edges =
      [&](FlowPoint from,
          std::initializer_list<std::pair<FlowPoint, FlowCompletion>>
              expected) {
        Need(from.value < flow.events.size());
        std::size_t count = 0;
        unsigned seen = 0;
        for (auto at = flow.events[from.value].outgoing; at;) {
          q.Step();
          Need(*at < flow.edges.size() && count < expected.size());
          const auto& edge = flow.edges[*at];
          Need(edge.from == from);
          unsigned ordinal = 0;
          bool found = false;
          for (const auto& [to, kind] : expected) {
            if (edge.to == to && edge.completion == kind) {
              Need(!(seen & (1U << ordinal)));
              seen |= 1U << ordinal;
              found = true;
            }
            ++ordinal;
          }
          Need(found);
          ++count;
          at = edge.outgoing;
        }
        Need(count == expected.size());
      };
  for (std::size_t i = 0; i < c.defaults.size(); ++i) {
    q.Step();
    const auto& d = c.defaults[i];
    const auto& ports = flow.parameter_defaults[i];
    Need(ports.parameter.value == d.ordinal &&
         tree.parameters[d.ordinal].initializer == ports.initializer);
    for (const auto p : {ports.argument, ports.decision, ports.provided,
                         ports.missing, ports.initialized})
      Need(p.value < flow.events.size());
    Need(ports.initializer.value < flow.expressions.size());
    edges(ports.argument, {{ports.decision, FlowCompletion::kNormal}});
    edges(ports.decision, {{ports.provided, FlowCompletion::kNotTaken},
                           {flow.expressions[ports.initializer.value],
                            FlowCompletion::kTaken}});
    edges(ports.provided, {{ports.initialized, FlowCompletion::kNormal}});
    edges(ports.missing, {{ports.initialized, FlowCompletion::kNormal}});
    const auto ordinal = d.ordinal;
    Need(commits[ordinal] == ports.initialized);
    if (ordinal) {
      Need(commits[ordinal - 1].has_value());
      edges(*commits[ordinal - 1], {{ports.argument, FlowCompletion::kNormal}});
    } else {
      auto point = flow.entry;
      for (std::size_t visited = 0; point != ports.argument; ++visited) {
        q.Step();
        Need(visited <= flow.events.size() && point.value < flow.events.size());
        const auto& event = flow.events[point.value];
        if (point != flow.entry) {
          Need(event.definition &&
               event.definition->value < flow.definitions.size());
          const auto kind = flow.definitions[event.definition->value].kind;
          Need(kind == FlowDefinitionKind::kFunctionSelf ||
               kind == FlowDefinitionKind::kClassInner);
        }
        Need(event.outgoing.has_value());
        const auto& next = flow.edges[*event.outgoing];
        Need(next.completion == FlowCompletion::kNormal && !next.outgoing);
        point = next.to;
      }
    }
    if (ordinal + 1 < starts.size()) {
      Need(starts[ordinal + 1].has_value());
      edges(ports.initialized,
            {{*starts[ordinal + 1], FlowCompletion::kNormal}});
    } else {
      Need(tree.body.value < flow.statements.size());
      edges(ports.initialized,
            {{flow.statements[tree.body.value], FlowCompletion::kNormal}});
    }
    Need(ports.initializer.value < flow.actions.size() &&
         flow.actions[ports.initializer.value]);
    const auto& action = *flow.actions[ports.initializer.value];
    Need(action.normal == ports.missing &&
         action.exceptions.thrown == flow.terminal &&
         !action.exceptions.finalizer &&
         action.before.value < flow.events.size());
    const auto& literal = flow.events[action.before.value];
    Need(literal.expression == ports.initializer && literal.entry_evaluation &&
         literal.effects &&
         literal.effects->exceptional ==
             analysis::ExceptionalCompletion::kImpossible);
    const auto start = flow.expressions[ports.initializer.value];
    if (start != action.before)
      edges(start, {{action.before, FlowCompletion::kNormal}});
    edges(action.before, {{ports.missing, FlowCompletion::kNormal}});
    Need(ports.caller_exception == flow.terminal);
    const auto definition = flow.events[ports.initialized.value].definition;
    Need(definition && definition->value < flow.definitions.size() &&
         flow.definitions[definition->value].binding ==
             tree.parameters[d.ordinal].symbol &&
         flow.definitions[definition->value].kind ==
             FlowDefinitionKind::kParameter);
  }
}
}  // namespace abcd::source
