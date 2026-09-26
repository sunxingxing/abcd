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
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      current_ = {static_cast<std::uint32_t>(i)};
      origin_ = {op.origin.value};
      if (std::holds_alternative<analysis::ValueJump>(op.operation)) continue;
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
        } else if constexpr (std::is_same_v<T, analysis::ValueString>) {
          const auto* text = std::get_if<TextId>(&constants_.operations[i]);
          RequireMeaning(text && text->value < constants_.text_count, Error::kConstant);
          RequireMeaning(Literal<TextId>(expression) == *text, Error::kConstant);
        } else {
          static_assert(std::is_same_v<T, analysis::ValueGuard> ||
                        std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueConstruct> ||
                        std::is_same_v<T, analysis::ValueArrayAllocation> ||
                        std::is_same_v<T, analysis::ValueNop> || std::is_same_v<T, analysis::ValueJump>);
          throw Error::kUnsupportedOperation;
        }
      }, op.operation);
    }
  }

 private:
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
        (flow_.expressions[current_.value] == id &&
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
        const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
        RequireMeaning(result != nullptr, Error::kOperand);
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kInline) {
          RequireMeaning(flow_.expressions[result->op.value] == expression, Error::kOperand);
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
};
}  // namespace

void CheckTreeOperands(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    const TreeFlow& flow, ExpressionConstants constants, TreeCheckBudget& budget) {
  budget.Admit(1, sizeof(OperandCheck));
  OperandCheck(graph, evaluation, tree, flow, constants, budget).Run();
}
}  // namespace abcd::source
