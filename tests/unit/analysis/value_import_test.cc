#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "abcd/analysis/value_environment.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_heritage.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
    const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
    constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    constexpr unsigned kByteBits = 8;
    const auto body = test::Body({op(abc::Opcode::kNEWLEXENV_IMM8),1,op(abc::Opcode::kRETURNUNDEFINED),
        op(abc::Opcode::kPOPLEXENV),static_cast<std::uint8_t>(kThrow),static_cast<std::uint8_t>(kThrow >> kByteBits)});
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{false,false,false,false}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body,profile,0));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body,input,0,0));
    const auto graph = std::get<ValueProgram>(ImportNormalValues(body,input,proof,frame));
    test::Check(std::holds_alternative<PopEnvironment>(input.instructions[2].operation));
    for (const auto index : {2U,3U}) {
      test::Check(!proof.environments.before[index] && graph.ops[index].life == ValueLife::kRemoved &&
          std::holds_alternative<ValueNop>(graph.ops[index].operation) &&
          graph.origins[index].pc == body.instructions[index].offset &&
          graph.origins[index].disposition == OriginDisposition::kRetained);
    }
    test::Check(!VerifyImportedValues(graph,input,proof,frame));
    test::Check(std::holds_alternative<ValueEnvironments>(BuildValueEnvironments(graph)));
    auto changed = graph; changed.ops[2].operation = ValueEnvironmentPop{};
    test::Check(VerifyImportedValues(changed,input,proof,frame).has_value());
    changed = graph; changed.ops[2].normal = LanguageEffect::kEnvironmentWrite;
    test::Check(VerifyImportedValues(changed,input,proof,frame).has_value());
    changed = graph; changed.ops[0].operation = ValueNop{};
    test::Check(VerifyImportedValues(changed,input,proof,frame).has_value());
    changed = graph; changed.ops[2].life = ValueLife::kLive;
    test::Check(VerifyImportedValues(changed,input,proof,frame).has_value());
  }

  {
    const auto body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
        static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 1,
        static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
        static_cast<std::uint8_t>(abc::Opcode::kSUPERCALLSPREAD_IMM8_V8), 0, 1,
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 2));
    const auto failure = std::get<SemanticProofFailure>(VerifySemanticSource(body, input, 2, 0));
    test::Check(failure.kind == SemanticProofError::kInternalValue && failure.pc == abc::CodeOffset{5});
  }
  // Spread super retains its explicit constructor and aggregate array. Never
  // reinterpret the array as a single range argument or drop its throwing call.
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const unsigned count : {1U, 2U, 200U, 201U}) {
      std::vector<std::uint8_t> bytes;
      for (unsigned i = 0; i < count; ++i)
        bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
            static_cast<std::uint8_t>(abc::Opcode::kSUPERCALLSPREAD_IMM8_V8), 7, 1});
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
      const auto body = test::Body(bytes);
      const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {false, false, false, false}));
      const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, frame.registers));
      const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
      auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
      test::Check(!VerifyImportedValues(graph, input, proof, frame));
      for (unsigned i = 0; i < count; ++i) {
        const auto& op = graph.ops[2 * i + 1];
        test::Check(std::get<ValueSuperSpread>(op.operation).cache_slot == 7);
        test::Check(op.result && op.use_count == 2 && !CanErasePureOp(op));
        test::Check(op.exceptional == ExceptionalCompletion::kMayThrow && op.normal == op.before_throw);
        test::Check(op.normal == (LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite |
            LanguageEffect::kUserCode | LanguageEffect::kAllocation));
        test::Check(graph.uses[op.first_use.value].role == OperandRole::kConstructor &&
            graph.uses[op.first_use.value + 1].role == OperandRole::kInput);
      }
      auto changed = graph;
      ++std::get<ValueSuperSpread>(changed.ops[1].operation).cache_slot;
      test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
      changed = graph; changed.ops[1].operation = ValueSuperCall{1, 7};
      test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
      changed = graph;
      std::swap(changed.uses[changed.ops[1].first_use.value].operand,
          changed.uses[changed.ops[1].first_use.value + 1].operand);
      test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
      test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
      for (unsigned i = 0; i < count; ++i)
        test::Check(graph.ops[2 * i + 1].life == ValueLife::kLive &&
            std::holds_alternative<ValueSuperSpread>(graph.ops[2 * i + 1].operation));
    }
  }
  // Every encoded guard mode retains its input occurrence and exception kind.
  for (const auto mode : {0U, 1U, 2U}) {
    const auto wire = static_cast<std::uint16_t>(abc::Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8);
    const std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
        static_cast<std::uint8_t>(wire), static_cast<std::uint8_t>(wire >> 8),
        static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)};
    const auto body = test::Body(bytes);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {false, false, false, false}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 0));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, 0, 0));
    auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    const auto& op = graph.ops[1];
    test::Check(!VerifyImportedValues(graph, input, proof, frame));
    test::Check(std::get<ValueSuperCheck>(op.operation).kind == std::get<SuperCheck>(input.instructions[1].operation).kind);
    test::Check(!op.result && op.use_count == 1 && !CanErasePureOp(op));
    test::Check(op.exceptional == (mode < 2 ? ExceptionalCompletion::kMayThrow : ExceptionalCompletion::kImpossible));
    const auto& use = graph.uses[op.first_use.value];
    test::Check(use.role == OperandRole::kGuardValue && use.purpose == ReadPurpose::kProtocol);
    auto changed = graph;
    std::get<ValueSuperCheck>(changed.ops[1].operation).kind = mode == 0 ?
        SuperCheckKind::kRequireUninitialized : SuperCheckKind::kRequireInitialized;
    test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
  }
  // Range super arguments are all arguments: the parent/newTarget are implicit
  // invocation inputs, never a fabricated first constructor operand.
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool wide : {false, true}) for (const unsigned count : {0U, 1U, 200U, 201U, 402U}) {
      if (!wide && count > UINT8_MAX) continue;
      std::vector<std::uint8_t> bytes;
      const auto opcode = wide ? abc::Opcode::kWIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8 :
          abc::Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8;
      const auto wire = static_cast<std::uint16_t>(opcode);
      bytes.push_back(static_cast<std::uint8_t>(wire));
      if (wide) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
      if (!wide) bytes.push_back(7); // IC slot, not an argument.
      bytes.push_back(static_cast<std::uint8_t>(count));
      if (wide) bytes.push_back(static_cast<std::uint8_t>(count >> 8));
      bytes.push_back(1);
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
      const auto body = test::Body(bytes);
      const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, count + 2, {false, false, false, false}));
      const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, frame.registers));
      const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
      auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
      test::Check(!VerifyImportedValues(graph, input, proof, frame));
      const auto& operation = graph.ops[0];
      const auto& call = std::get<ValueSuperCall>(operation.operation);
      test::Check(call.arguments == count && call.cache_slot == (wide ? std::nullopt : std::optional<std::uint16_t>{7}));
      test::Check(operation.use_count == count && operation.result && operation.exceptional == ExceptionalCompletion::kMayThrow);
      test::Check(operation.normal == input.instructions[0].effects.normal &&
          operation.before_throw == input.instructions[0].effects.before_throw);
      for (unsigned i = 0; i < count; ++i) {
        const auto& use = graph.uses[operation.first_use.value + i];
        test::Check(use.role == OperandRole::kArgument);
      }
      auto changed = graph; ++std::get<ValueSuperCall>(changed.ops[0].operation).arguments;
      test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
      changed = graph; changed.ops[0].operation = ValueConstruct{count};
      test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
      test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
      test::Check(std::holds_alternative<ValueSuperCall>(graph.ops[0].operation));
    }
  }
  // Accessor definition is an effectful five-input operation, not assignment.
  // Repeated register occurrences remain independently owned SSA uses.
  for (const auto profile : {abc::ProfileId::kDynamic1202,
      abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool shared : {false, true}) {
      for (const auto count : {1U, 200U, 201U, 402U}) {
        std::vector<std::uint8_t> bytes;
        for (std::uint8_t reg = 0; reg < 4; ++reg) {
          bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED));
          bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kSTA_V8)); bytes.push_back(reg);
        }
        constexpr unsigned kPrefix = 8, kStride = 2;
        for (unsigned n = 0; n < count; ++n) {
          bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDFALSE));
          bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8));
          for (std::uint8_t reg = 0; reg < 4; ++reg) bytes.push_back(shared ? 0 : reg);
        }
        bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
        const auto body = test::Body(bytes);
        const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 4));
        const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 4, 0));
        const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(4, 0, {false, false, false, false}));
        auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
        test::Check(!VerifyImportedValues(graph, method, proof, frame));
        const OperandRole roles[] = {OperandRole::kBase, OperandRole::kKey, OperandRole::kGetter,
          OperandRole::kSetter, OperandRole::kNameFlag};
        for (unsigned n = 0; n < count; ++n) {
          const auto at = kPrefix + n * kStride + 1;
          const auto& op = graph.ops[at];
          test::Check(std::holds_alternative<ValueDefineAccessor>(op.operation));
          test::Check(op.result && op.use_count == std::size(roles) && !CanErasePureOp(op));
          test::Check(op.normal == method.instructions[at].effects.normal &&
              op.before_throw == method.instructions[at].effects.before_throw &&
              op.exceptional == ExceptionalCompletion::kMayThrow);
          for (unsigned u = 0; u < std::size(roles); ++u) {
            auto& use = graph.uses[op.first_use.value + u];
            test::Check(use.role == roles[u]);
            test::Check(use.purpose == (roles[u] == OperandRole::kNameFlag ? ReadPurpose::kProtocol : ReadPurpose::kOperand));
          }
          auto stripped = op; stripped.normal = stripped.before_throw = LanguageEffect::kNone;
          stripped.exceptional = ExceptionalCompletion::kImpossible;
          test::Check(!CanErasePureOp(stripped));
        }
        auto& use = graph.uses[graph.ops[kPrefix + 1].first_use.value + 2];
        use.role = OperandRole::kSetter;
        test::Check(VerifyImportedValues(graph, method, proof, frame).has_value());
        use.role = OperandRole::kGetter;
        test::Check(!VerifyImportedValues(graph, method, proof, frame));
        test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
        for (unsigned n = 0; n < count; ++n)
          test::Check(graph.ops[kPrefix + n * kStride + 1].life == ValueLife::kLive);
      }
    }
  }
  // Class creation retains occurrence identity and the VM heritage protocol.
  // Both IC widths and every supported dynamic profile share this lowering.
  for (const auto profile : {abc::ProfileId::kDynamic1202,
      abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool wide : {false, true}) {
      for (const auto parent : {abc::Opcode::kLDHOLE, abc::Opcode::kLDNULL,
          abc::Opcode::kLDUNDEFINED}) {
        for (const auto count : {1U, 200U, 201U, 402U}) {
          constexpr unsigned kPrefix = 2;
          constexpr std::uint16_t kConstructor = 123, kLiteral = 456, kLength = 300;
          const std::uint16_t cache = wide ? 1000 : 10;
          std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(parent),
              static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0};
          const auto word = [&](std::uint16_t value) {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
          };
          for (unsigned n = 0; n < count; ++n) {
            bytes.push_back(static_cast<std::uint8_t>(wide ?
                abc::Opcode::kDEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8 :
                abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8));
            if (wide) word(cache); else bytes.push_back(static_cast<std::uint8_t>(cache));
            word(kConstructor); word(kLiteral); word(kLength); bytes.push_back(0);
          }
          bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
          const auto body = test::Body(bytes);
          const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 1));
          const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 1, 0));
          const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {false, false, false, false}));
          auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
          test::Check(!VerifyImportedValues(graph, method, proof, frame));
          for (unsigned n = 0; n < count; ++n) {
            const auto& op = graph.ops[kPrefix + n];
            const auto& creation = std::get<ValueClassCreation>(op.operation);
            test::Check(creation.constructor.value == kConstructor && creation.literal.value == kLiteral &&
                creation.length == kLength && creation.cache_slot == cache);
            test::Check(op.result && op.use_count == 1 && !CanErasePureOp(op));
            if (n) test::Check(op.result != graph.ops[kPrefix + n - 1].result);
            auto stripped = op;
            stripped.normal = stripped.before_throw = LanguageEffect::kNone;
            stripped.exceptional = ExceptionalCompletion::kImpossible;
            test::Check(!CanErasePureOp(stripped));
            const auto& use = graph.uses[op.first_use.value];
            test::Check(use.role == OperandRole::kHeritage && use.purpose == ReadPurpose::kProtocol);
            test::Check(op.normal == method.instructions[kPrefix + n].effects.normal &&
                op.before_throw == method.instructions[kPrefix + n].effects.before_throw &&
                op.exceptional == ExceptionalCompletion::kMayThrow);
          }
          auto& first = std::get<ValueClassCreation>(graph.ops[kPrefix].operation);
          for (unsigned field = 0; field < 5; ++field) {
            const auto saved = first;
            switch (field) {
              case 0: ++first.constructor.value; break;
              case 1: ++first.literal.value; break;
              case 2: ++first.length; break;
              case 3: ++first.cache_slot; break;
              case 4: first.kind = abc::ClassKind::kSendable; break;
            }
            test::Check(VerifyImportedValues(graph, method, proof, frame)->kind == ValueImportError::kInputMismatch);
            first = saved;
          }
          auto& use = graph.uses[graph.ops[kPrefix].first_use.value];
          use.purpose = ReadPurpose::kOperand;
          test::Check(VerifyImportedValues(graph, method, proof, frame).has_value());
          use.purpose = ReadPurpose::kProtocol;
          test::Check(!VerifyImportedValues(graph, method, proof, frame));
          test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
          for (unsigned n = 0; n < count; ++n)
            test::Check(graph.ops[kPrefix + n].life == ValueLife::kLive);
          auto heritages = std::get<ValueHeritages>(AnalyzeValueHeritage(graph));
          test::Check(heritages.revision == graph.revision && heritages.classes.size() == count);
          for (unsigned n = 0; n < count; ++n) {
            const auto& heritage = heritages.classes[n];
            test::Check(heritage.operation == OpId{kPrefix + n} &&
                heritage.input == graph.ops[kPrefix + n].first_use);
            test::Check(heritage.kind == (parent == abc::Opcode::kLDHOLE ? HeritageKind::kAbsent :
                parent == abc::Opcode::kLDNULL ? HeritageKind::kNull : HeritageKind::kUnknown));
          }
          test::Check(std::get<ValueHeritageError>(AnalyzeValueHeritage(graph, {0})) ==
              ValueHeritageError::kResourceLimit);
          test::Check(std::get<ValueHeritageError>(AnalyzeValueHeritage(graph, {UINT32_MAX, 0})) ==
              ValueHeritageError::kWorkLimit);
          auto& heritage_use = graph.uses[graph.ops[kPrefix].first_use.value];
          const auto old_role = heritage_use.role;
          heritage_use.role = OperandRole::kInput;
          test::Check(std::get<ValueHeritageError>(AnalyzeValueHeritage(graph)) == ValueHeritageError::kInput);
          heritage_use.role = old_role;
          const auto id = std::get<ValueId>(heritage_use.operand);
          const auto old_domain = graph.values[id.value].domain;
          graph.values[id.value].domain = ValueKinds::Language().Join(ValueKinds::Hole());
          test::Check(std::get<ValueHeritageError>(AnalyzeValueHeritage(graph)) == ValueHeritageError::kDomain);
          graph.values[id.value].domain = old_domain;
        }
      }
    }
  }
  // Own indexed definitions preserve acc and IC absence. They must neither
  // become [[Set]] nor disappear when their destination is otherwise unused.
  for (const auto profile : {abc::ProfileId::kDynamic1202,
      abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const auto encoding : {abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16,
        abc::Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16, abc::Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32}) {
    const bool wide = encoding == abc::Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32;
    const bool wide_cache = encoding == abc::Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16;
    for (const auto count : {1U, 200U, 201U, 402U}) {
      constexpr unsigned kPrefixOperations = 3;
      std::vector<std::uint8_t> bytes{
          static_cast<std::uint8_t>(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0,
          static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
          static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0};
      for (unsigned n = 0; n < count; ++n) {
        const auto opcode = static_cast<std::uint16_t>(encoding);
        bytes.push_back(static_cast<std::uint8_t>(opcode));
        if (wide) bytes.push_back(static_cast<std::uint8_t>(opcode >> 8));
        else { bytes.push_back(0); if (wide_cache) bytes.push_back(0); }
        bytes.push_back(0); // receiver register
        const std::uint32_t index = wide ? UINT32_MAX - n : n;
        const auto width = wide ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        for (unsigned byte = 0; byte < width; ++byte)
          bytes.push_back(static_cast<std::uint8_t>(index >> (byte * 8)));
      }
      bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
      const auto body = test::Body(bytes);
      const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 1));
      const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 1, 0));
      const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {false, false, false, false}));
      auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
      test::Check(!VerifyImportedValues(graph, method, proof, frame));
      for (unsigned n = 0; n < count; ++n) {
        const auto& op = graph.ops[kPrefixOperations + n];
        const auto& write = std::get<ValueIndexedWrite>(op.operation);
        test::Check(write.index == (wide ? UINT32_MAX - n : n) &&
            (wide ? !write.cache_slot : write.cache_slot == 0));
        test::Check(!op.result && op.use_count == 2 && !CanErasePureOp(op));
        test::Check(graph.uses[op.first_use.value].role == OperandRole::kBase);
        test::Check(graph.uses[op.first_use.value + 1].role == OperandRole::kStoredValue);
        test::Check(op.normal == method.instructions[kPrefixOperations + n].effects.normal);
        test::Check(op.exceptional == method.instructions[kPrefixOperations + n].effects.exceptional);
      }
      auto& first = std::get<ValueIndexedWrite>(graph.ops[kPrefixOperations].operation);
      first.cache_slot = wide ? std::optional<std::uint16_t>{0} : std::nullopt;
      test::Check(VerifyImportedValues(graph, method, proof, frame)->kind == ValueImportError::kInputMismatch);
      first.cache_slot = wide ? std::nullopt : std::optional<std::uint16_t>{0};
      first.index ^= 1;
      test::Check(VerifyImportedValues(graph, method, proof, frame)->kind == ValueImportError::kInputMismatch);
      first.index ^= 1;
      test::Check(!VerifyImportedValues(graph, method, proof, frame));
      graph.ops[kPrefixOperations].operation = ValuePropertyWrite{};
      test::Check(VerifyImportedValues(graph, method, proof, frame).has_value());
    }
  }
  }
  {
    const auto code = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDSYMBOL),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    for (const auto profile : {abc::ProfileId::kDynamic1202,
        abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
      const auto built = BuildSemanticMethod(code, profile, 0);
      test::Check(std::holds_alternative<SemanticMethod>(built));
      const auto& method = std::get<SemanticMethod>(built);
      test::Check(std::holds_alternative<SymbolConstructorLoad>(method.instructions.front().operation));
      const auto checked = VerifySemanticSource(code, method, 0, 0);
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 0, {false, false, false, false}));
      const auto imported = ImportNormalValues(code, method, std::get<SemanticProof>(checked), frame);
      test::Check(std::holds_alternative<ValueProgram>(imported));
      auto graph = std::get<ValueProgram>(imported);
      const auto& load = graph.ops.front();
      test::Check(std::holds_alternative<ValueSymbolConstructor>(load.operation));
      test::Check(load.use_count == 0 && load.result);
      test::Check(load.normal == LanguageEffect::kNone &&
          load.exceptional == ExceptionalCompletion::kImpossible);
      test::Check(graph.values[load.result->value].domain == ValueKinds::Defined());
      test::Check(!VerifyImportedValues(graph, method, std::get<SemanticProof>(checked), frame));
      graph.phases.push_back({{0}, {0}, ExceptionPhase::kBefore, OpId{0}});
      test::Check(VerifyImportedValues(graph, method, std::get<SemanticProof>(checked), frame)->kind ==
          ValueImportError::kExceptionFlow);
      graph.phases.clear();
      test::Check(!CanErasePureOp(load));
      graph.ops.front().operation = ValueGlobalObject{};
      test::Check(VerifyImportedValues(graph, method, std::get<SemanticProof>(checked), frame)->kind ==
          ValueImportError::kInputMismatch);
      auto malformed = code.instructions.front();
      malformed.decoded.operand_count = 1;
      test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, 0)).kind ==
          SemanticError::kMalformedInstruction);
    }
  }
  {
    const auto code = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDGLOBAL),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    for (const auto profile : {abc::ProfileId::kDynamic1202,
        abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
      const auto built = BuildSemanticMethod(code, profile, 0);
      const auto& method = std::get<SemanticMethod>(built);
      test::Check(std::holds_alternative<GlobalObjectLoad>(method.instructions.front().operation));
      const auto checked = VerifySemanticSource(code, method, 0, 0);
      const auto& proof = std::get<SemanticProof>(checked);
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 0, {false, false, false, false}));
      const auto imported = ImportNormalValues(code, method, proof, frame);
      test::Check(std::holds_alternative<ValueProgram>(imported));
      auto graph = std::get<ValueProgram>(imported);
      const auto& load = graph.ops.front();
      test::Check(std::holds_alternative<ValueGlobalObject>(load.operation));
      test::Check(load.use_count == 0 && load.result);
      test::Check(load.normal == LanguageEffect::kNone &&
          load.exceptional == ExceptionalCompletion::kImpossible);
      test::Check(graph.values[load.result->value].domain == ValueKinds::Defined());
      test::Check(!VerifyImportedValues(graph, method, proof, frame));
      test::Check(!CanErasePureOp(load));
      graph.ops.front().operation = ValueFrameFunction{};
      test::Check(VerifyImportedValues(graph, method, proof, frame)->kind ==
          ValueImportError::kInputMismatch);
      auto malformed = code.instructions.front();
      malformed.decoded.operand_count = 1;
      test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, 0)).kind ==
          SemanticError::kMalformedInstruction);
    }
  }
  {
    const auto code = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDFUNCTION),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    for (const auto profile : {abc::ProfileId::kDynamic1202,
        abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
      const auto built = BuildSemanticMethod(code, profile, 0);
      test::Check(std::holds_alternative<SemanticMethod>(built));
      const auto& method = std::get<SemanticMethod>(built);
      const auto& load = method.instructions.front();
      test::Check(std::holds_alternative<FrameFunctionLoad>(load.operation));
      test::Check(load.effects.normal_registers.read_count == 0 &&
          load.effects.normal_registers.write &&
          std::holds_alternative<Accumulator>(*load.effects.normal_registers.write));
      test::Check(load.effects.normal == LanguageEffect::kNone &&
          load.effects.exceptional == ExceptionalCompletion::kImpossible);
      const auto checked = VerifySemanticSource(code, method, 0, 0);
      test::Check(std::holds_alternative<SemanticProof>(checked));
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 0, {false, false, false, false}));
      const auto imported = ImportNormalValues(code, method, std::get<SemanticProof>(checked), frame);
      test::Check(std::holds_alternative<ValueProgram>(imported));
      auto graph = std::get<ValueProgram>(imported);
      test::Check(std::holds_alternative<ValueFrameFunction>(graph.ops.front().operation));
      test::Check(graph.ops.front().use_count == 0 && graph.ops.front().result);
      test::Check(graph.values[graph.ops.front().result->value].domain == ValueKinds::Defined());
      test::Check(!VerifyImportedValues(graph, method, std::get<SemanticProof>(checked), frame));
      test::Check(!CanErasePureOp(graph.ops.front()));
      graph.ops.front().operation = ValueFrameThis{};
      test::Check(VerifyImportedValues(graph, method, std::get<SemanticProof>(checked), frame)->kind ==
          ValueImportError::kInputMismatch);
      auto malformed = code.instructions.front();
      malformed.decoded.operand_count = 1;
      test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, 0)).kind ==
          SemanticError::kMalformedInstruction);

      // Mutating the ABI function copy must not change the independent frame
      // function or make it depend on an undefined register in the SSA graph.
      const auto abi = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 1, {.function_slot = true}));
      test::Check(abi.function.has_value());
      const auto overwritten = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDNULL),
          static_cast<std::uint8_t>(abc::Opcode::kSTA_V8),
          static_cast<std::uint8_t>(abi.function->value),
          static_cast<std::uint8_t>(abc::Opcode::kLDFUNCTION),
          static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
      const auto lifted = BuildSemanticMethod(overwritten, profile, abi.registers);
      const auto& input = std::get<SemanticMethod>(lifted);
      const auto validated = VerifySemanticSource(overwritten, input, abi.registers, abi.locals);
      const auto values = ImportNormalValues(overwritten, input, std::get<SemanticProof>(validated), abi);
      const auto& independent = std::get<ValueProgram>(values);
      test::Check(std::holds_alternative<ValueFrameFunction>(independent.ops[2].operation));
      test::Check(independent.ops[2].use_count == 0 && independent.ops[2].result);
      test::Check(independent.values[independent.ops[2].result->value].domain == ValueKinds::Defined());
      test::Check(!VerifyImportedValues(independent, input, std::get<SemanticProof>(validated), abi));
    }
  }
  {
    const auto code = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDTHIS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    const auto built = BuildSemanticMethod(code, abc::ProfileId::kDynamic1206, 0);
    test::Check(std::holds_alternative<SemanticMethod>(built));
    const auto& method = std::get<SemanticMethod>(built);
    const auto& load = method.instructions.front();
    test::Check(std::holds_alternative<FrameThisLoad>(load.operation));
    test::Check(load.effects.normal_registers.read_count == 0 &&
        load.effects.normal_registers.write &&
        std::holds_alternative<Accumulator>(*load.effects.normal_registers.write));
    test::Check(load.effects.normal == LanguageEffect::kNone &&
        load.effects.exceptional == ExceptionalCompletion::kImpossible);
    const auto proof = VerifySemanticSource(code, method, 0, 0);
    test::Check(std::get<SemanticProofFailure>(proof).kind == SemanticProofError::kInternalValue);
    test::Check(std::get<SemanticProofFailure>(proof).pc == abc::CodeOffset{1});
    const auto initialized = VerifySemanticSource(code, method, 0, 0, {},
        CapturedValues::kReject, {}, ClosureWrites::kUnknown, {},
        CallResultValues::kUnknown, FrameReceiver::kInitialized);
    const auto& known = std::get<SemanticProof>(initialized);
    test::Check(known.receiver == FrameReceiver::kInitialized);
    const auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 0, {false, false, false, false}));
    const auto imported = ImportNormalValues(code, method, known, frame);
    const auto& graph = std::get<ValueProgram>(imported);
    test::Check(std::holds_alternative<ValueFrameThis>(graph.ops[0].operation));
    test::Check(graph.ops[0].use_count == 0 && graph.ops[0].result.has_value());
    test::Check(!VerifyImportedValues(graph, method, known, frame));
    auto malformed = code.instructions.front();
    malformed.decoded.operand_count = 1;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        malformed, abc::ProfileId::kDynamic1206, 0)).kind == SemanticError::kMalformedInstruction);
    // Overwriting the ABI receiver copy with a legal JS value does not prove
    // that the independent VM frame receiver has been initialized by super.
    const auto overwritten = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDNULL),
        static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 2,
        static_cast<std::uint8_t>(abc::Opcode::kLDTHIS),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    constexpr std::uint32_t kImplicitSlots = 3;
    for (const auto profile : {abc::ProfileId::kDynamic1202,
        abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
      const auto lifted = BuildSemanticMethod(overwritten, profile, kImplicitSlots);
      const auto& input = std::get<SemanticMethod>(lifted);
      test::Check(std::holds_alternative<FrameThisLoad>(input.instructions[2].operation));
      test::Check(input.instructions[2].effects.normal_registers.read_count == 0);
      const auto checked = VerifySemanticSource(overwritten, input, kImplicitSlots, 0);
      test::Check(std::get<SemanticProofFailure>(checked).kind == SemanticProofError::kInternalValue);
      test::Check(std::get<SemanticProofFailure>(checked).pc == abc::CodeOffset{4});
    }
  }
  {
    const auto code = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDTRUE),
        static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER),
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    const auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 0, {false, false, false, false}));
    const auto built = BuildSemanticMethod(code, abc::ProfileId::kDynamic1206, 0);
    test::Check(std::holds_alternative<SemanticMethod>(built));
    const auto& method = std::get<SemanticMethod>(built);
    const auto& debug = method.instructions[1];
    test::Check(std::holds_alternative<DebuggerOperation>(debug.operation));
    test::Check(debug.pc == abc::CodeOffset{1});
    test::Check(debug.effects.normal == LanguageEffect::kDebugger &&
        debug.effects.before_throw == LanguageEffect::kNone &&
        debug.effects.exceptional == ExceptionalCompletion::kImpossible);
    test::Check(debug.effects.normal_registers.read_count == 0 &&
        !debug.effects.normal_registers.write);
    const auto checked = VerifySemanticSource(code, method, 0, 0);
    const auto& proof = std::get<SemanticProof>(checked);
    auto imported = ImportNormalValues(code, method, proof, frame);
    auto& graph = std::get<ValueProgram>(imported);
    test::Check(std::holds_alternative<ValueDebugger>(graph.ops[1].operation));
    test::Check(!graph.ops[1].result && graph.ops[1].use_count == 0);
    test::Check(!VerifyImportedValues(graph, method, proof, frame));
    auto stripped = graph.ops[1];
    stripped.normal = LanguageEffect::kNone;
    test::Check(!CanErasePureOp(stripped));
    auto malformed = code.instructions[1];
    malformed.decoded.operand_count = 1;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(
        malformed, abc::ProfileId::kDynamic1206, 0)).kind == SemanticError::kMalformedInstruction);
  }
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
    test::Check(std::get<ValueId>(stored.operand) ==
        std::get<ValueId>(value.uses[value.ops[kReturnIndex].first_use.value].operand));
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
    // Primitive copy propagation retains both uses as the same JS undefined,
    // without requiring the old accumulator SSA copy to survive.
    test::Check(std::get<SpecialValue>(changed.uses[retained.first_use.value].operand) == SpecialValue::kUndefined);
    test::Check(std::get<SpecialValue>(changed.uses[changed.ops[kReturnIndex].first_use.value].operand) ==
        SpecialValue::kUndefined);
    test::Check(!VerifyValueUses(changed, changed.uses.size()));
  }
  for (const bool wide : {false, true}) {
    const auto opcode = wide ? abc::Opcode::kSTOBJBYVALUE_IMM16_V8_V8 :
        abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8;
    constexpr std::uint8_t kObject = 0, kKey = 1, kCache = 0;
    std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED),
        static_cast<std::uint8_t>(opcode), kCache};
    if (wide) bytes.push_back(kCache);
    bytes.insert(bytes.end(), {kObject, kKey, returned});
    const auto body = test::Body(bytes);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(
        body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(
        body, input, frame.registers, frame.locals));
    const auto imported = ImportNormalValues(body, input, proof, frame);
    test::Check(std::holds_alternative<ValueProgram>(imported));
    const auto& value = std::get<ValueProgram>(imported);
    test::Check(!VerifyImportedValues(value, input, proof, frame));
    const auto& store = value.ops[1];
    test::Check(!store.result && store.use_count == 3);
    test::Check(value.uses[store.first_use.value].role == OperandRole::kBase);
    test::Check(value.uses[store.first_use.value + 1].role == OperandRole::kKey);
    test::Check(value.uses[store.first_use.value + 2].role == OperandRole::kStoredValue);
    test::Check(store.exceptional == ExceptionalCompletion::kMayThrow);
    test::Check(!CanErasePureOp(store));
    test::Check(std::holds_alternative<ValuePropertyWrite>(store.operation));
    test::Check(value.origins[store.origin.value].pc == abc::CodeOffset{1});
    test::Check(store.normal == input.instructions[1].effects.normal &&
        store.before_throw == input.instructions[1].effects.before_throw);
    test::Check(std::get<ValueId>(value.uses[store.first_use.value + 2].operand) ==
        std::get<ValueId>(value.uses[value.ops[2].first_use.value].operand));
    auto changed = value;
    changed.uses[store.first_use.value + 1].role = OperandRole::kStoredValue;
    test::Check(VerifyImportedValues(changed, input, proof, frame).has_value());
    auto stripped = store;
    stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(stripped));
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
  for (const bool wide : {false, true}) {
    constexpr std::uint8_t kCacheLow = 0x34, kCacheHigh = 0x12;
    constexpr std::uint8_t kLiteralLow = 0x78, kLiteralHigh = 0x56;
    constexpr abc::PoolIndex kLiteral{0x5678};
    const auto opcode = wide ? abc::Opcode::kCREATEARRAYWITHBUFFER_IMM16_ID16 :
        abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(opcode), kCacheLow};
    if (wide) bytes.push_back(kCacheHigh);
    bytes.insert(bytes.end(), {kLiteralLow, kLiteralHigh, returned});
    const auto body = test::Body(bytes);
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto value = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    test::Check(!VerifyImportedValues(value, input, proof, frame));
    test::Check(std::get<ValueArrayAllocation>(value.ops[0].operation).literal == kLiteral);
    test::Check(value.ops[0].use_count == 0 && value.ops[0].result &&
        value.ops[0].normal == LanguageEffect::kAllocation);
    auto changed = value;
    std::get<ValueArrayAllocation>(changed.ops[0].operation).literal.reset();
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = value;
    ++std::get<ValueArrayAllocation>(changed.ops[0].operation).literal->value;
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
    test::Check(!changed.exceptional_exits.empty());
    changed.exceptional_exits.front().handler = ValueHandler{{{0}, {0}, {0}}, {0}};
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
    changed = graph;
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
