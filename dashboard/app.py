##########################################################################################
# Dashboard Application
# Author: Abdulrahman Alfares
# Developed & Tested with Python v3.8.10
#
# Tab Switching callback logic obtained from:
# https://github.com/plotly/dash-sample-apps/tree/main/apps/dash-manufacture-spc-dashboard
#
#
#

import logging
import dash
from dash import dcc
from dash import html
from dash.dependencies import Input, Output, State
from dash import dash_table as dash_table
import plotly.graph_objs as go
import dash_daq as daq
import plotly.express as px
import pandas as pd
import argparse
from configparser import SafeConfigParser
import requests
from json import loads, dumps
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

#### Read Config File ####
parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument("-c", "--config", type=str, default='device.cfg', help="Config file name")
args = parser.parse_args()
configFile = args.config

parser = SafeConfigParser()
parser.optionxform = str
parser.read(configFile)
appIP = parser.get('CONFIG_DATA', 'appIP')
appPort = int(parser.get('CONFIG_DATA', 'appPort'))
appName = parser.get('CONFIG_DATA', 'appName')
dashHost = parser.get('CONFIG_DATA', 'dashHost')
cseIP = parser.get('CONFIG_DATA', 'cseIP')
csePort = parser.get('CONFIG_DATA', 'csePort')
cseID = parser.get('CONFIG_DATA', 'cseID')
cseName = parser.get('CONFIG_DATA', 'cseName')
token = parser.get('CONFIG_DATA', 'mapboxToken')
mapboxMapLink = parser.get('CONFIG_DATA', 'mapboxMapLink')

# Initialize Variables
newSensorBuffer = []
rmSensorBuffer = []
newActuatorBuffer = []
rmActuatorBuffer = []
newCINSensorBuffer = []
newCINActuatorBuffer = []
prevTempValue = None
prevHumValue = None
prevMoisValue = None
prevGPSValue = None
prevRainValue = None


#### Dash App IP & Port ####
ip = dashHost
port = 8050

### AE-ID for this device ###
originator = 'C'+appName

#### Style, Color, and theme variable initialization ####
alertStyle = {
  'display': 'flex',
  'flex-direction': 'column',
  'justify-content': 'space-evenly',
  'align-items': 'center',
  'align-self': 'center',
  'color': 'white',
  'padding-left': '2rem',
  'padding-right': '2rem',
  'width': '100%',
  'text-align': 'center'
}

alertStyle2 = {
  'display': 'flex',
  'flex-direction': 'column',
  'justify-content': 'space-evenly',
  'align-items': 'center',
  'align-self': 'center',
  'color': 'white',
  'padding-left': '2rem',
  'padding-right': '2rem',
  'width': '100%',
  'text-align': 'center',
  'margin-bottom': '2rem'
}

alertStyleHidden = {'color': 'white'}

indicatorOffColor = '#1E2130'
indicatorOnColor = '#91dfd2'

darkTheme = {
    'dark': True,
    'primary': '#161a28',
    'secondary': 'white',
    'detail': 'white'
}

#### Setup App Metadata ####
app = dash.Dash(
    __name__,
    meta_tags=[{"name": "viewport", "content": "width=device-width, initial-scale=1"}],
)
app.title = "Irrigation System Dashboard"

app.config["suppress_callback_exceptions"] = True

# Disable excessive http dash component post logs on command line
# Focuses on oneM2M primitives, notifications, and errors logging. 
log = logging.getLogger('werkzeug')
log.disabled = True

##### Generate map & graphs #####
def generateMap(locList):
    if (locList[0][0] != 0 and locList[0][1] != 0):
        latC = locList[0][0]
        lonC = locList[0][1]
        zoom = 15
    else:
        latC = 40.793396
        lonC = -77.860001
        zoom = 13
    gpsLoc = pd.DataFrame(locList, columns =['lat', 'lon'])
    mapFig = px.scatter_mapbox(gpsLoc, lat="lat", lon="lon", center={'lat': latC, 'lon': lonC}, zoom=zoom, height=620, size=[17])
    mapFig.update_layout(mapbox_style = mapboxMapLink, mapbox_accesstoken = token)
    mapFig.update_layout(margin={"r":0,"t":0,"l":0,"b":0})
    return mapFig


def generateGraph(x, y):
    figure =  go.Figure(
    {
        "data": [
            {
                "x": x,
                "y": y,
                "mode": "lines",
            }
        ],
        "layout": {
            "paper_bgcolor": "rgba(0,0,0,0)",
            "plot_bgcolor": "rgba(0,0,0,0)",
            "xaxis": dict(
                showline=False, showgrid=False, zeroline=False
            ),
            "yaxis": dict(
                showgrid=False, showline=False, zeroline=False
            ),
        },
    })
    figure.update_layout(height = 620, margin=dict(t=0,r=0,l=0,b=0,pad=0))
    return figure

#### Populate Dropdown List ####
def build_dropdown(devType):
    disAE = discover(type= "AE", label= devType)
    ddList = []
    devURI = []
    for x in range(0, len(disAE)):
        ddList.append(disAE[x].split("/")[-1])
        devURI.append(disAE[x])
    options = []
    for x in range(0, len(ddList)):
        options.append({"label": ddList[x], "value": devURI[x]})
    return options

#### Generate Buttons ####
def create_button(id, child, classN = ""):
    return html.Div(
        id = "buttonDiv",
        children = [
            html.Div(
                id = "card-1",
                children = [
                    html.Button(id = id, children = [child], n_clicks = 0)
                ]
            )
        ]
    )

#### Section Banners ####
def generate_section_banner(title):
    return html.Div(className="section-banner", children=title)

#### Banner ####
def build_banner():
    return html.Div(
        id="banner",
        className="banner",
        children=[
            html.Div(
                id="banner-text",
                children=[
                    html.H5("oneM2M-based Irrigation System Dashboard"),
                    html.H6("Capstone Team PJ3C"),
                ],
            ),
            html.Div(
                id="banner-logo",
                children=[
                    html.A(
                        html.Img(id="logo2", src="https://www.pinclipart.com/picdir/big/144-1445924_penn-state-logo-clipart.png"),
                        href = "https://www.engr.psu.edu/"
                    )
                ],
            ),
        ],
    )

#### Tabs ####
def build_tabs():
    return html.Div(
        id="tabs",
        className="tabs",
        children=[
            dcc.Tabs(
                id="app-tabs",
                value="tab2",
                className="custom-tabs",
                children=[
                    dcc.Tab(
                        id="Control-chart-tab",
                        label="Device & Sensor Monitoring",
                        value="tab2",
                        className="custom-tab",
                        selected_className="custom-tab--selected",
                    ),
                    dcc.Tab(
                        id="Specs-tab",
                        label="Device Control",
                        value="tab1",
                        className="custom-tab",
                        selected_className="custom-tab--selected",
                    ),
                    
                ],
            )
        ],
    )

#### Sensor Tab ####

# Real Time Stats Panel
def build_quick_stats_panel():
    return html.Div(
        id="quick-stats",
        
        children=[
            dcc.Interval(id = 'clkDevS', interval = 1*1000, n_intervals = 0),
            dcc.Interval(id = 'clkVal', interval = 4*1000, n_intervals = 0),
            generate_section_banner("Real Time Sensor Values"),
            
            html.Div(
                id="dropdown",
                children=[
                    dcc.Dropdown(id = "ddMenuS", options = build_dropdown("sensor"), clearable = True, placeholder= "Select Device ...", searchable = False)
                ],
            ),

            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxS', hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
            
            html.Div(
                id = 'card-temp',  
                children=[
                    html.P("Temperature"),
                    daq.Gauge(
                        id = "temp-gauge",
                        max = 45,
                        min = -20,
                        showCurrentValue = True,
                        value = None,
                        units = "C"
                    )
                ],
            ),
            html.Div(
                id="card-hum",
                children=[
                    html.P("Air Humidity"),
                    daq.Gauge(
                        id = "hum-gauge",
                        max = 100,
                        min = 0,
                        showCurrentValue = True,
                        value = None,
                        units = "%"
                    )
                ],
            ),

            html.Div(
                id="card-mois",
                children=[
                    html.P("Soil Moisture"),
                    daq.Gauge(
                        id = "mois-gauge",
                        max = 100,
                        min = 0,
                        showCurrentValue = True,
                        value = None,
                        units = "%"
                    )
                ],
            ),

            html.Div(
                id="card-rain",
                children=[
                    html.P("Rainfall Indicator"),
                    daq.Indicator(
                        id = 'rainfallIndicator',
                        value = False,
                        label = '',
                        labelPosition = 'bottom',
                        color=indicatorOffColor,
                    )
                ],
            ),
            
        ],
    )

# Top Panel
def build_top_panel():
    return html.Div(
        id="top-section-container",
        className="row",
        children=[
            # Temperature Graph
            html.Div(
                id="metric-summary-session",
                className="four columns",
                children=[
                    generate_section_banner("Temperature History"),
                    html.Div(
                        dcc.Graph(id = "temp-graph", figure=generateGraph([], [])),
                    ),
                ],
            ),
            # Humidity Graph
            html.Div(
                id="ooc-piechart-outer",
                className="four columns",
                children=[
                    generate_section_banner("Air Humidity History"),
                    html.Div(
                        dcc.Graph(id = "hum-graph", figure=generateGraph([], []))
                    ),
                ],
            ),
        ],
    )

# Bottom Panel 
def build_chart_panel():
    return html.Div(
        id="bottom-section-container",
        className="row",
        children=[
            # Soil Moisture Graph
            html.Div(
                id="metric-summary-session",
                className="four columns",
                children=[
                    generate_section_banner("Soil Moisture History"),
                    html.Div(
                        dcc.Graph(id = "mois-graph", figure=generateGraph([], [])),
                    ),
                ],
            ),
            # GPS Map
            html.Div(
                id="ooc-piechart-outer",
                className="four columns",
                children=[
                    generate_section_banner("GPS Location"),
                    dcc.Graph(
                        id="control-chart-live",
                        figure = generateMap([[0,0]]),    
                    ),
                ],
            ),
        ],
    )


#### Control Tab ####

# Side panel 
def build_control_side():
    return html.Div(
        id="control-side",
        children=[
            dcc.Interval(id = 'clkDevC', interval = 1*1000, n_intervals = 0),
            generate_section_banner("Link Devices"),
            html.Div(
                id = "card-3",
                children = [
                    html.P("Sensor Device")
                ]
            ),
            html.Div(
                id="dropdown1",
                children=[
                    dcc.Dropdown(id = "ddMenuSensor", options = build_dropdown("sensor"), clearable = True, placeholder= "Select Device ...", searchable = False)
                ],
            ),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxSensor', className = "center", hidden = True, children = [], style=alertStyleHidden)
                ], 
            ),
            html.Div(
                id = "card-3",
                children = [
                    html.P("Valve Control Device")
                ]
            ),
            html.Div(
                id="dropdown2",
                children=[
                    dcc.Dropdown(id = "ddMenuActuator", options = build_dropdown("actuator"), clearable = True, placeholder= "Select Device ...", searchable = False)
                ],
            ),   
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxActuator', className = "center", hidden = True, children = [], style=alertStyleHidden)
                ], 
            ),
            html.Div(
                id = "side-buttons-container",
                children = [
                    create_button("linkButton", "Link"),
                    create_button("unlinkButton", "Unlink")
                ]
            ),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxlink', className = "center", hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxunlink', className = "center", hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
        ]
    )

# Settings Column
def build_settings_column(id, cardID, title, max = None, min = None):
    if (id == "transmitP"):
        default = 5
    else:
        default = 1
    if (max == None):
        input = daq.NumericInput(
            id = id,
            value = default,
            max = 999,
            min = 1
    )
    else:
        input = daq.NumericInput(
            id = id,
            value = default,
            max = max, 
            min = min
        )
    return html.Div(
            html.Div(  
                id = cardID,
                children = [ 
                    html.P(title),
                    input
                ]
            )
    )

# Left settings column
def build_settings_panel_left():
    return html.Div(
        id = "settings-col",
        children=[
            build_settings_column("numAvg", "card-1", "Sensor values to average", min = 1, max = 100),
            build_settings_column("transmitP", "card-2", "Transmit time (s)"),
        ]
    )

# Right settings column
def build_settings_panel_right():
    return html.Div(
        id = "settings-col",
        children=[
            build_settings_column("sampleP", "card-3", "Time between sensor readings (s)"),
            create_button("settingsButton", "Submit"),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'settingsAlertbox', hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
        ]
    )

# Top panel 
def build_control_top():
    return html.Div(
        children = [
            generate_section_banner("Sensor Settings"),
            html.Div(
                id="top-section-container-control",
                className="row",
                children=[
                    html.Div(
                        id="metric-summary-session",
                        className="six columns",
                        children=[
                            build_settings_panel_left()
                        ],
                    ),
                    html.Div(
                        id="metric-summary-session",
                        className="six columns",
                        children=[
                            build_settings_panel_right()
                        ],
                    ),
                ]
            )
        ]
    )

# Column 1 of bottom panel
def build_control_panel_left():
    return html.Div(
        id = "control-col",
        children = [
            html.Div(
                id = "card-4",
                children = [
                    html.P("Manual Override"),
                ]
            ),
            html.P(id = "warning", children = ["Warning: Setting manual override on will disable automated irrigation functionality."]),
            html.Div(
                id = "card-1",
                children = [
                    daq.DarkThemeProvider(
                        theme = darkTheme, 
                        children =[
                            daq.PowerButton(
                            id ='manualOverride',
                            label = '',
                            on = False,
                            labelPosition = 'top',
                            color = indicatorOnColor
                            ),
                        ]
                    ) 
                ]
            ),
            html.Div(
                id = "card-4",
                children = [
                    html.P("Manual Control"),
                ]
            ),
            html.Div(
                id = "side-buttons-container",
                children = [
                    create_button("valveOn", "On"),
                    create_button("valveOff", "off")
                ]
            ),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxOn', hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
            html.Div(
                className= 'alert', children = [
                    html.Span(id = 'alertboxOff', hidden = True, children = [], style=alertStyleHidden)
                ]
            ),
        ]
    )

# Column 2 of bottom panel
def build_control_panel_right():
    return html.Div(
        id = "control-col",
        children = [
            html.Div(
                id = "card-3",
                children = [
                    
                    html.P("Control Valve Status"),
                    dcc.Interval(id = 'clkActStatus', interval = 1*1000, n_intervals = 0),
                    daq.Indicator(
                        id = 'valveStatus',
                        value = False,
                        label = '',
                        labelPosition = 'bottom',
                        color=indicatorOffColor,
                    )
                ]
            ),
        ]
    )

# Bottom Panel
def build_control_bottom():
    return html.Div(
        id = 'control-chart-container',
        children = [
            generate_section_banner("Manual Valve Controls"),
            html.Div(
                id="bottom-section-container-control",
                className="twelve columns",
                children=[
                    html.Div(
                        id="metric-summary-session",
                        className="six columns",
                        children=[
                            build_control_panel_left()
                        ],
                    ),
                    html.Div(
                        id="metric-summary-session",
                        className="six columns",
                        children=[
                            build_control_panel_right()
                        ],
                    ),
                ]
            )
        ]
    )

#### App Layout ####
app.layout = html.Div(
    id="big-app-container",
    children=[
        build_banner(),
        html.Div(
            id="app-container",
            children=[
                build_tabs(),
                # Main app
                html.Div(id="app-content", children = []),
            ],
        ),
    ],
)

#### Tab Switching Callback ####
@app.callback(
    Output("app-content", "children"),
    [Input("app-tabs", "value")],
)
def render_tab_content(tab_switch):
    if tab_switch == "tab1":
        return html.Div(
                id="status-container",
                children=[
                    build_control_side(),
                    html.Div(
                        id = "graphs-container",
                        children = [build_control_top(), build_control_bottom()],
                    ),
                ],
            )

    return html.Div(
            id="status-container",
            children=[
                build_quick_stats_panel(),
                html.Div(
                    id="graphs-container",
                    children=[build_top_panel(), build_chart_panel()],
                ),
            ],
        )

############################
#                          #
#   Sensor Tab Callbacks   #
#                          #
############################

#### Update Device List with subscriptions ####
@app.callback(
    Output('ddMenuS', 'options'),
    [Input('clkDevS', 'n_intervals')],
    [Input('ddMenuS', 'options')],
)   
def updateSensorList(n, ddMenu):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if triggerID == 'clkDevS':
        global newSensorBuffer
        global rmSensorBuffer

        if (newSensorBuffer == []) and (rmSensorBuffer == []):
            return dash.no_update
    
        newDevices = []
        newDevRn = []
        rmDevices = []
        rmDevRn = []

        for device in newSensorBuffer:
            option = {"label": device, "value": "cse-in/" + device}
            if (option not in ddMenu) and (device not in rmSensorBuffer):
                newDevices.append(option)
                newDevRn.append(device)
                newSensorBuffer.remove(device)

        for device in rmSensorBuffer:
            option = {"label": device, "value": "cse-in/" + device}
            if (option in ddMenu) and (device not in newDevices):
                rmDevices.append(option)
                rmDevRn.append(device)
                rmSensorBuffer.remove(device)
        
        if (newDevices == []) and (rmDevices == []):
            return dash.no_update

        for option in ddMenu:
            if option not in rmDevices:
                newDevices.append(option)
        
        return newDevices
    return dash.no_update

#### Temperature Values and Graph Updates ####
@app.callback(
    Output('temp-gauge', 'value'),
    Output('temp-graph', 'figure'),
    [Input('ddMenuS', 'value')],
    [Input('clkVal', 'n_intervals')],
    [Input('temp-graph', 'figure')],
)
def showTempVal(value, n, figure):
    
    global prevTempValue

    if ((value is not None)):
        
        discoverySUB = discover(type="Sub", location=value + "/Temperature", rn="tempSub")

        if (discoverySUB != []) and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != []) and (prevTempValue == value): #and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != []) 
            latestDate = max(figure['data'][0]['x'])
            tempDates = []
            tempVals = []
            global newCINSensorBuffer
            for item in newCINSensorBuffer:
                if (item[0] == value + "/Temperature"):
                    tempDate = item[2]
                    if (item[1] != "") and (tempDate > latestDate):
                        tempVals.append(float(item[1]))
                        tempDates.append(tempDate[0:4] + "-" + tempDate[4:6] + "-" + tempDate[6:11] + ":" + tempDate[11:13] + ":" + tempDate[13:15])
                    newCINSensorBuffer.remove(item)
            
            if (tempVals != []) and (tempDates != []):
                newestTemp = tempVals[tempDates.index(max(tempDates))]
                figure['data'][0]['x'].extend(tempDates)
                figure['data'][0]['y'].extend(tempVals)
                prevTempValue = value
                return newestTemp, figure
            prevTempValue = value
            return dash.no_update, dash.no_update
        if ((figure['data'][0]['x'] == []) and (figure['data'][0]['y'] == [])) or (prevTempValue != value):
            
            discoveryCNT = discover(type="Container", location=value, rn="Temperature")
            if (discoveryCNT != []):
                if discoverySUB == []:
                    subRI = createSubscription("tempSub", value + "/Temperature", [3])

                tempVals, tempDates = requestAllCIN(value + "/Temperature")
                if (tempVals != []) and (tempDates != []):   
                    newestTemp = tempVals[tempDates.index(max(tempDates))]

                    figure['data'][0]['x'] = tempDates
                    figure['data'][0]['y'] = tempVals
                    prevTempValue = value
                    return newestTemp, figure 
    prevTempValue = value
    return None, generateGraph([], [])


#### Soil Moisture Values and Graph Updates ####
@app.callback(
    Output('mois-gauge', 'value'),
    Output('mois-graph', 'figure'),
    [Input('ddMenuS', 'value')],
    [Input('clkVal', 'n_intervals')],
    [Input('mois-graph', 'figure')],

)
def showMoisVal(value, n, figure):
    global prevMoisValue
    if ((value is not None)):

        discoverySUB = discover(type="Sub", location=value + "/SoilMoisture", rn="moisSub")
        
        if (discoverySUB != [])  and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != []) and (prevMoisValue == value): #  and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != [])
            latestDate = max(figure['data'][0]['x'])
            dates = []
            vals = []
            global newCINSensorBuffer
            for item in newCINSensorBuffer:
                if (item[0] == value + "/SoilMoisture"):
                    date = item[2]
                    if (item[1] != "") and (date > latestDate):
                        vals.append(float(item[1]))
                        dates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
                    newCINSensorBuffer.remove(item)
            if (vals != []) and (dates != []):
                newestVal = vals[dates.index(max(dates))]
                figure['data'][0]['x'].extend(dates)
                figure['data'][0]['y'].extend(vals)
                prevMoisValue = value
                return newestVal, figure
            prevTempValue = value
            return dash.no_update, dash.no_update
        if  ((figure['data'][0]['x'] == []) and (figure['data'][0]['y'] == [])) or (prevMoisValue != value):
            
            
            discoveryCNT = discover(type="Container", location=value, rn="SoilMoisture")
            if (discoveryCNT != []):
                if discoverySUB == []:
                    subRI = createSubscription("moisSub", value + "/SoilMoisture", [3])

                vals, dates = requestAllCIN(value + "/SoilMoisture")
                if (vals != []) and (dates != []):          
                    newestVal = vals[dates.index(max(dates))]

                    figure['data'][0]['x']=dates
                    figure['data'][0]['y']=vals
                    prevMoisValue = value
                    return newestVal, figure 
    prevMoisValue = value
    return None, generateGraph([], [])


#### Humidity Values and Graph Updates ####
@app.callback(
    Output('hum-gauge', 'value'),
    Output('hum-graph', 'figure'),
    [Input('ddMenuS', 'value')],
    [Input('clkVal', 'n_intervals')],
    [Input('hum-graph', 'figure')],
)
def showHumVal(value, n, figure):
    global prevHumValue
    
    if ((value is not None)):

        discoverySUB = discover(type="Sub", location=value + "/Humidity", rn="humSub")

        if (discoverySUB != []) and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != []) and (prevHumValue == value): #and (figure['data'][0]['x'] != []) and (figure['data'][0]['y'] != [])
            
            latestDate = max(figure['data'][0]['x'])
            dates = []
            vals = []
            global newCINSensorBuffer
            for item in newCINSensorBuffer:
                if (item[0] == value + "/Humidity"):
                    date = item[2]
                    if (item[1] != "") and (date > latestDate):
                        vals.append(float(item[1]))
                        dates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
                    newCINSensorBuffer.remove(item)
            
            if (vals != []) and (dates != []):
                newestVal = vals[dates.index(max(dates))]
                figure['data'][0]['x'].extend(dates)
                figure['data'][0]['y'].extend(vals)
                prevHumValue = value
                return newestVal, figure
            prevHumValue = value
            return dash.no_update, dash.no_update
        if ((figure['data'][0]['x'] == []) and (figure['data'][0]['y'] == [])) or (prevHumValue != value):
            
            discoveryCNT = discover(type="Container", location=value, rn="Humidity")
            
            if discoveryCNT != []:
                if (discoverySUB == []):
                    subRI = createSubscription("humSub", value + "/Humidity", [3])
                
                vals, dates = requestAllCIN(value + "/Humidity")
                if (vals != []) and (dates != []):        
                    newestVal = vals[dates.index(max(dates))]

                    figure['data'][0]['x']=dates
                    figure['data'][0]['y']=vals
                    prevHumValue = value
                    return newestVal, figure 
    prevHumValue = value
    return None, generateGraph([], [])

#### GPS Location Update ####
@app.callback(
    Output('control-chart-live', 'figure'),
    [Input('ddMenuS', 'value')],
    [Input('clkVal', 'n_intervals')],
    [Input('control-chart-live', 'figure')]
)
def showGPSLoc(value, n, figure):
    global prevGPSValue
    if ((n>0) and (value is not None)):

        discoverySUB = discover(type="Sub", location=value + "/GPS", rn="gpsSub")
        if (discoverySUB != []) and ((figure['data'][0]['lat'] != [0]) or (figure['data'][0]['lon'] != [0])) and (prevGPSValue == value): #and ((figure['data'][0]['lat'] != [0]) or (figure['data'][0]['lon'] != [0])) 
            
            dates = []
            vals = []
            global newCINSensorBuffer
            for item in newCINSensorBuffer:
                if (item[0] == value + "/GPS"):
                    date = item[2]
                    if (item[1] != ""):
                        vals.append(item[1])
                        dates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
                    newCINSensorBuffer.remove(item)
            
            if (vals != []) and (dates != []):
                newestVal = vals[dates.index(max(dates))]
                if (newestVal == ""):
                    prevGPSValue = value
                    return generateMap([[0,0]])
                gpsLoc = newestVal.split(",")
                gpsLoc[0] = float(gpsLoc[0])
                gpsLoc[1] = float(gpsLoc[1])
                prevGPSValue = value
                return generateMap([gpsLoc])
            prevGPSValue = value
            return dash.no_update
        if (figure['data'][0]['lat'] == [0]) and (figure['data'][0]['lon'] == [0]) or (prevGPSValue != value):
            
            discoveryCNT = discover(type="Container", location=value, rn="GPS")
            if (discoveryCNT != []):
                if discoverySUB == []:
                    subRI = createSubscription("gpsSub", value + "/GPS", [3])

                gpsVal, gpsDate = requestVal(value + '/GPS/la')

                if (gpsVal == ""):
                    prevGPSValue = value
                    return generateMap([[0,0]])
                
                gpsLoc = gpsVal.split(",")
                gpsLoc[0] = float(gpsLoc[0])
                gpsLoc[1] = float(gpsLoc[1])
                prevGPSValue = value
                return generateMap([gpsLoc])
    prevGPSValue = value
    return generateMap([[0,0]])

#### Rainfall Trigger Update ####
@app.callback(
    Output('rainfallIndicator', 'value'),
    Output('rainfallIndicator', 'label'),
    Output('rainfallIndicator', 'color'),
    [Input('ddMenuS', 'value')],
    [Input('clkVal', 'n_intervals')],
    [Input('rainfallIndicator', 'value')],
    
)
def showRainIndicator(value, n, indicator):
    global prevRainValue
    if ((value is not None)):

        discovery = discover(type="Sub", location=value + "/RainfallTrigger", rn="rainSub")

        if (discovery != []) and (indicator != False) and (prevRainValue == value):
            dates = []
            vals = []
            global newCINSensorBuffer
            for item in newCINSensorBuffer:
                if (item[0] == value + "/RainfallTrigger"):
                    date = item[2]
                    if (item[1] != ""):
                        vals.append(item[1])
                        dates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
                    newCINSensorBuffer.remove(item)
            prevRainValue = value
            if (vals != []) and (dates != []):
                newestVal = vals[dates.index(max(dates))]
                if (newestVal == "OFF") or (newestVal == "off"):
                    return True, 'OFF', indicatorOffColor
                elif (newestVal == "ON") or (newestVal == "on"):
                    return True, 'ON', indicatorOnColor
                else:
                    return False, '', indicatorOffColor

            return dash.no_update, dash.no_update, dash.no_update
        if (discovery == []):
            discovery = discover(type="Container", location=value, rn="RainfallTrigger")

            if discovery != []:
                subRI = createSubscription("rainSub", value + "/RainfallTrigger", [3])

        val, date = requestVal(value + '/RainfallTrigger/la')
        prevRainValue = value
        if (val == "OFF") or (val == "off"):
            return True, 'OFF', indicatorOffColor
        elif (val == "ON") or (val == "on"):
            return True, 'ON', indicatorOnColor
        else:
            return False, '', indicatorOffColor
    prevRainValue = value
    return False, '', indicatorOffColor

#############################
#                           #
#   Control Tab Callbacks   #
#                           #
#############################

#### Update Sensor Device List ####
@app.callback( 
    Output('ddMenuSensor', 'options'),
    [Input('clkDevC', 'n_intervals')],
    [Input('ddMenuSensor', 'options')]
)   
def updateCSensorList(n, ddMenu):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if triggerID == 'clkDevC':
        global newSensorBuffer
        global rmSensorBuffer

        if (newSensorBuffer == []) and (rmSensorBuffer == []):
            return dash.no_update
        
        newDevices = []
        newDevRn = []
        rmDevices = []
        rmDevRn = []

        for device in newSensorBuffer:
            option = {"label": device, "value": "cse-in/"+device}
            if (option not in ddMenu) and (device not in rmSensorBuffer):
                newDevices.append(option)
                newDevRn.append(device)
                newSensorBuffer.remove(device)

        for device in rmSensorBuffer:
            option = {"label": device, "value": "cse-in/"+device}
            if (option in ddMenu) and (device not in newDevices):
                rmDevices.append(option)
                rmDevRn.append(device)
                rmSensorBuffer.remove(device)

        
        if (newDevices == []) and (rmDevices == []):
            return dash.no_update

        for option in ddMenu:
            if option not in rmDevices:
                newDevices.append(option)
        
        return newDevices
    return dash.no_update

#### Update Actuator Device List ####
@app.callback( 
    Output('ddMenuActuator', 'options'),
    [Input('clkDevC', 'n_intervals')],
    [Input('ddMenuActuator', 'options')]
)   
def updateActuatorList(n, ddMenu):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if triggerID == 'clkDevC':
        global newActuatorBuffer
        global rmActuatorBuffer

        if (newActuatorBuffer == []) and (rmActuatorBuffer == []):
            return dash.no_update
        
        newDevices = []
        newDevRn = []
        rmDevices = []
        rmDevRn = []

        for device in newActuatorBuffer:
            option = {"label": device, "value": "cse-in/"+device}
            if (option not in ddMenu) and (device not in rmActuatorBuffer):
                newDevices.append(option)
                newDevRn.append(device)
                newActuatorBuffer.remove(device)

        for device in rmActuatorBuffer:
            option = {"label": device, "value": "cse-in/"+device}
            if (option in ddMenu) and (device not in newDevices):
                rmDevices.append(option)
                rmDevRn.append(device)
                rmActuatorBuffer.remove(device)

        
        if (newDevices == []) and (rmDevices == []):
            return dash.no_update

        for option in ddMenu:
            if option not in rmDevices:
                newDevices.append(option)
        
        return newDevices
    return dash.no_update

#### Sensor Settings ####
@app.callback(
    Output('settingsAlertbox', 'children'),
    Output('settingsAlertbox', 'style'),
    Output('settingsAlertbox', 'hidden'),
    Output('settingsButton', 'n_clicks'),
    [Input('ddMenuSensor', 'value')],
    [Input('numAvg', 'value')],
    [Input('transmitP', 'value')],
    [Input('sampleP', 'value')],
    [Input('settingsButton', 'n_clicks')]
)
def settingsInput(value, numAvg, transmitP, sampleP, button):
    if ((button > 0) and (value is not None)):
        
        checkVal = transmitP - (numAvg * sampleP)
        if (checkVal < 4):
            errorMessage = [
                "Error: The transmit time has to be greater than 4 seconds " + 
                "more than the product of the sensor values to " +
                "average and the time between sensor readings!"
            ]
            return errorMessage, alertStyle, False, 0

        discovery = discover(type="Container", location = value + "/Settings")

        numLoc = value + "/Settings/numAverages"
        sampleLoc = value + "/Settings/samplePeriod"
        transmitLoc = value + "/Settings/transmitPeriod"

        if (numLoc in discovery) and (sampleLoc in discovery) and (transmitLoc in discovery):

            createContentInstance(str(numAvg), numLoc)
            createContentInstance(str(sampleP), sampleLoc)
            createContentInstance(str(transmitP), transmitLoc)
            if (checkVal >= 9):
                return [], alertStyleHidden, True, 0
            return ["Warning: GPS might not update properly with these settings!"], alertStyle, False, 0
        
        return ["Error: Settings cannot be modified on this sensor device!"], alertStyle, False, 0

    if ((button > 0) and (value is None)):
        return "Error: Please choose a device first!", alertStyle, False, 0

    return [], alertStyleHidden, True, 0

#### Turn on Valve ####
@app.callback(
    Output('valveOn', 'n_clicks'),
    Output('alertboxOff', 'children'),
    Output('alertboxOff', 'style'),
    Output('alertboxOff', 'hidden'),
    [Input('valveOn', 'n_clicks')],
    [Input('ddMenuActuator', 'value')],
    [Input('manualOverride', 'on')]
)
def actuatorOn(n, value, manualOverride):
    if (n > 0) and (value is not None) and (manualOverride):
        createContentInstance("11", value + "/requestedState")
        return 0, [], alertStyleHidden, True
    if (n > 0) and (manualOverride == False) and (value is not None):
        return 0, ["Please turn on manual override first!"], alertStyle, False
    return 0, [], alertStyleHidden, True

#### Turn off Valve ####
@app.callback(
    Output('valveOff', 'n_clicks'),
    Output('alertboxOn', 'children'),
    Output('alertboxOn', 'style'),
    Output('alertboxOn', 'hidden'),
    [Input('valveOff', 'n_clicks')],
    [Input('ddMenuActuator', 'value')],
    [Input('manualOverride', 'on')]
)
def actuatorOff(n, value, manualOverride):
    if (n > 0) and (value is not None) and (manualOverride):
        createContentInstance("10", value + "/requestedState")
        return 0, [], alertStyleHidden, True
    if (n > 0) and (manualOverride == False) and (value is not None):
        return 0, ["Please turn on manual override first!"], alertStyle, False
    return 0, [], alertStyleHidden, True

#### Manual Override Control ####
@app.callback(
    Output('manualOverride', 'on'),
    [Input('manualOverride', 'on')],
    [Input('ddMenuActuator', 'value')]
)
def manualOverrideC(manualOverride, value):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if (triggerID == 'manualOverride') and (value is not None):
        if (manualOverride):
            createContentInstance("10", value + "/requestedState")
        elif (manualOverride == False):
            createContentInstance("00", value + "/requestedState")
    elif (triggerID == 'ddMenuActuator') and (value is not None):
        status, date = requestVal(value + "/requestedState/la")
        if (len(status) == 2):
            if (status[0] == "1"):
                return True
        return False
    if (value is None):
        return False
    return dash.no_update


#### Valve Status Update ####
@app.callback(
    Output('valveStatus', 'value'),
    Output('valveStatus', 'label'),
    Output('valveStatus', 'color'),
    [Input('ddMenuActuator', 'value')],
    [Input('clkActStatus', 'n_intervals')],
    [Input('valveStatus', 'value')],
)
def showActuatorIndicator(value, n, indicator):
    if ((value is not None)):

        discovery = discover(type="Sub", location=value + "/actuatorState", rn="actSub")

        if (discovery != []) and (indicator != False):
            dates = []
            vals = []
            global newCINActuatorBuffer
            for item in newCINActuatorBuffer:
                if (item[0] == value + "/actuatorState"):
                    date = item[2]
                    if (item[1] != ""):
                        vals.append(item[1])
                        dates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
                    newCINActuatorBuffer.remove(item)

            if (vals != []) and (dates != []):
                newestVal = vals[dates.index(max(dates))]
                if (newestVal == "O"):
                    return True, 'OFF', indicatorOffColor
                elif (newestVal == "1"):
                    return True, 'ON', indicatorOnColor
                else:
                    return False, '', indicatorOffColor

            return dash.no_update, dash.no_update, dash.no_update
        if (discovery == []):
            discovery = discover(type="Container", location=value, rn="actuatorState")

            if discovery != []:
                subRI = createSubscription("actSub", value + "/actuatorState", [3])

        actVal, actDate = requestVal(value + '/actuatorState/la')

        if (actVal == "0"):
            return True, 'OFF', indicatorOffColor
        elif (actVal == "1"):
            return True, 'ON', indicatorOnColor
        else:
            return False, '', indicatorOffColor
    return False, '', indicatorOffColor

#### Link Devices ####
@app.callback(
    Output('linkButton', 'n_clicks'),
    Output('alertboxlink', 'children'),
    Output('alertboxlink', 'style'),
    Output('alertboxlink', 'hidden'),
    [Input('linkButton', 'n_clicks')],
    [Input('ddMenuActuator', 'value')],
    [Input('ddMenuSensor', 'value')]
)
def linkDev(n, aVal, sVal):
    if (n != 0) and (n != 7) and (n != 8) and (aVal is not None) and (sVal is not None):
        aDevName = aVal.split("/")[-1]
        sDevName = sVal.split("/")[-1]
        aLbl = requestVal(aVal, resourceType="AE")
        sLbl = requestVal(sVal, resourceType="AE")
        if (aDevName in sLbl) and (sDevName in aLbl):
            return 7, ["Devices are already linked!"], alertStyle, False
        if (aDevName not in sLbl):
            sLbl.append(aDevName)
            updateLabel(sVal, sLbl)
        if (sDevName not in aLbl):
            aLbl.append(sDevName)
            updateLabel(aVal, aLbl)
        return 8, ["Devices " + sDevName + " and " + aDevName + " have been linked!"], alertStyle, False
    elif (n > 0) and (aVal is not None) and (sVal is not None):
        return dash.no_update, dash.no_update, dash.no_update, dash.no_update
    return 0, [], alertStyleHidden, True


#### Unlink Devices ####
@app.callback(
    Output('unlinkButton', 'n_clicks'),
    Output('alertboxunlink', 'children'),
    Output('alertboxunlink', 'style'),
    Output('alertboxunlink', 'hidden'),
    [Input('unlinkButton', 'n_clicks')],
    [Input('ddMenuActuator', 'value')],
    [Input('ddMenuSensor', 'value')]
)
def unlinkDev(n, aVal, sVal):
    if (n != 0) and (n != 7) and (n != 8) and (aVal is not None) and (sVal is not None):
        aDevName = aVal.split("/")[-1]
        sDevName = sVal.split("/")[-1]
        aLbl = requestVal(aVal, resourceType="AE")
        sLbl = requestVal(sVal, resourceType="AE")
        if (aDevName not in sLbl) and (sDevName not in aLbl):
            return 7, ["Devices were not previously linked."], alertStyle, False
        if (aDevName in sLbl):
            sLbl.remove(aDevName)
            updateLabel(sVal, sLbl)
        if (sDevName in aLbl):
            aLbl.remove(sDevName)
            updateLabel(aVal, aLbl)
        return 8, ["Devices " + sDevName + " and " + aDevName + " have been unlinked!"], alertStyle, False
    elif (n > 0) and (aVal is not None) and (sVal is not None):
        return dash.no_update, dash.no_update, dash.no_update, dash.no_update
    return 0, [], alertStyleHidden, True

#### Display Sensor Linked Devices ####
@app.callback(
    Output('alertboxSensor', 'children'),
    Output('alertboxSensor', 'style'),
    Output('alertboxSensor', 'hidden'),
    [Input('ddMenuSensor', 'value')],
    [Input('linkButton', 'n_clicks')],
    [Input('unlinkButton', 'n_clicks')],
)
def displaySenLinked(value, linkButton, unlinkButton):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if (value is not None) and ((triggerID == 'ddMenuSensor') or (linkButton == 8) or (unlinkButton == 8)):
        devName = value.split("/")[-1]
        discovery = discover(type = "AE", rn=devName)
        if (discovery == []):
            return ["Error finding device."], alertStyle, False
        devLbl = requestVal(value, resourceType="AE")
        devLbl.remove("sensor")
        linkedDevList = ", ".join(devLbl)
        return ["Currently Linked Devices: " + linkedDevList], alertStyle, False
    if (value is None):
        return [], alertStyleHidden, True
    return dash.no_update, dash.no_update, dash.no_update

#### Display Actuator Linked Devices ####
@app.callback(
    Output('alertboxActuator', 'children'),
    Output('alertboxActuator', 'style'),
    Output('alertboxActuator', 'hidden'),
    [Input('ddMenuActuator', 'value')],
    [Input('linkButton', 'n_clicks')],
    [Input('unlinkButton', 'n_clicks')],
)
def displayActLinked(value, linkButton, unlinkButton):
    trigger = dash.callback_context
    triggerID = trigger.triggered[0]['prop_id'].split('.')[0]
    if (value is not None) and ((triggerID == 'ddMenuActuator') or (linkButton == 8) or (unlinkButton == 8)):
        devName = value.split("/")[-1]
        discovery = discover(type = "AE", rn=devName)
        if (discovery == []):
            return ["Error finding device."], alertStyle2, False
        devLbl = requestVal(value, resourceType="AE")
        devLbl.remove("actuator")
        linkedDevList = ", ".join(devLbl)
        return ["Currently Linked Devices: " + linkedDevList], alertStyle2, False
    if (value is None):
        return [], alertStyleHidden, True
    return dash.no_update, dash.no_update, dash.no_update

#### Parse oneM2M response outputs ####
def getResId(tag,r):
    try:
        resId = r.json()[tag]['ri']
    except:
        resId = ""
    return resId

def getCon(tag,r):
    try:
        con = r.json()[tag]['con']
    except:
        con = ""
    return con

def getCt(tag, r):
    try:
        ct = r.json()[tag]['ct']
    except:
        ct = ""
    return ct

def getDis(tag, r):
    try:
        list = r.json()[tag]
    except:
        list = []
    return list

def getCONs(tag, r):
    # Generate a list of contents and corresponding cts from input response
    outputCONs = []
    outputDates = []    
    try:
        list = r.json()[tag]["m2m:cin"]
        for element in list:
            try:
                outputCONs.append(float(element["con"]))
                date = element["ct"]
                outputDates.append(date[0:4] + "-" + date[4:6] + "-" + date[6:11] + ":" + date[11:13] + ":" + date[13:15])
            except:
                pass
    except:
        pass
    return outputCONs, outputDates

def getlbl(tag, r):
    try:
        labels = r.json()[tag]['lbl']
    except:
        labels = []
    return labels

#### Functions to send oneM2M primitives and requests to the CSE ####
def createAE(resourceName, origin = originator, acpi=""):
    poa = 'http://{}:{}'.format(appIP,appPort)
    if (acpi == ""):
        payld = { "m2m:ae": { "rr": True, "api": "NR_AE001", "apn": "IOTApp", "csz": [ "application/json" ], "srv": [ "2a" ], "rn": resourceName, "poa": [ poa ] } }
    else:
                payld = { "m2m:ae": { "rr": True, "acpi": [acpi], "api": "NR_AE001", "apn": "IOTApp", "csz": [ "application/json" ], "srv": [ "2a" ], "rn": resourceName, "poa": [ poa ] } }
    print ("AE Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + cseID
    hdrs = {'X-M2M-RVI':'2a', 'X-M2M-RI':"CAE_Test",'X-M2M-Origin':origin,'Content-Type':"application/json;ty=2"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("AE Create Response")
    print (r.text)
    return getResId('m2m:ae',r)

def deleteAE(resourceName):
    url = 'http://' + cseIP + ':' + csePort + '/' + cseName + '/'+ resourceName
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':originator,'Content-Type':"application/json"}
    r = requests.delete(url,  headers=hdrs)
    print ("AE Delete Response")
    print (r.text)

def createContainer(resourceName, parentID, origin, acpi, mni = 1):
    payld = { "m2m:cnt": { "rn": resourceName, "acpi": [acpi], "mni":mni} } 
    print ("CNT Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':origin,'Content-Type':"application/json;ty=3"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("CNT Create Response")
    print (r.text)
    return getResId('m2m:cnt',r)

def createACP(parentID, rn, devName, origin):
    payld = { "m2m:acp": { "rn": rn, "pv": { "acr": [{"acor": [originator], "acop": 63}, {"acor": [origin], "acop": 63}, {"acor": [devName], "acop": 63}] }, "pvs": {"acr": [{"acor": [origin], "acop": 63}]}}} 
    print ("ACP Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':origin,'Content-Type':"application/json;ty=1"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("ACP Create Response")
    print (r.text)
    return getResId('m2m:acp',r)

def createACPNotif(parentID, rn):
    payld = { "m2m:acp": { "rn": rn, "pv": { "acr": [{"acor": ["all"], "acop": 16}, {"acor": [originator], "acop": 63}] }, "pvs": {"acr": [{"acor": [originator], "acop": 63}]}}} 
    print ("ACP Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':"CAdmin",'Content-Type':"application/json;ty=1"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("ACP Create Response")
    print (r.text)
    return getResId('m2m:acp',r)

def requestVal(addr, resourceType="CIN"):
    url = 'http://' + cseIP + ':' + csePort + '/' + addr
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':originator, 'Accept':"application/json"}
    r = requests.get(url, headers=hdrs)
    if resourceType == "AE":
        return getlbl('m2m:ae', r)
    return getCon('m2m:cin',r), getCt('m2m:cin',r)

def requestAllCIN(addr):
    url = 'http://' + cseIP + ':' + csePort + '/' + addr + "?rcn=4"
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':originator, 'Accept':"application/json"}
    r = requests.get(url, headers=hdrs)
    return getCONs('m2m:cnt', r)

def updateLabel(addr, lbl):
    payld = {"m2m:ae": {"lbl": lbl}}
    url = 'http://' + cseIP + ':' + csePort + '/' + addr
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':'CAE_Test','X-M2M-Origin':originator, 'Accept':'application/json', 'Content-Type':'application/json'}
    r = requests.put(url, data=dumps(payld), headers=hdrs)

def discover(type="", label="", location=cseID, rn=""):
    if (type == "AE"):
        ty = "&ty=2"
    elif (type == "Container"):
        ty = "&ty=3"
    elif (type == "Content Instance"):
        ty = "&ty=4"
    elif (type == "Sub"):
        ty = "&ty=23"
    elif (type == "ACP"):
        ty = "&ty=1"
    else:
        ty = ""
    
    if (label != ""):
        label = "&lbl=" + label
    if (rn != ""):
        rn = "&rn=" + rn

    url = 'http://' + cseIP + ':' + csePort + '/' + location + '?fu=1' + ty + label + rn
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':originator, 'Accept':"application/json"}
    r = requests.get(url, headers=hdrs)
    return getDis('m2m:uril',r)


def createSubscription(resourceName, parentID, net, origin=originator):
    payld = { "m2m:sub": { "rn": resourceName, "enc": {"net":net}, "nu":[originator]} }
    print ("Sub Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"Sub",'X-M2M-Origin':origin,'Content-Type':"application/json;ty=23"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("SUB Create Response")
    print (r.text)
    return getResId('m2m:sub',r)

def createContentInstance(content, parentID):
    payld = { "m2m:cin": {"cnf": "application/text:0", "con": content} }
    print ("CIN Create Request")
    print (payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI':'2a','X-M2M-RI':"CAE_Test",'X-M2M-Origin':originator,'Content-Type':"application/json;ty=4"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print ("CIN Create Response")
    print (r.text)

def removefromAELabel(addr, removeList):
    lbl = requestVal(addr, resourceType="AE")
    for element in removeList:
        if (element in lbl):
            lbl.remove(element)
    updateLabel(addr, lbl)

def addtoAELabel(addr, addList):
    lbl = requestVal(addr, resourceType="AE")
    for element in addList:
        if (element not in lbl):
            lbl.append(element)
    updateLabel(addr, lbl)

#### Notifications ####
class SimpleHTTPRequestHandler(BaseHTTPRequestHandler): 

    def do_POST(self) -> None:
        global newCINSensorBuffer
        global newCINActuatorBuffer
        # Construct return header
        mySendResponse(self, 200, '2000')

        # Get headers and content data
        length = int(self.headers['Content-Length'])
        contentType = self.headers['Content-Type']
        post_data = self.rfile.read(length)
        
        # Print the content data
        print('### Notification')
        print (self.headers)
        print(post_data.decode('utf-8'))
        r = loads(post_data.decode('utf8').replace("'", '"'))
        # Obtain the net value (added or deleted)
        try:
            net = r['m2m:sgn']['nev']['net']
        except:
            net = None
        # Try a statement to parse if its an AE
        try:
            rn = r['m2m:sgn']['nev']['rep']['m2m:ae']['rn']
        except:
            rn = ""
        # Append to appropriate "buffer" if its an AE
        if (rn != ""):
            try:
                lbl = r['m2m:sgn']['nev']['rep']['m2m:ae']['lbl']
            except:
                lbl = []
            if "sensor" in lbl:
                if (net == 3) and (rn not in newSensorBuffer):
                    newSensorBuffer.append(rn)
                elif (net == 4) and (rn not in rmSensorBuffer):
                    rmSensorBuffer.append(rn)
            elif "actuator" in lbl:
                if (net == 3) and (rn not in newActuatorBuffer):
                    newActuatorBuffer.append(rn)
                elif (net == 4) and (rn not in rmActuatorBuffer):
                    rmActuatorBuffer.append(rn)

        # If its not an AE, then its probably a content instance
        elif (rn == ""):
            try:
                con = r['m2m:sgn']['nev']['rep']['m2m:cin']['con']
            except:
                con = ""
            
            if (con != ""):
                try:
                    lbl = r['m2m:sgn']['nev']['rep']['m2m:cin']['lbl']
                except:
                    lbl = []

                if (lbl != []):
                    url = 'cse-in/' + lbl[0]
                    ct = r['m2m:sgn']['nev']['rep']['m2m:cin']['ct']
                    if lbl[0].split("/")[-1] == "actuatorState":
                            newCINActuatorBuffer.append([url, con, ct])
                    newCINSensorBuffer.append([url, con, ct])

# Send appropriate response for oneM2M ACME CSE
def mySendResponse(self, responseCode, rsc):
        self.send_response(responseCode)
        self.send_header('X-M2M-RSC', rsc)
        self.end_headers()

# Run/Start HTTP server
def run_http_server():
    httpd = HTTPServer(('', appPort), SimpleHTTPRequestHandler)
    print('**starting server & waiting for connections**')
    httpd.serve_forever()

#### Run app and http Server ####
if __name__ == "__main__":

    # Start HTTP server to handle subscription notifications
    threading.Thread(target=run_http_server, daemon=True).start()

    # Verify that AE, ACP are created
    discoverAE = discover(type="AE", rn = appName)
    discoverACP = discover(type="ACP", rn = appName + "NotifACP")

    # Create ACP if it doesnt exist
    if (discoverACP == []):
        acpID = createACPNotif(cseID, appName + "NotifACP")

    # Create AE if it doesn't exist
    if (discoverAE == []):      
        aeID = createAE(appName, acpi="cse-in/" + appName + "NotifACP")

    # Create SUB if it doesn't exist
    discoverSUB = discover(type="Sub", rn = "deviceSub")
    if (discoverSUB == []):
        subID = createSubscription("deviceSub", cseID, [3, 4])    

    # Run dashboard app
    app.run_server(debug=False, host = ip,  port=8050, use_reloader=False)
