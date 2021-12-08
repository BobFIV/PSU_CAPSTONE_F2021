Data Processing Application

This application runs alongside the CSE in a cloud instance, and uses incoming sensor data (and sensor AE label) to determine a valve state and
send that to the corresponding actuator(s).

See the Hackster article for instructions on how to configure and set up/run the program:
https://www.hackster.io/psu-capstone-pj3c/cellular-iot-irrigation-system-for-onem2m-nordic-thingy-91-5cfc14

To modify how the valve state is calculated (for instance, to change moisture thresholds or add calculations rather than thresholding)
  edit the calculateActuatorState method
