#!/bin/sh -e
cd pgsql/contrib/clickhouse_fdw
git pull
cp pg2ch/* ~/project/ClickHouse/dbms/src/Server/

#cp ~/project/ClickHouse/dbms/src/Server/config.xml 
cd ~/project/ClickHouse/build/
make clickhouse -j 8
cd ~/project/pgsql/contrib/clickhouse_fdw/
make clean
make
make install

cd ~/project/bin
rm -rf ~/DemoDb
./initdb ~/DemoDb
./pg_ctl start -D ~/DemoDb
./psql postgres -c "create extension clickhouse_fdw ;"
./psql postgres -c "create server x foreign data wrapper clickhouse_fdw;"
./psql postgres -c "create foreign table t(id int) server x;"
./psql postgres -c "select * from t;"
./pg_ctl stop -D ~/DemoDb
