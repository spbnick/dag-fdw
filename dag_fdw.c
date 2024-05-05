#include "postgres.h"
#include "fmgr.h"
#include "foreign/fdwapi.h"
#include "access/reloptions.h"
#include "optimizer/pathnode.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/planmain.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "dag_fdw_table.h"
#include "dag_fdw_server.h"
#include "dag_fdw_rel.h"
#include "dag_fdw_opt.h"

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
