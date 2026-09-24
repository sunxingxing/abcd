#include "abcd/analysis/semantic_ir.h"

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/coroutine.h"

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
      if (decoded.opcode == Opcode::kTHROW_PREF_NONE) {
        if (decoded.operand_count != 0)
          return Fail(SemanticError::kMalformedInstruction, site.offset);
        return ThrowOperation{ScalarValue{Accumulator{}}};
      }
      return Fail(SemanticError::kUnsupportedInstruction, site.offset);
    case ControlTransfer::kSuspend:
      return Fail(SemanticError::kUnsupportedInstruction, site.offset);
    case ControlTransfer::kFallthrough:
      break;
  }
  if (decoded.opcode == Opcode::kNOP) return NoOperation{};
  const auto lexical = LiftLexicalInstruction(decoded, frame_size);
  if (!std::holds_alternative<LexicalError>(lexical)) {
    return std::visit([&](const auto& value)
        -> std::variant<SemanticOperation, SemanticFailure> {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, LexicalError>)
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      else
        return SemanticOperation{value};
    }, lexical);
  }
  if (std::get<LexicalError>(lexical) != LexicalError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto creation = LiftFunctionCreation(decoded);
  if (const auto* function = std::get_if<FunctionCreation>(&creation))
    return *function;
  if (std::get<CreationError>(creation) != CreationError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto module_store = LiftModuleStore(decoded);
  if (const auto* store = std::get_if<ModuleStore>(&module_store)) return *store;
  if (std::get<ModuleStoreError>(module_store) != ModuleStoreError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto constructor = LiftConstructorCall(decoded, frame_size);
  if (const auto* call = std::get_if<ConstructorCall>(&constructor)) return *call;
  if (std::get<ConstructorCallError>(constructor) != ConstructorCallError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto object = LiftObjectCreation(decoded);
  if (const auto* empty = std::get_if<EmptyObjectCreation>(&object)) return *empty;
  if (const auto* buffered = std::get_if<BufferedObjectCreation>(&object)) return *buffered;
  if (std::get<ObjectCreationError>(object) != ObjectCreationError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto array = LiftArrayCreation(decoded);
  if (const auto* buffered = std::get_if<BufferedArrayCreation>(&array)) return *buffered;
  if (std::get<ArrayCreationError>(array) != ArrayCreationError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto call = LiftOrdinaryCall(decoded, frame_size);
  if (const auto* value = std::get_if<OrdinaryCall>(&call)) return *value;
  if (std::get<CallError>(call) != CallError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto receiver_call = LiftReceiverCall(decoded, frame_size);
  if (const auto* value = std::get_if<ReceiverCall>(&receiver_call)) return *value;
  if (std::get<CallError>(receiver_call) != CallError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
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
  if (const auto* load = std::get_if<LoadHole>(&hole)) return *load;
  if (std::get<HoleError>(hole) != HoleError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto super = LiftSuperInstruction(decoded, frame_size);
  if (const auto* check = std::get_if<SuperCheck>(&super)) return *check;
  if (const auto* error = std::get_if<SuperError>(&super);
      error && *error != SuperError::kUnsupportedOpcode)
    return Fail(SemanticError::kMalformedInstruction, site.offset);
  const auto rest = LiftRestArguments(decoded);
  if (const auto* copy = std::get_if<RestCopy>(&rest)) return *copy;
  if (std::get<RestError>(rest) != RestError::kUnsupportedOpcode)
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
        } else if constexpr (std::is_same_v<T, LexicalLoad>) {
          // Pinned interpreter-inl.cpp LDLEXVAR: raw cell read, no user
          // getter and no TDZ check. Bounds and Hole proof are separate.
          effects.normal = LanguageEffect::kEnvironmentRead |
                           LanguageEffect::kLexicalRead;
        } else if constexpr (std::is_same_v<T, LexicalStore>) {
          effects.normal = LanguageEffect::kEnvironmentRead |
                           LanguageEffect::kLexicalWrite;
        } else if constexpr (std::is_same_v<T, PushEnvironment>) {
          // NEWLEXENV installs env/acc only after successful allocation.
          // Its new cells contain Hole; env identity is not a JS value.
          effects.before_throw = LanguageEffect::kEnvironmentRead |
                                 LanguageEffect::kAllocation;
          effects.normal = effects.before_throw |
                           LanguageEffect::kEnvironmentWrite;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, PopEnvironment>) {
          effects.normal = LanguageEffect::kEnvironmentRead |
                           LanguageEffect::kEnvironmentWrite;
        } else if constexpr (std::is_same_v<T, CurrentEnvironment>) {
          effects.normal = LanguageEffect::kEnvironmentRead;
        } else if constexpr (std::is_same_v<T, FunctionCreation>) {
          // Conservative effects until target attributes are resolved.
          // Ordinary and sendable creation differ in capture; this does not
          // authorize either wrapper or deduplication of dynamic allocations.
          effects.normal = LanguageEffect::kEnvironmentRead |
                           LanguageEffect::kModuleRead |
                           LanguageEffect::kHeapRead |
                           LanguageEffect::kAllocation;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, EmptyObjectCreation> ||
                             std::is_same_v<T, BufferedArrayCreation> ||
                             std::is_same_v<T, BufferedObjectCreation>) {
          effects.normal = LanguageEffect::kAllocation;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, OrdinaryCall> ||
                             std::is_same_v<T, ReceiverCall> ||
                             std::is_same_v<T, ConstructorCall>) {
          effects.normal = LanguageEffect::kHeapRead |
                           LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, ThrowOperation>) {
          // Pinned interpreter THROW_PREF_NONE passes acc to Throw and jumps
          // directly to exception dispatch. No coercion, normal destination,
          // or fallthrough may be invented; SSA still checks the value read.
          effects.exceptional = ExceptionalCompletion::kAlwaysThrow;
        } else if constexpr (std::is_same_v<T, AwaitValue>) {
          effects.normal = LanguageEffect::kSuspension |
                           LanguageEffect::kUserCode |
                           LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
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
        } else if constexpr (std::is_same_v<T, ModuleStore>) {
          effects.normal = LanguageEffect::kModuleWrite;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, RestCopy>) {
          effects.normal = LanguageEffect::kArgumentsRead |
                           LanguageEffect::kAllocation;
          effects.before_throw = effects.normal;
          effects.exceptional = ExceptionalCompletion::kMayThrow;
        } else if constexpr (std::is_same_v<T, SuperCheck>) {
          switch (value.kind) {
            case SuperCheckKind::kRequireInitialized:
            case SuperCheckKind::kRequireUninitialized:
              effects.normal = LanguageEffect::kGuard;
              effects.before_throw = effects.normal;
              effects.exceptional = ExceptionalCompletion::kMayThrow;
              break;
            case SuperCheckKind::kNoop:
              break;
          }
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
                        std::is_same_v<T, LoadHole> ||
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

InstructionLift LiftSemanticInstruction(const abc::BodyInstruction& site,
                                        abc::ProfileId profile,
                                        std::uint64_t frame_size) {
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
      return Fail(SemanticError::kUnsupportedProfile, site.offset);
  }
  auto operation = LiftOperation(site, frame_size);
  if (const auto* failure = std::get_if<SemanticFailure>(&operation)) return *failure;
  const auto registers = LiftRegisterEffect(site.decoded, frame_size);
  if (const auto* error = std::get_if<ScalarError>(&registers))
    return Fail(*error == ScalarError::kUnsupportedOpcode
                    ? SemanticError::kUnsupportedInstruction
                    : SemanticError::kMalformedInstruction, site.offset);
  auto semantic = std::get<SemanticOperation>(std::move(operation));
  auto effects = Effects(semantic, std::get<RegisterEffect>(registers));
  return SemanticInstruction{site.offset, std::move(semantic), effects};
}

SemanticResult BuildSemanticMethod(const abc::MethodBody& body,
                                   abc::ProfileId profile,
                                   std::uint64_t frame_size,
                                   SemanticBudget budget,
                                   std::span<const AwaitSite> awaits) {
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
  auto built = BuildNormalFlow(body, {budget.bytes});
  if (const auto* failure = std::get_if<FlowFailure>(&built))
    return Fail(failure->kind == FlowError::kResourceLimit
                    ? SemanticError::kResourceLimit
                    : SemanticError::kFlow,
                failure->offset);
  auto flow = std::get<NormalFlow>(std::move(built));
  const auto graph_bytes = flow.blocks.capacity() * sizeof(FlowBlock);
  if (graph_bytes > budget.bytes)
    return Fail(SemanticError::kResourceLimit);
  if (body.instructions.size() > budget.work)
    return SemanticFailure{SemanticError::kSsa, {}, SsaError::kWorkLimit};
  IdfQuota quota{budget.work - body.instructions.size()};
  try {
  const auto available = budget.bytes - graph_bytes;
  if (body.instructions.size() > available / sizeof(SemanticInstruction))
    return Fail(SemanticError::kResourceLimit);
  std::vector<SemanticInstruction> instructions;
  instructions.reserve(body.instructions.size());
  if (instructions.capacity() > available / sizeof(SemanticInstruction))
    return Fail(SemanticError::kResourceLimit);
  // Lift once before SSA. The exact same typed normal effects drive def/use,
  // renaming and subsequent source validation; future recovered operations
  // must not silently revert to the original VM operand effects in SSA.
  std::size_t next_await = 0;
  std::size_t next_mode = 0;
  for (const auto& site : body.instructions) {
    if (next_mode < awaits.size() && awaits[next_mode].mode_pc < site.offset)
      return Fail(SemanticError::kMalformedInstruction, site.offset);
    if (next_mode < awaits.size() && awaits[next_mode].mode_pc == site.offset) {
      if (next_mode >= next_await)
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      ++next_mode;
      const auto lifted = LiftCoroutineInstruction(site.decoded, frame_size);
      const auto* read = std::get_if<CoroutineRead>(&lifted);
      if (!read || read->kind != CoroutineReadKind::kResumeMode ||
          !std::holds_alternative<Accumulator>(read->state))
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      // The private async state can only resume NEXT or THROW. Native await
      // already takes the throwing path; normal continuation has exact NEXT.
      SemanticOperation operation = ScalarAssignment{
          Accumulator{}, ScalarValue{IntegerLiteral{
              static_cast<std::int32_t>(CoroutineResumeMode::kNext)}}};
      RegisterEffect registers;
      registers.write = Accumulator{};
      const auto effects = Effects(operation, registers);
      instructions.push_back({site.offset, std::move(operation), effects});
      continue;
    }
    if (next_await < awaits.size() && awaits[next_await].pc < site.offset)
      return Fail(SemanticError::kMalformedInstruction, site.offset);
    if (next_await < awaits.size() && awaits[next_await].pc == site.offset) {
      const auto& awaited = awaits[next_await++];
      const auto lifted = LiftCoroutineInstruction(site.decoded, frame_size);
      const auto* transfer = std::get_if<CoroutineTransfer>(&lifted);
      const auto* state = transfer
          ? std::get_if<abc::RegisterId>(&transfer->state) : nullptr;
      if (!transfer || transfer->kind != CoroutineTransferKind::kAwait ||
          !state || *state != awaited.origin.state ||
          !std::holds_alternative<Accumulator>(transfer->value))
        return Fail(SemanticError::kMalformedInstruction, site.offset);
      // Ownership and region equivalence belong to the async normalizer.
      // Native await reads the source value, never the discarded VM state.
      SemanticOperation operation = AwaitValue{Accumulator{}, awaited.origin};
      RegisterEffect registers;
      registers.reads[registers.read_count++] = Accumulator{};
      registers.write = Accumulator{};
      const auto effects = Effects(operation, registers);
      instructions.push_back({site.offset, std::move(operation), effects});
      continue;
    }
    auto lifted = LiftSemanticInstruction(site, profile, frame_size);
    if (const auto* failure = std::get_if<SemanticFailure>(&lifted))
      return *failure;
    instructions.push_back(std::get<SemanticInstruction>(std::move(lifted)));
  }
  if (next_await != awaits.size() || next_mode != awaits.size())
    return Fail(SemanticError::kMalformedInstruction);
  const auto instruction_bytes =
      instructions.capacity() * sizeof(SemanticInstruction);
  auto renamed = RegisterSsa::Build(
      body, flow, frame_size, quota, {available - instruction_bytes}, instructions);
  if (auto* ssa = std::get_if<RegisterSsa>(&renamed)) {
    const auto ssa_bytes = ssa->AllocatedBytes();
    if (ssa_bytes > available - instruction_bytes)
      return Fail(SemanticError::kResourceLimit);
    return SemanticMethod{std::move(instructions), std::move(flow),
                          std::move(*ssa), graph_bytes + ssa_bytes +
                              instruction_bytes};
  }
  return std::visit([](const auto& failure) -> SemanticFailure {
    using T = std::decay_t<decltype(failure)>;
    if constexpr (std::is_same_v<T, RegisterSsa>) {
      std::terminate();
    } else if constexpr (std::is_same_v<T, RegisterUseFailure>) {
      return {failure.kind == RegisterUseError::kResourceLimit
                  ? SemanticError::kResourceLimit : SemanticError::kSsa,
              failure.offset, failure};
    } else {
      return {failure == T::kResourceLimit ? SemanticError::kResourceLimit
                                           : SemanticError::kSsa,
              {}, failure};
    }
  }, renamed);
  } catch (const std::bad_alloc&) {
    return Fail(SemanticError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SemanticError::kResourceLimit);
  }
}
}  // namespace abcd::analysis
