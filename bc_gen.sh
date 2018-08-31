make opt
script=fib
echo $script
rm $script.abc
time ./aquario $script.lsp
time ./aquario $script.lsp

echo
#hexdump -C test.abc
#python dump_bc.py $script.abc
