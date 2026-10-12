#include <unistd.h>
#include <bit>
#include <iostream>
#include <sstream>
#include "analysis/ssa_fixture.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/analysis/value_import.h"
#include "abcd/source/tree_printer.h"

int main(int argc, char**) {
  using namespace abcd;
  using namespace abcd::source;
  constexpr std::uint32_t kPool = 64, kMethod = 210, kText = 400;
  constexpr std::array<std::uint32_t, 6> kCounts{0, 1, 6, 200, 201, 402};
  constexpr std::uint32_t kBufferStart = 1000, kBufferStride = 3000;
  std::vector<std::uint8_t> bytes(kBufferStart + kBufferStride * (kCounts.size() + 1));
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = value >> (8 * i);
  };
  bytes[kText] = 11; // MUTF8 length/ASCII flag, "hello".
  std::copy_n("hello", 5, bytes.begin() + kText + 1);
  for (std::size_t slot = 0; slot < kCounts.size(); ++slot) {
    const auto offset = kBufferStart + slot * kBufferStride;
    word(kPool + slot * sizeof(std::uint32_t), offset);
    word(offset, kCounts[slot] * 2); // Wire count includes each tag and value.
    auto at = offset + sizeof(std::uint32_t);
    for (std::uint32_t i = 0; i < kCounts[slot]; ++i) {
      const auto tag = slot == 2 ? std::array{abc::LiteralTag::kNull, abc::LiteralTag::kBoolean,
          abc::LiteralTag::kInteger, abc::LiteralTag::kDouble, abc::LiteralTag::kString,
          abc::LiteralTag::kDouble}[i] : abc::LiteralTag::kInteger;
      bytes[at++] = static_cast<std::uint8_t>(tag);
      switch (tag) {
        case abc::LiteralTag::kNull: bytes[at++] = 0; break;
        case abc::LiteralTag::kBoolean: bytes[at++] = 1; break;
        case abc::LiteralTag::kInteger: word(at, slot == 2 ? UINT32_MAX : i); at += 4; break;
        case abc::LiteralTag::kString: word(at, kText); at += 4; break;
        case abc::LiteralTag::kDouble: {
          const auto bits = std::bit_cast<std::uint64_t>(i == 3 ? -0.0 : 1.5);
          for (std::size_t b = 0; b < sizeof(bits); ++b) bytes[at++] = bits >> (8 * b);
          break;
        }
        default: std::abort();
      }
    }
  }
  const auto unsupported = kBufferStart + kCounts.size() * kBufferStride;
  word(kPool + kCounts.size() * sizeof(std::uint32_t), unsupported);
  word(unsupported, 2);
  bytes[unsupported + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
  word(unsupported + 5, kMethod);
  char path[] = "/tmp/abcd-array-constants-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.file_size = bytes.size(); header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {kCounts.size() + 1, kPool}, {}, {}});
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    header.profile = profile;
    for (std::uint32_t slot = 0; slot <= kCounts.size(); ++slot) {
      const auto body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16),
          0, static_cast<std::uint8_t>(slot), 0, static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
      const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {}));
      const auto input = std::get<analysis::SemanticMethod>(analysis::BuildSemanticMethod(body, profile, frame.registers));
      const auto proof = std::get<analysis::SemanticProof>(analysis::VerifySemanticSource(body, input, frame.registers, frame.locals));
      const auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, input, proof, frame));
      const auto decoded = ResolveTreeConstants(file, header, index, {kMethod}, graph);
      if (slot == kCounts.size()) {
        test::Check(std::get<TreeConstantError>(decoded) == TreeConstantError::kObjectLiteral); continue;
      }
      if (const auto* error = std::get_if<TreeConstantError>(&decoded)) std::cerr << "constant error=" << static_cast<unsigned>(*error) << '\n';
      const auto& constants = std::get<TreeConstants>(decoded);
      const auto aggregate = std::get<AggregateConstant>(constants.operations[0]);
      test::Check(aggregate.kind == AggregateKind::kArray && aggregate.count == kCounts[slot]);
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
      FunctionSyntax syntax{}; syntax.method = {kMethod}; syntax.callable = CallableKind::kFunction;
      syntax.strictness = Strictness::kStrict; syntax.names = constants.names; syntax.texts = constants.texts;
      const auto built = BuildFunctionTree(graph, control, evaluation, syntax, constants.View());
      if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) std::cerr << "tree error=" << static_cast<unsigned>(error->kind) << '\n';
      const auto tree = std::get<ConstructedFunction>(built).tree;
      test::Check(tree.properties.size() == kCounts[slot]);
      auto wrong_constants = constants;
      std::get<AggregateConstant>(wrong_constants.operations[0]).kind = AggregateKind::kObject;
      test::Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, wrong_constants.View())));
      if (!tree.properties.empty()) {
        auto changed = tree;
        changed.expressions[changed.properties.front().value.value].expression = TreeLiteral{UndefinedLiteral{}};
        test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, constants.View())));
      }
      std::ostringstream output;
      test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
      if (argc > 1 && profile == abc::ProfileId::kDynamic1206 && slot == 2) std::cout << output.str();
      // Repeated uses share decoded primitive storage, never the allocated array.
      auto repeated = graph;
      repeated.ops.push_back(graph.ops.front());
      const auto twice = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, {kMethod}, repeated));
      const auto alias = std::get<AggregateConstant>(twice.operations.back());
      test::Check(alias.first == aggregate.first && twice.properties.size() == constants.properties.size());
    }
  }
  test::Check(unlink(path) == 0);
}
