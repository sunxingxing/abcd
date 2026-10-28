// es2abc may copy a guarded module load to registers before its Hole guard
// (`ldexternalmodulevar; sta vN; throw.undefinedifholewithname`). The pair is
// certified when the copies cannot be observed on the guard's throw: the
// caller discards the frame, or no local handler reads the copied register.
#include "abcd/analysis/exception_modules.h"
#include "analysis/ssa_fixture.h"

#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr auto kLoad = static_cast<std::uint8_t>(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8);
constexpr auto kSta = static_cast<std::uint8_t>(abc::Opcode::kSTA_V8);
constexpr auto kLda = static_cast<std::uint8_t>(abc::Opcode::kLDA_V8);
constexpr auto kReturn = static_cast<std::uint8_t>(abc::Opcode::kRETURN);
constexpr auto kGuard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);

// try { v0 = load; guard; return v0 } catch { return HANDLER }
ExceptionModules Prove(bool caught, std::uint8_t handler_register) {
  std::vector<std::uint8_t> bytes{kLoad, 7, kSta, 0,
      static_cast<std::uint8_t>(kGuard & 0xff), static_cast<std::uint8_t>(kGuard >> 8), 3, 0,
      kLda, 0, kReturn};
  const auto end = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {kLda, handler_register, kReturn});
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.length = end; region.handler_count = 1;
    body.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {end}; handler.length = 3;
    body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto proved = ProveExceptionModules(flow, ssa);
  test::Check(std::holds_alternative<ExceptionModules>(proved));
  return std::get<ExceptionModules>(proved);
}
}  // namespace

int main() {
  // Caller exit: the frame and its copied Hole are discarded.
  const auto caller = Prove(false, 1);
  test::Check(caller.reads.size() == 1 && caller.reads[0].proof == ModuleReadProof::kCallerGuarded &&
      caller.reads[0].guard && caller.reads[0].guard->value == 2);
  // Local catch that never reads v0: the copy is dead on the throw arc.
  const auto dead = Prove(true, 1);
  test::Check(dead.reads.size() == 1 && dead.reads[0].proof == ModuleReadProof::kCatchGuarded &&
      dead.reads[0].guard && dead.reads[0].guard->value == 2);
  // Local catch reading v0 would observe Hole; source assignment never ran.
  test::Check(Prove(true, 0).reads.empty());
  std::cout << "exception module copies ok\n";
}
