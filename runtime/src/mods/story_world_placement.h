// Conservative placement queries. Native resource data stays on the player's machine.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace story_world_placement {
struct Point {float x=0,y=0,z=0;};
struct Bounds {Point minimum,maximum;};
struct ProtectedBounds {std::string stage;int room=-1;std::string kind,id;Bounds bounds;};
struct ProtectedRoute {std::string stage;int room=-1;std::string id;std::vector<Point> points;float radius=0;bool closed=false;};
struct Coverage {std::string stage;int room=-1;bool collision=false,routes=false,events=false;std::string reason;};
struct CoverageRegion {std::string stage;int room=-1;Bounds bounds;};
struct Index {std::vector<ProtectedBounds> bounds;std::vector<ProtectedRoute> routes;std::vector<Coverage> coverage;std::vector<CoverageRegion> regions;};
struct Conflict {std::string kind,id,message;};
bool valid(const Bounds&);
// Degrees, with the native actor's RY * RX * RZ rotation order.
bool transform_bounds(const Bounds&,Point position,Point rotation,Point scale,Bounds&);
std::vector<Conflict> check(const std::string& stage,int room,const Bounds&,const Index&,float clearance=100);
struct SweepResult {Point allowed_delta;std::vector<Conflict> conflicts;bool blocked=false;};
// Continuous horizontal motion of a fixed conservative wave/tilt envelope.
// A nonzero vertical delta is refused. Earliest contact stops the blocked axis;
// a second continuous query permits sliding along an unblocked axis. Unknown
// coverage, initial overlap and malformed dynamic obstacles stop all movement.
SweepResult sweep(const std::string& stage,int room,const Bounds& start,Point desired_delta,const Index&,float clearance=100);
// Bounded readers of original stage resources. Invalid or unsupported input never grants coverage.
// `world_position` and `world_yaw` are the room's placement from its stage MULT entry.
bool add_native_collision(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzb,Point world_position={},float world_yaw=0,std::string* why=nullptr);
bool add_native_layout(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzr,Point world_position={},float world_yaw=0,std::string* why=nullptr);
// Load loose or packed stage archives and their BFRES embedded DZB/DZR files.
// Never writes, exports or caches native bytes. A returned index may have incomplete coverage.
Index load_native_room(const std::string& game_directory,const std::string& stage,int room);
}
