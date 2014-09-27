/******************************************************************************
 * $Id$
 *
 * Project:  KML Driver
 * Purpose:  Implementation of OGRKMLDriver class.
 * Author:   Christopher Condit, condit@sdsc.edu;
 *           Jens Oberender, j.obi@troja.net
 *
 ******************************************************************************
 * Copyright (c) 2006, Christopher Condit
 *               2007, Jens Oberender
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
#include "ogr_kml.h"
#include "cpl_conv.h"
#include "cpl_error.h"

/************************************************************************/
/*                         OGRKMLDriverIdentify()                       */
/************************************************************************/

static int OGRKMLDriverIdentify( GDALOpenInfo* poOpenInfo )

{
    if( poOpenInfo->fpL == NULL )
        return FALSE;

    return( strstr((const char*)poOpenInfo->pabyHeader, "<kml") != NULL );
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

static GDALDataset *OGRKMLDriverOpen( GDALOpenInfo* poOpenInfo )

{
    if( poOpenInfo->eAccess == GA_Update )
        return NULL;

    if( !OGRKMLDriverIdentify(poOpenInfo) )
        return NULL;

    OGRKMLDataSource* poDS = NULL;

#ifdef HAVE_EXPAT
    poDS = new OGRKMLDataSource();

    if( poDS->Open( poOpenInfo->pszFilename, TRUE ) )
    {
        /*if( poDS->GetLayerCount() == 0 )
        {
            CPLError( CE_Failure, CPLE_OpenFailed, 
                "No layers in KML file: %s.", pszName );

            delete poDS;
            poDS = NULL;
        }*/
    }
    else
    {
        delete poDS;
        poDS = NULL;
    }
#endif

    return poDS;
}

/************************************************************************/
/*                               Create()                               */
/************************************************************************/

static GDALDataset *OGRKMLDriverCreate( const char * pszName,
                                        CPL_UNUSED int nBands,
                                        CPL_UNUSED int nXSize,
                                        CPL_UNUSED int nYSize,
                                        CPL_UNUSED GDALDataType eDT,
                                        char **papszOptions )
{
    CPLAssert( NULL != pszName );
    CPLDebug( "KML", "Attempt to create: %s", pszName );

    OGRKMLDataSource *poDS = new OGRKMLDataSource();

    if( !poDS->Create( pszName, papszOptions ) )
    {
        delete poDS;
        poDS = NULL;
    }

    return poDS;
}

/************************************************************************/
/*                           RegisterOGRKML()                           */
/************************************************************************/

void RegisterOGRKML()
{
    GDALDriver  *poDriver;

    if( GDALGetDriverByName( "KML" ) == NULL )
    {
        poDriver = new GDALDriver();

        poDriver->SetDescription( "KML" );
        poDriver->SetMetadataItem( GDAL_DCAP_VECTOR, "YES" );
        poDriver->SetMetadataItem( GDAL_DMD_LONGNAME,
                                   "Keyhole Markup Language (KML)" );
        poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "kml" );
        poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC,
                                   "drv_kml.html" );

        poDriver->SetMetadataItem( GDAL_DMD_CREATIONOPTIONLIST,
"<CreationOptionList>"
"  <Option name='GPX_USE_EXTENSIONS' type='boolean' description='Whether to write non-GPX attributes in an <extensions> tag' default='NO'/>"
"  <Option name='NameField' type='string' description='Field to use to fill the KML <name> element' default='Name'/>"
"  <Option name='DescriptionField' type='string' description='Field to use to fill the KML <description> element' default='Description'/>"
"  <Option name='AltitudeMode' type='string-select' description='Value of the <AltitudeMode> element for 3D geometries'>"
"    <Value>clampToGround</Value>"
"    <Value>relativeToGround</Value>"
"    <Value>absolute</Value>"
"  </Option>"
"</CreationOptionList>");

        poDriver->SetMetadataItem( GDAL_DS_LAYER_CREATIONOPTIONLIST, "<LayerCreationOptionList/>" );

        poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );

        poDriver->pfnOpen = OGRKMLDriverOpen;
        poDriver->pfnIdentify = OGRKMLDriverIdentify;
        poDriver->pfnCreate = OGRKMLDriverCreate;

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}
