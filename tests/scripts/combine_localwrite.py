#!/usr/bin/env python3

import os
import argparse

# Example: 2435 18875 18907 18907 0 244000
# read from filename with a prefix
# For each line, first five numbers are converted into key, the last number is the value
# The values are reduced to a map
# And the map is dumped out to a file with only the prefix
def combineLocalWrite(prefix, outfile):
    # list all files with the prefix
    files = [f for f in os.listdir('.') if f.startswith(prefix + "_")]
    m = {}
    # read each file
    for f in files:
        print("Processing", f)

        with open(f, 'r') as fp:
            for line in fp:
                # split the line into key and value
                # For each line, first five numbers are converted into key, the last number is the value
                key = " ".join(list(line.split()[:5]))
                value = line.split()[-1]

                if key in m:
                    # if the key is already in the map, add the value to the existing value
                    m[key] += int(value)
                else:
                    # if the key is not in the map, add it to the mapk
                    m[key] = int(value)

    # write the map to a file
    with open(outfile, 'w') as fp:
        for key in m:
            fp.write(key + ' ' + str(m[key]) + '\n')

if __name__ == '__main__':
    # get the command line argument
    parser = argparse.ArgumentParser()
    # first argument function name
    # second argument loop name
    # third argument output file name (optional)
    parser.add_argument("func", help="function name")
    parser.add_argument("loop", help="loop name")
    parser.add_argument("outfile", help="output file name", nargs='?', default=None)
    args = parser.parse_args()

    prefix = args.func + '-' + args.loop + ".result.slamp.profile"
    if args.outfile is None:
        args.outfile = prefix

    print(prefix)
    combineLocalWrite(prefix, args.outfile)
