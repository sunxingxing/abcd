#include "abcd/analysis/argument_window.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  // Independent ABI ordering: function, new.target, this, then parameters.
  for (std::uint32_t bits = 0; bits < 16; ++bits) {
    const auto flags = abc::DecodeCallFlags(bits);
    const auto hidden = std::uint32_t(flags.function_slot) +
        flags.new_target_slot + flags.this_slot;
    for (const std::uint32_t count : {0u, 1u, 200u, 201u, 65536u}) {
      constexpr std::uint32_t kLocals = 7;
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(kLocals, hidden + count, flags));
      const auto built = ArgumentWindow::Build(frame);
      if (!flags.extra_arguments) {
        test::Check(std::get<ArgumentWindowError>(built) ==
            ArgumentWindowError::kMissingExtraArguments);
        continue;
      }
      const auto window = std::get<ArgumentWindow>(built);
      test::Check(window.First().value == kLocals + hidden);
      test::Check(window.Count() == count);
      test::Check(!window.At(count) && !window.At(UINT32_MAX));
      if (count != 0) {
        test::Check(window.At(0)->value == kLocals + hidden);
        test::Check(window.At(count - 1)->value == frame.registers - 1);
      }
      for (const std::uint32_t actual : {0u, 1u, 200u, 201u, 65536u,
                                        UINT32_MAX}) {
        const auto split = window.Split(ActualArgumentCount{actual});
        test::Check(split.slots <= count && split.slots <= actual);
        test::Check(std::uint64_t(split.slots) + split.tail == actual);
        test::Check(actual < count ? split.slots == actual && split.tail == 0
                                  : split.slots == count);
      }
      auto malformed = frame;
      ++malformed.first_parameter.value;
      test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
          ArgumentWindowError::kInvalidFrame);
      malformed = frame;
      ++malformed.registers;
      test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
          ArgumentWindowError::kInvalidFrame);
      malformed = frame;
      malformed.function = frame.first_parameter;
      test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
          ArgumentWindowError::kInvalidFrame);
      for (const auto member : {&abc::FrameLayout::function,
                                &abc::FrameLayout::new_target,
                                &abc::FrameLayout::this_value}) {
        malformed = frame;
        if (!(malformed.*member)) continue;
        ++(malformed.*member)->value;
        test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
            ArgumentWindowError::kInvalidFrame);
      }
    }
  }
  // MethodLiteral runtime fields are 28 bits, not call-range operand widths.
  constexpr std::uint32_t kRuntimeCountMax = (1u << 28) - 1;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      kRuntimeCountMax, kRuntimeCountMax + 3, {true, true, true, true}));
  const auto window = std::get<ArgumentWindow>(ArgumentWindow::Build(frame));
  test::Check(window.At(kRuntimeCountMax - 1)->value == frame.registers - 1);
  auto malformed = frame;
  malformed.parameters = UINT32_MAX;
  test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
      ArgumentWindowError::kInvalidFrame);
  malformed = frame;
  malformed.locals = UINT32_MAX;
  test::Check(std::get<ArgumentWindowError>(ArgumentWindow::Build(malformed)) ==
      ArgumentWindowError::kInvalidFrame);
}
