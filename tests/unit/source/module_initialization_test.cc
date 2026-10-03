#include <algorithm>
#include <vector>

#include "../../../src/source/function_input.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
class Body {
 public:
  void Op(abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  }
  void Argument() {
    Op(abc::Opcode::kLDA_V8);
    bytes.push_back(0);
  }
  std::size_t Branch() {
    auto pc = bytes.size();
    Op(abc::Opcode::kJEQZ_IMM8);
    bytes.push_back(0);
    return pc;
  }
  std::size_t Jump() {
    auto pc = bytes.size();
    Op(abc::Opcode::kJMP_IMM8);
    bytes.push_back(0);
    return pc;
  }
  void Target(std::size_t pc, std::size_t target) {
    const auto delta =
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(pc);
    test::Check(delta >= INT8_MIN && delta <= INT8_MAX);
    bytes[pc + 1] = static_cast<std::uint8_t>(delta);
  }
  void Store(std::uint16_t slot) {
    Op(abc::Opcode::kLDAI_IMM32);
    bytes.insert(bytes.end(), {7, 0, 0, 0});
    Op(abc::Opcode::kWIDE_STMODULEVAR_PREF_IMM16);
    bytes.push_back(static_cast<std::uint8_t>(slot));
    bytes.push_back(static_cast<std::uint8_t>(slot >> 8));
  }
  void Create() {
    creations.push_back(bytes.size());
    Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
    bytes.insert(bytes.end(), {0, 0, 0, 0});
  }
  source::FunctionInput Input() {
    Op(abc::Opcode::kRETURNUNDEFINED);
    auto body = test::Body(bytes);
    source::RouteInputIdentity identity;
    identity.profile = abc::ProfileId::kDynamic1206;
    identity.abc_version = {12, 0, 6, 0};
    auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    return std::get<source::FunctionInput>(
        source::FunctionInput::Build(identity, {300}, {200}, body, frame,
                                     source::InputCaptureMode::kConditional));
  }
  void Check(const source::FunctionInput& input, std::size_t creation,
             std::uint16_t slot, bool initialized) const {
    const auto found = std::find_if(
        input.Input().instructions.begin(), input.Input().instructions.end(),
        [&](const auto& site) { return site.pc.value == creations[creation]; });
    test::Check(found != input.Input().instructions.end());
    const analysis::InstructionId site{
        static_cast<std::uint32_t>(found - input.Input().instructions.begin())};
    test::Check(input.InitializedModulesBefore(site).Contains({slot}) ==
                initialized);
  }
  std::vector<std::uint8_t> bytes;
  std::vector<std::size_t> creations;
};
void JoinedStore() {
  Body body;
  body.Argument();
  auto branch = body.Branch();
  body.Op(abc::Opcode::kLDTRUE);
  body.Target(branch, body.bytes.size());
  body.Store(0);
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 0, true);
}
void Diamond(bool both) {
  Body body;
  body.Store(0);
  body.Argument();
  auto branch = body.Branch();
  body.Store(1);
  body.Create();
  auto jump = body.Jump();
  body.Target(branch, body.bytes.size());
  if (both) body.Store(1);
  body.Create();
  body.Target(jump, body.bytes.size());
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 0, true);
  body.Check(input, 0, 1, true);
  body.Check(input, 1, 0, true);
  body.Check(input, 1, 1, both);
  body.Check(input, 2, 0, true);
  body.Check(input, 2, 1, both);
}
void Loop(bool executes) {
  Body body;
  body.Store(0);
  const auto start = body.bytes.size();
  if (executes) body.Store(1);
  body.Argument();
  auto branch = body.Branch();
  if (!executes) body.Store(1);
  body.Create();
  auto jump = body.Jump();
  body.Target(jump, start);
  body.Target(branch, body.bytes.size());
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 1, true);
  body.Check(input, 1, 1, executes);
  body.Check(input, 1, 0, true);
}
void BeforeStore() {
  Body body;
  body.Create();
  body.Store(0);
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 0, false);
  body.Check(input, 1, 0, true);
  test::Check(input.InitializedModulesBefore({UINT32_MAX}).Empty());
}
void UnreachableStore() {
  Body body;
  const auto jump = body.Jump();
  body.Store(0);
  body.Target(jump, body.bytes.size());
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 0, false);
}
std::uint64_t IndependentDiamonds(std::uint16_t count) {
  Body body;
  for (std::uint16_t slot = 0; slot < count; ++slot) {
    body.Argument();
    auto branch = body.Branch();
    body.Store(slot);
    body.Create();
    auto jump = body.Jump();
    body.Target(branch, body.bytes.size());
    if (slot % 2 == 0) body.Store(slot);
    body.Create();
    body.Target(jump, body.bytes.size());
    body.Create();
  }
  auto input = body.Input();
  for (std::uint16_t slot = 0; slot < count; ++slot) {
    body.Check(input, slot * 3, slot, true);
    body.Check(input, slot * 3 + 1, slot, slot % 2 == 0);
    body.Check(input, slot * 3 + 2, slot, slot % 2 == 0);
    if (slot) body.Check(input, slot * 3 + 2, slot - 1, (slot - 1) % 2 == 0);
  }
  const auto built =
      source::BuildModuleCreationStates(input.Input(), input.Proof(), {});
  test::Check(std::holds_alternative<source::ModuleCreationStates>(built));
  const auto& states = std::get<source::ModuleCreationStates>(built);
  test::Check(states.states.size() == count * 3);
  analysis::SemanticBudget limited;
  limited.work = states.work;
  test::Check(std::holds_alternative<source::ModuleCreationStates>(
      source::BuildModuleCreationStates(input.Input(), input.Proof(), {},
                                        limited)));
  --limited.work;
  test::Check(std::get<source::ModuleInitializationError>(
                  source::BuildModuleCreationStates(
                      input.Input(), input.Proof(), {}, limited)) ==
              source::ModuleInitializationError::kWorkLimit);
  limited = {};
  limited.bytes = 0;
  test::Check(std::get<source::ModuleInitializationError>(
                  source::BuildModuleCreationStates(
                      input.Input(), input.Proof(), {}, limited)) ==
              source::ModuleInitializationError::kResourceLimit);
  return states.work;
}
}  // namespace
int main() {
  JoinedStore();
  Diamond(true);
  Diamond(false);
  Loop(false);
  Loop(true);
  BeforeStore();
  UnreachableStore();
  IndependentDiamonds(200);
  IndependentDiamonds(201);
  const auto small = IndependentDiamonds(256);
  const auto medium = IndependentDiamonds(512);
  const auto large = IndependentDiamonds(1024);
  // Doubling independent state/output dimensions must stay below quadratic
  // growth. This bounded work-counter check is not a latency or RSS benchmark.
  test::Check(medium < small * 4 && large < medium * 4);
}
