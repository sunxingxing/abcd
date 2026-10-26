#include "../../../src/source/family_file.h"
#include "../../../src/source/runtime_name.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
template <class T, class... E>
T Take(std::variant<T, E...> v) {
  Check(std::holds_alternative<T>(v));
  return std::get<T>(std::move(v));
}
}  // namespace
int main(int argc, char** argv) {
  Check(argc == 2);
  auto file =
      Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  auto index = Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  auto lookup =
      Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto roles = Take<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  auto functions = Take<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  auto identity = MakeRouteInputIdentity(hash, header);
  identity.contract = SourceContract::kLanguageSourceV2;
  unsigned tested = 0, captured = 0;
  for (const auto& method : index.methods) {
    const auto name = RuntimeName(method);
    if (name != "plain" && name != "captured" && name != "multiple" &&
        name != "scale200" && name != "scale201" && name != "scale400" &&
        name != "scale800")
      continue;
    const auto sites = functions.Find(method.offset);
    Check(sites.count == 1);
    FunctionContext context{Take<CreationTreeContext>(
        CreationTreeContext::Resolve(*file, header, index, lookup, roles,
                                     functions, sites.first, identity))};
    auto input = Take<FilePhasedFunction>(
        FilePhasedFunction::Read(*file, header, index, context));
    const auto owner = input.ParameterDefaults();
    Check(owner != nullptr);
    const auto good = owner->Evidence();
    const auto check = [&](const PhasedParameterCertificate& c) {
      return VerifyPhasedParameterDefaults(owner->Original(), owner->Body(),
                                           owner->Frame(), c);
    };
    const auto verified = Take<std::uint64_t>(check(good));
    Check(std::get<PhasedParameterError>(VerifyPhasedParameterDefaults(
              owner->Original(), owner->Body(), owner->Frame(), good,
              {analysis::SemanticBudget{}.bytes, verified - 1})) ==
          PhasedParameterError::kWorkLimit);
    const auto residual = Take<PhasedParameterResidual>(owner->Residual());
    for (const auto& d : good.defaults) {
      const auto& branch = residual.body.instructions[d.decision.value];
      const auto& immediate = branch.decoded.operands[0];
      const auto mask = immediate.width_bits == 64
                            ? UINT64_MAX
                            : (std::uint64_t{1} << immediate.width_bits) - 1;
      Check(branch.decoded.branch_delta.has_value() &&
            immediate.raw_bits ==
                (static_cast<std::uint64_t>(*branch.decoded.branch_delta) &
                 mask));
    }
    Check(std::get<PhasedParameterError>(owner->Residual(
              {analysis::SemanticBudget{}.bytes, residual.work - 1})) ==
          PhasedParameterError::kWorkLimit);
    auto bad = good;
    ++bad.defaults.front().ordinal;
    Check(std::get<PhasedParameterError>(check(bad)) ==
          PhasedParameterError::kOrdinal);
    bad = good;
    std::swap(bad.defaults.front().provided, bad.defaults.front().missing);
    Check(std::get<PhasedParameterError>(check(bad)) ==
          PhasedParameterError::kDecision);
    bad = good;
    ++bad.defaults.front().entry.value;
    Check(std::get<PhasedParameterError>(check(bad)) ==
          PhasedParameterError::kCorrespondence);
    if (!good.defaults.front().initialized.empty()) {
      bad = good;
      ++bad.defaults.front().initialized.front().value;
      Check(std::get<PhasedParameterError>(check(bad)) ==
            PhasedParameterError::kCorrespondence);
    }
    if (!good.defaults.front().captures.empty()) {
      bad = good;
      ++bad.defaults.front().captures.front().commit.value;
      Check(std::get<PhasedParameterError>(check(bad)) ==
            PhasedParameterError::kCapture);
      ++captured;
    }
    Check(std::get<PhasedParameterError>(ProvePhasedParameterDefaults(
              owner->Original(), owner->Body(), owner->Frame(), good.length,
              {0})) == PhasedParameterError::kResourceLimit);
    Check(std::get<PhasedParameterError>(ProvePhasedParameterDefaults(
              owner->Original(), owner->Body(), owner->Frame(), good.length,
              {analysis::SemanticBudget{}.bytes, 0})) ==
          PhasedParameterError::kWorkLimit);
    const auto exact =
        Take<PhasedParameterCertificate>(ProvePhasedParameterDefaults(
            owner->Original(), owner->Body(), owner->Frame(), good.length));
    // Borrowed control/identity checks have temporary grants in addition to
    // the recorded result arenas. Find the actual admission boundary; do not
    // describe retained bytes as the whole call's minimum scratch grant.
    Check(std::holds_alternative<PhasedParameterCertificate>(
        ProvePhasedParameterDefaults(
            owner->Original(), owner->Body(), owner->Frame(), good.length,
            {analysis::SemanticBudget{}.bytes, exact.work})));
    std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
    while (low < high) {
      const auto mid = low + (high - low) / 2;
      const auto result = ProvePhasedParameterDefaults(
          owner->Original(), owner->Body(), owner->Frame(), good.length,
          {mid, exact.work});
      if (std::holds_alternative<PhasedParameterCertificate>(result))
        high = mid;
      else {
        Check(std::get<PhasedParameterError>(result) ==
              PhasedParameterError::kResourceLimit);
        low = mid + 1;
      }
    }
    Check(low > 0 && std::holds_alternative<PhasedParameterCertificate>(
                         ProvePhasedParameterDefaults(
                             owner->Original(), owner->Body(), owner->Frame(),
                             good.length, {low, exact.work})));
    Check(std::get<PhasedParameterError>(ProvePhasedParameterDefaults(
              owner->Original(), owner->Body(), owner->Frame(), good.length,
              {low - 1, exact.work})) == PhasedParameterError::kResourceLimit);
    Check(std::get<PhasedParameterError>(ProvePhasedParameterDefaults(
              owner->Original(), owner->Body(), owner->Frame(), good.length,
              {analysis::SemanticBudget{}.bytes, exact.work - 1})) ==
          PhasedParameterError::kWorkLimit);
    std::cout << *name << " defaults=" << good.defaults.size()
              << " instructions=" << owner->Body().instructions.size()
              << " arenas=" << exact.allocated_bytes << " minimum_bytes=" << low
              << " work=" << exact.work << '\n';
    ++tested;
  }
  Check((tested == 3 && captured == 1) || (tested == 1 && captured == 0));
}
