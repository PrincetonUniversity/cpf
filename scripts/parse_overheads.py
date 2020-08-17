#!/usr/bin/env python

import os
import re
import math
import argparse

def convert_size(size_bytes):
   if size_bytes == 0:
       return "0B"
   size_name = ("B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB")
   i = int(math.floor(math.log(size_bytes, 1024)))
   p = math.pow(1024, i)
   s = round(size_bytes / p, 2)
   return "%s %s" % (s, size_name[i])

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-f", "--file", type=str,
                        default="parallel.out", help="File to parse")
    parser.add_argument("-n", "--num_cores", type=int,
                        default=24, help="Number of cores")
    parser.add_argument("-p", "--privateer", action='store_true',
                        help="Using privateer timings")
    args = parser.parse_args()

    return args

if __name__ == '__main__':
    args = parse_args()
    filename = args.file
    num_cores = args.num_cores

    fd = open(filename, 'r')
    lines = fd.readlines()

    num_invocations = 0
    num_workers = 0
    num_stuff_re = re.compile('\*\*\* WORKER ([0-9]+) @invocation ([0-9]+) times \*\*\*')
    for line in lines:
        num_stuff_match = num_stuff_re.search(line)
        if num_stuff_match is not None:
            if int(num_stuff_match.group(1)) > num_workers:
                num_workers = int(num_stuff_match.group(1))
    num_workers += 1 # zero indexed

    for line in reversed(lines):
        num_stuff_match = num_stuff_re.search(line)
        if num_stuff_match is not None:
            num_invocations = int(num_stuff_match.group(2))
            break

    worker_spawn_sum = 0
    main_join_sum = 0
    total_invoc_sum = 0
    setup_sum = 0
    on_iter_sum = 0
    off_iter_sum = 0
    priv_read_sum = 0
    priv_write_sum = 0
    inter_checkpoint_sum = 0
    final_checkpoint_sum = 0

    inter_checkpoint_time_sum = 0
    prepare_ckpt_sum = 0
    unprepare_ckpt_sum = 0
    acquire_lock_sum = 0
    redux_to_partial_sum = 0
    redux_commit_to_partial_sum = 0
    private_to_partial_sum = 0
    private_commit_to_partial_sum = 0
    killpriv_to_partial_sum = 0
    killpriv_commit_to_partial_sum = 0

    # number of bytes written and read
    priv_read_set_sum = 0
    priv_write_set_sum = 0

    worker_spawn_re = re.compile('Worker spawn time:\s+(.+)')
    main_join_re = re.compile('Main wait join:\s+(.+)')
    total_invoc_re = re.compile('Worker invocation time:\s+(.+)')
    if ( args.privateer ):
        setup_re = re.compile('-- Worker setup time:\s+(.+)')
    else:
        setup_re = re.compile('-- Worker invocation setup time:\s+(.+)')
    on_iter_re = re.compile('---- Worker on iteration time:\s+(.+)')
    off_iter_re = re.compile('---- Worker off iteration time:\s+(.+)')
    priv_read_re = re.compile('---- Worker time in private read:\s+(.+)')
    priv_write_re = re.compile('---- Worker time in private write:\s+(.+)')
    inter_checkpoint_re = re.compile('-- Worker between iteration time:\s+(.+)')
    final_checkpoint_re = re.compile('-- Worker final checkpoint time:\s+(.+)')

    # probably wanna take this out later after debugging is done
    inter_checkpoint_time_re = re.compile('---- Worker intermediate checkpoint time:\s+(.+)')
    prepare_ckpt_re = re.compile('------ Worker prepare checkpoint time:\s+(.+)')
    unprepare_ckpt_re = re.compile('------ Worker unprepare checkpoint time:\s+(.+)')
    acquire_lock_re = re.compile('------ Worker acquire lock time:\s+(.+)')
    redux_to_partial_re = re.compile('------ Worker redux to partial:\s+(.+)')
    redux_commit_to_partial_re = re.compile('------ Committed redux to partial:\s+(.+)')
    private_to_partial_re = re.compile('------ Worker private to partial:\s+(.+)')
    private_commit_to_partial_re = re.compile('------ Committed private to partial:\s+(.+)')
    killpriv_to_partial_re = re.compile('------ Worker killpriv to partial:\s+(.+)')
    killpriv_commit_to_partial_re = re.compile('------ Committed killpriv to partial:\s+(.+)')

    # read and write sets
    priv_read_set_re = re.compile('Number of private bytes read:\s+(.+)')
    priv_write_set_re = re.compile('Number of private bytes written:\s+(.+)')

    for line in lines:
        worker_spawn_match = worker_spawn_re.search(line)
        main_join_match = main_join_re.search(line)
        total_invoc_match = total_invoc_re.search(line)
        setup_match = setup_re.search(line)
        on_iter_match = on_iter_re.search(line)
        off_iter_match = off_iter_re.search(line)
        priv_read_match = priv_read_re.search(line)
        priv_write_match = priv_write_re.search(line)
        inter_checkpoint_match = inter_checkpoint_re.search(line)
        final_checkpoint_match = final_checkpoint_re.search(line)

        inter_checkpoint_time_match = inter_checkpoint_time_re.search(line)
        prepare_ckpt_match = prepare_ckpt_re.search(line)
        unprepare_ckpt_match = unprepare_ckpt_re.search(line)
        acquire_lock_match = acquire_lock_re.search(line)
        redux_to_partial_match = redux_to_partial_re.search(line)
        redux_commit_to_partial_match = redux_commit_to_partial_re.search(line)
        private_to_partial_match = private_to_partial_re.search(line)
        private_commit_to_partial_match = private_commit_to_partial_re.search(line)
        killpriv_to_partial_match = killpriv_to_partial_re.search(line)
        killpriv_commit_to_partial_match = killpriv_commit_to_partial_re.search(line)

        priv_read_set_match = priv_read_set_re.search(line)
        priv_write_set_match = priv_write_set_re.search(line)

        if worker_spawn_match is not None:
            worker_spawn_sum += int(worker_spawn_match.group(1))
        if main_join_match is not None:
            main_join_sum += int(main_join_match.group(1))
        if total_invoc_match is not None:
            total_invoc_sum += int(total_invoc_match.group(1))
        if setup_match is not None:
            setup_sum += int(setup_match.group(1))
        if on_iter_match is not None:
            on_iter_sum += int(on_iter_match.group(1))
        if off_iter_match is not None:
            off_iter_sum += int(off_iter_match.group(1))
        if priv_read_match is not None:
            priv_read_sum += int(priv_read_match.group(1))
        if priv_write_match is not None:
            priv_write_sum += int(priv_write_match.group(1))
        if inter_checkpoint_match is not None:
            inter_checkpoint_sum += int(inter_checkpoint_match.group(1))
        if final_checkpoint_match is not None:
            final_checkpoint_sum += int(final_checkpoint_match.group(1))

        if inter_checkpoint_time_match is not None:
            inter_checkpoint_time_sum += int(inter_checkpoint_time_match.group(1))
        if prepare_ckpt_match is not None:
            prepare_ckpt_sum += int(prepare_ckpt_match.group(1))
        if unprepare_ckpt_match is not None:
            unprepare_ckpt_sum += int(unprepare_ckpt_match.group(1))
        if acquire_lock_match is not None:
            acquire_lock_sum += int(acquire_lock_match.group(1))
        if redux_to_partial_match is not None:
            redux_to_partial_sum += int(redux_to_partial_match.group(1))
        if redux_commit_to_partial_match is not None:
            redux_commit_to_partial_sum += int(redux_commit_to_partial_match.group(1))
        if private_to_partial_match is not None:
            private_to_partial_sum += int(private_to_partial_match.group(1))
        if private_commit_to_partial_match is not None:
            private_commit_to_partial_sum += int(private_commit_to_partial_match.group(1))
        if killpriv_to_partial_match is not None:
            killpriv_to_partial_sum += int(killpriv_to_partial_match.group(1))
        if killpriv_commit_to_partial_match is not None:
            killpriv_commit_to_partial_sum += int(killpriv_commit_to_partial_match.group(1))

        if priv_read_set_match is not None:
            priv_read_set_sum += int(priv_read_set_match.group(1))
        if priv_write_set_match is not None:
            priv_write_set_sum += int(priv_write_set_match.group(1))


    # for privateer, spawn/join overhead percentage can be calculated as follows:
    # P_spawn/join = ((worker_spawn_time/total_invocation_time +
    #                main_wait_join / (num_workers * total_invocation_time)) / num_invocations
    # or something like that
    if ( args.privateer ):
        total_invoc_sum += main_join_sum # not taken into account in runtime
        denominator = float(total_invoc_sum)
        print('*** Using privateer timers ***')
        print('Num invocations:', num_invocations)
        print('Num workers:', num_workers)
        print('spawn/join avg:', 100 * (worker_spawn_sum + main_join_sum)/ denominator)
        print('-- Worker spawn avg:', 100 * worker_spawn_sum / denominator)
        print('-- Main join avg:', 100 * main_join_sum / denominator)
        print('on iter avg:', 100 * on_iter_sum / denominator)
        print('off iter avg:', 100 * off_iter_sum / denominator)
        print('private read avg:', 100 * priv_read_sum / denominator)
        print('private write avg:', 100 * priv_write_sum / denominator)
        print('checkpoint avg:', 100 * (inter_checkpoint_sum + final_checkpoint_sum) / denominator)
        print('-- between iter (incl checkpoints) avg:', 100 * inter_checkpoint_sum / denominator)
        print('-- final checkpoint avg:', 100 * final_checkpoint_sum / denominator)
        total_percentage = worker_spawn_sum + main_join_sum + on_iter_sum + off_iter_sum + \
                        priv_read_sum + priv_write_sum + inter_checkpoint_sum + \
                        final_checkpoint_sum
        total_percentage *= 100
        total_percentage /= denominator
        print('Total avg:', total_percentage)
    else:
        denominator = float(total_invoc_sum)
        print('*** Using LSD timers ***')
        print('Num invocations:', num_invocations)
        print('Num workers:', num_workers)
        print('Worker setup avg:', 100 * setup_sum / denominator)
        print('on iter avg:', 100 * on_iter_sum / denominator)
        print('off iter avg:', 100 * off_iter_sum / denominator)
        print('private read avg:', 100 * priv_read_sum / denominator)
        print('private write avg:', 100 * priv_write_sum / denominator)
        print('checkpoint avg:', 100 * (inter_checkpoint_sum + final_checkpoint_sum) / denominator)
        print('-- between iter (incl checkpoints) avg:', 100 * inter_checkpoint_sum / denominator)
        print('-- final checkpoint avg:', 100 * final_checkpoint_sum / denominator)
        total_percentage = setup_sum + main_join_sum + on_iter_sum + off_iter_sum + \
                        priv_read_sum + priv_write_sum + inter_checkpoint_sum + \
                        final_checkpoint_sum
        total_percentage *= 100
        total_percentage /= denominator
        print('Total avg:', total_percentage)

    # print('acquiring lock/intermediate ckpt avg:', 100 * acquire_lock_sum / inter_checkpoint_time_sum)
    # print('prepare ckpt/intermediate ckpt avg:', 100 * prepare_ckpt_sum / inter_checkpoint_time_sum)
    # print('unprepare ckpt/intermediate ckpt avg:', 100 * unprepare_ckpt_sum / inter_checkpoint_time_sum)
    # print('redux worker to partial/intermediate ckpt avg:', 100 * redux_to_partial_sum / inter_checkpoint_time_sum)
    # print('redux committed to partial/intermediate ckpt avg:', 100 * redux_commit_to_partial_sum / inter_checkpoint_time_sum)
    # print('private worker to partial/intermediate ckpt avg:', 100 * private_to_partial_sum / inter_checkpoint_time_sum)
    # print('private committed to partial/intermediate ckpt avg:', 100 * private_commit_to_partial_sum / inter_checkpoint_time_sum)
    # print('killpriv worker to partial/intermediate ckpt avg:', 100 * killpriv_to_partial_sum / inter_checkpoint_time_sum)
    # print('killpriv committed to partial/intermediate ckpt avg:', 100 * killpriv_commit_to_partial_sum / inter_checkpoint_time_sum)

    print('Number of private bytes read:', convert_size(priv_read_set_sum))
    print('Number of private bytes written:', convert_size(priv_write_set_sum))
