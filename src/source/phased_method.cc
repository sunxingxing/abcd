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
  std::optional<VerifiedProtocolRegionOwnership> protocols;
  std::optional<abc::MethodBody> protocol_body;
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
// Delegated analysis scratch is destroyed on return. Transfer only its live
// result buffers into the owner ledger; peak_bytes includes exclusive scratch
// grants and must never be mistaken for retained storage.
void RetainCloseSummary(PhaseBudget& quota,const CloseCompletionSummary& summary) {
  const auto retain=[&](const auto& v){quota.Admit(v.capacity(),sizeof(typename std::decay_t<decltype(v)>::value_type));};
  retain(summary.entries);retain(summary.regions);
  for(const auto& region:summary.regions) {
    quota.Step();retain(region.members);retain(region.edges);
    if(region.outcomes) {
      const auto& o=*region.outcomes;retain(o.arrivals);retain(o.leaving);retain(o.comparisons);retain(o.catch_payloads);retain(o.events);
    }
    if(region.boundary) {
      const auto& b=*region.boundary;retain(b.ports);retain(b.unreachable_entries);retain(b.private_slots);retain(b.regions);retain(b.enclosures);retain(b.protected_ranges);
      for(const auto& range:b.protected_ranges) {
        quota.Step();retain(range.ordinary);retain(range.transport);retain(range.protocol);retain(range.dispatch);retain(range.exits);
      }
    }
  }
}
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
  return {body, storage_->Input(), storage_->regions, storage_->control,ForOfInput()};
}
std::optional<ForOfEvaluationInput> PhasedBody::ForOfInput() const {
  if(!storage_->protocols)return {};
  return ForOfEvaluationInput{*storage_->protocol_body,storage_->Input(),storage_->regions,storage_->control,*storage_->protocols,storage_->identity.contract};
}
std::size_t PhasedBody::AllocatedBytes() const { return storage_->bytes; }
std::uint64_t PhasedBody::Work() const { return storage_->work; }

std::variant<PhasedBody, PhasedMethodError, PhasedInputFailure, PhasedFlowFailure, PhasedModuleFailure, PhasedImportFailure, PhasedControlFailure> PhasedBody::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget,
    const PhasedModuleBinding* binding, analysis::CapturedValues capture_mode,
    analysis::ClosureWrites writes, std::span<const analysis::AwaitSite> awaits,
    std::span<const abc::CodeOffset> source_pcs, std::span<const analysis::YieldSite> yields,
    std::span<const analysis::LanguageLoadFact> initialized_modules, const ModuleSlotSet* entry_modules) {
  using namespace analysis;
  try {
    // Recovered scopes/snapshots can allocate at different VM locations.
    // This route does not prove resource-failure equivalence for contract V1.
    switch (context.Identity().contract) {
      case SourceContract::kLanguageAndHostV1: return Error::kContract;
      case SourceContract::kLanguageAndHostV2:
      case SourceContract::kLanguageAndHostV3:
      case SourceContract::kLanguageConstructorsV1:
      case SourceContract::kLanguageArithmeticConstructorsV1:
      case SourceContract::kLanguageNumericConstructorsV1:
      case SourceContract::kLanguageNumericV1:
      case SourceContract::kLanguageArithmeticV1:
      case SourceContract::kLanguageSourceV1:
      case SourceContract::kLanguageSourceV2:
      case SourceContract::kLanguageSourceV3:
      case SourceContract::kLanguageSourceV4:
      case SourceContract::kLanguageSourceV7:
      case SourceContract::kLanguageSourceV6:
      case SourceContract::kLanguageSourceV5: break;
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
    auto flow_result = BuildExceptionFlow(body, context.Identity().profile,
        frame.registers, {quota.Remaining().bytes, quota.Remaining().work}, awaits, &frame, yields);
    if (!std::holds_alternative<ExceptionFlow>(flow_result)) {
      return std::visit([&](const auto& failure) -> PhasedFlowFailure {
        using T = std::decay_t<decltype(failure)>;
        if constexpr (std::is_same_v<T, ExceptionFlow>) {
          // Not visited: keeps the success graph out of the failure envelope.
          throw Error::kInput;
        } else {
          PhasedFlowFailure result{PhaseError(failure, Error::kInput), failure, context.Target(), {}, {}};
          // Quota/whole-input failures own no instruction, including PC zero.
          if (result.kind == Error::kResourceLimit || result.kind == Error::kWorkLimit) return result;
          abc::CodeOffset pc;
          if constexpr (requires { failure.pc; }) pc = failure.pc;
          else pc = failure.offset;
          const auto at = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
              [](const auto& site, auto target) { return site.offset < target; });
          if (at != body.instructions.end() && at->offset == pc) {
            result.working_pc = pc;
            const auto i = static_cast<std::size_t>(at - body.instructions.begin());
            if (source_pcs.empty()) result.pc = pc;
            else if (i < source_pcs.size()) result.pc = source_pcs[i];
          }
          return result;
        }
      }, flow_result);
    }
    auto flow = std::get<ExceptionFlow>(std::move(flow_result));
    for(std::size_t i=0;i<flow.instructions.size();++i) {
      quota.Step();const auto& instruction=flow.instructions[i];
      if(const auto* load=std::get_if<ModuleLoad>(&instruction.operation);load&&load->space==ModuleSpace::kExternal&&(load->domain==ModuleDomain::kSendable||context.ModuleDomain()==ModuleDomain::kSendable)) {
        if(SendableOutput(context.Identity().contract)!=SendableOutputPolicy::kArkDirectiveV1)
          return PhasedFlowFailure{Error::kInput,SemanticFailure{SemanticError::kUnsupportedInstruction,instruction.pc},context.Target(),source_pcs.empty()?instruction.pc:source_pcs[i],instruction.pc};
        if(context.ModuleDomain()!=load->domain)return Error::kModules;
      }
    }
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
        NormalCallResults(context.Identity().contract), receiver, Arithmetic(context.Identity().contract)), Error::kValues);
    quota.Admit(values.AllocatedBytes()); quota.Step(value_work.Consumed());
    const auto arguments = frame.extra_arguments ? std::optional{ArgumentFrameFact{frame}} : std::nullopt;
    auto verified = VerifyExceptionInputs(flow, ssa, environments, values, quota.Remaining().work,
        arguments ? &*arguments : nullptr, ExceptionInputDomain::kHoleTransport, initialized);
    std::optional<PhasedInputFailure> input_gap;
    if (const auto* failure = std::get_if<ExceptionInputFailure>(&verified);
        failure && failure->kind == ExceptionInputError::kInternalValue) {
      // Preserve every successful plan. A caught explicit throw may carry a
      // branch-proven subset of its unrefined SSA domain; never upgrade loads,
      // pending external exceptions, or the value's other uses globally.
      input_gap = PhasedInputFailure{*failure, quota.Bytes(), quota.Work() + failure->work_units, failure->pc};
      quota.Step(failure->work_units);
      IdfQuota refined_work{quota.Remaining().work};
      auto refined = PhaseResult<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
          refined_work, {quota.Remaining().bytes}, PendingExceptions(context.Identity().contract), loads, {},
          NormalCallResults(context.Identity().contract), receiver, Arithmetic(context.Identity().contract),
          ExceptionThrowRefinement::kGuardedNonHole), Error::kValues);
      quota.Admit(refined.AllocatedBytes()); quota.Step(refined_work.Consumed());
      values = std::move(refined);
      verified = VerifyExceptionInputs(flow, ssa, environments, values, quota.Remaining().work,
          arguments ? &*arguments : nullptr, ExceptionInputDomain::kHoleTransport, initialized);
    }
    if (const auto* failure = std::get_if<ExceptionInputFailure>(&verified)) {
      auto original = *failure;
      if (original.node && original.node->value < flow.nodes.size()) {
        const auto site = flow.nodes[original.node->value].instruction;
        if (site.value < flow.instructions.size())
          original.pc = flow.instructions[site.value].origin.value_or(original.pc);
      }
      return PhasedInputFailure{original, quota.Bytes(), quota.Work() + failure->work_units, failure->pc};
    }
    auto proof = std::get<ExceptionInputProof>(std::move(verified));
    quota.Step(proof.work_units);
    const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
    auto built_import = BuildPhasedValues(input, quota.Remaining());
    if (const auto* failure = std::get_if<ValueImportFailure>(&built_import)) {
      auto original = *failure;
      // Import PCs belong to the working body. A creation PC is never a
      // substitute for a failed child site, including site-less quota errors.
      if (failure->pc) {
        for (const auto& instruction : flow.instructions) {
          quota.Step();
          if (instruction.pc == *failure->pc) {
            original.pc = instruction.origin.value_or(instruction.pc);
            break;
          }
        }
      }
      return PhasedImportFailure{PhaseError(*failure, Error::kImport), original,
          context.Target(), failure->pc};
    }
    auto imported = std::get<ImportedPhases>(std::move(built_import));
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
    if (!modules.reads.empty() || std::any_of(imported.graph.ops.begin(), imported.graph.ops.end(),
        [&](const auto& op) { quota.Step(1); return op.life == ValueLife::kLive &&
            std::holds_alternative<ValueModuleWrite>(op.operation); })) {
      auto guarded = BuildPhasedModuleGuards(input, imported.graph, quota.Remaining());
      if (const auto* failure = std::get_if<ModuleGuardError>(&guarded))
        return PhasedModuleFailure{PhaseError(*failure, Error::kImport), *failure};
      guards = std::get<ModuleGuards>(std::move(guarded));
      quota.Admit(guards->allocated_bytes); quota.Step(guards->work);
      if (binding) {
        const auto position = context.Position().value;
        if (position >= binding->index.methods.size()) return Error::kModules;
        const auto& method = binding->index.methods[position];
        if (method.offset != context.Target() || method.owner_offset != binding->record.record())
          return Error::kModules;
        const PhasedModuleInput selected{context.Identity(), method.offset, method.owner_offset,
            input, imported.graph, *guards, entry_modules};
        auto bound = BindPhasedModuleAliases(binding->file, binding->header,
            binding->index, selected, binding->record, binding->owner, {}, quota.Remaining());
        if (const auto* failure = std::get_if<ModuleAliasFailure>(&bound))
          return PhasedModuleFailure{PhaseError(*failure, Error::kModules), *failure};
        aliases = std::get<ModuleAliases>(std::move(bound));
        quota.Admit(aliases->allocated_bytes); quota.Step(aliases->work);
      }
    }
    auto planned_regions = BuildExceptionPlan(body, imported.graph, input, quota.Remaining());
    if (const auto* failure=std::get_if<ExceptionPlanFailure>(&planned_regions)) {
      std::optional<abc::CodeOffset> original, working;
      if(failure->exception_region){original=failure->exception_region->body_pc;working=failure->exception_region->working_body_pc;}
      return PhasedControlFailure{Error::kRegion,*failure,context.Target(),original,working};
    }
    if(const auto* error=std::get_if<ExceptionPlanError>(&planned_regions))return PhaseError(*error,Error::kRegion);
    auto regions=std::get<ExceptionPlan>(std::move(planned_regions));
    quota.Admit(regions.allocated_bytes); quota.Step(regions.work);
    std::optional<VerifiedProtocolRegionOwnership> protocols;
    bool acquisition=false;
    if(binding && ExternalExceptionNoHole(context.Identity().contract)==ExternalExceptionPolicy::kNoHoleV1) {
      for(const auto& op:imported.graph.ops) {
        quota.Step();acquisition|=op.life==ValueLife::kLive && std::holds_alternative<ValueIteratorAcquire>(op.operation);
      }
    }
    if(acquisition) {
      auto resolved=ResolveIteratorKeys({binding->file,binding->header,binding->index},context.Target(),imported.graph,quota.Remaining());
      auto* keys=std::get_if<IteratorKeys>(&resolved);
      if(!keys)return std::visit([](const auto& failure) {
        if constexpr(std::is_same_v<std::decay_t<decltype(failure)>,IteratorKeys>)return Error::kImport;
        else return PhaseError(failure,Error::kImport);
      },resolved);
      quota.Admit(keys->retained_bytes);quota.Step(keys->work);
      auto scanned=ScanIteratorProtocols(imported.graph,input,regions,*keys,quota.Remaining());
      if(auto* scan=std::get_if<IteratorProtocolScan>(&scanned)) {
        quota.Admit(scan->retained_bytes);quota.Step(scan->work);
        auto analyzed=AnalyzeCloseCompletion(body,binding->header.profile,imported.graph,input,regions,*keys,*scan,
            quota.Remaining(),context.Identity().contract);
        if(auto* summary=std::get_if<CloseCompletionSummary>(&analyzed)) {
          RetainCloseSummary(quota,*summary);quota.Step(summary->work);
          auto sealed=VerifyProtocolRegionOwnership(body,binding->header.profile,imported.graph,input,regions,
              std::move(*keys),std::move(*scan),std::move(*summary),quota.Remaining(),context.Identity().contract);
          if(auto* owner=std::get_if<VerifiedProtocolRegionOwnership>(&sealed)) {quota.Step(owner->VerificationWork());protocols.emplace(std::move(*owner));}
          else if(const auto& failure=std::get<CloseCompletionFailure>(sealed);
              failure.kind==CloseCompletionError::kResourceLimit || failure.kind==CloseCompletionError::kWorkLimit)
            return PhaseError(failure,Error::kControl);
        } else if(const auto& failure=std::get<CloseCompletionFailure>(analyzed);
            failure.kind==CloseCompletionError::kResourceLimit || failure.kind==CloseCompletionError::kWorkLimit)
          return PhaseError(failure,Error::kControl);
      } else return PhaseError(std::get<IteratorScanFailure>(scanned),Error::kControl);
    }
    auto controlled = BuildExceptionControl(body, imported.graph, input, regions, quota.Remaining(),protocols?&*protocols:nullptr);
    if (const auto* failure=std::get_if<ExceptionControlRegionFailure>(&controlled)) {
      if (input_gap) { input_gap->allocated_bytes = quota.Bytes(); input_gap->work = quota.Work(); }
      return PhasedControlFailure{Error::kControl,*failure,context.Target(),
          failure->exception_region ? failure->exception_region->body_pc : std::nullopt,
          failure->exception_region ? std::optional{failure->exception_region->working_body_pc} : std::nullopt,
          input_gap};
    }
    if (const auto* failure = std::get_if<ExceptionControlFailure>(&controlled))
      return PhasedControlFailure{PhaseError(*failure, Error::kControl), *failure, context.Target(),
          failure->cause.control.node.pc, failure->cause.control.node.working_pc};
    if (const auto* error = std::get_if<ExceptionControlError>(&controlled)) {
      const auto kind = PhaseError(*error, Error::kControl);
      // Resource refusal keeps its established API alternative and class;
      // ownership/location evidence describes semantic control refusals only.
      if (kind == Error::kResourceLimit || kind == Error::kWorkLimit) return kind;
      return PhasedControlFailure{kind, *error, context.Target()};
    }
    auto control = std::get<ExceptionControl>(std::move(controlled));
    quota.Admit(control.allocated_bytes); quota.Step(control.work);
    if(protocols) {
      const auto verified=VerifyExceptionControl(body,imported.graph,input,regions,control,quota.Remaining(),&*protocols);
      if(const auto* failure=std::get_if<ExceptionControlError>(&verified))return PhaseError(*failure,Error::kControl);
      quota.Step(std::get<std::uint64_t>(verified));
    }
    std::optional<abc::MethodBody> protocol_body;
    if(protocols) {
      const auto copy=[&](const auto& from,auto& to){using T=typename std::decay_t<decltype(to)>::value_type;quota.Admit(from.size(),sizeof(T));quota.Step(from.size());to.reserve(from.size());quota.Admit(to.capacity()-from.size(),sizeof(T));to.assign(from.begin(),from.end());};
      protocol_body.emplace();copy(body.instructions,protocol_body->instructions);copy(body.tries,protocol_body->tries);copy(body.handlers,protocol_body->handlers);
    }
    return PhasedBody(std::make_unique<Storage>(Storage{context.Identity(), context.Target(), frame, std::move(flow), std::move(ssa),
        std::move(environments), std::move(values), std::move(proof), std::move(initialized), std::move(imported.graph),
        std::move(cell_environments), std::move(cells),
        std::move(guards), std::move(aliases), std::move(regions), std::move(control),std::move(protocols),std::move(protocol_body),
        quota.Bytes(), quota.Work()}));
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<PhasedMethod, PhasedMethodError, EvaluationFailure> PhasedMethod::Build(
    const FunctionContext& context, const abc::MethodBody& body, analysis::SemanticBudget budget,
    const PhasedModuleBinding* binding) {
  auto built = PhasedBody::Build(context, body, budget, binding);
  if (const auto* error = std::get_if<PhasedMethodError>(&built)) return *error;
  if (const auto* failure = std::get_if<PhasedFlowFailure>(&built)) return failure->kind;
  if (std::holds_alternative<PhasedInputFailure>(built)) return PhasedMethodError::kValues;
  if (const auto* failure = std::get_if<PhasedModuleFailure>(&built)) return failure->kind;
  if (const auto* failure = std::get_if<PhasedImportFailure>(&built)) return failure->kind;
  if (const auto* failure = std::get_if<PhasedControlFailure>(&built)) {
    if(const auto* evaluation=std::get_if<EvaluationFailure>(&failure->cause))return *evaluation;
    return failure->kind;
  }
  return Evaluate(std::get<PhasedBody>(std::move(built)), budget);
}
std::variant<PhasedMethod, PhasedMethodError, EvaluationFailure> PhasedMethod::Evaluate(
    PhasedBody body, analysis::SemanticBudget budget) {
  try {
    PhaseBudget quota(budget);
    quota.Admit(sizeof(Storage)); quota.Admit(body.AllocatedBytes()); quota.Step(body.Work());
    const auto* aliases = body.Modules();
    const auto for_of=body.ForOfInput();
    auto evaluated = BuildPhasedEvaluationPlan(body.Graph(), quota.Remaining(), IncomingCells::kReject, {},
        aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{},{},{},analysis::ClosureWrites::kUnknown,{},{},for_of?&*for_of:nullptr);
    if (auto* failure = std::get_if<EvaluationFailure>(&evaluated)) {
      failure->evaluation.location.method = body.Target();
      if (failure->kind == EvaluationError::kResourceLimit) return Error::kResourceLimit;
      if (failure->kind == EvaluationError::kWorkLimit) return Error::kWorkLimit;
      return *failure;
    }
    auto evaluation = std::get<EvaluationPlan>(std::move(evaluated));
    quota.Admit(evaluation.allocated_bytes); quota.Step(evaluation.work);
    return PhasedMethod(std::make_unique<Storage>(Storage{std::move(body), std::move(evaluation),
        quota.Bytes(), quota.Work()}));
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
