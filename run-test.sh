make opt

SCRIPT=test/tak.lsp

#mark_sweep
time ./aquario -GC ms $SCRIPT
echo =======================
#copying
time ./aquario -GC copy $SCRIPT
echo =======================
#mark_compact
time ./aquario -GC mc $SCRIPT
echo =======================
#generational
time ./aquario -GC gen $SCRIPT 
echo =======================
#reference_count
time ./aquario -GC ref $SCRIPT
echo =======================