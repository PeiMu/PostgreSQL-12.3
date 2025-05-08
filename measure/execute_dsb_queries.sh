#!/bin/bash

if [ -z "$1" ]; then
  echo "Please enter Official or QuerySplit!"
  exit 1
fi

dir_1="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_$1/1/"
dir_2="/home/pei/Project/benchmarks/dsb-postgres/code/tools/1_instance_out_qs_$1/2/"
iteration=1

log_name=pg_dsb_$1.txt

rm -f dsb_result/${log_name}
mkdir -p dsb_result/

for i in $(eval echo {1.."${iteration}"}); do
  for sql in $(find "$dir_1" "$dir_2" -type f -name "*.sql"); do
    echo "execute ${sql}" 2>&1|tee -a ${log_name};
    psql -U postgres -d dsb -f "${sql}" 2>&1|tee -a ${log_name};
  done
done

mv ${log_name} dsb_result/.
