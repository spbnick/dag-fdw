MODULE_big = dag_fdw
OBJS = dag_fdw_rels.o dag_fdw_opt.o dag_fdw.o

EXTENSION = dag_fdw
DATA = dag_fdw--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
