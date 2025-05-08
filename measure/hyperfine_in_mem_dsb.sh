#!/bin/bash

log_name=pg_$1.csv

rm -rf pg_$1.csv

dir_name=$1
if [ ${dir_name} = "QuerySplit_with_stats" ]; then
  dir_name="QuerySplit"
fi
dir_1="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_${dir_name}/1/"
dir_2="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_${dir_name}/2/"
iteration=10

for sql in "${dir}"/*.sql; do
  #echo "hyperfine run ${sql}" 2>&1|tee -a ${log_name}
  hyperfine --warmup 5 --runs ${iteration} --export-csv temp.csv "psql -U postgres -d dsb -f ${sql}"
  cat temp.csv >> ${log_name}
done

mv pg_$1.csv dsb_result/.
rm temp.csv
