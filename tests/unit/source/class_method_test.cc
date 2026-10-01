#include "abcd/source/class_method.h"

#include <limits>
#include <sstream>
#include <type_traits>

#include "abcd/source/string_literal.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
source::ClassMethod Method() {
  return {{{}, abc::ClassMethodKind::kOrdinary, 1},
          {abc::FunctionKind::kNone, false, 0, {}},
          std::get<abc::FrameLayout>(
              abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({}))),
          abc::ClassPlacement::kPrototype,
          u"method",
          "MethodSelf",
          source::EffectiveMethodKind::kOrdinary};
}
void Emit(std::string_view export_name, const abc::MethodBody& body,
          abc::ClassPlacement placement = abc::ClassPlacement::kPrototype,
          std::u16string_view key = u"method") {
  auto method = Method();
  method.placement = placement;
  method.key = key;
  std::cout << "export const " << export_name
            << " = (() => { const C = class {\n";
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteClassMethod(method, body, std::cout)));
  std::cout << "\n}; const MethodSelf = C";
  if (placement == abc::ClassPlacement::kPrototype) std::cout << ".prototype";
  std::cout << '[';
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteStringLiteral(method.key, std::cout)));
  std::cout << "]; return C; })();\n";
}
}  // namespace
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    Emit("parameter", test::Body({0x60, 3, 0x64}));
    auto marked = Method();
    marked.member.length = 0;
    std::cout << "export const lengthMarker = (() => { const C = class {\n";
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteClassMethod(marked, test::Body({0x60, 3, 0x64}),
                                 std::cout)));
    std::cout << "\n}; const MethodSelf = C.prototype.method;"
                 " return C; })();\n";
    Emit("escaped", test::Body({0x60, 3, 0x64}),
         abc::ClassPlacement::kPrototype, u"quoted\"key");
    Emit("self", test::Body({0x60, 0, 0x64}));
    Emit("intrinsicSelf", test::Body({0xaf, 0x64}));
    Emit("staticIntrinsicSelf", test::Body({0xaf, 0x64}),
         abc::ClassPlacement::kStatic);
    Emit("receiver", test::Body({0x60, 2, 0x64}));
    Emit("newTarget", test::Body({0x60, 1, 0x64}));
    Emit("thrown", test::Body({0x60, 3, 0xfe, 0}));
    Emit("staticSelf", test::Body({0x60, 0, 0x64}),
         abc::ClassPlacement::kStatic);
    auto async = Method();
    async.attributes.kind = abc::FunctionKind::kAsync;
    async.frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(2, 4, abc::DecodeCallFlags({})));
    auto async_body = test::Body({0xae, 0x61, 0, 0x60, 5, 0xcd, 0,
                                  0x64, 0x61, 1, 0xce, 0, 0x64});
    async_body.tries.push_back({{3}, 5, 0, 1});
    async_body.handlers.push_back({{}, {8}, 5});
    std::cout << "export const asyncMethod = (() => { const C = class {\n";
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteClassMethod(async, async_body, std::cout)));
    std::cout << "\n}; const MethodSelf = C.prototype.method;"
                 " return C; })();\n";
    return 0;
  }
  const auto body = test::Body({0x60, 3, 0x64});
  std::ostringstream output;
  const auto result = source::WriteClassMethod(Method(), body, output);
  test::Check(std::get<std::size_t>(result) == output.str().size());
  test::Check(output.str().starts_with("method($p0)"));
  const auto reject = [&](source::ClassMethod method,
                          source::FunctionEmitError expected) {
    std::ostringstream text;
    test::Check(std::get<source::FunctionEmitError>(
                    source::WriteClassMethod(method, body, text)) == expected);
    test::Check(text.str().empty());
  };
  auto wrong = Method();
  wrong.effective_kind = source::EffectiveMethodKind::kUnknown;
  reject(wrong, source::FunctionEmitError::kKind);
  wrong = Method();
  wrong.attributes.kind = abc::FunctionKind::kAsync;
  reject(wrong, source::FunctionEmitError::kKind);
  wrong = Method();
  using MethodKindValue = std::underlying_type_t<abc::ClassMethodKind>;
  wrong.member.kind = static_cast<abc::ClassMethodKind>(
      std::numeric_limits<MethodKindValue>::max());
  reject(wrong, source::FunctionEmitError::kKind);
  wrong = Method();
  wrong.attributes.kind = abc::FunctionKind::kAsync;
  auto async = wrong;
  async.frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(2, 4, abc::DecodeCallFlags({})));
  auto async_body = test::Body({0xae, 0x61, 0, 0x60, 5, 0xcd, 0,
                                0x64, 0x61, 1, 0xce, 0, 0x64});
  async_body.tries.push_back({{3}, 5, 0, 1});
  async_body.handlers.push_back({{}, {8}, 5});
  std::ostringstream async_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteClassMethod(async, async_body, async_source)));
  test::Check(async_source.str().starts_with("async method($p0)"));
  const auto reject_async = [&](const abc::MethodBody& candidate) {
    std::ostringstream rejected;
    test::Check(std::get<source::FunctionEmitError>(
                    source::WriteClassMethod(async, candidate, rejected)) ==
                source::FunctionEmitError::kKind);
    test::Check(rejected.str().empty());
  };
  auto missing_handler = async_body;
  missing_handler.handlers.clear();
  reject_async(missing_handler);
  auto wrong_region = async_body;
  wrong_region.tries[0].length--;
  reject_async(wrong_region);
  auto wrong_promise = async_body;
  wrong_promise.instructions[3].decoded.operands[0].raw_bits = 1;
  reject_async(wrong_promise);
  auto read_promise = async_body;
  read_promise.instructions[2].decoded.operands[0].raw_bits = 0;
  reject_async(read_promise);
  auto wrong_catch = async_body;
  wrong_catch.handlers[0].type = abc::CatchTypeIndex{1};
  reject_async(wrong_catch);
  auto copied_abi = test::Body({0x44, 0x21, 0xae, 0x61, 0,
                                0x60, 5, 0xcd, 0, 0x64,
                                0x61, 1, 0xce, 0, 0x64});
  copied_abi.tries.push_back({{5}, 5, 0, 1});
  copied_abi.handlers.push_back({{}, {10}, 5});
  std::ostringstream copied_source;
  test::Check(std::holds_alternative<std::size_t>(source::WriteClassMethod(
      async, copied_abi, copied_source)));
  auto copied_local = copied_abi;
  copied_local.instructions[0].decoded.operands[1].raw_bits = 1;
  reject_async(copied_local);
  auto async_getter = async;
  async_getter.member.kind = abc::ClassMethodKind::kGetter;
  async_getter.member.length = 0;
  async_getter.effective_kind = source::EffectiveMethodKind::kGetter;
  std::ostringstream getter_rejected;
  test::Check(std::get<source::FunctionEmitError>(source::WriteClassMethod(
                  async_getter, async_body, getter_rejected)) ==
              source::FunctionEmitError::kKind);
  test::Check(getter_rejected.str().empty());
  for (const std::size_t count : {200, 201, 400}) {
    std::vector<std::uint8_t> bytes{0xae, 0x61, 0};
    bytes.insert(bytes.end(), count, 0xd5);
    bytes.insert(bytes.end(), {0x60, 5, 0xcd, 0, 0x64,
                               0x61, 1, 0xce, 0, 0x64});
    auto many = test::Body(bytes);
    many.tries.push_back({{3}, static_cast<std::uint32_t>(count + 5), 0, 1});
    many.handlers.push_back({{}, {static_cast<std::uint32_t>(count + 8)},
                             5});
    std::ostringstream many_source;
    test::Check(std::holds_alternative<std::size_t>(source::WriteClassMethod(
        async, many, many_source)));
  }
  wrong = Method();
  wrong.member.kind = abc::ClassMethodKind::kGetter;
  reject(wrong, source::FunctionEmitError::kKind);
  auto getter = Method();
  getter.member.kind = abc::ClassMethodKind::kGetter;
  getter.member.length = 0;
  getter.frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 3, abc::DecodeCallFlags({})));
  getter.effective_kind = source::EffectiveMethodKind::kGetter;
  std::ostringstream getter_source;
  const auto getter_result = source::WriteClassMethod(
      getter, test::Body({0x00, 0x65}), getter_source);
  test::Check(std::holds_alternative<std::size_t>(getter_result));
  test::Check(getter_source.str().starts_with("get method()"));
  std::ostringstream observed_self;
  test::Check(std::get<source::FunctionEmitError>(source::WriteClassMethod(
                  getter, test::Body({0x60, 0, 0x64}), observed_self)) ==
              source::FunctionEmitError::kFrame);
  test::Check(observed_self.str().empty());
  getter.frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(1, 3, abc::DecodeCallFlags({})));
  std::ostringstream dead_copy;
  test::Check(std::holds_alternative<std::size_t>(source::WriteClassMethod(
      getter, test::Body({0x44, 0x10, 0x00, 0x65}), dead_copy)));
  std::ostringstream live_copy;
  test::Check(std::get<source::FunctionEmitError>(source::WriteClassMethod(
                  getter, test::Body({0x44, 0x10, 0x60, 0, 0x64}),
                  live_copy)) == source::FunctionEmitError::kFrame);
  test::Check(live_copy.str().empty());
  const auto rest_body = test::Body({0x44, 0x40, 0x44, 0x51,
                                     0x44, 0x62, 0x44, 0x73,
                                     0xcf, 0, 0x61, 3, 0x60, 3, 0x64});
  for (const auto kind : {abc::ClassMethodKind::kGetter,
                          abc::ClassMethodKind::kSetter}) {
    auto rest_accessor = Method();
    rest_accessor.member.kind = kind;
    rest_accessor.member.length = 0;
    rest_accessor.effective_kind =
        kind == abc::ClassMethodKind::kGetter
            ? source::EffectiveMethodKind::kGetter
            : source::EffectiveMethodKind::kSetter;
    rest_accessor.frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(4, 4, abc::DecodeCallFlags({})));
    std::ostringstream rejected;
    test::Check(std::get<source::FunctionEmitError>(source::WriteClassMethod(
                    rest_accessor, rest_body, rejected)) ==
                source::FunctionEmitError::kLength);
    test::Check(rejected.str().empty());
  }
  auto setter = Method();
  setter.member.kind = abc::ClassMethodKind::kSetter;
  setter.effective_kind = source::EffectiveMethodKind::kSetter;
  std::ostringstream setter_source;
  const auto setter_result = source::WriteClassMethod(
      setter, test::Body({0x00, 0x65}), setter_source);
  test::Check(std::holds_alternative<std::size_t>(setter_result));
  test::Check(setter_source.str().starts_with("set method($p0)"));
  wrong = Method();
  wrong.attributes.sendable = true;
  reject(wrong, source::FunctionEmitError::kSendable);
  wrong = Method();
  wrong.self_binding = "$acc";
  reject(wrong, source::FunctionEmitError::kName);
  wrong = Method();
  wrong.member.length = 0;
  std::ostringstream marked;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteClassMethod(wrong, body, marked)));
  test::Check(marked.str().starts_with(
      "method($p0 = void 0)"));
  wrong.member.length = 2;
  reject(wrong, source::FunctionEmitError::kLength);
  wrong = Method();
  wrong.frame.this_value = abc::RegisterId{0};
  reject(wrong, source::FunctionEmitError::kFrame);
  for (const auto& unsupported :
       {test::Body({0x70, 0x64}), test::Body({0x3c, 0, 0x64})}) {
    std::ostringstream text;
    test::Check(!std::holds_alternative<std::size_t>(
        source::WriteClassMethod(Method(), unsupported, text)));
    test::Check(text.str().empty());
  }
  for (const auto bytes : {output.str().size() - 1, output.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = bytes;
    std::ostringstream text;
    const auto emitted = source::WriteClassMethod(Method(), body, text, budget);
    test::Check(std::holds_alternative<std::size_t>(emitted) ==
                (bytes == output.str().size()));
    test::Check(text.str() ==
                (bytes == output.str().size() ? output.str() : ""));
  }
  const auto fits = [&](std::size_t bytes) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    std::ostringstream text;
    const auto result = source::WriteClassMethod(Method(), body, text, budget);
    const bool accepted = std::holds_alternative<std::size_t>(result);
    test::Check(text.str() == (accepted ? output.str() : ""));
    return accepted;
  };
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  test::Check(!fits(low) && fits(high));
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    if (fits(middle))
      high = middle;
    else
      low = middle;
  }
  test::Check(fits(high) && !fits(high - 1));
  for (const std::uint16_t count : {200, 201, 400, 800, 1600}) {
    auto large = Method();
    large.member.length = count;
    large.frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, count + 3, abc::DecodeCallFlags({})));
    std::ostringstream text;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteClassMethod(large, test::Body({0x65}), text)));
    test::Check(text.str().find("$p" + std::to_string(count - 1)) !=
                std::string::npos);
  }
}
