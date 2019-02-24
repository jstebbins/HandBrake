/* qsv_common.c
 *
 * Copyright (c) 2003-2019 HandBrake Team
 * This file is part of the HandBrake source code.
 * Homepage: <http://handbrake.fr/>.
 * It may be used under the terms of the GNU General Public License v2.
 * For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#ifdef USE_QSV

#include "hb.h"
#include "ports.h"

#define HB_QSV_API_C
#include "qsv_api.h"

#ifdef USE_QSV_DLOPEN

#if defined(_WIN32)
#define LIBMFXDLL   "libmfx.dll"
#else
#define LIBMFXDLL   "libmfx.so"
#endif

hb_qsv_api_t qsv_api;

int hb_qsv_load_api(void)
{
    void * hdl = hb_dlopen(LIBMFXDLL);

    if (hdl == NULL)
    {
        return -1;
    }
    qsv_api.MFXInit                         = hb_dlsym(hdl,
           "MFXInit");
    qsv_api.MFXClose                        = hb_dlsym(hdl,
           "MFXClose");
    qsv_api.MFXQueryIMPL                    = hb_dlsym(hdl,
           "MFXQueryIMPL");
    qsv_api.MFXQueryVersion                 = hb_dlsym(hdl,
           "MFXQueryVersion");

    qsv_api.MFXVideoCORE_SetHandle          = hb_dlsym(hdl,
           "MFXVideoCORE_SetHandle");
    qsv_api.MFXVideoCORE_SyncOperation      = hb_dlsym(hdl,
           "MFXVideoCORE_SyncOperation");

    qsv_api.MFXVideoUSER_Load               = hb_dlsym(hdl,
           "MFXVideoUSER_Load");
    qsv_api.MFXVideoUSER_UnLoad             = hb_dlsym(hdl,
           "MFXVideoUSER_UnLoad");
    qsv_api.MFXVideoUSER_Register           = hb_dlsym(hdl,
           "MFXVideoUSER_Register");
    qsv_api.MFXVideoUSER_Unregister         = hb_dlsym(hdl,
           "MFXVideoUSER_Unregister");
    qsv_api.MFXVideoUSER_ProcessFrameAsync  = hb_dlsym(hdl,
           "MFXVideoUSER_ProcessFrameAsync");

    qsv_api.MFXVideoENCODE_Init             = hb_dlsym(hdl,
           "MFXVideoENCODE_Init");
    qsv_api.MFXVideoENCODE_Close            = hb_dlsym(hdl,
           "MFXVideoENCODE_Close");
    qsv_api.MFXVideoENCODE_Query            = hb_dlsym(hdl,
           "MFXVideoENCODE_Query");
    qsv_api.MFXVideoENCODE_QueryIOSurf      = hb_dlsym(hdl,
           "MFXVideoENCODE_QueryIOSurf");
    qsv_api.MFXVideoENCODE_GetVideoParam    = hb_dlsym(hdl,
           "MFXVideoENCODE_GetVideoParam");
    qsv_api.MFXVideoENCODE_EncodeFrameAsync = hb_dlsym(hdl,
           "MFXVideoENCODE_EncodeFrameAsync");

    qsv_api.MFXVideoVPP_Init                = hb_dlsym(hdl,
           "MFXVideoVPP_Init");
    qsv_api.MFXVideoVPP_RunFrameVPPAsync    = hb_dlsym(hdl,
           "MFXVideoVPP_RunFrameVPPAsync");
    qsv_api.MFXVideoVPP_QueryIOSurf         = hb_dlsym(hdl,
           "MFXVideoVPP_QueryIOSurf");

    if (qsv_api.MFXInit                         == NULL ||
        qsv_api.MFXClose                        == NULL ||
        qsv_api.MFXQueryIMPL                    == NULL ||
        qsv_api.MFXQueryVersion                 == NULL ||

        qsv_api.MFXVideoCORE_SetHandle          == NULL ||
        qsv_api.MFXVideoCORE_SyncOperation      == NULL ||

        qsv_api.MFXVideoUSER_Load               == NULL ||
        qsv_api.MFXVideoUSER_UnLoad             == NULL ||
        qsv_api.MFXVideoUSER_Register           == NULL ||
        qsv_api.MFXVideoUSER_Unregister         == NULL ||
        qsv_api.MFXVideoUSER_ProcessFrameAsync  == NULL ||

        qsv_api.MFXVideoENCODE_Init             == NULL ||
        qsv_api.MFXVideoENCODE_Close            == NULL ||
        qsv_api.MFXVideoENCODE_Query            == NULL ||
        qsv_api.MFXVideoENCODE_QueryIOSurf      == NULL ||
        qsv_api.MFXVideoENCODE_GetVideoParam    == NULL ||
        qsv_api.MFXVideoENCODE_EncodeFrameAsync == NULL ||

        qsv_api.MFXVideoVPP_Init                == NULL ||
        qsv_api.MFXVideoVPP_RunFrameVPPAsync    == NULL ||
        qsv_api.MFXVideoVPP_QueryIOSurf         == NULL)
    {
        memset(&qsv_api, 0, sizeof(qsv_api));
        return -1;
    }
    return 0;
}
#endif // USE_QSV_DLOPEN

#endif // USE_QSV
