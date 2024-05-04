/**
 * Defined relations
 */

#include "postgres.h"
#include "catalog/pg_type.h"
#include "dag_fdw_rels.h"

/** The relations supported by the FDW (terminated by .name == NULL) */
const struct dag_fdw_rel dag_fdw_rels[] = {
    {
     .name = "edges",
     .atttypids = {VARCHAROID, VARCHAROID, InvalidOid},
    },
    {.name = NULL}
};
