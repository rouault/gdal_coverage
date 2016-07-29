/******************************************************************************
 *
 * Project:  VFK Reader - Data block property definition
 * Purpose:  Implements VFKPropertyDefn class.
 * Author:   Martin Landa, landa.martin gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2009-2010, 2012, Martin Landa <landa.martin gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ****************************************************************************/

#include "vfkreader.h"
#include "vfkreaderp.h"

#include "cpl_conv.h"
#include "cpl_error.h"

CPL_CVSID("$Id$");

/*!
  \brief VFKPropertyDefn constructor

  \param pszName property name
  \param pszType property type (original, string)
  \param bLatin2 TRUE for "ISO-8859-2" otherwise "WINDOWS-1250" is used (only for "text" type)
*/
VFKPropertyDefn::VFKPropertyDefn(const char *pszName, const char *pszType,
                                 bool bLatin2)
{
    char *poChar, *poWidth, *pszWidth;
    int   nLength;

    m_pszName = CPLStrdup(pszName);
    m_pszType = CPLStrdup(pszType);

    poWidth = poChar = m_pszType + 1;
    for (nLength = 0; *poChar && *poChar != '.'; nLength++, poChar++)
        ;

    /* width */
    pszWidth = (char *) CPLMalloc(nLength+1);
    strncpy(pszWidth, poWidth, nLength);
    pszWidth[nLength] = '\0';

    m_nWidth  = atoi(pszWidth);
    CPLFree(pszWidth);

    /* precision */
    m_nPrecision = 0;

    /* type */
    m_pszEncoding = NULL;
    if (*m_pszType == 'N') {
        if (*poChar == '.') {
            m_eFType = OFTReal;
            m_nPrecision = atoi(poChar+1);
        }
        else {
            if (m_nWidth < 10)
                m_eFType = OFTInteger;
            else {
                m_eFType  = OFTString;
            }
        }
    }
    else if (*m_pszType == 'T') {
        /* string */
        m_eFType = OFTString;
        m_pszEncoding = bLatin2 ? CPLStrdup("ISO-8859-2") : CPLStrdup("WINDOWS-1250");
    }
    else if (*m_pszType == 'D') {
        /* date */
        /* m_eFType = OFTDateTime; */
        m_eFType = OFTString;
        m_nWidth = 25;
    }
    else {
        /* unknown - string */
        m_eFType = OFTString;
        m_pszEncoding = bLatin2 ? CPLStrdup("ISO-8859-2") : CPLStrdup("WINDOWS-1250");
    }
}

/*!
  \brief VFKPropertyDefn destructor
*/
VFKPropertyDefn::~VFKPropertyDefn()
{
    CPLFree(m_pszName);
    CPLFree(m_pszType);
    if (m_pszEncoding)
        CPLFree(m_pszEncoding);
}

/*!
  \brief Get SQL data type

  \return string with data type ("text" by default)
*/
CPLString VFKPropertyDefn::GetTypeSQL() const
{
    switch(m_eFType) {
    case OFTInteger:
        return CPLString("integer");
    case OFTReal:
        return CPLString("real");
    case OFTString:
        if (m_pszType[0] == 'N')
            return CPLString("integer");
        else
            return CPLString("text");
    default:
        return CPLString("text");
    }
}
