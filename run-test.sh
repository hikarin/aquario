make opt
#SCRIPT=test/tak.lsp
SCRIPT=test/fib.lsp

echo GC: copy
time ./aquario -GC copy $SCRIPT

echo GC: gen
time ./aquario -GC gen $SCRIPT

echo GC: ms
time ./aquario -GC ms $SCRIPT

echo GC: mc
time ./aquario -GC mc $SCRIPT

echo GC: ref
time ./aquario -GC ref $SCRIPT
