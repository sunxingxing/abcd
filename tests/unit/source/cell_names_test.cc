#include "../../../src/source/cell_names.h"
#include "../../../src/source/parameter_cell_names.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
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
void Parameters() {
  using namespace analysis;
  const auto run = [](std::vector<std::uint8_t> code, std::vector<std::u16string> names,
      std::vector<std::u16string> expected, bool existing = false) {
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 2, {false, false, false, false}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
    const auto cells = std::get<CellPlan>(BuildCellPlan(graph, environments));
    FunctionSyntax syntax; syntax.parameters.resize(frame.parameters); syntax.names = names;
    for (std::size_t i = 0; i < cells.cells.size(); ++i) {
      const auto& storage = std::get<AllocatedCell>(cells.cells[i].storage);
      syntax.locals.push_back({storage.allocation, storage.slot, {static_cast<std::uint32_t>(i)}});
    }
    if (existing) {
      syntax.names.push_back(u"original");
      syntax.parameters[0] = {NameId{static_cast<std::uint32_t>(syntax.names.size() - 1)}, NameEvidence::kDebugRange};
    }
    const auto original = syntax;
    const auto cost = std::get<ParameterCellNameCost>(NameCellParameters(graph, cells, syntax));
    for (std::size_t i = 0; i < syntax.parameters.size(); ++i) {
      const auto& p = syntax.parameters[i];
      Check(p.name ? syntax.names[p.name->value] == expected[i] : expected[i].empty());
      if (p.name) Check(p.evidence == (existing && i == 0 ? NameEvidence::kDebugRange : NameEvidence::kMetadata));
    }
    auto limited = original;
    Check(std::get<ParameterCellNameError>(NameCellParameters(graph, cells, limited,
        {1024 * 1024, cost.work - 1})) == ParameterCellNameError::kWorkLimit);
    for (std::size_t i = 0; i < limited.parameters.size(); ++i)
      Check(limited.parameters[i].name == original.parameters[i].name);
    Check(std::get<ParameterCellNameError>(NameCellParameters(graph, cells, limited, {0})) ==
        ParameterCellNameError::kResourceLimit);
    auto changed = cells; ++changed.revision.value;
    Check(std::get<ParameterCellNameError>(NameCellParameters(graph, changed, limited)) == ParameterCellNameError::kInput);
  };
  // Real instructions and cell topology, never slot zero as a parameter ABI.
  // The parameters are physical registers 2/3; naming follows their entry SSA.
  run({0xb6, 1, 0, 0, 0x60, 2, 0x3d, 0, 0x65}, {u"captured"}, {u"captured", u""});
  run({0xb6, 1, 0, 0, 0x44, 0x20, 0x60, 0, 0x61, 1, 0xd5, 0x4d, 2,
      0x60, 1, 0x3d, 0, 0x65}, {u"captured"}, {u"captured", u""});
  run({0xb6, 1, 0, 0, 0x60, 2, 0x4f, 6, 0x44, 0x20, 0x4d, 4, 0x44, 0x20,
      0x60, 0, 0x3d, 0, 0x65}, {u"captured"}, {u"captured", u""});
  run({0xb6, 1, 0, 0, 0x60, 2, 0x4f, 6, 0x44, 0x20, 0x4d, 4, 0x44, 0x30,
      0x60, 0, 0x3d, 0, 0x65}, {u"captured"}, {u"", u""});
  run({0xb6, 1, 0, 0, 0x00, 0x3d, 0, 0x65}, {u"unrelated"}, {u"", u""});
  run({0xb6, 2, 0, 0, 0x60, 2, 0x3d, 0, 0x60, 3, 0x3d, 0x10, 0x65},
      {u"first", u"second"}, {u"first", u"second"});
  run({0xb6, 2, 0, 0, 0x60, 2, 0x3d, 0, 0x60, 3, 0x3d, 0x10, 0x65},
      {u"same", u"same"}, {u"", u""});
  run({0xb6, 2, 0, 0, 0x60, 2, 0x3d, 0, 0x3d, 0x10, 0x65},
      {u"first", u"second"}, {u"", u""});
  run({0xb6, 2, 0, 0, 0x60, 2, 0x3d, 0, 0x3d, 0x10, 0x65},
      {u"same", u"same"}, {u"same", u""});
  run({0xb6, 2, 0, 0, 0x60, 2, 0x3d, 0, 0x60, 3, 0x3d, 0x10, 0x65},
      {u"first", u"original"}, {u"original", u""}, true);
  run({0xb6, 1, 0, 0, 0x60, 2, 0x3d, 0, 0x00, 0x3d, 0, 0x65},
      {u"mutable"}, {u"mutable", u""});
  run({0xb6, 1, 0, 0, 0x44, 0x20, 0x60, 3, 0x4f, 8, 0x44, 0x00, 0x4d,
      static_cast<std::uint8_t>(-4), 0xd5, 0xd5, 0x60, 0, 0x3d, 0, 0x65},
      {u"loop"}, {u"loop", u""});
  std::uint64_t previous = 0;
  for (const auto count : {200U, 201U, 402U, 804U}) {
    std::vector<std::uint8_t> code;
    for (std::uint32_t i = 0; i < count; ++i)
      code.insert(code.end(), {0xb6, 1, 0, 0, 0x60, 0, 0x3d, 0, 0x69});
    code.push_back(0x65);
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
    const auto cells = std::get<CellPlan>(BuildCellPlan(graph, environments));
    FunctionSyntax syntax; syntax.parameters.resize(1); syntax.names = {u"retained"};
    for (const auto& cell : cells.cells) {
      const auto& local = std::get<AllocatedCell>(cell.storage);
      syntax.locals.push_back({local.allocation, local.slot, {0}});
    }
    const auto cost = std::get<ParameterCellNameCost>(NameCellParameters(graph, cells, syntax));
    Check(syntax.parameters[0].name == NameId{0});
    if (count >= 402) Check(cost.work < previous * 3);
    previous = cost.work;
  }
}
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
int main() {
  Parameters();
  for (const auto shape : {Shape::kValid,Shape::kAmbiguousSlot,Shape::kBadSlot,Shape::kReservedName}) Run(shape);
}
