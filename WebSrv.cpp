#include "ESPWebDAV.h"
#include "logger.h"

// Sections are copied from ESP8266Webserver

// ------------------------
String ESPWebDAV::getMimeType(String path) {
// ------------------------
	if(path.endsWith(".html")) return "text/html";
	else if(path.endsWith(".htm")) return "text/html";
	else if(path.endsWith(".css")) return "text/css";
	else if(path.endsWith(".txt")) return "text/plain";
	else if(path.endsWith(".js")) return "application/javascript";
	else if(path.endsWith(".json")) return "application/json";
	else if(path.endsWith(".png")) return "image/png";
	else if(path.endsWith(".gif")) return "image/gif";
	else if(path.endsWith(".jpg")) return "image/jpeg";
	else if(path.endsWith(".ico")) return "image/x-icon";
	else if(path.endsWith(".svg")) return "image/svg+xml";
	else if(path.endsWith(".ttf")) return "application/x-font-ttf";
	else if(path.endsWith(".otf")) return "application/x-font-opentype";
	else if(path.endsWith(".woff")) return "application/font-woff";
	else if(path.endsWith(".woff2")) return "application/font-woff2";
	else if(path.endsWith(".eot")) return "application/vnd.ms-fontobject";
	else if(path.endsWith(".sfnt")) return "application/font-sfnt";
	else if(path.endsWith(".xml")) return "text/xml";
	else if(path.endsWith(".pdf")) return "application/pdf";
	else if(path.endsWith(".zip")) return "application/zip";
	else if(path.endsWith(".gz")) return "application/x-gzip";
	else if(path.endsWith(".appcache")) return "text/cache-manifest";

	return "application/octet-stream";
}




// ------------------------
String ESPWebDAV::urlDecode(const String& text)	{
// ------------------------
	String decoded = "";
	char temp[] = "0x00";
	unsigned int len = text.length();
	unsigned int i = 0;
	while (i < len)	{
		char decodedChar;
		char encodedChar = text.charAt(i++);
		if ((encodedChar == '%') && (i + 1 < len))	{
			temp[2] = text.charAt(i++);
			temp[3] = text.charAt(i++);
			decodedChar = strtol(temp, NULL, 16);
		}
		else {
			if (encodedChar == '+')
				decodedChar = ' ';
			else
				decodedChar = encodedChar;  // normal ascii char
		}
		decoded += decodedChar;
	}
	return decoded;
}





// ------------------------
String ESPWebDAV::urlToUri(String url)	{
// ------------------------
	if(url.startsWith("http://"))	{
		int uriStart = url.indexOf('/', 7);
		return url.substring(uriStart);
	}
	else
		return url;
}



// ------------------------
bool ESPWebDAV::isClientWaiting() {
// ------------------------
#if HTTPS_SUPPORTED
	if(securePending) return true;
#endif
	return server && server->hasClient();
}

// ------------------------
bool ESPWebDAV::httpsOn() {
// ------------------------
#if HTTPS_SUPPORTED
	return secureServer != nullptr;
#else
	return false;
#endif
}

#if HTTPS_SUPPORTED
// ------------------------
void ESPWebDAV::setSecureServer(BearSSL::WiFiServerSecure* s) {
// ------------------------
	if(!s && securePending) { secureClient.stop(); securePending = false; }
	secureServer = s;
}
#endif

// ------------------------
// Do the TLS handshake for a waiting https client. It takes about a second, so it's done
// before the SD card is taken from the printer. The request itself is served by handleClient().
void ESPWebDAV::acceptSecure() {
// ------------------------
#if HTTPS_SUPPORTED
	if(!secureServer || securePending || !secureServer->hasClient())
		return;
	// a TLS connection needs about 17 KB in one piece plus about 10 KB more
	if(ESP.getMaxFreeBlockSize() < 17500 || ESP.getFreeHeap() < 28000)	{
		WiFiClient c = secureServer->WiFiServer::accept();
		c.stop();
		static unsigned long lastLog = 0;
		if(!lastLog || millis() - lastLog > 60000UL)	{
			lastLog = millis() | 1;
			logger.web(LOG_SPARSE, "HTTPS connection refused: not enough memory (free %u, largest block %u)", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
		}
		return;
	}
	secureClient = secureServer->accept();
	if(secureClient.connected())
		securePending = true;
	else	{
		secureClient.stop();
		logger.web(LOG_NORMAL, "HTTPS handshake failed (browser doesn't trust the certificate yet?)");
	}
#endif
}




// ------------------------
void ESPWebDAV::handleClient(String blank) {
// ------------------------
	processClient(&ESPWebDAV::handleRequest, blank);
}



// ------------------------
void ESPWebDAV::rejectClient(String rejectMessage) {
// ------------------------
	processClient(&ESPWebDAV::handleReject, rejectMessage);
}



// ------------------------
void ESPWebDAV::processClient(THandlerFunction handler, String message) {
// ------------------------
	// Take the waiting connection: an https one (already through its handshake) first
	isSecure = false;
#if HTTPS_SUPPORTED
	if(securePending)	{
		securePending = false;
		client = &secureClient;
		isSecure = true;
	}
	else
#endif
	{
		if(!server) return;
#if HTTPS_SUPPORTED
		plainClient = server->accept();     // core 3.x name
#else
		plainClient = server->available();
#endif
		client = &plainClient;
	}
	if(!*client)
		return;

	// Wait until the client sends some data (give up after HTTP_MAX_POST_WAIT ms)
	unsigned long waitStart = millis();
	while(!client->available())	{
		if(!client->connected() || (millis() - waitStart > HTTP_MAX_POST_WAIT))	{
			client->stop();
			return;
		}
		delay(1);
	}
	
	// reset all variables
	_chunked = false;
	_responseHeaders = String();
	_contentLength = CONTENT_LENGTH_NOT_SET;
	method = String();
	uri = String();
	contentLengthHeader = String();
	depthHeader = String();
	hostHeader = String();
	destinationHeader = String();
	authHeader = String();
	busGivenBack = false;

	// extract uri, headers etc
	if(parseRequest())	{
		// AUTH=1: nothing is served (not even "printer busy") without a valid login
		if(!checkAuth())
			sendAuthChallenge();
		else
			// invoke the handler
			(this->*handler)(message);
	}
		
	// finalize the response
	if(_chunked)
		sendContent("");

	// send all data before closing connection
	client->flush();
	// close the connection
	client->stop();
}





// ------------------------
bool ESPWebDAV::parseRequest() {
// ------------------------
	// Read the first line of HTTP request
	String req = client->readStringUntil('\r');
	client->readStringUntil('\n');
	
	// First line of HTTP request looks like "GET /path HTTP/1.1"
	// Retrieve the "/path" part by finding the spaces
	int addr_start = req.indexOf(' ');
	int addr_end = req.indexOf(' ', addr_start + 1);
	if (addr_start == -1 || addr_end == -1) {
		return false;
	}

	method = req.substring(0, addr_start);
	uri = urlDecode(req.substring(addr_start + 1, addr_end));
	// DBG_PRINT("method: "); DBG_PRINT(method); DBG_PRINT(" url: "); DBG_PRINTLN(uri);
	
	// parse and finish all headers
	String headerName;
	String headerValue;
	
	while(1) {
		req = client->readStringUntil('\r');
		client->readStringUntil('\n');
		if(req == "") 
			// no more headers
			break;
			
		int headerDiv = req.indexOf(':');
		if (headerDiv == -1)
			break;
		
		headerName = req.substring(0, headerDiv);
		headerValue = req.substring(headerDiv + 2);
		// DBG_PRINT("\t"); DBG_PRINT(headerName); DBG_PRINT(": "); DBG_PRINTLN(headerValue);
		
		if(headerName.equalsIgnoreCase("Host"))
			hostHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Depth"))
			depthHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Content-Length"))
			contentLengthHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Destination"))
			destinationHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Authorization"))
			authHeader = headerValue;
	}
	
	return true;
}




// ------------------------
void ESPWebDAV::sendHeader(const String& name, const String& value, bool first) {
// ------------------------
	String headerLine = name + ": " + value + "\r\n";

	if (first)
		_responseHeaders = headerLine + _responseHeaders;
	else
		_responseHeaders += headerLine;
}



// ------------------------
void ESPWebDAV::send(String code, const char* content_type, const String& content) {
// ------------------------
	String header;
	_prepareHeader(header, code, content_type, content.length());

	client->write(header.c_str(), header.length());
	if(content.length())
		sendContent(content);
}



// ------------------------
void ESPWebDAV::_prepareHeader(String& response, String code, const char* content_type, size_t contentLength) {
// ------------------------
	response = "HTTP/1.1 " + code + "\r\n";

	if(content_type)
		sendHeader("Content-Type", content_type, true);
	
	if(_contentLength == CONTENT_LENGTH_NOT_SET)
		sendHeader("Content-Length", String(contentLength));
	else if(_contentLength != CONTENT_LENGTH_UNKNOWN)
		sendHeader("Content-Length", String(_contentLength));
	else if(_contentLength == CONTENT_LENGTH_UNKNOWN) {
		_chunked = true;
		sendHeader("Accept-Ranges","none");
		sendHeader("Transfer-Encoding","chunked");
	}
	sendHeader("Connection", "close");

	response += _responseHeaders;
	response += "\r\n";
}



// ------------------------
void ESPWebDAV::sendContent(const String& content) {
// ------------------------
	const char * footer = "\r\n";
	size_t size = content.length();
	
	if(_chunked) {
		char * chunkSize = (char *) malloc(11);
		if(chunkSize) {
			sprintf(chunkSize, "%x%s", size, footer);
			client->write(chunkSize, strlen(chunkSize));
			free(chunkSize);
		}
	}
	
	client->write(content.c_str(), size);
	
	if(_chunked) {
		client->write(footer, 2);
		if (size == 0) {
			_chunked = false;
		}
	}
}



// ------------------------
void ESPWebDAV::sendContent_P(PGM_P content) {
// ------------------------
	const char * footer = "\r\n";
	size_t size = strlen_P(content);
	
	if(_chunked) {
		char * chunkSize = (char *) malloc(11);
		if(chunkSize) {
			sprintf(chunkSize, "%x%s", size, footer);
			client->write(chunkSize, strlen(chunkSize));
			free(chunkSize);
		}
	}
	
	client->write_P(content, size);
	
	if(_chunked) {
		client->write(footer, 2);
		if (size == 0) {
			_chunked = false;
		}
	}
}



// ------------------------
void ESPWebDAV::setContentLength(size_t len)	{
// ------------------------
	_contentLength = len;
}


// ------------------------
size_t ESPWebDAV::readBytesWithTimeout(uint8_t *buf, size_t bufSize) {
// ------------------------
	int timeout_ms = HTTP_MAX_POST_WAIT;
	size_t numAvailable = 0;
	while(!(numAvailable = client->available()) && client->connected() && timeout_ms--) 
		delay(1);

	if(!numAvailable)
		return 0;

	return client->read(buf, bufSize);
}


// ------------------------
// Read exactly numToRead bytes unless the client stops sending for HTTP_MAX_POST_WAIT ms.
// (PUT writes whole 512-byte blocks, and over https data arrives in TLS records of any size.)
size_t ESPWebDAV::readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead) {
// ------------------------
	if(numToRead > bufSize)
		numToRead = bufSize;
	size_t got = 0;
	unsigned long last = millis();
	while(got < numToRead)	{
		if(client->available() > 0)	{
			int r = client->read(buf + got, numToRead - got);
			if(r > 0)	{
				got += r;
				last = millis();
				continue;
			}
		}
		if(!client->connected() || millis() - last > HTTP_MAX_POST_WAIT)
			break;
		delay(1);
	}
	return got;
}


