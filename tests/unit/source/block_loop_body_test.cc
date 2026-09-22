#include <array>
#include <sstream>

#include "abcd/source/acyclic_body.h"
#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace abcd::test;
namespace abcd::test {
void VerifyBlockLoopOrder();
}
int main(int argc, char** argv) {
  const std::array<std::string_view, 1> registers{"step"};
  // Re-read callee every iteration, call, and repeat if exactly false or 0.
  const auto body = Body({0x60, 0, 0x29, 0, 0x4f, 0xfc, 0x64});
  std::ostringstream output;
  const auto result =
      source::WriteBlockLoopBody(body, {"acc", registers}, output);
  Check(std::holds_alternative<std::size_t>(result));
  Check(output.str().find("while (true)") != std::string::npos);
  Check(output.str().find("continue;") != std::string::npos);
  std::ostringstream rejected;
  Check(std::get<analysis::AccumulatorFailure>(
            source::WriteAcyclicBody(body, {"acc", registers}, rejected))
            .order_error == analysis::OrderError::kCycle);
  source::AcyclicBudget budget;
  budget.output.bytes = output.str().size() - 1;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteBlockLoopBody(body, {"acc", registers}, rejected,
                                       budget))
            .emit_error == source::EmitError::kOutputLimit);
  budget.output.bytes++;
  std::ostringstream exact;
  Check(std::get<std::size_t>(source::WriteBlockLoopBody(
            body, {"acc", registers}, exact, budget)) == output.str().size());
  Check(exact.str() == output.str());
  budget.max_nesting = 1;
  Check(std::get<source::AcyclicError>(source::WriteBlockLoopBody(
            body, {"acc", registers}, rejected, budget)) ==
        source::AcyclicError::kNestingLimit);
  budget = {};
  budget.bytes = 0;
  Check(std::get<analysis::FlowFailure>(
            source::WriteBlockLoopBody(body, {"acc", registers}, rejected,
                                       budget))
            .kind == analysis::FlowError::kResourceLimit);
  auto protected_body = body;
  protected_body.handlers.emplace_back();
  Check(std::get<source::BodyEmitFailure>(
            source::WriteBlockLoopBody(protected_body, {"acc", registers},
                                       rejected))
            .kind == source::BodyEmitError::kExceptionRegions);
  const auto multiple = Body({0, 0x4d, 2, 0, 0x4d, 0xfc});
  Check(std::get<analysis::AccumulatorFailure>(
            source::WriteBlockLoopBody(multiple, {"acc", registers}, rejected))
            .order_error == analysis::OrderError::kCycle);
  const auto hole = Body({0, 0x4d, 2, 0x61, 0, 0x70, 0x4d, 0xfd});
  Check(std::get<analysis::AccumulatorFailure>(
            source::WriteBlockLoopBody(hole, {"acc", registers}, rejected))
            .offset == abc::CodeOffset{3});
  const auto reset = Body({0, 0x4d, 2, 0, 0x70, 0x4d, 0xfe});
  const auto reset_flow =
      std::get<analysis::AccumulatorFlow>(analysis::AnalyzeBlockLoopAccumulator(
          reset,
          std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(reset)), 1));
  Check(reset_flow.instruction_visits == 8);
  Check(reset_flow.entries[1] == analysis::AccumulatorState::kMayBeHole);
  const auto plain = Body({0, 0x64});
  std::ostringstream old_plain, new_plain;
  Check(std::holds_alternative<std::size_t>(
      source::WriteAcyclicBody(plain, {"acc", registers}, old_plain)));
  Check(std::holds_alternative<std::size_t>(
      source::WriteBlockLoopBody(plain, {"acc", registers}, new_plain)));
  Check(old_plain.str() == new_plain.str());
  Check(rejected.str().empty());
  const auto emit = [&](const abc::MethodBody& input) {
    std::ostringstream stream;
    Check(std::holds_alternative<std::size_t>(
        source::WriteBlockLoopBody(input, {"acc", registers}, stream)));
    return stream.str();
  };
  const auto nonzero = emit(Body({0x60, 0, 0x29, 0, 0x51, 0xfc, 0x64}));
  const auto spin = emit(Body({0x60, 0, 0x29, 0, 0x4d, 0xfc, 0x65}));
  const auto sequential = emit(
      Body({0x60, 0, 0x29, 0, 0x4f, 0xfc, 0x60, 0, 0x29, 0, 0x51, 0xfc, 0x64}));
  // A conditional prefix chooses to bypass or enter the loop. Its forward
  // labels must not become loop labels or accidentally break only the while.
  const std::array<std::string_view, 2> choice_registers{"step", "choice"};
  const auto choice =
      Body({0x60, 1, 0x4f, 8, 0x60, 0, 0x29, 0, 0x4f, 0xfc, 0x64});
  std::ostringstream chosen;
  Check(std::holds_alternative<std::size_t>(
      source::WriteBlockLoopBody(choice, {"acc", choice_registers}, chosen)));
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export function zero(step){let acc;\n"
              << output.str() << "}\n"
              << "export function nonzero(step){let acc;\n"
              << nonzero << "}\n"
              << "export function spin(step){let acc;\n"
              << spin << "}\n"
              << "export function sequential(step){let acc;\n"
              << sequential << "}\n"
              << "export function chosen(step,choice){let acc;\n"
              << chosen.str() << "}\n";
    return 0;
  }
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0, 0x4f, 0xff});
    bytes.push_back(0x64);
    const auto chain = Body(bytes);
    source::AcyclicBudget expanded;
    expanded.max_nesting = std::size_t(count) + 1;
    std::ostringstream stream;
    const auto size = std::get<std::size_t>(source::WriteBlockLoopBody(
        chain, {"acc", registers}, stream, expanded));
    Check(size == stream.str().size() && size < std::size_t(count) * 200);
  }
  VerifyBlockLoopOrder();
}
