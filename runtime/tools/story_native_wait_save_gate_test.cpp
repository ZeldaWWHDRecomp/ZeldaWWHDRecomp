// SPDX-License-Identifier: MPL-2.0
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#undef STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "mods/story_native_wait_save_gate.h"
#include "state_memory.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <vector>
#include <cstdint>

namespace A = story_native_event_arena;
namespace J = story_native_wait_journal;
namespace G = story_native_wait_save_gate;

int main() {
    (void)covered(); // fixture helper retained by the nested test include
    const auto layout = A::make(0x6000F000);
    J::Record clean{9, 1, layout.allocation.address, J::Phase::Unpublished};
    clean.definition[0] = 0xDA;
    std::array<uint8_t, J::kBytes> header{};
    assert(J::encode(clean, header));

    std::vector<uint8_t> snapshot(0x20000);
    std::copy(header.begin(), header.end(), snapshot.begin() + 0xF000);
    ss::Writer encoded;
    const std::vector<ss::MemoryRegion> source_regions{{0x60000000, 0x20000}};
    auto capture = [&] {
        encoded = {};
        ss::capture_regions(encoded, source_regions,
            [&](uint32_t address) { return snapshot.data() + address - 0x60000000; },
            [](const uint8_t*, size_t) { return true; });
    };
    capture();
    ss::Reader memory(encoded.b.data(), encoded.b.size());
    std::vector<ss::MemoryRegion> regions;
    ss::MemoryChunks chunks;
    assert(ss::read_regions(memory, regions, chunks));
    ss::Writer allocations;
    allocations.u32(layout.allocation.address + A::kBytes);
    allocations.u32(1);
    allocations.pod(mem::AllocRec{layout.allocation.address, A::kBytes, 123});
    auto view = [&](const ss::MemoryChunks& c) {
        return ss::native_gate::SavedView{
            ss::Reader(allocations.b.data(), allocations.b.size()), regions, c};
    };
    assert(G::saved_bootstrap(layout, view(chunks)) == clean);

    // The preflight uses snapshot chunks, not the mutable capture source.
    std::fill(snapshot.begin(), snapshot.end(), 0xA5);
    assert(G::saved_bootstrap(layout, view(chunks)) == clean);

    auto corrupted = chunks;
    std::vector<uint8_t> dirty(chunks.at(0x60000000),
                               chunks.at(0x60000000) + ss::kMemoryChunk);
    dirty[0xF180] = 1; // image outside the journal header
    corrupted[0x60000000] = dirty.data();
    assert(!G::saved_bootstrap(layout, view(corrupted)));

    J::Record active = clean;
    active.phase = J::Phase::Preparing;
    assert(J::encode(active, header));
    std::copy(header.begin(), header.end(), dirty.begin() + 0xF000);
    dirty[0xF180] = 0;
    assert(!G::saved_bootstrap(layout, view(corrupted)));

    assert(!G::saved_bootstrap(layout, view({})));
    allocations.b[4] = 0; // no exact arena allocation row
    assert(!G::saved_bootstrap(layout, view(chunks)));

    auto* guest = map_arena();
    {
        Readiness fixture;
        S::Session session(request(), fixture.source());
        assert(session.startup());
        G::Controller gate(session);
        {
            auto held = gate.begin_save();
            assert(held && !gate.begin_save() && !gate.begin_progress_load());
        }
        auto held = session.try_fence();
        assert(held);
        auto live = session.journal();
        assert(live && session.unexposed());
        held.reset();

        // A full load is admitted only for the exact never-exposed bootstrap
        // record and sparse snapshot arena. The retained Hold blocks a second
        // entry, and restored candidate admission stays deferred.
        assert(J::encode(*live, header));
        std::fill(snapshot.begin(), snapshot.end(), 0);
        std::copy(header.begin(), header.end(), snapshot.begin() + 0x10000);
        capture();
        memory = ss::Reader(encoded.b.data(), encoded.b.size());
        regions.clear();
        chunks.clear();
        assert(ss::read_regions(memory, regions, chunks));
        ss::Writer live_allocations;
        live_allocations.u32(arena_at + A::kBytes);
        live_allocations.u32(1);
        live_allocations.pod(mem::AllocRec{arena_at, A::kBytes, 123});
        auto live_view = [&](const ss::MemoryChunks& c) {
            return ss::native_gate::SavedView{
                ss::Reader(live_allocations.b.data(), live_allocations.b.size()), regions, c};
        };
        {
            auto load = gate.begin_load(live_view(chunks));
            assert(load && !gate.begin_save() && !gate.begin_load(live_view(chunks)));
            assert(gate.finish_load(live_view(chunks)));
        }
        assert(gate.begin_save());

        auto corrupt_chunks = chunks;
        std::vector<uint8_t> dirty_snapshot(chunks.at(0x60010000),
                                            chunks.at(0x60010000) + ss::kMemoryChunk);
        dirty_snapshot[0x180] = 1;
        corrupt_chunks[0x60010000] = dirty_snapshot.data();
        assert(!gate.begin_load(live_view(corrupt_chunks)));
        assert(gate.begin_save());
        {
            auto progress = gate.begin_progress_load();
            assert(progress && !gate.begin_save());
        }
        assert(!gate.begin_save() && !gate.begin_load(live_view(chunks)) &&
               !gate.begin_progress_load());
    }
    std::fill_n(guest, A::kBytes, 0);
    {
        Readiness fixture;
        S::Session session(request(), fixture.source());
        assert(session.startup());
        G::Controller gate(session);
        auto held = session.try_fence();
        assert(held);
        const auto live = session.journal();
        assert(live);
        held.reset();
        ss::Writer live_allocations;
        live_allocations.u32(arena_at + A::kBytes);
        live_allocations.u32(1);
        live_allocations.pod(mem::AllocRec{arena_at, A::kBytes, 123});
        auto live_view = [&](const ss::MemoryChunks& c) {
            return ss::native_gate::SavedView{
                ss::Reader(live_allocations.b.data(), live_allocations.b.size()), regions, c};
        };
        // The previous session's otherwise clean image has a different host
        // generation and cannot be loaded into this fresh session.
        assert(!gate.begin_load(live_view(chunks)));
        assert(J::encode(*live, header));
        std::fill(snapshot.begin(), snapshot.end(), 0);
        std::copy(header.begin(), header.end(), snapshot.begin() + 0x10000);
        capture();
        memory = ss::Reader(encoded.b.data(), encoded.b.size());
        regions.clear();
        chunks.clear();
        assert(ss::read_regions(memory, regions, chunks));
        {
            auto load = gate.begin_load(live_view(chunks));
            assert(load);
            guest[J::kBytes + 1] = 1; // simulate a corrupt restored arena
            assert(!gate.finish_load(live_view(chunks)));
        }
        assert(!gate.begin_save() && !gate.begin_load(live_view(chunks)));
    }
    std::fill_n(guest, A::kBytes, 0);
    {
        Readiness fixture;
        S::Session session(request(), fixture.source());assert(session.startup());
        G::Controller gate(session);assert(gate.begin_save());
        Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));
        {auto held=session.try_fence();assert(held&&session.stage_image()&&session.staged_expected());}
        // A durable private Preparing payload is outside the clean-bootstrap
        // save contract, even though no native consumer has been published.
        const ss::native_gate::SavedView denied{ss::Reader(header.data(),header.size()),regions,chunks};
        assert(!gate.begin_save()&&!gate.begin_load(denied)&&!gate.begin_progress_load());
    }
    unmap_arena(guest);
}
