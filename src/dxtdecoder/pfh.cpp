#include <GLES3/gl3.h>
#include "dxt_common.h"
#include <dlfcn.h>
#include <cstdlib>
#include <android/log.h>
#include "dobby.h"

#define TAG "InitRHI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// BulkData functions
typedef void  (*FN_MakeSureBulkDataIsLoaded)(void*);
typedef void* (*FN_Lock)(void*, uint32_t);
typedef void  (*FN_Unlock)(void*);
typedef int   (*FN_GetBulkDataSize)(void*);

static FN_MakeSureBulkDataIsLoaded eng_MakeSureBulkDataIsLoaded = nullptr;
static FN_Lock    eng_Lock    = nullptr;
static FN_Unlock  eng_Unlock  = nullptr;
static FN_GetBulkDataSize eng_GetBulkDataSize = nullptr;

// DXT decompressor (from your existing code)
extern uint32_t* decompress_dxt(GLenum format, const void* data,
                                 GLsizei width, GLsizei height);

typedef void (*FN_InitRHI)(void* self);
static FN_InitRHI orig_InitRHI = nullptr;

void my_InitRHI(void* self) {
    uint8_t* res = (uint8_t*)self;
    uint8_t* tex = *(uint8_t**)(res + 0x48);
    if (!tex) { orig_InitRHI(self); return; }

    uint8_t  format   = *(uint8_t*)(tex + 0x114);
    int      firstMip = *(int*)(res + 0x50);

    // TIndirectArray layout
    uint8_t* mipArrayObj = *(uint8_t**)(tex + 0xec);
    if (!mipArrayObj) { orig_InitRHI(self); return; }

    void**   mipData  = *(void***)(mipArrayObj + 0);
    int      mipCount = *(int*)(mipArrayObj + 4);

    if (!mipData || mipCount <= 0) { orig_InitRHI(self); return; }

    GLenum dxtFmt;
    switch(format) {
        case 5: dxtFmt = 0x83F0; break; // DXT1
        case 6: dxtFmt = 0x83F2; break; // DXT3
        case 7: dxtFmt = 0x83F3; break; // DXT5
        default:
            orig_InitRHI(self);
            return;
    }

    GLuint texHandle = 0;
    glGenTextures(1, &texHandle);
    glBindTexture(GL_TEXTURE_2D, texHandle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    for (int mip = firstMip; mip < mipCount; mip++) {
        uint8_t* mipMap = (uint8_t*)mipData[mip];
        if (!mipMap) break;

        int sizeX = *(int*)(mipMap + 0x34);
        int sizeY = *(int*)(mipMap + 0x38);
        if (sizeX <= 0 || sizeY <= 0) break;

        void* bulkData = mipMap;

        if (eng_MakeSureBulkDataIsLoaded)
            eng_MakeSureBulkDataIsLoaded(bulkData);

        void* rawData = eng_Lock ? eng_Lock(bulkData, 1) : nullptr;
        if (!rawData) break;

        uint32_t* rgba = decompress_dxt(dxtFmt, rawData, sizeX, sizeY);
        if (rgba) {
            glTexImage2D(GL_TEXTURE_2D, mip - firstMip,
                        GL_RGBA, sizeX, sizeY, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            free(rgba);
        }

        if (eng_Unlock) eng_Unlock(bulkData);
    }

    *(GLuint*)(res + 0x10c) = texHandle;
    LOGI("InitRHI: texture %d fmt=%d mips=%d",
         texHandle, format, mipCount - firstMip);
}

__attribute__((constructor))
static void install_hooks() {
    void* handle = dlopen("libUnrealEngine3.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) handle = dlopen("libUnrealEngine3.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) return;

    // Get BulkData functions
    eng_MakeSureBulkDataIsLoaded = (FN_MakeSureBulkDataIsLoaded)dlsym(handle,
        "_ZN16FUntypedBulkData24MakeSureBulkDataIsLoadedEv");
    eng_Lock = (FN_Lock)dlsym(handle,
        "_ZN16FUntypedBulkData4LockEj");
    eng_Unlock = (FN_Unlock)dlsym(handle,
        "_ZN16FUntypedBulkData6UnlockEv");

    // Hook InitRHI
    void* target = dlsym(handle,
        "_ZN18FTexture2DResource7InitRHIEv");
    if (target)
        DobbyHook(target, (void*)my_InitRHI, (void**)&orig_InitRHI);

    LOGI("Hooks installed");
    dlclose(handle);
}
