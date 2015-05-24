make opt

#copying
time ./aquario -GC copying tak.lsp

#mark_compact
time ./aquario -GC mark_compact tak.lsp

#generational
time ./aquario -GC generational tak.lsp