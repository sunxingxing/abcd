#include "abcd/source/base_constructor.h"

#include <unistd.h>

#include <array>
#include <iostream>
#include <sstream>
#include <vector>

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
source::BaseConstructor Constructor() {
  return {{{}, {}, 1, {}},
          {abc::FunctionKind::kBaseConstructor, false, 0, {}},
          std::get<abc::FrameLayout>(
              abc::BuildFrameLayout(0, 4, {true, true, true, true})),
          "Recovered"};
}
void Emit(std::string_view name, const abc::MethodBody& body,
          std::uint16_t length = 1) {
  auto constructor = Constructor();
  constructor.creation.length = length;
  std::cout << "export const " << name << " = class Recovered {\n";
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteBaseConstructor(constructor, body, std::cout)));
  std::cout << "\n};\n";
}
void EmitAssignments() {
  std::vector<std::uint8_t> bytes(256);
  constexpr std::uint32_t kName = 128, kPool = 64;
  for (std::size_t i = 0; i < sizeof(kName); ++i)
    bytes[kPool + i] = kName >> (8 * i);
  bytes[kName] = 3;
  bytes[kName + 1] = 'x';
  char path[] = "/tmp/abcd-constructor-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 256, {}, {1, kPool}, {}, {}});
  const source::MethodConstants constants{file, header, index, {210}, {}};
  const auto body =
      test::Body({0x60, 3, 0x43, 0, 0, 0, 2, 0x00, 0x43, 0, 0, 0, 2, 0x65});
  std::ostringstream text;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseConstructor(
      Constructor(), body, text, {}, &constants)));
  test::Check(text.str().find("this[\"x\"] = $acc;") != std::string::npos);
  std::cout << "export const assigned = class Recovered {\n"
            << text.str() << "\n};\n";
  const auto emits_with = [&](std::size_t bytes,
                              std::ostringstream& output) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    return std::holds_alternative<std::size_t>(source::WriteBaseConstructor(
        Constructor(), body, output, budget, &constants));
  };
  std::size_t low = 0;
  std::size_t high = source::LocalLexicalBudget{}.bytes;
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    std::ostringstream candidate;
    if (emits_with(middle, candidate))
      high = middle;
    else
      low = middle;
  }
  std::ostringstream tight;
  test::Check(emits_with(high, tight));
  test::Check(tight.str().find("$object[\"x\"]") != std::string::npos);
  std::ostringstream below;
  test::Check(!emits_with(low, below) && below.str().empty());
  const auto overwritten = test::Body(
      {0x44, 0x32, 0x60, 3, 0x43, 0, 0, 0, 2, 0x65});
  std::ostringstream indirect;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBaseConstructor(
      Constructor(), overwritten, indirect, {}, &constants)));
  test::Check(indirect.str().find("this[\"x\"]") == std::string::npos);
  test::Check(indirect.str().find("$object[\"x\"]") != std::string::npos);
  constexpr std::array<std::size_t, 3> kStoreCounts{200, 201, 402};
  std::array<std::size_t, kStoreCounts.size()> output_sizes{};
  for (std::size_t run = 0; run < kStoreCounts.size(); ++run) {
    std::vector<std::uint8_t> code{0x60, 2};
    for (std::size_t i = 0; i < kStoreCounts[run]; ++i)
      code.insert(code.end(), {0x43, 0, 0, 0, 2});
    code.push_back(0x65);
    std::ostringstream repeated;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteBaseConstructor(Constructor(), test::Body(code),
                                     repeated, {}, &constants)));
    const auto emitted = repeated.str();
    output_sizes[run] = emitted.size();
    constexpr std::string_view kDirectStore = "this[\"x\"] = $acc;";
    std::size_t found = 0;
    for (std::size_t at = emitted.find(kDirectStore);
         at != std::string::npos;
         at = emitted.find(kDirectStore, at + kDirectStore.size()))
      ++found;
    test::Check(found == kStoreCounts[run]);
  }
  test::Check(output_sizes[2] - output_sizes[0] ==
              (kStoreCounts[2] - kStoreCounts[0]) *
                  (output_sizes[1] - output_sizes[0]));
  unlink(path);
}
}  // namespace
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    Emit("parameter", test::Body({0x60, 3, 0x64}));
    Emit("lengthMarker", test::Body({0x60, 3, 0x64}), 0);
    Emit("self", test::Body({0x60, 0, 0x64}));
    Emit("intrinsicSelf", test::Body({0xaf, 0x64}));
    Emit("newTarget", test::Body({0x60, 1, 0x64}));
    Emit("receiver", test::Body({0x60, 2, 0x64}));
    Emit("thrown", test::Body({0x60, 3, 0xfe, 0}));
    EmitAssignments();
    return 0;
  }
  std::ostringstream out;
  const auto body = test::Body({0x60, 3, 0x64});
  const auto result = source::WriteBaseConstructor(Constructor(), body, out);
  test::Check(std::get<std::size_t>(result) == out.str().size());
  test::Check(out.str().starts_with("constructor($p0) {"));
  auto wrong = Constructor();
  wrong.creation.length = 0;
  std::ostringstream marked;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteBaseConstructor(wrong, body, marked)));
  test::Check(marked.str().starts_with("constructor($p0 = void 0)"));
  wrong.creation.length = 2;
  std::ostringstream rejected;
  test::Check(std::get<source::FunctionEmitError>(
                  source::WriteBaseConstructor(wrong, body, rejected)) ==
              source::FunctionEmitError::kLength);
  test::Check(rejected.str().empty());
  const auto reject = [&](source::BaseConstructor candidate,
                          source::FunctionEmitError expected) {
    std::ostringstream text;
    test::Check(std::get<source::FunctionEmitError>(
                    source::WriteBaseConstructor(candidate, body, text)) ==
                expected);
    test::Check(text.str().empty());
  };
  wrong = Constructor();
  wrong.attributes.sendable = true;
  reject(wrong, source::FunctionEmitError::kSendable);
  wrong = Constructor();
  wrong.class_binding = "$acc";
  reject(wrong, source::FunctionEmitError::kName);
  wrong = Constructor();
  wrong.frame.this_value = abc::RegisterId{0};
  reject(wrong, source::FunctionEmitError::kFrame);
  for (const auto& unsupported :
       {test::Body({0x70, 0x64}), test::Body({0x3c, 0, 0x64})}) {
    std::ostringstream text;
    test::Check(!std::holds_alternative<std::size_t>(
        source::WriteBaseConstructor(Constructor(), unsupported, text)));
    test::Check(text.str().empty());
  }
  auto large = Constructor();
  constexpr std::uint16_t kWideLength = 256;
  large.creation.length = kWideLength;
  large.frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, kWideLength + 3, {true, true, true, true}));
  std::ostringstream wide;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteBaseConstructor(large, test::Body({0x65}), wide)));
  test::Check(wide.str().find("$p255") != std::string::npos);
  for (const auto count : {out.str().size() - 1, out.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = count;
    std::ostringstream limited;
    const auto emitted =
        source::WriteBaseConstructor(Constructor(), body, limited, budget);
    test::Check(std::holds_alternative<std::size_t>(emitted) ==
                (count == out.str().size()));
    test::Check(limited.str() == (count == out.str().size() ? out.str() : ""));
  }
  // Locate the admitted scratch boundary without coupling to allocator sizes.
  const auto fits = [&](std::size_t bytes) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    std::ostringstream text;
    const auto emitted =
        source::WriteBaseConstructor(Constructor(), body, text, budget);
    const bool accepted = std::holds_alternative<std::size_t>(emitted);
    test::Check(text.str() == (accepted ? out.str() : ""));
    return accepted;
  };
  std::size_t low = 0;
  std::size_t high = source::LocalLexicalBudget{}.bytes;
  test::Check(!fits(low) && fits(high));
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    if (fits(middle))
      high = middle;
    else
      low = middle;
  }
  test::Check(fits(high) && !fits(high - 1));
}
