#include "game/asylum/present_sequence.h"
#include <cstdio>
using bvr::aa::native::PresentSequence;
using Fault = PresentSequence::Fault;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#condition); return 1; } } while (0)
int main() {
    PresentSequence sequence;
    // Real A1 failure: L, R, then movie/loading Presents WITHOUT another Begin.
    for (uint32_t generation=10; generation<14; ++generation) {
        sequence.begin(-1,generation);
        auto left=sequence.present(true);
        CHECK(left.eye==-1 && left.generation==generation && left.fault==Fault::None);
        sequence.begin(1,generation);
        auto right=sequence.present(true);
        CHECK(right.eye==1 && right.completedPair && right.fault==Fault::None);
        for (int extra=0; extra<3; ++extra) {
            auto untagged=sequence.present(true);
            CHECK(untagged.eye==0 && untagged.generation==0 && !untagged.completedPair && untagged.fault==Fault::None);
        }
    }
    // Begin may execute before the game-thread ctor validates the view.
    sequence.begin(-1,30);
    CHECK(sequence.present(true).eye==-1);
    sequence.begin(1,30);
    CHECK(sequence.present(true).completedPair);
    sequence.begin(-1,31);
    CHECK(sequence.present(false).eye==0);
    CHECK(sequence.present(true).eye==0); // Cannot resurrect a rejected ticket.
    sequence.begin(1,31);
    CHECK(sequence.present(true).fault==Fault::MissingLeft);
    // Fresh incorrect tags still fail; consuming tickets does not relax pairing.
    sequence.begin(-1,40); CHECK(sequence.present(true).eye==-1);
    sequence.begin(-1,40); CHECK(sequence.present(true).fault==Fault::RepeatedLeft);
    sequence.begin(-1,41); CHECK(sequence.present(true).eye==-1);
    sequence.begin(1,42); CHECK(sequence.present(true).fault==Fault::GenerationMismatch);
    sequence.begin(-1,43); CHECK(sequence.present(true).eye==-1);
    CHECK(sequence.present(true).eye==0);
    sequence.begin(1,43); CHECK(sequence.present(true).fault==Fault::MissingLeft);
    // A failed bridge/reset consumes any partial ticket without skipping desktop.
    sequence.begin(-1,44); sequence.reset(); CHECK(sequence.present(true).eye==0);
    sequence.begin(1,44); CHECK(sequence.present(true).fault==Fault::MissingLeft);
    sequence.begin(-1,45); CHECK(sequence.present(true).eye==-1);
    sequence.begin(1,45); CHECK(sequence.present(true).completedPair);
    sequence.begin(-1,100,true);auto afwLeft=sequence.present(true);CHECK(afwLeft.completedPair&&afwLeft.afw&&afwLeft.eye==-1);
    CHECK(sequence.present(true).eye==0);
    sequence.begin(1,101,true);auto afwRight=sequence.present(true);CHECK(afwRight.completedPair&&afwRight.afw&&afwRight.fault==Fault::None);
    sequence.begin(-1,102,true);CHECK(sequence.present(false).eye==0);
    sequence.begin(-1,103);CHECK(!sequence.present(true).completedPair);
    sequence.begin(1,103);CHECK(sequence.present(true).completedPair);
    std::puts("PASS: single-use native/AFW Present tickets; movie interleaving; invalid views; strict generation pairing; mode change/reset");
    return 0;
}
