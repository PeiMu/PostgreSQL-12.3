/*-------------------------------------------------------------------------
 *
 *
 *
 *
 * IDENTIFICATION
 *	  src/backend/parser/query_split.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "parser/query_split.h"
#include "fe_utils/simple_list.h"
#include "commands/event_trigger.h"
#include "commands/portalcmds.h"
#include "utils/relmapper.h"
#include "commands/vacuum.h"
#include "utils/rel.h"
#include "storage/buf_internals.h"

#define NEWBETTER 1
#define OLDBETTER 2

#define DumpSubQueryString  false
#define DEBUG_TOTAL_SIZE    false
#define DEBUG_MERGE_SUB_PLANS false
#define DEBUG_QUERY_SPLIT   false

#define SUBQUERIES_NUM      5

double total_size = 0.0;
//long long optimize_time = 0;
//long long execution_time = 0;
//long long materialization_time = 0;

#define half_rounded(x) (((x) + ((x) < 0 ? 0 : 1)) / 2)

List* rel2relids = NIL;

Datum pg_relation_size(PG_FUNCTION_ARGS);

static double getMatSize(Oid relid);

//Create a local query
static Query* createQuery(const Query* querytree, CommandDest dest, List* rtable, Index* transfer_array, int length);
//change the RangeTblEntry relid to the new one
static void dochange(RangeTblEntry* rte, char* relname, Relation relation, Oid relid);
//change the NullTest clause args to the new one
static bool doNullTestTransfor(NullTest* expr, Index* transfer_array);
//change the OpExpr clause args to the new one
static bool doOpExprTransfor(OpExpr* expr, Index* transfer_array);
//change the ScalarArrayOpExpr clause args to the new one
static bool doScalarArrayOpExprTransfor(ScalarArrayOpExpr* expr, Index* transfer_array);
//Get the var will link to unlocal table
static List* findvarlist(List* joinlist, Index* transfer_array, int length);
//from postgres.c
extern void finish_xact_command();
//Get local rtable
static List* getRT_1(List* global_rtable, bool* graph, int length, int i, int j, Index* transfer_array);
static List* getRT_2(List* global_rtable, bool* graph, int length, int i, Index* transfer_array);
//Get local rtables' foreign keys
static List* grFK(List* rtable);
//is this subquery is the last ?
static int hasNext(bool* graph, int length);
// Is this expr refer to two relationship table ?
static bool is_2relationship(OpExpr * opexpr, bool* is_relationship, int length);
//Is this a Entity-to-Relationship Join ?
static bool is_ER(OpExpr* opexpr, bool* is_relationship, int length);
//Is a foreign key join ?
static bool is_FK(OpExpr* opexpr, List* fklist);
//Is a restrict clause ?
static bool is_RC(Expr* expr);
//Transefer jointree to graph
static bool* List2Graph(bool* is_relationship, List* joinlist, List* FKlist, int length);
//Make a aggregation function as result
static List* removeAggref(List* targetList);
//give the new value to some var, prepare for the next subquery
static List* Prepare4Next(Query* global_query, Index* transfer_array, DR_intorel* receiver, PlannedStmt* plannedstmt,char* relname, List* FKlist);
static void Recon(char* query_string, char* commandTag, Node* pstmt, Query* ori_query, char* completionTag);
//remove redundant join
static void rRj(Query* querytree);
//Transfer fromlist to the local
static List* setfromlist(List* fromlist, Index* transfer_array, int length);
//Transfer global var to local
static List* setjoinlist(List* rclist, CommandDest dest, Index* transfer_array, int length);
//Make a local target list
static List* settargetlist(const List* global_rtable, List* local_rtable, CommandDest dest, List* varlist, List* targetlist, Index* transfer_array, int length);
//Remove used jointree
static List* simplifyjoinlist(List* list, CommandDest dest, Index* transfer_array, bool* graph, int length);
//Split Query by Foreign Key
static List* spq(char* query_string, char* commandTag, Node* pstmt, Query* querytree, char* completionTag);
//from postgres.c
extern void start_xact_command();
static int tarfunc(Index* rels, PlannedStmt* new, PlannedStmt* old);
//Execute the local query
static List* QSExecutor(char* query_string, const char* commandTag, Node* pstmt, PlannedStmt* plannedstmt, CommandDest dest, char* relname, char* completionTag, Query* querytree, Index* transfer_array, List* FKlist, MemoryContext oldcontext);
//find the subquery with lowest cost to be executed
static PlannedStmt* QSOptimizer(Query* global_query, bool* graph, Index* transfer_array, int length);
static Plan* find_node_with_nleaf_recursive(Plan* plan, int nleaf, int* leaf_has, int* depth);
static void walk_plantree(Plan* plan, Index* rel);

bool* is_relationship;
//the number of subquery
static int queryId = 0;
//where to send the result, to the client end or temporary table
CommandDest mydest;
Index* transfer_array = NULL;

timespec aqp_timer;
bool execute_plan_timer = false;

timespec diff(timespec start, timespec end)
{
    timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

timespec tic( )
{
    timespec start_time;
    if (-1 == clock_gettime(CLOCK_REALTIME, &start_time)) {
        elog(ERROR, "Could not get clock time!");
    }
    return start_time;
}

void printTimeSpec(timespec t, const char* prefix) {
    elog(INFO, "%s: %d.%09d\n", prefix, (int)t.tv_sec, (int)t.tv_nsec);
}

timespec toc( timespec* start_time, const char* prefix, bool print )
{
    timespec current_time;
    if (-1 == clock_gettime(CLOCK_REALTIME, &current_time)) {
        elog(ERROR, "Could not get clock time!");
        D_ASSERT(false);
    }
    timespec time_diff = diff( *start_time, current_time );
    if (true)
        printTimeSpec( time_diff, prefix );
    *start_time = current_time;
    return time_diff;
}

//The interface
void doQSparse(const char* query_string, const char* commandTag, Node* pstmt, Query* querytree, char* completionTag)
{
	List* FKlist = NIL;
	if (querytree->commandType != CMD_UTILITY && query_splitting_algorithm != Minsubquery)
	{
		//remove Redundant Join
		rRj(querytree);
	}
	PlannedStmt* plannedstmt = NULL;
	if (querytree->commandType == CMD_UTILITY)
	{
		MemoryContext oldcontext = MemoryContextSwitchTo(MessageContext);
		/* Utility commands require no planning. */
		plannedstmt = makeNode(PlannedStmt);
		plannedstmt->commandType = CMD_UTILITY;
		plannedstmt->canSetTag = querytree->canSetTag;
		plannedstmt->utilityStmt = querytree->utilityStmt;
		plannedstmt->stmt_location = querytree->stmt_location;
		plannedstmt->stmt_len = querytree->stmt_len;
		QSExecutor(query_string, commandTag, pstmt, plannedstmt, DestRemote, NULL, completionTag, querytree, NULL, NIL, oldcontext);
		return;
	}
	ListCell* lc;
	int length = 0;
#if MEASURE_TIME || MERGE_SUB_PLANS
    execute_plan_timer = true;
    aqp_timer = tic();
#endif
	foreach(lc, querytree->rtable)
	{
		RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc);
		if (rte->relkind != RELKIND_RELATION)
		{
			MemoryContext oldcontext = MemoryContextSwitchTo(MessageContext);
			plannedstmt = planner(querytree, CURSOR_OPT_PARALLEL_OK, NULL);
#if MEASURE_TIME
            if (execute_plan_timer) {
                timespec opt_time = toc(&aqp_timer, "PG optimization time is", false);
                // save time to a file
                FILE *file = fopen("time_log.csv", "a");
                if (NULL == file) {
                    printf("Error opening file\n");
                    exit(-1);
                }
                fprintf(file, "%d.%09d, ", (int)opt_time.tv_sec, (int)opt_time.tv_nsec);
                fclose(file);
            }
#endif
			QSExecutor(query_string, commandTag, pstmt, plannedstmt, DestRemote, NULL, completionTag, querytree, NULL, NIL, oldcontext);
#if MEASURE_TIME || MERGE_SUB_PLANS
            FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "\n");
        fclose(file);
#endif
			return;
		}
		length++;
	}
	if (length <= 2)
	{
		MemoryContext oldcontext = MemoryContextSwitchTo(MessageContext);
		plannedstmt = planner(querytree, CURSOR_OPT_PARALLEL_OK, NULL);
#if MEASURE_TIME
        if (execute_plan_timer) {
            timespec opt_time = toc(&aqp_timer, "PG optimization time is", false);
            // save time to a file
            FILE *file = fopen("time_log.csv", "a");
            if (NULL == file) {
                printf("Error opening file\n");
                exit(-1);
            }
            fprintf(file, "%d.%09d, ", (int)opt_time.tv_sec, (int)opt_time.tv_nsec);
            fclose(file);
        }
#endif
		QSExecutor(query_string, commandTag, pstmt, plannedstmt, DestRemote, NULL, completionTag, querytree, NULL, NIL, oldcontext);
#if MEASURE_TIME || MERGE_SUB_PLANS
        FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "\n");
        fclose(file);
#endif
		return;
	}
	//split parent query by foreign key
	Recon(query_string, commandTag, pstmt, querytree, completionTag);

	return;
}

//remove Redundant Join
static void rRj(Query* querytree)
{
	//get all the foreign key
	List* FKlist = grFK(querytree->rtable);
	int length = querytree->rtable->length;
	is_relationship = (bool*)palloc(length * sizeof(bool));
	memset(is_relationship, true, length * sizeof(bool));
	ListCell* lc;
	//referenced relation is entity
	foreach(lc, FKlist)
	{
		ForeignKeyOptInfo* fkOptInfo = (ForeignKeyOptInfo*)lc->data.ptr_value;
		int x = fkOptInfo->ref_relid - 1;
		is_relationship[x] = false;
	}
	if (querytree->jointree->quals == NULL)
		return;
	//SQL where clause
	switch (querytree->jointree->quals->type)
	{
		case T_BoolExpr:
		{	
			BoolExpr* expr = (BoolExpr*)querytree->jointree->quals;
			if (expr == NULL)
				return;
			//expression list in the SQL where clause
			List* where = expr->args;
			//remove the redundant expression in expression list
			foreach(lc, where)
			{
				//is this expression a filter clause ?
				if (is_RC(lc->data.ptr_value))
				{
					continue;
				}
				//is this expression contian two relationship table ?
				if (is_2relationship(lc->data.ptr_value, is_relationship, length))
				{
					//if yes, remove it from expression list
					where = list_delete(where, lfirst(lc));
					continue;
				}
			}
			break;
		}
		case T_OpExpr:
			break;
	}
	return;
}

void locateTempId(PlannedStmt *currentPlannedStmt, Oid *current_temp_table_id)
{
    // Check for temp table references in the RTEs of the currentPlannedStmt
    List *current_rtable = currentPlannedStmt->rtable;

    if (NULL == current_rtable) {
        elog(ERROR, "current rtable is NULL!");
    }

    Oid temp_table_id = 1;
    ListCell *lc;
    int index = 0;
    foreach(lc, current_rtable) {
        RangeTblEntry *rte = (RangeTblEntry *)lfirst(lc);
        if (rte->eref && rte->eref->aliasname && 0 == strncmp(rte->eref->aliasname, "temp", 4)) {
#if DEBUG_MERGE_SUB_PLANS
            elog(LOG, "found the temp table in rte! temp_table_id = %d", temp_table_id);
#endif
            current_temp_table_id[index] = temp_table_id;
            index++;
        }
        temp_table_id++;
    }
}

void ReplaceTempScanNode(Plan **dest_tree, Plan *source_tree, Oid temp_table_id)
{
    if (dest_tree == NULL || *dest_tree == NULL) {
        elog(ERROR, "dest tree is null!");
    }

    // Check if the dest_tree node references the temp table
    if (T_SeqScan == nodeTag(*dest_tree))
    {
        Scan *scanNode = (Scan *)*dest_tree;
        if (scanNode->scanrelid == temp_table_id)
        {
#if DEBUG_MERGE_SUB_PLANS
            elog(LOG, "found the temp RTE id %d in the dest_tree!", temp_table_id);
#endif
            // Generate a SubqueryScan node, including a scan wrapper and a subplan node
            SubqueryScan *subquery_scan = makeNode(SubqueryScan);
            // the scan wrapper is the scanNode
            subquery_scan->scan.plan.type = T_SubqueryScan;
            subquery_scan->scan.plan.startup_cost = scanNode->plan.startup_cost;
            subquery_scan->scan.plan.total_cost = scanNode->plan.total_cost;
            subquery_scan->scan.plan.plan_rows = scanNode->plan.plan_rows;
            subquery_scan->scan.plan.plan_width = scanNode->plan.plan_width;
            subquery_scan->scan.plan.parallel_aware = scanNode->plan.parallel_aware;
            subquery_scan->scan.plan.parallel_safe = scanNode->plan.parallel_safe;
            subquery_scan->scan.plan.plan_node_id = scanNode->plan.plan_node_id;
            subquery_scan->scan.plan.targetlist = copyObjectImpl(scanNode->plan.targetlist);
            subquery_scan->scan.plan.qual = copyObjectImpl(scanNode->plan.qual);
            subquery_scan->scan.plan.initPlan = copyObjectImpl(scanNode->plan.initPlan);
            subquery_scan->scan.plan.extParam = bms_copy(scanNode->plan.extParam);
            subquery_scan->scan.plan.allParam = bms_copy(scanNode->plan.allParam);

            subquery_scan->scan.scanrelid = scanNode->scanrelid;
            // the subplan node is the source_tree
            subquery_scan->subplan = copyObjectImpl(source_tree);
            *dest_tree = subquery_scan;
            return;
        }
    }

    // Recursively check left and right subtrees
    if ((*dest_tree)->lefttree)
        ReplaceTempScanNode(&(*dest_tree)->lefttree, source_tree, temp_table_id);

    if ((*dest_tree)->righttree)
        ReplaceTempScanNode(&(*dest_tree)->righttree, source_tree, temp_table_id);
}

void ModifyRtable(List **rtable, Oid temp_table_id) {
    ListCell *rt_lc;
    Oid rt_index = 1;
    foreach(rt_lc, (*rtable)) {
        RangeTblEntry *rte = (RangeTblEntry *) lfirst(rt_lc);
        if (rt_index == temp_table_id) {
            RangeTblEntry *new_rte = makeNode(RangeTblEntry);
            new_rte->alias = copyObjectImpl(rte->alias);
            new_rte->alias->aliasname = rte->eref->aliasname;
            new_rte->eref = copyObjectImpl(rte->eref);
            new_rte->rtekind = RTE_SUBQUERY;
            new_rte->security_barrier = false;
            new_rte->lateral = false;
            new_rte->inh = false;
            new_rte->inFromCl = false;
            new_rte->requiredPerms = 0;
            new_rte->checkAsUser = 0;
            rt_lc->data.ptr_value = new_rte;
            return;
        }
        rt_index++;
    }
}

// update varno, varnoold
void UpdateVarNodeIndex(Var **var, int current_rte_length) {
    Index new_index = (*var)->varnoold + current_rte_length;
    // also update varno if it equals to varnoold
    if ((*var)->varno == (*var)->varnoold) {
        (*var)->varno = new_index;
    }
    (*var)->varnoold = new_index;
}

// update paramid
void UpdateParamNodeIndex(Param **param, int current_param_num) {
    (*param)->paramid += current_param_num;
}

void UpdateRelabelTypeIndex(RelabelType **relabel_type, int current_rte_length) {
    if (IsA((*relabel_type)->arg, Var)) {
        Var *relabel_type_arg = (Var *)(*relabel_type)->arg;
        UpdateVarNodeIndex(&relabel_type_arg, current_rte_length);
    } else if (IsA((*relabel_type)->arg, Const)) {
        return;
    } else {
        elog(ERROR, "Unsupported relabel_type->arg type: %d in qual OpExpr RelabelType!", (*relabel_type)->arg->type);
    }
}

void UpdateOpExprIndex(OpExpr **qual_expr, int current_rte_length, int current_param_num) {
    ListCell *arg_lc;
    foreach(arg_lc, (*qual_expr)->args) {
        Node *arg_lc_node = (Node *) lfirst(arg_lc);
        if (IsA(arg_lc_node, Var)) {
            Var *var_node = (Var *)arg_lc_node;
            UpdateVarNodeIndex(&var_node, current_rte_length);
        } else if (IsA(arg_lc_node, Param)) {
            Param *param_node = (Param *)arg_lc_node;
            UpdateParamNodeIndex(&param_node, current_param_num);
        } else if (IsA(arg_lc_node, RelabelType)) {
            RelabelType *relabel_type_node = (RelabelType *)arg_lc_node;
            UpdateRelabelTypeIndex(&relabel_type_node, current_rte_length);
        } else if (IsA(arg_lc_node, Const)) {
            continue;
        } else {
            elog(ERROR, "Unsupported arg_lc_node type: %d in qual OpExpr!", arg_lc_node->type);
        }
    }
}

void UpdateScalarArrayOpExprIndex(ScalarArrayOpExpr **qual_expr, int current_rte_length, int current_param_num) {
    ListCell *arg_lc;
    foreach(arg_lc, (*qual_expr)->args) {
        Node *arg_lc_node = (Node *) lfirst(arg_lc);
        if (IsA(arg_lc_node, Var)) {
            Var *var_node = (Var *)arg_lc_node;
            UpdateVarNodeIndex(&var_node, current_rte_length);
        } else if (IsA(arg_lc_node, Param)) {
            Param *param_node = (Param *)arg_lc_node;
            UpdateParamNodeIndex(&param_node, current_param_num);
        } else if (IsA(arg_lc_node, RelabelType)) {
            RelabelType *relabel_type_node = (RelabelType *)arg_lc_node;
            UpdateRelabelTypeIndex(&relabel_type_node, current_rte_length);
        } else if (IsA(arg_lc_node, Const)) {
            continue;
        } else {
            elog(ERROR, "Unsupported arg_lc_node type: %d in qual ScalarArrayOpExpr!", arg_lc_node->type);
        }
    }
}

void UpdateBoolExprIndex(BoolExpr **bool_expr, int current_rte_length, int current_param_num) {
    ListCell *qual_lc;
    foreach(qual_lc, (*bool_expr)->args) {
        Node *qual_lc_node = (Node *) lfirst(qual_lc);
        if (IsA(qual_lc_node, OpExpr)) {
            OpExpr *op_expr_node = (OpExpr *)qual_lc_node;
            UpdateOpExprIndex(&op_expr_node, current_rte_length, current_param_num);
        } else if (IsA(qual_lc_node, BoolExpr)) {
            BoolExpr *bool_expr_node = (BoolExpr *)qual_lc_node;
            UpdateBoolExprIndex(&bool_expr_node, current_rte_length, current_param_num);
        } else {
            elog(ERROR, "Unsupported qual_lc_node type: %d in qual BoolExpr!", qual_lc_node->type);
        }
    }
}

void UpdateNullTestIndex(NullTest **null_test, int current_rte_length, int current_param_num) {
    if (IsA((*null_test)->arg, Var)) {
        Var *var_node = (Var *)((*null_test)->arg);
        UpdateVarNodeIndex(&var_node, current_rte_length);
    } else {
        elog(ERROR, "Unsupported null_test->arg type: %d in qual NullTest!", (*null_test)->arg->type);
    }
}

void UpdatePrevTreeIndex(Plan **planTree, int current_rte_length, int current_param_num)
{
    if (planTree == NULL || *planTree == NULL) {
        elog(ERROR, "Plan tree is null!");
    }

    // Check and update the left subtree
    if ((*planTree)->lefttree) {
        UpdatePrevTreeIndex(&(*planTree)->lefttree, current_rte_length, current_param_num);
    }

    // Check and update the right subtree
    if ((*planTree)->righttree) {
        UpdatePrevTreeIndex(&(*planTree)->righttree, current_rte_length, current_param_num);
    }

    // todo: update plan_node_id

    // update targetlist
#if DEBUG_MERGE_SUB_PLANS
    elog(LOG, "update targetlist: %s", nodeToString((*planTree)->targetlist));
#endif
    ListCell *lc;
    foreach(lc, (*planTree)->targetlist) {
        TargetEntry *te = (TargetEntry *) lfirst(lc);
        if (IsA(te->expr, Var)) {
            Var *var_node = (Var *)(te->expr);
            UpdateVarNodeIndex(&var_node, current_rte_length);
        } else if (IsA(te->expr, Aggref)) {
            Aggref *aggref = (Aggref *)te->expr;
            ListCell *arg_lc;
            foreach(arg_lc, aggref->args) {
                TargetEntry *agg_te = (TargetEntry *) lfirst(arg_lc);
                if (IsA(agg_te->expr, Var)) {
                    Var *var_node = (Var *)(agg_te->expr);
                    UpdateVarNodeIndex(&var_node, current_rte_length);
                } else if (IsA(agg_te->expr, Param)) {
                    Param *param_node = (Param *)(agg_te->expr);
                    UpdateParamNodeIndex(&param_node, current_param_num);
                } else if (IsA(agg_te->expr, Const)) {
                    continue;
                } else {
                    elog(ERROR, "Unsupported agg_te->expr type: %d in targetlist Aggref!", agg_te->expr->type);
                }
            }
        } else if (IsA(te->expr, Const)) {
            continue;
        } else {
            elog(ERROR, "Unsupported te->expr type: %d in targetlist!", te->expr->type);
        }

//        // adjust the removed resorigtbl, resorigcol to index_pairs[resorigcol].varnoold, index_pairs[resorigcol].varoattno
//        if (te->resorigtbl == removed_relation_oid) {
//            int pair_index = te->resorigcol-1;
//            int oid_index = index_pairs[pair_index].varnoold;
//            if(oid_index > removed_index) {
//                oid_index -= 1;
//            }
//            // count from 0
//            te->resorigtbl = list_nth_oid(prev_oids, oid_index-1);
//            te->resorigcol = index_pairs[pair_index].varoattno;
//        }
    }

    // update qual
    if (NULL != (*planTree)->qual) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update qual: %s", nodeToString((*planTree)->qual));
#endif
        ListCell *qual_lc;
        foreach(qual_lc, (*planTree)->qual) {
            Node *qual_node = (Node *) lfirst(qual_lc);
            if (IsA(qual_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qual_node;
                UpdateOpExprIndex(&qual_node, current_rte_length, current_param_num);
            } else if (IsA(qual_node, ScalarArrayOpExpr)) {
                ScalarArrayOpExpr *scalar_array_op_expr = (ScalarArrayOpExpr *)qual_node;
                UpdateScalarArrayOpExprIndex(&scalar_array_op_expr, current_rte_length, current_param_num);
            } else if (IsA(qual_node, BoolExpr)) {
                BoolExpr *bool_expr = (BoolExpr *)qual_node;
                UpdateBoolExprIndex(&bool_expr, current_rte_length, current_param_num);
            } else if (IsA(qual_node, NullTest)) {
                NullTest *null_test = (NullTest *)qual_node;
                UpdateNullTestIndex(&null_test, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qual_node type: %d in qual!", qual_node->type);
            }
        }
    }

    // update extParam, allParam
    if (NULL != (*planTree)->extParam) {
        Bitmapset *new_extParam = NULL;
        int ext_param_val = -1;
        while ((ext_param_val = bms_next_member((*planTree)->extParam, ext_param_val)) >= 0) {
#if DEBUG_MERGE_SUB_PLANS
            elog(LOG, "ext_param_id=%d", ext_param_val);
#endif
            new_extParam = bms_add_member(new_extParam, ext_param_val + current_param_num);
        }
        bms_free((*planTree)->extParam);
        (*planTree)->extParam = new_extParam;
    }
    if (NULL != (*planTree)->allParam) {
        Bitmapset *new_allParam = NULL;
        int all_param_val = -1;
        while ((all_param_val = bms_next_member((*planTree)->allParam, all_param_val)) >= 0) {
#if DEBUG_MERGE_SUB_PLANS
            elog(LOG, "all_param_val=%d", all_param_val);
#endif
            new_allParam = bms_add_member(new_allParam, all_param_val+current_param_num);
        }
        bms_free((*planTree)->allParam);
        (*planTree)->allParam = new_allParam;
    }

    // update Scan
    if (IsA((*planTree), SeqScan)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update Scan: %s", nodeToString(*planTree));
#endif
        Scan *scan_node = (Scan *)(*planTree);
        scan_node->scanrelid += current_rte_length;
    }

    // update BitmapIndexScan
    if (IsA((*planTree), BitmapIndexScan)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update BitmapIndexScan");
#endif
        BitmapIndexScan *bmi_scan = (BitmapIndexScan *)(*planTree);
        // update Scan
        Scan *scan_node = &bmi_scan->scan;
        scan_node->scanrelid += current_rte_length;
        // update qual
        ListCell *qual_lc;
        foreach(qual_lc, bmi_scan->indexqual) {
            Node *qual_node = (Node *) lfirst(qual_lc);
            if (IsA(qual_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qual_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qual_node type: %d in BitmapIndexScan indexqual!", qual_node->type);
            }
        }
        ListCell *qualorig_lc;
        foreach(qualorig_lc, bmi_scan->indexqualorig) {
            Node *qualorig_node = (Node *) lfirst(qualorig_lc);
            if (IsA(qualorig_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qualorig_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qual_node type: %d in BitmapIndexScan indexqualorig!", qualorig_node->type);
            }
        }
    }

    // update BitmapHeapScan
    if (IsA((*planTree), BitmapHeapScan)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update BitmapHeapScan");
#endif
        BitmapHeapScan *bmh_scan = (BitmapHeapScan *)(*planTree);
        // update Scan
        Scan *scan_node = &bmh_scan->scan;
        scan_node->scanrelid += current_rte_length;
        // update qual
        ListCell *qualorig_lc;
        foreach(qualorig_lc, bmh_scan->bitmapqualorig) {
            Node *qualorig_node = (Node *) lfirst(qualorig_lc);
            if (IsA(qualorig_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qualorig_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qualorig_node type: %d in BitmapHeapScan bitmapqualorig!", qualorig_node->type);
            }
        }
    }

    // update IndexScan
    if (IsA((*planTree), IndexScan)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update IndexScan");
#endif
        IndexScan *index_scan = (IndexScan *)(*planTree);
        // update Scan
        Scan *scan_node = &index_scan->scan;
        scan_node->scanrelid += current_rte_length;
        // update qual
        ListCell *qual_lc;
        foreach(qual_lc, index_scan->indexqual) {
            Node *qual_node = (Node *) lfirst(qual_lc);
            if (IsA(qual_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qual_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qual_node type: %d in IndexScan indexqual!", qual_node->type);
            }
        }
        ListCell *qualorig_lc;
        foreach(qualorig_lc, index_scan->indexqualorig) {
            Node *qualorig_node = (Node *) lfirst(qualorig_lc);
            if (IsA(qualorig_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)qualorig_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported qualorig_node type: %d in IndexScan indexqualorig!", qualorig_node->type);
            }
        }
    }

    // update SubqueryScan
    if (IsA((*planTree), SubqueryScan)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update SubqueryScan");
#endif
        SubqueryScan *subquery_scan = (SubqueryScan *)(*planTree);
        // update Scan
        Scan *scan_node = &subquery_scan->scan;
        scan_node->scanrelid += current_rte_length;
        // update subplan
        if (NULL != subquery_scan->subplan) {
            UpdatePrevTreeIndex(&subquery_scan->subplan, current_rte_length, current_param_num);
        } else {
            elog(ERROR, "SubqueryScan's subplan is NULL!");
        }
    }

    // update NestLoop
    if (IsA((*planTree), NestLoop)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update NestLoop");
#endif
        NestLoop *nest_loop = (NestLoop *)(*planTree);
        ListCell *nest_param_lc;
        foreach(nest_param_lc, nest_loop->nestParams) {
            Node *nest_param_node = (Node *) lfirst(nest_param_lc);
            if (IsA(nest_param_node, NestLoopParam)) {
                NestLoopParam *nest_loop_param = (NestLoopParam *) nest_param_node;
                // update paramno
                nest_loop_param->paramno += current_param_num;
                if (IsA(nest_loop_param->paramval, Var)) {
                    Var *var_node = (Var *)nest_loop_param->paramval;
                    UpdateVarNodeIndex(&var_node, current_rte_length);
                } else if (IsA(nest_loop_param->paramval, Const)) {
                    continue;
                } else {
                    elog(ERROR, "Unsupported nest_loop_param->paramval type: %d in NestLoop NestLoopParam!", nest_loop_param->paramval->vartype);
                }
            } else {
                elog(ERROR, "Unsupported nest_param_node type: %d in NestLoop!", nest_param_node->type);
            }
        }
    }

    // update Hash
    if (IsA((*planTree), Hash)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update Hash");
#endif
        Hash *hash = (Hash *)(*planTree);
        ListCell *hash_lc;
        foreach(hash_lc, hash->hashkeys) {
            Node *hashkey_node = (Node *) lfirst(hash_lc);
            if (IsA(hashkey_node, Var)) {
                Var *var_node = (Var *)hashkey_node;
                UpdateVarNodeIndex(&var_node, current_rte_length);
            } else if (IsA(hashkey_node, Const)) {
                continue;
            } else {
                elog(ERROR, "Unsupported hashkey_node type: %d in Hash!", hashkey_node->type);
            }
        }
        // todo: update skewTable, skewColumn
//        if (hash->skewTable == removed_relation_oid) {
//            elog(LOG, "update skewTable, skewColumn");
//            int real_table_index = index_pairs[hash->skewColumn-1].varnoold;
//            hash->skewColumn = index_pairs[hash->skewColumn-1].varoattno;
//            // count from 0
//            hash->skewTable = list_nth_oid(prev_oids, real_table_index-1);
//        }
    }

    // update HashJoin
    if (IsA((*planTree), HashJoin)) {
#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "update HashJoin");
#endif
        HashJoin *hash_join = (HashJoin *)(*planTree);
        ListCell *hashclauses_lc;
        foreach(hashclauses_lc, hash_join->hashclauses) {
            Node *hashclauses_node = (Node *) lfirst(hashclauses_lc);
            if (IsA(hashclauses_node, OpExpr)) {
                OpExpr *op_expr = (OpExpr *)hashclauses_node;
                UpdateOpExprIndex(&op_expr, current_rte_length, current_param_num);
            } else {
                elog(ERROR, "Unsupported hashclauses_node type: %d in HashJoin!", hashclauses_node->type);
            }
        }
        ListCell *hashkey_lc;
        foreach(hashkey_lc, hash_join->hashkeys) {
            Node *hashkey_node = (Node *) lfirst(hashkey_lc);
            if (IsA(hashkey_node, Var)) {
                Var *var_node = (Var *)hashkey_node;
                UpdateVarNodeIndex(&var_node, current_rte_length);
            } else if (IsA(hashkey_node, Const)) {
                continue;
            } else {
                elog(ERROR, "Unsupported hashkey_node type: %d in HashJoin hashkeys!", hashkey_node->type);
            }
        }
    }
}

static void Recon(char* query_string, char* commandTag, Node* pstmt, Query* ori_query, char* completionTag)
{
	MemoryContext oldcontext = MemoryContextSwitchTo(MessageContext);
	Query* global_query = copyObjectImpl(ori_query);
	PlannedStmt* plannedstmt = NULL;
	if (global_query->commandType == CMD_UTILITY)
	{
		plannedstmt = QSOptimizer(global_query, NULL, NULL, 0);
		QSExecutor(query_string, commandTag, pstmt, plannedstmt, DestRemote, NULL, completionTag, NULL, NULL, NIL, oldcontext);
		return;
	}
	int length = global_query->rtable->length;
	if (length == 1)
	{
		plannedstmt = QSOptimizer(global_query, NULL, NULL, length);
#if MEASURE_TIME
        if (execute_plan_timer) {
            timespec opt_time = toc(&aqp_timer, "PG optimization time is", false);
            // save time to a file
            FILE *file = fopen("time_log.csv", "a");
            if (NULL == file) {
                printf("Error opening file\n");
                exit(-1);
            }
            fprintf(file, "%d.%09d, ", (int)opt_time.tv_sec, (int)opt_time.tv_nsec);
            fclose(file);
        }
#endif
		QSExecutor(query_string, commandTag, pstmt, plannedstmt, DestRemote, NULL, completionTag, NULL, NULL, NIL, oldcontext);
#if MEASURE_TIME || MERGE_SUB_PLANS
        FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "\n");
        fclose(file);
#endif
		return;
	}

#if MEASURE_TIME || MERGE_SUB_PLANS
    execute_plan_timer = true;
    aqp_timer = tic();
#endif
	List* RClist = NIL;
	List* Joinlist = NIL;
	List* WhereClause = NIL;
	switch (global_query->jointree->quals->type)
	{
		case T_BoolExpr:
		{
			BoolExpr* expr = (BoolExpr*)global_query->jointree->quals;
			WhereClause = expr->args;
			break;
		}
		case T_OpExpr:
		{
			WhereClause = lappend(WhereClause, global_query->jointree->quals);
			break;
		}
	}
	ListCell* lc;
	foreach(lc, WhereClause)
	{
		if (is_RC(lc->data.ptr_value))
			RClist = lappend(RClist, lc->data.ptr_value);
		else
			Joinlist = lappend(Joinlist, lc->data.ptr_value);
	}
	List* FKlist = grFK(global_query->rtable);
	//transfer join list to join graph
	bool* graph = List2Graph(is_relationship, Joinlist, FKlist, length);
#if DEBUG_QUERY_SPLIT
    printf("print join graph:\n");
    for (int i = 0; i < length; i++) {
        for (int j = 0; j < length; j++) {
            printf("%d, ", graph[i*length+j]);
        }
        printf("\n");
    }
#endif
	//value start from 1, index start from 0
	transfer_array = (Index*)palloc(length * sizeof(Index));
#if DumpSubQueryString
    const char *dir_path = "/home/pei/Project/duckdb/measure/postgres_plan";
    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
        if (mkdir(dir_path, 0700) != 0) {
            printf("Error: create directory postgres_plan failed!!!");
            exit(-1);
        }
    }

    char file_name[100];
    sprintf(file_name, "%s%s", dir_path, "/postgres_plan");
    remove(file_name);
#endif
//    PlannedStmt *merged_stmt = makeNode(PlannedStmt);
    // an array to store the unmerged subqueries, index start from 1
    PlannedStmt *temp_stmts[SUBQUERIES_NUM] = {NULL};
    // an array to store the index of unmerged subqueries
    Oid temp_table_id[SUBQUERIES_NUM] = {0};
	while (plannedstmt = QSOptimizer(global_query, graph, transfer_array, length))
	{
#if DumpSubQueryString
        FILE *file = fopen(file_name, "a");
        if (NULL == file) {
            printf("Error: failed to open file!!!");
            exit(-1);
        }

        if (fputs(nodeToString(plannedstmt), file) == EOF) {
            printf("Error: failed to write to file!!!");
            fclose(file);
            exit(-1);
        }
        fputs("\n", file);
        fclose(file);
//        printf("subquery optimized plan: %s\n", nodeToString(plannedstmt));
#endif
        queryId++;
#if MERGE_SUB_PLANS

#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "%dth plannedstmt: %s", queryId, nodeToString(plannedstmt));
#endif
        Oid current_temp_table_id[SUBQUERIES_NUM] = {0};
        locateTempId(plannedstmt, current_temp_table_id);
        int current_temp_table_num = 0;
        for (current_temp_table_num = 0; current_temp_table_num < SUBQUERIES_NUM; current_temp_table_num++) {
            if (0 == current_temp_table_id[current_temp_table_num]) {
                break;
            }
        }
        for (int i = queryId - 1; i < SUBQUERIES_NUM; i++) {
            if (0 == temp_table_id[i]) {
                for (int j = 0; j < current_temp_table_num; j++) {
                    temp_table_id[i] = current_temp_table_id[j];
                    i++;
                }
                break;
            }
        }

#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "temp_table_id[0]=%d", temp_table_id[0]);
        elog(LOG, "temp_table_id[1]=%d", temp_table_id[1]);
        elog(LOG, "temp_table_id[2]=%d", temp_table_id[2]);
#endif

        // Merge planTree if applicable (custom logic may be needed)
        if (0 == temp_table_id[0]) {
            // fixme: it can be improved by `map`
            if (queryId > SUBQUERIES_NUM) {
                elog(ERROR, "queryId is out of bound of SUBQUERIES_NUM!");
            }
            temp_stmts[queryId] = copyObjectImpl(plannedstmt);
#if DEBUG_MERGE_SUB_PLANS
            elog(LOG, "stored temp_stmts[%d]: %s", queryId, nodeToString(temp_stmts[queryId]));
#endif
        } else {
            int current_rte_length = plannedstmt->rtable->length;
            int current_param_num = plannedstmt->paramExecTypes ? plannedstmt->paramExecTypes->length : 0;
            for (int i = 0; (i < SUBQUERIES_NUM); i++) {
                if (0 == temp_table_id[i]) {
                    // skip if it doesn't have `temp` table
                    continue;
                }
                int table_id = temp_table_id[i];
                int subquery_id = i + 1;

#if DEBUG_MERGE_SUB_PLANS
                elog(LOG, "table_id=%d", table_id);
                elog(LOG, "used temp_stmts[%d]: %s", subquery_id, nodeToString(temp_stmts[subquery_id]));
#endif

                UpdatePrevTreeIndex(&temp_stmts[subquery_id]->planTree, current_rte_length, current_param_num);
                ReplaceTempScanNode(&plannedstmt->planTree, temp_stmts[subquery_id]->planTree, table_id);
                ModifyRtable(&plannedstmt->rtable, table_id);

                plannedstmt->rtable = list_concat(copyObjectImpl(plannedstmt->rtable), temp_stmts[subquery_id]->rtable);
                plannedstmt->resultRelations = list_concat(copyObjectImpl(plannedstmt->resultRelations), temp_stmts[subquery_id]->resultRelations);
                plannedstmt->subplans = list_concat(copyObjectImpl(plannedstmt->subplans), temp_stmts[subquery_id]->subplans);
                plannedstmt->relationOids = list_concat(copyObjectImpl(plannedstmt->relationOids), temp_stmts[subquery_id]->relationOids);
                plannedstmt->invalItems = list_concat(copyObjectImpl(plannedstmt->invalItems), temp_stmts[subquery_id]->invalItems);
                plannedstmt->paramExecTypes = list_concat(copyObjectImpl(plannedstmt->paramExecTypes), temp_stmts[subquery_id]->paramExecTypes);
                plannedstmt->rewindPlanIDs = bms_union(copyObjectImpl(plannedstmt->rewindPlanIDs), temp_stmts[subquery_id]->rewindPlanIDs);

                current_rte_length += temp_stmts[subquery_id]->rtable->length;
                current_param_num += temp_stmts[subquery_id]->paramExecTypes ? temp_stmts[subquery_id]->paramExecTypes->length : 0;

                // reset once it is used
                temp_table_id[i] = 0;
            }
            temp_stmts[queryId] = copyObjectImpl(plannedstmt);
        }

#if DEBUG_MERGE_SUB_PLANS
        elog(LOG, "%dth merged_stmt: %s", queryId, nodeToString(plannedstmt));
#endif

#endif

		char* relname = NULL;
		//Should we output the result or save it as a temporary table
		if (mydest == DestIntoRel)
		{
			relname = palloc(7 * sizeof(char));
			sprintf(relname, "temp%d", queryId);
		}

		//Execute the subquery and do some change for next subquery creation
		FKlist = QSExecutor(query_string, commandTag, pstmt, plannedstmt, mydest, relname, completionTag, global_query, transfer_array, FKlist, oldcontext);
		//finish_xact_command();
		if (mydest == DestRemote)
		{
#if DEBUG_TOTAL_SIZE
            elog(INFO, "%d\t%lf\n", queryId - 1, total_size);
#endif
			break;
		}
		switch (global_query->jointree->quals->type)
		{
			case T_BoolExpr:
			{
				BoolExpr* expr = (BoolExpr*)global_query->jointree->quals;
				WhereClause = expr->args;
				break;
			}
			case T_OpExpr:
			{
				WhereClause = lappend(WhereClause, global_query->jointree->quals);
				break;
			}
		}
		Joinlist = NIL;
		ListCell* lc;
		foreach(lc, WhereClause)
		{
			if (!is_RC(lc->data.ptr_value))
				Joinlist = lappend(Joinlist, lc->data.ptr_value);
		}
		length = global_query->rtable->length;
		graph = List2Graph(is_relationship, Joinlist, FKlist, length);
#if MEASURE_TIME
        if (execute_plan_timer) {
            timespec post_aqp_time = toc(&aqp_timer, "post-AQP time is", false);
            // save time to a file
            FILE *file = fopen("time_log.csv", "a");
            if (NULL == file) {
                printf("Error opening file\n");
                exit(-1);
            }
            fprintf(file, "%d.%09d, ", (int)post_aqp_time.tv_sec, (int)post_aqp_time.tv_nsec);
            fclose(file);
        }
#endif
	}
#if MEASURE_TIME || MERGE_SUB_PLANS
    FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "\n");
        fclose(file);
#endif
	pfree(transfer_array);
	pfree(graph);
	transfer_array = NULL;
	graph = NULL;
	return;
}

//Planner
static PlannedStmt* QSOptimizer(Query* global_query, bool* graph, Index* transfer_array, int length)
{
	PlannedStmt* result = NULL;
	//start_xact_command();
	int remain = hasNext(graph, length);
	int X = 0, Y = 0;
	Index rels[2] = { 0, 0 };
	if(query_splitting_algorithm == RelationshipCenter || query_splitting_algorithm == EntityCenter)
	{
		if (order_decision == global_view)
		{
			PlannedStmt* temp = planner(copyObjectImpl(global_query), CURSOR_OPT_PARALLEL_OK, NULL);
			int leaf_has = 0, depth = 0;
			Plan* temp_plan = find_node_with_nleaf_recursive(temp->planTree, 2, &leaf_has, &depth);
			walk_plantree(temp_plan, rels);
			rels[0] = ((RangeTblEntry*)list_nth(global_query->rtable, rels[0] - 1))->relid;
			rels[1] = ((RangeTblEntry*)list_nth(global_query->rtable, rels[1] - 1))->relid;
		}
		for (int i = 0; i < length; i++)
		{
			if (remain > 1)
				mydest = DestIntoRel;
			else if (remain == 1)
				mydest = DestRemote;
			else if (remain == 0)
				return NULL;
			for (int j = 0; j < length; j++)
				transfer_array[j] = 0;
			//Get the rang table list for this subgraph
			List* rtable = getRT_2(global_query->rtable, graph, length, i, transfer_array);
			//Can this subgraph make a join ?
			if (rtable->length < 2)
			{
				continue;
			}
			char* relname = NULL;
			//If so, create a subquery
			Query* local_query = createQuery(global_query, mydest, rtable, transfer_array, length);
			PlannedStmt* candidate_result = planner(local_query, CURSOR_OPT_PARALLEL_OK, NULL);
			if (tarfunc(rels, candidate_result, result) == NEWBETTER)
			{
				if(result)
					pfree(result);
				result = candidate_result;
				X = i;
			}
			else
			{
				pfree(candidate_result);
			}
			candidate_result = NULL;
		}
	}
	else if (query_splitting_algorithm == Minsubquery)
	{
		if (order_decision == global_view)
		{
			PlannedStmt* temp = planner(copyObjectImpl(global_query), CURSOR_OPT_PARALLEL_OK, NULL);
			int leaf_has = 0, depth = 0;
			Plan* temp_plan = find_node_with_nleaf_recursive(temp->planTree, 2, &leaf_has, &depth);
			walk_plantree(temp_plan, rels);
			rels[0] = ((RangeTblEntry*)list_nth(global_query->rtable, rels[0] - 1))->relid;
			rels[1] = ((RangeTblEntry*)list_nth(global_query->rtable, rels[1] - 1))->relid;
		}
		for (int i = 0; i < length; i++)
		{
			for (int j = i + 1; j < length; j++)
			{
				if (remain > 1)
					mydest = DestIntoRel;
				else if (remain == 1)
					mydest = DestRemote;
				else if (remain == 0)
					return NULL;
				for (int j = 0; j < length; j++)
					transfer_array[j] = 0;
				//Get the rang table list for this subgraph
				List* rtable = getRT_1(global_query->rtable, graph, length, i, j, transfer_array);
				//Can this subgraph make a join ?
				if (rtable == NIL)
				{
					continue;
				}
				char* relname = NULL;
				//If so, create a subquery
				Query* local_query = createQuery(global_query, mydest, rtable, transfer_array, length);
				PlannedStmt* candidate_result = planner(local_query, CURSOR_OPT_PARALLEL_OK, NULL);
				if (tarfunc(rels, candidate_result, result) == NEWBETTER)
				{
					if(result)
						pfree(result);
					result = candidate_result;
					X = i;
					Y = j;
				}
				else
				{
					pfree(candidate_result);
				}
				candidate_result = NULL;
			}
		}
	}
	for (int j = 0; j < length; j++)
		transfer_array[j] = 0;
	if (query_splitting_algorithm == RelationshipCenter || query_splitting_algorithm == EntityCenter)
	{
		Index index = 1;
		for (int j = 0; j < length; j++)
		{
			if (graph[X * length + j] == true)
			{
				transfer_array[j] = index++;
			}
			else if (X == j)
			{
				transfer_array[j] = index++;
			}
			else
			{
				transfer_array[j] = 0;
			}
		}
		for (int j = 0; j < length; j++)
		{
			if (graph[X * length + j] == true)
			{
				graph[X * length + j] = false;
				graph[j * length + X] = false;
			}
		}
	}
	else if (query_splitting_algorithm == Minsubquery)
	{
		for (int j = 0; j < length; j++)
			transfer_array[j] = 0;
		transfer_array[X] = 1;
		transfer_array[Y] = 2;
		graph[X * length + Y] = false;
		for (int i = 0; i < length; i++)
		{
			if (i < X)
			{
				if (graph[i * length + X] == true && graph[i * length + Y] == true)
				{
					graph[i * length + X] = false;
				}
			}
			else if (i > X && i < Y)
			{
				if (graph[X * length + i] == true && graph[i * length + Y] == true)
				{
					graph[X * length + i] = false;
				}
			}
			else if (i > Y)
			{
				if (graph[X * length + i] == true && graph[Y * length + i] == true)
				{
					graph[X * length + i] = false;
				}
			}
		}
	}
	switch (global_query->jointree->quals->type)
	{
		case T_BoolExpr:
		{
			((BoolExpr*)global_query->jointree->quals)->args = simplifyjoinlist(((BoolExpr*)global_query->jointree->quals)->args, mydest, transfer_array, graph, length);
			break;
		}
		case T_OpExpr:
		{
			global_query->jointree->quals = NULL;
			break;
		}
	}
#if MEASURE_TIME
    if (execute_plan_timer) {
        timespec opt_time = toc(&aqp_timer, "PG optimization time is", false);
        // save time to a file
        FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "%d.%09d, ", (int)opt_time.tv_sec, (int)opt_time.tv_nsec);
        fclose(file);
    }
#endif
	return result;
}

//Executor
static List* QSExecutor(char* query_string, const char* commandTag, Node* pstmt, PlannedStmt* plannedstmt, CommandDest dest, char* relname, char* completionTag, Query* querytree, Index* transfer_array, List* FKlist, MemoryContext oldcontext)
{
	Oid relid;
	int16 format;
	Portal portal;
	List* plantree_list;
	DestReceiver* receiver = NULL;
	bool is_parallel_worker = false;
	BeginCommand(commandTag, dest);
	plantree_list = lappend(NIL, plannedstmt);
	CHECK_FOR_INTERRUPTS();
	portal = CreatePortal("", true, true);
	portal->visible = false;
	PortalDefineQuery(portal, NULL, query_string, commandTag, plantree_list, NULL);
//	PortalStart(portal, NULL, 0, SnapshotAny);
    PortalStart(portal, NULL, 0, InvalidSnapshot);
	format = 0;
	PortalSetResultFormat(portal, 1, &format);
	if (dest == DestRemote)
	{
		receiver = CreateDestReceiver(dest);
		SetRemoteDestReceiverParams(receiver, portal);
	}
	if (dest == DestIntoRel)
	{
		IntoClause* into = makeNode(IntoClause);
		into->rel = makeRangeVar(NULL, relname, plannedstmt->stmt_location);
		into->rel->relpersistence = RELPERSISTENCE_TEMP;
		into->onCommit = ONCOMMIT_NOOP;
		//into->onCommit = ONCOMMIT_DROP;
		into->rel->inh = false;
		into->skipData = false;
		into->viewQuery = NULL;
		receiver = CreateIntoRelDestReceiver(into);
	}
	MemoryContextSwitchTo(oldcontext);
#if MEASURE_TIME || MERGE_SUB_PLANS
    aqp_timer = tic();
#endif
	//Executor
	(void)PortalRun(portal, FETCH_ALL, true, true, receiver, receiver, completionTag);
#if MEASURE_TIME || MERGE_SUB_PLANS
    if (execute_plan_timer) {
        timespec exe_time = toc(&aqp_timer, "Execution time is", false);
        // save time to a file
        FILE *file = fopen("time_log.csv", "a");
        if (NULL == file) {
            printf("Error opening file\n");
            exit(-1);
        }
        fprintf(file, "%d.%09d, ", (int)exe_time.tv_sec, (int)exe_time.tv_nsec);
        fclose(file);
        aqp_timer = tic();
    }
#endif
    if (dest == DestIntoRel) {
#if MANUAL_ANALYZE
        VacuumParams params;
        params.index_cleanup = VACOPT_TERNARY_DEFAULT;
        params.truncate = VACOPT_TERNARY_DEFAULT;
        params.options = VACOPT_ANALYZE;
        params.freeze_min_age = -1;
        params.freeze_table_age = -1;
        params.multixact_freeze_min_age = -1;
        params.multixact_freeze_table_age = -1;
        params.is_wraparound = false;
        params.log_min_duration = -1;
        Oid relid = RangeVarGetRelid(((DR_intorel*)receiver)->into->rel, NoLock, true);
        analyze_rel(relid, ((DR_intorel*)receiver)->into->rel, &params, NIL, false, NULL);
        MemoryContext context = MemoryContextSwitchTo(MessageContext);

#if MEASURE_TIME
        if (execute_plan_timer) {
            timespec analyze_time = toc(&aqp_timer, "Analyze time is", false);
            // save time to a file
            FILE *file = fopen("time_log.csv", "a");
            if (NULL == file) {
                printf("Error opening file\n");
                exit(-1);
            }
            fprintf(file, "%d.%09d, ", (int)analyze_time.tv_sec, (int)analyze_time.tv_nsec);
            fclose(file);
            aqp_timer = tic();
        }
#endif

#endif

        FKlist = Prepare4Next(querytree, transfer_array, (DR_intorel*)receiver, plannedstmt, relname, FKlist);

#if MANUAL_ANALYZE
        #if DEBUG_TOTAL_SIZE
        double s = getMatSize(relid);
        total_size += s;
        #endif
        MemoryContextSwitchTo(context);
#endif
    }
	receiver->rDestroy(receiver);
	PortalDrop(portal, false);
//	EndCommand(completionTag, dest);
	return FKlist;
}

static List* Prepare4Next(Query* global_query, Index* transfer_array, DR_intorel* receiver, PlannedStmt* plannedstmt, char* relname, List* FKlist)
{
	int length = global_query->rtable->length;
	int X = -1;
	int before = 0;
	for (int i = 0; i < length; i++)
	{
		if (X == -1 && transfer_array[i] == 0)
		{
			continue;
		}
		else if (X == -1 && transfer_array[i] != 0)
		{
			X = i;
			before = 0;
			if (query_splitting_algorithm == RelationshipCenter)
				is_relationship[i - before] = false;
			else if (query_splitting_algorithm == EntityCenter)
				is_relationship[i - before] = true;
		}
		else if (transfer_array[i] != 0)
		{
			before++;
		}
		else if (transfer_array[i] == 0)
		{
			if (query_splitting_algorithm == RelationshipCenter || query_splitting_algorithm == EntityCenter)
				is_relationship[i - before] = is_relationship[i];
		}
	}
	ListCell* lc;
	ListCell* prev = NULL;
	foreach(lc, FKlist)
	{
		bool flag = false;
		ForeignKeyOptInfo* fkOptInfo = (ForeignKeyOptInfo*)lfirst(lc);
		int x = fkOptInfo->con_relid - 1;
		int y = fkOptInfo->ref_relid - 1;
		if (transfer_array[x] != 0 && transfer_array[y] != 0)
		{
			FKlist = list_delete_cell(FKlist, lc, prev);
		}
		else if (transfer_array[x] != 0)
		{
			fkOptInfo->con_relid = X + 1;
			prev = lc;
		}
		else if (transfer_array[y] != 0)
		{
			fkOptInfo->ref_relid = X + 1;
			prev = lc;
		}
		else
		{
			prev = lc;
		}
	}
	
	Oid relid = RangeVarGetRelid(receiver->into->rel, NoLock, true);
	Relation relation = table_open(relid, NoLock);
	List* varlist = pull_var_clause((Node*)global_query->jointree, 0);
	foreach(lc, varlist)
	{
		Var* var = (Var*)lfirst(lc);
		if (transfer_array[var->varnoold - 1] != 0)
		{
			RangeTblEntry* rte = (RangeTblEntry*)list_nth(global_query->rtable, var->varnoold - 1);
			int len = strlen(rte->eref->aliasname) + strlen(strVal(list_nth(rte->eref->colnames, var->varoattno - 1))) + 2;
			char* attrname = (char*)palloc(len * sizeof(char));
			sprintf(attrname, "%s_%s", rte->eref->aliasname, strVal(list_nth(rte->eref->colnames, var->varoattno - 1)));
			var->varno = X + 1;
			var->varnoold = var->varno;
			for (int i = 0; i < relation->rd_att->natts; i++)
			{
				if (strcmp(attrname, relation->rd_att->attrs[i].attname.data) == 0)
				{
					var->varattno = i + 1;
					var->varoattno = var->varattno;
					break;
				}
			}
			pfree(attrname);
			attrname = NULL;
		}
		else
		{
			int before = 0;
			for (int i = X + 1; i < var->varno - 1; i++)
			{
				if (transfer_array[i] != 0)
				{
					before++;
				}
			}
			var->varno -= before;
			var->varnoold = var->varno;
		}
	}
	foreach(lc, FKlist)
	{
		ForeignKeyOptInfo* fkOptInfo = (ForeignKeyOptInfo*)lfirst(lc);
		int before = 0;
		for (int i = X + 1; i < fkOptInfo->con_relid - 1; i++)
		{
			if (transfer_array[i] != 0)
			{
				before++;
			}
		}
		fkOptInfo->con_relid -= before;
		before = 0;
		for (int i = X + 1; i < fkOptInfo->ref_relid - 1; i++)
		{
			if (transfer_array[i] != 0)
			{
				before++;
			}
		}
		fkOptInfo->ref_relid -= before;
	}
	// The global relation involved in the subquery
	for (int i = length - 1; i > X; i--)
	{
		if (transfer_array[i] != 0)
		{
			RangeTblEntry* rte = list_nth(global_query->rtable, i);
			global_query->rtable = list_delete(global_query->rtable, list_nth(global_query->rtable, i));
			global_query->jointree->fromlist = list_delete(global_query->jointree->fromlist, list_nth(global_query->jointree->fromlist, i));
		}
	}
	RangeTblEntry* rte = (RangeTblEntry*)list_nth(global_query->rtable, X);
	dochange(rte, relname, relation, relid);
	Index index = 1;
	foreach(lc, global_query->jointree->fromlist)
	{
		RangeTblRef* rtr = (RangeTblRef*)lfirst(lc);
		rtr->rtindex = index++;
	}
	foreach(lc, global_query->targetList)
	{
		TargetEntry* tar = (TargetEntry*)lfirst(lc);
		Var* vtar = NULL;
		if (tar->expr->type == T_Aggref)
		{
			TargetEntry* te = lfirst(((Aggref*)tar->expr)->args->head);
			if (te->expr->type == T_Var)
				vtar = te->expr;
			else if (te->expr->type == T_RelabelType)
				vtar = (Var*)((RelabelType*)te->expr)->arg;
		}
		else if (tar->expr->type == T_RelabelType)
		{
			vtar = (Var*)((RelabelType*)tar->expr)->arg;
		}
		else if(tar->expr->type == T_Var)
		{
			vtar = (Var*)tar->expr;
		}
		if (vtar == NULL)
			continue;
		if (transfer_array[vtar->varnoold - 1] != 0)
		{
			tar->resorigtbl = relid;
			for (int i = 0; i < relation->rd_att->natts; i++)
			{
				if (strcmp(tar->resname, relation->rd_att->attrs[i].attname.data) == 0)
				{
					tar->resorigcol = i + 1;
					vtar->varattno = i + 1;
					vtar->varno = X + 1;
					vtar->varoattno = vtar->varattno;
					vtar->varnoold = vtar->varno;
					break;
				}
			}
		}
		else
		{
			int before = 0;
			for (int i = X + 1; i < vtar->varno; i++)
			{
				if (transfer_array[i] != 0)
				{
					before++;
				}
			}
			vtar->varno = vtar->varno - before;
			vtar->varnoold = vtar->varno;
		}
	}
	table_close(relation, NoLock);
	return FKlist;
}

//transfer joinlist to join graph
static bool* List2Graph(bool* is_relationship, List* joinlist, List* FKlist, int length)
{
	bool* graph = (bool*)palloc(length * length * sizeof(bool));
	memset(graph, false, length * length * sizeof(bool));
	ListCell* lc1;
	if (query_splitting_algorithm == RelationshipCenter || query_splitting_algorithm == EntityCenter)
	{
		foreach(lc1, FKlist)
		{
			bool flag = false;
			ForeignKeyOptInfo* fkOptInfo = (ForeignKeyOptInfo*)lc1->data.ptr_value;
			int x = fkOptInfo->con_relid - 1;
			int y = fkOptInfo->ref_relid - 1;
			ListCell* lc2;
			foreach(lc2, joinlist)
			{
				Var* var1 = (Var*)lfirst(((OpExpr*)lfirst(lc2))->args->head);
				Var* var2 = (Var*)lfirst(((OpExpr*)lfirst(lc2))->args->head->next);
				if (var1->varno - 1 == x && var2->varno - 1 == y)
				{
					flag = true;
					joinlist = list_delete(joinlist, lfirst(lc2));
				}
				else if (var1->varno - 1 == y && var2->varno - 1 == x)
				{
					flag = true;
					joinlist = list_delete(joinlist, lfirst(lc2));
				}
			}
			if (!flag)
				continue;
			if (query_splitting_algorithm == RelationshipCenter)
			{
				graph[x * length + y] = true;
			}
			else if (query_splitting_algorithm == EntityCenter)
			{
				graph[y * length + x] = true;
			}
		}
	}
	foreach(lc1, joinlist)
	{
		Var* var1 = (Var*)lfirst(((OpExpr*)lfirst(lc1))->args->head);
		Var* var2 = (Var*)lfirst(((OpExpr*)lfirst(lc1))->args->head->next);
		if (query_splitting_algorithm == Minsubquery)
		{
			if (var1->varno > var2->varno)
			{
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
			else if (var1->varno < var2->varno)
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
			}
		}
		else if (query_splitting_algorithm == RelationshipCenter)
		{
			if ((is_relationship[var1->varno - 1] == true) && (is_relationship[var2->varno - 1] == false))
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
			}
			else if ((is_relationship[var1->varno - 1] == false) && (is_relationship[var2->varno - 1] == true))
			{
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
			else if ((is_relationship[var1->varno - 1] == false) && (is_relationship[var2->varno - 1] == false))
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
			else
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
		}
		else if(query_splitting_algorithm == EntityCenter)
		{
			if ((is_relationship[var1->varno - 1] == false) && (is_relationship[var2->varno - 1] == true))
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
			}
			else if ((is_relationship[var1->varno - 1] == true) && (is_relationship[var2->varno - 1] == false))
			{
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
			else if ((is_relationship[var1->varno - 1] == false) && (is_relationship[var2->varno - 1] == false))
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
			else
			{
				graph[(var1->varno - 1) * length + var2->varno - 1] = true;
				graph[(var2->varno - 1) * length + var1->varno - 1] = true;
			}
		}
	}
	return graph;
}

static bool is_ER(OpExpr* opexpr, bool* is_relationship, int length)
{
	Var* var1 = (Var*)lfirst(opexpr->args->head);
	Var* var2 = (Var*)lfirst(opexpr->args->head->next);
	if (is_relationship[var1->varno - 1] && is_relationship[var2->varno - 1])
		return false;
	else if (is_relationship[var1->varno - 1] || is_relationship[var2->varno - 1])
		return true;
	else
		return false;
}

static bool is_FK(OpExpr* opexpr, List* fklist)
{
	ListCell* lc = NULL;
	Var* var1 = (Var*)lfirst(opexpr->args->head);
	Var* var2 = (Var*)lfirst(opexpr->args->head->next);
	foreach(lc, fklist)
	{
		ForeignKeyOptInfo* fkOptInfo = (ForeignKeyOptInfo*)lfirst(lc);
		if (var1->varno == fkOptInfo->con_relid && var2->varno == fkOptInfo->ref_relid)
			return true;
		else if (var2->varno == fkOptInfo->con_relid && var1->varno == fkOptInfo->ref_relid)
			return true;
	}
	return false;
}

static bool is_2relationship(OpExpr* opexpr, bool* is_relationship, int length)
{
	Var* var1 = (Var*)lfirst(opexpr->args->head);
	Var* var2 = (Var*)lfirst(opexpr->args->head->next);
	if (is_relationship[var1->varno - 1] && is_relationship[var2->varno - 1])
		return true;
	return false;
}

//Expr is a filter clause?
static bool is_RC(Expr* expr)
{
	if (expr->type != T_OpExpr)
		return true;
	OpExpr* opexpr = (OpExpr*)expr;
	return (((Node*)lfirst(opexpr->args->head->next))->type == T_Const);
}

//get rtable
static List* getRT_1(List* prtable, bool* graph, int length, int i, int j, Index* transfer_array)
{
	if (graph[i * length + j] == true)
	{
		List* rtable = NIL;
		RangeTblEntry* rte_i = copyObjectImpl(list_nth(prtable, i));
		RangeTblEntry* rte_j = copyObjectImpl(list_nth(prtable, j));
		rtable = lappend(rtable, rte_i);
		rtable = lappend(rtable, rte_j);
		transfer_array[i] = 1;
		transfer_array[j] = 2;
		return rtable;
	}
	return NIL;
}

static List* getRT_2(List* prtable, bool* graph, int length, int i, Index* transfer_array)
{
	Index index = 1;
	List* rtable = NIL;
	for (int j = 0; j < length; j++)
	{
		//graph[x][y]
		if (graph[i * length + j] == true)
		{
			RangeTblEntry* rte = copyObjectImpl(list_nth(prtable, j));
			rtable = lappend(rtable, rte);
			transfer_array[j] = index++;
		}
		else if (i == j)
		{
			RangeTblEntry* rte = copyObjectImpl(list_nth(prtable, i));
			rtable = lappend(rtable, rte);
			transfer_array[j] = index++;
		}
	}
	return rtable;
}

// Find the global exit
static List* findvarlist(List* joinlist, Index* transfer_array, int length)
{
	ListCell* lc;
	List* reslist = NIL;
	foreach(lc, joinlist)
	{
		Expr* expr = (Expr*)lfirst(lc);
		if (!is_RC(expr))
		{
			OpExpr* opexpr = (OpExpr*)expr;
			NodeTag type = ((Node*)lfirst(opexpr->args->head))->type;
			Var* var1 = lfirst(opexpr->args->head);
			Var* var2 = (Var*)lfirst(opexpr->args->head->next);
			// Current query to the periphery
			if (transfer_array[var1->varno - 1] != 0 && transfer_array[var2->varno - 1] == 0)
			{
				ListCell* lc1;
				bool append = true;
				foreach(lc1, reslist)
				{
					Var* var = (Var*)lfirst(lc1);
					if (var->varattno == var1->varattno && var->varno == var1->varno)
					{
						append = false;
						break;
					}
				}
				if (append)
				{
					Var* var = copyObjectImpl(var1);
					reslist = lappend(reslist, var);
				}
			}
			else if (transfer_array[var1->varno - 1] == 0 && transfer_array[var2->varno - 1] != 0)
			{
				ListCell* lc1;
				bool append = true;
				foreach(lc1, reslist)
				{
					Var* var = (Var*)lfirst(lc1);
					if (var->varattno == var2->varattno && var->varno == var2->varno)
					{
						append = false;
						break;
					}
				}
				if (append)
				{
					Var* var = copyObjectImpl(var2);
					reslist = lappend(reslist, var);
				}
			}
		}
	}
	return reslist;
}

static Query* createQuery(const Query* global_query, CommandDest dest, List* rtable, Index* transfer_array, int length)
{
	Query* query;
	query = makeNode(Query);
	query = copyObjectImpl(global_query);
	query->rtable = copyObjectImpl(rtable);
	query->jointree->fromlist = setfromlist(query->jointree->fromlist, transfer_array, length);
	List* varlist = NIL;
	switch (query->jointree->quals->type)
	{
		case T_BoolExpr:
		{
			varlist = findvarlist(((BoolExpr*)query->jointree->quals)->args, transfer_array, length);
			break;
		}
		case T_OpExpr:
		{
			List* temp = lappend(NIL, query->jointree->quals);
			varlist = findvarlist(temp, transfer_array, length);
			break;
		}
	}
	query->targetList = settargetlist(global_query->rtable, rtable, dest, varlist, query->targetList, transfer_array, length);
	switch (query->jointree->quals->type)
	{
		case T_BoolExpr:
		{
			((BoolExpr*)query->jointree->quals)->args = setjoinlist(((BoolExpr*)query->jointree->quals)->args, dest, transfer_array, length);
			break;
		}
		case T_OpExpr:
			break;
	}
	if (dest == DestRemote)
	{
		query->hasAggs = global_query->hasAggs;
	}
	else
	{
		query->hasAggs = false;
	}
	return query;
}

static bool doNullTestTransfor(NullTest* expr, Index* transfer_array)
{
	NodeTag type = nodeTag(expr->arg);
	Var* var = NULL;
	if (type == T_Var)
		var = (Var*)expr->arg;
	else if (type == T_RelabelType)
		var = (Var*)((RelabelType*)expr->arg)->arg;
	if (transfer_array[var->varno - 1] == 0)
		return false;
	var->varno = transfer_array[var->varno - 1];
	var->varnoold = var->varno;
	return true;
}

static bool doOpExprTransfor(OpExpr* expr, Index* transfer_array)
{
	bool flag = true;
	NodeTag type1 = ((Node*)lfirst(expr->args->head))->type;
	Var* var1 = NULL;
	Var* var2 = NULL;
	if (type1 == T_Var)
	{
		var1 = (Var*)lfirst(expr->args->head);
	}
	else if (type1 == T_RelabelType)
	{
		var1 = (Var*)((RelabelType*)lfirst(expr->args->head))->arg;
	}
	NodeTag type2 = ((Node*)lfirst(expr->args->head->next))->type;
	if (type2 == T_Var)
	{
		var2 = (Var*)lfirst(expr->args->head->next);
	}
	else if (type2 == T_RelabelType)
	{
		var2 = (Var*)((RelabelType*)lfirst(expr->args->head->next))->arg;
	}
	if (var1 && transfer_array[var1->varno - 1] == 0)
	{
		flag = false;
	}
	else if (var1)
	{
		var1->varno = transfer_array[var1->varno - 1];
		var1->varnoold = var1->varno;
	}
	if (var2 && transfer_array[var2->varno - 1] == 0)
	{
		flag = false;
	}
	else if (var2)
	{
		var2->varno = transfer_array[var2->varno - 1];
		var2->varnoold = var2->varno;
	}
	return flag;
}

static bool doScalarArrayOpExprTransfor(ScalarArrayOpExpr* expr, Index* transfer_array)
{
	NodeTag type = nodeTag(lfirst(expr->args->head));
	Var* var = NULL;
	if (type == T_Var)
		var = (Var*)lfirst(expr->args->head);
	else if (type == T_RelabelType)
		var = (Var*)((RelabelType*)lfirst(expr->args->head))->arg;
	if (transfer_array[var->varno - 1] == 0)
		return false;
	var->varno = transfer_array[var->varno - 1];
	var->varnoold = var->varno;
	return true;
}

static List* setjoinlist(List* qualslist, CommandDest dest, Index* transfer_array, int length)
{
	ListCell* lc;
	foreach(lc, qualslist)
	{
		Expr* expr = (Expr*)lfirst(lc);
		bool flag = true;
		switch (expr->type)
		{
			case T_NullTest:
			{
				NullTest* nulltest = (NullTest*)expr;
				flag = doNullTestTransfor(nulltest, transfer_array);
				break;
			}
			case T_OpExpr:
			{
				OpExpr* opexpr = (OpExpr*)expr;
				flag = doOpExprTransfor(opexpr, transfer_array);
				break;
			}
			case T_ScalarArrayOpExpr:
			{
				ScalarArrayOpExpr* scalararrayopexpr = (ScalarArrayOpExpr*)expr;
				flag = doScalarArrayOpExprTransfor(scalararrayopexpr, transfer_array);
				break;
			}
			case T_BoolExpr:
			{
				BoolExpr* boolexpr = (BoolExpr*)expr;
				boolexpr->args = setjoinlist(boolexpr->args, dest, transfer_array, length);
				if (boolexpr->args == NULL)
					flag = false;
				break;
			}
		}
		if (!flag)
		{
			qualslist = list_delete(qualslist, lfirst(lc));
		}
	}
	return qualslist;
}

static List* simplifyjoinlist(List* list, CommandDest dest, Index* transfer_array, bool* graph, int length)
{
	ListCell* lc;
	foreach(lc, list)
	{
		Expr* expr = (Expr*)lfirst(lc);
		if (is_RC(expr))
		{
			List* varlist = pull_var_clause(expr, 0);
			Var* var = (Var*)lfirst(varlist->head);
			if (transfer_array[var->varno - 1] != 0)
				list = list_delete(list, lfirst(lc));
		}
		else
		{
			bool flag = false;
			Index X = 0, Y = 0;
			OpExpr* opexpr = (OpExpr*)expr;
			Var* var1 = (Var*)lfirst(opexpr->args->head);
			Var* var2 = (Var*)lfirst(opexpr->args->head->next);
			Assert(var1->varno != var2->varno);
			X = var1->varno - 1;
			Y = var2->varno - 1;
			if (graph[X * length + Y] == false && graph[Y * length + X] == false)
			{
				list = list_delete(list, lfirst(lc));
			}
		}
	}
	return list;
}

static List* setfromlist(List* fromlist, Index* transfer_array, int length)
{
	ListCell* lc;
	foreach(lc, fromlist)
	{
		RangeTblRef* ref = (RangeTblRef*)lfirst(lc);
		if (transfer_array[ref->rtindex - 1] == 0)
		{
			fromlist = list_delete(fromlist, lfirst(lc));
			continue;
		}
		ref->rtindex = transfer_array[ref->rtindex - 1];
	}
	return fromlist;
}

//varlist - global, targetlist - global
static List* settargetlist(const List* global_rtable, List* local_rtable, CommandDest dest, List* varlist, List* targetlist, Index* transfer_array, int length)
{
	ListCell* lc;
	if (dest != DestRemote)
	{
		targetlist = removeAggref(targetlist);
	}
	foreach(lc, targetlist)
	{
		bool reserved = false;
		TargetEntry* tar = (TargetEntry*)lfirst(lc);
		ListCell* lc1;
		if (tar->expr->type == T_Aggref)
		{
			continue;
		}
		foreach(lc1, local_rtable)
		{
			RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc1);
			Var* vtar = NULL;
			if(tar->expr->type == T_Var)
				vtar = (Var*)tar->expr;
			else if (tar->expr->type == T_RelabelType)
				vtar = (Var*)((RelabelType*)tar->expr)->arg;
			if (vtar == NULL)
				continue;
			RangeTblEntry* rte_1 = list_nth(global_rtable, vtar->varno - 1);
			if (rte->relid == rte_1->relid)
			{
				vtar->varno = transfer_array[vtar->varno - 1];
				vtar->varnoold = vtar->varno;
				reserved = true;
				break;
			}
		}
		if (reserved)
			continue;
		targetlist = list_delete(targetlist, lfirst(lc));
	}
	foreach(lc, varlist)
	{
		Var* var = (Var*)lfirst(lc);
		if (var != NULL)
		{
			TargetEntry* tar = makeNode(TargetEntry);
			RangeTblEntry* rte = (RangeTblEntry*)list_nth(global_rtable, var->varno - 1);
			tar->resorigtbl = rte->relid;
			int len = strlen(rte->eref->aliasname) + strlen(strVal(list_nth(rte->eref->colnames, var->varattno - 1))) + 2;
			tar->resname = (char*)palloc(len * sizeof(char));
			sprintf(tar->resname, "%s_%s", rte->eref->aliasname, strVal(list_nth(rte->eref->colnames, var->varattno - 1)));
			tar->resorigcol = var->varattno;
			if(targetlist)
				tar->resno = targetlist->length + 1;
			else
				tar->resno = 1;
			// The table where the variable is located directly participates in this join
			if (transfer_array[var->varno - 1] != 0)
			{
				var->varno = transfer_array[var->varno - 1];
				var->varnoold = var->varno;
			}
			// The table where the variable is located indirectly participates in this join
			else
			{
				for (int i = 0; i < length; i++)
				{
					if (transfer_array[i] != 0)
					{
						var->varno = transfer_array[i];
						var->varnoold = var->varno;
						break;
					}
				}
			}
			tar->expr = copyObjectImpl(var);
			targetlist = lappend(targetlist, tar);
		}
	}
	return targetlist;
}

//get relation foreign key
static List* grFK(List* rtable)
{
	ListCell* lc;
	List* fkey_list = NIL;
	Index relid = 0;
	foreach(lc, rtable)
	{
		relid++;
		RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc);
		if (rte->relid == 0)
			continue;
		Relation relation;
		relation = table_open(rte->relid, NoLock);
		List* cachedfkeys;
		ListCell* lc1;
		cachedfkeys = RelationGetFKeyList(relation);
		foreach(lc1, cachedfkeys)
		{
			ForeignKeyCacheInfo* cachedfk = (ForeignKeyCacheInfo*)lfirst(lc1);
			Index rti;
			ListCell* lc2;
			Assert(cachedfk->conrelid == RelationGetRelid(relation));
			rti = 0;
			foreach(lc2, rtable)
			{
				RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc2);
				ForeignKeyOptInfo* info;
				rti++;
				if (rte->rtekind != RTE_RELATION || rte->relid != cachedfk->confrelid)
					continue;
				if (rti == relid)
					continue;
				/* OK, let's make an entry */
				info = makeNode(ForeignKeyOptInfo);
				info->con_relid = relid;
				info->ref_relid = rti;
				info->nkeys = cachedfk->nkeys;
				memcpy(info->conkey, cachedfk->conkey, sizeof(info->conkey));
				memcpy(info->confkey, cachedfk->confkey, sizeof(info->confkey));
				memcpy(info->conpfeqop, cachedfk->conpfeqop, sizeof(info->conpfeqop));
				/* zero out fields to be filled by match_foreign_keys_to_quals */
				info->nmatched_ec = 0;
				info->nmatched_rcols = 0;
				info->nmatched_ri = 0;
				memset(info->eclass, 0, sizeof(info->eclass));
				memset(info->rinfos, 0, sizeof(info->rinfos));
				fkey_list = lappend(fkey_list, info);
			}
		}
		table_close(relation, NoLock);
	}
	return fkey_list;
}

//Is this local query the last one ?
int hasNext(bool* graph, int length)
{
	bool* temp_graph = (bool*)palloc(length * length * sizeof(bool));
	for (int i = 0; i < length * length; i++)
	{
		temp_graph[i] = graph[i];
	}
	int total_cnt = 0;
	if (query_splitting_algorithm == Minsubquery)
	{
		for (int i = 0; i < length; i++)
		{
			for (int j = i + 1; j < length; j++)
			{
				if (temp_graph[i * length + j] == true)
				{
					temp_graph[i * length + j] = false;
					temp_graph[j * length + i] = false;
					total_cnt++;
				}
			}
		}
	}
	else if (query_splitting_algorithm == RelationshipCenter || query_splitting_algorithm == EntityCenter)
	{
		for (int i = 0; i < length; i++)
		{
			int cnt = 0;
			for (int j = 0; j < length; j++)
			{
				if (i == j)
					cnt++;
				if (temp_graph[i * length + j] == true)
				{
					temp_graph[i * length + j] = false;
					temp_graph[j * length + i] = false;
					cnt++;
				}
			}
			if (cnt > 1)
				total_cnt++;
		}
	}
	pfree(temp_graph);
	temp_graph = NULL;
	return total_cnt;
}

//Change the rte's relid and name
void dochange(RangeTblEntry* rte, char* relname, Relation relation, Oid relid)
{
	rte->relid = relid;
	pfree(rte->eref->aliasname);
	rte->eref->aliasname = relname;
	list_free(rte->eref->colnames);
	rte->eref->colnames = NIL;
	for (int i = 0; i < relation->rd_att->natts; i++)
	{
		char* str = (char*)palloc((strlen(relation->rd_att->attrs[i].attname.data) + 1) * sizeof(char));
		strcpy(str, relation->rd_att->attrs[i].attname.data);
		rte->eref->colnames = lappend(rte->eref->colnames, makeString(str));
	}
	return;
}

List* makeAggref(List* targetList)
{
	List* resList = NIL;
	ListCell* lc;
	foreach(lc, targetList)
	{
		TargetEntry* old_tar = (TargetEntry*)lfirst(lc);
		Oid old_vartype = ((Var*)old_tar->expr)->vartype;
		TargetEntry* tar = makeNode(TargetEntry);
		tar->resjunk = false;
		tar->resname = old_tar->resname;
		old_tar->resname = NULL;
		tar->resno = old_tar->resno;
		tar->resorigcol = 0;
		tar->resorigtbl = 0;
		tar->ressortgroupref = 0;
		Aggref* aggref = makeNode(Aggref);
		aggref->aggargtypes = lappend_oid(NIL, old_vartype);
		aggref->aggdirectargs = NULL;
		aggref->aggdistinct = NULL;
		aggref->aggfilter = NULL;
		switch (old_vartype)
		{
			case 23:
			{
				aggref->aggfnoid = 2132;
				aggref->inputcollid = 0;
				aggref->aggcollid = 0;
				aggref->aggtype = 23;
				break;
			}
			case 25:
			{
				aggref->aggfnoid = 2145;
				aggref->inputcollid = 100;
				aggref->aggcollid = 100;
				aggref->aggtype = 25;
				break;
			}
			default:
			{
				aggref->aggfnoid = 2145;
				aggref->inputcollid = 100;
				aggref->aggcollid = 100;
				aggref->aggtype = 25;
			}
		}
		aggref->aggkind = 'n';
		aggref->agglevelsup = 0;
		aggref->aggorder = NULL;
		aggref->aggsplit = AGGSPLIT_SIMPLE;
		aggref->aggstar = false;
		aggref->aggtranstype = 0;
		aggref->aggvariadic = false;
		aggref->args = lappend(NIL, old_tar);
		aggref->location = -1;
		tar->expr = aggref;
		resList = lappend(resList, tar);
	}
	return resList;
}

List* removeAggref(List* targetList)
{
	List* resList = NIL;
	ListCell* lc;
	foreach(lc, targetList)
	{
		TargetEntry* old_tar = (TargetEntry*)lfirst(lc);
		if (old_tar->expr->type == T_Aggref)
		{
			TargetEntry* tar = lfirst(((Aggref*)old_tar->expr)->args->head);
			if (tar->expr->type == T_RelabelType)
			{
				if (((RelabelType*)tar->expr)->relabelformat == COERCE_IMPLICIT_CAST)
					tar->expr = ((RelabelType*)tar->expr)->arg;
			}
			tar->resname = old_tar->resname;
			resList = lappend(resList, tar);
		}
		else
		{
			resList = lappend(resList, old_tar);
		}
	}
	return resList;
}

static Plan* find_node_with_nleaf_recursive(Plan* plan, int nleaf, int* leaf_has, int* depth)
{
	if (plan->lefttree == NULL)
	{
		*depth = *depth + 1;
		*leaf_has = 1;
		return NULL;
	}
	*depth = *depth + 1;
	int left_leaf = 0, right_leaf = 0, left_depth = *depth, right_depth = *depth;
	Plan* left_res = NULL;
	left_res = find_node_with_nleaf_recursive(plan->lefttree, nleaf, &left_leaf, &left_depth);
	Plan* right_res = NULL;
	if (plan->righttree)
		right_res = find_node_with_nleaf_recursive(plan->righttree, nleaf, &right_leaf, &right_depth);
	*leaf_has = left_leaf + right_leaf;
	if (left_res && right_res)
	{
		if (left_depth > right_depth)
		{
			*depth = left_depth;
			return left_res;
		}
		else
		{
			*depth = right_depth;
			return right_res;
		}
	}
	else if (left_res)
	{
		*depth = left_depth;;
		return left_res;
	}
	else if (right_res)
	{
		*depth = right_depth;
		return right_res;
	}
	else if (*leaf_has == nleaf)
	{
		*depth = (left_depth > right_depth) ? left_depth : right_depth;
		return plan;
	}
	else
	{
		*depth = (left_depth > right_depth) ? left_depth : right_depth;
		return NULL;
	}
}

static void walk_plantree(Plan* plan, Index* rel)
{
	Index res = 0;
	if (plan->lefttree == NULL)
	{
		res = ((Scan*)plan)->scanrelid;
		if (rel[0] == 0)
			rel[0] = res;
		else
			rel[1] = res;
	}
	if (plan->lefttree != NULL)
		walk_plantree(plan->lefttree, rel);
	if (plan->righttree != NULL)
		walk_plantree(plan->righttree, rel);
	return;
}

int tarfunc(Index* rels, PlannedStmt* new, PlannedStmt* old)
{
	if(old == NULL)
		return NEWBETTER;
	if (new->planTree->plan_rows > 10000000)
	{
		return OLDBETTER;
	}
	if (order_decision == only_cost)
	{
		if (old->planTree->total_cost < new->planTree->total_cost)
		{
			return OLDBETTER;
		}
		else
		{
			return NEWBETTER;
		}
	}
	if (order_decision == only_row)
	{
		if (old->planTree->plan_rows < new->planTree->plan_rows)
		{
			return OLDBETTER;
		}
		else
		{
			return NEWBETTER;
		}
	}
	double fac_old, fac_new;
	if (order_decision == hybrid_row)
	{
		if (new->planTree->plan_rows > 1)
			fac_new = new->planTree->plan_rows;
		else
			fac_new = 1;
		if (old->planTree->plan_rows > 1)
			fac_old = old->planTree->plan_rows;
		else
			fac_old = 1;
		if (fac_new / fac_old > old->planTree->total_cost / new->planTree->total_cost)
		{
			return OLDBETTER;
		}
		else
		{
			return NEWBETTER;
		}
	}
	else if (order_decision == hybrid_sqrt)
	{
		double fac_old, fac_new;
		if (new->planTree->plan_rows > 1)
			fac_new = sqrt(new->planTree->plan_rows);
		else
			fac_new = 1;
		if (old->planTree->plan_rows > 1)
			fac_old = sqrt(old->planTree->plan_rows);
		else
			fac_old = 1;
		if (fac_new / fac_old > old->planTree->total_cost / new->planTree->total_cost)
		{
			return OLDBETTER;
		}
		else
		{
			return NEWBETTER;
		}
	}
	else if (order_decision == hybrid_log)
	{
		if (new->planTree->plan_rows > 1)
			fac_new = log(new->planTree->plan_rows) / log(2);
		else
			fac_new = 1;
		if (old->planTree->plan_rows > 1)
			fac_old = log(old->planTree->plan_rows) / log(2);
		else
			fac_old = 1;
		if (fac_new / fac_old > old->planTree->total_cost / new->planTree->total_cost)
		{
			return OLDBETTER;
		}
		else
		{
			return NEWBETTER;
		}
	}
	else if (order_decision == global_view)
	{
		ListCell* lc;
		bool flag = false;
		foreach(lc, new->rtable)
		{
			RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc);
			if (rte->relid == rels[0])
			{
				flag = true;
				break;
			}
		}
		if (!flag)
			return OLDBETTER;
		flag = false;
		foreach(lc, new->rtable)
		{
			RangeTblEntry* rte = (RangeTblEntry*)lfirst(lc);
			if (rte->relid == rels[1])
			{
				flag = true;
				break;
			}
		}
		if (!flag)
			return OLDBETTER;
		return NEWBETTER;
	}
    return NEWBETTER;
}

// has bug with `MANUAL_ANALYZE`
static double getMatSize(Oid relid)
{
    int64 size = DatumGetInt64(DirectFunctionCall2Coll(pg_relation_size, 0, relid, (Datum)cstring_to_text("main")));
    if (Abs(size) < 10 * 1024)
        return (double)size / 1000000.0;
    else
    {
        size >>= 9;
        if (Abs(size) < 20 * 1024 - 1)
            return (double)half_rounded(size) / 1000.0;
        else
        {
            size >>= 10;
            if (Abs(size) < 20 * 1024 - 1)
                return (double)half_rounded(size);
            else
            {
                size >>= 10;
                if (Abs(size) < 20 * 1024 - 1)
                    return (double)half_rounded(size) * 1000.0;
                else
                {
                    size >>= 10;
                    return (double)half_rounded(size) * 1000000.0;
                }
            }
        }
    }
}
