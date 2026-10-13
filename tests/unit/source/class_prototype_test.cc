#include "../../../src/source/class_initializers.h"
#include "../../../src/source/evaluation_plan.h"
#include "abcd/analysis/value_import.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Case { kNamed, kSplit, kDiamond, kLoop, kEscape, kEffect, kComputed,
  kComputedCopy, kUnknownKey, kCopy, kPhi, kMixedPhi, kUsed, kOrdinary, kWrongKey };
ValueProgram Graph(abc::ProfileId profile, Case kind, unsigned count) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t value) { bytes.push_back(value); bytes.push_back(value >> 8); };
  const auto load = [&](std::uint8_t reg) { op(abc::Opcode::kLDA_V8); bytes.push_back(reg); };
  const auto save = [&](std::uint8_t reg) { op(abc::Opcode::kSTA_V8); bytes.push_back(reg); };
  constexpr std::uint8_t kRegisters = 4;
  const bool branch = kind == Case::kDiamond || kind == Case::kLoop || kind == Case::kPhi || kind == Case::kMixedPhi;
  const bool computed = kind == Case::kComputed || kind == Case::kComputedCopy || kind == Case::kUnknownKey;
  const unsigned args = branch || kind == Case::kUnknownKey ? 1 : 0;
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0); save(1);
  for (unsigned i = 0; i < count; ++i) {
    load(1); save(2);
    if (kind == Case::kCopy) { load(2); save(3); load(3); save(2); }
    if (kind == Case::kOrdinary) { op(abc::Opcode::kCREATEEMPTYOBJECT); save(2); }
    if (kind == Case::kSplit) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (branch) {
      load(kRegisters);
      if (kind == Case::kLoop) { op(abc::Opcode::kJNEZ_IMM8); bytes.push_back(static_cast<std::uint8_t>(-2)); }
      else {
        const auto conditional = bytes.size(); op(abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
        load(1); save(2);
        const auto jump = bytes.size(); op(abc::Opcode::kJMP_IMM8); bytes.push_back(0);
        bytes[conditional + 1] = bytes.size() - conditional;
        if (kind == Case::kMixedPhi) op(abc::Opcode::kCREATEEMPTYOBJECT); else load(1);
        save(2); bytes[jump + 1] = bytes.size() - jump;
      }
    }
    if (kind == Case::kEscape) { load(1); op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == Case::kEffect) op(abc::Opcode::kDEBUGGER);
    if (computed) {
      if (kind == Case::kUnknownKey) load(kRegisters);
      else { op(abc::Opcode::kLDA_STR_ID16); word(15); }
      if (kind == Case::kComputedCopy) { save(3); load(3); }
      op(abc::Opcode::kLDOBJBYVALUE_IMM8_V8); bytes.push_back(0); bytes.push_back(2);
    } else {
      load(2); op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.push_back(0); word(13);
    }
    if (kind == Case::kUsed) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
  }
  op(abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(bytes);
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, kRegisters + args));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, kRegisters, args));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(kRegisters, args, {false, false, false, false}));
  return std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
}
std::uint64_t Run(abc::ProfileId profile, Case kind, unsigned count) {
  auto graph = Graph(profile, kind, count);
  const auto found = FindClassPrototypeReads(graph);
  test::Check(std::holds_alternative<ClassPrototypeReads>(found));
  auto reads = std::get<ClassPrototypeReads>(found);
  const bool candidate = kind != Case::kOrdinary && kind != Case::kMixedPhi && kind != Case::kUnknownKey;
  test::Check(reads.reads.size() == (candidate ? count : 0));
  std::vector<ExpressionConstant> operations(graph.ops.size());
  const std::array<std::u16string, 1> names{kind == Case::kWrongKey ? u"getter" : u"prototype"};
  const std::array<std::u16string, 1> texts{u"prototype"};
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) operations[i] = NameId{0};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) operations[i] = TextId{0};
  }
  const ExpressionConstants constants{operations, names.size(), texts.size()};
  std::erase_if(reads.reads, [&](const auto& site) { return !HasClassPrototypeKey(site, constants, names, texts); });
  const auto projected = ProjectClassMembers(graph, {}, {}, {}, nullptr, reads.reads);
  test::Check(std::holds_alternative<ClassProjection>(projected));
  const auto& projection = std::get<ClassProjection>(projected);
  const bool removed = candidate && kind != Case::kUsed && kind != Case::kWrongKey;
  test::Check(projection.prototypes.size() == (removed ? count : 0));
  for (const auto& site : reads.reads) {
    test::Check(projection.owners[site.read.value] == (removed ? std::optional<OpId>{site.creation} : std::nullopt));
    if (site.key) {
      auto incorrect = operations; incorrect[site.key->value] = NameId{0};
      test::Check(!HasClassPrototypeKey(site, {incorrect, 1, 1}, names, texts));
      const std::array<std::u16string, 1> incorrect_text{u"getter"};
      test::Check(!HasClassPrototypeKey(site, constants, names, incorrect_text));
    }
  }
  if (!reads.reads.empty()) {
    auto forged = reads.reads; forged.front().creation = forged.front().read;
    test::Check(std::holds_alternative<ClassInitializerError>(ProjectClassMembers(graph, {}, {}, {}, nullptr, forged)));
    forged = reads.reads; forged.front().key = forged.front().read;
    test::Check(std::holds_alternative<ClassInitializerError>(ProjectClassMembers(graph, {}, {}, {}, nullptr, forged)));
    forged = reads.reads; forged.insert(forged.begin(), forged.front());
    test::Check(std::holds_alternative<ClassInitializerError>(ProjectClassMembers(graph, {}, {}, {}, nullptr, forged)));
    auto exact = SemanticBudget{projection.allocated_bytes, projection.work};
    test::Check(std::holds_alternative<ClassProjection>(ProjectClassMembers(graph, {}, exact, {}, nullptr, reads.reads)));
    --exact.work;
    test::Check(std::get<ClassInitializerError>(ProjectClassMembers(graph, {}, exact, {}, nullptr, reads.reads)) == ClassInitializerError::kWorkLimit);
    // Independent verifier cannot accept a caller-forged projection owner.
    if (kind == Case::kNamed || kind == Case::kComputed) {
      auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, {}, {}, {}, nullptr, reads.reads);
      test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
      auto plan = std::get<EvaluationPlan>(std::move(evaluated));
      test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan)));
      plan.class_projection.owners[reads.reads.front().read.value].reset();
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, plan)));
    }
  }
  if (candidate) {
    test::Check(std::get<ClassPrototypeError>(FindClassPrototypeReads(graph, {0})) == ClassPrototypeError::kResourceLimit);
    test::Check(std::get<ClassPrototypeError>(FindClassPrototypeReads(graph, {UINT32_MAX, 0})) == ClassPrototypeError::kWorkLimit);
    auto invalid = graph;
    const auto read = std::get<ClassPrototypeReads>(found).reads.front().read;
    invalid.uses[invalid.ops[read.value].first_use.value].operand = ValueId{static_cast<std::uint32_t>(graph.values.size())};
    test::Check(std::get<ClassPrototypeError>(FindClassPrototypeReads(invalid)) == ClassPrototypeError::kFlow);
  }
  std::cout << "prototype count=" << count << " case=" << static_cast<unsigned>(kind) << " work=" << reads.work + projection.work << '\n';
  return reads.work + projection.work;
}
}
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    std::array<std::uint64_t, 15> previous{};
    for (const auto count : {1U, 200U, 201U, 402U}) {
      std::size_t i = 0;
      for (const auto kind : {Case::kNamed, Case::kSplit, Case::kDiamond, Case::kLoop, Case::kEscape,
          Case::kEffect, Case::kComputed, Case::kComputedCopy, Case::kUnknownKey, Case::kCopy,
          Case::kPhi, Case::kMixedPhi, Case::kUsed, Case::kOrdinary, Case::kWrongKey}) {
        const auto work = Run(profile, kind, count);
        if (count == 201 || count == 402) test::Check(work < previous[i] * 3);
        previous[i++] = work;
      }
    }
  }
}
