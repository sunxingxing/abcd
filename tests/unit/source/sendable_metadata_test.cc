#include "../../../src/source/sendable_metadata.h"
#include "../../../src/source/sendable_failure_json.h"
#include "../../../tools/record_failure_json.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
static void Check(bool b, std::source_location at = std::source_location::current()) { if (!b) { std::cerr << at.line() << ": check failed\n"; std::abort(); } }
template<class T, class... V> T Take(std::variant<T,V...> v) { Check(std::holds_alternative<T>(v)); return std::get<T>(std::move(v)); }
static void JsonPaths(SendableFailure at) {
  Check(ParseSendable(nlohmann::json::parse(SendableJson(at).dump())) == at);
  RouteFailure route{RouteFailureClass::kUnproved, ValueRouteError::kTreeStage}; route.sendable = at;
  route.method = at.method; route.pc = at.pc;
  MethodRouteReceipt r; r.identity.file_size = 4096; r.identity.file_hash.bytes[0] = 1;
  r.identity.abc_version = {24,0,0,0}; r.identity.profile = abc::ProfileId::kDynamic2400;
  r.method = at.method; r.record = at.record; r.stage = SourceStage::kValuesVerified; r.work_revision = 0;
  r.failure = route;
  auto text = Take<std::string>(SerializeRouteReceipt(r));
  Check(Take<MethodRouteReceipt>(ParseRouteReceipt(text)).failure->sendable == at);
  RecordProgramFailure entry{RecordProgramError::kClass, {}}; entry.sendable = at;
  nlohmann::json a,b;
  tools::DescribeRecordFailure(RouteRefusal{route},a); tools::DescribeRecordFailure(entry,b);
  Check(a["source_failure"]["sendable"] == b["source_sendable"]);
  Check(!tools::RecordFailureIsFatal(entry,false));
  Check(tools::RecordFailureIsFatal(entry,true));
  auto invalid=SendableJson(at);invalid["kind"]=UINT32_MAX;Check(!ParseSendable(invalid));
  invalid=SendableJson(at);invalid["pc"]=-1;Check(!ParseSendable(invalid));
}
int main(int argc,char** argv) {
  Check(argc == 2 || argc == 3);
  static_assert(std::is_trivially_copyable_v<SendableFailure>);
  static_assert(std::is_trivially_copyable_v<SharedModuleFailure>);
  for (unsigned i=0;i<=static_cast<unsigned>(SendableError::kWorkLimit);++i) {
    SendableFailure at; at.kind=static_cast<SendableError>(i);at.method={100};at.record={80};
    JsonPaths(at);at.pc=abc::CodeOffset{0};at.working_pc=abc::CodeOffset{7};at.field=0;at.mask=0;
    at.cause=abc::SendableFieldError::kMask;JsonPaths(at);
  }
  for(unsigned i=0;i<=static_cast<unsigned>(SharedModuleError::kWorkLimit);++i) {
    SharedModuleFailure at;at.kind=static_cast<SharedModuleError>(i);at.record={80};
    for(int presence=0;presence<3;++presence) {
      at.shared=presence==0?std::optional<bool>{}:std::optional<bool>{presence==2};
      at.module=abc::EntityOffset{100};at.requests=1;at.imports=1;at.local_exports=1;
      at.cause=abc::ModuleError::kRequestIndex;
      Check(ParseSharedModule(nlohmann::json::parse(SharedModuleJson(at).dump()))==at);
      MethodRouteReceipt receipt;receipt.identity.file_size=4096;receipt.identity.file_hash.bytes[0]=1;
      receipt.identity.abc_version={24,0,0,0};receipt.identity.profile=abc::ProfileId::kDynamic2400;
      receipt.record=at.record;receipt.stage=SourceStage::kValuesVerified;receipt.work_revision=0;
      receipt.failure=RouteFailure{RouteFailureClass::kUnproved,ValueRouteError::kTreeStage};receipt.failure->shared_module=at;
      Check(Take<MethodRouteReceipt>(ParseRouteReceipt(Take<std::string>(SerializeRouteReceipt(receipt)))).failure->shared_module==at);
      auto invalid=SharedModuleJson(at);invalid["kind"]=UINT32_MAX;Check(!ParseSharedModule(invalid));
      invalid=SharedModuleJson(at);invalid["shared"]=1;Check(!ParseSharedModule(invalid));
    }
  }
  for(const SendableCause cause : std::array<SendableCause,6>{abc::MetadataError::kBadString,
      abc::LiteralError::kReference,abc::ClassBufferError::kSplit,abc::SendableFieldError::kMask,
      abc::BodyError::kCodeRange,analysis::CreationError::kMalformedInstruction}) {
    SendableFailure at;at.method={100};at.record={80};at.cause=cause;JsonPaths(at);
  }
  unsigned classes=0;
  for (int argument=1;argument<argc;++argument) {
    auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[argument]));
    auto fingerprint=Take<corpus::FileFingerprint>(file->Fingerprint());
    auto header=Take<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
    auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
    auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
    for (const auto& method:index.methods) {
      if (!method.code) continue;
      auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method.code));
      for (const auto& instruction:body.instructions) {
        auto lifted=analysis::LiftClassCreation(instruction.decoded);
        const auto* c=std::get_if<analysis::ClassCreation>(&lifted);
        if (!c || c->kind!=abc::ClassKind::kSendable) continue;
        ++classes;
        const auto read=[&](analysis::SemanticBudget b=analysis::SemanticBudget{}) { return ReadSendableMetadata(*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset,b); };
        const auto original=Take<SendableMetadata>(read());
        Check(original.members.kind==abc::ClassKind::kSendable);
        Check(original.location.pool==c->literal && original.location.literal && original.location.field_types);
        Check(!VerifySendableMetadata(original,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset));
        JsonPaths(original.location);
        // Nested decoders also need transient string/boundary storage. Find
        // the actual admission boundary; retained/cumulative bytes are not a
        // claim that the nested peak reservation has disappeared.
        std::size_t low=0, high=original.allocated_bytes*2+4096;
        Check(std::holds_alternative<SendableMetadata>(read({high,original.work})));
        while(low+1<high) {
          const auto mid=low+(high-low)/2;
          if(std::holds_alternative<SendableMetadata>(read({mid,original.work}))) high=mid;
          else low=mid;
        }
        Check(std::holds_alternative<SendableMetadata>(read({high,original.work})));
        Check(std::get<SendableFailure>(read({high-1,original.work})).kind==SendableError::kResourceLimit);
        Check(std::get<SendableFailure>(read({high,original.work-1})).kind==SendableError::kWorkLimit);
        Check(std::get<SendableFailure>(ReadSendableMetadata(*file,header,index,lookup,{0},method.offset,instruction.offset)).kind==SendableError::kOwner);
        Check(std::get<SendableFailure>(ReadSendableMetadata(*file,header,index,lookup,method.owner_offset,method.offset,{instruction.offset.value+1})).kind==SendableError::kCreation);
        auto wrong_pool=index;
        for(auto& region:wrong_pool.regions)
          if(region.start<=method.offset.value && method.offset.value<region.end) region.methods.count=0;
        auto pool_failure=std::get<SendableFailure>(ReadSendableMetadata(*file,header,wrong_pool,lookup,method.owner_offset,method.offset,instruction.offset));
        Check(pool_failure.kind==SendableError::kPool && pool_failure.pool==c->constructor && pool_failure.pc==instruction.offset);
        auto changed=original;changed.creation.literal.value++;
        Check(VerifySendableMetadata(changed,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset).has_value());
        changed=original;changed.location.record.value++;
        Check(VerifySendableMetadata(changed,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset).has_value());
        changed=original;changed.members.field_types=abc::EntityOffset{0};
        Check(VerifySendableMetadata(changed,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset).has_value());
        if (!original.fields.fields.empty()) {
          changed=original;changed.fields.fields[0].type.bits^=1;
          Check(VerifySendableMetadata(changed,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset).has_value());
          changed=original;changed.fields.fields[0].placement=abc::SendableFieldPlacement::kStatic;
          Check(VerifySendableMetadata(changed,*file,header,index,lookup,method.owner_offset,method.offset,instruction.offset).has_value());
        }
        std::cout << method.offset.value << '@' << instruction.offset.value << " fields=" << original.fields.fields.size() << " instance=" << original.fields.instance_count << '\n';
      }
    }
    for (const auto& record:index.records) {
      auto context=Take<RecordModuleContext>(ReadRecordModuleContext(*file,header,index,record,abc::FilePackaging::kMerged));
      Check(context.Attributes(*file,record.offset));
      Check(!context.Attributes(*file,{0}));
      auto shared=InspectSharedModule(context,*file,record.offset,SourceContract::kLanguageSourceV3);
      if (!context.ForOwner(*file,record.offset)) { Check(shared.kind==SharedModuleError::kOwner); continue; }
      Check(shared.kind==SharedModuleError::kOutputPolicy);
      Check(ParseSharedModule(nlohmann::json::parse(SharedModuleJson(shared).dump()))==shared);
      RouteFailure route{RouteFailureClass::kUnproved,ValueRouteError::kTreeStage};route.shared_module=shared;
      RecordProgramFailure entry{RecordProgramError::kModule,{}};entry.shared_module=shared;
      nlohmann::json a,b;tools::DescribeRecordFailure(RouteRefusal{route},a);tools::DescribeRecordFailure(entry,b);
      Check(a["source_failure"]["shared_module"]==b["source_shared_module"]);
      if (argument==2) {Check(shared.shared==true && shared.requests==1 && shared.imports==1 && shared.local_exports==1);Check(!context.OrdinarySynchronous(*file,record.offset));}
      if (argument==1) Check(shared.shared==false);
    }
  }
  Check(classes==(argc==2?1u:2u));
  std::cout << "metadata: original occurrence/pool/owner, independent mutations, exact budgets, both JSON paths; publication remains gated\n";
}
