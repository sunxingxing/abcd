#include "../../../src/source/class_tree_context.h"
#include "abcd/source/verified_function.h"
#include "abcd/source/tree_printer.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
static_assert(!std::is_copy_constructible_v<VerifiedFunction>);
static_assert(!std::is_constructible_v<VerifiedFunction, FunctionTree>);
static_assert(std::is_same_v<decltype(std::declval<VerifiedFunction&>().Tree()),
                             const FunctionTree&>);
void Check(bool condition) { if (!condition) std::abort(); }
void Run() {
  constexpr std::uint32_t kPool = 64, kLiteral = 128, kCaller = 256,
      kConstructor = 300, kMember = 350, kCallerCode = 400, kMemberCode = 430, kName = 500;
  std::vector<std::uint8_t> bytes(640);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor);
  word(kPool + sizeof(std::uint32_t), kLiteral);
  word(kLiteral, 8);  // Four tag/value entries, including prototype split.
  bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kLiteral + 5, kName);
  bytes[kLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
  word(kLiteral + 10, kMember);
  bytes[kLiteral + 14] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
  bytes[kLiteral + 15] = 1;
  bytes[kLiteral + 17] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 18, 1);
  bytes[kName] = 7;
  std::copy_n("run", 3, bytes.begin() + kName + 1);
  const std::vector<std::uint8_t> caller{1, 0, 10, 0, 0x35, 0, 0, 0, 1, 0, 0, 0, 0, 0x65};
  const std::vector<std::uint8_t> member{0, 4, 3, 0, 0x60, 3, 0x64};
  std::copy(caller.begin(), caller.end(), bytes.begin() + kCallerCode);
  std::copy(member.begin(), member.end(), bytes.begin() + kMemberCode);
  char path[] = "/tmp/abcd-class-tree-context-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCallerCode, {}, {2, kPool}, {}, {}});
  index.methods.resize(3);
  index.methods[0].offset = {kCaller};
  index.methods[0].code = abc::CodeInfo{{kCallerCode}, {kCallerCode + 4}, 1, 0, 10, 0};
  index.methods[1].offset = {kConstructor};
  index.methods[2].offset = {kMember};
  index.methods[2].code = abc::CodeInfo{{kMemberCode}, {kMemberCode + 4}, 0, 4, 3, 0};
  index.methods[2].name = "run";
  index.methods[2].name_utf16_length = 3;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  const auto range = sites.Find({kMember});
  Check(range.count == 1 && sites.Find({kConstructor}).count == 0);
  const auto* site = sites.At(range.first);
  Check(site && site->creator == abc::EntityOffset{kCaller} && site->pc.value == 0 && site->length == 1);
  auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  auto context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::holds_alternative<ClassTreeContext>(context));
  const auto& value = std::get<ClassTreeContext>(context);
  Check(value.Name() == u"run" && value.Frame().parameters == 1);
  Check(value.Callable() == CallableKind::kMethod && value.Mode() == Strictness::kStrict);
  auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::holds_alternative<VerifiedFunction>(prepared));
  const auto& function = std::get<VerifiedFunction>(prepared);
  Check(function.Name() == u"run" && function.Creation().target == abc::EntityOffset{kMember});
  Check(function.Tree().parameters.size() == 1);
  Check(function.Tree().callable == CallableKind::kMethod);
  Check(function.Tree().strictness == Strictness::kStrict);
  Check(function.Tree().identity == identity);
  Check(function.Receipt().stage == SourceStage::kTreeVerified);
  Check(function.Receipt().coverage == Coverage::kIncomplete);
  Check(!function.Receipt().failure);
  std::ostringstream output;
  const auto printing = PrintFunctionTree(function, output);
  Check(std::holds_alternative<PrintedFunction>(printing));
  const auto& printed = std::get<PrintedFunction>(printing);
  Check(printed.fragment == FunctionFragment::kClassMember);
  Check(printed.bytes == output.view().size());
  Check(output.view().starts_with("run($arg0) {\n"));
  Check(printed.expressions.size() == function.Tree().expressions.size());
  Check(printed.statements.size() == function.Tree().statements.size());
  for (const auto range : printed.expressions)
    Check(range.begin < range.end && range.end <= printed.bytes);
  for (const auto range : printed.statements)
    Check(range.begin < range.end && range.end <= printed.bytes);
  for (const auto offset : printed.anchors) Check(offset <= printed.bytes);
  std::ostringstream denied;
  Check(std::get<TreePrintError>(PrintFunctionTree(function, denied, {0})) == TreePrintError::kOutputLimit);
  Check(denied.view().empty());
  Check(std::get<TreePrintError>(PrintFunctionTree(function, denied, {}, {0, 0})) == TreePrintError::kResourceLimit);
  std::ostringstream broken;
  broken.setstate(std::ios::badbit);
  Check(std::get<TreePrintError>(PrintFunctionTree(function, broken)) == TreePrintError::kWrite);
  auto limited = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity, {0, 0});
  Check(std::get<FunctionPrepareFailure>(limited).kind == FunctionPrepareError::kResourceLimit);
  index.methods[2].code->arguments = 5;
  context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<ClassContextError>(std::get<ClassContextFailure>(context).error) == ClassContextError::kSignature);
  auto rejected = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<FunctionPrepareFailure>(rejected).kind == FunctionPrepareError::kContext);
  index.methods[2].code->arguments = 4;
  index.methods[2].name = "bad";
  context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<ClassContextError>(std::get<ClassContextFailure>(context).error) == ClassContextError::kName);
  Check(unlink(path) == 0);
}
}  // namespace
int main() { Run(); }
