# Python 3
#
# Ziyang Xu
# May 1, 2019
#
# Present the results in HTML, plots,
# and a bunch of interesting stuff

import argparse
import os
import json
import numpy as np
import dash
import dash_core_components as dcc
import dash_html_components as html
import plotly.graph_objects as go


# Geometric mean helper
def geo_mean_overflow(iterable):
    a = np.log(iterable)
    return np.exp(a.sum() / len(a))


class ResultProvider:

    def __init__(self, path):
        self._path = path

    def getPriorResults(self, bmark_list):
        prior_file = "prior_results.json"
        with open(prior_file, 'r') as fd:
            prior_results = json.load(fd)

        speedup_list = []
        text_list = []
        for bmark in bmark_list:
            if bmark in prior_results:
                result = prior_results[bmark][0]
                speedup = result['speedup']
                text = "on %d cores from %s " % (
                    result['cores'], result['paper'])
                speedup_list.append(speedup)
                text_list.append(text)

        return speedup_list, text_list

    def getSequentialData(self, bmark_list, date_list):
        # Newer result overwrite old result
        result_dict = {}
        for date in date_list:
            status_path = os.path.join(self._path, date, "status.json")
            with open(status_path, "r") as fd:
                status = json.load(fd)

            for bmark in bmark_list:
                if bmark in status and "RealSpeedup" in status[bmark]:
                    real_speedup = status[bmark]["RealSpeedup"]
                    if not real_speedup:
                        continue

                    if "seq_time" not in real_speedup:
                        continue

                    result_dict[bmark] = real_speedup["seq_time"]

        return result_dict

    def getParallelData(self, bmark_list, date_list):

        para_time_dict = {}
        for date in date_list:
            status_path = os.path.join(self._path, date, "status.json")
            with open(status_path, "r") as fd:
                status = json.load(fd)

            for bmark in bmark_list:
                if bmark in status and "RealSpeedup" in status[bmark]:
                    real_speedup = status[bmark]["RealSpeedup"]
                    if not real_speedup or 'para_time' not in real_speedup:
                        continue

                    para_time_dict[bmark] = real_speedup['para_time']

        return para_time_dict

    def getRealSpeedup(self, bmark_list, date_list):
        prior_speedup_list, prior_text_list = self.getPriorResults(bmark_list)

        bar_list = [{'x': bmark_list, 'y': prior_speedup_list,
                     'text': prior_text_list, 'type': 'bar', 'name': "Best Prior Result"}]

        for date in date_list:
            status_path = os.path.join(self._path, date, "status.json")
            with open(status_path, "r") as fd:
                status = json.load(fd)

            have_results_bmark_list = []
            real_speedup_list = []
            text_list = []
            for bmark in bmark_list:
                if bmark in status and "RealSpeedup" in status[bmark]:
                    real_speedup = status[bmark]["RealSpeedup"]
                    if not real_speedup:
                        continue
                    have_results_bmark_list.append(bmark)
                    real_speedup_list.append(real_speedup['speedup'])
                    text_list.append("Seq time: %s, para time: %s" % (
                        round(real_speedup['seq_time'], 2), round(real_speedup['para_time'], 2)))

            bar_list.append({'x': have_results_bmark_list, 'y': real_speedup_list,
                             'text': text_list, 'type': 'bar', 'name': "Results from" + date})
        return bar_list

    def updateResult(self, date_list):
        all_reg_results = {}

        for date in date_list:
            date_path = os.path.join(self._path, date)
            all_status = {}
            for filename in os.listdir(date_path):
                if filename.endswith(".json") and filename.startswith("status"):
                    with open(os.path.join(date_path, filename), 'r') as fd:
                        status = json.load(fd)
                        bmark = filename.replace(
                            "status_", "").replace(".json", "")
                        all_status[bmark] = status
            all_reg_results[date] = all_status
        self._all_reg_results = all_reg_results

    def getMultiCoreData(self, bmark_list, date_list):

        # Newer result overwrite old result
        result_dict = {}
        for date in date_list:
            status_path = os.path.join(self._path, date, "status.json")
            with open(status_path, "r") as fd:
                status = json.load(fd)

            for bmark in bmark_list:
                if bmark in status and "RealSpeedup" in status[bmark]:
                    real_speedup = status[bmark]["RealSpeedup"]
                    if not real_speedup:
                        continue

                    if "para_time_dict" not in real_speedup:
                        continue

                    para_time_dict = real_speedup["para_time_dict"]
                    x_list = []
                    y_list = []
                    for x, y in para_time_dict.items():
                        x_list.append(int(x))
                        y_list.append(y)
                    x_list, y_list = (list(t)
                                      for t in zip(*sorted(zip(x_list, y_list))))

                    result_dict[bmark] = [x_list, y_list]

        return result_dict

    def getLoopData(self, bmark):

        date_list = ['2019-06-08']
        self.updateResult(date_list)
        # TODO: fake result, only 05-22
        if bmark not in self._all_reg_results['2019-06-08']:
            print(bmark + " not exists")
            return None

        status = self._all_reg_results['2019-06-08'][bmark]
        if 'Experiment' in status and status['Experiment']:
            status = status['Experiment']
            if "speedup" not in status or "loops" not in status:
                print("NO Speedup or loops")
                return None
        else:
            return None

        speedup = status['speedup']
        loops = status['loops']

        the_rest = 100
        para_whole = 100 / speedup
        para_the_rest = para_whole

        data = []

        for loop, loop_info in loops.items():
            if 'selected' in loop_info and loop_info['selected']:
                if 'loop_speedup' in loop_info:
                    exec_coverage = loop_info['exec_coverage']
                    the_rest -= exec_coverage

                    loop_speedup = loop_info['loop_speedup']
                    para_coverage = exec_coverage / loop_speedup

                    para_the_rest -= para_coverage
                    data.append(go.Bar(
                        x=['Sequential', 'Parallel'],
                        y=[exec_coverage, para_coverage],
                        name=loop
                    ))

        data.append(go.Bar(
            x=['Sequential', 'Parallel'],
            y=[the_rest, para_the_rest],
            name='The Rest'
        ))

        return data

    def getSpeedupData(self, date_list, speedup_threshold=2.0):
        self.updateResult(date_list)
        speedup_bar_list = []
        speedup_bar_list_DOALL_only = []
        speedup_bar_list_without_DOALL = []

        def update_list(x_list, y_list, date):
            # sort two list together
            y_list, x_list = (list(t)
                              for t in zip(*sorted(zip(y_list, x_list))))

            geomean = geo_mean_overflow(y_list)
            x_list.append("geomean")
            y_list.append(geomean)
            # y_list = list(map(lambda x: x - 1, y_list))
            return {'x': x_list, 'y': y_list, 'type': 'bar',
                    'name': 'speedup for' + date}

        for date, reg_results in self._all_reg_results.items():
            x_list = []
            y_list = []
            x_list_DOALL = []
            y_list_DOALL = []
            x_list_no_DOALL = []
            y_list_no_DOALL = []
            for bmark, status in reg_results.items():
                if 'Experiment' in status and status['Experiment']:
                    if 'speedup' in status['Experiment']:
                        x_list.append(bmark)
                        y_list.append(status['Experiment']['speedup'])
                        if status['Experiment']['speedup'] < speedup_threshold:
                            continue
                        if 'loops' in status['Experiment']:
                            DOALL_only = True
                            for name, loop_status in status['Experiment']['loops'].items():
                                if 'selected' in loop_status and not loop_status['selected']:
                                    continue
                                if 'loop_stage' in loop_status and loop_status['loop_stage'] != "DSWP[P22]":
                                    DOALL_only = False
                                    break
                            if DOALL_only:
                                x_list_DOALL.append(bmark)
                                y_list_DOALL.append(
                                    status['Experiment']['speedup'])
                            else:
                                x_list_no_DOALL.append(bmark)
                                y_list_no_DOALL.append(
                                    status['Experiment']['speedup'])

            speedup_bar_list.append(update_list(x_list, y_list, date))
            speedup_bar_list_DOALL_only.append(update_list(x_list_DOALL,
                                                           y_list_DOALL, date))
            speedup_bar_list_without_DOALL.append(update_list(x_list_no_DOALL,
                                                              y_list_no_DOALL, date))

        return speedup_bar_list, speedup_bar_list_DOALL_only, speedup_bar_list_without_DOALL

    # Need to resolve nested loops issue
    def getLoopsDataForOneBmark(self, loops):
        raise NotImplementedError()
        loop_names = []
        for loop_name, loop_info in loops.items():
            loop_names.append(loop_name)


def parseArgs():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--root_path", type=str, required=True,
                        help="Root path of CPF benchmark directory")
    parser.add_argument("--port", type=str, default="8050",
                        help="Port for Dash server to run, 8050 or 8060 on AWS")
    args = parser.parse_args()

    return args.root_path, args.port


# some setting for plot
external_stylesheets = ['https://codepen.io/chriddyp/pen/bWLwgP.css']
app = dash.Dash(__name__, external_stylesheets=external_stylesheets)
app.config.suppress_callback_exceptions = True


def getRealSpeedupLayout(resultProvider):
    bmark_list = ["correlation", "2mm", "3mm", "covariance", "gemm", "doitgen", "swaptions",
                  "blackscholes", "052.alvinn", "enc-md5", "dijkstra-dynsize", "179.art"]
    date_list = ["2019-06-27", "2019-07-01", "2019-07-06", "2019-07-08"]
    data_real_speedup = resultProvider.getRealSpeedup(bmark_list, date_list)
    layout = [html.Div(children='''
            Real Speedup on 24 cores (Average of 3 runs)
        '''),

              # Data Layout:
              # [
              #     {'x': [1, 2, 3], 'y': [4, 1, 2], 'type': 'bar', 'name': 'SF'},
              #     {'x': [1, 2, 3], 'y': [2, 4, 5], 'type': 'bar', 'name': 'Montréal'},
              # ]

              dcc.Graph(
        id='real-speed-graph',
        figure={
            'data': data_real_speedup,
            'layout': {
                'title': 'Real Speedup'
            }
        }
    )]

    return layout


def getComparePrivateerLayout(resultProvider):
    bmark_list = ["correlation", "2mm", "3mm", "covariance", "gemm", "doitgen", "swaptions",
                  "blackscholes", "052.alvinn", "enc-md5", "dijkstra-dynsize", "179.art"]
    spec_list = ["blackscholes", "052.alvinn", "enc-md5", "dijkstra-dynsize", "179.art"]
    perspective_time_list = ["2019-08-05-18-54"]
    privateer_peep_time_list = ["2019-08-06-15-03"]
    privateer_both_time_list = ["2019-08-07-00-38"]
    perspective_SAMA_time_list = ["2019-08-09-01-52"]
    perspective_cheap_priv_time_list = ["2019-08-13-21-23", "2019-08-13-22-41", "2019-08-14-14-27"]
    # perspective_cheap_priv_time_list = ["2019-08-12-17-22"]
    seq_date_list = ['2019-07-02', '2019-07-28', '2019-08-05-12-41', '2019-08-05-16-14']
    seq_data = resultProvider.getSequentialData(bmark_list, seq_date_list)

    def getOneBar(time_list, bar_name, color):
        one_para_data = resultProvider.getParallelData(bmark_list, time_list)

        one_bmark_list = []
        one_speedup_list = []
        one_text_list = []
        one_spec_speedup_list = []
        one_nonspec_speedup_list = []
        for bmark, para_time in one_para_data.items():
            if bmark not in seq_data:
                continue
            one_bmark_list.append(bmark)
            seq_time = seq_data[bmark]
            speedup = round(seq_time / para_time, 2)
            one_speedup_list.append(speedup)
            if bmark in spec_list:
                one_spec_speedup_list.append(speedup)
            else:
                one_nonspec_speedup_list.append(speedup)
            one_text_list.append("Seq time: %s, para time: %s" %
                                  (round(seq_time, 2),
                                   round(para_time, 2)))
        one_speedup_list.append(geo_mean_overflow(one_speedup_list))
        one_bmark_list.append("Geomean")
        one_text_list.append("Geomean")

        # one_speedup_list.append(geo_mean_overflow(one_spec_speedup_list))
        # one_bmark_list.append("Spec Geomean")
        # one_text_list.append("Spec Geomean")

        # one_speedup_list.append(geo_mean_overflow(one_nonspec_speedup_list))
        # one_bmark_list.append("Nonspec Geomean")
        # one_text_list.append("Nonspec Geomean")

        bar_one = {'x': one_bmark_list, 'y': one_speedup_list, 'text': one_text_list,
                   'type': 'bar', 'name': bar_name, 'marker_color': color}
        return bar_one

    bar_privateer_peep = getOneBar(privateer_peep_time_list, "Privateer", '#ca0020')
    bar_privateer_both = getOneBar(privateer_both_time_list, "Per<i>spec</i>tive (Planner Only)", '#f4a582')
    # bar_perspective_SAMA = getOneBar(perspective_SAMA_time_list, "Per<i>spec</i>tive Speculative-Aware-Memory-Analysis", '#92c5de')
    bar_perspective_cheap_priv = getOneBar(perspective_cheap_priv_time_list, "Per<i>spec</i>tive (Planner + Efficient SpecPriv)", '#abd9e9')
    bar_perspective = getOneBar(perspective_time_list, "Per<i>spec</i>tive (Planner + Efficient SpecPriv +SAMA)", '#0571b0')
    
    bar_list = [bar_privateer_peep, bar_privateer_both, bar_perspective_cheap_priv, bar_perspective]

    fig = go.Figure({
                    'data': bar_list,
                    'layout': {
                        # 'title': 'Parallel Execution Comparison',
                        'legend': {'orientation': 'h', 'x': 0.2, 'y': 1.3},
                        'yaxis': {
                            'showline': True, 
                            'linewidth': 2,
                            'ticks': "inside",
                            'mirror': 'all',
                            'linecolor': 'black',
                            'gridcolor':'rgb(200,200,200)', 
                            'range': [0, 28.5],
                            'nticks': 15,
                            'title': {'text': "Whole Program Speedup over Sequential"},
                            'ticksuffix': "x",
                        },
                        'xaxis': {
                            'linecolor': 'black',
                            'showline': True, 
                            'linewidth': 2,
                            'mirror': 'all'
                        },
                        'font': {'family': 'Helvetica', 'color': "Black"},
                        'plot_bgcolor': 'white',
                        'autosize': False,
                        'width': 900, 
                        'height': 400}
                    })

    # fig.write_image("images/fig_compare.svg")
    layout = [html.Div(children='''
            Compare Parallel Runtime with Privateer on 28 cores (Average of 5 runs)
        '''),

              dcc.Graph(
        id='privateer-compare-graph',
        figure=fig
    )]

    return layout


def getEstimatedSpeedupLayout(resultProvider):
    date_list = ['2019-04-28', '2019-05-20', '2019-05-22',
                 '2019-06-04', '2019-06-06', '2019-06-08']

    data_speedup, data_speedup_DOALL, data_speedup_no_DOALL = resultProvider.getSpeedupData(
        date_list)

    layout = [html.H1(children='CPF Estimated Speedup Result')]

    layout_speedup = [html.Div(children='''
            Estimated Speedup on 22 cores
        '''),

                      # Data Layout:
                      # [
                      #     {'x': [1, 2, 3], 'y': [4, 1, 2], 'type': 'bar', 'name': 'SF'},
                      #     {'x': [1, 2, 3], 'y': [2, 4, 5], 'type': 'bar', 'name': 'Montréal'},
                      # ]

                      dcc.Graph(
        id='speed-graph',
        figure={
            'data': data_speedup,
            'layout': {
                'title': 'Speedup'
            }
        }
    )]

    layout_speedup_DOALL = [html.Div(children='''
            Estimated Speedup on 22 cores
        '''),
                            dcc.Graph(
        id='speed-graph-DOALL',
        figure={
            'data': data_speedup_DOALL,
            'layout': {
                'title': 'Speedup DOALL Only'
            }
        }
    )]

    layout_speedup_no_DOALL = [html.Div(children='''
            Estimated Speedup on 22 cores
        '''),
                               dcc.Graph(
        id='speed-graph-noDOALL',
        figure={
            'data': data_speedup_no_DOALL,
            'layout': {
                'title': 'Speedup DSWP (excluding DOALL only)'
            }
        }
    )]

    if layout_speedup:
        layout += layout_speedup

    if layout_speedup_DOALL:
        layout += layout_speedup_DOALL

    if layout_speedup_no_DOALL:
        layout += layout_speedup_no_DOALL

    return layout


def getOneBenchmarkLayout(resultProvider, bmark):
    data_bmark = resultProvider.getLoopData(bmark)
    if data_bmark is not None:
        layout = [html.Div(children='Speedup breakdown of ' + bmark),

                  dcc.Graph(
            id='bmark-graph',
            figure={
                'data': data_bmark,
                'layout': {
                    'title': 'Execution Time Breakdown',
                    'barmode': 'stack'
                }
            }
        )]
    else:
        layout = None

    return layout


def getMultiCoreLayout(resultProvider):
    bmark_list = ["enc-md5", "dijkstra-dynsize", "swaptions", "doitgen", "gemm", "blackscholes", "2mm",
                  "3mm", "179.art", "correlation", "covariance", "052.alvinn"]
    parallel_date_list = ['2019-07-26', '2019-07-27', '2019-07-30', '2019-08-05', '2019-08-06-02-43', '2019-08-10-02-33', '2019-08-11-02-09']
    seq_date_list = ['2019-07-02', '2019-07-28', '2019-08-05-12-41', '2019-08-05-16-14']

    fig = go.Figure()

    multicore_data = resultProvider.getMultiCoreData(
        bmark_list, parallel_date_list)
    seq_data = resultProvider.getSequentialData(bmark_list, seq_date_list)

    color_list =['#a6cee3', '#ffff99', '#1f78b4', '#6a3d9a','#fb9a99',
                 '#fdbf6f','#cab2d6', '#ff7f00', '#b2df8a', '#e31a1c',
                 '#33a02c','#b15928']
    shape_list = ["star", "star-square", "cross", "circle",
                  "square", "square-open", "circle-open", "x",
                  "triangle-up", "triangle-up-open", "diamond", "diamond-open"]
    fig.update_xaxes(range=[1, 28], showgrid=True, gridwidth=1, nticks=28,
                     title_text="Number of Cores", showline=True, linewidth=2, ticks="inside",
                     linecolor='black', gridcolor='rgb(200,200,200)', mirror='all', layer="below traces")
    fig.update_yaxes(range=[0, 28], showgrid=True, gridwidth=1, nticks=29, title_text="Whole Program Speedup over Sequential", ticks="inside",
                     showline=True, linewidth=2, linecolor='black', gridcolor='rgb(200,200,200)', mirror='all', ticksuffix="x", layer="below traces")
 
    for bmark in bmark_list:
        if bmark not in multicore_data:
            continue
        result =  multicore_data[bmark]
        x_list, y_list = result
        seq_time = seq_data[bmark]
        speedup_list = [seq_time / x for x in y_list]
        shape = shape_list.pop()
        color = color_list.pop()
        fig.add_trace(go.Scatter(x=x_list, y=speedup_list,
                      mode='lines+markers', line={'width': 1},
                      marker={"symbol": shape, "size": 8, 'opacity': 0.9},
                                     name=bmark))

    fig.update_layout(autosize=False,
                      width=1000, height=500,
                      plot_bgcolor='white',
                      font={'family': 'Helvetica', 'color': 'Black'},
                      legend=go.layout.Legend(
                          x=0.01,
                          y=0.98,
                          traceorder="normal",
                          bgcolor="White",
                          bordercolor="Black",
                          borderwidth=2))

    # fig.update_layout(title=go.layout.Title(text="Differnt Cores"))
    # fig.write_image("images/fig_multi_core.svg")

    layout = [html.Div(children='''
            Multiple Core Speedup Results
        '''),
              dcc.Graph(
        id='multicore-speedup-graph',
        figure=fig
    )]

    return layout


@app.callback(dash.dependencies.Output('page-content', 'children'),
              [dash.dependencies.Input('url', 'pathname')])
def display_page(pathname):
    if not pathname:
        return 404

    if pathname == '/':
        pathname = '/realSpeedup'

    if pathname == '/multiCore':
        layout = getMultiCoreLayout(app._resultProvider)
        return layout
    elif pathname == '/realSpeedup':
        layout = getRealSpeedupLayout(app._resultProvider)
        return layout
    elif pathname == '/estimatedSpeedup':
        layout = getEstimatedSpeedupLayout(app._resultProvider)
        return layout
    elif pathname == '/comparePrivateer':
        layout = getComparePrivateerLayout(app._resultProvider)
        return layout
    elif pathname.startswith("/bmark_"):
        bmark = pathname.split("_")[1]
        layout = getOneBenchmarkLayout(app._resultProvider, bmark)
        return layout
    else:
        return 404
    # You could also return a 404 "URL not found" page here


if __name__ == '__main__':
    cpf_root, port = parseArgs()
    result_path = os.path.join(cpf_root, "./results/")
    app._resultProvider = ResultProvider(result_path)

    app.layout = html.Div([
        dcc.Location(id='url', refresh=False),
        dcc.Link('Different Cores', href='/multiCore'),
        html.Br(),
        # dcc.Link('Real Speedup', href='/realSpeedup'),
        # html.Br(),
        dcc.Link('Compare with Privateer', href='/comparePrivateer'),
        html.Br(),
        dcc.Link('Estimated Speedup', href='/estimatedSpeedup'),
        html.Div(id='page-content')
    ])

    app.run_server(debug=False, host='0.0.0.0', port=port)
