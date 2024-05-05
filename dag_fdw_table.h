/**
 * Table configuration management.
 */

#ifndef DAG_FDW_TABLE_H
#define DAG_FDW_TABLE_H

#include "postgres.h"
#include "nodes/pg_list.h"

/** Table configuration */
struct dag_fdw_table {
    /** The server configuration */
    struct dag_fdw_server *server;
    /** The relation the table is representing */
    const struct dag_fdw_rel *rel;
};

/**
 * Parse configuration options for a table.
 *
 * @param ptable    The table to store the parsed options in.
 *                  Can be NULL to discard the options after parsing.
 * @param opts      The table options and values to parse.
 *
 * @return The pointer to the table configuration allocated in the current
 *         memory context.
 */
extern void dag_fdw_table_opts_parse(struct dag_fdw_table *ptable,
                                     const List *opts);

/**
 * Validate a table against a table definition.
 * Report errors on validation failure.
 *
 * @param table     The table definition to validate the table against.
 * @param id        The Oid of the table to validate against the definition.
 */
extern void dag_fdw_table_validate(const struct dag_fdw_table *table, Oid id);

/**
 * Get configuration of a table.
 *
 * @param id    The oid of the foreign table to get configuration of.
 *
 * @return The pointer to the table configuration allocated in the current
 *         memory context.
 */
extern struct dag_fdw_table *dag_fdw_table_get(Oid id);

#endif /* DAG_FDW_TABLE_H */
