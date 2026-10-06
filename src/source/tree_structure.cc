#include "tree_structure.h"
#include "../analysis/register_value_rule.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "tree_checks.h"
#include "class_tree.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
namespace {
using Error = TreeStructureError;
using SyntaxNode = std::variant<ExprId, StmtId>;
struct SyntaxFrame {
  SyntaxNode node;
  ScopeId scope;
  std::size_t next_child = 0;
  bool entered = false;
  std::optional<StmtId> loop;
  std::optional<SymbolId> catch_parameter;
};
bool ValidUnary(TreeUnary operation) {
  switch (operation) {
    case TreeUnary::kNot: case TreeUnary::kNegate: case TreeUnary::kBitNot:
    case TreeUnary::kPlus: case TreeUnary::kTypeof: return true;
  }
  return false;
}
bool ValidBinary(analysis::BinaryOperator operation) {
  using B = analysis::BinaryOperator;
  switch (operation) {
    case B::kAdd: case B::kSubtract: case B::kMultiply: case B::kDivide:
    case B::kRemainder: case B::kExponent: case B::kEqual: case B::kNotEqual:
    case B::kLess: case B::kLessEqual: case B::kGreater: case B::kGreaterEqual:
    case B::kShiftLeft: case B::kShiftRightUnsigned: case B::kShiftRightSigned:
    case B::kBitAnd: case B::kBitOr: case B::kBitXor: case B::kStrictEqual:
    case B::kStrictNotEqual: case B::kInstanceOf: case B::kIn: return true;
  }
  return false;
}
void CheckCallable(const FunctionTree& tree) {
  switch (tree.constructor_form) {
    case ConstructorForm::kExplicit: break;
    case ConstructorForm::kImplicit:
      RequireTree(tree.callable == CallableKind::kDerivedConstructor, Error::kUnsupportedSyntax); break;
    default: throw Error::kUnsupportedSyntax;
  }
  RequireTree(tree.strictness == Strictness::kStrict || tree.strictness == Strictness::kSloppy,
              Error::kUnsupportedSyntax);
  switch (tree.callable) {
    case CallableKind::kFunction: case CallableKind::kMethod:
    case CallableKind::kArrow: case CallableKind::kBaseConstructor:
    case CallableKind::kDerivedConstructor: case CallableKind::kAsyncFunction:
    case CallableKind::kAsyncMethod: case CallableKind::kAsyncArrow: case CallableKind::kGenerator:
    case CallableKind::kGeneratorMethod: case CallableKind::kAsyncGenerator: return;
  }
  throw Error::kUnsupportedSyntax;
}

class StructureCheck {
 public:
  StructureCheck(const FunctionTree& tree, TreeCheckBudget& budget, std::vector<ScopeId>& expression_scopes,
      const GlobalScope* destination)
      : tree_(tree), budget_(budget), scopes_(tree, budget), expression_scopes_(expression_scopes), destination_(destination) {
    budget.Array(expression_scopes_, tree.expressions.size());
    budget.Array(expressions_, tree.expressions.size());
    budget.Array(statements_, tree.statements.size());
    budget.Array(children_, tree.children.size());
    budget.Array(arguments_, tree.arguments.size());
    budget.Array(properties_, tree.properties.size());
    budget.Array(capture_edges_, tree.closure_captures.size());
    budget.Array(labels_used_, tree.labels.size());
    std::size_t functions = 0;
    for (const auto& node : tree.expressions) {
      budget.Step(); functions += std::holds_alternative<FunctionExpr>(node.expression);
    }
    budget.Array(functions_, functions);
    RequireTree(tree.expressions.size() <= std::numeric_limits<std::size_t>::max() - tree.statements.size(),
                Error::kResourceLimit);
    budget.Array(stack_, tree.expressions.size() + tree.statements.size());
  }
  std::size_t Run() {
    CheckCallable(tree_);
    for (const auto& issue : tree_.recovery_issues) {
      budget_.Step();
      RequireTree(!issue.end || (issue.begin && *issue.begin <= *issue.end), Error::kRange);
      switch (issue.reason) {
        case RecoveryReason::kUnsupportedOperation: case RecoveryReason::kUnsupportedControl:
        case RecoveryReason::kUnavailableValue: case RecoveryReason::kUnresolvedBinding:
        case RecoveryReason::kIncompleteChild: break;
        default: throw Error::kUnsupportedSyntax;
      }
    }
    switch (tree_.invocation) {
      case InvocationForm::kDeclared: break;
      case InvocationForm::kArrowMethod: {
        RequireTree(IsArrowCallable(tree_.callable) && tree_.strictness == Strictness::kStrict &&
            tree_.inferred_name && ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded,
            Error::kUnsupportedSyntax);
        bool snapshot = false;
        for (const auto& node : tree_.expressions) {
          budget_.Step(); snapshot |= std::holds_alternative<ArgumentSnapshotExpr>(node.expression);
        }
        RequireTree(snapshot, Error::kUnsupportedSyntax);
        break;
      }
      default: throw Error::kUnsupportedSyntax;
    }
    if (tree_.inferred_name) {
      RequireTree(tree_.inferred_name->value < tree_.names.size(), Error::kName);
      RequireTree((tree_.callable == CallableKind::kFunction ||
          IsArrowCallable(tree_.callable) || tree_.callable == CallableKind::kAsyncFunction ||
          tree_.callable == CallableKind::kGenerator || tree_.callable == CallableKind::kAsyncGenerator), Error::kUnsupportedSyntax);
    }
    RequireTree(tree_.parameter_scope.value < tree_.scopes.size() &&
                tree_.body_scope.value < tree_.scopes.size() &&
                tree_.parameter_scope != tree_.body_scope, Error::kScope);
    const auto& parameters = tree_.scopes[tree_.parameter_scope.value];
    const auto& body = tree_.scopes[tree_.body_scope.value];
    const auto outer = tree_.capture_scope ? tree_.capture_scope : tree_.module_scope;
    std::optional<ScopeId> self_scope;
    if (tree_.self) {
      const auto& self = tree_.symbols[tree_.self->value];
      self_scope = self.scope;
      RequireTree(tree_.inferred_name && self.scope != tree_.parameter_scope &&
          self.scope != tree_.body_scope && tree_.scopes[self.scope.value].parent == outer &&
          tree_.scopes[self.scope.value].kind == ScopeKind::kFunctionSelf, Error::kScope);
      switch (tree_.self_form) {
        case SelfForm::kNamedFunction:
          RequireTree(AllowsNamedSelf(tree_.callable) &&
              tree_.inferred_name == self.name &&
              self.evidence == NameEvidence::kMetadata, Error::kBinding);
          break;
        case SelfForm::kCreationBinding:
          RequireTree(self.evidence == NameEvidence::kSynthesized &&
              ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded, Error::kBinding);
          break;
        default: throw Error::kBinding;
      }
      budget_.Step(tree_.names[self.name.value].size());
      RequireTree(IsSourceIdentifier(std::u16string_view{tree_.names[self.name.value]}), Error::kName);
    } else RequireTree(tree_.self_form == SelfForm::kNamedFunction, Error::kBinding);
    const auto enclosing = self_scope ? self_scope : outer;
    RequireTree(parameters.kind == ScopeKind::kParameters && parameters.parent == enclosing &&
                body.kind == ScopeKind::kFunctionBody && body.parent == tree_.parameter_scope, Error::kScope);
    RequireTree(tree_.capture_scope.has_value() == !tree_.captures.empty(), Error::kScope);
    RequireTree(tree_.module_scope.has_value() == !tree_.modules.empty(), Error::kScope);
    if (tree_.module_scope) {
      const auto scope = *tree_.module_scope;
      RequireTree(scope.value < tree_.scopes.size() && scope != tree_.parameter_scope && scope != tree_.body_scope &&
          tree_.scopes[scope.value].kind == ScopeKind::kModule && !tree_.scopes[scope.value].parent, Error::kScope);
      scopes_.Enter(scope);
    }
    if (tree_.capture_scope) {
      const auto scope = *tree_.capture_scope;
      RequireTree(scope.value < tree_.scopes.size() && scope != tree_.parameter_scope && scope != tree_.body_scope &&
          tree_.scopes[scope.value].kind == ScopeKind::kCaptures && tree_.scopes[scope.value].parent == tree_.module_scope,
          Error::kScope);
      scopes_.Enter(scope);
    }
    RequireTree(tree_.body.value < tree_.statements.size(), Error::kRange);
    const auto* root = std::get_if<BlockStmt>(&tree_.statements[tree_.body.value].statement);
    RequireTree(root && root->scope == tree_.body_scope && !root->label, Error::kScope);
    if (self_scope) scopes_.Enter(*self_scope);
    scopes_.Enter(tree_.parameter_scope);
    // Literal and outer-capture defaults cannot read body bindings or later
    // formals. Captures must also pass active-scope shadowing checks. These
    // are separate expression occurrences in parameter scope,
    // never borrowed body nodes. General default expressions need an ordered
    // parameter-initialization proof before expanding this admission rule.
    for (const auto& parameter : tree_.parameters) {
      budget_.Step();
      if (!parameter.initializer) continue;
      const auto id = *parameter.initializer;
      RequireTree(id.value < tree_.expressions.size(), Error::kRange);
      const auto& expression = tree_.expressions[id.value].expression;
      const auto* captured = std::get_if<SymbolExpr>(&expression);
      RequireTree(std::holds_alternative<TreeLiteral>(expression) ||
          (captured && captured->symbol.value < tree_.symbols.size() &&
           tree_.symbols[captured->symbol.value].kind == BindingKind::kCapture), Error::kUnsupportedSyntax);
      SyntaxFrame initializer{id, tree_.parameter_scope, 0, false};
      Enter(initializer);
    }
    std::size_t depth = 1;
    std::size_t maximum_depth = depth;
    stack_[0] = {tree_.body, tree_.parameter_scope, 0, false};
    while (depth != 0) {
      budget_.Step();
      auto& frame = stack_[depth - 1];
      if (!frame.entered) { Enter(frame); frame.entered = true; }
      const auto child = Next(frame);
      if (child) {
        RequireTree(depth < stack_.size(), Error::kOwnership);
        std::optional<SymbolId> caught;
        if (const auto* parent = std::get_if<StmtId>(&frame.node)) {
          const auto* region = std::get_if<TryStmt>(&tree_.statements[parent->value].statement);
          const auto* statement = std::get_if<StmtId>(&*child);
          if (region && region->handler && statement && *statement == region->handler->body)
            caught = region->handler->parameter;
        }
        stack_[depth++] = {*child, frame.scope, 0, false, frame.loop, caught};
        maximum_depth = std::max(depth, maximum_depth);
      } else {
        if (const auto* statement = std::get_if<StmtId>(&frame.node)) {
          if (const auto* block = std::get_if<BlockStmt>(&tree_.statements[statement->value].statement))
            scopes_.Leave(block->scope);
          statements_[statement->value] = 2; // Finished owners are not ancestors.
        }
        --depth;
      }
    }
    scopes_.Leave(tree_.parameter_scope);
    if (self_scope) scopes_.Leave(*self_scope);
    if (tree_.capture_scope) scopes_.Leave(*tree_.capture_scope);
    if (tree_.module_scope) scopes_.Leave(*tree_.module_scope);
    scopes_.Finish();
    for (const auto seen : expressions_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto seen : statements_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto seen : children_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto seen : arguments_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto seen : properties_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto seen : capture_edges_) { budget_.Step(); RequireTree(seen, Error::kOwnership); }
    for (const auto used : labels_used_) { budget_.Step(); RequireTree(used, Error::kTarget); }
    RequireTree(next_function_ == functions_.size(), Error::kOwnership);
    std::sort(functions_.begin(), functions_.end(), [&](auto left, auto right) {
      budget_.Step(); return left < right;
    });
    for (std::size_t i = 1; i < functions_.size(); ++i) {
      budget_.Step(); RequireTree(functions_[i - 1] != functions_[i], Error::kOwnership);
    }
    return maximum_depth;
  }

 private:
  void Key(const MemberKey& key) const {
    if (const auto* name = std::get_if<NameId>(&key))
      RequireTree(name->value < tree_.names.size(), Error::kRange);
  }
  void Expression(const ExprNode& node) {
    RequireTree(node.origin.value < tree_.origins.size(), Error::kOrigin);
    std::visit([&](const auto& expression) {
      using T = std::decay_t<decltype(expression)>;
      if constexpr (std::is_same_v<T, TreeLiteral>) {
        if (const auto* text = std::get_if<TextId>(&expression))
          RequireTree(text->value < tree_.texts.size(), Error::kRange);
        if (const auto* number = std::get_if<NumberLiteral>(&expression)) {
          const auto value = std::bit_cast<double>(number->bits);
          const auto canonical = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
          RequireTree(!std::isnan(value) || number->bits == canonical ||
              (Arithmetic(tree_.identity.contract) == analysis::ArithmeticSemantics::kSourceNumeric &&
               analysis::detail::FloatNumberKinds({number->bits}).IsNumber()), Error::kUnsupportedSyntax);
        }
      } else if constexpr (std::is_same_v<T, SymbolExpr>) {
        scopes_.Reference(expression.symbol);
      } else if constexpr (std::is_same_v<T, GlobalNameExpr>) {
        RequireTree(expression.name.value < tree_.names.size(), Error::kRange);
        // Source identifier syntax only, including legal dollar names. Legacy
        // private-name exclusions are not language rules. The publication
        // owner separately verifies actual local and enclosing bindings.
        budget_.Step(tree_.names[expression.name.value].size());
        RequireTree(IsSourceIdentifier(std::u16string_view{tree_.names[expression.name.value]}), Error::kBinding);
      } else if constexpr (std::is_same_v<T, RealmObjectExpr> || std::is_same_v<T, SymbolConstructorExpr>) {
        // Binding selection belongs to the actual publication destination.
      } else if constexpr (std::is_same_v<T, ThisExpr>) {
        RequireTree(expression.owner == tree_.owner && tree_.invocation != InvocationForm::kArrowMethod,
            Error::kBinding);
      } else if constexpr (std::is_same_v<T, ClassExpr>) {
        RequireTree(expression.definition != nullptr, Error::kOwnership);
        ClassCaptures(*expression.definition);
      } else if constexpr (std::is_same_v<T, FunctionExpr>) {
        RequireTree(expression.child != tree_.owner && next_function_ < functions_.size(), Error::kOwnership);
        functions_[next_function_++] = expression.child;
        const auto range = expression.captures;
        RequireTree(range.first <= tree_.closure_captures.size() &&
            range.count <= tree_.closure_captures.size() - range.first, Error::kRange);
        for (std::uint32_t i = 0; i < range.count; ++i) {
          budget_.Step(); const auto at = range.first + i;
          RequireTree(!capture_edges_[at], Error::kOwnership); capture_edges_[at] = 1;
          const auto symbol = tree_.closure_captures[at];
          scopes_.Reference(symbol);
          RequireTree(tree_.symbols[symbol.value].kind == BindingKind::kLocal ||
              tree_.symbols[symbol.value].kind == BindingKind::kCapture, Error::kBinding);
        }
      } else if constexpr (std::is_same_v<T, FieldDefinitionExpr>) {
        RequireTree(expression.name.value < tree_.names.size(), Error::kName);
        // The receiver may be the original read's retained binding. Common
        // operand correspondence proves its identity; structure must not force
        // a new inline this read. Native field publication closes its context.
      } else if constexpr (std::is_same_v<T, MemberExpr>) {
        Key(expression.key);
      } else if constexpr (std::is_same_v<T, UnaryExpr>) {
        RequireTree(ValidUnary(expression.operation), Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, AwaitExpr>) {
        RequireTree(IsAsyncCallable(tree_.callable),
            Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, YieldExpr>) {
        RequireTree(IsGeneratorCallable(tree_.callable) &&
            (expression.form == YieldForm::kDirect || expression.form == YieldForm::kDelegate), Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, BinaryExpr>) {
        RequireTree(ValidBinary(expression.operation), Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
        // Child ownership and scope are checked by the shared iterative walk.
      } else if constexpr (std::is_same_v<T, SequenceExpr>) {
        // Native comma expression evaluates each owned child once, in order.
      } else if constexpr (std::is_same_v<T, UpdateExpr>) {
        RequireTree((expression.direction == UpdateDirection::kIncrement || expression.direction == UpdateDirection::kDecrement) &&
            (expression.result == UpdateResult::kPrefix || expression.result == UpdateResult::kPostfix), Error::kUnsupportedSyntax);
        scopes_.Reference(expression.symbol);
        scopes_.Reference(expression.symbol, true);
      } else if constexpr (std::is_same_v<T, LogicalExpr>) {
        RequireTree(expression.operation == TreeLogical::kAnd || expression.operation == TreeLogical::kOr ||
                    expression.operation == TreeLogical::kNullish,
                    Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, ArgumentSnapshotExpr>) {
        RequireTree(expression.owner == tree_.owner && tree_.strictness == Strictness::kStrict &&
            (!IsArrowCallable(tree_.callable) || tree_.invocation == InvocationForm::kArrowMethod) &&
            ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded, Error::kBinding);
        RequireTree(expression.slots.first <= tree_.arguments.size() &&
            expression.slots.count <= tree_.arguments.size() - expression.slots.first &&
            expression.slots.count == tree_.parameters.size(), Error::kRange);
      } else if constexpr (std::is_same_v<T, ConstructExpr>) {
        if (const auto* arguments = std::get_if<ArgumentRange>(&expression.arguments)) {
          RequireTree(arguments->first <= tree_.arguments.size() &&
              arguments->count <= tree_.arguments.size() - arguments->first, Error::kRange);
        } else {
          const auto array = std::get<ArrayArguments>(expression.arguments).array;
          RequireTree(ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded &&
              array.value < tree_.expressions.size(), Error::kUnsupportedSyntax);
          const auto& child = tree_.expressions[array.value].expression;
          const auto* aggregate = std::get_if<ObjectExpr>(&child);
          RequireTree(std::holds_alternative<EmptyArrayExpr>(child) ||
              (aggregate && aggregate->kind == AggregateKind::kArray), Error::kUnsupportedSyntax);
        }
      } else if constexpr (std::is_same_v<T, SuperCallExpr>) {
        RequireTree(tree_.callable == CallableKind::kDerivedConstructor, Error::kUnsupportedSyntax);
        if (const auto* range = std::get_if<ArgumentRange>(&expression.arguments)) {
          RequireTree(range->first <= tree_.arguments.size() &&
              range->count <= tree_.arguments.size() - range->first, Error::kRange);
          RequireTree(expression.source == analysis::SuperArgumentSource::kExplicit ||
              (expression.source == analysis::SuperArgumentSource::kInvocationFrame &&
               tree_.ImplicitConstructor() && range->count == 0), Error::kUnsupportedSyntax);
        } else {
          RequireTree(Constructors(tree_.identity.contract) == ConstructorSemantics::kSourceLanguage &&
              expression.source == analysis::SuperArgumentSource::kExplicit &&
              std::get<SpreadArguments>(expression.arguments).iterable.value < tree_.expressions.size(), Error::kUnsupportedSyntax);
        }
      } else if constexpr (std::is_same_v<T, CallExpr>) {
        RequireTree(expression.arguments.first <= tree_.arguments.size() &&
            expression.arguments.count <= tree_.arguments.size() - expression.arguments.first, Error::kRange);
        RequireTree(expression.reference == CallReference::kValue ||
            expression.reference == CallReference::kMember, Error::kUnsupportedSyntax);
        if (expression.reference == CallReference::kMember)
          RequireTree(expression.callee.value < tree_.expressions.size() &&
              std::holds_alternative<MemberExpr>(tree_.expressions[expression.callee.value].expression), Error::kUnsupportedSyntax);
      } else if constexpr (std::is_same_v<T, ObjectExpr>) {
        RequireTree(expression.kind == AggregateKind::kObject || expression.kind == AggregateKind::kArray,
            Error::kUnsupportedSyntax);
        RequireTree(expression.properties.first <= tree_.properties.size() &&
            expression.properties.count <= tree_.properties.size() - expression.properties.first, Error::kRange);
        RequireTree(expression.buffered <= expression.properties.count, Error::kRange);
      } else if constexpr (std::is_same_v<T, EmptyArrayExpr> || std::is_same_v<T, EmptyObjectExpr>) {
        // Each allocation occurrence has unique syntax ownership. Identity and
        // execution order are proved separately against its Value IR operation.
      } else {
        static_assert(std::is_same_v<T, AssignmentExpr>);
        if (const auto* symbol = std::get_if<SymbolTarget>(&expression.target))
          scopes_.Reference(symbol->symbol, true);
        else Key(std::get<MemberTarget>(expression.target).key);
      }
    }, node.expression);
  }
  void RequireBlock(StmtId id) const {
    RequireTree(id.value < tree_.statements.size() &&
                std::holds_alternative<BlockStmt>(tree_.statements[id.value].statement), Error::kScope);
  }
  void Transfer(StmtId target, std::optional<LabelId> label, std::optional<StmtId> nearest, bool continues) {
    RequireTree(target.value < statements_.size() && statements_[target.value] == 1, Error::kTarget);
    const auto* loop = std::get_if<LoopStmt>(&tree_.statements[target.value].statement);
    const auto* block = std::get_if<BlockStmt>(&tree_.statements[target.value].statement);
    RequireTree(loop || (!continues && block), Error::kTarget);
    if (loop && target == nearest) { RequireTree(!label, Error::kTarget); return; }
    RequireTree(label && (loop ? loop->label : block->label) == label && label->value < tree_.labels.size() &&
        tree_.labels[label->value] == target, Error::kTarget);
    labels_used_[label->value] = 1;
  }
  void Enter(SyntaxFrame& frame) {
    if (const auto* expression = std::get_if<ExprId>(&frame.node)) {
      RequireTree(expression->value < expressions_.size(), Error::kRange);
      RequireTree(!expressions_[expression->value], Error::kOwnership);
      expressions_[expression->value] = 1;
      expression_scopes_[expression->value] = frame.scope;
      Expression(tree_.expressions[expression->value]);
      return;
    }
    const auto id = std::get<StmtId>(frame.node);
    RequireTree(id.value < statements_.size(), Error::kRange);
    RequireTree(!statements_[id.value], Error::kOwnership);
    statements_[id.value] = 1;
    const auto& node = tree_.statements[id.value];
    RequireTree(node.origin.value < tree_.origins.size(), Error::kOrigin);
    if (const auto* block = std::get_if<BlockStmt>(&node.statement)) {
      if (block->label) RequireTree(block->label->value < tree_.labels.size() &&
          tree_.labels[block->label->value] == id, Error::kTarget);
      RequireTree(block->scope.value < tree_.scopes.size(), Error::kScope);
      const auto& scope = tree_.scopes[block->scope.value];
      const auto kind = frame.catch_parameter ? ScopeKind::kCatch :
          (id == tree_.body ? ScopeKind::kFunctionBody : ScopeKind::kBlock);
      RequireTree(scope.parent == frame.scope &&
                  scope.kind == kind, Error::kScope);
      RequireTree(block->first_child <= tree_.children.size() &&
                  block->child_count <= tree_.children.size() - block->first_child, Error::kRange);
      scopes_.Enter(block->scope);
      if (frame.catch_parameter) scopes_.CatchParameter(*frame.catch_parameter, block->scope);
      frame.scope = block->scope;
    } else if (const auto* declaration = std::get_if<DeclarationStmt>(&node.statement)) {
      scopes_.Declare(declaration->symbol, frame.scope, declaration->initializer.has_value());
    } else if (const auto* branch = std::get_if<IfStmt>(&node.statement)) {
      RequireBlock(branch->consequent);
      if (branch->alternate) RequireBlock(*branch->alternate);
    } else if (const auto* region = std::get_if<TryStmt>(&node.statement)) {
      RequireTree(region->handler || region->finalizer, Error::kUnsupportedSyntax);
      RequireBlock(region->body);
      if (region->handler) RequireBlock(region->handler->body);
      if (region->finalizer) RequireBlock(*region->finalizer);
      RequireTree(!std::get<BlockStmt>(tree_.statements[region->body.value].statement).label &&
          (!region->handler || !std::get<BlockStmt>(tree_.statements[region->handler->body.value].statement).label) &&
          (!region->finalizer || !std::get<BlockStmt>(tree_.statements[region->finalizer->value].statement).label),
          Error::kUnsupportedSyntax);
    } else if (const auto* loop = std::get_if<LoopStmt>(&node.statement)) {
      RequireBlock(loop->body);
      if (loop->label) RequireTree(loop->label->value < tree_.labels.size() &&
          tree_.labels[loop->label->value] == id, Error::kTarget);
      frame.loop = id;
    } else if (const auto* transfer = std::get_if<BreakStmt>(&node.statement)) {
      Transfer(transfer->target, transfer->label, frame.loop, false);
    } else if (const auto* transfer = std::get_if<ContinueStmt>(&node.statement)) {
      Transfer(transfer->target, transfer->label, frame.loop, true);
    } else if (const auto* missing = std::get_if<UnrecoveredStmt>(&node.statement)) {
      RequireTree(missing->issue.value < tree_.recovery_issues.size(), Error::kRange);
    }
  }
  std::optional<SyntaxNode> Next(SyntaxFrame& frame) {
    // At most three fixed children (computed assignment base/key/value). Blocks
    // stream their child slice instead of pushing an unbounded sibling list.
    constexpr std::size_t kMaximumFixedChildren = 3;
    std::array<SyntaxNode, kMaximumFixedChildren> children;
    std::size_t count = 0;
    const auto add = [&](auto id) { children[count++] = id; };
    const auto key = [&](const MemberKey& member) {
      if (const auto* computed = std::get_if<ComputedKey>(&member)) add(computed->expression);
    };
    if (const auto* expression = std::get_if<ExprId>(&frame.node)) {
      const auto& syntax = tree_.expressions[expression->value].expression;
      if (const auto* object = std::get_if<ObjectExpr>(&syntax)) {
        if (frame.next_child == object->properties.count) return {};
        const auto slot = object->properties.first + frame.next_child++;
        const auto& property = tree_.properties[slot];
        RequireTree(!properties_[slot], Error::kOwnership); properties_[slot] = 1;
        RequireTree(property.key.value < tree_.names.size() && property.origin.value < tree_.origins.size(), Error::kRange);
        if (object->kind == AggregateKind::kArray) {
          const auto index = std::to_string(frame.next_child - 1);
          RequireTree(tree_.names[property.key.value] == std::u16string(index.begin(), index.end()), Error::kUnsupportedSyntax);
        }
        return property.value;
      }
      if (const auto* call = std::get_if<CallExpr>(&syntax))
        return NextCall(frame, call->callee, call->arguments);
      if (const auto* construct = std::get_if<ConstructExpr>(&syntax)) {
        if (const auto* arguments = std::get_if<ArgumentRange>(&construct->arguments))
          return NextCall(frame, construct->constructor, *arguments);
        if (frame.next_child == 0) { ++frame.next_child; return construct->constructor; }
        if (frame.next_child == 1) { ++frame.next_child; return std::get<ArrayArguments>(construct->arguments).array; }
        return {};
      }
      if (const auto* super = std::get_if<SuperCallExpr>(&syntax)) {
        if (const auto* range = std::get_if<ArgumentRange>(&super->arguments)) {
          if (frame.next_child == range->count) return {};
          const auto slot = range->first + frame.next_child++;
          RequireTree(!arguments_[slot], Error::kOwnership); arguments_[slot] = 1;
          return tree_.arguments[slot];
        }
        if (frame.next_child++) return {};
        return std::get<SpreadArguments>(super->arguments).iterable;
      }
      if (const auto* snapshot = std::get_if<ArgumentSnapshotExpr>(&syntax)) {
        if (frame.next_child == snapshot->slots.count) return {};
        const auto slot = snapshot->slots.first + frame.next_child++;
        RequireTree(!arguments_[slot], Error::kOwnership); arguments_[slot] = 1;
        const auto id = tree_.arguments[slot];
        RequireTree(id.value < tree_.expressions.size(), Error::kRange);
        const auto& input = tree_.expressions[id.value].expression;
        RequireTree(std::holds_alternative<SymbolExpr>(input) ||
            std::holds_alternative<ThisExpr>(input), Error::kUnsupportedSyntax);
        return id;
      }
      std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, MemberExpr>) { add(node.base); key(node.key); }
        else if constexpr (std::is_same_v<T, UnaryExpr> || std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr>) add(node.operand);
        else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr> || std::is_same_v<T, SequenceExpr>) {
          add(node.left); add(node.right);
        } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
          add(node.condition); add(node.consequent); add(node.alternate);
        } else if constexpr (std::is_same_v<T, ClassExpr>) {
          if (node.heritage) add(*node.heritage);
        } else if constexpr (std::is_same_v<T, FieldDefinitionExpr>) {
          add(node.receiver); add(node.value);
        } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
          if (const auto* member = std::get_if<MemberTarget>(&node.target)) { add(member->base); key(member->key); }
          add(node.value);
        } else {
          static_assert(std::is_same_v<T, UpdateExpr> || std::is_same_v<T, TreeLiteral> || std::is_same_v<T, SymbolExpr> ||
                        std::is_same_v<T, GlobalNameExpr> ||
                        std::is_same_v<T, RealmObjectExpr> ||
                        std::is_same_v<T, SymbolConstructorExpr> ||
                        std::is_same_v<T, ArgumentSnapshotExpr> ||
                        std::is_same_v<T, ThisExpr> || std::is_same_v<T, CallExpr> ||
                        std::is_same_v<T, ConstructExpr> || std::is_same_v<T, SuperCallExpr> || std::is_same_v<T, EmptyArrayExpr> ||
                        std::is_same_v<T, EmptyObjectExpr> || std::is_same_v<T, ObjectExpr> || std::is_same_v<T, FunctionExpr>);
        }
      }, tree_.expressions[expression->value].expression);
    } else {
      const auto& statement = tree_.statements[std::get<StmtId>(frame.node).value].statement;
      if (const auto* block = std::get_if<BlockStmt>(&statement)) {
        if (frame.next_child == block->child_count) return {};
        const auto child = block->first_child + frame.next_child++;
        RequireTree(!children_[child], Error::kOwnership);
        children_[child] = 1;
        return tree_.children[child];
      }
      std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, DeclarationStmt>) { if (node.initializer) add(*node.initializer); }
        else if constexpr (std::is_same_v<T, ExpressionStmt>) add(node.expression);
        else if constexpr (std::is_same_v<T, IfStmt>) {
          add(node.condition); add(node.consequent); if (node.alternate) add(*node.alternate);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) { if (node.value) add(*node.value); }
        else if constexpr (std::is_same_v<T, ThrowStmt>) { add(node.value); }
        else if constexpr (std::is_same_v<T, TryStmt>) {
          add(node.body);
          if (node.handler) add(node.handler->body);
          if (node.finalizer) add(*node.finalizer);
        }
        else if constexpr (std::is_same_v<T, LoopStmt>) add(node.body);
        else { static_assert(std::is_same_v<T, BlockStmt> || std::is_same_v<T, BreakStmt> ||
                             std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, UnrecoveredStmt> ||
                             std::is_same_v<T, ContinueStmt>); }
      }, statement);
    }
    if (frame.next_child == count) return {};
    return children[frame.next_child++];
  }

  std::optional<SyntaxNode> NextCall(SyntaxFrame& frame, ExprId callee, ArgumentRange arguments) {
    // Stream siblings just like block children; a wide call does not grow the
    // traversal stack. Every argument arena slot must be uniquely owned.
    if (frame.next_child == 0) { ++frame.next_child; return callee; }
    const auto ordinal = frame.next_child - 1;
    if (ordinal == arguments.count) return {};
    const auto slot = arguments.first + ordinal;
    RequireTree(!arguments_[slot], Error::kOwnership);
    arguments_[slot] = 1;
    ++frame.next_child;
    return tree_.arguments[slot];
  }

  void ClassCaptures(const ClassTree& definition) {
    const auto family = [&](const PreparedFamily& prepared) {
      const auto& built = prepared.Family();
      RequireTree(built.root.value < built.members.size(), Error::kOwnership);
      const auto& root = built.members[built.root.value].tree;
      for (const auto& alias : root.captures) {
        budget_.Step(); RequireTree(alias.target.owner == tree_.owner, Error::kBinding);
        scopes_.Reference(alias.target.symbol);
      }
      // A prepared member's internal enclosure is closed; this checks only
      // its actual outer lexical destination, including TDZ name shadowing.
      for (const auto& member : built.members) {
        const auto& child = member.tree;
        std::vector<std::optional<ModuleSymbol>> modules;
        if (!child.modules.empty()) {
          budget_.Array(modules, child.symbols.size());
          for (const auto& alias : child.modules) {
            budget_.Step(); RequireTree(alias.symbol.value < modules.size() && !modules[alias.symbol.value], Error::kBinding);
            modules[alias.symbol.value] = alias.target;
          }
        }
        for (const auto& node : child.expressions) {
          budget_.Step();
          if (const auto* global = std::get_if<GlobalNameExpr>(&node.expression)) {
            RequireTree(global->name.value < child.names.size(), Error::kRange);
            scopes_.EnclosingReference(child.names[global->name.value]);
          } else if (const auto* reference = std::get_if<SymbolExpr>(&node.expression)) {
            RequireTree(reference->symbol.value < child.symbols.size(), Error::kRange);
            const auto& symbol = child.symbols[reference->symbol.value];
            if (symbol.kind != BindingKind::kModule) continue;
            RequireTree(symbol.name.value < child.names.size(), Error::kRange);
            // Module identity is checked by the prepared destination; lexical
            // aliases refer to the same record binding in the enclosing tree.
            RequireTree(reference->symbol.value < modules.size() && modules[reference->symbol.value], Error::kBinding);
            scopes_.EnclosingReference(child.names[symbol.name.value], &*modules[reference->symbol.value]);
          } else if (destination_) {
            if (std::holds_alternative<RealmObjectExpr>(node.expression)) {
              const auto binding = ResolveRealmBinding(child.identity.contract, destination_);
              RequireTree(binding.has_value(), Error::kBinding); scopes_.EnclosingReference(binding->name);
            } else if (std::holds_alternative<SymbolConstructorExpr>(node.expression)) {
              const auto binding = ResolveSymbolBinding(destination_);
              RequireTree(binding.has_value(), Error::kBinding); scopes_.EnclosingReference(*binding);
            } else if (std::holds_alternative<ArgumentSnapshotExpr>(node.expression)) {
              const auto binding = ResolveArgumentSnapshotBinding(destination_);
              RequireTree(binding.has_value(), Error::kBinding); scopes_.EnclosingReference(*binding);
            }
          }
        }
      }
    };
    if (definition.constructor) family(*definition.constructor);
    for (const auto& member : definition.members) if (member) family(*member);
    for (const auto& site : definition.definitions)
      for (const auto& member : site.bodies) if (member) family(*member);
  }
  const FunctionTree& tree_;
  TreeCheckBudget& budget_;
  TreeScopeCheck scopes_;
  std::vector<ScopeId>& expression_scopes_;
  const GlobalScope* destination_;
  std::vector<std::uint8_t> expressions_;
  std::vector<std::uint8_t> statements_;
  std::vector<std::uint8_t> children_;
  std::vector<std::uint8_t> arguments_;
  std::vector<std::uint8_t> properties_;
  std::vector<SyntaxFrame> stack_;
  std::vector<FunctionOwnerId> functions_;
  std::vector<std::uint8_t> capture_edges_;
  std::vector<std::uint8_t> labels_used_;
  std::size_t next_function_ = 0;
};
}  // namespace

std::variant<TreeStructure, TreeStructureError> VerifyTreeStructure(
    const FunctionTree& tree, analysis::SemanticBudget budget, const GlobalScope* destination) {
  try {
    const auto measured = MeasureTreeStorage(tree, budget);
    if (const auto* error = std::get_if<TreeStorageError>(&measured))
      return *error == TreeStorageError::kResourceLimit ? Error::kResourceLimit : Error::kWorkLimit;
    TreeCheckBudget checked(budget);
    checked.Admit(std::get<TreeStorage>(measured).bytes, 1);
    checked.Step(std::get<TreeStorage>(measured).work);
    checked.Admit(1, sizeof(StructureCheck));
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    RequireTree(tree.expressions.size() <= kMaxId && tree.statements.size() <= kMaxId &&
                tree.scopes.size() <= kMaxId && tree.symbols.size() <= kMaxId &&
                tree.names.size() <= kMaxId && tree.texts.size() <= kMaxId &&
                tree.origins.size() <= kMaxId && tree.origin_parts.size() <= kMaxId &&
                tree.anchors.size() <= kMaxId && tree.children.size() <= kMaxId &&
                tree.arguments.size() <= kMaxId && tree.properties.size() <= kMaxId &&
                tree.captures.size() <= kMaxId && tree.closure_captures.size() <= kMaxId &&
                tree.modules.size() <= kMaxId && tree.labels.size() <= kMaxId, Error::kResourceLimit);
    for (std::size_t i = 0; i < tree.receiver_guards.size(); ++i) {
      checked.Step();
      RequireTree(i == 0 || tree.receiver_guards[i - 1] < tree.receiver_guards[i], Error::kOwnership);
    }
    checked.Admit(1, sizeof(TreeStructure));
    TreeStructure result{tree.value_revision, tree.cfg_revision};
    result.maximum_depth = StructureCheck(tree, checked, result.expression_scopes, destination).Run();
    CheckTreeOrigins(tree, checked);
    result.allocated_bytes = checked.Bytes(); result.work = checked.Work();
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
