#include "phased_function_file.h"
#include "concurrent_async.h"

#include <bit>
#include <new>
#include <stdexcept>

#include "module_aliases.h"
#include "abcd/analysis/hole_guard.h"
#include "generator_completion.h"

namespace abcd::source {
namespace {
struct PhasedOwner {
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  std::optional<std::uint32_t> property;
  CallableKind callable;
  Strictness mode;
  abc::FrameLayout frame;
  std::uint32_t length;
  std::variant<analysis::FunctionCreation, analysis::ClassCreation> creation;
  bool operator==(const PhasedOwner&) const = default;
};
PhasedOwner DescribeOwner(const FunctionContext& context) {
  if (const auto* site = context.ClassSite())
    return {site->creator, site->pc, site->property, context.Callable(), context.Mode(), context.Frame(), context.Length(), site->creation};
  if (const auto* site = context.ConstructorSite())
    return {site->creator, site->pc, {}, context.Callable(), context.Mode(), context.Frame(), context.Length(), site->creation};
  const auto* site = context.CreationSite();
  return {site->creator, site->pc, {}, context.Callable(), context.Mode(), context.Frame(), context.Length(), site->creation};
}
}  // namespace
struct FilePhasedFunction::Storage {
  abc::MethodBody body;
  std::optional<abc::MethodBody> original;
  PhasedBody phases;
  analysis::ExceptionLocals locals;
  analysis::GuardedReads guarded;
  std::vector<analysis::InstructionId> children;
  InputCaptureMode mode;
  std::vector<CaptureUse> captures;
  std::vector<InputCaptureAccess> accesses;
  PhasedOwner owner;
  ModuleSlotSet entry_modules;
  std::vector<InputModuleRead> module_reads;
  abc::EntityOffset record;
  std::size_t bytes;
  std::uint64_t work;
};
FilePhasedFunction::FilePhasedFunction(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
FilePhasedFunction::FilePhasedFunction(FilePhasedFunction&&) noexcept = default;
FilePhasedFunction& FilePhasedFunction::operator=(FilePhasedFunction&&) noexcept = default;
FilePhasedFunction::~FilePhasedFunction() = default;
const abc::MethodBody& FilePhasedFunction::Body() const { return storage_->body; }
const abc::MethodBody& FilePhasedFunction::OriginalBody() const {
  return storage_->original ? *storage_->original : storage_->body;
}
const PhasedBody& FilePhasedFunction::Phases() const { return storage_->phases; }
const analysis::ExceptionLocals& FilePhasedFunction::Locals() const { return storage_->locals; }
const analysis::GuardedReads& FilePhasedFunction::GuardedReads() const { return storage_->guarded; }
std::span<const analysis::InstructionId> FilePhasedFunction::Children() const { return storage_->children; }
InputCaptureMode FilePhasedFunction::Mode() const { return storage_->mode; }
std::span<const CaptureUse> FilePhasedFunction::Captures() const { return storage_->captures; }
std::span<const InputCaptureAccess> FilePhasedFunction::CaptureAccesses() const { return storage_->accesses; }
bool FilePhasedFunction::MatchesOwner(const FunctionContext& context) const {
  return storage_->phases.Identity() == context.Identity() && storage_->phases.Target() == context.Target() &&
      storage_->owner == DescribeOwner(context);
}
std::span<const InputModuleRead> FilePhasedFunction::InitializedModuleReads() const { return storage_->module_reads; }
const ModuleSlotSet& FilePhasedFunction::EntryModules() const { return storage_->entry_modules; }
bool FilePhasedFunction::MatchesModuleCreation(const FunctionInput& parent, analysis::InstructionId creation) const {
  const auto& owner = storage_->owner;
  if (Phases().Identity() != parent.Identity() || Record() != parent.Record() || owner.creator != parent.Method() ||
      creation.value >= parent.Input().instructions.size() || creation.value >= parent.Proof().environments.before.size() ||
      !parent.Proof().environments.before[creation.value]) return false;
  const auto& instruction = parent.Input().instructions[creation.value];
  if (owner.pc != instruction.pc) return false;
  return std::visit([&](const auto& expected) {
    const auto* actual = std::get_if<std::decay_t<decltype(expected)>>(&instruction.operation);
    return actual && *actual == expected;
  }, owner.creation);
}
abc::EntityOffset FilePhasedFunction::Record() const { return storage_->record; }
std::size_t FilePhasedFunction::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t FilePhasedFunction::Work() const { return storage_->work; }
namespace {
using Error = PhasedFileError;
void Require(bool ok, Error error) { if (!ok) throw PhasedFileFailure{error}; }
template<class T, class... E> T Take(std::variant<T, E...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::visit([](const auto& error) {
    if constexpr (!std::is_same_v<T, std::decay_t<decltype(error)>>) throw PhasedFileFailure{error};
  }, result);
  throw PhasedFileFailure{Error::kIdentity};
}
}  // namespace
std::variant<FilePhasedFunction, PhasedFileFailure> FilePhasedFunction::Read(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const FunctionContext& context, analysis::SemanticBudget budget, const PhasedModuleBinding* modules,
    InputCaptureMode mode, const ParentModuleInput* parent_modules) {
  try {
    auto left = budget;
    const auto admit = [&](std::size_t count, std::size_t width = 1) {
      Require(count <= left.bytes / width, Error::kResourceLimit); left.bytes -= count * width;
    };
    const auto step = [&](std::uint64_t work) {
      Require(work <= left.work, Error::kWorkLimit); left.work -= work;
    };
    const auto arena = [&]<class T>(const std::vector<T>& values) { admit(values.capacity(), sizeof(T)); };
    const auto& identity = context.Identity();
    Require(identity.profile == header.profile && identity.abc_version == header.version &&
        identity.file_size == header.file_size, Error::kIdentity);
    Require(file.Size() == header.file_size && !file.CheckUnchanged(), Error::kChanged);
    const auto position = context.Position().value;
    Require(position < index.methods.size() && index.methods[position].offset == context.Target() &&
        index.methods[position].code.has_value(), Error::kMethod);
    const auto& method = index.methods[position];
    if (modules) {
      // One reader/binder transaction. A descriptor from another file or
      // metadata snapshot must not smuggle unrelated module aliases into it.
      Require(&modules->file == &file && &modules->header == &header && &modules->index == &index &&
          modules->record.record() == method.owner_offset, Error::kIdentity);
    }
    admit(sizeof(Storage));
    const auto call = Take<abc::CallType>(abc::ReadCallType(file, header, index, method.annotations, {left.bytes}));
    const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers,
        method.code->arguments, abc::DecodeCallFlags(call.value)));
    Require(frame == context.Frame(), Error::kFrame);
    step(method.code->instruction_bytes);
    auto body = Take<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *method.code, {left.bytes}));
    arena(body.instructions); arena(body.tries); arena(body.handlers);
    std::optional<abc::MethodBody> original;
    std::vector<abc::CodeOffset> source_pcs;
    std::vector<analysis::AwaitSite> awaits;
    std::vector<analysis::YieldSite> yields;
    if (IsGeneratorCallable(context.Callable())) {
      const GeneratorNames names{file, header, index, method.offset};
      auto normalization = NormalizeGenerator(body, frame, header.profile,
          ResourceFailures(identity.contract), left.bytes, &names,
          context.Callable() == CallableKind::kAsyncGenerator ? GeneratorExecution::kAsynchronous : GeneratorExecution::kSynchronous);
      if (const auto* error = std::get_if<FunctionEmitError>(&normalization)) throw PhasedFileFailure{*error};
      if (const auto* error = std::get_if<abc::MetadataError>(&normalization)) throw PhasedFileFailure{*error};
      auto normalized = std::get<GeneratorBody>(std::move(normalization));
      arena(normalized.body.instructions); arena(normalized.source_pcs); arena(normalized.yields); arena(normalized.awaits);
      arena(normalized.body.tries); arena(normalized.body.handlers);
      source_pcs = std::move(normalized.source_pcs); yields = std::move(normalized.yields); awaits = std::move(normalized.awaits);
      original.emplace(std::move(body)); body = std::move(normalized.body);
    } else if (IsAsyncCallable(context.Callable())) {
      Require(identity.contract == SourceContract::kLanguageAndHostV2 ||
          identity.contract == SourceContract::kLanguageAndHostV3 ||
          identity.contract == SourceContract::kLanguageArithmeticV1 ||
          identity.contract == SourceContract::kLanguageNumericV1 ||
          identity.contract == SourceContract::kLanguageConstructorsV1 ||
          identity.contract == SourceContract::kLanguageArithmeticConstructorsV1 ||
          identity.contract == SourceContract::kLanguageNumericConstructorsV1 ||
          identity.contract == SourceContract::kLanguageSourceV1 ||
          identity.contract == SourceContract::kLanguageSourceV2 ||
          (identity.contract == SourceContract::kLanguageSourceV3 || identity.contract == SourceContract::kLanguageSourceV4), Error::kIdentity);
      AsyncCompletion completion = FunctionEmitError::kKind;
      if (context.Concurrent()) {
        auto projected = NormalizeConcurrentAsync(body, frame, header.profile, left);
        if (auto* failure = std::get_if<ConcurrentFailure>(&projected)) {
          failure->method = method.offset; failure->record = context.Concurrent()->location.record;
          throw PhasedFileFailure{*failure};
        }
        if (auto* failure = std::get_if<AsyncCompletionFailure>(&projected)) {
          failure->async.location.method = method.offset; throw PhasedFileFailure{*failure};
        }
        auto normalized = std::get<ConcurrentAsyncBody>(std::move(projected));
        // Includes normalization scratch as well as returned arenas. Charge
        // once here; ordinary async keeps its historical accounting below.
        admit(normalized.allocated_bytes); step(normalized.work);
        source_pcs = std::move(normalized.origins); awaits = std::move(normalized.awaits);
        completion = std::move(normalized.body);
      } else completion = ProveAsyncCompletion(body, frame, left.bytes, &source_pcs, &awaits,
          header.profile, ResourceFailureScope::kExcluded);
      if (auto* failure = std::get_if<AsyncCompletionFailure>(&completion)) {
        failure->async.location.method = method.offset;
        throw PhasedFileFailure{*failure};
      }
      auto normalized = std::get<abc::MethodBody>(std::move(completion));
      if (!context.Concurrent()) {
        arena(normalized.instructions); arena(normalized.tries); arena(normalized.handlers);
        arena(source_pcs); arena(awaits);
      }
      original.emplace(std::move(body)); body = std::move(normalized);
    }
    Require(mode == InputCaptureMode::kClosed || mode == InputCaptureMode::kConditional, Error::kIdentity);
    ModuleSlotSet entry_modules;
    std::vector<analysis::LanguageLoadFact> initialized;
    std::vector<InputModuleRead> module_reads;
    if (parent_modules) {
      Require(mode == InputCaptureMode::kConditional &&
          parent_modules->parent.IsCreatorOf(context, method.owner_offset, parent_modules->creation), Error::kIdentity);
      step(parent_modules->parent.ModuleInitializationWork());
      entry_modules = parent_modules->parent.InitializedModulesBefore(parent_modules->creation);
      std::size_t count = 0;
      const auto candidate = [&](std::size_t i) {
        const auto lifted = analysis::LiftModuleLoad(body.instructions[i].decoded);
        const auto* load = std::get_if<analysis::ModuleLoad>(&lifted);
        if (!load || load->space != analysis::ModuleSpace::kLocal || !entry_modules.Contains(load->slot))
          return std::optional<analysis::ModuleSlot>{};
        if (i + 1 < body.instructions.size() && std::holds_alternative<analysis::HoleGuard>(
            analysis::LiftHoleInstruction(body.instructions[i + 1].decoded, frame.registers)))
          return std::optional<analysis::ModuleSlot>{};
        return std::optional{load->slot};
      };
      for (std::size_t i = 0; i < body.instructions.size(); ++i) {step(ModuleSlotSet::kPathWork + 2); count += candidate(i).has_value();}
      admit(count, sizeof(analysis::LanguageLoadFact)); initialized.reserve(count);
      admit(initialized.capacity() - count, sizeof(analysis::LanguageLoadFact));
      admit(count, sizeof(InputModuleRead)); module_reads.reserve(count);
      admit(module_reads.capacity() - count, sizeof(InputModuleRead));
      for (std::size_t i = 0; i < body.instructions.size(); ++i) {
        step(ModuleSlotSet::kPathWork + 2);
        const auto slot = candidate(i); if (!slot) continue;
        const analysis::InstructionId site{static_cast<std::uint32_t>(i)};
        initialized.push_back({site});
        step(parent_modules->parent.ModuleInitializationWork());
        module_reads.push_back({site, *slot, parent_modules->parent.Method(),
            parent_modules->parent.Input().instructions[parent_modules->creation.value].pc,
            parent_modules->parent.ModuleStoreBefore(*slot, parent_modules->creation)});
      }
    }
    // File families retain the same Language-write premise in Locals below;
    // VerifyFamilyWrites must discharge every descendant store. The lower-level
    // standalone builder keeps Unknown, and no cell read becomes a snapshot.
    auto analyzed = PhasedBody::Build(context, body, left, modules,
        mode == InputCaptureMode::kConditional ? analysis::CapturedValues::kLanguage : analysis::CapturedValues::kReject,
        analysis::ClosureWrites::kLanguage, awaits, source_pcs, yields, initialized, &entry_modules);
    if (auto* failure = std::get_if<PhasedInputFailure>(&analyzed)) {
      failure->allocated_bytes += budget.bytes - left.bytes;
      failure->work += budget.work - left.work;
    }
    auto phases = Take<PhasedBody>(std::move(analyzed));
    admit(phases.AllocatedBytes()); step(phases.Work());
    const auto input = phases.TreeInput(body).input;
    auto guarded_result = analysis::ProveGuardedReads(phases.Graph(), left);
    if (const auto* error = std::get_if<analysis::GuardedReadError>(&guarded_result)) {
      if (*error == analysis::GuardedReadError::kResourceLimit) throw PhasedFileFailure{Error::kResourceLimit};
      if (*error == analysis::GuardedReadError::kWorkLimit) throw PhasedFileFailure{Error::kWorkLimit};
      throw PhasedFileFailure{*error};
    }
    auto guarded = std::get<analysis::GuardedReads>(std::move(guarded_result));
    admit(guarded.allocated_bytes); step(guarded.work);
    std::size_t next_guarded = 0;
    // Retain the conditional scope proof once per method. This does NOT
    // discharge descendants' writes or admit source publication on its own.
    auto locals = Take<analysis::ExceptionLocals>(analysis::ProveExceptionLocals(input.flow,
        input.environments, left, {}, analysis::ClosureWrites::kLanguage));
    admit(locals.AllocatedBytes()); step(locals.plan.work_units);
    const auto captures = [](const analysis::SemanticOperation& op) {
      return std::holds_alternative<analysis::FunctionCreation>(op) ||
          std::holds_alternative<analysis::ClassCreation>(op) ||
          std::holds_alternative<analysis::BufferedObjectCreation>(op) ||
          std::holds_alternative<analysis::BufferedArrayCreation>(op);
    };
    std::size_t count = 0;
    for (std::size_t i = 0; i < input.flow.instructions.size(); ++i) {
      step(1); count += phases.Graph().ops[i].life == analysis::ValueLife::kLive &&
          captures(input.flow.instructions[i].operation);
    }
    std::vector<analysis::InstructionId> children;
    admit(count, sizeof(analysis::InstructionId)); children.reserve(count);
    admit(children.capacity() - count, sizeof(analysis::InstructionId));
    for (std::size_t i = 0; i < input.flow.instructions.size(); ++i) {
      step(1);
      if (phases.Graph().ops[i].life == analysis::ValueLife::kLive &&
          captures(input.flow.instructions[i].operation)) children.push_back({static_cast<std::uint32_t>(i)});
    }
    // Exception bindings name Before nodes, not instruction IDs. Record both
    // reads and writes in original instruction order for the common binder.
    std::size_t capture_count = 0;
    for (const auto& binding : input.environments.bindings) {
      step(1); capture_count += std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment);
    }
    std::vector<CaptureUse> uses;
    std::vector<InputCaptureAccess> accesses;
    admit(capture_count, sizeof(CaptureUse) + sizeof(InputCaptureAccess));
    uses.reserve(capture_count); accesses.reserve(capture_count);
    admit(uses.capacity() - capture_count, sizeof(CaptureUse));
    admit(accesses.capacity() - capture_count, sizeof(InputCaptureAccess));
    // Under the approved source TDZ premise, an access is a source access when
    // the normalized body keeps no handler: only the proven async/generator
    // completion protocol remains, which turns an early TDZ throw into the
    // same rejection or throw as the source. User handlers keep the strict rule.
    const auto domain = body.tries.empty() && CaptureReads(identity.contract) == CaptureReadSemantics::kSourceTdz
        ? analysis::CaptureDomain::kSourceAccess : analysis::CaptureDomain::kLanguage;
    for (const auto& binding : input.environments.bindings) {
      step(1);
      const auto* depth = std::get_if<analysis::CapturedDepth>(&binding.binding.environment);
      if (!depth) continue;
      Require(binding.instruction < input.flow.nodes.size(), Error::kMethod);
      const auto& before = input.flow.nodes[binding.instruction];
      Require(before.phase == analysis::ExceptionPhase::kBefore &&
          before.instruction.value < input.flow.instructions.size(), Error::kMethod);
      const auto& operation = input.flow.instructions[before.instruction.value].operation;
      const bool load = std::holds_alternative<analysis::LexicalLoad>(operation);
      Require(load || std::holds_alternative<analysis::LexicalStore>(operation), Error::kMethod);
      Require(accesses.empty() || accesses.back().instruction < before.instruction, Error::kMethod);
      const auto& instruction = input.flow.instructions[before.instruction.value];
      const auto working = phases.Graph().input.sites[before.instruction.value];
      while (working && next_guarded < guarded.reads.size() && guarded.reads[next_guarded].read < *working) {
        step(1); ++next_guarded;
      }
      const bool checked = load && working && next_guarded < guarded.reads.size() &&
          guarded.reads[next_guarded].read == *working;
      uses.push_back({{*depth, binding.binding.slot}, checked && domain == analysis::CaptureDomain::kLanguage ? analysis::CaptureDomain::kGuarded : domain,
          CaptureOrigin{method.offset, instruction.origin.value_or(instruction.pc), instruction.pc}});
      accesses.push_back({before.instruction, load ? analysis::LocalAccessKind::kLoad : analysis::LocalAccessKind::kStore});
    }
    Require(!file.CheckUnchanged(), Error::kChanged);
    return FilePhasedFunction(std::make_unique<const Storage>(Storage{std::move(body), std::move(original), std::move(phases),
        std::move(locals), std::move(guarded), std::move(children), mode, std::move(uses), std::move(accesses), DescribeOwner(context), std::move(entry_modules), std::move(module_reads), method.owner_offset,
        budget.bytes - left.bytes, budget.work - left.work}));
  } catch (const PhasedFileFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return PhasedFileFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return PhasedFileFailure{Error::kResourceLimit}; }
}
std::variant<std::uint64_t, FunctionInputError> VerifyInitializedModuleReads(
    const FunctionInput& parent, analysis::InstructionId creation, const FilePhasedFunction& child,
    analysis::SemanticBudget budget) {
  using Error = FunctionInputError;
  try {
    std::uint64_t work = 0;
    const auto step = [&](std::uint64_t count = 1) {
      if (count > budget.work - work) throw Error::kWorkLimit;
      work += count;
    };
    const auto premises = child.InitializedModuleReads();
    if (premises.empty() && child.EntryModules().Empty()) return work;
    step(parent.ModuleInitializationWork());
    if (!child.MatchesModuleCreation(parent, creation) ||
        !child.EntryModules().Same(parent.InitializedModulesBefore(creation))) throw Error::kModuleOwner;
    const auto input = child.Phases().TreeInput(child.Body()).input;
    if (premises.size() != input.proof.initialized_modules.size() || parent.Input().flow.blocks.empty())
      throw Error::kModuleOwner;
    const auto& entry = parent.Input().flow.blocks.front();
    for (std::size_t i = 0; i < premises.size(); ++i) {
      const auto& premise = premises[i];
      const auto cost = std::bit_width(parent.Input().instructions.size()) + 1;
      step(cost + ModuleSlotSet::kPathWork);
      if (premise.instruction != input.proof.initialized_modules[i].instruction ||
          premise.creator != parent.Method() || premise.creation_pc != parent.Input().instructions[creation.value].pc ||
          premise.instruction.value >= input.flow.instructions.size() || entry.first_instruction != 0 ||
          !child.EntryModules().Contains(premise.slot) ||
          premise.store != parent.ModuleStoreBefore(premise.slot, creation)) throw Error::kModuleOwner;
      const auto* load = std::get_if<analysis::ModuleLoad>(&input.flow.instructions[premise.instruction.value].operation);
      if (!load || load->space != analysis::ModuleSpace::kLocal || load->slot != premise.slot) throw Error::kModuleOwner;
      if (!premise.store) continue;
      if (premise.store->value >= entry.instruction_count || *premise.store >= creation) throw Error::kModuleOwner;
      const auto* store = std::get_if<analysis::ModuleStore>(&parent.Input().instructions[premise.store->value].operation);
      if (!store || store->slot != premise.slot) throw Error::kModuleOwner;
      step(cost);
      const auto reads = parent.Input().ssa.Reads(*premise.store);
      const auto kinds = reads.size() == 1 ? parent.Proof().values.At(reads.front().value) : std::nullopt;
      if (!kinds || !kinds->IsLanguage()) throw Error::kModuleOwner;
    }
    return work;
  } catch (Error error) {return error;}
}
}  // namespace abcd::source
