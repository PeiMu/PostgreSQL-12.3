#!/bin/bash

if [ "$#" -ne 1  ]; then
  echo "Please enter Official/QuerySPlit"
fi

dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_$1_subset"
iteration=1

mkdir -p job_result/

for i in $(eval echo {1.."${iteration}"}); do
  for sql in "${dir}"/*.sql; do
    echo "execute ${sql}" 2>&1|tee -a pg_query_split_result.txt;
    psql -U imdb -d imdb -f "${sql}" 2>&1|tee -a pg_query_split_result.txt;
  done
done

mv pg_query_split_result.txt job_result/.
