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

int main() {
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
  // Adjacent equal local names share one wire slot; x and y alias slot 0.
  Check(exports.slot_count() == 2);
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
}
