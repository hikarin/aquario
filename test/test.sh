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
    str="${str}Test($i) [${1}] failed: ${2} expected, but got $3\n"
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
verify +99 99
verify -2 -2
verify "'63" 63
verify "(+ 10 20 30)" 60
verify "(+ 30 1)" 31
verify "(+ 1 2)" 3
verify "(+ 1 -3)" -2
verify "(+ +1 -3 1000)" 998
verify "(- 3)" -3
verify "(- 2 1)" 1
verify "(- 1 2)" -1
verify "(- 3 5)" -2
verify "(- 3 5 7)" -9
verify "(- 3 5 7 100)" -109
verify "(* 2 3 4 5)" 120
verify "(+ (+ 1 2) (+ 3 4) (+ 5 6) (+ 7 8) (+ 9 10))" 55
verify "(+ (+ 1 2) (- 3 4) (+ 5 6) (- 7 8) (- 9 10))" 11
verify "(+ (* 1 2 3 4 5) (* 6 7 8 9 10))" 30360
verify "(/ 1)" 1
verify "(/ 2)" 0
verify "(/ 100 5 4)" 5
verify "(/ 100 (- 10 5) (/ 16 4))" 5
verify "(* 2 (- 10 5) (* 2 4))" 80

#comparison
verify "(= 3 2)" \#f
verify "(= 1 2)" \#f
verify "(= 2 2)" \#t
verify "(< 2 3)" \#t
verify "(< 3 3)" \#f
verify "(< 4 3)" \#f
verify "(<= 2 3)" \#t
verify "(<= 3 3)" \#t
verify "(<= 4 3)" \#f
verify "(> 2 3)" \#f
verify "(> 3 3)" \#f
verify "(> 4 3)" \#t
verify "(>= 2 3)" \#f
verify "(>= 3 3)" \#t
verify "(>= 4 3)" \#t

#list
verify "'(a b c)" '(a b c)'
verify "'(a b (c d e))" '(a b (c d e))'
verify "'(a b (c d e) f g (h i j k) l)" '(a b (c d e) f g (h i j k) l)'
verify "'(x y . z)" '(x y . z)'
verify "(cons 'a (cons 'b (cons 'c nil)))" '(a b c)'
verify "(cons 'x (cons 'y 'z))" '(x y . z)'
verify "(cons (cons 'a (cons 'b (cons 'c nil))) (cons (cons 'd (cons 'e (cons 'f nil))) nil))" '((a b c) (d e f))'
verify "(car '(a . b))" a
verify "(cdr '(a . b))" b
verify "(car (cdr (cdr '(a b (c d e) f g (h i j k) l)))" '(c d e)'
verify "(car (cdr (cdr (cdr (cdr (cdr '(a b (c d e) f g (h i j k) l)))))))" '(h i j k)'

#define
verify "(define m 100) (cons m m)" '(100 . 100)'
verify "(define x 999)" x
verify "(define lst '(a b c d e)) lst" '(a b c d e)'
verify "(define x '(a b c d e)) y" \#undef

#string
verify "(define str \"hoge\") str" \"hoge\"
verify "(define str \"hoge\") (eq? str str)" \#t
verify "(define str \"hoge\") (eq? str \"fuga\")" \#f

#symbol
verify "'test" test
verify "(quote a)" a
verify "(quote (quote b))" "'b"
verify "(quote 10)" 10
verify "(= (quote 1) 1)" \#t
verify "(+ '1 '2 '3)" 6
verify "(+ ''1 2 3)" "number required, but given '1"
verify "\'(+ 1 2)" "(+ 1 2)"
verify "(define x 'n) x" n
verify "(define x 100) (define y x) y" 100

#comment
verify ";(define m 100)
  m" \#undef
verify "(define m 100) ;100" "#undef"
verify "(define m 100) m;100" "100"
verify "(cons 1; 2)
  3)" "(1 . 3)"
verify "(cons 1 ; 2)
  3)" "(1 . 3)"

#lambda
verify "(define null? (lambda (x) (eq? x nil))) (null? '())" \#t
verify "(define prints (lambda (x y) (cons y x))) (prints 1 2)" "(2 . 1)"
verify "(define len (lambda (x) (if (eq? x nil) 0 (+ 1 (len (cdr x)))))) (len '(1 2 3 4))" 4
verify "(define fib (lambda (n) (if (< n 2) 1 (+ (fib (- n 1)) (fib (- n 2)))))) (fib 15)" 987
verify "(define list (lambda (x . y) (cons x y))) (list 1 2 3)" "(1 2 3)"
verify "(define list (lambda (x . y) (cons x y))) (define lst '(3 4 5)) (list 1 2 lst)" "(1 2 (3 4 5))"
verify "(define func (lambda (a . b) (cons b a))) (func 1 2 3 4 5)" "((2 3 4 5) . 1)"
verify "(define tak (lambda (x y z) (if (<= x y) z (tak (tak (- x 1) y z) (tak (- y 1) z x) (tak (- z 1) x y))))) (tak 8 4 0)" 1

#error
verify "(define lst '(a b c)) (car (car lst))" "pair required, but given a"

echo ""
if [ -n "$str" ]; then
    echo -e $str
fi
echo ""
