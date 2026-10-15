// Test-only boundary: examine original ABC metadata, not printed source.
#include "../../../src/source/sendable_metadata.h"
#include "../../../src/source/sendable_failure_json.h"
#include <iostream>
#include <cstdlib>
using namespace abcd;
template<class T,class... V> static T Take(std::variant<T,V...> v) {
  if (!std::holds_alternative<T>(v)) std::abort();
  return std::get<T>(std::move(v));
}
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  auto fingerprint=Take<corpus::FileFingerprint>(file->Fingerprint());
  auto header=Take<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
  auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const abc::EntityOffset method{static_cast<std::uint32_t>(std::stoul(argv[2]))};
  const abc::CodeOffset pc{static_cast<std::uint32_t>(std::stoul(argv[3]))};
  const auto pos=lookup.Find(method);if(!pos)return 2;
  const auto record=index.methods[pos->value].owner_offset;
  auto r=source::ReadSendableMetadata(*file,header,index,lookup,record,method,pc);
  if(const auto* e=std::get_if<source::SendableFailure>(&r)){std::cout<<source::SendableJson(*e)<<'\n';return 1;}
  const auto& m=std::get<source::SendableMetadata>(r);
  if(source::VerifySendableMetadata(m,*file,header,index,lookup,record,method,pc))return 3;
  auto j=source::SendableJson(m.location);j["fields"]=nlohmann::json::array();
  for(const auto& f:m.fields.fields) {
    const auto key=std::get<abc::EntityOffset>(f.key.value);
    const auto name=Take<std::u16string>(abc::ReadStringEntity(*file,header,key));
    j["fields"].push_back({{"key",key.value},{"name",std::string(name.begin(),name.end())},
        {"mask",f.type.bits},{"placement",static_cast<unsigned>(f.placement)}});
  }
  j["charged_bytes"]=m.allocated_bytes;j["work"]=m.work;
  std::cout<<j<<'\n';
}
