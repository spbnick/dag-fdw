/**
 * Defined relations
 */

#ifndef DAG_FDW_RELS_H
#define DAG_FDW_RELS_H

#include "dag_fdw_rel.h"

/** The relations supported by the FDW (terminated by .name == NULL) */
extern const struct dag_fdw_rel dag_fdw_rels[];

#endif /* DAG_FDW_RELS_H */
