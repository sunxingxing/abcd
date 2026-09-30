#include "generator_delegate.h"

#include <array>
#include <limits>
#include <new>
#include <string_view>
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "abcd/analysis/coroutine.h"

namespace abcd::source {
namespace {
using abc::Opcode;
using Error = FunctionEmitError;
void Require(bool condition) { if (!condition) throw Error::kKind; }
// OpenHarmony-v7.0-Release es2panda/compiler/{base/iterators.cpp,
// function/functionBuilder.cpp}, pinned by openharmony-v7.0-release.lock.json:
// Iterator/YieldStar allocation order and nested RegScopes. Reused temporaries
// are checked at their actual lifetime; do not accept generic loop resemblance.
enum Slot : std::uint32_t {
  kClosed, kMethod, kIterator, kResult, kMode, kNext, kExitReturn, kIterValue,
  kCompletion, kInnerResult, kInnerException, kException, kHole, kSlotCount
};
enum class Label : std::size_t { kLoop, kThrow, kReturn, kCall, kComplete,
    kNormal, kNoClose, kCloseEnd, kRethrowSkip, kCount };
enum class Route : std::size_t { kNext, kCallNext, kThrow, kCallThrow,
    kClosed, kNoReturn, kCloseDone, kRethrow, kCallReturn, kComplete,
    kResume, kNormal, kCount };
struct Edge { abc::CodeOffset destination; Label label; };
class DelegateParser {
 public:
  DelegateParser(const abc::MethodBody& body, std::size_t begin, abc::RegisterId state,
      const abc::FrameLayout& frame, const GeneratorNames& names, std::size_t bytes)
      : body_(body), begin_(begin), next_(begin), state_(state), frame_(frame), names_(names), bytes_(bytes) {}
  GeneratorDelegate Parse() {
    const auto& create = Next();
    Require(create.decoded.opcode == Opcode::kGETITERATOR_IMM8 || create.decoded.opcode == Opcode::kGETITERATOR_IMM16);
    const auto iterator = StoreAny();
    Require(iterator.value >= kIterator);
    first_ = {iterator.value - kIterator};
    Require(first_.value < frame_.locals && kSlotCount <= frame_.locals - first_.value &&
        (state_.value < first_.value || state_.value - first_.value >= kSlotCount));
    Name(kIterator, u"next"); Store(kMethod); Constant(Opcode::kLDFALSE, kClosed);
    Constant(Opcode::kLDUNDEFINED, kResult); Mode(analysis::CoroutineResumeMode::kNext); Store(kMode); Move(kNext, kMethod);
    Mark(Label::kLoop); Constant(Opcode::kLDFALSE, kExitReturn);
    Mode(analysis::CoroutineResumeMode::kNext); Equal(true, kMode); Branch(Route::kNext, Label::kThrow);
    Move(kMethod, kNext); Jump(Route::kCallNext, Label::kCall);
    Mark(Label::kThrow); Mode(analysis::CoroutineResumeMode::kThrow); Equal(true, kMode); Branch(Route::kThrow, Label::kReturn);
    Name(kIterator, u"throw"); Store(kMethod); Store(kCompletion); Expect(Opcode::kLDUNDEFINED); Equal(false, kCompletion);
    Branch(Route::kCallThrow, Label::kCall);
    Store(kCompletion); Load(kClosed); Expect(Opcode::kISTRUE); Branch(Route::kClosed, Label::kNoClose, true);
    Constant(Opcode::kLDTRUE, kClosed); Constant(Opcode::kLDUNDEFINED, kInnerResult); Constant(Opcode::kLDHOLE, kInnerException);
    const auto close_begin = Pc(); Name(kIterator, u"return"); Store(kMethod); Store(kException);
    Expect(Opcode::kLDUNDEFINED); Equal(false, kException); Branch(Route::kNoReturn, Label::kNoClose, true);
    Call(false); Store(kInnerResult);
    const auto close_end = Pc(); Jump(Route::kCloseDone, Label::kCloseEnd);
    const auto close_catch = Pc(); Store(kInnerException); Mark(Label::kCloseEnd); const auto close_catch_end = Pc();
    Load(kInnerException); Store(kException); Constant(Opcode::kLDHOLE, kHole); Load(kException);
    Binary(Opcode::kNOTEQ_IMM8_V8, kHole); Branch(Route::kRethrow, Label::kRethrowSkip);
    Load(kException); Expect(Opcode::kTHROW_PREF_NONE); Mark(Label::kRethrowSkip);
    Load(kInnerResult); Guard(kInnerResult); Mark(Label::kNoClose); Load(kCompletion);
    const auto reject = Pc(); Expect(Opcode::kTHROW_NOTEXISTS_PREF_NONE);
    Mark(Label::kReturn); Constant(Opcode::kLDTRUE, kExitReturn); Name(kIterator, u"return"); Store(kMethod); Store(kCompletion);
    Expect(Opcode::kLDUNDEFINED); Equal(false, kCompletion); Branch(Route::kCallReturn, Label::kCall);
    Load(kResult); Expect(Opcode::kRETURN);
    Mark(Label::kCall); Call(true); Store(kResult); Guard(kResult); Name(kResult, u"done"); Expect(Opcode::kISTRUE);
    Branch(Route::kComplete, Label::kComplete, true); Load(kResult);
    const auto suspend = Pc(); Register(Opcode::kSUSPENDGENERATOR_V8, state_); Register(Opcode::kLDA_V8, state_);
    const auto resume = Pc(); Expect(Opcode::kRESUMEGENERATOR); const auto result_store = Pc(); Store(kResult);
    Register(Opcode::kLDA_V8, state_); const auto mode = Pc(); Expect(Opcode::kGETRESUMEMODE);
    const auto mode_store = Pc(); Store(kMode); const auto branch = Pc(); Jump(Route::kResume, Label::kLoop);
    Mark(Label::kComplete); Load(kExitReturn); Expect(Opcode::kISFALSE); Branch(Route::kNormal, Label::kNormal, true);
    Name(kResult, u"value"); Expect(Opcode::kRETURN); Mark(Label::kNormal); Name(kResult, u"value");
    const auto continuation = Pc();
    for (const auto& edge : edges_) Require(edge.destination == labels_[static_cast<std::size_t>(edge.label)]);
    analysis::YieldRegion origin{create.offset, suspend, resume, result_store, mode, mode_store,
        continuation, state_, Reg(kIterator), Reg(kResult), Reg(kMode), branch, reject};
    origin.form = analysis::YieldForm::kDelegate;
    origin.delegate_registers = analysis::YieldDelegateRegisters{first_, kSlotCount};
    return {origin, begin_, next_, close_begin, close_end, close_catch, close_catch_end};
  }
 private:
  abc::CodeOffset Pc() const { Require(next_ < body_.instructions.size()); return body_.instructions[next_].offset; }
  const abc::BodyInstruction& Next() { Require(next_ < body_.instructions.size()); return body_.instructions[next_++]; }
  const abc::BodyInstruction& Expect(Opcode opcode) { const auto& site = Next(); Require(site.decoded.opcode == opcode); return site; }
  abc::RegisterId Operand(const abc::BodyInstruction& site, std::size_t operand = 0) const {
    Require(operand < site.decoded.operand_count && site.decoded.operands[operand].kind == abc::OperandKind::kRegister &&
        site.decoded.operands[operand].raw_bits < frame_.registers);
    return {static_cast<std::uint32_t>(site.decoded.operands[operand].raw_bits)};
  }
  abc::RegisterId Reg(Slot slot) const { return {first_.value + slot}; }
  void Register(Opcode opcode, abc::RegisterId reg) { Require(Operand(Expect(opcode)) == reg); }
  abc::RegisterId StoreAny() { return Operand(Expect(Opcode::kSTA_V8)); }
  void Store(Slot slot) { Register(Opcode::kSTA_V8, Reg(slot)); }
  void Load(Slot slot) { Register(Opcode::kLDA_V8, Reg(slot)); }
  void Move(Slot destination, Slot source) {
    const auto& site = Next();
    Require(site.decoded.opcode == Opcode::kMOV_V4_V4 || site.decoded.opcode == Opcode::kMOV_V8_V8 || site.decoded.opcode == Opcode::kMOV_V16_V16);
    Require(Operand(site) == Reg(destination) && Operand(site, 1) == Reg(source));
  }
  void Constant(Opcode opcode, Slot slot) { Expect(opcode); Store(slot); }
  void Mode(analysis::CoroutineResumeMode mode) {
    const auto& site = Expect(Opcode::kLDAI_IMM32);
    Require(site.decoded.operand_count == 1 && site.decoded.operands[0].raw_bits == static_cast<std::uint32_t>(mode));
  }
  void Binary(Opcode opcode, Slot slot) { Require(Operand(Expect(opcode), 1) == Reg(slot)); }
  void Equal(bool strict, Slot slot) { Binary(strict ? Opcode::kSTRICTEQ_IMM8_V8 : Opcode::kEQ_IMM8_V8, slot); }
  void Name(Slot object, std::u16string_view expected) {
    Load(object); const auto& site = Next();
    Require((site.decoded.opcode == Opcode::kLDOBJBYNAME_IMM8_ID16 || site.decoded.opcode == Opcode::kLDOBJBYNAME_IMM16_ID16) &&
        site.decoded.operand_count == 2 && site.decoded.operands[1].kind == abc::OperandKind::kString &&
        site.decoded.operands[1].raw_bits <= UINT32_MAX);
    const auto resolved = abc::ResolvePoolReference(names_.file, names_.header, names_.index, names_.method,
        {static_cast<std::uint32_t>(site.decoded.operands[1].raw_bits)}, abc::PoolEntityKind::kString);
    const auto* reference = std::get_if<abc::PoolReference>(&resolved);
    if (!reference) throw std::get<abc::MetadataError>(resolved);
    const auto read = abc::ReadStringEntity(names_.file, names_.header, reference->offset, {bytes_});
    const auto* name = std::get_if<std::u16string>(&read);
    if (!name) throw std::get<abc::MetadataError>(read);
    Require(*name == expected);
  }
  void Call(bool argument) {
    Load(kMethod); const auto& site = Expect(argument ? Opcode::kCALLTHIS1_IMM8_V8_V8 : Opcode::kCALLTHIS0_IMM8_V8);
    Require(Operand(site, 1) == Reg(kIterator));
    if (argument) Require(Operand(site, 2) == Reg(kResult));
  }
  void Guard(Slot slot) { Register(Opcode::kTHROW_IFNOTOBJECT_PREF_V8, Reg(slot)); }
  void Mark(Label label) { labels_[static_cast<std::size_t>(label)] = Pc(); }
  void Branch(Route route, Label label, bool inverse = false) {
    const auto& site = Next(); const auto opcode = site.decoded.opcode;
    Require(inverse ? (opcode == Opcode::kJNEZ_IMM8 || opcode == Opcode::kJNEZ_IMM16 || opcode == Opcode::kJNEZ_IMM32) :
        (opcode == Opcode::kJEQZ_IMM8 || opcode == Opcode::kJEQZ_IMM16 || opcode == Opcode::kJEQZ_IMM32));
    Require(site.branch_target.has_value()); edges_[static_cast<std::size_t>(route)] = {*site.branch_target, label};
  }
  void Jump(Route route, Label label) {
    const auto& site = Next();
    Require(site.decoded.opcode == Opcode::kJMP_IMM8 || site.decoded.opcode == Opcode::kJMP_IMM16 || site.decoded.opcode == Opcode::kJMP_IMM32);
    Require(site.branch_target.has_value()); edges_[static_cast<std::size_t>(route)] = {*site.branch_target, label};
  }
  const abc::MethodBody& body_;
  std::size_t begin_, next_;
  abc::RegisterId state_, first_{};
  const abc::FrameLayout& frame_;
  const GeneratorNames& names_;
  std::size_t bytes_;
  std::array<abc::CodeOffset, static_cast<std::size_t>(Label::kCount)> labels_{};
  std::array<Edge, static_cast<std::size_t>(Route::kCount)> edges_{};
};
}
std::variant<GeneratorDelegate, FunctionEmitError, abc::MetadataError> ParseGeneratorDelegate(
    const abc::MethodBody& body, std::size_t begin, abc::RegisterId state,
    const abc::FrameLayout& frame, const GeneratorNames& names, std::size_t bytes) {
  try { return DelegateParser(body, begin, state, frame, names, bytes).Parse(); }
  catch (Error error) { return error; }
  catch (abc::MetadataError error) { return error; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
}
}
