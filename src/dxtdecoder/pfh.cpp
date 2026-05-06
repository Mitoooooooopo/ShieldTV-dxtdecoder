#include <jni.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include "dobby.h"

#define TAG "ResourceMem"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// Use engine's own allocator
typedef void* (*FN_appMalloc)(size_t size, size_t align);
typedef void  (*FN_appFree)(void* ptr);

static FN_appMalloc eng_malloc = nullptr;
static FN_appFree   eng_free   = nullptr;

static void* eng_alloc(size_t size) {
    if (eng_malloc) return eng_malloc(size, 8);
    return calloc(1, size); // fallback
}

static void eng_release(void* ptr) {
    if (eng_free) eng_free(ptr);
    else free(ptr);
}

// FTexture2DResourceMem struct
struct FTexture2DResourceMem {
    void**   vtable;
    int      sizeX;
    int      sizeY;
    int      numMips;
    int      pixelFormat;
    void**   mipData;
    size_t*  mipSizes;
};

// Vtable stubs
static void  stub_dtor(void* self) {
    FTexture2DResourceMem* m = (FTexture2DResourceMem*)self;
    if (m->mipData) {
        for (int i = 0; i < m->numMips; i++)
            if (m->mipData[i]) eng_release(m->mipData[i]);
        eng_release(m->mipData);
    }
    if (m->mipSizes) eng_release(m->mipSizes);
    eng_release(m);
}
static int   stub_isValid(void* self) { return 1; }
static void* stub_getMip(void* self, int mip) {
    FTexture2DResourceMem* m = (FTexture2DResourceMem*)self;
    if (mip < 0 || mip >= m->numMips) return nullptr;
    return m->mipData[mip];
}
static void  stub_noop(void*) {}

static void* stub_vtable[] = {
    (void*)stub_dtor,     // slot 0 +0x00
    (void*)stub_dtor,     // slot 1 +0x04
    (void*)stub_noop,     // slot 2 +0x08
    (void*)stub_noop,     // slot 3 +0x0c
    (void*)stub_isValid,  // slot 4 +0x10 ← called at 0x9673ec
    (void*)stub_getMip,   // slot 5 +0x14
    (void*)stub_noop,     // slot 6 +0x18
};

// Hook target
typedef void* (*FN_CreateResourceMem)(void*, int, int, int, int, unsigned int, void*);
static FN_CreateResourceMem orig_CreateResourceMem = nullptr;

void* my_CreateResourceMem(void* self, int sizeX, int sizeY,
                            int numMips, int format,
                            unsigned int flags, void* counter) {
    LOGI("CreateResourceMem: %dx%d mips=%d fmt=%d", sizeX, sizeY, numMips, format);

    FTexture2DResourceMem* mem =
        (FTexture2DResourceMem*)calloc(1, sizeof(FTexture2DResourceMem));

    mem->vtable      = stub_vtable;
    mem->sizeX       = sizeX;
    mem->sizeY       = sizeY;
    mem->numMips     = numMips > 0 ? numMips : 1;
    mem->pixelFormat = format;
    mem->mipData     = (void**)calloc(mem->numMips, sizeof(void*));
    mem->mipSizes    = (size_t*)calloc(mem->numMips, sizeof(size_t));

    int w = sizeX, h = sizeY;
    for (int i = 0; i < mem->numMips; i++) {
        size_t size = (w < 1 ? 1 : w) * (h < 1 ? 1 : h) * 4;
        mem->mipData[i]  = calloc(size, 1);
        mem->mipSizes[i] = size;
        w >>= 1; h >>= 1;
    }
    return mem;
}

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called");

    void* handle = dlopen("libUnrealEngine3.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) {
        LOGI("Failed to get libUnrealEngine3.so handle");
        return JNI_VERSION_1_6;
    }

    // Get engine allocators
    eng_malloc = (FN_appMalloc)dlsym(handle, "_Z9appMallocjj");
    eng_free   = (FN_appFree)dlsym(handle,   "_Z7appFreePv");

    void* target = dlsym(handle,
        "_ZN10UTexture2D17CreateResourceMemEiii12EPixelFormatjP18FThreadSafeCounter");

    if (!target) {
        LOGI("CreateResourceMem symbol not found");
        return JNI_VERSION_1_6;
    }

    DobbyHook(target, (void*)my_CreateResourceMem, (void**)&orig_CreateResourceMem);
    LOGI("Hook installed successfully");

    return JNI_VERSION_1_6;
}
