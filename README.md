# Tiny SQL Parser

[![Build Status](https://travis-ci.org/zlikavac32/sql-query-parser.svg?branch=master)](https://travis-ci.org/zlikavac32/sql-query-parser)

Small **ASCII only** SQL parser for the `SELECT` statements intended to be used in query builders. The statement is decomposed into sections and placeholders are collected for each section. Only unnamed placeholders are supported which means that for every placeholder only it's offset within the section is recorded.

Currently this parser targets MySQL SQL grammar until version `8.0`.

This library should parse any valid `SELECT` statement but it will also parse some invalid statements as valid. This is by design, since intention is to use this to parse already valid statements and this way grammar is simplified. 

For more info about the library check `include/tsqlp.h`.

With the library comes standalone binary to decompose `SELECT` statements into sections called `tsqlp`.

For example, for the SQL

```sql
SELECT id, 2, ? + ? FROM t WHERE a = ?
```

`tsqlp` produces output

```text
columns 2 7 11 12 id, 2, ? + ?
tables 0 1 t
where 1 4 5 a = ?
```

Statement contains three sections, `columns`, `tables` and `where`. Section `column` has 2 parameters at offsets 7 and 11 and is 12 bytes long. Then comes 12 bytes of the section content. Section `tables` contains 0 parameters and is 1 byte long. Content of the `tables` section is `t`. Finally, `where` section has 1 parameter at offset 4 and the content of that section is `a = ?` which is 5 bytes long.

## Placeholders as table names

This parser supports placeholders as table name. That is something invalid in regular SQL but it's useful in query building.

For example, parser can parse `SELECT * FROM ?` and expose `?` as a placeholder which can later have inline subquery or something else.

## Installation

Clone this repository and within do the following.

```sh
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release 
make
sudo make install
```

If you get `tsqlp: error while loading shared libraries: libtsqlp.so: cannot open shared object file: No such file or directory` make sure `/usr/local/lib` is used by the `ldconfig` or change install path.
