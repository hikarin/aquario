make opt

#copying
time ./aquario -GC copy tak.lsp

#mark_compact
time ./aquario -GC mc tak.lsp

#generational
time ./aquario -GC gen tak.lsp