#include "abcd/source/local_lexical_body.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <ostream>
#include <streambuf>

#include "capture_stream.h"
#include "fresh_object_fields.h"
#include "function_template.h"
#include "leaf_sources.h"
#include "linear_instruction.h"
#include "module_binding.h"
#include "module_binding_name.h"
#include "named_scope.h"

namespace abcd::source {
namespace {
constexpr std::string_view kCellPrefix = "$lex";
constexpr std::string_view kInnerPrefix = "$inner";
constexpr std::string_view kImportPrefix = "$import";
class LexicalWriter {
 public:
  LexicalWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
                const MethodConstants* constants, const LeafSources* functions,
                analysis::CapturedValues captured_values,
                ObjectBufferBudget object_budget,
                std::span<const FreshFieldSite> fields,
                std::span<const analysis::StagedNamedInvocation> staged_calls)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants),
        functions_(functions),
        captured_values_(captured_values),
        object_budget_(object_budget), fields_(fields),
        staged_calls_(staged_calls) {}
  BodyEmitResult Write(const abc::MethodBody& body,
                       const analysis::LinearLexicals& plan) {
    try {
      for (const auto cell : plan.Cells()) {
        Text("let ");
        Cell(cell);
        Text(";\n");
      }
      for (std::size_t i = 0; i < plan.Statements().size() && !error_; ++i) {
        offset_ = body.instructions[i].offset;
        const auto& statement = plan.Statements()[i];
        const auto* staged_call = staged_cursor_ < staged_calls_.size()
                                      ? &staged_calls_[staged_cursor_]
                                      : nullptr;
        if (staged_call && i == staged_call->property) {
          const auto result = WriteStagedCallHead(*staged_call, offset_, names_,
                                                output_, {remaining_}, constants_);
          if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
            error_ = *failure;
          else
            Count(std::get<std::size_t>(result));
          continue;
        }
        if (staged_call &&
            (i == staged_call->spill || i == staged_call->reload))
          continue;
        if (staged_call && i == staged_call->call) {
          const auto result = WriteStagedCallEnd(*staged_call, offset_, names_,
                                               output_, {remaining_});
          if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
            error_ = *failure;
          else
            Count(std::get<std::size_t>(result));
          ++staged_cursor_;
          continue;
        }
        if (field_cursor_ < fields_.size() &&
            fields_[field_cursor_].instruction == i) {
          if (!std::holds_alternative<analysis::KeepInstruction>(statement))
            return BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                                   offset_, {}, {}};
          const auto last = field_cursor_ + 1 == fields_.size() ||
                            fields_[field_cursor_ + 1].first !=
                                fields_[field_cursor_].first;
          if (last) {
            const auto first = fields_[field_cursor_].first;
            const auto result = WriteFreshFieldBatch(
                fields_.subspan(first, field_cursor_ - first + 1), first,
                offset_, names_, output_, {remaining_}, constants_);
            if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
              error_ = *failure;
            else
              Count(std::get<std::size_t>(result));
          } else {
            Text("const ");
            Text(kFieldTempPrefix);
            Number(static_cast<std::uint32_t>(field_cursor_));
            Text(" = ");
            Slot(analysis::Accumulator{});
            Text(";\n");
          }
          ++field_cursor_;
          continue;
        }
        if (std::holds_alternative<analysis::KeepInstruction>(statement)) {
          const auto result = WriteLinearInstruction(
              body.instructions[i], names_, output_, {remaining_}, constants_,
              object_budget_);
          if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
            error_ = *failure;
          else
            Count(std::get<std::size_t>(result));
        } else if (const auto* invocation =
                       std::get_if<analysis::NamedInvocation>(&statement)) {
          const auto result = WriteNamedInvocation(
              *invocation, offset_, names_, output_, {remaining_}, constants_);
          if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
            error_ = *failure;
          else
            Count(std::get<std::size_t>(result));
        } else if (std::holds_alternative<analysis::LocalFunction>(statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          if (!error_) {
            const auto result =
                functions_->Write(next_function_++, output_, {remaining_});
            if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
              error_ = *failure;
            else
              Count(std::get<std::size_t>(result));
          }
          Text(";\n");
        } else if (const auto* load =
                       std::get_if<analysis::LocalLoad>(&statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Cell(plan.Cells()[load->cell.value]);
          Text(";\n");
        } else if (const auto* store =
                       std::get_if<analysis::LocalStore>(&statement)) {
          Cell(plan.Cells()[store->cell.value]);
          Text(" = ");
          Slot(store->value);
          Text(";\n");
        } else if (const auto* load =
                       std::get_if<analysis::CapturedLoad>(&statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Capture(load->cell);
          Text(";\n");
        } else if (const auto* store =
                       std::get_if<analysis::CapturedStore>(&statement)) {
          Capture(store->cell);
          Text(" = ");
          Slot(store->value);
          Text(";\n");
        } else if (const auto* load =
                       std::get_if<analysis::ExternalImport>(&statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Text(kImportPrefix);
          Number(load->slot.value);
          Text(";\n");
        } else if (const auto* load =
                       std::get_if<analysis::LocalModuleLoad>(&statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Text(kLocalModulePrefix);
          Number(load->slot.value);
          Text(";\n");
        }
      }
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

 private:
  void Count(std::size_t bytes) {
    remaining_ -= bytes;
    written_ += bytes;
  }
  void Text(std::string_view text) {
    if (error_) return;
    if (text.size() > remaining_) {
      error_ = BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kOutputLimit};
      return;
    }
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) {
      error_ = BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
      return;
    }
    Count(text.size());
  }
  void Number(std::uint32_t value) {
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1> text{};
    const auto end =
        std::to_chars(text.data(), text.data() + text.size(), value).ptr;
    Text(std::string_view(text.data(), end - text.data()));
  }
  void Cell(analysis::LocalCell cell) {
    Text(captured_values_ == analysis::CapturedValues::kLanguage ? kInnerPrefix
                                                                 : kCellPrefix);
    Number(cell.environment.value);
    Text("_");
    Number(cell.slot.value);
  }
  void Capture(analysis::CapturedSlot cell) {
    if (error_) return;
    auto* buffer = dynamic_cast<CaptureBuffer*>(output_.rdbuf());
    if (!buffer || !buffer->Capture(cell))
      error_ = BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
  }
  void Slot(analysis::ScalarSlot slot) {
    if (error_) return;
    const auto result = std::visit(
        [&](const auto& value) {
          return WriteScalarExpression(analysis::ScalarValue{value}, names_,
                                       output_, {remaining_});
        },
        slot);
    if (const auto* failure = std::get_if<EmitError>(&result))
      error_ =
          BodyEmitFailure{BodyEmitError::kExpression, offset_, {}, *failure};
    else
      Count(std::get<std::size_t>(result));
  }
  ScalarNames names_;
  std::ostream& output_;
  std::size_t remaining_;
  const MethodConstants* constants_;
  const LeafSources* functions_;
  analysis::CapturedValues captured_values_;
  ObjectBufferBudget object_budget_;
  std::span<const FreshFieldSite> fields_;
  std::span<const analysis::StagedNamedInvocation> staged_calls_;
  std::size_t staged_cursor_ = 0;
  std::size_t field_cursor_ = 0;
  std::size_t next_function_ = 0;
  std::size_t written_ = 0;
  abc::CodeOffset offset_{};
  std::optional<BodyEmitFailure> error_;
};
LocalLexicalResult WriteLexicals(const abc::MethodBody& body, ScalarNames names,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 const MethodConstants* constants,
                                 const abc::MethodLookup* methods,
                                 analysis::CapturedValues captured_values) {
  if (const auto failure = ValidateGlobalScalarNames(body, names, constants))
    return *failure;
  const auto resource = [] {
    return analysis::LinearLexicalFailure{
        analysis::LinearLexicalError::kResourceLimit, {}};
  };
  if (names.accumulator.starts_with(kCellPrefix) ||
      names.accumulator.starts_with(kInnerPrefix) ||
      names.accumulator.starts_with(kImportPrefix) ||
      names.accumulator.starts_with(kLocalModulePrefix))
    return BodyEmitFailure{
        BodyEmitError::kExpression, {}, {}, EmitError::kInvalidBinding};
  for (const auto name : names.registers)
    if (name.starts_with(kCellPrefix) || name.starts_with(kInnerPrefix) ||
        name.starts_with(kImportPrefix) ||
        name.starts_with(kLocalModulePrefix))
      return BodyEmitFailure{
          BodyEmitError::kExpression, {}, {}, EmitError::kInvalidBinding};
  try {
    CaptureDiscard buffer;
    std::ostream discard(&buffer);
    std::optional<LeafSources> functions;
    std::span<const analysis::ClosureCreation> creations;
    const auto validated = ValidateNamedScopes(
        body, names.registers.size(), constants, budget.bytes, budget.work);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&validated))
      return *failure;
    const auto& named_scopes = std::get<NamedScopes>(validated);
    budget.bytes -= named_scopes.AllocatedBytes();
    const auto module_loads = ValidateGuardedModuleLoads(
        body, constants, budget.bytes,
        budget.work - named_scopes.work_units);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&module_loads))
      return *failure;
    const auto& guarded_loads = std::get<GuardedModuleLoads>(module_loads);
    budget.bytes -= guarded_loads.AllocatedBytes();
    const auto built = analysis::BuildNormalFlow(body, {budget.bytes});
    if (const auto* failure = std::get_if<analysis::FlowFailure>(&built))
      return *failure;
    const auto& graph = std::get<analysis::NormalFlow>(built);
    const auto graph_bytes =
        graph.blocks.capacity() * sizeof(analysis::FlowBlock);
    if (graph_bytes > budget.bytes) return resource();
    const auto bound = analysis::AnalyzeEnvironments(
        body, graph, names.registers.size(), {budget.bytes - graph_bytes});
    if (const auto* failure =
            std::get_if<analysis::EnvironmentFlowFailure>(&bound))
      return *failure;
    const auto& environments = std::get<analysis::EnvironmentFlow>(bound);
    if (environments.allocated_bytes > budget.bytes - graph_bytes)
      return resource();
    budget.bytes -= graph_bytes + environments.allocated_bytes;
    if (methods) {
      if (!constants)
        return BodyEmitFailure{BodyEmitError::kMissingConstants, {}, {}, {}};
      auto prepared =
          LeafSources::Build(body, environments, *constants, *methods, budget);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&prepared))
        return *failure;
      functions = std::get<LeafSources>(std::move(prepared));
      if (functions->AllocatedBytes() > budget.bytes) return resource();
      budget.bytes -= functions->AllocatedBytes();
      creations = functions->Facts();
    }
    const auto remaining_work = budget.work - named_scopes.work_units -
                                guarded_loads.work_units;
    auto matched = analysis::MatchStagedNamedInvocations(
        body, names.registers.size(), budget.bytes, remaining_work);
    if (const auto* error = std::get_if<analysis::StagedMatchError>(&matched))
      return analysis::LinearLexicalFailure{
          *error == analysis::StagedMatchError::kWorkLimit
              ? analysis::LinearLexicalError::kWorkLimit
              : analysis::LinearLexicalError::kResourceLimit,
          {}};
    const auto& staged = std::get<analysis::StagedMatches>(matched);
    analysis::IdfQuota quota{remaining_work - staged.work_units};
    const auto& staged_calls = staged.calls;
    if (staged_calls.capacity() > budget.bytes /
                                      sizeof(analysis::StagedNamedInvocation))
      return resource();
    budget.bytes -= staged_calls.capacity() *
                    sizeof(analysis::StagedNamedInvocation);
    const auto planned = analysis::LinearLexicals::Build(
        body, environments, names.registers.size(), quota, {budget.bytes},
        creations, captured_values, named_scopes.admitted,
        guarded_loads.admitted, staged_calls);
    if (const auto* failure =
            std::get_if<analysis::LinearLexicalFailure>(&planned))
      return *failure;
    const auto& plan = std::get<analysis::LinearLexicals>(planned);
    if (plan.AllocatedBytes() > budget.bytes) return resource();
    const auto field_plan = BuildFreshFieldPlan(
        body, names.registers.size(), budget.bytes - plan.AllocatedBytes());
    if (const auto* failure = std::get_if<BodyEmitFailure>(&field_plan))
      return *failure;
    const auto& fields = std::get<FreshFieldPlan>(field_plan);
    if (fields.sites.size() > 1) {
      const auto collision = [](std::string_view name) {
        return name.starts_with(kFieldTempPrefix);
      };
      if (collision(names.accumulator) ||
          std::any_of(names.registers.begin(), names.registers.end(),
                      collision))
        return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                               EmitError::kInvalidBinding};
    }
    const ObjectBufferBudget object_budget{
        budget.bytes - plan.AllocatedBytes() - fields.AllocatedBytes()};
    const auto checked =
        LexicalWriter(names, discard, budget.output, constants,
                      functions ? &*functions : nullptr, captured_values,
                      object_budget, fields.sites, staged_calls)
            .Write(body, plan);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&checked))
      return *failure;
    const auto result =
        LexicalWriter(names, output, budget.output, constants,
                      functions ? &*functions : nullptr, captured_values,
                      object_budget, fields.sites, staged_calls)
            .Write(body, plan);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
      return *failure;
    return std::get<std::size_t>(result);
  } catch (const std::bad_alloc&) {
    return resource();
  } catch (const std::length_error&) {
    return resource();
  }
}
}  // namespace
LocalLexicalResult WriteLocalLexicalBody(const abc::MethodBody& body,
                                         ScalarNames names,
                                         std::ostream& output,
                                         LocalLexicalBudget budget,
                                         const MethodConstants* constants,
                                         const abc::MethodLookup* methods) {
  return WriteLexicals(body, names, output, budget, constants, methods,
                       analysis::CapturedValues::kReject);
}
LocalLexicalResult WriteLocalLexicalTemplate(const abc::MethodBody& body,
                                             ScalarNames names,
                                             std::ostream& output,
                                             LocalLexicalBudget budget,
                                             const MethodConstants* constants) {
  return WriteLexicals(body, names, output, budget, constants, nullptr,
                       analysis::CapturedValues::kLanguage);
}
}  // namespace abcd::source
