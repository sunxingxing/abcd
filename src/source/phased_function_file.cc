#include "phased_function_file.h"

#include <new>
#include <stdexcept>

#include "module_aliases.h"
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
  bool operator==(const PhasedOwner&) const = default;
};
PhasedOwner DescribeOwner(const FunctionContext& context) {
  if (const auto* site = context.ClassSite())
    return {site->creator, site->pc, site->property, context.Callable(), context.Mode(), context.Frame(), context.Length()};
  const auto* site = context.CreationSite();
  return {site->creator, site->pc, {}, context.Callable(), context.Mode(), context.Frame(), context.Length()};
}
}  // namespace
struct FilePhasedFunction::Storage {
  abc::MethodBody body;
  std::optional<abc::MethodBody> original;
  PhasedBody phases;
  analysis::ExceptionLocals locals;
  std::vector<analysis::InstructionId> children;
  InputCaptureMode mode;
  std::vector<CaptureUse> captures;
  std::vector<InputCaptureAccess> accesses;
  PhasedOwner owner;
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
std::span<const analysis::InstructionId> FilePhasedFunction::Children() const { return storage_->children; }
InputCaptureMode FilePhasedFunction::Mode() const { return storage_->mode; }
std::span<const CaptureUse> FilePhasedFunction::Captures() const { return storage_->captures; }
std::span<const InputCaptureAccess> FilePhasedFunction::CaptureAccesses() const { return storage_->accesses; }
bool FilePhasedFunction::MatchesOwner(const FunctionContext& context) const {
  return storage_->phases.Identity() == context.Identity() && storage_->phases.Target() == context.Target() &&
      storage_->owner == DescribeOwner(context);
}
abc::EntityOffset FilePhasedFunction::Record() const { return storage_->record; }
std::size_t FilePhasedFunction::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t FilePhasedFunction::Work() const { return storage_->work; }
namespace {
using Error = PhasedFileError;
void Require(bool ok, Error error) { if (!ok) throw PhasedFileFailure{error}; }
template<class T, class E> T Take(std::variant<T, E> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  throw PhasedFileFailure{std::get<E>(std::move(result))};
}
}  // namespace
std::variant<FilePhasedFunction, PhasedFileFailure> FilePhasedFunction::Read(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const FunctionContext& context, analysis::SemanticBudget budget, const PhasedModuleBinding* modules,
    InputCaptureMode mode) {
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
      arena(normalized.body.instructions); arena(normalized.source_pcs); arena(normalized.yields);
      source_pcs = std::move(normalized.source_pcs); yields = std::move(normalized.yields);
      original.emplace(std::move(body)); body = std::move(normalized.body);
    } else if (IsAsyncCallable(context.Callable())) {
      Require(identity.contract == SourceContract::kLanguageAndHostV2 ||
          identity.contract == SourceContract::kLanguageAndHostV3, Error::kIdentity);
      auto normalized = Take<abc::MethodBody>(ProveAsyncCompletion(body, frame,
          left.bytes, &source_pcs, &awaits, header.profile, ResourceFailureScope::kExcluded));
      arena(normalized.instructions); arena(normalized.tries); arena(normalized.handlers);
      arena(source_pcs); arena(awaits);
      original.emplace(std::move(body)); body = std::move(normalized);
    }
    Require(mode == InputCaptureMode::kClosed || mode == InputCaptureMode::kConditional, Error::kIdentity);
    // File families retain the same Language-write premise in Locals below;
    // VerifyFamilyWrites must discharge every descendant store. The lower-level
    // standalone builder keeps Unknown, and no cell read becomes a snapshot.
    auto phases = Take<PhasedBody>(PhasedBody::Build(context, body, left, modules,
        mode == InputCaptureMode::kConditional ? analysis::CapturedValues::kLanguage : analysis::CapturedValues::kReject,
        analysis::ClosureWrites::kLanguage, awaits, source_pcs, yields));
    admit(phases.AllocatedBytes()); step(phases.Work());
    const auto input = phases.TreeInput(body).input;
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
    for (const auto& instruction : input.flow.instructions) { step(1); count += captures(instruction.operation); }
    std::vector<analysis::InstructionId> children;
    admit(count, sizeof(analysis::InstructionId)); children.reserve(count);
    admit(children.capacity() - count, sizeof(analysis::InstructionId));
    for (std::size_t i = 0; i < input.flow.instructions.size(); ++i) {
      step(1);
      if (captures(input.flow.instructions[i].operation)) children.push_back({static_cast<std::uint32_t>(i)});
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
      uses.push_back({{*depth, binding.binding.slot}, analysis::CaptureDomain::kLanguage});
      accesses.push_back({before.instruction, load ? analysis::LocalAccessKind::kLoad : analysis::LocalAccessKind::kStore});
    }
    Require(!file.CheckUnchanged(), Error::kChanged);
    return FilePhasedFunction(std::make_unique<const Storage>(Storage{std::move(body), std::move(original), std::move(phases),
        std::move(locals), std::move(children), mode, std::move(uses), std::move(accesses), DescribeOwner(context), method.owner_offset,
        budget.bytes - left.bytes, budget.work - left.work}));
  } catch (const PhasedFileFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return PhasedFileFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return PhasedFileFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
