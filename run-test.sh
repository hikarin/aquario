make opt

#copying
time ./aquario -GC copying tak.lsp
#gprof ./aquario gmon.out > analysis.txt
#cat analysis.txt

#mark_compact
time ./aquario -GC mark_compact tak.lsp
#gprof ./aquario gmon.out > analysis.txt
#cat analysis.txt

#generational
time ./aquario -GC generational tak.lsp
#gprof ./aquario gmon.out > analysis.txt
#cat analysis.txt