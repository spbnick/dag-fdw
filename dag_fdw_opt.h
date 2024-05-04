/**
 * Object option definition and parsing.
 */

#ifndef DAG_FDW_OPT_H
#define DAG_FDW_OPT_H

#include "postgres.h"
#include "nodes/pg_list.h"
#include <stdbool.h>

/**
 * The prototype for an option value parsing function.
 *
 * @param str   The string to parse the value from.
 * @param pval  The location for the parsed value, the actual type is specific
 *              to the particular function. Can be NULL to discard the output.
 *
 * @return True if the option value parsed successfully, false otherwise.
 */
typedef bool (*dag_fdw_opt_parse)(const char *str, void *pval);

/** The definition of an FDW's option */
struct dag_fdw_opt_def {
    /** The name of the option */
    const char         *name;
    /** True if the option is required, false otherwise */
    bool                required;
    /** The value parsing function */
    dag_fdw_opt_parse   parse;
    /** The location for the parsed value */
    void               *pvalue;
};

/** Parse a positive integer option value */
extern bool dag_fdw_opt_pos_int_parse(const char *str, void *pval);

/** Parse a relation name option value */
extern bool dag_fdw_opt_rel_name_parse(const char *str, void *pval);

/**
 * Parse configuration options according to definitions.
 * Report an error, if parsing has failed.
 *
 * @param opt_defs  A pointer to an array of option definitions, terminated
 *                  with a NULL name. Parsed options will be output to
 *                  values referenced by definitions, and "required" flags for
 *                  received options will be set to false.
 * @param opts      The options and values to parse.
 */
extern void dag_fdw_opt_defs_parse(struct dag_fdw_opt_def *opt_defs,
                                   const List *opts);

#endif /* DAG_FDW_OPT_H */
