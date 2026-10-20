#ifndef ABCD_TEST_GUARDED_FIXTURE_H_
#define ABCD_TEST_GUARDED_FIXTURE_H_
#include "../../../src/analysis/guarded_reads.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_captures.h"
#include "analysis/ssa_fixture.h"
namespace abcd::test {
using namespace analysis;
enum class GuardedCallCase { kPlain, kForeign, kCatchState, kOutsideCatch, kPureWrapper, kTwoReads, kExtraUse, kSatisfied };
template<class F> auto WithGuardedGraph(std::size_t count, bool copies, bool split, bool escape, F&& inspect, bool reordered = false, bool calls = false, GuardedCallCase variant = GuardedCallCase::kPlain, bool pure_wrapper = false) {
  std::vector<std::uint8_t> bytes;
  const auto opcode = [&](abc::Opcode op) {
    const auto encoded = static_cast<std::uint16_t>(op);
    bytes.push_back(static_cast<std::uint8_t>(encoded));
    if (encoded > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(encoded >> 8));
  };
  if (variant == GuardedCallCase::kCatchState) {
    opcode(abc::Opcode::kLDAI_IMM32); bytes.insert(bytes.end(), {1,0,0,0});
    opcode(abc::Opcode::kSTA_V8); bytes.push_back(0);
  }
  const auto try_begin = static_cast<std::uint32_t>(bytes.size());
  std::optional<std::uint32_t> outside;
  for (std::size_t i = 0; i < count; ++i) {
    if (calls && variant != GuardedCallCase::kSatisfied) {
      opcode(abc::Opcode::kLDGLOBAL);
      opcode(abc::Opcode::kSTA_V8); bytes.push_back(1);
      opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, 0, 0});
      opcode(abc::Opcode::kSTA_V8); bytes.push_back(2);
    }
    if (variant == GuardedCallCase::kCatchState) {
      opcode(abc::Opcode::kLDAI_IMM32); bytes.insert(bytes.end(), {2,0,0,0});
      opcode(abc::Opcode::kSTA_V8); bytes.push_back(0);
    }
    opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4); bytes.push_back(0);
    if (copies) {
      opcode(abc::Opcode::kSTA_V8); bytes.push_back(0);
      opcode(abc::Opcode::kLDA_V8); bytes.push_back(0);
    }
    if (split) { opcode(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0, 0});
    if (calls) {
      if ((variant == GuardedCallCase::kPureWrapper || pure_wrapper)) { opcode(abc::Opcode::kTYPEOF_IMM8); bytes.push_back(0); }
      opcode(abc::Opcode::kSTA_V8); bytes.push_back(3);
      if (variant == GuardedCallCase::kSatisfied) {
        opcode(abc::Opcode::kLDGLOBAL); opcode(abc::Opcode::kSTA_V8); bytes.push_back(1);
        opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0,0,0});
        opcode(abc::Opcode::kSTA_V8); bytes.push_back(2);
      }
      if (variant == GuardedCallCase::kForeign) {
        opcode(abc::Opcode::kLDA_V8); bytes.push_back(1);
        opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0,0,0});
      }
      if (variant == GuardedCallCase::kTwoReads) {
        opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4); bytes.push_back(0);
        opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0,0});
        opcode(abc::Opcode::kSTA_V8); bytes.push_back(4);
      }
      if (variant == GuardedCallCase::kOutsideCatch) outside = static_cast<std::uint32_t>(bytes.size());
      opcode(abc::Opcode::kLDA_V8); bytes.push_back(2);
      if (variant == GuardedCallCase::kTwoReads || variant == GuardedCallCase::kExtraUse) {
        opcode(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8);
        bytes.insert(bytes.end(), {0,1,3,static_cast<std::uint8_t>(variant == GuardedCallCase::kTwoReads ? 4 : 3)});
      } else { opcode(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); bytes.insert(bytes.end(), {0, 1, 3}); }
    }
  }
  if (reordered) {
    Check(count == 1 && !copies && !split && !escape);
    bytes.clear();
    // Layout order is deliberately opposite to execution order. Entry jumps
    // over the guard/return to a load, which jumps back to the guard.
    opcode(abc::Opcode::kJMP_IMM8); bytes.push_back(7);
    opcode(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16); bytes.insert(bytes.end(), {0, 0});
    opcode(abc::Opcode::kRETURN);
    opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4); bytes.push_back(0);
    opcode(abc::Opcode::kJMP_IMM8); bytes.push_back(static_cast<std::uint8_t>(-7));
  }
  const auto end = static_cast<std::uint32_t>(bytes.size());
  opcode(abc::Opcode::kRETURN);
  if (escape || variant == GuardedCallCase::kCatchState) { opcode(abc::Opcode::kLDA_V8); bytes.push_back(0); }
  opcode(abc::Opcode::kRETURN);
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {try_begin}; region.length = outside.value_or(end) - try_begin; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {end + 1}; handler.length = static_cast<std::uint32_t>(bytes.size()) - end - 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(calls ? 5 : 1, 0, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto captures = std::get<ExceptionCaptures>(PlanExceptionCaptures(flow, environments));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, {}, value_work, {},
      PendingExceptionValues::kLanguage, captures.loads, {}, CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  return inspect(graph, body, input);
}
}  // namespace abcd::test
#endif
