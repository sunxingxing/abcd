#include <unistd.h>

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

#include "abcd/abc/literal_array.h"
#include "abcd/source/acyclic_body.h"
#include "abcd/source/linear_body.h"
#include "abcd/source/local_lexical_body.h"
#include "analysis/ssa_fixture.h"
#include "fresh_object_fields.h"
#include "../../../src/source/tree_constants.h"

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
    const auto first = std::get<source::ObjectConstant>(resolved.operations[0]);
    const auto second = std::get<source::ObjectConstant>(resolved.operations[1]);
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
  }
  const auto empty = test::Body({0x04, 0x64});
  std::ostringstream empty_source;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      empty, {"acc", {}}, empty_source)));
  test::Check(empty_source.str() == "acc = ({});\nreturn acc;\n");
  const std::array<std::string_view, 2> empty_fields{"object", "value"};
  const auto empty_field = test::Body({0x04, 0x61, 0, 0x60, 1,
                                       0xdc, 0, 8, 0, 0, 0x60, 0, 0x64});
  std::ostringstream empty_field_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(empty_field, {"acc", empty_fields},
                                    empty_field_source, {}, &constants)));
  test::Check(empty_field_source.str() ==
              "acc = ({});\nobject = acc;\nacc = value;\n"
              "object = ({...object, [\"field\"]: acc});\n"
              "acc = object;\nreturn acc;\n");
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
  const auto define_field = test::Body({0x04, 0x61, 0, 0x60, 1,
                                        0xdb, 0, 8, 0, 0, 0x60, 0, 0x64});
  std::ostringstream define_field_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(define_field, {"acc", fields},
                                    define_field_source, {}, &constants)));
  test::Check(define_field_source.str() == empty_field_source.str());
  const auto indexed_field = test::Body({
      0x04, 0x61, 0, 0x60, 1,
      0xfb, 2, 0, kIndexedKey, 0, 0, 0, 0,
      0x60, 1, 0xfb, 2, 0, 0xff, 0xff, 0xff, 0xff, 0,
      0x60, 0, 0x64});
  std::ostringstream indexed_field_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(indexed_field, {"acc", fields},
                                    indexed_field_source, {}, &constants)));
  test::Check(indexed_field_source.str().find(
                  "[" + std::to_string(kIndexedKey) + "]: $abcdField0") !=
              std::string::npos);
  test::Check(indexed_field_source.str().find(
                  "[" + std::to_string(UINT32_MAX) + "]: acc") !=
              std::string::npos);
  const std::array<std::string_view, 3> keyed_fields{"object", "value", "key"};
  const auto string_key_field = test::Body({
      0x04, 0x61, 0, 0x3e, 8, 0, 0x61, 2, 0x60, 1,
      0xfb, 1, 0, 2, 0, 0x60, 0, 0x64});
  std::ostringstream string_key_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(string_key_field,
                                    {"acc", keyed_fields}, string_key_source,
                                    {}, &constants)));
  test::Check(string_key_source.str().find("[\"field\"]: acc") !=
              std::string::npos);
  const auto string_proto_field = test::Body({
      0x04, 0x61, 0, 0x3e, 9, 0, 0x61, 2, 0x60, 1,
      0xfb, 1, 0, 2, 0, 0x60, 0, 0x64});
  std::ostringstream string_proto_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(string_proto_field,
                                    {"acc", keyed_fields}, string_proto_source,
                                    {}, &constants)));
  test::Check(string_proto_source.str().find("[\"__proto__\"]: acc") !=
              std::string::npos);
  const auto dynamic_key_field = test::Body({
      0x04, 0x61, 0, 0x60, 2, 0x61, 2, 0x60, 1,
      0xfb, 1, 0, 2, 0, 0x60, 0, 0x64});
  std::ostringstream dynamic_key_source;
  test::Check(std::holds_alternative<source::BodyEmitFailure>(
      source::WriteLocalLexicalBody(dynamic_key_field,
                                    {"acc", keyed_fields}, dynamic_key_source,
                                    {}, &constants)));
  test::Check(dynamic_key_source.str().empty());
  const auto overwritten_key = test::Body({
      0x04, 0x61, 0, 0x3e, 8, 0, 0x61, 2, 0x60, 1, 0x61, 2,
      0x60, 1, 0xfb, 1, 0, 2, 0, 0x60, 0, 0x64});
  std::ostringstream overwritten_source;
  test::Check(std::holds_alternative<source::BodyEmitFailure>(
      source::WriteLocalLexicalBody(overwritten_key,
                                    {"acc", keyed_fields}, overwritten_source,
                                    {}, &constants)));
  test::Check(overwritten_source.str().empty());
  const std::array<std::string_view, 3> more_fields{"object", "first", "last"};
  const auto redefine = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x60, 2, 0xdc, 0, 10, 0, 0, 0x60, 0, 0x64});
  const auto field_limit = source::BuildFreshFieldPlan(
      redefine, more_fields.size(), 2 * sizeof(source::FreshFieldSite) - 1);
  test::Check(std::get<source::BodyEmitFailure>(field_limit).kind ==
              source::BodyEmitError::kResourceLimit);
  std::ostringstream redefine_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(redefine, {"acc", more_fields},
                                    redefine_source, {}, &constants)));
  test::Check(redefine_source.str().find("const $abcdField0 = acc;\n") !=
              std::string::npos);
  const auto imported_between = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x7e, 0, 0x60, 2, 0xdc, 0, 10, 0, 0, 0x60, 0, 0x64});
  const auto imported_fields = source::BuildFreshFieldPlan(
      imported_between, more_fields.size(), 1024);
  test::Check(std::get<source::FreshFieldPlan>(imported_fields).sites.size() ==
              2);
  test::Check(redefine_source.str().find(
                  "object = ({...object, [\"field\"]: $abcdField0, "
                  "[\"text\"]: acc});") != std::string::npos);
  const auto spread = redefine_source.str().find("...object");
  test::Check(spread != std::string::npos &&
              redefine_source.str().find("...object", spread + 1) ==
                  std::string::npos);
  const std::array<std::string_view, 4> effect_fields{
      "object", "first", "last", "effect"};
  const auto intervening = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x60, 3, 0x29, 0, 0x60, 2, 0xdc, 0, 10, 0, 0, 0x60, 0, 0x64});
  std::ostringstream intervening_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(intervening, {"acc", effect_fields},
                                    intervening_source, {}, &constants)));
  const auto captured = intervening_source.str().find(
      "const $abcdField0 = acc;\n");
  const auto called = intervening_source.str().find("acc = (0, acc)();");
  const auto published = intervening_source.str().find(
      "object = ({...object, [\"field\"]: $abcdField0, "
      "[\"text\"]: acc});");
  test::Check(captured != std::string::npos && called != std::string::npos &&
              published != std::string::npos && captured < called &&
              called < published);
  const auto separate = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x07, 0, 0, 0, 0x61, 3, 0x60, 2, 0xdc, 0, 10, 0, 3,
      0x60, 3, 0x64});
  std::ostringstream separate_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(separate, {"acc", effect_fields},
                                    separate_source, {}, &constants)));
  test::Check(separate_source.str().find("object = ({...object") !=
              std::string::npos);
  test::Check(separate_source.str().find("effect = ({...effect") !=
              std::string::npos);
  const auto reused = test::Body({
      0x07, 0, 0, 0, 0x61, 0, 0x60, 1, 0xdc, 0, 8, 0, 0,
      0x07, 0, 0, 0, 0x61, 0, 0x60, 2, 0xdc, 0, 10, 0, 0,
      0x60, 0, 0x64});
  std::ostringstream reused_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(reused, {"acc", more_fields},
                                    reused_source, {}, &constants)));
  const auto first_chain = reused_source.str().find("object = ({...object");
  test::Check(first_chain != std::string::npos &&
              reused_source.str().find("object = ({...object", first_chain + 1) !=
                  std::string::npos);
  std::size_t previous_size = 0;
  constexpr std::array kDefinitionForms{
      abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8,
      abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8,
      abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8,
      abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8};
  for (const std::uint32_t count : {200U, 201U, 402U}) {
    std::vector<std::uint8_t> encoded{
        0x07, 0, 0, 0, 0x61, 0, 0x3e, 8, 0, 0x61, 2};
    for (std::uint32_t i = 0; i < count; ++i) {
      encoded.insert(encoded.end(), {0x60, 1});
      const auto form = kDefinitionForms[i % kDefinitionForms.size()];
      if (form == abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8)
        encoded.insert(encoded.end(), {0xfb, 2, 0, kIndexedKey, 0, 0, 0, 0});
      else if (form == abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8)
        encoded.insert(encoded.end(), {0xfb, 1, 0, 2, 0});
      else
        encoded.insert(encoded.end(),
                       {static_cast<std::uint8_t>(form), 0, 8, 0, 0});
    }
    encoded.insert(encoded.end(), {0x60, 0, 0x64});
    const auto many = test::Body(std::span(encoded));
    std::ostringstream many_source;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(many, {"acc", keyed_fields}, many_source, {},
                                      &constants)));
    const auto text = many_source.str();
    const auto first_spread = text.find("...object");
    test::Check(first_spread != std::string::npos &&
                text.find("...object", first_spread + 1) ==
                    std::string::npos);
    test::Check(text.find("$abcdField" + std::to_string(count - 2)) !=
                std::string::npos);
    if (previous_size && count == 402)
      test::Check(text.size() < previous_size * 3);
    previous_size = text.size();
  }
  const std::array<std::string_view, 3> colliding{
      "object", "first", "$abcdField0"};
  std::ostringstream collision;
  test::Check(std::get<source::BodyEmitFailure>(
                  source::WriteLocalLexicalBody(
                      redefine, {"acc", colliding}, collision, {}, &constants))
                  .emit_error == source::EmitError::kInvalidBinding);
  test::Check(collision.str().empty());
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
    std::cout << "export function empty() { let acc;\n" << empty_source.str()
              << "}\n";
    std::cout << "export function emptyField(value) { let acc, object;\n"
              << empty_field_source.str() << "}\n";
    std::cout << "export function definedField(value) { let acc, object;\n"
              << define_field_source.str() << "}\n";
    std::cout << "export function indexedField(value) { let acc, object;\n"
              << indexed_field_source.str() << "}\n";
    std::cout << "export function stringKeyField(value) { "
                 "let acc, object, key;\n"
              << string_key_source.str() << "}\n";
    std::cout << "export function stringProtoField(value) { "
                 "let acc, object, key;\n"
              << string_proto_source.str() << "}\n";
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
    std::cout << "export function intervening(first, last, effect) { "
                 "let acc, object;\n"
              << intervening_source.str() << "}\n";
    std::cout << "export function ownProto(value) { let acc, object;\n"
              << own_proto_source.str() << "}\n";
    std::cout << "export function preserved(value) { let acc, object;\n"
              << preserved_source.str() << "}\n";
  }
  test::Check(unlink(path) == 0);
}
