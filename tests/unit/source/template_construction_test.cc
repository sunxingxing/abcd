#include "../../../src/source/template_construction.h"

#include <cstdlib>
#include <iostream>

#include "../../../src/source/original_flow.h"
#include "../../../src/source/template_literal.h"
#include "../../../src/source/template_tree.h"
#include "abcd/analysis/semantic_ir.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool value) {
  if (!value) std::abort();
}
struct Fixture {
  ValueProgram g;
  std::vector<ValueId> aliases;
};
Fixture Make(std::uint32_t count) {
  Fixture f;
  auto& g = f.g;
  g.blocks.resize(1);
  g.values.push_back(
      {EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  auto add = [&](ValueOperation operation,
                 std::vector<std::pair<OperandRole, ValueOperand>> inputs,
                 bool result, LanguageEffect effects = LanguageEffect::kNone,
                 bool throws = false) {
    OpId id{static_cast<std::uint32_t>(g.ops.size())};
    UseId first{static_cast<std::uint32_t>(g.uses.size())};
    std::optional<ValueId> value;
    if (result) {
      value = ValueId{static_cast<std::uint32_t>(g.values.size())};
      g.values.push_back({OpResult{id, {}},
                          std::holds_alternative<ValueString>(operation)
                              ? ValueKinds::String()
                              : ValueKinds::Language(),
                          {}});
    }
    for (const auto& [role, input] : inputs) {
      UseId use{static_cast<std::uint32_t>(g.uses.size())};
      g.uses.push_back({id, role, ReadPurpose::kOperand, input});
      if (const auto* v = std::get_if<ValueId>(&input)) {
        auto old = g.values[v->value].first_use;
        g.uses.back().next = old;
        if (old) g.uses[old->value].previous = use;
        g.values[v->value].first_use = use;
      }
    }
    g.origins.push_back({{id.value}, {id.value}, {}});
    g.ops.push_back({operation,
                     {0},
                     g.blocks[0].op_count++,
                     first,
                     static_cast<std::uint32_t>(inputs.size()),
                     value,
                     effects,
                     effects,
                     throws ? ExceptionalCompletion::kMayThrow
                            : ExceptionalCompletion::kImpossible,
                     {id.value}});
    if (throws) g.exceptional_exits.push_back({id, {id.value}, effects, {}});
    return value;
  };
  const auto effects = LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite |
                       LanguageEffect::kUserCode | LanguageEffect::kAllocation;
  const auto raw = *add(ValueArrayAllocation{}, {}, true,
                        LanguageEffect::kAllocation),
             cooked = *add(ValueArrayAllocation{}, {}, true,
                           LanguageEffect::kAllocation);
  const auto define = [&](ValueId array, std::uint32_t index,
                          ValueOperand value) {
    add(ValueComputedOwnWrite{PropertyNameSetting::kNone,
                              OwnWriteKind::kDefineField},
        {{OperandRole::kBase, array},
         {OperandRole::kKey,
          analysis::IntegerLiteral{static_cast<std::int32_t>(index)}},
         {OperandRole::kStoredValue, value}},
        false, effects, true);
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto r = *add(ValueString{abc::PoolIndex{i * 2}}, {}, true);
    define(raw, i, r);
    const auto c = *add(ValueString{abc::PoolIndex{i * 2 + 1}}, {}, true);
    define(cooked, i, c);
  }
  const auto carrier =
      *add(ValueArrayAllocation{}, {}, true, LanguageEffect::kAllocation);
  define(carrier, 0, raw);
  define(carrier, 1, cooked);
  const auto object =
      *add(ValueUnary{UnaryOperator::kTemplateObject},
           {{OperandRole::kInput, carrier}}, true, effects, true);
  std::vector<std::pair<OperandRole, ValueOperand>> args{
      {OperandRole::kCallee, ValueId{0}}, {OperandRole::kArgument, object}};
  for (std::uint32_t i = 1; i < count; ++i)
    args.push_back({OperandRole::kArgument,
                    analysis::IntegerLiteral{static_cast<std::int32_t>(i)}});
  const auto value =
      *add(ValueInvoke{InvokeReceiver::kUndefined, count}, args, true,
           LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite |
               LanguageEffect::kUserCode,
           true);
  add(ValueReturn{}, {{OperandRole::kReturnValue, value}}, false);
  f.aliases.resize(g.values.size());
  for (std::uint32_t i = 0; i < f.aliases.size(); ++i) f.aliases[i] = {i};
  return f;
}
bool Proves(const Fixture& f, SemanticBudget grant = {}) {
  try {
    TreeCheckBudget b(grant);
    return BuildTemplateConstructions(f.g, f.aliases,
                                      SourceContract::kLanguageSourceV7, b)
               .sites.size() == 1;
  } catch (TreeStructureError) {
    return false;
  }
}
Fixture Phased(Fixture f) {
  auto& g = f.g;
  const auto count = g.ops.size();
  g.blocks.clear();
  g.edges.clear();
  for (std::uint32_t i = 0; i < count; ++i) {
    auto& op = g.ops[i];
    op.block = {2 * i};
    op.order = 0;
    if (op.result)
      std::get<OpResult>(g.values[op.result->value].origin).commit =
          WorkBlockId{2 * i + 1};
    auto e = static_cast<std::uint32_t>(g.edges.size());
    g.edges.push_back({{2 * i}, {2 * i + 1}, FlowEdgeKind::kFallthrough, 0});
    g.blocks.push_back({{i}, 1, {e}, 1});
    g.phases.push_back({{2 * i}, {i}, ExceptionPhase::kBefore, OpId{i}});
    e = g.edges.size();
    if (i + 1 < count)
      g.edges.push_back(
          {{2 * i + 1}, {2 * (i + 1)}, FlowEdgeKind::kFallthrough, 0});
    g.blocks.push_back(
        {{i + 1}, 0, {e}, static_cast<std::uint8_t>(i + 1 < count)});
    g.phases.push_back({{2 * i + 1}, {i}, ExceptionPhase::kCommit, OpId{i}});
  }
  return f;
}
}  // namespace
int main() {
  for (bool wide : {false, true}) {
    const auto op = wide ? abc::Opcode::kGETTEMPLATEOBJECT_IMM16
                         : abc::Opcode::kGETTEMPLATEOBJECT_IMM8;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(op), 44};
    if (wide) bytes.push_back(1);
    auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic2400));
    const auto semantic = std::get<SemanticInstruction>(LiftSemanticInstruction(
        {{0}, decoded}, abc::ProfileId::kDynamic2400, 3));
    const auto& unary = std::get<UnaryExpression>(
        std::get<ScalarAssignment>(semantic.operation).expression);
    Check(unary.op == UnaryOperator::kTemplateObject);
    Check(semantic.effects.exceptional == ExceptionalCompletion::kMayThrow);
    decoded.operands[0].kind = abc::OperandKind::kRegister;
    Check(std::holds_alternative<SemanticFailure>(LiftSemanticInstruction(
        {{0}, decoded}, abc::ProfileId::kDynamic2400, 3)));
  }
  {
    FunctionTree tree;
    tree.identity.contract = SourceContract::kLanguageSourceV7;
    tree.texts = {u"a", u"b"};
    auto quasis = std::make_shared<std::vector<TemplateQuasi>>(
        std::initializer_list<TemplateQuasi>{{{0}, TextId{0}},
                                             {{1}, TextId{1}}});
    tree.expressions.push_back({GlobalNameExpr{{0}}, {0}});
    tree.expressions.push_back({TemplateObjectExpr{quasis}, {0}});
    tree.expressions.push_back({TreeLiteral{source::IntegerLiteral{3}}, {0}});
    tree.arguments = {{1}, {2}};
    tree.expressions.push_back(
        {CallExpr{{0}, {0, 2}, CallReference::kValue, {}}, {0}});
    const auto valid = [&]() {
      try {
        TreeCheckBudget b({});
        VerifyTemplateCalls(tree, b);
        return true;
      } catch (TreeStructureError) {
        return false;
      }
    };
    Check(valid());
    (*quasis)[0].cooked = TextId{1};
    Check(!valid());
    (*quasis)[0].cooked = TextId{0};
    std::swap(tree.arguments[0], tree.arguments[1]);
    Check(!valid());
    std::swap(tree.arguments[0], tree.arguments[1]);
    tree.expressions.push_back(tree.expressions.back());
    Check(!valid());
    tree.expressions.pop_back();
    std::get<CallExpr>(tree.expressions.back().expression).arguments.count = 1;
    Check(!valid());
  }

  {
    const auto f = Make(1);
    TreeCheckBudget budget({});
    auto retained = std::make_shared<OriginalTemplates>();
    retained->input = f.g;
    retained->aliases = f.aliases;
    retained->proof = BuildTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, budget);
    retained->texts = {
        TemplateSiteText{{TemplateQuasiText{u"a", std::u16string{u"a"}}}}};
    FunctionTree tree;
    tree.identity.contract = SourceContract::kLanguageSourceV7;
    tree.texts = {u"a"};
    tree.expressions = {
        {GlobalNameExpr{{0}}, {0}},
        {TemplateObjectExpr{std::make_shared<const std::vector<TemplateQuasi>>(
             std::initializer_list<TemplateQuasi>{{{0}, TextId{0}}})},
         {0}},
        {CallExpr{{0}, {0, 1}, CallReference::kValue, {}}, {0}}};
    tree.arguments = {{1}};
    OriginalFlow original;
    original.templates = retained;
    original.value_revision = f.g.revision;
    original.cfg_revision = f.g.cfg_revision;
    original.blocks = f.g.blocks;
    original.edges = f.g.edges;
    original.uses = f.g.uses;
    original.exceptional_exits = f.g.exceptional_exits;
    for (const auto& op : f.g.ops)
      original.operations.push_back({op.block, op.first_use, op.use_count,
                                     op.result, op.normal, op.before_throw,
                                     op.exceptional, op.life, op.origin});
    const auto& site = retained->proof.sites.front();
    original.operations[site.operation.value].intrinsic_expression = ExprId{1};
    original.operations[site.call.value].intrinsic_expression = ExprId{2};
    const auto valid = [&]() {
      try {
        TreeCheckBudget b({});
        VerifyOriginalTemplates(tree, original, b);
        return true;
      } catch (TreeStructureError) {
        return false;
      }
    };
    Check(valid());
    tree.texts[0] = u"z";
    Check(!valid());
    tree.texts[0] = u"a";
    original.exceptional_exits.pop_back();
    Check(!valid());
    original.exceptional_exits = f.g.exceptional_exits;
    original.uses[0].role = OperandRole::kCallee;
    Check(!valid());
    original.uses = f.g.uses;
    retained->proof.sites[0].exceptions.pop_back();
    Check(!valid());
  }
  // Check both quasi counts and the requested substitution counts (Q = S + 1).
  for (auto count : {1U, 200U, 201U, 202U, 400U, 401U, 800U, 801U}) {
    const auto f = Make(count);
    Check(Proves(f));
    Check(Proves(Phased(f)));
    TreeCheckBudget b({});
    const auto proof = BuildTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, b);
    Check(proof.sites[0].quasis.size() == count);
    TreeCheckBudget verify({});
    Check(VerifyTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, proof, verify));
    auto broken = proof;
    broken.sites[0].quasis[0].raw.value++;
    TreeCheckBudget v({});
    Check(!VerifyTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, broken, v));
    broken = proof;
    broken.sites[0].events.pop_back();
    TreeCheckBudget omitted({});
    Check(!VerifyTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, broken, omitted));
    broken = proof;
    broken.sites[0].exceptions[0].handler =
        ValueHandler{{{0}, {0}, {123}}, {0}};
    TreeCheckBudget caught({});
    Check(!VerifyTemplateConstructions(
        f.g, f.aliases, SourceContract::kLanguageSourceV7, broken, caught));
    auto wrong = f;
    wrong.g.exceptional_exits.erase(wrong.g.exceptional_exits.begin());
    Check(!Proves(wrong));
    wrong = f;
    wrong.g.uses[wrong.g.ops[3].first_use.value + 1].operand =
        analysis::IntegerLiteral{10};
    Check(!Proves(wrong));
    wrong = f;
    wrong.g.uses[wrong.g.ops.back().first_use.value].operand =
        *wrong.g.ops[0].result;
    Check(!Proves(wrong));
    wrong = f;
    wrong.g.ops[3].operation = ValuePropertyWrite{};
    Check(!Proves(wrong));
    for (bool bytes : {false, true}) {
      std::uint64_t lo = 0, hi = 100000000;
      while (lo < hi) {
        const auto mid = (lo + hi) / 2;
        SemanticBudget grant{};
        if (bytes)
          grant.bytes = mid;
        else
          grant.work = mid;
        if (Proves(f, grant))
          hi = mid;
        else
          lo = mid + 1;
      }
      SemanticBudget exact{};
      if (bytes)
        exact.bytes = lo;
      else
        exact.work = lo;
      Check(Proves(f, exact));
      std::cout << count << (bytes ? " bytes=" : " work=") << lo << "\n";
      if (bytes)
        --exact.bytes;
      else
        --exact.work;
      Check(!Proves(f, exact));
    }
  }
  for (auto length : {200U, 201U, 400U, 800U}) {
    std::u16string raw(length, u'a');
    TreeCheckBudget b({});
    Check(CookTemplateRaw(raw, b) == raw);
    const auto bytes = b.Bytes();
    const auto work = b.Work();
    TreeCheckBudget exact({bytes, work});
    Check(CookTemplateRaw(raw, exact) == raw);
    try {
      TreeCheckBudget short_bytes({bytes - 1, work});
      (void)CookTemplateRaw(raw, short_bytes);
      Check(false);
    } catch (TreeStructureError e) {
      Check(e == TreeStructureError::kResourceLimit);
    }
    try {
      TreeCheckBudget short_work({bytes, work - 1});
      (void)CookTemplateRaw(raw, short_work);
      Check(false);
    } catch (TreeStructureError e) {
      Check(e == TreeStructureError::kWorkLimit);
    }
  }
  const auto cook = [](std::u16string_view raw) {
    TreeCheckBudget b({});
    return CookTemplateRaw(raw, b);
  };
  Check(cook(u"a\\n\\u0062\\x63\\u{1f600}") == u"a\nbc\U0001f600");
  Check(cook(u"\\`\\${\\\\") == u"`${\\");
  Check(cook(u"a\\\nb") == u"ab");
  Check(cook(u"a\n") == u"a\n");
  for (auto raw :
       {u"\\xG", u"\\u{}", u"\\u{110000}", u"\\8", u"\\01", u"\\u00q0"})
    Check(!cook(raw));
  for (auto raw : {u"`", u"${x}", u"\\", u"\r"}) {
    try {
      (void)cook(raw);
      Check(false);
    } catch (TreeStructureError e) {
      Check(e == TreeStructureError::kUnsupportedSyntax);
    }
  }
  std::cout << "template construction, exception ports, mutations, cooked/raw "
               "and exact budgets passed\n";
}
