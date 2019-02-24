/* qsv_api.h
 *
 * Copyright (c) 2003-2019 HandBrake Team
 * This file is part of the HandBrake source code.
 * Homepage: <http://handbrake.fr/>.
 * It may be used under the terms of the GNU General Public License v2.
 * For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#ifndef HB_QSV_API_H
#define HB_QSV_API_H

#ifdef USE_QSV

#ifdef USE_QSV_DLOPEN

#include "mfx/mfxvideo.h"
#include "mfx/mfxplugin.h"

typedef struct hb_qsv_api_s
{
    mfxStatus    (*MFXInit)(mfxIMPL, mfxVersion*, mfxSession*);
    mfxStatus    (*MFXClose)(mfxSession);
    mfxStatus    (*MFXQueryIMPL)(mfxSession, mfxIMPL*);
    mfxStatus    (*MFXQueryVersion)(mfxSession, mfxVersion*);

    mfxStatus    (*MFXVideoCORE_SetHandle)(mfxSession, mfxHandleType, mfxHDL);
    mfxStatus    (*MFXVideoCORE_SyncOperation)(mfxSession,
                    mfxSyncPoint, mfxU32);

    mfxStatus    (*MFXVideoUSER_Load)(mfxSession, const mfxPluginUID*, mfxU32);
    mfxStatus    (*MFXVideoUSER_UnLoad)(mfxSession, const mfxPluginUID*);
    mfxStatus    (*MFXVideoUSER_Register)(mfxSession, mfxU32, const mfxPlugin*);
    mfxStatus    (*MFXVideoUSER_Unregister)(mfxSession, mfxU32);
    mfxStatus    (*MFXVideoUSER_ProcessFrameAsync)(mfxSession, const mfxHDL*,
                    mfxU32, const mfxHDL*, mfxU32, mfxSyncPoint*);

    mfxStatus    (*MFXVideoENCODE_Init)(mfxSession, mfxVideoParam*);
    mfxStatus    (*MFXVideoENCODE_Close)(mfxSession);
    mfxStatus    (*MFXVideoENCODE_Query)(mfxSession,
                                         mfxVideoParam*, mfxVideoParam*);
    mfxStatus    (*MFXVideoENCODE_QueryIOSurf)(mfxSession, mfxVideoParam*,
                    mfxFrameAllocRequest*);
    mfxStatus    (*MFXVideoENCODE_GetVideoParam)(mfxSession, mfxVideoParam*);
    mfxStatus    (*MFXVideoENCODE_EncodeFrameAsync)(mfxSession,
                    mfxEncodeCtrl*, mfxFrameSurface1*, mfxBitstream*,
                    mfxSyncPoint*);

    mfxStatus    (*MFXVideoVPP_Init)(mfxSession, mfxVideoParam*);
    mfxStatus    (*MFXVideoVPP_QueryIOSurf)(mfxSession, mfxVideoParam*,
                    mfxFrameAllocRequest r[2]);
    mfxStatus    (*MFXVideoVPP_RunFrameVPPAsync)(mfxSession, mfxFrameSurface1*,
                    mfxFrameSurface1*, mfxExtVppAuxData*, mfxSyncPoint*);

} hb_qsv_api_t;

extern  hb_qsv_api_t qsv_api;

int     hb_qsv_load_api(void);

#if !defined(HB_QSV_API_C)

#define MFXInit                         qsv_api.MFXInit
#define MFXClose                        qsv_api.MFXClose
#define MFXQueryIMPL                    qsv_api.MFXQueryIMPL
#define MFXQueryVersion                 qsv_api.MFXQueryVersion

#define MFXVideoCORE_SetHandle          qsv_api.MFXVideoCORE_SetHandle
#define MFXVideoCORE_SyncOperation      qsv_api.MFXVideoCORE_SyncOperation

#define MFXVideoUSER_Load               qsv_api.MFXVideoUSER_Load
#define MFXVideoUSER_UnLoad             qsv_api.MFXVideoUSER_UnLoad
#define MFXVideoUSER_Register           qsv_api.MFXVideoUSER_Register
#define MFXVideoUSER_Unregister         qsv_api.MFXVideoUSER_Unregister
#define MFXVideoUSER_ProcessFrameAsync  qsv_api.MFXVideoUSER_ProcessFrameAsync

#define MFXVideoENCODE_Init             qsv_api.MFXVideoENCODE_Init
#define MFXVideoENCODE_Close            qsv_api.MFXVideoENCODE_Close
#define MFXVideoENCODE_Query            qsv_api.MFXVideoENCODE_Query
#define MFXVideoENCODE_QueryIOSurf      qsv_api.MFXVideoENCODE_QueryIOSurf
#define MFXVideoENCODE_GetVideoParam    qsv_api.MFXVideoENCODE_GetVideoParam
#define MFXVideoENCODE_EncodeFrameAsync qsv_api.MFXVideoENCODE_EncodeFrameAsync

#define MFXVideoVPP_Init                qsv_api.MFXVideoVPP_Init
#define MFXVideoVPP_RunFrameVPPAsync    qsv_api.MFXVideoVPP_RunFrameVPPAsync
#define MFXVideoVPP_QueryIOSurf         qsv_api.MFXVideoVPP_QueryIOSurf

#endif // !defined(HB_QSV_API_C)

#endif // USE_QSV_DLOPEN
#endif // USE_QSV
#endif // HB_QSV_API_H

