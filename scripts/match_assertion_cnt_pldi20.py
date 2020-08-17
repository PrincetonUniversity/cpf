import json
import csv

file_bon = "assertion_bon.json"
file_collab = "assertion_collab.json"

bmark_list = ['052.alvinn', '056.ear', '129.compress', '164.gzip', '175.vpr',
              '179.art', '181.mcf', '183.equake', '429.mcf',
              '456.hmmer', '462.libquantum', '470.lbm', '482.sphinx3', '519.lbm', '525.x264', '544.nab']

with open(file_bon, "r") as fd:
    map_bon = json.load(fd)

with open(file_collab, "r") as fd:
    map_collab = json.load(fd)

all_compare = []
for bmark in bmark_list:
    if bmark not in map_bon or bmark not in map_collab:
        print("%s results not there, skipping" % bmark)
        continue

    if not map_bon[bmark] or not map_collab[bmark]:
        print("%s results not complete, skipping" % bmark)
        continue

    for loop, bon_result in map_bon[bmark].items():
        if loop not in map_collab[bmark]:
            print("%s : %s collab results not complete, skipping" % (bmark, loop))
            continue

        exp_bon = bon_result['exp']
        inexp_bon = bon_result['inexp']

        collab_result = map_collab[bmark][loop]
        exp_collab = collab_result['exp']
        inexp_collab = collab_result['inexp']

        handled_bon = bon_result['handled']
        handled_collab = collab_result['handled']

        all_compare.append([bmark + loop, exp_bon, exp_collab, inexp_bon, inexp_collab])

        if handled_bon != handled_collab:
            print("%s %s:\n    Exp: %d->%d (%d) \n    Inexp: %d->%d (%d)\n    Handled: %d->%d (%d)" %
                    (bmark, loop,
                     exp_bon, exp_collab, exp_collab - exp_bon,
                     inexp_bon, inexp_collab, inexp_collab - inexp_bon,
                     handled_bon, handled_collab, handled_collab - handled_bon))

for line in all_compare:
    print("{:<20} {:32} {:32} {:32} {:32}".format(*line))

with open("out.csv", "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerows(all_compare)
