#include "abcd/analysis/creation_binding.h"

#include <unistd.h>

#include <cstdlib>

#include "abcd/analysis/method_captures.h"
#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  std::vector<std::uint8_t> bytes(512);
  constexpr std::uint32_t kPool = 100;
  constexpr std::uint32_t kTarget = 300;
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (8 * i));
  char path[] = "/tmp/abcd-creation-binding-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex metadata;
  metadata.regions.push_back({200, 400, {}, {1, kPool}, {}, {}});
  metadata.methods.resize(2);
  metadata.methods[0].offset = {200};
  metadata.methods[1].offset = {kTarget};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(metadata.methods));
  // newenv; definefunc; pop; definefunc; definemethod; nop; returnundefined.
  const std::uint8_t code[]{0x09, 1, 0x33, 0,    0, 0, 2, 0x69, 0x33, 0,
                            0,    0, 3,    0x34, 0, 0, 0, 4,    0xd5, 0x65};
  abc::MethodBody body;
  for (std::size_t pc = 0; pc < sizeof(code);) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(std::span(code).subspan(pc), header.profile));
    body.instructions.push_back(
        {{static_cast<std::uint32_t>(pc)}, decoded, {}});
    pc += decoded.size;
  }
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  auto flow = std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
  const auto bind = [&](std::size_t instruction) {
    return BindCreation(file, header, metadata, lookup, {200}, body, flow,
                        instruction);
  };
  const auto local = std::get<CreationBinding>(bind(1));
  const auto captured = std::get<CreationBinding>(bind(3));
  Check(local.target.position.value == 1 &&
        captured.target.position.value == 1);
  Check(local.offset.value == 2 && captured.offset.value == 8);
  Check(local.creation.length.value == 2 &&
        captured.creation.length.value == 3);
  Check(std::get<OrdinaryCapture>(local.capture).environment ==
        EnvironmentRef{EnvironmentId{0}});
  Check(std::get<OrdinaryCapture>(captured.capture).environment ==
        EnvironmentRef{CapturedDepth{0}});
  // The same callee entry-chain read maps through each actual creation site.
  // Its local environment IDs must never be reused as caller-local IDs.
  const std::uint8_t target_code[]{0x3c, 0, 0x65};
  abc::MethodBody target_body;
  for (std::size_t pc = 0; pc < sizeof(target_code);) {
    const auto decoded =
        std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
            std::span(target_code).subspan(pc), header.profile));
    target_body.instructions.push_back(
        {{static_cast<std::uint32_t>(pc)}, decoded, {}});
    pc += decoded.size;
  }
  const auto target_graph = std::get<NormalFlow>(BuildNormalFlow(target_body));
  auto target_flow = std::get<EnvironmentFlow>(
      AnalyzeEnvironments(target_body, target_graph, 1));
  Check(target_flow.bindings.size() == 1);
  const auto target_slot = target_flow.bindings[0].binding;
  const CapturedSlot capture_slot{
      std::get<CapturedDepth>(target_slot.environment), target_slot.slot};
  const auto local_use = std::get<LexicalBinding>(
      RebaseCapture(flow.environments, local.capture, capture_slot));
  const auto external_use = std::get<LexicalBinding>(
      RebaseCapture(flow.environments, captured.capture, capture_slot));
  Check(local_use.environment == EnvironmentRef{EnvironmentId{0}});
  Check(external_use.environment == EnvironmentRef{CapturedDepth{0}});
  const auto fingerprint =
      std::get<corpus::FileFingerprint>(file.Fingerprint());
  auto target_snapshot = std::get<MethodCaptures>(MethodCaptures::Build(
      file, header, metadata, lookup, {fingerprint.sha256, header.profile, {1}},
      target_body, std::move(target_flow)));
  const auto capture_only = [&](MethodCaptureBudget budget) {
    auto incoming = std::get<EnvironmentFlow>(
        AnalyzeEnvironments(target_body, target_graph, 1));
    return MethodCaptures::Build(file, header, metadata, lookup,
                                 {fingerprint.sha256, header.profile, {1}},
                                 target_body, std::move(incoming), budget);
  };
  Check(target_snapshot.PeakBytes() > target_snapshot.AllocatedBytes());
  Check(std::holds_alternative<MethodCaptures>(
      capture_only({target_snapshot.PeakBytes()})));
  Check(std::get<CaptureSummaryError>(std::get<MethodCaptureFailure>(
            capture_only({target_snapshot.PeakBytes() - 1}))) ==
        CaptureSummaryError::kResourceLimit);
  const auto snapshot = [&](MethodCaptureBudget budget =
                                MethodCaptureBudget{}) {
    auto incoming =
        std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
    return MethodCaptures::Build(file, header, metadata, lookup,
                                 {fingerprint.sha256, header.profile, {0}},
                                 body, std::move(incoming), budget);
  };
  auto caller_snapshot = std::get<MethodCaptures>(snapshot());
  Check(caller_snapshot.Creations().size() == 3);
  Check(std::get<LexicalBinding>(
            caller_snapshot.Resolve({2}, target_snapshot, capture_slot))
            .environment == EnvironmentRef{EnvironmentId{0}});
  Check(std::get<LexicalBinding>(
            caller_snapshot.Resolve({8}, target_snapshot, capture_slot))
            .environment == EnvironmentRef{CapturedDepth{0}});
  Check(std::get<MethodCaptureError>(
            caller_snapshot.Resolve({0}, target_snapshot, capture_slot)) ==
        MethodCaptureError::kCreationMissing);
  Check(std::get<MethodCaptureError>(
            caller_snapshot.Resolve({2}, caller_snapshot, capture_slot)) ==
        MethodCaptureError::kTargetMismatch);
  Check(std::get<MethodCaptureError>(
            caller_snapshot.Resolve({2}, target_snapshot, {{0}, {1}})) ==
        MethodCaptureError::kCaptureMissing);
  Check(std::holds_alternative<MethodCaptures>(
      snapshot({caller_snapshot.PeakBytes()})));
  Check(std::get<MethodCaptureError>(std::get<MethodCaptureFailure>(
            snapshot({caller_snapshot.PeakBytes() - 1}))) ==
        MethodCaptureError::kResourceLimit);
  auto moved_snapshot = std::move(caller_snapshot);
  Check(caller_snapshot.Creations().empty());
  Check(std::get<MethodCaptureError>(
            caller_snapshot.Resolve({2}, target_snapshot, capture_slot)) ==
        MethodCaptureError::kCreationMissing);
  Check(std::get<LexicalBinding>(
            moved_snapshot.Resolve({2}, target_snapshot, capture_slot))
            .slot.value == 0);
  auto alternate = fingerprint.sha256;
  alternate.bytes[0] ^=
      1;  // Simulate a distinct caller-provided file identity.
  auto another_flow = std::get<EnvironmentFlow>(
      AnalyzeEnvironments(target_body, target_graph, 1));
  const auto another_target = std::get<MethodCaptures>(MethodCaptures::Build(
      file, header, metadata, lookup, {alternate, header.profile, {1}},
      target_body, std::move(another_flow)));
  Check(std::get<MethodCaptureError>(
            moved_snapshot.Resolve({2}, another_target, capture_slot)) ==
        MethodCaptureError::kContextMismatch);
  for (const std::uint32_t count : {200U, 201U, 4096U}) {
    abc::MethodBody many;
    many.instructions.push_back(body.instructions[0]);
    auto site = body.instructions[1];
    auto pc = site.offset.value;
    for (std::uint32_t i = 0; i < count; ++i) {
      site.offset = {pc};
      many.instructions.push_back(site);
      pc += site.decoded.size;
    }
    auto end = body.instructions.back();
    end.offset = {pc};
    many.instructions.push_back(end);
    const auto many_graph = std::get<NormalFlow>(BuildNormalFlow(many));
    auto many_flow =
        std::get<EnvironmentFlow>(AnalyzeEnvironments(many, many_graph, 1));
    const auto ancestry_bytes = many_flow.environments.AllocatedBytes();
    const auto stored = std::get<MethodCaptures>(MethodCaptures::Build(
        file, header, metadata, lookup,
        {fingerprint.sha256, header.profile, {0}}, many, std::move(many_flow)));
    Check(stored.Creations().size() == count);
    Check(stored.AllocatedBytes() ==
          ancestry_bytes + count * sizeof(CreationBinding));
    for (const auto& creation : stored.Creations())
      Check(std::get<LexicalBinding>(
                stored.Resolve(creation.offset, target_snapshot, capture_slot))
                .environment == EnvironmentRef{EnvironmentId{0}});
  }
  // Mutating only attributes leaves lookup positions valid for this fixture.
  metadata.methods[1].access_flags = 8U << 8;
  const auto sendable_snapshot = std::get<MethodCaptures>(snapshot());
  Check(std::get<CaptureRebaseError>(
            sendable_snapshot.Resolve({2}, target_snapshot, capture_slot)) ==
        CaptureRebaseError::kSendableEnvironment);
  Check(std::holds_alternative<SendableCapture>(
      std::get<CreationBinding>(bind(1)).capture));
  const auto method = std::get<CreationBinding>(bind(4));
  Check(method.creation.kind == CreationKind::kMethod);
  Check(method.creation.home_object == HomeObjectInput::kAccumulator);
  Check(std::holds_alternative<OrdinaryCapture>(method.capture));
  metadata.methods[1].access_flags = 0xffU << 8;
  auto failed_flow =
      std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
  const auto failed_build = MethodCaptures::Build(
      file, header, metadata, lookup, {fingerprint.sha256, header.profile, {0}},
      body, std::move(failed_flow));
  Check(std::get<abc::MethodAttributeError>(std::get<MethodCaptureFailure>(
            failed_build)) == abc::MethodAttributeError::kUnknownKind);
  Check(failed_flow.environments.Size() == 1);
  Check(std::get<abc::MethodAttributeError>(bind(1)) ==
        abc::MethodAttributeError::kUnknownKind);
  metadata.methods[1].access_flags = 0;
  const auto absent = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(std::span(metadata.methods).first(1)));
  Check(std::get<abc::MethodTargetError>(BindCreation(
            file, header, metadata, absent, {200}, body, flow, 1)) ==
        abc::MethodTargetError::kTargetMissing);
  Check(std::get<abc::MethodTargetError>(BindCreation(
            file, header, metadata, lookup, {201}, body, flow, 1)) ==
        abc::MethodTargetError::kCallerMissing);
  body.instructions[1].decoded.operands[1].raw_bits = 1;
  Check(std::get<abc::MetadataError>(bind(1)) ==
        abc::MetadataError::kIndexRange);
  body.instructions[1].decoded.operands[1].raw_bits = UINT64_MAX;
  Check(std::get<CreationError>(bind(1)) ==
        CreationError::kMalformedInstruction);
  body.instructions[1].decoded.operands[1].raw_bits = 0;
  Check(std::get<CreationError>(bind(5)) == CreationError::kUnsupportedOpcode);
  Check(std::get<CreationBindingError>(bind(body.instructions.size())) ==
        CreationBindingError::kInstructionRange);
  flow.before[1].reset();
  Check(std::get<CreationBindingError>(bind(1)) ==
        CreationBindingError::kUnreachable);
  flow.before[1] = EnvironmentId{99};
  Check(std::get<CreationBindingError>(bind(1)) ==
        CreationBindingError::kInvalidEnvironment);
  flow.before.pop_back();
  Check(std::get<CreationBindingError>(bind(1)) ==
        CreationBindingError::kInvalidFlow);
  std::get<std::unique_ptr<corpus::AbcFile>>(opened).reset();
  metadata = {};
  body = {};
  target_body = {};
  Check(std::get<LexicalBinding>(
            moved_snapshot.Resolve({2}, target_snapshot, capture_slot))
            .environment == EnvironmentRef{EnvironmentId{0}});
  close(fd);
  unlink(path);
}
