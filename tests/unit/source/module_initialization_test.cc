#include <algorithm>
#include <vector>

#include "../../../src/source/function_input.h"
#include "../../../src/source/function_values.h"
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
  void Read(std::uint16_t slot) {
    Op(abc::Opcode::kWIDE_LDLOCALMODULEVAR_PREF_IMM16);
    bytes.push_back(static_cast<std::uint8_t>(slot));
    bytes.push_back(static_cast<std::uint8_t>(slot >> 8));
  }
  auto TryInput() {
    Op(abc::Opcode::kRETURNUNDEFINED);
    auto body = test::Body(bytes);
    source::RouteInputIdentity identity;
    identity.profile = abc::ProfileId::kDynamic1206;
    identity.abc_version = {12, 0, 6, 0};
    auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    return source::FunctionInput::Build(identity, {300}, {200}, body, frame,
                                        source::InputCaptureMode::kConditional);
  }
  source::FunctionInput Input() {
    return std::get<source::FunctionInput>(TryInput());
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
  // A store in the straight prelude after a creation (only creations, module
  // stores and moves between) happens before the function can first run.
  Body body;
  body.Create();
  body.Store(0);
  body.Create();
  auto input = body.Input();
  body.Check(input, 0, 0, true);
  body.Check(input, 1, 0, true);
  test::Check(input.InitializedModulesBefore({UINT32_MAX}).Empty());
  // Any other instruction ends the prelude: the later store is not counted.
  Body interrupted;
  interrupted.Create();
  interrupted.Read(5);
  interrupted.Store(0);
  interrupted.Create();
  auto ended = interrupted.Input();
  interrupted.Check(ended, 0, 0, false);
  interrupted.Check(ended, 1, 0, true);
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
void CheckRead(Body& body, bool initialized) {
  body.Read(1);
  body.Op(abc::Opcode::kRETURN);
  const auto built = body.TryInput();
  if (!initialized) {
    const auto* failure = std::get_if<source::FunctionInputFailure>(&built);
    test::Check(
        failure &&
        std::holds_alternative<analysis::SemanticProofFailure>(failure->cause));
    test::Check(std::get<analysis::SemanticProofFailure>(failure->cause).kind ==
                analysis::SemanticProofError::kInternalValue);
    return;
  }
  const auto& input = std::get<source::FunctionInput>(built);
  test::Check(input.Proof().module_reads.size() == 1 &&
              input.Proof().module_reads[0].proof ==
                  analysis::ModuleReadProof::kStored);
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto candidate = analysis::BuildModuleInitialization(input.Input(), {});
  const auto work = std::get<analysis::ModuleInitialization>(candidate).work;
  analysis::SemanticBudget limited;
  limited.work = work + input.Proof().locals.work_units - 1;
  // Candidate analysis may not spend work already reserved for the lexical
  // proof. Subtracting it afterward must never wrap into an unlimited quota.
  test::Check(std::holds_alternative<analysis::SemanticProofFailure>(
      analysis::VerifySemanticSource(test::Body(body.bytes), input.Input(),
                                     frame.registers, frame.locals, limited)));
  const auto values =
      source::BuildFunctionValues(input, test::Body(body.bytes), frame);
  test::Check(std::holds_alternative<source::FunctionValues>(values));
  const auto& graph = std::get<source::FunctionValues>(values);
  test::Check(graph.modules.pairs.size() == 1 && !graph.modules.pairs[0].guard);
  auto changed = graph.graph;
  // Remove just one real store. The branch graph and read identity still
  // exist; initialization must be rechecked on current control flow.
  for (auto& op : changed.ops) {
    if (!std::holds_alternative<analysis::ValueModuleWrite>(op.operation))
      continue;
    op.life = analysis::ValueLife::kRemoved;
    break;
  }
  test::Check(std::holds_alternative<source::ModuleGuardError>(
      source::VerifyModuleGuards(input, changed, graph.modules)));
}
void CombinedSiteOrder() {
  using namespace analysis;
  const std::array<ModuleStateSite, 2> sites{
      {{{0}, ModuleSlot{1}, true}, {{1}, {}, true}}};
  NormalFlow flow;
  flow.blocks.resize(1);
  flow.blocks[0].instruction_count = sites.size();
  const auto built = BuildModuleInitialization(sites, flow, {});
  const auto& states = std::get<ModuleInitialization>(built).states;
  test::Check(states.size() == 2 && !states[0].slots.Contains({1}) &&
              states[1].slots.Contains({1}));
}
void RawReads() {
  {
    Body body;
    body.Argument();
    auto branch = body.Branch();
    body.Op(abc::Opcode::kLDTRUE);
    body.Target(branch, body.bytes.size());
    body.Store(1);
    CheckRead(body, true);
  }
  for (const bool both : {false, true}) {
    Body body;
    body.Argument();
    auto branch = body.Branch();
    body.Store(1);
    auto jump = body.Jump();
    body.Target(branch, body.bytes.size());
    if (both) body.Store(1);
    body.Target(jump, body.bytes.size());
    CheckRead(body, both);
  }
  for (const bool executes : {false, true}) {
    Body body;
    const auto start = body.bytes.size();
    if (executes) body.Store(1);
    body.Argument();
    auto branch = body.Branch();
    if (!executes) body.Store(1);
    auto jump = body.Jump();
    body.Target(jump, start);
    body.Target(branch, body.bytes.size());
    CheckRead(body, executes);
  }
  {
    // An uninitialized read/store cycle cannot manufacture its own premise.
    Body body;
    body.Read(1);
    body.Op(abc::Opcode::kSTMODULEVAR_IMM8);
    body.bytes.push_back(1);
    CheckRead(body, false);
  }
  {
    Body body;
    body.Op(abc::Opcode::kLDHOLE);
    body.Op(abc::Opcode::kSTMODULEVAR_IMM8);
    body.bytes.push_back(1);
    CheckRead(body, false);
  }
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
  const auto built = analysis::BuildModuleInitialization(input.Input(), {});
  test::Check(std::holds_alternative<analysis::ModuleInitialization>(built));
  const auto& states = std::get<analysis::ModuleInitialization>(built);
  test::Check(states.states.size() == count * 3);
  analysis::SemanticBudget limited;
  limited.work = states.work;
  test::Check(std::holds_alternative<analysis::ModuleInitialization>(
      analysis::BuildModuleInitialization(input.Input(), {}, limited)));
  --limited.work;
  test::Check(
      std::get<analysis::ModuleInitializationError>(
          analysis::BuildModuleInitialization(input.Input(), {}, limited)) ==
      analysis::ModuleInitializationError::kWorkLimit);
  limited = {};
  limited.bytes = 0;
  test::Check(
      std::get<analysis::ModuleInitializationError>(
          analysis::BuildModuleInitialization(input.Input(), {}, limited)) ==
      analysis::ModuleInitializationError::kResourceLimit);
  return states.work;
}
}  // namespace
int main() {
  JoinedStore();
  Diamond(true);
  Diamond(false);
  Loop(false);
  Loop(true);
  CombinedSiteOrder();
  RawReads();
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
