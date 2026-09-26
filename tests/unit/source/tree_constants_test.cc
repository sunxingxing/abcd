#include "../../../src/source/tree_constants.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
void Run(std::string_view name, bool element) {
  constexpr std::uint32_t kPool = 64, kMethod = 100, kName = 128;
  constexpr std::uint32_t kAliasSlots = 2;
  std::vector<std::uint8_t> bytes(256);
  for (std::uint32_t slot = 0; slot < kAliasSlots; ++slot)
    for (std::size_t i = 0; i < sizeof(kName); ++i)
      bytes[kPool + slot * sizeof(kName) + i] = kName >> (8 * i);
  bytes[kName] = static_cast<std::uint8_t>(name.size() * 2 + 1);
  std::copy(name.begin(), name.end(), bytes.begin() + kName + 1);
  char path[] = "/tmp/abcd-tree-constants-XXXXXX";
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
  abc::MetadataIndex index;
  index.regions.push_back({kMethod, kName, {}, {kAliasSlots, kPool}, {}, {}});
  for (const std::uint32_t count : {2U, 200U, 201U, 4096U}) {
    analysis::ValueProgram graph;
    for (std::uint32_t i = 0; i < count; ++i) {
      analysis::ValueOp op{};
      op.operation = analysis::ValueNamedRead{{i % kAliasSlots}};
      graph.ops.push_back(op);
    }
    analysis::ValueOp literal{};
    literal.operation = analysis::ValueString{{0}};
    graph.ops.push_back(literal);
    const auto resolved = ResolveTreeConstants(file, header, index, {kMethod}, graph);
    if (element) {
      Check(std::get<TreeConstantError>(resolved) == TreeConstantError::kElementName);
      continue;
    }
    Check(std::holds_alternative<TreeConstants>(resolved));
    const auto& constants = std::get<TreeConstants>(resolved);
    Check(constants.names.size() == 1 && constants.texts.size() == 1);
    Check(constants.names[0] == constants.texts[0]);
    Check(constants.names[0] == std::u16string(name.begin(), name.end()));
    for (std::uint32_t i = 0; i < count; ++i)
      Check(std::get<NameId>(constants.operations[i]) == NameId{0});
    Check(std::get<TextId>(constants.operations.back()) == TextId{0});
    analysis::SemanticBudget budget;
    budget.work = constants.work - 1;
    Check(std::get<TreeConstantError>(ResolveTreeConstants(file, header, index, {kMethod}, graph, budget)) ==
          TreeConstantError::kWorkLimit);
    ++budget.work;
    Check(std::holds_alternative<TreeConstants>(ResolveTreeConstants(file, header, index, {kMethod}, graph, budget)));
  }
  Check(unlink(path) == 0);
}
}  // namespace
int main() {
  Run("pathStack", false);
  Run("0", true);
  Run("01", false);
}
