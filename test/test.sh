#!/bin/bash

str=""
i=0
GC=$1

echo "[GC: ${GC}]"

function success() {
    echo -n .
}

function fail() {
    echo -n F
    str="${str}Test ${i}: ${2} expected, but got $3\n"
}

function verify(){
    i=`expr $i + 1`
    result=$(echo "$1" | ./aquario -GC ${GC} 2> /dev/null | tail -1)
    if [ "$result" != "$2" ]; then
	fail "$1" "$2" "$result" $i
    else
	success
    fi
}

#number
verify 1 1
verify -2 -2
verify "'63" 63
verify "(+ 1 2)" 3
verify '(+ 1 -3)' -2
verify '(- 3)' -3
verify '(- 3 5)' -2
verify '(- 3 5 7)' -9
verify '(< 2 3)' \#t
verify '(< 3 3)' \#f
verify '(< 4 3)' \#f
verify "(+ (+ 1 2) (+ 3 4) (+ 5 6) (+ 7 8) (+ 9 10))" 55
verify "(+ (* 1 2 3 4 5) (* 6 7 8 9 10))" 30360

#list
verify "'(a b c)" '(a b c)'
verify "'(a b . c)" '(a b . c)'
verify "(define lst '(a b c)) (car (car lst))" \#undef

#define
verify "(define m 100) (cons m m)" '(100 . 100)'
verify "(define x 999)" x

#lambda
verify "(define fib (lambda (n) (if (< n 2) 1 (+ (fib (- n 1)) (fib (- n 2)))))) (fib 10)" 89
verify "(define null? (lambda (x) (eq? x nil))) (null? '())" \#t
verify "(define len (lambda (x) (if (eq? x nil) 0 (+ 1 (len (cdr x)))))) (len '(1 2 3 4 5))" 5

#string
verify "(define str \"hoge\") str" \"hoge\"
verify "(define str \"hoge\") (set! str \"fuga\") str" \"fuga\"
verify "(define str \"hoge\") (eq? str \"fuga\")" \#f

#symbol
verify "'test" test
verify "(quote a)" a
verify "'63" 63
verify "\'(+ 1 2)" "(+ 1 2)"
verify "(define x 'n) x" n

echo ""
if [ -n "$str" ]; then
    echo -e $str
fi
echo ""