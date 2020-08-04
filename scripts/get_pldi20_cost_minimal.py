# Python 3
#
# Ziyang Xu
# Nov 20, 2019 Created
#
# Special script for PLDI20
# Get cost of UO check, Private Reads and Writes

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

import git

import SlackBot
from ReportVisualizer import ReportVisualizer
from ResultParser import parseExp


def clean_all_bmarks(root_path, bmark_list, reg_option):
    # 0: remake all
    # 1: use profiling
    # 2: 1 + use sequential
    # 3: only clean sequential time
    # 4: only parallel time

    if reg_option == 0:
        clean_tgt = "clean"
    elif reg_option == 1:
        clean_tgt = "clean-exp"
    elif reg_option == 2:
        clean_tgt = "clean-speed"
    elif reg_option == 3:
        clean_tgt = "clean-seq"
    elif reg_option == 4:
        clean_tgt = "clean-para"
    else:
        assert False, "Regression option not valid"

    for bmark in bmark_list:
        os.chdir(os.path.join(root_path, bmark, "src"))
        make_process = subprocess.Popen(["make", clean_tgt],
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.STDOUT)
        if make_process.wait() != 0:
            print(colored("Clean failed for %s" % bmark, 'red'))

    print("Finish cleaning")
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
        print(colored("%s succeeded for %s, took %.4fs" % (profile_name, bmark, elapsed), 'green'))
        return True


def get_cost(root_path, bmark, result_path):
    print("Generating Experiment results on %s " % (bmark))

    os.chdir(os.path.join(root_path, bmark, "src"))

    exp_name = "cost.out"

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
        return False
    else:
        elapsed = time.time() - start_time

        # Create a backup
        shutil.copy(exp_name, os.path.join(result_path, bmark + "." + exp_name))

        print(colored("Experiment succeeded for %s, took %.4fs" % (bmark, elapsed), 'green'))
        return True


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
    if "Cost" in passes:
        status["Cost"] = get_cost(root_path, bmark, result_path)

    return {bmark: status}


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
    parser.add_argument("-b", "--bmark-list", type=str,
                        required=True, help="Path of benchmark json file")

    parser.add_argument("-n", "--core-num", type=int,
                        default=4, help="Core number")
    # parser.add_argument("-x", "--remove_old_exp", action="store_true",
    #                     help="If set, do `make clean-exp` in all testing benchmarks")

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
    config["bmark_list"] = bmark_list
    return config


def confirm_and_memo(config):
    # Get CPF and Regression Git Hash
    if 'LIBERTY_OBJ_DIR' in os.environ:
        config['obj_path'] = os.environ['LIBERTY_OBJ_DIR']
    else:
        print('Environment variables are not properly set up')
        print('Have you sourced cpf_environ.rc?')
        return False

    try:
        repo_bmark = git.Repo(config['root_path'])
    except git.exc.InvalidGitRepositoryError:
        print("Regression ROOT is not under Git, please check again!")
        return False

    try:
        repo_cpf = git.Repo(config['obj_path'], search_parent_directories=True)
    except git.exc.InvalidGitRepositoryError:
        print("CPF Framework is not under Git, please check again!")
        return False

    # Get 8 byte Git history
    sha_bmark = repo_bmark.head.object.hexsha
    config['sha_bmark'] = repo_bmark.git.rev_parse(sha_bmark, short=8)
    sha_cpf = repo_cpf.head.object.hexsha
    config['sha_cpf'] = repo_cpf.git.rev_parse(sha_cpf, short=8)
    config['branch_cpf'] = repo_cpf.active_branch.name

    # Results directory
    dt = datetime.datetime.now()
    config['result_path'] = os.path.join(config['root_path'], "results", dt.strftime('%Y-%m-%d-%H-%M'))


    # Preview configuration
    print("\n")
    print(colored("Please make sure you've committed all changes for both CPF and Regression, otherwise the Git history hash is meanlingless!", 'red'))
    print(colored("##### Configurations #####", 'green'))
    print("Benchmark root path: %s, with Git history: %s" %
          (colored(str(config['root_path']), 'yellow'), colored(str(config['sha_bmark']), 'yellow')))
    print("CPF object directory: %s, on branch %s, with Git history: %s" %
          (colored(str(config['obj_path']), 'yellow'),
           colored(str(config['branch_cpf']), 'yellow'),
           colored(str(config['sha_cpf']), 'yellow')))
    print("Core number: %s" % colored(str(config['core_num']), 'yellow'))
    print("Running test on %s benchmarks %s :" % (colored(str(len(config['bmark_list'])), 'yellow'), colored(str(config['bmark_list']), 'yellow')))

    print("Store results under directory: %s" % colored(config['result_path'], 'yellow'))
    print(colored("#### End of Configurations ####", 'green'))

    # Confirmation
    # Ask for memo
    while True:
        confirm = input("Continue with configurations above? (y/n) : ")
        if confirm == 'y':
            config['memo'] = input("A short note of this test: ")
            return config
        elif confirm == 'n':
            return False


if __name__ == "__main__":
    passes = ["Edge", "Loop", "LAMP", "SpecPriv", "Cost"]
    # passes = ["Edge", "Loop", "LAMP", "SLAMP", "SpecPriv", "HeaderPhi", "Experiment"]

    config = parse_args()

    config = confirm_and_memo(config)
    if not config:
        print("Negative confirmation or bad setup, let's start over, good luck!")
        quit()

    print("\n\n### Experiment Start ###")
    # Create a log with date + memo + configuration
    log_path = config['result_path'] + ".log"
    print("Creating log at %s" % log_path)
    with open(log_path, "w") as fd:
        json.dump(config, fd)
    print("\n")

    # Preprocesing
    # Create result directory
    if not os.path.exists(config['result_path']):
        os.makedirs(config['result_path'])

    # Clean old artifacts
    clean_all_bmarks(config['root_path'], config['bmark_list'], 0)

    # Finish till experiment
    status_list = Parallel(n_jobs=config['core_num'])(delayed(get_all_passes)(
        config['root_path'], bmark, passes, config['result_path']) for bmark in config['bmark_list'])
    status = dict(ChainMap(*status_list))

    os.chdir(config['result_path'])
    with open("status.json", "w") as fd:
        json.dump(status, fd)

