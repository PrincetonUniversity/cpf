# Python 3
#
# Ziyang Xu
# Apr 27, 2019
#
# Latest Major Update: July 12, 2019
# Visualize Report in different formats
# Please follow instructions in README.md

import csv
from os.path import join as path_join


class ReportVisualizer:

    def __init__(self, bmarks, passes, status, path):
        self.bmarks = bmarks  # Name, suite

        if "Experiment" in passes:
            passes.remove("Experiment")
            self.has_exp = True
        else:
            self.has_exp = False

        if "RealSpeedup" in passes:
            passes.remove("RealSpeedup")
            self.has_real_speedup = True
        else:
            self.has_real_speedup = False

        self.profile_passes = passes
        self.status = status
        self.path = path
        self.text_rows = self.statusToText()

    # Visualize all the selected loops into a table
    def dumpDepCoverageTable(self, coverage_filename="coverage.csv"):
        remedy_list = ["smtx-lamp-remedy",
                        "comm-libs-remedy",
                        "mem-ver-remedy",
                        "priv-remedy",
                        "loaded-value-pred-remedy",
                        "ctrl-spec-remedy",
                        "counted-iv-remedy",
                        "txio-remedy",

                        "locality-readonly-remedy",
                        "locality-redux-remedy",
                        "locality-local-remedy",
                        "locality-private-remedy",
                        "locality-separated-remedy",
                        "locality-subheaps-remedy",
                        "locality-aa-remedy",

                        "redux-remedy",
                        "mem-spec-aa-remedy",
                        "smtx-remedy",
                        "loop-fission-remedy",
                        "ptr-residue-remedy",
                        "header-phi-pred-remedy",

                        "replicable-stage-remedy"]

        dep_type_list = ["ctrl", "raw", "waw", "war", "reg"]

        table_rows = []

        first_row = ['Dep Type']
        for dep_type in dep_type_list:
            first_row.extend([dep_type] * (len(remedy_list)))

        table_rows.append(first_row)
        table_rows.append(["Remedy Type/Bmark+loop"] + remedy_list * len(dep_type_list))

        if not self.has_exp:
            print("Warning: no experiment available!")
            return None

        for bmark, results in self.status.items():
            if results['Experiment'] is None:
                    continue

            loops = results['Experiment']['loops']

            table_row = [bmark]
            for dep_type in dep_type_list:

                for remedy_name in remedy_list:
                    avail_cnt = 0
                    chosen_cnt = 0
                    # for loops that are selected, merge the stats
                    for loop_name, loop_info in loops.items():

                        # not selected loops
                        if "selected" not in loop_info or not loop_info["selected"]:
                            continue

                        # Bad dependence info
                        if "dependence_info" not in loop_info or not loop_info["dependence_info"]:
                            continue

                        dep_info = loop_info["dependence_info"]

                    if remedy_name in dep_info['avail_count'][dep_type]:
                        avail_cnt += dep_info['avail_count'][dep_type][remedy_name]
                    if remedy_name in dep_info['chosen_count'][dep_type]:
                        chosen_cnt += dep_info['chosen_count'][dep_type][remedy_name]

                    table_row.append(str(chosen_cnt) + "/" + str(avail_cnt))

            table_rows.append(table_row)

        # print
        csv_fullpath = path_join(self.path, coverage_filename)
        transposed_rows = map(list, zip(*table_rows))

        with open(csv_fullpath, 'w') as csv_fd:
            writer = csv.writer(csv_fd)
            writer.writerows(transposed_rows)

    def statusToText(self):
        text_rows = []

        title_line = ['Benchmarks'] + self.profile_passes
        if self.has_exp:
            title_line += ['Estimated Speedup', 'Worker Count', 'Exec Time Coverage', 'Loop Info']
        if self.has_real_speedup:
            title_line += ['Average Sequential Time', 'Average Parallel Time', 'Real Speedup']

        text_rows.append(title_line)

        for bmark, results in self.status.items():
            result_vis = [bmark]

            # visualize profs
            for idx, prof in enumerate(self.profile_passes):
                if results[prof]:
                    result_vis.append("y")
                else:
                    result_vis.append("n")

            #  visualize experiment
            if self.has_exp:
                if results['Experiment'] is None:
                    result_vis.extend(["-"] * 4)

                else:
                    result_exp = results['Experiment']

                    speedup = result_exp['speedup']
                    worker_cnt = result_exp['worker_cnt']
                    total_coverage = result_exp['total_coverage']
                    loops = result_exp['loops']

                    result_vis.append("%.2f" % speedup)
                    result_vis.append("%d" % worker_cnt)
                    result_vis.append("%.2f%%" % total_coverage)

                    # {loop_name: {"exec_coverage":float,
                    #              "exec_time": int, "total_time": int,
                    #              "total_lcDeps":int,
                    #              "covered_lcDeps", "lcDeps_coverage": float,
                    #              "stage": str,
                    #              "loop_speedup": float, "debug_info": str
                    #              "selected": bool
                    #             }}
                    loops_vis = ""
                    for loop_name, loop_info in loops.items():
                        loop_vis = ""
                        if "selected" in loop_info and loop_info["selected"]:
                            sel_ch = '-'
                        else:
                            sel_ch = 'X'
                        loop_vis += " %s %s(%s):" % (sel_ch, loop_name, loop_info["debug_info"])

                        if "loop_speedup" in loop_info:
                            loop_vis += "speedup = %.2fx;" % loop_info["loop_speedup"]

                        if "stage" in loop_info:
                            loop_vis += "stage = %s;" % loop_info["stage"]

                        if "total_lcDeps" in loop_info:
                            loop_vis += "covered (%d/%d) = %.2f LC Deps;" % (
                                loop_info["covered_lcDeps"], loop_info["total_lcDeps"],
                                loop_info["lcDeps_coverage"]
                            )

                        if "total_time" in loop_info:
                            loop_vis += "counts for (%d/%d) = %.2f%%" % (
                                loop_info["exec_time"], loop_info["total_time"],
                                loop_info["exec_coverage"]
                            )
                        loop_vis += "\n"
                        loops_vis += loop_vis
                    result_vis.append(loops_vis)

            if self.has_real_speedup:
                if results['RealSpeedup'] is None:
                    result_vis.extend(["-"] * 3)
                else:
                    real_speedup = results['RealSpeedup']
                    if "seq_time" not in real_speedup:
                        seq_time = "-"
                    else:
                        seq_time = real_speedup['seq_time']
                    if "para_time" not in real_speedup:
                        para_time = "-"
                    else:
                        para_time = real_speedup['para_time']
                    if "speedup" not in real_speedup:
                        speedup = "-"
                    else:
                        speedup = real_speedup['speedup']
                    result_vis.extend([seq_time, para_time, speedup])

            text_rows.append(result_vis)

        return text_rows

    def statusToSlack(self, threshold=1.5):
        result_list = []
        if self.has_exp:
            for bmark, results in self.status.items():
                if results['Experiment'] is None:
                    continue
                speedup = results['Experiment']['speedup']
                worker_cnt = results['Experiment']['worker_cnt']
                if speedup > threshold:
                    result_list.append((bmark, speedup, worker_cnt))

        return result_list

    # Generate a PDF file to show the result
    def dumpPDF(self, pdf_filename="status.pdf"):
        raise NotImplementedError()

    # Generate a HTML file to show the result
    def dumpHTML(self, html_filename="status.html"):
        raise NotImplementedError()

    # Generate a CSV file to show the result
    def dumpCSV(self, csv_filename='status.csv'):
        csv_fullpath = path_join(self.path, csv_filename)

        with open(csv_fullpath, 'w') as csv_fd:
            writer = csv.writer(csv_fd)
            writer.writerows(self.text_rows)


if __name__ == '__main__':
    import sys
    import os
    from ResultParser import parseExp

    if len(sys.argv) != 2:
        print("Use python ReportVisualizer.py benchmark.*.dump for testing")
        exit()

    filename = sys.argv[1]
    if not os.path.isfile(filename):
        print("Dump file doesn't exist")
        exit()

    with open(filename, 'r') as fd:
        lines = fd.readlines()

    status4test = parseExp(lines, "test")
    status = {"test": {"Experiment": status4test}}
    # print(status)

    reVis = ReportVisualizer(bmarks=['test'], passes=['Experiment'], status=status, path="./")
    reVis.dumpDepCoverageTable()
