#include "abcd/analysis/register_value_flow.h"
#include "abcd/analysis/semantic_validation.h"

#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace analysis;
using namespace abcd::test;
namespace abcd::test {
void VerifyValueFlow();
void VerifyValueScaling();
}  // namespace abcd::test
int main() {
  {
    // Pinned v7 ADD2 feeds its double result directly to JSTaggedValue. The
    // accepted negative signaling NaN 0xfff7000000000005 can quiet to
    // 0xffff000000000005; adding the tag offset then produces internal Hole.
    // Neither a legal input category nor addition by zero proves safe output.
    const auto input = Body({0x63, 5, 0, 0, 0, 0, 0, 0xf7, 0xff,
        0x61, 0, 0x62, 0, 0, 0, 0,
        static_cast<std::uint8_t>(abc::Opcode::kADD2_IMM8_V8), 0, 0, 0x64});
    const auto method = std::get<SemanticMethod>(BuildSemanticMethod(input, abc::ProfileId::kDynamic1206, 1));
    IdfQuota work{UINT64_MAX};
    const auto facts = std::get<RegisterValueFlow>(RegisterValueFlow::Build(input, method.ssa, 1, {}, work));
    Check(facts.At(*method.ssa.Write({0})) == ValueKinds::Number());
    Check(facts.At(*method.ssa.Write({3})) == ValueKinds::Unknown());
    const auto proof = VerifySemanticSource(input, method, 1, 0);
    const auto* failure = std::get_if<SemanticProofFailure>(&proof);
    Check(failure && failure->kind == SemanticProofError::kInternalValue && failure->pc == abc::CodeOffset{19});
  }
  const auto body = Body({0x70, 0x61, 0, 0x60, 0, 0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{100000};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 1, quota));
  const auto flow = std::get<RegisterValueFlow>(
      RegisterValueFlow::Build(body, ssa, 1, {}, quota));
  for (std::uint32_t i = 0; i < 3; ++i)
    Check(flow.At(*ssa.Write({i})) == ValueKinds::Hole());
  for (const auto comparison : {abc::Opcode::kEQ_IMM8_V8, abc::Opcode::kNOTEQ_IMM8_V8,
      abc::Opcode::kSTRICTEQ_IMM8_V8, abc::Opcode::kSTRICTNOTEQ_IMM8_V8}) {
    const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
    for (const bool environment : {false, true}) {
      std::vector<std::uint8_t> bytes;
      if (environment) bytes = {op(abc::Opcode::kNEWLEXENV_IMM8), 1};
      else bytes = {op(abc::Opcode::kLDHOLE)};
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0,
          op(abc::Opcode::kLDUNDEFINED), op(comparison), 0, 0, op(abc::Opcode::kRETURN)});
      const auto input = Body(bytes);
      IdfQuota work{UINT64_MAX};
      const auto renamed = std::get<RegisterSsa>(RegisterSsa::Build(
          input, std::get<NormalFlow>(BuildNormalFlow(input)), 1, work));
      const auto kinds = std::get<RegisterValueFlow>(RegisterValueFlow::Build(input, renamed, 1, {}, work));
      Check(kinds.At(*renamed.Write({0})) == (environment ? ValueKinds::Environment() : ValueKinds::Hole()));
      Check(kinds.At(*renamed.Write({3})) == (environment ? ValueKinds::Unknown() : ValueKinds::Language()));
    }
  }
  for (const auto bytes :
       {std::initializer_list<std::uint8_t>{0x63, 5, 0, 0, 0, 0, 0, 0xff, 0xff,
                                            0x64},
        {0x63, 5, 0, 0, 0, 0, 0, 0xff, 0x7f, 0x1f, 0, 0x64}}) {
    const auto input = Body(bytes);
    const auto named = std::get<RegisterSsa>(RegisterSsa::Build(
        input, std::get<NormalFlow>(BuildNormalFlow(input)), 0, quota));
    const auto types = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(input, named, 0, {}, quota));
    const InstructionId last{bytes.size() == 10 ? 0U : 1U};
    Check(types.At(*named.Write(last)) == ValueKinds::Unknown());
    if (last.value == 1)
      Check(types.At(*named.Write({0})) == ValueKinds::Number());
  }
  const auto analyze = [](const abc::MethodBody& input) {
    IdfQuota work{UINT64_MAX};
    const auto named = std::get<RegisterSsa>(RegisterSsa::Build(
        input, std::get<NormalFlow>(BuildNormalFlow(input)), 4, work));
    return std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(input, named, 4, {}, work));
  };
  struct Case {
    std::initializer_list<std::uint8_t> code;
    ValueKinds kind;
  };
  for (const auto& item :
       {Case{{0x09, 1, 0x61, 0, 0x60, 0, 0x64}, ValueKinds::Environment()},
        Case{{0x3c, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0xfc, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0x29, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0x42, 0, 0, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0x3a, 0, 0, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0x37, 0, 0, 0x64}, ValueKinds::Unknown()},
        Case{{0x33, 0, 0, 0, 0, 0x64}, ValueKinds::Language()},
        Case{{0x34, 0, 0, 0, 0, 0x64}, ValueKinds::Language()},
        Case{{0x3e, 0, 0, 0x64}, ValueKinds::String()},
        Case{{0x3f, 0, 0, 0, 0x64}, ValueKinds::Language()},
        Case{{0xad, 0x64}, ValueKinds::Language()},
        Case{{0x6d, 0x64}, ValueKinds::Language()},
        Case{{0x6c, 0x64}, ValueKinds::Language()},
        Case{{0xae, 0x64}, ValueKinds::Language()},
        Case{{0x8c, 0, 0, 0, 0, 0x64}, ValueKinds::Language()},
        Case{{0, 0x23, 0x64}, ValueKinds::Language()},
        Case{{0, 0x61, 0, 0x28, 0, 0, 0x64}, ValueKinds::Language()},
        Case{{0x70, 0xfe, 9, 0, 0, 0x64}, ValueKinds::Hole()}}) {
    const auto input = Body(item.code);
    IdfQuota work{UINT64_MAX};
    const auto named = std::get<RegisterSsa>(RegisterSsa::Build(
        input, std::get<NormalFlow>(BuildNormalFlow(input)), 4, work));
    const auto kinds = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(input, named, 4, {}, work));
    for (std::uint32_t i = 0; i < input.instructions.size(); ++i)
      if (const auto value = named.Write({i})) {
        if (kinds.At(*value) != item.kind)
          std::fprintf(
              stderr, "mismatch i=%u opcode=%u\n", i,
              static_cast<unsigned>(input.instructions[i].decoded.opcode));
        Check(kinds.At(*value) == item.kind);
      }
  }
  const auto cyclic = Body({0x45, 0, 0, 0x4d, 0xfd});
  const auto cyclic_ssa = std::get<RegisterSsa>(RegisterSsa::Build(
      cyclic, std::get<NormalFlow>(BuildNormalFlow(cyclic)), 1, quota));
  Check(std::holds_alternative<MethodEntry>(cyclic_ssa.Values()[0].origin));
  const std::array atoms{ValueKinds::Language(), ValueKinds::Hole(),
                         ValueKinds::Environment(),
                         ValueKinds::OtherInternal()};
  for (std::uint32_t mask = 1; mask < (1U << kValueKindCount); ++mask) {
    ValueKinds kinds;
    for (std::size_t bit = 0; bit < atoms.size(); ++bit)
      if (mask & (1U << bit)) kinds = kinds.Join(atoms[bit]);
    const std::array<EntryValueFact, 1> entry{{{{0}, kinds}}};
    const auto inferred = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(cyclic, cyclic_ssa, 1, entry, quota));
    for (const auto fact : inferred.Facts()) Check(fact == kinds);
    Check(kinds.IsLanguage() == (mask == 1));
  }
  Check(!ValueKinds{}.IsLanguage() &&
        !ValueKinds{}.Contains(VmValueKind::kHole));
  Check(ValueKinds::Unknown().Contains(VmValueKind::kOtherInternal));
  const std::array<EntryValueFact, 2> duplicate{
      {{{0}, ValueKinds::Language()}, {{0}, ValueKinds::Hole()}}};
  Check(std::get<ValueFlowFailure>(
            RegisterValueFlow::Build(cyclic, cyclic_ssa, 1, duplicate, quota))
            .kind == ValueFlowError::kDuplicateEntryFact);
  for (const EntryValueFact entry :
       {EntryValueFact{{UINT32_MAX}, ValueKinds::Language()},
        {{0}, {}},
        {*cyclic_ssa.Write({0}), ValueKinds::Language()}}) {
    Check(std::get<ValueFlowFailure>(
              RegisterValueFlow::Build(cyclic, cyclic_ssa, 1,
                                       std::span(&entry, 1), quota))
              .kind == ValueFlowError::kInvalidEntryFact);
  }
  auto owned = analyze(Body({0x70, 0x64}));
  auto moved = std::move(owned);
  Check(owned.Facts().empty() && owned.AllocatedBytes() == 0 && !owned.At({0}));
  owned = analyze(Body({0, 0x64}));
  owned = std::move(moved);
  Check(!moved.At({0}) && !owned.At({UINT32_MAX}));
  Check(owned.Facts().back() == ValueKinds::Hole());
  const auto malformed = RegisterValueFlow::Build({}, ssa, 1, {}, quota);
  Check(std::get<ValueFlowFailure>(malformed).kind ==
        ValueFlowError::kInvalidSsa);
  for (std::uint64_t limit = 0; limit < flow.work.units; ++limit) {
    IdfQuota limited{limit};
    Check(std::get<ValueFlowFailure>(
              RegisterValueFlow::Build(body, ssa, 1, {}, limited))
              .kind == ValueFlowError::kWorkLimit);
    Check(limited.Remaining() == 0);
  }
  IdfQuota exact{flow.work.units};
  Check(std::get<RegisterValueFlow>(
            RegisterValueFlow::Build(body, ssa, 1, {}, exact,
                                     {flow.work.peak_bytes}))
            .AllocatedBytes() == flow.AllocatedBytes());
  Check(exact.Remaining() == 0);
  IdfQuota unlimited{UINT64_MAX};
  Check(std::get<ValueFlowFailure>(
            RegisterValueFlow::Build(body, ssa, 1, {}, unlimited,
                                     {flow.work.peak_bytes - 1}))
            .kind == ValueFlowError::kResourceLimit);
  VerifyValueFlow();
  VerifyValueScaling();
}
