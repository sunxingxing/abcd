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
struct ChoiceFixture {
  bool consequent;
  bool alternate;
  unsigned depth;
  unsigned context;
};
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
  ValueId Construct(ValueId callee, ValueId argument) {
    return Add(ValueConstruct{1}, {{OperandRole::kConstructor, callee}, {OperandRole::kArgument, argument}});
  }
  ValueId Read(ValueId base, unsigned name) {
    return Add(ValueNamedRead{{name}}, {{OperandRole::kBase, base}});
  }
  ValueId MemberCall(ValueId base, ValueId argument) {
    const auto callee = Read(base, 4);
    return Add(ValueInvoke{InvokeReceiver::kExplicit, 1}, {{OperandRole::kCallee, callee},
        {OperandRole::kReceiver, base}, {OperandRole::kArgument, argument}});
  }
  std::string Print(ValueId result, bool logical = false, bool awaited = false, bool numeric_base = false,
      bool predicate = false, bool selected = false, std::optional<ChoiceFixture> choice = {}) {
    Add(ValueReturn{}, {{OperandRole::kReturnValue, result}}, false);
    graph.blocks.push_back({{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0});
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    FunctionSyntax syntax{}; syntax.owner = {0}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"a", u"b", u"c", u"ctor", u"fn", u"value"};
    for (unsigned i = 0; i < 3; ++i) syntax.parameters.push_back({NameId{i}, NameEvidence::kDebugRange});
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i)
      if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value};
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
    if (numeric_base) {
      // Isolated lexical grammar fixture: an integer base needs grouping before
      // a dot, independently of the original parameter's source spelling.
      for (auto& expression : tree.expressions)
        if (const auto* symbol = std::get_if<SymbolExpr>(&expression.expression); symbol && symbol->symbol.value == 0)
          expression.expression = TreeLiteral{source::IntegerLiteral{1}};
    }
    if (selected) {
      // Isolate conditional branch observation as well as predicate observation.
      // Both branches contain conversions, with getters evaluated only on the
      // selected branch. These syntax nodes make no imported-IR proof claim.
      for (auto& statement : tree.statements) if (auto* returned = std::get_if<ReturnStmt>(&statement.statement); returned && returned->value) {
        const auto condition = *returned->value;
        const auto origin = tree.expressions[condition.value].origin;
        const auto add = [&](TreeExpression expression) {
          const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
          tree.expressions.push_back({std::move(expression), origin}); return id;
        };
        const auto branch = [&](unsigned parameter) {
          const auto base = add(SymbolExpr{tree.parameters[parameter].symbol});
          const auto member = add(MemberExpr{base, NameId{5}});
          return add(UnaryExpr{TreeUnary::kNot, add(UnaryExpr{TreeUnary::kNot, member})});
        };
        const auto yes = branch(1), no = branch(2);
        returned->value = add(ConditionalExpr{condition, yes, no});
      }
    }
    if (choice) {
      // Isolate language AST Boolean choices, including value consumers that
      // must preserve a Boolean rather than only the condition's truthiness.
      for (auto& statement : tree.statements) if (auto* returned = std::get_if<ReturnStmt>(&statement.statement); returned && returned->value) {
        auto value = *returned->value;
        const auto origin = tree.expressions[value.value].origin;
        const auto add = [&](TreeExpression expression) {
          const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
          tree.expressions.push_back({std::move(expression), origin}); return id;
        };
        const auto right = [&] { return add(MemberExpr{add(SymbolExpr{tree.parameters[1].symbol}), NameId{5}}); };
        if (choice->context >= 8) {
          const auto boolean = [&](ExprId input) {
            return add(UnaryExpr{TreeUnary::kNot, add(UnaryExpr{TreeUnary::kNot, input})});
          };
          const auto left = boolean(value), rhs = boolean(right());
          value = add(LogicalExpr{choice->context == 8 ? TreeLogical::kNullish :
              choice->context == 9 ? TreeLogical::kAnd : TreeLogical::kOr, left, rhs});
        }
        for (unsigned i = 0; i < choice->depth; ++i) {
          const auto yes = add(TreeLiteral{BooleanLiteral{choice->consequent}});
          const auto no = add(TreeLiteral{BooleanLiteral{choice->alternate}});
          value = add(ConditionalExpr{value, yes, no});
        }
        switch (choice->context) {
          case 0: case 1: case 8: case 9: case 10: break;
          case 2: value = add(LogicalExpr{TreeLogical::kOr, value, right()}); break;
          case 3: value = add(LogicalExpr{TreeLogical::kNullish, value, right()}); break;
          case 4: value = add(UnaryExpr{TreeUnary::kNot, value}); break;
          case 5: value = add(BinaryExpr{BinaryOperator::kAdd, value, right()}); break;
          case 6: value = add(BinaryExpr{BinaryOperator::kExponent, value, right()}); break;
          case 7: value = add(BinaryExpr{BinaryOperator::kStrictEqual, value, right()}); break;
          default: test::Check(false);
        }
        returned->value = value;
      }
    }
    if (predicate) {
      // Printer isolation: a native condition observes truthiness, whereas
      // returns, assignments and nullish selections still observe exact values.
      for (auto& statement : tree.statements) if (auto* returned = std::get_if<ReturnStmt>(&statement.statement); returned && returned->value) {
        const auto condition = *returned->value;
        const auto origin = tree.expressions[condition.value].origin;
        const ExprId yes{static_cast<std::uint32_t>(tree.expressions.size())};
        tree.expressions.push_back({TreeLiteral{BooleanLiteral{true}}, origin});
        const ExprId no{static_cast<std::uint32_t>(tree.expressions.size())};
        tree.expressions.push_back({TreeLiteral{BooleanLiteral{false}}, origin});
        returned->value = ExprId{static_cast<std::uint32_t>(tree.expressions.size())};
        tree.expressions.push_back({ConditionalExpr{condition, yes, no}, origin});
      }
    }
    if (choice && choice->context == 1) {
      // A real IfStmt supplies truthiness observation. Its independent return
      // literals also distinguish exact-value printing from branch printing.
      const auto count = tree.statements.size();
      for (std::size_t i = 0; i < count; ++i) {
        const auto* returned = std::get_if<ReturnStmt>(&tree.statements[i].statement);
        if (!returned || !returned->value) continue;
        const auto condition = *returned->value;
        const auto origin = tree.statements[i].origin;
        const auto arm = [&](bool value) {
          const ExprId expression{static_cast<std::uint32_t>(tree.expressions.size())};
          tree.expressions.push_back({TreeLiteral{BooleanLiteral{value}}, origin});
          const StmtId statement{static_cast<std::uint32_t>(tree.statements.size())};
          tree.statements.push_back({ReturnStmt{expression}, origin});
          const auto first = static_cast<std::uint32_t>(tree.children.size());
          tree.children.push_back(statement);
          const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
          tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
          const StmtId block{static_cast<std::uint32_t>(tree.statements.size())};
          tree.statements.push_back({BlockStmt{scope, first, 1}, origin}); return block;
        };
        const auto yes = arm(true), no = arm(false);
        tree.statements[i].statement = IfStmt{condition, yes, no};
      }
    }
    std::ostringstream out;
    const auto printed = PrintFunctionTree(tree, out);
    test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto& positions = std::get<PrintedFunction>(printed);
    work_ = positions.work;
    const auto text = out.str();
    for (std::size_t i = 0; i < positions.expressions.size(); ++i) {
      const auto range = positions.expressions[i];
      test::Check(range.begin <= range.end && range.end <= text.size());
      if (std::holds_alternative<UnaryExpr>(tree.expressions[i].expression) ||
          std::holds_alternative<ConditionalExpr>(tree.expressions[i].expression))
        test::Check(range.begin < range.end);
      if (const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[i].expression);
          literal && std::holds_alternative<BooleanLiteral>(*literal))
        test::Check(range.begin < range.end);
    }
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
    case 30: value = f.Construct({0}, {1}); break;
    case 31: value = f.Construct(f.Invoke({0}, {1}), {2}); break;
    case 32: value = f.Construct(f.Read({0}, 3), {1}); break;
    case 33: value = f.Construct(f.Read(f.Invoke({0}, {1}), 3), {2}); break;
    case 34: value = f.MemberCall(f.Invoke({0}, {1}), {2}); break;
    case 35: value = f.Read(f.Invoke({0}, {1}), 5); break;
    case 36: value = f.Read(f.Construct({0}, {1}), 5); break;
    case 37: value = f.Construct(f.Construct({0}, {1}), {2}); break;
    case 38: value = f.Binary(BinaryOperator::kAdd, f.Construct({0}, {1}), {2}); break;
    case 39: value = f.Invoke(f.Read({0}, 4), {1}); break;
    case 40: value = f.Read({0}, 5); break;
    case 41: value = f.Read(f.Unary(UnaryOperator::kNegate, {0}), 5); break;
    case 42: value = f.Construct(f.Binary(BinaryOperator::kBitOr, {0}, {1}), {2}); break;
    case 43: value = f.Construct(f.Read(f.Read(f.Invoke({0}, {1}), 3), 3), {2}); break;
    default: test::Check(false);
  }
  return f.Print(value, (index >= 20 && index <= 27) || index == 29 || index == 42, index == 28, index == 40);
}
std::string PostfixChain(unsigned count, std::uint64_t* work = nullptr) {
  Fixture fixture;
  auto constructor = fixture.Invoke({0}, {1});
  for (unsigned i = 0; i < count; ++i) constructor = fixture.Read(constructor, 3);
  const auto source = fixture.Print(fixture.Construct(constructor, {2}));
  if (work) *work = fixture.Work();
  return source;
}
std::string BooleanContext(unsigned index, unsigned count = 2, std::uint64_t* work = nullptr) {
  Fixture f;
  const auto boolean = [&](ValueId input) { return f.Unary(UnaryOperator::kToBoolean, input); };
  auto value = f.Read({0}, 5);
  bool logical = false, predicate = false, selected = false;
  switch (index) {
    case 0: case 1:
      for (unsigned i = 0; i < count; ++i) value = f.Unary(UnaryOperator::kLogicalNot, value);
      predicate = index == 0; break;
    case 2: case 3: case 4: case 5:
      value = boolean(value);
      value = f.Binary(index == 4 ? BinaryOperator::kBitAnd : index == 5 ? BinaryOperator::kBitXor : BinaryOperator::kBitOr,
          value, boolean(f.Read({1}, 5)));
      logical = true; predicate = index != 3; break;
    case 6: value = f.Unary(UnaryOperator::kLogicalNot, boolean(value)); break;
    case 7:
      value = boolean(f.Binary(BinaryOperator::kBitOr, boolean(value), boolean(f.Read({1}, 5))));
      logical = true; predicate = true; break;
    case 8: value = boolean(f.Binary(BinaryOperator::kLess, value, f.Read({1}, 5))); predicate = true; break;
    case 9: value = f.Unary(UnaryOperator::kBitNot, boolean(value)); break;
    case 10:
      value = f.Binary(BinaryOperator::kBitAnd,
          boolean(f.Binary(BinaryOperator::kBitOr, value, f.Read({1}, 5))), boolean(f.Read({2}, 5)));
      logical = true; predicate = true; break;
    case 11:
      value = f.Unary(UnaryOperator::kLogicalNot, boolean(f.Binary(BinaryOperator::kBitXor, value, f.Read({1}, 5))));
      logical = true; break;
    case 12: case 13: value = boolean(value); selected = true; predicate = index == 13; break;
    default: test::Check(false);
  }
  auto source = f.Print(value, logical, false, false, predicate, selected);
  if (work) *work = f.Work();
  return source;
}
std::string BooleanChoice(unsigned index, unsigned depth = 1, std::uint64_t* work = nullptr) {
  test::Check(index < 44 && depth > 0);
  Fixture fixture;
  const auto condition = fixture.Read({0}, 5);
  const auto form = index % 4, context = index / 4;
  const auto source = fixture.Print(condition, false, false, false, false, false,
      ChoiceFixture{form != 1 && form != 3, form != 0 && form != 3, depth, context});
  if (work) *work = fixture.Work();
  return source;
}
int main(int argc, char** argv) {
  if (argc == 4) {
    if (std::string_view(argv[1]) == "--boolean-choice")
      std::cout << BooleanChoice(static_cast<unsigned>(std::stoul(argv[2])), static_cast<unsigned>(std::stoul(argv[3])));
    else {
      test::Check(std::string_view(argv[1]) == "--boolean-chain");
      std::cout << BooleanContext(static_cast<unsigned>(std::stoul(argv[2])), static_cast<unsigned>(std::stoul(argv[3])));
    }
    return 0;
  }
  if (argc == 3) {
    const auto count = static_cast<unsigned>(std::stoul(argv[2]));
    if (std::string_view(argv[1]) == "--postfix-chain") std::cout << PostfixChain(count);
    else if (std::string_view(argv[1]) == "--boolean-context") std::cout << BooleanContext(count);
    else { test::Check(std::string_view(argv[1]) == "--source"); std::cout << Case(count); }
    return 0;
  }
  constexpr std::array<std::string_view, 14> boolean_contexts{
      "return !!a.value;", "return !!a.value;", "return !!(a.value || b.value);",
      "return !!a.value || !!b.value;", "return !!(a.value && b.value);",
      "return !!(!!a.value ?? !!b.value);", "return !a.value;", "return !!(a.value || b.value);",
      "return !!(a.value < b.value);", "return ~!!a.value;",
      "return !!((a.value || b.value) && c.value);", "return !(a.value ?? b.value);",
      "return (a.value ? !!b.value : !!c.value);", "(a.value ? b.value : c.value)"};
  for (unsigned i = 0; i < boolean_contexts.size(); ++i)
    test::Check(BooleanContext(i).find(boolean_contexts[i]) != std::string::npos);
  for (const unsigned observation : {0U, 1U}) {
    std::uint64_t previous = 0;
    for (const unsigned count : {1U, 2U, 200U, 201U, 402U, 804U}) {
      std::uint64_t work = 0;
      const auto text = BooleanContext(observation, count, &work);
      test::Check(text.find(count % 2 ? "!a.value" : "!!a.value") != std::string::npos);
      if (count > 201) test::Check(work < 3 * previous);
      previous = work;
    }
  }
  for (unsigned index = 0; index < 44; ++index) {
    std::uint64_t previous = 0;
    for (const unsigned depth : {1U, 2U, 200U, 201U, 402U, 804U}) {
      std::uint64_t work = 0;
      const auto text = BooleanChoice(index, depth, &work);
      test::Check(text.find(" ? ") == std::string::npos);
      if (depth > 201) test::Check(work < 3 * previous);
      previous = work;
    }
  }
  constexpr std::array<std::string_view, 30> expected{
    "a - (b - c)", "a - b - c", "a / (b / c)", "a / b / c",
    "(a ** b) ** c", "a ** b ** c", "(-a) ** b", "-(a ** b)",
    "a + b * c", "(a + b) * c", "a + (b + c)", "-(-a)",
    "~(a & b)", "!(a instanceof b)", "a < b == c", "a < (b == c)",
    "a ** -b", "typeof (0, a + b)", "a & (b | c)", "a & b | c",
    "a ?? (b && c)", "(a ?? b) && c", "a || b && c", "(a || b) ?? c",
    "a && (b || c)", "a ?? (b ?? c)", "a ?? b ?? c", "a && b || c",
    "(await (a + b * c))", "(0, a || b)(c)"};
  for (unsigned i = 0; i < expected.size(); ++i)
    test::Check(Case(i).find(std::string("return ") + std::string(expected[i]) + ";") != std::string::npos);
  constexpr std::array<std::string_view, 14> postfix{
    "return new a(b);", "return new ((0, a)(b))(c);", "return new a.ctor(b);",
    "return new ((0, a)(b)).ctor(c);", ".fn(c);", "return (0, a)(b).value;",
    "return new a(b).value;", "return new new a(b)(c);", "return new a(b) + c;",
    "return (0, a.fn)(b);", "return (1).value;", "return (-a).value;",
    "return new (a || b)(c);", "return new ((0, a)(b)).ctor.ctor(c);"};
  for (unsigned i = 0; i < postfix.size(); ++i) test::Check(Case(i + 30).find(postfix[i]) != std::string::npos);
  std::uint64_t previous_postfix = 0;
  for (const auto count : {1U, 2U, 200U, 201U, 402U, 804U}) {
    std::uint64_t work = 0;
    test::Check(PostfixChain(count, &work).find("return new ((0, a)(b)).ctor") != std::string::npos);
    if (count > 201) test::Check(work < 3 * previous_postfix);
    previous_postfix = work;
  }
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
