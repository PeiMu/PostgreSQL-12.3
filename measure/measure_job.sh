#!/bin/bash

mkdir -p job_result/
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

# without updating statistics
echo "compile Postgres without updating statistics..."
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

echo "Official" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh Official

echo "QuerySplit" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit

pg_stop

# with updating statistics
echo "compile Postgres with updating statistics..."
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit_with_stats

mv compile.log job_result/.

pg_stop
