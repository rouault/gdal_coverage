#!/usr/bin/env python
###############################################################################
# $Id$
#
#  Project:  GDAL samples
#  Purpose:  Delete a virtual file
#  Author:   Even Rouault <even.rouault at spatialys.com>
#
###############################################################################
#  Copyright (c) 2017, Even Rouault <even.rouault at spatialys.com>
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  the rights to use, copy, modify, merge, publish, distribute, sublicense,
#  and/or sell copies of the Software, and to permit persons to whom the
#  Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included
#  in all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
#  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#  DEALINGS IN THE SOFTWARE.
###############################################################################

import sys

from osgeo import gdal

def Usage():
    print('Usage: gdal_rm [-r] filename')
    return -1

def gdal_rm_recurse(filename, simulate = False):
    dir_contents = gdal.ReadDir(filename)
    if dir_contents:
        for f in dir_contents:
            if f not in ('.', '..'):
                ret = gdal_rm_recurse(filename + '/' + f, simulate = simulate)
                if ret != 0:
                    return ret
        if simulate:
            print('Rmdir(%s)' % filename)
            return 0
        else:
            return gdal.Rmdir(filename)
    else:
        if simulate:
            print('Unlink(%s)' % filename)
            return 0
        else:
            return gdal.Unlink(filename)

def gdal_rm(argv, progress = None):
    filename = None
    recurse = False
    simulate = False

    argv = gdal.GeneralCmdLineProcessor( argv )
    if argv is None:
        return -1

    for i in range(1, len(argv)):
        if len(argv[i]) == 0:
            return Usage()

        if argv[i] == '-r':
            recurse = True
        elif argv[i] == '-simulate':
            simulate = True
        elif argv[i][0] == '-':
            print('Unexpected option : %s' % argv[i])
            return Usage()
        elif filename is None:
            filename = argv[i]
        else:
            print('Unexpected option : %s' % argv[i])
            return Usage()

    if filename is None:
        return Usage()

    if filename == '/':
        try:
            user_input_local = ''
            exec("""user_input_local = raw_input('Please confirm with YES your action: ')""")
            user_input = user_input_local
        except:
            user_input = input('Please confirm with YES your action: ')
        if user_input != 'YES':
            print('Aborted')
            return 1

    if recurse:
        ret = gdal_rm_recurse(filename, simulate = simulate)
    else:
        if simulate:
            print('gdal.Unlink(%s)' % filename)
            ret = 0
        else:
            ret = gdal.Unlink(filename)
    if ret != 0:
        print('Deletion failed')
    return ret

if __name__ == '__main__':
    sys.exit(gdal_rm(sys.argv))
