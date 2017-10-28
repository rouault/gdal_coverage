/******************************************************************************
 *
 * Project:  DXF Translator
 * Purpose:  Implements OGRDXFDataSource class
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2009, Frank Warmerdam <warmerdam@pobox.com>
 * Copyright (c) 2010-2013, Even Rouault <even dot rouault at mines-paris dot org>
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

#include "ogr_dxf.h"
#include "cpl_conv.h"
#include "cpl_string.h"

CPL_CVSID("$Id$")

/************************************************************************/
/*                          OGRDXFDataSource()                          */
/************************************************************************/

OGRDXFDataSource::OGRDXFDataSource() :
    fp(NULL),
    iEntitiesSectionOffset(0),
    bInlineBlocks(false),
    bMergeBlockGeometries(false)
{}

/************************************************************************/
/*                         ~OGRDXFDataSource()                          */
/************************************************************************/

OGRDXFDataSource::~OGRDXFDataSource()

{
/* -------------------------------------------------------------------- */
/*      Destroy layers.                                                 */
/* -------------------------------------------------------------------- */
    while( !apoLayers.empty() )
    {
        delete apoLayers.back();
        apoLayers.pop_back();
    }

/* -------------------------------------------------------------------- */
/*      Close file.                                                     */
/* -------------------------------------------------------------------- */
    if( fp != NULL )
    {
        VSIFCloseL( fp );
        fp = NULL;
    }
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRDXFDataSource::TestCapability( CPL_UNUSED const char * pszCap )
{
    return FALSE;
}

/************************************************************************/
/*                              GetLayer()                              */
/************************************************************************/

OGRLayer *OGRDXFDataSource::GetLayer( int iLayer )

{
    if( iLayer < 0 || iLayer >= (int) apoLayers.size() )
        return NULL;
    else
        return apoLayers[iLayer];
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

int OGRDXFDataSource::Open( const char * pszFilename, int bHeaderOnly )

{
    osEncoding = CPL_ENC_ISO8859_1;

    osName = pszFilename;

    bInlineBlocks = CPLTestBool(
        CPLGetConfigOption( "DXF_INLINE_BLOCKS", "TRUE" ) );
    bMergeBlockGeometries = CPLTestBool(
        CPLGetConfigOption( "DXF_MERGE_BLOCK_GEOMETRIES", "TRUE" ) );

    if( CPLTestBool(
            CPLGetConfigOption( "DXF_HEADER_ONLY", "FALSE" ) ) )
        bHeaderOnly = TRUE;

/* -------------------------------------------------------------------- */
/*      Open the file.                                                  */
/* -------------------------------------------------------------------- */
    fp = VSIFOpenL( pszFilename, "r" );
    if( fp == NULL )
        return FALSE;

    oReader.Initialize( fp );

/* -------------------------------------------------------------------- */
/*      Confirm we have a header section.                               */
/* -------------------------------------------------------------------- */
    char szLineBuf[257];
    bool bEntitiesOnly = false;

    if( ReadValue( szLineBuf ) != 0 || !EQUAL(szLineBuf,"SECTION") )
        return FALSE;

    if( ReadValue( szLineBuf ) != 2
        || (!EQUAL(szLineBuf,"HEADER") && !EQUAL(szLineBuf,"ENTITIES") && !EQUAL(szLineBuf,"TABLES")) )
        return FALSE;

    if( EQUAL(szLineBuf,"ENTITIES") )
        bEntitiesOnly = true;

    /* Some files might have no header but begin directly with a TABLES section */
    else if( EQUAL(szLineBuf,"TABLES") )
    {
        osEncoding = CPLGetConfigOption( "DXF_ENCODING", osEncoding );

        if( !ReadTablesSection() )
            return FALSE;
        if( ReadValue(szLineBuf) < 0 )
        {
            DXF_READER_ERROR();
            return FALSE;
        }
    }

/* -------------------------------------------------------------------- */
/*      Process the header, picking up a few useful pieces of           */
/*      information.                                                    */
/* -------------------------------------------------------------------- */
    else /* if( EQUAL(szLineBuf,"HEADER") ) */
    {
        if( !ReadHeaderSection() )
            return FALSE;
        if( ReadValue(szLineBuf) < 0 )
        {
            DXF_READER_ERROR();
            return FALSE;
        }

/* -------------------------------------------------------------------- */
/*      Process the CLASSES section, if present.                        */
/* -------------------------------------------------------------------- */
        if( EQUAL(szLineBuf,"ENDSEC") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"SECTION") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"CLASSES") )
        {
            // int nCode = 0;
            while( (/* nCode = */ ReadValue( szLineBuf,sizeof(szLineBuf) )) > -1
                   && !EQUAL(szLineBuf,"ENDSEC") )
            {
                //printf("C:%d/%s\n", nCode, szLineBuf );
            }
        }

/* -------------------------------------------------------------------- */
/*      Process the TABLES section, if present.                         */
/* -------------------------------------------------------------------- */
        if( EQUAL(szLineBuf,"ENDSEC") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"SECTION") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"TABLES") )
        {
            if( !ReadTablesSection() )
                return FALSE;
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Create a blocks layer if we are not in inlining mode.           */
/* -------------------------------------------------------------------- */
    if( !bInlineBlocks )
        apoLayers.push_back( new OGRDXFBlocksLayer( this ) );

/* -------------------------------------------------------------------- */
/*      Create out layer object - we will need it when interpreting     */
/*      blocks.                                                         */
/* -------------------------------------------------------------------- */
    apoLayers.push_back( new OGRDXFLayer( this ) );

/* -------------------------------------------------------------------- */
/*      Process the BLOCKS section if present.                          */
/* -------------------------------------------------------------------- */
    if( !bEntitiesOnly )
    {
        if( EQUAL(szLineBuf,"ENDSEC") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"SECTION") )
        {
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }

        if( EQUAL(szLineBuf,"BLOCKS") )
        {
            if( !ReadBlocksSection() )
                return FALSE;
            if( ReadValue(szLineBuf) < 0 )
            {
                DXF_READER_ERROR();
                return FALSE;
            }
        }
    }

    if( bHeaderOnly )
        return TRUE;

/* -------------------------------------------------------------------- */
/*      Now we are at the entities section, hopefully.  Confirm.        */
/* -------------------------------------------------------------------- */
    if( EQUAL(szLineBuf,"SECTION") )
    {
        if( ReadValue(szLineBuf) < 0 )
        {
            DXF_READER_ERROR();
            return FALSE;
        }
    }

    if( !EQUAL(szLineBuf,"ENTITIES") )
    {
        DXF_READER_ERROR();
        return FALSE;
    }

    iEntitiesSectionOffset = oReader.iSrcBufferFileOffset + oReader.iSrcBufferOffset;
    apoLayers[0]->ResetReading();

    return TRUE;
}

/************************************************************************/
/*                         ReadTablesSection()                          */
/************************************************************************/

bool OGRDXFDataSource::ReadTablesSection()

{
    char szLineBuf[257];
    int nCode = 0;

    while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > -1
           && !EQUAL(szLineBuf,"ENDSEC") )
    {
        // We are only interested in extracting tables.
        if( nCode != 0 || !EQUAL(szLineBuf,"TABLE") )
            continue;

        nCode = ReadValue( szLineBuf, sizeof(szLineBuf) );
        if( nCode < 0 )
        {
            DXF_READER_ERROR();
            return false;
        }

        if( nCode != 2 )
            continue;

        //CPLDebug( "DXF", "Found table %s.", szLineBuf );

        while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > -1
               && !EQUAL(szLineBuf,"ENDTAB") )
        {
            if( nCode == 0 && EQUAL(szLineBuf,"LAYER") )
            {
                if( !ReadLayerDefinition() )
                    return false;
            }
            if( nCode == 0 && EQUAL(szLineBuf,"LTYPE") )
            {
                if( !ReadLineTypeDefinition() )
                    return false;
            }
            if( nCode == 0 && EQUAL(szLineBuf,"DIMSTYLE") )
            {
                if( !ReadDimStyleDefinition() )
                    return false;
            }
        }
    }
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }

    CPLDebug( "DXF", "Read %d layer definitions.", (int) oLayerTable.size() );
    return true;
}

/************************************************************************/
/*                        ReadLayerDefinition()                         */
/************************************************************************/

bool OGRDXFDataSource::ReadLayerDefinition()

{
    char szLineBuf[257];
    int nCode = 0;
    std::map<CPLString,CPLString>  oLayerProperties;
    CPLString osLayerName = "";

    oLayerProperties["Hidden"] = "0";

    while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > 0 )
    {
        switch( nCode )
        {
          case 2:
            osLayerName = CPLString(szLineBuf).Recode( GetEncoding(),
                CPL_ENC_UTF8 );
            oLayerProperties["Exists"] = "1";
            break;

          case 6:
            oLayerProperties["Linetype"] = CPLString(szLineBuf).Recode(
                GetEncoding(), CPL_ENC_UTF8 );
            break;

          case 62:
            oLayerProperties["Color"] = szLineBuf;

            if( atoi(szLineBuf) < 0 ) // Is layer off?
                oLayerProperties["Hidden"] = "1";
            break;

          case 70:
            oLayerProperties["Flags"] = szLineBuf;
            if( atoi(szLineBuf) & 0x01 ) // Is layer frozen?
                oLayerProperties["Hidden"] = "1";
            break;

          case 370:
          case 39:
            oLayerProperties["LineWeight"] = szLineBuf;
            break;

          default:
            break;
        }
    }
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }

    if( !oLayerProperties.empty() )
        oLayerTable[osLayerName] = oLayerProperties;

    if( nCode == 0 )
        UnreadValue();
    return true;
}

/************************************************************************/
/*                        LookupLayerProperty()                         */
/************************************************************************/

const char *OGRDXFDataSource::LookupLayerProperty( const char *pszLayer,
                                                   const char *pszProperty )

{
    if( pszLayer == NULL )
        return NULL;

    try {
        return (oLayerTable[pszLayer])[pszProperty];
    } catch( ... ) {
        return NULL;
    }
}

/************************************************************************/
/*                       ReadLineTypeDefinition()                       */
/************************************************************************/

bool OGRDXFDataSource::ReadLineTypeDefinition()

{
    char szLineBuf[257];
    int nCode = 0;
    CPLString osLineTypeName;
    CPLString osLineTypeDef;

    while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > 0 )
    {
        switch( nCode )
        {
          case 2:
            osLineTypeName = CPLString(szLineBuf).Recode( GetEncoding(),
                CPL_ENC_UTF8 );
            break;

          case 49:
          {
              if( osLineTypeDef != "" )
                  osLineTypeDef += " ";

              if( szLineBuf[0] == '-' )
                  osLineTypeDef += szLineBuf+1;
              else
                  osLineTypeDef += szLineBuf;

              osLineTypeDef += "g";
          }
          break;

          default:
            break;
        }
    }
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }

    if( osLineTypeDef != "" )
        oLineTypeTable[osLineTypeName] = osLineTypeDef;

    if( nCode == 0 )
        UnreadValue();
    return true;
}

/************************************************************************/
/*                           LookupLineType()                           */
/************************************************************************/

const char *OGRDXFDataSource::LookupLineType( const char *pszName )

{
    if( oLineTypeTable.count(pszName) > 0 )
        return oLineTypeTable[pszName];
    else
        return NULL;
}

/************************************************************************/
/*                  PopulateDefaultDimStyleProperties()                 */
/************************************************************************/

void OGRDXFDataSource::PopulateDefaultDimStyleProperties(
    std::map<CPLString, CPLString>& oDimStyleProperties)

{
    const int* piCode = ACGetKnownDimStyleCodes();
    do
    {
        const char* pszProperty = ACGetDimStylePropertyName(*piCode);
        oDimStyleProperties[pszProperty] =
            ACGetDimStylePropertyDefault(*piCode);
    } while ( *(++piCode) );
}

/************************************************************************/
/*                       ReadDimStyleDefinition()                       */
/************************************************************************/

bool OGRDXFDataSource::ReadDimStyleDefinition()

{
    char szLineBuf[257];
    int nCode = 0;
    std::map<CPLString,CPLString> oDimStyleProperties;
    CPLString osDimStyleName = "";

    PopulateDefaultDimStyleProperties(oDimStyleProperties);

    while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > 0 )
    {
        switch( nCode )
        {
          case 2:
            osDimStyleName = CPLString(szLineBuf).Recode( GetEncoding(), CPL_ENC_UTF8 );
            break;

          default:
            const char* pszProperty = ACGetDimStylePropertyName(nCode);
            if( pszProperty )
                oDimStyleProperties[pszProperty] = szLineBuf;
            break;
        }
    }
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }

    if( !oDimStyleProperties.empty() )
        oDimStyleTable[osDimStyleName] = oDimStyleProperties;

    if( nCode == 0 )
        UnreadValue();
    return true;
}

/************************************************************************/
/*                           LookupDimStyle()                           */
/*                                                                      */
/*      If the specified DIMSTYLE does not exist, a default set of      */
/*      of style properties are copied into oDimStyleProperties and     */
/*      false is returned.  Otherwise true is returned.                 */
/************************************************************************/

bool OGRDXFDataSource::LookupDimStyle( const char *pszDimStyle,
    std::map<CPLString,CPLString>& oDimStyleProperties )

{
    if( pszDimStyle == NULL || !oDimStyleTable.count(pszDimStyle) )
    {
        PopulateDefaultDimStyleProperties(oDimStyleProperties);
        return false;
    }

    // make a copy of the DIMSTYLE properties, so no-one can mess around
    // with our original copy
    oDimStyleProperties = oDimStyleTable[pszDimStyle];
    return true;
}

/************************************************************************/
/*                         ReadHeaderSection()                          */
/************************************************************************/

bool OGRDXFDataSource::ReadHeaderSection()

{
    char szLineBuf[257];
    int nCode = 0;

    while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > -1
           && !EQUAL(szLineBuf,"ENDSEC") )
    {
        if( nCode != 9 )
            continue;

        CPLString l_osName = szLineBuf;

        if(ReadValue( szLineBuf, sizeof(szLineBuf) )<0)
        {
            DXF_READER_ERROR();
            return false;
        }

        CPLString osValue = szLineBuf;

        oHeaderVariables[l_osName] = osValue;
    }
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }

    nCode = ReadValue( szLineBuf, sizeof(szLineBuf) );
    if( nCode < 0 )
    {
        DXF_READER_ERROR();
        return false;
    }
    UnreadValue();

    /* Unusual DXF files produced by dxflib */
    /* such as http://www.ribbonsoft.com/library/architecture/plants/decd5.dxf */
    /* where there is a spurious ENDSEC in the middle of the header variables */
    if (nCode == 9 && STARTS_WITH_CI(szLineBuf, "$") )
    {
        while( (nCode = ReadValue( szLineBuf, sizeof(szLineBuf) )) > -1
            && !EQUAL(szLineBuf,"ENDSEC") )
        {
            if( nCode != 9 )
                continue;

            CPLString l_osName = szLineBuf;

            if( ReadValue( szLineBuf, sizeof(szLineBuf) ) < 0 )
            {
                DXF_READER_ERROR();
                return false;
            }

            CPLString osValue = szLineBuf;

            oHeaderVariables[l_osName] = osValue;
        }
        if( nCode < 0 )
        {
            DXF_READER_ERROR();
            return false;
        }
    }

    CPLDebug( "DXF", "Read %d header variables.",
              (int) oHeaderVariables.size() );

/* -------------------------------------------------------------------- */
/*      Decide on what CPLRecode() name to use for the files            */
/*      encoding or allow the encoding to be overridden.                */
/* -------------------------------------------------------------------- */
    CPLString osCodepage = GetVariable( "$DWGCODEPAGE", "ANSI_1252" );

    // not strictly accurate but works even without iconv.
    if( osCodepage == "ANSI_1252" )
        osEncoding = CPL_ENC_ISO8859_1;
    else if( STARTS_WITH_CI(osCodepage, "ANSI_") )
    {
        osEncoding = "CP";
        osEncoding += osCodepage + 5;
    }
    else
    {
        // fallback to the default
        osEncoding = CPL_ENC_ISO8859_1;
    }

    const char *pszEncoding = CPLGetConfigOption( "DXF_ENCODING", NULL );
    if( pszEncoding != NULL )
        osEncoding = pszEncoding;

    if( osEncoding != CPL_ENC_ISO8859_1 )
        CPLDebug( "DXF", "Treating DXF as encoding '%s', $DWGCODEPAGE='%s'",
                  osEncoding.c_str(), osCodepage.c_str() );
    return true;
}

/************************************************************************/
/*                            GetVariable()                             */
/*                                                                      */
/*      Fetch a variable that came from the HEADER section.             */
/************************************************************************/

const char *OGRDXFDataSource::GetVariable( const char *pszName,
                                           const char *pszDefault )

{
    if( oHeaderVariables.count(pszName) == 0 )
        return pszDefault;
    else
        return oHeaderVariables[pszName];
}

/************************************************************************/
/*                         AddStandardFields()                          */
/************************************************************************/

void OGRDXFDataSource::AddStandardFields( OGRFeatureDefn *poFeatureDefn )

{
    OGRFieldDefn  oLayerField( "Layer", OFTString );
    poFeatureDefn->AddFieldDefn( &oLayerField );

    OGRFieldDefn  oClassField( "SubClasses", OFTString );
    poFeatureDefn->AddFieldDefn( &oClassField );

    OGRFieldDefn  oExtendedField( "ExtendedEntity", OFTString );
    poFeatureDefn->AddFieldDefn( &oExtendedField );

    OGRFieldDefn  oLinetypeField( "Linetype", OFTString );
    poFeatureDefn->AddFieldDefn( &oLinetypeField );

    OGRFieldDefn  oEntityHandleField( "EntityHandle", OFTString );
    poFeatureDefn->AddFieldDefn( &oEntityHandleField );

    OGRFieldDefn  oTextField( "Text", OFTString );
    poFeatureDefn->AddFieldDefn( &oTextField );

    if( !bInlineBlocks )
    {
        OGRFieldDefn  oBlockNameField( "BlockName", OFTString );
        poFeatureDefn->AddFieldDefn( &oBlockNameField );

        OGRFieldDefn  oScaleField( "BlockScale", OFTRealList );
        poFeatureDefn->AddFieldDefn( &oScaleField );

        OGRFieldDefn  oBlockAngleField( "BlockAngle", OFTReal );
        poFeatureDefn->AddFieldDefn( &oBlockAngleField );

        OGRFieldDefn  oBlockOCSNormalField( "BlockOCSNormal", OFTRealList );
        poFeatureDefn->AddFieldDefn( &oBlockOCSNormalField );

        OGRFieldDefn  oBlockOCSCoordsField( "BlockOCSCoords", OFTRealList );
        poFeatureDefn->AddFieldDefn( &oBlockOCSCoordsField );

        // This field holds the name of the block on which the entity lies.
        // The BlockName field was previously used for this purpose; this
        // was changed because of the ambiguity with the BlockName field
        // used by INSERT entities.
        OGRFieldDefn  oBlockField( "Block", OFTString );
        poFeatureDefn->AddFieldDefn( &oBlockField );
    }
}
