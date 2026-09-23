#include "abcd/source/class_method.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "analysis/ssa_fixture.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::source::ClassMethod Accessor(abcd::abc::ClassMethodKind kind,
                                   abcd::abc::ClassPlacement placement) {
  using namespace abcd;
  const bool setter = kind == abc::ClassMethodKind::kSetter;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      1, setter ? 4 : 3, abc::DecodeCallFlags({})));
  return {{{}, kind, static_cast<std::uint16_t>(setter ? 1 : 0)},
          {abc::FunctionKind::kNone, false, 0, {}}, frame, placement,
          u"value", "AccessorSelf",
          setter ? source::EffectiveMethodKind::kSetter
                 : source::EffectiveMethodKind::kGetter};
}
void Member(const abcd::source::ClassMethod& method, std::ostream& output) {
  const auto body = abcd::test::Body({0x44, 0x10, 0x00, 0x65});
  Check(std::holds_alternative<std::size_t>(
      abcd::source::WriteClassMethod(method, body, output)));
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export class InstanceAccessor {\n";
    Member(Accessor(abc::ClassMethodKind::kGetter,
                    abc::ClassPlacement::kPrototype), std::cout);
    std::cout << '\n';
    Member(Accessor(abc::ClassMethodKind::kSetter,
                    abc::ClassPlacement::kPrototype), std::cout);
    std::cout << "\n}\nexport class StaticAccessor {\n";
    Member(Accessor(abc::ClassMethodKind::kGetter,
                    abc::ClassPlacement::kStatic), std::cout);
    std::cout << '\n';
    Member(Accessor(abc::ClassMethodKind::kSetter,
                    abc::ClassPlacement::kStatic), std::cout);
    std::cout << "\n}\n";
    return 0;
  }
  std::ostringstream output;
  const auto getter = Accessor(abc::ClassMethodKind::kGetter,
                               abc::ClassPlacement::kPrototype);
  Member(getter, output);
  Check(output.str().starts_with("get [\"value\"]()"));
  auto wrong = getter;
  wrong.effective_kind = source::EffectiveMethodKind::kOrdinary;
  std::ostringstream rejected;
  Check(std::get<source::FunctionEmitError>(source::WriteClassMethod(
            wrong, test::Body({0x00, 0x65}), rejected)) ==
        source::FunctionEmitError::kKind);
  Check(rejected.str().empty());
}
