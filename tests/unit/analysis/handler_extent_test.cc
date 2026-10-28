// es2abc may record a catch handler's code_size as 0. The region certificate
// then infers the handler extent from the normal join. Inference must agree
// with a recorded extent and must accept an empty handler at the join.
#include "abcd/analysis/exception_flow.h"
#include "abcd/analysis/exception_regions.h"
#include "analysis/ssa_fixture.h"

#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr auto U = static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED);
constexpr auto T = static_cast<std::uint8_t>(abc::Opcode::kLDTRUE);
constexpr auto R = static_cast<std::uint8_t>(abc::Opcode::kRETURN);
constexpr auto J = static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8);
constexpr auto G = static_cast<std::uint8_t>(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16);

void Region(abc::MethodBody& body, unsigned start, unsigned size, unsigned handler, unsigned length) {
  body.tries.push_back({{start}, size, static_cast<std::uint32_t>(body.handlers.size()), 1});
  abc::CatchHandler entry; entry.offset = {handler}; entry.length = length;
  body.handlers.push_back(entry);
}
ExceptionRegions Regions(const abc::MethodBody& body) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {}));
  const auto built = BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers, {}, {}, &frame);
  test::Check(std::holds_alternative<ExceptionFlow>(built));
  const auto regions = BuildExceptionRegions(body, std::get<ExceptionFlow>(built));
  test::Check(std::holds_alternative<ExceptionRegions>(regions));
  return std::get<ExceptionRegions>(regions);
}
void Same(const ExceptionRegions& a, const ExceptionRegions& b) {
  const auto same_scope = [](const std::optional<CatchScope>& x, const std::optional<CatchScope>& y) {
    return x.has_value() == y.has_value() && (!x || (x->region == y->region && x->part == y->part));
  };
  test::Check(a.regions.size() == b.regions.size() && a.source_order == b.source_order &&
      a.scopes.size() == b.scopes.size());
  for (std::size_t i = 0; i < a.scopes.size(); ++i) test::Check(same_scope(a.scopes[i], b.scopes[i]));
  for (std::size_t i = 0; i < a.regions.size(); ++i)
    test::Check(a.regions[i].body == b.regions[i].body && a.regions[i].handler == b.regions[i].handler &&
        same_scope(a.regions[i].parent, b.regions[i].parent) && a.regions[i].inactive == b.regions[i].inactive);
}
}  // namespace

int main() {
  // try { global } catch { undefined } ; true ; return
  //  0 tryldglobalbyname  4 jmp +5 -> 9  6 ldundefined  7 jmp +2 -> 9  9 ldtrue  10 return
  const std::initializer_list<std::uint8_t> bytes{G, 0, 0, 0, J, 5, U, J, 2, T, R};
  auto recorded = test::Body(bytes); Region(recorded, 0, 4, 6, 3);
  auto inferred = test::Body(bytes); Region(inferred, 0, 4, 6, 0);
  Same(Regions(recorded), Regions(inferred));
  // The handler is [ldundefined, jmp): it stops at the normal join (ldtrue).
  const auto falling = Regions(inferred);
  test::Check(falling.regions[0].handler.begin.value == 2 && falling.regions[0].handler.end.value == 4);

  // try { global } catch {} : the handler start is itself the normal join.
  //  0 tryldglobalbyname  4 jmp +2 -> 6  6 ldtrue  7 return
  auto empty = test::Body({G, 0, 0, 0, J, 2, T, R}); Region(empty, 0, 4, 6, 0);
  const auto discarded = Regions(empty);
  test::Check(discarded.regions[0].handler.begin == discarded.regions[0].handler.end &&
      discarded.regions[0].handler.begin.value == 2);
  // try { global } catch { return } : the handler runs to the end of code.
  //  0 tryldglobalbyname  4 ldtrue  5 return  6 ldundefined  7 return
  auto tail = test::Body({G, 0, 0, 0, T, R, U, R}); Region(tail, 0, 4, 6, 0);
  const auto ended = Regions(tail);
  test::Check(ended.regions[0].handler.begin.value == 3 && ended.regions[0].handler.end.value == 5);
  // try { global } catch { try { global } catch { undefined } } ; true ; return
  //  0 G  4 jmp->15  6 G  10 jmp->15  12 ldundefined  13 jmp->15  15 ldtrue  16 return
  // The inner try starts in the outer handler, so its catch belongs to it.
  const std::initializer_list<std::uint8_t> nested{G, 0, 0, 0, J, 11, G, 0, 0, 0, J, 5, U, J, 2, T, R};
  auto outer_recorded = test::Body(nested); Region(outer_recorded, 0, 4, 6, 9); Region(outer_recorded, 6, 4, 12, 3);
  auto outer_inferred = test::Body(nested); Region(outer_inferred, 0, 4, 6, 0); Region(outer_inferred, 6, 4, 12, 0);
  Same(Regions(outer_recorded), Regions(outer_inferred));
  const auto enclosing = Regions(outer_inferred);
  test::Check(enclosing.regions[0].handler.begin.value == 2 && enclosing.regions[0].handler.end.value == 6);
  std::cout << "handler extents ok\n";
}
