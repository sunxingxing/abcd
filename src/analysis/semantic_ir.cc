#include "abcd/analysis/semantic_ir.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "abcd/abc/control_transfer.h"

namespace abcd::analysis {
namespace {
using abc::ControlTransfer;
using abc::DecodedInstruction;
using abc::Opcode;

SemanticFailure Fail(SemanticError kind, abc::CodeOffset pc = {}) {
  return {kind, pc};
}

std::variant<SemanticOperation, SemanticFailure> LiftOperation(
    const abc::BodyInstruction& site, std::uint64_t frame_size) {
  const auto& decoded = site.decoded;
  const auto transfer = abc::GetControlTransfer(decoded.opcode);
  if (!transfer) return Fail(SemanticError::kUnsupportedInstruction, site.offset);
  switch (*transfer) {
    case ControlTransfer::kBranch: {
      if (!site.branch_target)
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      const auto predicate = LiftBranchCondition(decoded);
      if (const auto* condition = std::get_if<UnaryExpression>(&predicate))
        return BranchOperation{*condition, *site.branch_target};
      return Fail(SemanticError::kMalformedInstruction, site.offset);
    }
    case ControlTransfer::kJump:
      if (!site.branch_target)
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      return JumpOperation{*site.branch_target};
    case ControlTransfer::kReturn:
      if (decoded.opcode == Opcode::kRETURN)
        return ReturnOperation{ScalarValue{Accumulator{}}};
      if (decoded.opcode == Opcode::kRETURNUNDEFINED)
        return ReturnOperation{ScalarValue{SpecialValue::kUndefined}};
      return Fail(SemanticError::kUnsupportedInstruction, site.offset);
    case ControlTransfer::kThrow:
    case ControlTransfer::kSuspend:
      return Fail(SemanticError::kUnsupportedInstruction, site.offset);
    case ControlTransfer::kFallthrough:
      break;
  }
  if (decoded.opcode == Opcode::kNOP) return NoOperation{};
  if (decoded.opcode == Opcode::kLDA_STR_ID16) {
    if (!std::holds_alternative<RegisterEffect>(
            LiftRegisterEffect(decoded, frame_size)))
      return Fail(SemanticError::kMalformedInstruction, site.offset);
    // Retain the method-relative pool reference; resolve UTF-16 only at the
    // output boundary. Never use a property-name filter for string values.
    return StringLoad{abc::PoolIndex{
        static_cast<std::uint16_t>(decoded.operands.front().raw_bits)}};
  }
  const auto scalar = LiftScalarInstruction(decoded, frame_size);
  if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar))
    return *assignment;
  if (std::get<ScalarError>(scalar) != ScalarError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);

  const auto named = LiftNamedRead(decoded);
  if (const auto* read = std::get_if<NamedRead>(&named)) return *read;
  if (std::get<NamedReadError>(named) != NamedReadError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto named_write = LiftNamedWrite(decoded, frame_size);
  if (const auto* write = std::get_if<NamedWrite>(&named_write)) return *write;
  if (std::get<NamedWriteError>(named_write) !=
      NamedWriteError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto own_write = LiftOwnWrite(decoded, frame_size);
  if (const auto* write = std::get_if<OwnWrite>(&own_write)) return *write;
  if (std::get<OwnWriteError>(own_write) != OwnWriteError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto value_write = LiftValueWrite(decoded, frame_size);
  if (const auto* write = std::get_if<ValueWrite>(&value_write)) return *write;
  if (std::get<ValueWriteError>(value_write) !=
      ValueWriteError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto module = LiftModuleLoad(decoded);
  if (const auto* load = std::get_if<ModuleLoad>(&module)) return *load;
  if (std::get<ModuleLoadError>(module) != ModuleLoadError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto hole = LiftHoleInstruction(decoded, frame_size);
  if (const auto* guard = std::get_if<HoleGuard>(&hole)) return *guard;
  if (std::holds_alternative<LoadHole>(hole))
    return Fail(SemanticError::kUnsupportedInstruction, site.offset);
  if (std::get<HoleError>(hole) != HoleError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto global = LiftGlobalAccess(decoded);
  if (const auto* access = std::get_if<GlobalAccess>(&global)) return *access;
  if (std::get<GlobalAccessError>(global) !=
      GlobalAccessError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  return Fail(SemanticError::kUnsupportedInstruction, site.offset);
}

SemanticEffects Effects(const SemanticOperation& operation,
                        RegisterEffect registers) {
  SemanticEffects effects;
  effects.normal_registers = registers;
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ScalarAssignment>) {
          if (std::holds_alternative<PropertyRead>(value.expression)) {
            effects.normal = LanguageEffect::kHeapRead |
                             LanguageEffect::kUserCode;
            effects.before_throw = effects.normal;
            effects.exceptional = ExceptionalCompletion::kMayThrow;
          } else if (value.completion == CompletionEffect::kMayThrow) {
            effects.normal = LanguageEffect::kUserCode;
            effects.before_throw = effects.normal;
            effects.exceptional = ExceptionalCompletion::kMayThrow;
          }
        } else if constexpr (std::is_same_v<T, NamedRead>) {
          effects.normal = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, NamedWrite> ||
                             std::is_same_v<T, OwnWrite> ||
                             std::is_same_v<T, ValueWrite>) {
          effects.normal = LanguageEffect::kHeapWrite |
                           LanguageEffect::kUserCode;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, ModuleLoad>) {
          effects.normal = LanguageEffect::kModuleRead;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, HoleGuard>) {
          effects.normal = LanguageEffect::kGuard;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, GlobalAccess>) {
          effects.normal = LanguageEffect::kGlobalAccess |
                           LanguageEffect::kUserCode;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else {
          static_assert(std::is_same_v<T, NoOperation> ||
                        std::is_same_v<T, StringLoad> ||
                        std::is_same_v<T, BranchOperation> ||
                        std::is_same_v<T, JumpOperation> ||
                        std::is_same_v<T, ReturnOperation>);
        }
      },
      operation);
  return effects;
}
}  // namespace

SemanticResult BuildSemanticMethod(const abc::MethodBody& body,
                                   abc::ProfileId profile,
                                   std::uint64_t frame_size,
                                   SemanticBudget budget) {
  switch (profile) {
    case abc::ProfileId::kDynamic1202:
    case abc::ProfileId::kDynamic1206:
    case abc::ProfileId::kDynamic1301:
      break;
    case abc::ProfileId::kUnknown:
    case abc::ProfileId::kDynamic9:
    case abc::ProfileId::kDynamic1102:
    case abc::ProfileId::kDynamic2400:
    case abc::ProfileId::kStatic0107:
      return Fail(SemanticError::kUnsupportedProfile);
  }
  if (!body.tries.empty() || !body.handlers.empty())
    return Fail(SemanticError::kExceptionRegions);
  const auto built = BuildNormalFlow(body, {budget.bytes});
  if (const auto* failure = std::get_if<FlowFailure>(&built))
    return Fail(failure->kind == FlowError::kResourceLimit
                    ? SemanticError::kResourceLimit
                    : SemanticError::kFlow,
                failure->offset);
  auto flow = std::get<NormalFlow>(std::move(built));
  const auto graph_bytes = flow.blocks.capacity() * sizeof(FlowBlock);
  if (graph_bytes > budget.bytes)
    return Fail(SemanticError::kResourceLimit);
  IdfQuota quota{budget.work};
  auto renamed = RegisterSsa::Build(
      body, flow, frame_size, quota, {budget.bytes - graph_bytes});
  if (auto* ssa = std::get_if<RegisterSsa>(&renamed)) {
    const auto ssa_bytes = ssa->AllocatedBytes();
    if (ssa_bytes > budget.bytes - graph_bytes)
      return Fail(SemanticError::kResourceLimit);
    auto remaining = budget.bytes - graph_bytes - ssa_bytes;
    if (body.instructions.size() > remaining / sizeof(SemanticInstruction))
      return Fail(SemanticError::kResourceLimit);
    try {
      std::vector<SemanticInstruction> instructions;
      instructions.reserve(body.instructions.size());
      if (instructions.capacity() > remaining / sizeof(SemanticInstruction))
        return Fail(SemanticError::kResourceLimit);
      for (const auto& site : body.instructions) {
        auto operation = LiftOperation(site, frame_size);
        if (const auto* failure = std::get_if<SemanticFailure>(&operation))
          return *failure;
        const auto registers = LiftRegisterEffect(site.decoded, frame_size);
        if (const auto* error = std::get_if<ScalarError>(&registers))
          return Fail(*error == ScalarError::kUnsupportedOpcode
                          ? SemanticError::kUnsupportedInstruction
                          : SemanticError::kMalformedInstruction,
                      site.offset);
        auto semantic = std::get<SemanticOperation>(std::move(operation));
        auto effects = Effects(semantic, std::get<RegisterEffect>(registers));
        instructions.push_back({site.offset, std::move(semantic),
                                effects});
      }
      return SemanticMethod{std::move(instructions), std::move(flow),
                            std::move(*ssa), graph_bytes + ssa_bytes +
                                body.instructions.size() *
                                    sizeof(SemanticInstruction)};
    } catch (const std::bad_alloc&) {
      return Fail(SemanticError::kResourceLimit);
    } catch (const std::length_error&) {
      return Fail(SemanticError::kResourceLimit);
    }
  }
  const bool resource = std::holds_alternative<SsaError>(renamed) &&
                        std::get<SsaError>(renamed) == SsaError::kResourceLimit;
  return Fail(resource ? SemanticError::kResourceLimit : SemanticError::kSsa);
}
}  // namespace abcd::analysis
