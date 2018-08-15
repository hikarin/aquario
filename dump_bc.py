import struct
import opcode

data = open("test.abc", "r").read()
size = len(data)
counter = 0

while counter < size:
    op = ord(data[counter])
    print '{0:4d}: '.format(counter),
    counter += 1
    if op == opcode.PUSH:
        print "PUSH    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.NOP:
        print "NOP"
    elif op == opcode.ADD:
        print "ADD     "
    elif op == opcode.SUB:
        print "SUB     "
    elif op == opcode.MUL:
        print "MUL     "
    elif op == opcode.DIV:
        print "DIV     "
    elif op == opcode.PRINT:
        print "PRINT   "
    elif op == opcode.POP:
        print "POP     "
    elif op == opcode.EQ:
        print "EQ      "
    elif op == opcode.LT:
        print "LT      "
    elif op == opcode.LTE:
        print "LTE     "
    elif op == opcode.GT:
        print "GT     "
    elif op == opcode.GTE:
        print "GTE     "
    elif op == opcode.JEQ:
        print "JEQ     "
    elif op == opcode.JNEQ:
        print "JNEQ    "
    elif op == opcode.JMP:
        print "JMP     "
    elif op == opcode.JEQB:
        print "JEQB    "
    elif op == opcode.JNEQB:
        print "JNEQB   "
    elif op == opcode.JMPB:
        print "JMBP    "
    elif op == opcode.LOAD:
        print "LOAD     "
    elif op == opcode.RET:
        print "RET     "
    elif op == opcode.CONS:
        print "CONS    "
    elif op == opcode.CAR:
        print "CAR    "
    elif op == opcode.CDR:
        print "CDR     "
    elif op == opcode.PUSH_NIL:
        print "PUSH_NIL  "
    elif op == opcode.PUSH_TRUE:
        print "PUSH_TRUE "
    elif op == opcode.PUSH_FALSE:
        print "PUSH_FALSE"
    elif op == opcode.HALT:
        print "HALT     "
    elif op == opcode.QUOTE:
        print "QUOTE    "
    else:
        print op
