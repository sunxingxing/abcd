#include "abcd/source/class_method.h"

#include <sstream>

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
    Emit("escaped", test::Body({0x60, 3, 0x64}),
         abc::ClassPlacement::kPrototype, u"quoted\"key");
    Emit("self", test::Body({0x60, 0, 0x64}));
    Emit("receiver", test::Body({0x60, 2, 0x64}));
    Emit("newTarget", test::Body({0x60, 1, 0x64}));
    Emit("thrown", test::Body({0x60, 3, 0xfe, 0}));
    Emit("staticSelf", test::Body({0x60, 0, 0x64}),
         abc::ClassPlacement::kStatic);
    return 0;
  }
  const auto body = test::Body({0x60, 3, 0x64});
  std::ostringstream output;
  const auto result = source::WriteClassMethod(Method(), body, output);
  test::Check(std::get<std::size_t>(result) == output.str().size());
  test::Check(output.str().starts_with("[\"method\"]($p0)"));
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
  wrong.member.kind = abc::ClassMethodKind::kGetter;
  reject(wrong, source::FunctionEmitError::kKind);
  wrong = Method();
  wrong.attributes.sendable = true;
  reject(wrong, source::FunctionEmitError::kSendable);
  wrong = Method();
  wrong.self_binding = "$acc";
  reject(wrong, source::FunctionEmitError::kName);
  wrong = Method();
  wrong.member.length = 0;
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
