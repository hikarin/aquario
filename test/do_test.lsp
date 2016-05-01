(define test-result '())
(define total 0)
(define null?
  (lambda (x) (eq? x nil)))

(define assert-equal
  (lambda (exp1 exp2)
    (begin
     (set! total (+ total 1))
     (if (equal? exp1 exp2)
         (display ".")
       (begin (display "F")
              (set! test-result (cons (list total exp1 exp2 ) test-result)))))))

(define show-result-sub
  (lambda (lst)
    (if (null? lst)
        nil
      (begin
       (display "Test No.")
       (display (car (car lst)))
       (print "")
       (display " --- ")
       (display (car (cdr (car lst))))
       (print "")
       (display " --- ")
       (display (car (cdr (cdr (car lst)))))
       (print "")
       (show-result-sub (cdr lst))))))

(define show-result
  (lambda ()
    (if (null? test-result)
        (print "All tests succeeded!")
      (show-result-sub test-result))))

(load "test/test.lsp")

(print "")
(show-result)
(print "")

(gc-stress)
(load "test/test.lsp")
(print "")
(show-result)
(print "")