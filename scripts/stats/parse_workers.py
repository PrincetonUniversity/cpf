#!/usr/bin/env python

import os
import re
import argparse
from termcolor import colored
import numpy as np
from scipy.stats.mstats import gmean
import matplotlib.pyplot as plt
import matplotlib
import utils

# test_file = '/u/gc14/cpf-benchmarks/ks/src/parallel.out'
test_file = '/Users/greg/Documents/liberty/cpf-benchmarks/ks/src/parallel.out'
# test_file = '/Users/greg/Documents/liberty/cpf-benchmarks/reg_detect/src/parallel.out'
num_workers = 0
num_invocations = 0
max_iter = 0

using_invoc = 0
using_iter = 0

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sort', action='store_true',
            help='Sort worker times before calculating geomeans')
    parser.add_argument('--no_time_invocation', action='store_false',
            help='Invocations are not timed')
    parser.add_argument('--time_iteration', action='store_true',
            help='Iterations are timed')
    args = parser.parse_args()

    return args

if __name__ == '__main__':

    args = parse_args()

    fd = open(test_file, 'r')
    lines = fd.readlines()
    fd.close()

    num_iter_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*number iterations:\s*(\d+)')
    invoc_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*invocation time:\s*(\d+)')
    iter_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*iteration\s*(\d+):\s*(\d+)')
    cons_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*consume time:\s*(\d+)')
    prod_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*produce time:\s*(\d+)')
    cons_wait_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*consume wait time:\s*(\d+)')
    prod_wait_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*produce wait time:\s*(\d+)')

    num_cons_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*number consumes:\s*(\d+)')
    num_prod_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*number produces:\s*(\d+)')
    cons_empty_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*consume empty:\s*(\d+)')
    prod_full_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*produce full:\s*(\d+)')

    num_workers, num_invocations, max_iter = utils.get_basics(lines)

    print('Number of invocations: {0}'.format(num_invocations))
    print('Number of workers: {0}'.format(num_workers))
    print('Max iterations: {0}'.format(max_iter))

    # iter_times_array = np.zeros((num_invocations, num_workers, max_iter), dtype=np.int64)
    times_array = np.zeros((num_invocations, num_workers), dtype=np.int64)
    times_array_normalized = np.zeros((num_invocations, num_workers), dtype=np.float64)
    num_iters_array = np.zeros(num_invocations, dtype=np.int64)
    cons_time_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    prod_time_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    cons_wait_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    prod_wait_array = np.zeros((num_invocations, num_workers), dtype=np.float64)

    num_cons_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    num_prod_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    cons_empty_array = np.zeros((num_invocations, num_workers), dtype=np.float64)
    prod_full_array = np.zeros((num_invocations, num_workers), dtype=np.float64)

    # In each invocation, each worker has 4(ish) portions:
    #   - [0] Actual work (TBD what this actually means)
    #   - [1] Time actually producing
    #   - [2] Time waiting for produce
    #   - [3] Time actually consuming
    #   - [4] Time waiting for consume
    #   - Time waiting for other workers to finish (maybe)
    stacked_array = np.zeros((num_invocations, num_workers, 5), np.float64)
    stacked_geomeans = np.zeros((num_workers, 5), np.float64)

    # calculated later
    geomeans = np.zeros(num_workers, dtype=np.float64)
    errors = np.zeros((2, num_workers), dtype=np.float64)
    variances = np.zeros(num_workers, dtype=np.float64)

    geomeans_prod_time = np.zeros(num_workers, dtype=np.float64)
    geomeans_cons_time = np.zeros(num_workers, dtype=np.float64)
    geomeans_prod_wait = np.zeros(num_workers, dtype=np.float64)
    variances_prod_wait = np.zeros(num_workers, dtype=np.float64)
    geomeans_cons_wait = np.zeros(num_workers, dtype=np.float64)
    variances_cons_wait = np.zeros(num_workers, dtype=np.float64)

    geomeans_cons_empty = np.zeros(num_workers, dtype=np.float64)
    geomeans_prod_full = np.zeros(num_workers, dtype=np.float64)

    for line in lines:
        iter_time_match = iter_time_re.search(line)
        invoc_time_match = invoc_time_re.search(line)
        num_iter_match = num_iter_re.search(line)
        cons_wait_time_match = cons_wait_time_re.search(line)
        prod_wait_time_match = prod_wait_time_re.search(line)
        cons_time_match = cons_time_re.search(line)
        prod_time_match = prod_time_re.search(line)

        num_cons_match = num_cons_re.search(line)
        num_prod_match = num_prod_re.search(line)
        cons_empty_match = cons_empty_re.search(line)
        prod_full_match = prod_full_re.search(line)

        # if num_iter_match is not None:
        #     num_iters_array[int(num_iter_match.group(1))-1] =  int(num_iter_match.group(3))

        # if iter_time_match is not None:
        #     invoc = int(iter_time_match.group(1))
        #     worker = int(iter_time_match.group(2))
        #     iteration = int(iter_time_match.group(3))
        #     time = int(iter_time_match.group(4))
        #     iter_times_array[invoc-1, worker, iteration-1] = time

        if invoc_time_match is not None:
            times_array[int(invoc_time_match.group(1))-1,int(invoc_time_match.group(2))] = int(invoc_time_match.group(3))

        if cons_wait_time_match is not None:
            cons_wait_array[int(cons_wait_time_match.group(1))-1,int(cons_wait_time_match.group(2))] = float(cons_wait_time_match.group(3))
        if prod_wait_time_match is not None:
            prod_wait_array[int(prod_wait_time_match.group(1))-1,int(prod_wait_time_match.group(2))] = float(prod_wait_time_match.group(3))

        if cons_time_match is not None:
            cons_time_array[int(cons_time_match.group(1))-1,int(cons_time_match.group(2))] = float(cons_time_match.group(3)) - cons_wait_array[int(cons_time_match.group(1))-1, int(cons_time_match.group(2))]
        if prod_time_match is not None:
            prod_time_array[int(prod_time_match.group(1))-1,int(prod_time_match.group(2))] = float(prod_time_match.group(3)) - prod_wait_array[int(prod_time_match.group(1))-1, int(prod_time_match.group(2))]

        if num_cons_match is not None:
            num_cons_array[int(num_cons_match.group(1))-1,int(num_cons_match.group(2))] = float(num_cons_match.group(3))
        if num_prod_match is not None:
            num_prod_array[int(num_prod_match.group(1))-1,int(num_prod_match.group(2))] = float(num_prod_match.group(3))
        if cons_empty_match is not None:
            cons_empty_array[int(cons_empty_match.group(1))-1,int(cons_empty_match.group(2))] = float(cons_empty_match.group(3))
        if prod_full_match is not None:
            prod_full_array[int(prod_full_match.group(1))-1,int(prod_full_match.group(2))] = float(prod_full_match.group(3))

    for i in range(num_invocations):
        if args.sort:
            times_array[i,:] = np.sort(times_array[i,:])
        normalization = max(times_array[i,:])
        times_array_normalized[i,:] = times_array[i,:] / float(normalization)

        for j in range(num_workers):
            # prod_wait_array[i,j] = prod_wait_array[i,j] / float(times_array[i,j])
            # cons_wait_array[i,j] = cons_wait_array[i,j] / float(times_array[i,j])
            # print('Invocation {0} -- worker {1} total time: {2}'.format(i, j, times_array[i,j]))
            # print('Invocation {0} -- worker {1} prod wait time: {2}'.format(i, j, prod_wait_array[i,j]))
            # print('Invocation {0} -- worker {1} cons wait time: {2}'.format(i, j, cons_wait_array[i,j]))
            stacked_array[i,j,2] = prod_wait_array[i,j] / float(times_array[i,j])
            stacked_array[i,j,4] = cons_wait_array[i,j] / float(times_array[i,j])
            stacked_array[i,j,1] = (prod_time_array[i,j] - prod_wait_array[i,j]) / float(times_array[i,j])
            stacked_array[i,j,3] = (cons_time_array[i,j] - cons_wait_array[i,j]) / float(times_array[i,j])
            stacked_array[i,j,0] = 1.0 - stacked_array[i,j,1] - stacked_array[i,j,2] - stacked_array[i,j,3] - stacked_array[i,j,4]

    for i in range(num_workers):
        geomeans[i] = np.mean(times_array_normalized[:,i])
        errors[0, i] = geomeans[i] - min(times_array_normalized[:,i])
        errors[1, i] = max(times_array_normalized[:,i]) - geomeans[i]
        variances[i] = np.std(times_array_normalized[:,i])
        geomeans_prod_wait[i] = np.mean(prod_wait_array[:,i])
        geomeans_cons_wait[i] = np.mean(cons_wait_array[:,i])
        if np.count_nonzero(cons_empty_array[:,i]) == 0:
            geomeans_cons_empty[i] = 0.0
        else:
            geomeans_cons_empty[i] = np.mean(cons_empty_array[:,i] / num_cons_array[:,i])

        if np.count_nonzero(prod_full_array[:,i] == 0):
            geomeans_prod_full[i] = 0.0
        else:
            geomeans_prod_full[i] = np.mean(prod_full_array[:,i] / num_prod_array[:,i])
        stacked_geomeans[i,0] = np.mean(stacked_array[:,i,0])
        stacked_geomeans[i,1] = np.mean(stacked_array[:,i,1])
        stacked_geomeans[i,2] = np.mean(stacked_array[:,i,2])
        stacked_geomeans[i,3] = np.mean(stacked_array[:,i,3])
        stacked_geomeans[i,4] = np.mean(stacked_array[:,i,4])
        # stacked_geomeans[i,3] = np.mean(stacked_array[:,i,3])
        print('Geomeans worker {iteration}: {:.4f} {:.4f} {:.4f} {:.4f} {:.4f}'.format(stacked_geomeans[i,0], stacked_geomeans[i,1], stacked_geomeans[i,2], stacked_geomeans[i,3], stacked_geomeans[i,4], iteration=i))
        print('Cons empty geomean worker {iteration}: {:.4f}'.format(geomeans_cons_empty[i], iteration=i))
        print('Prod full  geomean worker {iteration}: {:.4f}'.format(geomeans_prod_full[i], iteration=i))

    matplotlib.use('TkAgg')
    xs = range(num_workers)

    p0 = plt.bar(xs, stacked_geomeans[:,0])
    p1 = plt.bar(xs, stacked_geomeans[:,1], bottom=stacked_geomeans[:,0])
    p2 = plt.bar(xs, stacked_geomeans[:,2], bottom=stacked_geomeans[:,1] + stacked_geomeans[:,0])
    p3 = plt.bar(xs, stacked_geomeans[:,3], bottom=stacked_geomeans[:,2] + stacked_geomeans[:,1] + stacked_geomeans[:,0])
    p4 = plt.bar(xs, stacked_geomeans[:,4], bottom=stacked_geomeans[:,3] + stacked_geomeans[:,2] + stacked_geomeans[:,1] + stacked_geomeans[:,0])
    plt.title('Worker Breakdown')
    plt.xlabel('Worker number')
    plt.ylabel('Fraction of execution time')

    for i in range(num_workers):
        plt.text(i-0.25, 1.0+.05, s='{:.2f}'.format(100.0*geomeans_cons_empty[i]), color='purple', size=6)
        plt.text(i-0.25, 1.0+.1, s='{:.2f}'.format(100.0*geomeans_prod_full[i]), color='green', size=6)

    plt.legend((p0[0], p1[0], p2[0], p3[0], p4[0]),
            ('Useful Work (?)', 'Useful produce', 'Produce wait', 'Useful consume', 'Consume wait time'),
            bbox_to_anchor=(1.0, 0.5), loc='center left')
    print('Plotted')

    """
    fig, (invoc_plot, prod_plot, cons_plot) = plt.subplots(1, 3)
    invoc_plot.bar(xs, geomeans, yerr=variances, align='center', ecolor='red', capsize=4)
    invoc_plot.set_title('Normalized worker invocation times across invocations (geomean)')
    invoc_plot.set_ylabel('Execution time (normalized to longest worker per invocation)')
    invoc_plot.set_xlabel('Worker number')

    prod_plot.bar(xs, geomeans_prod_wait)
    prod_plot.set_title('Produce to queue wait times')
    prod_plot.set_ylabel('Time (Normalized to invoc time)')
    prod_plot.set_xlabel('Worker number')

    cons_plot.bar(xs, geomeans_cons_wait)
    cons_plot.set_title('consuce to queue wait times')
    cons_plot.set_ylabel('Time (Normalized to invoc time)')
    cons_plot.set_xlabel('Worker number')
    """

    plt.show()
