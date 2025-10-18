#!/bin/bash

if [ -z "$1" ]; then
  echo "Please enter Official or QuerySplit!"
  exit 1
fi

if [ -z "$2" ]; then
  echo "Please enter scale factor to choose the correct database!"
  exit 1
fi

dir_1="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_$1/1/"
dir_2="/home/pei/Project/benchmarks/tpcds-kit/tools/1_instance_out_qs_$1/2/"
iteration=1

log_name=pg_tpcds_$2_$1.txt

rm -f tpcds_$2_result/${log_name}
mkdir -p tpcds_$2_result/

for i in $(eval echo {1.."${iteration}"}); do
  for sql in $(find "$dir_1" "$dir_2" -type f -name "*.sql"); do
    echo "execute ${sql}" 2>&1|tee -a ${log_name};
    psql -U postgres -d tpcds_$2 -P pager=off -f "${sql}" 2>&1|tee -a ${log_name};
  done
done

mv ${log_name} tpcds_$2_result/.
