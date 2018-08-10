import struct
import opcode

output = open("test.abc", "w+")
output.write(struct.pack('bb', opcode.PUSH, 33))
output.write(struct.pack('bb', opcode.PUSH, 6))
output.write(struct.pack('bb', opcode.JMP, 8))
output.write(struct.pack('b',  opcode.PRINT))
output.write(struct.pack('b', opcode.HALT))

output.write(struct.pack('bb', opcode.LOAD, 2))
output.write(struct.pack('bb', opcode.PUSH, 2))
output.write(struct.pack('b',  opcode.LT))
output.write(struct.pack('bb', opcode.JEQ, 35))

output.write(struct.pack('bb', opcode.LOAD, 2))
output.write(struct.pack('bb', opcode.PUSH, 1))
output.write(struct.pack('b', opcode.SUB))
output.write(struct.pack('bb', opcode.PUSH, 24))
output.write(struct.pack('bb', opcode.JMP, 8))

output.write(struct.pack('bb', opcode.LOAD, 3))
output.write(struct.pack('bb', opcode.PUSH, 2))
output.write(struct.pack('b', opcode.SUB))
output.write(struct.pack('bb', opcode.PUSH, 33))
output.write(struct.pack('bb', opcode.JMP, 8))
output.write(struct.pack('b', opcode.ADD))
output.write(struct.pack('b', opcode.RET))

output.write(struct.pack('bb', opcode.PUSH, 1))
output.write(struct.pack('b', opcode.RET))
