/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * Modified by: Julian Campanella
 * Penn State University Computer Engineering Capstone
 * November 4, 2021
 */

#include <string.h>
#include <zephyr.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <net/socket.h>
#include <modem/nrf_modem_lib.h>
#include <net/tls_credentials.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/modem_key_mgmt.h>
#include <string.h>
#include <logging/log.h>
#include <net/net_ip.h>
#include <net/http_client.h>
#include <regex.h>
#include <zephyr.h>
#include <ext_sensors.c>
#include <drivers/sensor.h>
#include <drivers/gps.h>
#include <drivers/adc.h>
#include <sys/reboot.h>
#include <hal/nrf_gpio.h>
#include <drivers/uart.h>
#include <net/socket.h>
#include <nrf9160.h>
LOG_MODULE_REGISTER(net_http_client_sample, LOG_LEVEL_INF);


/* HTTP and oneM2M settings */
//Port to connect to
#define HTTPS_PORT 8080
//HTTP server address to connect to
#define SERVER_ADDR4  "3.231.72.34"
//Variable to set if using Soil Mositure Sensor
#define SoilMoisture true
//Variable to set if using Rainfall Trigger Sensor
#define RainfallTrigger true
//CSE ID of the ACME in use
#define cseID "/id-in"
//CSE Name of the ACME in use
#define cseName "cse-in"
//Device name - change this if using with multiple devices running this code
#define deviceName "Thingy91_JC"
//originator - must be 'C'+deviceName by convention
#define originator "CThingy91_JC"
/* end HTTP and oneM2M settings */

/* Settings involving Sensors, Periods are in Seconds */
int	numAverages = 1; 	//number of times to average, max of 100
int transmitPeriod = 10; //time to wait between sending msgs to cloud ACME
int samplePeriod = 1;   //time between sensor samples
//length of HTTP respones buffer
#define MAX_RECV_BUF_LEN 1024

//variables to store relative max min ADC read values. This is at 1.8Vdc so this may need to be changed depending on your Vdd
int minMoisture = 175;
int maxMoisture = 400;

//variables needed for ADC channel reading
struct device *adc_dev;
#define BUFFER_SIZE 1
static uint16_t m_sample_buffer[BUFFER_SIZE];

//Setting needed ADC settings, this is only done if using the correct board (in this project Thingy:91, but ADC is compatible with the DK as well)
#if defined(CONFIG_BOARD_NRF9160DK_NRF9160_NS)	|| \
	defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
#include <hal/nrf_saadc.h>
#define ADC_DEVICE_NAME DT_ADC_0_NAME
#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_1ST_CHANNEL_ID 0
#define ADC_1ST_CHANNEL_INPUT NRF_SAADC_INPUT_AIN0	// can change this to AIN3 if want to sample TP33 instead of TP32. Not done for this project

#endif

/* buffer to store HTTP request response in*/
static uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];

static float GPSLatitude = 0.0;
static float GPSLongitude = 0.0;

static const struct device *gps_dev;
static struct k_work_delayable reboot_work;
static struct k_work gps_start_work;
static uint64_t	start_search_timestamp;
static uint64_t fix_timestamp;
double TempValues[100];
double HumValues[100];
double MoistureValues[100];
double moisture;


//ADC Channel Configuration
static const struct adc_channel_cfg m_1st_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_1ST_CHANNEL_ID,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
	.input_positive = ADC_1ST_CHANNEL_INPUT,
#endif
};

/* Function to sample the ADC channel based on the settings above. It will modify a percentage double variable based on the minMoisture and maxMoisture ADC counts above 
	and Return -1 or err on error, or 0 on success
*/
static int adc_sample(void)
{
	int ret;

	const struct adc_sequence sequence = {
		.channels = BIT(ADC_1ST_CHANNEL_ID),
		.buffer = m_sample_buffer,
		.buffer_size = sizeof(m_sample_buffer),
		.resolution = ADC_RESOLUTION,
	};

	if (!adc_dev) {
		return -1;
	}

	ret = adc_read(adc_dev, &sequence);
	if (ret < 0) {
		printk("ADC read err: %d\n", ret);
		return ret;
	}
	/* Print the AIN0 values */
		float adc_voltage = 0;
		adc_voltage = (float)(((float)m_sample_buffer[0] / 1023.0f) *
				      3600.0f);
		printk("ADC raw value: %d\n", m_sample_buffer[0]);
		printf("Measured voltage: %f mV\n", adc_voltage);
		double denom = maxMoisture - minMoisture;
		//inverted because higher voltage means less moisture
		moisture = 100.0-(m_sample_buffer[0] - minMoisture)/denom*100;
		printk("Percentage: %f\n", moisture);

	return ret;
}

/*  declaration of function for GPS thread
*/
static void gps_start_work_fn(struct k_work *work);

/* sets up a socket to an HTTP addr:port/url
	This is taken from the zephyr http_client examples */
static int setup_socket(sa_family_t family, const char *server, int port, int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
	int ret = 0;

	memset(addr, 0, addr_len);

	if (family == AF_INET) {
		net_sin(addr)->sin_family = AF_INET;
		net_sin(addr)->sin_port = htons(port);
		inet_pton(family, server, &net_sin(addr)->sin_addr);
	} else {
		net_sin6(addr)->sin6_family = AF_INET6;
		net_sin6(addr)->sin6_port = htons(port);
		inet_pton(family, server, &net_sin6(addr)->sin6_addr);
	}

	
	*sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
	

	if (*sock < 0) {
		LOG_ERR("Failed to create %s HTTP socket (%d)", family_str,
			-errno);
	}

	return ret;
}

/* Gets and stores the respones from an HTTP request
	This is taken from the zephyr http_client examples */
static void response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

/* connects a socket to an HTTP addr:port/url
	This is taken from the zephyr http_client examples */
static int connect_socket(sa_family_t family, const char *server, int port, int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(family, server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("Cannot connect to %s remote (%d)",
			family == AF_INET ? "IPv4" : "IPv6",
			-errno);
		ret = -errno;
	}

	return ret;
}

/* Creates Acess Control Policy that governs what can access the AE/CNT's of the device
	Currently set to give read/write access to itself and the dashboard application
*/
char *createACP(char *parentID, char *acpi)
{
	printk("Creating ACP");
    struct sockaddr_in addr4;
    int sock4 = -1;
    int32_t timeout = 5 * MSEC_PER_SEC;
    int ret = 0;
    int port = HTTPS_PORT;
    char url[20];
    sprintf(url, "/%s", parentID);
    char *substring;
	substring = "";
    if (IS_ENABLED(CONFIG_NET_IPV4))
    {
        (void)connect_socket(AF_INET, SERVER_ADDR4, port,
                             &sock4, (struct sockaddr *)&addr4,
                             sizeof(addr4));
    }

    if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
    {
        struct http_request req;

        memset(&req, 0, sizeof(req));
        char origin[30];
        sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
        const char *headers[] = {
            "X-M2M-RI: xyz1\r\n",
            origin,
            "X-M2M-RVI: 2a\r\n",
            "Content-Type: application/json;ty=1\r\n",
            NULL};
       char payload[275] = {0};
        sprintf(payload, "{ \"m2m:acp\": { \"rn\": \"%s\", \"pv\": { \"acr\": [{\"acor\": [\"%s\"], \"acop\": 63}, {\"acor\": [\"CdashApp\"], \"acop\": 63}, {\"acor\": [\"CdataProcessing\"], \"acop\": 63}] }, \"pvs\": {\"acr\": [{\"acor\": [\"%s\"], \"acop\": 63}]}}}", acpi, originator, originator);
        printk("ACP Create Payload: %s\r\n", payload);
        req.method = HTTP_POST;
        req.url = url;
        req.host = SERVER_ADDR4;
        req.protocol = "HTTP/1.1";
        req.payload = payload;
        req.payload_len = strlen(payload);
        req.response = response_cb;
        req.header_fields = headers;
        req.recv_buf = recv_buf_ipv4;
        req.recv_buf_len = sizeof(recv_buf_ipv4);

        printk("ACP create response:\n");
        ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
		char *riString = "\"ri\":";
        substring = strstr(recv_buf_ipv4, riString);
        substring = strtok(substring, ",");
        char *space = " ";
        substring = strstr(substring, space);

        char *result = substring;
        result++;
        result++;

        result[strlen(result) - 1] = 0;
        substring = result;
        printk("Result ri:%s\n", substring);
        close(sock4);
    }
    else
    {
        printk("Cannot open socket/send CNT command \r\n");
    }
    return substring;
}


/* creates an application entity, this is used as the overall device that holds containers for each sensor
	Returns an RI that is required for containers to access the AE
 */
char* createAE(char* resourceName, char* acpi) { 
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char* substring;

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection."); 
	// 	return -ECONNABORTED;
	// }
	substring = "";
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=2\r\n",
			NULL
		};
		char payload[225] = {0}; 
		sprintf(payload, "{ \"m2m:ae\": {\"rr\": true, \"rn\": \"%s\", \"acpi\": [\"%s\"], \"api\": \"NR_AE001\", \"apn\": \"IOTApp\", \"lbl\": [ \"sensor\" ], \"csz\": [\"application/json\"], \"srv\": [\"2a\"]} }", resourceName, acpi);
		printk("Application Entity Payload: %s\r\n", payload);
		req.method = HTTP_POST;
		req.url = "/cse-in";
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.payload = payload;
		req.payload_len = strlen(payload);
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("AE create response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 POST");

		char* riString = "\"ri\":";
		substring = strstr(recv_buf_ipv4, riString);
		substring = strtok(substring, ",");
		char* space = " ";
		substring = strstr(substring, space);
		char *result = substring;
		result++;
		result++;

		result[strlen(result)-1] = 0;
		substring = result;
		printk("Result ri:%s\n", substring);
		close(sock4);
	} else {
		printk("Cannot open socket/send AE command \r\n");
		substring = "";
	}
	return substring;
}

/* Checks if an application entity exists, this is used as the overall device that holds containers for each sensor
	Returns an RI that is required for containers to access the AE
 */
char* retrieveAE(char* resourceName) {
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char* substring;

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	substring = "";
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		char url[60];
		sprintf(url, "/cse-in?fu=1&ty=2&rn=%s&drt=2", resourceName);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Accept: application/json\r\n",
			NULL
		};
		req.method = HTTP_GET;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("AE retrieve response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 GET");

		printk("testing valid response, find []}\n");
		substring = strstr(recv_buf_ipv4,  "[]}");
		if (substring) {
			substring = "";
			return substring;
		}
		printk("finding %s\n", resourceName);
		substring = strstr(recv_buf_ipv4, resourceName);
		substring = strtok(substring, "\"");
		if (substring != NULL) {
			printk("Result of retrieve:%s\n", substring);
		} else {
			printk("No matching AE found");
			substring = "";
		}
		close(sock4);
	} else {
		printk("Cannot open socket/send AE command \r\n");
		substring = "";
	}
	return substring;
}

/* deletes an application entity, which deletes EVERYTHING under it as well
	Note that this can also be done on the webui for the ACME if you can access that
*/
int deleteAE(char* resourceName) { 
struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		char url[19+10+5+7+9] = {0};
		sprintf(url, "/cse-in/%s",resourceName);
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json\r\n",
			NULL
		};
		req.method = HTTP_DELETE;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);
		printk("AE Delete response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 DELETE");

		close(sock4);
	}
	return 0;
}

/* creates a container for the AE, this is used to seperate sensors and store content instances 
	Note that mni is required and not optional to provide
	Returns an RI that is required for content instances to access the CNT
*/
char* createCNT(char* resourceName, char* parentID, int mni, char* acpi) {
	printk("Creating container called %s\n", resourceName);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char url[50];
	sprintf(url, "/%s", parentID);
	char* substring = "";
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}
	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP connection.");
		return -ECONNABORTED;
	}

	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;
		printk("CNT Socket Connected\n");
		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=3\r\n",
			NULL
		};
		char payload[175] = {0};
		sprintf(payload, "{ \"m2m:cnt\": {\"rn\": \"%s\", \"acpi\": [\"%s\"], \"lbl\": [ \"sensorVal\" ], \"mni\": %d} }", resourceName, acpi, mni);
		printk("Container Create Payload: %s\r\n", payload);
		req.method = HTTP_POST;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.payload = payload;
		req.payload_len = strlen(payload);
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("CNT create response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
		char* riString = "\"ri\":";
		substring = strstr(recv_buf_ipv4, riString);
		substring = strtok(substring, ",");
		char* space = " ";
		substring = strstr(substring, space);

		char *result = substring;
		result++;
		result++;

		result[strlen(result)-1] = 0;
		substring = result;
		printk("Result ri:%s\n", substring);
		close(sock4);
	} else {
		printk("Cannot open socket/send CNT command \r\n");
	}
	return substring;
}

/* checks if a container with resourceName exists for the AE, this is used to seperate sensors and store content instances 
	Note that mni is required and not optional to provide, usually 10 for sensors and 1 for subscriptions. 
	Returns an RI that is required for content instances to access the CNT
*/
char* retrieveCNT(char* resourceName, char* parentID) {
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char* substring;

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}

	if (sock4 < 0) {
		LOG_ERR("Cannot create HTTP connection.");
		return -ECONNABORTED;
	}
	substring = "";
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		char url[80];
		sprintf(url, "/cse-in?fu=1&ty=3&rn=%s&pi=%s&drt=2", resourceName, parentID);
		printk("Query url: %s\n", url);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Accept: application/json\r\n",
			NULL
		};
		req.method = HTTP_GET;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("CNT retrieve response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 GET");

		printk("testing valid response, find []}\n");
		substring = strstr(recv_buf_ipv4,  "[]}");
		if (substring) {
			substring = "";
			return substring;
		}
		printk("finding cnt\n");
		substring = strstr(recv_buf_ipv4,  "cnt");
		substring = strtok(substring, "\"");
		if (substring != NULL) {
			printk("Result of retrieve:%s\n", substring);
		} else {
			printk("No matching CNT found");
			substring = "";
		}
		close(sock4);
	} else {
		printk("Cannot open socket/send CNT command \r\n");
		substring = "";
	}
	return substring;
}

/* creates a content instance for the AE, this is used to send sensor values to their respective containers 
	Assumes content is a string - as temp/hum/moisture sensors return doubles but GPS is two values in a string and rainfalltrig is bool
 */
int createCIN(char* parentID, char* content, char* label) {
	printk("Sending CIN with parentID: %s\n", parentID);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char url[80] = {0};
	sprintf(url, "/%s", parentID);
	printk("url created sucessfully\n");

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}

	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: sensorValue\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=4\r\n",
			NULL
		};
		printk("Headers successfully created\n\n Content is %s with size %zu", content, sizeof(content));
		char payload[120] = {0};
		sprintf(payload, "{ \"m2m:cin\": {\"cnf\": \"application/text:0\", \"lbl\": [\"%s\"],\"con\": \"%s\"} }", label, content);
		printk("Content Instance Payload: %s\r\n", payload);
		
		req.method = HTTP_POST;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.payload = payload;
		req.payload_len = strlen(payload);
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("CIN create response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 POST");

		close(sock4);
	} else {
		printk("Cannot open socket/send CNT command \r\n");
	}
	return 0;
}

/* retrieves latest content instance for the CNT, this is used to retrieve pertinent settings values from settings AE 
	Assumes content is an int - as numAverages and transmitPeriod should be integers
 */
void retrieveCIN(char* parentID, char* CNTName) {
	printk("Retrieving CIN from AE: %s and container: %s\n", parentID, CNTName);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 5 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char url[80];
	sprintf(url, "/cse-in/%s/%s/la", parentID, CNTName);
	printk("CIN retrieve url: %s\n", url);

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
				     &sock4, (struct sockaddr *)&addr4,
				     sizeof(addr4));
	}

	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: sensorValue\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json\r\n",
			NULL
		};
		printk("Headers successfully created\n\n");		
		req.method = HTTP_GET;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		//double check this section
		printk("CIN create response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 GET");
		char* conString = "con\": \"";
		char* substring = strstr(recv_buf_ipv4,  conString);
		substring += 7;
		substring = strtok(substring, "\"");

		printk("Result for setting %s is: %s\n", CNTName, substring);

		if (strcmp(CNTName, "numAverages") == 0) {
			numAverages = atoi(substring);
		}else if (strcmp(CNTName, "transmitPeriod") == 0) {
			transmitPeriod = atoi(substring);
		} else if (strcmp(CNTName, "samplePeriod") == 0) {
			samplePeriod = atoi(substring);
		} else {
			printk("Attempting to set unknown setting\n");
		}
	
		close(sock4);
	} else {
		printk("Cannot open socket/send CNT command \r\n");
	}
	return;
}

/* Function for receiving the values from the Temperature and Humidity sensor */
static void environmental_data_get(double *temp, double *hum)
{
		int err;

		/* Request data from external sensors. */
		err = ext_sensors_temperature_get(temp);
		if (err) {
			LOG_ERR("temperature_get, error: %d", err);
		}

		err = ext_sensors_humidity_get(hum);
		if (err) {
			LOG_ERR("humidity_get, error: %d", err);
		}
}

/* Needed for sensor read function, case of if it isn't broke don't fix it... */
static void ext_sensor_handler(const struct ext_sensor_evt *const evt)
{
	int err = -1;//to get rid of error lines
	switch (evt->type) {
	case EXT_SENSOR_EVT_TEMPERATURE_ERROR:
		LOG_ERR("EXT_SENSOR_EVT_TEMPERATURE_ERROR %d", err);
		break;
	case EXT_SENSOR_EVT_HUMIDITY_ERROR:
		LOG_ERR("EXT_SENSOR_EVT_HUMIDITY_ERROR %d", err);
		break;
	default:
		break;
	}
}

/* function for printing status of GPS fix, shows number of tracking satellites
*/
static void print_satellite_stats(struct gps_pvt *pvt_data)
{
	uint8_t tracked = 0;
	uint32_t tracked_sats = 0;
	static uint32_t prev_tracked_sats;
	char print_buf[100];
	size_t print_buf_len;

	for (int i = 0; i < GPS_PVT_MAX_SV_COUNT; ++i) {
		if ((pvt_data->sv[i].sv > 0) &&
		    (pvt_data->sv[i].sv < 33)) {
			tracked++;
			tracked_sats |= BIT(pvt_data->sv[i].sv - 1);
		}
	}

	if ((tracked_sats == 0) || (tracked_sats == prev_tracked_sats)) {
		if (tracked_sats != prev_tracked_sats) {
			prev_tracked_sats = tracked_sats;
			LOG_DBG("Tracking no satellites %d", 0);
		}

		return;
	}

	prev_tracked_sats = tracked_sats;
	print_buf_len = snprintk(print_buf, sizeof(print_buf), "Tracking:  ");

	for (size_t i = 0; i < 32; i++) {
		if (tracked_sats & BIT(i)) {
			print_buf_len +=
				snprintk(&print_buf[print_buf_len - 1],
					 sizeof(print_buf) - print_buf_len,
					 "%d  ", i + 1);
			if (print_buf_len < 0) {
				LOG_ERR("Failed to print satellite stats %d", -1);
				break;
			}
		}
	}

	LOG_INF("%s", log_strdup(print_buf));
	LOG_DBG("Searching for %lld seconds",
		(k_uptime_get() - start_search_timestamp) / 1000);
}

/* Function to print out Fix data for GPS to terminal, and set needed global variables
*/
static void print_pvt_data(struct gps_pvt *pvt_data)
{
	char buf[300];
	size_t len;
	GPSLatitude = pvt_data->latitude;
	GPSLongitude = pvt_data->longitude;

	len = snprintf(buf, sizeof(buf),
		      "\r\n\tLongitude:  %f\r\n\t"
		      "Latitude:   %f\r\n\t"
		      "Altitude:   %f\r\n\t"
		      "Speed:      %f\r\n\t"
		      "Heading:    %f\r\n\t"
		      "Date:       %02u-%02u-%02u\r\n\t"
		      "Time (UTC): %02u:%02u:%02u\r\n",
		      pvt_data->longitude, pvt_data->latitude,
		      pvt_data->altitude, pvt_data->speed, pvt_data->heading,
		      pvt_data->datetime.year, pvt_data->datetime.month,
		      pvt_data->datetime.day, pvt_data->datetime.hour,
		      pvt_data->datetime.minute, pvt_data->datetime.seconds);
	if (len < 0) {
		LOG_ERR("Could not construct PVT print %d", -1);
	} else {
		LOG_INF("%s", log_strdup(buf));
	}
}

/* Function to handle GPS events such as fix being found
*/
static void gps_handler(const struct device *dev, struct gps_event *evt)
{
	ARG_UNUSED(dev);

	switch (evt->type) {
	case GPS_EVT_SEARCH_STARTED:
		LOG_INF("GPS_EVT_SEARCH_STARTED %s", "");
		start_search_timestamp = k_uptime_get();
		break;
	case GPS_EVT_SEARCH_STOPPED:
		LOG_INF("GPS_EVT_SEARCH_STOPPED %s", "");
		break;
	case GPS_EVT_SEARCH_TIMEOUT:
		LOG_INF("GPS_EVT_SEARCH_TIMEOUT %s", "");
		break;
	case GPS_EVT_OPERATION_BLOCKED:
		LOG_INF("GPS_EVT_OPERATION_BLOCKED%s", "");
		break;
	case GPS_EVT_OPERATION_UNBLOCKED:
		LOG_INF("GPS_EVT_OPERATION_UNBLOCKED%s", "");
		break;
	case GPS_EVT_AGPS_DATA_NEEDED:
		LOG_INF("GPS_EVT_AGPS_DATA_NEEDED%s", "");
#if defined(CONFIG_NRF_CLOUD_PGPS)
		LOG_INF("A-GPS request from modem; emask:0x%08X amask:0x%08X utc:%u "
			"klo:%u neq:%u tow:%u pos:%u int:%u",
			evt->agps_request.sv_mask_ephe, evt->agps_request.sv_mask_alm,
			evt->agps_request.utc, evt->agps_request.klobuchar,
			evt->agps_request.nequick, evt->agps_request.system_time_tow,
			evt->agps_request.position, evt->agps_request.integrity);
		memcpy(&agps_request, &evt->agps_request, sizeof(agps_request));
#endif
		
		break;
	case GPS_EVT_PVT:
		print_satellite_stats(&evt->pvt);
		break;
	case GPS_EVT_PVT_FIX:;
		fix_timestamp = k_uptime_get();

		LOG_INF("---------       FIX       ---------%s", "");
		LOG_INF("Time to fix: %d seconds",
			(uint32_t)(fix_timestamp - start_search_timestamp) / 1000);
		print_pvt_data(&evt->pvt);
		LOG_INF("-----------------------------------%s", "");
#if defined(CONFIG_NRF_CLOUD_PGPS) && defined(CONFIG_PGPS_STORE_LOCATION)
		nrf_cloud_pgps_set_location(evt->pvt.latitude, evt->pvt.longitude);
#endif
		break;
	case GPS_EVT_NMEA_FIX:
		break;
	default:
		break;
	}
}
/* Reboot thread, seems unused 
*/
static void reboot_work_fn(struct k_work *work)
{
	LOG_WRN("Rebooting in 2 seconds...%s", "");
	k_sleep(K_SECONDS(2));
	sys_reboot(0);
}

/* Function to configure and start GPS work
*/
static void gps_start_work_fn(struct k_work *work)
{
	int err;
	struct gps_config gps_cfg = {
		.nav_mode = GPS_NAV_MODE_CONTINUOUS,
		.power_mode = GPS_POWER_MODE_DISABLED,
		.timeout = 120,
		.interval = 240,
		.priority = false, //need to send values to ACME more than get fix
	};

	ARG_UNUSED(work);

#if defined(CONFIG_NRF_CLOUD_PGPS)
	static bool initialized;

	if (!initialized) {
		struct nrf_cloud_pgps_init_param param = {
			.event_handler = pgps_handler,
			.storage_base = PM_MCUBOOT_SECONDARY_ADDRESS,
			.storage_size = PM_MCUBOOT_SECONDARY_SIZE};

		err = nrf_cloud_pgps_init(&param);
		if (err) {
			LOG_ERR("Error from PGPS init: %d", err);
		} else {
			initialized = true;
		}
	}
#endif

	err = gps_start(gps_dev, &gps_cfg);
	if (err) {
		LOG_ERR("Failed to start GPS, error: %d", err);
		return;
	}

	LOG_INF("Continuous GPS search started %s", "");
}

/* Function to initialize GPS work thread
*/
static void work_init(void)
{
	k_work_init(&gps_start_work, gps_start_work_fn);
	k_work_init_delayable(&reboot_work, reboot_work_fn);
#if defined(CONFIG_NRF_CLOUD_PGPS)
	k_work_init(&manage_pgps_work, manage_pgps);
	k_work_init(&notify_pgps_work, notify_pgps);
#endif
}

/* Function to initialize needed libraries and functionalities
	including LTE, GPS, ADC, etc
 */ 
static int init_app(void)
{
	int err;
		/* Initialize temp/hum sensors on Thingy:91 */
	err = ext_sensors_init(ext_sensor_handler);
	if (err) {
		LOG_ERR("ext_sensors_init, error: %d", err);
		return err;
	}
	#if defined(CONFIG_LTE_POWER_SAVING_MODE)
		err = lte_lc_psm_req(true);
		if (err) {
			LOG_ERR("PSM request failed, error: %d", err);
			return err;
		}

		LOG_INF("PSM mode requested%s\n", "");
	#endif
	printk("Waiting for network.. \n");
	err = lte_lc_init_and_connect();
	if (err) {
		printk("Failed to connect to the LTE network, err %d\n", err);
		return err;
	}
	printk("OK\n");
	work_init();
	gps_dev = device_get_binding("NRF9160_GPS");
	if (gps_dev == NULL) {
		LOG_ERR("Could not get binding to nRF9160 GPS%s", "");
		return -1;
	}

	err = gps_init(gps_dev, gps_handler);
	if (err) {
		LOG_ERR("Could not initialize GPS, error: %d", err);
		return err;
	}
	if (SoilMoisture) {//only attempt to initalize ADC if using Soil Moisture sensor
		adc_dev = device_get_binding("ADC_0");
		if (!adc_dev) {
			printk("device_get_binding ADC_0 failed\n");
		}
		err = adc_channel_setup(adc_dev, &m_1st_channel_cfg);
		if (err) {
			printk("Error in adc setup: %d\n", err);
		}
	}
	return 0;
}

/* Main program that will create the oneM2M resources/instances, read sensor values, and send these to the cloud ACME
 */
void main(void)
{
	k_sleep(K_MSEC(5000));//Wait so it is easier to see start of program if using MCUboot
	printk("oneM2M Sensor and GPS sample started\n\r");
	if (init_app() != 0) {
		return;
	}
	printk("initialization successful\n");
	k_work_submit(&gps_start_work); // start GPS
	nrf_gpio_pin_pull_t res = NRF_GPIO_PIN_PULLDOWN; //Pulldown resistor to read Rainfall Trigger sensor correctly
	//turn on green led
	nrf_gpio_cfg_output(30);
	nrf_gpio_pin_write(30, 1);

	//Pin 0.16 corresponds to Test Point 33 (AIN3), while Pin 0.13 is TP 32 (AIN0)
	nrf_gpio_cfg_input(16, res);
	/* Trigger offset calibration
	 * As this generates a _DONE and _RESULT event
	 * the first result will be incorrect. 
	 */
	NRF_SAADC_NS->TASKS_CALIBRATEOFFSET = 1;
	
	/* Setting up the main application entity as well as containers for sensor values*/

	char* temp = retrieveAE(deviceName);
	char aeRI[20];
	char acpi[40];
	printk("\n comparing \"%s\" to empty \n", temp);
	if (strcmp(temp, "") == 0) {
		char temp1[20] = originator;
		char temp2[5] = "ACP";
		strcat(temp1, temp2);
		char* temp = createACP(cseName, temp1);
		strcpy(acpi, temp);
		char* ri = createAE(deviceName, acpi);
		strcpy(aeRI, ri);
		sys_reboot(0);
	} else {
		strcpy(aeRI, originator);
	}

	temp = retrieveCNT("Temperature", originator);
	char cntTemp[30];
	char acpString[20] = deviceName;
	char acpiCNT[40];
	char temp3[5] = "ACP";
	strcat(acpString, temp3);
	printk("\n comparing \"%s\" to empty \n", temp);
	if (strcmp(temp, "") == 0) {
		char* temp4 = createACP(aeRI, acpString);
		printk("acpi for CNT: %s\n", temp4);
		strcpy(acpiCNT, temp4);
		printk("acpi for CNT: %s\n", acpiCNT);
		temp = createCNT("Temperature", aeRI, 10, acpiCNT);
		strcpy(cntTemp, temp);
	} else {
		strcpy(cntTemp, temp);
	}

	char cntHum[30];
	temp = retrieveCNT("Humidity", originator);
	printk("\n comparing \"%s\" to empty \n", temp);
	if (strcmp(temp, "") == 0) {
		printk("%s", acpiCNT);
		printk("aeRI %s acpiCNT %s", aeRI, acpiCNT);
		temp = createCNT("Humidity", aeRI, 10, acpiCNT);
		strcpy(cntHum, temp);
	} else {
		strcpy(cntHum, temp);
	}

	char cntGPS[30]; 
	temp = retrieveCNT("GPS", originator);
	printk("\n comparing \"%s\" to empty \n", temp);
	if (strcmp(temp, "") == 0) {
		printk("aeRI %s acpiCNT %s", aeRI, acpiCNT);
		temp = createCNT("GPS", aeRI, 10, acpiCNT);
		strcpy(cntGPS, temp);
	} else {
		strcpy(cntGPS, temp);
	}

	char cntMST[30]; 
	if (SoilMoisture) {
		temp = retrieveCNT("SoilMoisture", originator);
		printk("\n comparing \"%s\" to empty \n", temp);
		if (strcmp(temp, "") == 0) {
			printk("aeRI %s acpiCNT %s", aeRI, acpiCNT);
			temp = createCNT("SoilMoisture", aeRI, 10, acpiCNT);
			strcpy(cntMST, temp);
		} else {
			strcpy(cntMST, temp);
		}
	}

	char cntRTRG[30]; 
	if (RainfallTrigger) {
		temp = retrieveCNT("RainfallTrigger", originator);
		printk("\n comparing \"%s\" to empty \n", temp);
		if (strcmp(temp, "") == 0) {
			printk("aeRI %s acpiCNT %s", aeRI, acpiCNT);
			temp = createCNT("RainfallTrigger", aeRI, 10, acpiCNT);
			strcpy(cntRTRG, temp);
		} else {
			strcpy(cntRTRG, temp);
		}
	}
	/* Start sending sensor values, main loop of program 
	   */
	printk("\n\noneM2M initialization complete, sending sensor values \n\n");
	while (true) {
		int err;
		//First ADC event after startup is junk, so toss this away
		if (SoilMoisture) {
			err = adc_sample();
		}
		char settings[9] = "Settings";
		printk("Retrieving Settings CNT\n");
		temp = retrieveCNT(settings, originator);
		printk("\n comparing \"%s\" to empty \n", temp);
		/* Get settings if they exist, otherwise upload and use the defaults */
		if (strcmp(temp, "") != 0) {
			printk("Found existing settings on ACME...\n");
			char cntriSettings[50] = deviceName;
			strcat(cntriSettings, "/");
			strcat(cntriSettings, settings);
			retrieveCIN(cntriSettings, "numAverages");
			retrieveCIN(cntriSettings, "transmitPeriod");
			retrieveCIN(cntriSettings, "samplePeriod");
		} else {
			printk("No settings found, creating them\n");
			temp = createCNT(settings, aeRI, 10, acpiCNT);
			char cntriSettings[50];
			strcpy(cntriSettings, temp);
			temp = createCNT("numAverages", cntriSettings, 1, acpiCNT);
			char numAveCNT[40];
			strcpy(numAveCNT, temp);
			char numAve[5];
			sprintf(numAve, "%d", numAverages);
			char numAvelabel[50] = deviceName;
			strcat(numAvelabel, "/numAverages");
			createCIN(numAveCNT, numAve, numAvelabel);

			temp = createCNT("transmitPeriod", cntriSettings, 1, acpiCNT);
			char txPeriodCNT[40];
			strcpy(txPeriodCNT, temp);
			char txPeriod[15];
			sprintf(txPeriod, "%d", transmitPeriod);
			char txPerLbl[50] = deviceName;
			strcat(txPerLbl, "/transmitPeriod");
			createCIN(txPeriodCNT, txPeriod, txPerLbl);

			temp = createCNT("samplePeriod", cntriSettings, 1, acpiCNT);
			char saPeriodCNT[40];
			strcpy(saPeriodCNT, temp);
			char saPeriod[15];
			sprintf(saPeriod, "%d", samplePeriod);
			char saPerLbl[50] = deviceName;
			strcat(saPerLbl, "/samplePeriod");
			createCIN(saPeriodCNT, saPeriod, saPerLbl);
		}
		/* This portion is sensor values */

			for (int i = 0; i < numAverages; i++) {
				double temp; double hum;
				environmental_data_get(&temp, &hum);
				printk("\ntemp: %lf hum %lf\n", temp, hum);
				if (SoilMoisture) {
					err = adc_sample();
					if (err < 0) {
						printk("error with ADC sample: %d\n", err);
						break;
					}
					printk("Soil Moisture: %f\n", moisture);
					MoistureValues[i] = moisture;
				}
				TempValues[i] = temp;
				HumValues[i] = hum;
				k_sleep(K_SECONDS(samplePeriod));
			}
			double Temp = 0.0;
			double Hum = 0.0;
			double Moisture = 0.0;
			for (int i=0; i<numAverages; i++) {
				Temp += TempValues[i%100];
				Hum +=  HumValues[i%100];
				if (SoilMoisture) {
					Moisture = MoistureValues[i%100];
				}
			}
			int div;
			if (numAverages >=100) {
				div = 100;
			} else {
				div = numAverages;
			}
			Temp = Temp / div;
			Hum = Hum / div;
			if (SoilMoisture) {
				Moisture = Moisture / div;
			}

			//Only need to sample latest Rainfall Trigger, no need to sample multiple times
			if (RainfallTrigger) {
				int val = nrf_gpio_pin_read(16);// 1 is no rain trigger, 0 is rain trigger
				char* rainfall;
				if (val == 1) {
					rainfall = "OFF";
				} else {
					rainfall = "ON";
				}
				printk("rainfall: %s\n", rainfall);
				char RTlabel[50] = deviceName;
				strcat(RTlabel, "/RainfallTrigger");
				err = createCIN(cntRTRG, rainfall, RTlabel);
				if (err != 0) {
					printk("error with Rainfall Trigger CIN: %d\n", err);
					break; 
				}
			}


			char tempString[12];
			char humString[12];
			sprintf(tempString, "%f", Temp);
			sprintf(humString, "%f", Hum);
			char Tlabel[50] = deviceName;
			strcat(Tlabel, "/Temperature");
			err = createCIN(cntTemp, tempString, Tlabel);
			if (err != 0) {
				printk("error with Temp CIN: %d\n", err);
				break; 
			}
			char Hlabel[50] = deviceName;
			strcat(Hlabel, "/Humidity");
			err = createCIN(cntHum, humString, Hlabel);
			if (err != 0) {
				printk("error with Hum CIN: %d\n", err);
				break; 
			}

			if (SoilMoisture) {
				char moistureString[12];
				sprintf(moistureString, "%f", Moisture);
				char SMlabel[50] = deviceName;
				strcat(SMlabel, "/SoilMoisture");
				err = createCIN(cntMST, moistureString, SMlabel);
				if (err != 0) {
					printk("error with Soil Moisture CIN: %d\n", err);
					break; 
				}
			}

			char GPSCoords[30];
			sprintf(GPSCoords, "%f,%f", GPSLatitude, GPSLongitude);
			char GPSlabel[50] = deviceName;
			strcat(GPSlabel, "/GPS");
			err = createCIN(cntGPS, GPSCoords, GPSlabel);
			if (err != 0) {
				printk("error with GPS CIN: %d\n", err);
				break; 
			}
			printk("\n\nDone sending Data, GPS now working in background while waiting\n\n");

	 	k_sleep(K_SECONDS(transmitPeriod-numAverages*samplePeriod)); // wait correct amount of time, since sampling sensors variable times
	}
}
