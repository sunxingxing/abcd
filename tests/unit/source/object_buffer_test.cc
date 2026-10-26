#include <unistd.h>

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

#include "abcd/abc/literal_array.h"
#include "abcd/source/method_context.h"
#include "analysis/ssa_fixture.h"
#include "source/allocation_actions.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/analysis/value_import.h"

int main() {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64;
  constexpr std::uint32_t kMethod = 210;
  constexpr std::uint32_t kLiteral = 300;
  constexpr std::uint32_t kKey = 500;
  constexpr std::uint32_t kPrimitiveLiteral = 11000;
  constexpr std::uint32_t kPrimitiveKeys = 900;
  constexpr std::uint32_t kPrimitiveValue = 980;
  constexpr std::uint32_t kFieldName = 1080;
  constexpr std::uint8_t kIndexedKey = 201;
  std::vector<std::uint8_t> bytes(50000);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  word(kPool, kLiteral);
  word(kPool + 4, 320);
  word(kPool + 8, 360);
  word(kPool + 12, 380);
  word(kPool + 7 * sizeof(std::uint32_t), kPrimitiveLiteral);
  word(kPool + 8 * sizeof(std::uint32_t), kFieldName);
  word(kPool + 9 * sizeof(std::uint32_t), 520);
  word(kPool + 10 * sizeof(std::uint32_t), kKey);
  constexpr std::array<std::uint32_t, 3> kScaleLiterals{2000, 5000, 8000};
  for (std::size_t scale = 0; scale < kScaleLiterals.size(); ++scale)
    word(kPool + (scale + 4) * sizeof(std::uint32_t), kScaleLiterals[scale]);
  word(kLiteral, 4);
  bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kLiteral + 5, kKey);
  bytes[kLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kNull);
  word(320, 8);
  for (const auto at : {324U, 331U}) {
    bytes[at] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at + 1, kKey);
    bytes[at + 5] = static_cast<std::uint8_t>(abc::LiteralTag::kNull);
  }
  word(360, 4);
  bytes[364] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(365, kKey);
  bytes[369] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
  word(370, kMethod);
  word(380, 4);
  bytes[384] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(385, 520);
  bytes[389] = static_cast<std::uint8_t>(abc::LiteralTag::kNull);
  bytes[kKey] = 9;
  std::copy_n("text", 4, bytes.begin() + kKey + 1);
  bytes[520] = 19;
  std::copy_n("__proto__", 9, bytes.begin() + 521);
  constexpr std::array<std::uint32_t, 3> kScaleCounts{200, 201, 402};
  for (std::size_t scale = 0; scale < kScaleCounts.size(); ++scale) {
    word(kScaleLiterals[scale], 4 * kScaleCounts[scale]);
    std::size_t at = kScaleLiterals[scale] + sizeof(std::uint32_t);
    for (std::uint32_t i = 0; i < kScaleCounts[scale]; ++i) {
      bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
      word(at, 20000 + i * 8);
      at += sizeof(std::uint32_t);
      bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kNull);
      bytes[at++] = 0;
    }
  }
  for (std::uint32_t i = 0; i < kScaleCounts.back(); ++i) {
    const auto name = "k" + std::to_string(i);
    const auto at = 20000 + i * 8;
    bytes[at] = static_cast<std::uint8_t>(name.size() * 2 + 1);
    std::copy(name.begin(), name.end(), bytes.begin() + at + 1);
  }
  const auto string = [&](std::size_t at, std::string_view value) {
    bytes[at] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at + 1);
  };
  for (std::size_t i = 0; i < 4; ++i)
    string(kPrimitiveKeys + i * 20,
           std::array<std::string_view, 4>{"bool", "uint", "double", "str"}[i]);
  string(kPrimitiveValue, "hello");
  string(kFieldName, "field");
  word(kPrimitiveLiteral, 16);
  std::size_t at = kPrimitiveLiteral + sizeof(std::uint32_t);
  const auto key = [&](std::size_t index) {
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at, kPrimitiveKeys + index * 20);
    at += sizeof(std::uint32_t);
  };
  key(0);
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kBoolean);
  bytes[at++] = 1;
  key(1);
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(at, UINT32_MAX);
  at += sizeof(std::uint32_t);
  key(2);
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kDouble);
  const auto real = std::bit_cast<std::uint64_t>(1.5);
  for (std::size_t i = 0; i < sizeof(real); ++i)
    bytes[at++] = static_cast<std::uint8_t>(real >> (8 * i));
  key(3);
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(at, kPrimitiveValue);
  char path[] = "/tmp/abcd-object-buffer-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {11, kPool}, {}, {}});
  const source::MethodConstants constants{file, header, index, {kMethod},
                                          {1024}};
  for (const std::uint32_t slot : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
    analysis::ValueProgram values;
    values.origins.push_back({});
    for (const auto unused : {0, 1}) {
      static_cast<void>(unused);
      analysis::ValueOp allocation{};
      allocation.operation = analysis::ValueObjectAllocation{abc::PoolIndex{slot}};
      values.ops.push_back(allocation);
    }
    const auto typed = source::ResolveTreeConstants(file, header, index, {kMethod}, values);
    if (slot == 1 || slot == 2) {
      test::Check(std::get<source::TreeConstantError>(typed) == source::TreeConstantError::kObjectLiteral);
      continue;
    }
    const auto& resolved = std::get<source::TreeConstants>(typed);
    const auto first = std::get<source::AggregateConstant>(resolved.operations[0]);
    const auto second = std::get<source::AggregateConstant>(resolved.operations[1]);
    test::Check(first.first == second.first && first.count == second.count && resolved.properties.size() == first.count);
    if (slot >= 4 && slot <= 6) test::Check(first.count == kScaleCounts[slot - 4]);
    if (slot == 3) test::Check(resolved.names[resolved.properties[0].key.value] == u"__proto__");
    if (slot == 7) {
      test::Check(first.count == 4 && std::get<source::BooleanLiteral>(resolved.properties[0].value).value);
      test::Check(std::bit_cast<double>(std::get<source::NumberLiteral>(resolved.properties[1].value).bits) == UINT32_MAX);
      test::Check(std::get<source::NumberLiteral>(resolved.properties[2].value).bits == real);
      test::Check(resolved.texts[std::get<source::TextId>(resolved.properties[3].value).value] == u"hello");
    }
    analysis::SemanticBudget work_limit; work_limit.work = resolved.work - 1;
    test::Check(std::get<source::TreeConstantError>(source::ResolveTreeConstants(
        file, header, index, {kMethod}, values, work_limit)) == source::TreeConstantError::kWorkLimit);
    const auto encoded = test::Body({0x07, 0, static_cast<std::uint8_t>(slot), 0, 0x64});
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {}));
    const auto input = std::get<analysis::SemanticMethod>(analysis::BuildSemanticMethod(encoded, header.profile, frame.registers));
    const auto proof = std::get<analysis::SemanticProof>(analysis::VerifySemanticSource(encoded, input, frame.registers, frame.locals));
    const auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(encoded, input, proof, frame));
    const auto resolved_tree = std::get<source::TreeConstants>(source::ResolveTreeConstants(file, header, index, {kMethod}, graph));
    const auto control = std::get<source::ControlPlan>(source::BuildControlPlan(graph));
    const auto evaluation = std::get<source::EvaluationPlan>(source::BuildEvaluationPlan(graph));
    source::FunctionSyntax syntax{};
    syntax.method = {kMethod}; syntax.callable = source::CallableKind::kFunction;
    syntax.strictness = source::Strictness::kStrict;
    syntax.names = resolved_tree.names; syntax.texts = resolved_tree.texts;
    const auto built = std::get<source::ConstructedFunction>(source::BuildFunctionTree(graph,
        control, evaluation, syntax, resolved_tree.View()));
    const auto& tree=built.tree;
    test::Check(built.flow&&built.flow->original);
    test::CheckAllocationActions(tree,*built.flow->original);
    if(slot==0) {
      syntax.identity.contract=source::SourceContract::kLanguageSourceV5;
      const auto observable=std::get<source::ConstructedFunction>(source::BuildFunctionTree(graph,
          control,evaluation,syntax,resolved_tree.View()));
      test::Check(observable.flow&&observable.flow->original);
      bool unmodeled=false;
      for(const auto& operation:observable.flow->original->operations)
        unmodeled|=operation.completion==source::OriginalCompletionBoundary::kUnmodeled;
      test::Check(!unmodeled);
      test::CheckAllocationActions(observable.tree,*observable.flow->original);
    }
    test::Check(tree.properties.size() == first.count);
    test::Check(std::holds_alternative<source::TreeSemantics>(source::VerifyTreeSemantics(
        graph, control, evaluation, tree, resolved_tree.View())));
    auto changed = tree;
    changed.expressions[changed.properties[0].value.value].expression = source::TreeLiteral{source::UndefinedLiteral{}};
    test::Check(std::holds_alternative<source::TreeSemanticError>(source::VerifyTreeSemantics(
        graph, control, evaluation, changed, resolved_tree.View())));
    changed = tree;
    test::Check(changed.origins.size() > 1);
    changed.properties[0].origin = source::TreeOriginId{1};
    test::Check(std::holds_alternative<source::TreeSemanticError>(source::VerifyTreeSemantics(
        graph, control, evaluation, changed, resolved_tree.View())));
  }
  test::Check(unlink(path) == 0);
}
