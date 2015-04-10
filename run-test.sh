make prof
#time ./aquario -GC copying tak.lsp
#time ./aquario -GC mark_compact tak.lsp
time ./aquario -GC generational tak.lsp
gprof ./aquario gmon.out > analysis.txt
cat analysis.txt