/******************************************************************************
 * $Id$
 *
 * Project:  JPEG2000 driver based on OpenJPEG library
 * Purpose:  JPEG2000 driver based on OpenJPEG library
 * Author:   Even Rouault, <even dot rouault at spatialys dot com>
 *
 ******************************************************************************
 * Copyright (c) 2010-2014, Even Rouault <even dot rouault at spatialys dot com>
 * Copyright (c) 2015, European Union (European Environment Agency)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

/* This file is to be used with openjpeg 2.0 */

#if defined(OPENJPEG_VERSION) && OPENJPEG_VERSION >= 20100
#include <openjpeg-2.1/openjpeg.h>
#else
#include <stdio.h> /* openjpeg.h needs FILE* */
#include <openjpeg-2.0/openjpeg.h>
#endif
#include <vector>

#include "gdaljp2abstractdataset.h"
#include "cpl_string.h"
#include "gdaljp2metadata.h"
#include "cpl_multiproc.h"
#include "cpl_atomic_ops.h"

CPL_CVSID("$Id$");

/************************************************************************/
/*                  JP2OpenJPEGDataset_ErrorCallback()                  */
/************************************************************************/

static void JP2OpenJPEGDataset_ErrorCallback(const char *pszMsg, CPL_UNUSED void *unused)
{
    CPLError(CE_Failure, CPLE_AppDefined, "%s", pszMsg);
}

/************************************************************************/
/*               JP2OpenJPEGDataset_WarningCallback()                   */
/************************************************************************/

static void JP2OpenJPEGDataset_WarningCallback(const char *pszMsg, CPL_UNUSED void *unused)
{
    if( strcmp(pszMsg, "Empty SOT marker detected: Psot=12.\n") == 0 )
    {
        static int bWarningEmitted = FALSE;
        if( bWarningEmitted )
            return;
        bWarningEmitted = TRUE;
    }
    if( strcmp(pszMsg, "JP2 box which are after the codestream will not be read by this function.\n") != 0 )
        CPLError(CE_Warning, CPLE_AppDefined, "%s", pszMsg);
}

/************************************************************************/
/*                 JP2OpenJPEGDataset_InfoCallback()                    */
/************************************************************************/

static void JP2OpenJPEGDataset_InfoCallback(const char *pszMsg, CPL_UNUSED void *unused)
{
    char* pszMsgTmp = CPLStrdup(pszMsg);
    int nLen = (int)strlen(pszMsgTmp);
    while( nLen > 0 && pszMsgTmp[nLen-1] == '\n' )
    {
        pszMsgTmp[nLen-1] = '\0';
        nLen --;
    }
    CPLDebug("OPENJPEG", "info: %s", pszMsgTmp);
    CPLFree(pszMsgTmp);
}

/************************************************************************/
/*                      JP2OpenJPEGDataset_Read()                       */
/************************************************************************/

static OPJ_SIZE_T JP2OpenJPEGDataset_Read(void* pBuffer, OPJ_SIZE_T nBytes,
                                       void *pUserData)
{
    vsi_l_offset nOffsetBefore =  VSIFTellL((VSILFILE*)pUserData);
    int nRet = VSIFReadL(pBuffer, 1, nBytes, (VSILFILE*)pUserData);
#ifdef DEBUG_IO
    CPLDebug("OPENJPEG", "JP2OpenJPEGDataset_Read(%d) = %d", (int)nBytes, nRet);
#endif
    if (nRet == 0)
        nRet = -1;
    else if( nOffsetBefore == 0 && nBytes >= 4 )
    {
        /* Nasty hack : opj_get_decoded_tile() currently ignores
          OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG, so we "hide" the jp2 boxes
          that advertize a color table... */
        for(OPJ_SIZE_T i=0;i<nBytes-4;i++)
        {
            if( memcmp((GByte*)pBuffer + i, "pclr", 4)==0 ||
                memcmp((GByte*)pBuffer + i, "cmap", 4)==0 )
            {
                memcpy((GByte*)pBuffer +i, "XXXX", 4);
            }
        }
    }
    return nRet;
}

/************************************************************************/
/*                      JP2OpenJPEGDataset_Write()                      */
/************************************************************************/

static OPJ_SIZE_T JP2OpenJPEGDataset_Write(void* pBuffer, OPJ_SIZE_T nBytes,
                                       void *pUserData)
{
    int nRet = VSIFWriteL(pBuffer, 1, nBytes, (VSILFILE*)pUserData);
#ifdef DEBUG_IO
    CPLDebug("OPENJPEG", "JP2OpenJPEGDataset_Write(%d) = %d", (int)nBytes, nRet);
#endif
    return nRet;
}

/************************************************************************/
/*                       JP2OpenJPEGDataset_Seek()                      */
/************************************************************************/

static OPJ_BOOL JP2OpenJPEGDataset_Seek(OPJ_OFF_T nBytes, void * pUserData)
{
#ifdef DEBUG_IO
    CPLDebug("OPENJPEG", "JP2OpenJPEGDataset_Seek(%d)", (int)nBytes);
#endif
    return VSIFSeekL((VSILFILE*)pUserData, nBytes, SEEK_SET) == 0;
}

/************************************************************************/
/*                     JP2OpenJPEGDataset_Skip()                        */
/************************************************************************/

static OPJ_OFF_T JP2OpenJPEGDataset_Skip(OPJ_OFF_T nBytes, void * pUserData)
{
    vsi_l_offset nOffset = VSIFTellL((VSILFILE*)pUserData);
    nOffset += nBytes;
#ifdef DEBUG_IO
    CPLDebug("OPENJPEG", "JP2OpenJPEGDataset_Skip(%d -> " CPL_FRMT_GUIB ")",
             (int)nBytes, (GUIntBig)nOffset);
#endif
    VSIFSeekL((VSILFILE*)pUserData, nOffset, SEEK_SET);
    return nBytes;
}

/************************************************************************/
/* ==================================================================== */
/*                           JP2OpenJPEGDataset                         */
/* ==================================================================== */
/************************************************************************/

class JP2OpenJPEGRasterBand;

class JP2OpenJPEGDataset : public GDALJP2AbstractDataset
{
    friend class JP2OpenJPEGRasterBand;

    VSILFILE   *fp; /* Large FILE API */

    OPJ_CODEC_FORMAT eCodecFormat;
    OPJ_COLOR_SPACE eColorSpace;

    int         bIs420;

    int         iLevel;
    int         nOverviewCount;
    JP2OpenJPEGDataset** papoOverviewDS;
    int         bUseSetDecodeArea;

    int         nThreads;
    int         GetNumThreads();
    int         bEnoughMemoryToLoadOtherBands;

  protected:
    virtual int         CloseDependentDatasets();

  public:
                JP2OpenJPEGDataset();
                ~JP2OpenJPEGDataset();
    
    static int Identify( GDALOpenInfo * poOpenInfo );
    static GDALDataset  *Open( GDALOpenInfo * );
    static GDALDataset  *CreateCopy( const char * pszFilename,
                                           GDALDataset *poSrcDS, 
                                           int bStrict, char ** papszOptions, 
                                           GDALProgressFunc pfnProgress,
                                           void * pProgressData );

    virtual CPLErr  IRasterIO( GDALRWFlag eRWFlag,
                               int nXOff, int nYOff, int nXSize, int nYSize,
                               void * pData, int nBufXSize, int nBufYSize,
                               GDALDataType eBufType, 
                               int nBandCount, int *panBandMap,
                               GSpacing nPixelSpace, GSpacing nLineSpace,
                               GSpacing nBandSpace,
                               GDALRasterIOExtraArg* psExtraArg);

    static void         WriteBox(VSILFILE* fp, GDALJP2Box* poBox);

    CPLErr      ReadBlock( int nBand, VSILFILE* fp,
                           int nBlockXOff, int nBlockYOff, void * pImage,
                           int nBandCount, int *panBandMap );

    int         PreloadBlocks( JP2OpenJPEGRasterBand* poBand,
                               int nXOff, int nYOff, int nXSize, int nYSize,
                               int nBandCount, int *panBandMap );
};

/************************************************************************/
/* ==================================================================== */
/*                         JP2OpenJPEGRasterBand                        */
/* ==================================================================== */
/************************************************************************/

class JP2OpenJPEGRasterBand : public GDALPamRasterBand
{
    friend class JP2OpenJPEGDataset;
    int             bPromoteTo8Bit;
    GDALColorTable* poCT;

  public:

                JP2OpenJPEGRasterBand( JP2OpenJPEGDataset * poDS, int nBand,
                                       GDALDataType eDataType, int nBits,
                                       int bPromoteTo8Bit,
                                       int nBlockXSize, int nBlockYSize );
                ~JP2OpenJPEGRasterBand();

    virtual CPLErr          IReadBlock( int, int, void * );
    virtual CPLErr          IRasterIO( GDALRWFlag eRWFlag,
                                  int nXOff, int nYOff, int nXSize, int nYSize,
                                  void * pData, int nBufXSize, int nBufYSize,
                                  GDALDataType eBufType,
                                  GSpacing nPixelSpace, GSpacing nLineSpace,
                                  GDALRasterIOExtraArg* psExtraArg);

    virtual GDALColorInterp GetColorInterpretation();
    virtual GDALColorTable* GetColorTable() { return poCT; }

    virtual int             GetOverviewCount();
    virtual GDALRasterBand* GetOverview(int iOvrLevel);
    
    virtual int HasArbitraryOverviews() { return TRUE; }
};


/************************************************************************/
/*                        JP2OpenJPEGRasterBand()                       */
/************************************************************************/

JP2OpenJPEGRasterBand::JP2OpenJPEGRasterBand( JP2OpenJPEGDataset *poDS, int nBand,
                                              GDALDataType eDataType, int nBits,
                                              int bPromoteTo8Bit,
                                              int nBlockXSize, int nBlockYSize )

{
    this->poDS = poDS;
    this->nBand = nBand;
    this->eDataType = eDataType;
    this->nBlockXSize = nBlockXSize;
    this->nBlockYSize = nBlockYSize;
    this->bPromoteTo8Bit = bPromoteTo8Bit;
    poCT = NULL;

    if( (nBits % 8) != 0 )
        SetMetadataItem("NBITS",
                        CPLString().Printf("%d",nBits),
                        "IMAGE_STRUCTURE" );
}

/************************************************************************/
/*                      ~JP2OpenJPEGRasterBand()                        */
/************************************************************************/

JP2OpenJPEGRasterBand::~JP2OpenJPEGRasterBand()
{
    delete poCT;
}

/************************************************************************/
/*                            CLAMP_0_255()                             */
/************************************************************************/

static CPL_INLINE GByte CLAMP_0_255(int val)
{
    if (val < 0)
        return 0;
    else if (val > 255)
        return 255;
    else
        return (GByte)val;
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr JP2OpenJPEGRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                          void * pImage )
{
    JP2OpenJPEGDataset *poGDS = (JP2OpenJPEGDataset *) poDS;
    if ( poGDS->bEnoughMemoryToLoadOtherBands )
        return poGDS->ReadBlock(nBand, poGDS->fp, nBlockXOff, nBlockYOff, pImage,
                                poGDS->nBands, NULL);
    else
        return poGDS->ReadBlock(nBand, poGDS->fp, nBlockXOff, nBlockYOff, pImage,
                                1, &nBand);
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr JP2OpenJPEGRasterBand::IRasterIO( GDALRWFlag eRWFlag,
                                         int nXOff, int nYOff, int nXSize, int nYSize,
                                         void * pData, int nBufXSize, int nBufYSize,
                                         GDALDataType eBufType,
                                         GSpacing nPixelSpace, GSpacing nLineSpace,
                                         GDALRasterIOExtraArg* psExtraArg )
{
    JP2OpenJPEGDataset *poGDS = (JP2OpenJPEGDataset *) poDS;

    if( eRWFlag != GF_Read )
        return CE_Failure;

/* ==================================================================== */
/*      Do we have overviews that would be appropriate to satisfy       */
/*      this request?                                                   */
/* ==================================================================== */
    if( (nBufXSize < nXSize || nBufYSize < nYSize)
        && GetOverviewCount() > 0 && eRWFlag == GF_Read )
    {
        int         nOverview;
        GDALRasterIOExtraArg sExtraArg;
    
        GDALCopyRasterIOExtraArg(&sExtraArg, psExtraArg);

        nOverview =
            GDALBandGetBestOverviewLevel2(this, nXOff, nYOff, nXSize, nYSize,
                                        nBufXSize, nBufYSize, &sExtraArg);
        if (nOverview >= 0)
        {
            GDALRasterBand* poOverviewBand = GetOverview(nOverview);
            if (poOverviewBand == NULL)
                return CE_Failure;

            return poOverviewBand->RasterIO( eRWFlag, nXOff, nYOff, nXSize, nYSize,
                                            pData, nBufXSize, nBufYSize, eBufType,
                                            nPixelSpace, nLineSpace, &sExtraArg );
        }
    }

    poGDS->bEnoughMemoryToLoadOtherBands = poGDS->PreloadBlocks(this, nXOff, nYOff, nXSize, nYSize, 0, NULL);

    CPLErr eErr = GDALPamRasterBand::IRasterIO( eRWFlag, nXOff, nYOff, nXSize, nYSize,
                                         pData, nBufXSize, nBufYSize, eBufType,
                                         nPixelSpace, nLineSpace, psExtraArg );

    poGDS->bEnoughMemoryToLoadOtherBands = TRUE;
    return eErr;
}

/************************************************************************/
/*                            GetNumThreads()                           */
/************************************************************************/

int JP2OpenJPEGDataset::GetNumThreads()
{
    if( nThreads >= 1 )
        return nThreads;

    const char* pszThreads = CPLGetConfigOption("GDAL_NUM_THREADS", "ALL_CPUS");
    if (EQUAL(pszThreads, "ALL_CPUS"))
        nThreads = CPLGetNumCPUs();
    else
        nThreads = atoi(pszThreads);
    if (nThreads > 128)
        nThreads = 128;
    if (nThreads <= 0)
        nThreads = 1;
    return nThreads;
}

/************************************************************************/
/*                   JP2OpenJPEGReadBlockInThread()                     */
/************************************************************************/

class JobStruct
{
public:

    JP2OpenJPEGDataset* poGDS;
    int                 nBand;
    std::vector< std::pair<int, int> > oPairs;
    volatile int        nCurPair;
    int                 nBandCount;
    int                *panBandMap;
};

static void JP2OpenJPEGReadBlockInThread(void* userdata)
{
    int nPair;
    JobStruct* poJob = (JobStruct*) userdata;
    JP2OpenJPEGDataset* poGDS = poJob->poGDS;
    int nBand = poJob->nBand;
    int nPairs = (int)poJob->oPairs.size();
    int nBandCount = poJob->nBandCount;
    int* panBandMap = poJob->panBandMap;
    VSILFILE* fp = VSIFOpenL(poGDS->GetDescription(), "rb");
    if( fp == NULL )
    {
        CPLDebug("OPENJPEG", "Cannot open %s", poGDS->GetDescription());
        return;
    }

    while( (nPair = CPLAtomicInc(&(poJob->nCurPair))) < nPairs )
    {
        int nBlockXOff = poJob->oPairs[nPair].first;
        int nBlockYOff = poJob->oPairs[nPair].second;
        GDALRasterBlock* poBlock = poGDS->GetRasterBand(nBand)->
                GetLockedBlockRef(nBlockXOff,nBlockYOff, TRUE);
        if (poBlock == NULL)
            break;

        void* pDstBuffer = poBlock->GetDataRef();
        if (!pDstBuffer)
        {
            poBlock->DropLock();
            break;
        }

        poGDS->ReadBlock(nBand, fp, nBlockXOff, nBlockYOff, pDstBuffer,
                         nBandCount, panBandMap);

        poBlock->DropLock();
    }

    VSIFCloseL(fp);
}

/************************************************************************/
/*                           PreloadBlocks()                            */
/************************************************************************/

int JP2OpenJPEGDataset::PreloadBlocks(JP2OpenJPEGRasterBand* poBand,
                                      int nXOff, int nYOff, int nXSize, int nYSize,
                                      int nBandCount, int *panBandMap)
{
    int bRet = TRUE;
    int nXStart = nXOff / poBand->nBlockXSize;
    int nXEnd = (nXOff + nXSize - 1) / poBand->nBlockXSize;
    int nYStart = nYOff / poBand->nBlockYSize;
    int nYEnd = (nYOff + nYSize - 1) / poBand->nBlockYSize;
    GIntBig nReqMem = (GIntBig)(nXEnd - nXStart + 1) * (nYEnd - nYStart + 1) *
                      poBand->nBlockXSize * poBand->nBlockYSize * (GDALGetDataTypeSize(poBand->eDataType) / 8);

    int nMaxThreads = GetNumThreads();
    if( !bUseSetDecodeArea && nMaxThreads > 1 )
    {
        if( nReqMem > GDALGetCacheMax64() / (nBandCount == 0 ? 1 : nBandCount) )
            return FALSE;

        int nBlocksToLoad = 0;
        std::vector< std::pair<int,int> > oPairs;
        for(int nBlockXOff = nXStart; nBlockXOff <= nXEnd; ++nBlockXOff)
        {
            for(int nBlockYOff = nYStart; nBlockYOff <= nYEnd; ++nBlockYOff)
            {
                GDALRasterBlock* poBlock = poBand->TryGetLockedBlockRef(nBlockXOff,nBlockYOff);
                if (poBlock != NULL)
                {
                    poBlock->DropLock();
                    continue;
                }
                oPairs.push_back( std::pair<int,int>(nBlockXOff, nBlockYOff) );
                nBlocksToLoad ++;
            }
        }

        if( nBlocksToLoad > 1 )
        {
            int nThreads = MIN(nBlocksToLoad, nMaxThreads);
            CPLJoinableThread** pahThreads = (CPLJoinableThread**) CPLMalloc( sizeof(CPLJoinableThread*) * nThreads );
            int i;

            CPLDebug("OPENJPEG", "%d blocks to load", nBlocksToLoad);

            JobStruct oJob;
            oJob.poGDS = this;
            oJob.nBand = poBand->GetBand();
            oJob.oPairs = oPairs;
            oJob.nCurPair = -1;
            if( nBandCount > 0 )
            {
                oJob.nBandCount = nBandCount;
                oJob.panBandMap = panBandMap;
            }
            else
            {
                if( nReqMem <= GDALGetCacheMax64() / nBands )
                {
                    oJob.nBandCount = nBands;
                    oJob.panBandMap = NULL;
                }
                else
                {
                    bRet = FALSE;
                    oJob.nBandCount = 1;
                    oJob.panBandMap = &oJob.nBand;
                }
            }

            for(i=0;i<nThreads;i++)
                pahThreads[i] = CPLCreateJoinableThread(JP2OpenJPEGReadBlockInThread, &oJob);
            for(i=0;i<nThreads;i++)
                CPLJoinThread( pahThreads[i] );
            CPLFree(pahThreads);
        }
    }

    return bRet;
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr  JP2OpenJPEGDataset::IRasterIO( GDALRWFlag eRWFlag,
                               int nXOff, int nYOff, int nXSize, int nYSize,
                               void * pData, int nBufXSize, int nBufYSize,
                               GDALDataType eBufType, 
                               int nBandCount, int *panBandMap,
                               GSpacing nPixelSpace, GSpacing nLineSpace,
                               GSpacing nBandSpace,
                               GDALRasterIOExtraArg* psExtraArg)
{
    if( eRWFlag != GF_Read )
        return CE_Failure;

    if( nBandCount < 1 )
        return CE_Failure;

    JP2OpenJPEGRasterBand* poBand = (JP2OpenJPEGRasterBand*) GetRasterBand(panBandMap[0]);

/* ==================================================================== */
/*      Do we have overviews that would be appropriate to satisfy       */
/*      this request?                                                   */
/* ==================================================================== */

    if( (nBufXSize < nXSize || nBufYSize < nYSize)
        && poBand->GetOverviewCount() > 0 && eRWFlag == GF_Read )
    {
        int         nOverview;
        GDALRasterIOExtraArg sExtraArg;
    
        GDALCopyRasterIOExtraArg(&sExtraArg, psExtraArg);

        nOverview =
            GDALBandGetBestOverviewLevel2(poBand, nXOff, nYOff, nXSize, nYSize,
                                          nBufXSize, nBufYSize, &sExtraArg);
        if (nOverview >= 0)
        {
            return papoOverviewDS[nOverview]->RasterIO( eRWFlag, nXOff, nYOff, nXSize, nYSize,
                                                        pData, nBufXSize, nBufYSize, eBufType,
                                                        nBandCount, panBandMap,
                                                        nPixelSpace, nLineSpace, nBandSpace,
                                                        &sExtraArg);
        }
    }

    bEnoughMemoryToLoadOtherBands = PreloadBlocks(poBand, nXOff, nYOff, nXSize, nYSize, nBandCount, panBandMap);

    CPLErr eErr = GDALPamDataset::IRasterIO(   eRWFlag,
                                        nXOff, nYOff, nXSize, nYSize,
                                        pData, nBufXSize, nBufYSize,
                                        eBufType, 
                                        nBandCount, panBandMap,
                                        nPixelSpace, nLineSpace, nBandSpace,
                                        psExtraArg );

    bEnoughMemoryToLoadOtherBands = TRUE;
    return eErr;
}

/************************************************************************/
/*                             ReadBlock()                              */
/************************************************************************/

CPLErr JP2OpenJPEGDataset::ReadBlock( int nBand, VSILFILE* fp,
                                      int nBlockXOff, int nBlockYOff, void * pImage,
                                      int nBandCount, int* panBandMap )
{
    CPLErr          eErr = CE_None;
    opj_codec_t*    pCodec;
    opj_stream_t *  pStream;
    opj_image_t *   psImage;
    
    JP2OpenJPEGRasterBand* poBand = (JP2OpenJPEGRasterBand*) GetRasterBand(nBand);
    int nBlockXSize = poBand->nBlockXSize;
    int nBlockYSize = poBand->nBlockYSize;
    GDALDataType eDataType = poBand->eDataType;

    int nDataTypeSize = (GDALGetDataTypeSize(eDataType) / 8);

    int nTileNumber = nBlockXOff + nBlockYOff * poBand->nBlocksPerRow;
    int nWidthToRead = MIN(nBlockXSize, nRasterXSize - nBlockXOff * nBlockXSize);
    int nHeightToRead = MIN(nBlockYSize, nRasterYSize - nBlockYOff * nBlockYSize);

    pCodec = opj_create_decompress(eCodecFormat);

    opj_set_info_handler(pCodec, JP2OpenJPEGDataset_InfoCallback,NULL);
    opj_set_warning_handler(pCodec, JP2OpenJPEGDataset_WarningCallback, NULL);
    opj_set_error_handler(pCodec, JP2OpenJPEGDataset_ErrorCallback,NULL);

    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);

    if (! opj_setup_decoder(pCodec,&parameters))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "opj_setup_decoder() failed");
        return CE_Failure;
    }

    pStream = opj_stream_create(1024, TRUE); // Default 1MB is way too big for some datasets

    VSIFSeekL(fp, 0, SEEK_END);
    opj_stream_set_user_data_length(pStream, VSIFTellL(fp));
    /* Reseek to file beginning */
    VSIFSeekL(fp, 0, SEEK_SET);

    opj_stream_set_read_function(pStream, JP2OpenJPEGDataset_Read);
    opj_stream_set_seek_function(pStream, JP2OpenJPEGDataset_Seek);
    opj_stream_set_skip_function(pStream, JP2OpenJPEGDataset_Skip);
#if defined(OPENJPEG_VERSION) && OPENJPEG_VERSION >= 20100
    opj_stream_set_user_data(pStream, fp, NULL);
#else
    opj_stream_set_user_data(pStream, fp);
#endif

    if(!opj_read_header(pStream,pCodec,&psImage))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "opj_read_header() failed");
        return CE_Failure;
    }

    if (!opj_set_decoded_resolution_factor( pCodec, iLevel ))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "opj_set_decoded_resolution_factor() failed");
        eErr = CE_Failure;
        goto end;
    }

    if (bUseSetDecodeArea)
    {
        if (!opj_set_decode_area(pCodec,psImage,
                                 nBlockXOff*nBlockXSize,
                                 nBlockYOff*nBlockYSize,
                                 nBlockXOff*nBlockXSize+nWidthToRead,
                                 nBlockYOff*nBlockYSize+nHeightToRead))
        {
            CPLError(CE_Failure, CPLE_AppDefined, "opj_set_decode_area() failed");
            eErr = CE_Failure;
            goto end;
        }
        if (!opj_decode(pCodec,pStream, psImage))
        {
            CPLError(CE_Failure, CPLE_AppDefined, "opj_decode() failed");
            eErr = CE_Failure;
            goto end;
        }
    }
    else
    {
        if (!opj_get_decoded_tile( pCodec, pStream, psImage, nTileNumber ))
        {
            CPLError(CE_Failure, CPLE_AppDefined, "opj_get_decoded_tile() failed");
            eErr = CE_Failure;
            goto end;
        }
    }

    for(int xBand = 0; xBand < nBandCount; xBand ++)
    {
        void* pDstBuffer;
        GDALRasterBlock *poBlock = NULL;
        int iBand = (panBandMap) ? panBandMap[xBand] : xBand + 1;
        int bPromoteTo8Bit = ((JP2OpenJPEGRasterBand*)GetRasterBand(iBand))->bPromoteTo8Bit;

        if (iBand == nBand)
            pDstBuffer = pImage;
        else
        {
            poBlock = ((JP2OpenJPEGRasterBand*)GetRasterBand(iBand))->
                TryGetLockedBlockRef(nBlockXOff,nBlockYOff);
            if (poBlock != NULL)
            {
                poBlock->DropLock();
                continue;
            }

            poBlock = GetRasterBand(iBand)->
                GetLockedBlockRef(nBlockXOff,nBlockYOff, TRUE);
            if (poBlock == NULL)
            {
                continue;
            }

            pDstBuffer = poBlock->GetDataRef();
            if (!pDstBuffer)
            {
                poBlock->DropLock();
                continue;
            }
        }

        if (bIs420)
        {
            CPLAssert((int)psImage->comps[0].w >= nWidthToRead);
            CPLAssert((int)psImage->comps[0].h >= nHeightToRead);
            CPLAssert(psImage->comps[1].w == (psImage->comps[0].w + 1) / 2);
            CPLAssert(psImage->comps[1].h == (psImage->comps[0].h + 1) / 2);
            CPLAssert(psImage->comps[2].w == (psImage->comps[0].w + 1) / 2);
            CPLAssert(psImage->comps[2].h == (psImage->comps[0].h + 1) / 2);

            OPJ_INT32* pSrcY = psImage->comps[0].data;
            OPJ_INT32* pSrcCb = psImage->comps[1].data;
            OPJ_INT32* pSrcCr = psImage->comps[2].data;
            GByte* pDst = (GByte*)pDstBuffer;
            for(int j=0;j<nHeightToRead;j++)
            {
                for(int i=0;i<nWidthToRead;i++)
                {
                    int Y = pSrcY[j * psImage->comps[0].w + i];
                    int Cb = pSrcCb[(j/2) * psImage->comps[1].w + (i/2)];
                    int Cr = pSrcCr[(j/2) * psImage->comps[2].w + (i/2)];
                    if (iBand == 1)
                        pDst[j * nBlockXSize + i] = CLAMP_0_255((int)(Y + 1.402 * (Cr - 128)));
                    else if (iBand == 2)
                        pDst[j * nBlockXSize + i] = CLAMP_0_255((int)(Y - 0.34414 * (Cb - 128) - 0.71414 * (Cr - 128)));
                    else
                        pDst[j * nBlockXSize + i] = CLAMP_0_255((int)(Y + 1.772 * (Cb - 128)));
                }
            }
        }
        else
        {
            CPLAssert((int)psImage->comps[iBand-1].w >= nWidthToRead);
            CPLAssert((int)psImage->comps[iBand-1].h >= nHeightToRead);
            
            if( bPromoteTo8Bit )
            {
                for(int j=0;j<nHeightToRead;j++)
                {
                    for(int i=0;i<nWidthToRead;i++)
                    {
                        psImage->comps[iBand-1].data[j * psImage->comps[iBand-1].w + i] *= 255;
                    }
                }
            }

            if ((int)psImage->comps[iBand-1].w == nBlockXSize &&
                (int)psImage->comps[iBand-1].h == nBlockYSize)
            {
                GDALCopyWords(psImage->comps[iBand-1].data, GDT_Int32, 4,
                            pDstBuffer, eDataType, nDataTypeSize, nBlockXSize * nBlockYSize);
            }
            else
            {
                for(int j=0;j<nHeightToRead;j++)
                {
                    GDALCopyWords(psImage->comps[iBand-1].data + j * psImage->comps[iBand-1].w, GDT_Int32, 4,
                                (GByte*)pDstBuffer + j * nBlockXSize * nDataTypeSize, eDataType, nDataTypeSize,
                                nWidthToRead);
                }
            }
        }

        if (poBlock != NULL)
            poBlock->DropLock();
    }

end:
    opj_end_decompress(pCodec,pStream);
    opj_stream_destroy(pStream);
    opj_destroy_codec(pCodec);
    opj_image_destroy(psImage);

    return eErr;
}


/************************************************************************/
/*                         GetOverviewCount()                           */
/************************************************************************/

int JP2OpenJPEGRasterBand::GetOverviewCount()
{
    JP2OpenJPEGDataset *poGDS = (JP2OpenJPEGDataset *) poDS;
    return poGDS->nOverviewCount;
}

/************************************************************************/
/*                            GetOverview()                             */
/************************************************************************/

GDALRasterBand* JP2OpenJPEGRasterBand::GetOverview(int iOvrLevel)
{
    JP2OpenJPEGDataset *poGDS = (JP2OpenJPEGDataset *) poDS;
    if (iOvrLevel < 0 || iOvrLevel >= poGDS->nOverviewCount)
        return NULL;

    return poGDS->papoOverviewDS[iOvrLevel]->GetRasterBand(nBand);
}

/************************************************************************/
/*                       GetColorInterpretation()                       */
/************************************************************************/

GDALColorInterp JP2OpenJPEGRasterBand::GetColorInterpretation()
{
    JP2OpenJPEGDataset *poGDS = (JP2OpenJPEGDataset *) poDS;

    if( poCT )
        return GCI_PaletteIndex;

    if (poGDS->eColorSpace == OPJ_CLRSPC_GRAY)
        return GCI_GrayIndex;
    else if (poGDS->nBands == 3 || poGDS->nBands == 4)
    {
        switch(nBand)
        {
            case 1:
                return GCI_RedBand;
            case 2:
                return GCI_GreenBand;
            case 3:
                return GCI_BlueBand;
            case 4:
                return GCI_AlphaBand;
            default:
                return GCI_Undefined;
        }
    }

    return GCI_Undefined;
}

/************************************************************************/
/* ==================================================================== */
/*                           JP2OpenJPEGDataset                         */
/* ==================================================================== */
/************************************************************************/

/************************************************************************/
/*                        JP2OpenJPEGDataset()                          */
/************************************************************************/

JP2OpenJPEGDataset::JP2OpenJPEGDataset()
{
    fp = NULL;
    nBands = 0;
    eCodecFormat = OPJ_CODEC_UNKNOWN;
    eColorSpace = OPJ_CLRSPC_UNKNOWN;
    bIs420 = FALSE;
    iLevel = 0;
    nOverviewCount = 0;
    papoOverviewDS = NULL;
    bUseSetDecodeArea = FALSE;
    nThreads = -1;
    bEnoughMemoryToLoadOtherBands = TRUE;
}

/************************************************************************/
/*                         ~JP2OpenJPEGDataset()                        */
/************************************************************************/

JP2OpenJPEGDataset::~JP2OpenJPEGDataset()

{
    FlushCache();

    if( fp != NULL )
        VSIFCloseL( fp );

    CloseDependentDatasets();
}

/************************************************************************/
/*                      CloseDependentDatasets()                        */
/************************************************************************/

int JP2OpenJPEGDataset::CloseDependentDatasets()
{
    int bRet = GDALPamDataset::CloseDependentDatasets();
    if ( papoOverviewDS )
    {
        for( int i = 0; i < nOverviewCount; i++ )
            delete papoOverviewDS[i];
        CPLFree( papoOverviewDS );
        papoOverviewDS = NULL;
        bRet = TRUE;
    }
    return bRet;
}

/************************************************************************/
/*                            Identify()                                */
/************************************************************************/

int JP2OpenJPEGDataset::Identify( GDALOpenInfo * poOpenInfo )

{
    static const unsigned char jpc_header[] = {0xff,0x4f};
    static const unsigned char jp2_box_jp[] = {0x6a,0x50,0x20,0x20}; /* 'jP  ' */
        
    if( poOpenInfo->nHeaderBytes >= 16 
        && (memcmp( poOpenInfo->pabyHeader, jpc_header, 
                    sizeof(jpc_header) ) == 0
            || memcmp( poOpenInfo->pabyHeader + 4, jp2_box_jp, 
                    sizeof(jp2_box_jp) ) == 0
           ) )
        return TRUE;
    
    else
        return FALSE;
}
/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *JP2OpenJPEGDataset::Open( GDALOpenInfo * poOpenInfo )

{
    if (!Identify(poOpenInfo))
        return NULL;

    VSILFILE* fp = VSIFOpenL(poOpenInfo->pszFilename, "rb");
    if (!fp)
        return NULL;

    OPJ_CODEC_FORMAT eCodecFormat;

    /* Detect which codec to use : J2K or JP2 ? */
    static const unsigned char jpc_header[] = {0xff,0x4f};
    if (memcmp( poOpenInfo->pabyHeader, jpc_header, 
                    sizeof(jpc_header) ) == 0)
        eCodecFormat = OPJ_CODEC_J2K;
    else
        eCodecFormat = OPJ_CODEC_JP2;

    opj_codec_t* pCodec;

    pCodec = opj_create_decompress(eCodecFormat);

    opj_set_info_handler(pCodec, JP2OpenJPEGDataset_InfoCallback,NULL);
    opj_set_warning_handler(pCodec, JP2OpenJPEGDataset_WarningCallback, NULL);
    opj_set_error_handler(pCodec, JP2OpenJPEGDataset_ErrorCallback,NULL);

    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);

    if (! opj_setup_decoder(pCodec,&parameters))
    {
        VSIFCloseL(fp);
        return NULL;
    }

    opj_stream_t * pStream;
    pStream = opj_stream_create(1024, TRUE); // Default 1MB is way too big for some datasets

    VSIFSeekL(fp, 0, SEEK_END);
    opj_stream_set_user_data_length(pStream, VSIFTellL(fp));
    /* Reseek to file beginning */
    VSIFSeekL(fp, 0, SEEK_SET);

    opj_stream_set_read_function(pStream, JP2OpenJPEGDataset_Read);
    opj_stream_set_seek_function(pStream, JP2OpenJPEGDataset_Seek);
    opj_stream_set_skip_function(pStream, JP2OpenJPEGDataset_Skip);
#if defined(OPENJPEG_VERSION) && OPENJPEG_VERSION >= 20100
    opj_stream_set_user_data(pStream, fp, NULL);
#else
    opj_stream_set_user_data(pStream, fp);
#endif

    opj_image_t * psImage = NULL;
    OPJ_INT32  nX0,nY0;
    OPJ_UINT32 nTileW,nTileH;
#ifdef DEBUG
    OPJ_UINT32 nTilesX,nTilesY;
#endif
    if(!opj_read_header(pStream,pCodec,&psImage))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "opj_read_header() failed");
        opj_destroy_codec(pCodec);
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        VSIFCloseL(fp);
        return NULL;
    }

    opj_codestream_info_v2_t* pCodeStreamInfo = opj_get_cstr_info(pCodec);
    nX0 = pCodeStreamInfo->tx0;
    nY0 = pCodeStreamInfo->ty0;
    nTileW = pCodeStreamInfo->tdx;
    nTileH = pCodeStreamInfo->tdy;
#ifdef DEBUG
    nTilesX = pCodeStreamInfo->tw;
    nTilesY = pCodeStreamInfo->th;
#endif
    int mct = pCodeStreamInfo->m_default_tile_info.mct;
    int numResolutions = pCodeStreamInfo->m_default_tile_info.tccp_info[0].numresolutions;
    opj_destroy_cstr_info(&pCodeStreamInfo);

    if (psImage == NULL)
    {
        opj_destroy_codec(pCodec);
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        VSIFCloseL(fp);
        return NULL;
    }

#ifdef DEBUG
    int i;
    CPLDebug("OPENJPEG", "nX0 = %d", nX0);
    CPLDebug("OPENJPEG", "nY0 = %d", nY0);
    CPLDebug("OPENJPEG", "nTileW = %d", nTileW);
    CPLDebug("OPENJPEG", "nTileH = %d", nTileH);
    CPLDebug("OPENJPEG", "nTilesX = %d", nTilesX);
    CPLDebug("OPENJPEG", "nTilesY = %d", nTilesY);
    CPLDebug("OPENJPEG", "mct = %d", mct);
    CPLDebug("OPENJPEG", "psImage->x0 = %d", psImage->x0);
    CPLDebug("OPENJPEG", "psImage->y0 = %d", psImage->y0);
    CPLDebug("OPENJPEG", "psImage->x1 = %d", psImage->x1);
    CPLDebug("OPENJPEG", "psImage->y1 = %d", psImage->y1);
    CPLDebug("OPENJPEG", "psImage->numcomps = %d", psImage->numcomps);
    CPLDebug("OPENJPEG", "psImage->color_space = %d", psImage->color_space);
    CPLDebug("OPENJPEG", "numResolutions = %d", numResolutions);
    for(i=0;i<(int)psImage->numcomps;i++)
    {
        CPLDebug("OPENJPEG", "psImage->comps[%d].dx = %d", i, psImage->comps[i].dx);
        CPLDebug("OPENJPEG", "psImage->comps[%d].dy = %d", i, psImage->comps[i].dy);
        CPLDebug("OPENJPEG", "psImage->comps[%d].x0 = %d", i, psImage->comps[i].x0);
        CPLDebug("OPENJPEG", "psImage->comps[%d].y0 = %d", i, psImage->comps[i].y0);
        CPLDebug("OPENJPEG", "psImage->comps[%d].w = %d", i, psImage->comps[i].w);
        CPLDebug("OPENJPEG", "psImage->comps[%d].h = %d", i, psImage->comps[i].h);
        CPLDebug("OPENJPEG", "psImage->comps[%d].resno_decoded = %d", i, psImage->comps[i].resno_decoded);
        CPLDebug("OPENJPEG", "psImage->comps[%d].factor = %d", i, psImage->comps[i].factor);
        CPLDebug("OPENJPEG", "psImage->comps[%d].prec = %d", i, psImage->comps[i].prec);
        CPLDebug("OPENJPEG", "psImage->comps[%d].sgnd = %d", i, psImage->comps[i].sgnd);
    }
#endif

    if (psImage->x1 <= psImage->x0 ||
        psImage->y1 <= psImage->y0 ||
        psImage->numcomps == 0 ||
        psImage->comps[0].w != psImage->x1 - psImage->x0 ||
        psImage->comps[0].h != psImage->y1 - psImage->y0)
    {
        CPLDebug("OPENJPEG", "Unable to handle that image (1)");
        opj_destroy_codec(pCodec);
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        VSIFCloseL(fp);
        return NULL;
    }

    GDALDataType eDataType = GDT_Byte;
    if (psImage->comps[0].prec > 16)
    {
        if (psImage->comps[0].sgnd)
            eDataType = GDT_Int32;
        else
            eDataType = GDT_UInt32;
    }
    else if (psImage->comps[0].prec > 8)
    {
        if (psImage->comps[0].sgnd)
            eDataType = GDT_Int16;
        else
            eDataType = GDT_UInt16;
    }

    int bIs420  =  (psImage->color_space != OPJ_CLRSPC_SRGB &&
                    eDataType == GDT_Byte &&
                    psImage->numcomps == 3 &&
                    psImage->comps[1].w == psImage->comps[0].w / 2 &&
                    psImage->comps[1].h == psImage->comps[0].h / 2 &&
                    psImage->comps[2].w == psImage->comps[0].w / 2 &&
                    psImage->comps[2].h == psImage->comps[0].h / 2);

    if (bIs420)
    {
        CPLDebug("OPENJPEG", "420 format");
    }
    else
    {
        int iBand;
        for(iBand = 2; iBand <= (int)psImage->numcomps; iBand ++)
        {
            if( psImage->comps[iBand-1].w != psImage->comps[0].w ||
                psImage->comps[iBand-1].h != psImage->comps[0].h )
            {
                CPLDebug("OPENJPEG", "Unable to handle that image (2)");
                opj_destroy_codec(pCodec);
                opj_stream_destroy(pStream);
                opj_image_destroy(psImage);
                VSIFCloseL(fp);
                return NULL;
            }
        }
    }


/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    JP2OpenJPEGDataset     *poDS;
    int                 iBand;

    poDS = new JP2OpenJPEGDataset();
    poDS->eCodecFormat = eCodecFormat;
    poDS->eColorSpace = psImage->color_space;
    poDS->nRasterXSize = psImage->x1 - psImage->x0;
    poDS->nRasterYSize = psImage->y1 - psImage->y0;
    poDS->nBands = psImage->numcomps;
    poDS->fp = fp;
    poDS->bIs420 = bIs420;

    poDS->bUseSetDecodeArea = 
        (poDS->nRasterXSize == (int)nTileW &&
         poDS->nRasterYSize == (int)nTileH &&
         (poDS->nRasterXSize > 1024 ||
          poDS->nRasterYSize > 1024));

    if (poDS->bUseSetDecodeArea)
    {
        if (nTileW > 1024) nTileW = 1024;
        if (nTileH > 1024) nTileH = 1024;
    }

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    JP2OpenJPEGRasterBand* poFirstBand = NULL;

    for( iBand = 1; iBand <= poDS->nBands; iBand++ )
    {
        int bPromoteTo8Bit = (
            iBand == 4 && poDS->nBands == 4 &&
            psImage->comps[0].prec == 8 &&
            psImage->comps[1].prec == 8 &&
            psImage->comps[2].prec == 8 &&
            psImage->comps[3].prec == 1 && 
            CSLFetchBoolean(poOpenInfo->papszOpenOptions, "1BIT_ALPHA_PROMOTION",
                    CSLTestBoolean(CPLGetConfigOption("JP2OPENJPEG_PROMOTE_1BIT_ALPHA_AS_8BIT", "YES"))) );
        if( bPromoteTo8Bit )
            CPLDebug("JP2OpenJPEG", "Fourth (alpha) band is promoted from 1 bit to 8 bit");

        JP2OpenJPEGRasterBand* poBand =
            new JP2OpenJPEGRasterBand( poDS, iBand, eDataType,
                                        bPromoteTo8Bit ? 8: psImage->comps[iBand-1].prec,
                                        bPromoteTo8Bit,
                                        nTileW, nTileH);
        if( iBand == 1 )
            poFirstBand = poBand;
        poDS->SetBand( iBand, poBand );
    }

/* -------------------------------------------------------------------- */
/*      Look for color table.                                           */
/* -------------------------------------------------------------------- */
    if( eDataType == GDT_Byte && poDS->nBands == 1 )
    {
        GDALJP2Box oBox( fp );
        if( oBox.ReadFirst() )
        {
            while( strlen(oBox.GetType()) > 0 )
            {
                if( EQUAL(oBox.GetType(),"jp2h") )
                {
                    GDALJP2Box oSubBox( fp );

                    for( oSubBox.ReadFirstChild( &oBox );
                         strlen(oSubBox.GetType()) > 0;
                         oSubBox.ReadNextChild( &oBox ) )
                    {
                        GIntBig nDataLength = oSubBox.GetDataLength();
                        if( EQUAL(oSubBox.GetType(),"pclr") &&
                            nDataLength >= 3 &&
                            nDataLength <= 2 + 1 + 3 + 3 * 256 )
                        {
                            GByte* pabyCT = oSubBox.ReadBoxData();
                            if( pabyCT != NULL )
                            {
                                int nEntries = (pabyCT[0] << 8) | pabyCT[1];
                                int nComponents = pabyCT[2];
                                /* CPLDebug("OPENJPEG", "Color table found"); */
                                if( nEntries <= 256 && nComponents == 3 )
                                {
                                    /*CPLDebug("OPENJPEG", "resol[0] = %d", pabyCT[3]);
                                    CPLDebug("OPENJPEG", "resol[1] = %d", pabyCT[4]);
                                    CPLDebug("OPENJPEG", "resol[2] = %d", pabyCT[5]);*/
                                    if( pabyCT[3] == 7 && pabyCT[4] == 7 && pabyCT[5] == 7 &&
                                        nDataLength == 2 + 1 + 3 + 3 * nEntries )
                                    {
                                        GDALColorTable* poCT = new GDALColorTable();
                                        for(int i=0;i<nEntries;i++)
                                        {
                                            GDALColorEntry sEntry;
                                            sEntry.c1 = pabyCT[6 + 3 * i];
                                            sEntry.c2 = pabyCT[6 + 3 * i + 1];
                                            sEntry.c3 = pabyCT[6 + 3 * i + 2];
                                            sEntry.c4 = 255;
                                            poCT->SetColorEntry(i, &sEntry);
                                        }
                                        poFirstBand->poCT = poCT;
                                    }
                                }
                                CPLFree(pabyCT);
                            }
                            break;
                        }
                    }
                }

                if (!oBox.ReadNext())
                    break;
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Create overview datasets.                                       */
/* -------------------------------------------------------------------- */
    int nW = poDS->nRasterXSize;
    int nH = poDS->nRasterYSize;
    while (poDS->nOverviewCount+1 < numResolutions &&
           (nW > 256 || nH > 256) &&
           (poDS->bUseSetDecodeArea || ((nTileW % 2) == 0 && (nTileH % 2) == 0)))
    {
        nW /= 2;
        nH /= 2;

        VSILFILE* fpOvr = VSIFOpenL(poOpenInfo->pszFilename, "rb");
        if (!fpOvr)
            break;

        poDS->papoOverviewDS = (JP2OpenJPEGDataset**) CPLRealloc(
                    poDS->papoOverviewDS,
                    (poDS->nOverviewCount + 1) * sizeof(JP2OpenJPEGDataset*));
        JP2OpenJPEGDataset* poODS = new JP2OpenJPEGDataset();
        poODS->SetDescription( poOpenInfo->pszFilename );
        poODS->iLevel = poDS->nOverviewCount + 1;
        poODS->bUseSetDecodeArea = poDS->bUseSetDecodeArea;
        if (!poDS->bUseSetDecodeArea)
        {
            nTileW /= 2;
            nTileH /= 2;
        }
        else
        {
            if (nW < (int)nTileW || nH < (int)nTileH)
            {
                nTileW = nW;
                nTileH = nH;
                poODS->bUseSetDecodeArea = FALSE;
            }
        }

        poODS->eCodecFormat = poDS->eCodecFormat;
        poODS->eColorSpace = poDS->eColorSpace;
        poODS->nRasterXSize = nW;
        poODS->nRasterYSize = nH;
        poODS->nBands = poDS->nBands;
        poODS->fp = fpOvr;
        poODS->bIs420 = bIs420;
        for( iBand = 1; iBand <= poDS->nBands; iBand++ )
        {
            int bPromoteTo8Bit = (
                iBand == 4 && poDS->nBands == 4 &&
                psImage->comps[0].prec == 8 &&
                psImage->comps[1].prec == 8 &&
                psImage->comps[2].prec == 8 &&
                psImage->comps[3].prec == 1 && 
                CSLTestBoolean(CPLGetConfigOption("JP2OPENJPEG_PROMOTE_1BIT_ALPHA_AS_8BIT", "YES")) );

            poODS->SetBand( iBand, new JP2OpenJPEGRasterBand( poODS, iBand, eDataType,
                                                              bPromoteTo8Bit ? 8: psImage->comps[iBand-1].prec,
                                                              bPromoteTo8Bit,
                                                              nTileW, nTileH ) );
        }

        poDS->papoOverviewDS[poDS->nOverviewCount ++] = poODS;

    }

    opj_destroy_codec(pCodec);
    opj_stream_destroy(pStream);
    opj_image_destroy(psImage);
    pCodec = NULL;
    pStream = NULL;
    psImage = NULL;

/* -------------------------------------------------------------------- */
/*      More metadata.                                                  */
/* -------------------------------------------------------------------- */
    if( poDS->nBands > 1 )
    {
        poDS->SetMetadataItem( "INTERLEAVE", "PIXEL", "IMAGE_STRUCTURE" );
    }

    poDS->LoadJP2Metadata(poOpenInfo);

/* -------------------------------------------------------------------- */
/*      Initialize any PAM information.                                 */
/* -------------------------------------------------------------------- */
    poDS->SetDescription( poOpenInfo->pszFilename );
    poDS->TryLoadXML();

/* -------------------------------------------------------------------- */
/*      Check for overviews.                                            */
/* -------------------------------------------------------------------- */
    //poDS->oOvManager.Initialize( poDS, poOpenInfo->pszFilename );

    return( poDS );
}

/************************************************************************/
/*                           WriteBox()                                 */
/************************************************************************/

void JP2OpenJPEGDataset::WriteBox(VSILFILE* fp, GDALJP2Box* poBox)
{
    GUInt32   nLBox;
    GUInt32   nTBox;

    if( poBox == NULL )
        return;

    nLBox = (int) poBox->GetDataLength() + 8;
    nLBox = CPL_MSBWORD32( nLBox );

    memcpy(&nTBox, poBox->GetType(), 4);

    VSIFWriteL( &nLBox, 4, 1, fp );
    VSIFWriteL( &nTBox, 4, 1, fp );
    VSIFWriteL(poBox->GetWritableData(), 1, (int) poBox->GetDataLength(), fp);
}

/************************************************************************/
/*                          CreateCopy()                                */
/************************************************************************/

GDALDataset * JP2OpenJPEGDataset::CreateCopy( const char * pszFilename,
                                           GDALDataset *poSrcDS, 
                                           int bStrict, char ** papszOptions, 
                                           GDALProgressFunc pfnProgress,
                                           void * pProgressData )

{
    int  nBands = poSrcDS->GetRasterCount();
    int  nXSize = poSrcDS->GetRasterXSize();
    int  nYSize = poSrcDS->GetRasterYSize();

    if( nBands != 1 && nBands != 3 /* && nBands != 4 */ )
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Unable to export files with %d bands.", nBands );
        return NULL;
    }

    if (poSrcDS->GetRasterBand(1)->GetColorTable() != NULL)
    {
        CPLError( (bStrict) ? CE_Failure : CE_Warning, CPLE_NotSupported, 
                  "JP2OpenJPEG driver ignores color table. "
                  "The source raster band will be considered as grey level.\n"
                  "Consider using color table expansion (-expand option in gdal_translate)\n");
        if (bStrict)
            return NULL;
    }

    GDALDataType eDataType = poSrcDS->GetRasterBand(1)->GetRasterDataType();
    int nDataTypeSize = (GDALGetDataTypeSize(eDataType) / 8);
    if (eDataType != GDT_Byte && eDataType != GDT_Int16 && eDataType != GDT_UInt16
        && eDataType != GDT_Int32 && eDataType != GDT_UInt32)
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "JP2OpenJPEG driver only supports creating Byte, GDT_Int16, GDT_UInt16, GDT_Int32, GDT_UInt32");
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Analyze creation options.                                       */
/* -------------------------------------------------------------------- */
    OPJ_CODEC_FORMAT eCodecFormat = OPJ_CODEC_J2K;
    const char* pszCodec = CSLFetchNameValueDef(papszOptions, "CODEC", NULL);
    if (pszCodec)
    {
        if (EQUAL(pszCodec, "JP2"))
            eCodecFormat = OPJ_CODEC_JP2;
        else if (EQUAL(pszCodec, "J2K"))
            eCodecFormat = OPJ_CODEC_J2K;
        else
        {
            CPLError(CE_Warning, CPLE_NotSupported,
                    "Unsupported value for CODEC : %s. Defaulting to J2K",
                    pszCodec);
        }
    }
    else
    {
        if (strlen(pszFilename) > 4 &&
            EQUAL(pszFilename + strlen(pszFilename) - 4, ".JP2"))
        {
            eCodecFormat = OPJ_CODEC_JP2;
        }
    }

    int nBlockXSize =
        atoi(CSLFetchNameValueDef(papszOptions, "BLOCKXSIZE", "1024"));
    int nBlockYSize =
        atoi(CSLFetchNameValueDef(papszOptions, "BLOCKYSIZE", "1024"));
    if (nBlockXSize < 32 || nBlockYSize < 32)
    {
        CPLError(CE_Failure, CPLE_NotSupported, "Invalid block size");
        return NULL;
    }

    if (nXSize < nBlockXSize)
        nBlockXSize = nXSize;
    if (nYSize < nBlockYSize)
        nBlockYSize = nYSize;

    OPJ_PROG_ORDER eProgOrder = OPJ_LRCP;
    const char* pszPROGORDER =
            CSLFetchNameValueDef(papszOptions, "PROGRESSION", "LRCP");
    if (EQUAL(pszPROGORDER, "LRCP"))
        eProgOrder = OPJ_LRCP;
    else if (EQUAL(pszPROGORDER, "RLCP"))
        eProgOrder = OPJ_RLCP;
    else if (EQUAL(pszPROGORDER, "RPCL"))
        eProgOrder = OPJ_RPCL;
    else if (EQUAL(pszPROGORDER, "PCRL"))
        eProgOrder = OPJ_PCRL;
    else if (EQUAL(pszPROGORDER, "CPRL"))
        eProgOrder = OPJ_CPRL;
    else
    {
        CPLError(CE_Warning, CPLE_NotSupported,
                 "Unsupported value for PROGRESSION : %s. Defaulting to LRCP",
                 pszPROGORDER);
    }

    int bIsIrreversible =
            ! (CSLTestBoolean(CSLFetchNameValueDef(papszOptions, "REVERSIBLE", "NO")));

    double dfRate = 100. / 25;
    const char* pszQuality = CSLFetchNameValueDef(papszOptions, "QUALITY", NULL);
    if (pszQuality)
    {
        double dfQuality = CPLAtof(pszQuality);
        if (dfQuality > 0 && dfQuality <= 100)
        {
            dfRate = 100 / dfQuality;
        }
        else
        {
            CPLError(CE_Warning, CPLE_NotSupported,
                 "Unsupported value for QUALITY : %s. Defaulting to 25",
                 pszQuality);
        }
    }

    int nNumResolutions = 6;
    const char* pszResolutions = CSLFetchNameValueDef(papszOptions, "RESOLUTIONS", NULL);
    if (pszResolutions)
    {
        nNumResolutions = atoi(pszResolutions);
        if (nNumResolutions < 1 || nNumResolutions > 7)
        {
            nNumResolutions = 6;
            CPLError(CE_Warning, CPLE_NotSupported,
                 "Unsupported value for RESOLUTIONS : %s. Defaulting to 6",
                 pszResolutions);
        }
    }
    
    int bSOP = CSLTestBoolean(CSLFetchNameValueDef(papszOptions, "SOP", "FALSE"));
    int bEPH = CSLTestBoolean(CSLFetchNameValueDef(papszOptions, "EPH", "FALSE"));

    int bResample = (nBands == 3 && eDataType == GDT_Byte &&
            CSLTestBoolean(CSLFetchNameValueDef(papszOptions, "YCBCR420", "FALSE")));
    if (bResample && !((nXSize % 2) == 0 && (nYSize % 2) == 0 && (nBlockXSize % 2) == 0 && (nBlockYSize % 2) == 0))
    {
        CPLError(CE_Warning, CPLE_NotSupported,
                 "YCBCR420 unsupported when image size and/or tile size are not multiple of 2");
        bResample = FALSE;
    }

    const char* pszYCC = CSLFetchNameValue(papszOptions, "YCC");
    int bYCC = (nBands == 3 && eDataType == GDT_Byte &&
            CSLTestBoolean(CSLFetchNameValueDef(papszOptions, "YCC", "TRUE")));
    if( bResample && bYCC )
    {
        if( pszYCC != NULL )
        {
            CPLError(CE_Warning, CPLE_NotSupported,
                    "YCC unsupported when YCbCr requesting");
        }
        bYCC = FALSE;
    }

/* -------------------------------------------------------------------- */
/*      Setup encoder                                                  */
/* -------------------------------------------------------------------- */

    opj_cparameters_t parameters;
    opj_set_default_encoder_parameters(&parameters);
    if (bSOP)
        parameters.csty |= 0x02;
    if (bEPH)
        parameters.csty |= 0x04;
    parameters.cp_disto_alloc = 1;
    parameters.tcp_numlayers = 1;
    parameters.tcp_rates[0] = (float) dfRate;
    parameters.cp_tx0 = 0;
    parameters.cp_ty0 = 0;
    parameters.tile_size_on = TRUE;
    parameters.cp_tdx = nBlockXSize;
    parameters.cp_tdy = nBlockYSize;
    parameters.irreversible = bIsIrreversible;
    parameters.numresolution = nNumResolutions;
    parameters.prog_order = eProgOrder;
    parameters.tcp_mct = bYCC;

    opj_image_cmptparm_t* pasBandParams =
            (opj_image_cmptparm_t*)CPLMalloc(nBands * sizeof(opj_image_cmptparm_t));
    int iBand;
    for(iBand=0;iBand<nBands;iBand++)
    {
        pasBandParams[iBand].x0 = 0;
        pasBandParams[iBand].y0 = 0;
        if (bResample && iBand > 0)
        {
            pasBandParams[iBand].dx = 2;
            pasBandParams[iBand].dy = 2;
            pasBandParams[iBand].w = nXSize / 2;
            pasBandParams[iBand].h = nYSize / 2;
        }
        else
        {
            pasBandParams[iBand].dx = 1;
            pasBandParams[iBand].dy = 1;
            pasBandParams[iBand].w = nXSize;
            pasBandParams[iBand].h = nYSize;
        }
        pasBandParams[iBand].sgnd = (eDataType == GDT_Int16 || eDataType == GDT_Int32);
        pasBandParams[iBand].prec = 8 * nDataTypeSize;
    }
    int bSamePrecision = TRUE; // FIXME

    /* Always ask OpenJPEG to do codestream only. We will take care */
    /* of JP2 boxes */
    opj_codec_t* pCodec = opj_create_compress(OPJ_CODEC_J2K);
    if (pCodec == NULL)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "opj_create_compress() failed");
        CPLFree(pasBandParams);
        return NULL;
    }

    opj_set_info_handler(pCodec, JP2OpenJPEGDataset_InfoCallback,NULL);
    opj_set_warning_handler(pCodec, JP2OpenJPEGDataset_WarningCallback,NULL);
    opj_set_error_handler(pCodec, JP2OpenJPEGDataset_ErrorCallback,NULL);

    OPJ_COLOR_SPACE eColorSpace = (bResample) ? OPJ_CLRSPC_SYCC : (nBands == 3 || nBands == 4) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;

    opj_image_t* psImage = opj_image_tile_create(nBands,pasBandParams,
                                                 eColorSpace);

    if (psImage == NULL)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "opj_image_tile_create() failed");
        opj_destroy_codec(pCodec);
        CPLFree(pasBandParams);
        pasBandParams = NULL;
        return NULL;
    }

    psImage->x0 = 0;
    psImage->y0 = 0;
    psImage->x1 = nXSize;
    psImage->y1 = nYSize;
    psImage->color_space = eColorSpace;
    psImage->numcomps = nBands;

    if (!opj_setup_encoder(pCodec,&parameters,psImage))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "opj_setup_encoder() failed");
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        CPLFree(pasBandParams);
        pasBandParams = NULL;
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Setup GML and GeoTIFF information.                              */
/* -------------------------------------------------------------------- */
    GDALJP2Metadata oJP2MD;

    int bWriteExtraBoxes = FALSE;
    int bHasGeoreferencing = FALSE;
    int bGeoreferencingCompatOfGMLJP2 = FALSE;
    if( eCodecFormat == OPJ_CODEC_JP2 &&
        (CSLFetchBoolean( papszOptions, "GMLJP2", TRUE ) ||
         CSLFetchBoolean( papszOptions, "GeoJP2", TRUE )) )
    {
        if( poSrcDS->GetGCPCount() > 0 )
        {
            bWriteExtraBoxes = TRUE;
            bHasGeoreferencing = TRUE;
            oJP2MD.SetGCPs( poSrcDS->GetGCPCount(),
                            poSrcDS->GetGCPs() );
            oJP2MD.SetProjection( poSrcDS->GetGCPProjection() );
        }
        else
        {
            const char* pszWKT = poSrcDS->GetProjectionRef();
            if( pszWKT != NULL && pszWKT[0] != '\0' )
            {
                bGeoreferencingCompatOfGMLJP2 = TRUE;
                bHasGeoreferencing = TRUE;
                bWriteExtraBoxes = TRUE;
                oJP2MD.SetProjection( pszWKT );
            }
            double adfGeoTransform[6];
            if( poSrcDS->GetGeoTransform( adfGeoTransform ) == CE_None )
            {
                bGeoreferencingCompatOfGMLJP2 = TRUE;
                bHasGeoreferencing = TRUE;
                bWriteExtraBoxes = TRUE;
                oJP2MD.SetGeoTransform( adfGeoTransform );
            }
        }

        const char* pszAreaOrPoint = poSrcDS->GetMetadataItem(GDALMD_AREA_OR_POINT);
        oJP2MD.bPixelIsPoint = pszAreaOrPoint != NULL && EQUAL(pszAreaOrPoint, GDALMD_AOP_POINT);
    }

/* -------------------------------------------------------------------- */
/*      Create the dataset.                                             */
/* -------------------------------------------------------------------- */

    const char* pszAccess = EQUALN(pszFilename, "/vsisubfile/", 12) ? "r+b" : "w+b";
    VSILFILE* fp = VSIFOpenL(pszFilename, pszAccess);
    if (fp == NULL)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Cannot create file");
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Add JP2 boxes.                                                  */
/* -------------------------------------------------------------------- */
    vsi_l_offset nStartJP2C;
    int bUseXLBoxes = FALSE;
    int bGeoBoxesAfter = CSLFetchBoolean(papszOptions, "GEOBOXES_AFTER_JP2C", FALSE);

    if( eCodecFormat == OPJ_CODEC_JP2  )
    {
        GDALJP2Box jPBox(fp);
        jPBox.SetType("jP  ");
        jPBox.AppendWritableData(4, "\x0D\x0A\x87\x0A");
        WriteBox(fp, &jPBox);

        GDALJP2Box ftypBox(fp);
        ftypBox.SetType("ftyp");
        ftypBox.AppendWritableData(4, "jp2 "); /* Branding */
        ftypBox.AppendUInt32(0); /* minimum version */
        ftypBox.AppendWritableData(4, "jp2 "); /* Compatibility list: first value */
        if( bGeoreferencingCompatOfGMLJP2 &&
            CSLFetchBoolean( papszOptions, "GMLJP2", TRUE ) )
        {
            /* GMLJP2 uses lbl and asoc boxes, which are JPEG2000 Part II spec */
            /* advertizing jpx is required per 8.1 of 05-047r3 GMLJP2 */
            ftypBox.AppendWritableData(4, "jpx "); /* Compatibility list: second value */
        }
        WriteBox(fp, &ftypBox);

        GDALJP2Box ihdrBox(fp);
        ihdrBox.SetType("ihdr");
        ihdrBox.AppendUInt32(nXSize);
        ihdrBox.AppendUInt32(nYSize);
        ihdrBox.AppendUInt16(nBands);
        GByte BPC;
        if( bSamePrecision )
            BPC = (pasBandParams[0].prec-1) | (pasBandParams[0].sgnd << 7);
        else
            BPC = 255;
        ihdrBox.AppendUInt8(BPC);
        ihdrBox.AppendUInt8(7); /* C=Compression type: fixed value */
        ihdrBox.AppendUInt8(0); /* UnkC: 0= colourspace of the image is known */
                                /*and correctly specified in the Colourspace Specification boxes within the file */
        ihdrBox.AppendUInt8(0); /* IPR: 0=no intellectual property */

        GDALJP2Box bpccBox(fp);
        if( !bSamePrecision )
        {
            bpccBox.SetType("bpcc");
            for(int i=0;i<nBands;i++)
                bpccBox.AppendUInt8((pasBandParams[i].prec-1) | (pasBandParams[i].sgnd << 7));
        }

        GDALJP2Box colrBox(fp);
        colrBox.SetType("colr");
        colrBox.AppendUInt8(1); /* METHOD: 1=Enumerated Colourspace */
        colrBox.AppendUInt8(0); /* PREC: Precedence. 0=(field reserved for ISO use) */
        colrBox.AppendUInt8(0); /* APPROX: Colourspace approximation. */
        GUInt32 enumcs = 16;
        if( eColorSpace == OPJ_CLRSPC_SRGB )
            enumcs = 16;
        else if(  eColorSpace == OPJ_CLRSPC_GRAY )
            enumcs = 17;
        else if(  eColorSpace == OPJ_CLRSPC_SYCC )
            enumcs = 18;
        colrBox.AppendUInt32(enumcs); /* EnumCS: Enumerated colourspace */

        // Add res box if needed
        double dfXRes = 0, dfYRes = 0;
        int nResUnit = 0;
        GDALJP2Box* poRes = NULL;
        if( poSrcDS->GetMetadataItem("TIFFTAG_XRESOLUTION") != NULL
            && poSrcDS->GetMetadataItem("TIFFTAG_YRESOLUTION") != NULL
            && poSrcDS->GetMetadataItem("TIFFTAG_RESOLUTIONUNIT") != NULL )
        {
            dfXRes =
                CPLAtof(poSrcDS->GetMetadataItem("TIFFTAG_XRESOLUTION"));
            dfYRes =
                CPLAtof(poSrcDS->GetMetadataItem("TIFFTAG_YRESOLUTION"));
            nResUnit = atoi(poSrcDS->GetMetadataItem("TIFFTAG_RESOLUTIONUNIT"));
#define PIXELS_PER_INCH 2
#define PIXELS_PER_CM   3

            if( nResUnit == PIXELS_PER_INCH )
            {
                // convert pixels per inch to pixels per cm.
                dfXRes = dfXRes * 39.37 / 100.0;
                dfYRes = dfYRes * 39.37 / 100.0;
                nResUnit = PIXELS_PER_CM;
            }

            if( nResUnit == PIXELS_PER_CM &&
                dfXRes > 0 && dfYRes > 0 &&
                dfXRes < 65535 && dfYRes < 65535 )
            {
                /* Format a resd box and embed it inside a res box */
                GDALJP2Box oResd;
                oResd.SetType("resd");

                int nYDenom = 1;
                while (nYDenom < 32767 && dfYRes < 32767)
                {
                    dfYRes *= 2;
                    nYDenom *= 2;
                }
                int nXDenom = 1;
                while (nXDenom < 32767 && dfXRes < 32767)
                {
                    dfXRes *= 2;
                    nXDenom *= 2;
                }

                oResd.AppendUInt16((GUInt16)dfYRes);
                oResd.AppendUInt16((GUInt16)nYDenom);
                oResd.AppendUInt16((GUInt16)dfXRes);
                oResd.AppendUInt16((GUInt16)nXDenom);
                oResd.AppendUInt8(2); /* vertical exponent */
                oResd.AppendUInt8(2); /* horizontal exponent */

                GDALJP2Box* poResd = &oResd;
                poRes = GDALJP2Box::CreateAsocBox( 1, &poResd );
                poRes->SetType("res ");
            }
        }

        /* Build and write jp2h super box now */
        GDALJP2Box* apoBoxes[4];
        int nBoxes = 1;
        apoBoxes[0] = &ihdrBox;
        if( !bSamePrecision )
            apoBoxes[nBoxes++] = &bpccBox;
        apoBoxes[nBoxes++] = &colrBox;
        if( poRes )
            apoBoxes[nBoxes++] = poRes;
        GDALJP2Box* psJP2HBox = GDALJP2Box::CreateSuperBox( "jp2h",
                                                            nBoxes,
                                                            apoBoxes );
        WriteBox(fp, psJP2HBox);
        delete psJP2HBox;
        delete poRes;

        if( bHasGeoreferencing && !bGeoBoxesAfter )
        {
            if( CSLFetchBoolean( papszOptions, "GeoJP2", TRUE ) &&
                bHasGeoreferencing )
            {
                GDALJP2Box* poBox = oJP2MD.CreateJP2GeoTIFF();
                WriteBox(fp, poBox);
                delete poBox;
            }
            if( CSLFetchBoolean( papszOptions, "GMLJP2", TRUE ) &&
                bGeoreferencingCompatOfGMLJP2 )
            {
                GDALJP2Box* poBox = oJP2MD.CreateGMLJP2(nXSize,nYSize);
                WriteBox(fp, poBox);
                delete poBox;
            }
        }

        nStartJP2C = VSIFTellL(fp);
        bUseXLBoxes = CSLFetchBoolean(papszOptions, "JP2C_XLBOX", FALSE) || /* For debugging */
            (GIntBig)nXSize * nYSize * nBands * nDataTypeSize / dfRate > 4e9;
        GUInt32 nLBox = (bUseXLBoxes) ? 1 : 0;
        CPL_MSBPTR32(&nLBox);
        VSIFWriteL(&nLBox, 1, 4, fp);
        VSIFWriteL("jp2c", 1, 4, fp);
        if( bUseXLBoxes )
        {
            GUIntBig nXLBox = 0;
            VSIFWriteL(&nXLBox, 1, 8, fp);
        }
    }
    CPLFree(pasBandParams);
    pasBandParams = NULL;

    opj_stream_t * pStream;
    pStream = opj_stream_create(1024*1024, FALSE);
    opj_stream_set_write_function(pStream, JP2OpenJPEGDataset_Write);
    opj_stream_set_seek_function(pStream, JP2OpenJPEGDataset_Seek);
    opj_stream_set_skip_function(pStream, JP2OpenJPEGDataset_Skip);
#if defined(OPENJPEG_VERSION) && OPENJPEG_VERSION >= 20100
    opj_stream_set_user_data(pStream, fp, NULL);
#else
    opj_stream_set_user_data(pStream, fp);
#endif

    if (!opj_start_compress(pCodec,psImage,pStream))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "opj_start_compress() failed");
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        VSIFCloseL(fp);
        return NULL;
    }

    int nTilesX = (nXSize + nBlockXSize - 1) / nBlockXSize;
    int nTilesY = (nYSize + nBlockYSize - 1) / nBlockYSize;

    GByte* pTempBuffer =(GByte*)VSIMalloc(nBlockXSize * nBlockYSize *
                                          nBands * nDataTypeSize);
    if (pTempBuffer == NULL)
    {
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        VSIFCloseL(fp);
        return NULL;
    }

    GByte* pYUV420Buffer = NULL;
    if (bResample)
    {
        pYUV420Buffer =(GByte*)VSIMalloc(3 * nBlockXSize * nBlockYSize / 2);
        if (pYUV420Buffer == NULL)
        {
            opj_stream_destroy(pStream);
            opj_image_destroy(psImage);
            opj_destroy_codec(pCodec);
            CPLFree(pTempBuffer);
            VSIFCloseL(fp);
            return NULL;
        }
    }

/* -------------------------------------------------------------------- */
/*      Iterate over the tiles                                          */
/* -------------------------------------------------------------------- */
    pfnProgress( 0.0, NULL, pProgressData );

    CPLErr eErr = CE_None;
    int nBlockXOff, nBlockYOff;
    int iTile = 0;
    for(nBlockYOff=0;eErr == CE_None && nBlockYOff<nTilesY;nBlockYOff++)
    {
        for(nBlockXOff=0;eErr == CE_None && nBlockXOff<nTilesX;nBlockXOff++)
        {
            int nWidthToRead = MIN(nBlockXSize, nXSize - nBlockXOff * nBlockXSize);
            int nHeightToRead = MIN(nBlockYSize, nYSize - nBlockYOff * nBlockYSize);
            eErr = poSrcDS->RasterIO(GF_Read,
                                     nBlockXOff * nBlockXSize,
                                     nBlockYOff * nBlockYSize,
                                     nWidthToRead, nHeightToRead,
                                     pTempBuffer, nWidthToRead, nHeightToRead,
                                     eDataType,
                                     nBands, NULL,
                                     0,0,0,NULL);
            if (eErr == CE_None)
            {
                if (bResample)
                {
                    int j, i;
                    for(j=0;j<nHeightToRead;j++)
                    {
                        for(i=0;i<nWidthToRead;i++)
                        {
                            int R = pTempBuffer[j*nWidthToRead+i];
                            int G = pTempBuffer[nHeightToRead*nWidthToRead + j*nWidthToRead+i];
                            int B = pTempBuffer[2*nHeightToRead*nWidthToRead + j*nWidthToRead+i];
                            int Y = (int) (0.299 * R + 0.587 * G + 0.114 * B);
                            int Cb = CLAMP_0_255((int) (-0.1687 * R - 0.3313 * G + 0.5 * B  + 128));
                            int Cr = CLAMP_0_255((int) (0.5 * R - 0.4187 * G - 0.0813 * B  + 128));
                            pYUV420Buffer[j*nWidthToRead+i] = (GByte) Y;
                            pYUV420Buffer[nHeightToRead * nWidthToRead + ((j/2) * ((nWidthToRead)/2) + i/2) ] = (GByte) Cb;
                            pYUV420Buffer[5 * nHeightToRead * nWidthToRead / 4 + ((j/2) * ((nWidthToRead)/2) + i/2) ] = (GByte) Cr;
                        }
                    }

                    if (!opj_write_tile(pCodec,
                                        iTile,
                                        pYUV420Buffer,
                                        3 * nWidthToRead * nHeightToRead / 2,
                                        pStream))
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                "opj_write_tile() failed");
                        eErr = CE_Failure;
                    }
                }
                else
                {
                    if (!opj_write_tile(pCodec,
                                        iTile,
                                        pTempBuffer,
                                        nWidthToRead * nHeightToRead * nBands * nDataTypeSize,
                                        pStream))
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                "opj_write_tile() failed");
                        eErr = CE_Failure;
                    }
                }
            }

            if( !pfnProgress( (iTile + 1) * 1.0 / (nTilesX * nTilesY), NULL, pProgressData ) )
                eErr = CE_Failure;

            iTile ++;
        }
    }

    VSIFree(pTempBuffer);
    VSIFree(pYUV420Buffer);

    if (eErr != CE_None)
    {
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        VSIFCloseL(fp);
        return NULL;
    }

    if (!opj_end_compress(pCodec,pStream))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "opj_end_compress() failed");
        opj_stream_destroy(pStream);
        opj_image_destroy(psImage);
        opj_destroy_codec(pCodec);
        VSIFCloseL(fp);
        return NULL;
    }

    opj_stream_destroy(pStream);
    opj_image_destroy(psImage);
    opj_destroy_codec(pCodec);

/* -------------------------------------------------------------------- */
/*      Patch JP2C box length and add trailing JP2 boxes                */
/* -------------------------------------------------------------------- */
    if( eCodecFormat == OPJ_CODEC_JP2  )
    {
        vsi_l_offset nEndJP2C = VSIFTellL(fp);
        GUIntBig nBoxSize = nEndJP2C -nStartJP2C;
        if( bUseXLBoxes )
        {
            VSIFSeekL(fp, nStartJP2C + 8, SEEK_SET);
            CPL_MSBPTR64(&nBoxSize);
            VSIFWriteL(&nBoxSize, 8, 1, fp);
        }
        else
        {
            GUInt32 nBoxSize32 = (GUInt32)nBoxSize;
            if( (vsi_l_offset)nBoxSize32 != nBoxSize )
            {
                /*  Shouldn't happen hopefully */
                if( bHasGeoreferencing && bGeoBoxesAfter )
                {
                    CPLError(CE_Warning, CPLE_AppDefined,
                             "Cannot write GMLJP2/GeoJP2 boxes as codestream is unexpectedly > 4GB");
                    bHasGeoreferencing = FALSE;
                }
            }
            else
            {
                VSIFSeekL(fp, nStartJP2C, SEEK_SET);
                CPL_MSBPTR32(&nBoxSize32);
                VSIFWriteL(&nBoxSize32, 4, 1, fp);
            }
        }
        VSIFSeekL(fp, 0, SEEK_END);

        if( bHasGeoreferencing && bGeoBoxesAfter )
        {
            if( CSLFetchBoolean( papszOptions, "GMLJP2", TRUE ) &&
                bGeoreferencingCompatOfGMLJP2 )
            {
                GDALJP2Box* poBox = oJP2MD.CreateGMLJP2(nXSize,nYSize);
                WriteBox(fp, poBox);
                delete poBox;
            }
            if( CSLFetchBoolean( papszOptions, "GeoJP2", TRUE ) &&
                bHasGeoreferencing )
            {
                GDALJP2Box* poBox = oJP2MD.CreateJP2GeoTIFF();
                WriteBox(fp, poBox);
                delete poBox;
            }
        }
    }

    VSIFCloseL(fp);

/* -------------------------------------------------------------------- */
/*      Re-open dataset, and copy any auxilary pam information.         */
/* -------------------------------------------------------------------- */

    GDALOpenInfo oOpenInfo(pszFilename, GA_ReadOnly);
    JP2OpenJPEGDataset *poDS = (JP2OpenJPEGDataset*) JP2OpenJPEGDataset::Open(&oOpenInfo);

    if( poDS )
        poDS->CloneInfo( poSrcDS, GCIF_PAM_DEFAULT );

    return poDS;
}

/************************************************************************/
/*                      GDALRegister_JP2OpenJPEG()                      */
/************************************************************************/

void GDALRegister_JP2OpenJPEG()

{
    GDALDriver  *poDriver;
    
    if (! GDAL_CHECK_VERSION("JP2OpenJPEG driver"))
        return;

    if( GDALGetDriverByName( "JP2OpenJPEG" ) == NULL )
    {
        poDriver = new GDALDriver();
        
        poDriver->SetDescription( "JP2OpenJPEG" );
        poDriver->SetMetadataItem( GDAL_DCAP_RASTER, "YES" );
        poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, 
                                   "JPEG-2000 driver based on OpenJPEG library" );
        poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, 
                                   "frmt_jp2openjpeg.html" );
        poDriver->SetMetadataItem( GDAL_DMD_MIMETYPE, "image/jp2" );
        poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "jp2" );
        poDriver->SetMetadataItem( GDAL_DMD_CREATIONDATATYPES, 
                                   "Byte Int16 UInt16 Int32 UInt32" );

        poDriver->SetMetadataItem( GDAL_DMD_OPENOPTIONLIST, 
"<OpenOptionList>"
"   <Option name='1BIT_ALPHA_PROMOTION' type='boolean' description='Whether a 1-bit alpha channel should be promoted to 8-bit' default='YES'/>"
"</OpenOptionList>" );

        poDriver->SetMetadataItem( GDAL_DMD_CREATIONOPTIONLIST,
"<CreationOptionList>"
"   <Option name='CODEC' type='string-select' default='according to file extension. If unknown, default to J2K'>"
"       <Value>JP2</Value>"
"       <Value>J2K</Value>"
"   </Option>"
"   <Option name='GeoJP2' type='boolean' description='defaults to ON'/>"
"   <Option name='GMLJP2' type='boolean' description='defaults to ON'/>"
"   <Option name='QUALITY' type='float' description='Quality. 0-100' default='25'/>"
"   <Option name='REVERSIBLE' type='boolean' description='True if the compression is reversible' default='false'/>"
"   <Option name='RESOLUTIONS' type='int' description='Number of resolutions. 1-7' default='6'/>"
"   <Option name='BLOCKXSIZE' type='int' description='Tile Width' default='1024'/>"
"   <Option name='BLOCKYSIZE' type='int' description='Tile Height' default='1024'/>"
"   <Option name='PROGRESSION' type='string-select' default='LRCP'>"
"       <Value>LRCP</Value>"
"       <Value>RLCP</Value>"
"       <Value>RPCL</Value>"
"       <Value>PCRL</Value>"
"       <Value>CPRL</Value>"
"   </Option>"
"   <Option name='SOP' type='boolean' description='True to insert SOP markers' default='false'/>"
"   <Option name='EPH' type='boolean' description='True to insert EPH markers' default='false'/>"
"   <Option name='YCBCR420' type='boolean' description='if RGB must be resampled to YCbCr 4:2:0' default='false'/>"
"   <Option name='YCC' type='boolean' description='if RGB must be transformed to YCC color space' default='true'/>"
"</CreationOptionList>"  );

        poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );

        poDriver->pfnIdentify = JP2OpenJPEGDataset::Identify;
        poDriver->pfnOpen = JP2OpenJPEGDataset::Open;
        poDriver->pfnCreateCopy = JP2OpenJPEGDataset::CreateCopy;

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}
