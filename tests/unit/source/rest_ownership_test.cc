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
}
int main() {for (auto gap : {0U, 1U, 200U, 201U}) Run(gap); Run(1,true); Run(1,false,true);}
