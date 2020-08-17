# Python 3
#
# Ziyang Xu
# Apr 27, 2019 Updated
#
# Connect to slack, post message
# Please follow README.md in the sciprt/ directory

import json
import requests


def general_post(text, attachments, credential_str=None):
    if not credential_str:
        print("Credential Str not set")
        return False
    webhook_url = 'https://hooks.slack.com/services/' + credential_str
    slack_data = {'mrkdwn': True, 'text': text, 'attachments': attachments}

    response = requests.post(
        webhook_url, data=json.dumps(slack_data),
        headers={'Content-Type': 'application/json'}
    )

    if response.status_code != 200:
        return False
    return True


def post_result(result_list):
    fields = []
    for name, coverage, covered_cnt, all_cnt in result_list:
        fields.append({"title": name, "value": "Covered %.2f%% time, %d/%d hot loops" %
                                               (coverage, covered_cnt, all_cnt)})

    if result_list != []:
        general_post("*Benchmarks with non-trivial coverage:*",
                     [{"color": "good", "fields": fields}])


def post_dswp_result(result_list):
    fields = []
    for name, speedup, worker_cnt in result_list:
        fields.append(
            {"title": name, "value": "Estimated Speedup: %.2f%x on %d workers" % (speedup, worker_cnt)})

    if result_list != []:
        general_post("*Benchmarks with non-trivial speedup:*",
                     [{"color": "good", "fields": fields}])
