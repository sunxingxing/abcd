#include <set>
#include "abcd/analysis/isa_catalog.h"
#include "abcd/analysis/semantic_ir.h"
#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  std::set<abc::Opcode> seen;
  for (const auto& spec : analysis::PinnedIsaCatalog()) {
    test::Check(seen.insert(spec.opcode).second);
    std::array<std::uint8_t, abc::kMaxInstructionBytes> bytes{};
    const auto op=static_cast<unsigned>(spec.opcode);
    bytes[0]=op & 255; if(op>255) bytes[1]=op >> 8;
    const auto result=abc::DecodeInstruction(std::span(bytes).first(spec.size),abc::ProfileId::kDynamic1206);
    const auto* d=std::get_if<abc::DecodedInstruction>(&result);
    test::Check(d && d->opcode==spec.opcode && d->size==spec.size);
    test::Check((op>255 ? bytes[1]*256+bytes[0] : bytes[0])==op);
    for (std::size_t i=0;i<d->operand_count;++i) test::Check(d->operands[i].width_bits>0);
  }
  test::Check(seen.size()==332);
  // A refusal in one instruction must not prevent lifting the following one.
  abc::BodyInstruction unsupported{{0},{}};
  unsupported.decoded.opcode=abc::Opcode::kCREATEGENERATOROBJ_V8;
  test::Check(std::holds_alternative<analysis::SemanticFailure>(analysis::LiftSemanticInstruction(unsupported,abc::ProfileId::kDynamic1206,0)));
  abc::BodyInstruction next{{1},{}}; next.decoded.opcode=abc::Opcode::kLDUNDEFINED;
  const auto lifted=analysis::LiftSemanticInstruction(next,abc::ProfileId::kDynamic1206,0);
  test::Check(std::holds_alternative<analysis::SemanticInstruction>(lifted));
}
