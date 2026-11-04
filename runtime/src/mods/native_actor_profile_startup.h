// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace native_actor_profiles::startup {
// Host-service discovery runs while a library initializes. A refused library
// must not enable profile hooks; prior accepted imports remain enabled.
class ImportAttempt {
    bool& imported_;bool before_,accepted_=false;
public:
    explicit ImportAttempt(bool& imported):imported_(imported),before_(imported){}
    ~ImportAttempt(){if(!accepted_)imported_=before_;}
    void accept(){accepted_=true;}
};
class EntryScope {
    bool& allowed_;bool before_;
public:
    EntryScope(bool& allowed,bool game_main):allowed_(allowed),before_(allowed){allowed_=game_main;}
    ~EntryScope(){allowed_=before_;}
};
}
