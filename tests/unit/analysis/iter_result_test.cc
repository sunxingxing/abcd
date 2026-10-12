#include "abcd/analysis/iter_result.h"

#include <array>
#include <cstdlib>
#include <iostream>

#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/register_value_flow.h"
#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  const auto body = test::Body({0xb2, 0, 1, 0x64});
  const auto& decoded = body.instructions.front().decoded;
  const auto creation = std::get<analysis::IterResult>(
      analysis::LiftIterResult(decoded, 2));
  test::Check(creation.value.value == 0 && creation.done.value == 1);
  const auto effect = std::get<analysis::RegisterEffect>(
      analysis::LiftRegisterEffect(decoded, 2));
  test::Check(effect.read_count == 2 && effect.write &&
              std::get<abc::RegisterId>(effect.reads[0]).value == 0 &&
              std::get<abc::RegisterId>(effect.reads[1]).value == 1 &&
              std::holds_alternative<analysis::Accumulator>(*effect.write));
  const auto graph = std::get<analysis::NormalFlow>(
      analysis::BuildNormalFlow(body));
  analysis::IdfQuota work{UINT64_MAX};
  const auto ssa = std::get<analysis::RegisterSsa>(
      analysis::RegisterSsa::Build(body, graph, 2, work));
  const auto values = std::get<analysis::RegisterValueFlow>(
      analysis::RegisterValueFlow::Build(body, ssa, 2, {}, work));
  test::Check(values.At(*ssa.Write({0})) ==
              analysis::ValueKinds::Language());
  auto malformed = decoded;
  malformed.operand_count = 1;
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(malformed, 2)) ==
              analysis::IterResultError::kMalformedInstruction);
  malformed = decoded;
  malformed.operands[1].kind = abc::OperandKind::kImmediate;
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(malformed, 2)) ==
              analysis::IterResultError::kMalformedInstruction);
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(decoded, 1)) ==
              analysis::IterResultError::kRegisterRange);
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(body.instructions[1].decoded, 2)) ==
              analysis::IterResultError::kUnsupportedOpcode);
}
