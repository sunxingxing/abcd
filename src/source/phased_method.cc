#include "phased_method.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include "abcd/analysis/exception_modules.h"
#include "module_aliases.h"

namespace abcd::source {
struct PhasedBody::Storage {
  abc::FrameLayout frame;
  analysis::ExceptionFlow flow;
  analysis::ExceptionSsa ssa;
  analysis::EnvironmentFlow environments;
  analysis::ExceptionValueFlow values;
  analysis::ExceptionInputProof proof;
  analysis::ValueProgram graph;
  std::optional<ModuleGuards> modules;
  std::optional<ModuleAliases> aliases;
  ExceptionPlan regions;
  ExceptionControl control;
  std::size_t bytes;
  std::uint64_t work;
  analysis::PhasedValueInput Input() const {
    return {flow, ssa, environments, values, proof, frame};
  }
};
struct PhasedMethod::Storage {
  PhasedBody body;
  EvaluationPlan evaluation;
  std::size_t bytes;
  std::uint64_t work;
};
namespace {
using Error = PhasedMethodError;
template <class T> Error PhaseError(const T& failure, Error stage) {
  if constexpr (std::is_enum_v<T>) {
    if constexpr (requires { T::kWorkLimit; })
      if (failure == T::kWorkLimit) return Error::kWorkLimit;
    if constexpr (requires { T::kResourceLimit; })
      if (failure == T::kResourceLimit) return Error::kResourceLimit;
    return stage;
  } else if constexpr (requires { failure.kind; }) return PhaseError(failure.kind, stage);
  else if constexpr (requires { failure.cause; })
    return std::visit([&](const auto& cause) { return PhaseError(cause, stage); }, failure.cause);
  else return std::visit([&](const auto& cause) { return PhaseError(cause, stage); }, failure.reason);
}
template <class T, class... Items> T PhaseResult(std::variant<Items...>&& result, Error stage) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  throw std::visit([&](const auto& error) -> Error {
    if constexpr (std::is_same_v<std::decay_t<decltype(error)>, T>) return stage;
    else return PhaseError(error, stage);
  }, result);
}
class PhaseBudget {
 public:
  explicit PhaseBudget(analysis::SemanticBudget limit) : initial_(limit), remaining_(limit) {}
  void Admit(std::size_t count, std::size_t width = 1) {
    if (count > remaining_.bytes / width) throw Error::kResourceLimit;
    remaining_.bytes -= count * width;
  }
  void Step(std::uint64_t work = 1) {
    if (work > remaining_.work) throw Error::kWorkLimit;
    remaining_.work -= work;
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::size_t Bytes() const { return initial_.bytes - remaining_.bytes; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  analysis::SemanticBudget initial_, remaining_;
};
}  // namespace

PhasedMethod::PhasedMethod(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
PhasedMethod::PhasedMethod(PhasedMethod&&) noexcept = default;
PhasedMethod& PhasedMethod::operator=(PhasedMethod&&) noexcept = default;
PhasedMethod::~PhasedMethod() = default;
const analysis::ValueProgram& PhasedMethod::Graph() const { return storage_->body.Graph(); }
const EvaluationPlan& PhasedMethod::Evaluation() const { return storage_->evaluation; }
const ControlPlan& PhasedMethod::Control() const { return storage_->body.Control(); }
const ModuleAliases* PhasedMethod::Modules() const { return storage_->body.Modules(); }
ExceptionTreeInput PhasedMethod::TreeInput(const abc::MethodBody& body) const {
  return storage_->body.TreeInput(body);
}
std::size_t PhasedMethod::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t PhasedMethod::Work() const { return storage_->work; }

PhasedBody::PhasedBody(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
PhasedBody::PhasedBody(PhasedBody&&) noexcept = default;
PhasedBody& PhasedBody::operator=(PhasedBody&&) noexcept = default;
PhasedBody::~PhasedBody() = default;
const analysis::ValueProgram& PhasedBody::Graph() const { return storage_->graph; }
const ControlPlan& PhasedBody::Control() const { return storage_->control.skeleton; }
const ModuleAliases* PhasedBody::Modules() const { return storage_->aliases ? &*storage_->aliases : nullptr; }
ExceptionTreeInput PhasedBody::TreeInput(const abc::MethodBody& body) const {
  return {body, storage_->Input(), storage_->regions, storage_->control};
}
std::size_t PhasedBody::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t PhasedBody::Work() const { return storage_->work; }

std::variant<PhasedBody, PhasedMethodError> PhasedBody::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget,
    const PhasedModuleBinding* binding) {
  using namespace analysis;
  try {
    // Recovered scopes/snapshots can allocate at different VM locations.
    // This route does not prove resource-failure equivalence for contract V1.
    switch (context.Identity().contract) {
      case SourceContract::kLanguageAndHostV1: return Error::kContract;
      case SourceContract::kLanguageAndHostV2:
      case SourceContract::kLanguageAndHostV3: break;
      default: return Error::kContract;
    }
    PhaseBudget quota(budget);
    quota.Admit(sizeof(Storage));
    const auto frame = context.Frame();
    auto flow = PhaseResult<ExceptionFlow>(BuildExceptionFlow(body, context.Identity().profile,
        frame.registers, {quota.Remaining().bytes, quota.Remaining().work}, {}, &frame), Error::kInput);
    quota.Admit(flow.allocated_bytes); quota.Step(flow.work_units);
    IdfQuota ssa_work{quota.Remaining().work};
    auto ssa = PhaseResult<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers,
        ssa_work, {quota.Remaining().bytes}), Error::kSsa);
    quota.Admit(ssa.AllocatedBytes()); quota.Step(ssa_work.Consumed());
    auto environments = PhaseResult<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow,
        {quota.Remaining().bytes}), Error::kEnvironment);
    quota.Admit(environments.allocated_bytes); quota.Step(environments.instruction_visits);
    auto modules = PhaseResult<ExceptionModules>(ProveExceptionModules(flow, ssa,
        quota.Remaining()), Error::kValues);
    quota.Admit(modules.AllocatedBytes()); quota.Step(modules.work_units);
    std::vector<EntryValueFact> entries;
    quota.Admit(ssa.Values().size(), sizeof(EntryValueFact));
    entries.reserve(ssa.Values().size());
    quota.Admit(entries.capacity() - ssa.Values().size(), sizeof(EntryValueFact));
    for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
      quota.Step();
      const auto& value = ssa.Values()[i];
      const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
        entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
    }
    IdfQuota value_work{quota.Remaining().work};
    // Guarded module facts apply to the certified pair, never the raw load.
    // Preserve its source correspondence below before any evaluation/publication.
    // No Language premise is invented for lexical loads or explicit THROW.
    auto values = PhaseResult<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
        value_work, {quota.Remaining().bytes}, PendingExceptions(context.Identity().contract), modules.loads, {},
        NormalCallResults(context.Identity().contract)), Error::kValues);
    quota.Admit(values.AllocatedBytes()); quota.Step(value_work.Consumed());
    const auto arguments = frame.extra_arguments ? std::optional{ArgumentFrameFact{frame}} : std::nullopt;
    auto proof = PhaseResult<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments,
        values, quota.Remaining().work, arguments ? &*arguments : nullptr), Error::kValues);
    quota.Step(proof.work_units);
    const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
    auto imported = PhaseResult<ImportedPhases>(BuildPhasedValues(input, quota.Remaining()), Error::kImport);
    quota.Admit(imported.allocated_bytes); quota.Step(imported.work);
    std::optional<ModuleGuards> guards;
    std::optional<ModuleAliases> aliases;
    if (!modules.reads.empty()) {
      guards = PhaseResult<ModuleGuards>(BuildPhasedModuleGuards(input, imported.graph,
          quota.Remaining()), Error::kImport);
      quota.Admit(guards->allocated_bytes); quota.Step(guards->work);
      if (binding) {
        const auto position = context.Position().value;
        if (position >= binding->index.methods.size()) return Error::kModules;
        const auto& method = binding->index.methods[position];
        if (method.offset != context.Target() || method.owner_offset != binding->record.record())
          return Error::kModules;
        const PhasedModuleInput selected{context.Identity(), method.offset, method.owner_offset,
            input, imported.graph, *guards};
        aliases = PhaseResult<ModuleAliases>(BindPhasedModuleAliases(binding->file, binding->header,
            binding->index, selected, binding->record, binding->owner, {}, quota.Remaining()), Error::kModules);
        quota.Admit(aliases->allocated_bytes); quota.Step(aliases->work);
      }
    }
    auto regions = PhaseResult<ExceptionPlan>(BuildExceptionPlan(body, imported.graph, input,
        quota.Remaining()), Error::kRegion);
    quota.Admit(regions.allocated_bytes); quota.Step(regions.work);
    auto control = PhaseResult<ExceptionControl>(BuildExceptionControl(body, imported.graph, input,
        regions, quota.Remaining()), Error::kControl);
    quota.Admit(control.allocated_bytes); quota.Step(control.work);
    return PhasedBody(std::make_unique<Storage>(Storage{frame, std::move(flow), std::move(ssa),
        std::move(environments), std::move(values), std::move(proof), std::move(imported.graph),
        std::move(guards), std::move(aliases), std::move(regions), std::move(control),
        quota.Bytes(), quota.Work()}));
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<PhasedMethod, PhasedMethodError> PhasedMethod::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget,
    const PhasedModuleBinding* binding) {
  auto built = PhasedBody::Build(context, body, budget, binding);
  if (const auto* error = std::get_if<PhasedMethodError>(&built)) return *error;
  return Evaluate(std::get<PhasedBody>(std::move(built)), budget);
}
std::variant<PhasedMethod, PhasedMethodError> PhasedMethod::Evaluate(
    PhasedBody body, analysis::SemanticBudget budget) {
  try {
    PhaseBudget quota(budget);
    quota.Admit(sizeof(Storage)); quota.Admit(body.AllocatedBytes()); quota.Step(body.Work());
    const auto* aliases = body.Modules();
    auto evaluation = PhaseResult<EvaluationPlan>(BuildPhasedEvaluationPlan(body.Graph(),
        quota.Remaining(), IncomingCells::kReject, {},
        aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{}),
        Error::kEvaluation);
    quota.Admit(evaluation.allocated_bytes); quota.Step(evaluation.work);
    return PhasedMethod(std::make_unique<Storage>(Storage{std::move(body), std::move(evaluation),
        quota.Bytes(), quota.Work()}));
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
