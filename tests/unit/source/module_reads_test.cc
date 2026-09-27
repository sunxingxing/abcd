#include "../../../src/source/module_reads.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
constexpr abc::EntityOffset kFirst{96}, kEqual{112}, kOther{128};
analysis::ValueProgram Reads(analysis::ModuleSpace space, std::uint16_t slot,
                            std::uint32_t count = 1) {
  analysis::ValueProgram graph;
  for (std::uint32_t i = 0; i < count; ++i) {
    analysis::ValueOp op{};
    op.operation = analysis::ValueModuleRead{space, {slot}};
    graph.ops.push_back(op);
  }
  return graph;
}
void Expect(const std::variant<ModuleReads, ModuleReadFailure>& result, ModuleReadError error) {
  Check(std::get<ModuleReadError>(std::get<ModuleReadFailure>(result).reason) == error);
}
}  // namespace

int main() {
  std::vector<std::uint8_t> bytes(256);
  const auto string = [&](abc::EntityOffset offset, std::string_view text) {
    // Small ASCII fixture: one-byte ULEB UTF-16 length plus ASCII marker.
    constexpr std::uint8_t kAscii = 1;
    bytes[offset.value] = static_cast<std::uint8_t>((text.size() << 1) | kAscii);
    std::copy(text.begin(), text.end(), bytes.begin() + offset.value + 1);
  };
  string(kFirst, "first"); string(kEqual, "first"); string(kOther, "other");
  char path[] = "/tmp/abcd-module-reads-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::ModuleInfo module;
  module.requests = {kOther, kEqual};
  module.imports.push_back({kFirst, kEqual, {1}});
  module.namespace_imports.push_back({kOther, {0}});
  // Distinct string entities with equal content share only adjacent slots.
  module.local_exports = {{kFirst, kFirst}, {kEqual, kOther},
      {kOther, kOther}, {kEqual, kFirst}};
  for (std::uint16_t slot = 0; slot < 3; ++slot) {
    const auto result = ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kLocal, slot));
    const auto& binding = std::get<ModuleReads>(result).bindings.at(0);
    const auto expected = slot == 0 ? kFirst : slot == 1 ? kOther : kEqual;
    Check(std::get<LocalModuleBinding>(binding.target).local == expected);
    Check(binding.read.slot == analysis::ModuleSlot{slot});
  }
  Expect(ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kLocal, 3)), ModuleReadError::kSlot);
  Expect(ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kExternal, 2)), ModuleReadError::kSlot);
  auto invalid = Reads(analysis::ModuleSpace::kExternal, 2);
  constexpr abc::CodeOffset kReadPc{42};
  invalid.origins.push_back({{0}, kReadPc, {}});
  Check(std::get<ModuleReadFailure>(ResolveModuleReads(file, header, &module, invalid)).pc == kReadPc);
  Expect(ResolveModuleReads(file, header, nullptr, Reads(analysis::ModuleSpace::kExternal, 0)), ModuleReadError::kMissingModule);
  for (const auto count : {1U, 2U, 200U, 201U, 4096U}) {
    const auto graph = Reads(analysis::ModuleSpace::kExternal, 0, count);
    const auto result = ResolveModuleReads(file, header, &module, graph);
    const auto& reads = std::get<ModuleReads>(result);
    Check(reads.bindings.size() == count);
    for (std::uint32_t i = 0; i < count; ++i) {
      Check(reads.bindings[i].operation == analysis::OpId{i});
      const auto& target = std::get<ImportedModuleBinding>(reads.bindings[i].target);
      Check(target.request == abc::ModuleRequestId{1} && target.specifier == kEqual);
      Check(target.local == kFirst && target.imported == kEqual);
    }
    analysis::SemanticBudget budget;
    budget.work = reads.work - 1;
    Expect(ResolveModuleReads(file, header, &module, graph, budget), ModuleReadError::kWorkLimit);
    ++budget.work;
    Check(std::holds_alternative<ModuleReads>(ResolveModuleReads(file, header, &module, graph, budget)));
    budget.bytes = 0;
    Expect(ResolveModuleReads(file, header, &module, graph, budget), ModuleReadError::kResourceLimit);
  }
  const auto ns = ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kExternal, 1));
  const auto& target = std::get<NamespaceModuleBinding>(std::get<ModuleReads>(ns).bindings[0].target);
  Check(target.request == abc::ModuleRequestId{0} && target.specifier == kOther && target.local == kOther);
  module.imports[0].request = {2};
  Expect(ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kExternal, 0)), ModuleReadError::kRequest);
  for (const auto count : {1U, 2U, 200U, 201U, 4096U}) {
    module.local_exports.resize(count);
    for (std::uint32_t i = 0; i < count; ++i)
      module.local_exports[i] = {i % 2 == 0 ? kFirst : kEqual, kOther};
    const auto graph = Reads(analysis::ModuleSpace::kLocal, 0);
    const auto result = ResolveModuleReads(file, header, &module, graph);
    const auto& reads = std::get<ModuleReads>(result);
    Check(std::get<LocalModuleBinding>(reads.bindings[0].target).local == kFirst);
    Expect(ResolveModuleReads(file, header, &module, Reads(analysis::ModuleSpace::kLocal, 1)), ModuleReadError::kSlot);
    analysis::SemanticBudget budget;
    budget.work = reads.work - 1;
    Expect(ResolveModuleReads(file, header, &module, graph, budget), ModuleReadError::kWorkLimit);
    ++budget.work;
    Check(std::holds_alternative<ModuleReads>(ResolveModuleReads(file, header, &module, graph, budget)));
  }
  auto removed = Reads(analysis::ModuleSpace::kLocal, UINT16_MAX);
  removed.ops[0].life = analysis::ValueLife::kRemoved;
  Check(std::get<ModuleReads>(ResolveModuleReads(file, header, nullptr, removed)).bindings.empty());
  Check(unlink(path) == 0);
}
