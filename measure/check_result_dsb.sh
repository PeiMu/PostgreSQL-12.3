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

# w/o updating statistics
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

bash ./execute_dsb_queries.sh Official
bash ./execute_dsb_queries.sh QuerySplit

pg_stop
