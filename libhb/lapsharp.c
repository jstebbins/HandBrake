/* lapsharp.c

   Copyright (c) 2003-2017 HandBrake Team
   This file is part of the HandBrake source code
   Homepage: <http://handbrake.fr/>.
   It may be used under the terms of the GNU General Public License v2.
   For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "hb.h"

#define LAPSHARP_STRENGTH_LUMA_DEFAULT   0.2
#define LAPSHARP_STRENGTH_CHROMA_DEFAULT 0.2

#define LAPSHARP_KERNELS 3
#define LAPSHARP_KERNEL_LUMA_DEFAULT   2
#define LAPSHARP_KERNEL_CHROMA_DEFAULT 2

typedef struct
{
    double strength;       // strength
    char  *kernel_string;  // which kernel to use (user string)

    int    kernel;         // internal id; lapsharp_kernels[kernel]
} lapsharp_plane_context_t;

typedef struct {
    const int   *mem;
    const double coef;
    const int    size;
} lapsharp_kernel_t;

// 4-neighbor laplacian kernel (lap)
// Sharpens vertical and horizontal edges, less effective on diagonals
static const int lapsharp_kernel_lap[] =
{
 0, -1,  0,
-1,  5, -1,
 0, -1,  0
};

// Isotropic laplacian kernel (isolap)
// Minimial directionality, sharpens all edges similarly
static const int lapsharp_kernel_isolap[] =
{
-1, -4, -1,
-4, 25, -4,
-1, -4, -1
};

// Laplacian of gaussian kernel (log)
// Slightly better at noise rejection
static const int lapsharp_kernel_log[] =
{
 0,  0, -1,  0,  0,
 0, -1, -2, -1,  0,
-1, -2, 21, -2, -1,
 0, -1, -2, -1,  0,
 0,  0, -1,  0,  0
};

static lapsharp_kernel_t lapsharp_kernels[] =
{
    { lapsharp_kernel_lap,    (1.0 / 1), 3 },
    { lapsharp_kernel_isolap, (1.0 / 5), 3 },
    { lapsharp_kernel_log,    (1.0 / 5), 5 }
};

struct hb_filter_private_s
{
    lapsharp_plane_context_t plane_ctx[3];
};

static int hb_lapsharp_init(hb_filter_object_t *filter,
                            hb_filter_init_t   *init);

static int hb_lapsharp_work(hb_filter_object_t *filter,
                            hb_buffer_t ** buf_in,
                            hb_buffer_t ** buf_out);

static void hb_lapsharp_close(hb_filter_object_t *filter);

static const char hb_lapsharp_template[] =
    "y-strength=^"HB_FLOAT_REG"$:y-kernel=^"HB_ALL_REG"$:"
    "cb-strength=^"HB_FLOAT_REG"$:cb-kernel=^"HB_ALL_REG"$:"
    "cr-strength=^"HB_FLOAT_REG"$:cr-kernel=^"HB_ALL_REG"$";

hb_filter_object_t hb_filter_lapsharp =
{
    .id                = HB_FILTER_LAPSHARP,
    .enforce_order     = 1,
    .name              = "Sharpen (lapsharp)",
    .settings          = NULL,
    .init              = hb_lapsharp_init,
    .work              = hb_lapsharp_work,
    .close             = hb_lapsharp_close,
    .settings_template = hb_lapsharp_template,
};

static void hb_lapsharp(const uint8_t *src,
                              uint8_t *dst,
                        const int width,
                        const int height,
                        const int stride,
                        lapsharp_plane_context_t * ctx)
{
    const lapsharp_kernel_t *kernel = &lapsharp_kernels[ctx->kernel];

    // Sharpen using selected kernel
    const int offset_min    = -((kernel->size - 1) / 2);
    const int offset_max    =   (kernel->size + 1) / 2;
    const int stride_border =   (stride - width) / 2;
    int16_t   pixel;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            if ((y < offset_max) ||
                (y > height - offset_max) ||
                (x < stride_border + offset_max) ||
                (x > width + stride_border - offset_max))
            {
                *(dst + stride*y + x) = *(src + stride*y + x);
                continue;
            }
            pixel = 0;
            for (int k = offset_min; k < offset_max; k++)
            {
                for (int j = offset_min; j < offset_max; j++)
                {
                    pixel += kernel->mem[((j - offset_min) * kernel->size) + k - offset_min] * *(src + stride*(y + j) + (x + k));
                }
            }
            pixel = (int16_t)(((pixel * kernel->coef) - *(src + stride*y + x)) * ctx->strength) + *(src + stride*y + x);
            pixel = pixel < 0 ? 0 : pixel;
            pixel = pixel > 255 ? 255 : pixel;
            *(dst + stride*y + x) = (uint8_t)(pixel);
        }
    }
}

static int hb_lapsharp_init(hb_filter_object_t *filter,
                            hb_filter_init_t   *init)
{
    filter->private_data = calloc(sizeof(struct hb_filter_private_s), 1);
    hb_filter_private_t * pv = filter->private_data;

    // Mark parameters unset
    for (int c = 0; c < 3; c++)
    {
        pv->plane_ctx[c].strength      = -1;
        pv->plane_ctx[c].kernel_string = NULL;
        pv->plane_ctx[c].kernel        = -1;
    }

    // Read user parameters
    if (filter->settings != NULL)
    {
        hb_dict_t * dict = filter->settings;
        hb_dict_extract_double(&pv->plane_ctx[0].strength,      dict, "y-strength");
        hb_dict_extract_string(&pv->plane_ctx[0].kernel_string, dict, "y-kernel");

        hb_dict_extract_double(&pv->plane_ctx[1].strength,      dict, "cb-strength");
        hb_dict_extract_string(&pv->plane_ctx[1].kernel_string, dict, "cb-kernel");

        hb_dict_extract_double(&pv->plane_ctx[2].strength,      dict, "cr-strength");
        hb_dict_extract_string(&pv->plane_ctx[2].kernel_string, dict, "cr-kernel");
    }

    // Convert kernel user string to internal id
    for (int c = 0; c < 3; c++)
    {
        lapsharp_plane_context_t * ctx = &pv->plane_ctx[c];

        ctx->kernel = -1;

        if (ctx->kernel_string == NULL)
        {
            continue;
        }

        if (!strcasecmp(ctx->kernel_string, "lap"))
        {
            ctx->kernel = 0;
        }
        else if (!strcasecmp(ctx->kernel_string, "isolap"))
        {
            ctx->kernel = 1;
        }
        else if (!strcasecmp(ctx->kernel_string, "log"))
        {
            ctx->kernel = 2;
        }
    }

    // Cascade values
    // Cr not set; inherit Cb. Cb not set; inherit Y. Y not set; defaults.
    for (int c = 1; c < 3; c++)
    {
        lapsharp_plane_context_t * prev_ctx = &pv->plane_ctx[c - 1];
        lapsharp_plane_context_t * ctx      = &pv->plane_ctx[c];

        if (ctx->strength == -1) ctx->strength = prev_ctx->strength;
        if (ctx->kernel   == -1) ctx->kernel   = prev_ctx->kernel;
    }

    for (int c = 0; c < 3; c++)
    {
        lapsharp_plane_context_t * ctx = &pv->plane_ctx[c];

        // Replace unset values with defaults
        if (ctx->strength == -1)
        {
            ctx->strength = c ? LAPSHARP_STRENGTH_CHROMA_DEFAULT :
                                LAPSHARP_STRENGTH_LUMA_DEFAULT;
        }
        if (ctx->kernel   == -1)
        {
            ctx->kernel   = c ? LAPSHARP_KERNEL_CHROMA_DEFAULT :
                                LAPSHARP_KERNEL_LUMA_DEFAULT;
        }

        // Sanitize
        if (ctx->strength < 0)   ctx->strength = 0;
        if (ctx->strength > 1.5) ctx->strength = 1.5;
        if ((ctx->kernel < 0) || (ctx->kernel >= LAPSHARP_KERNELS))
        {
            ctx->kernel = c ? LAPSHARP_KERNEL_CHROMA_DEFAULT : LAPSHARP_KERNEL_LUMA_DEFAULT;
        }
    }

    return 0;
}

static void hb_lapsharp_close(hb_filter_object_t * filter)
{
    hb_filter_private_t *pv = filter->private_data;

    if (pv == NULL)
    {
        return;
    }

    free(pv);
    filter->private_data = NULL;
}

static int hb_lapsharp_work(hb_filter_object_t *filter,
                           hb_buffer_t ** buf_in,
                           hb_buffer_t ** buf_out)
{
    hb_filter_private_t *pv = filter->private_data;
    hb_buffer_t *in = *buf_in, *out;

    if (in->s.flags & HB_BUF_FLAG_EOF)
    {
        *buf_out = in;
        *buf_in = NULL;
        return HB_FILTER_DONE;
    }

    out = hb_frame_buffer_init(in->f.fmt, in->f.width, in->f.height);

    int c;
    for (c = 0; c < 3; c++)
    {
        lapsharp_plane_context_t * ctx = &pv->plane_ctx[c];
        hb_lapsharp(in->plane[c].data,
                    out->plane[c].data,
                    in->plane[c].width,
                    in->plane[c].height,
                    in->plane[c].stride,
                    ctx);
    }

    out->s = in->s;
    *buf_out = out;

    return HB_FILTER_OK;
}
