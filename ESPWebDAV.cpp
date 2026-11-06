// WebDAV server using ESP8266 and SD card filesystem
// Targeting Windows 7 Explorer WebDav

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <Hash.h>
#include <time.h>
#include "ESPWebDAV.h"
#include "logger.h"
#include "config.h"
#include "firmware.h"
#include "sdControl.h"
#include <Updater.h>
#include "manager_html.h"

// define cal constants
const char *months[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *wdays[]  = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};


// ------------------------
bool ESPWebDAV::init(int chipSelectPin, SdSpeed_t spiSpeed, int serverPort) {
// ------------------------
	// start the wifi server (only once, init() runs again after a reconnect)
	if(!server)	{
		server = new WiFiServer(serverPort);
		server->begin();
	}
	
	// initialize the SD card
	sdReady = sd.begin(chipSelectPin, spiSpeed);
	return sdReady;
}

// ------------------------
bool ESPWebDAV::initSD(int chipSelectPin, SdSpeed_t spiSpeed) {
	// initialize the SD card
	sdReady = sd.begin(chipSelectPin, spiSpeed);
	return sdReady;
}

// ------------------------
// Append text to a log file, moving it to oldPath first when it gets too big
bool ESPWebDAV::appendLog(const char* path, const char* oldPath, const String& text) {
// ------------------------
	if(!sdReady) return false;
	sd.chvol();
	FatFile f;
	if(f.open(path, O_RDONLY))	{
		uint32_t size = f.fileSize();
		f.close();
		if(size > LOG_FILE_MAX)	{
			sd.remove(oldPath);
			sd.rename(path, oldPath);
		}
	}
	if(!f.open(path, O_WRONLY | O_CREAT | O_APPEND))
		return false;
	size_t n = (size_t)f.write((const void*)text.c_str(), text.length());
	f.close();
	return n == text.length();
}

// ------------------------
bool ESPWebDAV::sdRename(const char* from, const char* to) {
// ------------------------
	if(!sdReady) return false;
	sd.chvol();
	if(!sd.exists(from)) return false;
	if(sd.exists(to)) sd.remove(to);
	return sd.rename(from, to);
}

// ------------------------
bool ESPWebDAV::sdWriteFile(const char* path, const char* text) {
// ------------------------
	if(!sdReady) return false;
	sd.chvol();
	FatFile f;
	if(!f.open(path, O_WRONLY | O_CREAT | O_TRUNC))
		return false;
	size_t len = strlen(text);
	size_t n = (size_t)f.write((const void*)text, len);
	f.close();
	return n == len;
}

// ------------------------
bool ESPWebDAV::startServer() {
// ------------------------
	// start the wifi server
	server->begin();
	return true;
}

// ------------------------
void ESPWebDAV::handleNotFound() {
// ------------------------
	String message = "Not found\n";
	message += "URI: ";
	message += uri;
	message += " Method: ";
	message += method;
	message += "\n";

	sendHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
	send("404 Not Found", "text/plain", message);
	DBG_PRINTLN("404 Not Found");
	logger.web(LOG_NORMAL, "404 %s %s", method.c_str(), uri.c_str());
}



// ------------------------
void ESPWebDAV::handleReject(String rejectMessage)	{
// ------------------------
	DBG_PRINT("Rejecting request: "); DBG_PRINTLN(rejectMessage);

	// the file manager page lives in flash, so it works even while the printer has the card
	if(handleManagerRoutes())
		return;
	if(method.equals("GET") && uri == "/")
		return redirectToManager();
	// lets the file manager page show "printer busy" instead of a fake listing
	sendHeader("X-Busy", rejectMessage);
	sendHeader("DAV", "1, 2");
	logger.web(LOG_SPARSE, "REJECTED %s %s from %s: %s", method.c_str(), uri.c_str(), client->remoteIP().toString().c_str(), rejectMessage.c_str());

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(RESOURCE_NONE);
	
	// handle properties
	if(method.equals("PROPFIND"))	{
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
		setContentLength(CONTENT_LENGTH_UNKNOWN);
		send("207 Multi-Status", "application/xml;charset=utf-8", "");
		sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 30 Nov 1979 00:00:00 GMT</D:getlastmodified><D:getetag>\"3333333333333333333333333333333333333333\"</D:getetag><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"));
		
		if(depthHeader.equals("1"))	{
			sendContent(F("<D:response><D:href>/"));
			sendContent(urlEncodePath(rejectMessage));
			sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 01 Apr 2016 16:07:40 GMT</D:getlastmodified><D:getetag>\"2222222222222222222222222222222222222222\"</D:getetag><D:resourcetype/><D:getcontentlength>0</D:getcontentlength><D:getcontenttype>application/octet-stream</D:getcontenttype></D:prop></D:propstat></D:response>"));
		}
		
		sendContent(F("</D:multistatus>"));
		return;
	}
	else
		// if reached here, means its a 404
		handleNotFound();
}




// set http_proxy=http://localhost:36036
// curl -v -X PROPFIND -H "Depth: 1" http://Rigidbot/Old/PipeClip.gcode
// Test PUT a file: curl -v -T c.txt -H "Expect:" http://Rigidbot/c.txt
// C:\Users\gsbal>curl -v -X LOCK http://Rigidbot/EMA_CPP_TRCC_Tutorial/Consumer.cpp -d "<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:lockinfo xmlns:D=\"DAV:\"><D:lockscope><D:exclusive/></D:lockscope><D:locktype><D:write/></D:locktype><D:owner><D:href>CARBON2\gsbal</D:href></D:owner></D:lockinfo>"
// ------------------------
void ESPWebDAV::handleRequest(String blank)	{
// ------------------------
	ResourceType resource = RESOURCE_NONE;

	// built-in file manager page
	if(handleManagerRoutes())
		return;

	// make this card the working volume for path-based opens (config.cpp uses its own SdFat)
	sd.chvol();

	// does uri refer to a file or directory or a null?
	FatFile tFile;
	if(tFile.open(uri.c_str(), O_RDONLY))	{
		resource = tFile.isDir() ? RESOURCE_DIR : RESOURCE_FILE;
		tFile.close();
	}

	DBG_PRINT("\r\nm: "); DBG_PRINT(method);
	DBG_PRINT(" r: "); DBG_PRINT(resource);
	DBG_PRINT(" u: "); DBG_PRINTLN(uri);
	// browsing requests are very frequent, so only at verbose; others (LOCK, OPTIONS, MKCOL...) at normal
	logger.web((method.equals("PROPFIND") || method.equals("GET") || method.equals("HEAD")) ? LOG_VERBOSE : LOG_NORMAL,
		"%s %s from %s", method.c_str(), uri.c_str(), client->remoteIP().toString().c_str());

	// add header that gets sent everytime
	// class 1 and 2 (locking); macOS Finder refuses servers that don't list class 1
	sendHeader("DAV", "1, 2");

	// handle properties
	if(method.equals("PROPFIND"))
		return handleProp(resource);
	
	if(method.equals("GET"))
		return handleGet(resource, true);

	if(method.equals("HEAD"))
		return handleGet(resource, false);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(resource);

	// handle file create/uploads
	if(method.equals("PUT"))
		return handlePut(resource);
	
	// handle file locks
	if(method.equals("LOCK"))
		return handleLock(resource);
	
	if(method.equals("UNLOCK"))
		return handleUnlock(resource);
	
	if(method.equals("PROPPATCH"))
		return handlePropPatch(resource);
	
	// directory creation
	if(method.equals("MKCOL"))
		return handleDirectoryCreate(resource);

	// move a file or directory
	if(method.equals("MOVE"))
		return handleMove(resource);
	
	// delete a file or directory
	if(method.equals("DELETE"))
		return handleDelete(resource);

	// if reached here, means its a 404
	handleNotFound();
}



// ------------------------
void ESPWebDAV::handleOptions(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing OPTION");
	sendHeader("Allow", "OPTIONS,GET,HEAD,PUT,DELETE,MKCOL,MOVE,COPY,PROPFIND,PROPPATCH,LOCK,UNLOCK");
	sendHeader("MS-Author-Via", "DAV");
	send("200 OK", NULL, "");
}



// ------------------------
void ESPWebDAV::handleLock(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing LOCK");
	
	// does URI refer to an existing resource
	if(resource == RESOURCE_NONE)
		return handleNotFound();
	
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

	size_t contentLen = contentLengthHeader.toInt();
	uint8_t buf[1024];
	// leave room for the terminating zero; larger lock requests are cut short
	if(contentLen > sizeof(buf) - 1)
		contentLen = sizeof(buf) - 1;
	size_t numRead = readBytesWithTimeout(buf, contentLen, contentLen);
	
	if(numRead == 0)
		return handleNotFound();

	buf[numRead] = 0;
	String inXML = String((char*) buf);
	int startIdx = inXML.indexOf("<D:href>");
	int endIdx = inXML.indexOf("</D:href>");
	if(startIdx < 0 || endIdx < 0)
		return handleNotFound();
		
	String lockUser = inXML.substring(startIdx + 8, endIdx);
	String resp1 = F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><write/></D:locktype><D:lockscope><exclusive/></D:lockscope><D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken><D:lockroot><D:href>");
	String resp2 = F("</D:href></D:lockroot><D:depth>infinity</D:depth><D:owner><a:href xmlns:a=\"DAV:\">");
	String resp3 = F("</a:href></D:owner><D:timeout>Second-3600</D:timeout></D:activelock></D:lockdiscovery></D:prop>");

	send("200 OK", "application/xml;charset=utf-8", resp1 + uri + resp2 + lockUser + resp3);
}



// ------------------------
void ESPWebDAV::handleUnlock(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing UNLOCK");
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
	send("204 No Content", NULL, "");
}



// ------------------------
void ESPWebDAV::handlePropPatch(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("PROPPATCH forwarding to PROPFIND");
	handleProp(resource);
}



// ------------------------
void ESPWebDAV::handleProp(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing PROPFIND");
	// check depth header
	DepthType depth = DEPTH_NONE;
	if(depthHeader.equals("1"))
		depth = DEPTH_CHILD;
	else if(depthHeader.equals("infinity"))
		depth = DEPTH_ALL;
	
	DBG_PRINT("Depth: "); DBG_PRINTLN(depth);

	// does URI refer to an existing resource
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(resource == RESOURCE_FILE)
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	else
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");

	setContentLength(CONTENT_LENGTH_UNKNOWN);
	send("207 Multi-Status", "application/xml;charset=utf-8", "");
	sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
	sendContent(F("<D:multistatus xmlns:D=\"DAV:\">"));

	// open this resource
	SdFile baseFile;
	baseFile.open(uri.c_str(), O_READ);
	sendPropResponse(false, &baseFile);

	if((resource == RESOURCE_DIR) && (depth == DEPTH_CHILD))	{
		// append children information to message
		SdFile childFile;
		while(childFile.openNext(&baseFile, O_READ)) {
			yield();
			sendPropResponse(true, &childFile);
			childFile.close();
		}
	}

	baseFile.close();
	sendContent(F("</D:multistatus>"));
}



// ------------------------
void ESPWebDAV::sendPropResponse(boolean recursing, FatFile *curFile)	{
// ------------------------
	char buf[255];
	curFile->getName(buf, sizeof(buf));

// String fullResPath = "http://" + hostHeader + uri;
	String fullResPath = uri;

	if(recursing)
		if(fullResPath.endsWith("/"))
			fullResPath += String(buf);
		else
			fullResPath += "/" + String(buf);

	// get file modified time
	uint16_t fatDate = 0, fatTime = 0;
	sdGetModifyDateTime(curFile, &fatDate, &fatTime);

	// convert to required format
	tm tmStr = {};
	tmStr.tm_hour = SD_HOUR(fatTime);
	tmStr.tm_min = SD_MINUTE(fatTime);
	tmStr.tm_sec = SD_SECOND(fatTime);
	tmStr.tm_year = SD_YEAR(fatDate) - 1900;
	tmStr.tm_mon = SD_MONTH(fatDate) - 1;
	tmStr.tm_mday = SD_DAY(fatDate);
	time_t t2t = mktime(&tmStr);
	tm *gTm = gmtime(&t2t);

	// Tue, 13 Oct 2015 17:07:35 GMT
	sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT", wdays[gTm->tm_wday], gTm->tm_mday, months[gTm->tm_mon], gTm->tm_year + 1900, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
	String fileTimeStamp = String(buf);


	// send the XML information about thyself to client
	sendContent(F("<D:response><D:href>"));
	// folders end in '/'
	if(curFile->isDir() && !fullResPath.endsWith("/"))
		fullResPath += "/";
	// full path, URL-encoded (spaces etc. as %20) as WebDAV clients like Finder expect
	sendContent(urlEncodePath(fullResPath));
	sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>"));
	// append modified date
	sendContent(fileTimeStamp);
	sendContent(F("</D:getlastmodified><D:getetag>"));
	// append unique tag generated from full path
	sendContent("\"" + sha1(fullResPath + fileTimeStamp) + "\"");
	sendContent(F("</D:getetag>"));

	if(curFile->isDir())
		sendContent(F("<D:resourcetype><D:collection/></D:resourcetype>"));
	else	{
		sendContent(F("<D:resourcetype/><D:getcontentlength>"));
		// append the file size
		sendContent(String(curFile->fileSize()));
		sendContent(F("</D:getcontentlength><D:getcontenttype>"));
		// append correct file mime type
		sendContent(getMimeType(fullResPath));
		sendContent(F("</D:getcontenttype>"));
	}
	sendContent(F("</D:prop></D:propstat></D:response>"));
}




// ------------------------
void ESPWebDAV::handleGet(ResourceType resource, bool isGet)	{
// ------------------------
	DBG_PRINTLN("Processing GET");

	// a browser asking for a folder (e.g. http://printer1.local/) gets the file manager
	if(resource == RESOURCE_DIR && isGet)
		return redirectToManager();

	// does URI refer to an existing file resource
	if(resource != RESOURCE_FILE)
		return handleNotFound();

	SdFile rFile;
	long tStart = millis();
	uint8_t buf[1460];
	rFile.open(uri.c_str(), O_READ);

	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	size_t fileSize = rFile.fileSize();
	setContentLength(fileSize);
	String contentType = getMimeType(uri);
	if(uri.endsWith(".gz") && contentType != "application/x-gzip" && contentType != "application/octet-stream")
		sendHeader("Content-Encoding", "gzip");

	send("200 OK", contentType.c_str(), "");

	if(isGet)	{
		// disable Nagle if buffer size > TCP MTU of 1460
		client->setNoDelay(true);

		// send the file
		while(rFile.available())	{
			// SD read speed ~ 17sec for 4.5MB file
			int numRead = rFile.read(buf, sizeof(buf));
			client->write(buf, numRead);
		}
	}

	rFile.close();
	if(isGet)
		logger.files(LOG_NORMAL, "DOWNLOAD %s (%u bytes, %lu ms) to %s", uri.c_str(), (unsigned)fileSize, (unsigned long)(millis() - tStart), client->remoteIP().toString().c_str());
	DBG_PRINT("File "); DBG_PRINT(fileSize); DBG_PRINT(" bytes sent in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
}




// ------------------------
void ESPWebDAV::handlePut(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing Put");

	// does URI refer to a directory
	if(resource == RESOURCE_DIR)
		return handleNotFound();

	SdFile nFile;
	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");

	// if file does not exist, create it
	if(resource == RESOURCE_NONE)	{
		if(!nFile.open(uri.c_str(), O_CREAT | O_WRITE))
			return handleWriteError("Unable to create a new file", &nFile);
	}

	// file is created/open for writing at this point
	DBG_PRINT(uri); DBG_PRINTLN(" - ready for data");
	// did server send any data in put
	size_t contentLen = contentLengthHeader.toInt();

	if(contentLen != 0)	{
		// buffer size is critical *don't change*
		const size_t WRITE_BLOCK_CONST = 512;
		uint8_t buf[WRITE_BLOCK_CONST];
		long tStart = millis();
		size_t numRemaining = contentLen;

		// high speed raw write implementation
		// close any previous file
		nFile.close();
		// delete old file
		sd.remove(uri.c_str());
	
		// create a contiguous file
		size_t contBlocks = (contentLen/WRITE_BLOCK_CONST + 1);
		uint32_t bgnBlock, endBlock;

		if (!nFile.createContiguous(uri.c_str(), contBlocks * WRITE_BLOCK_CONST))
			return handleWriteError("File create contiguous sections failed", &nFile);

		// get the location of the file's blocks
		if (!nFile.contiguousRange(&bgnBlock, &endBlock))
			return handleWriteError("Unable to get contiguous range", &nFile);

#if SDFAT_V2
		if (!sd.card()->writeStart(bgnBlock))
#else
		if (!sd.card()->writeStart(bgnBlock, contBlocks))
#endif
			return handleWriteError("Unable to start writing contiguous range", &nFile);

		// read data from stream and write to the file
		while(numRemaining > 0)	{
			size_t numToRead = (numRemaining > WRITE_BLOCK_CONST) ? WRITE_BLOCK_CONST : numRemaining;
			// never read past this file's data, so numRemaining can't wrap around
			size_t numRead = readBytesWithTimeout(buf, numToRead, numToRead);
			if(numRead == 0)
				break;

			// store whole buffer into file regardless of numRead
			if (!sd.card()->writeData(buf))
				return handleWriteError("Write data failed", &nFile);

			// reduce the number outstanding
			numRemaining -= numRead;
		}

		// stop writing operation
		if (!sd.card()->writeStop())
			return handleWriteError("Unable to stop writing contiguous range", &nFile);

		// detect timeout condition
		if(numRemaining)
			return handleWriteError("Timed out waiting for data", &nFile);

		// truncate the file to right length
		if(!nFile.truncate(contentLen))
			return handleWriteError("Unable to truncate the file", &nFile);

		logger.files(LOG_NORMAL, "UPLOAD %s (%u bytes, %lu ms) from %s", uri.c_str(), (unsigned)contentLen, (unsigned long)(millis() - tStart), client->remoteIP().toString().c_str());
		DBG_PRINT("File "); DBG_PRINT(contentLen - numRemaining); DBG_PRINT(" bytes stored in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
	}

	if(contentLen == 0)
		logger.files(LOG_NORMAL, "UPLOAD %s (empty file) from %s", uri.c_str(), client->remoteIP().toString().c_str());

	if(resource == RESOURCE_NONE)
		send("201 Created", NULL, "");
	else
		send("200 OK", NULL, "");

	nFile.close();
}




// ------------------------
void ESPWebDAV::handleWriteError(String message, FatFile *wFile)	{
// ------------------------
	// close this file
	wFile->close();
	// delete the wrile being written
	sd.remove(uri.c_str());
	// send error
	send("500 Internal Server Error", "text/plain", message);
	DBG_PRINTLN(message);
	logger.files(LOG_SPARSE, "UPLOAD %s FAILED: %s", uri.c_str(), message.c_str());
}


// ------------------------
void ESPWebDAV::handleDirectoryCreate(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MKCOL");
	
	// does URI refer to anything
	if(resource != RESOURCE_NONE)
		return handleNotFound();
	
	// create directory
	if (!sd.mkdir(uri.c_str(), true)) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to create directory");
		DBG_PRINTLN("Unable to create directory");
		logger.files(LOG_SPARSE, "MKDIR %s FAILED", uri.c_str());
		return;
	}

	DBG_PRINT(uri);	DBG_PRINTLN(" directory created");
	logger.files(LOG_NORMAL, "MKDIR %s from %s", uri.c_str(), client->remoteIP().toString().c_str());
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}



// ------------------------
void ESPWebDAV::handleMove(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MOVE");
	
	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(destinationHeader.length() == 0)
		return handleNotFound();
	
	String dest = urlDecode(urlToUri(destinationHeader));
	if(dest.length() > 1 && dest.endsWith("/"))
		dest.remove(dest.length() - 1);
		
	DBG_PRINT("Move destination: "); DBG_PRINTLN(dest);

	// move file or directory
	String src = uri;
	if(src.length() > 1 && src.endsWith("/"))
		src.remove(src.length() - 1);
	if ( !sd.rename(src.c_str(), dest.c_str())	) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to move");
		DBG_PRINTLN("Unable to move file/directory");
		logger.files(LOG_SPARSE, "MOVE %s -> %s FAILED", uri.c_str(), dest.c_str());
		return;
	}

	DBG_PRINTLN("Move successful");
	logger.files(LOG_NORMAL, "MOVE %s -> %s from %s", uri.c_str(), dest.c_str(), client->remoteIP().toString().c_str());
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}




// ------------------------
void ESPWebDAV::handleDelete(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing DELETE");
	
	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	bool retVal;
	
	if(resource == RESOURCE_FILE)
		// delete a file
		retVal = sd.remove(uri.c_str());
	else	{
		// delete a directory and everything in it (WebDAV DELETE on a folder is recursive)
		String path = uri;
		if(path.length() > 1 && path.endsWith("/"))
			path.remove(path.length() - 1);
		retVal = false;
		if(path.length() > 1)	{   // never delete the card's root
			FatFile dir;
			if(dir.open(path.c_str(), O_RDONLY))	{
				retVal = dir.rmRfStar();
				dir.close();
			}
		}
	}
		
	if(!retVal)	{
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to delete");
		DBG_PRINTLN("Unable to delete file/directory");
		logger.files(LOG_SPARSE, "DELETE %s FAILED", uri.c_str());
		return;
	}

	DBG_PRINTLN("Delete successful");
	logger.files(LOG_NORMAL, "DELETE %s from %s", uri.c_str(), client->remoteIP().toString().c_str());
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("200 OK", NULL, "");
}

// ------------------------
// Serve the built-in file manager at /_manager. Returns true if the request was handled.
bool ESPWebDAV::handleManagerRoutes() {
// ------------------------
	// firmware upload from the file manager (raw .bin body)
	if((method.equals("PUT") || method.equals("POST")) && uri == "/_update")	{
		handleFirmwareUpload();
		return true;
	}
	// card owner switch: GET /_card shows it, POST /_card?printer or ?network sets it
	if(uri == "/_card" || uri.startsWith("/_card?"))	{
		if(method.equals("POST") || method.equals("PUT"))	{
			bool toPrinter = uri.endsWith("printer");
			if(toPrinter != sdcontrol.reservedForPrinter())	{
				sdcontrol.reserveForPrinter(toPrinter);
				logger.net(LOG_NORMAL, "Card set to %s mode from %s", toPrinter ? "Printer" : "Network", client->remoteIP().toString().c_str());
			}
		}
		sendHeader("Cache-Control", "no-cache");
		send("200 OK", "text/plain", cardState());
		return true;
	}
	if(!(method.equals("GET") || method.equals("HEAD")))
		return false;
	// Card-sharing counters as plain text, e.g. http://<ip>/_diag (works while the printer has the card)
	if(uri == "/_diag")	{
		sendHeader("Cache-Control", "no-cache");
		send("200 OK", "text/plain", sdcontrol.diagSummary().c_str());
		return true;
	}
	// firmware build date, shown on the file manager page
	if(uri == "/_info")	{
		sendHeader("Cache-Control", "no-cache");
		// tells the page whether to offer "Update firmware" (always)
		sendHeader("X-Update", "1");
		sendHeader("X-Card", cardState());
		send("200 OK", "text/plain", "ESPWebDAV build " __DATE__ " " __TIME__);
		return true;
	}
	// the board's https certificate, to install as trusted on a computer
	if(uri == "/_cert" || uri == "/_cert.cer")	{
		handleCertDownload();
		return true;
	}
	if(uri == "/_manager" || uri == "/_manager/" || uri.startsWith("/_manager?"))	{
		// with HTTPS=1 the page is only served over https
		if(httpsOn() && !isSecure)
			redirectToManager();
		else
			handleManagerPage();
		return true;
	}
	return false;
}

// ------------------------
void ESPWebDAV::handleManagerPage() {
// ------------------------
	size_t len = strlen_P(MANAGER_HTML);
	sendHeader("Cache-Control", "no-cache");
	setContentLength(len);
	send("200 OK", "text/html; charset=utf-8", "");
	if(method.equals("GET"))
		client->write_P(MANAGER_HTML, len);
	logger.web(LOG_VERBOSE, "file manager page to %s", client->remoteIP().toString().c_str());
}

// ------------------------
// Send a browser to the file manager, opened at the folder it asked for
void ESPWebDAV::redirectToManager() {
// ------------------------
	String target = "/_manager";
	if(uri.length() > 1 && !uri.startsWith("/_manager"))	{
		String p = uri;
		p.replace("%", "%25");
		p.replace(" ", "%20");
		p.replace("#", "%23");
		target += "#" + p;
	}
	if(httpsOn() && !isSecure)
		target = "https://" + requestHost() + target;
	sendHeader("Location", target);
	send("302 Found", "text/plain", "File manager: " + target);
}

// ------------------------
// New firmware uploaded from the file manager. The SD card isn't needed, so it goes
// back to the printer for the 10-30 seconds this takes.
void ESPWebDAV::handleFirmwareUpload() {
// ------------------------
	// A login is not required here (owner's choice): with AUTH=1 the normal login check still applies,
	// without it anyone on the network can replace the firmware. Log who did.
	logger.net(LOG_SPARSE, "Firmware upload from %s%s", client->remoteIP().toString().c_str(), config.authOn() ? "" : " (no login set)");
	size_t len = contentLengthHeader.toInt();
	if(len < 4096)	{
		send("400 Bad Request", "text/plain", "Firmware update: no file, or file too small\n");
		return;
	}
	sdcontrol.relinquishBusControl();
	busGivenBack = true;
	String who = client->remoteIP().toString();
	logger.net(LOG_NORMAL, "Firmware upload started (%u bytes) from %s", (unsigned)len, who.c_str());

	String err;
	bool ok = Update.begin(len);
	if(!ok)
		err = firmwareError() + " (needs " + String(len) + " bytes, " + String(ESP.getFreeSketchSpace()) + " free; a gzipped .bin.gz is much smaller)";
	uint8_t buf[1024];
	size_t left = len;
	bool first = true;
	while(ok && left)	{
		size_t n = readBytesWithTimeout(buf, sizeof(buf), left < sizeof(buf) ? left : sizeof(buf));
		if(n == 0)	{ ok = false; err = "upload stopped"; break; }
		// firmware images start with 0xE9; gzip-compressed ones (unpacked at boot) with 0x1F
		if(first && buf[0] != 0xE9 && buf[0] != 0x1F)	{ ok = false; err = "not an ESP8266 firmware .bin (or gzipped .bin) file"; break; }
		first = false;
		if(Update.write(buf, n) != n)	{ ok = false; err = firmwareError(); break; }
		left -= n;
		yield();
	}
	if(ok && !Update.end())	{ ok = false; err = firmwareError(); }
	if(!ok)
		Update.end(true);   // throw away a partial update; the old firmware keeps running

	if(ok)	{
		logger.net(LOG_NORMAL, "Firmware upload OK from %s, restarting", who.c_str());
		send("200 OK", "text/plain", "Firmware updated, restarting\n");
		client->flush();
		client->stop();
		delay(500);
		ESP.restart();
		while(true) delay(100);
	}
	logger.net(LOG_SPARSE, "Firmware upload FAILED from %s: %s", who.c_str(), err.c_str());
	send("500 Internal Server Error", "text/plain", "Firmware update failed: " + err + "\n");
}

// ------------------------
// "printer" (Printer mode), "busy" (printer used it in the last PRINTER_WAIT s) or "network"
const char* ESPWebDAV::cardState() {
// ------------------------
	if(sdcontrol.reservedForPrinter()) return "printer";
	if(sdcontrol.printerActive()) return "busy";
	return "network";
}

// ------------------------
// The name the browser used for the board (Host header without the port)
String ESPWebDAV::requestHost() {
// ------------------------
	String h = hostHeader;
	h.trim();
	int colon = h.indexOf(':');
	if(colon >= 0)
		h.remove(colon);
	if(h.length() == 0)
		h = WiFi.localIP().toString();
	return h;
}

// ------------------------
void ESPWebDAV::handleCertDownload() {
// ------------------------
	size_t len = 0;
	const uint8_t* der = https.certDer(len);
	if(!der)	{
		send("404 Not Found", "text/plain", "No certificate: HTTPS is off (set HTTPS=1)");
		return;
	}
	sendHeader("Content-Disposition", String("attachment; filename=\"") + config.deviceName() + ".cer\"");
	sendHeader("Cache-Control", "no-cache");
	setContentLength(len);
	send("200 OK", "application/x-x509-ca-cert", "");
	if(method.equals("GET"))
		client->write(der, len);
	logger.web(LOG_NORMAL, "certificate downloaded by %s", client->remoteIP().toString().c_str());
}

// ------------------------
// Percent-encode everything in a path except letters, digits, - _ . ~ and /
String ESPWebDAV::urlEncodePath(const String& s) {
// ------------------------
	static const char hex[] = "0123456789ABCDEF";
	String out;
	out.reserve(s.length() + 8);
	for(unsigned i = 0; i < s.length(); i++)	{
		uint8_t c = (uint8_t)s[i];
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
			out += (char)c;
		else	{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

// ------------------------
String ESPWebDAV::xmlEscape(const String& s) {
// ------------------------
	String out = s;
	out.replace("&", "&amp;");
	out.replace("<", "&lt;");
	out.replace(">", "&gt;");
	return out;
}

ESPWebDAV dav;
