.. _https_client:

nRF9160: oneM2M Sensor Sample
#####################

.. contents::
   :local:
   :depth: 2

The oneM2M Sensor sample demonstrates an implementation of client-side oneM2M communication with a cloud CSE, sending sensor data from internal and optional external wired sensors


Requirements
************

The sample supports the following development kit:

.. table-from-rows:: /includes/sample_board_rows.txt
   :header: heading
   :rows: nrf9160dk_nrf9160_ns

.. include:: /includes/spm.txt

Overview
********

The sample first initializes the needed libraries such as LTE, PSM, ADC, and GPS
   The GPS is initaialized to work on a background thread, however it shares radio resources with the LTE - so when the LTE is in use the GPS is blocked.
   GPS has lower priority than the LTE for this application.
It then connects to the cloud CSE pointed to by the addr and port configuration settings, begins retrieving an AE, then CNTs.
   If these resources do not already exist they and their corresponding ACPs are created
Then the program enters a loop of:
   Retrieving latest user settings from the Settings CNT
   Reading the Temperature, SoilMoisture, and Humidity sensors averaging according to settings, with times between sensor reads defined by Settings
   Reads latest GPS and Rainfall Trigger data
   Sends all of these values as CINs to the cloud CSE
   Time between transmissions to the cloud CSE is defined in Settings
   
Installation
********
See the Hackster article for detailed instructions on writing/programming the device and setting up the environment:
https://www.hackster.io/psu-capstone-pj3c/cellular-iot-irrigation-system-for-onem2m-nordic-thingy-91-5cfc14

