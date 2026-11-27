// D-sweep (dsweep.cpp): the object archives the resource control holds (issue #703).
//
// ResourceControlHD::mArchiveInfo (+0x14) is a sead TreeMapPool: a red-black tree (root +0) whose 128 nodes are
// stored right after the pool (+0x14 from it, 0x20 bytes each: left +0, right +4, the archive info +0x18). The
// sweep looked archives up by scanning that storage. A released archive's node goes back to the pool's free list
// (TreeMapNode::erase: only its left link is rewritten) and keeps the info pointer, while the info, its archive
// heaps and every model made from it are freed. So an archive the sweep had released itself was still "there" when
// its name was looked up again before another load took the node: since #686's scene guard queues a dropped archive
// again, that happens after the title's idle intro. The sweep then took it for one the game holds ("held by the
// game: later"), and on its next turn swept its freed models: a read of 0x28 in ShapePacket::submitShape (the
// shader program's state, cleared by its destructor), Vtil:vtil1.bdl in #703. Only the tree's nodes are held
// archives. WWHD_DSWEEP_ARCHIVE_SLOTS=1 scans the storage as before.
#pragma once
#include <cstdint>
#include <vector>

namespace dsweep_archives {

constexpr uint32_t kLeft = 0, kRight = 4, kValue = 0x18;  // ResourceNode
constexpr uint32_t kNodeSize = 0x20, kNodes = 128;        // ControlResourcePoolHD::mNodes
constexpr int kMaxDepth = 40;                              // a red-black tree of 128 nodes is far shallower

// the values of the nodes reachable from root (the held archives' infos); load(a) reads a guest word, node(a)
// tells whether a node can be read there
template <class Load, class Readable>
void tree_values(uint32_t root, const Load& load, const Readable& node, std::vector<uint32_t>& out, int depth = 0) {
    if (!node(root) || depth > kMaxDepth || out.size() >= kNodes) return;
    out.push_back(load(root + kValue));
    tree_values(load(root + kLeft), load, node, out, depth + 1);
    tree_values(load(root + kRight), load, node, out, depth + 1);
}
// the values of every node of the storage, free ones too (the old lookup)
template <class Load>
std::vector<uint32_t> storage_values(uint32_t storage, const Load& load) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < kNodes; ++i) out.push_back(load(storage + i * kNodeSize + kValue));
    return out;
}

}  // namespace dsweep_archives
