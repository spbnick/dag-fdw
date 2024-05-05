/**
 * Server configuration management.
 */

#include "dag_fdw_server.h"
#include "dag_fdw_opt.h"
#include "foreign/foreign.h"

void
dag_fdw_server_opts_parse(struct dag_fdw_server *pserver, const List *opts)
{
    struct dag_fdw_server server = {0,};
    struct dag_fdw_opt_def opt_defs[] = {
        {.name       = "node_id_len",
         .required   = true,
         .parse      = dag_fdw_opt_pos_int_parse,
         .pvalue     = &server.node_id_len},
        {.name = NULL}
    };
    dag_fdw_opt_defs_parse(opt_defs, opts);
    if (pserver != NULL) {
        *pserver = server;
    }
}

struct dag_fdw_server *
dag_fdw_server_get(Oid id)
{
    struct dag_fdw_server *server = palloc0(sizeof(*server));
    dag_fdw_server_opts_parse(server, GetForeignServer(id)->options);
    return server;
}
