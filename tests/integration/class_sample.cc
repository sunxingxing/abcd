#include <sys/resource.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abcd/abc/call_type.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/detect.h"
#include "abcd/source/created_class.h"
#include "abcd/source/module_imports.h"
#include "abcd/source/record_module_context.h"
#include "analysis/ssa_fixture.h"

// Validation fixture, not a record exporter. Every exported factory corresponds
// to one admitted creation site; rejection counts remain visible on stderr.
int main(int argc, char** argv) {
  using namespace abcd;
  test::Check(argc >= 2 && argc <= 4);
  std::optional<abc::EntityOffset> selected_record;
  if (argc == 3)
    test::Check(std::string_view(argv[2]) == "--module-aware");
  if (argc == 4) {
    test::Check(std::string_view(argv[2]) == "--record");
    const std::string_view text(argv[3]);
    std::uint32_t offset = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), offset);
    test::Check(parsed.ec == std::errc{} &&
                parsed.ptr == text.data() + text.size());
    selected_record = abc::EntityOffset{offset};
  }
  const bool module_aware = argc != 2;
  constexpr rlim_t kProcessBytes = 1000000000;
  rlimit limit{};
  test::Check(getrlimit(RLIMIT_AS, &limit) == 0);
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessBytes);
  test::Check(setrlimit(RLIMIT_AS, &limit) == 0);
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto hash = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto detection =
      std::get<corpus::Detection>(corpus::DetectAbc(file, hash));
  test::Check(detection.kind == corpus::SampleKind::kDynamic);
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  test::Check(header.checksum == hash.adler32);
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header, {kIndexBytes}));
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto role_result =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  if (const auto* failure =
          std::get_if<analysis::MethodRoleFailure>(&role_result)) {
    std::cerr << "method_roles_error_variant=" << failure->error.index()
              << '\n';
  } else {
    const auto& roles = std::get<analysis::MethodRoleIndex>(role_result);
    std::size_t ordinary = 0, conflicts = 0;
    for (std::uint32_t i = 0; i < index.methods.size(); ++i) {
      if (roles.At({i}) == analysis::MethodRole::kOrdinary) ++ordinary;
      if (roles.At({i}) == analysis::MethodRole::kConflict) ++conflicts;
    }
    std::cerr << "ordinary_roles=" << ordinary
              << " conflicting_roles=" << conflicts << '\n';
  }
  std::optional<abc::FilePackaging> packaging;
  if (module_aware) {
    const auto detected = abc::DetectFilePackaging(file, header, index);
    test::Check(std::holds_alternative<abc::FilePackaging>(detected));
    packaging = std::get<abc::FilePackaging>(detected);
  }
  std::size_t sites = 0, ready = 0, module_records = 0, prelude_bytes = 0;
  std::size_t visited_records = 0;
  for (const auto& record : index.records) {
    if (selected_record && record.offset != *selected_record) continue;
    ++visited_records;
    std::vector<std::string> factory_names;
    std::vector<std::string_view> enclosing_bindings;
    if (selected_record) {
      // The selected fixture publishes every site as a sibling module binding.
      // Certify that complete set before allowing any unqualified global read.
      for (const auto& method : std::span(index.methods).subspan(
               record.first_method, record.method_count)) {
        if (!method.code) continue;
        const auto body = std::get<abc::MethodBody>(
            abc::DecodeMethodBody(file, header.profile, *method.code));
        const auto call = std::get<abc::CallType>(
            abc::ReadCallType(file, header, index, method.annotations));
        const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
            method.code->registers, method.code->arguments,
            abc::DecodeCallFlags(call.value)));
        const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
            analysis::ProveClassHeritage(body, frame.registers));
        for (const auto& site : heritage)
          factory_names.push_back("site_" +
                                  std::to_string(method.offset.value) + "_" +
                                  std::to_string(site.pc.value));
      }
      std::sort(factory_names.begin(), factory_names.end());
      enclosing_bindings.reserve(factory_names.size());
      for (const auto& name : factory_names)
        enclosing_bindings.push_back(name);
    }
    std::optional<source::RecordModuleContext> context;
    std::ostringstream prelude;
    if (module_aware) {
      auto resolved = source::ReadRecordModuleContext(
          file, header, index, record, *packaging);
      test::Check(std::holds_alternative<source::RecordModuleContext>(resolved));
      context.emplace(
          std::move(std::get<source::RecordModuleContext>(resolved)));
      if (const auto module = context->ForOwner(file, record.offset)) {
        ++module_records;
        const auto emitted = source::WriteModuleImports(file, header, *module,
                                                         prelude);
        test::Check(std::holds_alternative<std::size_t>(emitted));
        prelude_bytes += std::get<std::size_t>(emitted);
      }
    }
    if (selected_record)
      std::cout << "// Incomplete class-site fixture; not a recovered record.\n"
                << prelude.str();
    for (const auto& method : std::span(index.methods).subspan(
             record.first_method, record.method_count)) {
      if (!method.code) continue;
      const auto body = std::get<abc::MethodBody>(
          abc::DecodeMethodBody(file, header.profile, *method.code));
      const auto call = std::get<abc::CallType>(
          abc::ReadCallType(file, header, index, method.annotations));
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(method.code->registers, method.code->arguments,
                                abc::DecodeCallFlags(call.value)));
      const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
          analysis::ProveClassHeritage(body, frame.registers));
      std::size_t at = 0;
      for (const auto& site : heritage) {
        ++sites;
        while (body.instructions[at].offset.value < site.pc.value) ++at;
        source::LocalLexicalBudget budget;
        constexpr std::size_t kOutputBytes = 1024 * 1024;
        budget.output.bytes = kOutputBytes;
        std::ostringstream source;
        const auto factory_name = "site_" + std::to_string(method.offset.value) +
                                  "_" + std::to_string(site.pc.value);
        const source::GlobalScope enclosing_scope{enclosing_bindings, {}};
        const auto module = context
                                ? context->ForOwner(file, method.owner_offset)
                                : std::shared_ptr<const abc::ModuleInfo>{};
        const auto result = source::WriteCreatedClass(
            file, header, index, lookup, method.offset, body.instructions[at],
            site, source, budget, {},
            std::get_if<analysis::MethodRoleIndex>(&role_result),
            module.get(), selected_record ? &enclosing_scope : nullptr);
        if (std::holds_alternative<std::size_t>(result)) {
          ++ready;
          if (!module_aware || selected_record)
            std::cout << "export function " << factory_name
                      << "() { return " << source.str()
                      << "; }\n";
        } else {
          test::Check(source.str().empty());
          std::cerr << "rejected caller=" << method.offset.value
                    << " pc=" << site.pc.value << " variant=" << result.index()
                    << '\n';
          if (const auto* failure =
                  std::get_if<source::CreatedClassError>(&result))
            std::cerr << "  class_kind=" << static_cast<int>(*failure)
                      << '\n';
          if (const auto* failure =
                  std::get_if<analysis::LinearLexicalFailure>(&result))
            std::cerr << "  lexical_kind=" << static_cast<int>(failure->kind)
                      << " body_pc=" << failure->offset.value << '\n';
          if (const auto* failure =
                  std::get_if<source::FunctionEmitError>(&result))
            std::cerr << "  function_kind=" << static_cast<int>(*failure)
                      << '\n';
          if (const auto* failure =
                  std::get_if<source::BodyEmitFailure>(&result))
            std::cerr << "  body_kind=" << static_cast<int>(failure->kind)
                      << " body_pc=" << failure->offset.value
                      << " emit_kind="
                      << (failure->emit_error
                              ? static_cast<int>(*failure->emit_error)
                              : -1)
                      << '\n';
        }
      }
    }
  }
  if (selected_record) test::Check(visited_records == 1);
  test::Check(!file.CheckUnchanged());
  test::Check(static_cast<bool>(std::cout));
  std::cerr << "sites=" << sites << " ready=" << ready << '\n';
  if (module_aware)
    std::cerr << "module_records=" << module_records
              << " prelude_bytes=" << prelude_bytes << '\n';
}
