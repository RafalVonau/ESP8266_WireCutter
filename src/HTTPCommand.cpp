/*
 * HTTPCommand - implements a web page and allows to execute commands from a web browser.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "HTTPCommand.h"
#include "www_fs.h"

extern volatile int catCounter;

//====================================================================================
//=============================-- NETWORK EVENTS--====================================
//====================================================================================

static void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}
//====================================================================================

static void handle_request(AsyncWebServerRequest *request, char *type, const uint8_t * data, int len)
{
	AsyncWebServerResponse *response = request->beginResponse_P(200, type, data, len);
	response->addHeader("Content-Encoding", "gzip");
	request->send(response);
}
//====================================================================================

HTTPCommand::HTTPCommand(CommandDB *db): Command(db)
{
	m_server = new AsyncWebServer(80);
	m_events = new AsyncEventSource("/events");

	m_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){handle_request(request, "text/html", __index_html, www_index_html_size);});
	/* Java Script */
	m_server->on("/main.js", HTTP_GET, [](AsyncWebServerRequest *request){handle_request(request, "text/javascript", __main_js, www_main_js_size);});
	/* CSS */
	m_server->on("/main.css", HTTP_GET, [](AsyncWebServerRequest *request){handle_request(request, "text/css", __main_css, www_main_css_size);});
	/* Manifest */
	m_server->on("/manifest-icon-192.maskable.png", HTTP_GET, [](AsyncWebServerRequest *request){handle_request(request, "image/png", __manifest_icon_192_maskable_png, www_manifest_icon_192_maskable_png_size);});
	m_server->on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *request){handle_request(request, "application/manifest+json", __manifest_json, www_manifest_json_size);});

	m_server->on("/post", HTTP_POST, [this](AsyncWebServerRequest *request) {
		String message;
		if (request->hasParam("cmd", true)) {
			message = request->getParam("cmd", true)->value() + "\r";
			handleData(message.c_str(), message.length());
		} else {
			message = "No message sent";
		}
		request->send(200, "text/plain", message);
	});

	m_server->on("/cnt", HTTP_GET, [](AsyncWebServerRequest *request){
		String message = "{\"cnt\": "+String(catCounter) + " }";
		request->send(200, "application/json", message);
	});


	m_server->onNotFound(notFound);

	m_events->onConnect([](AsyncEventSourceClient *client) {client->send("hello!",NULL,millis(),1000);});
	m_server->addHandler(m_events);
	m_server->begin();
}
//====================================================================================

HTTPCommand::~HTTPCommand() 
{
	delete m_events;
	delete m_server;
}
//====================================================================================

void HTTPCommand::handleData(const char *data, int n)
{
	int i;
	
	for (i=0; i < n; ++i) {
		char inChar = data[i];
		if ((inChar == '\r') || (inChar == '\n')) {
			if (bufPos) {
				buffer[bufPos] = '\0';
				m_db->executeCommand(this, buffer);
				clearBuffer();
			}
		} else if (isprint(inChar)) {     // Only printable characters into the buffer
			if (bufPos < COMMAND_BUFFER) {
				buffer[bufPos++] = inChar;  // Put character into buffer
			}
		}
	}
}
//====================================================================================
