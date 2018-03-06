/******************************************************************************
 *
 * Project:  WFS Translator
 * Purpose:  Implements OGRWFSDriver.
 * Author:   Even Rouault, even dot rouault at spatialys dot com
 *
 ******************************************************************************
 * Copyright (c) 2018, Even Rouault <even dot rouault at spatialys dot com>
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

#include "ogrsf_frmts.h"
#include "cpl_conv.h"
#include "cpl_http.h"
#include "swq.h"

#include <memory>
#include <vector>
#include <set>

// g++ -Wshadow -Wextra -std=c++11 -fPIC -g -Wall ogr/ogrsf_frmts/wfs/ogrwfs3*.cpp -shared -o ogr_WFS3.so -Iport -Igcore -Iogr -Iogr/ogrsf_frmts -Iogr/ogrsf_frmts/gml -Iogr/ogrsf_frmts/wfs -L. -lgdal

extern "C" void RegisterOGRWFS3();

/************************************************************************/
/*                           OGRWFS3Dataset                             */
/************************************************************************/
class OGRWFS3Layer;

class OGRWFS3Dataset : public GDALDataset
{
        friend class OGRWFS3Layer;

        CPLString                              m_osRootURL;
        int                                    m_nPageSize = 10;
        std::vector<std::unique_ptr<OGRLayer>> m_apoLayers;
        bool                                   m_bAPIDocLoaded = false;
        CPLJSONDocument                        m_oAPIDoc;

        bool                    DownloadJSon(
            const CPLString& osURL,
            CPLJSONDocument& oDoc,
            const char* pszAccept = "application/geo+json, application/json",
            CPLStringList* paosHeaders = nullptr);

    public:
        OGRWFS3Dataset() {}
        ~OGRWFS3Dataset() {}

        int GetLayerCount() override
                            { return static_cast<int>(m_apoLayers.size()); }
        OGRLayer* GetLayer(int idx) override;

        bool Open(GDALOpenInfo*);
        const CPLJSONDocument& GetAPIDoc();
};

/************************************************************************/
/*                            OGRWFS3Layer                              */
/************************************************************************/

class OGRWFS3Layer: public OGRLayer
{
        OGRWFS3Dataset* m_poDS = nullptr;
        OGRFeatureDefn* m_poFeatureDefn = nullptr;
        OGREnvelope     m_oExtent;
        bool            m_bFeatureDefnEstablished = false;
        std::unique_ptr<GDALDataset> m_poUnderlyingDS;
        OGRLayer*       m_poUnderlyingLayer = nullptr;
        GIntBig         m_nFID = 1;
        CPLString       m_osGetURL;
        CPLString       m_osAttributeFilter;
        bool            m_bFilterMustBeClientSideEvaluated = false;
        bool            m_bGotQueriableAttributes = false;
        std::set<CPLString> m_aoSetQueriableAttributes;

        void            EstablishFeatureDefn();
        OGRFeature     *GetNextRawFeature();
        CPLString       AddFilters(const CPLString& osURL);
        CPLString       BuildFilter(swq_expr_node* poNode);
        bool            SupportsResultTypeHits();
        void            GetQueriableAttributes();

    public:
        OGRWFS3Layer(OGRWFS3Dataset* poDS,
                     const CPLString& osName,
                     const CPLString& osTitle,
                     const CPLString& osDescription,
                     const CPLJSONArray& oBBOX,
                     const CPLJSONArray& oLinks,
                     const CPLJSONArray& oCRS);
       ~OGRWFS3Layer();

       const char*     GetName() override { return GetDescription(); }
       OGRFeatureDefn* GetLayerDefn() override;
       void            ResetReading() override;
       OGRFeature*     GetNextFeature() override;
       int             TestCapability(const char*) override { return false; }
       GIntBig         GetFeatureCount(int bForce = FALSE) override;
       OGRErr          GetExtent(OGREnvelope *psExtent,
                                 int bForce = TRUE) override;
       OGRErr          GetExtent(int iGeomField, OGREnvelope *psExtent,
                                 int bForce) override
                { return OGRLayer::GetExtent(iGeomField, psExtent, bForce); }

       void            SetSpatialFilter( OGRGeometry *poGeom ) override;
       void            SetSpatialFilter( int iGeomField, OGRGeometry *poGeom )
                                                                    override
                { OGRLayer::SetSpatialFilter(iGeomField, poGeom); }
       OGRErr          SetAttributeFilter( const char *pszQuery ) override;

};

/************************************************************************/
/*                           DownloadJSon()                             */
/************************************************************************/

bool OGRWFS3Dataset::DownloadJSon(const CPLString& osURL,
                                  CPLJSONDocument& oDoc,
                                  const char* pszAccept,
                                  CPLStringList* paosHeaders)
{
    char** papszOptions = CSLSetNameValue(nullptr,
            "HEADERS", (CPLString("Accept: ") + pszAccept).c_str());
    CPLHTTPResult* psResult = CPLHTTPFetch(osURL, papszOptions);
    CSLDestroy(papszOptions);
    if( !psResult )
        return false;

    if( psResult->pszErrBuf != nullptr )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "%s",
                psResult->pabyData ?
                    reinterpret_cast<const char*>(psResult->pabyData) :
                psResult->pszErrBuf);
        CPLHTTPDestroyResult(psResult);
        return false;
    }

    if( psResult->pszContentType == nullptr ||
        (strstr(psResult->pszContentType, "application/json") == nullptr &&
         strstr(psResult->pszContentType, "application/geo+json") == nullptr) )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Unexpected Content-Type: %s",
                 psResult->pszContentType ?
                    psResult->pszContentType : "(null)" );
        CPLHTTPDestroyResult(psResult);
        return false;
    }

    if( psResult->pabyData == nullptr )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Empty content returned by server");
        CPLHTTPDestroyResult(psResult);
        return false;
    }

    if( paosHeaders )
    {
        *paosHeaders = CSLDuplicate(psResult->papszHeaders);
    }

    bool bOK =
        oDoc.LoadMemory( reinterpret_cast<const char*>(psResult->pabyData) );
    CPLHTTPDestroyResult(psResult);
    return bOK;
}

/************************************************************************/
/*                            GetAPIDoc()                               */
/************************************************************************/

const CPLJSONDocument& OGRWFS3Dataset::GetAPIDoc()
{
    if( m_bAPIDocLoaded )
        return m_oAPIDoc;
    m_bAPIDocLoaded = true;
    CPLPushErrorHandler(CPLQuietErrorHandler);
    bool bOK = DownloadJSon(m_osRootURL + "/api", m_oAPIDoc,
                     "application/openapi+json;version=3.0, application/json");
    CPLPopErrorHandler();
    CPLErrorReset();
    if( bOK )
    {
        return m_oAPIDoc;
    }

#ifndef REMOVE_HACK
    if( DownloadJSon(m_osRootURL + "/api/", m_oAPIDoc,
                     "application/openapi+json;version=3.0, application/json") )
    {
        return m_oAPIDoc;
    }
#endif
    return m_oAPIDoc;
}

/************************************************************************/
/*                              Open()                                  */
/************************************************************************/

bool OGRWFS3Dataset::Open(GDALOpenInfo* poOpenInfo)
{
    m_osRootURL = 
        CSLFetchNameValueDef(poOpenInfo->papszOpenOptions, "URL",
            poOpenInfo->pszFilename + strlen("WFS3:"));
    CPLJSONDocument oDoc;
    if( !DownloadJSon(m_osRootURL, oDoc) )
        return false;
    m_nPageSize = atoi( CSLFetchNameValueDef(poOpenInfo->papszOpenOptions,
                            "PAGE_SIZE",CPLSPrintf("%d", m_nPageSize)) );

    CPLJSONArray oCollections = oDoc.GetRoot().GetArray("collections");
    if( !oCollections.IsValid() )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "No collections array");
        return false;
    }

    for( int i = 0; i < oCollections.Size(); i++ )
    {
        CPLJSONObject oCollection = oCollections[i];
        if( oCollection.GetType() != CPLJSONObject::Type::Object )
            continue;
        CPLString osName( oCollection.GetString("name") );
        if( osName.empty() )
            continue;
        CPLString osTitle( oCollection.GetString("title") );
        CPLString osDescription( oCollection.GetString("description") );
        CPLJSONArray oBBOX = oCollection.GetArray("extent/bbox");
        CPLJSONArray oLinks = oCollection.GetArray("links");
        CPLJSONArray oCRS = oCollection.GetArray("crs");
        m_apoLayers.push_back( std::unique_ptr<OGRWFS3Layer>( new
            OGRWFS3Layer(this, osName, osTitle, osDescription,
                         oBBOX, oLinks, oCRS) ) );
    }

    return true;
}

/************************************************************************/
/*                             GetLayer()                               */
/************************************************************************/

OGRLayer* OGRWFS3Dataset::GetLayer(int nIndex)
{
    if( nIndex < 0 || nIndex >= GetLayerCount() )
        return nullptr;
    return m_apoLayers[nIndex].get();
}

/************************************************************************/
/*                             Identify()                               */
/************************************************************************/

static int OGRWFS3DriverIdentify( GDALOpenInfo* poOpenInfo )

{
    return STARTS_WITH_CI(poOpenInfo->pszFilename, "WFS3:");
}

/************************************************************************/
/*                           OGRWFS3Layer()                             */
/************************************************************************/

OGRWFS3Layer::OGRWFS3Layer(OGRWFS3Dataset* poDS,
                           const CPLString& osName,
                           const CPLString& osTitle,
                           const CPLString& osDescription,
                           const CPLJSONArray& oBBOX,
                           const CPLJSONArray& /* oLinks */,
                           const CPLJSONArray& oCRS) :
    m_poDS(poDS)
{
    m_poFeatureDefn = new OGRFeatureDefn(osName);
    m_poFeatureDefn->Reference();
    SetDescription(osName);
    if( !osTitle.empty() )
        SetMetadataItem("TITLE", osTitle.c_str());
    if( !osDescription.empty() )
        SetMetadataItem("DESCRIPTION", osDescription.c_str());
    if( oBBOX.IsValid() && oBBOX.Size() == 4 )
    {
        m_oExtent.MinX = oBBOX[0].ToDouble();
        m_oExtent.MinY = oBBOX[1].ToDouble();
        m_oExtent.MaxX = oBBOX[2].ToDouble();
        m_oExtent.MaxY = oBBOX[3].ToDouble();

        // Handle bbox over antimerdian, which we do not support properly
        // in OGR
        if( m_oExtent.MinX > m_oExtent.MaxX &&
            fabs(m_oExtent.MinX) <= 180.0 &&
            fabs(m_oExtent.MaxX) <= 180.0 )
        {
            m_oExtent.MinX = -180.0;
            m_oExtent.MaxX = 180.0;
        }
    }
    if( !oCRS.IsValid() || oCRS.Size() == 0 )
    {
        OGRSpatialReference* poSRS = new OGRSpatialReference();
        poSRS->SetFromUserInput(SRS_WKT_WGS84);
        m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
        poSRS->Release();
    }

    ResetReading();
}

/************************************************************************/
/*                          ~OGRWFS3Layer()                             */
/************************************************************************/

OGRWFS3Layer::~OGRWFS3Layer()
{
    m_poFeatureDefn->Release();
}

/************************************************************************/
/*                            GetLayerDefn()                            */
/************************************************************************/

OGRFeatureDefn* OGRWFS3Layer::GetLayerDefn()
{
    if( !m_bFeatureDefnEstablished )
        EstablishFeatureDefn();
    return m_poFeatureDefn;
}

/************************************************************************/
/*                        EstablishFeatureDefn()                        */
/************************************************************************/

void OGRWFS3Layer::EstablishFeatureDefn()
{
    CPLAssert(!m_bFeatureDefnEstablished);
    m_bFeatureDefnEstablished = true;

    CPLJSONDocument oDoc;
    if( !m_poDS->DownloadJSon(
                m_poDS->m_osRootURL + "/" + GetDescription(), oDoc) )
        return;

    CPLString osTmpFilename(CPLSPrintf("/vsimem/wfs3_%p.json", this));
    oDoc.Save(osTmpFilename);
    std::unique_ptr<GDALDataset> poDS(
      reinterpret_cast<GDALDataset*>(
        GDALOpenEx(osTmpFilename, GDAL_OF_VECTOR | GDAL_OF_INTERNAL,
                   nullptr, nullptr, nullptr)));
    VSIUnlink(osTmpFilename);
    if( !poDS.get() )
        return;
    OGRLayer* poLayer = poDS->GetLayer(0);
    if( !poLayer )
        return;
    OGRFeatureDefn* poFeatureDefn = poLayer->GetLayerDefn();
    m_poFeatureDefn->SetGeomType( poFeatureDefn->GetGeomType() );
    for( int i = 0; i < poFeatureDefn->GetFieldCount(); i++ )
    {
        m_poFeatureDefn->AddFieldDefn( poFeatureDefn->GetFieldDefn(i) );
    }
}

/************************************************************************/
/*                           ResetReading()                             */
/************************************************************************/

void OGRWFS3Layer::ResetReading()
{
    m_poUnderlyingDS.reset();
    m_poUnderlyingLayer = nullptr;
    m_nFID = 1;
    m_osGetURL = m_poDS->m_osRootURL + "/" + GetDescription();
    if( m_poDS->m_nPageSize > 0 )
    {
        m_osGetURL = CPLURLAddKVP(m_osGetURL, "count",
                            CPLSPrintf("%d", m_poDS->m_nPageSize));
    }
    m_osGetURL = AddFilters(m_osGetURL);
}

/************************************************************************/
/*                           AddFilters()                               */
/************************************************************************/

CPLString OGRWFS3Layer::AddFilters(const CPLString& osURL)
{
    CPLString osURLNew(osURL);
    if( m_poFilterGeom )
    {
        osURLNew = CPLURLAddKVP(osURLNew, "bbox",
            CPLSPrintf("%.18g,%.18g,%.18g,%.18g",
                       m_sFilterEnvelope.MinX,
                       m_sFilterEnvelope.MinY,
                       m_sFilterEnvelope.MaxX,
                       m_sFilterEnvelope.MaxY));
    }
    if( !m_osAttributeFilter.empty() )
    {
        if( osURLNew.find('?') == std::string::npos )
            osURLNew += "?";
        else
            osURLNew += "&";
        osURLNew += m_osAttributeFilter;
    }
    return osURLNew;
}

/************************************************************************/
/*                         GetNextRawFeature()                          */
/************************************************************************/

OGRFeature* OGRWFS3Layer::GetNextRawFeature()
{
    if( !m_bFeatureDefnEstablished )
        EstablishFeatureDefn();

    OGRFeature* poSrcFeature = nullptr;
    while( true )
    {
        if( m_poUnderlyingDS.get() == nullptr )
        {
            if( m_osGetURL.empty() )
                return nullptr;

            CPLJSONDocument oDoc;

            CPLString osURL(m_osGetURL);
            m_osGetURL.clear();
            CPLStringList aosHeaders;
            if( !m_poDS->DownloadJSon(osURL, oDoc,
                                      "application/geo+json, application/json",
                                      &aosHeaders) )
            {
                return nullptr;
            }

            CPLString osTmpFilename(CPLSPrintf("/vsimem/wfs3_%p.json", this));
            oDoc.Save(osTmpFilename);
            m_poUnderlyingDS = std::unique_ptr<GDALDataset>(
            reinterpret_cast<GDALDataset*>(
                GDALOpenEx(osTmpFilename, GDAL_OF_VECTOR | GDAL_OF_INTERNAL,
                        nullptr, nullptr, nullptr)));
            VSIUnlink(osTmpFilename);
            if( !m_poUnderlyingDS.get() )
            {
                return nullptr;
            }
            m_poUnderlyingLayer = m_poUnderlyingDS->GetLayer(0);
            if( !m_poUnderlyingLayer )
            {
                m_poUnderlyingDS.reset();
                return nullptr;
            }

            CPLJSONArray oLinks = oDoc.GetRoot().GetArray("links");
            if( oLinks.IsValid() )
            {
                for( int i = 0; i < oLinks.Size(); i++ )
                {
                    CPLJSONObject oLink = oLinks[i];
                    if( !oLink.IsValid() ||
                        oLink.GetType() != CPLJSONObject::Type::Object )
                    {
                        continue;
                    }
                    if( oLink.GetString("rel") == "next" &&
                        oLink.GetString("type") == "application/geo+json" )
                    {
                        m_osGetURL = oLink.GetString("href");
                        break;
                    }
                }
            }

            if( m_osGetURL.empty() )
            {
                for( int i = 0; i < aosHeaders.size(); i++ )
                {
                    CPLDebug("WFS3", "%s", aosHeaders[i]);
                    if( STARTS_WITH_CI(aosHeaders[i], "Link=") &&
                        strstr(aosHeaders[i], "rel=\"next\"") &&
                        strstr(aosHeaders[i], "type=\"application/geo+json\"") )
                    {
                        const char* pszStart = strchr(aosHeaders[i], '<');
                        if( pszStart )
                        {
                            const char* pszEnd = strchr(pszStart + 1, '>');
                            if( pszEnd )
                            {
                                m_osGetURL = pszStart + 1;
                                m_osGetURL.resize(pszEnd - pszStart - 1);
                            }
                        }
                        break;
                    }
                }
            }

#ifndef REMOVE_HACK
            if( m_osGetURL.empty() &&
                m_poUnderlyingLayer->GetFeatureCount() > 0 )
            {
                m_osGetURL = m_poDS->m_osRootURL + "/" + GetDescription();
                if( m_poDS->m_nPageSize > 0 )
                {
                    m_osGetURL = CPLURLAddKVP(m_osGetURL, "count",
                                        CPLSPrintf("%d", m_poDS->m_nPageSize));
                }
                m_osGetURL = CPLURLAddKVP(m_osGetURL, "startIndex",
                    CPLSPrintf(CPL_FRMT_GIB,
                        m_nFID + m_poUnderlyingLayer->GetFeatureCount() - 1));
                m_osGetURL = AddFilters(m_osGetURL);
            }
#endif
        }

        poSrcFeature = m_poUnderlyingLayer->GetNextFeature();
        if( poSrcFeature )
            break;
        m_poUnderlyingDS.reset();
        m_poUnderlyingLayer = nullptr;
    }

    OGRFeature* poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFrom(poSrcFeature);
    poFeature->SetFID(m_nFID);
    m_nFID ++;
    return poFeature;
}

/************************************************************************/
/*                         GetNextFeature()                             */
/************************************************************************/

OGRFeature* OGRWFS3Layer::GetNextFeature()
{
    while( true )
    {
        OGRFeature  *poFeature = GetNextRawFeature();
        if (poFeature == nullptr)
            return nullptr;

        if( (m_poFilterGeom == nullptr ||
                FilterGeometry(poFeature->GetGeometryRef())) &&
            (m_poAttrQuery == nullptr ||
                m_poAttrQuery->Evaluate(poFeature)) )
        {
            return poFeature;
        }
        else
        {
            delete poFeature;
        }
    }
}

/************************************************************************/
/*                      SupportsResultTypeHits()                        */
/************************************************************************/

bool OGRWFS3Layer::SupportsResultTypeHits()
{
    CPLJSONDocument oDoc = m_poDS->GetAPIDoc();
    if( oDoc.GetRoot().GetString("openapi").empty() )
        return false;

    CPLJSONArray oParameters = oDoc.GetRoot().GetObj("paths")
                                  .GetObj(CPLString("/") + GetDescription())
                                  .GetObj("get")
                                  .GetArray("parameters");
    if( !oParameters.IsValid() )
        return false;
    for( int i = 0; i < oParameters.Size(); i++ )
    {
        CPLJSONObject oParam = oParameters[i];
        CPLString osRef = oParam.GetString("$ref");
        if( !osRef.empty() && osRef.find("#/") == 0 )
        {
            oParam = oDoc.GetRoot().GetObj(osRef.substr(2));
        }
        if( oParam.GetString("name") == "resultType" &&
            oParam.GetString("in") == "query" )
        {
            CPLJSONArray oEnum = oParam.GetArray("schema/enum");
            for( int j = 0; j < oEnum.Size(); j++ )
            {
                if( oEnum[j].ToString() == "hits" )
                    return true;
            }
            return false;
        }
    }

    return false;
}

/************************************************************************/
/*                         GetFeatureCount()                            */
/************************************************************************/

GIntBig OGRWFS3Layer::GetFeatureCount(int bForce)
{
    if( SupportsResultTypeHits() && !m_bFilterMustBeClientSideEvaluated )
    {
        CPLString osURL(m_poDS->m_osRootURL + "/" + GetDescription());
        osURL = CPLURLAddKVP(osURL, "resultType", "hits");
        osURL = AddFilters(osURL);
        CPLJSONDocument oDoc;
        if( m_poDS->DownloadJSon(osURL, oDoc) )
        {
            GIntBig nFeatures = oDoc.GetRoot().GetLong("numberMatched", -1);
            if( nFeatures >= 0 )
                return nFeatures;
        }
    }

    return OGRLayer::GetFeatureCount(bForce);
}

/************************************************************************/
/*                             GetExtent()                              */
/************************************************************************/

OGRErr OGRWFS3Layer::GetExtent(OGREnvelope* psEnvelope, int bForce)
{
    if( m_oExtent.IsInit() )
    {
        *psEnvelope = m_oExtent;
        return OGRERR_NONE;
    }
    return OGRLayer::GetExtent(psEnvelope, bForce);
}

/************************************************************************/
/*                          SetSpatialFilter()                          */
/************************************************************************/

void OGRWFS3Layer::SetSpatialFilter(  OGRGeometry *poGeomIn )
{
    InstallFilter( poGeomIn );

    ResetReading();
}

/************************************************************************/
/*                          SetSpatialFilter()                          */
/************************************************************************/

CPLString OGRWFS3Layer::BuildFilter(swq_expr_node* poNode)
{
    if( poNode->eNodeType == SNT_OPERATION &&
        poNode->nOperation == SWQ_AND && poNode->nSubExprCount == 2 )
    {
         // For AND, we can deal with a failure in one of the branch
        // since client-side will do that extra filtering
        CPLString osFilter1 = BuildFilter(poNode->papoSubExpr[0]);
        CPLString osFilter2 = BuildFilter(poNode->papoSubExpr[1]);
        if( !osFilter1.empty() && !osFilter2.empty() )
        {
            return osFilter1 + "&" + osFilter2;
        }
        else if( !osFilter1.empty() )
            return osFilter1;
        else
            return osFilter2;
    }
    else if (poNode->eNodeType == SNT_OPERATION &&
             poNode->nOperation == SWQ_EQ &&
             poNode->nSubExprCount == 2 &&
             poNode->papoSubExpr[0]->eNodeType == SNT_COLUMN &&
             poNode->papoSubExpr[1]->eNodeType == SNT_CONSTANT )
    {
        const int nFieldIdx = poNode->papoSubExpr[0]->field_index;
        OGRFieldDefn* poFieldDefn = GetLayerDefn()->GetFieldDefn(nFieldIdx);
        if( poFieldDefn &&
            m_aoSetQueriableAttributes.find(poFieldDefn->GetNameRef()) !=
                m_aoSetQueriableAttributes.end() )
        {
            if( poNode->papoSubExpr[1]->field_type == SWQ_STRING )
            {
                char* pszEscapedValue = CPLEscapeString(
                    poNode->papoSubExpr[1]->string_value, -1, CPLES_URL);
                CPLString osRet(poFieldDefn->GetNameRef());
                osRet += "=";
                osRet += pszEscapedValue;
                CPLFree(pszEscapedValue);
                return osRet;
            }
            if( poNode->papoSubExpr[1]->field_type == SWQ_INTEGER )
            {
                CPLString osRet(poFieldDefn->GetNameRef());
                osRet += "=";
                osRet += CPLSPrintf(CPL_FRMT_GIB,
                                    poNode->papoSubExpr[1]->int_value);
                return osRet;
            }
        }
    }
    m_bFilterMustBeClientSideEvaluated = true;
    return CPLString();
}

/************************************************************************/
/*                        GetQueriableAttributes()                      */
/************************************************************************/

void OGRWFS3Layer::GetQueriableAttributes()
{
    if( m_bGotQueriableAttributes )
        return;
    m_bGotQueriableAttributes = true;
    CPLJSONDocument oDoc = m_poDS->GetAPIDoc();
    if( oDoc.GetRoot().GetString("openapi").empty() )
        return;

    CPLJSONArray oParameters = oDoc.GetRoot().GetObj("paths")
                                  .GetObj(CPLString("/") + GetDescription())
                                  .GetObj("get")
                                  .GetArray("parameters");
    if( !oParameters.IsValid() )
        return;
    for( int i = 0; i < oParameters.Size(); i++ )
    {
        CPLJSONObject oParam = oParameters[i];
        CPLString osRef = oParam.GetString("$ref");
        if( !osRef.empty() && osRef.find("#/") == 0 )
        {
            oParam = oDoc.GetRoot().GetObj(osRef.substr(2));
        }
        if( oParam.GetString("in") == "query" &&
            GetLayerDefn()->GetFieldIndex(
                                oParam.GetString("name").c_str()) >= 0 )
        {
            m_aoSetQueriableAttributes.insert(oParam.GetString("name"));
        }
    }
}

/************************************************************************/
/*                         SetAttributeFilter()                         */
/************************************************************************/

OGRErr OGRWFS3Layer::SetAttributeFilter( const char *pszQuery )

{
    OGRErr eErr = OGRLayer::SetAttributeFilter(pszQuery);

    m_osAttributeFilter.clear();
    m_bFilterMustBeClientSideEvaluated = false;
    if( m_poAttrQuery != nullptr )
    {
        GetQueriableAttributes();

        swq_expr_node* poNode = (swq_expr_node*) m_poAttrQuery->GetSWQExpr();

        poNode->ReplaceBetweenByGEAndLERecurse();

        m_osAttributeFilter = BuildFilter(poNode);
        if( m_osAttributeFilter.empty() )
        {
            CPLDebug("WFS3",
                        "Full filter will be evaluated on client side.");
        }
        else if( m_bFilterMustBeClientSideEvaluated )
        {
            CPLDebug("WFS3",
                "Only part of the filter will be evaluated on server side.");
        }
    }

    ResetReading();

    return eErr;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

static GDALDataset *OGRWFS3DriverOpen( GDALOpenInfo* poOpenInfo )

{
    if( !OGRWFS3DriverIdentify(poOpenInfo) || poOpenInfo->eAccess == GA_Update )
        return nullptr;
    std::unique_ptr<OGRWFS3Dataset> poDataset(new OGRWFS3Dataset());
    if( !poDataset->Open(poOpenInfo) )
        return nullptr;
    return poDataset.release();
}

/************************************************************************/
/*                           RegisterOGRWFS()                           */
/************************************************************************/

void RegisterOGRWFS3()

{
    if( GDALGetDriverByName( "WFS3" ) != nullptr )
        return;

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription( "WFS3" );
    poDriver->SetMetadataItem( GDAL_DCAP_VECTOR, "YES" );
    poDriver->SetMetadataItem( GDAL_DMD_LONGNAME,
                               "OGC WFS 3 client (Web Feature Service)" );
    poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, "drv_wfs3.html" );

    poDriver->SetMetadataItem( GDAL_DMD_CONNECTION_PREFIX, "WFS3:" );

    poDriver->SetMetadataItem( GDAL_DMD_OPENOPTIONLIST,
"<OpenOptionList>"
"  <Option name='URL' type='string' "
        "description='URL to the WFS server endpoint' required='true'/>"
"  <Option name='PAGE_SIZE' type='int' "
        "description='Maximum number of features to retrieve in a single request'/>"
"</OpenOptionList>" );

    poDriver->pfnIdentify = OGRWFS3DriverIdentify;
    poDriver->pfnOpen = OGRWFS3DriverOpen;

    GetGDALDriverManager()->RegisterDriver( poDriver );
}
