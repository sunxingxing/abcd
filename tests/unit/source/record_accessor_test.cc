#include "abcd/source/record_program.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_class.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kRecord = 100;
  constexpr std::uint32_t kEntry = 120;
  constexpr std::uint32_t kConstructor = 200;
  constexpr std::uint32_t kGetter = 220;
  constexpr std::uint32_t kSetter = 240;
  constexpr std::uint32_t kMember = 260;
  constexpr std::uint32_t kCollision = 280;
  constexpr std::uint32_t kPool = 300;
  constexpr std::uint32_t kEntryCode = 400;
  constexpr std::uint32_t kClassLiteral = 600;
  constexpr std::uint32_t kClassName = 700;
  constexpr std::uint32_t kPrototypeName = 720;
  constexpr std::uint32_t kPropertyName = 740;
  constexpr std::uint32_t kMemberName = 760;
  constexpr std::uint32_t kConstructorCode = 800;
  constexpr std::uint32_t kGetterCode = 850;
  constexpr std::uint32_t kSetterCode = 900;
  constexpr std::uint32_t kMemberCode = 950;
  constexpr std::uint32_t kCollisionCode = 1000;
  constexpr std::size_t kFileBytes = 2048;
  constexpr std::uint32_t kCodeHeaderBytes = 4;
  constexpr std::uint8_t kEntryLocals = 8;
  constexpr std::uint8_t kHiddenArguments = 3;
  constexpr std::uint8_t kReturnUndefined = 0x65;
  std::vector<std::uint8_t> instructions;
  const auto append = [&](std::initializer_list<std::uint8_t> bytes) {
    instructions.insert(instructions.end(), bytes);
  };
  append({0x70, 0x61, 0});
  append({0x35, 0, 0, 0, 1, 0, 0, 0, 0});
  append({0x61, 1, 0x60, 1, 0x42, 0, 5, 0, 0x61, 2});
  append({0x60, 2, 0x34, 0, 2, 0, 0, 0x61, 3});
  append({0x3e, 4, 0, 0x61, 4, 0x00, 0x61, 5, 0x03});
  append({0xbc, 2, 4, 3, 5});
  append({0x60, 2, 0x34, 0, 3, 0, 1, 0x61, 3});
  append({0x3e, 4, 0, 0x61, 4, 0x00, 0x61, 5, 0x03});
  append({0xbc, 2, 4, 5, 3});
  append({0x60, 1, 0x7c, 0, kReturnUndefined});
  Check(instructions.size() < UINT8_MAX);
  std::array<std::uint8_t, kFileBytes> bytes{};
  const auto word = [&](std::uint32_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  for (const auto [slot, value] :
       std::array<std::pair<std::uint32_t, std::uint32_t>, 6>{
           {{0, kConstructor}, {1, kClassLiteral}, {2, kGetter},
            {3, kSetter}, {4, kPropertyName}, {5, kPrototypeName}}})
    word(kPool + slot * sizeof(std::uint32_t), value);
  const auto string = [&](std::uint32_t at, std::string_view value) {
    bytes[at] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at + 1);
  };
  string(kClassName, "Accessor");
  string(kPrototypeName, "prototype");
  string(kPropertyName, "value");
  string(kMemberName, "read");
  word(kClassLiteral, 8);
  bytes[kClassLiteral + 4] =
      static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kClassLiteral + 5, kMemberName);
  bytes[kClassLiteral + 9] =
      static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
  word(kClassLiteral + 10, kMember);
  bytes[kClassLiteral + 14] =
      static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
  bytes[kClassLiteral + 15] = 0;
  bytes[kClassLiteral + 17] =
      static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kClassLiteral + 18, 1);
  const auto code = [&](std::uint32_t at, std::uint8_t locals,
                        std::uint8_t arguments,
                        std::span<const std::uint8_t> body) {
    Check(body.size() < UINT8_MAX);
    bytes[at] = locals;
    bytes[at + 1] = arguments;
    bytes[at + 2] = static_cast<std::uint8_t>(body.size());
    std::copy(body.begin(), body.end(), bytes.begin() + at + kCodeHeaderBytes);
    return abc::CodeInfo{{at}, {at + kCodeHeaderBytes}, locals, arguments,
                         static_cast<std::uint32_t>(body.size()), 0};
  };
  const auto entry_code = code(kEntryCode, kEntryLocals, kHiddenArguments,
                               instructions);
  constexpr std::array<std::uint8_t, 1> kEmptyBody{kReturnUndefined};
  const auto constructor_code = code(kConstructorCode, 0, kHiddenArguments,
                                      kEmptyBody);
  const auto getter_code = code(kGetterCode, 0, kHiddenArguments, kEmptyBody);
  const auto setter_code = code(kSetterCode, 0, kHiddenArguments + 1,
                                kEmptyBody);
  const auto member_code = code(kMemberCode, 0, kHiddenArguments, kEmptyBody);
  const auto collision_code = code(kCollisionCode, 0, kHiddenArguments,
                                   kEmptyBody);
  char path[] = "/tmp/abcd-record-accessor-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1301;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, kFileBytes, {}, {6, kPool}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.method_count = 6;
  index.records.push_back(record);
  const auto method = [&](std::uint32_t offset, std::string name,
                          abc::CodeInfo body) {
    abc::MethodInfo result;
    result.offset = {offset};
    result.owner_offset = record.offset;
    result.name = std::move(name);
    result.name_utf16_length = result.name.size();
    result.code = body;
    return result;
  };
  index.methods.push_back(method(kEntry, "func_main_0", entry_code));
  index.methods.push_back(method(kConstructor, "#scope#Accessor",
                                 constructor_code));
  index.methods.push_back(method(kGetter, "#scope#value", getter_code));
  index.methods.push_back(method(kSetter, "#scope#value^1", setter_code));
  index.methods.push_back(method(kMember, "#scope#read", member_code));
  index.methods.push_back(method(kCollision, "#scope#read", collision_code));
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  abc::ModuleInfo module;
  module.local_exports.push_back({{kClassName}, {kClassName}});
  std::ostringstream output;
  const auto result = source::WriteRecordProgram(
      file, header, index, lookup, record, module, roles, output);
  if (const auto* failure = std::get_if<source::RecordProgramFailure>(&result)) {
    std::cerr << "source_error=" << static_cast<unsigned>(failure->kind)
              << " pc=" << failure->pc.value << '\n';
    return EXIT_FAILURE;
  }
  Check(output.str().find("get [\"value\"]()") != std::string::npos);
  Check(output.str().find("set [\"value\"]($p0)") != std::string::npos);
  Check(output.str().find("[\"read\"]()") != std::string::npos);
  Check(output.str().find("export { $local0 as \"Accessor\" }") !=
        std::string::npos);
  const auto entry_body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
      file, header.profile, entry_code));
  const auto creation = std::find_if(
      entry_body.instructions.begin(), entry_body.instructions.end(),
      [](const abc::BodyInstruction& instruction) {
        return instruction.decoded.opcode ==
            abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8;
      });
  Check(creation != entry_body.instructions.end());
  const std::array<source::ClassAccessor, 1> collision{{
      {u"read", {5}, abc::ClassPlacement::kPrototype,
       abc::ClassMethodKind::kGetter, 0}}};
  std::ostringstream rejected_collision;
  const auto collision_result = source::WriteCreatedClass(
      file, header, index, lookup, {kEntry}, *creation,
      {creation->offset, analysis::HeritageKind::kAbsent},
      rejected_collision, {}, {}, &roles, nullptr, nullptr, nullptr,
      collision);
  Check(std::get<source::CreatedClassError>(collision_result) ==
        source::CreatedClassError::kAccessorOrder);
  Check(rejected_collision.str().empty());
  auto async_index = index;
  constexpr std::uint32_t kKindShift = 8;
  async_index.methods[2].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kAsync) << kKindShift;
  const auto async_lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(async_index.methods));
  const auto async_roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, async_index,
                                       async_lookup));
  std::ostringstream rejected;
  const auto refusal = source::WriteRecordProgram(
      file, header, async_index, async_lookup, record, module, async_roles,
      rejected);
  Check(std::get<source::RecordProgramFailure>(refusal).kind ==
        source::RecordProgramError::kInitializer);
  Check(rejected.str().empty());
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << output.str();
  Check(!file.CheckUnchanged());
  Check(unlink(path) == 0);
}
