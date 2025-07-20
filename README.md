## How to Compile

```bash
export PREFIX=YOUR_BIN_PATH
mkdir build && cd build
sudo apt install libreadline-dev zlib1g-dev make bison flex gawk

../configure --prefix=$PREFIX
make -j32 && make check
sudo make install
```

Reference: https://www.postgresql.org/docs/16/install-make.html#CONFIGURE-OPTIONS

----------------------------------------------------

## Setup

```bash
# create database
pg_ctl -D $PREFIX/data initdb # initdb -D $PREFIX/data

# start server
pg_ctl start -l $PREFIX/logfile -D $PREFIX/data

# stop server
pg_ctl stop -D $PREFIX/data -m smart -s
```


PostgreSQL Database Management System
=====================================

This directory contains the source code distribution of the PostgreSQL database management system.

PostgreSQL is an advanced object-relational database management system
that supports an extended subset of the SQL standard, including
transactions, foreign keys, subqueries, triggers, user-defined types, and functions.  This distribution also contains C language bindings.

PostgreSQL has many language interfaces, many of which are listed here:

	https://www.postgresql.org/download

See the file INSTALL for instructions on how to build and install
PostgreSQL.  That file also lists supported operating systems and
hardware platforms and contains information regarding any other
software packages that are required to build or run the PostgreSQL
system.  Copyright and license information can be found in the
file COPYRIGHT.  A comprehensive documentation set is included in this
distribution; it can be read as described in the installation
instructions.

The latest version of this software may be obtained at
https://www.postgresql.org/download/.  For more information look at our
web site located at https://www.postgresql.org/.
