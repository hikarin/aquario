make opt

#mark_sweep
time ./aquario -GC ms tak.lsp

#copying
time ./aquario -GC copy tak.lsp

#mark_compact
time ./aquario -GC mc tak.lsp

#generational
time ./aquario -GC gen tak.lsp

#reference_count
time ./aquario -GC ref tak.lsp