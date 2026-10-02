#include "../../src/source/prepared_family.h"
#include "../../src/source/expression_forest.h"
#include "analysis/ssa_fixture.h"

#include <unistd.h>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
// File-backed Code entities and real instruction decoding; metadata is a
// synthetic fixture, not evidence for the container parser.
constexpr unsigned kByteBits = 8, kUlebBits = 7, kSlotBits = 4, kFunctionKindShift = 8;
constexpr std::uint32_t kPool = 64, kRecord = 128, kNestedName = 220, kWriteName = 240,
    kCaller = 256, kParent = 300, kLeft = 320, kRight = 340, kWriter = 360, kCodeStart = 400;
constexpr std::uint8_t kImplicit = 3, kLocals = 2, kFirstParameter = kImplicit + kLocals;
enum class BindingFault { kOrder, kDuplicate, kSelfOwner };
std::uint8_t Op(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  constexpr std::uint8_t kContinue = 1U << kUlebBits;
  do {
    const auto low = static_cast<std::uint8_t>(value & (kContinue - 1)); value >>= kUlebBits;
    bytes.push_back(low | (value ? kContinue : 0));
  } while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::uint8_t locals, std::uint8_t parameters,
    const std::vector<std::uint8_t>& code) {
  const auto begin = static_cast<std::uint32_t>(bytes.size());
  Uleb(bytes, locals); Uleb(bytes, parameters + kImplicit); Uleb(bytes, code.size()); Uleb(bytes, 0);
  const auto body = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{begin}, {body}, locals, static_cast<std::uint32_t>(parameters + kImplicit), static_cast<std::uint32_t>(code.size()), 0};
}
std::uint64_t Run(abc::ProfileId profile, unsigned count, bool print) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  const std::vector<std::uint32_t> pool{kParent, kLeft, kRight, kWriter, kNestedName, kWriteName};
  for (std::size_t i = 0; i < pool.size(); ++i)
    for (unsigned byte = 0; byte < sizeof(std::uint32_t); ++byte)
      bytes[kPool + i * sizeof(std::uint32_t) + byte] = static_cast<std::uint8_t>(pool[i] >> (byte * kByteBits));
  const auto text = [&](std::uint32_t at, std::string_view value) {
    // Panda ASCII strings store (UTF-16 length << 1) | ASCII in ULEB128.
    bytes[at++] = static_cast<std::uint8_t>((value.size() << 1) | 1);
    for (const auto character : value) bytes[at++] = static_cast<std::uint8_t>(character);
  };
  text(kNestedName, "nested"); text(kWriteName, "write");
  const auto caller = Append(bytes, 0, 0, {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 3,
      Op(abc::Opcode::kRETURNUNDEFINED)});
  std::vector<std::uint8_t> code{Op(abc::Opcode::kNEWLEXENV_IMM8), 2,
      Op(abc::Opcode::kLDA_V8), kFirstParameter, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      Op(abc::Opcode::kLDA_V8), kFirstParameter + 1, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 1U << kSlotBits};
  for (unsigned i = 0; i < count; ++i) {
    // The ordinary closure executes before the literal. AST dependency order
    // constructs the literal's two closures first; that must not select the
    // ordinary closure's FunctionBinding or change runtime capture identity.
    code.insert(code.end(), {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 1, 0, 0,
        Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kLDA_V8), kFirstParameter + 2,
        Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
        Op(abc::Opcode::kCREATEEMPTYOBJECT), Op(abc::Opcode::kSTA_V8), 1,
        Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 2, 0, 0,
        Op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 4, 0, 1,
        Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 1,
        Op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 5, 0, 1,
        Op(abc::Opcode::kLDA_V8), kFirstParameter + 2, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1});
  }
  code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 1, Op(abc::Opcode::kRETURN)});
  const auto parent = Append(bytes, kLocals, 3, code);
  const auto left = Append(bytes, 0, 0, {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kRETURN)});
  const auto right = Append(bytes, 0, 0, {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 1U << kSlotBits, Op(abc::Opcode::kRETURN)});
  const auto writer = Append(bytes, 0, 1, {Op(abc::Opcode::kLDA_V8), kImplicit,
      Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 1U << kSlotBits,
      Op(abc::Opcode::kRETURN)});
  char path[] = "/tmp/abcd-closure-schedule-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = profile; header.file_size = bytes.size();
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      profile == abc::ProfileId::kDynamic1206 ? abc::AbcVersion{12, 0, 6, 0} : abc::AbcVersion{13, 0, 1, 0};
  abc::MetadataIndex index; index.regions.push_back({kCaller, kCodeStart, {}, {static_cast<std::uint32_t>(pool.size()), kPool}, {}, {}});
  index.methods.resize(5);
  const std::vector<std::uint32_t> offsets{kCaller, kParent, kLeft, kRight, kWriter};
  const std::vector<abc::CodeInfo> bodies{caller, parent, left, right, writer};
  const std::vector<std::string> names{"caller", "make", "readLeft", "readRight", "writeBoth"};
  for (std::size_t i = 0; i < index.methods.size(); ++i) {
    auto& method = index.methods[i]; method.offset = {offsets[i]}; method.code = bodies[i];
    method.owner_offset = {kRecord}; method.name = names[i]; method.name_utf16_length = names[i].size();
    method.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  }
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  const FamilyDestination destination{Strictness::kStrict, nullptr};
  // The largest fixture has 1,207 members. Use an explicit, bounded allowance
  // for its cumulative preparation charge; production defaults stay separate.
  constexpr std::size_t kFamilyPreparationBytes = 128 * 1024 * 1024;
  auto prepared = PreparedFamily::PrepareCreated(*file, header, index, lookup, roles, sites,
      sites.Find({kParent}).first, identity, destination, {kFamilyPreparationBytes});
  if (!std::holds_alternative<PreparedFamily>(prepared)) test::Check(unlink(path) == 0);
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  const auto& family = std::get<PreparedFamily>(prepared);
  test::Check(family.Family().members.size() == 1 + 3 * count && family.Family().writes.has_value());
  for (const auto& member : family.Family().members) test::Check(member.tree.recovery_issues.empty());
  if (count == 1) {
    const FunctionContext owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        *file, header, index, lookup, roles, sites, sites.Find({kParent}).first, identity))};
    for (const auto fault : {BindingFault::kOrder, BindingFault::kDuplicate, BindingFault::kSelfOwner}) {
      auto inputs = std::get<FileFamily>(ReadFunctionFamily(*file, header, index, lookup, roles, sites,
          owner.Position(), identity, {}, nullptr, &owner));
      auto& member = inputs.members[0]; auto& bindings = member.children;
      test::Check(bindings.size() == 3);
      switch (fault) {
        case BindingFault::kOrder: std::swap(bindings[0], bindings[1]); break;
        case BindingFault::kDuplicate: bindings[1].creation = bindings[0].creation; break;
        case BindingFault::kSelfOwner: bindings[1].child = inputs.Owner(FileFamily::kRoot); break;
      }
      // The family entry point deliberately rebinds its inputs. Exercise the
      // actual evaluation/AST boundary rather than expecting that repair to fail.
      const auto& graph = member.function.Graph();
      const auto demands = member.function.Cells() ? std::span<const CellDemand>(member.function.Cells()->demands) :
          std::span<const CellDemand>{};
      const auto evaluation = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, bindings, {}, demands, {},
          analysis::ClosureWrites::kLanguage);
      if (fault != BindingFault::kSelfOwner) {
        test::Check(std::holds_alternative<EvaluationError>(evaluation)); continue;
      }
      test::Check(std::holds_alternative<EvaluationPlan>(evaluation));
      std::vector<ExpressionConstant> constants(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i)
        if (const auto* field = std::get_if<analysis::ValueOwnWrite>(&graph.ops[i].operation))
          constants[i] = NameId{field->name.value - 4};
      const auto forest = BuildExpressionForest(graph, std::get<EvaluationPlan>(evaluation), 3,
          inputs.Owner(FileFamily::kRoot), {constants, 2});
      test::Check(std::get<ExpressionError>(forest) == ExpressionError::kOwnership);
    }
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(family.PrintDraft(destination, output)));
  if (print) std::cout << output.str();
  test::Check(unlink(path) == 0);
  return family.Work();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    test::Check(std::string_view{argv[1]} == "--source");
    Run(abc::ProfileId::kDynamic1206, 2, true); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, count, false);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
