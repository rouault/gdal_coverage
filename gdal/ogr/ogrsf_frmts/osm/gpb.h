/******************************************************************************
 * $Id$
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Author:   Even Rouault, <even dot rouault at mines dash paris dot org>
 * Purpose:  Google Protocol Buffer generic handling functions
 *
 ******************************************************************************
 * Copyright (c) 2012, Even Rouault <even dot rouault at mines-paris dot org>
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

#ifndef GPB_H_INCLUDED
#define GPB_H_INCLUDED

#include "cpl_port.h"
#include "cpl_error.h"
#include "cpl_string.h"

#include <string>
#include <exception>

#ifndef CHECK_OOB
#define CHECK_OOB 1
#endif

class GPBException: public std::exception
{
        std::string m_osMessage;
    public:
        explicit GPBException(int nLine): m_osMessage(
            CPLSPrintf("Parsing error occured at line %d", nLine)) {}

        const char* what() const noexcept override
                                        { return m_osMessage.c_str(); }
};

#define THROW_GPB_EXCEPTION throw GPBException(__LINE__)

/************************************************************************/
/*                Google Protocol Buffer definitions                    */
/************************************************************************/

#define WT_VARINT       0
#define WT_64BIT        1
#define WT_DATA         2
#define WT_STARTGROUP   3
#define WT_ENDGROUP     4
#define WT_32BIT        5

#define MAKE_KEY(nFieldNumber, nWireType) ((nFieldNumber << 3) | nWireType)
#define GET_WIRETYPE(nKey) (nKey & 0x7)
#define GET_FIELDNUMBER(nKey) (nKey >> 3)

/************************************************************************/
/*                          ReadVarUInt32()                             */
/************************************************************************/

inline int ReadVarUInt32(GByte** ppabyData)
{
    unsigned int nVal = 0;
    int nShift = 0;
    GByte* pabyData = *ppabyData;

    while(true)
    {
        int nByte = *pabyData;
        if (!(nByte & 0x80))
        {
            *ppabyData = pabyData + 1;
            return nVal | ((unsigned)nByte << nShift);
        }
        nVal |= (nByte & 0x7f) << nShift;
        pabyData ++;
        nShift += 7;
        if( nShift == 28 )
        {
            nByte = *pabyData;
            if (!(nByte & 0x80))
            {
                *ppabyData = pabyData + 1;
                return nVal | (((unsigned)nByte & 0xf) << nShift);
            }
            *ppabyData = pabyData;
            return nVal;
        }
    }
}

#define READ_VARUINT32(pabyData, pabyDataLimit, nVal)  \
    { \
        nVal = ReadVarUInt32(&pabyData); \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_SIZE(pabyData, pabyDataLimit, nSize) \
    { \
        READ_VARUINT32(pabyData, pabyDataLimit, nSize); \
        if (CHECK_OOB && nSize > (unsigned int)(pabyDataLimit - pabyData)) THROW_GPB_EXCEPTION; \
    }

/************************************************************************/
/*                          ReadVarUInt64()                             */
/************************************************************************/

inline GUIntBig ReadVarUInt64(GByte** ppabyData)
{
    GUIntBig nVal = 0;
    int nShift = 0;
    GByte* pabyData = *ppabyData;

    while(true)
    {
        int nByte = *pabyData;
        if (!(nByte & 0x80))
        {
            *ppabyData = pabyData + 1;
            return nVal | ((GUIntBig)nByte << nShift);
        }
        nVal |= ((GUIntBig)(nByte & 0x7f)) << nShift;
        pabyData ++;
        nShift += 7;
        if( nShift == 63 )
        {
            nByte = *pabyData;
            if (!(nByte & 0x80))
            {
                *ppabyData = pabyData + 1;
                return nVal | (((GUIntBig)nByte & 1) << nShift);
            }
            *ppabyData = pabyData;
            return nVal;
        }
    }
}

#define READ_VARUINT64(pabyData, pabyDataLimit, nVal)  \
    { \
        nVal = ReadVarUInt64(&pabyData); \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_VARINT64(pabyData, pabyDataLimit, nVal)  \
    { \
        nVal = (GIntBig)ReadVarUInt64(&pabyData); \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_VARSINT64(pabyData, pabyDataLimit, nVal)  \
    { \
        GUIntBig l_nVal = ReadVarUInt64(&pabyData); \
        nVal = ((l_nVal & 1) == 0) ? (GIntBig)(l_nVal >> 1) : -(GIntBig)(l_nVal >> 1)-1; \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_VARSINT64_NOCHECK(pabyData, pabyDataLimit, nVal)  \
    { \
        GUIntBig l_nVal = ReadVarUInt64(&pabyData); \
        nVal = ((l_nVal & 1) == 0) ? (GIntBig)(l_nVal >> 1) : -(GIntBig)(l_nVal >> 1)-1; \
    }

#define READ_SIZE64(pabyData, pabyDataLimit, nSize) \
    { \
        READ_VARUINT64(pabyData, pabyDataLimit, nSize); \
        if (CHECK_OOB && nSize > (unsigned int)(pabyDataLimit - pabyData)) THROW_GPB_EXCEPTION; \
    }

/************************************************************************/
/*                           ReadVarInt64()                             */
/************************************************************************/

inline GIntBig ReadVarInt64(GByte** ppabyData)
{
    return static_cast<GIntBig>(ReadVarUInt64(ppabyData));
}

/************************************************************************/
/*                           ReadVarInt32()                             */
/************************************************************************/

inline int ReadVarInt32(GByte** ppabyData)
{
    /*  If you use int32 or int64 as the type for a negative number, */
    /* the resulting varint is always ten bytes long */
    GIntBig nVal = static_cast<GIntBig>(ReadVarUInt64(ppabyData));
    return static_cast<int>(nVal);
}

#define READ_VARINT32(pabyData, pabyDataLimit, nVal)  \
    { \
        nVal = ReadVarInt32(&pabyData); \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_VARSINT32(pabyData, pabyDataLimit, nVal)  \
    { \
        nVal = ReadVarInt32(&pabyData); \
        nVal = ((nVal & 1) == 0) ? (int)(((unsigned int)nVal) >> 1) : -(int)(((unsigned int)nVal) >> 1)-1; \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

/************************************************************************/
/*                            ReadFloat32()                             */
/************************************************************************/

inline float ReadFloat32(GByte** ppabyData, GByte* pabyDataLimit)
{
    if( *ppabyData + sizeof(float) > pabyDataLimit )
        THROW_GPB_EXCEPTION;
    float fValue;
    memcpy(&fValue, *ppabyData, sizeof(float));
    CPL_LSBPTR32(&fValue);
    *ppabyData += sizeof(float);
    return fValue;
}

/************************************************************************/
/*                            ReadFloat64()                             */
/************************************************************************/

inline double ReadFloat64(GByte** ppabyData, GByte* pabyDataLimit)
{
    if( *ppabyData + sizeof(double) > pabyDataLimit )
        THROW_GPB_EXCEPTION;
    double dfValue;
    memcpy(&dfValue, *ppabyData, sizeof(double));
    CPL_LSBPTR64(&dfValue);
    *ppabyData += sizeof(double);
    return dfValue;
}

/************************************************************************/
/*                            SkipVarInt()                              */
/************************************************************************/

inline void SkipVarInt(GByte** ppabyData)
{
    GByte* pabyData = *ppabyData;
    while(true)
    {
        int nByte = *pabyData;
        if (!(nByte & 0x80))
        {
            *ppabyData = pabyData + 1;
            return;
        }
        pabyData ++;
    }
}

#define SKIP_VARINT(pabyData, pabyDataLimit) \
    { \
        SkipVarInt(&pabyData); \
        if (CHECK_OOB && pabyData > pabyDataLimit) THROW_GPB_EXCEPTION; \
    }

#define READ_FIELD_KEY(nKey) READ_VARINT32(pabyData, pabyDataLimit, nKey)

#define READ_TEXT_WITH_SIZE(pabyData, pabyDataLimit, pszTxt, l_nDataLength) do { \
        READ_SIZE(pabyData, pabyDataLimit, l_nDataLength); \
        pszTxt = (char*)VSI_MALLOC_VERBOSE(l_nDataLength + 1); \
        if( pszTxt == nullptr ) THROW_GPB_EXCEPTION; \
        memcpy(pszTxt, pabyData, l_nDataLength); \
        pszTxt[l_nDataLength] = 0; \
        pabyData += l_nDataLength; } while(0)

#define READ_TEXT(pabyData, pabyDataLimit, pszTxt) do { \
        unsigned int l_nDataLength; \
        READ_TEXT_WITH_SIZE(pabyData, pabyDataLimit, pszTxt, l_nDataLength); } while(0)

/************************************************************************/
/*                         SkipUnknownField()                           */
/************************************************************************/

#define SKIP_UNKNOWN_FIELD_INLINE(pabyData, pabyDataLimit, verbose) \
        int nWireType = GET_WIRETYPE(nKey); \
        if (verbose) \
        { \
            int nFieldNumber = GET_FIELDNUMBER(nKey); \
            CPLDebug("PBF", "Unhandled case: nFieldNumber = %d, nWireType = %d", nFieldNumber, nWireType); \
        } \
        switch (nWireType) \
        { \
            case WT_VARINT: \
            { \
                SKIP_VARINT(pabyData, pabyDataLimit); \
                break; \
            } \
            case WT_64BIT: \
            { \
                if (CHECK_OOB && pabyDataLimit - pabyData < 8) THROW_GPB_EXCEPTION; \
                pabyData += 8; \
                break; \
            } \
            case WT_DATA: \
            { \
                unsigned int nDataLength; \
                READ_SIZE(pabyData, pabyDataLimit, nDataLength); \
                pabyData += nDataLength; \
                break; \
            } \
            case WT_32BIT: \
            { \
                if (CHECK_OOB && pabyDataLimit - pabyData < 4) THROW_GPB_EXCEPTION; \
                pabyData += 4; \
                break; \
            } \
            default: \
                THROW_GPB_EXCEPTION; \
        }

inline int SkipUnknownField(int nKey, GByte* pabyData, GByte* pabyDataLimit, int verbose)
{
    GByte* pabyDataBefore = pabyData;
    try
    {
        SKIP_UNKNOWN_FIELD_INLINE(pabyData, pabyDataLimit, verbose);
        return static_cast<int>(pabyData - pabyDataBefore);
    }
    catch( const GPBException& e )
    {
        if( verbose )
        {
            CPLError(CE_Failure, CPLE_AppDefined, "%s", e.what());
        }
        return -1;
    }
}

#define SKIP_UNKNOWN_FIELD(pabyData, pabyDataLimit, verbose) \
    { \
        int _nOffset = SkipUnknownField(nKey, pabyData, pabyDataLimit, verbose); \
        if (_nOffset < 0) \
            THROW_GPB_EXCEPTION; \
        pabyData += _nOffset; \
    }

#endif /* GPB_H_INCLUDED */
