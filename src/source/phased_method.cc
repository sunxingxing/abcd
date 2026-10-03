#include "phased_method.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include "abcd/analysis/exception_modules.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/exception_captures.h"
#include "module_aliases.h"

namespace abcd::source {
struct PhasedBody::Storage {
  RouteInputIdentity identity;
  abc::EntityOffset target;
  abc::FrameLayout frame;
  analysis::ExceptionFlow flow;
  analysis::ExceptionSsa ssa;
  analysis::EnvironmentFlow environments;
  analysis::ExceptionValueFlow values;
  analysis::ExceptionInputProof proof;
  std::vector<analysis::LanguageLoadFact> initialized_modules;
  analysis::ValueProgram graph;
  std::optional<analysis::ValueEnvironments> cell_environments;
  std::optional<CellPlan> cells;
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
const RouteInputIdentity& PhasedBody::Identity() const { return storage_->identity; }
abc::EntityOffset PhasedBody::Target() const { return storage_->target; }
const ControlPlan& PhasedBody::Control() const { return storage_->control.skeleton; }
const ModuleAliases* PhasedBody::Modules() const { return storage_->aliases ? &*storage_->aliases : nullptr; }
const analysis::ValueEnvironments* PhasedBody::CellEnvironments() const {
  return storage_->cell_environments ? &*storage_->cell_environments : nullptr;
}
const CellPlan* PhasedBody::Cells() const { return storage_->cells ? &*storage_->cells : nullptr; }
ExceptionTreeInput PhasedBody::TreeInput(const abc::MethodBody& body) const {
  return {body, storage_->Input(), storage_->regions, storage_->control};
}
std::size_t PhasedBody::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t PhasedBody::Work() const { return storage_->work; }

std::variant<PhasedBody, PhasedMethodError, PhasedInputFailure> PhasedBody::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget,
    const PhasedModuleBinding* binding, analysis::CapturedValues capture_mode,
    analysis::ClosureWrites writes, std::span<const analysis::AwaitSite> awaits,
    std::span<const abc::CodeOffset> source_pcs, std::span<const analysis::YieldSite> yields,
    std::span<const analysis::LanguageLoadFact> initialized_modules) {
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
    // Normalized suspension facts come from the same file owner's completion
    // normalization, not from opcode resemblance or provenance alone.
    if ((!source_pcs.empty() || !awaits.empty() || !yields.empty()) &&
        ((!IsAsyncCallable(context.Callable()) && !IsGeneratorCallable(context.Callable())) ||
         source_pcs.size() != body.instructions.size())) return Error::kInput;
    if ((!awaits.empty() && !IsAsyncCallable(context.Callable())) ||
        (!yields.empty() && !IsGeneratorCallable(context.Callable()))) return Error::kInput;
    for (const auto& yield : yields) {
      const auto execution = IsAsyncCallable(context.Callable()) ? YieldExecution::kAsynchronous : YieldExecution::kSynchronous;
      if (yield.origin.execution != execution) return Error::kInput;
    }
    auto flow = PhaseResult<ExceptionFlow>(BuildExceptionFlow(body, context.Identity().profile,
        frame.registers, {quota.Remaining().bytes, quota.Remaining().work}, awaits, &frame, yields), Error::kInput);
    for (std::size_t i = 0; i < source_pcs.size(); ++i) {
      if (i && source_pcs[i] <= source_pcs[i - 1]) return Error::kInput;
      flow.instructions[i].origin = source_pcs[i];
    }
    quota.Admit(flow.allocated_bytes); quota.Step(flow.work_units);
    IdfQuota ssa_work{quota.Remaining().work};
    auto ssa = PhaseResult<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers,
        ssa_work, {quota.Remaining().bytes}), Error::kSsa);
    quota.Admit(ssa.AllocatedBytes()); quota.Step(ssa_work.Consumed());
    auto environments = PhaseResult<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow,
        {quota.Remaining().bytes}), Error::kEnvironment);
    quota.Admit(environments.allocated_bytes); quota.Step(environments.instruction_visits);
    quota.Admit(initialized_modules.size(), sizeof(LanguageLoadFact));
    std::vector<LanguageLoadFact> initialized(initialized_modules.begin(), initialized_modules.end());
    quota.Admit(initialized.capacity() - initialized_modules.size(), sizeof(LanguageLoadFact));
    quota.Step(initialized.size());
    auto modules = PhaseResult<ExceptionModules>(ProveExceptionModules(flow, ssa,
        quota.Remaining(), initialized), Error::kValues);
    quota.Admit(modules.AllocatedBytes()); quota.Step(modules.work_units);
    auto receiver = FrameReceiver::kUnknown;
    switch (context.Callable()) {
      case CallableKind::kFunction:
      case CallableKind::kMethod:
      case CallableKind::kBaseConstructor:
      case CallableKind::kAsyncFunction:
      case CallableKind::kAsyncMethod:
      case CallableKind::kGenerator:
      case CallableKind::kGeneratorMethod:
      case CallableKind::kAsyncGenerator:
        receiver = FrameReceiver::kInitialized;
        break;
      case CallableKind::kArrow:
      case CallableKind::kAsyncArrow:
      case CallableKind::kDerivedConstructor:
        break;
    }
    bool lexical_reads = false;
    for (const auto& instruction : flow.instructions) {
      quota.Step();
      lexical_reads |= std::holds_alternative<LexicalLoad>(instruction.operation);
      // An entry premise cannot cover a receiver-changing super protocol.
      // Async await here is owner-normalized and restores this same frame;
      // raw suspend/resume is not admitted by the common semantic importer.
      // Pinned runtime: RuntimeStubs::SaveFrameToContext saves This/Method;
      // interpreter-inl.cpp restores state->thisObj/function on resume.
      if ((std::holds_alternative<SuperForward>(instruction.operation) || std::holds_alternative<SuperRange>(instruction.operation) ||
          std::holds_alternative<SuperSpread>(instruction.operation))) receiver = FrameReceiver::kUnknown;
    }
    std::optional<ExceptionLocals> locals;
    if (lexical_reads) {
      locals = PhaseResult<ExceptionLocals>(ProveExceptionLocals(flow, environments,
          quota.Remaining(), {}, writes), Error::kValues);
      quota.Admit(locals->AllocatedBytes()); quota.Step(locals->plan.work_units);
    }
    std::optional<ExceptionCaptures> captures;
    if (capture_mode == CapturedValues::kLanguage) {
      captures = PhaseResult<ExceptionCaptures>(PlanExceptionCaptures(flow, environments,
          quota.Remaining()), Error::kValues);
      quota.Admit(captures->AllocatedBytes()); quota.Step(captures->work_units);
    } else if (capture_mode != CapturedValues::kReject) return Error::kValues;
    std::vector<ExceptionLoadFact> loads;
    const auto load_count = modules.loads.size() + (locals ? locals->loads.size() : 0) +
        (captures ? captures->loads.size() : 0);
    quota.Admit(load_count, sizeof(ExceptionLoadFact)); loads.reserve(load_count);
    quota.Admit(loads.capacity() - load_count, sizeof(ExceptionLoadFact));
    quota.Step(load_count);
    loads.insert(loads.end(), modules.loads.begin(), modules.loads.end());
    if (locals) loads.insert(loads.end(), locals->loads.begin(), locals->loads.end());
    if (captures) loads.insert(loads.end(), captures->loads.begin(), captures->loads.end());
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
    // Local load facts require completed-store dominance. EVERY store is
    // validated below, including exceptional paths; these are not assumptions
    // about default undefined initialization. Conditional captured loads retain
    // their original occurrences in FilePhasedFunction; each parent must bind
    // them to initialized cells and complete-family writes close that premise.
    auto values = PhaseResult<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
        value_work, {quota.Remaining().bytes}, PendingExceptions(context.Identity().contract), loads, {},
        NormalCallResults(context.Identity().contract), receiver), Error::kValues);
    quota.Admit(values.AllocatedBytes()); quota.Step(value_work.Consumed());
    const auto arguments = frame.extra_arguments ? std::optional{ArgumentFrameFact{frame}} : std::nullopt;
    auto verified = VerifyExceptionInputs(flow, ssa, environments, values, quota.Remaining().work,
        arguments ? &*arguments : nullptr, ExceptionInputDomain::kHoleTransport, initialized);
    if (const auto* failure = std::get_if<ExceptionInputFailure>(&verified)) {
      auto original = *failure;
      if (original.node && original.node->value < flow.nodes.size()) {
        const auto site = flow.nodes[original.node->value].instruction;
        if (site.value < flow.instructions.size())
          original.pc = flow.instructions[site.value].origin.value_or(original.pc);
      }
      return PhasedInputFailure{original, quota.Bytes(), quota.Work() + failure->work_units};
    }
    auto proof = std::get<ExceptionInputProof>(std::move(verified));
    quota.Step(proof.work_units);
    const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
    auto imported = PhaseResult<ImportedPhases>(BuildPhasedValues(input, quota.Remaining()), Error::kImport);
    quota.Admit(imported.allocated_bytes); quota.Step(imported.work);
    bool needs_cells = false;
    for (const auto& op : imported.graph.ops) {
      quota.Step();
      needs_cells |= std::holds_alternative<ValueEnvironmentPush>(op.operation) ||
          std::holds_alternative<ValueEnvironmentPop>(op.operation) ||
          std::holds_alternative<ValueLexicalLoad>(op.operation) ||
          std::holds_alternative<ValueLexicalStore>(op.operation) ||
          std::holds_alternative<ValueFunctionCreation>(op.operation) ||
          std::holds_alternative<ValueClassCreation>(op.operation);
    }
    std::optional<ValueEnvironments> cell_environments;
    std::optional<CellPlan> cells;
    if (needs_cells) {
      // Keep working IDs with their owner. Original phase EnvironmentIds are
      // a separate namespace; capture rebasing must use allocation provenance.
      cell_environments = PhaseResult<ValueEnvironments>(BuildPhasedValueEnvironments(
          imported.graph, quota.Remaining()), Error::kEnvironment);
      quota.Admit(cell_environments->allocated_bytes); quota.Step(cell_environments->work);
      cells = PhaseResult<CellPlan>(BuildPhasedCellPlan(imported.graph, *cell_environments,
          quota.Remaining()), Error::kCells);
      quota.Admit(cells->allocated_bytes); quota.Step(cells->work);
    }
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
    return PhasedBody(std::make_unique<Storage>(Storage{context.Identity(), context.Target(), frame, std::move(flow), std::move(ssa),
        std::move(environments), std::move(values), std::move(proof), std::move(initialized), std::move(imported.graph),
        std::move(cell_environments), std::move(cells),
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
  if (std::holds_alternative<PhasedInputFailure>(built)) return PhasedMethodError::kValues;
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
