#include "abcd/source/local_lexical_body.h"

#include <array>
#include <charconv>
#include <limits>
#include <ostream>
#include <streambuf>

#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/register_effect.h"
#include "capture_stream.h"
#include "function_template.h"
#include "leaf_sources.h"
#include "linear_instruction.h"
#include "module_binding.h"
#include "named_scope.h"

namespace abcd::source {
namespace {
constexpr std::string_view kCellPrefix = "$lex";
constexpr std::string_view kInnerPrefix = "$inner";
constexpr std::string_view kImportPrefix = "$import";
class FreshObjectFields {
 public:
  std::optional<abc::RegisterId> Step(
      const abc::DecodedInstruction& instruction, std::size_t frame_size) {
    const auto lifted = analysis::LiftRegisterEffect(instruction, frame_size);
    const auto* effect = std::get_if<analysis::RegisterEffect>(&lifted);
    if (!effect) {
      created_ = false;
      stored_.reset();
      active_.reset();
      return {};
    }
    if (created_) {
      created_ = false;
      if (instruction.opcode == abc::Opcode::kSTA_V8 &&
          effect->read_count == 1 &&
          std::holds_alternative<analysis::Accumulator>(effect->reads[0]) &&
          effect->write &&
          std::holds_alternative<abc::RegisterId>(*effect->write))
        stored_ = std::get<abc::RegisterId>(*effect->write);
      return {};
    }
    if (stored_) {
      const bool overwrites_acc =
          effect->write &&
          std::holds_alternative<analysis::Accumulator>(*effect->write);
      active_ = overwrites_acc && !Reads(*effect, *stored_) &&
                        !ReadsAccumulator(*effect)
                    ? stored_
                    : std::nullopt;
      stored_.reset();
    }
    if (std::holds_alternative<analysis::ObjectCreation>(
            analysis::LiftObjectCreation(instruction))) {
      active_.reset();
      created_ = true;
      return {};
    }
    const auto field = analysis::LiftOwnWrite(instruction, frame_size);
    if (instruction.opcode ==
            abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8 &&
        active_) {
      if (const auto* write = std::get_if<analysis::OwnWrite>(&field);
          write && write->kind == analysis::OwnWriteKind::kDefineField &&
          write->object == *active_)
        return active_;
    }
    if (active_ &&
        (Reads(*effect, *active_) || Writes(*effect, *active_)))
      active_.reset();
    return {};
  }

 private:
  static bool Reads(const analysis::RegisterEffect& effect,
                    abc::RegisterId reg) {
    for (std::size_t i = 0; i < effect.read_count; ++i)
      if (const auto* read = std::get_if<abc::RegisterId>(&effect.reads[i]);
          read && *read == reg)
        return true;
    return effect.range && reg.value >= effect.range->first.value &&
           reg.value - effect.range->first.value < effect.range->count;
  }
  static bool ReadsAccumulator(const analysis::RegisterEffect& effect) {
    for (std::size_t i = 0; i < effect.read_count; ++i)
      if (std::holds_alternative<analysis::Accumulator>(effect.reads[i]))
        return true;
    return false;
  }
  static bool Writes(const analysis::RegisterEffect& effect,
                     abc::RegisterId reg) {
    return effect.write &&
           std::get_if<abc::RegisterId>(&*effect.write) &&
           *std::get_if<abc::RegisterId>(&*effect.write) == reg;
  }
  bool created_ = false;
  std::optional<abc::RegisterId> stored_;
  std::optional<abc::RegisterId> active_;
};
class LexicalWriter {
 public:
  LexicalWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
                const MethodConstants* constants, const LeafSources* functions,
                analysis::CapturedValues captured_values,
                ObjectBufferBudget object_budget)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants),
        functions_(functions),
        captured_values_(captured_values),
        object_budget_(object_budget) {}
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
        const auto fresh_object = fields_.Step(body.instructions[i].decoded,
                                               names_.registers.size());
        const auto& statement = plan.Statements()[i];
        if (std::holds_alternative<analysis::KeepInstruction>(statement)) {
          const auto result = WriteLinearInstruction(
              body.instructions[i], names_, output_, {remaining_}, constants_,
              object_budget_, fresh_object);
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
  FreshObjectFields fields_;
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
  const auto resource = [] {
    return analysis::LinearLexicalFailure{
        analysis::LinearLexicalError::kResourceLimit, {}};
  };
  if (names.accumulator.starts_with(kCellPrefix) ||
      names.accumulator.starts_with(kInnerPrefix) ||
      names.accumulator.starts_with(kImportPrefix))
    return BodyEmitFailure{
        BodyEmitError::kExpression, {}, {}, EmitError::kInvalidBinding};
  for (const auto name : names.registers)
    if (name.starts_with(kCellPrefix) || name.starts_with(kInnerPrefix) ||
        name.starts_with(kImportPrefix))
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
    const auto imports = ValidateGuardedImports(
        body, constants, budget.bytes,
        budget.work - named_scopes.work_units);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&imports))
      return *failure;
    const auto& guarded_imports = std::get<GuardedImports>(imports);
    budget.bytes -= guarded_imports.AllocatedBytes();
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
    analysis::IdfQuota quota{budget.work - named_scopes.work_units -
                             guarded_imports.work_units};
    const auto planned = analysis::LinearLexicals::Build(
        body, environments, names.registers.size(), quota, {budget.bytes},
        creations, captured_values, named_scopes.admitted,
        guarded_imports.admitted);
    if (const auto* failure =
            std::get_if<analysis::LinearLexicalFailure>(&planned))
      return *failure;
    const auto& plan = std::get<analysis::LinearLexicals>(planned);
    if (plan.AllocatedBytes() > budget.bytes) return resource();
    const ObjectBufferBudget object_budget{budget.bytes - plan.AllocatedBytes()};
    const auto checked =
        LexicalWriter(names, discard, budget.output, constants,
                      functions ? &*functions : nullptr, captured_values,
                      object_budget)
            .Write(body, plan);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&checked))
      return *failure;
    const auto result =
        LexicalWriter(names, output, budget.output, constants,
                      functions ? &*functions : nullptr, captured_values,
                      object_budget)
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
