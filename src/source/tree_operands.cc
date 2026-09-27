#include "tree_correspondence.h"

#include <bit>
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
    std::size_t next_function = 0;
    std::size_t next_module = 0;
    std::vector<std::optional<std::size_t>> module_aliases;
    if (!evaluation_.modules.empty()) {
      budget_.Array(module_aliases, tree_.symbols.size());
      for (std::size_t i = 0; i < tree_.modules.size(); ++i) {
        budget_.Step();
        const auto symbol = tree_.modules[i].symbol;
        RequireMeaning(symbol.value < module_aliases.size() && !module_aliases[symbol.value], Error::kOperand);
        module_aliases[symbol.value] = i;
      }
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      current_ = {static_cast<std::uint32_t>(i)};
      origin_ = {op.origin.value};
      if (std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation)) continue;
      RequireMeaning(flow_.expressions[i].has_value(), Error::kOperand);
      const auto expression = *flow_.expressions[i];
      const auto input = [&](std::uint32_t ordinal) -> const analysis::ValueOperand& {
        return graph_.uses[op.first_use.value + ordinal].operand;
      };
      std::visit([&](const auto& operation) {
        using T = std::decay_t<decltype(operation)>;
        if constexpr (std::is_same_v<T, analysis::ValueCopy> ||
                      std::is_same_v<T, analysis::ValueReturn>) {
          Input(input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueUnary>) {
          Unary(operation.op, input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueBranch>) {
          Unary(operation.predicate, input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueBinary>) {
          const auto& binary = Node<BinaryExpr>(expression);
          RequireMeaning(binary.operation == operation.op, Error::kOperator);
          Input(input(0), binary.left); Input(input(1), binary.right);
        } else if constexpr (std::is_same_v<T, analysis::ValueModuleRead>) {
          RequireMeaning(next_module < evaluation_.modules.size(), Error::kOperand);
          const auto& module = evaluation_.modules[next_module++];
          const auto symbol = Node<SymbolExpr>(expression).symbol;
          RequireMeaning(module.pair.read == current_ && module.read == operation &&
              symbol.value < module_aliases.size() && module_aliases[symbol.value] &&
              tree_.modules[*module_aliases[symbol.value]].target == module.target, Error::kOperand);
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
        } else if constexpr (std::is_same_v<T, analysis::ValueOwnWrite>) {
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
        } else if constexpr (std::is_same_v<T, analysis::ValueConstruct>) {
          const auto& construct = Node<ConstructExpr>(expression);
          Input(input(0), construct.constructor);
          constexpr std::uint32_t kConstructorPrefix = 1;
          Arguments(op, construct.arguments, kConstructorPrefix, operation.arguments);
        } else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation>) {
          static_cast<void>(Node<EmptyArrayExpr>(expression));
        } else if constexpr (std::is_same_v<T, analysis::ValueObjectAllocation>) {
          if (operation.literal) {
            const auto& object = Node<ObjectExpr>(expression);
            const auto* expected = std::get_if<ObjectConstant>(&constants_.operations[i]);
            RequireMeaning(expected && expected->first <= constants_.properties.size() &&
                expected->count <= constants_.properties.size() - expected->first &&
                i < flow_.buffered_properties.size() &&
                flow_.buffered_properties[i] == expected->count &&
                object.properties.count >= expected->count, Error::kConstant);
            for (std::uint32_t p = 0; p < expected->count; ++p) {
              budget_.Step();
              const auto& source = constants_.properties[expected->first + p];
              const auto& actual = tree_.properties[object.properties.first + p];
              RequireMeaning(source.key.value < constants_.name_count && actual.key == source.key, Error::kConstant);
              BufferedLiteral(source.value, actual.value);
            }
            return;
          }
          RequireMeaning(std::holds_alternative<EmptyObjectExpr>(tree_.expressions[expression.value].expression) ||
              std::holds_alternative<ObjectExpr>(tree_.expressions[expression.value].expression), Error::kOperator);
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
        } else {
          static_assert(std::is_same_v<T, analysis::ValueModuleWrite> ||
                        std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                        std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                        std::is_same_v<T, analysis::ValueNop> || std::is_same_v<T, analysis::ValueJump>);
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
      const auto& schedule = evaluation_.phis->bundles[i].schedule;
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
    const auto* value = std::get_if<analysis::ValueId>(&callee);
    RequireMeaning(value != nullptr, Error::kOperand);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
    RequireMeaning(result != nullptr, Error::kOperand);
    const auto& property = graph_.ops[result->op.value];
    const auto& placement = evaluation_.operations[result->op.value];
    RequireMeaning((std::holds_alternative<analysis::ValueNamedRead>(property.operation) ||
        std::holds_alternative<analysis::ValuePropertyRead>(property.operation)) &&
        property.life == analysis::ValueLife::kLive && property.block == op.block &&
        property.order < op.order && placement.kind == EvaluationKind::kInline &&
        placement.consumer == op.first_use, Error::kOperand);
    const auto& member = Node<MemberExpr>(call.callee);
    if (const auto* id = std::get_if<analysis::ValueId>(&receiver)) {
      const auto& definition = graph_.values[id->value];
      if (const auto* producer = std::get_if<analysis::OpResult>(&definition.origin))
        RequireMeaning(evaluation_.operations[producer->op.value].kind == EvaluationKind::kRoot &&
            flow_.snapshots[producer->op.value].has_value(), Error::kOperand);
      else if (std::holds_alternative<analysis::PhiDefinition>(definition.origin))
        // TreeFlow derives this symbol from the actual, exclusively owned edge
        // assignments. Same-block property/call checks above exclude a write
        // to the provisional phi location between reference creation and use.
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
          LeafOrigin(expression);
          RequireMeaning(value.value < flow_.phis.size() && flow_.phis[value.value] &&
              Node<SymbolExpr>(expression).symbol == *flow_.phis[value.value], Error::kOperand);
          return;
        }
        const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
        RequireMeaning(result != nullptr, Error::kOperand);
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kInline) {
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
    }, operand);
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
