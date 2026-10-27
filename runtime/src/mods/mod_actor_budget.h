#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "story_game_pieces_budget.h"
namespace mod_actor::budget {
inline constexpr uint64_t ModelLimit=2u*1024*1024,ModLimit=8u*1024*1024,RoomLimit=32u*1024*1024;
// Game piece sets (story_game_pieces.h): a set archive may take up to PieceRoomLimit, and the pieces of a
// room (each set archive once, each placement) stay within it together (the island buildings' budget).
inline constexpr uint64_t PieceRoomLimit=story_game_pieces::kRoomBudgetBytes;
inline constexpr uint32_t BackingReserve=8u*1024*1024,SolidReserve=1024*1024;
// what loading an archive file takes from its heap: its decompressed size (Yaz0 header), rounded as the game's
// buffer is, plus room for the buffer's alignment and the archive's resource records; 0 without a readable file
inline uint64_t archive_need(const std::string& path){
    if(path.empty())return 0;FILE*f=std::fopen(path.c_str(),"rb");if(!f)return 0;
    unsigned char h[8]{};const size_t got=std::fread(h,1,8,f);std::fseek(f,0,SEEK_END);const long size=std::ftell(f);std::fclose(f);
    const uint64_t bytes=got==8&&std::memcmp(h,"Yaz0",4)==0?(uint64_t(h[4])<<24|uint64_t(h[5])<<16|uint64_t(h[6])<<8|h[7]):size>0?uint64_t(size):0;
    return bytes?((bytes+0x1F)&~uint64_t(0x1F))+0x10000:0;
}
inline std::string mib(uint64_t bytes){return std::to_string(bytes/(1024*1024))+" MiB";}
struct Record {std::string mod,archive;int room=0;uint32_t backing=0,resource=0,instance=0,actor=0;bool loaded=false,active=false,piece=false;};
struct Usage {uint64_t room=0,pieces=0;std::map<std::string,uint64_t> mods;};
// Cached backing retains its measured charge until the native archive unloads. Models shared by
// several actor declarations are counted once; each native actor and its solid heap count separately.
inline bool check(const std::vector<Record>& records,int room,Usage& out,std::string& why){
    out={};std::map<std::string,std::pair<std::string,uint64_t>> models;
    std::set<std::string> piece_archives,all_pieces;
    for(const auto& r:records){if(r.loaded||r.active){const uint64_t cost=uint64_t(r.backing)+r.resource;if(cost>(r.piece?PieceRoomLimit:ModelLimit)){why=r.piece?"game piece set exceeds "+mib(PieceRoomLimit)+" of game heap allocations":"mod model exceeds "+mib(ModelLimit)+" of game heap allocations";return false;}auto& model=models[r.archive];model.first=r.mod;model.second=std::max(model.second,cost);if(r.piece)all_pieces.insert(r.archive);if(r.piece&&(r.active?r.room==room:true))piece_archives.insert(r.archive);}
        if(r.active){const uint64_t cost=uint64_t(r.instance)+r.actor;if(!r.piece)out.mods[r.mod]+=cost;if(r.room==room){out.room+=cost;if(r.piece)out.pieces+=cost;}}}
    for(const auto& [archive,model]:models){if(!all_pieces.contains(archive))out.mods[model.first]+=model.second;out.room+=model.second;if(piece_archives.contains(archive))out.pieces+=model.second;}
    if(out.pieces>PieceRoomLimit){why="game pieces in this room exceed "+mib(PieceRoomLimit)+" (each set archive once, each placement)";return false;}
    for(const auto& [mod,bytes]:out.mods)if(bytes>ModLimit){why="mod actor allocations exceed "+mib(ModLimit)+": "+mod;return false;}
    if(out.room>RoomLimit){why="room mod actor allocations exceed "+mib(RoomLimit);return false;}return true;
}
}
