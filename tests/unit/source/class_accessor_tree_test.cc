#include "../../../src/source/class_tree.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/family_print.h"
#include "abcd/source/global_reference.h"
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <source_location>
namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok, const std::source_location& at = std::source_location::current()) {
  if (!ok) { std::cerr << "accessor tree check " << at.line() << '\n'; std::abort(); }
}
std::string Recover(bool is_static, bool return_prototype = false, int ordinary_parity = -1) {
  constexpr std::uint32_t kPool = 64, kOwner = 128, kLiteral = 160,
      kCreator = 256, kConstructor = 300, kGetter = 340, kSetter = 380,
      kCreatorCode = 512, kConstructorCode = 768, kGetterCode = 800, kSetterCode = 832,
      kKey = 900, kPrototype = 920, kField = 944;
  std::vector<std::uint8_t> bytes(1024);
  const auto word = [&](std::size_t at, std::uint32_t n) {
    for (unsigned i = 0; i < sizeof(n); ++i) bytes[at + i] = static_cast<std::uint8_t>(n >> (8 * i));
  };
  const std::uint32_t pool[] = {kConstructor, kLiteral, kGetter, kSetter, kKey, kPrototype, kField};
  for (std::size_t i = 0; i < std::size(pool); ++i) word(kPool + i * sizeof(std::uint32_t), pool[i]);
  const auto text = [&](std::size_t at, std::string_view value) {
    bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at);
  };
  text(kKey, "value"); text(kPrototype, "prototype"); text(kField, "_value");
  word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 5, 0); // Empty class buffer, prototype split zero.
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode value) { code.push_back(static_cast<std::uint8_t>(value)); };
  const auto load = [&](std::uint8_t r) { op(abc::Opcode::kLDA_V8); code.push_back(r); };
  const auto save = [&](std::uint8_t r) { op(abc::Opcode::kSTA_V8); code.push_back(r); };
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  code.insert(code.end(), {0, 0, 0, 1, 0, 0, 0, 0}); save(1);
  if (!is_static) { op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); code.insert(code.end(), {0, 5, 0}); }
  save(2);
  // Repeated definitions test ordering and preserving the opposite descriptor
  // half. Every original method occurrence must still have a prepared body.
  for (unsigned i = 0; i < 4; ++i) {
    const bool setter = i % 2;
    op(abc::Opcode::kLDA_STR_ID16); code.insert(code.end(), {4, 0}); save(3);
    load(2); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8);
    code.insert(code.end(), {0, static_cast<std::uint8_t>(setter ? 3 : 2), 0, static_cast<std::uint8_t>(setter)});
    save(setter ? 5 : 4);
    if (static_cast<int>(i % 2) == ordinary_parity) {
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 4, 0, 2});
    } else {
      op(abc::Opcode::kLDUNDEFINED); save(setter ? 4 : 5);
      op(abc::Opcode::kLDFALSE); op(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8);
      code.insert(code.end(), {2, 3, 4, 5});
    }
  }
  // A distinct ordinary object literal in the same graph must still be
  // planned; class method installations must not be claimed as its fields.
  op(abc::Opcode::kCREATEEMPTYOBJECT); save(0); load(3);
  op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 6, 0, 0});
  load(return_prototype ? 2 : 1); op(abc::Opcode::kRETURN);
  // Compact Panda headers: all fixture bodies stay within one ULEB payload.
  Check(code.size() < 128);
  const auto body = [&](std::uint32_t at, std::uint8_t locals, std::uint8_t args, std::vector<std::uint8_t> data) {
    bytes[at] = locals; bytes[at + 1] = args; bytes[at + 2] = static_cast<std::uint8_t>(data.size());
    std::copy(data.begin(), data.end(), bytes.begin() + at + 4);
    return abc::CodeInfo{{at}, {at + 4}, locals, args, static_cast<std::uint32_t>(data.size()), 0};
  };
  const auto opcode = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  abc::MetadataIndex index; index.regions.push_back({kCreator, kCreatorCode, {}, {std::size(pool), kPool}, {}, {}});
  index.methods.resize(4);
  const std::uint32_t offsets[] = {kCreator, kConstructor, kGetter, kSetter};
  const std::string names[] = {"factory", "Accessors", "value", "value"};
  for (std::size_t i = 0; i < 4; ++i) {
    index.methods[i].offset = {offsets[i]}; index.methods[i].owner_offset = {kOwner};
    index.methods[i].name = names[i]; index.methods[i].name_utf16_length = names[i].size();
  }
  index.methods[0].code = body(kCreatorCode, 6, 3, code);
  index.methods[1].code = body(kConstructorCode, 0, 3, {opcode(abc::Opcode::kRETURNUNDEFINED)});
  index.methods[2].code = body(kGetterCode, 0, 3, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 6, 0, opcode(abc::Opcode::kRETURN)});
  index.methods[3].code = body(kSetterCode, 1, 4, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kSTA_V8), 0, opcode(abc::Opcode::kLDA_V8), 4,
      opcode(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 6, 0, 0, opcode(abc::Opcode::kRETURNUNDEFINED)});
  char path[] = "/tmp/abcd-accessor-tree-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path); const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  auto loaded = ReadFunctionValues(file, header, index, {0}, identity, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(loaded)); const auto& creator = std::get<FileFunction>(loaded);
  const auto& graph = creator.values.graph; const auto& definitions = creator.values.definitions.sites;
  Check(definitions.size() == 4);
  auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, analysis::ClosureWrites::kUnknown, definitions);
  if (const auto* error = std::get_if<EvaluationError>(&evaluated)) std::cerr << "evaluation " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<EvaluationPlan>(evaluated)); const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  GlobalScope globals{};
  auto built = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
      definitions.front().creation, identity, &globals, {}, nullptr, definitions);
  if (const auto* error = std::get_if<ClassTreeFailure>(&built)) std::cerr << "class failure " << error->index() << '\n';
  Check(std::holds_alternative<ClassTree>(built));
  auto definition = std::make_shared<ClassTree>(std::get<ClassTree>(std::move(built)));
  auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, {kCreator}, graph));
  constants.operations[definitions.front().creation.value] = definition;
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {kCreator}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = constants.names; syntax.texts = constants.texts;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  auto tree = BuildFunctionTree(graph, control, evaluation, syntax, constants.View());
  if (const auto* error = std::get_if<FunctionTreeError>(&tree)) std::cerr << "tree failure " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<ConstructedFunction>(tree));
  // Independent operand checking must reject removal or substitution of the
  // prepared body behind an otherwise well-formed projection certificate.
  auto missing = std::move(definition->definitions.back()); definition->definitions.pop_back();
  Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
  definition->definitions.push_back(std::move(missing));
  std::ostringstream output;
  auto printed = PrintFunctionTree(std::get<ConstructedFunction>(tree).tree, output, {}, {}, &globals);
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check(std::get<PrintedFunction>(printed).class_members.size() == 5);
  const auto read_family = [&] {
    return std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup, roles, functions, {0}, identity,
        {}, nullptr, nullptr, {}, FamilyClassMode::kRetainRoot));
  };
  const auto& root_tree = std::get<ConstructedFunction>(tree).tree;
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, root_tree));
  auto incomplete = read_family(); incomplete.members[0].classes.pop_back();
  const auto denied_family = BuildRecordFamilyTrees(file, header, index, std::move(incomplete),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  Check(std::holds_alternative<FamilyTreeFailure>(denied_family));
  auto composed = BuildRecordFamilyTrees(file, header, index, read_family(),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  if (const auto* error = std::get_if<FamilyTreeFailure>(&composed)) std::cerr << "family failure " << error->cause.index() << '\n';
  Check(std::holds_alternative<ConstructedFamily>(composed));
  std::ostringstream module;
  auto module_print = PrintModuleFamilyDraft(std::get<ConstructedFamily>(composed), module, {}, {}, &globals);
  Check(std::holds_alternative<PrintedFamilyDraft>(module_print));
  Check(std::get<PrintedFamilyDraft>(module_print).members[0].class_members.size() == 5);
  Check(unlink(path) == 0); return output.str();
}
}
int main(int argc, char** argv) {
  if (argc == 2) { std::cout << Recover(std::string_view(argv[1]) == "--static", std::string_view(argv[1]) == "--retained",
      std::string_view(argv[1]) == "--method-last" ? 1 : std::string_view(argv[1]) == "--accessor-last" ? 0 : -1); return 0; }
  Recover(false); Recover(true); Recover(false, true); Recover(false, false, 0); Recover(false, false, 1);
}
