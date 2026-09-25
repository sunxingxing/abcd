#include "abcd/source/created_function.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <sstream>

#include "abcd/abc/module_info.h"
#include "abcd/analysis/method_roles.h"
#include "analysis/ssa_fixture.h"
#include "source/leaf_function_checks.h"

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 100;
  constexpr std::uint32_t kPoolEntries = 5;
  constexpr std::uint32_t kCaller = 200;
  constexpr std::uint32_t kTarget = 300;
  constexpr std::uint32_t kCode = 400;
  constexpr std::uint32_t kLargeCode = 800;
  constexpr std::uint32_t kLargeInstructions = 65536;
  std::vector<std::uint8_t> bytes(kLargeCode + kLargeInstructions + 16);
  const auto word = [&](std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i)
      bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  const auto string = [&](std::size_t offset, std::string_view value) {
    test::Check(value.size() < 64);
    bytes[offset++] = (value.size() << 1) | 1;
    for (const char byte : value) bytes[offset++] = byte;
    bytes[offset] = 0;
  };
  // ANNOTATION tag 6 -> class index 0, one callType element of scalar type I.
  bytes[64] = 6;
  word(65, 256);
  word(128, 560);
  bytes[258] = 1;
  word(260, 600);
  word(264, 1);  // HaveThis only: three declared parameters in this frame.
  bytes[268] = 'I';
  string(560, "L_ESCallTypeAnnotation;");
  string(600, "callType");
  word(kPool + sizeof(kTarget), kTarget);
  word(kPool + 2 * sizeof(kTarget), 350);
  word(kPool + 3 * sizeof(kTarget), 620);
  word(kPool + 4 * sizeof(kTarget), 660);
  string(620, "value");
  string(640, "./module-import-dep.mjs");
  string(660, "createdGlobal");
  const std::uint8_t captured[]{0, 4, 3, 0, 0x3c, 0, 0x64};
  const std::uint8_t nested[]{0, 4, 6, 0, 0x33, 0, 0, 0, 1, 0x64};
  std::copy(std::begin(captured), std::end(captured), bytes.begin() + 700);
  std::copy(std::begin(nested), std::end(nested), bytes.begin() + 720);
  const std::uint8_t empty[]{0, 4, 1, 0, 0x65};
  std::copy(std::begin(empty), std::end(empty), bytes.begin() + 740);
  const auto fixture = [&](std::size_t offset,
                           std::initializer_list<std::uint8_t> code) {
    bytes[offset] = 0;
    bytes[offset + 1] = 4;
    bytes[offset + 2] = code.size();
    bytes[offset + 3] = 0;
    std::copy(code.begin(), code.end(), bytes.begin() + offset + 4);
  };
  fixture(450, {0x60, 3, 0x3d, 0, 0x3c, 0, 0x64});           // captured setter
  fixture(470, {0x09, 1, 0x60, 3, 0x3d, 0, 0x3c, 1, 0x64});  // local + outer
  fixture(490, {0x3c, 1, 0x64});                             // outer ancestor
  fixture(510, {0x70, 0x3d, 0, 0x65});  // forbidden internal store
  fixture(530, {0x3c, 0x10, 0x64});     // captured slot 1
  fixture(760, {0x7e, 0, 0xfe, 9, 3, 0, 0x64});
  fixture(780, {0x3f, 0, 4, 0, 0x64});  // TRYLDGLOBALBYNAME
  std::size_t large_start = kLargeCode;
  const auto uleb = [&](std::uint32_t value) {
    do {
      auto byte = static_cast<std::uint8_t>(value & 0x7f);
      value >>= 7;
      bytes[large_start++] = value ? byte | 0x80 : byte;
    } while (value);
  };
  uleb(0);
  uleb(4);
  uleb(kLargeInstructions);
  uleb(0);
  std::fill_n(bytes.begin() + large_start, kLargeInstructions, 0xd5);
  bytes[large_start + kLargeInstructions - 3] = 0x3c;
  bytes[large_start + kLargeInstructions - 2] = 0;
  bytes[large_start + kLargeInstructions - 1] = 0x64;
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (8 * i));
  // Missing callType defaults to self/new.target/this plus one parameter.
  const std::uint8_t code[]{0, 4, 3, 0, 0x60, 3, 0x64};
  std::copy(std::begin(code), std::end(code), bytes.begin() + kCode);
  char path[] = "/tmp/abcd-created-function-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back(
      {kCaller, kCode, {1, 128}, {kPoolEntries, kPool}, {}, {}});
  index.methods.resize(3);
  index.methods[0].offset = {kCaller};
  auto& target = index.methods[1];
  target.offset = {kTarget};
  target.name = "#scope#Recovered^1";
  target.name_utf16_length = target.name.size();
  target.access_flags = 1U << 8;
  target.code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 4, 3, 0};
  auto& other = index.methods[2];
  other.offset = {350};
  other.name = "Other";
  other.name_utf16_length = other.name.size();
  other.access_flags = target.access_flags;
  other.code = abc::CodeInfo{{740}, {744}, 0, 4, 1, 0};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto creation = test::Body({0x33, 0, 0, 0, 1}).instructions[0].decoded;
  std::ostringstream output;
  const auto result = source::WriteCreatedFunction(file, header, index, lookup,
                                                   {kCaller}, creation, output);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(output.str().find("function Recovered($p0)") !=
              std::string::npos);
  test::Check(std::get<std::size_t>(result) == output.str().size());
  constexpr unsigned kFunctionKindShift = 8;
  target.access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kArrow)
      << kFunctionKindShift;
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  test::Check(roles.At({1}) == analysis::MethodRole::kUnseen);
  std::ostringstream unproved_arrow;
  const auto unproved_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, unproved_arrow);
  test::Check(std::get<source::CreatedFunctionError>(unproved_result) ==
              source::CreatedFunctionError::kRole);
  test::Check(unproved_arrow.str().empty());
  std::ostringstream arrow;
  const auto arrow_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, arrow, {}, {}, nullptr,
      &roles);
  test::Check(std::holds_alternative<std::size_t>(arrow_result));
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const arrow = " << arrow.str() << ";\n";
  target.access_flags = 1U << kFunctionKindShift;
  abc::ModuleInfo module;
  module.requests.push_back({640});
  module.imports.push_back({{620}, {620}, {0}});
  const auto saved_import_code = target.code;
  target.code = abc::CodeInfo{{760}, {764}, 0, 4, 7, 0};
  std::ostringstream imported;
  const auto imported_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, imported, {}, {},
      &module);
  test::Check(std::holds_alternative<std::size_t>(imported_result));
  test::Check(imported.str().find("$acc = $import0;") != std::string::npos);
  target.owner_offset = {999};
  std::ostringstream foreign;
  const auto foreign_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, foreign, {}, {},
      &module);
  test::Check(std::get<source::CreatedFunctionError>(foreign_result) ==
              source::CreatedFunctionError::kModuleOwner);
  test::Check(foreign.str().empty());
  target.owner_offset = {};
  target.code = saved_import_code;
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const recovered = " << output.str() << ";\n";
  target.annotations = {{64}, 1};
  auto annotated_creation = creation;
  annotated_creation.operands[2].raw_bits = 3;
  std::ostringstream annotated;
  test::Check(std::holds_alternative<std::size_t>(source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, annotated_creation, annotated)));
  test::Check(annotated.str().find("$r0 = this") != std::string::npos);
  test::Check(annotated.str().find("$r3 = $p2") != std::string::npos);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const annotated = " << annotated.str() << ";\n";
  target.annotations = {};
  const source::BaseFunction parent{
      std::get<analysis::FunctionCreation>(
          analysis::LiftFunctionCreation(creation)),
      std::get<abc::MethodAttributes>(
          abc::DecodeMethodAttributes(target.access_flags, header.profile)),
      std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({}))),
      "Factory"};
  const source::MethodConstants parent_constants{
      file, header, index, {kCaller}, {}};
  const auto parent_body = test::Body({0x09, 1, 0x33, 0, 0, 0, 1, 0x64});
  std::ostringstream factory;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      parent, parent_body, factory, source::RoutePolicy::kLegacyAllowed, {},
      &parent_constants, &lookup)));
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const factory = " << factory.str() << ";\n";
  target.code = abc::CodeInfo{{780}, {784}, 0, 4, 5, 0};
  std::ostringstream unproved_global;
  const auto unproved_global_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, unproved_global);
  test::Check(std::get<source::BodyEmitFailure>(unproved_global_result).kind ==
              source::BodyEmitError::kGlobalScope);
  test::Check(unproved_global.str().empty());
  const source::GlobalScope complete_scope{{}, {}};
  std::ostringstream proved_global;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteCreatedFunction(file, header, index, lookup, {kCaller},
                                   creation, proved_global, {}, {}, nullptr,
                                   nullptr, &complete_scope)));
  test::Check(proved_global.str().find("$acc = createdGlobal;") !=
              std::string::npos);
  const std::array<std::string_view, 1> shadowing{"createdGlobal"};
  const source::GlobalScope shadowed_scope{shadowing, {}};
  std::ostringstream shadowed_global;
  const auto shadowed_result = source::WriteCreatedFunction(
      file, header, index, lookup, {kCaller}, creation, shadowed_global,
      {}, {}, nullptr, nullptr, &shadowed_scope);
  test::Check(std::get<source::BodyEmitFailure>(shadowed_result).kind ==
              source::BodyEmitError::kGlobalReference);
  test::Check(shadowed_global.str().empty());
  const source::MethodConstants scoped_parent{
      file, header, index, {kCaller}, {}, nullptr, nullptr, complete_scope};
  std::ostringstream nested_global;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      parent, parent_body, nested_global, source::RoutePolicy::kLegacyAllowed,
      {}, &scoped_parent, &lookup)));
  test::Check(nested_global.str().find("$acc = createdGlobal;") !=
              std::string::npos);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const globalFactory = " << nested_global.str()
              << ";\n";
  target.code = saved_import_code;
  test::CheckLeafFunctions(file, header, index, lookup,
                           {{kLargeCode},
                            {static_cast<std::uint32_t>(large_start)},
                            0,
                            4,
                            kLargeInstructions,
                            0},
                           argc == 2 && std::string_view(argv[1]) == "--emit");
  const auto render = [&](source::LocalLexicalBudget budget = {}) {
    std::ostringstream rendered;
    const auto result = source::WriteCreatedFunction(
        file, header, index, lookup, {kCaller}, creation, rendered, budget);
    if (!std::holds_alternative<std::size_t>(result))
      test::Check(rendered.str().empty());
    return result;
  };
  for (const auto& [raw, expected] : {std::pair{"Recovered", "Recovered"},
                                      {"#Recovered", "Recovered"},
                                      {"#a#b#Recovered^1", "Recovered"},
                                      {"#a^b#Recovered", "Recovered"},
                                      {"#a#Recovered^", "Recovered"}}) {
    target.name = raw;
    target.name_utf16_length = target.name.size();
    std::ostringstream rendered;
    test::Check(
        std::holds_alternative<std::size_t>(source::WriteCreatedFunction(
            file, header, index, lookup, {kCaller}, creation, rendered)));
    test::Check(rendered.str().find(std::string("function ") + expected +
                                    "(") != std::string::npos);
  }
  for (const auto raw : {"Recovered^1", "a#Recovered", "#a#",
                         "#a#Recovered^1^2", "#a#eval", "#a#$acc"}) {
    target.name = raw;
    target.name_utf16_length = target.name.size();
    test::Check(std::get<source::FunctionEmitError>(render()) ==
                source::FunctionEmitError::kName);
  }
  for (const auto& raw : {std::string{}, std::string("#\xc3\xa9#Recovered"),
                          std::string("a\0b", 3)}) {
    target.name = raw;
    target.name_utf16_length = target.name.size();
    test::Check(std::get<source::CreatedFunctionError>(render()) ==
                source::CreatedFunctionError::kName);
  }
  for (const auto count : {200u, 201u, 4096u, 65536u}) {
    target.name = std::string(count, '#') + "Recovered^1";
    target.name_utf16_length = target.name.size();
    test::Check(std::holds_alternative<std::size_t>(render()));
  }
  target.name = "Recovered";
  target.name_utf16_length = 1;
  test::Check(std::get<source::CreatedFunctionError>(render()) ==
              source::CreatedFunctionError::kName);
  target.name_utf16_length = target.name.size();
  const auto saved_code = target.code;
  target.code.reset();
  test::Check(std::get<source::CreatedFunctionError>(render()) ==
              source::CreatedFunctionError::kMissingCode);
  target.code = saved_code;
  ++target.code->registers;
  test::Check(std::get<abc::BodyFailure>(render()).kind ==
              abc::BodyError::kMetadataMismatch);
  target.code = saved_code;
  target.code->arguments = 0;
  test::Check(std::get<abc::FrameError>(render()) ==
              abc::FrameError::kArgumentUnderflow);
  target.code = saved_code;
  target.annotations = {{511}, 1};
  test::Check(std::holds_alternative<abc::MetadataError>(render()));
  target.annotations = {};
  std::ostringstream rejected;
  const auto absent = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(std::span(index.methods).first(1)));
  test::Check(std::get<abc::MethodTargetError>(source::WriteCreatedFunction(
                  file, header, index, absent, {kCaller}, creation,
                  rejected)) == abc::MethodTargetError::kTargetMissing);
  test::Check(std::get<abc::MethodTargetError>(source::WriteCreatedFunction(
                  file, header, index, lookup, {kCaller + 1}, creation,
                  rejected)) == abc::MethodTargetError::kCallerMissing);
  auto invalid_creation = creation;
  invalid_creation.operands[1].raw_bits = kPoolEntries;
  test::Check(std::get<abc::MetadataError>(source::WriteCreatedFunction(
                  file, header, index, lookup, {kCaller}, invalid_creation,
                  rejected)) == abc::MetadataError::kIndexRange);
  test::Check(std::get<analysis::CreationError>(source::WriteCreatedFunction(
                  file, header, index, lookup, {kCaller},
                  test::Body({0x65}).instructions[0].decoded, rejected)) ==
              analysis::CreationError::kUnsupportedOpcode);
  test::Check(rejected.str().empty());
  source::LocalLexicalBudget budget;
  budget.bytes = 0;
  test::Check(std::get<abc::BodyFailure>(render(budget)).kind ==
              abc::BodyError::kResourceLimit);
  budget = {};
  budget.output.bytes = 1;
  test::Check(!std::holds_alternative<std::size_t>(render(budget)));
  const auto body = std::get<abc::MethodBody>(
      abc::DecodeMethodBody(file, header.profile, *target.code));
  const source::BaseFunction base{
      std::get<analysis::FunctionCreation>(
          analysis::LiftFunctionCreation(creation)),
      std::get<abc::MethodAttributes>(
          abc::DecodeMethodAttributes(target.access_flags, header.profile)),
      std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({}))),
      target.name};
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  while (low < high) {
    budget = {};
    budget.bytes = low + (high - low) / 2;
    std::ostringstream rendered;
    const auto result = source::WriteBaseFunction(
        base, body, rendered, source::RoutePolicy::kLegacyAllowed, budget);
    if (std::holds_alternative<std::size_t>(result))
      high = budget.bytes;
    else
      low = budget.bytes + 1;
  }
  const auto retained =
      body.instructions.capacity() * sizeof(abc::BodyInstruction) +
      body.tries.capacity() * sizeof(abc::TryRegion) +
      body.handlers.capacity() * sizeof(abc::CatchHandler);
  budget = {};
  budget.bytes = retained + low;
  test::Check(std::holds_alternative<std::size_t>(render(budget)));
  --budget.bytes;
  test::Check(!std::holds_alternative<std::size_t>(render(budget)));
  test::Check(unlink(path) == 0);
}
