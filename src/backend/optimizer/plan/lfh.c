/*-------------------------------------------------------------------------
 *
 * LFH.c
 *	  learn from history.
 *
 *
 * IDENTIFICATION
 *	  src\backend\optimizer\plan\LFH.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "optimizer/cost.h"
#include "optimizer/lfh.h"
#include "optimizer/planmain.h"
#include "nodes/bitmapset.h"
#include "nodes/nodes.h"
#include "utils/rel.h"
#include <stdlib.h>
#include "utils/hashutils.h"

#define foreach_myList(cell, l)	for ((cell) = mylist_head(l); (cell) != NULL; (cell) = lnext(cell))

myList* CheckList = ((myList*)NULL);

double factor = 0.1;
extern double clamp_row_est(double nrows);
extern Selectivity get_foreign_key_join_selectivity(PlannerInfo* root, Relids outer_relids, Relids inner_relids, SpecialJoinInfo* sjinfo, List** restrictlist);
extern Selectivity clauselist_selectivity(PlannerInfo* root, List* clauses, int varRelid, JoinType jointype, SpecialJoinInfo* sjinfo);

static inline myListCell* mylist_head(const myList* l)
{
	return l ? l->head : NULL;
}

/* Append a myListCell to the tail of myList. */
myList* ListRenew(myList* l, void* p)
{
	if (l == (myList*)NULL)
	{
		myListCell* new_head;
		new_head = (myListCell*)malloc(sizeof(ListCell));
		if (new_head)
		{
			new_head->next = NULL;
		}
		l = (myList*)malloc(sizeof(myList));
		if (l)
		{
			l->type = T_myList;
			l->length = 1;
			l->head = new_head;
			l->tail = new_head;
		}
	}
	else
	{
		myListCell* new_tail;
		new_tail = (myListCell*)malloc(sizeof(ListCell));
		if (new_tail)
		{
			new_tail->next = NULL;
			l->tail->next = new_tail;
			l->tail = new_tail;
			l->length++;
		}
	}
	l->tail->data = p;
	return l;
}

/* Create a new History */
void* CreateNewHistory(int rel)
{
	History* temp = (History*)malloc(sizeof(History));
	temp->type = T_History;
	temp->content = rel;
	temp->is_true = false;
	temp->rows = 0.0;
	return temp;
}

/* Check myList CheckList find out if we meet this rel before ? */
History* LookupHistory(int rel)
{
	myListCell* lc;
	foreach_myList(lc, CheckList)
	{
		History* one_page = ((History*)lc->data);
		if (one_page->content == rel)
		{
			return one_page;
		}
	}
	return NULL;
}

/* learn_from_history
*  Whenever caculating a cardinality of a temporary relation, the optimizer call this function.
*  The input joinrel is the temporary relation, inner_rel and outer_rel is joinrel's subtree.
*  If the joinrel we have met before this SQL query, we return the true selectivity instead of use Postgres's estimation.
*  If not we record this joinrel and get the true selectivity after executor.
*/
double learn_from_history(const PlannerInfo* root, const RelOptInfo* joinrel,
	const RelOptInfo* outer_rel, const RelOptInfo* inner_rel,
	const List* joininfo, SpecialJoinInfo* sjinfo)
{
	int rel = joinrel->relids->words[0];
	/* If we have met this relation before ? */
	History* his = LookupHistory(rel);
	/* Never met before*/
	if (his == NULL)
	{
		his = (History*)CreateNewHistory(rel);
		CheckList = ListRenew(CheckList, his);
		/* Join Relation */
		if (outer_rel && inner_rel)
		{
			Selectivity fkselec;
			Selectivity jselec;
			Selectivity pselec;
			fkselec = get_foreign_key_join_selectivity(root, outer_rel->relids, inner_rel->relids, sjinfo, &joininfo);
			if (IS_OUTER_JOIN(sjinfo->jointype))
			{
				List* joinquals = NIL;
				List* pushedquals = NIL;
				ListCell* l;
				foreach(l, joininfo)
				{
					RestrictInfo* rinfo = lfirst_node(RestrictInfo, l);

					if (RINFO_IS_PUSHED_DOWN(rinfo, joinrel->relids))
						pushedquals = lappend(pushedquals, rinfo);
					else
						joinquals = lappend(joinquals, rinfo);
				}
				jselec = clauselist_selectivity(root, joinquals, 0, sjinfo->jointype, sjinfo);
				pselec = clauselist_selectivity(root, pushedquals, 0, sjinfo->jointype, sjinfo);
				list_free(joinquals);
				list_free(pushedquals);
			}
			else
			{
				jselec = clauselist_selectivity(root, joininfo, 0, sjinfo->jointype, sjinfo);
				pselec = 0.0;
			}
			double nrows;
			switch (sjinfo->jointype)
			{
			case JOIN_INNER:
				nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
				break;
			case JOIN_LEFT:
				nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
				if (nrows < outer_rel->rows)
					nrows = outer_rel->rows;
				nrows *= pselec;
				break;
			case JOIN_FULL:
				nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
				if (nrows < outer_rel->rows)
					nrows = outer_rel->rows;
				if (nrows < inner_rel->rows)
					nrows = inner_rel->rows;
				nrows *= pselec;
				break;
			case JOIN_SEMI:
				nrows = outer_rel->rows * fkselec * jselec;
				break;
			case JOIN_ANTI:
				nrows = outer_rel->rows * (1.0 - fkselec * jselec);
				nrows *= pselec;
				break;
			default:
				nrows = 0;
				break;
			}
			return clamp_row_est(nrows);
		}
		/* Base Relation */
		else
		{
			Selectivity selec = clauselist_selectivity(root, joinrel->baserestrictinfo, 0, JOIN_INNER, NULL);
			return clamp_row_est(selec * joinrel->tuples);
		}
	}
	/* Have been met */
	else
	{
		/* Have never been Executed */
		if (!his->is_true)
		{
			/* Join Relation */
			if (outer_rel && inner_rel)
			{
				Selectivity fkselec;
				Selectivity jselec;
				Selectivity pselec;
				fkselec = get_foreign_key_join_selectivity(root, outer_rel->relids, inner_rel->relids, sjinfo, &joininfo);
				if (IS_OUTER_JOIN(sjinfo->jointype))
				{
					List* joinquals = NIL;
					List* pushedquals = NIL;
					ListCell* l;
					foreach(l, joininfo)
					{
						RestrictInfo* rinfo = lfirst_node(RestrictInfo, l);
						if (RINFO_IS_PUSHED_DOWN(rinfo, joinrel->relids))
							pushedquals = lappend(pushedquals, rinfo);
						else
							joinquals = lappend(joinquals, rinfo);
					}
					jselec = clauselist_selectivity(root, joinquals, 0, sjinfo->jointype, sjinfo);
					pselec = clauselist_selectivity(root, pushedquals, 0, sjinfo->jointype, sjinfo);
					list_free(joinquals);
					list_free(pushedquals);
				}
				else
				{
					jselec = clauselist_selectivity(root, joininfo, 0, sjinfo->jointype, sjinfo);
					pselec = 0.0;
				}
				double nrows;
				switch (sjinfo->jointype)
				{
				case JOIN_INNER:
					nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
					break;
				case JOIN_LEFT:
					nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
					if (nrows < outer_rel->rows)
						nrows = outer_rel->rows;
					nrows *= pselec;
					break;
				case JOIN_FULL:
					nrows = outer_rel->rows * inner_rel->rows * fkselec * jselec;
					if (nrows < outer_rel->rows)
						nrows = outer_rel->rows;
					if (nrows < inner_rel->rows)
						nrows = inner_rel->rows;
					nrows *= pselec;
					break;
				case JOIN_SEMI:
					nrows = outer_rel->rows * fkselec * jselec;
					break;
				case JOIN_ANTI:
					nrows = outer_rel->rows * (1.0 - fkselec * jselec);
					nrows *= pselec;
					break;
				default:
					nrows = 0;
					break;
				}
				return clamp_row_est(nrows * factor);
			}
			/* Base Relation */
			else
			{
				Selectivity selec = clauselist_selectivity(root, joinrel->baserestrictinfo, 0, JOIN_INNER, NULL);
				return clamp_row_est(selec * joinrel->tuples);
			}
		}
		/* Have been Executed */
		else
		{
			return his->rows;
		}
	}
}

/* After executor, we call this function to get the true selectivity for each temporary relation we stored in function learn_from_history().
*  Use recursion to transverse the PlanState to get the true cardinality.
*/
int learnSelectivity(const QueryDesc* queryDesc, const PlanState* planstate)
{
	Plan* plan = planstate->plan;
	// When we meet SeqScan, we reach the end of PlanState tree.
	if (plan->type == T_SeqScan)
	{
		int base_rel = (1 << ((Scan*)plan)->scanrelid);
		myListCell* lc;
		foreach_myList(lc, CheckList)
		{
			if (((History*)lc->data)->content == base_rel)
			{
				if (((History*)lc->data)->is_true == false)
				{
					((History*)lc->data)->is_true = true;
					((History*)lc->data)->rows = planstate->instrument->tuplecount;
				}
				break;
			}
		}
		return base_rel;
	}
	else if (plan->type == T_BitmapHeapScan)
	{
		return learnSelectivity(queryDesc, planstate->lefttree);
	}
	else if (plan->type == T_BitmapIndexScan)
	{
		int base_rel = (1 << ((Scan*)plan)->scanrelid);
		myListCell* lc;
		foreach_myList(lc, CheckList)
		{
			if (((History*)lc->data)->content == base_rel)
			{
				if (((History*)lc->data)->is_true == false)
				{
					((History*)lc->data)->is_true = true;
					if (planstate->instrument->ntuples == 0.0)
						((History*)lc->data)->rows = planstate->instrument->tuplecount;
					else
						((History*)lc->data)->rows = planstate->instrument->ntuples;
				}
				break;
			}
		}
		return base_rel;
	}
	else if (plan->type == T_IndexScan)
	{
		int base_rel = (1 << ((Scan*)plan)->scanrelid);
		IndexScanState* indexscanstate = (IndexScanState*)planstate;
		if (indexscanstate->iss_NumRuntimeKeys)
		{
			return base_rel;
		}
		myListCell* lc;
		foreach_myList(lc, CheckList)
		{
			if (((History*)lc->data)->content == base_rel)
			{
				if (((History*)lc->data)->is_true == false)
				{
					((History*)lc->data)->is_true = true;
					if (planstate->instrument->ntuples == 0.0)
						((History*)lc->data)->rows = planstate->instrument->tuplecount;
					else
						((History*)lc->data)->rows = planstate->instrument->ntuples;
				}
				break;
			}
		}
		return base_rel;
	}
	else if (plan->type == T_IndexOnlyScan)
	{
		int base_rel = (1 << ((Scan*)plan)->scanrelid);
		IndexScanState* indexscanstate = (IndexScanState*)planstate;
		if (indexscanstate->iss_NumRuntimeKeys)
		{
			return base_rel;
		}
		myListCell* lc;
		foreach_myList(lc, CheckList)
		{
			if (((History*)lc->data)->content == base_rel)
			{
				if (((History*)lc->data)->is_true == false)
				{
					((History*)lc->data)->is_true = true;
					if (planstate->instrument->ntuples == 0.0)
						((History*)lc->data)->rows = planstate->instrument->tuplecount;
					else
						((History*)lc->data)->rows = planstate->instrument->ntuples;
				}
				break;
			}
		}
		return base_rel;
	}
	double left_tuple = -1, right_tuple = -1;
	int left = 0;
	int right = 0;
	History* his = (History*)NULL;
	// Get the true cardinality of the lefttree if exists.
	if (planstate->lefttree)
	{
		left = learnSelectivity(queryDesc, planstate->lefttree);
	}
	// Get the true cardinality of the righttree if exists.
	if (planstate->righttree)
	{
		right = learnSelectivity(queryDesc, planstate->righttree);
	}
	if (plan->type == T_Agg)
	{
		return 0;
	}
	int c = left | right;
	/* This PlanState Node represent a temporary relation
	*  And we need to find out the corresponding History
	*/
	myListCell* lc;
	foreach_myList(lc, CheckList)
	{
		his = ((History*)lc->data);
		if ((his->content == c) && (!his->is_true))
		{
			/* If we find a corresponding History get the true selectivity */
			his->rows = planstate->instrument->tuplecount;
			his->is_true = true;
			break;
		}
	}
	/* Return the bitmap of this Node */
	return c;
}