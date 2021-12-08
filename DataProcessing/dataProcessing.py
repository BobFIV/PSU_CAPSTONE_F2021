# for sending http requests
import requests
from json import loads, dumps

# for threading for sensors and http listener
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

appIP = "DEFINE THIS"
appPort = 8000
appName = "dataProcessing"
dashHost = "localhost"
cseIP = "3.231.72.34"
csePort = "8080"
cseID = "/id-in"
cseName = "cse-in"


newSensorBuffer = []
newActuatorBuffer = []

sensorSub = ""
cntSub = ""
cinFlag = ""

# AE-ID for this device
originator = 'C' + appName


def calculateActuatorState(temp, hum, gps, soilMoisture, rainfallTrigger, maxThreshold=60.0):
    latitude = gps[0]
    longitude = gps[1]

    if not (latitude == '0.0' and longitude == '0.0'):  # no GPS fix
        url = 'http://api.openweathermap.org/data/2.5/weather?lat={}&lon={}&appid=ac7c75b9937a495021393024d0a90c44&units=metric'.format(
            latitude, longitude)

        res = requests.get(url)
        data = res.json()

        pressure = data['main']['pressure']
    else:
        pressure = ""

    if temp < 4:  # 4 in Celsius based on grass, too cold
        return 0
    # for humidity, for most mature plants is 50-60%
    elif hum > 60:
        return 0
    elif rainfallTrigger == 'ON':
        return 0
    elif soilMoisture != -1.0 and soilMoisture > maxThreshold:
        return 0
    else:
        return 1  # turning it on


#### oneM2M Functions ####
def getResId(tag, r):
    try:
        resId = r.json()[tag]['ri']
    except:
        # already created
        resId = ""
    return resId


def getCon(tag, r):
    try:
        con = r.json()[tag]['con']
    except:
        # already created
        con = ""
    return con


def getCt(tag, r):
    try:
        ct = r.json()[tag]['ct']
    except:
        # already created
        ct = ""
    return ct


def getDis(tag, r):
    try:
        list = r.json()[tag]
    except:
        # already created
        list = ""
    return list


def getlbl(tag, r):
    try:
        labels = r.json()[tag]['lbl']
    except:
        labels = ""
    return labels


def createAE(resourceName, origin=originator, acpi=""):
    poa = 'http://{}:{}'.format(appIP, appPort)
    if acpi == "":
        payld = {"m2m:ae": {"rr": True, "api": "NR_AE001", "apn": "IOTApp", "csz": ["application/json"], "srv": ["2a"],
                            "rn": resourceName, "poa": [poa]}}
    else:
        payld = {"m2m:ae": {"rr": True, "acpi": [acpi], "api": "NR_AE001", "apn": "IOTApp", "csz": ["application/json"],
                            "srv": ["2a"], "rn": resourceName, "poa": [poa]}}
    print("AE Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + cseID
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': origin, 'Content-Type': "application/json;ty=2"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("AE Create Response")
    print(r.text)
    return getResId('m2m:ae', r)


def deleteAE(resourceName):
    url = 'http://' + cseIP + ':' + csePort + '/' + cseName + '/' + resourceName
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': originator, 'Content-Type': "application/json"}
    r = requests.delete(url, headers=hdrs)
    print("AE Delete Response")
    print(r.text)


def createContainer(resourceName, parentID, origin, acpi, mni=1):
    payld = {"m2m:cnt": {"rn": resourceName, "acpi": [acpi], "mni": mni}}
    print("CNT Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': origin, 'Content-Type': "application/json;ty=3"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("CNT Create Response")
    print(r.text)

    return getResId('m2m:cnt', r)


def createACP(parentID, rn, devName, origin):
    payld = {"m2m:acp": {"rn": rn, "pv": {
        "acr": [{"acor": [originator], "acop": 63}, {"acor": [origin], "acop": 63}, {"acor": [devName], "acop": 63}]},
                         "pvs": {"acr": [{"acor": [origin], "acop": 63}]}}}
    print("ACP Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': origin, 'Content-Type': "application/json;ty=1"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("ACP Create Response")
    print(r.text)

    return getResId('m2m:acp', r)


def createACPNotif(parentID, rn):
    payld = {"m2m:acp": {"rn": rn, "pv": {"acr": [{"acor": ["all"], "acop": 16}, {"acor": [originator], "acop": 63}]},
                         "pvs": {"acr": [{"acor": [originator], "acop": 63}, {"acor": [originator], "acop": 36}]}}}
    print("ACP Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "v4f36p7xg", 'X-M2M-Origin': "CAdmin",
            'Content-Type': "application/json;ty=1"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("ACP Create Response")
    print(r.text)

    return getResId('m2m:acp', r)


def requestVal(addr):
    url = 'http://' + cseIP + ':' + csePort + '/' + addr
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': originator, 'Accept': "application/json"}
    r = requests.get(url, headers=hdrs)
    return getlbl('m2m:ae', r)

def retrieveCIN(addr):
    url = 'http://' + cseIP + ':' + csePort + '/' + addr + '/la'
    print(url)
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': originator, 'Accept': "application/json"}
    r = requests.get(url, headers=hdrs)
    return getCon('m2m:cin', r)


def discover(type, label="", location=cseID, rn=""):
    if type == "AE":
        ty = "2"
    elif type == "Container":
        ty = "3"
    elif type == "Content Instance":
        ty = "4"
    else:
        return "errorType"

    if label != "":
        label = "&lbl=" + label
    if rn != "":
        rn = "&rn=" + rn

    url = 'http://' + cseIP + ':' + csePort + '/' + location + '?fu=1&drt=2&ty=' + ty + label + rn
    print(url)
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "CAE_Test", 'X-M2M-Origin': originator, 'Accept': "application/json"}
    r = requests.get(url, headers=hdrs)
    print(r.text)
    return getDis('m2m:uril', r)


def createSubscription(resourceName, parentID, net):
    payld = {"m2m:sub": {"rn": resourceName, "enc": {"net": net}, "nu": [originator]}}
    print("Sub Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    print(url)
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "Sub", 'X-M2M-Origin': originator,
            'Content-Type': "application/json;ty=23"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("SUB Create Response")
    print(r.text)
    return getResId('m2m:sub', r)


def createContentInstance(content, parentID):
    payld = {"m2m:cin": {"cnf": "application/text:0", "con": content}}
    print("CIN Create Request")
    print(payld)
    url = 'http://' + cseIP + ':' + csePort + '/' + parentID
    hdrs = {'X-M2M-RVI': '2a', 'X-M2M-RI': "sensorValue", 'X-M2M-Origin': originator,
            'Content-Type': "application/json;ty=4"}
    r = requests.post(url, data=dumps(payld), headers=hdrs)
    print("CIN Create Response")
    print(r.text)

    return getResId('m2m:cin', r)


#### Notifications ####
class SimpleHTTPRequestHandler(BaseHTTPRequestHandler):  # fix for valve notifications

    def do_POST(self) -> None:

        # Construct return header
        mySendResponse(self, 200, '2000')

        # Get headers and content data
        length = int(self.headers['Content-Length'])
        contentType = self.headers['Content-Type']
        post_data = self.rfile.read(length)

        # Print the content data
        print('### Notification')
        print(self.headers)
        print(post_data.decode('utf-8'))
        r = loads(post_data.decode('utf8').replace("'", '"'))
        try:
        # Obtain the net value (added or deleted)
            net = r['m2m:sgn']['nev']['net']
        except:
            net = 0
        # Try a statement to parse if its an AE
        ae = False
        CNT = False
        CIN = False
        try:
            rn = r['m2m:sgn']['nev']['rep']['m2m:ae']['rn']
            ae = True
        except:
            try:
                rn = r['m2m:sgn']['nev']['rep']['m2m:cnt']['rn']
                CNT = True
            except:
                try:
                    rn = r['m2m:sgn']['nev']['rep']['m2m:cin']['con']
                    CIN = True
                except:
                    rn = ""
                    pass
        # Append to appropriate "buffer" if its an AE
        if ae:
            lbl = r['m2m:sgn']['nev']['rep']['m2m:ae']['lbl']
            if "sensor" in lbl:
                if (net == 3) and (rn not in newSensorBuffer):
                    newSensorBuffer.append(rn)
                    print("New Sensor Buffer: ")
                    print(newSensorBuffer)
                    global sensorSub
                    sensorSub = cseName+"/"+rn
                elif (net == 4) and (rn in newSensorBuffer):
                    newSensorBuffer.remove(rn)
                    print("New Sensor Buffer: ")
                    print(newSensorBuffer)
            elif "actuator" in lbl:
                if (net == 3) and (rn not in newActuatorBuffer):
                    newActuatorBuffer.append(rn)
                    print("New Actuator Buffer: ")
                    print(newActuatorBuffer)
                elif (net == 4) and (rn in newActuatorBuffer):
                    newActuatorBuffer.remove(rn)
                    print("New Actuator Buffer: ")
                    print(newActuatorBuffer)
        elif CNT:
            if rn == "GPS":  # GPS is used because it is the last CIN to be sent, quick fix
                print("GPS CNT")
                pi = r['m2m:sgn']['nev']['rep']['m2m:cnt']['pi']
                global cntSub
                cntSub = cseName + "/" + pi[1:] + "/" + rn
            else:
                print("Non-GPS CNT\n")
            pass
        elif CIN:  # only get from GPS CNT for now
            lbl = r['m2m:sgn']['nev']['rep']['m2m:cin']['lbl']
            lbl = "cse-in/"+lbl[0]
            global cinFlag
            cinFlag = lbl
        else:
            pass
        print("Done handling, waiting for new notification")


def mySendResponse(self, responseCode, rsc):
    self.send_response(responseCode)
    self.send_header('X-M2M-RSC', rsc)
    self.end_headers()


def run_http_server():
    httpd = HTTPServer(('', appPort), SimpleHTTPRequestHandler)
    print('**starting server & waiting for notifications**')
    httpd.serve_forever()


#### Run app and http Server ####
if __name__ == "__main__":

    # Start HTTP server to handle subscription notifications
    threading.Thread(target=run_http_server, daemon=True).start()

    discoverList = discover("AE", rn=appName)

    if not discoverList:
        # Register as AE
        acpID = createACPNotif(cseID, "dataProcNotifACP")
        aeID = createAE(appName, acpi=acpID)
        subID = createSubscription("dataProcessingSub", cseID, [3, 4])
    while True:
        if sensorSub != "":  # create subscription to AE
            createSubscription("dataProcessingSub", sensorSub, [3])
            sensorSub = ""
        elif cntSub != "":  # create subscription to CNT
            createSubscription("dataProcessingSub", cntSub, [3])
            cntSub = ""
        elif cinFlag != "":  # grab latest values from sensor
            aeri = cinFlag.split("/")[1]
            temp = retrieveCIN("cse-in/"+aeri+"/Temperature")
            hum = retrieveCIN("cse-in/"+aeri + "/Humidity")
            gps = retrieveCIN("cse-in/"+aeri + "/GPS")
            soilMoisture = retrieveCIN("cse-in/"+aeri + "/SoilMoisture")
            try:  # error if soilMoisture not found since this is optional
                soilMoisture = float(soilMoisture)
            except:
                soilMoisture = -1.0
            rainfallTrigger = retrieveCIN("cse-in/"+aeri + "/RainfallTrigger")
            resultState = calculateActuatorState(float(temp), float(hum), gps.split(","), soilMoisture, rainfallTrigger)
            if resultState == 0:
                printState = "OFF"
            else:
                printState = "ON"
            print("\n\n Actuator State will be:" + printState + "\n\n")
            lblList = requestVal("cse-in/"+aeri)
            lblList.remove("sensor")
            for label in lblList:
                currentState = retrieveCIN("cse-in/" + label + "/requestedState")
                print(currentState)
                if not currentState or currentState[0] == "0":  # we are in automatic mode
                    cntri = discover("Container", location="cse-in/"+label, rn="requestedState")
                    cntri = cntri[0].split("id-in/")[1]
                    print(cntri)
                    content = "0"
                    content += str(resultState)
                    createContentInstance(content, cntri)
                else:  # in manual mode, do not send automatic state
                    pass
            cinFlag = ""
        else:
            pass
