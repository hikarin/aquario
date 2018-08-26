import struct
import opcode

counter = 0
data = open("test.abc", "r").read()
size = len(data)

def concat():
    global counter
    global data
    string = ""
    while True:
        chr = struct.unpack_from("s", data, counter)[0]
        if chr == '\0':
            break
        string += chr
        counter += 1
    counter += 1
    return string

while counter < size:
    op = ord(data[counter])
    print '{0:4d}: '.format(counter),
    counter += 1
    if op == opcode.PUSH:
        print "PUSH    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.JNEQ:
        print "JNEQ    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.JMP:
        print "JMP     ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.SET:
        print "SET     ",
        string = concat()
        print '{:>6}'.format(string)
    elif op == opcode.REF:
        print "REF     ",
        string = concat()
        print '{:>6}'.format(string)
    elif op == opcode.FUND:
        print "FUND    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand),
        counter += 8
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.FUNC:
        print "FUNC    ",
        string = concat()
        print '{:>6}'.format(string)
    elif op == opcode.LOAD:
        print "LOAD    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.SROT:
        print "SROT    ",
        operand = struct.unpack_from("<l", data, counter)[0]
        print '{0:6d}'.format(operand)
        counter += 8
    elif op == opcode.FUNCS:
        print "FUNCS   "
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
        print "GT      "
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

