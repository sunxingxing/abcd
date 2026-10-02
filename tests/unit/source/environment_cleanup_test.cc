#include "../../../src/source/environment_cleanup.h"
#include "analysis/ssa_fixture.h"
#include "abcd/abc/call_type.h"
#include <algorithm>
#include <limits>
#include <string>

#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
enum class Mutation { kNone, kOverwrite, kReturn, kEntry, kOuterPop, kDebugger, kTyped, kDoublePop };
static_assert(!std::is_default_constructible_v<EnvironmentCleanup>);
static_assert(!std::is_copy_constructible_v<EnvironmentCleanup>);
abc::MethodBody Body(unsigned count, Mutation mutation = Mutation::kNone) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr unsigned kByteBits = 8;
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  std::vector<std::uint8_t> bytes;
  std::vector<abc::TryRegion> tries;
  std::vector<abc::CatchHandler> handlers;
  for (unsigned i = 0; i < count; ++i) {
    if (mutation != Mutation::kOuterPop) bytes.insert(bytes.end(),{op(abc::Opcode::kNEWLEXENV_IMM8),1});
    if (mutation == Mutation::kDoublePop) bytes.insert(bytes.end(),{op(abc::Opcode::kNEWLEXENV_IMM8),1});
    const auto begin = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(),{op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16),0,0,0});
    tries.push_back({{begin},static_cast<std::uint32_t>(bytes.size()-begin),i,1});
    if (mutation == Mutation::kEntry) {
      test::Check(count == 1);
      constexpr std::uint8_t kJumpSize = 2;
      bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),kJumpSize});
    } else {
      bytes.push_back(op(abc::Opcode::kPOPLEXENV));
      if (mutation == Mutation::kDoublePop) bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    }
  }
  if (mutation != Mutation::kEntry) bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  for (unsigned i = 0; i < count; ++i) {
    const auto begin = static_cast<std::uint32_t>(bytes.size());
    if (mutation == Mutation::kOverwrite) bytes.push_back(op(abc::Opcode::kLDUNDEFINED));
    if (mutation == Mutation::kDebugger) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    if (mutation == Mutation::kDoublePop) bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    if (mutation == Mutation::kReturn) bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
    else bytes.insert(bytes.end(),{static_cast<std::uint8_t>(kThrow),static_cast<std::uint8_t>(kThrow >> kByteBits)});
    handlers.push_back({mutation == Mutation::kTyped ? std::optional{abc::CatchTypeIndex{0}} : std::nullopt,
        {begin},static_cast<std::uint32_t>(bytes.size()-begin)});
  }
  auto body = test::Body(bytes); body.tries = std::move(tries); body.handlers = std::move(handlers); return body;
}
}
int main(int argc, char** argv) {
  if (argc == 3) {
    auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
    const auto fingerprint = std::get<corpus::FileFingerprint>(file->Fingerprint());
    const auto header = std::get<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
    test::Check(header.checksum == fingerprint.adler32);
    const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
    const auto id = std::stoul(argv[2]); test::Check(id <= std::numeric_limits<std::uint32_t>::max());
    const auto method = std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& item) { return item.offset.value == id; });
    test::Check(method != index.methods.end() && method->code);
    const auto call = std::get<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers,method->code->arguments,abc::DecodeCallFlags(call.value)));
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code));
    const auto proof = EnvironmentCleanup::Build(body,header.profile,frame);
    test::Check(std::holds_alternative<EnvironmentCleanup>(proof));
    const auto& cleanup = std::get<EnvironmentCleanup>(proof);
    test::Check(cleanup.Handlers().size() == body.handlers.size());
    for (const auto& pop : cleanup.Pops())
      std::cout << "pop PC " << body.instructions[pop.instruction.value].offset.value
                << " allocation PC " << body.instructions[pop.allocation.value].offset.value << '\n';
    for (const auto& handler : cleanup.Handlers())
      std::cout << "handler " << handler.handler.value << " rethrow PC " << body.instructions[handler.rethrow.value].offset.value << '\n';
    return 0;
  }
  test::Check(argc == 1);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{false,false,false,false}));
  for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,2U,200U,201U,402U}) {
      const auto body = Body(count);
      const auto result = EnvironmentCleanup::Build(body,profile,frame);
      test::Check(std::holds_alternative<EnvironmentCleanup>(result));
      const auto& proof = std::get<EnvironmentCleanup>(result);
      test::Check(proof.Handlers().size() == count && proof.Pops().size() == count);
      for (unsigned i = 0; i < count; ++i) {
        test::Check(proof.Handlers()[i].pop_count == 1 && proof.Handlers()[i].first_pop == i);
        test::Check(body.instructions[proof.Pops()[i].allocation.value].decoded.opcode == abc::Opcode::kNEWLEXENV_IMM8);
        test::Check(body.instructions[proof.Pops()[i].instruction.value].offset == body.handlers[i].offset);
        test::Check(body.instructions[proof.Handlers()[i].rethrow.value].decoded.opcode == abc::Opcode::kTHROW_PREF_NONE);
      }
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
    }
    const auto nested_body = Body(1,Mutation::kDoublePop);
    const auto nested = std::get<EnvironmentCleanup>(EnvironmentCleanup::Build(nested_body,profile,frame));
    test::Check(nested.Handlers().size() == 1 && nested.Handlers()[0].pop_count == 2);
    test::Check(nested.Pops()[0].allocation.value > nested.Pops()[1].allocation.value);
    const auto bounded_body = Body(1);
    const auto measured = std::get<EnvironmentCleanup>(EnvironmentCleanup::Build(bounded_body,profile,frame));
    analysis::SemanticBudget exact; exact.work = measured.Work();
    test::Check(std::holds_alternative<EnvironmentCleanup>(EnvironmentCleanup::Build(bounded_body,profile,frame,exact)));
    --exact.work;
    test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(bounded_body,profile,frame,exact)));
    for (const auto mutation : {Mutation::kOverwrite,Mutation::kReturn,Mutation::kEntry,
        Mutation::kOuterPop,Mutation::kDebugger,Mutation::kTyped})
      test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(Body(1,mutation),profile,frame)));
    test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(Body(1),profile,frame,{0,1000})));
    test::Check(std::get<CleanupError>(std::get<CleanupFailure>(EnvironmentCleanup::Build(Body(1),profile,frame,{1024*1024,0}))) == CleanupError::kWorkLimit);
  }
}
