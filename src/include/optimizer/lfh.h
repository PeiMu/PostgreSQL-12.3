/*-------------------------------------------------------------------------
 *
 * LFH.h
 *	  learn from history.
 *
 *
 * IDENTIFICATION
 *	  src\include\optimizer\LFH.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef LFH_H
#define LFH_H

#include "postgres.h"
#include "executor/execdesc.h"
#include "nodes/execnodes.h"
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "optimizer/pathnode.h"

typedef struct myListCell myListCell;

// To avoid palloc() appearing in this block, it's convinient to define new structure as myXXX.
// Not like postgres structure XXX use palloc() to allocate the memory, the structure myXXX use malloc().
// The reason is the memory allocated by palloc() would be free after each query executed.
// But if we want to learn from the history, some data must maintain during the whole period.
typedef struct myList
{
	NodeTag type; // The type is T_myList
	int	length; // The length of the List
	myListCell* head;
	myListCell* tail;
} myList;

struct myListCell
{
	void* data;
	myListCell* next;
};
/* struct History
*  If a temporary relation appears first time, it will be recorded as a History when the SQL is executing.
*  If the temporary relation have already appeared in the previous query, we return the selectivity to help caculating the tuples more accurate.
*  The value of attribute "selec" will be assigned after the SQL query is executed.
*/
typedef struct History
{
	//Consider C = A join B
	NodeTag type; // The type is T_History
	int content; //The entity of this relation
	double rows;
	bool is_true;
} History;
/* Because we always add same base relation into different temporary relations' childRelList, making an array of base relations let us don't need to substantialize base relation each time during a SQL query */
/* When opimizer meet a temporary relation check if we meet it before. If so return the true selectivity, if not, add the relation into a list of history */
double learn_from_history(const PlannerInfo* root, const RelOptInfo* joinrel, const RelOptInfo* outter_rel, const RelOptInfo* inner_rel, const List* joininfo, SpecialJoinInfo* sjinfo);
/* Get the true selectivity after executor */
int learnSelectivity(const QueryDesc* queryDesc, const PlanState* planstate);
#endif