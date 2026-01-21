/*-------------------------------------------------------------------------
 *
 *
 *
 *
 * IDENTIFICATION
 *	  src/include/parser/query_split.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef QUERY_SPLIT_H
#define QUERY_SPLIT_H

#include "postgres.h"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif
#ifndef HAVE_GETRUSAGE
#include "rusagestub.h"
#endif
#include "access/parallel.h"
#include "access/printtup.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "catalog/namespace.h"
#include "commands/async.h"
#include "commands/createas.h"
#include "commands/matview.h"
#include "commands/prepare.h"
#include "executor/spi.h"
#include "jit/jit.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "nodes/parsenodes.h"
#include "nodes/pathnodes.h"
#include "nodes/pg_list.h"
#include "nodes/print.h"
#include "nodes/makefuncs.h"
#include "optimizer/optimizer.h"
#include "pgstat.h"
#include "pg_trace.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "pg_getopt.h"
#include "postmaster/autovacuum.h"
#include "postmaster/postmaster.h"
#include "replication/logicallauncher.h"
#include "replication/logicalworker.h"
#include "replication/slot.h"
#include "replication/walsender.h"
#include "rewrite/rewriteHandler.h"
#include "storage/bufmgr.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "storage/sinval.h"
#include "tcop/fastpath.h"
#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
#include "mb/pg_wchar.h"
#include "postgres.h"
#include "fmgr.h"
#include "storage/lmgr.h"  /* For LockRelationOid */

#include <stdbool.h>

#define MEASURE_TIME false
#define MERGE_SUB_PLANS false
#define MANUAL_ANALYZE false
#define SERIALIZE_WITH_OID true

void doQSparse(const char* query_string, const char* commandTag, Node* pstmt, Query* querytree, char* completionTag);

// SERIALIZE_WITH_OID
#include "utils/lsyscache.h"
#include "catalog/namespace.h"
#include "catalog/index.h"       /* For IndexGetRelation */
/*-------------------------------------------------------------------------
 * OID Translation Support
 *
 * These structures and functions support serializing plans from one database
 * (with FK constraints) and executing them in another database (columnar).
 *-------------------------------------------------------------------------
 */

/* Maximum number of relations in a single plan */
#define MAX_PLAN_RELATIONS 64

/* OID mapping entry: maps old OID to schema.table name (or index) */
typedef struct OidMapEntry
{
  Oid         old_oid;
  char        schema_name[NAMEDATALEN];
  char        table_name[NAMEDATALEN];
  bool        is_index;                    /* true if this is an index */
  char        index_name[NAMEDATALEN];     /* index name if is_index */
} OidMapEntry;

/* OID mapping for a plan */
typedef struct OidMap
{
  int         num_entries;
  OidMapEntry entries[MAX_PLAN_RELATIONS];
} OidMap;

/* Function declarations for OID translation */
static void BuildOidMap(PlannedStmt *plan, OidMap *map);
static void WriteOidMapToFile(FILE *file, OidMap *map);
static bool ReadOidMapFromFile(FILE *file, OidMap *map);
static Oid LookupNewOid(OidMap *map, Oid old_oid);
static void TranslateOidsInPlan(PlannedStmt *plan, OidMap *map);
static void TranslateOidsInPlanTree(Plan *plan, OidMap *map);

/*
 * OID Translation Functions for Cross-Database Plan Execution
 *
 * Use these to serialize plans from a database with FK constraints
 * and execute them in a database with columnar storage.
 */

/* Serialize a plan with OID-to-name mapping (call in source DB with FKs) */
void SerializePlanWithOidMap(PlannedStmt *plan, const char *filepath);

/* Execute serialized plans with OID translation (call in target columnar DB) */
void ExecuteSerializedPlans(const char *filepath,
                            const char *query_string,
                            const char *commandTag,
                            char *completionTag);
// SERIALIZE_WITH_OID end

typedef struct timespec timespec;
timespec diff(timespec start, timespec end);
timespec tic( );
timespec toc( timespec* start_time, const char* prefix, bool print );

#endif
