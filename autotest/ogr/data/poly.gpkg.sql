-- SQL GPKG
CREATE TABLE gpkg_spatial_ref_sys (srs_name,srs_id,organization,organization_coordsys_id,definition,description);
INSERT INTO gpkg_spatial_ref_sys VALUES('OSGB 1936 / British National Grid',4327,'NONE',4327,'PROJCS["OSGB 1936 / British National Grid",GEOGCS["OSGB 1936",DATUM["OSGB_1936",SPHEROID["Airy_1830",6377563.396,299.3249646]],PRIMEM["Greenwich",0],UNIT["Degree",0.017453292519943295]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",49],PARAMETER["central_meridian",-2],PARAMETER["scale_factor",0.9996012717],PARAMETER["false_easting",400000],PARAMETER["false_northing",-100000],UNIT["Meter",1]]',NULL);
CREATE TABLE gpkg_contents (table_name,data_type,identifier,description,last_change,min_x,min_y,max_x,max_y,srs_id);
INSERT INTO gpkg_contents VALUES('poly','features','poly','','',478316.0,4762880.0,481645.0,4765610.0,4327);
CREATE TABLE gpkg_ogr_contents(table_name TEXT NOT NULL PRIMARY KEY,feature_count);
INSERT INTO gpkg_ogr_contents VALUES('poly',1);
CREATE TABLE gpkg_geometry_columns (table_name,column_name,geometry_type_name,srs_id,z,m);
INSERT INTO gpkg_geometry_columns VALUES('poly','geom','POLYGON',4327,0,0);
CREATE TABLE poly (fid INTEGER PRIMARY KEY, geom POLYGON);
INSERT INTO poly VALUES(1,X'47500003E7100000000000007C461D41000000C016521D41000000202E2D5241000000A0EA2D524101030000000100000014000000000000602F491D41000000207F2D5241000000C028471D41000000E0922D5241000000007C461D4100000060AE2D524100000080C9471D4100000020B62D5241000000209C4C1D41000000E0D82D5241000000608D4C1D41000000A0DD2D5241000000207F4E1D41000000A0EA2D524100000020294F1D4100000080CA2D524100000000B4511D41000000E0552D5241000000C016521D4100000080452D5241000000E0174E1D41000000202E2D524100000020414D1D41000000E04C2D5241000000E04B4D1D41000000605E2D524100000040634D1D41000000E0742D5241000000A0EF4C1D41000000E08D2D5241000000E04E4C1D41000000E0A12D5241000000E0B04B1D4100000060B82D524100000080974A1D4100000080AE2D524100000080CF491D4100000080952D5241000000602F491D41000000207F2D5241');
CREATE TABLE gpkg_extensions (table_name,column_name,extension_name,definition,scope);
INSERT INTO gpkg_extensions VALUES('','','gpkg_rtree_index','GeoPackage 1.0 Specification Annex L','write-only');
CREATE VIRTUAL TABLE rtree_poly_geom USING rtree(id, minx, maxx, miny, maxy);
INSERT INTO rtree_poly_geom SELECT fid, ST_MinX(geom), ST_MaxX(geom), ST_MinY(geom), ST_MaxY(geom) FROM poly;
