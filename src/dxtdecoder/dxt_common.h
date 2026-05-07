#pragma once
#include <GLES3/gl3.h>
#include <stdint.h>

uint32_t* decompress_dxt(GLenum format, const void* data,
                          GLsizei width, GLsizei height);
