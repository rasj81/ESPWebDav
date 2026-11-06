#pragma once
#include <ESP8266WiFi.h>
#include <SdFat.h>
#include "sdfat_compat.h"
#include "https.h"

// Debug logging shares the serial line with the printer (Marlin reads G-code from it).
// Uncomment only when debugging with the ESP disconnected from the printer.
// #define DEBUG

#ifdef DEBUG
	#define DBG_PRINT(...) 		{ Serial.print(__VA_ARGS__); }
	#define DBG_PRINTLN(...) 	{ Serial.println(__VA_ARGS__); }
#else
	#define DBG_PRINT(...) 		{}
	#define DBG_PRINTLN(...) 	{}
#endif

// constants for WebServer
#define CONTENT_LENGTH_UNKNOWN ((size_t) -1)
#define CONTENT_LENGTH_NOT_SET ((size_t) -2)
#define HTTP_MAX_POST_WAIT 		5000 

enum ResourceType { RESOURCE_NONE, RESOURCE_FILE, RESOURCE_DIR };
enum DepthType { DEPTH_NONE, DEPTH_CHILD, DEPTH_ALL };


class ESPWebDAV	{
public:
	bool init(int chipSelectPin, SdSpeed_t spiSpeed, int serverPort);
  bool initSD(int chipSelectPin, SdSpeed_t spiSpeed);
  uint32_t sdErrorCode() { return ((uint32_t)sd.card()->errorCode() << 8) | sd.card()->errorData(); }
  bool startServer();
	bool isClientWaiting();
	void acceptSecure();        // TLS handshake for a waiting https client (before the card is taken)
	bool httpsOn();
#if HTTPS_SUPPORTED
	void setSecureServer(BearSSL::WiFiServerSecure* s);
#endif
	void handleClient(String blank = "");
	void rejectClient(String rejectMessage);

	// SD helpers for other modules (caller must own the SPI bus)
	bool isSdReady() { return sdReady; }
	// true if the last request gave the SD card back to the printer early (firmware upload)
	bool gaveBusBack() { return busGivenBack; }
	bool appendLog(const char* path, const char* oldPath, const String& text);
	bool sdRename(const char* from, const char* to);
	bool sdWriteFile(const char* path, const char* text);
	
protected:
	typedef void (ESPWebDAV::*THandlerFunction)(String);
	
	void processClient(THandlerFunction handler, String message);
	void handleNotFound();
	void handleReject(String rejectMessage);
	void handleRequest(String blank);
	void handleOptions(ResourceType resource);
	void handleLock(ResourceType resource);
	void handleUnlock(ResourceType resource);
	void handlePropPatch(ResourceType resource);
	void handleProp(ResourceType resource);
	void sendPropResponse(boolean recursing, FatFile *curFile);
	void handleGet(ResourceType resource, bool isGet);
	void handlePut(ResourceType resource);
	void handleWriteError(String message, FatFile *wFile);
	void handleDirectoryCreate(ResourceType resource);
	void handleMove(ResourceType resource);
	void handleDelete(ResourceType resource);
	bool handleManagerRoutes();
	void handleManagerPage();
	void redirectToManager();
	void handleCertDownload();
	void handleFirmwareUpload();
	const char* cardState();
	bool checkAuth();
	void sendAuthChallenge();
	String authParam(const String& h, const char* name);
	String makeNonce(uint32_t ts);
	String requestHost();
	String xmlEscape(const String& s);
	String urlEncodePath(const String& s);

	// Sections are copied from ESP8266Webserver
	String getMimeType(String path);
	String urlDecode(const String& text);
	String urlToUri(String url);
	bool parseRequest();
	void sendHeader(const String& name, const String& value, bool first = false);
	void send(String code, const char* content_type, const String& content);
	void _prepareHeader(String& response, String code, const char* content_type, size_t contentLength);
	void sendContent(const String& content);
	void sendContent_P(PGM_P content);
	void setContentLength(size_t len);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead);
	
	
	// variables pertaining to current most HTTP request being serviced
	WiFiServer *server;
	SdFat sd;
	bool sdReady = false;

	WiFiClient	plainClient;
	WiFiClient*	client = &plainClient;  // the connection being served (plain or TLS)
	bool		isSecure = false;
#if HTTPS_SUPPORTED
	BearSSL::WiFiClientSecure secureClient;
	BearSSL::WiFiServerSecure* secureServer = nullptr;
	bool		securePending = false;
#endif
	String 		method;
	String 		uri;
	String 		contentLengthHeader;
	String 		depthHeader;
	String 		hostHeader;
	String		destinationHeader;
	String		authHeader;
	bool		authStale = false;
	bool		busGivenBack = false;
	uint32_t	nonceSecret = 0;

	String 		_responseHeaders;
	bool		_chunked;
	int			_contentLength;
};

extern ESPWebDAV dav;
