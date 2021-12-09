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
LOG_MODULE_REGISTER(net_http_client_sample, LOG_LEVEL_INF);
#include <net/net_ip.h>
#include <net/http_client.h>
#include <regex.h>
#include <sys/printk.h>
#include <hal/nrf_gpio.h>
#define INPUT NRF_GPIO_PIN_MAP(6, 1)
#include <drivers/uart.h>
#include <net/socket.h>
#include <nrf9160.h>
#include <adp536x.h>
#define ADP536X_I2C_DEV_NAME DT_LABEL(DT_NODELABEL(i2c2))

/* HTTP and oneM2M settings */
// Port to connect to
#define HTTPS_PORT 8080
// length of HTTP respones buffer
#define MAX_RECV_BUF_LEN 1024
// HTTP server address to connect to
#define SERVER_ADDR4 "DEFINE THIS"
// Variable to know if Thingy:91 is being used
// Set to false if using the nrf9160dk
#define Thingy91 true
// CSE ID of the ACME in use
#define cseID "/id-in"
// CSE Name of the ACME in use
#define cseName "cse-in"
// Device name - change this if using with multiple devices running this code
#define deviceName "actuator_AR"
// originator - should just be 'C'+deviceName by convention
#define originator "Cactuator_AR"
/* end HTTP and oneM2M settings */

/* buffer to store HTTP request response in*/
static uint8_t recv_buf_ipv4[1024];

/* sets up a socket to an HTTP addr:port/url
	This is taken from the zephyr http_client examples */
static int setup_socket(sa_family_t family, const char *server, int port,
						int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
	int ret = 0;

	memset(addr, 0, addr_len);

	if (family == AF_INET)
	{
		net_sin(addr)->sin_family = AF_INET;
		net_sin(addr)->sin_port = htons(port);
		inet_pton(family, server, &net_sin(addr)->sin_addr);
	}
	else
	{
		net_sin6(addr)->sin6_family = AF_INET6;
		net_sin6(addr)->sin6_port = htons(port);
		inet_pton(family, server, &net_sin6(addr)->sin6_addr);
	}

	*sock = socket(family, SOCK_STREAM, IPPROTO_TCP);

	if (*sock < 0)
	{
		LOG_ERR("Failed to create %s HTTP socket (%d)", family_str,
				-errno);
	}

	return ret;
}

/* Gets and stores the respones from an HTTP request
	This is taken from the zephyr http_client examples */
static void response_cb(struct http_response *rsp,
						enum http_final_call final_data,
						void *user_data)
{
	if (final_data == HTTP_DATA_MORE)
	{
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	}
	else if (final_data == HTTP_DATA_FINAL)
	{
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

/* connects a socket to an HTTP addr:port/url
	This is taken from the zephyr http_client examples */
static int connect_socket(sa_family_t family, const char *server, int port,
						  int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(family, server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0)
	{
		return -1;
	}

	ret = connect(*sock, addr, addr_len);
	if (ret < 0)
	{
		LOG_ERR("Cannot connect to %s remote (%d)",
				family == AF_INET ? "IPv4" : "IPv6",
				-errno);
		ret = -errno;
	}

	return ret;
}

/* Initialize AT communications */
int at_comms_init(void)
{
	int err;

	err = at_cmd_init();
	if (err)
	{
		printk("Failed to initialize AT commands, err %d\n", err);
		return err;
	}

	err = at_notif_init();
	if (err)
	{
		printk("Failed to initialize AT notifications, err %d\n", err);
		return err;
	}
	printk("Initialized AT notifications\n");
	return 0;
}

// creating a polling channel to emulate notifications
char *createPCH(char *parentID)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *substring;
	char *result = "worked";

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

		char url[40] = {0};

		sprintf(url, "/cse-in/%s", parentID);
		printk("url created sucessfully: %s\n", url);
		char origin[30];
		// sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=15\r\n",
			NULL};
		/*
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=23\r\n",
			NULL};
		*/
		printk("headers: %s", *headers);
		char payload[80] = {0};
		sprintf(payload, "{ \"m2m:pch\": {\"rn\":\"ThingyPCH\"}}");
		// sprintf(payload, "{ \"m2m:ae\": {\"rr\": true, \"rn\": \"%s\", \"acpi\": [\"%s\"], \"api\": \"NR_AE001\", \"apn\": \"IOTApp\", \"lbl\": [ \"Thingy91\",\"actuator\" ], \"csz\": [\"application/json\"], \"srv\": [\"2a\"]} }", resourceName, acpi);
		printk("PCH Payload: %s\r\n", payload);
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
		/*
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
		*/
		// printk("post completing req sucessfully\n");

		ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
		printk("PCH create response:\n");
		printk("%d", ret);
		printk("Post actual ret");
		printk("Response Dump: %s\r", recv_buf_ipv4);
		printk(" sucessfully\n");
		return (result);
		/*
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
		*/
	}
	return (result);
}
int retrievePCH(char *resourceName)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 25 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *substring;
	char *result;
	double returnVal = 0;
	int returnInt = 0;

	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	// change type in url
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		char url[100];
		sprintf(url, "http://%s:%d/cse-in/%s/ThingyPCH/pcu", SERVER_ADDR4, HTTPS_PORT, resourceName);
		// sprintf(url, "https://%s:%d%s?fu=1&ty=3&lbl=sensorVal", SERVER_ADDR4, HTTPS_PORT, cseID);
		printk("Query url: %s\n", url);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Accept: application/json\r\n",
			NULL};
		req.method = HTTP_GET;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("PCH retrieve response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 GET");
		printk("Response Dump: %s\r", recv_buf_ipv4);

		// printk("testing valid response, find []\n");
		printk("finding content\n");
		char *postString;
		substring = strstr(recv_buf_ipv4, "\"con\": ");
		printk("Got past strstr");
		if (substring != NULL)
		{
			substring += 8;
			returnVal = strtod(substring, &postString);
			returnInt = (int)returnVal;
			/*substring = strtok(substring, ",");
			substring += 8;
			result = substring;
			result = strtok(result, "\"");
			printk("Result of retrieve:%s\n", result);
			returnVal = strtod(result, &postString);
			printk("Conversion Successful\n");*/
		}
		else
		{
			printk("No matching AE found");
			substring = "";
			returnInt = -1;
		}
		close(sock4);
	}
	return returnInt;
}
// for retrieving content instances
int retrieveCIN(char *resourceName, char *cntName)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *substring;
	char *result;
	double returnVal = 0;
	int returnInt = 0;

	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	// change type in url
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		char url[100];
		printk("\nRight before creating URL\n");
		sprintf(url, "http://%s:%d/cse-in/%s/%s/la", SERVER_ADDR4, HTTPS_PORT, resourceName, cntName);
		// sprintf(url, "https://%s:%d%s?fu=1&ty=3&lbl=sensorVal", SERVER_ADDR4, HTTPS_PORT, cseID);
		printk("Query url: %s\n", url);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Accept: application/json\r\n",
			NULL};
		req.method = HTTP_GET;
		req.url = url;
		req.host = SERVER_ADDR4;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.header_fields = headers;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

		printk("CIN retrieve response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 GET");
		printk("Response Dump: %s\r", recv_buf_ipv4);

		// printk("testing valid response, find []\n");
		printk("finding content\n");
		char *postString;
		substring = strstr(recv_buf_ipv4, "\"con\": ");
		printk("Got past strstr");
		if (substring != NULL)
		{
			substring += 8;
			returnVal = strtod(substring, &postString);
			returnInt = (int)returnVal;
			/*substring = strtok(substring, ",");
			substring += 8;
			result = substring;
			result = strtok(result, "\"");
			printk("Result of retrieve:%s\n", result);
			returnVal = strtod(result, &postString);
			printk("Conversion Successful\n");*/
		}
		else
		{
			printk("No matching AE found");
			substring = "";
			returnInt = 0;
		}
		close(sock4);
	}
	return returnInt;
}

char *createAE(char *resourceName, char *acpi)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *substring;

	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	substring = "";
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
			"Content-Type: application/json;ty=2\r\n",
			NULL};
		char payload[225] = {0};
		sprintf(payload, "{ \"m2m:ae\": {\"rr\": false, \"rn\": \"%s\", \"acpi\": [\"%s\"], \"api\": \"NR_AE001\", \"apn\": \"IOTApp\", \"lbl\": [ \"actuator\" ], \"csz\": [\"application/json\"], \"srv\": [\"2a\"]} }", resourceName, acpi);
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
		printk("Response Dump: %s\r", recv_buf_ipv4);

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
		printk("Cannot open socket/send AE command \r\n");
		substring = "";
	}
	return substring;
}

/* Checks if an application entity exists, this is used as the overall device that holds containers for each sensor
	Returns an RI that is required for containers to access the AE
 */
char *retrieveAE(char *resourceName)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *substring;

	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	substring = "";
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
		struct http_request req;

		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		char url[45];
		sprintf(url, "/cse-in?fu=1&ty=2&rn=%s&drt=2", resourceName);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Accept: application/json\r\n",
			NULL};
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
		printk("Response Dump: %s\r", recv_buf_ipv4);

		printk("finding %s\n", resourceName);
		substring = strstr(recv_buf_ipv4, resourceName);
		if (substring != NULL)
		{
			char *result = substring;
			result[strlen(result) - 1] = 0;
			result[strlen(result) - 1] = 0;
			result[strlen(result) - 1] = 0;
			substring = result;
			printk("Result of retrieve:%s\n", substring);
		}
		else
		{
			printk("No matching AE found");
			substring = "";
		}
		close(sock4);
	}
	else
	{
		printk("Cannot open socket/send AE command \r\n");
		substring = "";
	}
	return substring;
}

/* Creates Acess Control Policy that governs what can access the AE/CNT's of the device
 */
char *createACP(char *parentID, char *acpi)
{
	printk("Creating ACP");
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 20 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
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
		char payload[300] = {0};
		sprintf(payload, "{ \"m2m:acp\": { \"rn\": \"%s\", \"pv\": { \"acr\": [{\"acor\": [\"%s\"], \"acop\": 63}, {\"acor\": [\"CdashApp\"], \"acop\": 63}, {\"acor\": [\"CaddNumInput\"], \"acop\": 63}, {\"acor\": [\"CdataProcessing\"], \"acop\": 63}] }, \"pvs\": {\"acr\": [{\"acor\": [\"%s\"], \"acop\": 63}]}}}", acpi, originator, originator);
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
/* creating subscriptiong
 */
char *createSUB(char *resourceName, char *parentID, int mni)
{
	printk("Creating Subscription for %s\n", resourceName);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char url[40];
	sprintf(url, "/cse-in/%s/%s", parentID, resourceName);
	printk("url: %s", url);
	char *substring = "";
	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
		struct http_request req;
		printk("SUB Socket Connected\n");
		memset(&req, 0, sizeof(req));
		char origin[30];
		sprintf(origin, "X-M2M-Origin: %s\r\n", originator);
		const char *headers[] = {
			"X-M2M-RI: xyz1\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=23\r\n",
			NULL};
		char payload[150] = {0};
		sprintf(payload, "{ \"m2m:sub\": {\"rn\": \"%s\", \"enc\": {\"net\":[3]}, \"nu\":[\"%s\"]} }", resourceName, originator);
		printk("SUB Create Payload: %s\r\n", payload);
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

		printk("SUB create response:\n");
		ret = http_client_req(sock4, &req, timeout, "IPv4 POST");
		printk("Response dump: %s\n", recv_buf_ipv4);
		char *riString = "\"ri\":";
		/*
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
		close(sock4);*/
	}
	else
	{
		printk("Cannot open socket/send SUB command \r\n");
	}
	return substring;
}

/* creates a container for the AE, this is used to seperate sensors and store content instances
	Note that mni is required and not optional to provide, usually 10 for sensors and 1 for subscriptions.
	Returns an RI that is required for content instances to access the CNT
*/
char *createCNT(char *resourceName, char *parentID, int mni, char *acpi)
{
	printk("Creating container called %s\n", resourceName);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char url[30];
	sprintf(url, "/%s", parentID);
	char *substring = "";
	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
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
			NULL};
		printk("Headers: %s \n", headers);
		char payload[150] = {0};
		sprintf(payload, "{ \"m2m:cnt\": {\"rn\": \"%s\", \"acpi\": [\"%s\"], \"lbl\": [ \"actVal\" ], \"mni\": %d} }", resourceName, acpi, mni);
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
		printk("Response dump: %s\n", recv_buf_ipv4);
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

/* Function to initialize needed libraries and functionalities
	TODO: add GPS functionality - seems to interfere with LTE init */
static int init_app(void)
{
	int err;
	err = nrf_modem_lib_init(NORMAL_MODE);
	if (err)
	{
		printk("Failed to initialize modem library!");
		return err;
	}
	/* Initialize AT comms in order to set up LTE */
	err = at_comms_init();
	if (err)
	{
		return err;
	}
	/* Initialize temp/hum sensors on Thingy:91 */
	// if (setup_modem() != 0) {
	// 	printk("Failed to initialize modem\n");
	// 	return -1;
	// }

	// /* Initialize and configure GNSS */
	// if (nrf_modem_gnss_init() != 0) {
	// 	printk("Failed to initialize GNSS interface\n");
	// 	return -1;
	// }

	// if (nrf_modem_gnss_nmea_mask_set(NRF_MODEM_GNSS_NMEA_RMC_MASK |
	// 				 NRF_MODEM_GNSS_NMEA_GGA_MASK |
	// 				 NRF_MODEM_GNSS_NMEA_GLL_MASK |
	// 				 NRF_MODEM_GNSS_NMEA_GSA_MASK |
	// 				 NRF_MODEM_GNSS_NMEA_GSV_MASK) != 0) {
	// 	printk("Failed to set GNSS NMEA mask\n");
	// 	return -1;
	// }

	// if (nrf_modem_gnss_fix_retry_set(0) != 0) {
	// 	printk("Failed to set GNSS fix retry\n");
	// 	return -1;
	// }

	// if (nrf_modem_gnss_fix_interval_set(1) != 0) {
	// 	printk("Failed to set GNSS fix interval\n");
	// 	return -1;
	// }

	// if (nrf_modem_gnss_start() != 0) {
	// 	printk("Failed to start GNSS\n");
	// 	return -1;
	// }
	printk("Waiting for network.. ");
	err = lte_lc_init_and_connect();
	if (err)
	{
		printk("Failed to connect to the LTE network, err %d\n", err);
		return err;
	}
	printk("OK\n");
	return 0;
}

int createstrCIN(char *parentID, char *content, char *label)
{
	printk("Sending CIN with parentID: %s\n", parentID);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char url[40] = {0};
	sprintf(url, "/%s", parentID);
	printk("url created sucessfully\n");

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
			"X-M2M-RI: sensorValue\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=4\r\n",
			NULL};
		printk("Headers successfully created\n\n Content is %s with size %zu", content, sizeof(content));
		char payload[180] = {0};
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
	}
	else
	{
		printk("Cannot open socket/send CIN command \r\n");
	}
	return 0;
}
/* creates a content instance for the AE, this is used to send sensor values to their respective containers
	Assumes content is a double - as temp/hum sensors return doubles
 */
int createCIN(char *parentID, int content, char *label)
{
	printk("Sending CIN with parentID: %s\n", parentID);
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char url[40] = {0};
	sprintf(url, "/%s", parentID);
	printk("url created sucessfully\n");

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
			"X-M2M-RI: sensorValue\r\n",
			origin,
			"X-M2M-RVI: 2a\r\n",
			"Content-Type: application/json;ty=4\r\n",
			NULL};
		printk("Headers successfully created\n\n Content is %d with size %zu", content, sizeof(content));
		char payload[180] = {0};
		sprintf(payload, "{ \"m2m:cin\": {\"cnf\": \"application/text:0\", \"lbl\": [\"%s\"],\"con\": \"%d\"} }", label, content);
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
	}
	else
	{
		printk("Cannot open socket/send CIN command \r\n");
	}
	return 0;
}

char *retrieveCNT(char *resourceName, char *parentID)
{
	struct sockaddr_in addr4;
	int sock4 = -1;
	int32_t timeout = 10 * MSEC_PER_SEC;
	int ret = 0;
	int port = HTTPS_PORT;
	char *substring;
	memset(recv_buf_ipv4, 0, sizeof(recv_buf_ipv4));
	char *result;

	if (IS_ENABLED(CONFIG_NET_IPV4))
	{
		(void)connect_socket(AF_INET, SERVER_ADDR4, port,
							 &sock4, (struct sockaddr *)&addr4,
							 sizeof(addr4));
	}

	// if (sock4 < 0) {
	// 	LOG_ERR("Cannot create HTTP connection.");
	// 	return -ECONNABORTED;
	// }
	// change type in url
	substring = "";
	if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4))
	{
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
			NULL};
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
		printk("Response Dump: %s\r", recv_buf_ipv4);
		printk("finding cnt\n");
		substring = strstr(recv_buf_ipv4, "cnt");
		if (substring != NULL)
		{
			result = substring;
			result[strlen(result) - 1] = 0;
			result[strlen(result) - 1] = 0;
			result[strlen(result) - 1] = 0;
			printk("Result of retrieve:%s\n", result);
		}
		else
		{
			printk("No matching CNT found");
			substring = "";
		}
		close(sock4);
	}
	else
	{
		printk("Cannot open socket/send CNT command \r\n");
		substring = "";
	}
	return result;
}
void updateBatteryState(char *batCNT)
{
	int ret;
	int err;
	char battery[32] = {0};
	char *bat;
	ret = at_cmd_write("AT%XVBAT", battery, sizeof(battery), NULL);
	if (ret != 0)
	{
		printk("failed to read battery %d\n", ret);
	}
	else
	{
		bat = strstr(battery, " ");
		bat++;
		int batteryRaw = atoi(bat);
		float batPercent = batteryRaw / 4500.0 * 100;
		printk("Battery Percentage: %f%%\n", batPercent);

		char batString[12];
		sprintf(batString, "%f", batPercent);
		printk("%s", batString);
		char batLabel[50] = deviceName;
		strcat(batLabel, "/Info/Battery");
		err = createstrCIN(batCNT, batString, batLabel);
		if (err != 0)
		{
			printk("error with Battery CIN: %d\n", err);
		}
	}
}
void main(void)
{
	if (Thingy91)
	{
		k_sleep(K_MSEC(5000)); // for testing, since Thingy:91 does not have soft reset switch it is easier
	}
	printk("HTTPS client sample started\n\r");
	if (init_app() != 0)
	{
		return;
	}
	printk("initialization successful\n");
	/* Setting up the main application entity as well as containers for actuator values*/
    printk("%s\n\n", deviceName);
	char *temp = retrieveAE(deviceName);
	char aeRI[20];
	char acpi[40];
	char lbl[40];
	char cntInfo[30];
	char cntBat[30];
	printk("\n comparing \"%s\" to empty \n", temp);
	if (strcmp(temp, "") == 0)
	{
		char temp1[20] = originator;
		char temp2[5] = "ACP";
		strcat(temp1, temp2);
		char *temp = createACP(cseName, temp1);
		strcpy(acpi, temp);
		char *ri = createAE(deviceName, acpi);
		strcpy(aeRI, ri);
		char acpString[20] = deviceName;
		char acpiCNT[40];
		char temp3[5] = "ACP";
		strcat(acpString, temp3);
		char *temp4 = createACP(aeRI, acpString);
		printk("acpi for CNT: %s\n", temp4);
		strcpy(acpiCNT, temp4);
		temp = createCNT("actuatorState", aeRI, 1, acpiCNT);
		temp = createCNT("requestedState", aeRI, 1, acpiCNT);
		temp = createSUB("requestedState", deviceName, 10);
		temp = createPCH(deviceName);
		printk("here");
		temp = createCNT("Info", aeRI, 10, acpiCNT);
		printk("here");
		strcpy(cntInfo, temp);
		printk("%s", cntInfo);
		temp = createCNT("Battery", cntInfo, 10, acpiCNT);
		printk("here");
		strcpy(cntBat, temp);
		updateBatteryState(cntBat);
	}
	else
	{
		strcpy(aeRI, originator);
	}
	memset(lbl, 0, sizeof(lbl));
	strcpy(lbl, deviceName);
	strcat(lbl, "/actuatorState");
	printk("%s", lbl);
	int err = 0;
	char actCNT[30];
	temp = retrieveCNT("actuatorState", originator);
	strcpy(actCNT, temp);
	temp = retrieveCNT("Info", originator);
	strcpy(cntInfo, temp);
	temp = retrieveCNT("Battery", cntInfo);
	strcpy(cntBat, temp);
	printk("%s", actCNT);
	printk("Battery: %s", cntBat);
	err = createCIN(actCNT, 0, lbl);
	while (true)
	{
		printk("\n\n\n\nRetrieving CIN \n\n\n\n\n\n");
		int testCalc = retrievePCH(deviceName);
		printk("Returned testCalc: %d\n", testCalc);
		int valveState = testCalc % 10;
		printk("ValveState is: %d\n", valveState);
		printk("New Actuator state is: %d\n", testCalc);
		if (valveState == 1)
		{
			nrf_gpio_cfg_output(13);
			nrf_gpio_pin_write(13, 1); // sets Test Point 32 high as expected
			err = createCIN(actCNT, valveState, lbl);
			updateBatteryState(cntBat);
		}
		if (valveState == 0)
		{
			nrf_gpio_cfg_output(13);
			nrf_gpio_pin_write(13, 0); // sets Test Point 32 high as expected
			err = createCIN(actCNT, valveState, lbl);
			updateBatteryState(cntBat);
		}
		if (valveState == -1)
		{
			printk("\n\n\n\nPolling Channel timeout so no change\n\n\n\n");
		}
		if (err != 0)
		{
			printk("error with Temp CIN: %d", err);
		}
	}
}
