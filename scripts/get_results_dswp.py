# Python 3
#
# Ziyang Xu
# Feb 28, 2019 Created
# Apr 27, 2019 Updated
#
# Generate all profilings in parallel
# Please follow README.md in the sciprt/ directory

import argparse
import json
import os
import subprocess
import shutil
from joblib import Parallel, delayed
from collections import ChainMap
from termcolor import colored
import datetime
import time

import SlackBot
from ReportVisualizer import ReportVisualizer
from ResultParser import parseExp


def clean_all_bmarks(root_path, bmark_list, exp_only):
    if exp_only:
        clean_tgt = "clean-exp"
    else:
        clean_tgt = "clean"
    for bmark in bmark_list:
        os.chdir(os.path.join(root_path, bmark, "src"))
        make_process = subprocess.Popen(["make", clean_tgt],
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.STDOUT)
        if make_process.wait() != 0:
            print(colored("Clean failed for %s" % bmark, 'red'))
            return -1

    if exp_only:
        print("Finish clean all experiments(dump)")
    else:
        print("Finish clean all benchmarks")
    return 0


def get_one_prof(root_path, bmark, profile_name, profile_recipe):
    print("Generating %s on %s " % (profile_name, bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))
    start_time = time.time()
    make_process = subprocess.Popen(["make", profile_recipe],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.STDOUT)

    if make_process.wait() != 0:
        elapsed = time.time() - start_time
        print(colored("%s failed for %s , took %.4fs" % (profile_name, bmark, elapsed), 'red'))
        return False
    else:
        elapsed = time.time() - start_time
        print(colored("%s success for %s, took %.4fs" % (profile_name, bmark, elapsed), 'green'))
        return True


# ZY - check whether all profilings are there;
# if remake_profile == True, ignore remake them by the Makefile, else abort
def get_exp_result(root_path, bmark, result_path, remake_profile=False):
    print("Generating Experiment results on %s " % (bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))

    exp_name = "benchmark.collaborative-pipeline.dump"

    # Check LAMP, SLAMP, HEADERPHI, and SPECPRIV
    if remake_profile:
        if not os.path.isfile("benchmark.lamp.out"):
            print(colored("No LAMP for %s, will remake" % bmark, 'red'))
        if not os.path.isfile("benchmark.result.slamp.profile"):
            print(colored("No SLAMP for %s, will remake" % bmark, 'red'))
        if not os.path.isfile("benchmark.specpriv-profile.out"):
            print(colored("No specpriv for %s, will remake " % bmark, 'red'))
    else:
        if not os.path.isfile("benchmark.lamp.out"):
            print(colored("No LAMP for %s, abort!" % bmark, 'red'))
            return None
        if not os.path.isfile("benchmark.result.slamp.profile"):
            print(colored("No SLAMP for %s, abort!" % bmark, 'red'))
            return None
        if not os.path.isfile("benchmark.specpriv-profile.out"):
            print(colored("No SpecPriv for %s, abort" % bmark, 'red'))
            return None

    # 22 APR don't need headerphi because not needed anymore
    # if not os.path.isfile("benchmark.headerphi_prof.out"):
    #    print(colored("No headerphi for benchmark"+bmark, 'red'))

    # Force redo; 22 APR don't do that, have -x option
    # if os.path.isfile(exp_name):
    #    os.remove(exp_name)

    start_time = time.time()
    make_process = subprocess.Popen(["make", exp_name],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.STDOUT)

    if make_process.wait() != 0:
        elapsed = time.time() - start_time
        print(colored("Experiment failed for %s, took %.4fs" % (bmark, elapsed), 'red'))
        return None
    else:
        elapsed = time.time() - start_time
        with open(exp_name, 'r') as fd:
            lines = fd.readlines()

        # Parse experiment results
        parsed_result = parseExp(lines, bmark)

        # Create a backup
        shutil.copy(exp_name, os.path.join(result_path, bmark + "." + exp_name))

        print(colored("Experiment success for %s, took %.4fs" % (bmark, elapsed), 'green'))
        return parsed_result


def get_all_passes(root_path, bmark, passes, result_path):
    status = {}
    if "Edge" in passes:
        status["Edge"] = get_one_prof(root_path, bmark, 'Edge Profile', "benchmark.edgeProf.out")
    if "Loop" in passes:
        status["Loop"] = get_one_prof(root_path, bmark, 'Loop Profile', "benchmark.loopProf.out")
    if "LAMP" in passes:
        status["LAMP"] = get_one_prof(root_path, bmark, 'LAMP', "benchmark.lamp.out")
    if "SLAMP" in passes:
        status["SLAMP"] = get_one_prof(root_path, bmark, 'SLAMP', "benchmark.result.slamp.profile")
    if "SpecPriv" in passes:
        status["SpecPriv"] = get_one_prof(root_path, bmark, 'SpecPriv Profile', "benchmark.specpriv-profile.out")
    if "HeaderPhi" in passes:
        status["HeaderPhi"] = get_one_prof(root_path, bmark, 'HeaderPhi Profile', "benchmark.headerphi_prof.out")
    if "Experiment" in passes:
        if len(passes) == 1:  # only experiment
            status["Experiment"] = get_exp_result(root_path, bmark, result_path, remake_profile=True)
        else:
            status["Experiment"] = get_exp_result(root_path, bmark, result_path, remake_profile=False)

    # Generate a json on the fly
    os.chdir(result_path)
    with open("status_" + bmark + ".json", "w") as fd:
        json.dump(status, fd)

    return {bmark: status}


def get_benchmark_list_from_suite(suite, bmark_list):
    # Get suite configuration from json
    if suite == "All":
        suite_list = [k for k, v in bmark_list.items() if v["available"]]
    else:
        suite_list = [k for k, v in bmark_list.items(
        ) if suite in v["suites"] and v["available"]]
    return suite_list


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--root_path", type=str, required=True,
                        help="Root path of CPF benchmark directory")
    parser.add_argument("-b", "--bmark_list", type=str,
                        required=True, help="Path of benchmark json file")

    parser.add_argument("-n", "--core_num", type=int,
                        default=4, help="Core number")
    parser.add_argument("-r", "--remove_old", action="store_true",
                        help="If set, do `make clean` in all testing benchmarks")
    parser.add_argument("-x", "--remove_old_exp", action="store_true",
                        help="If set, do `make clean-exp` in all testing benchmarks")
    parser.add_argument("-o", "--post_slack", action="store_true",
                        help="If set, results will be posted to slack")

    parser.add_argument("-s", "--suite", type=str, choices=[
        'All', 'Spec', 'SpecFP', 'SpecInt',
        'PolyBench', 'PARSEC', 'MediaBench', 'Toys',
        'MiBench', 'Trimaran', 'Utilities', 'MicroBench'],
        help="Choose specific test suite")

    parser.add_argument("-c", "--use_custom",
                        action="store_true", help="Use customize list")
    parser.add_argument("-l", "--custom_list", nargs='+',
                        default=[], help="List of strings (name of benchmarks)")
    args = parser.parse_args()

    if (args.use_custom):
        bmark_list = args.custom_list
        print("Warning: You are responsible for the correctness of list")
        # assert check_benchmark_list(bmark_list) # check if all strings in the benchmark suite
    else:
        assert args.suite
        with open(args.bmark_list, 'r') as fd:
            bmark_list = json.load(fd)
        bmark_list = get_benchmark_list_from_suite(args.suite, bmark_list)

    return args.root_path, args.core_num, args.remove_old, args.remove_old_exp, args.post_slack, bmark_list


if __name__ == "__main__":
    root_path, core_num, remove_old, remove_old_exp, post_slack, bmark_list = parse_args()
    print("Root path set as: %s" % root_path)
    print("Core number set as: %d" % core_num)
    print("Do make clean or not:", remove_old)
    print("Do make clean-exp or not:", remove_old_exp)
    print("Running test on:", bmark_list)
    print("\n\n")

    if remove_old:
        clean_all_bmarks(root_path, bmark_list, exp_only=False)

    if remove_old_exp:
        clean_all_bmarks(root_path, bmark_list, exp_only=True)

    # Check and create /results
    today = datetime.date.today()
    result_path = os.path.join(root_path, "results", today.isoformat())
    if not os.path.exists(result_path):
        os.makedirs(result_path)

    # TODO: Create Log file

    passes = ["Edge", "Loop", "LAMP", "SLAMP", "SpecPriv", "Experiment"]
    # passes = ["Edge", "Loop", "LAMP", "SLAMP", "SpecPriv", "HeaderPhi", "Experiment"]

    status_list = Parallel(n_jobs=core_num)(delayed(get_all_passes)(
        root_path, bmark, passes, result_path) for bmark in bmark_list)
    status = dict(ChainMap(*status_list))

    reVis = ReportVisualizer(bmarks=bmark_list, passes=passes, status=status, path=result_path)
    reVis.dumpCSV()

    if post_slack:
        result_list = reVis.statusToSlack(threshold=1.2)
        SlackBot.post_dswp_result(result_list)
