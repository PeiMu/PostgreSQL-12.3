## Compilation
```bash
# w/o updating statistics
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure

# with updating statistics
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h
```

### Compilation with Performance Breakdown
```bash
# w/o updating statistics
echo "compile Postgres without updating statistics..."
# change `MEASURE_TIME` to true
sed -i 's/#define MEASURE_TIME\s\+false/#define MEASURE_TIME true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h

echo "compile QuerySplit with updating statistics..."
# change `MEASURE_TIME` to true
sed -i 's/#define MEASURE_TIME\s\+false/#define MEASURE_TIME true/' ../src/include/parser/query_split.h
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MEASURE_TIME\s\+true/#define MEASURE_TIME false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h
```

### Compilation with Merging Back
```bash
# w/o updating statistics
echo "compile QuerySplit wo updating statistics and merge back sub-plans..."
# change `MERGE_SUB_PLANS` to true
sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h

# with updating statistics
echo "compile QuerySplit with updating statistics merge back sub-plans..."
# change `MERGE_SUB_PLANS` to true
sed -i 's/#define MERGE_SUB_PLANS\s\+false/#define MERGE_SUB_PLANS true/' ../src/include/parser/query_split.h
# change `MANUAL_ANALYZE` to true
sed -i 's/#define MANUAL_ANALYZE\s\+false/#define MANUAL_ANALYZE true/' ../src/include/parser/query_split.h
cd ../build && make clean && make -j32 && sudo make install && pg_start && cd ../measure
# rest
sed -i 's/#define MERGE_SUB_PLANS\s\+true/#define MERGE_SUB_PLANS false/' ../src/include/parser/query_split.h
sed -i 's/#define MANUAL_ANALYZE\s\+true/#define MANUAL_ANALYZE false/' ../src/include/parser/query_split.h
```

## Measure Performance
```bash
# measure JOB
sudo rm -rf job_result/
bash ./measure_job.sh && bash ./measure_breakdown_time_job.sh

# measure DSB
sudo rm -rf dsb_10_result/
bash ./measure_dsb.sh 10 && bash ./measure_breakdown_time_dsb.sh 10

sudo rm -rf dsb_100_result/
bash ./measure_dsb.sh 100 && bash ./measure_breakdown_time_dsb.sh 100
```

## Test
```bash
# check JOB
bash ./check_result_job.sh

# check DSB
bash ./check_result_dsb.sh 10

bash ./check_result_dsb.sh 100
```
