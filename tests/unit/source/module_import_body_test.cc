#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include "abcd/abc/module_info.h"
#include "abcd/source/local_lexical_body.h"
#include "abcd/source/module_imports.h"
#include "analysis/ssa_fixture.h"
#include "module_binding.h"

namespace {
void Word(std::vector<std::uint8_t>& bytes, std::size_t at,
          std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void String(std::vector<std::uint8_t>& bytes, std::size_t at,
            std::string_view value) {
  abcd::test::Check(value.size() < 64);
  bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
  std::copy(value.begin(), value.end(), bytes.begin() + at);
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(1024);
  constexpr std::size_t kPoolTable = 64;
  constexpr std::size_t kLocal = 530;
  constexpr std::size_t kImported = 550;
  constexpr std::size_t kRequest = 570;
  Word(bytes, kPoolTable, kLocal);
  String(bytes, kLocal, "value");
  String(bytes, kImported, "value");
  String(bytes, kRequest, "./module-import-dep.mjs");
  char path[] = "/tmp/abcd-module-body-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  test::Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {1, kPoolTable}, {}, {}});
  abc::ModuleInfo module;
  module.requests.push_back({kRequest});
  module.imports.push_back({{kLocal}, {kImported}, {0}});
  module.local_exports.push_back({{kLocal}, {kLocal}});
  const source::MethodConstants constants{file, header, index, {210}, {},
                                          &module};
  const auto body = test::Body({0x7e, 0, 0xfe, 9, 0, 0, 0x64});
  std::ostringstream output;
  const auto imports = source::WriteModuleImports(file, header, module, output);
  test::Check(std::holds_alternative<std::size_t>(imports));
  test::Check(output.str() ==
              "import \"./module-import-dep.mjs\";\n"
              "import { \"value\" as $import0 } from "
              "\"./module-import-dep.mjs\";\n");
  const auto import_source = output.str();
  output.str({});
  const auto result = source::WriteLocalLexicalBody(
      body, {"acc", {}}, output, {}, &constants);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(output.str() == "acc = $import0;\nreturn acc;\n");
  const auto body_source = output.str();
  source::GlobalScope local_scope{};
  local_scope.module_slots = 1;
  const source::MethodConstants local_constants{
      file, header, index, {210}, {}, &module, nullptr, local_scope};
  const auto local_body = test::Body({0x7d, 0, 0xfe, 9, 0, 0, 0x64});
  output.str({});
  const auto local_result = source::WriteLocalLexicalBody(
      local_body, {"acc", {}}, output, {}, &local_constants);
  test::Check(std::holds_alternative<std::size_t>(local_result));
  test::Check(output.str() == "acc = $local0;\nreturn acc;\n");
  const auto local_source = output.str();
  output.str({});
  const auto no_local_scope = source::WriteLocalLexicalBody(
      local_body, {"acc", {}}, output, {}, &constants);
  test::Check(std::get<source::BodyEmitFailure>(no_local_scope).kind ==
              source::BodyEmitError::kModuleBinding);
  test::Check(output.str().empty());
  const auto colliding_local = source::WriteLocalLexicalBody(
      local_body, {"$local0", {}}, output, {}, &local_constants);
  test::Check(std::get<source::BodyEmitFailure>(colliding_local).emit_error ==
              source::EmitError::kInvalidBinding);
  test::Check(output.str().empty());
  const auto unguarded_local = source::WriteLocalLexicalBody(
      test::Body({0x7d, 0, 0x64}), {"acc", {}}, output, {},
      &local_constants);
  test::Check(std::get<source::BodyEmitFailure>(unguarded_local).kind ==
              source::BodyEmitError::kModuleBinding);
  test::Check(output.str().empty());
  const auto invalid_local_slot = source::WriteLocalLexicalBody(
      test::Body({0x7d, 1, 0xfe, 9, 0, 0, 0x64}), {"acc", {}}, output, {},
      &local_constants);
  test::Check(std::get<source::BodyEmitFailure>(invalid_local_slot).kind ==
              source::BodyEmitError::kModuleBinding);
  test::Check(output.str().empty());
  const source::MethodConstants detached{file, header, index, {210}, {}};
  std::ostringstream rejected;
  const auto missing = source::WriteLocalLexicalBody(
      body, {"acc", {}}, rejected, {}, &detached);
  test::Check(std::get<source::BodyEmitFailure>(missing).kind ==
              source::BodyEmitError::kMissingModule);
  test::Check(rejected.str().empty());
  const auto unguarded = source::WriteLocalLexicalBody(
      test::Body({0x7e, 0, 0x64}), {"acc", {}}, rejected, {}, &constants);
  test::Check(std::get<source::BodyEmitFailure>(unguarded).kind ==
              source::BodyEmitError::kModuleBinding);
  const auto bad_slot = source::WriteLocalLexicalBody(
      test::Body({0x7e, 1, 0xfe, 9, 0, 0, 0x64}), {"acc", {}}, rejected, {},
      &constants);
  test::Check(std::get<source::BodyEmitFailure>(bad_slot).kind ==
              source::BodyEmitError::kModuleBinding);
  const auto deferred_name = source::WriteLocalLexicalBody(
      test::Body({0x7e, 0, 0xfe, 9, 1, 0, 0x64}), {"acc", {}}, rejected, {},
      &constants);
  test::Check(std::holds_alternative<std::size_t>(deferred_name));
  test::Check(rejected.str() == "acc = $import0;\nreturn acc;\n");
  rejected.str({});
  auto bad_module = module;
  bad_module.imports[0].request = {1};
  const auto invalid_imports = source::WriteModuleImports(
      file, header, bad_module, rejected);
  test::Check(std::get<source::ModuleImportFailure>(invalid_imports).kind ==
              source::ModuleImportError::kRequestIndex);
  const source::MethodConstants bad_constants{file, header, index, {210}, {},
                                               &bad_module};
  const auto invalid_body = source::WriteLocalLexicalBody(
      body, {"acc", {}}, rejected, {}, &bad_constants);
  test::Check(std::get<source::BodyEmitFailure>(invalid_body).kind ==
              source::BodyEmitError::kModuleBinding);
  test::Check(rejected.str().empty());
  const auto limited = source::WriteModuleImports(
      file, header, module, rejected, {16});
  test::Check(std::get<source::ModuleImportFailure>(limited).kind ==
              source::ModuleImportError::kOutputLimit);
  test::Check(rejected.str().empty());
  abc::ModuleInfo namespace_module;
  namespace_module.requests = module.requests;
  namespace_module.namespace_imports.push_back({{kLocal}, {0}});
  std::ostringstream namespace_output;
  const auto namespace_result = source::WriteModuleImports(
      file, header, namespace_module, namespace_output);
  test::Check(std::holds_alternative<std::size_t>(namespace_result));
  test::Check(namespace_output.str() ==
              "import \"./module-import-dep.mjs\";\n"
              "import * as $import0 from \"./module-import-dep.mjs\";\n");
  for (const std::size_t count : {200, 201, 400, 800}) {
    std::vector<std::uint8_t> instructions;
    instructions.reserve(count * 6 + 1);
    for (std::size_t i = 0; i < count; ++i)
      instructions.insert(instructions.end(),
                          {i % 2 == 0 ? std::uint8_t{0x7e}
                                      : std::uint8_t{0x7d},
                           0, 0xfe, 9, 0, 0});
    instructions.push_back(0x64);
    const auto repeated = test::Body(instructions);
    const auto validated = source::ValidateGuardedModuleLoads(
        repeated, &local_constants, repeated.instructions.size(),
        repeated.instructions.size() * 2);
    const auto& admitted = std::get<source::GuardedModuleLoads>(validated);
    test::Check(admitted.work_units == repeated.instructions.size() * 2);
    test::Check(static_cast<std::size_t>(std::count(
                    admitted.admitted.begin(), admitted.admitted.end(), 1)) ==
                count);
    const auto exhausted = source::ValidateGuardedModuleLoads(
        repeated, &local_constants, repeated.instructions.size(),
        admitted.work_units - 1);
    test::Check(std::get<source::BodyEmitFailure>(exhausted).kind ==
                source::BodyEmitError::kWorkLimit);
  }
  test::Check(unlink(path) == 0);
  if (argc == 2) {
    test::Check(std::string_view(argv[1]) == "--emit");
    std::cout << import_source << "export function read() {\nlet acc;\n"
              << body_source << "}\n"
              << "export let $local0 = 3;\n"
              << "export function setLocal(value) { $local0 = value; }\n"
              << "export function readLocal() {\nlet acc;\n"
              << local_source << "}\n";
    test::Check(static_cast<bool>(std::cout));
  } else {
    test::Check(argc == 1);
  }
}
