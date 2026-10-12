#include "function_input.h"

#include <new>
#include <stdexcept>
#include <algorithm>
#include <bit>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = FunctionInputError;
using analysis::ModuleInitializationError;
using analysis::ModuleInitialization;
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
    std::size_t allocated_bytes, std::optional<Owner> owner,
    std::vector<EntryModuleStore>&& stores, std::vector<InputModuleRead>&& modules,
    ModuleSlotSet entry_modules, std::vector<analysis::ModuleStateSnapshot>&& creations)
    : identity_(identity), method_(method), record_(record), mode_(mode),
      input_(std::move(input)), proof_(std::move(proof)), captures_(std::move(captures)),
      accesses_(std::move(accesses)), allocated_bytes_(allocated_bytes), owner_(owner),
      module_stores_(std::move(stores)), modules_(std::move(modules)),
      entry_modules_(std::move(entry_modules)), module_creations_(std::move(creations)) {}

std::optional<analysis::InstructionId> FunctionInput::ModuleStoreBefore(
    analysis::ModuleSlot slot, analysis::InstructionId creation) const {
  if (creation.value >= input_.instructions.size() ||
      (!std::holds_alternative<analysis::FunctionCreation>(input_.instructions[creation.value].operation) &&
       !std::holds_alternative<analysis::ClassCreation>(input_.instructions[creation.value].operation)))
    return {};
  const auto found = std::lower_bound(module_stores_.begin(), module_stores_.end(), slot,
      [](const auto& store, auto sought) { return store.slot < sought; });
  if (found == module_stores_.end() || found->slot != slot || found->instruction >= creation) return {};
  return found->instruction;
}
ModuleSlotSet FunctionInput::InitializedModulesBefore(analysis::InstructionId creation) const {
  if (creation.value >= input_.instructions.size() ||
      (!std::holds_alternative<analysis::FunctionCreation>(input_.instructions[creation.value].operation) &&
       !std::holds_alternative<analysis::ClassCreation>(input_.instructions[creation.value].operation))) return {};
  const auto found = std::lower_bound(module_creations_.begin(), module_creations_.end(), creation,
      [](const auto& state, auto site) { return state.instruction < site; });
  return found != module_creations_.end() && found->instruction == creation ? found->slots : ModuleSlotSet{};
}
bool FunctionInput::MatchesModuleCreation(const FunctionInput& parent, analysis::InstructionId creation) const {
  if (!owner_ || identity_ != parent.identity_ || record_ != parent.record_ ||
      owner_->creator != parent.method_ || creation.value >= parent.input_.instructions.size() ||
      creation.value >= parent.proof_.environments.before.size() ||
      !parent.proof_.environments.before[creation.value]) return false;
  const auto& instruction = parent.input_.instructions[creation.value];
  if (owner_->pc != instruction.pc) return false;
  return std::visit([&](const auto& expected) {
    const auto* actual = std::get_if<std::decay_t<decltype(expected)>>(&instruction.operation);
    return actual && *actual == expected;
  }, owner_->creation);
}

bool FunctionInput::IsCreatorOf(const FunctionContext& child, abc::EntityOffset child_record,
    analysis::InstructionId creation) const {
  const auto owner = DescribeOwner(child);
  if (identity_ != child.Identity() || record_ != child_record || owner.creator != method_ ||
      creation.value >= input_.instructions.size() || creation.value >= proof_.environments.before.size() ||
      !proof_.environments.before[creation.value]) return false;
  const auto& instruction = input_.instructions[creation.value];
  if (instruction.pc != owner.pc) return false;
  return std::visit([&](const auto& expected) {
    const auto* actual = std::get_if<std::decay_t<decltype(expected)>>(&instruction.operation);
    return actual && *actual == expected;
  }, owner.creation);
}

FunctionInput::Owner FunctionInput::DescribeOwner(const FunctionContext& owner) {
  if (const auto* site = owner.ClassSite())
    return {site->creator, site->pc, site->property, owner.Callable(), owner.Mode(), owner.Frame(), site->creation};
  if (const auto* site = owner.ConstructorSite())
    return {site->creator, site->pc, {}, owner.Callable(), owner.Mode(), owner.Frame(), site->creation};
  const auto* site = owner.CreationSite();
  return {site->creator, site->pc, {}, owner.Callable(), owner.Mode(), owner.Frame(), site->creation};
}
bool FunctionInput::MatchesOwner(const FunctionContext& owner) const {
  return owner_ && identity_ == owner.Identity() && method_ == owner.Target() &&
      *owner_ == DescribeOwner(owner);
}

std::variant<FunctionInput, FunctionInputFailure> FunctionInput::Build(
    RouteInputIdentity identity, abc::EntityOffset method, abc::EntityOffset record,
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    InputCaptureMode mode, analysis::SemanticBudget budget, const FunctionContext* owner,
    const ParentModuleInput* module_input) {
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
        case CallableKind::kGenerator:
        case CallableKind::kGeneratorMethod:
        case CallableKind::kAsyncGenerator:
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
    ModuleSlotSet entry_modules;
    if (module_input) {
      // Match an actual function/class allocation before sharing membership.
      // A resolved class-buffer member retains that allocation's wire operands.
      const auto& parent = module_input->parent;
      if (mode != InputCaptureMode::kConditional || !owner ||
          !parent.IsCreatorOf(*owner, record, module_input->creation)) throw Error::kModuleOwner;
      quota.Step(parent.ModuleInitializationWork());
      entry_modules = parent.InitializedModulesBefore(module_input->creation);
    }
    auto lifted = analysis::BuildSemanticMethod(body, identity.profile,
        frame.registers, quota.Remaining(), {}, &frame);
    auto* input = std::get_if<analysis::SemanticMethod>(&lifted);
    if (!input) return FunctionInputFailure{std::get<analysis::SemanticFailure>(lifted)};
    // An initialized entry premise is not a proof across receiver-changing
    // protocols. Super/resume ownership must supply a phase-specific proof.
    for (const auto& instruction : input->instructions) {
      quota.Step();
      if ((std::holds_alternative<analysis::SuperForward>(instruction.operation) ||
          std::holds_alternative<analysis::SuperRange>(instruction.operation) ||
          std::holds_alternative<analysis::SuperSpread>(instruction.operation)))
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
    std::vector<analysis::LanguageLoadFact> loads;
    std::vector<InputModuleRead> module_reads;
    if (module_input) {
      const auto guarded = [&](std::size_t i) {
        if (i + 1 >= input->instructions.size()) return false;
        const auto* guard = std::get_if<analysis::HoleGuard>(&input->instructions[i + 1].operation);
        if (!guard || !std::holds_alternative<analysis::Accumulator>(guard->value) ||
            !std::holds_alternative<analysis::HoleNameSlot>(guard->name)) return false;
        quota.Step(std::bit_width(input->flow.blocks.size()) + 1);
        const auto found = std::upper_bound(input->flow.blocks.begin(), input->flow.blocks.end(), i,
            [](auto site, const auto& block) { return site < block.first_instruction; });
        if (found == input->flow.blocks.begin()) return false;
        const auto& block = *std::prev(found);
        return i + 1 < std::size_t(block.first_instruction) + block.instruction_count;
      };
      std::size_t count = 0;
      for (std::size_t i = 0; i < input->instructions.size(); ++i) {
        quota.Step();
        const auto* load = std::get_if<analysis::ModuleLoad>(&input->instructions[i].operation);
        // Preserve the established guarded route where it already applies.
        // Initialization is needed only for a raw read without that pair.
        if (!load || load->space != analysis::ModuleSpace::kLocal || guarded(i)) continue;
        quota.Step(ModuleSlotSet::kPathWork);
        count += entry_modules.Contains(load->slot);
      }
      quota.Reserve(loads, count); quota.Reserve(module_reads, count);
      for (std::size_t i = 0; i < input->instructions.size(); ++i) {
        quota.Step();
        const auto* load = std::get_if<analysis::ModuleLoad>(&input->instructions[i].operation);
        if (!load || load->space != analysis::ModuleSpace::kLocal || guarded(i)) continue;
        quota.Step(ModuleSlotSet::kPathWork + std::bit_width(module_input->parent.module_stores_.size()) + 1);
        const auto store = module_input->parent.ModuleStoreBefore(load->slot, module_input->creation);
        if (!entry_modules.Contains(load->slot)) continue;
        const analysis::InstructionId site{static_cast<std::uint32_t>(i)};
        loads.push_back({site});
        module_reads.push_back({site, load->slot, context->creator, context->pc, store});
      }
    }
    auto verified = analysis::VerifySemanticSource(body, *input, frame.registers, frame.locals,
        quota.Remaining(), captures, loads, writes, {}, NormalCallResults(identity.contract), receiver,
        arguments ? &*arguments : nullptr, Arithmetic(identity.contract));
    auto* proof = std::get_if<analysis::SemanticProof>(&verified);
    if (!proof) {
      auto failure = std::get<analysis::SemanticProofFailure>(verified);
      failure.allocated_bytes += budget.bytes - quota.Remaining().bytes;
      failure.work_units += budget.work - quota.Remaining().work;
      return FunctionInputFailure{failure};
    }
    quota.Admit(proof->allocated_bytes, 1);
    // Normal entry dominates all reachable blocks. Its completed Language
    // stores initialize a module slot before later creations; no name/metadata
    // inference, branch-local initialization or per-child parent scan.
    std::vector<EntryModuleStore> stores;
    const auto& entry = input->flow.blocks.front();
    if (entry.first_instruction != 0 || entry.instruction_count > input->instructions.size())
      throw Error::kModuleOwner;
    std::size_t store_count = 0;
    for (std::size_t i = 0; i < entry.instruction_count; ++i) {
      quota.Step(); store_count += std::holds_alternative<analysis::ModuleStore>(input->instructions[i].operation);
    }
    quota.Reserve(stores, store_count);
    for (std::size_t i = 0; i < entry.instruction_count; ++i) {
      quota.Step();
      if (const auto* store = std::get_if<analysis::ModuleStore>(&input->instructions[i].operation))
        stores.push_back({store->slot, {static_cast<std::uint32_t>(i)}});
    }
    std::sort(stores.begin(), stores.end(), [&](const auto& a, const auto& b) {
      quota.Step(); return a.slot < b.slot || (a.slot == b.slot && a.instruction < b.instruction);
    });
    stores.erase(std::unique(stores.begin(), stores.end(), [&](const auto& a, const auto& b) {
      quota.Step(); return a.slot == b.slot;
    }), stores.end());
    auto initialized = analysis::BuildModuleInitialization(*input, entry_modules, quota.Remaining());
    if (const auto* error = std::get_if<ModuleInitializationError>(&initialized)) {
      switch (*error) {
        case ModuleInitializationError::kWorkLimit: throw Error::kWorkLimit;
        case ModuleInitializationError::kResourceLimit: throw Error::kResourceLimit;
        case ModuleInitializationError::kInput: throw Error::kModuleOwner;
      }
    }
    auto states = std::get<ModuleInitialization>(std::move(initialized));
    quota.Admit(states.allocated_bytes, 1); quota.Step(states.work);
    // The common analysis also answers same-body reads. This owner retains
    // only child-creation snapshots; original read proof lives in SemanticProof.
    std::erase_if(states.states, [&](const auto& state) {
      quota.Step(); const auto& operation=input->instructions[state.instruction.value].operation;
      return !std::holds_alternative<analysis::FunctionCreation>(operation) &&
          !std::holds_alternative<analysis::ClassCreation>(operation);
    });
    std::vector<CaptureUse> uses;
    std::vector<InputCaptureAccess> accesses;
    std::size_t count = 0;
    for (const auto& binding : proof->environments.bindings) {
      quota.Step(); count += std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment);
    }
    quota.Reserve(uses, count); quota.Reserve(accesses, count);
    // A captured load is Guarded when, in its own block and on its only path,
    // nothing but effect-free register copies separates it from a Hole guard
    // of the unchanged accumulator. No copy is read and no handler can observe
    // a copy before the guard completes, so Hole never escapes the load/guard
    // pair. Methods with try regions keep the strict Language obligation.
    const auto guarded_load = [&](std::uint32_t site) {
      if (!body.tries.empty() || !std::holds_alternative<analysis::LexicalLoad>(input->instructions[site].operation))
        return false;
      quota.Step(std::bit_width(input->flow.blocks.size()) + 1);
      const auto found = std::upper_bound(input->flow.blocks.begin(), input->flow.blocks.end(), site,
          [](auto value, const auto& block) { return value < block.first_instruction; });
      if (found == input->flow.blocks.begin()) return false;
      const auto& block = *std::prev(found);
      const auto end = std::size_t(block.first_instruction) + block.instruction_count;
      for (auto i = std::size_t(site) + 1; i < end; ++i) {
        quota.Step();
        const auto& instruction = input->instructions[i];
        if (const auto* guard = std::get_if<analysis::HoleGuard>(&instruction.operation))
          return std::holds_alternative<analysis::Accumulator>(guard->value) &&
              std::holds_alternative<analysis::HoleNameSlot>(guard->name);
        const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
        const auto* source = copy ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
        if (!source || !std::holds_alternative<abc::RegisterId>(copy->destination) ||
            !std::holds_alternative<analysis::Accumulator>(*source) ||
            instruction.effects.normal != analysis::LanguageEffect::kNone ||
            instruction.effects.exceptional != analysis::ExceptionalCompletion::kImpossible) return false;
      }
      return false;
    };
    for (const auto& binding : proof->environments.bindings) {
      quota.Step();
      const auto* depth = std::get_if<analysis::CapturedDepth>(&binding.binding.environment);
      if (!depth) continue;
      const auto kind = Access(*input, binding.instruction);
      const auto domain = kind == analysis::LocalAccessKind::kLoad && guarded_load(binding.instruction)
          ? analysis::CaptureDomain::kGuarded : analysis::CaptureDomain::kLanguage;
      uses.push_back({{*depth, binding.binding.slot}, domain});
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
        uses.capacity() * sizeof(CaptureUse) + accesses.capacity() * sizeof(InputCaptureAccess) +
        stores.capacity() * sizeof(EntryModuleStore) + module_reads.capacity() * sizeof(InputModuleRead) +
        states.allocated_bytes;
    return FunctionInput(identity, method, record, mode, std::move(*input),
        std::move(*proof), std::move(uses), std::move(accesses), retained, context,
        std::move(stores), std::move(module_reads), std::move(entry_modules), std::move(states.states));
  } catch (Error error) { return FunctionInputFailure{error};
  } catch (const std::bad_alloc&) { return FunctionInputFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FunctionInputFailure{Error::kResourceLimit}; }
}

std::variant<std::uint64_t, FunctionInputError> VerifyInitializedModuleReads(
    const FunctionInput& parent, analysis::InstructionId creation, const FunctionInput& child,
    analysis::SemanticBudget budget) {
  try {
    InputBudget quota(budget);
    if (child.InitializedModuleReads().empty() && !child.HasInheritedModules()) return std::uint64_t{0};
    if (!child.HasOwner() || parent.Identity() != child.Identity() || parent.Record() != child.Record() ||
        creation.value >= parent.Input().instructions.size()) throw Error::kModuleOwner;
    const auto& occurrence = parent.Input().instructions[creation.value];
    const auto edge_work = std::bit_width(parent.module_creations_.size()) + 1;
    quota.Step(edge_work);
    if (!child.MatchesModuleCreation(parent, creation) ||
        !child.entry_modules_.Same(parent.InitializedModulesBefore(creation))) throw Error::kModuleOwner;
    const auto& entry = parent.Input().flow.blocks.front();
    std::size_t cursor = 0;
    std::uint64_t work = edge_work;
    for (const auto& fact : child.Proof().module_reads) {
      quota.Step(); ++work;
      if (fact.proof != analysis::ModuleReadProof::kInitialized) continue;
      const auto premises = child.InitializedModuleReads();
      if (cursor >= premises.size()) throw Error::kModuleOwner;
      const auto& premise = premises[cursor++];
      const auto cost = std::bit_width(parent.Input().instructions.size()) + 1;
      quota.Step(cost + ModuleSlotSet::kPathWork); work += cost + ModuleSlotSet::kPathWork;
      const auto store = parent.ModuleStoreBefore(premise.slot, creation);
      if (premise.instruction != fact.instruction || premise.creator != parent.Method() ||
          premise.creation_pc != occurrence.pc || store != premise.store ||
          premise.instruction.value >= child.Input().instructions.size() ||
          entry.first_instruction != 0)
        throw Error::kModuleOwner;
      const auto* load = std::get_if<analysis::ModuleLoad>(&child.Input().instructions[premise.instruction.value].operation);
      if (!load || load->space != analysis::ModuleSpace::kLocal || load->slot != premise.slot ||
          !child.entry_modules_.Contains(premise.slot)) throw Error::kModuleOwner;
      if (!premise.store) continue; // Complete inherited root identity was checked above.
      if (premise.store->value >= parent.Input().instructions.size() ||
          premise.store->value >= entry.instruction_count || *premise.store >= creation) throw Error::kModuleOwner;
      const auto* original = std::get_if<analysis::ModuleStore>(&parent.Input().instructions[premise.store->value].operation);
      if (!original || original->slot != premise.slot) throw Error::kModuleOwner;
      quota.Step(cost); work += cost;
      const auto reads = parent.Input().ssa.Reads(*premise.store);
      if (reads.size() != 1) throw Error::kModuleOwner;
      const auto kinds = parent.Proof().values.At(reads.front().value);
      if (!kinds || !kinds->IsLanguage()) throw Error::kModuleOwner;
    }
    if (cursor != child.InitializedModuleReads().size()) throw Error::kModuleOwner;
    return work;
  } catch (Error error) { return error; }
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
  if (const auto* error = std::get_if<CaptureFailure>(&bound)) return *error;
  auto result = std::get<CapturePlan>(std::move(bound));
  result.work += kOwnerCheckWork;
  return result;
}
}  // namespace abcd::source
