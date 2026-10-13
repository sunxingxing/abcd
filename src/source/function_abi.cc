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
        Require(IsArrowCallable(tree_.callable) && tree_.inferred_name &&
            tree_.strictness == Strictness::kStrict, Error::kSyntax);
        break;
      default: throw Error::kSyntax;
    }
    const bool invocation_rest = tree_.parameter_form == ParameterForm::kInvocationRest;
    switch (tree_.parameter_form) {
      case ParameterForm::kFrameFormals:
        Require(tree_.parameters.size() == frame_.parameters, Error::kParameter); break;
      case ParameterForm::kInvocationRest:
        Require(tree_.callable == CallableKind::kDerivedConstructor && !tree_.ImplicitConstructor() &&
            tree_.strictness == Strictness::kStrict && length == 0 && graph_.arguments &&
            graph_.arguments->frame == frame_ && tree_.parameters.size() == 1 &&
            tree_.parameters.front().rest && !tree_.parameters.front().initializer &&
            ConstructorForwarding(tree_.identity.contract) == ConstructorForwardingSemantics::kSourceRestSpread,
            Error::kParameter);
        break;
      default: throw Error::kParameter;
    }
    if (tree_.self)
      Require((AllowsNamedSelf(tree_.callable) ||
          (IsArrowCallable(tree_.callable) && tree_.self_form == SelfForm::kCreationBinding)) &&
          tree_.self->value < tree_.symbols.size() &&
          tree_.symbols[tree_.self->value].kind == BindingKind::kFunctionSelf, Error::kSyntax);
    Array(result_.parameters, tree_.parameters.size());
    Array(formals_, tree_.symbols.size());
    result_.length = length;
    std::uint32_t source_length = frame_.parameters;
    for (std::size_t i = 0; i < tree_.parameters.size(); ++i) {
      Step();
      const auto& parameter = tree_.parameters[i];
      Require(parameter.symbol.value < formals_.size(), Error::kParameter);
      if (parameter.rest) {
        Require(i + 1 == tree_.parameters.size() && !parameter.initializer &&
            result_.layout == ParameterLayout::kSimple, Error::kParameter);
        result_.layout = ParameterLayout::kRest;
        source_length = static_cast<std::uint32_t>(i);
      }
      if (parameter.initializer) {
        Require(parameter.initializer->value < tree_.expressions.size(), Error::kParameter);
        const auto& initializer = tree_.expressions[parameter.initializer->value].expression;
        const auto* captured = std::get_if<SymbolExpr>(&initializer);
        Require(std::holds_alternative<TreeLiteral>(initializer) ||
            (captured && captured->symbol.value < tree_.symbols.size() &&
             tree_.symbols[captured->symbol.value].kind == BindingKind::kCapture), Error::kParameter);
        if (result_.layout == ParameterLayout::kSimple) source_length = static_cast<std::uint32_t>(i);
        result_.layout = ParameterLayout::kLiteralDefaults;
      }
      Require(!formals_[parameter.symbol.value] &&
          tree_.symbols[parameter.symbol.value].kind == BindingKind::kParameter, Error::kParameter);
      formals_[parameter.symbol.value] = static_cast<std::uint32_t>(i);
      result_.parameters[i].symbol = parameter.symbol;
    }
    Require(tree_.ImplicitConstructor() ? (tree_.callable == CallableKind::kDerivedConstructor && length == 0) :
        length == source_length, Error::kLength);
    // SSA uses carry already-simplified entry identities. Do not count removed
    // MOVs as source ABI uses or use their register destinations as symbols.
    for (const auto& use : graph_.uses) {
      Step();
      if (use.life != analysis::ValueLife::kLive) continue;
      if (evaluation_)
        if (const auto* owner = std::get_if<analysis::OpId>(&use.owner);
            owner && (evaluation_->operations[owner->value].kind == EvaluationKind::kElidedPure ||
                      evaluation_->operations[owner->value].kind == EvaluationKind::kAlias)) continue;
      if (const auto* owner = std::get_if<analysis::OpId>(&use.owner);
          owner && IsReceiverGuard(tree_, *owner)) continue;
      if ((tree_.ImplicitConstructor() || invocation_rest) && use.role == analysis::OperandRole::kConstructor &&
          use.purpose == analysis::ReadPurpose::kProtocol) {
        const auto* owner = std::get_if<analysis::OpId>(&use.owner);
        const auto* call = owner ? std::get_if<analysis::ValueSuperCall>(&graph_.ops[owner->value].operation) : nullptr;
        Require(call && call->source == analysis::SuperArgumentSource::kInvocationFrame, Error::kEntry);
        continue; // TreeOperands reproved the native constructor and raw frame.
      }
      if (tree_.callable == CallableKind::kDerivedConstructor &&
          Constructors(tree_.identity.contract) == ConstructorSemantics::kSourceLanguage &&
          use.role == analysis::OperandRole::kConstructor) {
        const auto* owner = std::get_if<analysis::OpId>(&use.owner);
        if (owner && std::holds_alternative<analysis::ValueSuperSpread>(graph_.ops[owner->value].operation))
          continue; // Original invocation identity is owned by derived projection.
      }
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
    for (const auto& capture : tree_.closure_captures) {
      Step();
      if (invocation_rest) Require(capture != tree_.parameters.front().symbol, Error::kParameter);
    }
    std::uint64_t rest_reads = 0, forwarding_calls = 0;
    for (const auto& op : graph_.ops) {
      if (op.life == analysis::ValueLife::kLive) {
        if (const auto* call = std::get_if<analysis::ValueSuperCall>(&op.operation);
            call && call->source == analysis::SuperArgumentSource::kInvocationFrame) Increment(forwarding_calls);
      }
      if (op.life == analysis::ValueLife::kLive)
        if (const auto* rest = std::get_if<analysis::ValueRestArguments>(&op.operation)) {
          Require(!invocation_rest && result_.layout == ParameterLayout::kRest &&
              rest->first_argument + 1U == tree_.parameters.size() && op.use_count == 0, Error::kParameter);
          Increment(rest_reads);
        }
      Step();
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueFrameThis>(op.operation)) {
        Require(tree_.callable == CallableKind::kFunction || tree_.callable == CallableKind::kAsyncFunction ||
            IsMethodCallable(tree_.callable) ||
            tree_.callable == CallableKind::kBaseConstructor || IsGeneratorCallable(tree_.callable),
            Error::kReceiver);
        Increment(result_.frame_receiver_reads);
      }
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueFrameFunction>(op.operation)) {
        Require(tree_.self.has_value(), Error::kEntry);
        Increment(result_.frame_function_reads);
        result_.callable = AbiObservation::kFrameIdentity;
      }
    }
    for (std::size_t i = 0; i < result_.parameters.size(); ++i) {
      Step();
      const auto& parameter = result_.parameters[i];
      if (tree_.parameters[i].rest) {
        // The physical parameter entry is the first actual rest argument, not
        // an array. Only the single checked allocation introduces this symbol.
        // Correspondence also permits its SSA consumers to read that native
        // binding directly; their count is not an ABI admission condition.
        Require(parameter.reads.operands == 0 && parameter.writes == 0, Error::kParameter);
        if (invocation_rest)
          Require(rest_reads == 0 && forwarding_calls != 0 &&
              parameter.reads.references == forwarding_calls, Error::kParameter);
        else Require(rest_reads == 1 && parameter.reads.references >= rest_reads, Error::kParameter);
      } else {
        Require((parameter.reads.operands != 0) == (parameter.reads.references != 0) &&
            parameter.writes == 0, Error::kParameter);
      }
    }
    // This census follows verified value/tree correspondence. In a derived
    // constructor the VM entry is undefined, while successful super results
    // use initialized native this without an ABI-slot read. TreeOperands owns
    // each actual receiver identity; a read-count equality cannot prove it.
    if (tree_.callable != CallableKind::kDerivedConstructor)
      Require((result_.receiver.operands != 0 || result_.frame_receiver_reads != 0) ==
          (result_.receiver.references != 0), Error::kReceiver);
    Require((result_.frame_function_reads != 0) == (result_.self_references != 0) &&
        tree_.self.has_value() == (result_.frame_function_reads != 0), Error::kEntry);
    // A JS arrow inherits this; a VM receiver entry is dynamic. Captured outer
    // receivers must instead retain their explicit enclosing-owner binding.
    Require(!IsArrowCallable(tree_.callable) ||
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
    Require(tree_.parameter_form == ParameterForm::kFrameFormals &&
        value.index < result_.parameters.size(), Error::kParameter);
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
    Require(tree_.parameter_form == ParameterForm::kFrameFormals, Error::kEntry);
    snapshot_ = true;
    Require(graph_.arguments && graph_.arguments->frame == frame_ &&
        node.owner == tree_.owner && tree_.strictness == Strictness::kStrict &&
        (!IsArrowCallable(tree_.callable) || tree_.invocation == InvocationForm::kArrowMethod) &&
        node.slots.count == frame_.parameters &&
        ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded,
        Error::kEntry);
  }
  void Expression(const AssignmentExpr& node) {
    if (const auto* target = std::get_if<SymbolTarget>(&node.target)) Symbol(target->symbol, true);
  }
  void Expression(const UpdateExpr& node) {
    Symbol(node.symbol, false); Symbol(node.symbol, true);
  }
  template <class T> void Expression(const T&) {
    static_assert(std::is_same_v<T, TreeLiteral> || std::is_same_v<T, MemberExpr> ||
        std::is_same_v<T, GlobalNameExpr> ||
        std::is_same_v<T, RealmObjectExpr> ||
        std::is_same_v<T, SymbolConstructorExpr> ||
        std::is_same_v<T, UnaryExpr> || std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr> || std::is_same_v<T, BinaryExpr> ||
        std::is_same_v<T, SequenceExpr> || std::is_same_v<T, LogicalExpr> || std::is_same_v<T, ConditionalExpr> || std::is_same_v<T, CallExpr> ||
        std::is_same_v<T, ConstructExpr> || std::is_same_v<T, SuperCallExpr> || std::is_same_v<T, EmptyArrayExpr> ||
        std::is_same_v<T, EmptyObjectExpr> || std::is_same_v<T, ObjectExpr> || std::is_same_v<T, ArrayExpr> || std::is_same_v<T, ArrayElement> || std::is_same_v<T, FunctionExpr> || std::is_same_v<T, ClassExpr> || std::is_same_v<T, FieldDefinitionExpr>);
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
