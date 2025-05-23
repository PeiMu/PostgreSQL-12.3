#!/bin/bash

Project_path=/home/pei/Project/project_bins
pg_start() {
  pg_ctl start -l $Project_path/logfile -D $Project_path/data
}
pg_stop() {
  pg_ctl stop -D $Project_path/data -m smart -s
}
rm_pg_log() {
  rm $Project_path/logfile
}

if [ -z "$1" ]; then
  echo "Please enter scale factor to choose the correct database!"
  exit 1
fi

sudo rm -rf dsb_$1_result/pg_dsb_$1_Official.txt
sudo rm -rf dsb_$1_result/pg_dsb_$1_QuerySplit.txt

# use `InvalidSnapshot` in DSB
sed -i 's/PortalStart(portal, NULL, 0, SnapshotAny);/PortalStart(portal, NULL, 0, InvalidSnapshot);/' ../src/backend/parser/query_split.c

# w/o updating statistics
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

bash ./execute_dsb_queries.sh Official $1
bash ./execute_dsb_queries.sh QuerySplit $1

echo "Comparing the results of the Official Postgres VS QuerySplit"
diff dsb_$1_result/pg_dsb_$1_Official.txt dsb_$1_result/pg_dsb_$1_QuerySplit.txt 2>&1|tee dsb_$1_diff_Official_QuerySplit.txt

mv dsb_$1_diff_Official_QuerySplit.txt dsb_$1_result/.
sudo rm -rf dsb_$1_result/pg_dsb_$1_QuerySplit.txt
pg_stop

sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h

bash ./execute_dsb_queries.sh QuerySplit $1

echo "Comparing the results of the Official Postgres VS QuerySplit with merge_back"
diff dsb_$1_result/pg_dsb_$1_Official.txt dsb_$1_result/pg_dsb_$1_QuerySplit.txt 2>&1|tee dsb_$1_diff_Official_QuerySplit_merge.txt

# reset to `SnapshotAny`
sed -i 's/PortalStart(portal, NULL, 0, InvalidSnapshot);/PortalStart(portal, NULL, 0, SnapshotAny);/' ../src/backend/parser/query_split.c

mv dsb_$1_diff_Official_QuerySplit_merge.txt dsb_$1_result/.
pg_stop
