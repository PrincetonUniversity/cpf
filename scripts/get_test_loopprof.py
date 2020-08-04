# Python 3
#
# Ziyang Xu
# July 31, 2019 Created
#
# Generate loop profilings for test_args in parallel

import argparse
import json
import os
import subprocess
import shutil
from joblib import Parallel, delayed
from collections import ChainMap
import re
import time
from termcolor import colored
from pprint import pprint
from ResultParser import parseExp


# Get runtime percentage of each loop
# Get loop selection from benchmark dump
def parse_loop_percent(lines, bmark):

    loops = {}
    # - MAIN__ :: L90 (filename:swim.c, line:416, col:10)    Time 3378 / 3435 Coverage: 98.3%
    loop_info_re = re.compile(
        r' - (.+) \((.+)\).*Time ([0-9]+) / ([0-9]+) Coverage: ([0-9\.]+)%')
    loop_info_nodebug_re = re.compile(
        r' - (.+).*Time ([0-9]+) / ([0-9]+) Coverage: ([0-9\.]+)%')

    for line in lines:
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

    return loops


def clean_all_bmarks(root_path, bmark_list):
    for bmark in bmark_list:
        os.chdir(os.path.join(root_path, bmark, "src"))
        if os.path.isfile("benchmark.test-loopProf.parsed.out"):
            os.remove("benchmark.test-loopProf.parsed.out")

        if os.path.isfile("benchmark.test-loopProf.out"):
            os.remove("benchmark.test-loopProf.out")

    print("Finish cleaning")
    return 0


def get_test_loop_prof(root_path, bmark, result_path):

    profile_name = "Test LoopProf"
    profile_recipe = "benchmark.test-loopProf.parsed.out"
    print("Generating %s on %s " % (profile_name, bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))
    start_time = time.time()
    make_process = subprocess.Popen(["make", profile_recipe],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.STDOUT)
    if make_process.wait() != 0:
        elapsed = time.time() - start_time
        print(colored("%s failed for %s , took %.4fs" % (profile_name, bmark, elapsed), 'red'))
        return None
    else:
        elapsed = time.time() - start_time
        print(colored("%s succeeded for %s, took %.4fs" % (profile_name, bmark, elapsed), 'green'))

        with open(profile_recipe, 'r') as fd:
            lines = fd.readlines()

        # Parse experiment results
        parsed_result = parse_loop_percent(lines, bmark)

        # Create a backup
        shutil.copy(profile_recipe, os.path.join(result_path, bmark + "." + profile_recipe))
        return parsed_result


# ZY - check whether all profilings are there;
# if remake_profile == True, ignore remake them by the Makefile, else abort
def get_exp_result(root_path, bmark, result_path):
    print("Generating Experiment results on %s " % (bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))

    exp_name = "benchmark.collaborative-pipeline.dump"

    # Check LAMP, SLAMP, HEADERPHI, and SPECPRIV
    if not os.path.isfile("benchmark.lamp.out"):
        print(colored("No LAMP for %s, abort!" % bmark, 'red'))
        return None
    # if not os.path.isfile("benchmark.result.slamp.profile"):
    #     print(colored("No SLAMP for %s, abort!" % bmark, 'red'))
    #     return None
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

        print(colored("Experiment succeeded for %s, took %.4fs" % (bmark, elapsed), 'green'))
        return parsed_result


def get_test_loop_info(root_path, bmark, result_path):
    dump_result = get_exp_result(root_path, bmark, result_path)
    test_loops = get_test_loop_prof(root_path, bmark, result_path)
    if not dump_result:
        print("Dump cannot be generated for %s, abort")
        return None

    if not test_loops:
        print("Test LoopProf cannot be generated for %s, abort")
        return None

    if "loops" not in dump_result:
        print("Dump failed for %s, abort" % bmark)
        return None

    loops = dump_result['loops']

    total_time = 0
    covered_time = 0
    covered_percentage = 0
    selected_loops = []

    for name in loops:
        if "selected" in loops[name] and loops[name]["selected"]:
            selected_loops.append(name)
            if name not in test_loops:
                print("Dump and test loops don't match for %s, abort" % bmark)
                return None

            total_time = test_loops[name]["total_time"]
            covered_time += test_loops[name]["exec_time"]
            covered_percentage += test_loops[name]["exec_coverage"]

    return {bmark: {"total_time": total_time, "covered_time": covered_time,
                    "covered_percentage": covered_percentage,
                    "selected_loops": selected_loops,
                    "all_loops_info": test_loops}}


def get_benchmark_list_from_suite(suite, bmark_list):
    # Get suite configuration from json
    if suite == "All":
        suite_list = [k for k, v in bmark_list.items() if v["available"]]
    else:
        suite_list = [k for k, v in bmark_list.items() if suite in v["suites"] and v["available"]]
    return suite_list


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--root-path", type=str, required=True,
                        help="Root path of CPF benchmark directory")
    parser.add_argument("-r", "--result-path", type=str, required=True,
                        help="Result path")
    parser.add_argument("-b", "--bmark-list", type=str,
                        required=True, help="Path of benchmark json file")
    parser.add_argument("-n", "--core-num", type=int,
                        default=4, help="Core number")
    parser.add_argument("-s", "--suite", type=str, choices=[
        'All', 'Spec', 'SpecFP', 'SpecInt',
        'PolyBench', 'PARSEC', 'MediaBench', 'Toys',
        'MiBench', 'Trimaran', 'Utilities', 'MicroBench'],
        help="Choose specific test suite")

    args = parser.parse_args()

    assert args.suite, "No suite specify"
    with open(args.bmark_list, 'r') as fd:
        bmark_list = json.load(fd)
    bmark_list = get_benchmark_list_from_suite(args.suite, bmark_list)

    config = {}
    config['root_path'] = args.root_path
    config['core_num'] = args.core_num
    config["result_path"] = args.result_path
    config["bmark_list"] = bmark_list
    return config


if __name__ == "__main__":
    config = parse_args()
    if not config:
        print("Bad args, let's start over, good luck!")
        quit()

    print("\n\n### Experiment Start ###")

    # Preprocesing
    # Create result directory
    if not os.path.exists(config['result_path']):
        os.makedirs(config['result_path'])

    config['result_path'] = os.path.abspath(config['result_path'])

    # Clean old artifacts
    clean_all_bmarks(config['root_path'], config['bmark_list'])

    # Finish till experiment
    status_list = Parallel(n_jobs=config['core_num'])(delayed(get_test_loop_info)(
        config['root_path'], bmark, config['result_path']) for bmark in config['bmark_list'])
    status = dict(ChainMap(*status_list))

    os.chdir(config['result_path'])
    with open("test_loop_percent.json", "w") as fd:
        json.dump(status, fd)

    pprint(status)

    for key, value in status.items():
        print("%s\t%s%%" % (key, value['covered_percentage'] * 100))
