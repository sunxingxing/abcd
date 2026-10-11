#include "../../../src/source/family_emission.h"
#include "../../../src/source/family_route_failure.h"
#include "source/preserved_completions.h"
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <algorithm>
#include <sstream>
namespace {
using namespace abcd;
using namespace abcd::source;
enum class Call { kPositional, kSpread, kRest };
template<class T,class... Alternatives>
T Take(std::variant<Alternatives...> result,std::source_location at=std::source_location::current()) {
  if(auto* value=std::get_if<T>(&result))return std::move(*value);
  std::cerr<<"constructor fixture "<<at.line()<<" alternative "<<result.index()<<'\n';
  std::visit([](const auto& error) {
    if constexpr(std::is_enum_v<std::decay_t<decltype(error)>>)std::cerr<<unsigned(error)<<'\n';
  },result);
  std::abort();
}
std::string Run(abc::ProfileId profile,Call call,unsigned copies,bool split,bool guards) {
  const auto op=[](abc::Opcode value){return static_cast<std::uint8_t>(value);};
  constexpr unsigned kPool=64,kLiteral=128,kCaller=256,kConstructor=300,kCallerCode=400,kCode=512;
  constexpr std::uint8_t kLocals=3,kArguments=4,kSelf=3,kParameter=6;
  std::vector<std::uint8_t> code;
  const auto guard=[&](std::uint8_t flag) {
    const auto wire=static_cast<std::uint16_t>(abc::Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8);
    code.insert(code.end(),{static_cast<std::uint8_t>(wire),static_cast<std::uint8_t>(wire>>8),flag});
  };
  if(call==Call::kRest)code.insert(code.end(),{op(abc::Opcode::kCOPYRESTARGS_IMM8),0,op(abc::Opcode::kSTA_V8),0});
  else code.insert(code.end(),{op(abc::Opcode::kMOV_V8_V8),0,kParameter});
  code.insert(code.end(),{op(abc::Opcode::kMOV_V8_V8),1,kSelf});
  for(unsigned i=0;i<copies;++i)code.insert(code.end(),{op(abc::Opcode::kMOV_V8_V8),2,1,op(abc::Opcode::kMOV_V8_V8),1,2});
  if(split)code.insert(code.end(),{op(abc::Opcode::kJMP_IMM8),2});
  if(guards){code.insert(code.end(),{op(abc::Opcode::kLDA_V8),kSelf+2});guard(1);}
  if(call==Call::kPositional)code.insert(code.end(),{op(abc::Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8),0,1,0});
  else code.insert(code.end(),{op(abc::Opcode::kLDA_V8),1,op(abc::Opcode::kSUPERCALLSPREAD_IMM8_V8),0,0});
  if(guards)guard(0);
  code.push_back(op(abc::Opcode::kRETURN));
  const std::uint8_t length=call==Call::kRest?0:1;
  const std::vector<std::uint8_t> caller{op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8),0,0,0,1,0,length,0,4,op(abc::Opcode::kRETURN)};
  constexpr unsigned kMaxCodeHeader=4*5; // Four u32 ULEB fields.
  std::vector<std::uint8_t> bytes(kCode+kMaxCodeHeader+code.size());
  const auto word=[&](unsigned at,unsigned value){for(unsigned i=0;i<sizeof(value);++i)bytes[at+i]=value>>(8*i);};
  word(kPool,kConstructor);word(kPool+sizeof(unsigned),kLiteral);
  word(kLiteral,2);bytes[kLiteral+sizeof(unsigned)]=static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  const auto body=[&](unsigned offset,unsigned locals,const std::vector<std::uint8_t>& instructions) {
    auto cursor=offset;
    for(unsigned value:{locals,unsigned(kArguments),static_cast<unsigned>(instructions.size()),0U})
      do {auto byte=static_cast<std::uint8_t>(value&0x7f);value>>=7;bytes[cursor++]=byte|(value?0x80:0);}while(value);
    std::copy(instructions.begin(),instructions.end(),bytes.begin()+cursor);
    return abc::CodeInfo{{offset},{cursor},locals,kArguments,static_cast<unsigned>(instructions.size()),0};
  };
  const auto caller_body=body(kCallerCode,1,caller),constructor_body=body(kCode,kLocals,code);
  char path[]="/tmp/abcd-preserved-constructor-XXXXXX";
  const int fd=mkstemp(path);test::PreservationCheck(fd>=0);
  test::PreservationCheck(write(fd,bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size())&&close(fd)==0);
  const auto opened=corpus::AbcFile::Open(path);
  const auto& file=*std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;header.profile=profile;header.kind=abc::AbcKind::kDynamic;
  switch(profile) {
    case abc::ProfileId::kDynamic1202:header.version={12,0,2,0};break;
    case abc::ProfileId::kDynamic1206:header.version={12,0,6,0};break;
    case abc::ProfileId::kDynamic1301:header.version={13,0,1,0};break;
    default:std::abort();
  }
  header.layout=abc::DynamicLayout{};header.file_size=bytes.size();
  abc::MetadataIndex index;index.regions.push_back({kCaller,kCode,{},{2,kPool},{},{}});
  index.methods.resize(2);
  index.methods[0].offset={kCaller};index.methods[0].owner_offset={80};
  index.methods[0].code=caller_body;
  auto& method=index.methods[1];method.offset={kConstructor};method.owner_offset={80};
  method.name="#scope#Derived^1";method.name_utf16_length=method.name.size();
  method.access_flags=static_cast<unsigned>(abc::FunctionKind::kBaseConstructor)<<8;
  method.code=constructor_body;
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file,header,index,lookup));
  const auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file,header,index,lookup));
  RouteInputIdentity identity;identity.profile=profile;identity.file_size=header.file_size;identity.abc_version=header.version;
  identity.contract=SourceContract::kLanguageNumericConstructorsV1;
  const auto creator=Take<FileFunction>(ReadFunctionValues(file,header,index,{0},identity,InputCaptureMode::kClosed));
  test::PreservationCheck(creator.values.classes.classes.size()==1);
  const FamilyDestination destination{Strictness::kStrict,nullptr,FunctionFragment::kClassMember};
  auto prepared=PreparedFamily::PrepareConstructor(file,header,index,lookup,roles,functions,creator,
      creator.values.classes.classes.front().operation,identity,destination);
  if(const auto* error=std::get_if<FamilyPrepareFailure>(&prepared)) {
    std::cerr<<"constructor preparation "<<error->cause.index()<<' '<<unsigned(FamilyRouteFailure(*error).failure_class)<<'\n';
    if(const auto* reason=std::get_if<FamilyPrepareError>(&error->cause))std::cerr<<"reason "<<unsigned(*reason)<<'\n';
  }
  test::PreservationCheck(std::holds_alternative<PreparedFamily>(prepared));
  auto family=std::get<PreparedFamily>(std::move(prepared));
  std::size_t calls=0;
  for(const auto& member:family.Family().members) {
    test::PreservationCheck(member.flow.original!=nullptr);
    calls+=test::CheckPreservedCompletions(member.tree,*member.flow.original);
  }
  test::PreservationCheck(calls==1);
  std::ostringstream output;
  const auto emitted=FamilyEmission::Write(std::move(family),destination,output,{0});
  test::PreservationCheck(std::holds_alternative<FamilyEmission>(emitted));
  for(const auto& receipt:std::get<FamilyEmission>(emitted).Receipts())
    test::PreservationCheck(receipt.coverage==Coverage::kComplete&&!receipt.legacy_writer_used);
  test::PreservationCheck(!file.CheckUnchanged()&&unlink(path)==0);
  return output.str();
}
}
int main() {
  nlohmann::json result=nlohmann::json::array();
  for(auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(auto call:{Call::kPositional,Call::kSpread,Call::kRest})for(bool split:{false,true})for(unsigned copies:{0U,201U})
      for(bool guards:{false,true})
        result.push_back({{"call",static_cast<unsigned>(call)},{"source",Run(profile,call,copies,split,guards)}});
  std::cout<<result.dump()<<'\n';
}
