#include "abcd/analysis/creation_binding.h"

#include <unistd.h>

#include <cstdlib>

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
  // Mutating only attributes leaves lookup positions valid for this fixture.
  metadata.methods[1].access_flags = 8U << 8;
  Check(std::holds_alternative<SendableCapture>(
      std::get<CreationBinding>(bind(1)).capture));
  const auto method = std::get<CreationBinding>(bind(4));
  Check(method.creation.kind == CreationKind::kMethod);
  Check(method.creation.home_object == HomeObjectInput::kAccumulator);
  Check(std::holds_alternative<OrdinaryCapture>(method.capture));
  metadata.methods[1].access_flags = 0xffU << 8;
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
  close(fd);
  unlink(path);
}
