#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "../../../src/analysis/value_edits.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  auto body = test::Body({0x60, 0, 0x61, 1, 0x60, 1, 0x64});
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 2, {false, false, false, false}));
  const auto built = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206,
                                        frame.registers);
  const auto& method = std::get<SemanticMethod>(built);
  const auto checked = VerifySemanticSource(body, method, frame.registers, frame.locals);
  const auto& proof = std::get<SemanticProof>(checked);
  const auto imported = ImportNormalValues(body, method, proof, frame);
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(graph.ops.size() == body.instructions.size());
  test::Check(graph.values.size() == method.ssa.Values().size());
  test::Check(graph.input.values.size() == graph.values.size());
  test::Check(graph.input.sites.size() == body.instructions.size());
  test::Check(!VerifyValueUses(graph, graph.uses.size()));
  test::Check(graph.exceptional_exits.empty());
  test::Check(!VerifyImportedValues(graph, method, proof, frame));
  auto corrupt = graph;
  corrupt.ops[0].operation = ValueNop{};
  test::Check(VerifyImportedValues(corrupt, method, proof, frame)->kind ==
              ValueImportError::kInputMismatch);
  corrupt = graph;
  corrupt.uses[0].input->ordinal += 1;
  test::Check(VerifyImportedValues(corrupt, method, proof, frame)->kind ==
              ValueImportError::kOperandMismatch);
  corrupt = graph;
  corrupt.origins[0].pc.value += 1;
  test::Check(VerifyImportedValues(corrupt, method, proof, frame)->kind ==
              ValueImportError::kInputMismatch);
  corrupt = graph;
  corrupt.revision.value += 1;
  test::Check(VerifyImportedValues(corrupt, method, proof, frame)->kind ==
              ValueImportError::kInputMismatch);
  corrupt = graph;
  bool changed_entry = false;
  for (auto& value : corrupt.values) {
    auto* entry = std::get_if<EntryDefinition>(&value.origin);
    if (!entry) continue;
    auto* parameter = std::get_if<ParameterEntry>(entry);
    if (!parameter) continue;
    ++parameter->index;
    changed_entry = true;
    break;
  }
  test::Check(changed_entry);
  test::Check(VerifyImportedValues(corrupt, method, proof, frame)->kind ==
              ValueImportError::kInputMismatch);
  std::size_t occurrences = 0;
  for (const auto& use : graph.uses) {
    if (!use.input) continue;
    ++occurrences;
    const auto* value = std::get_if<ValueId>(&use.operand);
    test::Check(value != nullptr);
    test::Check(graph.input.values[value->value] == *value);
  }
  test::Check(occurrences == method.ssa.AllReads().size());
  const auto memory_limited = ImportNormalValues(body, method, proof, frame, {0, 1000});
  test::Check(std::get<ValueImportFailure>(memory_limited).kind == ValueImportError::kResourceLimit);
  const auto work_limited = ImportNormalValues(body, method, proof, frame, {1024 * 1024, 0});
  test::Check(std::get<ValueImportFailure>(work_limited).kind == ValueImportError::kWorkLimit);
  body.tries.push_back({{0}, 1, 0, 0});
  test::Check(std::get<ValueImportFailure>(
      ImportNormalValues(body, method, proof, frame)).kind == ValueImportError::kExceptionFlow);
  body.tries.clear();
  body.instructions[0].offset.value = 1;
  test::Check(std::get<ValueImportFailure>(
      ImportNormalValues(body, method, proof, frame)).kind == ValueImportError::kInputMismatch);
  // Entry-header loop: its phi has a virtual method-entry input in addition
  // to the real backedge. Import must not invent a predecessor for entry.
  const auto loop = test::Body({0x45, 1, 1, 0x4d, 0xfd});
  const auto loop_built = BuildSemanticMethod(loop, abc::ProfileId::kDynamic1206,
                                             frame.registers);
  const auto& loop_method = std::get<SemanticMethod>(loop_built);
  const auto loop_checked = VerifySemanticSource(loop, loop_method,
                                                 frame.registers, frame.locals);
  const auto& loop_proof = std::get<SemanticProof>(loop_checked);
  const auto loop_import = ImportNormalValues(loop, loop_method, loop_proof, frame);
  const auto& loop_graph = std::get<ValueProgram>(loop_import);
  test::Check(!VerifyImportedValues(loop_graph, loop_method, loop_proof, frame));
  bool has_entry = false;
  bool has_backedge = false;
  for (const auto& use : loop_graph.uses) {
    if (!use.incoming) continue;
    has_entry |= std::holds_alternative<MethodEntryKey>(*use.incoming);
    has_backedge |= std::holds_alternative<EdgeId>(*use.incoming);
  }
  test::Check(has_entry && has_backedge);
  // Pinned ISA forms: empty object has no operands; empty array has an IC slot.
  constexpr std::uint8_t kFirstCacheSlot = 0;
  const auto returned = static_cast<std::uint8_t>(abc::Opcode::kRETURN);
  for (const bool external : {false, true}) for (const bool wide : {false, true}) {
    // Pinned v7 local/external module slots, including the wide u16 boundary.
    constexpr std::uint8_t kWidePrefix = 0xfd, kSlotByte = 0xff;
    const auto opcode = external ? (wide ? abc::Opcode::kWIDE_LDEXTERNALMODULEVAR_PREF_IMM16 :
        abc::Opcode::kLDEXTERNALMODULEVAR_IMM8) : (wide ? abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16 :
        abc::Opcode::kLDLOCALMODULEVAR_IMM8);
    std::vector<std::uint8_t> bytes;
    if (wide) bytes = {kWidePrefix, static_cast<std::uint8_t>(static_cast<std::uint16_t>(opcode) >> 8), kSlotByte, kSlotByte};
    else bytes = {static_cast<std::uint8_t>(opcode), kSlotByte};
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
    const auto body = test::Body(bytes);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto value = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    test::Check(!VerifyImportedValues(value, input, proof, frame));
    const auto& module = std::get<ValueModuleRead>(value.ops[0].operation);
    test::Check(module.space == (external ? ModuleSpace::kExternal : ModuleSpace::kLocal));
    test::Check(module.slot.value == (wide ? UINT16_MAX : UINT8_MAX));
    test::Check(value.ops[0].normal == LanguageEffect::kModuleRead &&
        value.ops[0].exceptional == ExceptionalCompletion::kMayThrow && value.ops[0].use_count == 0);
    auto stripped = value.ops[0];
    stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(stripped));
    auto changed = value;
    std::get<ValueModuleRead>(changed.ops[0].operation).space = external ? ModuleSpace::kLocal : ModuleSpace::kExternal;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = value; --std::get<ValueModuleRead>(changed.ops[0].operation).slot.value;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  }
  for (const bool wide : {false, true}) {
    // STMODULEVAR preserves the accumulator; even an unused stored value is
    // an observable live-cell update, not a removable register assignment.
    constexpr std::uint8_t kWidePrefix = 0xfd, kSlotByte = 0xff;
    constexpr std::uint32_t kStoreIndex = 1, kReturnIndex = 2;
    constexpr abc::CodeOffset kStorePc{1};
    const auto opcode = wide ? abc::Opcode::kWIDE_STMODULEVAR_PREF_IMM16 :
        abc::Opcode::kSTMODULEVAR_IMM8;
    std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED)};
    if (wide) bytes.insert(bytes.end(), {kWidePrefix,
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(opcode) >> 8),
        kSlotByte, kSlotByte});
    else bytes.insert(bytes.end(), {static_cast<std::uint8_t>(opcode), kSlotByte});
    bytes.push_back(returned);
    const auto body = test::Body(bytes);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(
        body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(
        body, input, frame.registers, frame.locals));
    const auto value = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    test::Check(!VerifyImportedValues(value, input, proof, frame));
    const auto& store = value.ops[kStoreIndex];
    const auto slot = std::get<ValueModuleWrite>(store.operation).slot;
    test::Check(slot.value == (wide ? UINT16_MAX : UINT8_MAX));
    test::Check(!store.result && store.use_count == 1);
    test::Check(value.origins[store.origin.value].pc == kStorePc);
    test::Check(store.normal == LanguageEffect::kModuleWrite &&
        store.before_throw == LanguageEffect::kModuleWrite &&
        store.exceptional == ExceptionalCompletion::kMayThrow);
    const auto& stored = value.uses[store.first_use.value];
    test::Check(stored.role == OperandRole::kStoredValue);
    test::Check(stored.operand == value.uses[value.ops[kReturnIndex].first_use.value].operand);
    auto stripped = store;
    stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(stripped));
    auto changed = value;
    --std::get<ValueModuleWrite>(changed.ops[kStoreIndex].operation).slot.value;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind ==
        ValueImportError::kInputMismatch);
    changed = value;
    changed.uses[store.first_use.value].role = OperandRole::kInput;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind ==
        ValueImportError::kOperandMismatch);
    changed = value;
    changed.ops[kStoreIndex].before_throw = LanguageEffect::kNone;
    test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
    changed = value;
    test::Check(std::holds_alternative<ValueSimplifyStats>(
        SimplifyValues(changed, changed.revision)));
    const auto& retained = changed.ops[kStoreIndex];
    test::Check(retained.life == ValueLife::kLive && retained.operation == store.operation);
    test::Check(retained.origin == store.origin && retained.normal == store.normal &&
        retained.before_throw == store.before_throw && retained.exceptional == store.exceptional);
    test::Check(changed.uses[retained.first_use.value].operand ==
        changed.uses[changed.ops[kReturnIndex].first_use.value].operand);
    test::Check(!VerifyValueUses(changed, changed.uses.size()));
  }
  for (const bool array : {false, true}) {
    const auto allocation = array ? test::Body({
        static_cast<std::uint8_t>(abc::Opcode::kCREATEEMPTYARRAY_IMM8), kFirstCacheSlot, returned}) :
        test::Body({static_cast<std::uint8_t>(abc::Opcode::kCREATEEMPTYOBJECT), returned});
    const auto semantics = BuildSemanticMethod(allocation, abc::ProfileId::kDynamic1206, frame.registers);
    const auto& input = std::get<SemanticMethod>(semantics);
    const auto verified = VerifySemanticSource(allocation, input, frame.registers, frame.locals);
    const auto& source = std::get<SemanticProof>(verified);
    const auto lowered = ImportNormalValues(allocation, input, source, frame);
    const auto& value = std::get<ValueProgram>(lowered);
    test::Check(!VerifyImportedValues(value, input, source, frame));
    test::Check(value.ops[0].use_count == 0 && value.ops[0].result.has_value());
    test::Check(array ? std::holds_alternative<ValueArrayAllocation>(value.ops[0].operation) :
        std::holds_alternative<ValueObjectAllocation>(value.ops[0].operation));
    test::Check(value.ops[0].normal == LanguageEffect::kAllocation);
    auto changed = value;
    changed.ops[0].operation = array ? ValueOperation{ValueObjectAllocation{}} : ValueOperation{ValueArrayAllocation{}};
    test::Check(VerifyImportedValues(changed, input, source, frame)->kind == ValueImportError::kInputMismatch);
  }
  for (const bool wide : {false, true}) {
    constexpr std::uint8_t kCacheLow = 0x34, kCacheHigh = 0x12;
    constexpr std::uint8_t kLiteralLow = 0x78, kLiteralHigh = 0x56;
    constexpr abc::PoolIndex kLiteral{0x5678};
    const auto opcode = wide ? abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16 :
        abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(opcode), kCacheLow};
    if (wide) bytes.push_back(kCacheHigh);
    bytes.insert(bytes.end(), {kLiteralLow, kLiteralHigh, returned});
    const auto body = test::Body(bytes);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto value = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    test::Check(!VerifyImportedValues(value, input, proof, frame));
    test::Check(std::get<ValueObjectAllocation>(value.ops[0].operation).literal == kLiteral);
    test::Check(value.ops[0].use_count == 0 && value.ops[0].result &&
        value.ops[0].normal == LanguageEffect::kAllocation);
    auto changed = value;
    std::get<ValueObjectAllocation>(changed.ops[0].operation).literal.reset();
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = value;
    ++std::get<ValueObjectAllocation>(changed.ops[0].operation).literal->value;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  }
  for (const auto opcode : {abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8,
      abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8,
      abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8,
      abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8}) {
    constexpr std::uint8_t kReceiver = 0, kValue = 1;
    constexpr std::uint8_t kCacheLow = 0x34, kCacheHigh = 0x12;
    constexpr std::uint8_t kNameLow = 0x78, kNameHigh = 0x56;
    constexpr std::uint16_t kName = 0x5678, kWideCache = 0x1234;
    const bool wide = opcode == abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8;
    const bool store = wide || opcode == abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), kValue,
        static_cast<std::uint8_t>(opcode), kCacheLow};
    if (wide) bytes.push_back(kCacheHigh);
    bytes.insert(bytes.end(), {kNameLow, kNameHigh, kReceiver, returned});
    const auto body = test::Body(bytes);
    const auto built = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers);
    const auto& input = std::get<SemanticMethod>(built);
    const auto checked = VerifySemanticSource(body, input, frame.registers, frame.locals);
    const auto& proof = std::get<SemanticProof>(checked);
    const auto imported = ImportNormalValues(body, input, proof, frame);
    const auto& graph = std::get<ValueProgram>(imported);
    test::Check(!VerifyImportedValues(graph, input, proof, frame));
    const auto& op = graph.ops[1];
    const auto& write = std::get<ValueOwnWrite>(op.operation);
    test::Check(write.kind == (store ? OwnWriteKind::kStore : OwnWriteKind::kDefineField));
    test::Check(write.name.value == kName && write.cache_slot == (wide ? kWideCache : kCacheLow));
    test::Check(!op.result && op.use_count == 2 && op.exceptional == ExceptionalCompletion::kMayThrow);
    test::Check(graph.uses[op.first_use.value].role == OperandRole::kBase);
    const auto& stored = graph.uses[op.first_use.value + 1];
    test::Check(stored.role == OperandRole::kStoredValue &&
        std::get<ValueId>(stored.operand) ==
        std::get<ValueId>(graph.uses[graph.ops[2].first_use.value].operand));
    auto changed = graph;
    changed.ops[1].operation = ValueNamedWrite{write.name};
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = graph;
    std::get<ValueOwnWrite>(changed.ops[1].operation).cache_slot ^= 1;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = graph;
    std::get<ValueOwnWrite>(changed.ops[1].operation).kind = store ? OwnWriteKind::kDefineField : OwnWriteKind::kStore;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = graph;
    ++std::get<ValueOwnWrite>(changed.ops[1].operation).name.value;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  }
}
