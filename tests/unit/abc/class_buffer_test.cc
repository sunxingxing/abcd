#include "abcd/abc/class_buffer.h"

#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd::abc;
  const auto ref = [](LiteralTag tag, std::uint32_t offset) {
    return LiteralEntry{tag, EntityOffset{offset}};
  };
  const auto bits = [](LiteralTag tag, std::uint64_t value) {
    return LiteralEntry{tag, LiteralBits{value}};
  };
  LiteralArray array;
  array.entries = {ref(LiteralTag::kString, 60),
                   ref(LiteralTag::kMethod, 100),
                   bits(LiteralTag::kMethodAffiliate, 2),
                   ref(LiteralTag::kString, 60),
                   ref(LiteralTag::kGetter, 101),
                   bits(LiteralTag::kMethodAffiliate, 0),
                   bits(LiteralTag::kInteger, 1)};
  const auto layout = std::get<ClassBuffer>(DecodeClassBuffer(array));
  Check(layout.properties.size() == 2);
  Check(layout.properties[0].placement == ClassPlacement::kPrototype);
  Check(layout.properties[1].placement == ClassPlacement::kStatic);
  const auto& method = std::get<ClassMethod>(layout.properties[0].value);
  Check(method.target.value == 100 && method.length == 2 &&
        method.kind == ClassMethodKind::kOrdinary);
  Check(std::get<ClassMethod>(layout.properties[1].value).kind ==
        ClassMethodKind::kGetter);
  Check(std::get<ClassBuffer>(
            DecodeClassBuffer({{bits(LiteralTag::kInteger, 0)}, {}}))
            .properties.empty());
  Check(std::get<ClassBufferError>(DecodeClassBuffer({})) ==
        ClassBufferError::kShape);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(array, {0})) ==
        ClassBufferError::kResourceLimit);
  const auto required = (array.entries.size() - 1) / 2 * sizeof(ClassProperty);
  Check(std::holds_alternative<ClassBuffer>(
      DecodeClassBuffer(array, {required})));
  Check(std::get<ClassBufferError>(DecodeClassBuffer(array, {required - 1})) ==
        ClassBufferError::kResourceLimit);
  auto bad = array;
  bad.entries.back() = bits(LiteralTag::kInteger, 3);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(bad)) ==
        ClassBufferError::kSplit);
  bad = array;
  bad.entries[2] = bits(LiteralTag::kInteger, 2);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(bad)) ==
        ClassBufferError::kAffiliate);
  bad = array;
  bad.entries[2] = bits(LiteralTag::kMethodAffiliate, UINT16_MAX + 1ULL);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(bad)) ==
        ClassBufferError::kAffiliate);
  bad = array;
  bad.entries[0] = bits(LiteralTag::kBoolean, 1);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(bad)) ==
        ClassBufferError::kKey);
  bad = array;
  bad.entries[1] = bits(LiteralTag::kMethod, 100);
  Check(std::get<ClassBufferError>(DecodeClassBuffer(bad)) ==
        ClassBufferError::kValue);
  for (const auto value :
       {bits(LiteralTag::kBoolean, 1), bits(LiteralTag::kInteger, 42),
        bits(LiteralTag::kDouble, 0x7ff8000000000000ULL),
        bits(LiteralTag::kAccessor, 0), bits(LiteralTag::kNull, 0),
        ref(LiteralTag::kString, 60)}) {
    LiteralArray scalars{
        {bits(LiteralTag::kInteger, 42), value, bits(LiteralTag::kInteger, 0)},
        {}};
    const auto decoded = std::get<ClassBuffer>(DecodeClassBuffer(scalars));
    Check(decoded.properties[0].placement == ClassPlacement::kStatic);
    Check(std::get<LiteralEntry>(decoded.properties[0].value).tag == value.tag);
  }
  for (const auto tag :
       {LiteralTag::kGeneratorMethod, LiteralTag::kAsyncGeneratorMethod,
        LiteralTag::kSetter}) {
    auto kinds = array;
    kinds.entries[1].tag = tag;
    Check(std::holds_alternative<ClassBuffer>(DecodeClassBuffer(kinds)));
  }
  for (const std::size_t count : {200U, 201U, 8192U}) {
    LiteralArray many;
    for (std::size_t i = 0; i < count; ++i) {
      many.entries.push_back(ref(LiteralTag::kString, 60));
      many.entries.push_back(bits(LiteralTag::kNull, 0));
    }
    many.entries.push_back(bits(LiteralTag::kInteger, count));
    Check(std::get<ClassBuffer>(DecodeClassBuffer(many)).properties.size() ==
          count);
  }
}
