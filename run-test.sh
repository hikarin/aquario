make opt
time ./aquario -GC copying tak.lsp
time ./aquario -GC mark_compact tak.lsp
time ./aquario -GC generational tak.lsp