#include "nicegraf.h"

/**
 * Characteristics of an image format.
 */
typedef struct ngf_image_format_properties {
  uint32_t nchannels;     // < Number of color channels.
  uint32_t bits_per_px;   // < Bytes per pixel.
  bool     is_depth;      // < Whether the format is suitable for a depth attachment.
  bool     is_stencil;    // < Whether the format is suitable for a stencil attachment.
  bool     is_srgb;       // < Whether it's an sRGB format.
} ngf_image_format_properties;

/**
 * A static array containing the description of each image format,
 * indexed by the image format enum.
 */
extern ngf_image_format_properties NGF_IMAGE_FORMAT_PROPS[];

ngf_image_format_properties NGF_IMAGE_FORMAT_PROPS[] = {
    /* NGF_IMAGE_FORMAT_R8 */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 1u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG8, */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGB8 */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGBA8 */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_SRGB8 */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = true},

    /* NGF_IMAGE_FORMAT_SRGBA8 */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = true},

    /* NGF_IMAGE_FORMAT_BGR8 */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_BGRA8 */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_BGR8_SRGB */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = true},

    /* NGF_IMAGE_FORMAT_BGRA8_SRGB */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = true},

    /* NGF_IMAGE_FORMAT_R32F */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG32F */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 4u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGB32F */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 4u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGBA32F */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 4u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R16F */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG16F */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 2u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGB16F */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 2u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGBA16F */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 2u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG11B10F */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R16_UNORM */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R16_SNORM */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R16U */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R16S */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG16U */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 2u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGB16U */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 2u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGBA16U */
    {.nchannels   = 4u,
     .bits_per_px = 8u * 2u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_R32U */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RG32U */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 4u * 2u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGB32U */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 4u * 3u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_RGBA32U */
    {.nchannels   = 3u,
     .bits_per_px = 8u * 4u * 4u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_DEPTH32 */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 4u,
     .is_depth    = true,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_DEPTH16 */
    {.nchannels   = 1u,
     .bits_per_px = 8u * 2u,
     .is_depth    = true,
     .is_stencil  = false,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_DEPTH24_STENCIL8 */
    {.nchannels   = 2u,
     .bits_per_px = 8u * 4u,
     .is_depth    = true,
     .is_stencil  = true,
     .is_srgb     = false},

    /* NGF_IMAGE_FORMAT_UNDEFINED */
    {.nchannels   = 0u,
     .bits_per_px = 8u * 0u,
     .is_depth    = false,
     .is_stencil  = false,
     .is_srgb     = false}};

// This will make the compiler complain if we miss some formats.
extern ngf_image_format_properties NGF_IMAGE_FORMAT_PROPS[NGF_IMAGE_FORMAT_COUNT];