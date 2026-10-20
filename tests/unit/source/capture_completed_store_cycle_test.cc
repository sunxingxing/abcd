#include <iostream>

#include "../../../src/analysis/semantic_locals.h"
#include "../../../src/source/capture_plan.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct Input {
  abc::MethodBody body;
  SemanticMethod method;
  SemanticProof proof;
  InstructionId creation;
};
Input Make(unsigned width, bool late, bool copies) {
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode x) {
    const auto n = static_cast<std::uint16_t>(x);
    code.push_back(n & UINT8_MAX);
    if (n > UINT8_MAX) code.push_back(n >> 8);
  };
  const auto store = [&] {
    op(abc::Opcode::kLDTRUE);
    if (copies) {
      op(abc::Opcode::kSTA_V8);
      code.push_back(1);
      op(abc::Opcode::kLDA_V8);
      code.push_back(1);
    }
    op(abc::Opcode::kSTLEXVAR_IMM4_IMM4);
    code.push_back(0);
  };
  const auto jump = [&](abc::Opcode x) {
    const auto at = code.size();
    op(x);
    code.insert(code.end(), 4, 0);
    return at;
  };
  const auto bind = [&](std::size_t at, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - at);
    for (unsigned n = 0; n < 4; ++n) code[at + 1 + n] = delta >> (8 * n);
  };
  op(abc::Opcode::kNEWLEXENV_IMM8);
  code.push_back(1);
  if (!late) store();
  const auto header = code.size();
  op(abc::Opcode::kLDA_V8);
  code.push_back(0);
  const auto exit = jump(abc::Opcode::kJEQZ_IMM32);
  for (unsigned i = 0; i < width; ++i) {
    op(abc::Opcode::kJMP_IMM8);
    code.push_back(2);
  }
  op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
  code.insert(code.end(), {0, 0, 0, 0});
  if (late) store();
  const auto back = jump(abc::Opcode::kJMP_IMM32);
  bind(back, header);
  bind(exit, code.size());
  op(abc::Opcode::kRETURNUNDEFINED);
  auto body = test::Body(code);
  auto method = std::get<SemanticMethod>(
      BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 2));
  auto proof = std::get<SemanticProof>(VerifySemanticSource(
      body, method, 2, 0, {}, {}, {}, ClosureWrites::kLanguage));
  InstructionId creation;
  for (std::size_t i = 0; i < method.instructions.size(); ++i)
    if (std::holds_alternative<FunctionCreation>(
            method.instructions[i].operation))
      creation = {static_cast<std::uint32_t>(i)};
  return {std::move(body), std::move(method), std::move(proof), creation};
}
void Check(unsigned width, bool copies) {
  const auto input = Make(width, false, copies);
  const auto& method = input.method;
  const CreationBinding creation{
      method.instructions[input.creation.value].pc,
      std::get<FunctionCreation>(
          method.instructions[input.creation.value].operation),
      {{0}, {}},
      OrdinaryCapture{EnvironmentId{0}}};
  const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage};
  const ScopeFacts facts(method, input.proof);
  const auto built =
      BuildCapturePlan(facts, input.creation, creation, {&use, 1});
  test::Check(std::holds_alternative<CapturePlan>(built));
  const auto& plan = std::get<CapturePlan>(built);
  test::Check(!plan.bindings.front().deferred);
  test::Check(std::holds_alternative<std::uint64_t>(
      VerifyCapturePlan(facts, input.creation, creation, {&use, 1}, plan)));
  const auto local = [&](SemanticBudget limit) {
    return ProveLocalAccesses(method, input.proof.environments, limit,
                              ClosureWrites::kLanguage, {});
  };
  std::size_t lo = 0, hi = SemanticBudget{}.bytes;
  while (lo < hi) {
    const auto mid = lo + (hi - lo) / 2;
    if (std::holds_alternative<LocalAccessPlan>(local({mid})))
      hi = mid;
    else
      lo = mid + 1;
  }
  const auto exact = std::get<LocalAccessPlan>(local({lo})).work_units;
  test::Check(lo &&
              std::holds_alternative<SemanticProofFailure>(local({lo - 1})));
  test::Check(std::holds_alternative<LocalAccessPlan>(local({lo, exact})));
  test::Check(exact && std::holds_alternative<SemanticProofFailure>(
                           local({lo, exact - 1})));
  auto wrong = use;
  wrong.cell.slot = {1};
  test::Check(std::holds_alternative<CaptureFailure>(
      BuildCapturePlan(facts, input.creation, creation, {&wrong, 1})));
  auto forged = plan;
  forged.bindings.front().cell = CapturedSlot{{0}, {0}};
  test::Check(std::holds_alternative<CaptureFailure>(
      VerifyCapturePlan(facts, input.creation, creation, {&use, 1}, forged)));
  std::cout << width << " copies=" << copies << " bytes=" << lo
            << " work=" << exact << '\n';
}
}  // namespace
int main() {
  for (unsigned n : {1U, 200U, 201U, 400U, 800U}) {
    Check(n, false);
    Check(n, true);
  }
  const auto input = Make(1, true, false);
  const auto& m = input.method;
  const CreationBinding creation{
      m.instructions[input.creation.value].pc,
      std::get<FunctionCreation>(
          m.instructions[input.creation.value].operation),
      {{0}, {}},
      OrdinaryCapture{EnvironmentId{0}}};
  const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage};
  const ScopeFacts facts(m, input.proof);
  test::Check(std::holds_alternative<CaptureFailure>(
      BuildCapturePlan(facts, input.creation, creation, {&use, 1})));
}
