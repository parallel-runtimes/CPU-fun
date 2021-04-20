#
# Generate random text which we can use as input to our
# searching code.
#

import random

chars = [chr(i) for i in range(ord('a'),ord('z'))] +\
        [chr(i) for i in range(ord('A'),ord('Z'))] +\
        [chr(i) for i in range(ord('0'),ord('9'))] +\
        [' ', '-', '.', '_']

def generateLine(len):
    res = ""
    for c in [random.choice(chars) for i in range(len)]:
        res += c
    return res

def generateFile(lineLen, lines):
    for l in range(lines):
        print (generateLine(lineLen))

from optparse import OptionParser
options = OptionParser()
(options, args) = options.parse_args()
if len(args) < 2:
    print("Need two options: linewidth linecount")
else:
    generateFile(int(args[0]), int(args[1]))


