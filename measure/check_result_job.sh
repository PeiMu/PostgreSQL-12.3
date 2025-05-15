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

sudo rm -rf job_result/pg_job_Official.txt
sudo rm -rf job_result/pg_job_QuerySplit.txt

cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

bash ./execute_job_queries.sh Official
bash ./execute_job_queries.sh QuerySplit

echo "Comparing the results of the Official Postgres VS QuerySplit"
diff job_result/pg_job_Official.txt job_result/pg_job_QuerySplit.txt 2>&1|tee job_diff_Official_QuerySplit.txt

mv job_diff_Official_QuerySplit.txt job_result/
sudo rm -rf job_result/pg_job_QuerySplit.txt
pg_stop

sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h

bash ./execute_job_queries.sh QuerySplit

echo "Comparing the results of the Official Postgres VS QuerySplit with merge_back"
diff job_result/pg_job_Official.txt job_result/pg_job_QuerySplit.txt 2>&1|tee job_diff_Official_QuerySplit_merge.txt

mv job_diff_Official_QuerySplit_merge.txt job_result/
pg_stop
