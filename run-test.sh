make opt
time ./aquario -GC reference_count tak.lsp
time ./aquario -GC copying tak.lsp
time ./aquario -GC mark_compact tak.lsp
#time ./aquario -GC generational tak.lsp