#include "phased_method.h"

#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
struct PhasedMethod::Storage {
  abc::FrameLayout frame;
  analysis::ExceptionFlow flow;
  analysis::ExceptionSsa ssa;
  analysis::EnvironmentFlow environments;
  analysis::ExceptionValueFlow values;
  analysis::ExceptionInputProof proof;
  analysis::ValueProgram graph;
  ExceptionPlan regions;
  ExceptionControl control;
  EvaluationPlan evaluation;
  std::size_t bytes;
  std::uint64_t work;
  analysis::PhasedValueInput Input() const {
    return {flow, ssa, environments, values, proof, frame};
  }
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
  } else return PhaseError(failure.kind, stage);
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
const analysis::ValueProgram& PhasedMethod::Graph() const { return storage_->graph; }
const EvaluationPlan& PhasedMethod::Evaluation() const { return storage_->evaluation; }
const ControlPlan& PhasedMethod::Control() const { return storage_->control.skeleton; }
ExceptionTreeInput PhasedMethod::TreeInput(const abc::MethodBody& body) const {
  return {body, storage_->Input(), storage_->regions, storage_->control};
}
std::size_t PhasedMethod::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t PhasedMethod::Work() const { return storage_->work; }

std::variant<PhasedMethod, PhasedMethodError> PhasedMethod::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget) {
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
    // Only entry and versioned host contracts are premises. Do not invent
    // Language facts for lexical/module loads or the result of explicit THROW.
    auto values = PhaseResult<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
        value_work, {quota.Remaining().bytes}, PendingExceptions(context.Identity().contract), {}, {},
        NormalCallResults(context.Identity().contract)), Error::kValues);
    quota.Admit(values.AllocatedBytes()); quota.Step(value_work.Consumed());
    const auto arguments = frame.extra_arguments ? std::optional{ArgumentFrameFact{frame}} : std::nullopt;
    auto proof = PhaseResult<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments,
        values, quota.Remaining().work, arguments ? &*arguments : nullptr), Error::kValues);
    quota.Step(proof.work_units);
    const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
    auto imported = PhaseResult<ImportedPhases>(BuildPhasedValues(input, quota.Remaining()), Error::kImport);
    quota.Admit(imported.allocated_bytes); quota.Step(imported.work);
    auto regions = PhaseResult<ExceptionPlan>(BuildExceptionPlan(body, imported.graph, input,
        quota.Remaining()), Error::kRegion);
    quota.Admit(regions.allocated_bytes); quota.Step(regions.work);
    auto control = PhaseResult<ExceptionControl>(BuildExceptionControl(body, imported.graph, input,
        regions, quota.Remaining()), Error::kControl);
    quota.Admit(control.allocated_bytes); quota.Step(control.work);
    auto evaluation = PhaseResult<EvaluationPlan>(BuildPhasedEvaluationPlan(imported.graph,
        quota.Remaining()), Error::kEvaluation);
    quota.Admit(evaluation.allocated_bytes); quota.Step(evaluation.work);
    return PhasedMethod(std::make_unique<Storage>(Storage{frame, std::move(flow), std::move(ssa),
        std::move(environments), std::move(values), std::move(proof), std::move(imported.graph),
        std::move(regions), std::move(control), std::move(evaluation), quota.Bytes(), quota.Work()}));
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
