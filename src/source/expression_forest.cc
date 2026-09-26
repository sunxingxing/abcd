#include "expression_forest.h"

#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = ExpressionError;
void Require(bool condition, Error error) { if (!condition) throw error; }

class ForestBuilder {
 public:
  ForestBuilder(const analysis::ValueProgram& graph,
                const EvaluationPlan& evaluation, FunctionOwnerId owner,
                ExpressionConstants constants, analysis::SemanticBudget budget,
                BindingPlan bindings)
      : graph_(graph), evaluation_(evaluation), owner_(owner), constants_(constants),
        budget_(budget), bytes_(budget.bytes), work_(budget.work) {
    Require(bindings.allocated_bytes <= bytes_, Error::kResourceLimit);
    bytes_ -= bindings.allocated_bytes;
    Step(bindings.work);
    forest_.bindings = std::move(bindings);
  }

  ExpressionForest Build() {
    Require(constants_.operations.size() == graph_.ops.size(), Error::kConstant);
    Admit(1, sizeof(ExpressionForest));
    // Largest admitted lowering is TrueOrNonzeroNumber: three leaf reads,
    // three comparisons/type tests and two logical nodes. Reserve a generous
    // fixed per-op bound; each input use additionally needs at most one leaf.
    constexpr std::size_t kNodesPerOperation = 12;
    constexpr std::size_t kOriginPartsPerOperation = 2;
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(graph_.ops.size() <= kMaxId / kNodesPerOperation &&
            graph_.uses.size() <= kMaxId - graph_.ops.size() * kNodesPerOperation &&
            graph_.origins.size() <= kMaxId - graph_.ops.size(), Error::kResourceLimit);
    Reserve(forest_.expressions, graph_.uses.size() + graph_.ops.size() * kNodesPerOperation);
    Reserve(forest_.origins, graph_.origins.size() + graph_.ops.size());
    Reserve(forest_.origin_parts, graph_.ops.size() * kOriginPartsPerOperation);
    Reserve(forest_.operations, graph_.ops.size());
    forest_.operations.resize(graph_.ops.size());
    for (const auto& origin : graph_.origins) {
      Step();
      Require(origin.disposition != analysis::OriginDisposition::kCombined, Error::kOrigin);
      if (origin.disposition == analysis::OriginDisposition::kElidedPure)
        forest_.origins.emplace_back(ElidedOrigin{origin.site, origin.pc, origin.original});
      else
        forest_.origins.emplace_back(OriginalSite{origin.site, origin.pc, origin.original});
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      Require(op.origin.value < graph_.origins.size() &&
              graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained,
              Error::kOrigin);
      const TreeOriginId origin{op.origin.value};
      std::array<ExprId, 2> operands{};
      Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
      for (std::uint32_t j = 0; j < op.use_count; ++j) {
        Step();
        operands[j] = Operand(graph_.uses[op.first_use.value + j].operand, origin);
      }
      forest_.operations[i] = Lower(op, operands, origin, constants_.operations[i]);
    }
    forest_.allocated_bytes = budget_.bytes - bytes_;
    forest_.work = budget_.work - work_;
    return std::move(forest_);
  }

 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit);
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit);
    bytes_ -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T));
    values.reserve(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  ExprId Add(TreeExpression expression, TreeOriginId origin) {
    Step();
    Require(forest_.expressions.size() < forest_.expressions.capacity(), Error::kResourceLimit);
    const ExprId id{static_cast<std::uint32_t>(forest_.expressions.size())};
    forest_.expressions.push_back({std::move(expression), origin});
    return id;
  }
  ExprId Literal(TreeLiteral value, TreeOriginId origin) {
    return Add(std::move(value), origin);
  }
  ExprId Combine(ExprId expression, TreeOriginId origin) {
    Require(forest_.origins.size() < forest_.origins.capacity() &&
            forest_.origin_parts.capacity() - forest_.origin_parts.size() >= 2,
            Error::kResourceLimit);
    const auto first = static_cast<std::uint32_t>(forest_.origin_parts.size());
    forest_.origin_parts.push_back(forest_.expressions[expression.value].origin);
    forest_.origin_parts.push_back(origin);
    const TreeOriginId combined{static_cast<std::uint32_t>(forest_.origins.size())};
    forest_.origins.emplace_back(CombinedOrigin{first, 2});
    forest_.expressions[expression.value].origin = combined;
    return expression;
  }
  ExprId RepeatLeaf(ExprId expression) {
    // Predicate expansion may repeat only already evaluated bindings/literals.
    // Clone leaves, not IDs: final executable syntax must remain an owned tree.
    const auto node = forest_.expressions[expression.value];
    Require(std::holds_alternative<TreeLiteral>(node.expression) ||
            std::holds_alternative<SymbolExpr>(node.expression) ||
            std::holds_alternative<ThisExpr>(node.expression), Error::kOwnership);
    return Add(node.expression, node.origin);
  }
  ExprId Operand(const analysis::ValueOperand& operand, TreeOriginId origin) {
    return std::visit([&](const auto& value) -> ExprId {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::IntegerLiteral>)
        return Literal(IntegerLiteral{value.value}, origin);
      else if constexpr (std::is_same_v<T, analysis::Float64Bits>)
        return Literal(NumberLiteral{value.value}, origin);
      else if constexpr (std::is_same_v<T, analysis::SpecialValue>) {
        using S = analysis::SpecialValue;
        switch (value) {
          case S::kUndefined: return Literal(UndefinedLiteral{}, origin);
          case S::kNull: return Literal(NullLiteral{}, origin);
          case S::kTrue: return Literal(BooleanLiteral{true}, origin);
          case S::kFalse: return Literal(BooleanLiteral{false}, origin);
          case S::kNaN: return Literal(NumberLiteral{std::bit_cast<std::uint64_t>(
              std::numeric_limits<double>::quiet_NaN())}, origin);
          case S::kInfinity: return Literal(NumberLiteral{std::bit_cast<std::uint64_t>(
              std::numeric_limits<double>::infinity())}, origin);
        }
        throw Error::kUnsupportedOperation;
      } else {
        static_assert(std::is_same_v<T, analysis::ValueId>);
        Require(value.value < forest_.bindings.values.size(), Error::kBinding);
        const auto& binding = forest_.bindings.values[value.value];
        if (const auto* symbol = std::get_if<SymbolId>(&binding))
          return Add(SymbolExpr{*symbol}, origin);
        if (std::holds_alternative<ReceiverBinding>(binding))
          return Add(ThisExpr{owner_}, origin);
        const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value.value].origin);
        Require(result && result->op.value < forest_.operations.size() &&
                evaluation_.operations[result->op.value].kind == EvaluationKind::kInline &&
                forest_.operations[result->op.value].has_value(), Error::kUnavailableValue);
        return *forest_.operations[result->op.value];
      }
    }, operand);
  }
  ExprId Unary(analysis::UnaryOperator operation, ExprId input,
               const analysis::ValueOperand& original, TreeOriginId origin) {
    using U = analysis::UnaryOperator;
    using B = analysis::BinaryOperator;
    const auto unary = [&](TreeUnary kind, ExprId operand) {
      return Add(UnaryExpr{kind, operand}, origin);
    };
    switch (operation) {
      case U::kNegate: return unary(TreeUnary::kNegate, input);
      case U::kBitNot: return unary(TreeUnary::kBitNot, input);
      case U::kLogicalNot: return unary(TreeUnary::kNot, input);
      case U::kTypeOf: return unary(TreeUnary::kTypeof, input);
      case U::kToNumber: return unary(TreeUnary::kPlus, input);
      case U::kToBoolean: return unary(TreeUnary::kNot, unary(TreeUnary::kNot, input));
      case U::kFalseOrZero:
      case U::kTrueOrNonzeroNumber: {
        if (BooleanEvaluation(graph_, original))
          return operation == U::kFalseOrZero ? unary(TreeUnary::kNot, input) : Combine(input, origin);
        const auto repeated = RepeatLeaf(input);
        const auto boolean = Literal(BooleanLiteral{operation == U::kTrueOrNonzeroNumber}, origin);
        const auto left = Add(BinaryExpr{B::kStrictEqual, input, boolean}, origin);
        const auto zero = Literal(IntegerLiteral{0}, origin);
        if (operation == U::kFalseOrZero) {
          const auto right = Add(BinaryExpr{B::kStrictEqual, repeated, zero}, origin);
          return Add(LogicalExpr{TreeLogical::kOr, left, right}, origin);
        }
        Require(constants_.number_type_text && constants_.number_type_text->value < constants_.text_count,
                Error::kConstant);
        const auto third = RepeatLeaf(input);
        const auto type = unary(TreeUnary::kTypeof, repeated);
        const auto number = Literal(*constants_.number_type_text, origin);
        const auto is_number = Add(BinaryExpr{B::kStrictEqual, type, number}, origin);
        const auto nonzero = Add(BinaryExpr{B::kStrictNotEqual, third, zero}, origin);
        const auto right = Add(LogicalExpr{TreeLogical::kAnd, is_number, nonzero}, origin);
        return Add(LogicalExpr{TreeLogical::kOr, left, right}, origin);
      }
      // ++/-- require a Number-or-BigInt preserving lowering, not x +/- 1.
      case U::kToNumeric: case U::kIncrement: case U::kDecrement:
        throw Error::kUnsupportedOperation;
    }
    throw Error::kUnsupportedOperation;
  }
  std::optional<ExprId> Lower(const analysis::ValueOp& op,
      const std::array<ExprId, 2>& operands, TreeOriginId origin,
      const ExpressionConstant& constant) {
    return std::visit([&](const auto& operation) -> std::optional<ExprId> {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, analysis::ValueCopy>)
        return Combine(operands[0], origin);
      else if constexpr (std::is_same_v<T, analysis::ValueUnary>)
        return Unary(operation.op, operands[0], graph_.uses[op.first_use.value].operand, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueBranch>)
        return Unary(operation.predicate, operands[0], graph_.uses[op.first_use.value].operand, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueBinary>)
        return Add(BinaryExpr{operation.op, operands[0], operands[1]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueNamedRead> ||
                         std::is_same_v<T, analysis::ValueNamedWrite>) {
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        if constexpr (std::is_same_v<T, analysis::ValueNamedRead>)
          return Add(MemberExpr{operands[0], *name}, origin);
        else
          return Add(AssignmentExpr{MemberTarget{operands[0], *name}, operands[1]}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>)
        return Add(MemberExpr{operands[0], ComputedKey{operands[1]}}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueString>) {
        const auto* text = std::get_if<TextId>(&constant);
        Require(text && text->value < constants_.text_count, Error::kConstant);
        return Literal(*text, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueReturn>)
        return operands[0];
      else if constexpr (std::is_same_v<T, analysis::ValueJump>)
        return {};
      else {
        static_assert(std::is_same_v<T, analysis::ValueGuard> ||
                      std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueConstruct> ||
                      std::is_same_v<T, analysis::ValueArrayAllocation> ||
                      std::is_same_v<T, analysis::ValueNop>);
        throw Error::kUnsupportedOperation;
      }
    }, op.operation);
  }

  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  FunctionOwnerId owner_;
  ExpressionConstants constants_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  ExpressionForest forest_;
};
}  // namespace

std::variant<ExpressionForest, ExpressionError> BuildExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner,
    ExpressionConstants constants, analysis::SemanticBudget budget) {
  auto bindings = BuildBindingPlan(graph, evaluation, parameter_count, budget);
  if (const auto* error = std::get_if<BindingError>(&bindings)) {
    if (*error == BindingError::kResourceLimit) return Error::kResourceLimit;
    if (*error == BindingError::kWorkLimit) return Error::kWorkLimit;
    return Error::kBinding;
  }
  try {
    return ForestBuilder(graph, evaluation, owner, constants, budget,
                         std::get<BindingPlan>(std::move(bindings))).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
