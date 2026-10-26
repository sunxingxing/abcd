#include "array_pattern_normalize.h"

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/scalar.h"

namespace abcd::source {
namespace {
using abc::Opcode;
struct Mismatch {};
void Require(bool condition) { if (!condition) throw Mismatch{}; }

struct Region { std::size_t index; abc::CodeOffset begin, end, handler; };
struct StepMatch { std::size_t first; };
struct PatternMatch {
  std::size_t begin = 0, end = 0;  // [begin, end) instruction indices.
  abc::RegisterId iterator{};
  std::vector<StepMatch> steps;
  std::vector<std::size_t> kept;   // Kept instruction indices, increasing.
  std::vector<std::size_t> regions;
  std::vector<abc::RegisterId> registers;  // Protocol registers (with iterator).
  std::size_t close = 0;
};

// One exact pinned es2panda DestructuringIterator/Context occurrence.
class PatternParser {
 public:
  PatternParser(const abc::MethodBody& body, std::size_t begin, const abc::FrameLayout& frame,
                const GeneratorNames& names, std::size_t bytes)
      : body_(body), next_(begin), begin_(begin), frame_(frame), names_(names), bytes_(bytes) {}
  PatternMatch Parse() {
    PatternMatch match; match.begin = begin_;
    const auto& acquire = Next();
    Require(acquire.decoded.opcode == Opcode::kGETITERATOR_IMM8 || acquire.decoded.opcode == Opcode::kGETITERATOR_IMM16);
    Keep(match, begin_);
    const auto iterator = Store();
    Keep(match, next_ - 1);
    match.iterator = iterator; Use(iterator);
    Name(iterator, u"next");
    method_ = Store(); closed_ = Constant(Opcode::kLDFALSE);
    done_ = Constant(Opcode::kLDFALSE); result_ = Constant(Opcode::kLDUNDEFINED);
    Require(Distinct({iterator, method_, closed_, done_, result_}));
    const auto context = Pc();
    // Elements: each starts with a protected Step; a binding is one store.
    std::optional<abc::CodeOffset> context_end;
    while (true) {
      Require(next_ < body_.instructions.size());
      const auto pc = Pc();
      const auto* found = RegionAt(pc, true);
      if (!found) break;
      const auto step = *found;
      // The context region shares its first step's begin; skip it here.
      if (!context_end) {
        const auto* outer = RegionAt(pc, false);
        Require(outer && outer->begin == context);
        context_end = outer->end; outer_ = *outer;
      }
      match.steps.push_back({next_}); Keep(match, next_);
      Step(step, match);
      if (next_ < body_.instructions.size() && Pc() != *context_end && !StepStart(Pc())) {
        const auto& binding = Next();
        const auto opcode = binding.decoded.opcode;
        if (opcode == Opcode::kSTA_V8) Require(!Protocol(Operand(binding)));
        else Require(opcode == Opcode::kSTLEXVAR_IMM4_IMM4 || opcode == Opcode::kSTLEXVAR_IMM8_IMM8 ||
                     opcode == Opcode::kWIDE_STLEXVAR_PREF_IMM16_IMM16);
        Keep(match, next_ - 1);
      }
      if (Pc() == *context_end) break;
    }
    Require(context_end && Pc() == *context_end && !match.steps.empty());
    match.regions.push_back(outer_.index);
    // Context end: normal completion close, then the abrupt handler.
    match.close = next_; Keep(match, next_);
    Load(done_); Boolean(true); const auto finished = Branch(true);
    Close(false, match);
    const auto after_normal = Jump();
    Require(Pc() == outer_.handler);
    Register(Opcode::kSTA_V8, result_); Load(done_); Boolean(true); const auto rethrow = Branch(true);
    Load(result_); Close(true, match);
    Require(Pc() == rethrow); Load(result_); Expect(Opcode::kTHROW_PREF_NONE);
    Require(Pc() == finished && Pc() == after_normal);
    match.end = next_;
    // The folded pattern leaves acc unspecified; the next instruction must
    // define it before any read.
    Require(next_ < body_.instructions.size() && !body_.instructions[next_].decoded.accumulator_read);
    match.registers = registers_;
    return match;
  }

 private:
  const abc::BodyInstruction& Next() { Require(next_ < body_.instructions.size()); return body_.instructions[next_++]; }
  abc::CodeOffset Pc() const { Require(next_ < body_.instructions.size()); return body_.instructions[next_].offset; }
  const abc::BodyInstruction& Expect(Opcode opcode) { const auto& site = Next(); Require(site.decoded.opcode == opcode); return site; }
  abc::RegisterId Operand(const abc::BodyInstruction& site, std::size_t operand = 0) const {
    Require(operand < site.decoded.operand_count && site.decoded.operands[operand].kind == abc::OperandKind::kRegister &&
        site.decoded.operands[operand].raw_bits < frame_.locals);
    return {static_cast<std::uint32_t>(site.decoded.operands[operand].raw_bits)};
  }
  void Use(abc::RegisterId reg) {
    if (std::find(registers_.begin(), registers_.end(), reg) == registers_.end()) registers_.push_back(reg);
  }
  bool Protocol(abc::RegisterId reg) const {
    return std::find(registers_.begin(), registers_.end(), reg) != registers_.end();
  }
  static bool Distinct(std::initializer_list<abc::RegisterId> regs) {
    for (auto a = regs.begin(); a != regs.end(); ++a)
      for (auto b = a + 1; b != regs.end(); ++b) if (*a == *b) return false;
    return true;
  }
  abc::RegisterId Store() { const auto reg = Operand(Expect(Opcode::kSTA_V8)); Use(reg); return reg; }
  void Register(Opcode opcode, abc::RegisterId reg) { Require(Operand(Expect(opcode)) == reg); }
  void Load(abc::RegisterId reg) { Register(Opcode::kLDA_V8, reg); }
  abc::RegisterId Constant(Opcode opcode) { Expect(opcode); return Store(); }
  void Constant(Opcode opcode, abc::RegisterId reg) { Expect(opcode); Register(Opcode::kSTA_V8, reg); }
  void Keep(PatternMatch& match, std::size_t index) { match.kept.push_back(index); }
  void Boolean(bool positive) {
    const auto& site = Next();
    Require(site.decoded.opcode == (positive ? Opcode::kISTRUE : Opcode::kISFALSE) ||
        site.decoded.opcode == (positive ? Opcode::kCALLRUNTIME_ISTRUE_PREF_IMM8 : Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8));
    const auto lifted = analysis::LiftScalarInstruction(site.decoded, frame_.registers);
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted); Require(assignment != nullptr);
    const auto* unary = std::get_if<analysis::UnaryExpression>(&assignment->expression);
    Require(unary && unary->op == (positive ? analysis::UnaryOperator::kToBoolean : analysis::UnaryOperator::kLogicalNot) &&
        std::holds_alternative<analysis::Accumulator>(unary->input));
  }
  // Branch on acc != 0 (JNEZ) or == 0 (JEQZ); returns the target.
  abc::CodeOffset Branch(bool nonzero) {
    const auto& site = Next(); const auto opcode = site.decoded.opcode;
    Require(nonzero ? (opcode == Opcode::kJNEZ_IMM8 || opcode == Opcode::kJNEZ_IMM16 || opcode == Opcode::kJNEZ_IMM32) :
        (opcode == Opcode::kJEQZ_IMM8 || opcode == Opcode::kJEQZ_IMM16 || opcode == Opcode::kJEQZ_IMM32));
    Require(site.branch_target.has_value()); return *site.branch_target;
  }
  abc::CodeOffset Jump() {
    const auto& site = Next(); const auto opcode = site.decoded.opcode;
    Require(opcode == Opcode::kJMP_IMM8 || opcode == Opcode::kJMP_IMM16 || opcode == Opcode::kJMP_IMM32);
    Require(site.branch_target.has_value()); return *site.branch_target;
  }
  void Name(abc::RegisterId object, std::u16string_view expected) {
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
  void Call(abc::RegisterId method) {
    Load(method); const auto& site = Expect(Opcode::kCALLTHIS0_IMM8_V8);
    Require(Operand(site, 1) == iterator());
  }
  abc::RegisterId iterator() const { return registers_.front(); }
  // A catch-all try region beginning at `pc`: the innermost (step) one or the
  // outermost (context) one when several begin together.
  const Region* RegionAt(abc::CodeOffset pc, bool innermost) {
    std::optional<Region> found;
    for (std::size_t i = 0; i < body_.tries.size(); ++i) {
      const auto& region = body_.tries[i];
      if (region.offset != pc || region.handler_count != 1 || region.first_handler >= body_.handlers.size()) continue;
      const auto& handler = body_.handlers[region.first_handler];
      if (handler.type) continue;
      const Region candidate{i, region.offset, {region.offset.value + region.length}, handler.offset};
      if (!found || (innermost ? candidate.end.value < found->end.value : candidate.end.value > found->end.value))
        found = candidate;
    }
    if (!found) return nullptr;
    scratch_ = *found; return &scratch_;
  }
  bool StepStart(abc::CodeOffset pc) {
    const auto* region = RegionAt(pc, true);
    return region && region->end.value < outer_.end.value && body_.instructions[next_].decoded.opcode == Opcode::kLDA_V8;
  }
  void Step(Region region, PatternMatch& match) {
    match.regions.push_back(region.index);
    Load(done_); Boolean(true); const auto no_close = Branch(true);
    Call(method_);
    const auto next_result = Store(); Register(Opcode::kTHROW_IFNOTOBJECT_PREF_V8, next_result);
    Require(next_result != done_ && next_result != result_ && next_result != method_);
    Name(next_result, u"done"); Register(Opcode::kSTA_V8, done_);
    Boolean(false); const auto normal = Branch(true);
    Constant(Opcode::kLDTRUE, done_); Expect(Opcode::kLDUNDEFINED); Require(Jump() == no_close);
    Require(Pc() == normal); Name(next_result, u"value"); Register(Opcode::kSTA_V8, result_);
    const auto value_end = Jump();
    Require(Pc() == no_close); Expect(Opcode::kLDUNDEFINED);
    Require(Pc() == region.end);
    Require(Jump() == value_end);
    Require(Pc() == region.handler);
    Register(Opcode::kSTA_V8, result_); Constant(Opcode::kLDTRUE, done_); Load(result_); Expect(Opcode::kTHROW_PREF_NONE);
    Require(Pc() == value_end);
  }
  void Close(bool abrupt, PatternMatch& match) {
    const auto completion = Store();
    Load(closed_); Boolean(true); const auto no_return = Branch(true);
    Constant(Opcode::kLDTRUE, closed_);
    const auto inner_result = Constant(Opcode::kLDUNDEFINED);
    const auto inner_exception = Constant(Opcode::kLDHOLE);
    Require(Distinct({completion, inner_result, inner_exception}));
    const auto* region = RegionAt(Pc(), true);
    Require(region != nullptr); const auto close = *region;
    match.regions.push_back(close.index);
    Name(iterator(), u"return"); Register(Opcode::kSTA_V8, method_);
    const auto test = Store(); Expect(Opcode::kLDUNDEFINED);
    Require(Operand(Expect(Opcode::kEQ_IMM8_V8), 1) == test); Require(Branch(true) == no_return);
    Call(method_); Register(Opcode::kSTA_V8, inner_result);
    Require(Pc() == close.end);
    const auto catch_end = Jump();
    Require(Pc() == close.handler); Register(Opcode::kSTA_V8, inner_exception);
    Require(Pc() == catch_end);
    if (abrupt) {
      Load(completion); Expect(Opcode::kTHROW_PREF_NONE);
      // es2panda keeps the unreachable result check after the throw.
      Load(inner_result); Register(Opcode::kTHROW_IFNOTOBJECT_PREF_V8, inner_result);
      Require(Pc() == no_return); Load(completion); Expect(Opcode::kTHROW_PREF_NONE);
      return;
    }
    // EmitRethrow: rethrow the caught close error unless it is the Hole.
    Load(inner_exception); const auto saved = Store();
    Expect(Opcode::kLDHOLE); const auto hole = Store();
    Load(saved); Require(Operand(Expect(Opcode::kNOTEQ_IMM8_V8), 1) == hole);
    const auto skip = Branch(false); Load(saved); Expect(Opcode::kTHROW_PREF_NONE);
    Require(Pc() == skip); Load(inner_result); Register(Opcode::kTHROW_IFNOTOBJECT_PREF_V8, inner_result);
    Require(Pc() == no_return); Load(completion);
  }

  const abc::MethodBody& body_;
  std::size_t next_, begin_;
  const abc::FrameLayout& frame_;
  const GeneratorNames& names_;
  std::size_t bytes_;
  abc::RegisterId method_{}, closed_{}, done_{}, result_{};
  std::vector<abc::RegisterId> registers_;
  Region outer_{}, scratch_{};
};

// Instruction-level register liveness restricted to a small register set.
class Liveness {
 public:
  Liveness(const abc::MethodBody& body, std::span<const abc::RegisterId> registers)
      : body_(body), registers_(registers) {}
  // Live-in mask at instruction `index`.
  std::vector<std::uint32_t> Compute() {
    const auto count = body_.instructions.size();
    std::vector<std::uint32_t> live(count + 1, 0);
    std::vector<std::size_t> index_of_pc;
    const auto find = [&](abc::CodeOffset pc) -> std::optional<std::size_t> {
      const auto at = std::lower_bound(body_.instructions.begin(), body_.instructions.end(), pc,
          [](const auto& site, abc::CodeOffset wanted) { return site.offset.value < wanted.value; });
      if (at == body_.instructions.end() || at->offset != pc) return {};
      return static_cast<std::size_t>(at - body_.instructions.begin());
    };
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t i = count; i-- > 0;) {
        const auto& site = body_.instructions[i];
        std::uint32_t out = 0;
        const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
        const bool terminal = transfer && (*transfer == abc::ControlTransfer::kReturn ||
                                           *transfer == abc::ControlTransfer::kThrow);
        const bool jump = site.decoded.jump && !site.decoded.conditional;
        if (!terminal && !jump && i + 1 < count) out |= live[i + 1];
        if (site.branch_target) if (const auto target = find(*site.branch_target)) out |= live[*target];
        for (const auto& region : body_.tries) {
          if (site.offset.value < region.offset.value || site.offset.value >= region.offset.value + region.length) continue;
          for (std::uint32_t h = 0; h < region.handler_count && region.first_handler + h < body_.handlers.size(); ++h)
            if (const auto handler = find(body_.handlers[region.first_handler + h].offset)) out |= live[*handler];
        }
        std::uint32_t in = out;
        for (std::size_t o = 0; o < site.decoded.operand_count; ++o) {
          const auto& operand = site.decoded.operands[o];
          if (operand.kind != abc::OperandKind::kRegister) continue;
          const auto bit = Bit(operand.raw_bits);
          if (!bit) continue;
          if (operand.access == abc::OperandAccess::kWrite) in &= ~*bit;
        }
        for (std::size_t o = 0; o < site.decoded.operand_count; ++o) {
          const auto& operand = site.decoded.operands[o];
          if (operand.kind != abc::OperandKind::kRegister || operand.access == abc::OperandAccess::kWrite) continue;
          if (const auto bit = Bit(operand.raw_bits)) in |= *bit;
        }
        if (in != live[i]) { live[i] = in; changed = true; }
      }
    }
    return live;
  }
 private:
  std::optional<std::uint32_t> Bit(std::uint64_t raw) const {
    for (std::size_t r = 0; r < registers_.size(); ++r)
      if (registers_[r].value == raw) return 1u << r;
    return {};
  }
  const abc::MethodBody& body_;
  std::span<const abc::RegisterId> registers_;
};

bool Inside(abc::CodeOffset pc, abc::CodeOffset begin, abc::CodeOffset end) {
  return pc.value > begin.value && pc.value < end.value;
}
}  // namespace

std::variant<ArrayPatternBody, abc::MetadataError> NormalizeArrayPatterns(
    const abc::MethodBody& body, std::span<const abc::CodeOffset> pcs,
    const abc::FrameLayout& frame, const GeneratorNames& names, std::size_t bytes) {
  try {
    ArrayPatternBody result;
    std::vector<PatternMatch> matches;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto opcode = body.instructions[i].decoded.opcode;
      if (opcode != Opcode::kGETITERATOR_IMM8 && opcode != Opcode::kGETITERATOR_IMM16) continue;
      if (!matches.empty() && i < matches.back().end) continue;
      try {
        auto match = PatternParser(body, i, frame, names, bytes).Parse();
        if (match.registers.size() > 32) continue;
        const auto begin = body.instructions[match.begin].offset, end = body.instructions[match.end].offset;
        std::vector<bool> consumed(body.tries.size());
        for (const auto r : match.regions) consumed[r] = true;
        bool closed = true;
        // Nothing outside enters the interior; unconsumed regions either
        // enclose the whole pattern or lie outside it.
        for (std::size_t k = 0; k < body.instructions.size() && closed; ++k) {
          if (k >= match.begin && k < match.end) continue;
          const auto& target = body.instructions[k].branch_target;
          if (target && Inside(*target, begin, end)) closed = false;
        }
        for (std::size_t r = 0; r < body.tries.size() && closed; ++r) {
          if (consumed[r]) continue;
          const auto& region = body.tries[r];
          const abc::CodeOffset first = region.offset, last{region.offset.value + region.length};
          const bool encloses = first.value <= begin.value && last.value >= end.value;
          const bool outside = last.value <= begin.value || first.value >= end.value;
          if (!encloses && !outside) closed = false;
          for (std::uint32_t h = 0; h < region.handler_count && region.first_handler + h < body.handlers.size(); ++h)
            if (Inside(body.handlers[region.first_handler + h].offset, begin, end) ||
                body.handlers[region.first_handler + h].offset == begin) closed = false;
        }
        if (!closed) continue;
        // Removed register writes must be dead at the pattern's exits.
        const auto live = Liveness(body, match.registers).Compute();
        if (live[match.end]) continue;
        bool dead = true;
        for (std::size_t r = 0; r < body.tries.size() && dead; ++r) {
          if (consumed[r]) continue;
          const auto& region = body.tries[r];
          if (region.offset.value > begin.value || region.offset.value + region.length < end.value) continue;
          for (std::uint32_t h = 0; h < region.handler_count && region.first_handler + h < body.handlers.size(); ++h) {
            const auto handler = body.handlers[region.first_handler + h].offset;
            const auto at = std::find_if(body.instructions.begin(), body.instructions.end(),
                [&](const auto& site) { return site.offset == handler; });
            if (at != body.instructions.end() && live[static_cast<std::size_t>(at - body.instructions.begin())]) dead = false;
          }
        }
        if (!dead) continue;
        matches.push_back(std::move(match));
      } catch (const Mismatch&) {}
    }
    if (matches.empty()) {
      result.body = body;
      if (pcs.empty()) for (const auto& site : body.instructions) result.source_pcs.push_back(site.offset);
      else result.source_pcs.assign(pcs.begin(), pcs.end());
      return result;
    }
    if (matches.size() > bytes / sizeof(FoldRange))
      return abc::MetadataError::kResourceLimit;
    result.folds.reserve(matches.size());
    if (result.folds.capacity() > bytes / sizeof(FoldRange))
      return abc::MetadataError::kResourceLimit;
    bytes -= result.folds.capacity() * sizeof(FoldRange);
    std::vector<bool> drop(body.instructions.size()), consumed(body.tries.size());
    for (const auto& match : matches) {
      result.folds.push_back({body.instructions[match.begin].offset,
                             body.instructions[match.end].offset});
      for (std::size_t k = match.begin; k < match.end; ++k) drop[k] = true;
      for (const auto k : match.kept) drop[k] = false;
      for (const auto r : match.regions) consumed[r] = true;
      result.sites.push_back({body.instructions[match.begin].offset, analysis::PatternAction::kAcquire, match.iterator});
      for (const auto& step : match.steps)
        result.sites.push_back({body.instructions[step.first].offset, analysis::PatternAction::kStep, match.iterator});
      result.sites.push_back({body.instructions[match.close].offset, analysis::PatternAction::kClose, match.iterator});
    }
    // Rebase kept instructions to contiguous offsets. Every surviving branch
    // target, region boundary and handler must name a kept instruction (or the
    // body end); a dropped target means the fold was not closed.
    std::vector<std::uint32_t> rebased(body.instructions.size() + 1);
    std::uint32_t next = 0;
    for (std::size_t k = 0; k < body.instructions.size(); ++k) {
      rebased[k] = next;
      if (!drop[k]) next += body.instructions[k].decoded.size;
    }
    rebased[body.instructions.size()] = next;
    const auto map = [&](abc::CodeOffset pc) -> abc::CodeOffset {
      if (!body.instructions.empty()) {
        const auto& last = body.instructions.back();
        if (pc.value == last.offset.value + last.decoded.size) return {next};
      }
      const auto at = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
          [](const auto& site, abc::CodeOffset wanted) { return site.offset.value < wanted.value; });
      if (at == body.instructions.end() || at->offset != pc) throw abc::MetadataError::kRead;
      const auto k = static_cast<std::size_t>(at - body.instructions.begin());
      if (drop[k]) throw abc::MetadataError::kRead;
      return {rebased[k]};
    };
    for (std::size_t k = 0; k < body.instructions.size(); ++k) {
      if (drop[k]) continue;
      auto site = body.instructions[k];
      site.offset = {rebased[k]};
      if (site.branch_target) {
        site.branch_target = map(*site.branch_target);
        site.decoded.branch_delta = static_cast<std::int64_t>(site.branch_target->value) - site.offset.value;
      }
      result.body.instructions.push_back(site);
      result.source_pcs.push_back(pcs.empty() ? body.instructions[k].offset : pcs[k]);
    }
    for (std::size_t r = 0; r < body.tries.size(); ++r) {
      if (consumed[r]) continue;
      auto region = body.tries[r];
      const auto first_pc = map(region.offset);
      const auto last_pc = map({region.offset.value + region.length});
      const auto first = static_cast<std::uint32_t>(result.body.handlers.size());
      for (std::uint32_t h = 0; h < region.handler_count; ++h) {
        auto handler = body.handlers.at(region.first_handler + h);
        handler.offset = map(handler.offset);
        result.body.handlers.push_back(handler);
      }
      region.offset = first_pc; region.length = last_pc.value - first_pc.value;
      region.first_handler = first;
      result.body.tries.push_back(region);
    }
    for (auto& site : result.sites) site.pc = map(site.pc);
    std::sort(result.sites.begin(), result.sites.end(),
        [](const auto& a, const auto& b) { return a.pc.value < b.pc.value; });
    return result;
  } catch (abc::MetadataError error) { return error;
  } catch (const std::bad_alloc&) { return abc::MetadataError::kResourceLimit;
  } catch (const std::length_error&) { return abc::MetadataError::kResourceLimit;
  } catch (const std::out_of_range&) { return abc::MetadataError::kRead; }
}
}  // namespace abcd::source
