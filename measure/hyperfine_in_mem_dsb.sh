#!/bin/bash

log_name=pg_$1.csv

rm -rf ${log_name}

dir_name=$1
if [ ${dir_name} = "QuerySplit_with_stats" ]; then
  dir_name="QuerySplit"
fi
dir_1="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_${dir_name}/1/"
dir_2="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_${dir_name}/2/"
iteration=10

for sql in $(find "$dir_1" "$dir_2" -type f -name "*.sql"); do
  #echo "hyperfine run ${sql}" 2>&1|tee -a ${log_name}
  hyperfine --warmup 5 --runs ${iteration} --export-csv temp.csv "psql -U postgres -d dsb_$2 -P pager=off -f ${sql}"
  cat temp.csv >> ${log_name}
done

mv ${log_name} dsb_$2_result/.
rm temp.csv
