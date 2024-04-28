#include "postgres.h"
#include "fmgr.h"
#include "foreign/fdwapi.h"
#include "access/reloptions.h"
#include "optimizer/pathnode.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/planmain.h"
#include "utils/rel.h"
#include "access/table.h"
#include "foreign/foreign.h"
#include "commands/defrem.h"
#include "nodes/pg_list.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "utils/lsyscache.h"
#include "utils/varlena.h"
#include "utils/builtins.h"

PG_FUNCTION_INFO_V1(dag_fdw_validator);
PG_FUNCTION_INFO_V1(dag_fdw_handler);

static void dag_fdw_GetForeignRelSize(PlannerInfo *root,
                                      RelOptInfo *baserel,
                                      Oid foreigntableid);

static void dag_fdw_GetForeignPaths(PlannerInfo *root,
                                    RelOptInfo *baserel,
                                    Oid foreigntableid);

static ForeignScan *dag_fdw_GetForeignPlan(PlannerInfo *root,
                                           RelOptInfo *baserel,
                                           Oid foreigntableid,
                                           ForeignPath *best_path,
                                           List *tlist,
                                           List *scan_clauses,
                                           Plan *outer_plan);

static void dag_fdw_BeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *dag_fdw_IterateForeignScan(ForeignScanState *node);
static void dag_fdw_ReScanForeignScan(ForeignScanState *node);
static void dag_fdw_EndForeignScan(ForeignScanState *node);

/** Server configuration */
struct dag_fdw_server {
    /** The length of node IDs in bytes */
    size_t      node_id_len;
};

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

/** The relations supported by the FDW (terminated by .name == NULL) */
static const struct dag_fdw_rel dag_fdw_rels[] = {
    {
     .name = "edges",
     .atttypids = {VARCHAROID, VARCHAROID, InvalidOid},
    },
    {.name = NULL}
};

/** Table configuration */
struct dag_fdw_table {
    /** The server configuration */
    struct dag_fdw_server *server;
    /** The relation the table is representing */
    const struct dag_fdw_rel *rel;
};

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
static bool
dag_fdw_opt_pos_int_parse(const char *str, void *pval)
{
    int num;
    int end;
    int *pnum = (int *)pval;
    if (sscanf(str, "%d%n", &num, &end) == 1 &&
        str[end] == '\0' && num > 0) {
        if (pnum != NULL) {
            *pnum = num;
        }
        return true;
    }
    return false;
}

/** Parse a relation name option value */
static bool
dag_fdw_opt_rel_name_parse(const char *str, void *pval)
{
    const struct dag_fdw_rel *rel;
    const struct dag_fdw_rel **prel =
        (const struct dag_fdw_rel **)pval;
    for (rel = dag_fdw_rels; rel->name != NULL; rel++) {
        if (strcmp(str, rel->name) == 0) {
            if (prel != NULL) {
                *prel = rel;
            }
            return true;
        }
    }
    return false;
}

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
static void
dag_fdw_opt_defs_parse(struct dag_fdw_opt_def *opt_defs, const List *opts)
{
    ListCell   *cell;
    struct dag_fdw_opt_def *opt_def;

    Assert(opt_defs != NULL);

    if (opt_defs->name == NULL && opts != NIL) {
        ereport(ERROR,
                errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                errmsg("No options are accepted in this context"));
    }

    /* For each provided option */
    foreach(cell, opts) {
        DefElem                    *def = (DefElem *)lfirst(cell);
        const char                 *name = def->defname;
        const char                 *str = defGetString(def);
        ClosestMatchState           match_state;

        initClosestMatch(&match_state, def->defname, 4);

        /* For each option definition */
        for (opt_def = opt_defs; opt_def->name; opt_def++) {
            /* If this is the option definition */
            if (strcmp(name, opt_def->name) == 0) {
                /* If parsing is successful */
                if (opt_def->parse(str, opt_def->pvalue)) {
                    opt_def->required = false;
                    break;
                }
                ereport(
                    ERROR,
                    errcode(ERRCODE_SYNTAX_ERROR),
                    errmsg("invalid value for option %s: \"%s\"", name, str)
                );
            }
            updateClosestMatch(&match_state, opt_def->name);
        }

        /* If we haven't found a definition for the option */
        if (!opt_def->name) {
            const char *closest_match = getClosestMatch(&match_state);
            ereport(ERROR,
                    errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                    errmsg("unknown option \"%s\"", def->defname),
                    closest_match ? errhint(
                        "Perhaps you meant \"%s\".", closest_match
                    ) : 0);
        }
    }

    /* Check all required options are provided */
    for (opt_def = opt_defs; opt_def->name; opt_def++) {
        if (opt_def->required) {
            ereport(
                ERROR,
                errcode(ERRCODE_FDW_OPTION_NAME_NOT_FOUND),
                errmsg("No value for required option %s", opt_def->name)
            );
        }
    }
}

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
static void
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
static void
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

/*
 * Validate the generic options given to a FOREIGN DATA WRAPPER, SERVER,
 * USER MAPPING or FOREIGN TABLE that uses dag_fdw.
 *
 * Raise an ERROR if the option or its value is considered invalid.
 */
Datum
dag_fdw_validator(PG_FUNCTION_ARGS)
{
    List       *opts = untransformRelOptions(PG_GETARG_DATUM(0));
    Oid         catalog = PG_GETARG_OID(1);
    switch (catalog) {
    case ForeignServerRelationId:
        dag_fdw_server_opts_parse(NULL, opts);
        break;
    case ForeignTableRelationId:
        dag_fdw_table_opts_parse(NULL, opts);
        break;
    case ForeignDataWrapperRelationId:
        if (opts != NIL) {
            ereport(ERROR,
                    errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                    errmsg("No options are accepted in this context"));
        }
        break;
    default:
        ereport(
            ERROR,
            errcode(ERRCODE_FDW_ERROR),
            errmsg("Creating %s objects not supported", get_rel_name(catalog))
        );
    }
    PG_RETURN_VOID();
}

Datum
dag_fdw_handler(PG_FUNCTION_ARGS)
{
    FdwRoutine *fdwroutine = makeNode(FdwRoutine);
    fdwroutine->GetForeignRelSize = dag_fdw_GetForeignRelSize;
    fdwroutine->GetForeignPaths = dag_fdw_GetForeignPaths;
    fdwroutine->GetForeignPlan = dag_fdw_GetForeignPlan;
    fdwroutine->BeginForeignScan = dag_fdw_BeginForeignScan;
    fdwroutine->IterateForeignScan = dag_fdw_IterateForeignScan;
    fdwroutine->ReScanForeignScan = dag_fdw_ReScanForeignScan;
    fdwroutine->EndForeignScan = dag_fdw_EndForeignScan;
    PG_RETURN_POINTER(fdwroutine);
}

static const char dag_fdw_data[][2][20] = {
#include "dag_fdw_sample.inc"
};

/**
 * Get configuration of a server.
 *
 * @param id    The oid of the foreign server to get configuration of.
 *
 * @return The pointer to the server configuration allocated in the current
 *         memory context.
 */
static struct dag_fdw_server *
dag_fdw_server_get(Oid id)
{
    struct dag_fdw_server *server = palloc0(sizeof(*server));
    dag_fdw_server_opts_parse(server, GetForeignServer(id)->options);
    return server;
}

/**
 * Validate a table against a table definition.
 * Report errors on validation failure.
 *
 * @param table     The table definition to validate the table against.
 * @param id        The Oid of the table to validate against the definition.
 */
static void
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

/**
 * Get configuration of a table.
 *
 * @param id    The oid of the foreign table to get configuration of.
 *
 * @return The pointer to the table configuration allocated in the current
 *         memory context.
 */
static struct dag_fdw_table *
dag_fdw_table_get(Oid id)
{
    struct dag_fdw_table   *table = palloc0(sizeof(*table));
    ForeignTable           *ft = GetForeignTable(id);
    table->server = dag_fdw_server_get(ft->serverid);
    dag_fdw_table_opts_parse(table, ft->options);
    dag_fdw_table_validate(table, id);
    return table;
}

static void
dag_fdw_GetForeignRelSize(PlannerInfo *root,
                          RelOptInfo *baserel,
                          Oid foreigntableid)
{
    struct dag_fdw_table *table = dag_fdw_table_get(foreigntableid);
    baserel->fdw_private = table;
    baserel->rows = sizeof(dag_fdw_data) / sizeof(*dag_fdw_data);
}

static void
dag_fdw_GetForeignPaths(PlannerInfo *root,
                        RelOptInfo *baserel,
                        Oid foreigntableid)
{
    Path *path = (Path *)create_foreignscan_path(
        root,
        baserel,
        NULL,               /* default pathtarget */
        baserel->rows,      /* rows */
        1,                  /* startup cost */
        1 + baserel->rows,  /* total cost */
        NIL,                /* no pathkeys */
        NULL,               /* no required outer relids */
        NULL,               /* no fdw_outerpath */
        NIL                 /* no fdw_private */
    );
    add_path(baserel, path);
}

static ForeignScan *
dag_fdw_GetForeignPlan(PlannerInfo *root,
                       RelOptInfo *baserel,
                       Oid foreigntableid,
                       ForeignPath *best_path,
                       List *tlist,
                       List *scan_clauses,
                       Plan *outer_plan)
{
#if 0
    struct dag_fdw_table *table = baserel->fdw_private;
#endif
    scan_clauses = extract_actual_clauses(scan_clauses, false);
    return make_foreignscan(
        tlist,
        scan_clauses,
        baserel->relid,
        NIL, /* no expressions we will evaluate */
        NIL, /* no private datum list */
        NIL, /* no custom tlist; our scan tuple looks like tlist */
        NIL, /* no quals we will recheck */
        outer_plan
    );
}

typedef struct dag_fdw_scan_state {
    size_t i;
    text *node;
    text *parent_node;
} dag_fdw_scan_state;

static void
dag_fdw_BeginForeignScan(ForeignScanState *node, int eflags)
{
#if 0
    ForeignScan *fs = (ForeignScan *)node->ss.ps.plan;
#endif
    dag_fdw_scan_state *state = palloc0(sizeof(dag_fdw_scan_state));
    state->node = palloc(sizeof(**dag_fdw_data) * 2 + VARHDRSZ);
    SET_VARSIZE(state->node, sizeof(**dag_fdw_data) * 2);
    state->parent_node = palloc(sizeof(**dag_fdw_data) * 2 + VARHDRSZ);
    SET_VARSIZE(state->parent_node, sizeof(**dag_fdw_data) * 2);
    node->fdw_state = state;
}

static TupleTableSlot *
dag_fdw_IterateForeignScan(ForeignScanState *node)
{
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
    dag_fdw_scan_state *state = node->fdw_state;

    ExecClearTuple(slot);

    if (state->i < sizeof(dag_fdw_data) / sizeof(*dag_fdw_data)) {
        hex_encode(dag_fdw_data[state->i][0], sizeof(**dag_fdw_data),
                   VARDATA(state->node));
        hex_encode(dag_fdw_data[state->i][1], sizeof(**dag_fdw_data),
                   VARDATA(state->parent_node));
        slot->tts_isnull[0] = false;
        slot->tts_values[0] = PointerGetDatum(state->node);
        slot->tts_isnull[1] = false;
        slot->tts_values[1] = PointerGetDatum(state->parent_node);
        ExecStoreVirtualTuple(slot);
        state->i++;
    }

    return slot;
}

static void
dag_fdw_ReScanForeignScan(ForeignScanState *node)
{
    dag_fdw_scan_state *state = node->fdw_state;
    state->i = 0;
}

static void
dag_fdw_EndForeignScan(ForeignScanState *node)
{
    /* Relying on PostgreSQL to destroy our data along with the context */
}

PG_MODULE_MAGIC;
