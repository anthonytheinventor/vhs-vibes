// vst2_min.h: the few VST 2.4 declarations drive.cpp and render.cpp need, for a plugin that has no
// header of its own to give them (one that declares the interface inside a source file, say).
// Written for this harness from the public binary layout; the numbers and the field order are the
// interface. Used when the harness is compiled without SRC and without MPC_OPT_ABI.
#ifndef MPC_OPT_VST2_MIN_H
#define MPC_OPT_VST2_MIN_H
#include <stdint.h>

typedef intptr_t VstIntPtr;
typedef int32_t VstInt32;
struct AEffect;
typedef VstIntPtr (*audioMasterCallback)(struct AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

struct AEffect
{
    VstInt32 magic;
    VstIntPtr (*dispatcher)(struct AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
    void (*process)(struct AEffect*, float** in, float** out, VstInt32 frames);
    void (*setParameter)(struct AEffect*, VstInt32 index, float value);
    float (*getParameter)(struct AEffect*, VstInt32 index);
    VstInt32 numPrograms, numParams, numInputs, numOutputs, flags;
    VstIntPtr resvd1, resvd2;
    VstInt32 initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    VstInt32 uniqueID, version;
    void (*processReplacing)(struct AEffect*, float** in, float** out, VstInt32 frames);
    void (*processDoubleReplacing)(struct AEffect*, double** in, double** out, VstInt32 frames);
    char future[56];
};

enum { effSetProgram = 2, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12 };
#endif
