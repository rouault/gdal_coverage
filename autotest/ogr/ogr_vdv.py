#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test read functionality for OGR VDV driver.
# Author:   Even Rouault <even.rouault at spatialys.com>
#
###############################################################################
# Copyright (c) 2015, Even Rouault <even.rouault at spatialys.com>
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

import os
import shutil
import sys

sys.path.append( '../pymod' )

import gdaltest
import ogrtest
from osgeo import gdal
from osgeo import ogr

###############################################################################
# Basic test of .idf file

def ogr_idf_1():

    ds = ogr.Open('data/test.idf')
    lyr = ds.GetLayer(0)
    f = lyr.GetNextFeature()
    if f['NODE_ID'] != 1 or f['foo'] != 'U' or f.GetGeometryRef().ExportToWkt() != 'POINT (2 49)':
        gdaltest.post_reason('fail')
        f.DumpReadable()
        return 'fail'

    lyr = ds.GetLayer(1)
    f = lyr.GetNextFeature()
    if f.GetGeometryRef().ExportToWkt() != 'LINESTRING (2 49,2.5 49.5,2.7 49.7,3 50)':
        gdaltest.post_reason('fail')
        f.DumpReadable()
        return 'fail'

    lyr = ds.GetLayer(2)
    f = lyr.GetNextFeature()
    if f.GetGeometryRef().ExportToWkt() != 'POINT (2.5 49.5)':
        gdaltest.post_reason('fail')
        f.DumpReadable()
        return 'fail'

    lyr = ds.GetLayer(3)
    f = lyr.GetNextFeature()
    if f['FOO'] != 1:
        gdaltest.post_reason('fail')
        f.DumpReadable()
        return 'fail'

    return 'success'

###############################################################################
# Run test_ogrsf on .idf

def ogr_idf_2():

    import test_cli_utilities
    if test_cli_utilities.get_test_ogrsf_path() is None:
        return 'skip'

    ret = gdaltest.runexternal(test_cli_utilities.get_test_ogrsf_path() + ' -ro data/test.idf')

    if ret.find('INFO') == -1 or ret.find('ERROR') != -1:
        print(ret)
        return 'fail'

    return 'success'

###############################################################################
# Create a VDV file

def ogr_vdv_1(filename = 'tmp/test.x10', dsco = [], lco = []):

    ds = ogr.GetDriverByName('VDV').CreateDataSource(filename, options = dsco)
    ds.CreateLayer('empty', options = lco)
    lyr = ds.CreateLayer('lyr_1', options = lco)
    lyr.CreateField(ogr.FieldDefn('str_field', ogr.OFTString))
    lyr.CreateField(ogr.FieldDefn('int_field', ogr.OFTInteger))
    lyr.CreateField(ogr.FieldDefn('int64_field', ogr.OFTInteger64))

    bool_field = ogr.FieldDefn('bool_field', ogr.OFTInteger)
    bool_field.SetSubType(ogr.OFSTBoolean)
    lyr.CreateField(bool_field)

    fld = ogr.FieldDefn('str2_field', ogr.OFTString)
    fld.SetWidth(2)
    lyr.CreateField(fld)

    fld = ogr.FieldDefn('int2_field', ogr.OFTInteger)
    fld.SetWidth(2)
    lyr.CreateField(fld)

    f = ogr.Feature(lyr.GetLayerDefn())
    f.SetField('str_field', 'a"b')
    f.SetField('int_field', 12)
    f.SetField('bool_field', 1)
    lyr.CreateFeature(f)

    f = ogr.Feature(lyr.GetLayerDefn())
    lyr.CreateFeature(f)

    lyr = ds.CreateLayer('another_layer', options = lco)
    lyr.CreateField(ogr.FieldDefn('str_field', ogr.OFTString))
    for i in range(5):
        f = ogr.Feature(lyr.GetLayerDefn())
        f.SetField('str_field', i)
        lyr.CreateFeature(f)

    ds = None

    # Do nothing
    ds = ogr.Open(filename, update=1)
    ds = None

    ds = ogr.Open(filename, update=1)
    ds.CreateLayer('empty2', options = lco)
    ds = None

    return 'success'

###############################################################################
# Read it

def ogr_vdv_2(src_filename = 'tmp/test.x10'):

    out_filename = '/vsimem/vdv/ogr_vdv_2.x10'
    gdal.Unlink(out_filename)

    src_ds = ogr.Open(src_filename)
    out_ds = ogr.GetDriverByName('VDV').CreateDataSource(out_filename)
    layer_names = [ src_ds.GetLayer(idx).GetName() for idx in range(src_ds.GetLayerCount()) ]
    layer_names.sort()
    for layer_name in layer_names:
        src_lyr = src_ds.GetLayer(layer_name)
        options = [ 'HEADER_SRC_DATE=01.01.1970', 'HEADER_SRC_TIME=00.00.00', 'HEADER_foo=bar' ]
        dst_lyr = out_ds.CreateLayer(src_lyr.GetName(), options = options)
        for field_idx in range(src_lyr.GetLayerDefn().GetFieldCount()):
            dst_lyr.CreateField(src_lyr.GetLayerDefn().GetFieldDefn(field_idx))
        for src_f in src_lyr:
            dst_f = ogr.Feature(dst_lyr.GetLayerDefn())
            dst_f.SetFrom(src_f)
            dst_lyr.CreateFeature(dst_f)
    out_ds = None

    expected = """mod; DD.MM.YYYY; HH:MM:SS; free
src; "UNKNOWN"; "01.01.1970"; "00.00.00"
chs; "ISO8859-1"
ver; "1.4"
ifv; "1.4"
dve; "1.4"
fft; ""
foo; "bar"
tbl; another_layer
atr; str_field
frm; char[80]
rec; "0"
rec; "1"
rec; "2"
rec; "3"
rec; "4"
end; 5
tbl; lyr_1
atr; str_field; int_field; int64_field; bool_field; str2_field; int2_field
frm; char[80]; num[10.0]; num[19.0]; boolean; char[2]; num[1.0]
rec; "a""b"; 12; NULL; 1; NULL; NULL
rec; NULL; NULL; NULL; NULL; NULL; NULL
end; 2
tbl; empty
atr;
frm;
end; 0
tbl; empty2
atr;
frm;
end; 0
eof; 4
"""

    f = gdal.VSIFOpenL(out_filename, 'rb')
    got = gdal.VSIFReadL(1, 10000, f).decode('latin1')
    gdal.VSIFCloseL(f)

    if got != expected:
        gdaltest.post_reason('fail')
        print(got)
        return 'fail'

    gdal.Unlink(out_filename)

    return 'success'

###############################################################################
# Run test_ogrsf on it

def ogr_vdv_3():

    import test_cli_utilities
    if test_cli_utilities.get_test_ogrsf_path() is None:
        return 'skip'

    ret = gdaltest.runexternal(test_cli_utilities.get_test_ogrsf_path() + ' -ro tmp/test.x10')

    if ret.find('INFO') == -1 or ret.find('ERROR') != -1:
        print(ret)
        return 'fail'

    return 'success'

###############################################################################
# Create a VDV directory

def ogr_vdv_4():
    return ogr_vdv_1(filename = 'tmp/test_x10', dsco = ['SINGLE_FILE=NO'], lco = ['EXTENSION=txt'])

###############################################################################
# Read it

def ogr_vdv_5():
    return ogr_vdv_2(src_filename = 'tmp/test_x10')

###############################################################################
# Run test_ogrsf on it

def ogr_vdv_6():

    import test_cli_utilities
    if test_cli_utilities.get_test_ogrsf_path() is None:
        return 'skip'

    ret = gdaltest.runexternal(test_cli_utilities.get_test_ogrsf_path() + ' -ro tmp/test_x10')

    if ret.find('INFO') == -1 or ret.find('ERROR') != -1:
        print(ret)
        return 'fail'

    return 'success'

###############################################################################
# Run VDV452

def ogr_vdv_7():

    tests = [ ('VDV-452','STOP','POINT_LONGITUDE','POINT_LATITUDE'),
              ('VDV-452-ENGLISH','STOP','POINT_LONGITUDE','POINT_LATITUDE'),
              ('VDV-452','REC_ORT','ORT_POS_LAENGE','ORT_POS_BREITE'),
              ('VDV-452-GERMAN','REC_ORT','ORT_POS_LAENGE','ORT_POS_BREITE') ]

    out_filename = '/vsimem/vdv/ogr_vdv_7.x10'

    for (profile,lyrname,longname,latname) in tests:

        ds = ogr.GetDriverByName('VDV').CreateDataSource(out_filename)
        lyr = ds.CreateLayer(lyrname, geom_type = ogr.wkbPoint, options = ['PROFILE=' + profile])
        f = ogr.Feature(lyr.GetLayerDefn())
        lng = - (123 + 45. / 60 + 56.789 / 3600)
        lat = - ( 23 + 45. / 60 + 56.789 / 3600)
        f.SetGeometry(ogr.CreateGeometryFromWkt('POINT(%.10f %.10f)' % (lng, lat)))
        lyr.CreateFeature(f)
        ds = None

        ds = ogr.Open(out_filename)
        lyr = ds.GetLayer(0)
        f = lyr.GetNextFeature()
        if f[longname] != -1234556789 or \
           f[latname] != -234556789 or \
           ogrtest.check_feature_geometry(f, 'POINT (-123.765774722222 -23.7657747222222)') != 0:
            gdaltest.post_reason('fail')
            f.DumpReadable()
            return 'fail'
        ds = None

        gdal.Unlink('/vsimem/vdv/ogr_vdv_7.x10')


    tests = [ ('VDV-452', True), ('VDV-452-ENGLISH', False), ('VDV-452-GERMAN', False) ]

    for (profile, strict) in tests:

        ds = ogr.GetDriverByName('VDV').CreateDataSource(out_filename)
        gdal.ErrorReset()
        gdal.PushErrorHandler()
        lyr = ds.CreateLayer('UNKNOWN', options = ['PROFILE=' + profile, 'PROFILE_STRICT=' + str(strict)])
        gdal.PopErrorHandler()
        if gdal.GetLastErrorMsg() == '':
            gdaltest.post_reason('fail')
            return 'fail'
        if strict and lyr is not None:
            gdaltest.post_reason('fail')
            return 'fail'
        elif not strict and lyr is None:
            gdaltest.post_reason('fail')
            return 'fail'

        if profile == 'VDV-452-GERMAN':
            lyr_name = 'REC_ORT'
        else:
            lyr_name = 'STOP'
        lyr = ds.CreateLayer(lyr_name, options = ['PROFILE=' + profile, 'PROFILE_STRICT=' + str(strict)])
        gdal.ErrorReset()
        gdal.PushErrorHandler()
        ret = lyr.CreateField(ogr.FieldDefn('UNKNOWN'))
        gdal.PopErrorHandler()
        if gdal.GetLastErrorMsg() == '':
            gdaltest.post_reason('fail')
            return 'fail'
        if strict and ret == 0:
            gdaltest.post_reason('fail')
            return 'fail'
        elif not strict and ret != 0:
            gdaltest.post_reason('fail')
            return 'fail'

        ds = None

        gdal.Unlink('/vsimem/vdv/ogr_vdv_7.x10')

    return 'success'

###############################################################################
# Test a few error cases

def ogr_vdv_8():

    gdal.PushErrorHandler()
    ds = ogr.GetDriverByName('VDV').CreateDataSource('/does/not_exist')
    gdal.PopErrorHandler()
    if ds is not None:
        gdaltest.post_reason('fail')
        return 'fail'

    gdal.PushErrorHandler()
    ds = ogr.GetDriverByName('VDV').CreateDataSource('/does/not_exist', options = ['SINGLE_FILE=FALSE'])
    gdal.PopErrorHandler()
    if ds is not None:
        gdaltest.post_reason('fail')
        return 'fail'

    # Add layer in non writable directory
    if sys.platform.startswith('linux'):
        os.mkdir('tmp/ogr_vdv_8')
        open('tmp/ogr_vdv_8/empty.x10', 'wb').write('tbl; foo\natr;\nfrm;\n'.encode('latin1'))
        # 0555 = 365
        os.chmod('tmp/ogr_vdv_8', 365)
        try:
            open('tmp/ogr_vdv_8/another_file','wb').close()
            shutil.rmtree('tmp/ogr_vdv_8')
            do_test = False
        except:
            do_test = True
        if do_test:
            ds = ogr.Open('tmp/ogr_vdv_8', update = 1)
            gdal.PushErrorHandler()
            lyr = ds.CreateLayer('another_layer')
            gdal.PopErrorHandler()
            # 0755 = 493
            os.chmod('tmp/ogr_vdv_8', 493)
            ds = None
            shutil.rmtree('tmp/ogr_vdv_8')
            if lyr is not None:
                gdaltest.post_reason('fail')
                return 'fail'

    out_filename = '/vsimem/vdv/ogr_vdv_8.x10'
    ds = ogr.GetDriverByName('VDV').CreateDataSource(out_filename)

    # File already exists
    gdal.PushErrorHandler()
    ds2 = ogr.GetDriverByName('VDV').CreateDataSource(out_filename)
    gdal.PopErrorHandler()
    if ds2 is not None:
        gdaltest.post_reason('fail')
        return 'fail'

    if ds.TestCapability(ogr.ODsCCreateLayer) != 1:
        gdaltest.post_reason('fail')
        return 'fail'

    lyr1 = ds.CreateLayer("lyr1")
    if lyr1.TestCapability(ogr.OLCSequentialWrite) != 1:
        gdaltest.post_reason('fail')
        return 'fail'
    if lyr1.TestCapability(ogr.OLCCreateField) != 1:
        gdaltest.post_reason('fail')
        return 'fail'

    lyr1.ResetReading()

    gdal.PushErrorHandler()
    lyr1.GetNextFeature()
    gdal.PopErrorHandler()

    lyr1.CreateFeature(ogr.Feature(lyr1.GetLayerDefn()))

    # Layer structure is now frozen
    if lyr1.TestCapability(ogr.OLCCreateField) != 0:
        gdaltest.post_reason('fail')
        return 'fail'

    gdal.PushErrorHandler()
    ret = lyr1.CreateField(ogr.FieldDefn('not_allowed'))
    gdal.PopErrorHandler()
    if ret == 0:
        gdaltest.post_reason('fail')
        return 'fail'

    lyr2 = ds.CreateLayer("lyr2")
    lyr2.CreateFeature(ogr.Feature(lyr2.GetLayerDefn()))

    # Test interleaved writing

    if lyr1.TestCapability(ogr.OLCSequentialWrite) != 0:
        gdaltest.post_reason('fail')
        return 'fail'

    gdal.PushErrorHandler()
    ret = lyr1.CreateFeature(ogr.Feature(lyr1.GetLayerDefn()))
    gdal.PopErrorHandler()
    if ret == 0:
        gdaltest.post_reason('fail')
        return 'fail'

    if lyr1.GetFeatureCount() != 1:
        gdaltest.post_reason('fail')
        return 'fail'

    ds = None

    # Test appending new layer to file without eof
    gdal.FileFromMemBuffer(out_filename, 'tbl; foo\natr; atr\nfrm; char[40]\nrec; "foo"\n')
    ds = ogr.Open(out_filename, update=1)
    lyr = ds.CreateLayer('new_layer')
    lyr.CreateField(ogr.FieldDefn('atr'))
    f = ogr.Feature(lyr.GetLayerDefn())
    f['atr'] = 'bar'
    lyr.CreateFeature(f)
    f = None
    ds = None

    expected = """tbl; foo
atr; atr
frm; char[40]
rec; "foo"
tbl; new_layer
atr; atr
frm; char[80]
rec; "bar"
end; 1
eof; 2
"""

    f = gdal.VSIFOpenL(out_filename, 'rb')
    got = gdal.VSIFReadL(1, 10000, f).decode('latin1')
    gdal.VSIFCloseL(f)

    if got != expected:
        gdaltest.post_reason('fail')
        print(got)
        return 'fail'

    # Test we are robust against missing end;
    ds = ogr.Open(out_filename)
    for i in range(2):
        lyr = ds.GetLayer(i)
        if lyr.GetFeatureCount() != 1:
            gdaltest.post_reason('fail')
            return 'fail'
        lyr.ResetReading()
        fc = 0
        for f in lyr:
            fc += 1
        if fc != 1:
            gdaltest.post_reason('fail')
            return 'fail'
        lyr = None
    ds = None

    # Test appending new layer to file without terminating \n
    gdal.FileFromMemBuffer(out_filename, 'tbl; foo\natr; atr\nfrm; char[40]\nrec; "foo"\neof; 1')
    ds = ogr.Open(out_filename, update=1)
    lyr = ds.CreateLayer('new_layer')
    lyr.CreateField(ogr.FieldDefn('atr'))
    f = ogr.Feature(lyr.GetLayerDefn())
    f['atr'] = 'bar'
    lyr.CreateFeature(f)
    f = None
    ds = None

    f = gdal.VSIFOpenL(out_filename, 'rb')
    got = gdal.VSIFReadL(1, 10000, f).decode('latin1')
    gdal.VSIFCloseL(f)

    if got != expected:
        gdaltest.post_reason('fail')
        print(got)
        return 'fail'

    gdal.Unlink(out_filename)

    return 'success'

###############################################################################
# Cleanup

def ogr_vdv_cleanup():

    gdal.Unlink('tmp/test.x10')
    gdal.Unlink('/vsimem/vdv/ogr_vdv_2.x10')
    gdal.Unlink('/vsimem/vdv/ogr_vdv_7.x10')
    gdal.Unlink('/vsimem/vdv/ogr_vdv_8.x10')
    files = gdal.ReadDir('tmp/test_x10')
    if files is not None:
        for f in files:
            gdal.Unlink('tmp/test_x10/' + f)
    gdal.Rmdir('tmp/test_x10')

    return 'success'

gdaltest_list = [
    ogr_idf_1,
    ogr_idf_2,
    ogr_vdv_1,
    ogr_vdv_2,
    ogr_vdv_3,
    ogr_vdv_4,
    ogr_vdv_5,
    ogr_vdv_6,
    ogr_vdv_7,
    ogr_vdv_8,
    ogr_vdv_cleanup ]


if __name__ == '__main__':

    gdaltest.setup_run( 'ogr_vdv' )

    gdaltest.run_tests( gdaltest_list )

    gdaltest.summarize()
