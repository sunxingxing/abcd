// Test-only independent ABC oracle; no output syntax is trusted as metadata.
#include "../../../src/source/sendable_metadata.h"
#include "../../../src/source/function_file.h"
#include "../../../src/source/runtime_name.h"
#include "abcd/abc/record_attributes.h"
#include <nlohmann/json.hpp>
#include <iostream>
using namespace abcd;
template<class T,class... V>T Take(std::variant<V...> v){if(auto* t=std::get_if<T>(&v))return std::move(*t);std::cerr<<"variant="<<v.index()<<'\n';std::abort();}
static std::string Text(const std::u16string& s){return {s.begin(),s.end()};}
int main(int argc,char** argv) {
  if(argc!=2 && argc!=3)return 2;
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto packaging=Take<abc::FilePackaging>(abc::DetectFilePackaging(*file,header,index));
  nlohmann::json all=nlohmann::json::array();
  for(const auto& record:index.records) {
    if(argc==3 && record.offset.value!=std::stoul(argv[2]))continue;
    nlohmann::json r;r["classes"]=nlohmann::json::array();
    auto context=Take<source::RecordModuleContext>(source::ReadRecordModuleContext(*file,header,index,record,packaging));
    const auto module=context.ForOwner(*file,record.offset);if(!module)continue;
    const auto* flags=context.Attributes(*file,record.offset);r["shared"]=flags && flags->shared.value_or(false);r["sendable_environments"]=0;
    r["requests"]=nlohmann::json::array();r["exports"]=nlohmann::json::array();
    for(const auto& request:module->requests)r["requests"].push_back(Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,request))));
    for(std::size_t slot=0;slot<module->local_exports.size();++slot){const auto& e=module->local_exports[slot];r["exports"].push_back({{"slot",slot},{"local",Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,e.local)))},{"export",Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,e.exported)))}});}
    for(std::size_t i=record.first_method;i<record.first_method+record.method_count;++i) {
      const auto& method=index.methods[i];if(!method.code)continue;
      const auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method.code));
      for(const auto& instruction:body.instructions) {
        switch(instruction.decoded.opcode) {
          case abc::Opcode::kCALLRUNTIME_NEWSENDABLEENV_PREF_IMM8:
          case abc::Opcode::kCALLRUNTIME_WIDENEWSENDABLEENV_PREF_IMM16:r["sendable_environments"]=r["sendable_environments"].get<unsigned>()+1;break;
          default:break;
        }
        const auto lifted=analysis::LiftClassCreation(instruction.decoded);
        const auto* c=std::get_if<analysis::ClassCreation>(&lifted);
        if(!c || c->kind!=abc::ClassKind::kSendable)continue;
        const auto m=Take<source::SendableMetadata>(source::ReadSendableMetadata(*file,header,index,lookup,record.offset,method.offset,instruction.offset));
        nlohmann::json j;j["kind"]=static_cast<unsigned>(c->kind);j["length"]=c->length;j["fields"]=nlohmann::json::array();j["members"]=nlohmann::json::array();
        j["instance_count"]=m.fields.instance_count;
        for(const auto& field:m.fields.fields)j["fields"].push_back({{"name",Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,std::get<abc::EntityOffset>(field.key.value))))},{"mask",field.type.bits},{"placement",static_cast<unsigned>(field.placement)}});
        for(const auto& member:m.members.properties) {
          const auto& target=std::get<abc::ClassMethod>(member.value);
          j["members"].push_back({{"name",Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,std::get<abc::EntityOffset>(member.key.value))))},{"kind",static_cast<unsigned>(target.kind)},{"length",target.length},{"placement",static_cast<unsigned>(member.placement)}});
        }
        const auto ctor=*lookup.Find(*m.location.constructor);
        const auto attrs=Take<abc::MethodAttributes>(abc::DecodeMethodAttributes(index.methods[ctor.value].access_flags,header.profile));
        j["constructor_sendable_flag"]=attrs.sendable;
        j["constructor_kind"]=static_cast<unsigned>(attrs.kind);
        auto identity=source::MakeRouteInputIdentity(hash,header);identity.contract=source::SourceContract::kLanguageSourceV3;
        const auto entry=Take<source::FileFunction>(source::ReadFunctionValues(*file,header,index,{static_cast<std::uint32_t>(i)},identity,source::InputCaptureMode::kConditional));
        const auto& graph=entry.values.graph;
        for(const auto& op:graph.ops)if(const auto* created=std::get_if<analysis::ValueClassCreation>(&op.operation);created && created->kind==abc::ClassKind::kSendable) {
          const auto value=std::get<analysis::ValueId>(graph.uses[op.first_use.value].operand);
          j["heritage"]=graph.values[value.value].domain!=analysis::ValueKinds::Hole();
        }
        j["sets"]=nlohmann::json::array();j["sendable_environments"]=0;
        const auto ctorbody=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*index.methods[ctor.value].code));
        for(const auto& site:ctorbody.instructions) {
          const auto lifted=analysis::LiftSemanticInstruction(site,header.profile,index.methods[ctor.value].code->registers+index.methods[ctor.value].code->arguments);
          if(const auto* semantic=std::get_if<analysis::SemanticInstruction>(&lifted))if(const auto* write=std::get_if<analysis::NamedWrite>(&semantic->operation)) {
            const auto key=Take<abc::PoolReference>(abc::ResolvePoolReference(*file,header,index,index.methods[ctor.value].offset,write->name,abc::PoolEntityKind::kString));
            j["sets"].push_back(Text(Take<std::u16string>(abc::ReadStringEntity(*file,header,key.offset))));
          }
          switch(site.decoded.opcode) {
            case abc::Opcode::kCALLRUNTIME_NEWSENDABLEENV_PREF_IMM8:
            case abc::Opcode::kCALLRUNTIME_WIDENEWSENDABLEENV_PREF_IMM16:j["sendable_environments"]=j["sendable_environments"].get<unsigned>()+1;break;
            default:break;
          }
        }
        r["classes"].push_back(std::move(j));
      }
    }
    if(!r["classes"].empty())all.push_back(std::move(r));
  }
  std::cout<<all<<'\n';
}
