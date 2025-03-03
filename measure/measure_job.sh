#!/bin/bash

rm -rf job_result/
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
cd ../build && make -j32 && sudo make install && rm_pg_log && pg_start && cd ../measure

echo "Official" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh Official

echo "QuerySplit" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit

# with updating statistics
echo "compile Postgres without updating statistics..."
cd ../build && make CFLAGS="-DMANUAL_ANALYZE" -j32 && sudo make install && rm_pg_log && pg_start && cd ../measure

echo "QuerySplit" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit

mv compile.log job_result/.

pg_stop
