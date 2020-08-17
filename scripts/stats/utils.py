import re

def get_basics(lines):

    num_workers = 0
    num_invocations = 0
    max_iter = 0

    num_workers_re = re.compile(r'\*\*\* NUM_WORKERS: (\d+).*')
    num_iter_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*number iterations:\s*(\d+)')
    invoc_time_re = re.compile(r'@(\d+)\s*Worker\s*(\d+)\s*invocation time:\s*(\d+)')

    for line in lines:
        num_workers_match = num_workers_re.search(line)
        num_invocations_match = invoc_time_re.search(line)
        num_iter_match = num_iter_re.search(line)
        if num_workers_match is not None:
            num_workers = int(num_workers_match.group(1))
        if num_invocations_match is not None:
            if int(num_invocations_match.group(1)) > num_invocations:
                num_invocations = int(num_invocations_match.group(1))
        if num_iter_match is not None:
            if int(num_iter_match.group(3)) > max_iter:
                max_iter = int(num_iter_match.group(3))

    return num_workers, num_invocations, max_iter
