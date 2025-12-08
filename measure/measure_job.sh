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
cd ../build && make clean >> compile.log 2>&1 && make >> compile.log 2>&1 && sudo make install  >> compile.log 2>&1 && pg_start && cd ../measure

# run ANALYZE
psql -U imdb -d imdb -c "ANALYZE;"

echo "Official" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh Official
#bash ./execute_job_queries.sh Official

echo "QuerySplit" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit
#bash ./execute_job_queries.sh QuerySplit

pg_stop

# with enabling middleware
echo "compile Postgres with enabling middleware"
# change `ENABLE_MIDDLEWARE` to true
sed -i 's/#define ENABLE_MIDDLEWARE\s\+false/#define ENABLE_MIDDLEWARE true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define ENABLE_MIDDLEWARE\s\+true/#define ENABLE_MIDDLEWARE false/' ../src/include/parser/query_split.h

echo "QuerySplit with enabling middleware" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit_with_middleware
#bash ./execute_job_queries.sh QuerySplit

pg_stop

# with updating statistics
echo "compile Postgres with updating statistics..."
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit_with_stats
#bash ./execute_job_queries.sh QuerySplit

pg_stop

# with updating statistics and enabling middleware
echo "compile Postgres with updating statistics and enabling middleware"
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
# change `ENABLE_MIDDLEWARE` to true
sed -i 's/#define ENABLE_MIDDLEWARE\s\+false/#define ENABLE_MIDDLEWARE true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h
sed -i 's/#define ENABLE_MIDDLEWARE\s\+true/#define ENABLE_MIDDLEWARE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics and enabling middleware" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job.sh QuerySplit_with_stats_and_middleware
#bash ./execute_job_queries.sh QuerySplit

pg_stop

mv compile.log job_result/.
