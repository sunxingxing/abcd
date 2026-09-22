#include <sys/resource.h>

#include <algorithm>
#include <iostream>
#include <sstream>

#include "abcd/abc/call_type.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/detect.h"
#include "abcd/source/created_class.h"
#include "analysis/ssa_fixture.h"

// Validation fixture, not a record exporter. Every exported factory corresponds
// to one admitted creation site; rejection counts remain visible on stderr.
int main(int argc, char** argv) {
  using namespace abcd;
  test::Check(argc == 2);
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
  std::size_t sites = 0, ready = 0;
  for (const auto& method : index.methods) {
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
      const auto result = source::WriteCreatedClass(
          file, header, index, lookup, method.offset, body.instructions[at],
          site, source, budget);
      if (std::holds_alternative<std::size_t>(result)) {
        ++ready;
        std::cout << "export function site_" << method.offset.value << '_'
                  << site.pc.value << "() { return " << source.str() << "; }\n";
      } else {
        test::Check(source.str().empty());
        std::cerr << "rejected caller=" << method.offset.value
                  << " pc=" << site.pc.value << " variant=" << result.index()
                  << '\n';
      }
    }
  }
  test::Check(!file.CheckUnchanged());
  test::Check(static_cast<bool>(std::cout));
  std::cerr << "sites=" << sites << " ready=" << ready << '\n';
}
