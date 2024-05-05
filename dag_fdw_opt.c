/**
 * Object option definition and parsing.
 */

#include "dag_fdw_opt.h"
#include "dag_fdw_rels.h"
#include "nodes/parsenodes.h"
#include "utils/varlena.h"
#include "commands/defrem.h"

bool
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

bool
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

void
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
