#include "abcd/abc/sendable_fields.h"
#include <cstdlib>
#include <iostream>
using namespace abcd::abc;
static void Check(bool value) { if (!value) std::abort(); }
static LiteralEntry Bits(std::uint64_t n) { return {LiteralTag::kInteger, LiteralBits{n}}; }
static LiteralEntry Key(std::uint32_t n = 100) { return {LiteralTag::kString, EntityOffset{n}}; }
int main() {
  const auto fields = [](std::size_t n) {
    LiteralArray a;
    for (std::size_t i = 0; i < n; ++i) {
      a.entries.push_back(Key()); a.entries.push_back(Bits(i % 256));
    }
    a.entries.push_back(Bits(n / 2)); return a;
  };
  for (auto n : {0u, 1u, 200u, 201u, 400u, 800u}) {
    const auto a = fields(n);
    const auto decoded = DecodeSendableFields(a);
    Check(std::holds_alternative<SendableFields>(decoded));
    const auto& f = std::get<SendableFields>(decoded);
    Check(f.fields.size() == n && f.instance_count == n / 2);
    Check(f.allocated_bytes >= sizeof(SendableFields) + n * sizeof(SendableField));
    Check(f.work >= static_cast<std::uint64_t>(n) + 1);
    for (std::size_t i = 0; i < n; ++i) {
      Check(f.fields[i].type.bits == i % 256);
      Check(f.fields[i].placement == (i < n / 2 ? SendableFieldPlacement::kInstance : SendableFieldPlacement::kStatic));
    }
    const auto bytes = f.allocated_bytes;
    const auto work = f.work;
    Check(std::holds_alternative<SendableFields>(DecodeSendableFields(a, {bytes, work})));
    if (bytes) Check(std::get<SendableFieldFailure>(DecodeSendableFields(a, {bytes - 1, work})).kind == SendableFieldError::kResourceLimit);
    if (work) Check(std::get<SendableFieldFailure>(DecodeSendableFields(a, {bytes, work - 1})).kind == SendableFieldError::kWorkLimit);
  }
  auto a = fields(2);
  a.entries[1] = Bits(256);
  auto bad = std::get<SendableFieldFailure>(DecodeSendableFields(a));
  Check(bad.kind == SendableFieldError::kMask && bad.field == 0);
  a = fields(2); a.entries[0] = Bits(1);
  Check(std::get<SendableFieldFailure>(DecodeSendableFields(a)).kind == SendableFieldError::kKey);
  a = fields(2); a.entries.back() = Bits(3);
  Check(std::get<SendableFieldFailure>(DecodeSendableFields(a)).kind == SendableFieldError::kSplit);
  a = fields(2); a.entries.pop_back();
  Check(std::get<SendableFieldFailure>(DecodeSendableFields(a)).kind == SendableFieldError::kShape);
  a = fields(2); a.entries[1] = {LiteralTag::kDouble, LiteralBits{1}};
  Check(std::get<SendableFieldFailure>(DecodeSendableFields(a)).kind == SendableFieldError::kMask);
  LiteralArray members{{Bits(0), {LiteralTag::kLiteralArray, EntityOffset{80}}}, {}};
  Check(std::get<ClassBufferError>(DecodeClassBuffer(members)) == ClassBufferError::kShape);
  auto sendable = std::get<ClassBuffer>(DecodeClassLiteral(members, ClassKind::kSendable));
  Check(sendable.kind == ClassKind::kSendable && sendable.field_types == EntityOffset{80});
  Check(sendable.properties.empty());
  members.entries.back().tag = LiteralTag::kInteger;
  Check(std::holds_alternative<ClassBufferError>(DecodeClassLiteral(members, ClassKind::kSendable)));
  std::cout << "sendable fields: typed layout, ordered masks/split, mutations, 200/201/400/800 and exact budgets\n";
}
