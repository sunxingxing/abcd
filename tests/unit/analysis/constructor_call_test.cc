#include "abcd/analysis/constructor_call.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/source/acyclic_body.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abcd::abc::MethodBody body;
  for (std::size_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abcd::abc::DecodedInstruction>(
        abcd::abc::DecodeInstruction(bytes.subspan(pc),
                                     abcd::abc::ProfileId::kDynamic1301));
    body.instructions.push_back(
        {{static_cast<std::uint32_t>(pc)}, decoded, {}});
    pc += decoded.size;
  }
  return body;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  using namespace analysis;
  struct Case {
    std::vector<std::uint8_t> encoding;
    std::uint16_t count;
    std::optional<std::uint16_t> profile_slot;
    std::string_view source;
  };
  const std::array cases{
      Case{{0x08, 7, 1, 0}, 1, 7, "acc = new (ctor)();\nreturn acc;\n"},
      Case{{0x08, 7, 3, 0}, 3, 7, "acc = new (ctor)(a, b);\nreturn acc;\n"},
      Case{{0x83, 0x34, 0x12, 2, 0}, 2, 0x1234,
           "acc = new (ctor)(a);\nreturn acc;\n"},
      Case{{0xfd, 0x01, 3, 0, 0}, 3, std::nullopt,
           "acc = new (ctor)(a, b);\nreturn acc;\n"}};
  const std::array<std::string_view, 3> names{"ctor", "a", "b"};
  for (std::size_t index = 0; index < cases.size(); ++index) {
    const auto& item = cases[index];
    const auto decoded = Body(item.encoding).instructions.front().decoded;
    const auto call = std::get<ConstructorCall>(
        LiftConstructorCall(decoded, names.size()));
    Check(call.inputs.first.value == 0 && call.inputs.count == item.count);
    Check(call.profile_slot == item.profile_slot);
    Check(call.ArgumentCount() == item.count - 1);
    for (std::uint32_t i = 0; i < call.ArgumentCount(); ++i)
      Check(call.ArgumentAt(i)->value == i + 1);
    Check(!call.ArgumentAt(call.ArgumentCount()));
    const auto effect = std::get<RegisterEffect>(
        LiftRegisterEffect(decoded, names.size()));
    Check(effect.read_count == 0 && effect.range &&
          effect.range->first.value == 0 &&
          effect.range->count == item.count && effect.write &&
          std::holds_alternative<Accumulator>(*effect.write));
    std::vector<std::uint8_t> code = item.encoding;
    code.push_back(0x64);
    const auto body = Body(code);
    std::ostringstream rendered, ordered;
    Check(std::holds_alternative<std::size_t>(
        source::WriteLinearBody(body, {"acc", names}, rendered)));
    Check(std::holds_alternative<std::size_t>(
        source::WriteAcyclicBody(body, {"acc", names}, ordered)));
    Check(rendered.str() == item.source && ordered.str() == rendered.str());
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    Check(std::holds_alternative<AccumulatorFlow>(
        AnalyzeAcyclicAccumulator(body, graph, names.size())));
    IdfQuota work{UINT64_MAX};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, graph, names.size(), work));
    const auto values = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(body, ssa, names.size(), {}, work));
    Check(values.At(*ssa.Write({0})) == ValueKinds::Language());
    if (argc == 2 && std::string_view(argv[1]) == "--emit")
      std::cout << "export function construct" << index
                << "(ctor,a,b){let acc;\n" << rendered.str() << "}\n";
    auto malformed = decoded;
    malformed.operand_count = 0;
    Check(std::get<ConstructorCallError>(
              LiftConstructorCall(malformed, names.size())) ==
          ConstructorCallError::kMalformedInstruction);
    for (std::size_t i = 0; i < decoded.operand_count; ++i) {
      malformed = decoded;
      malformed.operands[i].kind = abc::OperandKind::kString;
      Check(std::get<ConstructorCallError>(
                LiftConstructorCall(malformed, names.size())) ==
            ConstructorCallError::kMalformedInstruction);
    }
    const bool wide = decoded.opcode ==
                      abc::Opcode::kWIDE_NEWOBJRANGE_PREF_IMM16_V8;
    const bool word_cache = decoded.opcode ==
                            abc::Opcode::kNEWOBJRANGE_IMM16_IMM8_V8;
    if (!wide) {
      malformed = decoded;
      malformed.operands[0].raw_bits =
          word_cache ? std::uint64_t{UINT16_MAX} + 1
                     : std::uint64_t{UINT8_MAX} + 1;
      Check(std::get<ConstructorCallError>(
                LiftConstructorCall(malformed, UINT64_MAX)) ==
            ConstructorCallError::kMalformedInstruction);
    }
    malformed = decoded;
    malformed.operands[wide ? 0 : 1].raw_bits =
        wide ? std::uint64_t{UINT16_MAX} + 1
             : std::uint64_t{UINT8_MAX} + 1;
    Check(std::get<ConstructorCallError>(
              LiftConstructorCall(malformed, UINT64_MAX)) ==
          ConstructorCallError::kMalformedInstruction);
    malformed = decoded;
    malformed.operands[wide ? 1 : 2].raw_bits =
        std::uint64_t{UINT8_MAX} + 1;
    Check(std::get<ConstructorCallError>(
              LiftConstructorCall(malformed, UINT64_MAX)) ==
          ConstructorCallError::kMalformedInstruction);
    malformed = decoded;
    const auto count_operand = item.profile_slot ? 1U : 0U;
    malformed.operands[count_operand].raw_bits = 0;
    Check(std::get<ConstructorCallError>(
              LiftConstructorCall(malformed, names.size())) ==
          ConstructorCallError::kEmptyRange);
    Check(std::get<ConstructorCallError>(
              LiftConstructorCall(decoded, item.count - 1)) ==
          ConstructorCallError::kRegisterRange);
  }
  for (const auto count : {200U, 201U, 257U, 400U}) {
    const std::vector<std::uint8_t> code{
        0xfd, 0x01, static_cast<std::uint8_t>(count),
        static_cast<std::uint8_t>(count >> 8), 0, 0x64};
    const auto body = Body(code);
    const auto call = std::get<ConstructorCall>(
        LiftConstructorCall(body.instructions[0].decoded, count));
    Check(call.inputs.count == count &&
          call.ArgumentAt(count - 2)->value == count - 1);
    std::vector<std::string> storage;
    storage.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
      storage.push_back("r" + std::to_string(i));
    const std::vector<std::string_view> names(storage.begin(), storage.end());
    std::ostringstream rendered;
    Check(std::holds_alternative<std::size_t>(
        source::WriteLinearBody(body, {"acc", names}, rendered)));
    Check(rendered.str().starts_with("acc = new (r0)(r1") &&
          rendered.str().ends_with("r" + std::to_string(count - 1) +
                                   ");\nreturn acc;\n"));
  }
  const auto other = Body(std::array<std::uint8_t, 1>{0x64});
  Check(std::get<ConstructorCallError>(LiftConstructorCall(
            other.instructions[0].decoded, 0)) ==
        ConstructorCallError::kUnsupportedOpcode);
  const ConstructorCall invalid{{{0}, 0}, {}};
  Check(invalid.ArgumentCount() == 0 && !invalid.ArgumentAt(0));
}
