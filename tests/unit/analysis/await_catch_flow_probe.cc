// Read-only test boundary for original methods with an async reject wrapper.
#include "abcd/abc/metadata_index.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/analysis/await_catch_flow.h"
#include "abcd/analysis/await_catch.h"
#include "../../../src/source/async_failure_json.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
template<class T,class...U>T Take(std::variant<U...> v){if(auto*p=std::get_if<T>(&v))return std::move(*p);test::Check(false);std::abort();}
int main(int argc,char**argv) {
  test::Check(argc==3);
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto entity=std::stoul(argv[2]);
  const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](auto&m){return m.offset.value==entity;});
  test::Check(method!=index.methods.end()&&method->code);
  const auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code));
  const auto frame=std::uint64_t(method->code->registers)+method->code->arguments;
  test::Check(body.instructions.size()>=3);
  // Test oracle for these fixture wrappers only. No production admission or
  // completion proof is supplied by this read-only diagnostic executable.
  const auto& caught=body.instructions[body.instructions.size()-3];
  test::Check(caught.decoded.opcode==abc::Opcode::kSTA_V8);
  test::Check(body.instructions[body.instructions.size()-2].decoded.opcode==abc::Opcode::kASYNCFUNCTIONREJECT_V8);
  const auto regions=Take<std::vector<analysis::AwaitRegion>>(analysis::FindAwaitRegions(body,header.profile,frame,analysis::SemanticBudget{}.bytes));
  const auto proof=Take<analysis::AwaitCatchFlowProof>(analysis::ProveAwaitCatchFlow(body,regions,header.profile,frame,caught.offset));
  Take<analysis::CatchFlowCost>(analysis::VerifyAwaitCatchFlow(body,regions,header.profile,frame,caught.offset,proof));
  auto diagnostic=std::make_shared<analysis::AwaitCatchFlowDiagnostic>();diagnostic->verified=true;diagnostic->queries=proof.queries;
  source::AsyncCompletionEvidence evidence;evidence.check=source::AsyncCompletionCheck::kCatchInput;
  evidence.location.method=method->offset;evidence.catch_flow=std::move(diagnostic);
  auto out=source::AsyncCompletionJson(evidence);
  const auto fast=analysis::ProveAwaitCatchInputs(body,regions,header.profile,frame,caught.offset);
  out["fast_proof"]=std::holds_alternative<analysis::AwaitCatchProof>(fast);
  if(const auto* f=std::get_if<analysis::AwaitCatchFailure>(&fast)){
    out["fast_reason"]=static_cast<unsigned>(f->kind);
    if(f->observation)out["fast_prefix"]=static_cast<unsigned>(f->observation->prefix);
  }
  out["peak_bytes"]=proof.peak_bytes;out["work"]=proof.work;out["states"]=proof.states.size();
  std::cout<<out<<'\n';
}
