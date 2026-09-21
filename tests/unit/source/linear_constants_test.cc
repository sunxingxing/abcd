#include <unistd.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "abcd/source/linear_body.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::size_t at,
          std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (8 * i);
}
}  // namespace
int main(int argc, char** argv) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(1024);
  Word(bytes, 64, 512);
  Word(bytes, 68, 1023);  // Invalid string payload at second slot.
  const std::array<std::uint8_t, 10> text{10,   'A',  0xc0, 0x80, 0xed,
                                          0xa0, 0x80, '"',  '\\', 0};
  std::copy(text.begin(), text.end(), bytes.begin() + 512);
  bytes[1023] = 0x80;
  char path[] = "/tmp/abcd-linear-constants-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {2, 64}, {}, {}});
  source::MethodConstants constants{file, header, index, {210}, {1024}};
  abc::MethodBody body;
  const std::array<std::uint8_t, 3> load{0x3e, 0, 0};
  const std::array<std::uint8_t, 1> ret{0x64};
  body.instructions.push_back(
      {{0},
       std::get<abc::DecodedInstruction>(
           abc::DecodeInstruction(load, header.profile)),
       {}});
  body.instructions.push_back({{3},
                               std::get<abc::DecodedInstruction>(
                                   abc::DecodeInstruction(ret, header.profile)),
                               {}});
  std::ostringstream output;
  const auto result =
      source::WriteLinearBody(body, {"acc", {}}, output, {}, &constants);
  Check(std::get<std::size_t>(result) == output.str().size());
  Check(output.str() == "acc = \"A\\u0000\\ud800\\\"\\\\\";\nreturn acc;\n");
  const std::array<std::string_view, 1> registers{"object"};
  std::array<std::string, 2> named_sources;
  for (std::size_t form = 0; form < named_sources.size(); ++form) {
    const std::vector<std::uint8_t> encoded =
        form == 0
            ? std::vector<std::uint8_t>{0x60, 0, 0x42, 0, 0, 0, 0x64}
            : std::vector<std::uint8_t>{0x60, 0, 0x90, 0xff, 0xff, 0, 0, 0x64};
    abc::MethodBody named;
    for (std::size_t offset = 0; offset < encoded.size();) {
      const auto decoded =
          std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
              std::span(encoded).subspan(offset), header.profile));
      named.instructions.push_back(
          {{static_cast<std::uint32_t>(offset)}, decoded, {}});
      offset += decoded.size;
    }
    std::ostringstream rendered;
    Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        named, {"acc", registers}, rendered, {}, &constants)));
    named_sources[form] = rendered.str();
    Check(named_sources[form] ==
          "acc = object;\nacc = (acc)[\"A\\u0000\\ud800\\\"\\\\\"];\nreturn "
          "acc;\n");
    std::ostringstream invalid;
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(named, {"acc", registers}, invalid))
              .kind == source::BodyEmitError::kMissingConstants);
    Check(invalid.str().empty());
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLinearBody(named, {"acc", registers}, invalid,
                                      {named_sources[form].size() - 1},
                                      &constants))
              .emit_error == source::EmitError::kOutputLimit);
    Check(invalid.str().empty());
    std::ostringstream exact;
    Check(std::get<std::size_t>(source::WriteLinearBody(
              named, {"acc", registers}, exact, {named_sources[form].size()},
              &constants)) == named_sources[form].size());
    named.instructions[1].decoded.operands[1].raw_bits = 1;
    const auto failure =
        std::get<source::BodyEmitFailure>(source::WriteLinearBody(
            named, {"acc", registers}, invalid, {}, &constants));
    Check(failure.offset.value == 2 && failure.metadata_error.has_value());
    Check(invalid.str().empty());
  }
  std::ostringstream rejected;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteLinearBody(body, {"acc", {}}, rejected))
            .kind == source::BodyEmitError::kMissingConstants);
  Check(rejected.str().empty());
  constants.string_budget.bytes = 0;
  Check(
      std::get<source::BodyEmitFailure>(
          source::WriteLinearBody(body, {"acc", {}}, rejected, {}, &constants))
          .metadata_error == abc::MetadataError::kResourceLimit);
  constants.string_budget.bytes = 1024;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteLinearBody(body, {"acc", {}}, rejected,
                                    {output.str().size() - 1}, &constants))
            .emit_error == source::EmitError::kOutputLimit);
  auto bad_load = body.instructions.front();
  bad_load.offset.value = 3;
  bad_load.decoded.operands[0].raw_bits = 1;
  body.instructions.insert(body.instructions.begin() + 1, bad_load);
  body.instructions.back().offset.value = 6;
  const auto failure = std::get<source::BodyEmitFailure>(
      source::WriteLinearBody(body, {"acc", {}}, rejected, {}, &constants));
  Check(failure.offset.value == 3 && failure.metadata_error.has_value());
  Check(rejected.str().empty());
  unlink(path);
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export function recovered(){let acc;\n"
              << output.str() << "}\n";
    for (std::size_t i = 0; i < named_sources.size(); ++i)
      std::cout << "export function named" << i << "(object){let acc;\n"
                << named_sources[i] << "}\n";
  }
}
