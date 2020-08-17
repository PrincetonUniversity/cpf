# Python 3
#
# Ziyang Xu
# Aug 4, 2019 Created
#
# Get Parallel Runtime of 28 Workers
# Minimal Script

import argparse
import json
import os
import subprocess
from termcolor import colored
import datetime
import git


def clean_all_bmarks(root_path, bmark_list):
    clean_tgt = "clean-exp"

    for bmark in bmark_list:
        os.chdir(os.path.join(root_path, bmark, "src"))
        make_process = subprocess.Popen(["make", clean_tgt],
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.STDOUT)
        if make_process.wait() != 0:
            print(colored("Clean failed for %s" % bmark, 'red'))

    print("Finish cleaning")
    return 0


def get_para_time(root_path, bmark, times, num_workers=28):
    print("Try to get parallel execution time, running times is %d, test workers is %d" % (times, num_workers))
    os.chdir(os.path.join(root_path, bmark, "src"))
    exp_name = "reg_para"
    para_time_name = "parallel.time"

    time_list = []
    for run_time in range(times):
        make_process = subprocess.Popen(["make", exp_name, "REG_NUM_WORKERS=" + str(num_workers)],
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.STDOUT)

        if make_process.wait() != 0 and os.path.exists(para_time_name):
            print(colored("Parallel time failed for %s" % bmark, 'red'))
            return False
        else:
            with open(para_time_name, 'r') as fd:
                line = fd.readline()
            try:
                time_list.append(float(line))
                print("NO. %d: %.2fs" % (run_time, float(line)))
            except ValueError:
                return False

            os.remove(para_time_name)
    print(colored("Parallel time measurement succeeded for %s!" % bmark, 'green'))
    print(time_list)

    return time_list, sum(time_list) / times


def get_real_speedup(root_path, bmark, times=3, num_workers=28):
    print("Generating real speedup for %s " % (bmark))
    os.chdir(os.path.join(root_path, bmark, "src"))

    real_speedup = {}

    # Generate and parallel
    para_time_list, para_time = get_para_time(root_path, bmark, times, num_workers=num_workers)
    print("%s %.3f on %d workers" % (bmark, para_time, num_workers))
    if para_time and para_time > 0:
        real_speedup['para_time'] = round(para_time, 3)
        real_speedup['para_time_list_dict'] = {num_workers: para_time_list}
    else:
        print(colored("Oh snap! Getting execution time failed for %s!" % (bmark), 'green'))
        return None

    return real_speedup


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
    parser.add_argument("-w", "--num-workers", type=int,
                        default=28, help="Number of parallel worker")
    parser.add_argument("-t", "--test-times", type=int,
                        default=3, help="Test times for sequential and parallel version")
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
    config['test_times'] = args.test_times
    config['num_workers'] = args.num_workers
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
        repo_cpf = git.Repo(config['obj_path'], search_parent_directories=True)
    except git.exc.InvalidGitRepositoryError:
        print("CPF Framework is not under Git, please check again!")
        return False

    # Get 8 byte Git history
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
    print("Benchmark root path: %s" %
          (colored(str(config['root_path']), 'yellow')))
    print("CPF object directory: %s, on branch %s, with Git history: %s" %
          (colored(str(config['obj_path']), 'yellow'),
           colored(str(config['branch_cpf']), 'yellow'),
           colored(str(config['sha_cpf']), 'yellow')))
    print("Test times: %s" % colored(str(config['test_times']), 'yellow'))
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
    config = parse_args()

    config = confirm_and_memo(config)
    if not config:
        print("Negative confirmation or bad setup, let's start over, good luck!")
        quit()

    print("\n\n### Experiment Start ###")

    # Create result directory
    if not os.path.exists(config['result_path']):
        os.makedirs(config['result_path'])
    # Create a log with date + memo + configuration
    log_path = config['result_path'] + ".log"
    print("Creating log at %s" % log_path)
    with open(log_path, "w") as fd:
        json.dump(config, fd)
    print("\n")

    # Clean old artifacts
    # clean_all_bmarks(config['root_path'], config['bmark_list'])

    status = {}
    for bmark in config['bmark_list']:
        real_speedup = get_real_speedup(config['root_path'], bmark, config['test_times'], config['num_workers'])
        status[bmark] = {'RealSpeedup': real_speedup}

        # Dump on the fly
        os.chdir(config['result_path'])
        with open("status.json", "w") as fd:
            json.dump(status, fd)

        if real_speedup:
            print("For %s, para time: %.2f" % (bmark, real_speedup['para_time']))

    os.chdir(config['result_path'])
    with open("status.json", "w") as fd:
        json.dump(status, fd)
