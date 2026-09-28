#include "function_input.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = FunctionInputError;
class InputBudget {
 public:
  explicit InputBudget(analysis::SemanticBudget budget) : remaining_(budget) {}
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw Error::kResourceLimit;
    remaining_.bytes -= count * width;
  }
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) throw Error::kWorkLimit;
    remaining_.work -= count;
  }
  template <class T> void Reserve(std::vector<T>& items, std::size_t count) {
    Admit(count, sizeof(T)); items.reserve(count); Admit(items.capacity() - count, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
 private:
  analysis::SemanticBudget remaining_;
};
analysis::LocalAccessKind Access(const analysis::SemanticMethod& input, std::uint32_t site) {
  if (site >= input.instructions.size()) throw Error::kCapture;
  const auto& operation = input.instructions[site].operation;
  if (std::holds_alternative<analysis::LexicalLoad>(operation)) return analysis::LocalAccessKind::kLoad;
  if (std::holds_alternative<analysis::LexicalStore>(operation)) return analysis::LocalAccessKind::kStore;
  throw Error::kCapture;
}
}  // namespace

FunctionInput::FunctionInput(RouteInputIdentity identity, abc::EntityOffset method,
    abc::EntityOffset record, InputCaptureMode mode,
    analysis::SemanticMethod&& input, analysis::SemanticProof&& proof,
    std::vector<CaptureUse>&& captures, std::vector<InputCaptureAccess>&& accesses,
    std::size_t allocated_bytes, std::optional<Owner> owner)
    : identity_(identity), method_(method), record_(record), mode_(mode),
      input_(std::move(input)), proof_(std::move(proof)), captures_(std::move(captures)),
      accesses_(std::move(accesses)), allocated_bytes_(allocated_bytes), owner_(owner) {}

FunctionInput::Owner FunctionInput::DescribeOwner(const FunctionContext& owner) {
  if (const auto* site = owner.ClassSite())
    return {site->creator, site->pc, site->property, owner.Callable(), owner.Mode(), owner.Frame()};
  const auto* site = owner.CreationSite();
  return {site->creator, site->pc, {}, owner.Callable(), owner.Mode(), owner.Frame()};
}
bool FunctionInput::MatchesOwner(const FunctionContext& owner) const {
  return owner_ && identity_ == owner.Identity() && method_ == owner.Target() &&
      *owner_ == DescribeOwner(owner);
}

std::variant<FunctionInput, FunctionInputFailure> FunctionInput::Build(
    RouteInputIdentity identity, abc::EntityOffset method, abc::EntityOffset record,
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    InputCaptureMode mode, analysis::SemanticBudget budget, const FunctionContext* owner) {
  try {
    if (owner && (owner->Identity() != identity || owner->Target() != method ||
        owner->Frame() != frame)) return FunctionInputFailure{Error::kOwner};
    const auto context = owner ? std::optional{DescribeOwner(*owner)} : std::nullopt;
    auto receiver = analysis::FrameReceiver::kUnknown;
    if (owner) {
      switch (owner->Callable()) {
        case CallableKind::kFunction:
        case CallableKind::kMethod:
        case CallableKind::kBaseConstructor:
          receiver = analysis::FrameReceiver::kInitialized;
          break;
        case CallableKind::kArrow:
        case CallableKind::kAsyncArrow:
        case CallableKind::kDerivedConstructor:
        case CallableKind::kAsyncFunction:
        case CallableKind::kAsyncMethod:
          break;
      }
    }
    analysis::CapturedValues captures;
    analysis::ClosureWrites writes;
    switch (mode) {
      case InputCaptureMode::kClosed:
        captures = analysis::CapturedValues::kReject; writes = analysis::ClosureWrites::kUnknown; break;
      case InputCaptureMode::kConditional:
        captures = analysis::CapturedValues::kLanguage; writes = analysis::ClosureWrites::kLanguage; break;
      default: throw Error::kMode;
    }
    InputBudget quota(budget); quota.Admit(1, sizeof(FunctionInput));
    auto lifted = analysis::BuildSemanticMethod(body, identity.profile,
        frame.registers, quota.Remaining(), {}, &frame);
    auto* input = std::get_if<analysis::SemanticMethod>(&lifted);
    if (!input) return FunctionInputFailure{std::get<analysis::SemanticFailure>(lifted)};
    // An initialized entry premise is not a proof across receiver-changing
    // protocols. Super/resume ownership must supply a phase-specific proof.
    for (const auto& instruction : input->instructions) {
      quota.Step();
      if (std::holds_alternative<analysis::SuperRange>(instruction.operation))
        receiver = analysis::FrameReceiver::kUnknown;
    }
    // The upstream proof budget includes its input method. Do not subtract it
    // twice; retain that full charge after construction before allocating uses.
    // Actual count/tail belong to the validated current callable, including
    // arrows' own VM invocation (not the enclosing source arguments binding).
    // Legal JS entry values are the existing ABI premise; explicit bytecode
    // callers must still prove all call operands. Retain owner_ for publication.
    const auto arguments = owner && frame.extra_arguments
        ? std::optional{analysis::ArgumentFrameFact{frame}} : std::nullopt;
    auto verified = analysis::VerifySemanticSource(body, *input, frame.registers, frame.locals,
        quota.Remaining(), captures, {}, writes, {}, NormalCallResults(identity.contract), receiver,
        arguments ? &*arguments : nullptr);
    auto* proof = std::get_if<analysis::SemanticProof>(&verified);
    if (!proof) return FunctionInputFailure{std::get<analysis::SemanticProofFailure>(verified)};
    quota.Admit(proof->allocated_bytes, 1);
    std::vector<CaptureUse> uses;
    std::vector<InputCaptureAccess> accesses;
    std::size_t count = 0;
    for (const auto& binding : proof->environments.bindings) {
      quota.Step(); count += std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment);
    }
    quota.Reserve(uses, count); quota.Reserve(accesses, count);
    for (const auto& binding : proof->environments.bindings) {
      quota.Step();
      const auto* depth = std::get_if<analysis::CapturedDepth>(&binding.binding.environment);
      if (!depth) continue;
      const auto kind = Access(*input, binding.instruction);
      uses.push_back({{*depth, binding.binding.slot}, analysis::CaptureDomain::kLanguage});
      accesses.push_back({{binding.instruction}, kind});
    }
    // Independently match the verifier's conditional read ledger. Stores are
    // separate obligations, never evidence that a read was initialized.
    std::size_t read = 0;
    for (std::size_t i = 0; i < uses.size(); ++i) {
      quota.Step();
      const auto& access = accesses[i];
      if (i != 0 && accesses[i - 1].instruction >= access.instruction) throw Error::kCapture;
      if (access.kind != analysis::LocalAccessKind::kLoad) continue;
      // Closed verification can retain an unused raw load without treating it
      // as Language. Only conditional mode must have a matching read premise.
      if (mode == InputCaptureMode::kClosed) continue;
      if (read >= proof->captured_reads.size()) throw Error::kCapture;
      const auto& actual = proof->captured_reads[read++];
      const auto expected = uses[i].cell;
      if (actual.instruction != access.instruction || actual.cell.depth != expected.depth ||
          actual.cell.slot.value != expected.slot.value) throw Error::kCapture;
    }
    if (read != proof->captured_reads.size()) throw Error::kCapture;
    const auto retained = sizeof(FunctionInput) + proof->allocated_bytes +
        uses.capacity() * sizeof(CaptureUse) + accesses.capacity() * sizeof(InputCaptureAccess);
    return FunctionInput(identity, method, record, mode, std::move(*input),
        std::move(*proof), std::move(uses), std::move(accesses), retained, context);
  } catch (Error error) { return FunctionInputFailure{error};
  } catch (const std::bad_alloc&) { return FunctionInputFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FunctionInputFailure{Error::kResourceLimit}; }
}

InputBindingResult BindFunctionInputs(const abc::MetadataIndex& index, const FunctionInput& parent,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const FunctionInput& child, analysis::SemanticBudget budget) {
  constexpr std::uint64_t kOwnerCheckWork = 1;
  if (budget.work < kOwnerCheckWork) return CaptureError::kWorkLimit;
  budget.work -= kOwnerCheckWork;
  if (parent.Identity() != child.Identity() || parent.Record() != child.Record()) return InputBindingError::kIdentity;
  if (child.Mode() != InputCaptureMode::kConditional) return InputBindingError::kMode;
  if (creation.target.position.value >= index.methods.size()) return InputBindingError::kTarget;
  const auto& target = index.methods[creation.target.position.value];
  if (target.offset != child.Method() || target.owner_offset != child.Record()) return InputBindingError::kTarget;
  auto bound = BuildCapturePlan(ScopeFacts(parent.Input(), parent.Proof()), site, creation, child.Captures(), budget);
  if (const auto* error = std::get_if<CaptureError>(&bound)) return *error;
  auto result = std::get<CapturePlan>(std::move(bound));
  result.work += kOwnerCheckWork;
  return result;
}
}  // namespace abcd::source
