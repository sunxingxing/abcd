#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "abcd/source/acyclic_body.h"
#include "abcd/source/base_function.h"
#include "abcd/source/linear_body.h"
#include "abcd/source/local_lexical_body.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::size_t at,
          std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (8 * i);
}
}  // namespace
int main(int argc, char** argv) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(1024);
  Word(bytes, 64, 512);
  Word(bytes, 68, 1023);  // Invalid string payload at second slot.
  struct NameCase {
    std::string_view name;
    bool element;
  };
  constexpr std::array name_cases{
      NameCase{"0", true},           NameCase{"4294967294", true},
      NameCase{"4294967295", false}, NameCase{"01", false},
      NameCase{"-0", false},         NameCase{"9999999999", false},
      NameCase{"", false},           NameCase{"1.0", false},
      NameCase{"globalValue", false}};
  std::size_t string_at = 600;
  for (std::size_t i = 0; i < name_cases.size(); ++i) {
    Word(bytes, 72 + i * sizeof(std::uint32_t), string_at);
    bytes[string_at++] =
        static_cast<std::uint8_t>(name_cases[i].name.size() * 2 + 1);
    for (const auto ch : name_cases[i].name) bytes[string_at++] = ch;
    bytes[string_at++] = 0;
  }
  const std::array<std::uint8_t, 10> text{10,   'A',  0xc0, 0x80, 0xed,
                                          0xa0, 0x80, '"',  '\\', 0};
  std::copy(text.begin(), text.end(), bytes.begin() + 512);
  bytes[1023] = 0x80;
  char path[] = "/tmp/abcd-linear-constants-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {2 + name_cases.size(), 64}, {}, {}});
  source::MethodConstants constants{file, header, index, {210}, {1024}};
  Check(source::AllowsGlobalReference(u"globalValue", {{}, "Container"}));
  for (const auto invalid : {u"", u"1name", u"a-b", u"$acc", u"await",
                             u"eval", u"arguments", u"ClassValue",
                             u"MethodSelf0", u"Container", u"名"})
    Check(!source::AllowsGlobalReference(invalid, {{}, "Container"}));
  const std::array<std::string_view, 2> outer{"Other", "globalValue"};
  Check(!source::AllowsGlobalReference(u"globalValue",
                                       {outer, "Container"}));
  Check(!source::AllowsGlobalReference(u"globalValue",
                                       {{}, "Container", "globalValue"}));
  for (const std::size_t count : {200U, 201U, 402U}) {
    std::vector<std::string> stored;
    stored.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
      stored.push_back("binding" + std::to_string(i));
    std::sort(stored.begin(), stored.end());
    std::vector<std::string_view> names;
    names.reserve(count);
    for (const auto& name : stored) names.push_back(name);
    Check(source::AllowsGlobalReference(u"globalValue",
                                        {names, "Container"}));
    Check(!source::AllowsGlobalReference(u"binding200",
                                         {names, "Container"}) ==
          (count > 200));
  }
  abc::MethodBody body;
  const std::array<std::uint8_t, 3> load{0x3e, 0, 0};
  const std::array<std::uint8_t, 1> ret{0x64};
  body.instructions.push_back(
      {{0},
       std::get<abc::DecodedInstruction>(
           abc::DecodeInstruction(load, header.profile)),
       {}});
  body.instructions.push_back({{3},
                               std::get<abc::DecodedInstruction>(
                                   abc::DecodeInstruction(ret, header.profile)),
                               {}});
  std::ostringstream output;
  const auto result =
      source::WriteLinearBody(body, {"acc", {}}, output, {}, &constants);
  Check(std::get<std::size_t>(result) == output.str().size());
  Check(output.str() == "acc = \"A\\u0000\\ud800\\\"\\\\\";\nreturn acc;\n");
  // The historical wire form decodes, but the v7 constructor semantics are
  // not automatically a source proof for this method's 12.0.6 profile.
  abc::MethodBody historical_construct;
  const std::array<std::uint8_t, 4> construct_code{0x08, 0, 1, 0};
  historical_construct.instructions.push_back(
      {{0},
       std::get<abc::DecodedInstruction>(
           abc::DecodeInstruction(construct_code, header.profile)),
       {}});
  historical_construct.instructions.push_back(
      {{4},
       std::get<abc::DecodedInstruction>(
           abc::DecodeInstruction(ret, header.profile)),
       {}});
  const std::array<std::string_view, 1> constructor_name{"ctor"};
  std::ostringstream refused_construct;
  Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
            historical_construct, {"acc", constructor_name},
            refused_construct, {}, &constants))
            .kind == source::BodyEmitError::kUnsupportedInstruction);
  Check(refused_construct.str().empty());
  const std::array<std::string_view, 1> registers{"object"};
  std::array<std::string, 2> named_sources;
  std::array<std::string, 2> store_sources;
  std::string collision_source;
  const std::array<std::string_view, 2> store_registers{"object", "value"};
  for (std::size_t form = 0; form < store_sources.size(); ++form) {
    const std::vector<std::uint8_t> encoded =
        form == 0 ? std::vector<std::uint8_t>{0x60, 1, 0x43, 0, 0, 0, 0, 0x64}
                  : std::vector<std::uint8_t>{0x60, 1, 0x91, 0xff, 0xff,
                                              0,    0, 0,    0x64};
    abc::MethodBody store;
    for (std::size_t pc = 0; pc < encoded.size();) {
      const auto decoded =
          std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
              std::span(encoded).subspan(pc), header.profile));
      store.instructions.push_back(
          {{static_cast<std::uint32_t>(pc)}, decoded, {}});
      pc += decoded.size;
    }
    std::ostringstream rendered;
    Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        store, {"acc", store_registers}, rendered, {}, &constants)));
    store_sources[form] = rendered.str();
    std::ostringstream ordered;
    Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
        store, {"acc", store_registers}, ordered, {}, &constants)));
    Check(ordered.str() == rendered.str());
    if (form == 0) {
      const std::array<std::string_view, 2> collision_names{"$object",
                                                            "$value"};
      std::ostringstream collision;
      Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
          store, {"$acc", collision_names}, collision, {}, &constants)));
      collision_source = collision.str();
    }
    for (std::size_t i = 0; i < name_cases.size(); ++i) {
      store.instructions[1].decoded.operands[1].raw_bits = i + 2;
      std::ostringstream checked;
      const auto emitted = source::WriteLinearBody(
          store, {"acc", store_registers}, checked, {}, &constants);
      if (name_cases[i].element) {
        Check(std::get<source::BodyEmitFailure>(emitted).kind ==
              source::BodyEmitError::kIndexedName);
        Check(checked.str().empty());
      } else {
        Check(std::holds_alternative<std::size_t>(emitted));
      }
    }
    store.instructions[1].decoded.operands[1].raw_bits = 0;
    std::ostringstream rejected_store;
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(
                  store, {"acc", store_registers}, rejected_store,
                  {store_sources[form].size() - 1}, &constants))
              .emit_error == source::EmitError::kOutputLimit);
    Check(rejected_store.str().empty());
    store.instructions.erase(store.instructions.begin());
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(store, {"acc", store_registers},
                                      rejected_store, {}, &constants))
              .kind == source::BodyEmitError::kAccumulatorHole);
    Check(rejected_store.str().empty());
  }
  for (std::size_t form = 0; form < named_sources.size(); ++form) {
    const std::vector<std::uint8_t> encoded =
        form == 0
            ? std::vector<std::uint8_t>{0x60, 0, 0x42, 0, 0, 0, 0x64}
            : std::vector<std::uint8_t>{0x60, 0, 0x90, 0xff, 0xff, 0, 0, 0x64};
    abc::MethodBody named;
    for (std::size_t offset = 0; offset < encoded.size();) {
      const auto decoded =
          std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
              std::span(encoded).subspan(offset), header.profile));
      named.instructions.push_back(
          {{static_cast<std::uint32_t>(offset)}, decoded, {}});
      offset += decoded.size;
    }
    std::ostringstream rendered;
    Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        named, {"acc", registers}, rendered, {}, &constants)));
    named_sources[form] = rendered.str();
    Check(named_sources[form] ==
          "acc = object;\nacc = (acc)[\"A\\u0000\\ud800\\\"\\\\\"];\nreturn "
          "acc;\n");
    for (std::size_t i = 0; i < name_cases.size(); ++i) {
      named.instructions[1].decoded.operands[1].raw_bits = i + 2;
      std::ostringstream checked;
      const auto emitted = source::WriteLinearBody(named, {"acc", registers},
                                                   checked, {}, &constants);
      if (name_cases[i].element) {
        Check(std::get<source::BodyEmitFailure>(emitted).kind ==
              source::BodyEmitError::kIndexedName);
        Check(checked.str().empty());
      } else {
        Check(std::holds_alternative<std::size_t>(emitted));
      }
    }
    named.instructions[1].decoded.operands[1].raw_bits = 0;
    std::ostringstream invalid;
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(named, {"acc", registers}, invalid))
              .kind == source::BodyEmitError::kMissingConstants);
    Check(invalid.str().empty());
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(named, {"acc", registers}, invalid,
                                      {named_sources[form].size() - 1},
                                      &constants))
              .emit_error == source::EmitError::kOutputLimit);
    Check(invalid.str().empty());
    std::ostringstream exact;
    Check(std::get<std::size_t>(source::WriteLinearBody(
              named, {"acc", registers}, exact, {named_sources[form].size()},
              &constants)) == named_sources[form].size());
    named.instructions[1].decoded.operands[1].raw_bits = 1;
    const auto failure =
        std::get<source::BodyEmitFailure>(source::WriteLinearBody(
            named, {"acc", registers}, invalid, {}, &constants));
    Check(failure.offset.value == 2 && failure.metadata_error.has_value());
    Check(invalid.str().empty());
  }
  const auto global_body = [&](std::span<const std::uint8_t> encoded) {
    abc::MethodBody result;
    for (std::size_t pc = 0; pc < encoded.size();) {
      const auto decoded = std::get<abc::DecodedInstruction>(
          abc::DecodeInstruction(encoded.subspan(pc), header.profile));
      result.instructions.push_back(
          {{static_cast<std::uint32_t>(pc)}, decoded, {}});
      pc += decoded.size;
    }
    return result;
  };
  constexpr auto kGlobalSlot = name_cases.size() + 1;
  for (const auto& code : {
           std::vector<std::uint8_t>{0x3f, 0,
                                     static_cast<std::uint8_t>(kGlobalSlot),
                                     0, 0x64},
           std::vector<std::uint8_t>{0x8c, 0, 0,
                                     static_cast<std::uint8_t>(kGlobalSlot),
                                     0, 0x64}}) {
    auto global = global_body(code);
    std::ostringstream refused;
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"acc", {}}, refused, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalScope);
    Check(refused.str().empty());
    constants.global_scope = source::GlobalScope{{}, "Container"};
    std::ostringstream shadowed;
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"globalValue", {}}, shadowed, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalReference);
    Check(shadowed.str().empty());
    const std::array<std::string_view, 1> colliding_register{"globalValue"};
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"$acc", colliding_register}, shadowed, {},
              &constants)).kind == source::BodyEmitError::kGlobalReference);
    Check(shadowed.str().empty());
    std::ostringstream accepted;
    Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        global, {"$acc", {}}, accepted, {}, &constants)));
    Check(accepted.str() == "$acc = globalValue;\nreturn $acc;\n");
    std::ostringstream ordered;
    Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
        global, {"$acc", {}}, ordered, {}, &constants)));
    Check(ordered.str() == accepted.str());
    std::ostringstream lexical;
    Check(std::holds_alternative<std::size_t>(source::WriteLocalLexicalBody(
        global, {"$acc", {}}, lexical, {}, &constants)));
    Check(lexical.str() == accepted.str());
    const auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 0, {false, false, false, false}));
    const source::BaseFunction self{
        {analysis::CreationKind::kFunction, {}, {0},
         analysis::HomeObjectInput::kCurrentFunction},
        {abc::FunctionKind::kBaseConstructor, false, 0, {}},
        frame, "globalValue"};
    std::ostringstream self_shadowed;
    Check(std::get<source::BodyEmitFailure>(source::WriteBaseFunction(
              self, global, self_shadowed, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalReference);
    Check(self_shadowed.str().empty());
    const auto named_frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    const source::BaseFunction named{
        {analysis::CreationKind::kFunction, {}, {1},
         analysis::HomeObjectInput::kCurrentFunction},
        {abc::FunctionKind::kBaseConstructor, false, 0, {}},
        named_frame, "Recovered"};
    const std::array<std::string_view, 1> shadowing_parameter{"globalValue"};
    constants.source_parameters = shadowing_parameter;
    std::ostringstream parameter_shadowed;
    Check(std::get<source::BodyEmitFailure>(source::WriteBaseFunction(
              named, global, parameter_shadowed, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalReference);
    Check(parameter_shadowed.str().empty());
    const std::array<std::string_view, 1> safe_parameter{"width"};
    constants.source_parameters = safe_parameter;
    std::ostringstream named_output;
    Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
        named, global, named_output, {}, &constants)));
    Check(named_output.str().find("function Recovered(width)") !=
          std::string::npos);
    const auto double_frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 2, {false, false, false, false}));
    auto double_named = named;
    double_named.frame = double_frame;
    double_named.creation.length.value = 2;
    const std::array<std::string_view, 2> duplicated{"width", "width"};
    const std::array<std::uint8_t, 1> empty_method{0x65};
    constants.source_parameters = duplicated;
    std::ostringstream invalid_parameters;
    Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
              double_named, global_body(empty_method), invalid_parameters, {},
              &constants)) == source::FunctionEmitError::kName);
    Check(invalid_parameters.str().empty());
    const std::array<std::string_view, 2> private_name{"$acc", "width"};
    constants.source_parameters = private_name;
    Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
              double_named, global_body(empty_method), invalid_parameters, {},
              &constants)) == source::FunctionEmitError::kName);
    Check(invalid_parameters.str().empty());
    constants.source_parameters = {};
    std::ostringstream short_output;
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"$acc", {}}, short_output,
              {accepted.str().size() - 1}, &constants)).emit_error ==
          source::EmitError::kOutputLimit);
    Check(short_output.str().empty());
    constants.global_scope = source::GlobalScope{{}, "globalValue"};
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"$acc", {}}, refused, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalReference);
    Check(refused.str().empty());
    constants.global_scope = source::GlobalScope{{}, "Container"};
    global.instructions.front().decoded.operands[1].raw_bits = 0;
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"$acc", {}}, refused, {}, &constants)).kind ==
          source::BodyEmitError::kGlobalReference);
    global.instructions.front().decoded.operands[1].raw_bits = 1;
    Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
              global, {"$acc", {}}, refused, {}, &constants)).kind ==
          source::BodyEmitError::kConstant);
    Check(refused.str().empty());
    constants.global_scope.reset();
  }
  std::ostringstream rejected;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteLinearBody(body, {"acc", {}}, rejected))
            .kind == source::BodyEmitError::kMissingConstants);
  Check(rejected.str().empty());
  constants.string_budget.bytes = 0;
  Check(
      std::get<source::BodyEmitFailure>(
          source::WriteLinearBody(body, {"acc", {}}, rejected, {}, &constants))
          .metadata_error == abc::MetadataError::kResourceLimit);
  constants.string_budget.bytes = 1024;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteLinearBody(body, {"acc", {}}, rejected,
                                    {output.str().size() - 1}, &constants))
            .emit_error == source::EmitError::kOutputLimit);
  auto bad_load = body.instructions.front();
  bad_load.offset.value = 3;
  bad_load.decoded.operands[0].raw_bits = 1;
  body.instructions.insert(body.instructions.begin() + 1, bad_load);
  body.instructions.back().offset.value = 6;
  const auto failure = std::get<source::BodyEmitFailure>(
      source::WriteLinearBody(body, {"acc", {}}, rejected, {}, &constants));
  Check(failure.offset.value == 3 && failure.metadata_error.has_value());
  Check(rejected.str().empty());
  unlink(path);
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export function recovered(){let acc;\n"
              << output.str() << "}\n";
    for (std::size_t i = 0; i < named_sources.size(); ++i)
      std::cout << "export function named" << i << "(object){let acc;\n"
                << named_sources[i] << "}\n";
    for (std::size_t i = 0; i < store_sources.size(); ++i)
      std::cout << "export function write" << i << "(object,value){let acc;\n"
                << store_sources[i] << "}\n";
    std::cout << "export function writeCollision($object,$value){let $acc;\n"
              << collision_source << "}\n";
  }
}
