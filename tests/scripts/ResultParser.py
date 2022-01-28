# Python 3
#
# Ziyang Xu
# Apr 27, 2019
#
# Latest Major Update: July 12, 2019
# Parse result dump
# Please follow instructions in README.md

import re


# Parse result
def parseExp(lines, bmark):

    if lines is None or len(lines) == 0:
        print("Warning: %s Unexpected dump; empty" % bmark)
        return None

    # Find anchors
    # 1. "Focus on these loops..."
    # 2. "Parallelizable loops:"
    # 3. "Total expected speedup..."
    anchor_loop_info = -1
    anchor_loop_speedup = -1
    anchor_loop_selection = -1
    for idx, line in enumerate(lines):
        if "Focus on these loops" in line:
            anchor_loop_info = idx
        if "Parallelizable loops:" in line:
            anchor_loop_speedup = idx
        if "Total expected speedup" in line:
            anchor_loop_selection = idx

    if anchor_loop_info < 0 or anchor_loop_speedup < 0 or anchor_loop_selection < 0:
        print("Warning: %s Unexpected dump; cannot find all anchors" % bmark)
        return None

    # {loop_name: {"exec_coverage":float,
    #              "exec_time": int, "total_time": int,
    #              "total_lcDeps":int,
    #              "covered_lcDeps", "lcDeps_coverage": float,
    #              "stage": str,
    #              "loop_speedup": float, "debug_info": str
    #              "selected": bool
    #             }}
    loops = {}

    # Find Loops
    # Get loop info from the first several lines

    # - MAIN__ :: L90 (filename:swim.c, line:416, col:10)    Time 3378 / 3435 Coverage: 98.3%
    loop_info_re = re.compile(
        r' - (.+) \((.+)\).*Time ([0-9]+) / ([0-9]+) Coverage: ([0-9\.]+)%')
    loop_info_nodebug_re = re.compile(
        r' - (.+).*Time ([0-9]+) / ([0-9]+) Coverage: ([0-9\.]+)%')

    for line in lines[anchor_loop_info + 1:]:
        if line.startswith(" - "):
            loop_info_re_parsed = loop_info_re.findall(line)

            if loop_info_re_parsed:
                loop_name, debug_info, exec_time, total_time, exec_coverage = loop_info_re_parsed[0]
            else:
                print("Warning: %s parsed fail, seems no debug_info" % bmark)
                loop_info_re_parsed = loop_info_nodebug_re.findall(line)
                if not loop_info_re_parsed:
                    print("Warning: %s parsed fail, no debug_info also failed, check first several lines of the dump file" % bmark)
                    return None
                loop_name, exec_time, total_time, exec_coverage = loop_info_re_parsed[0]
                debug_info = ""

            loops[loop_name.strip()] = {
                "debug_info": debug_info, "exec_time": int(exec_time),
                "total_time": int(total_time), "exec_coverage": float(exec_coverage)
            }

        else:
            break

    # Check loops is not empty
    if len(loops) == 0:
        print("Warning: %s Unexpected dump; cannot get loop name" % bmark)
        return None

    # Get loop speedup
    # - 88.01% MAIN__ :: for.cond30 DSWP[P21-S] (Loop speedup: 3.38x)

    # TODO: extension to more techniques (assuming DSWP now)
    loop_speedup_re = re.compile(
        r' - .+%.(.+).(DSWP\[.*\]).\(Loop speedup:.([0-9\.]+)x')

    for line in lines[anchor_loop_speedup + 1:]:
        loop_speedup_re_parsed = loop_speedup_re.findall(line)
        if loop_speedup_re_parsed:
            try:
                loop_name, loop_stage, loop_speedup = loop_speedup_re_parsed[0]
                if loop_name.strip() not in loops:
                    print("Warning: %s Unexpected dump; loop_name doesn't match" % bmark)
                    return None
                loops[loop_name.strip()].update({
                    "loop_stage": loop_stage,
                    "loop_speedup": float(loop_speedup)
                })
            except re.error:
                print(loop_speedup_re_parsed)
        else:
            break

    # Find Speedup
    # "Total expected speedup: 5.84x using 22 workers.""
    speedup_re = re.compile(
        r'Total expected speedup: ([0-9\.]+)x using ([0-9]+) workers')
    speedup_re_parsed = speedup_re.findall(lines[anchor_loop_selection])
    if not speedup_re_parsed:
        print("Warning: %s Unexpected dump; cannot parse total speedup" % bmark)
        return None
    speedup, worker_cnt = speedup_re_parsed[0]
    speedup = float(speedup)
    worker_cnt = int(worker_cnt)

    # Find Selection
    # - 87.95% depth 3    MAIN__ :: for.cond33    DSWP[P22]          #regrn-par-loop
    # X 88.01% depth 2    MAIN__ :: for.cond30    DSWP[P21-S]            #regrn-no-par-loop
    loop_selection_re = re.compile(
        r' ([-X]) .* depth [0-9]+   (.+) DSWP\[')
    for line in lines[anchor_loop_selection + 1:]:
        loop_selection_re_parsed = loop_selection_re.findall(line)
        if loop_selection_re_parsed:
            sel, loop_name = loop_selection_re_parsed[0]
            if sel == '-':
                selected = True
            else:
                selected = False
            if loop_name.strip() not in loops:
                print("Warning: %s Unexpected dump; loop_name doesn't match" % bmark)
                return None
            else:
                loops[loop_name.strip()].update({"selected": selected})

    # Find coverage
    total_coverage = 0.0
    for name in loops:
        if "selected" in loops[name] and loops[name]["selected"]:
            if "exec_coverage" in loops[name]:
                total_coverage += loops[name]["exec_coverage"]
            else:
                print("Warning: %s Unexpected dump; with selection info but no coverage" % bmark)

    # Find Loop Carried Deps coverage of hot loops
    # Coverage of loop-carried dependences for hot loop MAIN__ :: for.cond30 covered=12, total=15 , percentage=80.00%
    loop_coverage_re = re.compile(
        r'Coverage of loop-carried dependences for hot loop (.+) covered=([0-9]+).*total=([0-9]+).*percentage=([0-9\.]+)%')
    for line in lines:
        if line.startswith("Coverage of"):
            loop_coverage_re_parsed = loop_coverage_re.findall(line)
            if loop_coverage_re_parsed:
                loop_name, covered_lcDeps, total_lcDeps, lcDeps_coverage = loop_coverage_re_parsed[0]
                if loop_name.strip() not in loops:
                    print("Warning: %s Unexpected dump; loop_name doesn't match" % bmark)
                    return None
                loops[loop_name.strip()].update({
                    "covered_lcDeps": int(covered_lcDeps),
                    "total_lcDeps": int(total_lcDeps),
                    "lcDeps_coverage": float(lcDeps_coverage)
                })

    # Find Conflict Counts for hot loops
    # Conflict Count for kernel_lu::for.cond : 0
    conflict_re = re.compile(
        r'Conflict Count for (.+) : ([0-9]+)')
    for line in lines:
        if line.startswith("Conflict Count"):
            conflict_re_parsed = conflict_re.findall(line)
            if conflict_re_parsed:
                loop_name, conflict_cnt = conflict_re_parsed[0]
                if loop_name.strip() not in loops:
                    print("Warning: %s Unexpected dump; loop_name doesn't match" % bmark)
                    return None
                loops[loop_name.strip()].update({
                    "conflict_cnt": int(conflict_cnt),
                })

    # Find loops, memory dependence queries
    # Total memory dependence queries to CAF: 784
    # Memory Loop-Carried Deps Count: 37
    # Register Loop-Carried Deps Count: 2
    # Control Loop-Carried Deps Count: 68

    # -====================================================-
    # Selected Remedies:
    # ----------------------------------------------------
    # ( ptr-residue-remedy ) chosen to address criticicm (Mem, RAW, LC):
    #   store %struct._QITEM* null, %struct._QITEM** %59, align 8, !dbg !578, !tbaa !382, !namer !579 (filename:dijkstra_large.c, line:81, col:15) ->
    #   %60 = load %struct._QITEM*, %struct._QITEM** %qNext7.i.i, align 8, !dbg !592, !tbaa !382, !namer !594 (filename:dijkstra_large.c, line:89, col:19)
    # Alternative remedies for the same criticism: ( .... )
    # ------------------------------------------------------

    loop_anchor_list = []
    loop_name_list = []
    loop_name_from_weight_re = re.compile(
        r'Compute weight for loop (.+)\.\.\.')
    for idx, line in enumerate(lines):
        if line.startswith("Compute weight for loop"):
            loop_name_from_weight_parsed = loop_name_from_weight_re.findall(line)
            if loop_name_from_weight_parsed:
                loop_name = loop_name_from_weight_parsed[0]
                if loop_name.strip() not in loops:
                    print("Warning: %s Unexpected dump; loop_name doesn't match" % bmark)
                    return None
                loop_anchor_list.append(idx)
                loop_name_list.append(loop_name.strip())

    # all the loops correspond to the previous list from now on
    for anchor, loop_name in zip(loop_anchor_list, loop_name_list):
        num_queries = -1
        num_raw_lcdep = -1
        num_war_lcdep = -1
        num_waw_lcdep = -1
        num_reg_lcdep = -1
        num_control_lcdep = -1

        # remedies_start_offset = -1
        for idx, line in enumerate(lines[anchor + 1:]):
            if line.startswith("Total memory dependence queries to CAF"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_queries = res[0]
            elif line.startswith("RAW Memory Loop-Carried Deps Count"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_raw_lcdep = res[0]
            elif line.startswith("WAR Memory Loop-Carried Deps Count"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_war_lcdep = res[0]
            elif line.startswith("WAW Memory Loop-Carried Deps Count"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_waw_lcdep = res[0]
            elif line.startswith("Register Loop-Carried Deps Count"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_reg_lcdep = res[0]
            elif line.startswith("Control Loop-Carried Deps Count"):
                res = [int(i) for i in line.split() if i.isdigit()]
                if len(res) == 1:
                    num_control_lcdep = res[0]
            # elif line.startswith("Selected Remedies"):
            #     remedies_start_offset = idx + 1
            elif line.startswith("Compute weight for loop"):
                break

        chosen_type_count = {
            "reg": 0,
            "ctrl": 0,
            "raw": 0,
            "waw": 0,
            "war": 0
        }

        chosen_dict = {
            "reg": {},
            "ctrl": {},
            "raw": {},
            "waw": {},
            "war": {}
        }

        avail_dict = {
            "reg": {},
            "ctrl": {},
            "raw": {},
            "waw": {},
            "war": {}
        }

        # if remedies_start_offset == -1:
        #     continue

        # remedies_start_anchor = anchor + remedies_start_offset

        chosen_re = re.compile(
            r'\((.+?)\) chosen.+\((.+?)\)')
        avail_re = re.compile(
            r'Remedies (.+) can address.+\((.+)\).+')
        name_re = re.compile(
            r'\((.+?)\)')

        type_state = None
        lc_ii_state = None
        # We don't care any IR Dep!
        for line in lines[anchor + 1:]:

            if line.startswith("Compute weight for loop"):
                break
            chosen_parsed = chosen_re.findall(line)
            avail_parsed = avail_re.findall(line)
            if chosen_parsed:
                remediator_name, dep_info = chosen_parsed[0]
                remediator_name = remediator_name.strip()
            elif avail_parsed:
                remediator_names, dep_info = avail_parsed[0]
            else:
                # avail or chosen remedy not found
                continue

            # determine type
            if "Reg" in dep_info:
                type_state = "reg"
            elif "WAW" in dep_info:
                type_state = "waw"
            elif "WAR" in dep_info:
                type_state = "war"
            elif "RAW" in dep_info:
                type_state = "raw"
            elif "Control" in dep_info:
                type_state = "ctrl"
            else:
                print("Warning: Unexpected dump for %s, no type in dep info!" % bmark)
                return None

            # determine lc or ii
            if "LC" in dep_info:
                lc_ii_state = "lc"
            elif "II" in dep_info:
                lc_ii_state = "ii"
            else:
                print("Warning: Unexpected dump for %s, no LC/II in dep info!" % bmark)
                return None

            # skip if II
            if lc_ii_state == "ii":
                continue

            if chosen_parsed:
                # add to both dicts
                chosen_type_count[type_state] += 1
                if remediator_name in chosen_dict[type_state]:
                    chosen_dict[type_state][remediator_name] += 1
                else:
                    chosen_dict[type_state][remediator_name] = 1

                # if remediator_name in avail_dict[type_state]:
                #     avail_dict[type_state][remediator_name] += 1
                # else:
                #     avail_dict[type_state][remediator_name] = 1

            elif avail_parsed:
                name_parsed = name_re.findall(remediator_names)
                if name_parsed:
                    for remediator_name in name_parsed:
                        remediator_name = remediator_name.strip()
                        if remediator_name in avail_dict[type_state]:
                            avail_dict[type_state][remediator_name] += 1
                        else:
                            avail_dict[type_state][remediator_name] = 1

            # # not used!
            # elif line.startswith("Alternative remedies for the same criticism"):
            #     if not type_state or not lc_ii_state:
            #         print("Warning: Alternative remedies appear without context in %s" % bmark)
            #         return None
            #     if lc_ii_state == "ii":
            #         continue
            #     name_parsed = name_re.findall(line)
            #     if name_parsed:
            #         for remediator_name in name_parsed:
            #             remediator_name = remediator_name.strip()
            #             if remediator_name in avail_dict[type_state]:
            #                 avail_dict[type_state][remediator_name] += 1
            #             else:
            #                 avail_dict[type_state][remediator_name] = 1

        # # Check memory deps add up correctly, wont work if not DOALL loop!
        # if num_raw_lcdep != chosen_type_count['raw'] or num_war_lcdep != chosen_type_count['war'] or num_waw_lcdep != chosen_type_count['waw']:
        #     print("Warning: Unexpected dump, not all memory deps are covered!")

        avail_dict['ctrl']['replicable-stage-remedy'] = chosen_dict['ctrl']['replicable-stage-remedy'] = num_control_lcdep - chosen_type_count['ctrl']
        avail_dict['reg']['replicable-stage-remedy'] = chosen_dict['reg']['replicable-stage-remedy'] = num_reg_lcdep - chosen_type_count['reg']

        final_dict = {
            "num_queries": num_queries,
            "num_raw_lcdep": num_raw_lcdep,
            "num_waw_lcdep": num_waw_lcdep,
            "num_war_lcdep": num_war_lcdep,
            "num_reg_lcdep": num_reg_lcdep,
            "num_control_lcdep": num_control_lcdep,
            "chosen_count": chosen_dict,
            "avail_count": avail_dict
        }
        loops[loop_name]["dependence_info"] = final_dict

    return {"speedup": speedup, "worker_cnt": worker_cnt, "total_coverage": total_coverage,
            "loops": loops}


# Generate a JSON file to show the result
def dumpJSON(self, json_filename="status.json"):
    raise NotImplementedError()


if __name__ == '__main__':
    import sys
    import os
    from pprint import pprint
    if len(sys.argv) != 2:
        print("Use python ResultParser.py benchmark.*.dump for testing")
        exit()

    filename = sys.argv[1]
    if not os.path.isfile(filename):
        print("Dump file doesn't exist")
        exit()

    with open(filename, 'r') as fd:
        lines = fd.readlines()

    pprint(parseExp(lines, "test"))
