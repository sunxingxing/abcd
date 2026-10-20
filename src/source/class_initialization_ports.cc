#include "class_initialization_ports.h"

#include <algorithm>

#include "class_inner_scope.h"
#include "class_tree.h"
#include "effect_order.h"
#include "flow_dominance.h"
#include "recovery_flow.h"

namespace abcd::source {
namespace {
using Error = TreeStructureError;
void Need(bool ok) { RequireTree(ok, Error::kOwnership); }
ClassInitializationPortKind Kind(ClassSelfPortKind kind) {
  switch (kind) {
    case ClassSelfPortKind::kInternalCommit:
      return ClassInitializationPortKind::kClassInnerInitialize;
    case ClassSelfPortKind::kInitializerBefore:
      return ClassInitializationPortKind::kStaticInitializeBefore;
    case ClassSelfPortKind::kInitializerCommit:
      return ClassInitializationPortKind::kStaticInitializeCommit;
    case ClassSelfPortKind::kPublicCommit:
      return ClassInitializationPortKind::kPublicPublish;
    case ClassSelfPortKind::kCompletedClass:
      return ClassInitializationPortKind::kCompletedClass;
    case ClassSelfPortKind::kClassAvailable:
      break;
  }
  throw Error::kOwnership;
}
void Arc(const RecoveryFlow& flow, FlowPoint from, FlowPoint to,
         FlowCompletion completion, TreeCheckBudget& q) {
  Need(from.value < flow.events.size() && to.value < flow.events.size());
  bool seen = false;
  for (auto e = flow.events[from.value].outgoing; e;
       e = flow.edges[*e].outgoing) {
    q.Step();
    Need(*e < flow.edges.size());
    const auto& edge = flow.edges[*e];
    if (edge.completion == completion) {
      Need(!seen && edge.to == to);
      seen = true;
    }
  }
  Need(seen);
}
const ClassTreeInitialization& Class(const FunctionTree& tree,
                                     ExprId expression) {
  Need(expression.value < tree.expressions.size());
  const auto* c =
      std::get_if<ClassExpr>(&tree.expressions[expression.value].expression);
  Need(c && c->definition && c->definition->class_self &&
       c->definition->class_self->certificate);
  return *c->definition->class_self;
}
}  // namespace
void VerifyClassInitializationCache(const RecoveryFlow& flow,
                                    const OriginalClassInitializationPort& port,
                                    const FlowOriginalAction& action,
                                    TreeCheckBudget& q) {
  q.Step();
  Need(flow.original &&
       port.kind == ClassInitializationPortKind::kClassInnerInitialize &&
       port.value);
  const auto& raw = flow.original->operations[port.operation.value];
  Need(raw.write && action.before.value < flow.events.size() &&
       flow.events[action.before.value].definition);
  const auto& definition =
      flow.definitions[flow.events[action.before.value].definition->value];
  Need(definition.original && definition.WriteSite() == raw.write &&
       definition.binding.value < flow.bindings.size());
  const OriginalSourceDefinition* expected = nullptr;
  if (const auto* expression = std::get_if<ExprId>(&*raw.write)) {
    Need(expression->value < flow.original->definitions.size() &&
         flow.original->definitions[expression->value]);
    expected = &*flow.original->definitions[expression->value];
  } else {
    const auto statement = std::get<StmtId>(*raw.write);
    Need(statement.value < flow.original->declarations.size() &&
         flow.original->declarations[statement.value]);
    expected = &*flow.original->declarations[statement.value];
  }
  Need(definition.original == expected &&
       definition.binding == expected->binding &&
       flow.bindings[definition.binding.value].cell_access ==
           CellVisibility::kClosed);
  const auto* saved = std::get_if<OriginalSavedDefinition>(&expected->origin);
  Need(saved && std::get_if<analysis::OpId>(&saved->placement) &&
       std::get<analysis::OpId>(saved->placement) == port.operation &&
       std::get_if<analysis::ValueId>(&saved->operand) &&
       std::get<analysis::ValueId>(saved->operand) == *port.value);
}
void RetainClassInitializationPorts(const analysis::ValueProgram& graph,
                                    const FunctionTree& tree,
                                    OriginalFlow& original,
                                    TreeCheckBudget& q) {
  using namespace analysis;
  std::size_t count = 0;
  for (const auto& e : tree.expressions) {
    q.Step();
    if (const auto* c = std::get_if<ClassExpr>(&e.expression);
        c && c->definition && c->definition->class_self)
      ++count;
  }
  if (!count) return;
  const auto ordered = BuildEffectOrder(graph, q.Remaining());
  if (const auto* error = std::get_if<ValueAvailabilityError>(&ordered)) {
    if (*error == ValueAvailabilityError::kResourceLimit)
      throw Error::kResourceLimit;
    if (*error == ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kOwnership;
  }
  const auto& order = std::get<EffectOrder>(ordered);
  q.Admit(order.allocated_bytes, 1);
  q.Step(order.work);
  q.Array(original.class_initializations, count);
  std::size_t next = 0;
  struct Publication {
    SymbolId binding;
    OpId creation, commit;
  };
  std::vector<Publication> publications;
  q.Admit(graph.ops.size(), sizeof(Publication));
  publications.reserve(graph.ops.size());
  q.Admit(publications.capacity() - graph.ops.size(), sizeof(Publication));
  for (std::uint32_t e = 0; e < tree.expressions.size(); ++e) {
    q.Step();
    const auto* c = std::get_if<ClassExpr>(&tree.expressions[e].expression);
    if (!c || !c->definition || !c->definition->class_self) continue;
    const auto& init = Class(tree, {e});
    const auto& proof = *init.certificate;
    const auto& scope = proof.Scope();
    Need(proof.Identity() == tree.identity && proof.Creator() == tree.method &&
         proof.CreatorRevision() == graph.revision &&
         proof.CreatorControlRevision() == graph.cfg_revision &&
         scope.binding == init.binding && scope.ports == init.ports &&
         scope.cleanup == init.cleanup &&
         scope.initializer_exception == init.exception &&
         c->definition->operation == scope.binding.creation);
    Need(c->definition->initializer &&
         c->definition->initializer->tree.class_scope == init.certificate);
    const auto& fields = *c->definition->initializer;
    Need(fields.context.Target() == proof.Initializer() &&
         fields.tree.method == proof.Initializer() && fields.tree.class_inner &&
         fields.tree.class_inner->name.value < fields.tree.names.size() &&
         c->definition->name ==
             fields.tree.names[fields.tree.class_inner->name.value]);
    VerifyClassInnerScope(c->definition->initializer->tree, q);
    // The raw class allocation succeeds before static initialization. Its
    // intrinsic action cannot borrow the later whole-ClassExpr completion.
    Need(scope.binding.creation.value < original.operations.size());
    original.operations[scope.binding.creation.value].completion =
        OriginalCompletionBoundary::kIntrinsicAction;
    auto& record = original.class_initializations[next++];
    record.expression = {e};
    record.certificate = init.certificate;
    q.Array(record.ports, scope.ports.size());
    std::size_t size = 0, inner = 0, before = 0, commit = 0, publish = 0, completed = 0;
    std::optional<OpId> internal, call;
    for (const auto& port : scope.ports) {
      q.Step();
      if (port.kind == ClassSelfPortKind::kClassAvailable) continue;
      Need(port.operation.value < graph.ops.size());
      const auto& op = graph.ops[port.operation.value];
      Need(op.life == ValueLife::kLive && op.origin == port.origin);
      record.ports[size++] = {Kind(port.kind), port.operation, port.origin,
                              port.use, port.value};
      switch (port.kind) {
        case ClassSelfPortKind::kInternalCommit:
          Need(std::holds_alternative<ValueLexicalStore>(op.operation));
          internal = port.operation;
          ++inner;
          break;
        case ClassSelfPortKind::kInitializerBefore:
          Need(std::holds_alternative<ValueInvoke>(op.operation));
          call = port.operation;
          ++before;
          break;
        case ClassSelfPortKind::kInitializerCommit:
          ++commit;
          Need(call == port.operation);
          break;
        case ClassSelfPortKind::kPublicCommit: {
          ++publish;
          Need(port.use && port.value && port.use->value < graph.uses.size());
          const auto& use = graph.uses[port.use->value];
          Need(std::get_if<OpId>(&use.owner) &&
               std::get<OpId>(use.owner) == port.operation &&
               use.role == OperandRole::kStoredValue &&
               std::get_if<ValueId>(&use.operand) &&
               std::get<ValueId>(use.operand) == *port.value);
          const auto& native = original.operations[port.operation.value];
          Need(native.write.has_value());
          const OriginalSourceDefinition* definition = nullptr;
          if (const auto* expression = std::get_if<ExprId>(&*native.write)) {
            Need(expression->value < original.definitions.size() &&
                 original.definitions[expression->value]);
            definition = &*original.definitions[expression->value];
          } else {
            const auto statement = std::get<StmtId>(*native.write);
            Need(statement.value < original.declarations.size() &&
                 original.declarations[statement.value]);
            definition = &*original.declarations[statement.value];
          }
          Need(definition->binding.value < tree.symbols.size());
          publications.push_back(
              {definition->binding, scope.binding.creation, port.operation});
          break;
        }
        case ClassSelfPortKind::kCompletedClass: {
          ++completed;
          Need(port.use && port.value && port.use->value < graph.uses.size());
          const auto& use = graph.uses[port.use->value];
          Need(use.life == ValueLife::kLive &&
               std::get_if<OpId>(&use.owner) &&
               std::get<OpId>(use.owner) == port.operation &&
               std::get_if<ValueId>(&use.operand) &&
               std::get<ValueId>(use.operand) == *port.value);
          break;
        }
        case ClassSelfPortKind::kClassAvailable:
          break;
      }
    }
    record.ports.resize(size);
    Need(inner == 1 && before == 1 && commit == 1 && (publish || completed) && internal &&
         call);
    Need(fields.call.creation == scope.binding.creation &&
         fields.call.call == *call);
    const auto& invoking = graph.ops[call->value];
    Need(order.Before(graph, *internal, invoking.block, invoking.order));
    if (init.exception) {
      bool cleanup = false;
      for (const auto& route : init.cleanup) {
        q.Step();
        cleanup |= route.handler == init.exception->handler;
      }
      Need(cleanup);
    }
    for (const auto& p : record.ports)
      if (p.kind == ClassInitializationPortKind::kPublicPublish ||
          p.kind == ClassInitializationPortKind::kCompletedClass) {
        q.Step();
        const auto& op = graph.ops[p.operation.value];
        Need(order.Before(graph, *call, op.block, op.order));
      }
  }
  // Shared address index: no per-class CFG scan. Original operand verification
  // has already bound these physical reads/writes to their real source units.
  std::sort(publications.begin(), publications.end(),
            [&](const auto& a, const auto& b) {
              q.Step();
              return a.binding < b.binding;
            });
  for (std::size_t o = 0; o < graph.ops.size(); ++o) {
    q.Step();
    std::optional<SymbolId> binding;
    if (original.cells && o < original.cells->references.size())
      if (const auto& read = original.cells->references[o];
          read && read->kind == CellAccessKind::kLoad) {
        Need(read->cell.value < original.cells->bindings.size());
        binding = original.cells->bindings[read->cell.value].binding;
      }
    if (std::holds_alternative<ValueModuleRead>(graph.ops[o].operation) &&
        original.operations[o].expression) {
      const auto e = *original.operations[o].expression;
      Need(e.value < tree.expressions.size());
      if (const auto* reference =
              std::get_if<SymbolExpr>(&tree.expressions[e.value].expression))
        binding = reference->symbol;
    }
    if (!binding) continue;
    const auto at = std::lower_bound(publications.begin(), publications.end(),
                                     *binding, [&](const auto& p, SymbolId b) {
                                       q.Step();
                                       return p.binding < b;
                                     });
    if (at == publications.end() || at->binding != *binding) continue;
    const auto id = OpId{static_cast<std::uint32_t>(o)};
    const auto& op = graph.ops[o];
    const auto& creation = graph.ops[at->creation.value];
    Need(order.Before(graph, id, creation.block, creation.order) ||
         order.Before(graph, at->commit, op.block, op.order));
  }
}
void VerifyClassInitializationRecovery(const FunctionTree& tree,
                                       const RecoveryFlow& flow,
                                       TreeCheckBudget& q) {
  if (flow.class_initializations.empty()) {
    Need(!flow.original || flow.original->class_initializations.empty());
    return;
  }
  Need(flow.original && flow.original->class_initializations.size() ==
                            flow.class_initializations.size());
  const auto analyzed = AnalyzeFlowDominance(flow, q.Remaining());
  if (const auto* error = std::get_if<Error>(&analyzed)) throw *error;
  const auto& order = std::get<FlowDominance>(analyzed);
  q.Admit(order.allocated_bytes, 1);
  q.Step(order.work);
  std::vector<std::optional<std::size_t>> source;
  q.Array(source, tree.expressions.size());
  for (std::size_t n = 0; n < flow.class_initializations.size(); ++n) {
    q.Step();
    const auto& c = flow.class_initializations[n];
    Need(c.expression.value < source.size() && !source[c.expression.value]);
    source[c.expression.value] = n;
  }
  struct CacheOwner {
    const OriginalClassInitializationPort* port;
    FlowPoint commit;
  };
  std::vector<std::optional<CacheOwner>> cache_owners;
  q.Array(cache_owners, flow.original->operations.size());
  // One occurrence index across all classes/projections. Operand verification
  // supplies original SSA identity; the current flow supplies the actual read
  // position. Dominating the invocation alone would miss an early saved read.
  struct CompletedInput {
    bool requested = false;
    std::optional<ExprId> expression;
  };
  bool continuation = false;
  for (const auto& raw : flow.original->class_initializations)
    for (const auto& port : raw.ports) {
      q.Step();
      continuation |= port.kind == ClassInitializationPortKind::kCompletedClass;
    }
  std::vector<CompletedInput> inputs;
  if (continuation) {
    q.Array(inputs, flow.original->uses.size());
    for (const auto& raw : flow.original->class_initializations)
      for (const auto& port : raw.ports) {
        q.Step();
        if (port.kind != ClassInitializationPortKind::kCompletedClass) continue;
        Need(port.use && port.use->value < inputs.size());
        inputs[port.use->value].requested = true;
      }
    for (const auto& input : flow.original->inputs) {
      q.Step();
      if (!input.occurrence) continue;
      Need(input.occurrence->value < inputs.size());
      auto& at = inputs[input.occurrence->value];
      if (!at.requested) continue;
      Need(!at.expression || *at.expression == input.expression);
      at.expression = input.expression;
    }
  }
  for (const auto& raw : flow.original->class_initializations) {
    q.Step();
    Need(raw.expression.value < source.size() && source[raw.expression.value]);
    const auto& c = flow.class_initializations[*source[raw.expression.value]];
    const auto& klass = Class(tree, raw.expression);
    Need(klass.certificate == raw.certificate &&
         c.exceptions.thrown == flow.terminal && !c.exceptions.finalizer);
    const auto& scope = raw.certificate->Scope();
    Need(klass.binding == scope.binding && klass.ports == scope.ports &&
         klass.cleanup == scope.cleanup &&
         klass.exception == scope.initializer_exception);
    std::size_t expected = 0;
    for (const auto& port : scope.ports) {
      q.Step();
      if (port.kind == ClassSelfPortKind::kClassAvailable) continue;
      Need(expected < raw.ports.size() &&
           raw.ports[expected++] == OriginalClassInitializationPort{
                                        Kind(port.kind), port.operation,
                                        port.origin, port.use, port.value});
    }
    Need(expected == raw.ports.size());
    Need(scope.binding.creation.value < flow.original_actions.size());
    const auto& allocation =
        flow.original_actions[scope.binding.creation.value];
    Need(allocation && allocation->before == c.allocation &&
         allocation->normal == c.inner &&
         flow.original->operations[scope.binding.creation.value].completion ==
             OriginalCompletionBoundary::kIntrinsicAction);
    Arc(flow, c.allocation, c.inner, FlowCompletion::kNormal, q);
    Arc(flow, c.inner, c.before, FlowCompletion::kNormal, q);
    Arc(flow, c.before, c.commit, FlowCompletion::kNormal, q);
    Arc(flow, c.before, c.exceptions.thrown, FlowCompletion::kThrow, q);
    std::size_t inner = 0, before = 0, commit = 0, published = 0, completed = 0;
    for (const auto& port : raw.ports) {
      q.Step();
      Need(port.operation.value < flow.original->operations.size());
      const auto& op = flow.original->operations[port.operation.value];
      Need(op.origin == port.origin);
      switch (port.kind) {
        case ClassInitializationPortKind::kClassInnerInitialize:
          ++inner;
          Need(!cache_owners[port.operation.value]);
          cache_owners[port.operation.value] = CacheOwner{&port, c.commit};
          Need(order.Dominates(c.inner, c.before));
          Need(flow.original_actions[port.operation.value] &&
               flow.original_actions[port.operation.value]->before == c.inner &&
               flow.original_actions[port.operation.value]->normal == c.before);
          break;
        case ClassInitializationPortKind::kStaticInitializeBefore:
          ++before;
          Need(op.exceptional == analysis::ExceptionalCompletion::kMayThrow);
          Need(flow.original_actions[port.operation.value] &&
               flow.original_actions[port.operation.value]->before ==
                   c.before &&
               flow.original_actions[port.operation.value]->normal == c.commit);
          break;
        case ClassInitializationPortKind::kStaticInitializeCommit:
          ++commit;
          break;
        case ClassInitializationPortKind::kCompletedClass: {
          ++completed;
          Need(port.use && port.value &&
               port.use->value < flow.original->uses.size());
          const auto& use = flow.original->uses[port.use->value];
          Need(use.life == analysis::ValueLife::kLive &&
               std::get_if<analysis::OpId>(&use.owner) &&
               std::get<analysis::OpId>(use.owner) == port.operation &&
               std::get_if<analysis::ValueId>(&use.operand) &&
               std::get<analysis::ValueId>(use.operand) == *port.value);
          Need(flow.original_actions[port.operation.value].has_value());
          const auto& action = *flow.original_actions[port.operation.value];
          Need(order.Dominates(c.commit, action.before));
          Need(inputs[port.use->value].expression.has_value());
          const auto expression = *inputs[port.use->value].expression;
          Need(expression.value < flow.expressions.size() &&
               expression.value < flow.value_uses.size() &&
               flow.value_uses[expression.value] != FlowValueUse::kUnmodeled &&
               order.Dominates(c.commit, flow.expressions[expression.value]));
          if (op.exceptional == analysis::ExceptionalCompletion::kMayThrow)
            Arc(flow, action.before, action.exceptions.thrown,
                FlowCompletion::kThrow, q);
          break;
        }
        case ClassInitializationPortKind::kPublicPublish: {
          ++published;
          Need(port.operation.value < flow.original_actions.size() &&
               flow.original_actions[port.operation.value]);
          const auto& action = *flow.original_actions[port.operation.value];
          Need(action.before.value < flow.events.size() &&
               flow.events[action.before.value].definition &&
               order.Dominates(c.commit, action.before));
          const auto& definition =
              flow.definitions[flow.events[action.before.value]
                                   .definition->value];
          Need(op.write.has_value());
          const OriginalSourceDefinition* expected = nullptr;
          if (const auto* expression = std::get_if<ExprId>(&*op.write)) {
            Need(expression->value < flow.original->definitions.size() &&
                 flow.original->definitions[expression->value]);
            expected = &*flow.original->definitions[expression->value];
          } else {
            const auto statement = std::get<StmtId>(*op.write);
            Need(statement.value < flow.original->declarations.size() &&
                 flow.original->declarations[statement.value]);
            expected = &*flow.original->declarations[statement.value];
          }
          Need(definition.original == expected &&
               definition.WriteSite() == op.write);
          break;
        }
      }
    }
    Need(inner == 1 && before == 1 && commit == 1 && (published || completed));
  }
  // Source caches are materializations of the same verified result, not the
  // internal initialization port. Check all retained aliases in one shared
  // pass, including proposals that omit a dead private materialization.
  for (const auto& definition : flow.definitions) {
    q.Step();
    if (!definition.original) continue;
    const auto* saved =
        std::get_if<OriginalSavedDefinition>(&definition.original->origin);
    const auto* operation =
        saved ? std::get_if<analysis::OpId>(&saved->placement) : nullptr;
    if (!operation || operation->value >= cache_owners.size() ||
        !cache_owners[operation->value])
      continue;
    const auto& owner = *cache_owners[operation->value];
    VerifyClassInitializationCache(flow, *owner.port,
                                   {definition.point, definition.point, {}}, q);
    Need(order.Dominates(owner.commit, definition.point));
  }
}
}  // namespace abcd::source
