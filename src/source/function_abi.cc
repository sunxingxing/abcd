#include "function_abi.h"
#include "evaluation_plan.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = FunctionAbiError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class AbiBuilder {
 public:
  AbiBuilder(const analysis::ValueProgram& graph, const FunctionTree& tree,
             const abc::FrameLayout& frame, analysis::SemanticBudget budget,
             const EvaluationPlan* evaluation = nullptr)
      : graph_(graph), tree_(tree), frame_(frame), initial_(budget), remaining_(budget), evaluation_(evaluation) {}
  ConstructedAbi Build(std::uint32_t length) {
    Require(graph_.revision == tree_.value_revision && graph_.cfg_revision == tree_.cfg_revision, Error::kRevision);
    if (evaluation_) {
      const auto verified = VerifyPhasedEvaluationPlan(graph_, *evaluation_, remaining_);
      if (const auto* error = std::get_if<EvaluationError>(&verified)) {
        if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
        if (*error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
        throw Error::kEvaluation;
      }
      Step(std::get<std::uint64_t>(verified));
    }
    switch (tree_.invocation) {
      case InvocationForm::kDeclared: break;
      case InvocationForm::kArrowMethod:
        Require(tree_.callable == CallableKind::kArrow && tree_.inferred_name &&
            tree_.strictness == Strictness::kStrict, Error::kSyntax);
        break;
      default: throw Error::kSyntax;
    }
    Require(tree_.parameters.size() == frame_.parameters, Error::kParameter);
    if (tree_.self)
      Require((tree_.callable == CallableKind::kFunction || tree_.callable == CallableKind::kAsyncFunction ||
          (tree_.callable == CallableKind::kArrow && tree_.self_form == SelfForm::kCreationBinding)) &&
          tree_.self->value < tree_.symbols.size() &&
          tree_.symbols[tree_.self->value].kind == BindingKind::kFunctionSelf, Error::kSyntax);
    Array(result_.parameters, frame_.parameters);
    Array(formals_, tree_.symbols.size());
    result_.length = length;
    std::uint32_t source_length = frame_.parameters;
    for (std::size_t i = 0; i < tree_.parameters.size(); ++i) {
      Step();
      const auto& parameter = tree_.parameters[i];
      Require(!parameter.rest && parameter.symbol.value < formals_.size(), Error::kParameter);
      if (parameter.initializer) {
        Require(parameter.initializer->value < tree_.expressions.size() &&
            std::holds_alternative<TreeLiteral>(tree_.expressions[parameter.initializer->value].expression), Error::kParameter);
        if (result_.layout == ParameterLayout::kSimple) source_length = static_cast<std::uint32_t>(i);
        result_.layout = ParameterLayout::kLiteralDefaults;
      }
      Require(!formals_[parameter.symbol.value] &&
          tree_.symbols[parameter.symbol.value].kind == BindingKind::kParameter, Error::kParameter);
      formals_[parameter.symbol.value] = static_cast<std::uint32_t>(i);
      result_.parameters[i].symbol = parameter.symbol;
    }
    Require(length == source_length, Error::kLength);
    // SSA uses carry already-simplified entry identities. Do not count removed
    // MOVs as source ABI uses or use their register destinations as symbols.
    for (const auto& use : graph_.uses) {
      Step();
      if (use.life != analysis::ValueLife::kLive) continue;
      if (evaluation_)
        if (const auto* owner = std::get_if<analysis::OpId>(&use.owner);
            owner && (evaluation_->operations[owner->value].kind == EvaluationKind::kElidedPure ||
                      evaluation_->operations[owner->value].kind == EvaluationKind::kAlias)) continue;
      const auto* id = std::get_if<analysis::ValueId>(&use.operand);
      if (!id) continue;
      Require(id->value < graph_.values.size(), Error::kEntry);
      const auto& definition = graph_.values[evaluation_ ? ResolveEvaluationValue(*evaluation_, *id).value : id->value];
      Require(definition.life == analysis::ValueLife::kLive, Error::kEntry);
      if (const auto* entry = std::get_if<analysis::EntryDefinition>(&definition.origin)) {
        const auto* owner = std::get_if<analysis::OpId>(&use.owner);
        const bool snapshot = use.purpose == analysis::ReadPurpose::kProtocol && owner &&
            owner->value < graph_.ops.size() && use.role == analysis::OperandRole::kArgument &&
            std::holds_alternative<analysis::ValueArgumentSnapshot>(graph_.ops[owner->value].operation);
        Require(use.purpose == analysis::ReadPurpose::kOperand || snapshot, Error::kEntry);
        std::visit([&](const auto& value) { Entry(value); }, *entry);
      }
    }
    for (const auto& expression : tree_.expressions) {
      Step();
      std::visit([&](const auto& value) { Expression(value); }, expression.expression);
    }
    for (const auto& op : graph_.ops) {
      Step();
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueFrameThis>(op.operation)) {
        Require(tree_.callable == CallableKind::kFunction || tree_.callable == CallableKind::kAsyncFunction ||
            tree_.callable == CallableKind::kMethod ||
            tree_.callable == CallableKind::kBaseConstructor, Error::kReceiver);
        Increment(result_.frame_receiver_reads);
      }
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueFrameFunction>(op.operation)) {
        Require(tree_.self.has_value(), Error::kEntry);
        Increment(result_.frame_function_reads);
        result_.callable = AbiObservation::kFrameIdentity;
      }
    }
    for (const auto& parameter : result_.parameters) {
      Step();
      Require((parameter.reads.operands != 0) == (parameter.reads.references != 0) &&
          parameter.writes == 0, Error::kParameter);
    }
    Require((result_.receiver.operands != 0 || result_.frame_receiver_reads != 0) ==
        (result_.receiver.references != 0), Error::kReceiver);
    Require((result_.frame_function_reads != 0) == (result_.self_references != 0) &&
        tree_.self.has_value() == (result_.frame_function_reads != 0), Error::kEntry);
    // A JS arrow inherits this; a VM receiver entry is dynamic. Captured outer
    // receivers must instead retain their explicit enclosing-owner binding.
    Require(tree_.callable != CallableKind::kArrow ||
        (result_.receiver.operands == 0 && result_.frame_receiver_reads == 0 &&
         result_.receiver.references == 0), Error::kReceiver);
    Require(tree_.invocation != InvocationForm::kArrowMethod || snapshot_, Error::kEntry);
    return {std::move(result_), initial_.bytes - remaining_.bytes,
            initial_.work - remaining_.work};
  }
 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  static void Increment(std::uint64_t& count) {
    Require(count != std::numeric_limits<std::uint64_t>::max(), Error::kResourceLimit);
    ++count;
  }
  void Entry(analysis::ParameterEntry value) {
    Require(value.index < result_.parameters.size(), Error::kParameter);
    Increment(result_.parameters[value.index].reads.operands);
  }
  void Entry(analysis::ReceiverEntry) {
    Require(frame_.this_value.has_value(), Error::kReceiver);
    Increment(result_.receiver.operands);
  }
  void Entry(analysis::CallableEntry) { throw Error::kEntry; }
  void Entry(analysis::NewTargetEntry) { throw Error::kEntry; }
  void Entry(const analysis::UnknownEntry&) { throw Error::kEntry; }
  void Symbol(SymbolId id, bool write) {
    Require(id.value < formals_.size(), Error::kParameter);
    if (const auto ordinal = formals_[id.value]) {
      auto& parameter = result_.parameters[*ordinal];
      Increment(write ? parameter.writes : parameter.reads.references);
    } else {
      // Self needs an explicit immutable-binding census. No implicit arguments
      // binding may appear under an ordinary local spelling.
      const auto kind = tree_.symbols[id.value].kind;
      if (kind == BindingKind::kFunctionSelf) {
        Require(tree_.self == id && !write, Error::kSyntax);
        Increment(result_.self_references);
        return;
      }
      Require(kind == BindingKind::kCompilerTemporary || kind == BindingKind::kLocal ||
          kind == BindingKind::kCapture || kind == BindingKind::kModule ||
          kind == BindingKind::kCatchParameter, Error::kSyntax);
    }
  }
  void Expression(SymbolExpr node) { Symbol(node.symbol, false); }
  void Expression(ThisExpr node) {
    // LDTHIS does not need an ABI register copy. ReceiverEntry still checks
    // its optional slot separately; tree correspondence proves each origin.
    Require(node.owner == tree_.owner, Error::kReceiver);
    Increment(result_.receiver.references);
  }
  void Expression(ArgumentSnapshotExpr node) {
    snapshot_ = true;
    Require(graph_.arguments && graph_.arguments->frame == frame_ &&
        node.owner == tree_.owner && tree_.strictness == Strictness::kStrict &&
        (tree_.callable != CallableKind::kArrow || tree_.invocation == InvocationForm::kArrowMethod) &&
        node.slots.count == frame_.parameters &&
        ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded,
        Error::kEntry);
  }
  void Expression(const AssignmentExpr& node) {
    if (const auto* target = std::get_if<SymbolTarget>(&node.target)) Symbol(target->symbol, true);
  }
  template <class T> void Expression(const T&) {
    static_assert(std::is_same_v<T, TreeLiteral> || std::is_same_v<T, MemberExpr> ||
        std::is_same_v<T, GlobalNameExpr> ||
        std::is_same_v<T, RealmObjectExpr> ||
        std::is_same_v<T, SymbolConstructorExpr> ||
        std::is_same_v<T, UnaryExpr> || std::is_same_v<T, AwaitExpr> || std::is_same_v<T, BinaryExpr> ||
        std::is_same_v<T, LogicalExpr> || std::is_same_v<T, CallExpr> ||
        std::is_same_v<T, ConstructExpr> || std::is_same_v<T, EmptyArrayExpr> ||
        std::is_same_v<T, EmptyObjectExpr> || std::is_same_v<T, ObjectExpr> || std::is_same_v<T, FunctionExpr>);
    // Child expressions are counted once by their arena entries, not recursively.
  }
  const analysis::ValueProgram& graph_;
  const FunctionTree& tree_;
  const abc::FrameLayout& frame_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
  const EvaluationPlan* evaluation_;
  FunctionAbi result_;
  bool snapshot_ = false;
  std::vector<std::optional<std::uint32_t>> formals_;
};
}  // namespace

std::variant<ConstructedAbi, FunctionAbiError> BuildFunctionAbi(
    const analysis::ValueProgram& graph, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length,
    analysis::SemanticBudget budget) {
  try { return AbiBuilder(graph, tree, frame, budget).Build(length);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<ConstructedAbi, FunctionAbiError> BuildPhasedFunctionAbi(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length, analysis::SemanticBudget budget) {
  try { return AbiBuilder(graph, tree, frame, budget, &evaluation).Build(length);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
