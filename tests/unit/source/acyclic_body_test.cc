#include "abcd/source/acyclic_body.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
void Check(bool value) {
  if (!value) std::abort();
}
class FailingBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};
abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded =
        std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
            std::span(bytes).subspan(pc), abc::ProfileId::kDynamic1206));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta)
      instruction.branch_target = abc::CodeOffset{
          static_cast<std::uint32_t>(std::int64_t(pc) + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  return body;
}
abc::MethodBody Body(std::initializer_list<std::uint8_t> bytes) {
  return Body(std::span(bytes.begin(), bytes.size()));
}
}  // namespace
int main(int argc, char** argv) {
  const std::array<std::string_view, 1> registers{"input"};
  const source::ScalarNames names{"acc", registers};
  // lda input; jeqz taken; ldai 11; jmp join; taken: ldai 22; join: return
  const auto body = Body(
      {0x60, 0, 0x4f, 9, 0x62, 11, 0, 0, 0, 0x4d, 7, 0x62, 22, 0, 0, 0, 0x64});
  std::ostringstream output;
  Check(std::get<std::size_t>(source::WriteAcyclicBody(body, names, output)) ==
        output.str().size());
  source::AcyclicBudget budget;
  budget.output.bytes = output.str().size() - 1;
  std::ostringstream rejected;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteAcyclicBody(body, names, rejected, budget))
            .emit_error == source::EmitError::kOutputLimit);
  Check(rejected.str().empty());
  std::string scaled;
  for (const std::uint32_t count : {200U, 201U}) {
    std::vector<std::uint8_t> bytes{0x00};
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0x4f, 2});
    bytes.push_back(0x64);
    const auto chain = Body(bytes);
    Check(std::get<source::AcyclicError>(source::WriteAcyclicBody(
              chain, names, rejected)) == source::AcyclicError::kNestingLimit);
    source::AcyclicBudget expanded;
    expanded.max_nesting = count;
    std::ostringstream stream;
    Check(std::holds_alternative<std::size_t>(
        source::WriteAcyclicBody(chain, names, stream, expanded)));
    scaled = stream.str();
    constexpr std::size_t kMaxBytesPerBlock = 200;
    Check(scaled.size() < kMaxBytesPerBlock * (count + 1));
  }
  budget.output.bytes++;
  std::ostringstream exact;
  Check(std::get<std::size_t>(source::WriteAcyclicBody(
            body, names, exact, budget)) == output.str().size());
  Check(exact.str() == output.str());
  budget.max_nesting = 0;
  Check(std::get<source::AcyclicError>(
            source::WriteAcyclicBody(body, names, rejected, budget)) ==
        source::AcyclicError::kNestingLimit);
  Check(rejected.str().empty());
  const auto loop = Body({0, 0x4d, 0xff});
  Check(std::get<analysis::AccumulatorFailure>(
            source::WriteAcyclicBody(loop, names, rejected))
            .order_error == analysis::OrderError::kCycle);
  auto protected_body = body;
  protected_body.tries.push_back({});
  Check(std::get<source::BodyEmitFailure>(
            source::WriteAcyclicBody(protected_body, names, rejected))
            .kind == source::BodyEmitError::kExceptionRegions);
  const auto hole = Body({0x4f, 2, 0x64});
  Check(std::get<analysis::AccumulatorFailure>(
            source::WriteAcyclicBody(hole, names, rejected))
            .kind == analysis::AccumulatorError::kReadBeforeValue);
  Check(rejected.str().empty());
  std::ostringstream thrown;
  Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
      Body({0x60, 0, 0x4f, 4, 0xfe, 0, 0x65}), names, thrown)));
  std::ostringstream layout;
  Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
      Body({0x4d, 8, 0x62, 42, 0, 0, 0, 0x64, 0x4d, 0xfa}), names, layout)));
  const std::array<std::string_view, 2> effect_registers{"input", "key"};
  std::ostringstream effect;
  Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
      Body({0x60, 1,  0x4f, 12, 0x62, 0, 0, 0,    0, 0x37, 0,   0,
            0x4d, 10, 0x62, 1,  0,    0, 0, 0x37, 0, 0,    0x64}),
      {"acc", effect_registers}, effect)));
  for (const bool exceptions : {false, true}) {
    FailingBuffer buffer;
    std::ostream failed(&buffer);
    if (exceptions) failed.exceptions(std::ios::badbit | std::ios::failbit);
    Check(std::get<source::BodyEmitFailure>(
              source::WriteAcyclicBody(body, names, failed))
              .emit_error == source::EmitError::kWrite);
  }
  source::AcyclicBudget no_memory;
  no_memory.bytes = 0;
  Check(std::get<analysis::FlowFailure>(
            source::WriteAcyclicBody(body, names, rejected, no_memory))
            .kind == analysis::FlowError::kResourceLimit);
  Check(rejected.str().empty());
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export function diamond(input){let acc;\n"
              << output.str() << "}\n";
    std::cout << "export function conditionalThrow(input){let acc;\n"
              << thrown.str() << "}\n";
    std::cout << "export function layout(){let acc;\n" << layout.str() << "}\n";
    std::cout << "export function effect(input,key){let acc;\n"
              << effect.str() << "}\n";
    std::cout << "export function scaled(){let acc;\n" << scaled << "}\n";
  }
}
