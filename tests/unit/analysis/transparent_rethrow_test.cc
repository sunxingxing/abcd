#include "abcd/analysis/transparent_rethrow.h"

#include <cstdlib>
#include <iostream>
#include <span>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/profile_capabilities.h"
#include "abcd/analysis/named_invocation.h"

namespace {
using namespace abcd;
void Check(bool value) {
  if (!value) std::abort();
}
abc::MethodBody Body(std::initializer_list<std::uint8_t> list,
                     abc::ProfileId profile = abc::ProfileId::kDynamic1301) {
  const std::span bytes(list.begin(), list.size());
  abc::MethodBody body;
  for (std::size_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes.subspan(pc), profile));
    body.instructions.push_back({{static_cast<std::uint32_t>(pc)}, decoded, {}});
    pc += decoded.size;
  }
  return body;
}
// RETURNUNDEFINED; THROW (handler at pc 1). One region covers pc 0.
constexpr std::uint8_t kReturnUndefined = 0x65;
constexpr std::uint8_t kLdUndefined = 0x00;
constexpr std::uint8_t kThrowPrefix = 0xfe;
constexpr std::uint8_t kThrow = 0x00;
void Protect(abc::MethodBody& body, std::uint32_t handler_pc) {
  body.handlers.push_back({{}, {handler_pc}, 1});
  body.tries.push_back({{0}, 1, static_cast<std::uint32_t>(body.handlers.size() - 1), 1});
}
}  // namespace

int main() {
  // Every handler rethrows: regions are erased and the unreachable trailing
  // handler instruction is dropped.
  auto transparent = Body({kReturnUndefined, kThrowPrefix, kThrow});
  Protect(transparent, 1);
  Protect(transparent, 1);
  Check(analysis::EraseTransparentRethrowRegions(transparent));
  Check(transparent.tries.empty() && transparent.handlers.empty());
  Check(transparent.instructions.size() == 1);
  // A handler that does anything but rethrow keeps every region (one
  // transparent region must not be erased beside an opaque one).
  auto opaque = Body({kReturnUndefined, kThrowPrefix, kThrow, kReturnUndefined});
  Protect(opaque, 1);
  Protect(opaque, 3);
  Check(!analysis::EraseTransparentRethrowRegions(opaque));
  Check(opaque.tries.size() == 2 && opaque.instructions.size() == 3);
  // A handler reached by fallthrough stays as code after erasure.
  auto fallthrough = Body({kLdUndefined, kThrowPrefix, kThrow});
  Protect(fallthrough, 1);
  Check(analysis::EraseTransparentRethrowRegions(fallthrough));
  Check(fallthrough.tries.empty() && fallthrough.instructions.size() == 2);
  // A handler that is also a branch target stays as code.
  auto targeted = Body({kReturnUndefined, kThrowPrefix, kThrow});
  targeted.instructions[0].branch_target = abc::CodeOffset{1};
  Protect(targeted, 1);
  Check(analysis::EraseTransparentRethrowRegions(targeted));
  Check(targeted.instructions.size() == 2);
  // A handler covered by a region rethrows into dispatch again: a self loop
  // (region covers its own handler) and a mutual chain must keep the regions.
  auto self_loop = Body({kLdUndefined, kThrowPrefix, kThrow});
  self_loop.handlers.push_back({{}, {1}, 1});
  self_loop.tries.push_back({{0}, 3, 0, 1});
  Check(!analysis::EraseTransparentRethrowRegions(self_loop) && self_loop.tries.size() == 1);
  auto mutual = Body({kReturnUndefined, kThrowPrefix, kThrow, kThrowPrefix, kThrow});
  mutual.handlers.push_back({{}, {3}, 1});
  mutual.tries.push_back({{1}, 2, 0, 1});  // covers handler at 1, sends to 3
  mutual.handlers.push_back({{}, {1}, 1});
  mutual.tries.push_back({{3}, 2, 1, 1});  // covers handler at 3, sends to 1
  Check(!analysis::EraseTransparentRethrowRegions(mutual) && mutual.tries.size() == 2);
  // Malformed handler slices are left untouched.
  auto malformed = Body({kReturnUndefined, kThrowPrefix, kThrow});
  malformed.tries.push_back({{0}, 1, 5, 1});
  Check(!analysis::EraseTransparentRethrowRegions(malformed) && malformed.tries.size() == 1);
  auto plain_body = Body({kReturnUndefined});
  Check(!analysis::EraseTransparentRethrowRegions(plain_body));

  // CALLTHIS*WITHNAME lifts to the same receiver call as CALLTHIS*.
  // callthis1withname cache=0, string_id=1, receiver v0, argument v1.
  const auto named = Body({0xde, 0x00, 0x01, 0x00, 0x00, 0x01}, abc::ProfileId::kDynamic2400);
  const auto plain = Body({0x2e, 0x00, 0x00, 0x01}, abc::ProfileId::kDynamic2400);
  const auto lifted_named = analysis::LiftReceiverCall(named.instructions[0].decoded, 2);
  const auto lifted_plain = analysis::LiftReceiverCall(plain.instructions[0].decoded, 2);
  const auto* a = std::get_if<analysis::ReceiverCall>(&lifted_named);
  const auto* b = std::get_if<analysis::ReceiverCall>(&lifted_plain);
  Check(a && b && a->receiver == b->receiver && a->Count() == 1 && b->Count() == 1 &&
        *a->At(0) == *b->At(0));
  Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(named.instructions[0].decoded, 1)) ==
        analysis::CallError::kRegisterRange);

  // Profile capability table rows (abc/profile_capabilities.h).
  static_assert(abc::HasPinnedRuntimeSemantics(abc::ProfileId::kDynamic2400));
  static_assert(abc::HasPinnedRuntimeSemantics(abc::ProfileId::kDynamic1301));
  static_assert(!abc::HasPinnedRuntimeSemantics(abc::ProfileId::kDynamic9));
  static_assert(!abc::HasPinnedRuntimeSemantics(abc::ProfileId::kDynamic1102));
  static_assert(!abc::CapabilitiesOf(abc::ProfileId::kStatic0107).container_decoding);
  static_assert(abc::CapabilitiesOf(abc::ProfileId::kDynamic1206).literal_arrays_in_header);
  static_assert(!abc::CapabilitiesOf(abc::ProfileId::kDynamic2400).literal_arrays_in_header);
  std::cout << "transparent_rethrow_test: ok\n";
  return EXIT_SUCCESS;
}
