#include "abcd/analysis/capture_propagation.h"

#include <unistd.h>

#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  constexpr std::uint32_t kPool = 100;
  const std::uint32_t targets[]{300, 400};
  std::vector<std::uint8_t> bytes(512);
  for (std::size_t t = 0; t < std::size(targets); ++t)
    for (unsigned b = 0; b < sizeof(targets[t]); ++b)
      bytes[kPool + t * sizeof(targets[t]) + b] =
          static_cast<std::uint8_t>(targets[t] >> (8 * b));
  char path[] = "/tmp/abcd-capture-propagation-XXXXXX";
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
  metadata.regions.push_back({200, 500, {}, {2, kPool}, {}, {}});
  metadata.methods.resize(3);
  for (std::size_t i = 0; i < metadata.methods.size(); ++i)
    metadata.methods[i].offset = {static_cast<std::uint32_t>(200 + i * 100)};
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
  const std::uint8_t root_code[]{0x09, 1,    0x33, 0, 0, 0, 0,
                                 0x69, 0x33, 0,    0, 0, 0, 0x65};
  const std::uint8_t middle_code[]{0x33, 0, 1, 0, 0, 0x65};
  const std::uint8_t leaf_code[]{0x3c, 0, 0x65};
  auto root = build({0}, root_code);
  auto middle = build({1}, middle_code);
  auto leaf = build({2}, leaf_code);
  std::vector<CaptureContext> contexts{{&root, {}},
                                       {&middle, CaptureParent{{0}, {2}}},
                                       {&leaf, CaptureParent{{1}, {0}}},
                                       {&middle, CaptureParent{{0}, {8}}},
                                       {&leaf, CaptureParent{{3}, {0}}}};
  const CaptureRequirement seeds[]{
      {{2}, {{0}, {0}}}, {{4}, {{0}, {0}}}, {{2}, {{0}, {0}}}};
  auto result =
      std::get<CapturePropagation>(CapturePropagation::Build(contexts, seeds));
  Check(result.Facts().size() == 5 && result.Transfers() == 4);
  const auto* inner = result.Find(seeds[0]);
  Check(inner != nullptr);
  const auto* inherited =
      result.Find(std::get<CaptureFactId>(inner->destination));
  Check(inherited && inherited->requirement.context.value == 1);
  const auto local = std::get<CaptureLocal>(inherited->destination);
  Check(local.context.value == 0 && local.environment.value == 0 &&
        local.slot.value == 0);
  const auto* boundary = result.Find(CaptureRequirement{{0}, {{0}, {0}}});
  Check(boundary &&
        std::holds_alternative<CaptureBoundary>(boundary->destination));
  Check(result.Find(CaptureRequirement{{1}, {{0}, {1}}}) == nullptr);
  Check(std::holds_alternative<CapturePropagation>(
      CapturePropagation::Build(contexts, seeds, {result.PeakBytes()})));
  Check(std::get<CapturePropagationError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(contexts, seeds,
                                          {result.PeakBytes() - 1}))
                .cause) == CapturePropagationError::kResourceLimit);
  const auto moved = std::move(result);
  Check(result.Contexts().empty() && result.Facts().empty());
  Check(result.Find(seeds[0]) == nullptr && result.PeakBytes() == 0);
  Check(moved.Contexts().size() == contexts.size());
  contexts.push_back({&leaf, CaptureParent{{1}, {0}}});
  const CaptureRequirement siblings[]{{{2}, {{0}, {0}}}, {{5}, {{0}, {0}}}};
  const auto shared = std::get<CapturePropagation>(
      CapturePropagation::Build(contexts, siblings));
  Check(shared.Facts().size() == 3 && shared.Transfers() == 3);
  Check(std::get<CaptureFactId>(shared.Find(siblings[0])->destination) ==
        std::get<CaptureFactId>(shared.Find(siblings[1])->destination));
  // Structural validation is global; semantic edge validation is demand-driven.
  auto invalid = contexts;
  invalid[1].parent = CaptureParent{{1}, {2}};
  Check(std::get<CapturePropagationError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(invalid, {}))
                .cause) == CapturePropagationError::kInvalidParent);
  invalid = contexts;
  invalid[0].method = nullptr;
  Check(std::get<CapturePropagationError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(invalid, {}))
                .cause) == CapturePropagationError::kInvalidContext);
  invalid = contexts;
  invalid[1].parent->creation = {0};
  Check(std::holds_alternative<CapturePropagation>(
      CapturePropagation::Build(invalid, {})));
  Check(std::get<MethodCaptureError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(invalid, seeds))
                .cause) == MethodCaptureError::kCreationMissing);
  const CaptureRequirement missing[]{{{1}, {{0}, {0}}}};
  Check(std::get<CapturePropagationError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(contexts, missing))
                .cause) == CapturePropagationError::kMissingSeed);
  const CaptureRequirement outside[]{{{UINT32_MAX}, {{0}, {0}}}};
  Check(std::get<CapturePropagationError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(contexts, outside))
                .cause) == CapturePropagationError::kInvalidContext);
  const auto empty =
      std::get<CapturePropagation>(CapturePropagation::Build({}, {}, {0}));
  Check(empty.Facts().empty() && empty.Contexts().empty() &&
        empty.AllocatedBytes() == 0 && empty.PeakBytes() == 0);
  const auto registry = std::get<CapturePropagation>(CapturePropagation::Build(
      contexts, {}, {contexts.size() * sizeof(CaptureContextInfo)}));
  Check(registry.Facts().empty() &&
        registry.PeakBytes() == registry.AllocatedBytes());
  const std::uint8_t other_slot_code[]{0x3c, 0x10, 0x65};
  const auto other_slot = build({2}, other_slot_code);
  invalid = contexts;
  invalid[2].method = &other_slot;
  const CaptureRequirement slot_seed[]{{{2}, {{0}, {1}}}};
  const auto slot_failure = std::get<CapturePropagationFailure>(
      CapturePropagation::Build(invalid, slot_seed));
  Check(std::get<EnvironmentError>(slot_failure.cause) ==
            EnvironmentError::kSlotRange &&
        slot_failure.context.value == 1 &&
        slot_failure.captured->slot.value == 1);
  const auto wrong_target = build({1}, leaf_code);
  invalid = contexts;
  invalid[2].method = &wrong_target;
  Check(std::get<MethodCaptureError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(invalid, seeds))
                .cause) == MethodCaptureError::kTargetMismatch);
  constexpr std::uint32_t kSendableFlag = 8U << 8;
  metadata.methods[1].access_flags = kSendableFlag;
  const auto sendable_root = build({0}, root_code);
  metadata.methods[1].access_flags = 0;
  invalid = contexts;
  invalid[0].method = &sendable_root;
  Check(std::get<CaptureRebaseError>(
            std::get<CapturePropagationFailure>(
                CapturePropagation::Build(invalid, seeds))
                .cause) == CaptureRebaseError::kSendableEnvironment);
  auto roots = contexts;
  roots.push_back({&leaf, {}});
  const CaptureRequirement root_seed[]{{{6}, {{0}, {0}}}};
  auto assigned =
      std::get<CapturePropagation>(CapturePropagation::Build(roots, root_seed));
  Check(
      assigned.Facts().size() == 1 && assigned.Transfers() == 0 &&
      std::holds_alternative<CaptureBoundary>(assigned.Facts()[0].destination));
  assigned = std::get<CapturePropagation>(CapturePropagation::Build({}, {}));
  Check(assigned.Contexts().empty() && assigned.Facts().empty());
  // Repeated methods do not imply cycles in a finite supplied context chain.
  // Duplicate seeds must not cause an independent ancestor walk per seed.
  const std::uint8_t recursive_code[]{0x33, 0, 0, 0, 0, 0x3c, 0, 0x65};
  const auto recursive = build({1}, recursive_code);
  for (const std::uint32_t count : {200U, 201U, 4096U, 16384U}) {
    std::vector<CaptureContext> chain(count, {&recursive, {}});
    for (std::uint32_t i = 1; i < count; ++i)
      chain[i].parent = CaptureParent{{i - 1}, {0}};
    const std::vector<CaptureRequirement> repeated(
        count, CaptureRequirement{{count - 1}, {{0}, {0}}});
    const auto propagated = std::get<CapturePropagation>(
        CapturePropagation::Build(chain, repeated));
    Check(propagated.Facts().size() == count &&
          propagated.Transfers() == count - 1);
    Check(propagated.AllocatedBytes() ==
          count * (sizeof(CaptureContextInfo) + sizeof(CaptureFact) +
                   sizeof(CaptureFactId)));
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto* fact = propagated.Find(CaptureRequirement{{i}, {{0}, {0}}});
      Check(fact != nullptr);
      if (i == 0) {
        Check(std::holds_alternative<CaptureBoundary>(fact->destination));
      } else {
        const auto* parent =
            propagated.Find(std::get<CaptureFactId>(fact->destination));
        Check(parent && parent->requirement.context.value == i - 1);
      }
    }
    Check(std::get<CapturePropagationError>(
              std::get<CapturePropagationFailure>(
                  CapturePropagation::Build(chain, repeated,
                                            {propagated.AllocatedBytes()}))
                  .cause) == CapturePropagationError::kResourceLimit);
  }
  // Final facts and the origin/parent registry survive all borrowed inputs.
  contexts.clear();
  invalid.clear();
  roots.clear();
  {
    auto released_root = std::move(root);
    auto released_middle = std::move(middle);
    auto released_leaf = std::move(leaf);
  }
  std::get<std::unique_ptr<corpus::AbcFile>>(opened).reset();
  metadata = {};
  Check(moved.Contexts()[0].origin.file == fingerprint.sha256 &&
        moved.Contexts()[2].parent->context.value == 1);
  Check(moved.Find(seeds[0]) != nullptr);
  Check(moved.Find(CaptureFactId{UINT32_MAX}) == nullptr);
  close(fd);
  unlink(path);
}
