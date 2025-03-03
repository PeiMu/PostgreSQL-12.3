dir="/home/pei/Project/benchmarks/imdb_job-postgres/QuerySplit/queries_new_settings_QuerySplit_subset"
iteration=1

rm -f result/*
mkdir -p result/

for i in $(eval echo {1.."${iteration}"}); do
  for sql in "${dir}"/*.sql; do
    echo "execute ${sql}" 2>&1|tee -a pg_query_split_${i}.txt;
    psql -U imdb -d imdb -f "${sql}" 2>&1|tee -a pg_query_split_${i}.txt;
  done
done

mv pg_query_split_* result/.
