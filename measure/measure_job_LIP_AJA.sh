#!/bin/bash

if [ -z "$1" ]; then
  echo "Please enter job_rand or job_slow!"
  exit 1
fi

mkdir -p job_LIP_AJA_result/
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

# without updating statistics
echo "compile Postgres..."
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

# run ANALYZE
psql -U imdb -d imdb -c "ANALYZE;"

if [ $1 = "job_rand" ]; then
  echo "lip_auto_rewrite" 2>&1|tee -a compile.log
  bash ./hyperfine_in_mem_job_LIP_AJA.sh lip_auto_rewrite lip_aja
  echo "Official all" 2>&1|tee -a compile.log
  bash ./hyperfine_in_mem_job_LIP_AJA.sh Official all
fi

echo "Official" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job_LIP_AJA.sh Official $1

echo "LIP_AJA" 2>&1|tee -a compile.log
bash ./hyperfine_in_mem_job_LIP_AJA.sh LIP_AJA $1

pg_stop

