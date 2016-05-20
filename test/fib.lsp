(define <= (lambda (x y) (if (< x y) #t
			   (if (= x y) #t
			     #f))))

(define fib (lambda (n)
	      (if (<= n 2)
		  1
		(+ (fib (- n 1)) (fib (- n 2))))))

(print (fib 30))