#include "abcd/analysis/ordinary_call.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "abcd/source/acyclic_body.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
abcd::abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abcd::abc::MethodBody body;
  for (std::size_t pc = 0; pc < bytes.size();) {
    const auto decoded =
        std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
            bytes.subspan(pc), abcd::abc::ProfileId::kDynamic1206));
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
  const std::vector<std::vector<std::uint8_t>> encodings{
      {0x29, 0xff},          {0x2a, 0xff, 1},    {0x2b, 0xff, 2, 1},
      {0x2c, 0xff, 1, 1, 3}, {0x73, 0xff, 3, 1}, {0x73, 0, 0, 255}};
  const std::array<std::string_view, 4> names{"callee", "a", "b", "c"};
  const std::array<std::vector<std::uint32_t>, 6> arguments{
      std::vector<std::uint32_t>{}, {1}, {2, 1}, {1, 1, 3}, {1, 2, 3}, {}};
  for (std::size_t i = 0; i < encodings.size(); ++i) {
    const auto decoded = Body(encodings[i]).instructions.front().decoded;
    const auto call =
        std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, names.size()));
    Check(call.Count() == arguments[i].size());
    for (std::uint32_t j = 0; j < call.Count(); ++j)
      Check(call.At(j)->value == arguments[i][j]);
    Check(!call.At(call.Count()));
    std::vector<std::uint8_t> code{0x60, 0};
    code.insert(code.end(), encodings[i].begin(), encodings[i].end());
    code.push_back(0x64);
    const auto body = Body(code);
    std::ostringstream rendered, ordered;
    Check(std::holds_alternative<std::size_t>(
        source::WriteLinearBody(body, {"acc", names}, rendered)));
    Check(std::holds_alternative<std::size_t>(
        source::WriteAcyclicBody(body, {"acc", names}, ordered)));
    Check(rendered.str() == ordered.str());
    std::ostringstream exact;
    Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        body, {"acc", names}, exact, {rendered.str().size()})));
    Check(exact.str() == rendered.str());
    std::ostringstream rejected;
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(body, {"acc", names}, rejected,
                                      {rendered.str().size() - 1}))
              .emit_error == source::EmitError::kOutputLimit);
    Check(rejected.str().empty());
    if (argc == 2 && std::string_view(argv[1]) == "--emit") {
      std::cout << "export function call" << i << "(callee,a,b,c){let acc;\n"
                << rendered.str() << "}\n";
      std::cout << "export function failure" << i
                << "(callee,a,b,c){let acc;try {\n"
                << rendered.str()
                << "} catch(error) {return [callee,acc,error];}}\n";
    }
    auto malformed = decoded;
    malformed.operand_count = 0;
    Check(std::get<CallError>(LiftOrdinaryCall(malformed, names.size())) ==
          CallError::kMalformedInstruction);
    for (std::size_t operand = 0; operand < decoded.operand_count; ++operand) {
      malformed = decoded;
      malformed.operands[operand].raw_bits = 256;
      Check(std::get<CallError>(LiftOrdinaryCall(malformed, UINT64_MAX)) ==
            CallError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kString;
      Check(std::get<CallError>(LiftOrdinaryCall(malformed, names.size())) ==
            CallError::kMalformedInstruction);
    }
    std::vector<std::uint8_t> uninitialized = encodings[i];
    uninitialized.push_back(0x64);
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(Body(uninitialized), {"acc", names},
                                      rejected))
              .kind == source::BodyEmitError::kAccumulatorHole);
    Check(rejected.str().empty());
  }
  const auto last_register = Body(std::array<std::uint8_t, 3>{0x2a, 0, 255})
                                 .instructions.front()
                                 .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(last_register, 256))
            .At(0)
            ->value == 255);
  Check(std::get<CallError>(LiftOrdinaryCall(last_register, 255)) ==
        CallError::kRegisterRange);
  const auto empty_range = Body(std::array<std::uint8_t, 4>{0x73, 0, 0, 255})
                               .instructions.front()
                               .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(empty_range, 0)).Count() == 0);
  const auto crossing_range =
      Body(std::array<std::uint8_t, 4>{0x73, 0, 255, 255})
          .instructions.front()
          .decoded;
  Check(std::get<OrdinaryCall>(LiftOrdinaryCall(crossing_range, 510))
            .At(254)
            ->value == 509);
  Check(std::get<CallError>(LiftOrdinaryCall(crossing_range, 509)) ==
        CallError::kRegisterRange);
  for (const std::size_t count : {200, 201, 4096, 16384}) {
    std::vector<std::uint8_t> code{0x60, 0};
    for (std::size_t i = 0; i < count; ++i) code.insert(code.end(), {0x29, 0});
    code.push_back(0x64);
    std::ostringstream rendered;
    const auto result =
        source::WriteLinearBody(Body(code), {"acc", names}, rendered);
    Check(std::get<std::size_t>(result) == rendered.str().size());
    constexpr std::string_view kCallStatement = "acc = (0, acc)();\n";
    Check(rendered.str().size() ==
          std::string_view("acc = callee;\nreturn acc;\n").size() +
              count * kCallStatement.size());
  }
  for (const std::uint8_t count : {200, 201, 255}) {
    const std::vector<std::uint8_t> code{0x60, 0, 0x73, 0, count, 1, 0x64};
    const auto body = Body(code);
    const auto& decoded = body.instructions[1].decoded;
    const auto call =
        std::get<OrdinaryCall>(LiftOrdinaryCall(decoded, count + 1));
    Check(call.Count() == count && call.At(count - 1)->value == count);
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, count)) ==
          CallError::kRegisterRange);
    std::vector<std::string> storage{"callee"};
    for (std::size_t i = 0; i < count; ++i)
      storage.push_back("p" + std::to_string(i));
    const std::vector<std::string_view> views(storage.begin(), storage.end());
    std::ostringstream source;
    Check(std::holds_alternative<std::size_t>(
        source::WriteLinearBody(body, {"acc", views}, source)));
    if (argc == 2 && std::string_view(argv[1]) == "--emit") {
      std::cout << "export function range" << unsigned(count) << "(callee";
      for (std::size_t i = 1; i < views.size(); ++i)
        std::cout << ',' << views[i];
      std::cout << "){let acc;\n" << source.str() << "}\n";
    }
  }
  for (const auto& encoding : {std::vector<std::uint8_t>{0xfd, 4, 1, 0, 1},
                               {0x2d, 0, 0},
                               {0xfc, 11, 0}}) {
    const auto decoded = Body(encoding).instructions.front().decoded;
    Check(std::get<CallError>(LiftOrdinaryCall(decoded, 4)) ==
          CallError::kUnsupportedOpcode);
    std::vector<std::uint8_t> code{0x60, 0};
    code.insert(code.end(), encoding.begin(), encoding.end());
    code.push_back(0x64);
    std::ostringstream rejected;
    Check(std::holds_alternative<source::BodyEmitFailure>(
        source::WriteLinearBody(Body(code), {"acc", names}, rejected)));
    Check(rejected.str().empty());
  }
}
