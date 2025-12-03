#!/bin/bash

if [ "$#" -ne 1  ]; then
  echo "Please enter Official or QuerySplit!"
fi

dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_$1_subset"
iteration=1

log_name=pg_job_$1.txt

rm -rf job_result/${log_name}
mkdir -p job_result/

for i in $(eval echo {1.."${iteration}"}); do
  for sql in "${dir}"/*.sql; do
    echo "execute ${sql}" 2>&1|tee -a ${log_name};
    psql -U imdb -d imdb -f "${sql}" 2>&1|tee -a ${log_name};
  done
done

mv ${log_name} job_result/.
