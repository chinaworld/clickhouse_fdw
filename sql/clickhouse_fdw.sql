/*-------------------------------------------------------------------------
 *
 *                foreign-data wrapper  clickhouse
 *
 * Copyright (c) 2013, PostgreSQL Global Development Group
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author:  Andrew Dunstan <andrew@dunslane.net>
 *
 * IDENTIFICATION
 *                clickhouse_fdw/=sql/clickhouse_fdw.sql
 *
 *-------------------------------------------------------------------------
 */

CREATE FUNCTION clickhouse_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION clickhouse_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER clickhouse_fdw
  HANDLER clickhouse_fdw_handler
  VALIDATOR clickhouse_fdw_validator;

CREATE OR REPLACE FUNCTION retcomposite(IN integer, IN integer,
    OUT f1 integer, OUT f2 integer, OUT f3 integer)
    RETURNS SETOF record
    AS 'MODULE_PATHNAME', 'retcomposite'
    LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION ch_execute(text,text)
    RETURNS SETOF record
    AS 'MODULE_PATHNAME', 'ch_execute'
    LANGUAGE C IMMUTABLE STRICT;