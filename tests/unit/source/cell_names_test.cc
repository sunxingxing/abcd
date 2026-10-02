#include "../../../src/source/cell_names.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/abc/literal_array.h"
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <source_location>
namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) { std::cerr << where.line() << '\n'; std::abort(); }
}
enum class Shape { kValid, kAmbiguousSlot, kBadSlot, kReservedName };
void Run(Shape shape) {
  constexpr std::uint32_t kPool = 64, kMethod = 100, kLiteral = 128, kText = 200;
  constexpr unsigned kByteBits = 8;
  constexpr std::uint16_t kSlots = 2;
  std::vector<std::uint8_t> bytes(300);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  for (unsigned i = 0; i < kSlots; ++i) word(kPool + i * sizeof(kLiteral), kLiteral);
  // Literal count counts tag/value entries. Scope shape is count,(name,slot)*.
  word(kLiteral, 2 * (1 + 2 * kSlots));
  bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kLiteral + 5,kSlots);
  constexpr std::size_t kEncodedEntry = 1 + sizeof(std::uint32_t), kTextStride = 32;
  for (unsigned i = 0; i < kSlots; ++i) {
    const auto offset = kLiteral + sizeof(std::uint32_t) + kEncodedEntry * (1 + 2 * i);
    bytes[offset] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(offset + 1,kText + i * kTextStride);
    bytes[offset + kEncodedEntry] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    word(offset + kEncodedEntry + 1,shape == Shape::kAmbiguousSlot ? 0 : shape == Shape::kBadSlot ? kSlots : i);
    const auto name = shape == Shape::kReservedName ? std::string("eval") : "cell" + std::to_string(i);
    bytes[kText + i * kTextStride] = static_cast<std::uint8_t>(name.size() * 2 + 1);
    std::copy(name.begin(),name.end(),bytes.begin() + kText + i * kTextStride + 1);
  }
  char path[] = "/tmp/abcd-cell-names-XXXXXX";
  const auto fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd,bytes.data(),bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index; index.regions.push_back({kMethod,kLiteral,{}, {kSlots,kPool},{},{}});
  std::uint64_t previous = 0;
  for (const unsigned count : {1U,2U,200U,201U,402U,804U}) {
    // Isolate metadata resolution; decoded record fixtures separately prove
    // cell ownership, child captures and final name visibility.
    analysis::ValueProgram graph;
    for (unsigned i = 0; i < count; ++i) {
      analysis::ValueOp op{}; op.operation = analysis::ValueEnvironmentPush{kSlots,abc::PoolIndex{i % kSlots}};
      graph.ops.push_back(op);
    }
    std::vector<std::u16string> names; std::vector<LocalNameSyntax> hints;
    const auto result = ResolveCellNames(file,header,index,{kMethod},graph,names,hints);
    if (shape == Shape::kBadSlot) { Check(std::get<TreeConstantError>(result) == TreeConstantError::kLexicalName); continue; }
    const auto cost = std::get<CellNameStorage>(result);
    Check(hints.size() == (shape == Shape::kValid ? count * kSlots : 0));
    for (std::size_t i = 0; i < hints.size(); ++i) {
      Check(hints[i].allocation.value == i / kSlots && hints[i].slot.value == i % kSlots);
      Check(names[hints[i].name.value] == (i % kSlots ? u"cell1" : u"cell0"));
    }
    if (count == 402 || count == 804) Check(cost.work < previous * 3);
    previous = cost.work;
    names.clear(); names.shrink_to_fit(); hints.clear(); hints.shrink_to_fit();
    analysis::SemanticBudget small; small.work = cost.work - 1;
    Check(std::get<TreeConstantError>(ResolveCellNames(file,header,index,{kMethod},graph,names,hints,small)) == TreeConstantError::kWorkLimit);
  }
  Check(unlink(path) == 0);
}
}
int main() { for (const auto shape : {Shape::kValid,Shape::kAmbiguousSlot,Shape::kBadSlot,Shape::kReservedName}) Run(shape); }
