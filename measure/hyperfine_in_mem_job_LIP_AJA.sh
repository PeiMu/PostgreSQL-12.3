#!/bin/bash

log_name=pg_$1_$2.csv

rm -rf ${log_name}

dir="/home/pei/Project/benchmarks/imdb_job-postgres/LIP_AJA/$1/$2"
iteration=10

for sql in "${dir}"/*.sql; do
  #echo "hyperfine run ${sql}" 2>&1|tee -a ${log_name}
  hyperfine --warmup 5 --runs ${iteration} --export-csv temp.csv "psql -U pei -d imdb -f ${sql}"
  cat temp.csv >> ${log_name}
done

mv ${log_name} job_LIP_AJA_result/.
rm temp.csv
