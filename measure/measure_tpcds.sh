#!/bin/bash

if [ -z "$1" ]; then
  echo "Please enter scale factor to choose the correct database!"
  exit 1
fi

mkdir -p tpcds_$1_result/
rm -rf compile.log

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

# use `InvalidSnapshot` in TPCDS
sed -i 's/PortalStart(portal, NULL, 0, SnapshotAny);/PortalStart(portal, NULL, 0, InvalidSnapshot);/' ../src/backend/parser/query_split.c

# without updating statistics
echo "compile Postgres without updating statistics..."
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure

# run ANALYZE
psql -U postgres -d tpcds_$1 -c "ANALYZE;"

echo "Official" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_tpcds.sh Official $1

echo "QuerySplit" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_tpcds.sh QuerySplit $1

pg_stop

# with updating statistics
echo "compile Postgres with updating statistics..."
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_tpcds.sh QuerySplit_with_stats $1

mv compile.log tpcds_$1_result/.

# reset to `SnapshotAny`
sed -i 's/PortalStart(portal, NULL, 0, InvalidSnapshot);/PortalStart(portal, NULL, 0, SnapshotAny);/' ../src/backend/parser/query_split.c

pg_stop
