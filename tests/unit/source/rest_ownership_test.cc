#include "../../../src/source/rest_ownership.h"
#include "../../../src/source/function_values.h"
#include "analysis/ssa_fixture.h"
namespace {
using namespace abcd; using namespace abcd::source; using namespace abcd::analysis;
void Run(std::size_t gap, bool physical = false, bool observable = false) {
  const auto op = [](abc::Opcode value) {return static_cast<std::uint8_t>(value);};
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(4, 4, {true,true,true,true}));
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kMOV_V8_V8), 3, 7,
      op(abc::Opcode::kMOV_V8_V8), 1, 5};
  bytes.insert(bytes.end(), gap, op(abc::Opcode::kNOP));
  if (observable) bytes.push_back(op(abc::Opcode::kDEBUGGER));
  bytes.insert(bytes.end(), {op(abc::Opcode::kCOPYRESTARGS_IMM8), 0, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(physical ? 7 : 0), op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  RouteInputIdentity identity{}; identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {}, {}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto proof = ProveRestOwnership(values.graph, body, frame, 0);
  if (physical || observable) test::Check(std::holds_alternative<RestOwnershipError>(proof));
  else {
    test::Check(std::holds_alternative<RestOwnership>(proof)); const auto& cost = std::get<RestOwnership>(proof);
    test::Check(std::holds_alternative<RestOwnership>(ProveRestOwnership(values.graph, body, frame, 0, {cost.allocated_bytes, cost.work})));
    test::Check(std::get<RestOwnershipError>(ProveRestOwnership(values.graph, body, frame, 0, {cost.allocated_bytes, cost.work - 1})) == RestOwnershipError::kWorkLimit);
    test::Check(std::get<RestOwnershipError>(ProveRestOwnership(values.graph, body, frame, 0, {0, cost.work})) == RestOwnershipError::kResourceLimit);
  }
}
enum class Change { kNone, kInvoke, kEscape, kArgument, kRead, kExit };
void Declarations(std::size_t count, Change change = Change::kNone, bool split = false) {
  const auto op = [](abc::Opcode value) {
    return static_cast<std::uint8_t>(value);
  };
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(2, 5, {true, true, true, true}));
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kNEWLEXENV_IMM8), 1};
  const std::uint8_t saved = split ? 1 : 0;
  for (std::size_t i = 0; i < count; ++i) {
    bytes.insert(bytes.end(),
                 {op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0,
                  op(abc::Opcode::kSTA_V8), saved, op(abc::Opcode::kLDA_V8), saved,
                  op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
    if (split) bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 2});
  }
  switch (change) {
    case Change::kNone:
      break;
    case Change::kInvoke:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
                                 op(abc::Opcode::kCALLARG0_IMM8), 0});
      break;
    case Change::kEscape:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
                                 op(abc::Opcode::kSTLEXVAR_IMM8_IMM8), 1, 0});
      break;
    case Change::kArgument:
      bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 6, 0});
      break;
    case Change::kRead:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0});
      break;
    case Change::kExit:
      bytes.insert(bytes.end(),
                   {op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kJEQZ_IMM8), 5,
                    op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
      break;
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kCOPYRESTARGS_IMM8), 1,
                             op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(
      identity, {}, {}, body, frame, InputCaptureMode::kConditional));
  const auto values =
      std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto proof = ProveRestOwnership(values.graph, body, frame, 1, {},
                                        nullptr, &*values.environments);
  if (change != Change::kNone) {
    test::Check(std::get<RestOwnershipError>(proof) ==
                (change == Change::kArgument ? RestOwnershipError::kArgumentUse
                                             : RestOwnershipError::kOrder));
    return;
  }
  test::Check(std::holds_alternative<RestOwnership>(proof));
  const auto cost = std::get<RestOwnership>(proof);
  std::cout << "declarations=" << count << " bytes=" << cost.allocated_bytes
            << " work=" << cost.work << '\n';
  test::Check(cost.declarations.size() == 1 + 2 * count);
  test::Check(std::holds_alternative<RestOwnership>(VerifyRestOwnership(
      values.graph, body, frame, 1, cost, {}, &*values.environments)));
  test::Check(std::get<RestOwnershipError>(VerifyRestOwnership(
                  values.graph, body, frame, 1, cost,
                  {cost.allocated_bytes, cost.work}, &*values.environments)) ==
              RestOwnershipError::kWorkLimit);
  const auto verified = std::get<RestOwnership>(VerifyRestOwnership(
      values.graph, body, frame, 1, cost, {}, &*values.environments));
  test::Check(verified.work == cost.work + cost.declarations.size());
  test::Check(std::holds_alternative<RestOwnership>(VerifyRestOwnership(
      values.graph, body, frame, 1, cost,
      {verified.allocated_bytes, verified.work}, &*values.environments)));
  test::Check(std::get<RestOwnershipError>(VerifyRestOwnership(
                  values.graph, body, frame, 1, cost,
                  {verified.allocated_bytes, verified.work - 1},
                  &*values.environments)) == RestOwnershipError::kWorkLimit);
  for (const auto mutation : {0, 1, 2}) {
    auto forged = cost;
    if (mutation == 0)
      forged.declarations[0].role = RestDeclarationRole::kCellStore;
    if (mutation == 1) forged.declarations.pop_back();
    if (mutation == 2)
      forged.declarations.push_back(forged.declarations.back());
    test::Check(std::get<RestOwnershipError>(VerifyRestOwnership(
                    values.graph, body, frame, 1, forged, {},
                    &*values.environments)) == RestOwnershipError::kOrder);
  }
  auto forged = values.graph;
  const auto store =
      std::find_if(forged.ops.begin(), forged.ops.end(), [](const auto& op) {
        return std::holds_alternative<ValueLexicalStore>(op.operation);
      });
  std::get<ValueLexicalStore>(store->operation).address.depth.value = 1;
  test::Check(std::get<RestOwnershipError>(VerifyRestOwnership(
                  forged, body, frame, 1, cost, {}, &*values.environments)) ==
              RestOwnershipError::kOrder);
  test::Check(std::holds_alternative<RestOwnership>(ProveRestOwnership(
      values.graph, body, frame, 1, {cost.allocated_bytes, cost.work}, nullptr,
      &*values.environments)));
  test::Check(
      std::get<RestOwnershipError>(ProveRestOwnership(
          values.graph, body, frame, 1, {cost.allocated_bytes - 1, cost.work},
          nullptr, &*values.environments)) ==
      RestOwnershipError::kResourceLimit);
  test::Check(
      std::get<RestOwnershipError>(ProveRestOwnership(
          values.graph, body, frame, 1, {cost.allocated_bytes, cost.work - 1},
          nullptr, &*values.environments)) == RestOwnershipError::kWorkLimit);
}
}  // namespace
int main() {
  for (auto gap : {0U, 1U, 200U, 201U, 400U, 800U}) Run(gap);
  Run(1, true);
  Run(1, false, true);
  for (auto count : {1U, 200U, 201U, 400U, 800U}) Declarations(count);
  Declarations(201, Change::kNone, true);
  for (auto change : {Change::kInvoke, Change::kEscape, Change::kArgument,
                      Change::kRead, Change::kExit})
    Declarations(1, change);
}
