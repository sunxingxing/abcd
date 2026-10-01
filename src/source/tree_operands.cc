#include "tree_correspondence.h"
#include "class_tree.h"
#include "phase_evaluation.h"
#include "logical_evaluation.h"

#include <bit>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = TreeSemanticError;
class OperandCheck {
 public:
  OperandCheck(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      const FunctionTree& tree, const TreeFlow& flow,
      ExpressionConstants constants, TreeCheckBudget& budget)
      : graph_(graph), evaluation_(evaluation), tree_(tree), flow_(flow),
        constants_(constants), budget_(budget) {}
  void Run() {
    RequireMeaning(constants_.operations.size() == graph_.ops.size() &&
                   constants_.name_count <= tree_.names.size() &&
                   constants_.text_count <= tree_.texts.size(), Error::kConstant);
    if (constants_.number_type_text)
      RequireMeaning(constants_.number_type_text->value < constants_.text_count &&
                     tree_.texts[constants_.number_type_text->value] == u"number", Error::kConstant);
    for (const auto read : evaluation_.class_projection.prototypes) {
      budget_.Step();
      RequireMeaning(read.value < constants_.operations.size(), Error::kConstant);
      const auto* name = std::get_if<NameId>(&constants_.operations[read.value]);
      RequireMeaning(name && name->value < tree_.names.size() && tree_.names[name->value] == u"prototype", Error::kConstant);
    }
    for (const auto& call : evaluation_.class_initializers) {
      budget_.Step();
      RequireMeaning(call.creation.value < constants_.operations.size(), Error::kConstant);
      const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[call.creation.value]);
      RequireMeaning(definition && *definition && (*definition)->initializer &&
          (*definition)->initializer->call == call, Error::kConstant);
      const auto& fields = *(*definition)->initializer;
      const auto* site = fields.context.CreationSite();
      const auto* method = std::get_if<analysis::ValueFunctionCreation>(&graph_.ops[call.method.value].operation);
      RequireMeaning(site && method && fields.tree.identity == tree_.identity &&
          fields.tree.method == site->target && site->creator == tree_.method &&
          site->pc == graph_.origins[graph_.ops[call.method.value].origin.value].pc &&
          site->creation.method == method->method, Error::kConstant);
    }
    for (const auto& definition_site : evaluation_.class_definitions) {
      budget_.Step();
      RequireMeaning(definition_site.creation.value < constants_.operations.size(), Error::kConstant);
      const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[definition_site.creation.value]);
      RequireMeaning(definition && *definition, Error::kConstant);
      const auto& members = (*definition)->definitions;
      const auto found = std::lower_bound(members.begin(), members.end(), definition_site.definition,
          [&](const auto& member, auto id) { budget_.Step(); return member.site.definition < id; });
      RequireMeaning(found != members.end() && found->site == definition_site, Error::kConstant);
      const auto check = [&](const PreparedFamily& family, analysis::OpId method_id, abc::ClassMethodKind kind) {
        budget_.Step(); const auto* site = family.DefinedMemberCreation();
        const auto* method = std::get_if<analysis::ValueFunctionCreation>(&graph_.ops[method_id.value].operation);
        RequireMeaning(site && method && family.Identity() == tree_.identity &&
            site->function.creator == tree_.method && site->definition == definition_site.definition && site->kind == kind &&
            site->function.pc == graph_.origins[graph_.ops[method_id.value].origin.value].pc &&
            site->function.creation.method == method->method, Error::kConstant);
      };
      const auto methods = definition_site.Methods();
      for (std::size_t i = 0; i < methods.size(); ++i) {
        RequireMeaning(found->bodies[i].has_value() == methods[i].operation.has_value(), Error::kConstant);
        if (found->bodies[i]) check(*found->bodies[i], *methods[i].operation, methods[i].kind);
      }
    }
    std::size_t next_function = 0;
    std::size_t next_module = 0;
    std::vector<std::optional<std::size_t>> module_aliases;
    if (!tree_.modules.empty()) {
      budget_.Array(module_aliases, tree_.symbols.size());
      for (std::size_t i = 0; i < tree_.modules.size(); ++i) {
        budget_.Step();
        const auto symbol = tree_.modules[i].symbol;
        RequireMeaning(symbol.value < module_aliases.size() && !module_aliases[symbol.value], Error::kOperand);
        module_aliases[symbol.value] = i;
      }
    }
    RequireMeaning(tree_.constructor_guards == evaluation_.constructor_guards, Error::kOperand);
    RequireMeaning(tree_.constructor_guards.size() == constants_.constructor_guards.size() &&
        std::equal(tree_.constructor_guards.begin(), tree_.constructor_guards.end(), constants_.constructor_guards.begin()), Error::kOperand);
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      if (evaluation_.operations[i].kind == EvaluationKind::kClassProjection || IsAbsentHeritageTransport(graph_, evaluation_, op) || op.life != analysis::ValueLife::kLive ||
          evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
          evaluation_.operations[i].kind == EvaluationKind::kAlias) continue;
      current_ = {static_cast<std::uint32_t>(i)};
      origin_ = {op.origin.value};
      if (std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation)) continue;
      if (std::holds_alternative<analysis::ValueDebugger>(op.operation)) {
        RequireMeaning(!op.result && op.use_count == 0 && !flow_.expressions[i], Error::kOperand);
        continue;
      }
      if (evaluation_.operations[i].kind == EvaluationKind::kCellGuard) {
        RequireMeaning(evaluation_.cells && IsInitializedCellGuard(graph_, *evaluation_.cells, current_) &&
            !flow_.expressions[i], Error::kOperand);
        continue;
      }
      if (IsConstructorGuard(tree_, current_)) {
        RequireMeaning(tree_.callable == CallableKind::kDerivedConstructor &&
            std::holds_alternative<analysis::ValueSuperCheck>(op.operation) && !op.result &&
            !flow_.expressions[i], Error::kOperand);
        continue;
      }
      RequireMeaning(flow_.expressions[i].has_value(), Error::kOperand);
      const auto expression = *flow_.expressions[i];
      if (!flow_.logical_operations.empty() && flow_.logical_operations[i]) {
        const auto& logical = Node<LogicalExpr>(expression);
        RequireMeaning(logical.operation == *flow_.logical_operations[i], Error::kOperand);
        if (std::holds_alternative<analysis::ValueUnary>(op.operation))
          Input(graph_.uses[op.first_use.value].operand, logical.left);
        else RequireMeaning(std::holds_alternative<analysis::ValueBranch>(op.operation), Error::kOperand);
        continue;
      }
      const auto input = [&](std::uint32_t ordinal) -> const analysis::ValueOperand& {
        return graph_.uses[op.first_use.value + ordinal].operand;
      };
      std::visit([&](const auto& operation) {
        using T = std::decay_t<decltype(operation)>;
        if constexpr (std::is_same_v<T, analysis::ValueFrameFunction>) {
          RequireMeaning(op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage() && tree_.self &&
              Node<SymbolExpr>(expression).symbol == tree_.self, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueGlobalObject>) {
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          (void)Node<RealmObjectExpr>(expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueSymbolConstructor>) {
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          (void)Node<SymbolConstructorExpr>(expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueRestArguments>) {
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage() &&
              operation.first_argument < tree_.parameters.size() &&
              tree_.parameters[operation.first_argument].rest &&
              Node<SymbolExpr>(expression).symbol == tree_.parameters[operation.first_argument].symbol, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueArgumentSnapshot>) {
          const auto& snapshot = Node<ArgumentSnapshotExpr>(expression);
          RequireMeaning(graph_.arguments && evaluation_.arguments == graph_.arguments &&
              snapshot.owner == tree_.owner && operation.slots == graph_.arguments->frame.parameters,
              Error::kOperand);
          Arguments(op, snapshot.slots, 0, operation.slots);
        } else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>) {
          RequireMeaning(op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          RequireMeaning(Node<ThisExpr>(expression).owner == tree_.owner, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueCopy> ||
                      std::is_same_v<T, analysis::ValueThrow> ||
                      std::is_same_v<T, analysis::ValueReturn>) {
          Input(input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueUnary>) {
          Unary(operation.op, input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueAwait>) {
          Input(input(0), Node<AwaitExpr>(expression).operand);
        } else if constexpr (std::is_same_v<T, analysis::ValueYield>) {
          const auto& yielded = Node<YieldExpr>(expression);
          RequireMeaning((yielded.form == YieldForm::kDirect && operation.origin.form == analysis::YieldForm::kDirect) ||
              (yielded.form == YieldForm::kDelegate && operation.origin.form == analysis::YieldForm::kDelegate), Error::kOperator);
          Input(input(0), yielded.operand);
        } else if constexpr (std::is_same_v<T, analysis::ValueBranch>) {
          Unary(operation.predicate, input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueBinary>) {
          const auto& binary = Node<BinaryExpr>(expression);
          RequireMeaning(binary.operation == HoleSourceOperator(evaluation_.holes ? &*evaluation_.holes : nullptr,
              current_, operation.op), Error::kOperator);
          Input(input(0), binary.left); Input(input(1), binary.right);
        } else if constexpr (std::is_same_v<T, analysis::ValueHole>) {
          RequireMeaning(evaluation_.holes && tree_.hole && Node<SymbolExpr>(expression).symbol == *tree_.hole,
              Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueModuleRead>) {
          RequireMeaning(next_module < evaluation_.modules.size(), Error::kOperand);
          const auto& module = evaluation_.modules[next_module++];
          const auto symbol = Node<SymbolExpr>(expression).symbol;
          RequireMeaning(module.pair.read == current_ && module.read == operation &&
              symbol.value < module_aliases.size() && module_aliases[symbol.value] &&
              tree_.modules[*module_aliases[symbol.value]].target == module.target, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueModuleWrite>) {
          const auto* binding = std::get_if<ModuleStoreConstant>(&constants_.operations[i]);
          RequireMeaning(binding && binding->slot == operation.slot, Error::kConstant);
          SymbolId symbol;
          if (binding->initialize) {
            RequireMeaning(flow_.statements[i].has_value(), Error::kOperand);
            const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[flow_.statements[i]->value].statement);
            RequireMeaning(declaration && declaration->initializer == expression, Error::kOperand);
            symbol = declaration->symbol;
            Input(input(0), expression);
          } else {
            const auto& assignment = Node<AssignmentExpr>(expression);
            const auto* target = std::get_if<SymbolTarget>(&assignment.target);
            RequireMeaning(target != nullptr, Error::kOperand);
            symbol = target->symbol;
            Input(input(0), assignment.value);
          }
          RequireMeaning(symbol.value < module_aliases.size() && module_aliases[symbol.value], Error::kOperand);
          const auto& alias = tree_.modules[*module_aliases[symbol.value]];
          RequireMeaning(alias.target == binding->target && alias.access == ModuleAccess::kMutable &&
              (!binding->initialize || alias.local), Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueGuard>) {
          // Flow independently proved this placement is the checked read's
          // same expression; combined-origin checking covers both original PCs.
          RequireMeaning(evaluation_.operations[i].kind == EvaluationKind::kModuleGuard, Error::kUnsupportedOperation);
        } else if constexpr (std::is_same_v<T, analysis::ValueLexicalLoad>) {
          RequireMeaning(Node<SymbolExpr>(expression).symbol == CellSymbol(), Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>) {
          const auto& assignment = Node<AssignmentExpr>(expression);
          const auto* target = std::get_if<SymbolTarget>(&assignment.target);
          RequireMeaning(target && target->symbol == CellSymbol(), Error::kOperand);
          Input(input(0), assignment.value);
        } else if constexpr (std::is_same_v<T, analysis::ValueNamedRead>) {
          const auto& member = Node<MemberExpr>(expression);
          Named(member.key, constants_.operations[i]);
          Input(input(0), member.base);
        } else if constexpr (std::is_same_v<T, analysis::ValueNamedWrite>) {
          const auto& assigned = Node<AssignmentExpr>(expression);
          const auto* member = std::get_if<MemberTarget>(&assigned.target);
          RequireMeaning(member != nullptr, Error::kOperator);
          Named(member->key, constants_.operations[i]);
          Input(input(0), member->base); Input(input(1), assigned.value);
        } else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>) {
          const auto& member = Node<MemberExpr>(expression);
          const auto* key = std::get_if<ComputedKey>(&member.key);
          RequireMeaning(key != nullptr, Error::kOperator);
          Input(input(0), member.base); Input(input(1), key->expression);
        } else if constexpr (std::is_same_v<T, analysis::ValuePropertyWrite>) {
          RequireMeaning(tree_.strictness == Strictness::kStrict, Error::kUnsupportedOperation);
          const auto& assignment = Node<AssignmentExpr>(expression);
          const auto* member = std::get_if<MemberTarget>(&assignment.target);
          RequireMeaning(member != nullptr, Error::kOperator);
          const auto* key = std::get_if<ComputedKey>(&member->key);
          RequireMeaning(key != nullptr, Error::kOperator);
          Input(input(0), member->base); Input(input(1), key->expression);
          Input(input(2), assignment.value);
        } else if constexpr (std::is_same_v<T, analysis::ValueIndexedWrite>) {
          RequireMeaning(flow_.initializers[i].has_value(), Error::kOperand);
          const auto& property = tree_.properties[flow_.initializers[i]->value];
          const auto expected = std::to_string(operation.index);
          RequireMeaning(property.key.value < tree_.names.size() &&
              tree_.names[property.key.value] == std::u16string(expected.begin(), expected.end()), Error::kConstant);
          const auto previous = edge_input_; edge_input_ = true;
          Input(input(1), property.value); edge_input_ = previous;
        } else if constexpr (std::is_same_v<T, analysis::ValueOwnWrite>) {
          if (IsReceiverField(graph_, op)) {
            const auto& field = Node<FieldDefinitionExpr>(expression);
            const auto* name = std::get_if<NameId>(&constants_.operations[i]);
            RequireMeaning(name && field.name == *name, Error::kConstant);
            Input(input(0), field.receiver); Input(input(1), field.value);
            return;
          }
          RequireMeaning(flow_.initializers[i].has_value(), Error::kOperand);
          const auto& property = tree_.properties[flow_.initializers[i]->value];
          const auto* name = std::get_if<NameId>(&constants_.operations[i]);
          RequireMeaning(name && name->value < constants_.name_count && property.key == *name, Error::kConstant);
          // LiteralPlan proved the receiver's allocation; only its stored value
          // appears in syntax, always as a pre-allocation snapshot or leaf.
          const auto previous = edge_input_; edge_input_ = true;
          Input(input(1), property.value);
          edge_input_ = previous;
        } else if constexpr (std::is_same_v<T, analysis::ValueString>) {
          const auto* text = std::get_if<TextId>(&constants_.operations[i]);
          RequireMeaning(text && text->value < constants_.text_count, Error::kConstant);
          RequireMeaning(Literal<TextId>(expression) == *text, Error::kConstant);
        } else if constexpr (std::is_same_v<T, analysis::ValueGlobalRead>) {
          RequireMeaning(operation.lookup == analysis::GlobalLookup::kName, Error::kUnsupportedOperation);
          const auto* name = std::get_if<NameId>(&constants_.operations[i]);
          RequireMeaning(name && name->value < constants_.name_count &&
              Node<GlobalNameExpr>(expression).name == *name, Error::kConstant);
        } else if constexpr (std::is_same_v<T, analysis::ValueInvoke>) {
          const auto& call = Node<CallExpr>(expression);
          Input(input(0), call.callee);
          constexpr std::uint32_t kValuePrefix = 1;
          constexpr std::uint32_t kMemberPrefix = 2;
          if (operation.receiver == analysis::InvokeReceiver::kExplicit) {
            RequireMeaning(call.reference == CallReference::kMember, Error::kOperand);
            Receiver(op, call, input(0), input(1));
            Arguments(op, call.arguments, kMemberPrefix, operation.arguments);
          } else {
            RequireMeaning(operation.receiver == analysis::InvokeReceiver::kUndefined &&
                call.reference == CallReference::kValue, Error::kOperand);
            Arguments(op, call.arguments, kValuePrefix, operation.arguments);
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueSuperCall>) {
          RequireMeaning(tree_.callable == CallableKind::kDerivedConstructor, Error::kOperand);
          const auto& call = Node<SuperCallExpr>(expression);
          Arguments(op, call.arguments, 0, operation.arguments);
        } else if constexpr (std::is_same_v<T, analysis::ValueConstruct>) {
          const auto& construct = Node<ConstructExpr>(expression);
          Input(input(0), construct.constructor);
          constexpr std::uint32_t kConstructorPrefix = 1;
          Arguments(op, construct.arguments, kConstructorPrefix, operation.arguments);
        } else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation> ||
                             std::is_same_v<T, analysis::ValueObjectAllocation>) {
          constexpr bool array = std::is_same_v<T, analysis::ValueArrayAllocation>;
          constexpr auto kind = array ? AggregateKind::kArray : AggregateKind::kObject;
          if (operation.literal) {
            const auto& object = Node<ObjectExpr>(expression);
            RequireMeaning(object.kind == kind, Error::kOperator);
            const auto* expected = std::get_if<AggregateConstant>(&constants_.operations[i]);
            RequireMeaning(expected && expected->kind == kind && expected->first <= constants_.properties.size() &&
                expected->count <= constants_.properties.size() - expected->first &&
                i < flow_.buffered_properties.size() &&
                flow_.buffered_properties[i] == expected->count &&
                object.properties.count >= expected->count, Error::kConstant);
            for (std::uint32_t p = 0; p < expected->count; ++p) {
              budget_.Step();
              const auto& source = constants_.properties[expected->first + p];
              const auto& actual = tree_.properties[object.properties.first + p];
              RequireMeaning(source.key.value < constants_.name_count, Error::kConstant);
              const auto& source_name = tree_.names[source.key.value];
              const auto& actual_name = tree_.names[actual.key.value];
              budget_.Step(source_name.size()); budget_.Step(actual_name.size());
              RequireMeaning(actual_name == source_name, Error::kConstant);
              if (!flow_.defined_properties[object.properties.first + p])
                BufferedLiteral(source.value, actual.value);
            }
            if constexpr (array) return;
            // A folded own definition may replace only the last buffered
            // occurrence of its key. Otherwise a following buffer value would
            // overwrite it. Check actual keys, not NameId equality or a builder
            // supplied lookup; distinct pool entries can spell the same name.
            std::vector<std::uint32_t> order;
            budget_.Array(order, expected->count);
            for (std::uint32_t p = 0; p < expected->count; ++p) order[p] = p;
            const auto key = [&](auto p) -> std::u16string_view {
              return tree_.names[constants_.properties[expected->first + p].key.value];
            };
            std::sort(order.begin(), order.end(), [&](auto a, auto b) {
              const auto x = key(a), y = key(b); budget_.Step(); budget_.Step(x.size()); budget_.Step(y.size());
              return x == y ? a < b : x < y;
            });
            for (std::size_t p = 1; p < order.size(); ++p) {
              const auto x = key(order[p - 1]), y = key(order[p]);
              budget_.Step(); budget_.Step(x.size()); budget_.Step(y.size());
              if (x == y) RequireMeaning(!flow_.defined_properties[object.properties.first + order[p - 1]], Error::kConstant);
            }
            return;
          }
          const auto* aggregate = std::get_if<ObjectExpr>(&tree_.expressions[expression.value].expression);
          RequireMeaning(aggregate ? aggregate->kind == kind :
              (array ? std::holds_alternative<EmptyArrayExpr>(tree_.expressions[expression.value].expression) :
                       std::holds_alternative<EmptyObjectExpr>(tree_.expressions[expression.value].expression)), Error::kOperator);
        } else if constexpr (std::is_same_v<T, analysis::ValueFunctionCreation>) {
          RequireMeaning(next_function < evaluation_.functions.size(), Error::kOperand);
          const auto& function = evaluation_.functions[next_function++];
          const auto& created = Node<FunctionExpr>(expression);
          RequireMeaning(function.creation == current_ && function.child != tree_.owner &&
              created.child == function.child && created.captures.count == function.captures.size(), Error::kOperand);
          for (std::size_t c = 0; c < function.captures.size(); ++c) {
            budget_.Step(); const auto cell = function.captures[c];
            RequireMeaning(cell.value < flow_.cells.size() && flow_.cells[cell.value] &&
                tree_.closure_captures[created.captures.first + c] == *flow_.cells[cell.value], Error::kOperand);
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueClassCreation>) {
          const auto& created = Node<ClassExpr>(expression);
          const auto* bound = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[i]);
          RequireMeaning(bound && *bound && created.definition == *bound, Error::kConstant);
          const auto& definition = *created.definition;
          RequireMeaning(definition.identity == tree_.identity && definition.creation.creator == tree_.method &&
              definition.creation.pc == graph_.origins[op.origin.value].pc &&
              definition.creation.creation.constructor == operation.constructor &&
              definition.creation.creation.literal == operation.literal &&
              definition.creation.creation.length == operation.length &&
              definition.creation.creation.cache_slot == operation.cache_slot && op.use_count == 1, Error::kOperand);
          const auto* heritage = std::get_if<analysis::ValueId>(&input(0));
          const bool absent = heritage && graph_.values[heritage->value].domain == analysis::ValueKinds::Hole();
          const auto& family = definition.constructor.Family();
          RequireMeaning(evaluation_.flow == EvaluationFlow::kNormal && created.heritage.has_value() != absent &&
              family.members[family.root.value].tree.callable ==
                  (absent ? CallableKind::kBaseConstructor : CallableKind::kDerivedConstructor), Error::kOperand);
          if (created.heritage) Input(input(0), *created.heritage);
        } else {
          static_assert(
                        std::is_same_v<T, analysis::ValueSuperCall> || std::is_same_v<T, analysis::ValueSuperCheck> || std::is_same_v<T, analysis::ValueDefineAccessor> || std::is_same_v<T, analysis::ValueHole> ||
                        std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                        std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                        std::is_same_v<T, analysis::ValueNop> || std::is_same_v<T, analysis::ValueDebugger> ||
                        std::is_same_v<T, analysis::ValueJump>);
          throw Error::kUnsupportedOperation;
        }
      }, op.operation);
    }
    RequireMeaning(next_function == evaluation_.functions.size(), Error::kOperand);
    RequireMeaning(next_module == evaluation_.modules.size(), Error::kOperand);
    CopyInputs();
  }

 private:
  SymbolId CellSymbol() const {
    RequireMeaning(evaluation_.cells.has_value(), Error::kOperand);
    const auto cell = evaluation_.cells->cells.operations[current_.value];
    RequireMeaning(cell && cell->value < flow_.cells.size() && flow_.cells[cell->value], Error::kOperand);
    return *flow_.cells[cell->value];
  }
  void CopyInputs() {
    if (!evaluation_.phis) return;
    RequireMeaning(flow_.copies.size() == evaluation_.phis->bundles.size(), Error::kOperand);
    edge_input_ = true;
    for (std::size_t i = 0; i < flow_.copies.size(); ++i) {
      budget_.Step();
      const auto& copies = flow_.copies[i];
      if (evaluation_.exceptions &&
          std::holds_alternative<ThrowCopies>(evaluation_.exceptions->placements[i])) {
        const auto& bundle = evaluation_.phis->bundles[i];
        RequireMeaning(copies.expressions.size() == bundle.count && copies.temporaries.empty(), Error::kOperand);
        for (std::uint32_t j = 0; j < bundle.count; ++j) {
          budget_.Step();
          const auto root = copies.expressions[j];
          origin_ = tree_.expressions[root.value].origin;
          // Snapshot the pre-throw operand, not a normal result or a cycle
          // temporary from the abstract exceptional parallel-copy schedule.
          Input(evaluation_.phis->copies[bundle.first + j].source, Node<AssignmentExpr>(root).value);
        }
        continue;
      }
      const auto& schedule = evaluation_.phis->bundles[i].schedule;
      if (copies.selected) {
        RequireMeaning(copies.expressions.empty() && copies.temporaries.empty() && schedule.steps.size() == 1, Error::kOperand);
        origin_ = tree_.expressions[copies.selected->value].origin;
        Input(std::get<analysis::ValueOperand>(schedule.steps[0].source), *copies.selected);
        continue;
      }
      RequireMeaning(copies.expressions.size() == schedule.steps.size(), Error::kOperand);
      for (std::size_t j = 0; j < copies.expressions.size(); ++j) {
        budget_.Step();
        const auto root = copies.expressions[j];
        const auto& assignment = Node<AssignmentExpr>(root);
        origin_ = tree_.expressions[root.value].origin;
        const auto& source = schedule.steps[j].source;
        if (const auto* temporary = std::get_if<CopyTempId>(&source)) {
          RequireMeaning(temporary->value < copies.temporaries.size() &&
              copies.temporaries[temporary->value].has_value(), Error::kOperand);
          LeafOrigin(assignment.value);
          RequireMeaning(Node<SymbolExpr>(assignment.value).symbol ==
              *copies.temporaries[temporary->value], Error::kOperand);
        } else Input(std::get<analysis::ValueOperand>(source), assignment.value);
      }
    }
  }
  void Arguments(const analysis::ValueOp& op, ArgumentRange arguments,
      std::uint32_t prefix, std::uint32_t count) {
    RequireMeaning(arguments.count == count && arguments.first <= tree_.arguments.size() &&
        count <= tree_.arguments.size() - arguments.first, Error::kOperand);
    for (std::uint32_t i = 0; i < count; ++i)
      Input(graph_.uses[op.first_use.value + prefix + i].operand, tree_.arguments[arguments.first + i]);
  }
  void Receiver(const analysis::ValueOp& op, const CallExpr& call,
      const analysis::ValueOperand& callee, const analysis::ValueOperand& receiver) {
    // Derive the actual reference from executable syntax, independently of the
    // builder's CallPlan. Its base must represent the suppressed receiver use.
    const auto resolved_callee = ResolveEvaluationOperand(evaluation_, callee);
    const auto resolved_receiver = ResolveEvaluationOperand(evaluation_, receiver);
    const auto* value = std::get_if<analysis::ValueId>(&resolved_callee);
    RequireMeaning(value != nullptr, Error::kOperand);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
    RequireMeaning(result != nullptr, Error::kOperand);
    const auto& property = graph_.ops[result->op.value];
    const auto& placement = evaluation_.operations[result->op.value];
    RequireMeaning((std::holds_alternative<analysis::ValueNamedRead>(property.operation) ||
        std::holds_alternative<analysis::ValuePropertyRead>(property.operation)) &&
        property.life == analysis::ValueLife::kLive &&
        ((property.block == op.block && property.order < op.order) ||
         (CrossPhaseInline(graph_, evaluation_, result->op) || CrossLogicalInline(graph_, evaluation_, result->op))) &&
        (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) &&
        placement.consumer == op.first_use, Error::kOperand);
    const auto& member = Node<MemberExpr>(call.callee);
    if (const auto* id = std::get_if<analysis::ValueId>(&resolved_receiver)) {
      const auto& definition = graph_.values[id->value];
      if (const auto* producer = std::get_if<analysis::OpResult>(&definition.origin)) {
        const auto& base = evaluation_.operations[producer->op.value];
        RequireMeaning((base.kind == EvaluationKind::kRoot && flow_.snapshots[producer->op.value].has_value()) ||
            ((base.kind == EvaluationKind::kInline || base.kind == EvaluationKind::kNestedLiteral) && base.consumer == property.first_use), Error::kOperand);
      } else if (std::holds_alternative<analysis::PhiDefinition>(definition.origin))
        // TreeFlow derives this symbol from the actual, exclusively owned edge
        // assignments. Same-block/no-copy corridor checks exclude a write to
        // the provisional phi location between reference creation and use.
        RequireMeaning(id->value < flow_.phis.size() && flow_.phis[id->value], Error::kOperand);
      else RequireMeaning(std::holds_alternative<analysis::EntryDefinition>(definition.origin), Error::kOperand);
    }
    const auto saved_origin = origin_;
    const auto saved_current = current_;
    origin_ = {property.origin.value};
    current_ = result->op;
    Input(receiver, member.base);
    origin_ = saved_origin;
    current_ = saved_current;
  }
  template <class T> const T& Node(ExprId id) const {
    RequireMeaning(id.value < tree_.expressions.size(), Error::kOperand);
    const auto* value = std::get_if<T>(&tree_.expressions[id.value].expression);
    RequireMeaning(value != nullptr, Error::kOperator);
    return *value;
  }
  template <class T> const T& Literal(ExprId id) const {
    const auto* value = std::get_if<T>(&Node<TreeLiteral>(id));
    RequireMeaning(value != nullptr, Error::kOperand);
    return *value;
  }
  void LeafOrigin(ExprId id) const {
    const auto origin = tree_.expressions[id.value].origin;
    // Identity lowering can combine provenance on its root leaf. The origin
    // pass independently proves membership of every such retained operation.
    RequireMeaning(origin == origin_ ||
        (!edge_input_ && flow_.expressions[current_.value] == id &&
         std::holds_alternative<CombinedOrigin>(tree_.origins[origin.value])), Error::kOrigin);
  }
  void Input(const analysis::ValueOperand& operand, ExprId expression) {
    budget_.Step();
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::ValueId>) {
        const auto& definition = graph_.values[value.value];
        if (const auto* entry = std::get_if<analysis::EntryDefinition>(&definition.origin)) {
          LeafOrigin(expression);
          if (const auto* parameter = std::get_if<analysis::ParameterEntry>(entry)) {
            RequireMeaning(parameter->index < tree_.parameters.size() &&
                Node<SymbolExpr>(expression).symbol == tree_.parameters[parameter->index].symbol, Error::kOperand);
          } else if (std::holds_alternative<analysis::ReceiverEntry>(*entry)) {
            RequireMeaning(Node<ThisExpr>(expression).owner == tree_.owner, Error::kOperand);
          } else throw Error::kOperand;
          return;
        }
        if (std::holds_alternative<analysis::PhiDefinition>(definition.origin)) {
          if (!evaluation_.logical_values.empty() && evaluation_.logical_values[value.value]) {
            RequireMeaning(!edge_input_ && flow_.expressions[evaluation_.logical_values[value.value]->value] == expression, Error::kOperand);
            return;
          }
          LeafOrigin(expression);
          RequireMeaning(value.value < flow_.phis.size() && flow_.phis[value.value] &&
              Node<SymbolExpr>(expression).symbol == *flow_.phis[value.value], Error::kOperand);
          return;
        }
        if (std::holds_alternative<analysis::CatchDefinition>(definition.origin)) {
          LeafOrigin(expression);
          RequireMeaning(value.value < flow_.catch_parameters.size() && flow_.catch_parameters[value.value] &&
              Node<SymbolExpr>(expression).symbol == *flow_.catch_parameters[value.value], Error::kOperand);
          return;
        }
        const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
        RequireMeaning(result != nullptr, Error::kOperand);
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kNestedLiteral) {
          RequireMeaning(flow_.expressions[result->op.value] == expression, Error::kOperand);
          return;
        }
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kLogicalLeft) {
          // Fresh flow selected the predicate/keep edge as one native LHS.
          // Both original observations must name that exact owned expression.
          RequireMeaning(flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else if (evaluation_.operations[result->op.value].kind == EvaluationKind::kPhiInput) {
          RequireMeaning(edge_input_ && flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else if (evaluation_.operations[result->op.value].kind == EvaluationKind::kInline) {
          RequireMeaning(!edge_input_ && flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else {
          LeafOrigin(expression);
          RequireMeaning(flow_.snapshots[result->op.value].has_value() &&
              Node<SymbolExpr>(expression).symbol == *flow_.snapshots[result->op.value], Error::kOperand);
        }
      } else if constexpr (std::is_same_v<T, analysis::IntegerLiteral>) {
        LeafOrigin(expression);
        RequireMeaning(Literal<IntegerLiteral>(expression).value == value.value, Error::kOperand);
      } else if constexpr (std::is_same_v<T, analysis::Float64Bits>) {
        LeafOrigin(expression);
        RequireMeaning(Literal<NumberLiteral>(expression).bits == value.value, Error::kOperand);
      } else {
        static_assert(std::is_same_v<T, analysis::SpecialValue>);
        LeafOrigin(expression);
        using S = analysis::SpecialValue;
        switch (value) {
          case S::kUndefined: static_cast<void>(Literal<UndefinedLiteral>(expression)); break;
          case S::kNull: static_cast<void>(Literal<NullLiteral>(expression)); break;
          case S::kTrue: RequireMeaning(Literal<BooleanLiteral>(expression).value, Error::kOperand); break;
          case S::kFalse: RequireMeaning(!Literal<BooleanLiteral>(expression).value, Error::kOperand); break;
          case S::kNaN:
            RequireMeaning(Literal<NumberLiteral>(expression).bits ==
                std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()), Error::kOperand); break;
          case S::kInfinity:
            RequireMeaning(Literal<NumberLiteral>(expression).bits ==
                std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()), Error::kOperand); break;
        }
      }
    }, ResolveEvaluationOperand(evaluation_, operand));
  }
  void BufferedLiteral(const TreeLiteral& expected, ExprId expression) {
    LeafOrigin(expression);
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      const auto& actual = Literal<T>(expression);
      if constexpr (std::is_same_v<T, NullLiteral>) return;
      else if constexpr (std::is_same_v<T, UndefinedLiteral>) throw Error::kConstant;
      else if constexpr (std::is_same_v<T, NumberLiteral>) RequireMeaning(actual.bits == value.bits, Error::kConstant);
      else if constexpr (std::is_same_v<T, TextId>)
        RequireMeaning(value.value < constants_.text_count && actual == value, Error::kConstant);
      else RequireMeaning(actual.value == value.value, Error::kConstant);
    }, expected);
  }
  void Named(const MemberKey& key, const ExpressionConstant& constant) const {
    const auto* name = std::get_if<NameId>(&key);
    const auto* expected = std::get_if<NameId>(&constant);
    RequireMeaning(name && expected && expected->value < constants_.name_count && *name == *expected, Error::kConstant);
  }
  const BinaryExpr& Compare(ExprId expression, analysis::BinaryOperator operation) const {
    const auto& binary = Node<BinaryExpr>(expression);
    RequireMeaning(binary.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return binary;
  }
  const LogicalExpr& Logical(ExprId expression, TreeLogical operation) const {
    const auto& logical = Node<LogicalExpr>(expression);
    RequireMeaning(logical.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return logical;
  }
  ExprId UnaryInput(ExprId expression, TreeUnary operation) const {
    const auto& unary = Node<UnaryExpr>(expression);
    RequireMeaning(unary.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return unary.operand;
  }
  void Unary(analysis::UnaryOperator operation, const analysis::ValueOperand& operand, ExprId expression) {
    using U = analysis::UnaryOperator;
    switch (operation) {
      case U::kNegate: Input(operand, UnaryInput(expression, TreeUnary::kNegate)); return;
      case U::kBitNot: Input(operand, UnaryInput(expression, TreeUnary::kBitNot)); return;
      case U::kLogicalNot: Input(operand, UnaryInput(expression, TreeUnary::kNot)); return;
      case U::kTypeOf: Input(operand, UnaryInput(expression, TreeUnary::kTypeof)); return;
      case U::kToNumber: Input(operand, UnaryInput(expression, TreeUnary::kPlus)); return;
      case U::kToBoolean:
        Input(operand, UnaryInput(UnaryInput(expression, TreeUnary::kNot), TreeUnary::kNot)); return;
      case U::kFalseOrZero:
      case U::kTrueOrNonzeroNumber:
        if (BooleanEvaluation(graph_, operand)) {
          Input(operand, operation == U::kFalseOrZero ? UnaryInput(expression, TreeUnary::kNot) : expression);
          return;
        }
        Predicate(operation, operand, expression); return;
      case U::kToNumeric: case U::kIncrement: case U::kDecrement:
        throw Error::kUnsupportedOperation;
    }
    throw Error::kUnsupportedOperation;
  }
  void Predicate(analysis::UnaryOperator operation, const analysis::ValueOperand& operand, ExprId expression) {
    using B = analysis::BinaryOperator;
    const bool positive = operation == analysis::UnaryOperator::kTrueOrNonzeroNumber;
    const auto& disjunction = Logical(expression, TreeLogical::kOr);
    const auto& boolean = Compare(disjunction.left, B::kStrictEqual);
    Input(operand, boolean.left);
    LeafOrigin(boolean.right);
    RequireMeaning(Literal<BooleanLiteral>(boolean.right).value == positive, Error::kOperand);
    if (!positive) {
      const auto& zero = Compare(disjunction.right, B::kStrictEqual);
      Input(operand, zero.left);
      LeafOrigin(zero.right);
      RequireMeaning(Literal<IntegerLiteral>(zero.right).value == 0, Error::kOperand);
      return;
    }
    const auto& conjunction = Logical(disjunction.right, TreeLogical::kAnd);
    const auto& type = Compare(conjunction.left, B::kStrictEqual);
    Input(operand, UnaryInput(type.left, TreeUnary::kTypeof));
    LeafOrigin(type.right);
    RequireMeaning(constants_.number_type_text && Literal<TextId>(type.right) == *constants_.number_type_text,
                   Error::kConstant);
    const auto& nonzero = Compare(conjunction.right, B::kStrictNotEqual);
    Input(operand, nonzero.left);
    LeafOrigin(nonzero.right);
    RequireMeaning(Literal<IntegerLiteral>(nonzero.right).value == 0, Error::kOperand);
  }

  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  const FunctionTree& tree_;
  const TreeFlow& flow_;
  ExpressionConstants constants_;
  TreeCheckBudget& budget_;
  analysis::OpId current_;
  TreeOriginId origin_;
  bool edge_input_ = false;
};
}  // namespace

void CheckTreeOperands(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    const TreeFlow& flow, ExpressionConstants constants, TreeCheckBudget& budget) {
  budget.Admit(1, sizeof(OperandCheck));
  OperandCheck(graph, evaluation, tree, flow, constants, budget).Run();
}
}  // namespace abcd::source
