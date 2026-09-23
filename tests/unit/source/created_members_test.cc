#include <unistd.h>

#include <algorithm>
#include <sstream>

#include "abcd/abc/module_info.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_class.h"
#include "abcd/source/created_function.h"
#include "analysis/ssa_fixture.h"

namespace {
void Run(std::string_view name, std::uint32_t split, abcd::abc::LiteralTag tag,
         bool expected, bool emit = false) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64, kLiteral = 128, kCaller = 256,
                          kConstructor = 300, kMember = 350, kStatic = 360,
                          kCode = 400, kMemberCode = 430, kCallerCode = 460,
                          kName = 600;
  constexpr unsigned kKindShift = 8;
  std::vector<std::uint8_t> bytes(1024);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor);
  word(kPool + 4, kLiteral);
  word(kPool + 8, 620);
  constexpr std::uint32_t kInner = 370, kNestedCode = 720,
                          kInnerCode = 740, kNestedConstructorCode = 760,
                          kArrowThisCode = 780, kArrowSelfCode = 800,
                          kArrowNewTargetCode = 820,
                          kArrowDeadContextCode = 840,
                          kArrowLiveContextCode = 860;
  word(kPool + 12, kInner);
  word(kLiteral, 14);  // Two string/method/affiliate triples and the split.
  for (const auto at : {kLiteral + 4, kLiteral + 17}) {
    bytes[at] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at + 1, kName);
    bytes[at + 5] = static_cast<std::uint8_t>(tag);
    word(at + 6, at == kLiteral + 4 ? kMember : kStatic);
    bytes[at + 10] =
        static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
    bytes[at + 11] = 1;
  }
  bytes[kLiteral + 30] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 31, split);
  bytes[kName] = name.size() * 2 + 1;  // MUTF-8 ASCII flag.
  std::copy(name.begin(), name.end(), bytes.begin() + kName + 1);
  const std::vector<std::uint8_t> ctor{0, 3, 1, 0, 0x65};
  const std::vector<std::uint8_t> member{0, 4, 3, 0, 0x60, 0, 0x64};
  const std::vector<std::uint8_t> caller{1, 0, 13, 0, 0x70, 0x61, 0, 0x35, 0,
                                         0, 0, 1,  0, 0,    0,    0, 0x65};
  std::copy(ctor.begin(), ctor.end(), bytes.begin() + kCode);
  std::copy(member.begin(), member.end(), bytes.begin() + kMemberCode);
  std::copy(caller.begin(), caller.end(), bytes.begin() + kCallerCode);
  const std::vector<std::uint8_t> import_method{
      0, 4, 7, 0, 0x7e, 0, 0xfe, 9, 2, 0, 0x64};
  std::copy(import_method.begin(), import_method.end(), bytes.begin() + 500);
  bytes[620] = 11;
  std::copy_n("value", 5, bytes.begin() + 621);
  constexpr std::string_view kRequest = "./module-import-dep.mjs";
  bytes[650] = kRequest.size() * 2 + 1;
  std::copy(kRequest.begin(), kRequest.end(), bytes.begin() + 651);
  constexpr std::uint32_t kBadCode = 700;
  const std::vector<std::uint8_t> unsupported{0, 4, 2, 0, 0x70, 0x64};
  std::copy(unsupported.begin(), unsupported.end(), bytes.begin() + kBadCode);
  const std::vector<std::uint8_t> nested{0, 4, 6, 0, 0x33, 0, 3, 0, 1, 0x64};
  const std::vector<std::uint8_t> inner{0, 4, 3, 0, 0x60, 3, 0x64};
  const std::vector<std::uint8_t> nested_ctor_bytes{
      0, 3, 6, 0, 0x33, 0, 3, 0, 1, 0x64};
  const std::vector<std::uint8_t> arrow_this{0, 4, 3, 0, 0x60, 2, 0x64};
  const std::vector<std::uint8_t> arrow_self{0, 4, 3, 0, 0x60, 0, 0x64};
  const std::vector<std::uint8_t> arrow_new_target{0, 4, 3, 0, 0x60, 1,
                                                    0x64};
  const std::vector<std::uint8_t> arrow_dead_context{
      0, 4, 7, 0, 0x44, 0x20, 0x44, 0x10, 0x60, 3, 0x64};
  const std::vector<std::uint8_t> arrow_live_context{
      0, 4, 5, 0, 0x44, 0x20, 0x60, 0, 0x64};
  std::copy(nested.begin(), nested.end(), bytes.begin() + kNestedCode);
  std::copy(inner.begin(), inner.end(), bytes.begin() + kInnerCode);
  std::copy(nested_ctor_bytes.begin(), nested_ctor_bytes.end(),
            bytes.begin() + kNestedConstructorCode);
  std::copy(arrow_this.begin(), arrow_this.end(),
            bytes.begin() + kArrowThisCode);
  std::copy(arrow_self.begin(), arrow_self.end(),
            bytes.begin() + kArrowSelfCode);
  std::copy(arrow_new_target.begin(), arrow_new_target.end(),
            bytes.begin() + kArrowNewTargetCode);
  std::copy(arrow_dead_context.begin(), arrow_dead_context.end(),
            bytes.begin() + kArrowDeadContextCode);
  std::copy(arrow_live_context.begin(), arrow_live_context.end(),
            bytes.begin() + kArrowLiveContextCode);
  char path[] = "/tmp/abcd-created-members-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCode, {}, {4, kPool}, {}, {}});
  index.methods.resize(5);
  index.methods[0].offset = {kCaller};
  index.methods[0].code =
      abc::CodeInfo{{kCallerCode}, {kCallerCode + 4}, 1, 0, 13, 0};
  index.methods[1].offset = {kConstructor};
  index.methods[1].name = "MethodSelf0";
  index.methods[1].name_utf16_length = index.methods[1].name.size();
  index.methods[1].code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 3, 1, 0};
  auto& method = index.methods[2];
  method.offset = {kMember};
  method.name = "#scope#" + std::string(name) + "^1";
  method.name_utf16_length = method.name.size();
  method.code = abc::CodeInfo{{kMemberCode}, {kMemberCode + 4}, 0, 4, 3, 0};
  index.methods[3] = method;
  index.methods[3].offset = {kStatic};
  auto& inner_method = index.methods[4];
  inner_method.offset = {kInner};
  inner_method.name = "#scope#Inner^1";
  inner_method.name_utf16_length = inner_method.name.size();
  inner_method.access_flags = 1U << kKindShift;
  inner_method.code =
      abc::CodeInfo{{kInnerCode}, {kInnerCode + 4}, 0, 4, 3, 0};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  test::Check(roles.At({2}) == (tag == abc::LiteralTag::kMethod
                                    ? analysis::MethodRole::kOrdinary
                                    : analysis::MethodRole::kOther));
  const auto body =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 1, 0, 0, 0, 0, 0x65});
  const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
      analysis::ProveClassHeritage(body, 1))[0];
  std::ostringstream output;
  const auto result = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, body.instructions[2], heritage,
      output, {}, {}, &roles);
  test::Check(std::holds_alternative<std::size_t>(result) == expected);
  if (!expected) {
    test::Check(output.str().empty());
    test::Check(unlink(path) == 0);
    return;
  }
  test::Check(std::get<std::size_t>(result) == output.str().size());
  if (name == "method") {
    test::Check(roles.At({1}) == analysis::MethodRole::kOther);
    auto& constructor = index.methods[1];
    constructor.access_flags =
        static_cast<std::uint32_t>(abc::FunctionKind::kArrow) << kKindShift;
    const auto define = test::Body({0x33, 0, 0, 0, 1, 0x64});
    std::ostringstream conflicting_role;
    const auto conflict_result = source::WriteCreatedFunction(
        file, header, index, lookup, {kCaller}, define.instructions[0].decoded,
        conflicting_role, {}, {}, nullptr, &roles);
    test::Check(std::get<source::CreatedFunctionError>(conflict_result) ==
                source::CreatedFunctionError::kRole);
    test::Check(conflicting_role.str().empty());
    constructor.access_flags = 0;
    const auto original_code = method.code;
    method.code = abc::CodeInfo{{kNestedCode}, {kNestedCode + 4}, 0, 4, 6, 0};
    std::ostringstream closure;
    const auto closure_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, closure, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(closure_result));
    test::Check(closure.str().find("function Inner($p0)") !=
                std::string::npos);
    if (emit)
      std::cout << "export const recoveredClosure = " << closure.str()
                << ";\n";
    inner_method.access_flags =
        static_cast<std::uint32_t>(abc::FunctionKind::kArrow) << kKindShift;
    inner_method.code =
        abc::CodeInfo{{kInnerCode}, {kInnerCode + 4}, 0, 4, 3, 0};
    std::ostringstream arrow;
    const auto arrow_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, arrow, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(arrow_result));
    if (emit)
      std::cout << "export const recoveredArrow = " << arrow.str() << ";\n";
    inner_method.code = abc::CodeInfo{{kArrowDeadContextCode},
                                      {kArrowDeadContextCode + 4}, 0, 4, 7, 0};
    std::ostringstream dead_context;
    const auto dead_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, dead_context, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(dead_result));
    if (emit)
      std::cout << "export const recoveredDeadContextArrow = "
                << dead_context.str() << ";\n";
    inner_method.code = abc::CodeInfo{{kArrowLiveContextCode},
                                      {kArrowLiveContextCode + 4}, 0, 4, 5, 0};
    std::ostringstream live_context;
    const auto live_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, live_context, {}, {}, &roles);
    test::Check(std::get<source::BodyEmitFailure>(live_result).kind ==
                source::BodyEmitError::kCreation);
    test::Check(live_context.str().empty());
    for (const auto code : {kArrowThisCode, kArrowNewTargetCode}) {
      inner_method.code = abc::CodeInfo{{code}, {code + 4}, 0, 4, 3, 0};
      std::ostringstream rejected_slot;
      const auto slot_result = source::WriteCreatedClass(
          file, header, index, lookup, {kCaller}, body.instructions[2],
          heritage, rejected_slot, {}, {}, &roles);
      test::Check(std::get<source::BodyEmitFailure>(slot_result).kind ==
                  source::BodyEmitError::kCreation);
      test::Check(rejected_slot.str().empty());
    }
    inner_method.code =
        abc::CodeInfo{{kArrowSelfCode}, {kArrowSelfCode + 4}, 0, 4, 3, 0};
    std::ostringstream self_arrow;
    const auto self_arrow_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, self_arrow, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(self_arrow_result));
    if (emit)
      std::cout << "export const recoveredArrowSelf = " << self_arrow.str()
                << ";\n";
    const auto named_arrow = inner_method.name;
    inner_method.name = "#scope#";
    inner_method.name_utf16_length = inner_method.name.size();
    inner_method.code =
        abc::CodeInfo{{kInnerCode}, {kInnerCode + 4}, 0, 4, 3, 0};
    std::ostringstream anonymous_arrow;
    const auto anonymous_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, anonymous_arrow, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(anonymous_result));
    if (emit)
      std::cout << "export const recoveredAnonymousArrow = "
                << anonymous_arrow.str() << ";\n";
    inner_method.name = named_arrow;
    inner_method.name_utf16_length = inner_method.name.size();
    inner_method.access_flags =
        static_cast<std::uint32_t>(abc::FunctionKind::kGenerator)
        << kKindShift;
    std::ostringstream rejected_generator;
    const auto generator_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, rejected_generator, {}, {}, &roles);
    test::Check(std::get<source::BodyEmitFailure>(generator_result).kind ==
                source::BodyEmitError::kCreation);
    test::Check(rejected_generator.str().empty());
    inner_method.access_flags = 1U << kKindShift;
    inner_method.code =
        abc::CodeInfo{{kInnerCode}, {kInnerCode + 4}, 0, 4, 3, 0};
    method.code = original_code;
    const auto original_constructor_code = constructor.code;
    constructor.code =
        abc::CodeInfo{{kNestedConstructorCode},
                      {kNestedConstructorCode + 4}, 0, 3, 6, 0};
    std::ostringstream constructor_source;
    const auto constructor_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, constructor_source, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(constructor_result));
    test::Check(constructor_source.str().find("function Inner($p0)") !=
                std::string::npos);
    constructor.code = original_constructor_code;
    auto& static_method = index.methods[3];
    const auto original_static_code = static_method.code;
    static_method.code =
        abc::CodeInfo{{kNestedCode}, {kNestedCode + 4}, 0, 4, 6, 0};
    std::ostringstream static_source;
    const auto static_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, static_source, {}, {}, &roles);
    test::Check(std::holds_alternative<std::size_t>(static_result));
    test::Check(static_source.str().find("static [\"method\"]") !=
                std::string::npos);
    if (emit)
      std::cout << "export const recoveredStaticClosure = "
                << static_source.str() << ";\n";
    static_method.code = original_static_code;
    abc::ModuleInfo module;
    module.requests.push_back({650});
    module.imports.push_back({{620}, {620}, {0}});
    const auto saved_import_code = method.code;
    method.code = abc::CodeInfo{{500}, {504}, 0, 4, 7, 0};
    std::ostringstream imported;
    const auto import_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, imported, {}, {}, &roles, &module);
    test::Check(std::holds_alternative<std::size_t>(import_result));
    test::Check(imported.str().find("$acc = $import0;") !=
                std::string::npos);
    method.owner_offset = {999};
    std::ostringstream foreign;
    const auto foreign_result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2],
        heritage, foreign, {}, {}, &roles, &module);
    test::Check(std::get<source::CreatedClassError>(foreign_result) ==
                source::CreatedClassError::kModuleOwner);
    test::Check(foreign.str().empty());
    method.owner_offset = {};
    method.code = saved_import_code;
  }
  if (emit) std::cout << "export const recovered = " << output.str() << ";\n";
  const auto render = [&](source::LocalLexicalBudget budget = {},
                          const analysis::MethodRoleIndex* proof = nullptr) {
    std::ostringstream text;
    const auto result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2], heritage,
        text, budget, {}, proof);
    if (!std::holds_alternative<std::size_t>(result))
      test::Check(text.str().empty());
    return result;
  };
  test::Check(std::get<source::CreatedClassError>(render()) ==
              source::CreatedClassError::kMembers);
  const auto saved_name = method.name;
  method.name = "wrong";
  method.name_utf16_length = method.name.size();
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kName);
  method.name = saved_name;
  method.name_utf16_length = method.name.size();
  method.access_flags = static_cast<unsigned>(abc::FunctionKind::kAsync)
                        << kKindShift;
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kMembers);
  method.access_flags = 0;
  const auto saved_code = method.code;
  method.code.reset();
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kMissingCode);
  method.code = saved_code;
  index.methods[3].code = abc::CodeInfo{{kBadCode}, {kBadCode + 4}, 0, 4, 2, 0};
  // Last member fails, after a valid constructor and first member in dry run.
  test::Check(!std::holds_alternative<std::size_t>(render({}, &roles)));
  index.methods[3].code = saved_code;
  for (const auto count : {output.str().size() - 1, output.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = count;
    test::Check(std::holds_alternative<std::size_t>(render(budget, &roles)) ==
                (count == output.str().size()));
  }
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  const auto fits = [&](std::size_t bytes) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    return std::holds_alternative<std::size_t>(render(budget, &roles));
  };
  test::Check(!fits(low) && fits(high));
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    if (fits(middle))
      high = middle;
    else
      low = middle;
  }
  test::Check(fits(high) && !fits(high - 1));
  test::Check(unlink(path) == 0);
}
}  // namespace
int main(int argc, char** argv) {
  using abcd::abc::LiteralTag;
  Run("method", 1, LiteralTag::kMethod, true,
      argc == 2 && std::string_view(argv[1]) == "--emit");
  Run("quoted\"key", 1, LiteralTag::kMethod, true);
  Run("__proto__", 1, LiteralTag::kMethod, true);
  Run("", 1, LiteralTag::kMethod, true);
  for (const auto key :
       {"constructor", "name", "length", "prototype", "0", "4294967294"})
    Run(key, 1, LiteralTag::kMethod, false);
  Run("4294967295", 1, LiteralTag::kMethod, true);
  Run("method", 0, LiteralTag::kMethod, false);
  Run("method", 2, LiteralTag::kMethod, false);
  for (const auto tag :
       {LiteralTag::kGetter, LiteralTag::kSetter, LiteralTag::kGeneratorMethod,
        LiteralTag::kAsyncGeneratorMethod})
    Run("method", 1, tag, false);
}
