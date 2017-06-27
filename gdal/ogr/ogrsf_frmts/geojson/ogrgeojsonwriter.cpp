/******************************************************************************
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Implementation of GeoJSON writer utilities (OGR GeoJSON Driver).
 * Author:   Mateusz Loskot, mateusz@loskot.net
 *
 ******************************************************************************
 * Copyright (c) 2007, Mateusz Loskot
 * Copyright (c) 2008-2014, Even Rouault <even dot rouault at mines-paris dot org>
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

#include "ogrgeojsonwriter.h"
#include "ogrgeojsonutils.h"
#include "ogr_geojson.h"
#include "ogrgeojsonreader.h"
#include <json.h>  // JSON-C
#include <json_object_private.h>
#include <printbuf.h>
#include <ogr_api.h>
#include <ogr_p.h>

CPL_CVSID("$Id$")


/************************************************************************/
/*                         SetRFC7946Settings()                         */
/************************************************************************/

/*! @cond Doxygen_Suppress */
void OGRGeoJSONWriteOptions::SetRFC7946Settings()
{
    bBBOXRFC7946 = true;
    if( nCoordPrecision < 0 )
        nCoordPrecision = 7;
    bPolygonRightHandRule = true;
    bCanPatchCoordinatesWithNativeData = false;
    bHonourReservedRFC7946Members = true;
}
/*! @endcond */

/************************************************************************/
/*                        json_object_new_coord()                       */
/************************************************************************/

static json_object* json_object_new_coord( double dfVal,
                                           const OGRGeoJSONWriteOptions& oOptions )
{
    // If coordinate precision is specified, or significant figures is not
    // then use the '%f' formatting.
    if( oOptions.nCoordPrecision >= 0 || oOptions.nSignificantFigures < 0 )
        return json_object_new_double_with_precision(dfVal,
                                                     oOptions.nCoordPrecision);

    return json_object_new_double_with_significant_figures(dfVal,
                                                oOptions.nSignificantFigures);
}

/************************************************************************/
/*                     OGRGeoJSONIsPatchablePosition()                  */
/************************************************************************/

static bool OGRGeoJSONIsPatchablePosition( json_object* poJSonCoordinates,
                                           json_object* poNativeCoordinates )
{
    return
        json_object_get_type(poJSonCoordinates) == json_type_array &&
        json_object_get_type(poNativeCoordinates) == json_type_array &&
        json_object_array_length(poJSonCoordinates) == 3 &&
        json_object_array_length(poNativeCoordinates) >= 4 &&
        json_object_get_type(json_object_array_get_idx(poJSonCoordinates,
                                                       0)) != json_type_array &&
        json_object_get_type(json_object_array_get_idx(poNativeCoordinates,
                                                       0)) != json_type_array;
}

/************************************************************************/
/*                    OGRGeoJSONIsCompatiblePosition()                  */
/************************************************************************/

static bool OGRGeoJSONIsCompatiblePosition( json_object* poJSonCoordinates,
                                           json_object* poNativeCoordinates )
{
    return
        json_object_get_type(poJSonCoordinates) == json_type_array &&
        json_object_get_type(poNativeCoordinates) == json_type_array &&
        json_object_array_length(poJSonCoordinates) ==
            json_object_array_length(poNativeCoordinates) &&
        json_object_get_type(json_object_array_get_idx(poJSonCoordinates,
                                                       0)) != json_type_array &&
        json_object_get_type(json_object_array_get_idx(poNativeCoordinates,
                                                       0)) != json_type_array;
}

/************************************************************************/
/*                       OGRGeoJSONPatchPosition()                      */
/************************************************************************/

static void OGRGeoJSONPatchPosition( json_object* poJSonCoordinates,
                                     json_object* poNativeCoordinates )
{
    const int nLength = json_object_array_length(poNativeCoordinates);
    for( int i = 3; i < nLength; i++ )
    {
        json_object_array_add(poJSonCoordinates,
            json_object_get(
                json_object_array_get_idx(poNativeCoordinates, i)));
    }
}

/************************************************************************/
/*                      OGRGeoJSONIsPatchableArray()                    */
/************************************************************************/

static bool OGRGeoJSONIsPatchableArray( json_object* poJSonArray,
                                        json_object* poNativeArray,
                                        int nDepth )
{
    if( nDepth == 0 )
        return OGRGeoJSONIsPatchablePosition(poJSonArray, poNativeArray);

    int nLength = 0;
    if( json_object_get_type(poJSonArray) == json_type_array &&
        json_object_get_type(poNativeArray) == json_type_array &&
        (nLength = json_object_array_length(poJSonArray)) ==
                            json_object_array_length(poNativeArray) )
    {
        if( nLength > 0 )
        {
            json_object* poJSonChild =
                json_object_array_get_idx(poJSonArray, 0);
            json_object* poNativeChild =
                json_object_array_get_idx(poNativeArray, 0);
            if( !OGRGeoJSONIsPatchableArray(poJSonChild, poNativeChild,
                                            nDepth - 1) )
            {
                return false;
            }
            // Light check as a former extensive check was done in
            // OGRGeoJSONComputePatchableOrCompatibleArray
        }
        return true;
    }
    return false;
}

/************************************************************************/
/*                OGRGeoJSONComputePatchableOrCompatibleArray()         */
/************************************************************************/

/* Returns true if the objects are comparable, ie Point vs Point, LineString
   vs LineString, but they might not be patchable or compatible */
static bool OGRGeoJSONComputePatchableOrCompatibleArrayInternal(
                                                    json_object* poJSonArray,
                                                    json_object* poNativeArray,
                                                    int nDepth,
                                                    bool& bOutPatchable,
                                                    bool& bOutCompatible)
{
    if( nDepth == 0 )
    {
        bOutPatchable &= OGRGeoJSONIsPatchablePosition(poJSonArray, poNativeArray);
        bOutCompatible &= OGRGeoJSONIsCompatiblePosition(poJSonArray, poNativeArray);
        return json_object_get_type(poJSonArray) == json_type_array &&
               json_object_get_type(poNativeArray) == json_type_array &&
               json_object_get_type(json_object_array_get_idx(poJSonArray,
                                                            0)) != json_type_array &&
               json_object_get_type(json_object_array_get_idx(poNativeArray,
                                                            0)) != json_type_array;
    }

    int nLength = 0;
    if( json_object_get_type(poJSonArray) == json_type_array &&
        json_object_get_type(poNativeArray) == json_type_array &&
        (nLength = json_object_array_length(poJSonArray)) ==
                            json_object_array_length(poNativeArray) )
    {
        for( int i=0; i < nLength; i++ )
        {
            json_object* poJSonChild =
                json_object_array_get_idx(poJSonArray, i);
            json_object* poNativeChild =
                json_object_array_get_idx(poNativeArray, i);
            if( !OGRGeoJSONComputePatchableOrCompatibleArrayInternal(poJSonChild,
                                                   poNativeChild,
                                                   nDepth - 1,
                                                   bOutPatchable,
                                                   bOutCompatible) )
            {
                return false;
            }
            if (!bOutPatchable && !bOutCompatible)
                break;
        }
        return true;
    }
    else
    {
        bOutPatchable = false;
        bOutCompatible = false;
        return false;
    }
}

/* Returns true if the objects are comparable, ie Point vs Point, LineString
   vs LineString, but they might not be patchable or compatible */
static bool OGRGeoJSONComputePatchableOrCompatibleArray( json_object* poJSonArray,
                                                    json_object* poNativeArray,
                                                    int nDepth,
                                                    bool& bOutPatchable,
                                                    bool& bOutCompatible)
{
    bOutPatchable = true;
    bOutCompatible = true;
    return OGRGeoJSONComputePatchableOrCompatibleArrayInternal(poJSonArray,
                                                          poNativeArray,
                                                          nDepth,
                                                          bOutPatchable,
                                                          bOutCompatible);
}

/************************************************************************/
/*                        OGRGeoJSONPatchArray()                        */
/************************************************************************/

static void OGRGeoJSONPatchArray( json_object* poJSonArray,
                                  json_object* poNativeArray,
                                  int nDepth )
{
    if( nDepth == 0 )
    {
        OGRGeoJSONPatchPosition(poJSonArray, poNativeArray);
        return;
    }
    const int nLength = json_object_array_length(poJSonArray);
    for( int i = 0; i<nLength; i++ )
    {
        json_object* poJSonChild = json_object_array_get_idx(poJSonArray, i);
        json_object* poNativeChild =
            json_object_array_get_idx(poNativeArray, i);
        OGRGeoJSONPatchArray(poJSonChild, poNativeChild,nDepth-1);
    }
}

/************************************************************************/
/*                        OGRGeoJSONIsPatchableGeometry()                */
/************************************************************************/

static bool OGRGeoJSONIsPatchableGeometry( json_object* poJSonGeometry,
                                           json_object* poNativeGeometry,
                                           bool& bOutPatchableCoords,
                                           bool& bOutCompatibleCoords )
{
    if( json_object_get_type(poJSonGeometry) != json_type_object ||
        json_object_get_type(poNativeGeometry) != json_type_object )
    {
        return false;
    }

    json_object* poType = CPL_json_object_object_get(poJSonGeometry, "type");
    json_object* poNativeType = CPL_json_object_object_get(poNativeGeometry, "type");
    if( poType == NULL || poNativeType == NULL ||
        json_object_get_type(poType) != json_type_string ||
        json_object_get_type(poNativeType) != json_type_string ||
        strcmp(json_object_get_string(poType),
               json_object_get_string(poNativeType)) != 0 )
    {
        return false;
    }

    json_object_iter it;
    it.key = NULL;
    it.val = NULL;
    it.entry = NULL;
    json_object_object_foreachC(poNativeGeometry, it)
    {
        if( strcmp(it.key, "coordinates") == 0 )
        {
            json_object* poJSonCoordinates =
                CPL_json_object_object_get(poJSonGeometry, "coordinates");
            json_object* poNativeCoordinates = it.val;
            // 0 = Point
            // 1 = LineString or MultiPoint
            // 2 = MultiLineString or Polygon
            // 3 = MultiPolygon
            for( int i = 0; i <= 3; i++ )
            {
                if( OGRGeoJSONComputePatchableOrCompatibleArray(poJSonCoordinates,
                                                       poNativeCoordinates,
                                                       i,
                                                       bOutPatchableCoords,
                                                       bOutCompatibleCoords) )
                {
                    return bOutPatchableCoords || bOutCompatibleCoords;
                }
            }
            return false;
        }
        if( strcmp(it.key, "geometries") == 0 )
        {
            json_object* poJSonGeometries =
                CPL_json_object_object_get(poJSonGeometry, "geometries");
            json_object* poNativeGeometries = it.val;
            int nLength = 0;
            if( json_object_get_type(poJSonGeometries) == json_type_array &&
                json_object_get_type(poNativeGeometries) == json_type_array &&
                (nLength = json_object_array_length(poJSonGeometries)) ==
                    json_object_array_length(poNativeGeometries) )
            {
                for( int i=0; i < nLength; i++ )
                {
                    json_object* poJSonChild =
                        json_object_array_get_idx(poJSonGeometries, i);
                    json_object* poNativeChild =
                        json_object_array_get_idx(poNativeGeometries, i);
                    if( !OGRGeoJSONIsPatchableGeometry(poJSonChild,
                                                       poNativeChild,
                                                       bOutPatchableCoords,
                                                       bOutCompatibleCoords) )
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
    }
    return false;
}

/************************************************************************/
/*                        OGRGeoJSONPatchGeometry()                     */
/************************************************************************/

static void OGRGeoJSONPatchGeometry( json_object* poJSonGeometry,
                                     json_object* poNativeGeometry,
                                     bool bPatchableCoordinates,
                                     const OGRGeoJSONWriteOptions& oOptions )
{
    json_object_iter it;
    it.key = NULL;
    it.val = NULL;
    it.entry = NULL;
    json_object_object_foreachC(poNativeGeometry, it)
    {
        if( strcmp(it.key, "type") == 0 ||
            strcmp(it.key, "bbox") == 0 )
        {
            continue;
        }
        if( strcmp(it.key, "coordinates") == 0 )
        {
            if( !bPatchableCoordinates &&
                !oOptions.bCanPatchCoordinatesWithNativeData )
            {
                continue;
            }

            json_object* poJSonCoordinates =
                CPL_json_object_object_get(poJSonGeometry, "coordinates");
            json_object* poNativeCoordinates = it.val;
            for( int i = 0; i <= 3; i++ )
            {
                if( OGRGeoJSONIsPatchableArray(poJSonCoordinates,
                                               poNativeCoordinates, i) )
                {
                    OGRGeoJSONPatchArray(poJSonCoordinates,
                                         poNativeCoordinates, i);
                    break;
                }
            }

            continue;
        }
        if( strcmp(it.key, "geometries") == 0 )
        {
            json_object* poJSonGeometries =
                CPL_json_object_object_get(poJSonGeometry, "geometries");
            json_object* poNativeGeometries = it.val;
            int nLength = json_object_array_length(poJSonGeometries);
            for( int i=0; i < nLength; i++ )
            {
                json_object* poJSonChild =
                    json_object_array_get_idx(poJSonGeometries, i);
                json_object* poNativeChild =
                    json_object_array_get_idx(poNativeGeometries, i);
                OGRGeoJSONPatchGeometry(poJSonChild, poNativeChild,
                                        bPatchableCoordinates, oOptions);
            }

            continue;
        }

        // See https://tools.ietf.org/html/rfc7946#section-7.1
        if( oOptions.bHonourReservedRFC7946Members &&
            (strcmp(it.key, "geometry") == 0 ||
             strcmp(it.key, "properties") == 0 ||
             strcmp(it.key, "features") == 0) )
        {
            continue;
        }

        json_object_object_add( poJSonGeometry, it.key,
                                json_object_get(it.val) );
    }
}

/************************************************************************/
/*                           OGRGeoJSONGetBBox                          */
/************************************************************************/

OGREnvelope3D OGRGeoJSONGetBBox( OGRGeometry* poGeometry,
                                 const OGRGeoJSONWriteOptions& oOptions )
{
    OGREnvelope3D sEnvelope;
    poGeometry->getEnvelope(&sEnvelope);

    if( oOptions.bBBOXRFC7946 )
    {
        // Heuristics to determine if the geometry was split along the
        // date line.
        const double EPS = 1e-7;
        const OGRwkbGeometryType eType =
                        wkbFlatten(poGeometry->getGeometryType());
        if( OGR_GT_IsSubClassOf(eType, wkbGeometryCollection) &&
            reinterpret_cast<OGRGeometryCollection*>(poGeometry)->
                                            getNumGeometries() >= 2 &&
            fabs(sEnvelope.MinX - (-180.0)) < EPS &&
            fabs(sEnvelope.MaxX - 180.0) < EPS )
        {
            OGRGeometryCollection* poGC =
                reinterpret_cast<OGRGeometryCollection*>(poGeometry);
            double dfWestLimit = -180.0;
            double dfEastLimit = 180.0;
            bool bWestLimitIsInit = false;
            bool bEastLimitIsInit = false;
            for( int i=0; i < poGC->getNumGeometries(); ++i )
            {
                OGREnvelope sEnvelopePart;
                poGC->getGeometryRef(i)->getEnvelope(&sEnvelopePart);
                const bool bTouchesMinus180 =
                            fabs(sEnvelopePart.MinX - (-180.0)) < EPS;
                const bool bTouchesPlus180 =
                            fabs(sEnvelopePart.MaxX - 180.0) < EPS;
                if( bTouchesMinus180 && !bTouchesPlus180 )
                {
                    if( sEnvelopePart.MaxX > dfEastLimit ||
                        !bEastLimitIsInit )
                    {
                        bEastLimitIsInit = true;
                        dfEastLimit = sEnvelopePart.MaxX;
                    }
                }
                else if( bTouchesPlus180 && !bTouchesMinus180 )
                {
                    if( sEnvelopePart.MinX < dfWestLimit ||
                        !bWestLimitIsInit )
                    {
                        bWestLimitIsInit = true;
                        dfWestLimit = sEnvelopePart.MinX;
                    }
                }
                else if( !bTouchesMinus180 && !bTouchesPlus180 )
                {
                    if( sEnvelopePart.MinX > 0 &&
                        (sEnvelopePart.MinX < dfWestLimit ||
                            !bWestLimitIsInit))
                    {
                        bWestLimitIsInit = true;
                        dfWestLimit = sEnvelopePart.MinX;
                    }
                    else if ( sEnvelopePart.MaxX < 0 &&
                                (sEnvelopePart.MaxX > dfEastLimit ||
                                !bEastLimitIsInit) )
                    {
                        bEastLimitIsInit = true;
                        dfEastLimit = sEnvelopePart.MaxX;
                    }
                }
            }
            sEnvelope.MinX = dfWestLimit;
            sEnvelope.MaxX = dfEastLimit;
        }
    }

    return sEnvelope;
}

/************************************************************************/
/*                           OGRGeoJSONWriteFeature                     */
/************************************************************************/

json_object* OGRGeoJSONWriteFeature( OGRFeature* poFeature,
                                     const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poFeature );

    bool bWriteBBOX = oOptions.bWriteBBOX;

    json_object* poObj = json_object_new_object();
    CPLAssert( NULL != poObj );

    json_object_object_add( poObj, "type",
                            json_object_new_string("Feature") );

/* -------------------------------------------------------------------- */
/*      Write native JSon data.                                         */
/* -------------------------------------------------------------------- */
    bool bIdAlreadyWritten = false;
    const char* pszNativeMediaType = poFeature->GetNativeMediaType();
    json_object* poNativeGeom = NULL;
    json_object* poNativeId = NULL;
    bool bHasProperties = true;
    if( pszNativeMediaType &&
        EQUAL(pszNativeMediaType, "application/vnd.geo+json") )
    {
        const char* pszNativeData = poFeature->GetNativeData();
        json_object* poNativeJSon = NULL;
        if( pszNativeData && OGRJSonParse(pszNativeData, &poNativeJSon) &&
            json_object_get_type(poNativeJSon) == json_type_object )
        {
            json_object_iter it;
            it.key = NULL;
            it.val = NULL;
            it.entry = NULL;
            CPLString osNativeData;
            bHasProperties = false;
            json_object_object_foreachC(poNativeJSon, it)
            {
                if( strcmp(it.key, "type") == 0 )
                {
                    continue;
                }
                if( strcmp(it.key, "properties") == 0 )
                {
                    bHasProperties = true;
                    continue;
                }
                if( strcmp(it.key, "bbox") == 0 )
                {
                    bWriteBBOX = true;
                    continue;
                }
                if( strcmp(it.key, "geometry") == 0 )
                {
                    poNativeGeom = json_object_get(it.val);
                    continue;
                }
                if( strcmp(it.key, "id") == 0 )
                {
                    // See https://tools.ietf.org/html/rfc7946#section-3.2
                    if( oOptions.bHonourReservedRFC7946Members &&
                        json_object_get_type(it.val) != json_type_string &&
                        json_object_get_type(it.val) != json_type_int &&
                        json_object_get_type(it.val) != json_type_double )
                    {
                        continue;
                    }

                    bIdAlreadyWritten = true;
                    poNativeId = it.val;
                }

                // See https://tools.ietf.org/html/rfc7946#section-7.1
                if( oOptions.bHonourReservedRFC7946Members &&
                    (strcmp(it.key, "coordinates") == 0 ||
                     strcmp(it.key, "geometries") == 0 ||
                     strcmp(it.key, "features") == 0) )
                {
                    continue;
                }

                json_object_object_add( poObj, it.key,
                                        json_object_get(it.val) );
            }
            json_object_put(poNativeJSon);
        }
    }

/* -------------------------------------------------------------------- */
/*      Write FID if available                                          */
/* -------------------------------------------------------------------- */
    if( poFeature->GetFID() != OGRNullFID && !bIdAlreadyWritten )
    {
        json_object_object_add( poObj, "id",
                                json_object_new_int64(poFeature->GetFID()) );
    }

/* -------------------------------------------------------------------- */
/*      Write feature attributes to GeoJSON "properties" object.        */
/* -------------------------------------------------------------------- */
    bool bWriteIdIfFoundInAttributes = true;
    if( bIdAlreadyWritten && poNativeId != NULL )
    {
        int nIdx = poFeature->GetFieldIndex("id");
        if( json_object_get_type(poNativeId) == json_type_string &&
            nIdx >= 0 &&
            poFeature->GetFieldDefnRef(nIdx)->GetType() == OFTString &&
            strcmp(json_object_get_string(poNativeId), poFeature->GetFieldAsString(nIdx)) == 0 )
        {
            bWriteIdIfFoundInAttributes = false;
        }
        else if ( json_object_get_type(poNativeId) == json_type_int &&
                  nIdx >= 0 &&
                  (poFeature->GetFieldDefnRef(nIdx)->GetType() == OFTInteger ||
                   poFeature->GetFieldDefnRef(nIdx)->GetType() == OFTInteger64) &&
                  json_object_get_int64(poNativeId) == poFeature->GetFieldAsInteger64(nIdx) )
        {
            bWriteIdIfFoundInAttributes = false;
        }
    }

    if( bHasProperties )
    {
        json_object* poObjProps
            = OGRGeoJSONWriteAttributes( poFeature, bWriteIdIfFoundInAttributes, oOptions );
        json_object_object_add( poObj, "properties", poObjProps );
    }

/* -------------------------------------------------------------------- */
/*      Write feature geometry to GeoJSON "geometry" object.            */
/*      Null geometries are allowed, according to the GeoJSON Spec.     */
/* -------------------------------------------------------------------- */
    json_object* poObjGeom = NULL;

    OGRGeometry* poGeometry = poFeature->GetGeometryRef();
    if( NULL != poGeometry )
    {
        poObjGeom = OGRGeoJSONWriteGeometry( poGeometry, oOptions );

        if( bWriteBBOX && !poGeometry->IsEmpty() )
        {
            OGREnvelope3D sEnvelope = OGRGeoJSONGetBBox( poGeometry, oOptions );

            json_object* poObjBBOX = json_object_new_array();
            json_object_array_add(
                poObjBBOX,
                json_object_new_coord(sEnvelope.MinX, oOptions));
            json_object_array_add(
                poObjBBOX,
                json_object_new_coord(sEnvelope.MinY, oOptions));
            if( wkbHasZ(poGeometry->getGeometryType()) )
                json_object_array_add(
                    poObjBBOX,
                    json_object_new_coord(sEnvelope.MinZ, oOptions));
            json_object_array_add(
                poObjBBOX,
                json_object_new_coord(sEnvelope.MaxX, oOptions));
            json_object_array_add(
                poObjBBOX,
                json_object_new_coord(sEnvelope.MaxY, oOptions));
            if( wkbHasZ(poGeometry->getGeometryType()) )
                json_object_array_add(
                    poObjBBOX,
                    json_object_new_coord(sEnvelope.MaxZ, oOptions));

            json_object_object_add( poObj, "bbox", poObjBBOX );
        }

        bool bOutPatchableCoords = false;
        bool bOutCompatibleCoords = false;
        if( OGRGeoJSONIsPatchableGeometry( poObjGeom, poNativeGeom,
                                           bOutPatchableCoords,
                                           bOutCompatibleCoords ) )
        {
            OGRGeoJSONPatchGeometry( poObjGeom, poNativeGeom,
                                     bOutPatchableCoords, oOptions );
        }
    }

    json_object_object_add( poObj, "geometry", poObjGeom );

    if( poNativeGeom != NULL )
        json_object_put(poNativeGeom);

    return poObj;
}

/************************************************************************/
/*                        OGRGeoJSONWriteAttributes                     */
/************************************************************************/

json_object* OGRGeoJSONWriteAttributes( OGRFeature* poFeature,
                                        bool bWriteIdIfFoundInAttributes,
                                        const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poFeature );

    json_object* poObjProps = json_object_new_object();
    CPLAssert( NULL != poObjProps );

    OGRFeatureDefn* poDefn = poFeature->GetDefnRef();
    for( int nField = 0; nField < poDefn->GetFieldCount(); ++nField )
    {
        if( !poFeature->IsFieldSet(nField) )
        {
            continue;
        }

        OGRFieldDefn* poFieldDefn = poDefn->GetFieldDefn( nField );
        CPLAssert( NULL != poFieldDefn );
        OGRFieldType eType = poFieldDefn->GetType();
        OGRFieldSubType eSubType = poFieldDefn->GetSubType();

        if( !bWriteIdIfFoundInAttributes &&
            strcmp(poFieldDefn->GetNameRef(), "id") == 0 )
        {
            continue;
        }

        json_object* poObjProp = NULL;

        if( poFeature->IsFieldNull(nField) )
        {
            // poObjProp = NULL;
        }
        else if( OFTInteger == eType )
        {
            if( eSubType == OFSTBoolean )
                poObjProp = json_object_new_boolean(
                    poFeature->GetFieldAsInteger( nField ) );
            else
                poObjProp = json_object_new_int(
                    poFeature->GetFieldAsInteger( nField ) );
        }
        else if( OFTInteger64 == eType )
        {
            if( eSubType == OFSTBoolean )
                poObjProp = json_object_new_boolean(
                    (json_bool)poFeature->GetFieldAsInteger64( nField ) );
            else
                poObjProp = json_object_new_int64(
                    poFeature->GetFieldAsInteger64( nField ) );
        }
        else if( OFTReal == eType )
        {
            poObjProp = json_object_new_double_with_significant_figures(
                poFeature->GetFieldAsDouble(nField),
                oOptions.nSignificantFigures );
        }
        else if( OFTString == eType )
        {
            const char* pszStr = poFeature->GetFieldAsString(nField);
            const size_t nLen = strlen(pszStr);
            poObjProp = NULL;
            if( (pszStr[0] == '{' && pszStr[nLen-1] == '}') ||
                (pszStr[0] == '[' && pszStr[nLen-1] == ']') )
            {
                OGRJSonParse(pszStr, &poObjProp, false);
            }
            if( poObjProp == NULL )
                poObjProp = json_object_new_string( pszStr );
        }
        else if( OFTIntegerList == eType )
        {
            int nSize = 0;
            const int* panList =
                poFeature->GetFieldAsIntegerList(nField, &nSize);
            poObjProp = json_object_new_array();
            for( int i = 0; i < nSize; i++ )
            {
                if( eSubType == OFSTBoolean )
                    json_object_array_add(
                        poObjProp,
                        json_object_new_boolean(panList[i]));
                else
                    json_object_array_add(
                        poObjProp,
                        json_object_new_int(panList[i]));
            }
        }
        else if( OFTInteger64List == eType )
        {
            int nSize = 0;
            const GIntBig* panList =
                poFeature->GetFieldAsInteger64List(nField, &nSize);
            poObjProp = json_object_new_array();
            for( int i = 0; i < nSize; i++ )
            {
                if( eSubType == OFSTBoolean )
                    json_object_array_add(
                        poObjProp,
                        json_object_new_boolean(
                            static_cast<json_bool>(panList[i])));
                else
                    json_object_array_add(
                        poObjProp,
                        json_object_new_int64(panList[i]));
            }
        }
        else if( OFTRealList == eType )
        {
            int nSize = 0;
            const double* padfList =
                poFeature->GetFieldAsDoubleList(nField, &nSize);
            poObjProp = json_object_new_array();
            for( int i = 0; i < nSize; i++ )
            {
                json_object_array_add(
                    poObjProp,
                    json_object_new_double_with_significant_figures(
                        padfList[i],
                        oOptions.nSignificantFigures));
            }
        }
        else if( OFTStringList == eType )
        {
            char** papszStringList = poFeature->GetFieldAsStringList(nField);
            poObjProp = json_object_new_array();
            for( int i = 0; papszStringList && papszStringList[i]; i++ )
            {
                json_object_array_add(
                    poObjProp,
                    json_object_new_string(papszStringList[i]));
            }
        }
        else
        {
            poObjProp = json_object_new_string(
                poFeature->GetFieldAsString(nField) );
        }

        json_object_object_add( poObjProps,
                                poFieldDefn->GetNameRef(),
                                poObjProp );
    }

    return poObjProps;
}

/************************************************************************/
/*                           OGRGeoJSONWriteGeometry                    */
/************************************************************************/

json_object* OGRGeoJSONWriteGeometry( OGRGeometry* poGeometry,
                                      int nCoordPrecision,
                                      int nSignificantFigures )
{
    OGRGeoJSONWriteOptions oOptions;
    oOptions.nCoordPrecision = nCoordPrecision;
    oOptions.nSignificantFigures = nSignificantFigures;
    return OGRGeoJSONWriteGeometry( poGeometry, oOptions );
}

json_object* OGRGeoJSONWriteGeometry( OGRGeometry* poGeometry,
                                      const OGRGeoJSONWriteOptions& oOptions )
{
    if( poGeometry == NULL )
    {
        CPLAssert( false );
        return NULL;
    }

    OGRwkbGeometryType eFType = wkbFlatten(poGeometry->getGeometryType());
    // For point empty, return a null geometry. For other empty geometry types,
    // we will generate an empty coordinate array, which is propably also
    // borderline.
    if( eFType == wkbPoint && poGeometry->IsEmpty() )
    {
        return NULL;
    }

    json_object* poObj = json_object_new_object();
    CPLAssert( NULL != poObj );

/* -------------------------------------------------------------------- */
/*      Build "type" member of GeoJSOn "geometry" object.               */
/* -------------------------------------------------------------------- */

    // XXX - mloskot: workaround hack for pure JSON-C API design.
    char* pszName = const_cast<char*>(OGRGeoJSONGetGeometryName( poGeometry ));
    json_object_object_add( poObj, "type", json_object_new_string(pszName) );

/* -------------------------------------------------------------------- */
/*      Build "coordinates" member of GeoJSOn "geometry" object.        */
/* -------------------------------------------------------------------- */
    json_object* poObjGeom = NULL;

    if( eFType == wkbGeometryCollection  )
    {
        poObjGeom =
            OGRGeoJSONWriteGeometryCollection(
                static_cast<OGRGeometryCollection*>(poGeometry), oOptions );
        json_object_object_add( poObj, "geometries", poObjGeom);
    }
    else
    {
        if( wkbPoint == eFType )
            poObjGeom =
                OGRGeoJSONWritePoint( static_cast<OGRPoint*>(poGeometry),
                                      oOptions );
        else if( wkbLineString == eFType )
            poObjGeom =
                OGRGeoJSONWriteLineString(
                    static_cast<OGRLineString*>(poGeometry), oOptions );
        else if( wkbPolygon == eFType )
            poObjGeom =
                OGRGeoJSONWritePolygon( static_cast<OGRPolygon*>(poGeometry),
                                        oOptions );
        else if( wkbMultiPoint == eFType )
            poObjGeom =
                OGRGeoJSONWriteMultiPoint(
                    static_cast<OGRMultiPoint*>(poGeometry), oOptions );
        else if( wkbMultiLineString == eFType )
            poObjGeom =
                OGRGeoJSONWriteMultiLineString(
                    static_cast<OGRMultiLineString*>(poGeometry), oOptions );
        else if( wkbMultiPolygon == eFType )
            poObjGeom =
                OGRGeoJSONWriteMultiPolygon(
                    static_cast<OGRMultiPolygon*>(poGeometry), oOptions );
        else
        {
            CPLDebug( "GeoJSON",
                      "Unsupported geometry type detected. "
                      "Feature gets NULL geometry assigned." );
        }

        if( poObjGeom != NULL )
        {
            json_object_object_add( poObj, "coordinates", poObjGeom);
        }
        else
        {
            json_object_put(poObj);
            poObj = NULL;
        }
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWritePoint                       */
/************************************************************************/

json_object* OGRGeoJSONWritePoint( OGRPoint* poPoint,
                                   const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poPoint );

    json_object* poObj = NULL;

    // Generate "coordinates" object for 2D or 3D dimension.
    if( wkbHasZ(poPoint->getGeometryType()) )
    {
        poObj = OGRGeoJSONWriteCoords( poPoint->getX(),
                                       poPoint->getY(),
                                       poPoint->getZ(),
                                       oOptions );
    }
    else if( !poPoint->IsEmpty() )
    {
        poObj = OGRGeoJSONWriteCoords( poPoint->getX(),
                                       poPoint->getY(),
                                       oOptions );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteLineString                  */
/************************************************************************/

json_object* OGRGeoJSONWriteLineString( OGRLineString* poLine,
                                        const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poLine );

    // Generate "coordinates" object for 2D or 3D dimension.
    json_object* poObj =
        OGRGeoJSONWriteLineCoords( poLine, oOptions );

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWritePolygon                     */
/************************************************************************/

json_object* OGRGeoJSONWritePolygon( OGRPolygon* poPolygon,
                                     const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poPolygon );

    // Generate "coordinates" array object.
    json_object* poObj = json_object_new_array();

    // Exterior ring.
    OGRLinearRing* poRing = poPolygon->getExteriorRing();
    if( poRing == NULL )
        return poObj;

    json_object* poObjRing =
        OGRGeoJSONWriteRingCoords( poRing, true, oOptions );
    if( poObjRing == NULL )
    {
        json_object_put(poObj);
        return NULL;
    }
    json_object_array_add( poObj, poObjRing );

    // Interior rings.
    const int nCount = poPolygon->getNumInteriorRings();
    for( int i = 0; i < nCount; ++i )
    {
        poRing = poPolygon->getInteriorRing( i );
        if( poRing == NULL )
            continue;

        poObjRing =
            OGRGeoJSONWriteRingCoords( poRing, false, oOptions );
        if( poObjRing == NULL )
        {
            json_object_put(poObj);
            return NULL;
        }

        json_object_array_add( poObj, poObjRing );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteMultiPoint                  */
/************************************************************************/

json_object* OGRGeoJSONWriteMultiPoint( OGRMultiPoint* poGeometry,
                                        const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poGeometry );

    // Generate "coordinates" object for 2D or 3D dimension.
    json_object* poObj
        = json_object_new_array();

    for( int i = 0; i < poGeometry->getNumGeometries(); ++i )
    {
        OGRGeometry* poGeom = poGeometry->getGeometryRef( i );
        CPLAssert( NULL != poGeom );
        OGRPoint* poPoint = static_cast<OGRPoint*>(poGeom);

        json_object* poObjPoint =
            OGRGeoJSONWritePoint(poPoint, oOptions);
        if( poObjPoint == NULL )
        {
            json_object_put(poObj);
            return NULL;
        }

        json_object_array_add( poObj, poObjPoint );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteMultiLineString             */
/************************************************************************/

json_object* OGRGeoJSONWriteMultiLineString( OGRMultiLineString* poGeometry,
                                             const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poGeometry );

    // Generate "coordinates" object for 2D or 3D dimension.
    json_object* poObj = json_object_new_array();

    for( int i = 0; i < poGeometry->getNumGeometries(); ++i )
    {
        OGRGeometry* poGeom = poGeometry->getGeometryRef( i );
        CPLAssert( NULL != poGeom );
        OGRLineString* poLine = static_cast<OGRLineString*>(poGeom);

        json_object* poObjLine =
            OGRGeoJSONWriteLineString( poLine, oOptions );
        if( poObjLine == NULL )
        {
            json_object_put(poObj);
            return NULL;
        }

        json_object_array_add( poObj, poObjLine );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteMultiPolygon                */
/************************************************************************/

json_object* OGRGeoJSONWriteMultiPolygon( OGRMultiPolygon* poGeometry,
                                          const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poGeometry );

    // Generate "coordinates" object for 2D or 3D dimension.
    json_object* poObj = json_object_new_array();

    for( int i = 0; i < poGeometry->getNumGeometries(); ++i )
    {
        OGRGeometry* poGeom = poGeometry->getGeometryRef( i );
        CPLAssert( NULL != poGeom );
        OGRPolygon* poPoly = static_cast<OGRPolygon*>(poGeom);

        json_object* poObjPoly =
            OGRGeoJSONWritePolygon( poPoly, oOptions );
        if( poObjPoly == NULL )
        {
            json_object_put(poObj);
            return NULL;
        }

        json_object_array_add( poObj, poObjPoly );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteGeometryCollection          */
/************************************************************************/

json_object* OGRGeoJSONWriteGeometryCollection(
    OGRGeometryCollection* poGeometry, const OGRGeoJSONWriteOptions& oOptions )
{
    CPLAssert( NULL != poGeometry );

    /* Generate "geometries" object. */
    json_object* poObj = json_object_new_array();

    for( int i = 0; i < poGeometry->getNumGeometries(); ++i )
    {
        OGRGeometry* poGeom = poGeometry->getGeometryRef( i );
        CPLAssert( NULL != poGeom );

        json_object* poObjGeom =
            OGRGeoJSONWriteGeometry( poGeom, oOptions );
        if( poGeom == NULL )
        {
            json_object_put(poObj);
            return NULL;
        }

        json_object_array_add( poObj, poObjGeom );
    }

    return poObj;
}

/************************************************************************/
/*                           OGRGeoJSONWriteCoords                      */
/************************************************************************/

json_object* OGRGeoJSONWriteCoords( double const& fX, double const& fY,
                                    const OGRGeoJSONWriteOptions& oOptions )
{
    json_object* poObjCoords = NULL;
    if( CPLIsInf(fX) || CPLIsInf(fY) ||
        CPLIsNan(fX) || CPLIsNan(fY) )
    {
        CPLError(CE_Warning, CPLE_AppDefined,
                 "Infinite or NaN coordinate encountered");
        return NULL;
    }
    poObjCoords = json_object_new_array();
    json_object_array_add( poObjCoords,
                           json_object_new_coord( fX, oOptions ) );
    json_object_array_add( poObjCoords,
                           json_object_new_coord( fY, oOptions ) );

    return poObjCoords;
}

json_object* OGRGeoJSONWriteCoords( double const& fX, double const& fY,
                                    double const& fZ,
                                    const OGRGeoJSONWriteOptions& oOptions )
{
    if( CPLIsInf(fX) || CPLIsInf(fY) || CPLIsInf(fZ) ||
        CPLIsNan(fX) || CPLIsNan(fY) || CPLIsNan(fZ) )
    {
        CPLError(CE_Warning, CPLE_AppDefined,
                 "Infinite or NaN coordinate encountered");
        return NULL;
    }
    json_object* poObjCoords = json_object_new_array();
    json_object_array_add( poObjCoords,
                           json_object_new_coord( fX, oOptions ) );
    json_object_array_add( poObjCoords,
                           json_object_new_coord( fY, oOptions ) );
    json_object_array_add( poObjCoords,
                           json_object_new_coord( fZ, oOptions ) );

    return poObjCoords;
}

/************************************************************************/
/*                           OGRGeoJSONWriteLineCoords                  */
/************************************************************************/

json_object* OGRGeoJSONWriteLineCoords( OGRLineString* poLine,
                                        const OGRGeoJSONWriteOptions& oOptions )
{
    json_object* poObjPoint = NULL;
    json_object* poObjCoords = json_object_new_array();

    const int nCount = poLine->getNumPoints();
    const bool bHasZ = wkbHasZ(poLine->getGeometryType());
    for( int i = 0; i < nCount; ++i )
    {
        if( !bHasZ )
            poObjPoint =
                OGRGeoJSONWriteCoords( poLine->getX(i), poLine->getY(i),
                                       oOptions );
        else
            poObjPoint =
                OGRGeoJSONWriteCoords( poLine->getX(i), poLine->getY(i),
                                       poLine->getZ(i),
                                       oOptions );
        if( poObjPoint == NULL )
        {
            json_object_put(poObjCoords);
            return NULL;
        }
        json_object_array_add( poObjCoords, poObjPoint );
    }

    return poObjCoords;
}

/************************************************************************/
/*                        OGRGeoJSONWriteRingCoords                     */
/************************************************************************/

json_object* OGRGeoJSONWriteRingCoords( OGRLinearRing* poLine,
                                        bool bIsExteriorRing,
                                        const OGRGeoJSONWriteOptions& oOptions )
{
    json_object* poObjPoint = NULL;
    json_object* poObjCoords = json_object_new_array();

    bool bInvertOrder = oOptions.bPolygonRightHandRule &&
                        ((bIsExteriorRing && poLine->isClockwise()) ||
                         (!bIsExteriorRing && !poLine->isClockwise()));

    const int nCount = poLine->getNumPoints();
    const bool bHasZ = wkbHasZ(poLine->getGeometryType());
    for( int i = 0; i < nCount; ++i )
    {
        const int nIdx = (bInvertOrder) ? nCount - 1 - i: i;
        if( !bHasZ )
            poObjPoint =
                OGRGeoJSONWriteCoords( poLine->getX(nIdx), poLine->getY(nIdx),
                                       oOptions );
        else
            poObjPoint =
                OGRGeoJSONWriteCoords( poLine->getX(nIdx), poLine->getY(nIdx),
                                       poLine->getZ(nIdx),
                                       oOptions );
        if( poObjPoint == NULL )
        {
            json_object_put(poObjCoords);
            return NULL;
        }
        json_object_array_add( poObjCoords, poObjPoint );
    }

    return poObjCoords;
}

/************************************************************************/
/*                           OGR_G_ExportToJson                         */
/************************************************************************/

/**
 * \brief Convert a geometry into GeoJSON format.
 *
 * The returned string should be freed with CPLFree() when no longer required.
 *
 * This method is the same as the C++ method OGRGeometry::exportToJson().
 *
 * @param hGeometry handle to the geometry.
 * @return A GeoJSON fragment or NULL in case of error.
 */

char* OGR_G_ExportToJson( OGRGeometryH hGeometry )
{
    return OGR_G_ExportToJsonEx(hGeometry, NULL);
}

/************************************************************************/
/*                           OGR_G_ExportToJsonEx                       */
/************************************************************************/

/**
 * \brief Convert a geometry into GeoJSON format.
 *
 * The returned string should be freed with CPLFree() when no longer required.
 *
 * The following options are supported :
 * <ul>
 * <li>COORDINATE_PRECISION=number: maximum number of figures after decimal separator to write in coordinates.</li>
 * <li>SIGNIFICANT_FIGURES=number: maximum number of significant figures (GDAL &gt;= 2.1).</li>
 * </ul>
 *
 * If COORDINATE_PRECISION is defined, SIGNIFICANT_FIGURES will be ignored if
 * specified.
 * When none are defined, the default is COORDINATE_PRECISION=15.
 *
 * This method is the same as the C++ method OGRGeometry::exportToJson().
 *
 * @param hGeometry handle to the geometry.
 * @param papszOptions a null terminated list of options.
 * @return A GeoJSON fragment or NULL in case of error.
 *
 * @since OGR 1.9.0
 */

char* OGR_G_ExportToJsonEx( OGRGeometryH hGeometry, char** papszOptions )
{
    VALIDATE_POINTER1( hGeometry, "OGR_G_ExportToJson", NULL );

    OGRGeometry* poGeometry = reinterpret_cast<OGRGeometry *>( hGeometry );

    const int nCoordPrecision =
        atoi(CSLFetchNameValueDef(papszOptions, "COORDINATE_PRECISION", "-1"));

    const int nSignificantFigures =
        atoi(CSLFetchNameValueDef(papszOptions, "SIGNIFICANT_FIGURES", "-1"));

    OGRGeoJSONWriteOptions oOptions;
    oOptions.nCoordPrecision = nCoordPrecision;
    oOptions.nSignificantFigures = nSignificantFigures;

    json_object* poObj =
       OGRGeoJSONWriteGeometry( poGeometry, oOptions );

    if( NULL != poObj )
    {
        char* pszJson = CPLStrdup( json_object_to_json_string( poObj ) );

        // Release JSON tree.
        json_object_put( poObj );

        return pszJson;
    }

    // Translation failed.
    return NULL;
}

/************************************************************************/
/*               OGR_json_double_with_precision_to_string()             */
/************************************************************************/

static int OGR_json_double_with_precision_to_string( struct json_object *jso,
                                                     struct printbuf *pb,
                                                     int /* level */,
                                                     int /* flags */)
{
    // TODO(schwehr): Explain this casting.
    const int nPrecision =
        static_cast<int>(reinterpret_cast<GUIntptr_t>(jso->_userdata));
    char szBuffer[75] = {};
    OGRFormatDouble( szBuffer, sizeof(szBuffer), jso->o.c_double, '.',
                     (nPrecision < 0) ? 15 : nPrecision );
    if( szBuffer[0] == 't' /*oobig */ )
    {
        CPLsnprintf(szBuffer, sizeof(szBuffer), "%.18g", jso->o.c_double);
    }
    return printbuf_memappend(pb, szBuffer, static_cast<int>(strlen(szBuffer)));
}

/************************************************************************/
/*                   json_object_new_double_with_precision()            */
/************************************************************************/

json_object* json_object_new_double_with_precision(double dfVal,
                                                   int nCoordPrecision)
{
    json_object* jso = json_object_new_double(dfVal);
    json_object_set_serializer(jso, OGR_json_double_with_precision_to_string,
                               (void*)(size_t)nCoordPrecision, NULL );
    return jso;
}

/************************************************************************/
/*             OGR_json_double_with_significant_figures_to_string()     */
/************************************************************************/

static int
OGR_json_double_with_significant_figures_to_string( struct json_object *jso,
                                                    struct printbuf *pb,
                                                    int /* level */,
                                                    int /* flags */)
{
    char szBuffer[75] = {};
    int nSize = 0;
    if( CPLIsNan(jso->o.c_double))
        nSize = CPLsnprintf(szBuffer, sizeof(szBuffer), "NaN");
    else if( CPLIsInf(jso->o.c_double) )
    {
        if( jso->o.c_double > 0 )
            nSize = CPLsnprintf(szBuffer, sizeof(szBuffer), "Infinity");
        else
            nSize = CPLsnprintf(szBuffer, sizeof(szBuffer), "-Infinity");
    }
    else
    {
        char szFormatting[32] = {};
        const int nSignificantFigures = (int) (GUIntptr_t) jso->_userdata;
        const int nInitialSignificantFigures =
            nSignificantFigures >= 0 ? nSignificantFigures : 17;
        CPLsnprintf(szFormatting, sizeof(szFormatting),
                    "%%.%dg", nInitialSignificantFigures);
        nSize = CPLsnprintf(szBuffer, sizeof(szBuffer),
                            szFormatting, jso->o.c_double);
        const char* pszDot = NULL;
        if( nSize+2 < static_cast<int>(sizeof(szBuffer)) &&
            (pszDot = strchr(szBuffer, '.')) == NULL )
        {
            nSize += CPLsnprintf(szBuffer + nSize, sizeof(szBuffer) - nSize,
                                 ".0");
        }

        // Try to avoid .xxxx999999y or .xxxx000000y rounding issues by
        // decreasing a bit precision.
        if( nInitialSignificantFigures > 10 &&
            pszDot != NULL &&
            (strstr(pszDot, "999999") != NULL ||
             strstr(pszDot, "000000") != NULL) )
        {
            bool bOK = false;
            for( int i = 1; i <= 3; i++ )
            {
                CPLsnprintf(szFormatting, sizeof(szFormatting),
                            "%%.%dg", nInitialSignificantFigures- i);
                nSize = CPLsnprintf(szBuffer, sizeof(szBuffer),
                                    szFormatting, jso->o.c_double);
                pszDot = strchr(szBuffer, '.');
                if( pszDot != NULL &&
                    strstr(pszDot, "999999") == NULL &&
                    strstr(pszDot, "000000") == NULL )
                {
                    bOK = true;
                    break;
                }
            }
            if( !bOK )
            {
                CPLsnprintf(szFormatting, sizeof(szFormatting),
                            "%%.%dg", nInitialSignificantFigures);
                nSize = CPLsnprintf(szBuffer, sizeof(szBuffer),
                                    szFormatting, jso->o.c_double);
                if( nSize+2 < static_cast<int>(sizeof(szBuffer)) &&
                    strchr(szBuffer, '.') == NULL )
                {
                    nSize +=
                        CPLsnprintf(szBuffer + nSize, sizeof(szBuffer) - nSize,
                                    ".0");
                }
            }
        }
    }

    return printbuf_memappend(pb, szBuffer, nSize);
}

/************************************************************************/
/*              json_object_new_double_with_significant_figures()       */
/************************************************************************/

json_object *
json_object_new_double_with_significant_figures( double dfVal,
                                                 int nSignificantFigures )
{
    json_object* jso = json_object_new_double(dfVal);
    // TODO(schwehr): Convert C cast.
    json_object_set_serializer(
        jso, OGR_json_double_with_significant_figures_to_string,
        (void*)(size_t)nSignificantFigures, NULL );
    return jso;
}
