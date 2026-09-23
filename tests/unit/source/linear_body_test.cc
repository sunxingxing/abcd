#include "abcd/source/linear_body.h"

#include <array>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
class FailingBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};
abcd::abc::MethodBody Body(std::initializer_list<std::uint8_t> encoded) {
  const std::vector<std::uint8_t> bytes(encoded);
  abcd::abc::MethodBody body;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    auto instruction = std::get<abcd::abc::DecodedInstruction>(
        abcd::abc::DecodeInstruction(std::span(bytes).subspan(offset),
                                     abcd::abc::ProfileId::kDynamic1206));
    body.instructions.push_back(
        {{static_cast<std::uint32_t>(offset)}, instruction, {}});
    offset += instruction.size;
  }
  return body;
}
}  // namespace
int main() {
  using namespace abcd::source;
  const std::array<std::string_view, 1> registers{"r0"};
  const ScalarNames names{"acc", registers};
  const auto body =
      Body({0x60, 0, 0x0a, 0, 0, 0x64});  // lda r0; add r0; return
  std::ostringstream output;
  const auto result = WriteLinearBody(body, names, output);
  Check(std::get<std::size_t>(result) == output.str().size());
  Check(output.str() == "acc = r0;\nacc = (r0 + acc);\nreturn acc;\n");
  const std::array<std::string_view, 2> membership_registers{"key", "object"};
  std::ostringstream membership;
  Check(std::holds_alternative<std::size_t>(
      WriteLinearBody(Body({0x60, 1, 0x25, 0, 0, 0x64}),
                      {"acc", membership_registers}, membership)));
  Check(membership.str() ==
        "acc = object;\nacc = (key in acc);\nreturn acc;\n");
  std::ostringstream limited;
  Check(std::get<BodyEmitFailure>(
            WriteLinearBody(body, names, limited, {output.str().size() - 1}))
            .emit_error == EmitError::kOutputLimit);
  Check(limited.str().empty());
  // Indexed runtime reads need receiver/path evidence before JS lowering.
  // Even a valid scalar prefix must not leak when one appears later.
  for (const auto& indexed :
       {Body({0x60, 0, 0x3a, 0, 0, 0, 0x64}),
        Body({0x60, 0, 0x88, 0, 0, 0, 0, 0x64}),
        Body({0x60, 0, 0xfd, 8, 0xff, 0xff, 0xff, 0xff, 0x64}),
        Body({0x60, 0, 0xfc, 29, 0, 0xff, 0xff, 0xff, 0xff, 0x64})}) {
    Check(std::get<BodyEmitFailure>(WriteLinearBody(indexed, names, limited))
              .kind == BodyEmitError::kUnsupportedInstruction);
    Check(limited.str().empty());
  }
  std::ostringstream exact;
  Check(std::get<std::size_t>(WriteLinearBody(
            body, names, exact, {output.str().size()})) == output.str().size());
  Check(exact.str() == output.str());
  Check(std::get<BodyEmitFailure>(
            WriteLinearBody(Body({0, 0x4f, 0}), names, limited))
            .kind == BodyEmitError::kUnsupportedInstruction);
  Check(limited.str().empty());
  Check(std::get<BodyEmitFailure>(WriteLinearBody(body, {"acc", {}}, limited))
            .scalar_error == abcd::analysis::ScalarError::kRegisterRange);
  Check(limited.str().empty());
  for (const auto& invalid :
       {Body({0x64}), Body({0x0a, 0, 0, 0x64}), Body({0xfe, 0}),
        Body({0x37, 0, 0, 0x64}), Body({0x85, 0, 0, 0, 0x64}),
        Body({0x25, 0, 0, 0x64}), Body({0x42, 0, 0, 0, 0x64}),
        Body({0x90, 0, 0, 0, 0, 0x64})}) {
    Check(std::get<BodyEmitFailure>(WriteLinearBody(invalid, names, limited))
              .kind == BodyEmitError::kAccumulatorHole);
    Check(limited.str().empty());
  }
  Check(std::get<BodyEmitFailure>(WriteLinearBody(Body({0}), names, limited))
            .kind == BodyEmitError::kMissingTerminator);
  Check(std::get<BodyEmitFailure>(
            WriteLinearBody(Body({0x65, 0}), names, limited))
            .kind == BodyEmitError::kTrailingInstructions);
  Check(std::get<BodyEmitFailure>(
            WriteLinearBody(Body({0x4f, 0}), names, limited))
            .kind == BodyEmitError::kUnsupportedInstruction);
  auto protected_body = body;
  protected_body.tries.push_back({});
  Check(
      std::get<BodyEmitFailure>(WriteLinearBody(protected_body, names, limited))
          .kind == BodyEmitError::kExceptionRegions);
  Check(limited.str().empty());
  std::ostringstream thrown;
  Check(std::holds_alternative<std::size_t>(
      WriteLinearBody(Body({0x60, 0, 0xfe, 0}), names, thrown)));
  Check(thrown.str() == "acc = r0;\nthrow acc;\n");
  std::ostringstream this_value;
  Check(std::holds_alternative<std::size_t>(WriteLinearBody(
      Body({0x6f, 0x64}), names, this_value)));
  Check(this_value.str() == "acc = this;\nreturn acc;\n");
  std::ostringstream debugger_value;
  Check(std::holds_alternative<std::size_t>(WriteLinearBody(
      Body({0x02, 0xb0, 0x64}), names, debugger_value)));
  Check(debugger_value.str() ==
        "acc = true;\ndebugger;\nreturn acc;\n");
  for (const auto& unsupported :
       {Body({0xad, 0x64}), Body({0x6d, 0x64}),
        Body({0x70, 0x64}), Body({0xfc, 0, 0x64}),
        Body({0x6e, 0x64}), Body({0x69, 0x64}),
        Body({0xfc, 1, 0x64}), Body({0x6c, 0x64}),
        Body({0xae, 0x64})}) {
    std::ostringstream refused;
    Check(std::get<BodyEmitFailure>(
              WriteLinearBody(unsupported, names, refused))
              .kind == BodyEmitError::kUnsupportedInstruction);
    Check(refused.str().empty());
  }
  std::ostringstream unbound_function;
  Check(std::get<BodyEmitFailure>(WriteLinearBody(
            Body({0xaf, 0x64}), names, unbound_function))
            .kind == BodyEmitError::kCurrentFunctionBinding);
  Check(unbound_function.str().empty());
  std::ostringstream undef;
  Check(std::holds_alternative<std::size_t>(
      WriteLinearBody(Body({0xd5, 0x65}), names, undef)));
  Check(undef.str() == "return;\n");
  for (const std::size_t count : {200u, 201u, 4096u, 65536u}) {
    auto large = Body({0xd5});
    large.instructions.resize(count, large.instructions.front());
    large.instructions.push_back(Body({0x65}).instructions.front());
    for (std::size_t i = 0; i < large.instructions.size(); ++i)
      large.instructions[i].offset.value = static_cast<std::uint32_t>(i);
    std::ostringstream rendered;
    Check(std::get<std::size_t>(WriteLinearBody(large, names, rendered)) ==
          std::string_view("return;\n").size());
  }
  for (const bool exceptions : {false, true}) {
    FailingBuffer buffer;
    std::ostream failed(&buffer);
    if (exceptions) failed.exceptions(std::ios::badbit | std::ios::failbit);
    Check(std::get<BodyEmitFailure>(WriteLinearBody(body, names, failed))
              .emit_error == EmitError::kWrite);
  }
}
