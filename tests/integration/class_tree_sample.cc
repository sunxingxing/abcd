#include "../../src/source/class_tree.h"
#include "../../src/source/family_route_failure.h"
#include "../../src/source/evaluation_plan.h"
#include "abcd/corpus/abc_file.h"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <string_view>
namespace {
void Check(bool value) { if (!value) std::abort(); }
std::uint32_t Number(const char* value) {
  std::string_view text{value}; std::uint32_t result;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  Check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()); return result;
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  Check(argc == 4);
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto hash = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  const auto creator = lookup.Find({Number(argv[2])}); Check(creator.has_value());
  RouteInputIdentity identity; identity.profile = header.profile;
  identity.abc_version = header.version; identity.file_size = header.file_size;
  auto caller = ReadFunctionValues(file, header, index, *creator, identity, InputCaptureMode::kClosed);
  if (!std::holds_alternative<FileFunction>(caller)) { std::cerr << "caller failed\n"; return 1; }
  const auto& function = std::get<FileFunction>(caller);
  const auto ordinal = Number(argv[3]); Check(ordinal < function.values.classes.classes.size());
  const auto operation = function.values.classes.classes[ordinal].operation;
  // The unchanged full record graph must also schedule this creation. A
  // detached class body alone does not exercise heritage or surrounding order.
  const auto evaluated = BuildEvaluationPlan(function.values.graph);
  if (const auto* error = std::get_if<EvaluationError>(&evaluated)) {
    std::cerr << "caller evaluation failure " << static_cast<unsigned>(*error) << '\n'; return 1;
  }
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(function.values.graph, evaluation)));
  Check(evaluation.operations[operation.value].kind == EvaluationKind::kRoot ||
      evaluation.operations[operation.value].kind == EvaluationKind::kInline);
  auto built = BuildClassTree(file, header, index, lookup, roles, functions, classes, function, operation, identity);
  if (const auto* failure = std::get_if<ClassTreeFailure>(&built)) {
    std::cerr << "class tree failure " << failure->index();
    if (const auto* family = std::get_if<FamilyPrepareFailure>(failure)) {
      const auto reason = FamilyRouteFailure(*family);
      std::cerr << " family " << family->cause.index() << " stage " << static_cast<unsigned>(reason.reason);
    }
    std::cerr << '\n'; return 1;
  }
  const auto& tree = std::get<ClassTree>(built);
  std::ostringstream output;
  const auto printed = PrintClassTree(tree, output);
  if (!std::holds_alternative<PrintedClassTree>(printed)) {
    std::cerr << "class print failure " << printed.index() << '\n'; return 1;
  }
  const auto& positions = std::get<PrintedClassTree>(printed);
  Check(positions.bytes == output.str().size() && positions.members.size() == tree.members.size() + 1);
  std::size_t last = 0;
  for (const auto& member : positions.members) {
    Check(member.offset >= last && member.offset + member.positions.bytes <= positions.bytes);
    last = member.offset + member.positions.bytes;
  }
  std::ostringstream limited;
  const auto small = PrintClassTree(tree, limited, {1});
  Check(std::get<TreePrintError>(small) == TreePrintError::kOutputLimit);
  std::cout << output.str();
}
