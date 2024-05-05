/**
 * Server configuration management.
 */

#ifndef DAG_FDW_SERVER_H
#define DAG_FDW_SERVER_H

#include "postgres.h"
#include "nodes/pg_list.h"

/** Server configuration */
struct dag_fdw_server {
    /** The length of node IDs in bytes */
    size_t      node_id_len;
};

/**
 * Parse configuration options for a server.
 *
 * @param pserver   The server to store the parsed options in.
 *                  Can be NULL to discard the options after parsing.
 * @param opts      The server options and values to parse.
 *
 * @return The pointer to the server configuration allocated in the current
 *         memory context.
 */
extern void dag_fdw_server_opts_parse(struct dag_fdw_server *pserver,
                                      const List *opts);

/**
 * Get configuration of a server.
 *
 * @param id    The oid of the foreign server to get configuration of.
 *
 * @return The pointer to the server configuration allocated in the current
 *         memory context.
 */
extern struct dag_fdw_server *dag_fdw_server_get(Oid id);

#endif /* DAG_FDW_SERVER_H */
