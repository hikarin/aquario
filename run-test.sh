make opt

#copying
time ./aquario -GC copy fib.lsp
echo =======================
#mark_compact
time ./aquario -GC mc fib.lsp
echo =======================
#generational
time ./aquario -GC gen fib.lsp
echo =======================
#reference_count
time ./aquario -GC ref fib.lsp
echo =======================