#include "abcd/abc/pool_reference.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <variant>
#include <vector>

#include "abcd/abc/method_references.h"
#include "abcd/abc/method_target.h"
#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Put32(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
}  // namespace

int main() {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(512);
  Header header;
  header.kind = AbcKind::kDynamic;
  header.profile = ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.foreign_offset = 410;
  header.foreign_size = 10;
  header.layout = DynamicLayout{{}, {}, {}, {2, 64}};
  // Same pool slots in adjacent regions have different targets. The pool
  // contains heterogeneous entities, not just methods.
  for (std::uint32_t region = 0; region < 2; ++region) {
    const auto base = 64 + region * 40;
    Put32(bytes, base, 200 + region * 100);
    Put32(bytes, base + 4, 300 + region * 100);
    Put32(bytes, base + 16, 4);
    Put32(bytes, base + 20, 144 + region * 16);
    for (std::uint32_t slot = 0; slot < 3; ++slot)
      Put32(bytes, 144 + region * 16 + slot * 4, 400 + region * 30 + slot * 10);
    Put32(bytes, 144 + region * 16 + 12, region == 0 ? 0 : bytes.size());
  }
  char path[] = "/tmp/abcd-pool-reference-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  auto opened = abcd::corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened);
  auto built = BuildMetadataIndex(file, header);
  Check(std::holds_alternative<MetadataIndex>(built));
  auto& index = std::get<MetadataIndex>(built);
  Check(index.regions.size() == 2);
  MethodBody body;
  BodyInstruction instruction;
  instruction.decoded.operand_count = 3;
  instruction.decoded.operands[0].kind = OperandKind::kMethod;
  instruction.decoded.operands[1].kind = OperandKind::kString;
  instruction.decoded.operands[1].raw_bits = 1;
  instruction.decoded.operands[2].kind = OperandKind::kLiteralArray;
  instruction.decoded.operands[2].raw_bits = 2;
  body.instructions.push_back(instruction);
  instruction.offset.value = 10;
  body.instructions.push_back(instruction);
  auto references = ResolveMethodReferences(file, header, index, {200}, body);
  Check(std::holds_alternative<MethodReferences>(references));
  const auto& resolved = std::get<MethodReferences>(references);
  Check(resolved.references.size() == 3);
  Check(resolved.operand_count == 6);
  for (std::size_t i = 0; i < resolved.references.size(); ++i) {
    Check(resolved.references[i].slot.value == i);
    Check(resolved.references[i].target.offset.value == 400 + i * 10);
    Check(resolved.references[i].first_use.value == 0);
  }
  Check(std::get<ReferenceFailure>(
            ResolveMethodReferences(file, header, index, {200}, body, {0}))
            .kind == ReferenceError::kResourceLimit);
  body.instructions.back().decoded.operands[2].raw_bits = UINT64_MAX;
  const auto overflow = std::get<ReferenceFailure>(
      ResolveMethodReferences(file, header, index, {200}, body));
  Check(overflow.kind == ReferenceError::kIndexOverflow);
  Check(overflow.pc.value == 10);
  body.instructions.back().decoded.operands[2].raw_bits = 3;
  const auto invalid = std::get<ReferenceFailure>(
      ResolveMethodReferences(file, header, index, {200}, body));
  Check(invalid.kind == ReferenceError::kResolution);
  Check(invalid.metadata_error == MetadataError::kReference);
  Check(invalid.pc.value == 10);
  for (const std::size_t length : {200U, 201U, 1024U, 8192U}) {
    body.instructions.assign(length, instruction);
    const auto many = std::get<MethodReferences>(
        ResolveMethodReferences(file, header, index, {200}, body));
    Check(many.references.size() == 3);
    Check(many.operand_count == length * 3);
  }
  body.instructions.assign(1, instruction);
  for (auto& operand : body.instructions.front().decoded.operands)
    operand.raw_bits = 0;
  const auto mixed = std::get<MethodReferences>(
      ResolveMethodReferences(file, header, index, {200}, body));
  Check(mixed.references.size() == 3);  // Same slot, different semantic roles.
  body.instructions.front().decoded.operand_count = kMaxInstructionOperands + 1;
  Check(std::get<ReferenceFailure>(
            ResolveMethodReferences(file, header, index, {200}, body))
            .kind == ReferenceError::kMalformedOperands);
  body.instructions.assign(1, instruction);
  body.instructions.front().decoded.operands[0].kind = OperandKind::kField;
  Check(std::get<ReferenceFailure>(
            ResolveMethodReferences(file, header, index, {200}, body))
            .kind == ReferenceError::kUnsupportedOperand);
  body.instructions.clear();
  Check(std::get<MethodReferences>(
            ResolveMethodReferences(file, header, index, {200}, body, {0}))
            .references.empty());
  const std::array kinds{PoolEntityKind::kMethod, PoolEntityKind::kString,
                         PoolEntityKind::kLiteralArray};
  for (std::uint32_t region = 0; region < 2; ++region) {
    for (std::uint32_t slot = 0; slot < kinds.size(); ++slot) {
      const auto result = ResolvePoolReference(
          file, header, index, {200 + region * 100}, {slot}, kinds[slot]);
      Check(std::holds_alternative<PoolReference>(result));
      const auto ref = std::get<PoolReference>(result);
      Check(ref.kind == kinds[slot]);
      Check(ref.offset.value == 400 + region * 30 + slot * 10);
    }
    Check(std::get<MetadataError>(ResolvePoolReference(
              file, header, index, {200 + region * 100}, {3},
              PoolEntityKind::kString)) == MetadataError::kReference);
  }
  for (const auto pc : {199U, 400U}) {
    const auto result = ResolvePoolReference(file, header, index, {pc}, {0},
                                             PoolEntityKind::kString);
    Check(std::get<MetadataError>(result) == MetadataError::kRegionRange);
  }
  for (const auto slot : {4U, UINT32_MAX}) {
    const auto result = ResolvePoolReference(file, header, index, {200}, {slot},
                                             PoolEntityKind::kString);
    Check(std::get<MetadataError>(result) == MetadataError::kIndexRange);
  }
  Header unsupported = header;
  unsupported.profile = ProfileId::kStatic0107;
  Check(std::get<MetadataError>(ResolvePoolReference(
            file, unsupported, index, {200}, {0}, PoolEntityKind::kString)) ==
        MetadataError::kUnsupportedProfile);
  // Supply method membership separately from the heterogeneous pool fixture.
  // Offset 410 is foreign; 420 is in-file but not a local method either.
  for (const auto offset : {430U, 200U, 400U, 300U}) {
    MethodInfo method;
    method.offset = {offset};
    index.methods.push_back(std::move(method));
  }
  // Pinned method access layout: low access byte, kind/sendable byte, region.
  constexpr std::uint32_t kAsyncSendableFlags = (4U | 8U) << 8;
  constexpr std::uint32_t kAccess = 0x21;
  constexpr std::uint32_t kRegion = 201;
  index.methods[0].access_flags =
      (kRegion << 16) | kAsyncSendableFlags | kAccess;
  const auto lookup =
      std::get<MethodLookup>(MethodLookup::Build(index.methods));
  const auto first = std::get<MethodTarget>(
      ResolveMethodTarget(file, header, index, lookup, {200}, {0}));
  Check(first.position.value == 2 &&
        first.attributes.kind == FunctionKind::kNone);
  const auto second = std::get<MethodTarget>(
      ResolveMethodTarget(file, header, index, lookup, {300}, {0}));
  Check(second.position.value == 0 &&
        second.attributes.kind == FunctionKind::kAsync);
  Check(second.attributes.sendable && second.attributes.access == kAccess &&
        second.attributes.pool_region.value == kRegion);
  Check(std::get<MethodTargetError>(
            ResolveMethodTarget(file, header, index, lookup, {200}, {1})) ==
        MethodTargetError::kTargetMissing);
  Check(std::get<MethodTargetError>(
            ResolveMethodTarget(file, header, index, lookup, {200}, {2})) ==
        MethodTargetError::kTargetMissing);
  Check(std::get<MethodTargetError>(
            ResolveMethodTarget(file, header, index, lookup, {201}, {0})) ==
        MethodTargetError::kCallerMissing);
  Check(std::get<MetadataError>(
            ResolveMethodTarget(file, header, index, lookup, {200}, {4})) ==
        MetadataError::kIndexRange);
  index.methods[0].access_flags = 0xffU << 8;
  Check(std::get<MethodAttributeError>(
            ResolveMethodTarget(file, header, index, lookup, {300}, {0})) ==
        MethodAttributeError::kUnknownKind);
  index.methods[0].offset = {431};
  Check(std::get<MethodTargetError>(
            ResolveMethodTarget(file, header, index, lookup, {300}, {0})) ==
        MethodTargetError::kIndexMismatch);
  // A size change is deterministic even on filesystems whose timestamps
  // coalesce two writes within the same clock tick.
  const std::uint8_t changed = 1;
  Check(pwrite(fd, &changed, 1, bytes.size()) == 1);
  Check(std::get<MetadataError>(ResolvePoolReference(
            file, header, index, {200}, {0}, PoolEntityKind::kString)) ==
        MetadataError::kFileChanged);
  close(fd);
  unlink(path);
}
