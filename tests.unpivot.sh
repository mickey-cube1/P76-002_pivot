#! /bin/sh

LANG=C

./pivot -R << EOF
1 a b c d e
2 abc def ghi
3	a	bc  de f	g
EOF
