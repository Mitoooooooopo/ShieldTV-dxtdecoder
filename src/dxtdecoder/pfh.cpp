#include "dobby.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

typedef void* (*FN_CreateResourceMem)(void*, int, int, int, int, unsigned int, void*);
static FN_CreateResourceMem orig_CreateResourceMem = nullptr;

void* my_CreateResourceMem(void* self, int sizeX, int sizeY, 
                            int numMips, int format, 
                            unsigned int flags, void* counter) {

    FTexture2DResourceMem* mem =
        (FTexture2DResourceMem*)calloc(1, sizeof(FTexture2DResourceMem));

    mem->vtable      = stub_vtable;
    mem->sizeX       = sizeX;
    mem->sizeY       = sizeY;
    mem->numMips     = numMips;
    mem->pixelFormat = format;
    mem->mipData     = (void**)calloc(numMips, sizeof(void*));
    mem->mipSizes    = (size_t*)calloc(numMips, sizeof(size_t));

    int w = sizeX, h = sizeY;
    for (int i = 0; i < numMips; i++) {
        size_t size = (w<1?1:w) * (h<1?1:h) * 4;
        mem->mipData[i]  = calloc(size, 1);
        mem->mipSizes[i] = size;
        w >>= 1; h >>= 1;
    }
    return mem;
}

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    void* handle = dlopen("libUnrealEngine3.so", RTLD_NOLOAD);
    void* target = dlsym(handle, 
        "_ZN10UTexture2D17CreateResourceMemEiii12EPixelFormatjP18FThreadSafeCounter");
    
    DobbyHook(target, (void*)my_CreateResourceMem, (void**)&orig_CreateResourceMem);
    return JNI_VERSION_1_6;
}
