# Python 3
#
# Ziyang Xu
# Nov 22, 2019
#
# Special Parsing File for PLDI20
# Get Assertion count (expensive and inexpensive)

import re


# Parse result
def parseExp(lines, bmark):
    loops = {}
    if lines is None or len(lines) == 0:
        print("Warning: %s Unexpected dump; empty" % bmark)
        return None

    # Start of findBestStrategy for loop main::while.cond.i debug: (filename:dijkstra_large.c, line:138, col:5)
    loop_info_re = re.compile(
        r'.* for loop (.+) debug:.*')

    idx = 0
    while idx < len(lines):
        line = lines[idx]
        if line.startswith("Start of findBestStrategy"):
            loop_info_re_parsed = loop_info_re.findall(line)
            if loop_info_re_parsed:
                loop_name = loop_info_re_parsed[0]
            else:
                print("Warning: %s parsed fail" % bmark)
                idx += 1
                continue

            if lines[idx + 1].startswith("no bbcount for") or lines[idx + 1].startswith("Read::getUnderlyingAUs:") or "In context LOOP" in lines[idx + 1]:
                print("BBcount/Read tweak for %s:%s" % (bmark, loop_name))
                while lines[idx + 1].startswith("no bbcount for") or lines[idx + 1].startswith("Read::getUnderlyingAUs:") or "In context LOOP" in lines[idx + 1]:
                    idx += 1
            # Handled Criticisms: 25
            # Expensive Validation Code Count: 0
            # Inexpensive Validation Code Count: 45
            if lines[idx + 1].startswith("Handled"):
                handled_cnt = int(''.join(filter(str.isdigit, lines[idx + 1])))
                exp_cnt = int(''.join(filter(str.isdigit, lines[idx + 2])))
                inexp_cnt = int(''.join(filter(str.isdigit, lines[idx + 3])))

                print(handled_cnt, exp_cnt, inexp_cnt)

                loops[loop_name.strip()] = {"handled": handled_cnt, "exp": exp_cnt, "inexp": inexp_cnt}

        idx += 1

    return loops
