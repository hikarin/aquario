(assert-equal '(+ 1 2) '3)
(assert-equal '(+ (+ 1 2) (+ 3 4) (+ 5 6) (+ 7 8) (+ 9 10)) '(+ 1 2 3 4 5 6 7 8 9 10))
(assert-equal '(+ (* 1 2 3 4 5 6 7 8 9 10) (* 1 2 3 4 5 6 7 8 9 10)) (* 3628800 2))
(assert-equal '(+ -5 -4) '-9)
(assert-equal  '(+ 456 -321) '(- 456 321))
(assert-equal '(- 10 15) '-5)
(assert-equal  '(- 100 10 10 10 10 10 10 10 10 10) '10) 
(assert-equal '(- 21 17) '4)
(assert-equal '(* 14 18) '252)
(assert-equal '(* 2 2 2 2 2 2 2 2 2 2) '1024)
(assert-equal '(/ 21 7) '3)
(assert-equal '(/ 13 3) '4)
(assert-equal '(/ (- (+ 1 (* 3 79)) 38) 40) '5)
(assert-equal '(* 451 (+ (- 213 78) (- 7 10) (+ (/ 100 10) 8) (- (+ 4 5) (* 31 100) 500) 711) 800) '-984984000)

(assert-equal '(define x 999) ''x)
(assert-equal '(undef? x) '#f)
(assert-equal '(undef? m) '#t)
(assert-equal '(undef? (define p (define q r))) '#f)
(assert-equal '(undef? p) '#f)
(assert-equal '(undef? q) '#t)

(assert-equal '(define str "hoge") ''str)
(assert-equal 'str '"hoge")
(assert-equal '(set! str "fuga") '"fuga")
(assert-equal 'str '"fuga")
(assert-equal '(eq? str "fuga") '#f)
(assert-equal '(begin (define str1 "hoge") (define str2 "hoge") (equal? str1 str2)) '#t)
(assert-equal '(begin (set! str1 "fugafuga") (set! str2 "fugafuga") (equal? str1 str2)) '#t)

(assert-equal '(define sym 'hogehoge) ''hogehoge)
(assert-equal 'sym ''hogehoge)
(assert-equal '(set! sym 'hogehoge) ''hogehoge)
(assert-equal 'sym ''hogehoge)
(assert-equal '(eq? sym 'hogehoge) '#f)
(assert-equal ''x ''x)
(assert-equal '(equal? 'x 'y) '#f)

(assert-equal ''(1 2 3 4 5 6) '(list 1 2 3 4 5 6))
(assert-equal '(list 100 'hoge "fuga" (+ 10 20 30)) ''(100 hoge "fuga" 60))
(assert-equal '(cdr (list 70 60 50 40 30 20)) ''(60 50 40 30 20))
(assert-equal '(car (cdr (list 70 60 50 40 30 20 10))) '60)
(assert-equal '(car (cdr (cdr '(70 60 50 40 30 20 10)))) '50)
(assert-equal '(car (cdr (cdr (cdr '(70 60 50 40 30 20 10))))) '40)

(assert-equal '(define c (cons 5 10)) '(cons 5 10))
(assert-equal '(car c) '5)
(assert-equal '(cdr c) '10)
(assert-equal '(define d (cons c c)) '(cons (cons 5 10) (cons 5 10)))

(assert-equal '(define x 100) '100)
(assert-equal 'x '100)
(assert-equal '(begin (define fact (lambda (n)
			       (if (= n 1) 1
				 (* n (fact (- n 1))))))
		      (fact 10))
	      '3628800)
(assert-equal '(begin (define fib (lambda (n)
				    (if (< n 2) 1
				      (+ (fib (- n 1)) (fib (- n 2))))))
		      (fib 10))
	      '89)

(assert-equal '((lambda (x y) (+ x y)) 123 456) '579)
(assert-equal '((lambda (x y z) (* x y z)) (fib 4) (fib 5) (fib 6)) '520)
