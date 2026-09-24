#include "spread_field_constructor.h"

#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64;
  constexpr std::uint32_t kMethod = 600;
  constexpr std::uint32_t kValue = 800;
  constexpr std::uint32_t kName = 820;
  constexpr std::uint32_t kNumericName = 840;
  constexpr std::size_t kBytes = 1024;
  std::vector<std::uint8_t> bytes(kBytes);
  const auto word = [&](std::uint32_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  word(kPool + 31 * sizeof(std::uint32_t), kValue);
  word(kPool + 56 * sizeof(std::uint32_t), kName);
  word(kPool + 57 * sizeof(std::uint32_t), kNumericName);
  bytes[kValue] = 1;
  bytes[kValue + 1] = 0;
  constexpr char kField[] = "curBp";
  bytes[kName] = (sizeof(kField) - 1) * 2 + 1;
  for (std::size_t i = 0; i < sizeof(kField); ++i)
    bytes[kName + 1 + i] = kField[i];
  bytes[kNumericName] = 3;
  bytes[kNumericName + 1] = '0';
  char path[] = "/tmp/abcd-spread-field-XXXXXX";
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
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = kBytes;
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kMethod, kBytes, {}, {58, kPool}, {}, {}});
  const source::MethodConstants constants(file, header, index, {kMethod}, {});
  analysis::SpreadFieldDerivedConstructor proof;
  proof.writes.push_back({{56}, {31}});
  std::ostringstream output;
  const auto result = source::WriteSpreadFieldConstructor(
      proof, 0, constants, output);
  Check(std::holds_alternative<std::size_t>(result));
  Check(output.str() ==
        "constructor(...$rest) { super(...$rest); this[\"curBp\"] = \"\"; }");
  Check(std::get<std::size_t>(result) == output.str().size());
  std::ostringstream refused;
  source::EmitBudget budget;
  budget.bytes = output.str().size() - 1;
  Check(std::get<source::EmitError>(source::WriteSpreadFieldConstructor(
            proof, 0, constants, refused, budget)) ==
        source::EmitError::kOutputLimit);
  Check(refused.str().empty());
  auto numeric = proof;
  numeric.writes[0].name = {57};
  std::ostringstream numeric_output;
  const auto numeric_result = source::WriteSpreadFieldConstructor(
      numeric, 0, constants, numeric_output);
  const auto* numeric_error =
      std::get_if<source::BodyEmitFailure>(&numeric_result);
  Check(numeric_error &&
        numeric_error->kind == source::BodyEmitError::kIndexedName);
  Check(numeric_output.str().empty());
  Check(unlink(path) == 0);
}
