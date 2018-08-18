make debug
#python bc_gen.py
#time ./aquario test.abc

./aquario test.lsp
echo
#hexdump -C test.abc
python dump_bc.py
