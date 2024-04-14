CREATE FUNCTION dag_fdw_validator(options text[], catalog oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION dag_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER dag_fdw
  HANDLER dag_fdw_handler
  VALIDATOR dag_fdw_validator;
