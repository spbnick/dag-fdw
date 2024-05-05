/**
 * Table configuration management.
 */

#include "dag_fdw_table.h"
#include "dag_fdw_server.h"
#include "dag_fdw_rel.h"
#include "dag_fdw_opt.h"
#include "foreign/foreign.h"
#include "utils/rel.h"
#include "catalog/pg_type.h"
#include "access/table.h"

void
dag_fdw_table_opts_parse(struct dag_fdw_table *ptable, const List *opts)
{
    struct dag_fdw_table table;
    struct dag_fdw_opt_def opt_defs[] = {
        {.name       = "relation",
         .required   = true,
         .parse      = dag_fdw_opt_rel_name_parse,
         .pvalue     = &table.rel},
        {.name = NULL}
    };
    if (ptable != NULL) {
        table = *ptable;
    }
    dag_fdw_opt_defs_parse(opt_defs, opts);
    if (ptable != NULL) {
        *ptable = table;
    }
}

void
dag_fdw_table_validate(const struct dag_fdw_table *table, Oid id)
{
    Relation                rel;
    TupleDesc               tuple_desc;
    const Oid              *poid;
    size_t                  i;
    Form_pg_attribute       attr;

    Assert(table != NULL);
    Assert(id != InvalidOid);

    rel = table_open(id, AccessShareLock);
    tuple_desc = RelationGetDescr(rel);

    /* For each attribute type / tuple's attribute definition pair */
    for (poid = table->rel->atttypids, i = 0;
         *poid != InvalidOid && i < tuple_desc->natts;
         poid++, i++) {
        attr = TupleDescAttr(tuple_desc, i);
        if (attr->atttypid != *poid) {
            ereport(
                ERROR,
                errcode(ERRCODE_FDW_ERROR),
                errmsg("relation \"%s\" (%s): "
                       "invalid type of column #%zu \"%s\": %u, expecting %u",
                       RelationGetRelationName(rel),
                       table->rel->name, i, NameStr(attr->attname),
                       attr->atttypid, *poid)
            );
        }
        if (*poid == VARCHAROID &&
            attr->atttypmod !=
                (int32)table->server->node_id_len * 2 + VARHDRSZ) {
            ereport(
                ERROR,
                errcode(ERRCODE_FDW_ERROR),
                errmsg("relation \"%s\" (%s): "
                       "The VARCHAR column #%zu \"%s\" length "
                       "doesn't match the length of node ID representation",
                       RelationGetRelationName(rel),
                       table->rel->name, i, NameStr(attr->attname))
            );
        }
    }

    if (*poid != InvalidOid || i != tuple_desc->natts) {
        ereport(
            ERROR,
            errcode(ERRCODE_FDW_ERROR),
            errmsg("relation \"%s\" (%s): invalid number of columns",
                   RelationGetRelationName(rel),
                   table->rel->name)
        );
    }

    table_close(rel, AccessShareLock);
}

struct dag_fdw_table *
dag_fdw_table_get(Oid id)
{
    struct dag_fdw_table   *table = palloc0(sizeof(*table));
    ForeignTable           *ft = GetForeignTable(id);
    table->server = dag_fdw_server_get(ft->serverid);
    dag_fdw_table_opts_parse(table, ft->options);
    dag_fdw_table_validate(table, id);
    return table;
}
