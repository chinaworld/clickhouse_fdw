/*-------------------------------------------------------------------------
 *
 * Clickhouse Foreign Data Wrapper for PostgreSQL
 *
 * Copyright (c) 2013 Andrew Dunstan
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author: Andrew Dunstan <andrew@dunslane.net>
 *
 * IDENTIFICATION
 *		  clickhouse_fdw/src/clickhouse_fdw.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/reloptions.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "funcapi.h"
#include "utils/varlena.h"
#include "../pg2ch/interface.h"

PG_MODULE_MAGIC;

/*
 * SQL functions
 */
extern Datum clickhouse_fdw_handler(PG_FUNCTION_ARGS);
extern Datum clickhouse_fdw_validator(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(clickhouse_fdw_handler);
PG_FUNCTION_INFO_V1(clickhouse_fdw_validator);


/* callback functions */
#if (PG_VERSION_NUM >= 90200)
static void clickhouseGetForeignRelSize(PlannerInfo *root,
						   RelOptInfo *baserel,
						   Oid foreigntableid);

static void clickhouseGetForeignPaths(PlannerInfo *root,
						 RelOptInfo *baserel,
						 Oid foreigntableid);

#if (PG_VERSION_NUM < 90500)
static ForeignScan *clickhouseGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses);
#else
static ForeignScan *clickhouseGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses,
						Plan *outer_plan
	);
#endif

#else /* 9.1 only */
static FdwPlan *clickhousePlanForeignScan(Oid foreigntableid, PlannerInfo *root, RelOptInfo *baserel);
#endif

static void clickhouseBeginForeignScan(ForeignScanState *node,
						  int eflags);

static TupleTableSlot *clickhouseIterateForeignScan(ForeignScanState *node);

static void clickhouseReScanForeignScan(ForeignScanState *node);

static void clickhouseEndForeignScan(ForeignScanState *node);

#if (PG_VERSION_NUM >= 90300)
static void clickhouseAddForeignUpdateTargets(Query *parsetree,
								 RangeTblEntry *target_rte,
								 Relation target_relation);

static List *clickhousePlanForeignModify(PlannerInfo *root,
						   ModifyTable *plan,
						   Index resultRelation,
						   int subplan_index);

static void clickhouseBeginForeignModify(ModifyTableState *mtstate,
							ResultRelInfo *rinfo,
							List *fdw_private,
							int subplan_index,
							int eflags);

static TupleTableSlot *clickhouseExecForeignInsert(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot);

static TupleTableSlot *clickhouseExecForeignUpdate(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot);

static TupleTableSlot *clickhouseExecForeignDelete(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot);

static void clickhouseEndForeignModify(EState *estate,
						  ResultRelInfo *rinfo);

static int	clickhouseIsForeignRelUpdatable(Relation rel);

#endif

static void clickhouseExplainForeignScan(ForeignScanState *node,
							struct ExplainState * es);

#if (PG_VERSION_NUM >= 90300)
static void clickhouseExplainForeignModify(ModifyTableState *mtstate,
							  ResultRelInfo *rinfo,
							  List *fdw_private,
							  int subplan_index,
							  struct ExplainState * es);
#endif

#if (PG_VERSION_NUM >= 90200)
static bool clickhouseAnalyzeForeignTable(Relation relation,
							 AcquireSampleRowsFunc *func,
							 BlockNumber *totalpages);
#endif

#if (PG_VERSION_NUM >= 90500)

static void clickhouseGetForeignJoinPaths(PlannerInfo *root,
							 RelOptInfo *joinrel,
							 RelOptInfo *outerrel,
							 RelOptInfo *innerrel,
							 JoinType jointype,
							 JoinPathExtraData *extra);


static RowMarkType clickhouseGetForeignRowMarkType(RangeTblEntry *rte,
							   LockClauseStrength strength);

static HeapTuple clickhouseRefetchForeignRow(EState *estate,
						   ExecRowMark *erm,
						   Datum rowid,
						   bool *updated);

static List *clickhouseImportForeignSchema(ImportForeignSchemaStmt *stmt,
							 Oid serverOid);

#endif

/*
 * structures used by the FDW
 *
 * These next structures are not actually used by clickhouse,but something like
 * them will be needed by anything more complicated that does actual work.
 */

/*
 * Describes the valid options for objects that use this wrapper.
 */
struct clickhouseFdwOption
{
	const char *optname;
	Oid			optcontext;		/* Oid of catalog in which option may appear */
};

/*
 * The plan state is set up in clickhouseGetForeignRelSize and stashed away in
 * baserel->fdw_private and fetched in clickhouseGetForeignPaths.
 */
typedef struct
{
	char	   *foo;
	int			bar;
} ClickhouseFdwPlanState;

/*
 * The scan state is for maintaining state for a scan, eiher for a
 * SELECT or UPDATE or DELETE.
 *
 * It is set up in clickhouseBeginForeignScan and stashed in node->fdw_state
 * and subsequently used in clickhouseIterateForeignScan,
 * clickhouseEndForeignScan and clickhouseReScanForeignScan.
 */
typedef struct
{
	char	   *baz;
	int			blurfl;
} ClickhouseFdwScanState;

/*
 * The modify state is for maintaining state of modify operations.
 *
 * It is set up in clickhouseBeginForeignModify and stashed in
 * rinfo->ri_FdwState and subsequently used in clickhouseExecForeignInsert,
 * clickhouseExecForeignUpdate, clickhouseExecForeignDelete and
 * clickhouseEndForeignModify.
 */
typedef struct
{
	char	   *chimp;
	int			chump;
} ClickhouseFdwModifyState;


Datum
clickhouse_fdw_handler(PG_FUNCTION_ARGS)
{
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	elog(DEBUG1, "entering function %s", __func__);

	/*
	 * assign the handlers for the FDW
	 *
	 * This function might be called a number of times. In particular, it is
	 * likely to be called for each INSERT statement. For an explanation, see
	 * core postgres file src/optimizer/plan/createplan.c where it calls
	 * GetFdwRoutineByRelId(().
	 */

	/* Required by notations: S=SELECT I=INSERT U=UPDATE D=DELETE */

	/* these are required */
#if (PG_VERSION_NUM >= 90200)
	fdwroutine->GetForeignRelSize = clickhouseGetForeignRelSize; /* S U D */
	fdwroutine->GetForeignPaths = clickhouseGetForeignPaths;		/* S U D */
	fdwroutine->GetForeignPlan = clickhouseGetForeignPlan;		/* S U D */
#else
	fdwroutine->PlanForeignScan = clickhousePlanForeignScan;     /* S */
#endif
	fdwroutine->BeginForeignScan = clickhouseBeginForeignScan;	/* S U D */
	fdwroutine->IterateForeignScan = clickhouseIterateForeignScan;		/* S */
	fdwroutine->ReScanForeignScan = clickhouseReScanForeignScan; /* S */
	fdwroutine->EndForeignScan = clickhouseEndForeignScan;		/* S U D */

	/* remainder are optional - use NULL if not required */
	/* support for insert / update / delete */
#if (PG_VERSION_NUM >= 90300)
	fdwroutine->IsForeignRelUpdatable = clickhouseIsForeignRelUpdatable;
	fdwroutine->AddForeignUpdateTargets = clickhouseAddForeignUpdateTargets;		/* U D */
	fdwroutine->PlanForeignModify = clickhousePlanForeignModify; /* I U D */
	fdwroutine->BeginForeignModify = clickhouseBeginForeignModify;		/* I U D */
	fdwroutine->ExecForeignInsert = clickhouseExecForeignInsert; /* I */
	fdwroutine->ExecForeignUpdate = clickhouseExecForeignUpdate; /* U */
	fdwroutine->ExecForeignDelete = clickhouseExecForeignDelete; /* D */
	fdwroutine->EndForeignModify = clickhouseEndForeignModify;	/* I U D */
#endif

	/* support for EXPLAIN */
	fdwroutine->ExplainForeignScan = clickhouseExplainForeignScan;		/* EXPLAIN S U D */
#if (PG_VERSION_NUM >= 90300)
	fdwroutine->ExplainForeignModify = clickhouseExplainForeignModify;	/* EXPLAIN I U D */
#endif

#if (PG_VERSION_NUM >= 90200)
	/* support for ANALYSE */
	fdwroutine->AnalyzeForeignTable = clickhouseAnalyzeForeignTable;		/* ANALYZE only */
#endif


#if (PG_VERSION_NUM >= 90500)
	/* Support functions for IMPORT FOREIGN SCHEMA */
	fdwroutine->ImportForeignSchema = clickhouseImportForeignSchema;

	/* Support for scanning foreign joins */
	fdwroutine->GetForeignJoinPaths = clickhouseGetForeignJoinPaths;

	/* Support for locking foreign rows */
	fdwroutine->GetForeignRowMarkType = clickhouseGetForeignRowMarkType;
	fdwroutine->RefetchForeignRow = clickhouseRefetchForeignRow;

#endif


	PG_RETURN_POINTER(fdwroutine);
}

Datum
clickhouse_fdw_validator(PG_FUNCTION_ARGS)
{
	List	   *options_list = untransformRelOptions(PG_GETARG_DATUM(0));

	elog(DEBUG1, "entering function %s", __func__);

	/* make sure the options are valid */

	/* no options are supported */

	if (list_length(options_list) > 0)
		ereport(ERROR,
				(errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
				 errmsg("invalid options"),
				 errhint("Clickhouse FDW does not support any options")));

	PG_RETURN_VOID();
}

#if (PG_VERSION_NUM >= 90200)
static void
clickhouseGetForeignRelSize(PlannerInfo *root,
						   RelOptInfo *baserel,
						   Oid foreigntableid)
{
	/*
	 * Obtain relation size estimates for a foreign table. This is called at
	 * the beginning of planning for a query that scans a foreign table. root
	 * is the planner's global information about the query; baserel is the
	 * planner's information about this table; and foreigntableid is the
	 * pg_class OID of the foreign table. (foreigntableid could be obtained
	 * from the planner data structures, but it's passed explicitly to save
	 * effort.)
	 *
	 * This function should update baserel->rows to be the expected number of
	 * rows returned by the table scan, after accounting for the filtering
	 * done by the restriction quals. The initial value of baserel->rows is
	 * just a constant default estimate, which should be replaced if at all
	 * possible. The function may also choose to update baserel->width if it
	 * can compute a better estimate of the average result row width.
	 */

	ClickhouseFdwPlanState *plan_state;

	elog(DEBUG1, "entering function %s", __func__);

	baserel->rows = 0;

	plan_state = palloc0(sizeof(ClickhouseFdwPlanState));
	baserel->fdw_private = (void *) plan_state;

	/* initialize required state in plan_state */

}

static void
clickhouseGetForeignPaths(PlannerInfo *root,
						 RelOptInfo *baserel,
						 Oid foreigntableid)
{
	/*
	 * Create possible access paths for a scan on a foreign table. This is
	 * called during query planning. The parameters are the same as for
	 * GetForeignRelSize, which has already been called.
	 *
	 * This function must generate at least one access path (ForeignPath node)
	 * for a scan on the foreign table and must call add_path to add each such
	 * path to baserel->pathlist. It's recommended to use
	 * create_foreignscan_path to build the ForeignPath nodes. The function
	 * can generate multiple access paths, e.g., a path which has valid
	 * pathkeys to represent a pre-sorted result. Each access path must
	 * contain cost estimates, and can contain any FDW-private information
	 * that is needed to identify the specific scan method intended.
	 */

	/*
	 * ClickhouseFdwPlanState *plan_state = baserel->fdw_private;
	 */

	Cost		startup_cost,
				total_cost;

	elog(DEBUG1, "entering function %s", __func__);

	startup_cost = 0;
	total_cost = startup_cost + baserel->rows;

	/* Create a ForeignPath node and add it as only possible path */
	add_path(baserel, (Path *)
			 create_foreignscan_path(root, baserel,
#if (PG_VERSION_NUM >= 90600)
									 NULL,      /* default pathtarget */
#endif
									 baserel->rows,
									 startup_cost,
									 total_cost,
									 NIL,		/* no pathkeys */
									 NULL,		/* no outer rel either */
#if (PG_VERSION_NUM >= 90500)
									 NULL,      /* no extra plan */
#endif
									 NIL));		/* no fdw_private data */
}


#if (PG_VERSION_NUM < 90500)
static ForeignScan *
clickhouseGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses)
#else
static ForeignScan *
clickhouseGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses,
						Plan *outer_plan)
#endif
{
	/*
	 * Create a ForeignScan plan node from the selected foreign access path.
	 * This is called at the end of query planning. The parameters are as for
	 * GetForeignRelSize, plus the selected ForeignPath (previously produced
	 * by GetForeignPaths), the target list to be emitted by the plan node,
	 * and the restriction clauses to be enforced by the plan node.
	 *
	 * This function must create and return a ForeignScan plan node; it's
	 * recommended to use make_foreignscan to build the ForeignScan node.
	 *
	 */

	/*
	 * ClickhouseFdwPlanState *plan_state = baserel->fdw_private;
	 */

	Index		scan_relid = baserel->relid;

	/*
	 * We have no native ability to evaluate restriction clauses, so we just
	 * put all the scan_clauses into the plan node's qual list for the
	 * executor to check. So all we have to do here is strip RestrictInfo
	 * nodes from the clauses and ignore pseudoconstants (which will be
	 * handled elsewhere).
	 */

	elog(DEBUG1, "entering function %s", __func__);

	scan_clauses = extract_actual_clauses(scan_clauses, false);

	/* Create the ForeignScan node */
#if(PG_VERSION_NUM < 90500)
	return make_foreignscan(tlist,
							scan_clauses,
							scan_relid,
							NIL,	/* no expressions to evaluate */
							NIL);		/* no private state either */
#else
	return make_foreignscan(tlist,
							scan_clauses,
							scan_relid,
							NIL,	/* no expressions to evaluate */
							NIL,	/* no private state either */
							NIL,	/* no custom tlist */
							NIL,    /* no remote quals */
							outer_plan);
#endif

}

#else

static FdwPlan *
clickhousePlanForeignScan(Oid foreigntableid, PlannerInfo *root, RelOptInfo *baserel)
{
	FdwPlan    *fdwplan;
	fdwplan = makeNode(FdwPlan);
	fdwplan->fdw_private = NIL;
	fdwplan->startup_cost = 0;
	fdwplan->total_cost = 0;
	return fdwplan;
}

#endif


static void
clickhouseBeginForeignScan(ForeignScanState *node,
						  int eflags)
{
	/*
	 * Begin executing a foreign scan. This is called during executor startup.
	 * It should perform any initialization needed before the scan can start,
	 * but not start executing the actual scan (that should be done upon the
	 * first call to IterateForeignScan). The ForeignScanState node has
	 * already been created, but its fdw_state field is still NULL.
	 * Information about the table to scan is accessible through the
	 * ForeignScanState node (in particular, from the underlying ForeignScan
	 * plan node, which contains any FDW-private information provided by
	 * GetForeignPlan). eflags contains flag bits describing the executor's
	 * operating mode for this plan node.
	 *
	 * Note that when (eflags & EXEC_FLAG_EXPLAIN_ONLY) is true, this function
	 * should not perform any externally-visible actions; it should only do
	 * the minimum required to make the node state valid for
	 * ExplainForeignScan and EndForeignScan.
	 *
	 */

	ClickhouseFdwScanState * scan_state = palloc0(sizeof(ClickhouseFdwScanState));
	node->fdw_state = scan_state;

	elog(DEBUG1, "entering function %s", __func__);

}


static TupleTableSlot *
clickhouseIterateForeignScan(ForeignScanState *node)
{
	/*
	 * Fetch one row from the foreign source, returning it in a tuple table
	 * slot (the node's ScanTupleSlot should be used for this purpose). Return
	 * NULL if no more rows are available. The tuple table slot infrastructure
	 * allows either a physical or virtual tuple to be returned; in most cases
	 * the latter choice is preferable from a performance standpoint. Note
	 * that this is called in a short-lived memory context that will be reset
	 * between invocations. Create a memory context in BeginForeignScan if you
	 * need longer-lived storage, or use the es_query_cxt of the node's
	 * EState.
	 *
	 * The rows returned must match the column signature of the foreign table
	 * being scanned. If you choose to optimize away fetching columns that are
	 * not needed, you should insert nulls in those column positions.
	 *
	 * Note that PostgreSQL's executor doesn't care whether the rows returned
	 * violate any NOT NULL constraints that were defined on the foreign table
	 * columns — but the planner does care, and may optimize queries
	 * incorrectly if NULL values are present in a column declared not to
	 * contain them. If a NULL value is encountered when the user has declared
	 * that none should be present, it may be appropriate to raise an error
	 * (just as you would need to do in the case of a data type mismatch).
	 */


	/* ----
	 * ClickhouseFdwScanState *scan_state =
	 *	 (ClickhouseFdwScanState *) node->fdw_state;
	 * ----
	 */

	TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;

	elog(DEBUG1, "entering function %s", __func__);

	ExecClearTuple(slot);

	/* get the next record, if any, and fill in the slot */

	/* then return the slot */
	return slot;
}


static void
clickhouseReScanForeignScan(ForeignScanState *node)
{
	/*
	 * Restart the scan from the beginning. Note that any parameters the scan
	 * depends on may have changed value, so the new scan does not necessarily
	 * return exactly the same rows.
	 */

	/* ----
	 * ClickhouseFdwScanState *scan_state =
	 *	 (ClickhouseFdwScanState *) node->fdw_state;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

}


static void
clickhouseEndForeignScan(ForeignScanState *node)
{
	/*
	 * End the scan and release resources. It is normally not important to
	 * release palloc'd memory, but for example open files and connections to
	 * remote servers should be cleaned up.
	 */

	/* ----
	 * ClickhouseFdwScanState *scan_state =
	 *	 (ClickhouseFdwScanState *) node->fdw_state;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

}


#if (PG_VERSION_NUM >= 90300)
static void
clickhouseAddForeignUpdateTargets(Query *parsetree,
								 RangeTblEntry *target_rte,
								 Relation target_relation)
{
	/*
	 * UPDATE and DELETE operations are performed against rows previously
	 * fetched by the table-scanning functions. The FDW may need extra
	 * information, such as a row ID or the values of primary-key columns, to
	 * ensure that it can identify the exact row to update or delete. To
	 * support that, this function can add extra hidden, or "junk", target
	 * columns to the list of columns that are to be retrieved from the
	 * foreign table during an UPDATE or DELETE.
	 *
	 * To do that, add TargetEntry items to parsetree->targetList, containing
	 * expressions for the extra values to be fetched. Each such entry must be
	 * marked resjunk = true, and must have a distinct resname that will
	 * identify it at execution time. Avoid using names matching ctidN or
	 * wholerowN, as the core system can generate junk columns of these names.
	 *
	 * This function is called in the rewriter, not the planner, so the
	 * information available is a bit different from that available to the
	 * planning routines. parsetree is the parse tree for the UPDATE or DELETE
	 * command, while target_rte and target_relation describe the target
	 * foreign table.
	 *
	 * If the AddForeignUpdateTargets pointer is set to NULL, no extra target
	 * expressions are added. (This will make it impossible to implement
	 * DELETE operations, though UPDATE may still be feasible if the FDW
	 * relies on an unchanging primary key to identify rows.)
	 */

	elog(DEBUG1, "entering function %s", __func__);

}


static List *
clickhousePlanForeignModify(PlannerInfo *root,
						   ModifyTable *plan,
						   Index resultRelation,
						   int subplan_index)
{
	/*
	 * Perform any additional planning actions needed for an insert, update,
	 * or delete on a foreign table. This function generates the FDW-private
	 * information that will be attached to the ModifyTable plan node that
	 * performs the update action. This private information must have the form
	 * of a List, and will be delivered to BeginForeignModify during the
	 * execution stage.
	 *
	 * root is the planner's global information about the query. plan is the
	 * ModifyTable plan node, which is complete except for the fdwPrivLists
	 * field. resultRelation identifies the target foreign table by its
	 * rangetable index. subplan_index identifies which target of the
	 * ModifyTable plan node this is, counting from zero; use this if you want
	 * to index into plan->plans or other substructure of the plan node.
	 *
	 * If the PlanForeignModify pointer is set to NULL, no additional
	 * plan-time actions are taken, and the fdw_private list delivered to
	 * BeginForeignModify will be NIL.
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return NULL;
}


static void
clickhouseBeginForeignModify(ModifyTableState *mtstate,
							ResultRelInfo *rinfo,
							List *fdw_private,
							int subplan_index,
							int eflags)
{
	/*
	 * Begin executing a foreign table modification operation. This routine is
	 * called during executor startup. It should perform any initialization
	 * needed prior to the actual table modifications. Subsequently,
	 * ExecForeignInsert, ExecForeignUpdate or ExecForeignDelete will be
	 * called for each tuple to be inserted, updated, or deleted.
	 *
	 * mtstate is the overall state of the ModifyTable plan node being
	 * executed; global data about the plan and execution state is available
	 * via this structure. rinfo is the ResultRelInfo struct describing the
	 * target foreign table. (The ri_FdwState field of ResultRelInfo is
	 * available for the FDW to store any private state it needs for this
	 * operation.) fdw_private contains the private data generated by
	 * PlanForeignModify, if any. subplan_index identifies which target of the
	 * ModifyTable plan node this is. eflags contains flag bits describing the
	 * executor's operating mode for this plan node.
	 *
	 * Note that when (eflags & EXEC_FLAG_EXPLAIN_ONLY) is true, this function
	 * should not perform any externally-visible actions; it should only do
	 * the minimum required to make the node state valid for
	 * ExplainForeignModify and EndForeignModify.
	 *
	 * If the BeginForeignModify pointer is set to NULL, no action is taken
	 * during executor startup.
	 */

	ClickhouseFdwModifyState *modify_state =
		palloc0(sizeof(ClickhouseFdwModifyState));
	rinfo->ri_FdwState = modify_state;

	elog(DEBUG1, "entering function %s", __func__);

}


static TupleTableSlot *
clickhouseExecForeignInsert(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot)
{
	/*
	 * Insert one tuple into the foreign table. estate is global execution
	 * state for the query. rinfo is the ResultRelInfo struct describing the
	 * target foreign table. slot contains the tuple to be inserted; it will
	 * match the rowtype definition of the foreign table. planSlot contains
	 * the tuple that was generated by the ModifyTable plan node's subplan; it
	 * differs from slot in possibly containing additional "junk" columns.
	 * (The planSlot is typically of little interest for INSERT cases, but is
	 * provided for completeness.)
	 *
	 * The return value is either a slot containing the data that was actually
	 * inserted (this might differ from the data supplied, for example as a
	 * result of trigger actions), or NULL if no row was actually inserted
	 * (again, typically as a result of triggers). The passed-in slot can be
	 * re-used for this purpose.
	 *
	 * The data in the returned slot is used only if the INSERT query has a
	 * RETURNING clause. Hence, the FDW could choose to optimize away
	 * returning some or all columns depending on the contents of the
	 * RETURNING clause. However, some slot must be returned to indicate
	 * success, or the query's reported rowcount will be wrong.
	 *
	 * If the ExecForeignInsert pointer is set to NULL, attempts to insert
	 * into the foreign table will fail with an error message.
	 *
	 */

	/* ----
	 * ClickhouseFdwModifyState *modify_state =
	 *	 (ClickhouseFdwModifyState *) rinfo->ri_FdwState;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return slot;
}


static TupleTableSlot *
clickhouseExecForeignUpdate(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot)
{
	/*
	 * Update one tuple in the foreign table. estate is global execution state
	 * for the query. rinfo is the ResultRelInfo struct describing the target
	 * foreign table. slot contains the new data for the tuple; it will match
	 * the rowtype definition of the foreign table. planSlot contains the
	 * tuple that was generated by the ModifyTable plan node's subplan; it
	 * differs from slot in possibly containing additional "junk" columns. In
	 * particular, any junk columns that were requested by
	 * AddForeignUpdateTargets will be available from this slot.
	 *
	 * The return value is either a slot containing the row as it was actually
	 * updated (this might differ from the data supplied, for example as a
	 * result of trigger actions), or NULL if no row was actually updated
	 * (again, typically as a result of triggers). The passed-in slot can be
	 * re-used for this purpose.
	 *
	 * The data in the returned slot is used only if the UPDATE query has a
	 * RETURNING clause. Hence, the FDW could choose to optimize away
	 * returning some or all columns depending on the contents of the
	 * RETURNING clause. However, some slot must be returned to indicate
	 * success, or the query's reported rowcount will be wrong.
	 *
	 * If the ExecForeignUpdate pointer is set to NULL, attempts to update the
	 * foreign table will fail with an error message.
	 *
	 */

	/* ----
	 * ClickhouseFdwModifyState *modify_state =
	 *	 (ClickhouseFdwModifyState *) rinfo->ri_FdwState;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return slot;
}


static TupleTableSlot *
clickhouseExecForeignDelete(EState *estate,
						   ResultRelInfo *rinfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planSlot)
{
	/*
	 * Delete one tuple from the foreign table. estate is global execution
	 * state for the query. rinfo is the ResultRelInfo struct describing the
	 * target foreign table. slot contains nothing useful upon call, but can
	 * be used to hold the returned tuple. planSlot contains the tuple that
	 * was generated by the ModifyTable plan node's subplan; in particular, it
	 * will carry any junk columns that were requested by
	 * AddForeignUpdateTargets. The junk column(s) must be used to identify
	 * the tuple to be deleted.
	 *
	 * The return value is either a slot containing the row that was deleted,
	 * or NULL if no row was deleted (typically as a result of triggers). The
	 * passed-in slot can be used to hold the tuple to be returned.
	 *
	 * The data in the returned slot is used only if the DELETE query has a
	 * RETURNING clause. Hence, the FDW could choose to optimize away
	 * returning some or all columns depending on the contents of the
	 * RETURNING clause. However, some slot must be returned to indicate
	 * success, or the query's reported rowcount will be wrong.
	 *
	 * If the ExecForeignDelete pointer is set to NULL, attempts to delete
	 * from the foreign table will fail with an error message.
	 */

	/* ----
	 * ClickhouseFdwModifyState *modify_state =
	 *	 (ClickhouseFdwModifyState *) rinfo->ri_FdwState;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return slot;
}


static void
clickhouseEndForeignModify(EState *estate,
						  ResultRelInfo *rinfo)
{
	/*
	 * End the table update and release resources. It is normally not
	 * important to release palloc'd memory, but for example open files and
	 * connections to remote servers should be cleaned up.
	 *
	 * If the EndForeignModify pointer is set to NULL, no action is taken
	 * during executor shutdown.
	 */

	/* ----
	 * ClickhouseFdwModifyState *modify_state =
	 *	 (ClickhouseFdwModifyState *) rinfo->ri_FdwState;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

}

static int
clickhouseIsForeignRelUpdatable(Relation rel)
{
	/*
	 * Report which update operations the specified foreign table supports.
	 * The return value should be a bit mask of rule event numbers indicating
	 * which operations are supported by the foreign table, using the CmdType
	 * enumeration; that is, (1 << CMD_UPDATE) = 4 for UPDATE, (1 <<
	 * CMD_INSERT) = 8 for INSERT, and (1 << CMD_DELETE) = 16 for DELETE.
	 *
	 * If the IsForeignRelUpdatable pointer is set to NULL, foreign tables are
	 * assumed to be insertable, updatable, or deletable if the FDW provides
	 * ExecForeignInsert, ExecForeignUpdate, or ExecForeignDelete
	 * respectively. This function is only needed if the FDW supports some
	 * tables that are updatable and some that are not. (Even then, it's
	 * permissible to throw an error in the execution routine instead of
	 * checking in this function. However, this function is used to determine
	 * updatability for display in the information_schema views.)
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return (1 << CMD_UPDATE) | (1 << CMD_INSERT) | (1 << CMD_DELETE);
}
#endif


static void
clickhouseExplainForeignScan(ForeignScanState *node,
							struct ExplainState * es)
{
	/*
	 * Print additional EXPLAIN output for a foreign table scan. This function
	 * can call ExplainPropertyText and related functions to add fields to the
	 * EXPLAIN output. The flag fields in es can be used to determine what to
	 * print, and the state of the ForeignScanState node can be inspected to
	 * provide run-time statistics in the EXPLAIN ANALYZE case.
	 *
	 * If the ExplainForeignScan pointer is set to NULL, no additional
	 * information is printed during EXPLAIN.
	 */

	elog(DEBUG1, "entering function %s", __func__);

}


#if (PG_VERSION_NUM >= 90300)
static void
clickhouseExplainForeignModify(ModifyTableState *mtstate,
							  ResultRelInfo *rinfo,
							  List *fdw_private,
							  int subplan_index,
							  struct ExplainState * es)
{
	/*
	 * Print additional EXPLAIN output for a foreign table update. This
	 * function can call ExplainPropertyText and related functions to add
	 * fields to the EXPLAIN output. The flag fields in es can be used to
	 * determine what to print, and the state of the ModifyTableState node can
	 * be inspected to provide run-time statistics in the EXPLAIN ANALYZE
	 * case. The first four arguments are the same as for BeginForeignModify.
	 *
	 * If the ExplainForeignModify pointer is set to NULL, no additional
	 * information is printed during EXPLAIN.
	 */

	/* ----
	 * ClickhouseFdwModifyState *modify_state =
	 *	 (ClickhouseFdwModifyState *) rinfo->ri_FdwState;
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

}
#endif


#if (PG_VERSION_NUM >= 90200)
static bool
clickhouseAnalyzeForeignTable(Relation relation,
							 AcquireSampleRowsFunc *func,
							 BlockNumber *totalpages)
{
	/* ----
	 * This function is called when ANALYZE is executed on a foreign table. If
	 * the FDW can collect statistics for this foreign table, it should return
	 * true, and provide a pointer to a function that will collect sample rows
	 * from the table in func, plus the estimated size of the table in pages
	 * in totalpages. Otherwise, return false.
	 *
	 * If the FDW does not support collecting statistics for any tables, the
	 * AnalyzeForeignTable pointer can be set to NULL.
	 *
	 * If provided, the sample collection function must have the signature:
	 *
	 *	  int
	 *	  AcquireSampleRowsFunc (Relation relation, int elevel,
	 *							 HeapTuple *rows, int targrows,
	 *							 double *totalrows,
	 *							 double *totaldeadrows);
	 *
	 * A random sample of up to targrows rows should be collected from the
	 * table and stored into the caller-provided rows array. The actual number
	 * of rows collected must be returned. In addition, store estimates of the
	 * total numbers of live and dead rows in the table into the output
	 * parameters totalrows and totaldeadrows. (Set totaldeadrows to zero if
	 * the FDW does not have any concept of dead rows.)
	 * ----
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return false;
}
#endif


#if (PG_VERSION_NUM >= 90500)
static void
clickhouseGetForeignJoinPaths(PlannerInfo *root,
							 RelOptInfo *joinrel,
							 RelOptInfo *outerrel,
							 RelOptInfo *innerrel,
							 JoinType jointype,
							 JoinPathExtraData *extra)
{
	/*
	 * Create possible access paths for a join of two (or more) foreign tables
	 * that all belong to the same foreign server. This optional function is
	 * called during query planning. As with GetForeignPaths, this function
	 * should generate ForeignPath path(s) for the supplied joinrel, and call
	 * add_path to add these paths to the set of paths considered for the
	 * join. But unlike GetForeignPaths, it is not necessary that this
	 * function succeed in creating at least one path, since paths involving
	 * local joining are always possible.
	 *
	 * Note that this function will be invoked repeatedly for the same join
	 * relation, with different combinations of inner and outer relations; it
	 * is the responsibility of the FDW to minimize duplicated work.
	 *
	 * If a ForeignPath path is chosen for the join, it will represent the
	 * entire join process; paths generated for the component tables and
	 * subsidiary joins will not be used. Subsequent processing of the join
	 * path proceeds much as it does for a path scanning a single foreign
	 * table. One difference is that the scanrelid of the resulting
	 * ForeignScan plan node should be set to zero, since there is no single
	 * relation that it represents; instead, the fs_relids field of the
	 * ForeignScan node represents the set of relations that were joined. (The
	 * latter field is set up automatically by the core planner code, and need
	 * not be filled by the FDW.) Another difference is that, because the
	 * column list for a remote join cannot be found from the system catalogs,
	 * the FDW must fill fdw_scan_tlist with an appropriate list of
	 * TargetEntry nodes, representing the set of columns it will supply at
	 * runtime in the tuples it returns.
	 */

	elog(DEBUG1, "entering function %s", __func__);

}


static RowMarkType
clickhouseGetForeignRowMarkType(RangeTblEntry *rte,
							   LockClauseStrength strength)
{
	/*
	 * Report which row-marking option to use for a foreign table. rte is the
	 * RangeTblEntry node for the table and strength describes the lock
	 * strength requested by the relevant FOR UPDATE/SHARE clause, if any. The
	 * result must be a member of the RowMarkType enum type.
	 *
	 * This function is called during query planning for each foreign table
	 * that appears in an UPDATE, DELETE, or SELECT FOR UPDATE/SHARE query and
	 * is not the target of UPDATE or DELETE.
	 *
	 * If the GetForeignRowMarkType pointer is set to NULL, the ROW_MARK_COPY
	 * option is always used. (This implies that RefetchForeignRow will never
	 * be called, so it need not be provided either.)
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return ROW_MARK_COPY;

}

static HeapTuple
clickhouseRefetchForeignRow(EState *estate,
						   ExecRowMark *erm,
						   Datum rowid,
						   bool *updated)
{
	/*
	 * Re-fetch one tuple from the foreign table, after locking it if
	 * required. estate is global execution state for the query. erm is the
	 * ExecRowMark struct describing the target foreign table and the row lock
	 * type (if any) to acquire. rowid identifies the tuple to be fetched.
	 * updated is an output parameter.
	 *
	 * This function should return a palloc'ed copy of the fetched tuple, or
	 * NULL if the row lock couldn't be obtained. The row lock type to acquire
	 * is defined by erm->markType, which is the value previously returned by
	 * GetForeignRowMarkType. (ROW_MARK_REFERENCE means to just re-fetch the
	 * tuple without acquiring any lock, and ROW_MARK_COPY will never be seen
	 * by this routine.)
	 *
	 * In addition, *updated should be set to true if what was fetched was an
	 * updated version of the tuple rather than the same version previously
	 * obtained. (If the FDW cannot be sure about this, always returning true
	 * is recommended.)
	 *
	 * Note that by default, failure to acquire a row lock should result in
	 * raising an error; a NULL return is only appropriate if the SKIP LOCKED
	 * option is specified by erm->waitPolicy.
	 *
	 * The rowid is the ctid value previously read for the row to be
	 * re-fetched. Although the rowid value is passed as a Datum, it can
	 * currently only be a tid. The function API is chosen in hopes that it
	 * may be possible to allow other datatypes for row IDs in future.
	 *
	 * If the RefetchForeignRow pointer is set to NULL, attempts to re-fetch
	 * rows will fail with an error message.
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return NULL;

}


static List *
clickhouseImportForeignSchema(ImportForeignSchemaStmt *stmt,
							 Oid serverOid)
{
	/*
	 * Obtain a list of foreign table creation commands. This function is
	 * called when executing IMPORT FOREIGN SCHEMA, and is passed the parse
	 * tree for that statement, as well as the OID of the foreign server to
	 * use. It should return a list of C strings, each of which must contain a
	 * CREATE FOREIGN TABLE command. These strings will be parsed and executed
	 * by the core server.
	 *
	 * Within the ImportForeignSchemaStmt struct, remote_schema is the name of
	 * the remote schema from which tables are to be imported. list_type
	 * identifies how to filter table names: FDW_IMPORT_SCHEMA_ALL means that
	 * all tables in the remote schema should be imported (in this case
	 * table_list is empty), FDW_IMPORT_SCHEMA_LIMIT_TO means to include only
	 * tables listed in table_list, and FDW_IMPORT_SCHEMA_EXCEPT means to
	 * exclude the tables listed in table_list. options is a list of options
	 * used for the import process. The meanings of the options are up to the
	 * FDW. For example, an FDW could use an option to define whether the NOT
	 * NULL attributes of columns should be imported. These options need not
	 * have anything to do with those supported by the FDW as database object
	 * options.
	 *
	 * The FDW may ignore the local_schema field of the
	 * ImportForeignSchemaStmt, because the core server will automatically
	 * insert that name into the parsed CREATE FOREIGN TABLE commands.
	 *
	 * The FDW does not have to concern itself with implementing the filtering
	 * specified by list_type and table_list, either, as the core server will
	 * automatically skip any returned commands for tables excluded according
	 * to those options. However, it's often useful to avoid the work of
	 * creating commands for excluded tables in the first place. The function
	 * IsImportableForeignTable() may be useful to test whether a given
	 * foreign-table name will pass the filter.
	 */

	elog(DEBUG1, "entering function %s", __func__);

	return NULL;
}

#endif


PG_FUNCTION_INFO_V1(retcomposite);

Datum
retcomposite(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tupdesc;
    AttInMetadata       *attinmeta;

     /* stuff done only on the first call of the function */
     if (SRF_IS_FIRSTCALL())
     {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        /* total number of tuples to be returned */
        funcctx->max_calls = PG_GETARG_UINT32(0);

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context "
                            "that cannot accept type record")));

        /*
         * generate attribute metadata needed later to produce tuples from raw
         * C strings
         */
        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta;

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr = funcctx->call_cntr;
    max_calls = funcctx->max_calls;
    attinmeta = funcctx->attinmeta;

    if (call_cntr < max_calls)    /* do when there is more left to send */
    {
        char       **values;
        HeapTuple    tuple;
        Datum        result;

        /*
         * Prepare a values array for building the returned tuple.
         * This should be an array of C strings which will
         * be processed later by the type input functions.
         */
        values = (char **) palloc(3 * sizeof(char *));
        values[0] = (char *) palloc(16 * sizeof(char));
        values[1] = (char *) palloc(16 * sizeof(char));
        values[2] = (char *) palloc(16 * sizeof(char));

        snprintf(values[0], 16, "%d", 1 * PG_GETARG_INT32(1));
        snprintf(values[1], 16, "%d", 2 * PG_GETARG_INT32(1));
        snprintf(values[2], 16, "%d", 3 * PG_GETARG_INT32(1));

        /* build a tuple */
        tuple = BuildTupleFromCStrings(attinmeta, values);

        /* make the tuple into a datum */
        result = HeapTupleGetDatum(tuple);

        /* clean up (this is not really necessary) */
        pfree(values[0]);
        pfree(values[1]);
        pfree(values[2]);
        pfree(values);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else    /* do when there is no more left */
    {
        SRF_RETURN_DONE(funcctx);
    }
}



PG_FUNCTION_INFO_V1(ch_execute);

Datum
ch_execute(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tupdesc;
    AttInMetadata       *attinmeta;
	char				*sql;
	CHReadCtx			*userCtx;

     /* stuff done only on the first call of the function */
     if (SRF_IS_FIRSTCALL())
     {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        /* total number of tuples to be returned */
        funcctx->max_calls = 7;



        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context "
                            "that cannot accept type record")));

        /*
         * generate attribute metadata needed later to produce tuples from raw
         * C strings
         */
        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta;


		sql = (char*) text_to_cstring(PG_GETARG_TEXT_PP(0));
		userCtx = palloc0(sizeof(CHReadCtx));
		funcctx->user_fctx = userCtx;
		userCtx->sql = sql;
		userCtx->natts = tupdesc->natts;
		userCtx->tupleValues = palloc(sizeof(char*) * tupdesc->natts);
		userCtx->tupleValues[0] = palloc(16);

		begin_ch_query(userCtx);

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr = funcctx->call_cntr;
    max_calls = funcctx->max_calls;
    attinmeta = funcctx->attinmeta;

    if (read_ch_query((CHReadCtx*)funcctx->user_fctx))    /* do when there is more left to send */
    {
        //char       **values;
        HeapTuple    tuple;
        Datum        result;

        /*
         * Prepare a values array for building the returned tuple.
         * This should be an array of C strings which will
         * be processed later by the type input functions.
         */
        //values = (char **) palloc(3 * sizeof(char *));
        //values[0] = (char *) palloc(16 * sizeof(char));

        //snprintf(values[0], 16, "%d", 123);

        /* build a tuple */
        tuple = BuildTupleFromCStrings(attinmeta, ((CHReadCtx*)funcctx->user_fctx)->tupleValues);

        /* make the tuple into a datum */
        result = HeapTupleGetDatum(tuple);

        /* clean up (this is not really necessary) */
        //pfree(values[0]);
        //pfree(values);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else    /* do when there is no more left */
    {
		end_ch_query((CHReadCtx*)funcctx->user_fctx);
        SRF_RETURN_DONE(funcctx);
    }
}