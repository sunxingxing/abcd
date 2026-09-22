#include <unistd.h>

#include <cstdlib>

#include "abcd/analysis/method_captures.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  constexpr std::uint32_t kPool = 100;
  constexpr std::uint32_t kRoot = 200;
  constexpr std::uint32_t kMiddle = 300;
  constexpr std::uint32_t kLeaf = 400;
  const std::uint32_t targets[]{kMiddle, kLeaf};
  std::vector<std::uint8_t> bytes(512);
  for (std::size_t t = 0; t < std::size(targets); ++t)
    for (unsigned b = 0; b < sizeof(targets[t]); ++b)
      bytes[kPool + t * sizeof(targets[t]) + b] =
          static_cast<std::uint8_t>(targets[t] >> (8 * b));
  char path[] = "/tmp/abcd-capture-demand-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint =
      std::get<corpus::FileFingerprint>(file.Fingerprint());
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex metadata;
  metadata.regions.push_back({kRoot, 500, {}, {2, kPool}, {}, {}});
  metadata.methods.resize(3);
  metadata.methods[0].offset = {kRoot};
  metadata.methods[1].offset = {kMiddle};
  metadata.methods[2].offset = {kLeaf};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(metadata.methods));
  const auto build = [&](abc::MethodPosition method,
                         std::span<const std::uint8_t> code) {
    abc::MethodBody body;
    for (std::size_t pc = 0; pc < code.size();) {
      const auto decoded = std::get<abc::DecodedInstruction>(
          abc::DecodeInstruction(code.subspan(pc), header.profile));
      body.instructions.push_back(
          {{static_cast<std::uint32_t>(pc)}, decoded, {}});
      pc += decoded.size;
    }
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    auto flow = std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
    return std::get<MethodCaptures>(MethodCaptures::Build(
        file, header, metadata, lookup,
        {fingerprint.sha256, header.profile, method}, body, std::move(flow)));
  };
  // Root creates middle both inside and outside its local lexical allocation.
  // Middle never reads the slot: only leaf has a direct captured requirement.
  const std::uint8_t root_code[]{0x09, 1,    0x33, 0, 0, 0, 0,
                                 0x69, 0x33, 0,    0, 0, 0, 0x65};
  const std::uint8_t middle_code[]{0x33, 0, 1, 0, 0, 0x65};
  const std::uint8_t leaf_code[]{0x3c, 0, 0x65};
  const auto root = build({0}, root_code);
  const auto middle = build({1}, middle_code);
  const auto leaf = build({2}, leaf_code);
  Check(middle.Captures().Uses().empty());
  const auto leaf_slot = leaf.Captures().Uses().front().captured;
  const auto inherited =
      std::get<LexicalBinding>(middle.Resolve({0}, leaf, leaf_slot));
  const CapturedSlot demand{std::get<CapturedDepth>(inherited.environment),
                            inherited.slot};
  Check(std::get<MethodCaptureError>(root.Resolve({2}, middle, demand)) ==
        MethodCaptureError::kCaptureMissing);
  Check(std::get<LexicalBinding>(root.Rebase({2}, middle.Origin(), demand))
            .environment == EnvironmentRef{EnvironmentId{0}});
  Check(std::get<LexicalBinding>(root.Rebase({8}, middle.Origin(), demand))
            .environment == EnvironmentRef{CapturedDepth{0}});
  Check(std::get<EnvironmentError>(root.Rebase(
            {2}, middle.Origin(), {{0}, {1}})) == EnvironmentError::kSlotRange);
  Check(
      std::get<MethodCaptureError>(root.Rebase({0}, middle.Origin(), demand)) ==
      MethodCaptureError::kCreationMissing);
  Check(std::get<MethodCaptureError>(root.Rebase({2}, leaf.Origin(), demand)) ==
        MethodCaptureError::kTargetMismatch);
  auto different = middle.Origin();
  different.file.bytes[0] ^= 1;
  Check(std::get<MethodCaptureError>(root.Rebase({2}, different, demand)) ==
        MethodCaptureError::kContextMismatch);
  different = middle.Origin();
  different.profile = abc::ProfileId::kDynamic1102;
  Check(std::get<MethodCaptureError>(root.Rebase({2}, different, demand)) ==
        MethodCaptureError::kContextMismatch);
  // Passing an absent demand does not hide earlier creation/target errors.
  const CapturedSlot absent{{0}, {1}};
  Check(std::get<MethodCaptureError>(root.Resolve({0}, middle, absent)) ==
        MethodCaptureError::kCreationMissing);
  Check(std::get<MethodCaptureError>(root.Resolve({2}, leaf, absent)) ==
        MethodCaptureError::kTargetMismatch);
  Check(std::get<MethodCaptureError>(root.Resolve({2}, middle, absent)) ==
        MethodCaptureError::kCaptureMissing);
  // A middle-frame pop shifts the outward demand even without a middle read.
  const std::uint8_t popped_code[]{0x69, 0x33, 0, 1, 0, 0, 0x65};
  const auto popped = build({1}, popped_code);
  const auto shifted =
      std::get<LexicalBinding>(popped.Rebase({1}, leaf.Origin(), leaf_slot));
  Check(shifted.environment == EnvironmentRef{CapturedDepth{1}});
  Check(std::get<LexicalBinding>(
            root.Rebase(
                {2}, popped.Origin(),
                {std::get<CapturedDepth>(shifted.environment), shifted.slot}))
            .environment == EnvironmentRef{CapturedDepth{0}});
  Check(std::get<EnvironmentError>(
            popped.Rebase({1}, leaf.Origin(), {{UINT32_MAX}, {0}})) ==
        EnvironmentError::kDepthRange);
  const std::uint8_t pushed_code[]{0x09, 1, 0x33, 0, 1, 0, 0, 0x65};
  const auto pushed = build({1}, pushed_code);
  Check(std::get<LexicalBinding>(pushed.Rebase({2}, leaf.Origin(), leaf_slot))
            .environment == EnvironmentRef{EnvironmentId{0}});
  for (const std::uint16_t count : {200, 201, 4096, 65535}) {
    const std::uint8_t wide_code[]{0xfd,
                                   2,
                                   static_cast<std::uint8_t>(count),
                                   static_cast<std::uint8_t>(count >> 8),
                                   0x33,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0x65};
    const auto wide = build({0}, wide_code);
    const auto payload = wide.AllocatedBytes();
    for (std::uint16_t slot = 0; slot < count; ++slot) {
      const auto bound = std::get<LexicalBinding>(
          wide.Rebase({4}, middle.Origin(), {{0}, {slot}}));
      Check(bound.environment == EnvironmentRef{EnvironmentId{0}} &&
            bound.slot.value == slot);
    }
    Check(wide.AllocatedBytes() == payload);
    Check(std::get<EnvironmentError>(
              wide.Rebase({4}, middle.Origin(), {{0}, {count}})) ==
          EnvironmentError::kSlotRange);
  }
  constexpr std::uint32_t kSendableFlag = 8U << 8;
  metadata.methods[1].access_flags = kSendableFlag;
  const auto sendable = build({0}, root_code);
  Check(std::get<CaptureRebaseError>(
            sendable.Rebase({2}, middle.Origin(), demand)) ==
        CaptureRebaseError::kSendableEnvironment);
  Check(std::get<MethodCaptureError>(sendable.Resolve({2}, middle, demand)) ==
        MethodCaptureError::kCaptureMissing);
  close(fd);
  unlink(path);
}
