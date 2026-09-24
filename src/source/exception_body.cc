#include "exception_body.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>
#include "abcd/analysis/exception_modules.h"
#include "abcd/analysis/exception_regions.h"
#include "abcd/source/exception_source.h"
#include "abcd/source/named_scope.h"

namespace abcd::source {
SemanticBodyResult BuildExceptionBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  using namespace analysis;
  const auto fail = [](bool resource, abc::CodeOffset pc = {}) {
    return SourceFailure{resource ? SourceError::kResourceLimit : SourceError::kInvalidEffect, pc};
  };
  try {
    if (names.registers.size() != frame.registers || frame.locals > frame.registers)
      return fail(false);
    // Normalized async and rebased bodies need their own exception-owner
    // certificate. Never attach an original handler table to changed code.
    if (!constants.source_pcs.empty() || !constants.awaits.empty())
      return SourceFailure{SourceError::kUnsupportedOperation, {}};
    auto remaining = budget.bytes;
    const auto retain = [&](std::size_t bytes) {
      if (bytes > remaining) throw fail(true);
      remaining -= bytes;
    };
    auto work_left = budget.work;
    const auto stage_budget = [&] { return SemanticBudget{remaining, work_left}; };
    const auto charge = [&](std::uint64_t units) {
      if (units > work_left) throw fail(true);
      work_left -= units;
    };
    const auto built = BuildExceptionFlow(body, constants.header.profile, frame.registers,
        {remaining, budget.work});
    const auto* flow = std::get_if<ExceptionFlow>(&built);
    if (!flow) {
      if (const auto* error = std::get_if<SemanticFailure>(&built)) return *error;
      if (const auto* error = std::get_if<FlowFailure>(&built))
        return fail(error->kind == FlowError::kResourceLimit, error->offset);
      const auto& error = std::get<DispatchFailure>(built);
      return fail(error.kind == DispatchError::kResourceLimit, error.offset);
    }
    retain(flow->allocated_bytes);
    IdfQuota ssa_work{work_left};
    const auto renamed = ExceptionSsa::Build(*flow, frame.registers, ssa_work, {remaining});
    const auto* ssa = std::get_if<ExceptionSsa>(&renamed);
    if (!ssa) return std::visit([](const auto& error) -> SemanticFailure {
      using T = std::decay_t<decltype(error)>;
      if constexpr (std::is_same_v<T, ExceptionSsa>) {
        std::terminate();
      } else if constexpr (std::is_same_v<T, RegisterUseFailure>) {
        const bool resource = error.kind == RegisterUseError::kResourceLimit ||
                              error.kind == RegisterUseError::kWorkLimit;
        return {resource ? SemanticError::kResourceLimit : SemanticError::kSsa, error.offset, error};
      } else {
        bool resource = error == T::kResourceLimit;
        if constexpr (requires { T::kWorkLimit; }) resource |= error == T::kWorkLimit;
        return {resource ? SemanticError::kResourceLimit : SemanticError::kSsa, {}, error};
      }
    }, renamed);
    charge(ssa_work.Consumed());
    retain(ssa->AllocatedBytes());
    const auto bound = AnalyzeExceptionEnvironments(*flow, {remaining});
    const auto* environments = std::get_if<EnvironmentFlow>(&bound);
    if (!environments) {
      const auto& error = std::get<EnvironmentFlowFailure>(bound);
      return fail(error.kind == EnvironmentFlowError::kResourceLimit, error.offset);
    }
    retain(environments->allocated_bytes);
    const auto planned = ProveExceptionLocals(*flow, *environments, stage_budget());
    const auto* locals = std::get_if<ExceptionLocals>(&planned);
    if (!locals) {
      const auto& error = std::get<ExceptionLocalFailure>(planned);
      return fail(error.kind == ExceptionLocalError::kResourceLimit, error.pc);
    }
    retain(locals->AllocatedBytes());
    charge(locals->plan.work_units);
    const auto module_plan = ProveExceptionModules(*flow, *ssa, stage_budget());
    const auto* modules = std::get_if<ExceptionModules>(&module_plan);
    if (!modules) return fail(std::get<ExceptionModuleError>(module_plan) == ExceptionModuleError::kResourceLimit);
    retain(modules->AllocatedBytes());
    charge(modules->work_units);
    std::vector<ExceptionLoadFact> loads;
    const auto load_count = locals->loads.size() + modules->loads.size();
    if (load_count > remaining / sizeof(ExceptionLoadFact)) return fail(true);
    loads.reserve(load_count);
    retain(loads.capacity() * sizeof(ExceptionLoadFact));
    loads.insert(loads.end(), locals->loads.begin(), locals->loads.end());
    loads.insert(loads.end(), modules->loads.begin(), modules->loads.end());
    std::vector<EntryValueFact> entries;
    if (ssa->Values().size() > remaining / sizeof(EntryValueFact)) return fail(true);
    entries.reserve(ssa->Values().size());
    retain(entries.capacity() * sizeof(EntryValueFact));
    for (std::size_t i = 0; i < ssa->Values().size(); ++i) {
      charge(1);
      const auto& value = ssa->Values()[i];
      const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
        entries.push_back({SsaValueId{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
    }
    IdfQuota value_work{work_left};
    const auto analyzed = ExceptionValueFlow::Build(*flow, *ssa, entries, value_work,
        {remaining}, PendingExceptionValues::kUnknown, loads);
    const auto* values = std::get_if<ExceptionValueFlow>(&analyzed);
    if (!values) {
      const auto& error = std::get<ValueFlowFailure>(analyzed);
      return fail(error.kind == ValueFlowError::kResourceLimit || error.kind == ValueFlowError::kWorkLimit);
    }
    retain(values->AllocatedBytes());
    charge(value_work.Consumed());
    const auto checked_inputs = VerifyExceptionInputs(*flow, *ssa, *environments,
        *values, work_left);
    if (const auto* error = std::get_if<ExceptionInputFailure>(&checked_inputs))
      return SemanticProofFailure{error->kind == ExceptionInputError::kResourceLimit
          ? SemanticProofError::kResourceLimit : SemanticProofError::kInternalValue, error->pc};
    charge(std::get<ExceptionInputProof>(checked_inputs).work_units);
    const auto recovered = BuildExceptionRegions(body, *flow, stage_budget());
    const auto* regions = std::get_if<ExceptionRegions>(&recovered);
    if (!regions) {
      const auto& error = std::get<CatchRegionFailure>(recovered);
      return RegionFailure{error.kind == CatchRegionError::kResourceLimit
          ? RegionError::kResourceLimit : RegionError::kExceptionShape, error.pc};
    }
    retain(regions->allocated_bytes);
    charge(regions->work_units);
    const auto layout = BuildNormalFlow(body, {remaining});
    const auto* blocks = std::get_if<NormalFlow>(&layout);
    if (!blocks) {
      const auto& error = std::get<FlowFailure>(layout);
      return fail(error.kind == FlowError::kResourceLimit, error.offset);
    }
    retain(blocks->blocks.capacity() * sizeof(FlowBlock));
    const auto scoped = ValidateNamedScopes(body, frame.registers, &constants,
        remaining, work_left);
    const auto* scopes = std::get_if<NamedScopes>(&scoped);
    if (!scopes) {
      const auto& error = std::get<BodyEmitFailure>(scoped);
      return fail(error.kind == BodyEmitError::kResourceLimit || error.kind == BodyEmitError::kWorkLimit,
                  error.offset);
    }
    retain(scopes->AllocatedBytes());
    charge(scopes->work_units);
    auto structured = BuildExceptionRegionTree(*flow, *regions, *blocks, remaining);
    auto* tree = std::get_if<SemanticRegionTree>(&structured);
    if (!tree) return std::get<RegionFailure>(structured);
    const auto rest = names.rest ? ProveRestPrologue(body, frame, names.rest->first_argument) : std::nullopt;
    if (names.rest && !rest) return fail(false);
    const ExceptionAstInput input{*flow, *ssa, *environments, *values, *locals, *blocks, modules->reads};
    const auto lowered = BuildExceptionSourceAst(input, std::move(*tree), stage_budget(), scopes->admitted, rest);
    const auto* ast = std::get_if<SourceAst>(&lowered);
    if (!ast) return std::get<SourceFailure>(lowered);
    retain(ast->allocated_bytes);
    // Share the remaining allowance between output and transient constants.
    constexpr std::size_t kEmissionReserveParts = 8;
    const auto print_bytes = std::min(output_bytes, remaining / kEmissionReserveParts);
    auto bounded_constants = constants;
    bounded_constants.string_budget.bytes = std::min(constants.string_budget.bytes, print_bytes);
    auto printed = PrintSourceBody(*ast, *blocks, names, bounded_constants, print_bytes);
    if (auto* source = std::get_if<PrintedSource>(&printed)) return std::move(*source);
    return std::get<SourceFailure>(printed);
  } catch (const SourceFailure& error) { return error;
  } catch (const std::bad_alloc&) { return fail(true);
  } catch (const std::length_error&) { return fail(true); }
}
}  // namespace abcd::source
