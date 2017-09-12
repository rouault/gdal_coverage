#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test PDS4 format
# Author:   Even Rouault, <even.rouault at spatialys.com>
#
###############################################################################
# Copyright (c) 2017, Hobu Inc
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
###############################################################################

import contextlib
import struct
import sys

sys.path.append( '../pymod' )

from osgeo import gdal
from osgeo import ogr
from osgeo import osr
import gdaltest

###############################################################################
# Validate XML file against schemas

def validate_xml(filename):

    if ogr.GetDriverByName('GMLAS') is None:
        return 'skip'

    if not gdaltest.download_file('https://pds.nasa.gov/pds4/pds/v1/PDS4_PDS_1800.xsd',
                                  'pds.nasa.gov_pds4_pds_v1_PDS4_PDS_1800.xsd',
                                  force_download = True):
        return 'skip'

    if not gdaltest.download_file('https://pds.nasa.gov/pds4/pds/v1/PDS4_PDS_1700.xsd',
                                  'pds.nasa.gov_pds4_pds_v1_PDS4_PDS_1700.xsd',
                                  force_download = True):
        return 'skip'

    if not gdaltest.download_file('https://pds.nasa.gov/pds4/cart/v1/PDS4_CART_1700.xsd',
                                  'pds.nasa.gov_pds4_cart_v1_PDS4_CART_1700.xsd',
                                  force_download = True):
        return 'skip'

    ds = gdal.OpenEx('GMLAS:' + filename, open_options = [
                'VALIDATE=YES',
                'FAIL_IF_VALIDATION_ERROR=YES',
                'CONFIG_FILE=<Configuration><AllowRemoteSchemaDownload>false</AllowRemoteSchemaDownload><SchemaCache><Directory>tmp/cache</Directory></SchemaCache></Configuration>'])
    if ds is None:
        return 'fail'
    return 'success'

###############################################################################
# Perform simple read test on PDS4 dataset.

def pds4_1():
    srs = """PROJCS["Transverse Mercator Earth",
    GEOGCS["GCS_Earth",
        DATUM["D_North_American_Datum_1927",
            SPHEROID["North_American_Datum_1927",6378206.4,0]],
        PRIMEM["Reference_Meridian",0],
        UNIT["degree",0.0174532925199433]],
    PROJECTION["Transverse_Mercator"],
    PARAMETER["latitude_of_origin",0],
    PARAMETER["central_meridian",-117],
    PARAMETER["scale_factor",0.9996],
    PARAMETER["false_easting",0],
    PARAMETER["false_northing",0]]
"""
    gt = (-59280.0, 60.0, 0.0, 3751320.0, 0.0, -60.0)

    tst = gdaltest.GDALTest( 'PDS4', 'byte_pds4.xml', 1, 4672 )
    return tst.testOpen( check_prj = srs, check_gt = gt )

###############################################################################
# hide_substitution_warnings_error_handler()

def hide_substitution_warnings_error_handler_cbk(type, errno, msg):
    if msg.find('substituted') < 0:
        print(msg)

@contextlib.contextmanager
def hide_substitution_warnings_error_handler():
  handler = gdal.PushErrorHandler(hide_substitution_warnings_error_handler_cbk)
  try:
    yield handler
  finally:
    gdal.PopErrorHandler()

###############################################################################
# Test CreateCopy() with defaults

def pds4_2():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053 )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test CreateCopy() with explicit INTERLEAVE=BSQ

def pds4_3():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053, options = ['INTERLEAVE=BSQ'] )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test CreateCopy() with explicit INTERLEAVE=BIP

def pds4_4():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053, options = ['INTERLEAVE=BIP'] )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test CreateCopy() with explicit INTERLEAVE=BIL

def pds4_5():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053, options = ['INTERLEAVE=BIL'] )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test CreateCopy() with explicit INTERLEAVE=BSQ and IMAGE_FORMAT=GEOTIFF

def pds4_6():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053, options = ['INTERLEAVE=BSQ', 'IMAGE_FORMAT=GEOTIFF'] )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test CreateCopy() with explicit INTERLEAVE=BIP and IMAGE_FORMAT=GEOTIFF

def pds4_7():

    tst = gdaltest.GDALTest( 'PDS4', 'rgbsmall.tif', 2, 21053, options = ['INTERLEAVE=BIP', 'IMAGE_FORMAT=GEOTIFF'] )
    with hide_substitution_warnings_error_handler():
        ret = tst.testCreateCopy( vsimem = 1, strict_in = 1, quiet_error_handler = False )
    return ret

###############################################################################
# Test SRS support

def pds4_8():

    filename = '/vsimem/out.xml'
    for proj4 in [ '+proj=eqc +lat_ts=43.75 +lat_0=10 +lon_0=-112.5 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs',
                   '+proj=lcc +lat_1=10 +lat_0=10 +lon_0=-112.5 +k_0=0.9 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs', # LCC_1SP
                   '+proj=lcc +lat_1=9 +lat_2=11 +lat_0=10 +lon_0=-112.5 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs', # LCC_2SP
                   '+proj=omerc +lat_0=10 +lonc=11 +alpha=12 +k=0.9 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs', # Oblique Mercator Azimuth Center
                   '+proj=omerc +lat_0=10 +lon_1=11 +lat_1=12 +lon_2=13 +lat_2=14 +k=0.9 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs', # Oblique Mercator 2 points
                   '+proj=stere +lat_0=90 +lat_ts=90 +lon_0=10 +k=0.9 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs', # Polar Stereographic
                   '+proj=poly +lat_0=9 +lon_0=10 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs',
                   '+proj=sinu +lon_0=10 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs',
                   '+proj=tmerc +lat_0=11 +lon_0=10 +k=0.9 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs',
                 ]:
        ds = gdal.GetDriverByName('PDS4').Create(filename, 1, 1)
        sr = osr.SpatialReference()
        sr.ImportFromProj4(proj4)
        ds.SetProjection(sr.ExportToWkt())
        ds.SetGeoTransform([0,1,0,0,0,-1])
        with gdaltest.error_handler():
            ds = None

        ret = validate_xml(filename)
        if ret == 'fail':
            gdaltest.post_reason('validation of file for %s failed' % proj4)
            return 'fail'

        ds = gdal.Open(filename)
        wkt = ds.GetProjectionRef()
        sr = osr.SpatialReference()
        sr.SetFromUserInput(wkt)
        got_proj4 = sr.ExportToProj4().strip()
        if got_proj4 != proj4:
            gdaltest.post_reason('got %s, expected %s' % (got_proj4, proj4))
            print('')
            print(got_proj4)
            print(proj4)
            return 'fail'


    # longlat doesn't roundtrip as such
    ds = gdal.GetDriverByName('PDS4').Create(filename, 1, 1)
    sr = osr.SpatialReference()
    sr.ImportFromProj4('+proj=longlat +a=2439400 +b=2439400 +no_defs')
    ds.SetProjection(sr.ExportToWkt())
    ds.SetGeoTransform([2,1,0,49,0,-2])
    with gdaltest.error_handler():
        ds = None
    ds = gdal.Open(filename)
    wkt = ds.GetProjectionRef()
    sr = osr.SpatialReference()
    sr.SetFromUserInput(wkt)
    got_proj4 = sr.ExportToProj4().strip()
    proj4 = '+proj=eqc +lat_ts=0 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +a=2439400 +b=2439400 +units=m +no_defs'
    if got_proj4 != proj4:
        gdaltest.post_reason('got %s, expected %s' % (got_proj4, proj4))
        print('')
        print(got_proj4)
        print(proj4)
        return 'fail'
    got_gt = ds.GetGeoTransform()
    expected_gt = (85151.12354629935, 42575.561773149675, 0.0, 2086202.5268843342, 0.0, -85151.12354629935)
    if max([abs(got_gt[i] - expected_gt[i]) for i in range(6)]) > 1:
        gdaltest.post_reason('fail')
        print('')
        print(got_gt)
        print(expected_gt)
        return 'fail'
    ds = None

    gdal.GetDriverByName('PDS4').Delete(filename)
    return 'success'

###############################################################################
# Test nodata / mask

def pds4_9():

    ds = gdal.Open('data/byte_pds4.xml')
    ndv = ds.GetRasterBand(1).GetNoDataValue()
    if ndv != 74:
        gdaltest.post_reason('fail')
        print(ndv)
        return 'fail'

    cs = ds.GetRasterBand(1).GetMaskBand().Checksum()
    if cs != 4800:
        gdaltest.post_reason('fail')
        print(cs)
        return 'fail'
    ds = None

    filename = '/vsimem/out.xml'
    # Test copy of all specialConstants
    with hide_substitution_warnings_error_handler():
        gdal.Translate(filename, 'data/byte_pds4.xml', format = 'PDS4')

    ret = validate_xml(filename)
    if ret == 'fail':
        gdaltest.post_reason('validation failed')
        return 'fail'

    ds = gdal.Open(filename)
    ndv = ds.GetRasterBand(1).GetNoDataValue()
    if ndv != 74:
        gdaltest.post_reason('fail')
        print(ndv)
        return 'fail'

    cs = ds.GetRasterBand(1).GetMaskBand().Checksum()
    if cs != 4800:
        gdaltest.post_reason('fail')
        print(cs)
        return 'fail'

    ds = None

    filename = '/vsimem/out.xml'
    # Test copy of all specialConstants and overide noData
    with hide_substitution_warnings_error_handler():
        gdal.Translate(filename, 'data/byte_pds4.xml', format = 'PDS4', noData = 75)

    ret = validate_xml(filename)
    if ret == 'fail':
        gdaltest.post_reason('validation failed')
        return 'fail'

    ds = gdal.Open(filename)
    ndv = ds.GetRasterBand(1).GetNoDataValue()
    if ndv != 75:
        gdaltest.post_reason('fail')
        print(ndv)
        return 'fail'

    cs = ds.GetRasterBand(1).GetMaskBand().Checksum()
    if cs != 4833:
        gdaltest.post_reason('fail')
        print(cs)
        return 'fail'

    ds = None

    # Test just setting noData
    for format in [ 'RAW', 'GEOTIFF' ]:
        with hide_substitution_warnings_error_handler():
            gdal.Translate(filename, 'data/byte_pds4.xml', format = 'PDS4',
                        creationOptions = [ 'USE_SRC_LABEL=NO',
                                            'IMAGE_FORMAT='+format ] )

        ret = validate_xml(filename)
        if ret == 'fail':
            gdaltest.post_reason('validation failed')
            return 'fail'

        ds = gdal.Open(filename)
        ndv = ds.GetRasterBand(1).GetNoDataValue()
        if ndv != 74:
            gdaltest.post_reason('fail')
            print(ndv)
            return 'fail'

        ds = None

    gdal.GetDriverByName('PDS4').Delete(filename)

    return 'success'

###############################################################################
# Test scale / offset

def pds4_10():

    filename = '/vsimem/out.xml'
    for format in [ 'RAW', 'GEOTIFF' ]:
        ds = gdal.GetDriverByName('PDS4').Create(filename, 1, 1,
                                                 options = ['IMAGE_FORMAT='+format])
        ds.GetRasterBand(1).SetScale(2)
        ds.GetRasterBand(1).SetOffset(3)
        with hide_substitution_warnings_error_handler():
            ds = None

        ds = gdal.Open(filename)
        scale = ds.GetRasterBand(1).GetScale()
        if scale != 2:
            gdaltest.post_reason('fail')
            print(scale)
            return 'fail'
        offset = ds.GetRasterBand(1).GetOffset()
        if offset != 3:
            gdaltest.post_reason('fail')
            print(offset)
            return 'fail'
        ds = None

        gdal.GetDriverByName('PDS4').Delete(filename)

    return 'success'

###############################################################################
# Test various data types

def pds4_11():

    filename = '/vsimem/out.xml'
    for (dt, data) in [ (gdal.GDT_Byte, struct.pack('B', 255)),
                        (gdal.GDT_UInt16, struct.pack('H', 65535)),
                        (gdal.GDT_Int16, struct.pack('h', -32768)),
                        (gdal.GDT_UInt32, struct.pack('I', 4000000000)),
                        (gdal.GDT_Int32, struct.pack('i', -2000000000)),
                        (gdal.GDT_Float32, struct.pack('f', 1.25)),
                        (gdal.GDT_Float64, struct.pack('d', 1.25)),
                        (gdal.GDT_CFloat32, struct.pack('ff', 1.25, 2.5)),
                        (gdal.GDT_CFloat64, struct.pack('dd', 1.25, 2.5))]:
        ds = gdal.GetDriverByName('PDS4').Create(filename, 1, 1, 1, dt)
        ds.GetRasterBand(1).WriteRaster(0, 0, 1, 1, data)
        with hide_substitution_warnings_error_handler():
            ds = None

        with gdaltest.config_option('PDS4_FORCE_MASK', 'YES'):
            ds = gdal.Open(filename)
        if ds.GetRasterBand(1).DataType != dt:
            gdaltest.post_reason('fail')
            print(ds.GetRasterBand(1).DataType)
            print(dt)
            return 'fail'
        got_data = ds.GetRasterBand(1).ReadRaster(0, 0, 1, 1)
        if got_data != data:
            gdaltest.post_reason('fail')
            print(dt)
            return 'fail'
        cs = ds.GetRasterBand(1).GetMaskBand().Checksum()
        if cs != 3:
            gdaltest.post_reason('fail')
            print(dt)
            print(cs)
            return 'fail'
        ds = None

    gdal.GetDriverByName('PDS4').Delete(filename)

    return 'success'

###############################################################################
# Test various creation options

def pds4_12():

    filename = '/vsimem/out.xml'
    ds = gdal.GetDriverByName('PDS4').Create(filename, 1, 1,
        options = ['VAR_LOGICAL_IDENTIFIER=logical_identifier',
                   'VAR_TITLE=title',
                   'VAR_INVESTIGATION_AREA_NAME=ian',
                   'VAR_INVESTIGATION_AREA_LID_REFERENCE=ialr',
                   'VAR_OBSERVING_SYSTEM_NAME=osn',
                   'VAR_UNUSED=foo',
                   'TEMPLATE=data/byte_pds4.xml',
                   'BOUNDING_DEGREES=1,2,3,4',
                   'LATITUDE_TYPE=planetographic',
                   'LONGITUDE_DIRECTION=Positive West',
                   'IMAGE_FILENAME=/vsimem/myimage.raw'])
    sr = osr.SpatialReference()
    sr.ImportFromProj4('+proj=longlat +a=2439400 +b=2439400 +no_defs')
    ds.SetProjection(sr.ExportToWkt())
    ds.SetGeoTransform([2,1,0,49,0,-2])
    ds = None

    f = gdal.VSIFOpenL(filename, 'rb')
    if f:
        data = gdal.VSIFReadL(1, 10000, f).decode('ascii')
        gdal.VSIFCloseL(f)
    if data.find('<logical_identifier>logical_identifier</logical_identifier>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:west_bounding_coordinate unit="deg">1</cart:west_bounding_coordinate>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:east_bounding_coordinate unit="deg">3</cart:east_bounding_coordinate>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:north_bounding_coordinate unit="deg">4</cart:north_bounding_coordinate>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:south_bounding_coordinate unit="deg">2</cart:south_bounding_coordinate>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:latitude_type>planetographic</cart:latitude_type>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<cart:longitude_direction>Positive West</cart:longitude_direction>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'
    if data.find('<file_name>myimage.raw</file_name>') < 0:
        gdaltest.post_reason('fail')
        print(data)
        return 'fail'

    gdal.GetDriverByName('PDS4').Delete(filename)

    return 'success'

gdaltest_list = [
    pds4_1,
    pds4_2,
    pds4_3,
    pds4_4,
    pds4_5,
    pds4_6,
    pds4_7,
    pds4_8,
    pds4_9,
    pds4_10,
    pds4_11,
    pds4_12 ]

if __name__ == '__main__':

    gdaltest.setup_run( 'pds4' )

    gdaltest.run_tests( gdaltest_list )

    gdaltest.summarize()

