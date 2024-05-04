/**
 * Relation definition.
 */

#ifndef DAG_FDW_REL_H
#define DAG_FDW_REL_H

/** The definition of an FDW's relation */
struct dag_fdw_rel {
    /** The name of the relation */
    const char             *name;
    /**
     * Oids of attribute (column) types, terminated with InvalidOid.
     * Used to validate the number and type of columns. Additionally, all
     * VARCHAR columns are validated to be the size required for node IDs, as
     * configured for the server.
     */
    Oid                     atttypids[8];
};

#endif /* DAG_FDW_REL_H */
