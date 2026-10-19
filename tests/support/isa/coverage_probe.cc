// Read-only measurement: raw lifting is not a source-equivalence certificate.
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <iomanip>
#include <fstream>
#include <map>
#include <sstream>
#include <nlohmann/json.hpp>
#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/isa_catalog.h"
#include "abcd/analysis/exception_edge.h"
#include "process_limit.h"

namespace {
using namespace abcd;
using Json = nlohmann::json;
enum class ProbeError { kChecksum, kAllocation, kInput };
template<class T> constexpr std::string_view TypeName() {
  constexpr std::string_view text = __PRETTY_FUNCTION__;
  const auto start = text.find("T = ") + 4;
  return text.substr(start, text.find(']', start) - start);
}
// Typed semantic fields are diagnostic boundary labels, never opcode dispatch.
// This preserves role names (receiver/key/source/etc.) as well as the compact
// register may-read set. Variant alternatives remain explicitly identified.
template<class T> Json Describe(const T& value) {
  if constexpr (std::is_enum_v<T>) return static_cast<unsigned>(value);
  else if constexpr (std::is_arithmetic_v<T>) return value;
  else if constexpr (requires { std::variant_size<T>::value; }) {
    return std::visit([](const auto& item) -> Json { return Describe(item); }, value);
  } else if constexpr (requires { value.has_value(); *value; }) {
    return value ? Describe(*value) : Json(nullptr);
  } else if constexpr (requires { std::tuple_size<T>::value; value.begin(); }) {
    Json result=Json::array(); for (const auto& item:value) result.push_back(Describe(item)); return result;
  } else {
    Json result{{"type",TypeName<T>()}};
#define FIELD(name) if constexpr (requires { value.name; }) result[#name]=Describe(value.name)
    FIELD(destination); FIELD(expression); FIELD(input); FIELD(left); FIELD(right);
    FIELD(receiver); FIELD(key); FIELD(value); FIELD(object); FIELD(source);
    FIELD(callee); FIELD(iterator); FIELD(method); FIELD(state); FIELD(owner);
    FIELD(kind); FIELD(name); FIELD(index); FIELD(slot); FIELD(level);
    FIELD(arguments); FIELD(first); FIELD(count); FIELD(values); FIELD(constructor);
    FIELD(literal); FIELD(length); FIELD(parent); FIELD(cache_slot); FIELD(mode);
    FIELD(target); FIELD(predicate); FIELD(op); FIELD(completion); FIELD(lookup);
    FIELD(loading); FIELD(domain); FIELD(operation); FIELD(scope); FIELD(id);
#undef FIELD
    if constexpr (requires { value.First(); value.Count(); }) {
      result["first"]=Describe(value.First()); result["count"]=value.Count();
    }
    return result;
  }
}
Json Slot(const analysis::ScalarSlot& slot) {
  if (const auto* r = std::get_if<abc::RegisterId>(&slot)) return {{"register", r->value}};
  return {{"accumulator", true}};
}
Json Encoding(const abc::DecodedInstruction& d) {
  const auto code = static_cast<unsigned>(d.opcode);
  Json operands = Json::array();
  for (std::size_t i = 0; i < d.operand_count; ++i) {
    const auto& o = d.operands[i];
    operands.push_back({{"kind", unsigned(o.kind)}, {"access", unsigned(o.access)},
      {"width", o.width_bits}, {"signed", o.is_signed}, {"float", o.is_float}});
  }
  return {{"opcode", code}, {"prefix", code > 255 ? Json(code & 255) : Json(nullptr)},
    {"opcode_byte", code > 255 ? code >> 8 : code}, {"size", d.size},
    {"format", operands}, {"acc_read", d.accumulator_read}, {"acc_write", d.accumulator_write}};
}
void Catalog() {
  for (const auto& spec : analysis::PinnedIsaCatalog()) {
    std::array<std::uint8_t, abc::kMaxInstructionBytes> bytes{};
    const auto op = static_cast<unsigned>(spec.opcode);
    bytes[0] = op & 255; if (op > 255) bytes[1] = op >> 8;
    const auto decoded = abc::DecodeInstruction(std::span(bytes).first(spec.size), abc::ProfileId::kDynamic1206);
    auto row = Encoding(std::get<abc::DecodedInstruction>(decoded));
    row["type"] = "catalog"; row["family"] = unsigned(spec.group);
    row["profile"] = unsigned(abc::ProfileId::kDynamic1206);
    std::cout << row << '\n';
  }
}
struct RecoveryRecords {
  Json document;
  std::map<std::uint32_t, const Json*> records;
  std::map<std::pair<std::uint32_t,std::uint32_t>, const Json*> method_failures;
  void Read(const char* path, const std::string& digest) {
    // Production manifest only: reserve 64 MiB for a <=1 MiB JSON document
    // (DOM/string/node expansion included), separate from index/body budgets.
    constexpr std::size_t kManifestBytes=1024*1024;
    if (std::filesystem::file_size(path)>kManifestBytes) throw std::length_error("manifest budget");
    std::ifstream stream(path); stream >> document;
    if (document.at("sha256")!=digest) throw std::invalid_argument("manifest fingerprint");
    for (const auto& record:document.at("records")) {
      if (!records.emplace(record.at("entity").get<std::uint32_t>(), &record).second)
        throw std::invalid_argument("duplicate recovery record");
      const auto owner=record.at("entity").get<std::uint32_t>();
      if (record.contains("source_failure") && record.at("source_failure").contains("method")) {
        const auto& failure=record.at("source_failure");
        method_failures[{owner,failure.at("method").get<std::uint32_t>()}]=&failure;
      }
      if (record.contains("source_omitted_members")) for(const auto& failure:record.at("source_omitted_members"))
        method_failures[{owner,failure.at("method").get<std::uint32_t>()}]=&failure;
    }
  }
  Json At(std::uint32_t record, std::uint32_t method) const {
    Json result{{"record_status",nullptr},{"method_status","not_individually_reported"},
                {"record_first_refusal",nullptr},{"method_first_refusal",nullptr}};
    const auto found=records.find(record);
    if (found==records.end()) return result;
    const auto& r=*found->second;
    if (r.contains("source_status")) result["record_status"]=r.at("source_status");
    if (r.contains("source_failure")) {
      const auto& failure=r.at("source_failure"); result["record_first_refusal"]=failure;
    }
    const auto failure=method_failures.find({record,method});
    if(failure!=method_failures.end()) {
      result["method_status"]="refused"; result["method_first_refusal"]=*failure->second;
    }
    return result;
  }
};
int Probe(const char* path, const char* previous, const char* v4) {
  auto opened = corpus::AbcFile::Open(path);
  const auto* owner = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owner) { std::cout << Json{{"type","file_failure"},{"domain","FileError"},{"value",unsigned(std::get<corpus::FileError>(opened))}} << '\n'; return 1; }
  const auto& file = **owner;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) { std::cout << Json{{"type","file_failure"},{"domain","FileError"},{"value",unsigned(std::get<corpus::FileError>(fingerprint))}} << '\n'; return 1; }
  std::ostringstream hex; for (auto b : hash->sha256.bytes) hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
  RecoveryRecords previous_records, v4_records;
  if (previous) previous_records.Read(previous, hex.str());
  if (v4) v4_records.Read(v4, hex.str());
  const auto parsed = abc::ParseHeader(std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header) {
    std::cout << Json{{"type","file_failure"},{"sha256",hex.str()},{"domain","HeaderError"},{"value",unsigned(std::get<abc::HeaderError>(parsed))}} << '\n'; return 1;
  }
  if (header->checksum != hash->adler32) {
    std::cout << Json{{"type","file_failure"},{"sha256",hex.str()},{"domain","ProbeError"},{"value",unsigned(ProbeError::kChecksum)}} << '\n'; return 1;
  }
  // One file and one body at a time. Index 128 MiB + body 64 MiB + dispatch
  // 16 MiB are reserved independently; JSON is bounded by one instruction.
  const auto indexed = abc::BuildMetadataIndex(file, *header, {128 * 1024 * 1024});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) { std::cout << Json{{"type","file_failure"},{"domain","MetadataError"},{"value",unsigned(std::get<abc::MetadataError>(indexed))}} << '\n'; return 1; }
  for (const auto& method : index->methods) {
    Json base{{"sha256",hex.str()}, {"profile",unsigned(header->profile)},
      {"record",method.owner_offset.value}, {"method",method.offset.value}};
    base["recovery"]={{"previous",previous_records.At(method.owner_offset.value, method.offset.value)},
                      {"v4",v4_records.At(method.owner_offset.value, method.offset.value)}};
    if (!method.code) { base["type"]="method_no_code"; std::cout << base << '\n'; continue; }
    const auto& code = *method.code;
    const auto decoded = abc::DecodeMethodBody(file, header->profile, code, {64 * 1024 * 1024});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      const auto& f = std::get<abc::BodyFailure>(decoded);
      base["type"]="method_decode_failure"; base["pc"]=f.offset.value;
      base["cause"]={{"domain","BodyError"},{"value",unsigned(f.kind)}};
      if (f.instruction_error) base["cause"]["instruction_error"]=unsigned(*f.instruction_error);
      if (f.file_error) base["cause"]["file_error"]=unsigned(*f.file_error);
      std::cout << base << '\n'; continue;
    }
    const auto call = abc::ReadCallType(file, *header, *index, method.annotations, {1024 * 1024});
    std::optional<abc::FrameLayout> frame;
    Json frame_failure = nullptr;
    if (const auto* type = std::get_if<abc::CallType>(&call)) {
      const auto layout = abc::BuildFrameLayout(code.registers, code.arguments, abc::DecodeCallFlags(type->value));
      if (const auto* f = std::get_if<abc::FrameLayout>(&layout)) frame = *f;
      else frame_failure={{"domain","FrameError"},{"value",unsigned(std::get<abc::FrameError>(layout))}};
    } else frame_failure={{"domain","MetadataError"},{"value",unsigned(std::get<abc::MetadataError>(call))}};
    const auto dispatched = analysis::BuildExceptionDispatch(*body, header->profile, {16 * 1024 * 1024});
    const auto* dispatch = std::get_if<analysis::ExceptionDispatch>(&dispatched);
    for (const auto& site : body->instructions) {
      Json row = base; row["type"]="instruction"; row["pc"]=site.offset.value;
      row["encoding"]=Encoding(site.decoded); row["decode"]="ok";
      row["operands"]=Json::array();
      for (std::size_t i=0; i<site.decoded.operand_count; ++i) row["operands"].push_back(site.decoded.operands[i].raw_bits);
      row["branch_target"]=site.branch_target ? Json(site.branch_target->value) : Json(nullptr);
      if (!frame) { row["lift"]={{"status","context_refusal"},{"cause",frame_failure}}; std::cout << row << '\n'; continue; }
      const auto lifted = analysis::LiftSemanticInstruction(site, header->profile, frame->registers, &*frame);
      if (const auto* s = std::get_if<analysis::SemanticInstruction>(&lifted)) {
        Json result{{"status","ok"}};
        std::visit([&](const auto& op) { result["semantic_family"]=TypeName<std::decay_t<decltype(op)>>(); result["roles"]=Describe(op); }, s->operation);
        const auto& e=s->effects; result["normal_effects"]=unsigned(e.normal);
        result["before_throw_effects"]=unsigned(e.before_throw);
        result["exceptional_completion"]=unsigned(e.exceptional);
        result["exception_cause"]=unsigned(e.exception_cause);
        result["inputs"]=Json::array();
        for (std::size_t i=0;i<e.normal_registers.read_count;++i) result["inputs"].push_back(Slot(e.normal_registers.reads[i]));
        if (const auto& range=e.normal_registers.range) result["input_range"]={{"first",range->first.value},{"count",range->count}};
        result["normal_commit"]=e.normal_registers.write ? Slot(*e.normal_registers.write) : Json(nullptr);
        result["exception_exit"]=nullptr;
        if (dispatch) {
          if (const auto edge=analysis::ResolveExceptionEdge(*s,*dispatch)) {
            result["exception_exit"]={{"before",edge->before.value},{"payload_variant",edge->exception.index()}};
            result["exception_exit"]["handler_pc"]=edge->target ? Json(edge->target->handler_pc.value) : Json(nullptr);
            if (edge->target) result["exception_exit"]["try_id"]=edge->target->region.value;
          }
        } else result["dispatch_failure"]={{"domain","DispatchError"},{"value",unsigned(std::get<analysis::DispatchFailure>(dispatched).kind)}};
        row["lift"]=std::move(result);
      } else {
        const auto& f=std::get<analysis::SemanticFailure>(lifted);
        row["lift"]={{"status","refused"},{"cause",{{"domain","SemanticError"},{"value",unsigned(f.kind)},{"pc",f.pc.value}}}};
        // Raw instruction lifting has no SSA construction and no nested SSA cause.
        if (f.ssa_cause.index()!=0) throw std::logic_error("unserialized raw SSA cause");
      }
      std::cout << row << '\n'; if (!std::cout) return 1;
    }
  }
  if (const auto changed=file.CheckUnchanged()) {
    std::cout << Json{{"type","file_failure"},{"domain","FileError"},{"value",unsigned(*changed)}} << '\n'; return 1;
  }
  return 0;
}
}
int main(int argc,char** argv) {
  const auto limit=abcd::tools::EstablishProcessLimit();
  if (!abcd::tools::ProcessLimitAcceptable(limit)) return 1;
  try {
    if (argc!=2 && argc!=4) return 2;
    if (std::string_view(argv[1])=="--catalog") Catalog();
    else { const auto code=Probe(argv[1],argc==4 ? argv[2] : nullptr,argc==4 ? argv[3] : nullptr); nlohmann::json report; abcd::tools::ReportProcessMemory(report,limit); std::cerr << report << '\n'; return code; }
    return std::cout ? 0 : 1;
  } catch (const std::length_error&) { std::cerr << "{\"type\":\"manifest_resource_failure\"}\n"; return 1;
  } catch (const std::bad_alloc&) { std::cerr << Json{{"type","resource_failure"},{"domain","ProbeError"},{"value",unsigned(ProbeError::kAllocation)}} << '\n'; return 1;
  } catch (const std::exception&) { std::cerr << Json{{"type","input_failure"},{"domain","ProbeError"},{"value",unsigned(ProbeError::kInput)}} << '\n'; return 1; }
}
