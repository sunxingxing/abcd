#include "script_scope.h"

#include <algorithm>
#include <new>

#include "abcd/source/scalar_expression.h"
#include "tree_semantics.h"
#include "tree_structure.h"
namespace abcd::source {
bool ScriptDeclaration::operator==(const ScriptDeclaration& other) const {
  const auto same =
      value.index() == other.value.index() &&
      std::visit(
          [&](const auto& input) {
            using T = std::decay_t<decltype(input)>;
            const auto* rhs = std::get_if<T>(&other.value);
            if (!rhs) return false;
            if constexpr (std::is_same_v<T, analysis::IntegerLiteral>)
              return input.value == rhs->value;
            else if constexpr (std::is_same_v<T, analysis::Float64Bits>)
              return input.value == rhs->value;
            else
              return input == *rhs;
          },
          value);
  return store == other.store && name == other.name && kind == other.kind &&
         same && normal == other.normal && before_throw == other.before_throw &&
         exceptional == other.exceptional && handler == other.handler;
}

namespace {
using E = ScriptScopeError;
struct Ledger {
  analysis::SemanticBudget left;
  std::uint64_t work = 0;
  void Step() {
    if (!left.work--) throw E::kWorkLimit;
    ++work;
  }
  void Admit(std::size_t n, std::size_t size) {
    if (n > left.bytes / size) throw E::kResourceLimit;
    left.bytes -= n * size;
  }
};
// Entry hoisting is a private instantiation prefix. Function values may only
// be consumed by their one global commit. No observable expression is moved.
ScriptScope Derive(const analysis::ValueProgram& graph,
                   const TreeConstants& constants, abc::EntityOffset record,
                   abc::EntityOffset entry, SourceContract contract,
                   Ledger& budget) {
  if (!GlobalScripts(contract)) throw E::kContract;
  if (constants.method != entry || constants.value_revision != graph.revision ||
      constants.cfg_revision != graph.cfg_revision)
    throw E::kCorrespondence;
  ScriptScope scope{record, entry, graph.revision, graph.cfg_revision};
  std::size_t count = 0;
  for (const auto& op : graph.ops) {
    budget.Step();
    count += op.life == analysis::ValueLife::kLive &&
             std::holds_alternative<analysis::ValueGlobalWrite>(op.operation);
  }
  budget.Admit(count, sizeof(ScriptDeclaration));
  scope.declarations.reserve(count);
  budget.Admit(scope.declarations.capacity() - count,
               sizeof(ScriptDeclaration));
  budget.Admit(graph.values.size(), sizeof(std::uint32_t));
  std::vector<std::uint32_t> uses(graph.values.size());
  for (const auto& use : graph.uses) {
    budget.Step();
    if (use.life == analysis::ValueLife::kLive)
      if (const auto* value = std::get_if<analysis::ValueId>(&use.operand)) {
        if (value->value >= uses.size()) throw E::kCorrespondence;
        ++uses[value->value];
      }
  }
  // Index the original first store per global name. A later initializer of
  // an already hoisted var is an assignment, never a second declaration.
  budget.Admit(count, sizeof(analysis::OpId));
  std::vector<analysis::OpId> stores;
  stores.reserve(count);
  budget.Admit(stores.capacity() - count, sizeof(analysis::OpId));
  budget.Admit(graph.ops.size(), sizeof(std::uint8_t));
  std::vector<std::uint8_t> first(graph.ops.size());
  const auto name_at = [&](analysis::OpId id) -> std::u16string_view {
    if (id.value >= constants.operations.size()) throw E::kCorrespondence;
    const auto* name = std::get_if<NameId>(&constants.operations[id.value]);
    if (!name || name->value >= constants.names.size()) throw E::kDeclaration;
    return constants.names[name->value];
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (graph.ops[i].life == analysis::ValueLife::kLive &&
        std::holds_alternative<analysis::ValueGlobalWrite>(
            graph.ops[i].operation))
      stores.push_back({static_cast<std::uint32_t>(i)});
  }
  std::sort(stores.begin(), stores.end(), [&](auto a, auto b) {
    budget.Step();
    const auto x = name_at(a), y = name_at(b);
    return x == y ? a < b : x < y;
  });
  for (std::size_t i = 0; i < stores.size(); ++i) {
    budget.Step();
    if (!i || name_at(stores[i - 1]) != name_at(stores[i]))
      first[stores[i].value] = 1;
  }
  budget.Admit(graph.ops.size(), sizeof(std::uint8_t));
  std::vector<std::uint8_t> instantiation(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& op = graph.ops[i];
    if (op.life != analysis::ValueLife::kLive) {
      instantiation[i] = 1;
      continue;
    }
    const auto* store = std::get_if<analysis::ValueGlobalWrite>(&op.operation);
    if (!store) {
      const auto allocation =
          static_cast<std::uint16_t>(analysis::LanguageEffect::kAllocation);
      const auto effects = static_cast<std::uint16_t>(op.normal) |
                           static_cast<std::uint16_t>(op.before_throw);
      const bool inert =
          effects == 0 &&
          op.exceptional == analysis::ExceptionalCompletion::kImpossible;
      const bool private_allocation =
          ResourceFailures(contract) == ResourceFailureScope::kExcluded &&
          (effects & ~allocation) == 0 &&
          (op.exceptional == analysis::ExceptionalCompletion::kImpossible ||
           op.exception_cause == analysis::ExceptionCause::kResourceExhaustion);
      // An environment cannot escape as a language operand: the original
      // semantic input and native cell activation proof already check that.
      instantiation[i] =
          inert || private_allocation ||
          std::holds_alternative<analysis::ValueFunctionCreation>(
              op.operation) ||
          std::holds_alternative<analysis::ValueEnvironmentPush>(op.operation);
      continue;
    }
    if (i >= constants.operations.size() || op.use_count != 1 ||
        op.first_use.value >= graph.uses.size())
      throw E::kDeclaration;
    const auto* name = std::get_if<NameId>(&constants.operations[i]);
    if (!name || name->value >= constants.names.size() ||
        !IsSourceIdentifier(constants.names[name->value]))
      throw E::kDeclaration;
    const auto& input = graph.uses[op.first_use.value].operand;
    GlobalDeclaration kind = GlobalDeclaration::kNone;
    switch (store->operation) {
      case analysis::GlobalOp::kInitMutable:
        kind = GlobalDeclaration::kLet;
        break;
      case analysis::GlobalOp::kInitConst:
        kind = GlobalDeclaration::kConst;
        break;
      case analysis::GlobalOp::kWriteVar:
        if (first[i]) {
          if (const auto* special = std::get_if<analysis::SpecialValue>(&input);
              special && *special == analysis::SpecialValue::kUndefined)
            kind = GlobalDeclaration::kVar;
          else if (const auto* value = std::get_if<analysis::ValueId>(&input)) {
            if (value->value >= graph.values.size()) throw E::kDeclaration;
            if (const auto* result = std::get_if<analysis::OpResult>(
                    &graph.values[value->value].origin);
                result && result->op.value < graph.ops.size() &&
                std::holds_alternative<analysis::ValueFunctionCreation>(
                    graph.ops[result->op.value].operation)) {
              if (uses[value->value] != 1) throw E::kEscape;
              kind = GlobalDeclaration::kFunction;
            }
          }
        }
        break;
      case analysis::GlobalOp::kWriteName:
        break;
      case analysis::GlobalOp::kReadName:
      case analysis::GlobalOp::kReadVar:
        throw E::kDeclaration;
    }
    instantiation[i] =
        kind == GlobalDeclaration::kVar || kind == GlobalDeclaration::kFunction;
    std::optional<analysis::ValueHandler> handler;
    const auto exit = std::lower_bound(
        graph.exceptional_exits.begin(), graph.exceptional_exits.end(),
        analysis::OpId{static_cast<std::uint32_t>(i)},
        [&](const auto& item, analysis::OpId id) {
          budget.Step();
          return item.op < id;
        });
    if (exit != graph.exceptional_exits.end() && exit->op.value == i)
      handler = exit->handler;
    scope.declarations.push_back({{static_cast<std::uint32_t>(i)},
                                  *name,
                                  kind,
                                  input,
                                  op.normal,
                                  op.before_throw,
                                  op.exceptional,
                                  handler});
  }
  // One shared may-observation dataflow. No physical block/PC order is a
  // hoisting proof. Each block has only unreachable / clean / observed states,
  // so it is scanned at most twice; every original normal edge participates.
  budget.Admit(graph.blocks.size(), sizeof(std::uint8_t));
  std::vector<std::uint8_t> state(graph.blocks.size());
  budget.Admit(graph.ops.size(), sizeof(std::uint8_t));
  std::vector<std::uint8_t> observed(graph.ops.size());
  budget.Admit(graph.blocks.size(), 2 * sizeof(analysis::WorkBlockId));
  std::vector<analysis::WorkBlockId> queue;
  queue.reserve(graph.blocks.size() * 2);
  budget.Admit(queue.capacity() - graph.blocks.size() * 2,
               sizeof(analysis::WorkBlockId));
  if (graph.blocks.empty()) throw E::kEntry;
  state[0] = 1;
  queue.push_back({0});
  for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
    budget.Step();
    const auto id = queue[cursor];
    const auto& block = graph.blocks[id.value];
    bool seen = state[id.value] == 2;
    if (block.first_op.value > graph.ops.size() ||
        block.op_count > graph.ops.size() - block.first_op.value ||
        block.first_edge.value > graph.edges.size() ||
        block.edge_count > graph.edges.size() - block.first_edge.value)
      throw E::kCorrespondence;
    for (std::size_t i = block.first_op.value;
         i < block.first_op.value + block.op_count; ++i) {
      budget.Step();
      observed[i] |= seen;
      seen |= !instantiation[i];
    }
    for (std::size_t i = block.first_edge.value;
         i < block.first_edge.value + block.edge_count; ++i) {
      budget.Step();
      const auto target = graph.edges[i].target;
      if (target.value >= state.size()) throw E::kCorrespondence;
      const std::uint8_t next = seen ? 2 : 1;
      if (state[target.value] < next) {
        state[target.value] = next;
        queue.push_back(target);
      }
    }
  }
  for (const auto& declaration : scope.declarations) {
    budget.Step();
    if ((declaration.kind == GlobalDeclaration::kVar ||
         declaration.kind == GlobalDeclaration::kFunction) &&
        observed[declaration.store.value])
      throw E::kOrder;
  }
  // Global lexical declarations are owned by script scope and execute once.
  // A repeated declaration or a declaration under control is a different
  // scope/activation obligation, never silently hoisted to this record.
  budget.Admit(scope.declarations.size(), sizeof(std::size_t));
  std::vector<std::size_t> order;
  order.reserve(scope.declarations.size());
  budget.Admit(order.capacity() - scope.declarations.size(),
               sizeof(std::size_t));
  for (std::size_t i = 0; i < scope.declarations.size(); ++i) {
    budget.Step();
    const auto& declaration = scope.declarations[i];
    order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](auto a, auto b) {
    budget.Step();
    const auto left =
                   std::u16string_view{
                       constants.names[scope.declarations[a].name.value]},
               right = std::u16string_view{
                   constants.names[scope.declarations[b].name.value]};
    return left == right
               ? scope.declarations[a].store < scope.declarations[b].store
               : left < right;
  });
  std::optional<std::u16string_view> previous_name;
  bool declared = false, variable = false;
  for (const auto index : order) {
    budget.Step();
    const auto& declaration = scope.declarations[index];
    const auto name =
        std::u16string_view{constants.names[declaration.name.value]};
    if (!previous_name || *previous_name != name) {
      previous_name = name;
      declared = false;
      variable = false;
    }
    if (declaration.kind != GlobalDeclaration::kNone) {
      if (declared) throw E::kDeclaration;
      declared = true;
      variable = declaration.kind == GlobalDeclaration::kVar ||
                 declaration.kind == GlobalDeclaration::kFunction;
    }
    if (std::get<analysis::ValueGlobalWrite>(
            graph.ops[declaration.store.value].operation)
                .operation == analysis::GlobalOp::kWriteVar &&
        !variable)
      throw E::kDeclaration;
  }
  scope.allocated_bytes =
      scope.declarations.capacity() * sizeof(ScriptDeclaration);
  scope.work = budget.work;
  return scope;
}
}  // namespace
std::variant<ScriptScope, ScriptScopeError> BuildScriptScope(
    const analysis::ValueProgram& graph, const TreeConstants& constants,
    abc::EntityOffset record, abc::EntityOffset entry, SourceContract contract,
    analysis::SemanticBudget budget) {
  try {
    Ledger ledger{budget};
    return Derive(graph, constants, record, entry, contract, ledger);
  } catch (E e) {
    return e;
  } catch (const std::bad_alloc&) {
    return E::kResourceLimit;
  }
}
std::variant<std::uint64_t, ScriptScopeError> VerifyScriptScope(
    const analysis::ValueProgram& graph, const TreeConstants& constants,
    const ScriptScope& scope, abc::EntityOffset expected_record,
    SourceContract contract, analysis::SemanticBudget budget) {
  try {
    if (scope.record != expected_record) return E::kOwner;
    Ledger ledger{budget};
    auto fresh = Derive(graph, constants, expected_record, constants.method,
                        contract, ledger);
    if (fresh.record != scope.record || fresh.entry != scope.entry ||
        fresh.revision != scope.revision ||
        fresh.cfg_revision != scope.cfg_revision ||
        fresh.declarations != scope.declarations)
      return E::kCorrespondence;
    return ledger.work;
  } catch (E e) {
    return e;
  } catch (const std::bad_alloc&) {
    return E::kResourceLimit;
  }
}
std::variant<std::shared_ptr<const ScriptScopeProof>, ScriptScopeError>
ScriptScopeProof::Verify(const analysis::ValueProgram& graph,
                         const TreeConstants& constants,
                         const ScriptScope& scope, FunctionTree& tree,
                         const ControlPlan& control,
                         const EvaluationPlan& evaluation,
                         std::span<const abc::MethodInfo> methods,
                         analysis::SemanticBudget budget) {
  const auto checked = VerifyScriptScope(
      graph, constants, scope, tree.script_record.value_or(abc::EntityOffset{}),
      tree.identity.contract, budget);
  if (const auto* error = std::get_if<E>(&checked)) return *error;
  try {
    budget.work -= std::get<std::uint64_t>(checked);
    auto syntax = VerifyTreeSemantics(graph, control, evaluation, tree,
                                      constants.View(), budget);
    if (const auto* error = std::get_if<TreeSemanticError>(&syntax))
      return *error == TreeSemanticError::kResourceLimit ? E::kResourceLimit
             : *error == TreeSemanticError::kWorkLimit   ? E::kWorkLimit
                                                         : E::kCorrespondence;
    const auto& correspondence = std::get<TreeSemantics>(syntax);
    Ledger ledger{budget};
    ledger.Admit(correspondence.allocated_bytes, 1);
    ledger.left.work -= correspondence.work;
    ledger.Admit(1, sizeof(ScriptScopeProof) +
                        sizeof(std::shared_ptr<const ScriptScopeProof>) +
                        2 * sizeof(std::size_t));
    ledger.Admit(scope.declarations.size(), sizeof(ScriptDeclaration));
    std::shared_ptr<ScriptScopeProof> proof(new ScriptScopeProof);
    proof->identity_ = tree.identity;
    proof->revision_ = graph.revision;
    proof->cfg_revision_ = graph.cfg_revision;
    proof->record_ = scope.record;
    proof->entry_ = scope.entry;
    proof->declarations_ = scope.declarations;
    ledger.Admit(methods.size(), sizeof(abc::EntityOffset));
    proof->owners_.reserve(methods.size());
    for (const auto& method : methods) {
      ledger.Step();
      if (method.owner_offset != scope.record) return E::kOwner;
      proof->owners_.push_back(method.offset);
    }
    std::sort(proof->owners_.begin(), proof->owners_.end(),
              [&](auto a, auto b) {
                ledger.Step();
                return a < b;
              });
    if (!std::binary_search(proof->owners_.begin(), proof->owners_.end(),
                            scope.entry))
      return E::kEntry;
    ledger.Admit(scope.declarations.size(), sizeof(std::u16string));
    proof->variables_.reserve(scope.declarations.size());
    for (const auto& declaration : scope.declarations) {
      ledger.Step();
      if (declaration.kind != GlobalDeclaration::kVar &&
          declaration.kind != GlobalDeclaration::kFunction)
        continue;
      const auto& name = constants.names[declaration.name.value];
      ledger.Admit(name.size() + 1, 2 * sizeof(char16_t));
      proof->variables_.push_back(name);
    }
    std::sort(proof->variables_.begin(), proof->variables_.end(),
              [&](const auto& a, const auto& b) {
                ledger.Step();
                return a < b;
              });
    if (tree.method != scope.entry || tree.value_revision != scope.revision ||
        tree.cfg_revision != scope.cfg_revision || !tree.script_record ||
        *tree.script_record != scope.record)
      return E::kOwner;
    // A script is not a function wrapper. Its original global declarations
    // are explicit global references; private frame/cell temporaries must not
    // leak into the global lexical record. Block/loop-owned cells stay private.
    for (const auto& symbol : tree.symbols) {
      ledger.Step();
      if (symbol.scope == tree.body_scope ||
          symbol.scope == tree.parameter_scope)
        return E::kPrivateScope;
    }
    ledger.Admit(graph.ops.size(), sizeof(std::optional<ExprId>));
    std::vector<std::optional<ExprId>> expressions(graph.ops.size());
    for (const auto& at : tree.operations) {
      ledger.Step();
      if (at.operation.value >= expressions.size()) return E::kCorrespondence;
      if (const auto* expr = std::get_if<ExprId>(&at.node))
        expressions[at.operation.value] = *expr;
    }
    ledger.Admit(tree.expressions.size(), sizeof(std::uint32_t));
    std::vector<std::uint32_t> statements(tree.expressions.size());
    const auto* body =
        std::get_if<BlockStmt>(&tree.statements[tree.body.value].statement);
    if (!body) return E::kEntry;
    for (std::uint32_t i = 0; i < body->child_count; ++i) {
      ledger.Step();
      if (const auto* statement = std::get_if<ExpressionStmt>(
              &tree.statements[tree.children[body->first_child + i].value]
                   .statement))
        ++statements[statement->expression.value];
    }
    for (const auto& declaration : scope.declarations) {
      ledger.Step();
      const auto expression = expressions[declaration.store.value];
      if (!expression || *expression >= ExprId{static_cast<std::uint32_t>(
                                            tree.expressions.size())})
        return E::kCorrespondence;
      auto* global = std::get_if<GlobalNameExpr>(
          &tree.expressions[expression->value].expression);
      if (!global || global->name != declaration.name || !global->value)
        return E::kCorrespondence;
      if (declaration.kind != GlobalDeclaration::kNone &&
          statements[expression->value] != 1)
        return E::kOrder;
      global->declaration = declaration.kind;
    }
    // Pending user decision: STGLOBALVAR invokes a pre-existing global
    // accessor even for an entry undefined store. Native declaration
    // instantiation does not. V9's two approved differences do not cover it.
    for (const auto& declaration : scope.declarations) {
      ledger.Step();
      if (declaration.kind == GlobalDeclaration::kVar ||
          declaration.kind == GlobalDeclaration::kFunction)
        return E::kHostGlobalAccessor;
    }
    return std::shared_ptr<const ScriptScopeProof>(std::move(proof));
  } catch (E e) {
    return e;
  } catch (const std::bad_alloc&) {
    return E::kResourceLimit;
  }
}
bool ScriptScopeProof::Owns(const FunctionTree& tree) const {
  return tree.identity == identity_ && tree.script_record == record_ &&
         std::binary_search(owners_.begin(), owners_.end(), tree.method);
}
bool ScriptScopeProof::HasVar(std::u16string_view name) const {
  const auto found =
      std::lower_bound(variables_.begin(), variables_.end(), name);
  return found != variables_.end() && *found == name;
}
std::size_t ScriptScopeProof::AllocatedBytes() const {
  // libc++'s shared owner holds a vptr, pointer and two reference counters.
  auto bytes = sizeof(*this) + sizeof(std::shared_ptr<const ScriptScopeProof>) +
               2 * sizeof(std::size_t) +
               owners_.capacity() * sizeof(abc::EntityOffset) +
               declarations_.capacity() * sizeof(ScriptDeclaration) +
               variables_.capacity() * sizeof(std::u16string);
  for (const auto& name : variables_)
    bytes += (name.capacity() + 1) * sizeof(char16_t);
  return bytes;
}
}  // namespace abcd::source
