import struct
import opcode

output = open("test.abc", "w+")

#(cons 1 (cons 2 (cons 3 NIL)))

output.write(struct.pack('b',  opcode.PUSH_NIL))
output.write(struct.pack('bb', opcode.PUSH, 10))
output.write(struct.pack('b',  opcode.CONS))

output.write(struct.pack('bb', opcode.PUSH, 9))
output.write(struct.pack('bb', opcode.LOAD, 0))
output.write(struct.pack('bb', opcode.PUSH, 0))
output.write(struct.pack('b',  opcode.GT))
output.write(struct.pack('bb', opcode.JNEQ, 20))
output.write(struct.pack('bb', opcode.PUSH, 0))
output.write(struct.pack('bb', opcode.LOAD, 0))
output.write(struct.pack('b',  opcode.PRINT))
output.write(struct.pack('bb', opcode.JMP, 6))

#output.write(struct.pack('bb', opcode.LOAD, 2))
#output.write(struct.pack('bb', opcode.LOAD, 2))
#output.write(struct.pack('b', opcode.CONS))

output.write(struct.pack('bb', opcode.PUSH, 100))
output.write(struct.pack('b', opcode.PRINT))
output.write(struct.pack('b', opcode.HALT))

