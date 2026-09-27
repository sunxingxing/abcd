#include "abcd/source/module_exports.h"

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(abc::kDynamicHeaderSize);
  const auto string = [&](std::string_view value) {
    const abc::EntityOffset offset{static_cast<std::uint32_t>(bytes.size())};
    Check(value.size() < 64);
    bytes.push_back(static_cast<std::uint8_t>((value.size() << 1) | 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    return offset;
  };
  const auto a = string("a"), b = string("b");
  const auto x = string("x"), y = string("y"), z = string("z");
  std::vector<abc::EntityOffset> scale_names;
  for (std::size_t i = 0; i < 402; ++i)
    scale_names.push_back(string("alias" + std::to_string(i)));
  char path[] = "/tmp/abcd-module-exports-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::ModuleInfo module;
  module.local_exports = {{a, x}, {a, y}, {b, z}};
  auto built = source::BuildModuleExports(file, header, module);
  Check(std::holds_alternative<source::ModuleExports>(built));
  auto exports = std::get<source::ModuleExports>(std::move(built));
  Check(exports.slot_count() == 2 && !exports.Complete());
  Check(exports.State({2}) == nullptr);
  Check(std::holds_alternative<source::ModuleTdz>(*exports.State({0})));
  std::ostringstream aliases;
  const auto declared = exports.WriteAliases(aliases);
  Check(std::get<std::size_t>(declared) == aliases.str().size());
  Check(aliases.str() ==
        "export { $local0 as \"x\" };\n"
        "export { $local0 as \"y\" };\n"
        "export { $local1 as \"z\" };\n");
  std::ostringstream short_aliases;
  Check(std::get<source::ModuleExportError>(
            exports.WriteAliases(short_aliases, {1})) ==
        source::ModuleExportError::kOutputLimit);
  Check(short_aliases.str().empty());
  std::ostringstream stores;
  Check(std::get<source::ModuleExportError>(
            exports.WriteStore({{2}}, stores)) ==
        source::ModuleExportError::kSlot);
  Check(stores.str().empty());
  Check(std::get<source::ModuleExportError>(
            exports.WriteStore({{0}}, stores, {1})) ==
        source::ModuleExportError::kOutputLimit);
  Check(stores.str().empty() && !exports.Complete());
  Check(std::holds_alternative<std::size_t>(exports.WriteStore({{0}}, stores, {}, abc::CodeOffset{0})));
  Check(std::get<source::ModuleInitialization>(*exports.State({0})).pc == abc::CodeOffset{0});
  Check(!exports.Complete());
  Check(std::holds_alternative<std::size_t>(exports.WriteStore({{0}}, stores, {}, abc::CodeOffset{9})));
  Check(std::get<source::ModuleInitialization>(*exports.State({0})).pc == abc::CodeOffset{0});
  Check(std::holds_alternative<std::size_t>(exports.WriteStore({{1}}, stores)));
  Check(std::holds_alternative<source::UnlocatedModuleInitialization>(*exports.State({1})));
  Check(exports.Complete());
  Check(stores.str() ==
        "let $local0 = $acc;\n$local0 = $acc;\nlet $local1 = $acc;\n");
  Check(std::holds_alternative<std::size_t>(exports.WriteStore({{1}}, stores, {}, abc::CodeOffset{12})));
  Check(std::holds_alternative<source::UnlocatedModuleInitialization>(*exports.State({1})));
  auto fresh = std::get<source::ModuleExports>(source::BuildModuleExports(file, header, module));
  std::ostringstream failed;
  failed.setstate(std::ios::badbit);
  Check(std::get<source::ModuleExportError>(fresh.WriteStore({{0}}, failed, {}, abc::CodeOffset{8})) ==
      source::ModuleExportError::kWrite);
  Check(std::holds_alternative<source::ModuleTdz>(*fresh.State({0})));
  module.local_exports[2].exported = x;
  Check(std::get<source::ModuleExportError>(
            source::BuildModuleExports(file, header, module)) ==
        source::ModuleExportError::kDuplicateExport);
  module.local_exports = {{a, x}, {b, y}, {a, z}};
  const auto nonadjacent = source::BuildModuleExports(file, header, module);
  Check(std::get<source::ModuleExports>(nonadjacent).slot_count() == 3);
  source::ModuleExportBudget empty_budget;
  empty_budget.bytes = 0;
  Check(std::get<source::ModuleExportError>(
            source::BuildModuleExports(file, header, module, empty_budget)) ==
        source::ModuleExportError::kResourceLimit);
  auto static_header = header;
  static_header.kind = abc::AbcKind::kStatic;
  Check(std::get<source::ModuleExportError>(
            source::BuildModuleExports(file, static_header, module)) ==
        source::ModuleExportError::kProfile);
  for (const auto count : {200U, 201U, 402U}) {
    module.local_exports.clear();
    for (std::size_t i = 0; i < count; ++i)
      module.local_exports.push_back({a, scale_names[i]});
    const auto scaled = source::BuildModuleExports(file, header, module);
    Check(std::get<source::ModuleExports>(scaled).slot_count() == 1);
  }
  Check(unlink(path) == 0);
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << aliases.str() << "let $acc = 1;\n"
              << "let $local0 = $acc;\n$acc = 2;\n"
              << "$local0 = $acc;\nlet $local1 = $acc;\n";
    Check(static_cast<bool>(std::cout));
  } else {
    Check(argc == 1);
  }
}
