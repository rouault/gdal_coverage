-- SQL RASTERLITE
CREATE TABLE spatial_ref_sys (srid INTEGER PRIMARY KEY,auth_name,auth_srid,ref_sys_name,proj4text,srtext);
INSERT INTO spatial_ref_sys VALUES(26711,'epsg',26711,'NAD27 / UTM zone 11N','+proj=utm +zone=11 +datum=NAD27 +units=m +no_defs','PROJCS["NAD27 / UTM zone 11N",GEOGCS[NAD27,DATUM[North_American_Datum_1927,SPHEROID["Clarke 1866",6378206.4,294.9786982138982]],UNIT[degree,0.0174532925199433]],PROJECTION[Transverse_Mercator],PARAMETER[central_meridian,-117],PARAMETER[scale_factor,0.9996],PARAMETER[false_easting,500000],AUTHORITY[EPSG,26711]]');
CREATE TABLE geometry_columns (f_table_name,f_geometry_column,geometry_type,coord_dimension,srid,spatial_index_enabled);
INSERT INTO geometry_columns VALUES('byte_metadata','geometry',3,2,26711,1);
CREATE TABLE byte_rasters (id INTEGER PRIMARY KEY,raster);
INSERT INTO byte_rasters VALUES(1,X'49492A00080000000B000001030001000000140000000101030001000000140000000201030001000000080000000301030001000000010000000601030001000000010000001101040001000000920000001501030001000000010000001601030001000000140000001701040001000000900100001C0103000100000001000000530103000100000001000000000000006B7B847384848C8484846B846B84846B7B739C9473846B7B9473A5738C6B7B7B63847B848484639C73848C847B738C6B8C73847B6B848473736B736B94847B7B7384847B737B737B6B73946B738C7384849C848C84847373737B947BA57B846B6B849C7BBDADAD949473947B6B8473849C637B738484CE6BC5AD948C8C8463847B738C848463847B84AD7B73947B9473947B8C7B6B7384736B73637B63B5636B7B7384737B847384847B7B846373637B8473736B8C8C638C63737B6B846B736B737B847B6B7B84848484847B63847B6B9463737B8CAD7B6B7B7B7B6B7B7B7B6B8C7B7B73735A6BAD6B6B6B6B63847B73AD94637B7B6B7B636BBDAD6B73736B638C6BAD8C9484846B7B6363736384638C73947B63847B948C8C6B8C5A6B736B5A637B7373737B7B9473946384A5949C7B6B6B6B738C637363636B7384735A7B73BDAD8C8CA573845A63735A63636B6384636B84849CB58CAD7B8463737B4A73637B8C9C84A58C8C63ADF7FFCE846B8C7B9484A5A5948C847B6B7B6B7BB5B59C949C9C9CB5849473846B6B6B6B6B73636B');
CREATE TABLE byte_metadata (id INTEGER PRIMARY KEY,tile_id,width,height,pixel_x_size,pixel_y_size,geometry POLYGON);
INSERT INTO byte_metadata VALUES(1,0,20,20,60.0,60.0,X'0001576800000000000040E61A4100000000749C4C410000000000F91A4100000000CC9E4C417C0300000001000000050000000000000040E61A4100000000749C4C410000000040E61A4100000000CC9E4C410000000000F91A4100000000CC9E4C410000000000F91A4100000000749C4C410000000040E61A4100000000749C4C41FE');
CREATE VIRTUAL TABLE idx_byte_metadata_geometry USING rtree(pkid, xmin, xmax, ymin, ymax);
INSERT INTO idx_byte_metadata_geometry SELECT rowid, ST_MinX(geometry), ST_MaxX(geometry), ST_MinY(geometry), ST_MaxY(geometry) FROM byte_metadata;
