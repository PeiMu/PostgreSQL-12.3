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

Official_dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_Official_subset"
QuerySplit_dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_QuerySplit_subset"
iteration=15 # 5 warm up + 10 runs

LOG_NAME=time_log.csv
rm -rf $Project_path/data/*${LOG_NAME}

# without updating statistics
echo "compile Postgres without updating statistics..."
# change `MEASURE_TIME` to true
sed -i 's/#define MEASURE_TIME\s\+false/#define MEASURE_TIME true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h

echo "Official" 2>&1|tee -a compile.log
echo "Optimize, Execute"  >> ${LOG_NAME};
for sql in "${Official_dir}"/*.sql; do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U imdb -d imdb -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} pg_Official_breakdown_${LOG_NAME}

###### without updating statistics
echo "QuerySplit wo updating statistics" 2>&1|tee -a compile.log
for sql in "${QuerySplit_dir}"/*.sql; do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U imdb -d imdb -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_wo_stats_breakdown_${LOG_NAME}

pg_stop


# merge back to the whole plan
echo "compile QuerySplit wo updating statistics and merge back sub-plans..."
# change `MERGE_SUB_PLANS` to true
sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h

echo "QuerySplit wo updating statistics merge back sub-plans" 2>&1|tee -a compile.log
for sql in "${QuerySplit_dir}"/*.sql; do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U imdb -d imdb -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_whole_plan_wo_stats_breakdown_${LOG_NAME}

pg_stop
###### without updating statistics


# with updating statistics
echo "compile QuerySplit with updating statistics..."
# change `MEASURE_TIME` to true
sed -i 's/#define MEASURE_TIME\s\+false/#define MEASURE_TIME true/' ../src/include/parser/query_split.h
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics" 2>&1|tee -a compile.log
for sql in "${QuerySplit_dir}"/*.sql; do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U imdb -d imdb -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_with_stats_breakdown_${LOG_NAME}

pg_stop


# merge back to the whole plan
echo "compile QuerySplit with updating statistics merge back sub-plans..."
# change `MERGE_SUB_PLANS` to true
sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics merge back sub-plans" 2>&1|tee -a compile.log
for sql in "${QuerySplit_dir}"/*.sql; do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U imdb -d imdb -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_whole_plan_breakdown_${LOG_NAME}

pg_stop

mv *${LOG_NAME} job_result/.
