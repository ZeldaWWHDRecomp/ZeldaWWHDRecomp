// Kept lowered states (decomp build, fast mode, WWHD_NATIVE_DRAW): included by gfx/vulkan/draw.cpp after its packet
// state (LoweredKind, LoweredPacket) and the register journal check (packet_regs_restored), not compiled anywhere else.
//
// WWHD_NATIVE_DRAW_KEEP=N (0, the default: off; at most 8): a packet kind keeps up to N earlier lowered states of the
// current frame, submission and pass next to the one it uses. A site whose draws switch between a few states with
// GX2 calls of its own between the instances (a particle renderer binding another emitter's texture, then the first
// one's again) lowers today at every switch back, because the register file no longer holds what the kind was
// lowered with. With kept states, an instance whose kind's current state fails the journal check first tries the
// kept ones: a kept state whose journal check passes (every register written since its lowering holds the value it
// was lowered with again, the epoch bumps all journalled) and whose frame, submission, pass, command buffer and
// surface epoch are the current ones is exactly what a lowering now would produce, so it becomes the kind's state
// and the instance replays (or batches) with it. The check is the replay's own (packet_regs_restored, the same
// validity fields), so the pictures are the replays' pictures: exact by construction.
// A lowering keeps the state it replaces (when it is still of this frame, submission and pass); a new chain (another
// draw in between) ends the kept states with the kinds' states. Open batches that hold the kind are drawn before a
// state is swapped (they read the kind's state when drawn).
#pragma once

constexpr uint32_t kKeptMax = 8;
inline uint32_t kept_states() {
  static const uint32_t n = [] {
    const char* e = std::getenv("WWHD_NATIVE_DRAW_KEEP");
    const uint32_t v = e && *e ? std::min<uint32_t>(uint32_t(std::strtoul(e, nullptr, 10)), kKeptMax) : 0;
    LOG("[native-draw] kept lowered states per kind: %u (WWHD_NATIVE_DRAW_KEEP)", v);
    return v;
  }();
  return n;
}
struct KeptStates {
  std::array<std::unique_ptr<LoweredKind>, kKeptMax> slot;
  uint32_t next = 0;
};
inline std::array<std::array<KeptStates, native_draw::kMaxKinds>, native_draw::kMaxPackets>& kept_table() {
  static auto* t = new std::array<std::array<KeptStates, native_draw::kMaxKinds>, native_draw::kMaxPackets>();
  return *t;
}
inline KeptStates& kept_of(uint32_t id, uint32_t kind) {
  return kept_table()[id % native_draw::kMaxPackets][kind % native_draw::kMaxKinds];
}
// a lowered state's fields swap; the kind's history (the fields kept across lowerings) stays with the kind
inline void kept_swap(LoweredKind& a, LoweredKind& b) {
  std::swap(a, b);
  std::swap(a.loggedMdi, b.loggedMdi);
  std::swap(a.loggedBatch, b.loggedBatch);
  std::swap(a.lastBlockAddr, b.lastBlockAddr);
  std::swap(a.lastBlockSize, b.lastBlockSize);
  std::swap(a.lastSeen, b.lastSeen);
  std::swap(a.blocksVary, b.blocksVary);
  std::swap(a.rangeVaries, b.rangeVaries);
}
inline bool kept_current(const LoweredKind& k) {
  return k.valid && k.submission == R.submissionGeneration && k.frame == R.frame && k.passCount == R.renderPassCount &&
         R.rendering && R.passTracked && g_surface_epoch == k.surfaceEpoch && R.recording && k.cmd == R.cmd;
}
// the kind's state failed the journal check: a kept state that passes it becomes the kind's state (true)
inline bool kept_take(LoweredPacket& P, LoweredKind& L, const native_draw::Packet& pk, uint32_t kind, const uint32_t* r) {
  auto& K = kept_of(pk.id, kind);
  for (auto& s : K.slot) {
    if (!s || !kept_current(*s)) continue;
    if (s->epoch != native_draw::g_epoch && !packet_regs_restored(*s, pk, r)) continue;
    if (pendingBatch.active) native_draw::flush_batch();  // (an open batch draws with the kind's state)
    if (mdiBatch.active) native_draw::flush_mdi();
    kept_swap(L, *s);
    P.boundKind = -1;  // (another pipeline and index buffer: the replay binds them)
    P.mdiKind = -1;
    return true;
  }
  return false;
}
// a lowering of the kind comes: its current state, if still of this frame, submission and pass, is kept
inline void kept_store(LoweredKind& L, const native_draw::Packet& pk, uint32_t kind) {
  if (!kept_current(L)) return;
  auto& K = kept_of(pk.id, kind);
  const uint32_t n = kept_states();
  auto& s = K.slot[K.next++ % n];
  if (!s) s = std::make_unique<LoweredKind>();
  if (pendingBatch.active) native_draw::flush_batch();
  if (mdiBatch.active) native_draw::flush_mdi();
  kept_swap(L, *s);  // (L now holds the slot's old state, which the lowering replaces)
  L.valid = false;
}
// a new chain: the kept states of the packet end with its kinds' states
inline void kept_end(uint32_t id) {
  for (auto& K : kept_table()[id % native_draw::kMaxPackets])
    for (auto& s : K.slot)
      if (s) s->valid = false;
}
// chained(): the packet's own target writes move the kept states' surface epoch as the kinds'
inline void kept_chain(uint32_t id, uint64_t before, uint64_t now) {
  for (auto& K : kept_table()[id % native_draw::kMaxPackets])
    for (auto& s : K.slot)
      if (s && s->valid && s->surfaceEpoch == before) s->surfaceEpoch = now;
}
