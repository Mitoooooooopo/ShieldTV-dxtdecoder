#include <dlfcn.h>
#include <cstdint>

#define PF_DXT1     5
#define PF_DXT3     6
#define PF_DXT5     7
#define PF_A8R8G8B8 2

extern "C" int _ZN10UTexture2D23GetEffectivePixelFormatE12EPixelFormatjN3UE313EPlatformTypeE(
    int format, unsigned int sizeX, int platform) {

    if (format == PF_DXT1 ||
        format == PF_DXT3 ||
        format == PF_DXT5) {
        return PF_A8R8G8B8;
    }

    if (!real_GetEffective)
        real_GetEffective = (FN_GetEffectivePixelFormat)
            dlsym(RTLD_NEXT, "_ZN10UTexture2D23GetEffectivePixelFormatE12EPixelFormatjN3UE313EPlatformTypeE");

    return real_GetEffective(format, sizeX, platform);
}
