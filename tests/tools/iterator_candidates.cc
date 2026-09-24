#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "abcd/abc/metadata_index.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/analysis/iterator.h"
#include "abcd/analysis/first_for_in.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/corpus/detect.h"

namespace {
namespace abc = abcd::abc;
namespace corpus = abcd::corpus;
namespace analysis = abcd::analysis;

int Inspect(std::string_view path, std::optional<std::uint32_t> selected) {
  const auto opened = corpus::AbcFile::Open(path);
  const auto* owned = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owned) return 1;
  const auto& file = **owned;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return 1;
  const auto detection = corpus::DetectAbc(file, *hash);
  const auto* kind = std::get_if<corpus::Detection>(&detection);
  if (!kind || kind->kind != corpus::SampleKind::kDynamic) return 1;
  const auto parsed = abc::ParseHeader(
      std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->checksum != hash->adler32) return 1;
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return 1;
  constexpr std::size_t kDumpInstructionLimit = 20000;
  bool found = false;
  for (const auto& record : index->records) {
    const auto methods = std::span(index->methods).subspan(
        record.first_method, record.method_count);
    for (const auto& method : methods) {
      if (!method.code || (selected && method.offset.value != *selected))
        continue;
      const auto decoded = abc::DecodeMethodBody(file, header->profile,
                                                 *method.code);
      const auto* body = std::get_if<abc::MethodBody>(&decoded);
      if (!body) return 1;
      if (selected && body->instructions.size() > kDumpInstructionLimit)
        return 2;
      nlohmann::json sites = nlohmann::json::array();
      nlohmann::json instructions = nlohmann::json::array();
      for (const auto& site : body->instructions) {
        const auto operation = analysis::LiftIteratorInstruction(
            site.decoded, std::uint64_t{method.code->registers} +
                              method.code->arguments);
        if (!std::holds_alternative<analysis::IteratorError>(operation))
          sites.push_back({{"pc", site.offset.value},
                           {"opcode", static_cast<unsigned>(site.decoded.opcode)}});
        if (!selected) continue;
        nlohmann::json operands = nlohmann::json::array();
        for (std::size_t i = 0; i < site.decoded.operand_count; ++i)
          operands.push_back({{"kind", static_cast<unsigned>(
                                           site.decoded.operands[i].kind)},
                              {"bits", site.decoded.operands[i].raw_bits}});
        instructions.push_back({{"pc", site.offset.value},
                                {"opcode", static_cast<unsigned>(
                                               site.decoded.opcode)},
                                {"operands", std::move(operands)},
                                {"target", site.branch_target
                                               ? nlohmann::json(
                                                     site.branch_target->value)
                                               : nlohmann::json(nullptr)}});
      }
      if (sites.empty() && !selected) continue;
      found = true;
      nlohmann::json item{{"input", std::string(path)},
                          {"record", record.descriptor},
                          {"method", method.name},
                          {"entity", method.offset.value},
                          {"registers", method.code->registers},
                          {"arguments", method.code->arguments},
                          {"instructions_count", body->instructions.size()},
                          {"tries", body->tries.size()},
                          {"iterator_sites", std::move(sites)}};
      if (selected) {
        const auto call_type = abc::ReadCallType(file, *header, *index,
                                                 method.annotations);
        const auto attributes = abc::DecodeMethodAttributes(
            method.access_flags, header->profile);
        if (!std::holds_alternative<abc::CallType>(call_type) ||
            !std::holds_alternative<abc::MethodAttributes>(attributes))
          return 1;
        const auto layout = abc::BuildFrameLayout(
            method.code->registers, method.code->arguments,
            abc::DecodeCallFlags(std::get<abc::CallType>(call_type).value));
        if (!std::holds_alternative<abc::FrameLayout>(layout)) return 1;
        const auto& frame = std::get<abc::FrameLayout>(layout);
        const auto& flags = std::get<abc::MethodAttributes>(attributes);
        item["frame"] = {{"locals", frame.locals},
                         {"registers", frame.registers},
                         {"first_parameter", frame.first_parameter.value},
                         {"parameters", frame.parameters}};
        item["function_kind"] = static_cast<unsigned>(flags.kind);
        item["sendable"] = flags.sendable;
        item["first_for_in"] =
            analysis::MatchFirstForIn(*body, frame).has_value();
        item["instructions"] = std::move(instructions);
        auto regions = nlohmann::json::array();
        for (const auto& region : body->tries)
          regions.push_back({{"pc", region.offset.value},
                             {"length", region.length},
                             {"first_handler", region.first_handler},
                             {"handler_count", region.handler_count}});
        auto handlers = nlohmann::json::array();
        for (const auto& handler : body->handlers)
          handlers.push_back({{"pc", handler.offset.value},
                              {"length", handler.length},
                              {"type", handler.type
                                           ? nlohmann::json(handler.type->value)
                                           : nlohmann::json(nullptr)}});
        item["try_regions"] = std::move(regions);
        item["handlers"] = std::move(handlers);
      }
      std::cout << item << '\n';
    }
  }
  return selected && !found ? 1 : 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: iterator_candidates INPUT.abc [METHOD_ENTITY]\n";
    return 1;
  }
  std::optional<std::uint32_t> selected;
  if (argc == 3) {
    std::uint32_t entity = 0;
    const std::string_view text(argv[2]);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        entity);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      return 1;
    selected = entity;
  }
  return Inspect(argv[1], selected);
}
