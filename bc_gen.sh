make opt
python bc_gen.py
#hexdump -C test.abc
time ./aquario test.abc
