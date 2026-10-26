#include "../../../src/source/protocol_rebase.h"
#include "../../support/analysis/ssa_fixture.h"
#include <bit>

using namespace abcd;
using namespace abcd::source;
using test::Check;

int main() {
  for (unsigned n : {20u, 200u, 201u, 400u, 800u}) {
    std::vector<std::uint8_t> bytes(n, static_cast<std::uint8_t>(abc::Opcode::kNOP));
    auto before = test::Body(bytes);
    auto after = test::Body(std::span(bytes).subspan(2));
    std::vector<abc::CodeOffset> origins, kept;
    for (unsigned i = 0; i < n; ++i) {
      origins.push_back({100 + i});
      if (i >= 2) kept.push_back({100 + i});
    }
    std::vector<FoldRange> folds{{{0}, {2}}};
    analysis::AwaitRegion original{};
    original.await = {104}; original.mode = {106};
    // Other raw protocol instructions were removed by the earlier owner.
    original.suspend = original.resume = original.result_store = {99};
    original.mode_store = original.branch = original.reject = original.continuation = {99};
    std::vector<analysis::AwaitSite> awaits{{{4}, original, {6}}};
    analysis::YieldRegion yielded{};
    yielded.create = {108}; yielded.mode = {110};
    yielded.suspend = yielded.resume = yielded.result_store = yielded.mode_store = {99};
    yielded.continuation = yielded.branch = yielded.reject = {99};
    yielded.return_route = analysis::YieldReturnRoute{{112}, {113}, {114}, abc::CodeOffset{115}};
    std::vector<analysis::YieldSite> yields{{{8}, abc::CodeOffset{10}, yielded}};
    const auto view = [&] {
      return ProtocolRebaseInput{before, after, origins, kept, folds, awaits, yields,
                                abc::ProfileId::kDynamic1301};
    };
    const auto built = RebaseProtocols(view());
    Check(std::holds_alternative<RebasedProtocols>(built));
    const auto proof = std::get<RebasedProtocols>(built);
    Check(proof.awaits[0].pc.value == 2 && proof.awaits[0].mode_pc.value == 4);
    Check(proof.yields[0].pc.value == 6 && proof.yields[0].mode_pc->value == 8);
    Check(proof.awaits[0].origin == original && proof.yields[0].origin == yielded);
    Check(std::holds_alternative<std::uint64_t>(VerifyRebasedProtocols(view(), proof)));
    auto bad = proof;
    bad.awaits[0].mode_pc = {3};
    Check(std::get<ProtocolRebaseError>(VerifyRebasedProtocols(view(), bad)) == ProtocolRebaseError::kAnchor);
    bad = proof; bad.yields[0].origin.return_route->cleanup = {114};
    Check(std::get<ProtocolRebaseError>(VerifyRebasedProtocols(view(), bad)) == ProtocolRebaseError::kAnchor);
    awaits[0].mode_pc = {7};
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view())) == ProtocolRebaseError::kAnchor);
    awaits[0].mode_pc = {6};
    // A certificate cannot claim an instruction which the fold removes, even
    // when its replacement offset would numerically be another valid anchor.
    awaits[0].pc = {1};
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view())) == ProtocolRebaseError::kAnchor);
    awaits[0].pc = {4};
    yields[0].pc = {4}; yields[0].mode_pc = abc::CodeOffset{6};
    yields[0].origin.create = {104}; yields[0].origin.mode = {106};
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view())) == ProtocolRebaseError::kCollision);
    yields[0].pc = {8}; yields[0].mode_pc = abc::CodeOffset{10};
    yields[0].origin = yielded;
    // Fold boundaries cannot split an otherwise retained suspend/mode pair.
    folds.push_back({{5}, {6}});
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view())) == ProtocolRebaseError::kAnchor);
    folds.pop_back();
    // Both selected handlers exist, but a changed destination is not the same
    // catch snapshot. Dispatch is compared via provenance, not renumbered IDs.
    before.tries = {{{4}, 3, 0, 1}}; before.handlers = {{{}, {16}, 0}};
    after.tries = {{{2}, 3, 0, 1}}; after.handlers = {{{}, {15}, 0}};
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view())) == ProtocolRebaseError::kDispatch);
    after.handlers[0].offset = {14};
    Check(std::holds_alternative<RebasedProtocols>(RebaseProtocols(view())));
    before.tries.clear(); before.handlers.clear(); after.tries.clear(); after.handlers.clear();
    Check(std::holds_alternative<RebasedProtocols>(RebaseProtocols(view(), {proof.allocated_bytes, proof.work})));
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view(), {proof.allocated_bytes - 1, proof.work})) == ProtocolRebaseError::kResourceLimit);
    Check(std::get<ProtocolRebaseError>(RebaseProtocols(view(), {proof.allocated_bytes, proof.work - 1})) == ProtocolRebaseError::kWorkLimit);
    Check(proof.work < 600 * n * (1 + std::bit_width(n)));
  }
}
