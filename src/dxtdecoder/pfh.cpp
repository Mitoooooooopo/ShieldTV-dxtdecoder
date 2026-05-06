#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "ResourceMem"

struct FTexture2DResourceMem {
    void**   vtable;
    int      sizeX;
    int      sizeY;
    int      numMips;
    int      pixelFormat;
    void**   mipData;
    size_t*  mipSizes;
};

// Vtable slot 4 — likely IsValid() or GetMipCount()
static int stub_slot4(void* self) {
    return 1; // return true/valid
}

static void stub_destructor(void* self) {
    FTexture2DResourceMem* mem = (FTexture2DResourceMem*)self;
    if (mem->mipData) {
        for (int i = 0; i < mem->numMips; i++)
            if (mem->mipData[i]) free(mem->mipData[i]);
        free(mem->mipData);
    }
    if (mem->mipSizes) free(mem->mipSizes);
    free(mem);
}

static void* stub_vtable[] = {
    (void*)stub_destructor, // slot 0 +0x00
    (void*)stub_destructor, // slot 1 +0x04
    (void*)stub_slot4,      // slot 2 +0x08
    (void*)stub_slot4,      // slot 3 +0x0c
    (void*)stub_slot4,      // slot 4 +0x10 ← called at 0x9673ec
    (void*)stub_slot4,      // slot 5 +0x14
    (void*)stub_slot4,      // slot 6 +0x18
};

extern "C" void* _ZN10UTexture2D17CreateResourceMemEiii12EPixelFormatjP18FThreadSafeCounter(
    void* self,
    int sizeX, int sizeY, int numMips,
    int format, unsigned int flags,
    void* counter) {

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "CreateResourceMem: %dx%d mips=%d fmt=%d",
        sizeX, sizeY, numMips, format);

    FTexture2DResourceMem* mem =
        (FTexture2DResourceMem*)calloc(1, sizeof(FTexture2DResourceMem));

    mem->vtable     = stub_vtable;
    mem->sizeX      = sizeX;
    mem->sizeY      = sizeY;
    mem->numMips    = numMips;
    mem->pixelFormat = format;

    // Allocate mip data array
    mem->mipData  = (void**)calloc(numMips, sizeof(void*));
    mem->mipSizes = (size_t*)calloc(numMips, sizeof(size_t));

    // Allocate RGBA8 buffer for each mip level
    int w = sizeX, h = sizeY;
    for (int i = 0; i < numMips; i++) {
        size_t size = (w < 1 ? 1 : w) * (h < 1 ? 1 : h) * 4;
        mem->mipData[i]  = calloc(size, 1);
        mem->mipSizes[i] = size;
        w >>= 1; h >>= 1;
    }

    return mem;
}
