#include "../../../src/source/concurrent_async.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
using namespace abcd;
using namespace source;
using test::Check;
int main() {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  const auto profile = abc::ProfileId::kDynamic1206;
  // These raw tails deliberately have no implicit catch: notification proof
  // alone is not permission to normalize/publish an async function.
  auto body = test::Body({0x00, 0xcd,0, 0xfb,0, 0x64, 0x61,1, 0xce,0, 0xfb,0, 0x64});
  auto proof = std::get<AsyncNotifications>(ProveAsyncNotifications(body,frame,profile));
  Check(proof.ports.size()==2);
  Check(proof.ports[0].kind==analysis::AsyncSettlement::kResolve);
  Check(proof.ports[1].kind==analysis::AsyncSettlement::kReject);
  Check(std::holds_alternative<std::uint64_t>(VerifyAsyncNotifications(body,frame,profile,proof)));
  auto wrong=proof;wrong.ports[1].notification=wrong.ports[0].notification;
  Check(std::holds_alternative<ConcurrentFailure>(VerifyAsyncNotifications(body,frame,profile,wrong)));
  wrong=proof;wrong.members[proof.ports[0].notification.value].reset();
  Check(std::holds_alternative<ConcurrentFailure>(VerifyAsyncNotifications(body,frame,profile,wrong)));
  Check(std::holds_alternative<AsyncCompletionFailure>(NormalizeConcurrentAsync(body,frame,profile)));
  auto reject=[&](std::initializer_list<std::uint8_t> bytes) {
    Check(std::holds_alternative<ConcurrentFailure>(ProveAsyncNotifications(test::Body(bytes),frame,profile)));
  };
  reject({0x00,0xcd,0,0x64}); // missing success notification
  reject({0x00,0xce,0,0x64}); // missing rejection notification
  reject({0x00,0xcd,0,0xfb,0,0xfb,0,0x64});
  reject({0x00,0xce,0,0xfb,0,0xfb,0,0x64});
  reject({0x00,0xfb,0,0xcd,0,0x64}); // before settlement
  reject({0x00,0xfb,0,0xce,0,0x64});
  reject({0x00,0xcd,0,0x00,0xfb,0,0x64}); // wrong Promise value
  reject({0x00,0xce,0,0x00,0xfb,0,0x64});
  reject({0x00,0xcd,0,0xfb,0,0x00,0x64}); // return differs from notify
  reject({0x00,0xcd,0,0xfb,0,0x42,0,0,0,0x64}); // getter after notify
  // Copy/register variants: data-flow proves the identity, not adjacency.
  auto copies=test::Body({0x00,0xcd,0,0x61,1,0x60,1,0xfb,0,0x61,1,0x60,1,0x64});
  Check(std::holds_alternative<AsyncNotifications>(ProveAsyncNotifications(copies,frame,profile)));
  // Jump/block split preserving the same promise-valued completion port.
  auto jump=test::Body({0x00,0xcd,0,0x4d,2,0xfb,0,0x64});
  Check(std::holds_alternative<AsyncNotifications>(ProveAsyncNotifications(jump,frame,profile)));
  auto complete=[](unsigned copies) {
    std::vector<std::uint8_t> bytes{0xae,0x61,0,0x00,0xcd,0};
    for(unsigned i=0;i<copies;++i)bytes.insert(bytes.end(),{0x61,1,0x60,1});
    bytes.insert(bytes.end(),{0xfb,0,0x64});
    const auto handler=static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(),{0x61,1,0xce,0,0xfb,0,0x64});
    auto raw=test::Body(bytes);raw.tries={{{3},handler-3,0,1}};raw.handlers={{{},{handler},7}};return raw;
  };
  auto full=complete(0);
  auto normalized=std::get<ConcurrentAsyncBody>(NormalizeConcurrentAsync(full,frame,profile));
  Check(normalized.body.instructions.size()==2 && normalized.origins.size()==2);
  Check(normalized.origins[1]==abc::CodeOffset{8});
  const auto input=std::get<analysis::SemanticMethod>(analysis::BuildSemanticMethod(normalized.body,profile,frame.registers));
  const auto semantic=std::get<analysis::SemanticProof>(analysis::VerifySemanticSource(normalized.body,input,frame.registers,frame.locals));
  auto graph=std::get<analysis::ValueProgram>(analysis::ImportNormalValues(normalized.body,input,semantic,frame));
  for(std::size_t i=0;i<graph.origins.size();++i)graph.origins[i].original=normalized.origins[i];
  ConcurrentAsyncEvidence evidence{full,frame,profile};
  Check(std::holds_alternative<std::uint64_t>(VerifyConcurrentAsync(evidence,graph)));
  auto changed=graph;changed.origins[1].original=abc::CodeOffset{0};
  Check(std::holds_alternative<ConcurrentFailure>(VerifyConcurrentAsync(evidence,changed)));
  auto bad_state=full;bad_state.instructions[7].decoded.operands[0].raw_bits=1;
  Check(std::holds_alternative<AsyncCompletionFailure>(NormalizeConcurrentAsync(bad_state,frame,profile)));
  auto bad_promise=full;bad_promise.instructions[1].decoded.operands[0].raw_bits=1;
  Check(std::holds_alternative<AsyncCompletionFailure>(NormalizeConcurrentAsync(bad_promise,frame,profile)));
  for (unsigned n : {200u,201u,400u,800u}) {
    std::vector<std::uint8_t> bytes{0x00,0xcd,0};
    for(unsigned i=0;i<n;++i)bytes.insert(bytes.end(),{0x61,1,0x60,1});
    bytes.insert(bytes.end(),{0xfb,0,0x64});
    auto large=test::Body(bytes);
    auto p=std::get<AsyncNotifications>(ProveAsyncNotifications(large,frame,profile));
    Check(p.work<50*large.instructions.size());
    Check(std::holds_alternative<AsyncNotifications>(ProveAsyncNotifications(large,frame,profile,{p.allocated_bytes,p.work})));
    Check(std::get<ConcurrentFailure>(ProveAsyncNotifications(large,frame,profile,{p.allocated_bytes-1,p.work})).kind==ConcurrentError::kResourceLimit);
    Check(std::get<ConcurrentFailure>(ProveAsyncNotifications(large,frame,profile,{p.allocated_bytes,p.work-1})).kind==ConcurrentError::kWorkLimit);
    auto raw=complete(n);
    const auto normalized=std::get<ConcurrentAsyncBody>(NormalizeConcurrentAsync(raw,frame,profile));
    Check(normalized.body.instructions.size()==2);
    // Exact delegated-normalizer boundary: locate its minimum byte allowance,
    // then check that one fewer byte refuses before any uncharged allocation.
    std::size_t low=0,high=normalized.allocated_bytes;
    while(low<high) {
      const auto middle=low+(high-low)/2;
      auto attempt=NormalizeConcurrentAsync(raw,frame,profile,{middle,normalized.work});
      if(std::holds_alternative<ConcurrentAsyncBody>(attempt))high=middle;else low=middle+1;
    }
    Check(std::holds_alternative<ConcurrentAsyncBody>(NormalizeConcurrentAsync(raw,frame,profile,{low,normalized.work})));
    Check(!std::holds_alternative<ConcurrentAsyncBody>(NormalizeConcurrentAsync(raw,frame,profile,{low-1,normalized.work})));
    Check(std::get<ConcurrentFailure>(NormalizeConcurrentAsync(raw,frame,profile,{normalized.allocated_bytes,normalized.work-1})).kind==ConcurrentError::kWorkLimit);
  }
}
