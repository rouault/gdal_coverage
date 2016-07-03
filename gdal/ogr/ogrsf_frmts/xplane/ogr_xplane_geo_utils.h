/******************************************************************************
 * $Id$
 *
 * Project:  X-Plane aeronautical data reader
 * Purpose:  Definition of geo-computation functions
 * Author:   Even Rouault, even dot rouault at mines dash paris dot org
 *
 ******************************************************************************
 * Copyright (c) 2008, Even Rouault <even dot rouault at mines-paris dot org>
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

#ifndef OGR_XPLANE_GEO_UTILS_H_INCLUDED
#define OGR_XPLANE_GEO_UTILS_H_INCLUDED

double OGRXPlane_Distance(double dfLatA_deg, double dfLonA_deg,
                          double dfLatB_deg, double dfLonB_deg);

double OGRXPlane_Track(double dfLatA_deg, double dfLonA_deg,
                       double dfLatB_deg, double dfLonB_deg);

int OGRXPlane_ExtendPosition(double dfLatA_deg, double dfLonA_deg,
                             double dfDistance, double dfHeading,
                             double* pdfLatB_deg, double* pdfLonB_deg);


#endif /* ndef OGR_XPLANE_GEO_UTILS_H_INCLUDED */
