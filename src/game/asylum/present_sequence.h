#pragma once
#include <cstdint>

namespace bvr::aa::native {
// Render-thread only. A BeginDrawing ticket belongs to exactly ONE Present.
// Movies/loading may Present again without issuing a viewport BeginDrawing.
class PresentSequence {
public:
    enum class Fault { None, RepeatedLeft, MissingLeft, GenerationMismatch };
    struct Result {
        int eye{};
        uint32_t generation{};
        bool completedPair{};
        bool afw{};
        Fault fault{Fault::None};
    };
    void begin(int eye, uint32_t generation, bool afw=false) {
        pending = true;
        pendingEye = eye;
        pendingGeneration = generation;
        pendingAFW = afw;
    }
    Result present(bool validatedView) {
        Result result{};
        if (pending && validatedView) {
            result.eye = pendingEye;
            result.generation = pendingGeneration;
            result.afw = pendingAFW && result.eye!=0;
        }
        pending = false; // Consume even neutral or rejected tickets.
        if(result.afw){haveLeft=false;result.completedPair=true;return result;}
        if (result.eye < 0) {
            if (haveLeft) result.fault = Fault::RepeatedLeft;
            else { haveLeft = true; leftGeneration = result.generation; }
        } else if (result.eye > 0) {
            if (!haveLeft) result.fault = Fault::MissingLeft;
            else if (leftGeneration != result.generation) result.fault = Fault::GenerationMismatch;
            else result.completedPair = true;
            haveLeft = false;
        } else haveLeft = false; // Untagged content interrupts a partial pair.
        if (result.fault != Fault::None) { result.eye = 0; result.generation = 0; haveLeft = false; }
        return result;
    }
    void reset() { pending = false; haveLeft = false; }
private:
    bool pending{}, haveLeft{},pendingAFW{};
    int pendingEye{};
    uint32_t pendingGeneration{}, leftGeneration{};
};
}
