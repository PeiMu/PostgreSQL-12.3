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

Official_dir_1="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_Official/1/"
Official_dir_2="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_Official/2/"
QuerySplit_dir_1="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_QuerySplit/1/"
QuerySplit_dir_2="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_QuerySplit/2/"
iteration=15 # 5 warm up + 10 runs

LOG_NAME=time_log.csv
rm -rf $Project_path/data/*${LOG_NAME}

# use `InvalidSnapshot` in TPCDS
sed -i 's/PortalStart(portal, NULL, 0, SnapshotAny);/PortalStart(portal, NULL, 0, InvalidSnapshot);/' ../src/backend/parser/query_split.c

# without updating statistics
echo "compile Postgres without updating statistics..."
# change `MEASURE_TIME` to true
sed -i 's/#define MEASURE_TIME\s\+false/#define MEASURE_TIME true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h

echo "Official" 2>&1|tee -a compile.log
echo "Optimize, Execute"  >> $Project_path/data/${LOG_NAME};
for sql in $(find "${Official_dir_1}" "${Official_dir_2}" -type f -name "*.sql"); do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U postgres -d tpcds_$1 -P pager=off -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} pg_Official_breakdown_${LOG_NAME}

###### without updating statistics
echo "QuerySplit wo updating statistics" 2>&1|tee -a compile.log
for sql in $(find "${QuerySplit_dir_1}" "${QuerySplit_dir_2}" -type f -name "*.sql"); do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U postgres -d tpcds_$1 -P pager=off -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_wo_stats_breakdown_${LOG_NAME}

pg_stop


# merge back to the whole plan
echo "compile QuerySplit wo updating statistics and merge back sub-plans..."
# change `MERGE_SUB_PLANS` to true
sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h

echo "QuerySplit wo updating statistics merge back sub-plans" 2>&1|tee -a compile.log
for sql in $(find "${QuerySplit_dir_1}" "${QuerySplit_dir_2}" -type f -name "*.sql"); do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U postgres -d tpcds_$1 -P pager=off -f "${sql}";
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
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics" 2>&1|tee -a compile.log
for sql in $(find "${QuerySplit_dir_1}" "${QuerySplit_dir_2}" -type f -name "*.sql"); do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U postgres -d tpcds_$1 -P pager=off -f "${sql}";
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
cd ../build && make clean >> compile.log 2>&1 && make -j32 >> compile.log 2>&1 && sudo make install >> compile.log 2>&1 && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h

echo "QuerySplit with updating statistics merge back sub-plans" 2>&1|tee -a compile.log
for sql in $(find "${QuerySplit_dir_1}" "${QuerySplit_dir_2}" -type f -name "*.sql"); do
  echo "execute ${sql}" >> $Project_path/data/${LOG_NAME};
  for i in $(eval echo {1.."${iteration}"}); do
    psql -U postgres -d tpcds_$1 -P pager=off -f "${sql}";
  done
done
mv $Project_path/data/${LOG_NAME} QuerySplit_whole_plan_breakdown_${LOG_NAME}

# reset to `SnapshotAny`
sed -i 's/PortalStart(portal, NULL, 0, InvalidSnapshot);/PortalStart(portal, NULL, 0, SnapshotAny);/' ../src/backend/parser/query_split.c

pg_stop

mv *${LOG_NAME} tpcds_$1_result/.
