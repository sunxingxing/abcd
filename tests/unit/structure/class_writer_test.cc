#include <unistd.h>

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sstream>

#include "abcd/corpus/abc_file.h"
#include "record_text.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  for (const bool bad_key : {false, true}) {
    std::vector<std::uint8_t> bytes(512);
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (std::size_t i = 0; i < sizeof(value); ++i)
        bytes[at + i] = value >> (8 * i);
    };
    // Regional pool: constructor, malformed buffer, valid buffer.
    word(64, 100);
    word(68, 128);
    word(72, 160);
    word(76, 80);
    word(88, 84);
    bytes[80] = 3;
    bytes[81] = 'x';
    bytes[82] = 0;
    bytes[84] = 1;
    bytes[85] = 0xff;
    bytes[86] = 0;
    const auto literal = [&](std::size_t at, std::uint32_t key,
                             std::uint32_t value) {
      word(at, 6);
      bytes[at + 4] = 5;
      word(at + 5, key);
      bytes[at + 9] = 5;
      word(at + 10, value);
      bytes[at + 14] = 2;
      word(at + 15, 0);
    };
    literal(128, bad_key ? 84 : 80, bad_key ? 80 : 84);
    literal(160, 80, 80);
    constexpr std::uint8_t kDefineClass = 0x35, kReturnUndefined = 0x65;
    std::vector<std::uint8_t> code{1,
                                   0,
                                   24,
                                   0,
                                   kDefineClass,
                                   0,
                                   0,
                                   0,
                                   1,
                                   0,
                                   0,
                                   0,
                                   0,
                                   kDefineClass,
                                   0,
                                   0,
                                   0,
                                   2,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0x7a,
                                   0,
                                   3,
                                   0,
                                   0,
                                   kReturnUndefined};
    constexpr std::uint8_t kStoreOwnName = 0x7a;
    constexpr std::size_t kOwnSiteOffset = 22;
    code.insert(code.begin() + kOwnSiteOffset,
                {kStoreOwnName, 0, 7, 0, 0, kStoreOwnName, 0, 6, 0, 0});
    code[2] = 34;
    constexpr std::uint8_t kReadGlobalName = 0x3f;
    code.insert(code.end() - 1, {kReadGlobalName, 0, 7, 0, kReadGlobalName, 0,
                                 6, 0, kReadGlobalName, 0, 3, 0});
    code[2] = 46;
    constexpr std::uint8_t kAssignProperty = 0x43;
    code.insert(code.end() - 1, {kAssignProperty, 0, 3, 0, 0});
    code[2] = 51;
    code.insert(code.begin() + 4, {0x70, 0x61, 0});
    constexpr std::size_t kCodeHeaderBytes = 4;
    constexpr std::uint16_t kSendableReserved = 0x1234;
    constexpr std::uint16_t kEncodedLegacyLiteral = 0x5678;
    code.insert(code.end() - 1,
                {0xfb, 7, 0x34, 0x12, 0, 0, 2, 0, 0, 0, 0,
                 0xfc, 0x12, 0, 0, 0x78, 0x56, 0, 0, 0, 0});
    code[2] = static_cast<std::uint8_t>(code.size() - kCodeHeaderBytes);
    std::copy(code.begin(), code.end(), bytes.begin() + 200);
    char path[] = "/tmp/abcd-class-writer-XXXXXX";
    const auto fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) ==
          static_cast<ssize_t>(bytes.size()));
    close(fd);
    auto opened = corpus::AbcFile::Open(path);
    const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
    abc::Header header;
    header.kind = abc::AbcKind::kDynamic;
    header.profile = abc::ProfileId::kDynamic1206;
    header.layout = abc::DynamicLayout{};
    header.file_size = bytes.size();
    abc::MetadataIndex index;
    abc::MethodInfo caller, ctor;
    caller.offset = {96};
    caller.code = abc::CodeInfo{{200}, {204}, 1, 0,
                              static_cast<std::uint32_t>(code.size() -
                                                         kCodeHeaderBytes), 0};
    ctor.offset = {100};
    index.methods = {caller, ctor};
    abc::IndexRegion region;
    region.start = 60;
    region.end = bytes.size();
    region.methods = {7, 64};
    index.regions.push_back(region);
    const auto lookup =
        std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
    structure::RecordText text(file, header, {});
    std::ostringstream out;
    out << "{\"known_module\":true";
    abc::FrameLayout class_frame;
    class_frame.registers = 1;
    structure::WriteClassSites(text, index, lookup, caller, out, &class_frame);
    out << '}';
    const auto json = nlohmann::json::parse(out.str());
    Check(json["known_module"] == true);
    Check(json["class_sites"].size() == 4);
    Check(json["class_sites"][0].contains("error"));
    Check(!json["class_sites"][0].contains("constructor"));
    Check(json["class_sites"][1]["constructor"] == 100);
    Check(json["class_sites"][1]["heritage"] == "absent");
    Check(json["class_sites"][2]["error"]["domain"] == "class_profile");
    Check(json["class_sites"][3]["error"]["domain"] == "class_profile");
    auto v7_header = header;
    v7_header.profile = abc::ProfileId::kDynamic1301;
    structure::RecordText v7_text(file, v7_header, {});
    std::ostringstream v7_out;
    v7_out << "{\"known_module\":true";
    structure::WriteClassSites(v7_text, index, lookup, caller, v7_out,
                               &class_frame);
    v7_out << '}';
    const auto v7 = nlohmann::json::parse(v7_out.str());
    Check(v7["class_sites"][2]["form"] == "sendable");
    Check(v7["class_sites"][2]["constructor"] == 100);
    Check(v7["class_sites"][2]["literal_pool_index"] == 2);
    Check(v7["class_sites"][2]["buffer"] == 160);
    Check(v7["class_sites"][2]["reserved_operand"] ==
          kSendableReserved);
    Check(v7["class_sites"][2]["properties"] == nullptr);
    Check(v7["class_sites"][2]["source_admission"] == false);
    Check(v7["class_sites"][3]["form"] == "legacy");
    Check(v7["class_sites"][3]["constructor"] == 100);
    Check(v7["class_sites"][3]["encoded_literal"] ==
          kEncodedLegacyLiteral);
    Check(v7["class_sites"][3]["literal_pool_index"] == 1);
    Check(v7["class_sites"][3]["buffer"] == 128);
    Check(v7["class_sites"][3]["lexical_environment_register"] == 0);
    Check(v7["class_sites"][3]["properties"] == nullptr);
    Check(v7["class_sites"][3]["source_admission"] == false);
    abc::FrameLayout empty_frame;
    std::ostringstream invalid_out;
    invalid_out << "{\"known_module\":true";
    structure::WriteClassSites(v7_text, index, lookup, caller, invalid_out,
                               &empty_frame);
    invalid_out << '}';
    const auto invalid = nlohmann::json::parse(invalid_out.str());
    Check(invalid["class_sites"][2]["error"]["domain"] == "define");
    Check(invalid["class_sites"][3]["error"]["domain"] == "define");
    structure::StructureBudget denied;
    denied.heritage_bytes = 0;
    structure::RecordText denied_text(file, header, denied);
    std::ostringstream denied_out;
    denied_out << "{\"limited\":true";
    structure::WriteClassSites(denied_text, index, lookup, caller, denied_out,
                               &class_frame);
    denied_out << '}';
    const auto denied_json = nlohmann::json::parse(denied_out.str());
    Check(denied_json.contains("heritage_scan_error"));
    Check(denied_json["class_sites"][1]["heritage"] == "unknown");
    Check(json["class_sites"][1]["properties"].size() == 1);
    Check(json["property_writes"].size() == 4);
    Check(json["property_writes"][3]["kind"] == "assignment");
    Check(json["property_writes"][0].contains("error"));
    Check(json["property_writes"][1].contains("error"));
    Check(!json["property_writes"][1].contains("name"));
    Check(json["property_writes"][2]["name"] == "x");
    Check(json["property_writes"][2]["kind"] == "own_store");
    Check(json["property_writes"][2]["instance"] == nullptr);
    Check(json["global_accesses"].size() == 3);
    Check(json["global_accesses"][0].contains("error"));
    Check(json["global_accesses"][1].contains("error"));
    Check(json["global_accesses"][2]["operation"] == "name_read");
    Check(json["global_accesses"][2]["name"] == "x");
    const std::vector<std::uint8_t> mixed{kStoreOwnName,
                                          0,
                                          3,
                                          0,
                                          0,
                                          0xdb,
                                          0,
                                          6,
                                          0,
                                          0,
                                          0x00,
                                          0x61,
                                          0,
                                          kAssignProperty,
                                          0,
                                          3,
                                          0,
                                          0,
                                          kReturnUndefined};
    abc::MethodBody body;
    for (std::size_t at = 0; at < mixed.size();) {
      const auto instruction = std::get<abc::DecodedInstruction>(
          abc::DecodeInstruction(std::span(mixed).subspan(at), header.profile));
      body.instructions.push_back(
          {{static_cast<std::uint32_t>(at)}, instruction, {}});
      at += instruction.size;
    }
    abc::FrameLayout frame;
    frame.registers = 1;
    frame.this_value = abc::RegisterId{0};
    std::ostringstream mixed_out;
    mixed_out << "{\"mixed\":true";
    structure::WritePropertySites(text, index, caller, body, mixed_out, &frame);
    mixed_out << '}';
    const auto mixed_json = nlohmann::json::parse(mixed_out.str());
    Check(mixed_json["property_writes"][0]["receiver_origin"] == "entry_this");
    Check(mixed_json["property_writes"][1].contains("error"));
    Check(mixed_json["property_writes"][2]["receiver_origin"] == "unknown");
    Check(mixed_json["property_writes"][2]["kind"] == "assignment");
    unlink(path);
  }
}
