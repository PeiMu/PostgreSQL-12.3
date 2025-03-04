#!/bin/bash

log_name=pg_$1.csv

rm -rf pg_$1.csv

dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_$1_subset"
iteration=10

for sql in "${dir}"/*.sql; do
  #echo "hyperfine run ${sql}" 2>&1|tee -a ${log_name}
  hyperfine --warmup 5 --runs ${iteration} --export-csv temp.csv "psql -U imdb -d imdb -f ${sql}"
  cat temp.csv >> ${log_name}
done

mv pg_$1.csv job_result/.
rm temp.csv