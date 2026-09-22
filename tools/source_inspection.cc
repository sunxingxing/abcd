#include "source_inspection.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <ostream>
#include <streambuf>
#include <type_traits>

#include "abcd/abc/call_type.h"

namespace abcd::tools {
namespace {
class SourceSink : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize size) override {
    return size;
  }
  int_type overflow(int_type value) override {
    return traits_type::not_eof(value);
  }
};

const char* Category(abc::MetadataError) { return "metadata"; }
const char* Category(abc::FrameError) { return "frame"; }
const char* Category(analysis::FlowError) { return "flow"; }
const char* Category(analysis::AccumulatorError) { return "accumulator"; }
const char* Category(analysis::OrderError) { return "order"; }
const char* Category(source::BodyEmitError) { return "body"; }
const char* Category(source::AcyclicError) { return "acyclic"; }
const char* Category(source::EmitError) { return "emission"; }
const char* Category(SourceProbeError) { return "probe"; }
}  // namespace

SourceCounts& SourceCounts::operator+=(const SourceCounts& other) {
  ready += other.ready;
  rejected += other.rejected;
  bytes += other.bytes;
  for (const auto& [obstacle, count] : other.obstacles)
    obstacles[obstacle] += count;
  return *this;
}

nlohmann::json SerializeCounts(const SourceCounts& counts) {
  auto obstacles = nlohmann::json::array();
  for (const auto& [obstacle, count] : counts.obstacles) {
    auto item = std::visit(
        [&](auto error) {
          return nlohmann::json{{"category", Category(error)},
                                {"error", static_cast<int>(error)},
                                {"count", count}};
        },
        obstacle.error);
    if (obstacle.opcode)
      item["opcode"] = static_cast<std::uint16_t>(*obstacle.opcode);
    obstacles.push_back(std::move(item));
  }
  return {{"source_body_ready", counts.ready},
          {"source_body_rejected", counts.rejected},
          {"source_bytes", counts.bytes},
          {"source_obstacles", std::move(obstacles)}};
}

void InspectSource(const corpus::AbcFile& file, const abc::Header& header,
                   const abc::MetadataIndex& index,
                   const abc::MethodInfo& method, const abc::MethodBody& body,
                   SourceCounts* counts) {
  const auto reject = [&](SourceError error,
                          std::optional<abc::CodeOffset> pc = {}) {
    std::optional<abc::Opcode> opcode;
    if (pc) {
      const auto found = std::lower_bound(
          body.instructions.begin(), body.instructions.end(), *pc,
          [](const abc::BodyInstruction& instruction, abc::CodeOffset offset) {
            return instruction.offset < offset;
          });
      if (found != body.instructions.end() && found->offset == *pc)
        opcode = found->decoded.opcode;
    }
    ++counts->rejected;
    ++counts->obstacles[SourceObstacle{error, opcode}];
  };
  const auto call_type =
      abc::ReadCallType(file, header, index, method.annotations);
  if (const auto* error = std::get_if<abc::MetadataError>(&call_type)) {
    reject(*error);
    return;
  }
  const auto layout = abc::BuildFrameLayout(
      method.code->registers, method.code->arguments,
      abc::DecodeCallFlags(std::get<abc::CallType>(call_type).value));
  if (const auto* error = std::get_if<abc::FrameError>(&layout)) {
    reject(*error);
    return;
  }
  const auto registers = std::get<abc::FrameLayout>(layout).registers;
  constexpr std::size_t kNameBudget = 64 * 1024 * 1024;
  // One prefix byte plus all decimal uint32 digits. Fixed arrays admit their
  // entire payload before allocation, without vector over-capacity. Views stay
  // valid through emission. Allocator overhead belongs to the process ceiling.
  using RegisterName =
      std::array<char, 2 + std::numeric_limits<std::uint32_t>::digits10>;
  constexpr auto kRegisterBytes =
      sizeof(RegisterName) + sizeof(std::string_view);
  if (registers > kNameBudget / kRegisterBytes) {
    reject(SourceProbeError::kNameBudget);
    return;
  }
  auto storage = std::make_unique<RegisterName[]>(registers);
  auto names = std::make_unique<std::string_view[]>(registers);
  for (std::uint32_t i = 0; i < registers; ++i) {
    auto& name = storage[i];
    name[0] = 'r';
    const auto encoded =
        std::to_chars(name.data() + 1, name.data() + name.size(), i);
    names[i] = std::string_view(name.data(), encoded.ptr - name.data());
  }
  SourceSink buffer;
  std::ostream sink(&buffer);
  const source::MethodConstants constants{
      file, header, index, method.offset, {}};
  const auto result = source::WriteBlockLoopBody(
      body, {"acc", std::span(names.get(), registers)}, sink, {}, &constants);
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::size_t>) {
          ++counts->ready;
          counts->bytes += value;
        } else if constexpr (std::is_same_v<T, source::AcyclicError>) {
          reject(value);
        } else if constexpr (std::is_same_v<T, analysis::AccumulatorFailure>) {
          if (value.order_error)
            reject(*value.order_error);
          else
            reject(value.kind,
                   value.kind == analysis::AccumulatorError::
                                     kUnsupportedInstruction ||
                           value.kind ==
                               analysis::AccumulatorError::kReadBeforeValue
                       ? std::optional(value.offset)
                       : std::nullopt);
        } else if constexpr (std::is_same_v<T, source::BodyEmitFailure>) {
          if (value.emit_error)
            reject(*value.emit_error, value.offset);
          else if (value.metadata_error)
            reject(*value.metadata_error, value.offset);
          else
            reject(value.kind,
                   value.kind == source::BodyEmitError::kExceptionRegions
                       ? std::nullopt
                       : std::optional(value.offset));
        } else {
          static_assert(std::is_same_v<T, analysis::FlowFailure>);
          reject(value.kind,
                 value.kind == analysis::FlowError::kResourceLimit ||
                         value.kind == analysis::FlowError::kEmptyBody
                     ? std::nullopt
                     : std::optional(value.offset));
        }
      },
      result);
}
}  // namespace abcd::tools
