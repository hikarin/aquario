import struct
import opcode

output = open("test.abc", "w+")

#(cons 1 (cons 2 (cons 3 NIL)))
output.write(struct.pack('b',  opcode.PUSH_NIL))
output.write(struct.pack('bb', opcode.PUSH, 3))
output.write(struct.pack('b', opcode.CONS))
output.write(struct.pack('bb', opcode.PUSH, 2))
output.write(struct.pack('b', opcode.CONS))
output.write(struct.pack('bb', opcode.PUSH, 1))
output.write(struct.pack('b', opcode.CONS))
output.write(struct.pack('b', opcode.PRINT))
