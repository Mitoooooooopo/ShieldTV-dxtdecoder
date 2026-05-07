#include <GLES3/gl3.h>
#include "dxt_common.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <android/log.h>

#define TAG "DXTDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// DXT1 formats
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

// ── Color unpacking ──────────────────────────────────────
static void unpack_rgb565(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = (c >> 11) & 0x1F; *r = (*r << 3) | (*r >> 2);
    *g = (c >>  5) & 0x3F; *g = (*g << 2) | (*g >> 4);
    *b = (c >>  0) & 0x1F; *b = (*b << 3) | (*b >> 2);
}

// ── DXT1 block decoder ───────────────────────────────────
static void decode_dxt1_block(const uint8_t* src, uint32_t* dst,
                               int bw, int bh, int stride, bool alpha) {
    uint16_t c0 = src[0] | (src[1] << 8);
    uint16_t c1 = src[2] | (src[3] << 8);
    uint32_t bits = src[4] | (src[5]<<8) | (src[6]<<16) | (src[7]<<24);

    uint8_t r[4], g[4], b[4], a[4];
    unpack_rgb565(c0, &r[0], &g[0], &b[0]); a[0] = 255;
    unpack_rgb565(c1, &r[1], &g[1], &b[1]); a[1] = 255;

    if (c0 > c1) {
        r[2] = (2*r[0]+r[1])/3; g[2] = (2*g[0]+g[1])/3;
        b[2] = (2*b[0]+b[1])/3; a[2] = 255;
        r[3] = (r[0]+2*r[1])/3; g[3] = (g[0]+2*g[1])/3;
        b[3] = (b[0]+2*b[1])/3; a[3] = 255;
    } else {
        r[2] = (r[0]+r[1])/2; g[2] = (g[0]+g[1])/2;
        b[2] = (b[0]+b[1])/2; a[2] = 255;
        r[3] = 0; g[3] = 0; b[3] = 0;
        a[3] = alpha ? 0 : 255;
    }

    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int idx = (bits >> (2*(y*4+x))) & 3;
            dst[y*stride+x] = (a[idx]<<24)|(b[idx]<<16)|(g[idx]<<8)|r[idx];
        }
    }
}

// ── DXT5 alpha block decoder ─────────────────────────────
static void decode_dxt5_alpha(const uint8_t* src, uint8_t* alpha,
                               int bw, int bh, int stride) {
    uint8_t a0 = src[0], a1 = src[1];
    uint8_t av[8];
    av[0] = a0; av[1] = a1;
    if (a0 > a1) {
        av[2]=(6*a0+1*a1)/7; av[3]=(5*a0+2*a1)/7;
        av[4]=(4*a0+3*a1)/7; av[5]=(3*a0+4*a1)/7;
        av[6]=(2*a0+5*a1)/7; av[7]=(1*a0+6*a1)/7;
    } else {
        av[2]=(4*a0+1*a1)/5; av[3]=(3*a0+2*a1)/5;
        av[4]=(2*a0+3*a1)/5; av[5]=(1*a0+4*a1)/5;
        av[6]=0; av[7]=255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= ((uint64_t)src[2+i] << (8*i));
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int idx = (bits >> (3*(y*4+x))) & 7;
            alpha[y*stride+x] = av[idx];
        }
}

// ── DXT3 alpha block decoder ─────────────────────────────
static void decode_dxt3_alpha(const uint8_t* src, uint8_t* alpha,
                               int bw, int bh, int stride) {
    for (int y = 0; y < bh; y++) {
        uint16_t row = src[y*2] | (src[y*2+1] << 8);
        for (int x = 0; x < bw; x++) {
            uint8_t a = (row >> (x*4)) & 0xF;
            alpha[y*stride+x] = (a << 4) | a;
        }
    }
}

// ── Main decompress function ─────────────────────────────
static uint32_t* decompress_dxt(GLenum format,
                                  const void* data,
                                  GLsizei width, GLsizei height) {
    int blocks_x = (width  + 3) / 4;
    int blocks_y = (height + 3) / 4;
    uint32_t* rgba = (uint32_t*)malloc(width * height * 4);
    if (!rgba) return nullptr;

    bool is_dxt1 = (format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
                    format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
    bool is_dxt5 = (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
    bool has_alpha = (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);

    int block_size = is_dxt1 ? 8 : 16;
    const uint8_t* src = (const uint8_t*)data;

    for (int by = 0; by < blocks_y; by++) {
        for (int bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = src + (by*blocks_x+bx) * block_size;
            int px = bx * 4, py = by * 4;
            int bw = (width  - px < 4) ? width  - px : 4;
            int bh = (height - py < 4) ? height - py : 4;
            uint32_t* out = rgba + py*width + px;

            if (is_dxt1) {
                decode_dxt1_block(block, out, bw, bh, width, has_alpha);
            } else {
                // DXT3 or DXT5 — alpha in first 8 bytes, color in last 8
                uint8_t alpha[16];
                if (is_dxt5)
                    decode_dxt5_alpha(block, alpha, bw, bh, 4);
                else
                    decode_dxt3_alpha(block, alpha, bw, bh, 4);

                // Decode color into temp buffer
                uint32_t temp[16] = {};
                decode_dxt1_block(block+8, temp, bw, bh, 4, false);

                // Merge alpha
                for (int y = 0; y < bh; y++)
                    for (int x = 0; x < bw; x++) {
                        uint32_t c = temp[y*4+x];
                        out[y*width+x] = (c & 0x00FFFFFF) |
                                         ((uint32_t)alpha[y*4+x] << 24);
                    }
            }
        }
    }
    return rgba;
}

// ── Hook ────────────────────────────────────────────────
typedef void (*FN_CompTexImage2D)(GLenum,GLint,GLenum,GLsizei,GLsizei,
                                   GLint,GLsizei,const void*);
typedef void (*FN_TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,
                               GLint,GLenum,GLenum,const void*);

static FN_CompTexImage2D real_comp  = nullptr;
static FN_TexImage2D     real_tex2d = nullptr;

static void ensure_real() {
    if (!real_comp)
        real_comp = (FN_CompTexImage2D)dlsym(RTLD_NEXT,"glCompressedTexImage2D");
    if (!real_tex2d)
        real_tex2d = (FN_TexImage2D)dlsym(RTLD_NEXT,"glTexImage2D");
}

extern "C" void glCompressedTexImage2D(GLenum target, GLint level,
                                        GLenum internalformat,
                                        GLsizei width, GLsizei height,
                                        GLint border, GLsizei imageSize,
                                        const void* data) {
    ensure_real();

    if (internalformat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT  ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {

        LOGI("DXT decode: fmt=0x%x %dx%d", internalformat, width, height);

        uint32_t* rgba = decompress_dxt(internalformat, data, width, height);
        if (rgba) {
            real_tex2d(target, level, GL_RGBA, width, height,
                       border, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            free(rgba);
        } else {
            LOGI("DXT decode: malloc failed %dx%d", width, height);
        }
        return;
    }

    real_comp(target, level, internalformat,
              width, height, border, imageSize, data);
}

// ── Also hook glCompressedTexSubImage2D ─────────────────
typedef void (*FN_CompTexSubImage2D)(GLenum,GLint,GLint,GLint,
                                      GLsizei,GLsizei,GLenum,GLsizei,const void*);
typedef void (*FN_TexSubImage2D)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,
                                  GLenum,GLenum,const void*);

static FN_CompTexSubImage2D real_comp_sub  = nullptr;
static FN_TexSubImage2D     real_tex2d_sub = nullptr;

extern "C" void glCompressedTexSubImage2D(GLenum target, GLint level,
                                           GLint xoffset, GLint yoffset,
                                           GLsizei width, GLsizei height,
                                           GLenum format, GLsizei imageSize,
                                           const void* data) {
    if (!real_comp_sub)
        real_comp_sub = (FN_CompTexSubImage2D)
            dlsym(RTLD_NEXT,"glCompressedTexSubImage2D");
    if (!real_tex2d_sub)
        real_tex2d_sub = (FN_TexSubImage2D)
            dlsym(RTLD_NEXT,"glTexSubImage2D");

    if (format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT  ||
        format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
        format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {

        uint32_t* rgba = decompress_dxt(format, data, width, height);
        if (rgba) {
            real_tex2d_sub(target, level, xoffset, yoffset,
                           width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            free(rgba);
        }
        return;
    }

    real_comp_sub(target, level, xoffset, yoffset,
                  width, height, format, imageSize, data);
}
