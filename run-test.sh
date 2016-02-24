make opt

#copying
time ./aquario -GC copy test/fib.lsp
echo =======================
#mark_compact
time ./aquario -GC mc test/fib.lsp
echo =======================
#generational
time ./aquario -GC gen test/fib.lsp
echo =======================
#reference_count
time ./aquario -GC ref test/fib.lsp
echo =======================