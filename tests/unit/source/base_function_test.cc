#include "abcd/source/base_function.h"
#include "abcd/source/linear_body.h"
#include "function_template.h"

#include <unistd.h>

#include <iostream>
#include <limits>
#include <sstream>

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
source::BaseFunction Function(std::uint32_t locals = 0,
                              std::uint32_t parameters = 1) {
  return {{analysis::CreationKind::kFunction,
           {},
           {static_cast<std::uint8_t>(parameters)},
           analysis::HomeObjectInput::kCurrentFunction},
          {abc::FunctionKind::kBaseConstructor, false, 0, {}},
          std::get<abc::FrameLayout>(abc::BuildFrameLayout(
              locals, parameters + 3, {true, true, true, true})),
          "Recovered"};
}
abc::MethodBody RestBody() {
  return test::Body({0x44, 0x40, 0x44, 0x51, 0x44, 0x62,
                     0x44, 0x73, 0xcf, 0, 0x61, 3, 0x60, 3, 0x64});
}
abc::MethodBody LeadingRestBody() {
  return test::Body({0x44, 0x50, 0x44, 0x61, 0x44, 0x72,
                     0x44, 0x83, 0x44, 0x94, 0xcf, 1,
                     0x61, 4, 0x60, 4, 0x64});
}
abc::MethodBody DefaultNullBody() {
  // v7: compare the ABI parameter with undefined, branch past null storage
  // when unequal, then return the unchanged or defaulted parameter.
  return test::Body({0x60, 3, 0x00, 0x28, 0, 3, 0x4f, 5,
                     0x01, 0x61, 3, 0x60, 3, 0x64});
}
class FailingBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};
abc::MethodBody FirstKeyBody() {
  auto body = test::Body({
      0xd5, 0x60, 0x05, 0x66, 0x61, 0x01, 0x4d, 0x08,
      0x61, 0x00, 0x60, 0x00, 0xfe, 0x00, 0x36, 0x01,
      0x61, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x51, 0x05,
      0x60, 0x00, 0x64, 0x65});
  body.tries.push_back({{1}, 5, 0, 1});
  body.handlers.push_back({{}, {8}, 0});
  return body;
}
void Emit(std::string_view name, source::BaseFunction function,
          const abc::MethodBody& body) {
  std::cout << "export const " << name << " = ";
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      function, body, std::cout, source::RoutePolicy::kLegacyAllowed)));
  std::cout << ";\n";
}
void EmitFixtures() {
  Emit("parameter", Function(), test::Body({0x60, 3, 0x64}));
  auto length_marker = Function();
  length_marker.creation.length.value = 0;
  Emit("lengthMarker", length_marker, test::Body({0x60, 3, 0x64}));
  Emit("defaultNull", length_marker, DefaultNullBody());
  auto second_marker = Function(0, 2);
  second_marker.creation.length.value = 1;
  Emit("secondMarker", second_marker, test::Body({0x60, 4, 0x64}));
  auto async_marker = length_marker;
  async_marker.attributes.kind = abc::FunctionKind::kAsync;
  Emit("asyncMarker", async_marker, test::Body({0x60, 3, 0x64}));
  auto arrow_marker = length_marker;
  arrow_marker.attributes.kind = abc::FunctionKind::kArrow;
  std::cout << "export const arrowMarker = ";
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteArrowFunction(arrow_marker, test::Body({0x60, 3, 0x64}),
                                 std::cout, {}, nullptr,
                                 analysis::CapturedValues::kReject)));
  std::cout << ";\n";
  Emit("self", Function(), test::Body({0x60, 0, 0x64}));
  Emit("intrinsicSelf", Function(), test::Body({0xaf, 0x64}));
  Emit("intrinsicAfterSlotWrite", Function(),
       test::Body({0x00, 0x61, 0, 0xaf, 0x64}));
  Emit("newTarget", Function(), test::Body({0x60, 1, 0x64}));
  Emit("receiver", Function(), test::Body({0x60, 2, 0x64}));
  Emit("local", Function(1), test::Body({0x60, 0, 0x64}));
  Emit("overwriteSelf", Function(),
       test::Body({0x60, 3, 0x61, 0, 0x60, 0, 0x64}));
  Emit("independent", Function(), test::Body({0x00, 0x61, 3, 0x60, 0, 0x64}));
  Emit("thrown", Function(), test::Body({0x60, 3, 0xfe, 0}));
  Emit("lexical", Function(),
       test::Body({0x09, 1, 0x60, 3, 0x3d, 0, 0x3c, 0, 0x64}));
  Emit("empty", Function(0, 0), test::Body({0x65}));
  Emit("length255", Function(0, 255), test::Body({0x65}));
  auto rest = Function(4);
  rest.creation.length.value = 0;
  Emit("rest", rest, RestBody());
  auto leading_rest = Function(5, 2);
  leading_rest.creation.length.value = 1;
  Emit("leadingRest", leading_rest, LeadingRestBody());
  std::cout << "export function fresh() { return ";
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      Function(), test::Body({0x60, 0, 0x64}), std::cout,
      source::RoutePolicy::kLegacyAllowed)));
  std::cout << "; }\n";
  char path[] = "/tmp/abcd-first-for-in-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(close(fd) == 0);
  const auto opened = corpus::AbcFile::Open(path);
  test::Check(unlink(path) == 0);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1301;
  abc::MetadataIndex index;
  const source::MethodConstants constants{file, header, index, {0}, {}};
  std::cout << "export const firstForIn = ";
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      Function(2), FirstKeyBody(), std::cout,
      source::RoutePolicy::kLegacyAllowed, {}, &constants)));
  std::cout << ";\n";
}
}  // namespace
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    EmitFixtures();
    return 0;
  }
  const auto body = test::Body({0x60, 3, 0x64});
  std::ostringstream output;
  const auto result = source::WriteBaseFunction(Function(), body, output,
                                                source::RoutePolicy::kLegacyAllowed);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(std::get<std::size_t>(result) == output.str().size());
  test::Check(output.str().find("function Recovered($p0)") !=
              std::string::npos);
  // A01: this exact body succeeds on the legacy route above. Requiring the
  // new route must refuse before emitting even a function header.
  std::ostringstream route_output;
  const auto route_result = source::WriteBaseFunction(
      Function(), body, route_output, source::RoutePolicy::kRequireValueTree);
  test::Check(std::holds_alternative<source::RouteRefusal>(route_result));
  test::Check(route_output.str().empty());
  const auto template_result = source::WriteBaseFunctionTemplate(
      Function(), body, route_output, {}, nullptr,
      source::RoutePolicy::kRequireValueTree);
  test::Check(std::holds_alternative<source::RouteRefusal>(template_result));
  test::Check(route_output.str().empty());
  auto invalid_owner = Function();
  invalid_owner.name = "not a function identifier";
  test::Check(std::holds_alternative<source::RouteRefusal>(
      source::WriteBaseFunction(invalid_owner, body, route_output,
                               source::RoutePolicy::kRequireValueTree)));
  test::Check(route_output.str().empty());
  auto length_marker = Function();
  length_marker.creation.length.value = 0;
  std::ostringstream marker_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      length_marker, body, marker_output,
      source::RoutePolicy::kLegacyAllowed)));
  test::Check(marker_output.str().find(
                  "function Recovered($p0 = void 0)") != std::string::npos);
  test::Check(marker_output.str().starts_with(
      "(() => {\"use strict\"; return "));
  auto second_marker = Function(0, 2);
  second_marker.creation.length.value = 1;
  std::ostringstream second_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      second_marker, test::Body({0x60, 4, 0x64}), second_output,
      source::RoutePolicy::kLegacyAllowed)));
  test::Check(second_output.str().find(
                  "function Recovered($p0, $p1 = void 0)") !=
              std::string::npos);
  auto rest = Function(4);
  rest.creation.length.value = 0;
  std::ostringstream rest_output;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteBaseFunction(rest, RestBody(), rest_output,
                                source::RoutePolicy::kLegacyAllowed)));
  test::Check(rest_output.str().find("function Recovered(...$p0)") !=
              std::string::npos);
  source::LocalLexicalBudget rest_budget;
  rest_budget.output.bytes = rest_output.str().size() - 1;
  std::ostringstream rest_limited;
  test::Check(std::get<source::EmitError>(source::WriteBaseFunction(
                  rest, RestBody(), rest_limited,
                  source::RoutePolicy::kLegacyAllowed, rest_budget)) ==
              source::EmitError::kOutputLimit);
  test::Check(rest_limited.str().empty());
  auto leading_rest = Function(5, 2);
  leading_rest.creation.length.value = 1;
  std::ostringstream leading_rest_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      leading_rest, LeadingRestBody(), leading_rest_output,
      source::RoutePolicy::kLegacyAllowed)));
  test::Check(leading_rest_output.str().find(
                  "function Recovered($p0, ...$p1)") != std::string::npos);
  auto bad_rest = RestBody();
  bad_rest.instructions[4].decoded.operands[0].raw_bits = 1;
  std::ostringstream rest_rejected;
  test::Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
                  rest, bad_rest, rest_rejected,
                  source::RoutePolicy::kLegacyAllowed)) ==
              source::FunctionEmitError::kLength);
  test::Check(rest_rejected.str().empty());
  bad_rest = RestBody();
  bad_rest.instructions[6].decoded.operands[0].raw_bits = 7;
  test::Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
                  rest, bad_rest, rest_rejected,
                  source::RoutePolicy::kLegacyAllowed)) ==
              source::FunctionEmitError::kLength);
  test::Check(rest_rejected.str().empty());
  auto no_extra = rest;
  no_extra.frame.extra_arguments = false;
  test::Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
                  no_extra, RestBody(), rest_rejected,
                  source::RoutePolicy::kLegacyAllowed)) ==
              source::FunctionEmitError::kLength);
  test::Check(rest_rejected.str().empty());
  {
    char path[] = "/tmp/abcd-first-for-in-XXXXXX";
    const auto fd = mkstemp(path);
    test::Check(fd >= 0);
    test::Check(close(fd) == 0);
    const auto opened = corpus::AbcFile::Open(path);
    test::Check(unlink(path) == 0);
    const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
    abc::Header header;
    header.kind = abc::AbcKind::kDynamic;
    header.profile = abc::ProfileId::kDynamic1301;
    abc::MetadataIndex index;
    const auto first_key = FirstKeyBody();
    const source::MethodConstants constants{file, header, index, {0}, {}};
    std::ostringstream recovered;
    const auto emitted = source::WriteBaseFunction(
        Function(2), first_key, recovered, source::RoutePolicy::kLegacyAllowed,
        {}, &constants);
    test::Check(std::holds_alternative<std::size_t>(emitted));
    test::Check(recovered.str().find(
                    "for (const $forInKey in $r5) {\n"
                    "return $forInKey;\n}\nreturn;\n") !=
                std::string::npos);
    source::LocalLexicalBudget limited_budget;
    limited_budget.output.bytes = recovered.str().size() - 1;
    std::ostringstream limited;
    test::Check(std::get<source::EmitError>(source::WriteBaseFunction(
                    Function(2), first_key, limited,
                    source::RoutePolicy::kLegacyAllowed, limited_budget,
                    &constants)) == source::EmitError::kOutputLimit);
    test::Check(limited.str().empty());
    header.profile = abc::ProfileId::kDynamic1206;
    std::ostringstream historical;
    const auto refused = source::WriteBaseFunction(
        Function(2), first_key, historical, source::RoutePolicy::kLegacyAllowed,
        {}, &constants);
    test::Check(!std::holds_alternative<std::size_t>(refused));
    test::Check(historical.str().empty());
    auto marked = Function();
    marked.creation.length.value = 0;
    std::ostringstream admitted_marker;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteBaseFunction(marked, body, admitted_marker,
                                  source::RoutePolicy::kLegacyAllowed,
                                  {}, &constants)));
    header.profile = abc::ProfileId::kDynamic1102;
    std::ostringstream old_marker;
    test::Check(std::get<source::FunctionEmitError>(
                    source::WriteBaseFunction(marked, body, old_marker,
                                              source::RoutePolicy::kLegacyAllowed,
                                              {}, &constants)) ==
                source::FunctionEmitError::kLength);
    test::Check(old_marker.str().empty());
  }
  {
    const auto intrinsic = test::Body({0xaf, 0x64});
    std::ostringstream missing;
    const auto refused = source::WriteLinearBody(
        intrinsic, {"acc", {}}, missing);
    test::Check(std::get<source::BodyEmitFailure>(refused).kind ==
                source::BodyEmitError::kCurrentFunctionBinding);
    test::Check(missing.str().empty());
    std::ostringstream bound;
    const auto emitted = source::WriteLinearBody(
        intrinsic, {"acc", {}, "Recovered"}, bound);
    test::Check(std::get<std::size_t>(emitted) == bound.str().size());
    test::Check(bound.str() == "acc = Recovered;\nreturn acc;\n");
    std::ostringstream invalid;
    const auto unsafe = source::WriteLinearBody(
        intrinsic, {"acc", {}, "x);evil("}, invalid);
    test::Check(std::get<source::BodyEmitFailure>(unsafe).emit_error ==
                source::EmitError::kInvalidBinding);
    test::Check(invalid.str().empty());
    auto malformed = intrinsic;
    malformed.instructions[0].decoded.operand_count = 1;
    std::ostringstream rejected;
    test::Check(std::get<source::BodyEmitFailure>(source::WriteLinearBody(
                    malformed, {"acc", {}, "Recovered"}, rejected))
                    .kind == source::BodyEmitError::kUnsupportedInstruction);
    test::Check(rejected.str().empty());
  }
  const auto reject = [&](const source::BaseFunction& function,
                          source::FunctionEmitError expected) {
    std::ostringstream rejected;
    test::Check(std::get<source::FunctionEmitError>(source::WriteBaseFunction(
                    function, body, rejected,
                    source::RoutePolicy::kLegacyAllowed)) == expected);
    test::Check(rejected.str().empty());
  };
  auto invalid = Function();
  invalid.creation.kind = analysis::CreationKind::kMethod;
  reject(invalid, source::FunctionEmitError::kCreation);
  invalid = Function();
  invalid.creation.home_object = analysis::HomeObjectInput::kAccumulator;
  reject(invalid, source::FunctionEmitError::kCreation);
  invalid = Function();
  invalid.attributes.sendable = true;
  reject(invalid, source::FunctionEmitError::kSendable);
  for (std::size_t i = 0; i < abc::kFunctionKindCount; ++i) {
    invalid = Function();
    invalid.attributes.kind = static_cast<abc::FunctionKind>(i);
    if (invalid.attributes.kind != abc::FunctionKind::kBaseConstructor &&
        invalid.attributes.kind != abc::FunctionKind::kAsync)
      reject(invalid, source::FunctionEmitError::kKind);
  }
  invalid = Function();
  invalid.attributes.kind = abc::FunctionKind::kAsync;
  std::ostringstream async_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
      invalid, body, async_output, source::RoutePolicy::kLegacyAllowed)));
  test::Check(async_output.str().find("async function Recovered($p0)") !=
              std::string::npos);
  for (const auto name : {"", "eval", "arguments", "await", "class", "a-b",
                          "1x", "$r0", "$acc", "$p0", "$lex0_0", "x);evil("}) {
    invalid = Function();
    invalid.name = name;
    reject(invalid, source::FunctionEmitError::kName);
  }
  invalid = Function();
  invalid.creation.length.value = 2;
  reject(invalid, source::FunctionEmitError::kLength);
  for (const auto field : {0, 1, 2, 3, 4, 5, 6}) {
    invalid = Function();
    switch (field) {
      case 0:
        invalid.frame.function = abc::RegisterId{1};
        break;
      case 1:
        invalid.frame.new_target = abc::RegisterId{0};
        break;
      case 2:
        invalid.frame.this_value = abc::RegisterId{0};
        break;
      case 3:
        invalid.frame.first_parameter = abc::RegisterId{0};
        break;
      case 4:
        invalid.frame.parameters = 2;
        break;
      case 5:
        invalid.frame.locals = 5;
        break;
      case 6:
        invalid.frame.registers = std::numeric_limits<std::uint32_t>::max();
        break;
    }
    reject(invalid, source::FunctionEmitError::kFrame);
  }
  for (unsigned flags = 0; flags < 16; ++flags) {
    auto function = Function(2, 0);
    const auto call = abc::DecodeCallFlags(flags);
    const auto hidden =
        unsigned(call.this_slot) + call.function_slot + call.new_target_slot;
    function.frame =
        std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, hidden, call));
    std::ostringstream rendered;
    test::Check(std::holds_alternative<std::size_t>(source::WriteBaseFunction(
        function, test::Body({0x65}), rendered,
        source::RoutePolicy::kLegacyAllowed)));
  }
  for (const auto bytes : {output.str().size() - 1, output.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = bytes;
    std::ostringstream rendered;
    const auto limited = source::WriteBaseFunction(
        Function(), body, rendered, source::RoutePolicy::kLegacyAllowed,
        budget);
    test::Check(std::holds_alternative<std::size_t>(limited) ==
                (bytes == output.str().size()));
    test::Check(rendered.str() ==
                (bytes == output.str().size() ? output.str() : ""));
  }
  for (const auto& unsupported :
       {test::Body({0x64}), test::Body({0x70, 0x64}),
        test::Body({0x09, 1, 0x3c, 0, 0x64}), test::Body({0x3c, 0, 0x64}),
        test::Body({0x60, 3, 0x0a, 0, 3, 0x64})}) {
    std::ostringstream rejected;
    test::Check(!std::holds_alternative<std::size_t>(source::WriteBaseFunction(
        Function(), unsupported, rejected,
        source::RoutePolicy::kLegacyAllowed)));
    test::Check(rejected.str().empty());
  }
  {
    // The branch emitter accepts NEG syntactically, but its result can be an
    // impure NaN encoding. A later CFG read must fail before publishing text.
    const auto unsafe = test::Body({0x00, 0x1f, 0, 0x4f, 3, 0x64, 0x65});
    std::ostringstream rejected;
    const auto proof = source::WriteBaseFunction(Function(), unsafe, rejected,
                                                 source::RoutePolicy::kLegacyAllowed);
    test::Check(std::get<source::BodyEmitFailure>(proof).kind ==
                source::BodyEmitError::kInternalValue);
    test::Check(rejected.str().empty());
    const auto joined = test::Body(
        {0x00, 0x4f, 6, 0x1f, 0, 0x4d, 3, 0x00, 0x64});
    const auto mixed = source::WriteBaseFunction(Function(), joined, rejected,
                                                 source::RoutePolicy::kLegacyAllowed);
    test::Check(std::get<source::BodyEmitFailure>(mixed).kind ==
                source::BodyEmitError::kInternalValue);
    test::Check(rejected.str().empty());
  }
  for (const auto count : {200u, 201u, 4096u, 65536u}) {
    std::ostringstream rendered;
    const auto result = source::WriteBaseFunction(
        Function(count, 0), test::Body({0x65}), rendered,
        source::RoutePolicy::kLegacyAllowed);
    test::Check(std::get<std::size_t>(result) == rendered.str().size());
    test::Check(rendered.str().size() < std::size_t(count) * 24 + 256);
  }
  std::size_t low = 0, high = 64 * 1024 * 1024;
  while (low < high) {
    const auto middle = low + (high - low) / 2;
    source::LocalLexicalBudget budget;
    budget.bytes = middle;
    std::ostringstream rendered;
    const auto limited = source::WriteBaseFunction(
        Function(), body, rendered, source::RoutePolicy::kLegacyAllowed,
        budget);
    if (std::holds_alternative<std::size_t>(limited))
      high = middle;
    else {
      test::Check(rendered.str().empty());
      low = middle + 1;
    }
  }
  test::Check(low > 0);
  for (const bool exceptions : {false, true}) {
    FailingBuffer buffer;
    std::ostream failed(&buffer);
    if (exceptions) failed.exceptions(std::ios::badbit | std::ios::failbit);
    test::Check(std::get<source::EmitError>(source::WriteBaseFunction(
                    Function(), body, failed,
                    source::RoutePolicy::kLegacyAllowed)) ==
                source::EmitError::kWrite);
  }
}
