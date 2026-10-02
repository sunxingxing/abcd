#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <iostream>
#include <sstream>

// Printer/AST isolation fixture. Declared ValueProgram domains are test inputs,
// not evidence that arbitrary ABC arithmetic passes the separate input proof.
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
class Fixture {
 public:
  Fixture() {
    for (unsigned i = 0; i < 3; ++i) graph.values.push_back({EntryDefinition{ParameterEntry{i}}, ValueKinds::Language(), {}});
  }
  ValueId Binary(BinaryOperator operation, ValueId left, ValueId right) {
    return Add(ValueBinary{operation}, {{OperandRole::kLeft, left}, {OperandRole::kRight, right}});
  }
  ValueId Unary(UnaryOperator operation, ValueId input) { return Add(ValueUnary{operation}, {{OperandRole::kInput, input}}); }
  ValueId Invoke(ValueId callee, ValueId argument) {
    return Add(ValueInvoke{InvokeReceiver::kUndefined, 1}, {{OperandRole::kCallee, callee}, {OperandRole::kArgument, argument}});
  }
  std::string Print(ValueId result, bool logical = false, bool awaited = false) {
    Add(ValueReturn{}, {{OperandRole::kReturnValue, result}}, false);
    graph.blocks.push_back({{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0});
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    FunctionSyntax syntax{}; syntax.owner = {0}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"a", u"b", u"c"};
    for (unsigned i = 0; i < 3; ++i) syntax.parameters.push_back({NameId{i}, NameEvidence::kDebugRange});
    std::vector<ExpressionConstant> constants(graph.ops.size());
    const ExpressionConstants resolved{constants, syntax.names.size()};
    const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
    test::Check(std::holds_alternative<ConstructedFunction>(built));
    auto tree = std::get<ConstructedFunction>(built).tree;
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    if (logical) {
      // Source-AST grammar fixtures only: these nodes are not asserted to be
      // equivalent to the bitwise IR scaffold verified above.
      for (auto& expression : tree.expressions) if (const auto* binary = std::get_if<BinaryExpr>(&expression.expression)) {
        const auto original = *binary;
        const auto kind = original.operation == BinaryOperator::kBitAnd ? TreeLogical::kAnd :
            original.operation == BinaryOperator::kBitOr ? TreeLogical::kOr : TreeLogical::kNullish;
        test::Check(original.operation == BinaryOperator::kBitAnd || original.operation == BinaryOperator::kBitOr ||
            original.operation == BinaryOperator::kBitXor);
        expression.expression = LogicalExpr{kind, original.left, original.right};
      }
    }
    if (awaited) {
      // Isolated async source-AST grammar, not an imported coroutine proof.
      tree.callable = CallableKind::kAsyncFunction;
      for (auto& statement : tree.statements) if (auto* returned = std::get_if<ReturnStmt>(&statement.statement); returned && returned->value) {
        const auto operand = *returned->value;
        returned->value = ExprId{static_cast<std::uint32_t>(tree.expressions.size())};
        tree.expressions.push_back({AwaitExpr{operand}, tree.expressions[operand.value].origin});
      }
    }
    std::ostringstream out;
    const auto printed = PrintFunctionTree(tree, out);
    test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto& positions = std::get<PrintedFunction>(printed);
    work_ = positions.work;
    const auto text = out.str();
    for (const auto range : positions.expressions)
      test::Check(range.begin <= range.end && range.end <= text.size());
    return text;
  }
  std::uint64_t Work() const { return work_; }
 private:
  std::uint64_t work_ = 0;
  ValueId Add(ValueOperation operation, std::initializer_list<std::pair<OperandRole, ValueId>> inputs, bool result = true) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    const ValueId value{static_cast<std::uint32_t>(graph.values.size())};
    if (result) graph.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    graph.origins.push_back({{id.value}, {id.value}, {}});
    const auto effect = result ? LanguageEffect::kUserCode : LanguageEffect::kNone;
    graph.ops.push_back({operation, {0}, id.value, {static_cast<std::uint32_t>(graph.uses.size())},
        static_cast<std::uint32_t>(inputs.size()), result ? std::optional{value} : std::nullopt,
        effect, effect, result ? ExceptionalCompletion::kMayThrow : ExceptionalCompletion::kImpossible, {id.value}});
    for (const auto& [role, input] : inputs) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse occurrence{id, role, ReadPurpose::kOperand, input, {}, {}, {}, graph.values[input.value].first_use};
      if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
      graph.values[input.value].first_use = use; graph.uses.push_back(occurrence);
    }
    return value;
  }
  ValueProgram graph;
};
}
std::string Case(unsigned index) {
  Fixture f; ValueId value;
  switch (index) {
    case 0: value = f.Binary(BinaryOperator::kSubtract, {0}, f.Binary(BinaryOperator::kSubtract, {1}, {2})); break;
    case 1: value = f.Binary(BinaryOperator::kSubtract, f.Binary(BinaryOperator::kSubtract, {0}, {1}), {2}); break;
    case 2: value = f.Binary(BinaryOperator::kDivide, {0}, f.Binary(BinaryOperator::kDivide, {1}, {2})); break;
    case 3: value = f.Binary(BinaryOperator::kDivide, f.Binary(BinaryOperator::kDivide, {0}, {1}), {2}); break;
    case 4: value = f.Binary(BinaryOperator::kExponent, f.Binary(BinaryOperator::kExponent, {0}, {1}), {2}); break;
    case 5: value = f.Binary(BinaryOperator::kExponent, {0}, f.Binary(BinaryOperator::kExponent, {1}, {2})); break;
    case 6: value = f.Binary(BinaryOperator::kExponent, f.Unary(UnaryOperator::kNegate, {0}), {1}); break;
    case 7: value = f.Unary(UnaryOperator::kNegate, f.Binary(BinaryOperator::kExponent, {0}, {1})); break;
    case 8: value = f.Binary(BinaryOperator::kAdd, {0}, f.Binary(BinaryOperator::kMultiply, {1}, {2})); break;
    case 9: value = f.Binary(BinaryOperator::kMultiply, f.Binary(BinaryOperator::kAdd, {0}, {1}), {2}); break;
    case 10: value = f.Binary(BinaryOperator::kAdd, {0}, f.Binary(BinaryOperator::kAdd, {1}, {2})); break;
    case 11: value = f.Unary(UnaryOperator::kNegate, f.Unary(UnaryOperator::kNegate, {0})); break;
    case 12: value = f.Unary(UnaryOperator::kBitNot, f.Binary(BinaryOperator::kBitAnd, {0}, {1})); break;
    case 13: value = f.Unary(UnaryOperator::kLogicalNot, f.Binary(BinaryOperator::kInstanceOf, {0}, {1})); break;
    case 14: value = f.Binary(BinaryOperator::kEqual, f.Binary(BinaryOperator::kLess, {0}, {1}), {2}); break;
    case 15: value = f.Binary(BinaryOperator::kLess, {0}, f.Binary(BinaryOperator::kEqual, {1}, {2})); break;
    case 16: value = f.Binary(BinaryOperator::kExponent, {0}, f.Unary(UnaryOperator::kNegate, {1})); break;
    case 17: value = f.Unary(UnaryOperator::kTypeOf, f.Binary(BinaryOperator::kAdd, {0}, {1})); break;
    case 18: value = f.Binary(BinaryOperator::kBitAnd, {0}, f.Binary(BinaryOperator::kBitOr, {1}, {2})); break;
    case 19: value = f.Binary(BinaryOperator::kBitOr, f.Binary(BinaryOperator::kBitAnd, {0}, {1}), {2}); break;
    case 20: value = f.Binary(BinaryOperator::kBitXor, {0}, f.Binary(BinaryOperator::kBitAnd, {1}, {2})); break;
    case 21: value = f.Binary(BinaryOperator::kBitAnd, f.Binary(BinaryOperator::kBitXor, {0}, {1}), {2}); break;
    case 22: value = f.Binary(BinaryOperator::kBitOr, {0}, f.Binary(BinaryOperator::kBitAnd, {1}, {2})); break;
    case 23: value = f.Binary(BinaryOperator::kBitXor, f.Binary(BinaryOperator::kBitOr, {0}, {1}), {2}); break;
    case 24: value = f.Binary(BinaryOperator::kBitAnd, {0}, f.Binary(BinaryOperator::kBitOr, {1}, {2})); break;
    case 25: value = f.Binary(BinaryOperator::kBitXor, {0}, f.Binary(BinaryOperator::kBitXor, {1}, {2})); break;
    case 26: value = f.Binary(BinaryOperator::kBitXor, f.Binary(BinaryOperator::kBitXor, {0}, {1}), {2}); break;
    case 27: value = f.Binary(BinaryOperator::kBitOr, f.Binary(BinaryOperator::kBitAnd, {0}, {1}), {2}); break;
    case 28: value = f.Binary(BinaryOperator::kAdd, {0}, f.Binary(BinaryOperator::kMultiply, {1}, {2})); break;
    case 29: value = f.Invoke(f.Binary(BinaryOperator::kBitOr, {0}, {1}), {2}); break;
    default: test::Check(false);
  }
  return f.Print(value, (index >= 20 && index <= 27) || index == 29, index == 28);
}
int main(int argc, char** argv) {
  if (argc == 3) {
    test::Check(std::string_view(argv[1]) == "--source");
    std::cout << Case(static_cast<unsigned>(std::stoul(argv[2]))); return 0;
  }
  constexpr std::array<std::string_view, 30> expected{
    "a - (b - c)", "a - b - c", "a / (b / c)", "a / b / c",
    "(a ** b) ** c", "a ** b ** c", "(-a) ** b", "-(a ** b)",
    "a + b * c", "(a + b) * c", "a + (b + c)", "-(-a)",
    "~(a & b)", "!(a instanceof b)", "a < b == c", "a < (b == c)",
    "a ** -b", "typeof (0, a + b)", "a & (b | c)", "a & b | c",
    "a ?? (b && c)", "(a ?? b) && c", "a || b && c", "(a || b) ?? c",
    "a && (b || c)", "a ?? (b ?? c)", "a ?? b ?? c", "a && b || c",
    "(await (a + b * c))", "(0, (a || b))(c)"};
  for (unsigned i = 0; i < expected.size(); ++i)
    test::Check(Case(i).find(std::string("return ") + std::string(expected[i]) + ";") != std::string::npos);
  for (const bool right : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U, 804U}) {
      Fixture fixture; ValueId value{2};
      for (unsigned i = 0; i < count; ++i) value = right ?
          fixture.Binary(BinaryOperator::kSubtract, {0}, value) :
          fixture.Binary(BinaryOperator::kSubtract, value, {0});
      fixture.Print(value);
      if (count > 201) test::Check(fixture.Work() < 3 * previous);
      previous = fixture.Work();
    }
  }
}
