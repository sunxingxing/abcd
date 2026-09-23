#include <unistd.h>

#include <algorithm>
#include <bit>
#include <sstream>

#include "abcd/abc/literal_array.h"
#include "abcd/source/acyclic_body.h"
#include "abcd/source/linear_body.h"
#include "abcd/source/local_lexical_body.h"
#include "analysis/ssa_fixture.h"

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64;
  constexpr std::uint32_t kMethod = 210;
  constexpr std::uint32_t kLiteral = 300;
  constexpr std::uint32_t kKey = 500;
  constexpr std::uint32_t kPrimitiveLiteral = 11000;
  constexpr std::uint32_t kPrimitiveKeys = 900;
  constexpr std::uint32_t kPrimitiveValue = 980;
  constexpr std::uint32_t kFieldName = 1080;
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
  const auto body = test::Body({0x07, 0, 0, 0, 0x64});
  std::ostringstream output;
  const auto result = source::WriteLinearBody(body, {"acc", {}}, output, {},
                                              &constants);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(output.str() ==
              "acc = ({[\"text\"]: null});\nreturn acc;\n");
  const auto rejection = [&](std::uint8_t slot,
                             source::ObjectBufferError expected) {
    const auto candidate = test::Body({0x07, 0, slot, 0, 0x64});
    std::ostringstream rejected;
    const auto result = source::WriteLinearBody(
        candidate, {"acc", {}}, rejected, {}, &constants);
    test::Check(std::get<source::BodyEmitFailure>(result).object_error ==
                expected);
    test::Check(rejected.str().empty());
  };
  rejection(1, source::ObjectBufferError::kDuplicateKey);
  rejection(2, source::ObjectBufferError::kValue);
  const auto proto = test::Body({0x07, 0, 3, 0, 0x64});
  std::ostringstream proto_source;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      proto, {"acc", {}}, proto_source, {}, &constants)));
  test::Check(proto_source.str().find("[\"__proto__\"]: null") !=
              std::string::npos);
  const auto primitive = test::Body({0x82, 0, 0, 7, 0, 0x64});
  std::ostringstream primitive_source;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      primitive, {"acc", {}}, primitive_source, {}, &constants)));
  test::Check(primitive_source.str() ==
              "acc = ({[\"bool\"]: true, [\"uint\"]: 4294967295, "
              "[\"double\"]: (1.5), [\"str\"]: \"hello\"});\nreturn acc;\n");
  const std::array<std::string_view, 2> fields{"object", "value"};
  const auto field = test::Body({0x07, 0, 0, 0, 0x61, 0, 0x60, 1,
                                 0xdc, 0, 8, 0, 0, 0x60, 0, 0x64});
  std::ostringstream field_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(field, {"acc", fields}, field_source, {},
                                    &constants)));
  test::Check(field_source.str() ==
              "acc = ({[\"text\"]: null});\nobject = acc;\nacc = value;\n"
              "object = ({...object, [\"field\"]: acc});\n"
              "acc = object;\nreturn acc;\n");
  const std::array<std::string_view, 3> more_fields{"object", "first", "last"};
  const auto redefine = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x60, 2, 0xdc, 0, 10, 0, 0, 0x60, 0, 0x64});
  std::ostringstream redefine_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(redefine, {"acc", more_fields},
                                    redefine_source, {}, &constants)));
  test::Check(redefine_source.str().find(
                  "object = ({...object, [\"text\"]: acc});") !=
              std::string::npos);
  const auto own_proto = test::Body({0x07, 0, 0, 0, 0x61, 0, 0x60, 1,
                                     0xdc, 0, 9, 0, 0, 0x60, 0, 0x64});
  std::ostringstream own_proto_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(own_proto, {"acc", fields},
                                    own_proto_source, {}, &constants)));
  test::Check(own_proto_source.str().find("[\"__proto__\"]: acc") !=
              std::string::npos);
  const auto preserved = test::Body({0x07, 0, 0, 0, 0x61, 0, 0x60, 1,
                                     0xdc, 0, 8, 0, 0, 0x64});
  std::ostringstream preserved_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(preserved, {"acc", fields},
                                    preserved_source, {}, &constants)));
  test::Check(preserved_source.str().ends_with("return acc;\n"));
  const std::array<std::string_view, 3> aliased{"object", "value", "alias"};
  const auto escaped = test::Body({0x07, 0, 0, 0, 0x61, 0, 0x60, 0,
                                   0x61, 2, 0x60, 1, 0xdc, 0, 8, 0, 0,
                                   0x60, 2, 0x64});
  std::ostringstream escaped_source;
  test::Check(!std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(escaped, {"acc", aliased}, escaped_source,
                                    {}, &constants)));
  test::Check(escaped_source.str().empty());
  const auto built = analysis::BuildNormalFlow(body, {1024 * 1024});
  test::Check(std::holds_alternative<analysis::NormalFlow>(built));
  const auto& graph = std::get<analysis::NormalFlow>(built);
  const auto analyzed =
      analysis::AnalyzeAcyclicAccumulator(body, graph, 0, {1024 * 1024});
  test::Check(std::holds_alternative<analysis::AccumulatorFlow>(analyzed));
  const auto& flow = std::get<analysis::AccumulatorFlow>(analyzed);
  source::AcyclicBudget no_object_scratch;
  no_object_scratch.bytes = graph.blocks.capacity() *
                                sizeof(analysis::FlowBlock) +
                            flow.allocated_bytes;
  std::ostringstream ordered_rejected;
  const auto ordered = source::WriteAcyclicBody(
      body, {"acc", {}}, ordered_rejected, no_object_scratch, &constants);
  test::Check(std::holds_alternative<source::BodyEmitFailure>(ordered));
  test::Check(std::get<source::BodyEmitFailure>(ordered).kind ==
              source::BodyEmitError::kResourceLimit);
  test::Check(ordered_rejected.str().empty());
  for (std::size_t scale = 0; scale < kScaleCounts.size(); ++scale) {
    const auto candidate = test::Body(
        {0x07, 0, static_cast<std::uint8_t>(scale + 4), 0, 0x64});
    std::ostringstream rendered;
    const auto result = source::WriteLinearBody(
        candidate, {"acc", {}}, rendered, {}, &constants);
    test::Check(std::holds_alternative<std::size_t>(result));
    test::Check(rendered.str().find("[\"k" +
                                    std::to_string(kScaleCounts[scale] - 1) +
                                    "\"]: null") != std::string::npos);
  }
  std::ostringstream exhausted;
  const auto limited = source::WriteLinearBody(
      body, {"acc", {}}, exhausted, {}, &constants,
      source::ObjectBufferBudget{0});
  test::Check(std::get<source::BodyEmitFailure>(limited).kind ==
              source::BodyEmitError::kResourceLimit);
  test::Check(exhausted.str().empty());
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export function object() { let acc;\n" << output.str()
              << "}\n";
    std::cout << "export function proto() { let acc;\n" << proto_source.str()
              << "}\n";
    std::cout << "export function primitive() { let acc;\n"
              << primitive_source.str() << "}\n";
    std::cout << "export function field(value) { let acc, object;\n"
              << field_source.str() << "}\n";
    std::cout << "export function redefine(first, last) { let acc, object;\n"
              << redefine_source.str() << "}\n";
    std::cout << "export function ownProto(value) { let acc, object;\n"
              << own_proto_source.str() << "}\n";
    std::cout << "export function preserved(value) { let acc, object;\n"
              << preserved_source.str() << "}\n";
  }
  test::Check(unlink(path) == 0);
}
