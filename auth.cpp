// Login for WebDAV and the file manager (AUTH=1, DAV_USER, DAV_PASSWORD).
// Digest (MD5) works over plain http with macOS Finder, Windows and Linux clients,
// so the password is never sent in clear text. Basic is also accepted over https.
#include "ESPWebDAV.h"
#include "config.h"
#include "logger.h"
#include <MD5Builder.h>
#include <base64.h>

#define AUTH_REALM  "ESPWebDAV"
#define NONCE_LIFE  600     // seconds a login challenge stays valid (clients renew it silently)

static String md5hex(const String& s) {
	MD5Builder m;
	m.begin();
	m.add(s);
	m.calculate();
	return m.toString();
}

// A challenge value the board can check later without remembering it: time + keyed hash
String ESPWebDAV::makeNonce(uint32_t ts) {
	if(!nonceSecret)
		nonceSecret = ESP.random() | 1;
	String t = String(ts, HEX);
	return t + "-" + md5hex(t + ":" + String(nonceSecret, HEX)).substring(0, 16);
}

// Value of name=value or name="value" in an Authorization header
String ESPWebDAV::authParam(const String& h, const char* name) {
	int n = strlen(name);
	int i = 0;
	while(true)	{
		i = h.indexOf(name, i);
		if(i < 0)
			return "";
		char before = i > 0 ? h[i - 1] : ' ';
		if((before == ' ' || before == ',' || before == '\t') && (int)h.length() > i + n && h[i + n] == '=')
			break;
		i += n;
	}
	int v = i + n + 1;
	if(v < (int)h.length() && h[v] == '"')	{
		int e = h.indexOf('"', v + 1);
		return e < 0 ? String("") : h.substring(v + 1, e);
	}
	int e = h.indexOf(',', v);
	String r = e < 0 ? h.substring(v) : h.substring(v, e);
	r.trim();
	return r;
}

bool ESPWebDAV::checkAuth() {
	authStale = false;
	if(!config.authOn())
		return true;
	// the public certificate needs no login (it's needed before the browser trusts the page)
	if((method.equals("GET") || method.equals("HEAD")) && (uri == "/_cert" || uri == "/_cert.cer"))
		return true;

	String h = authHeader;
	h.trim();
	if(h.length() == 0)
		return false;   // first request: normal, the client answers the challenge

	String user = config.davUser();
	String pass = config.davPassword();
	String who = "?";
	bool ok = false;

	if(h.length() > 6 && h.substring(0, 6).equalsIgnoreCase("Basic "))	{
		if(!isSecure)	{
			logger.web(LOG_SPARSE, "LOGIN REFUSED: Basic login over plain http from %s (use https, or a client that does Digest)",
				client->remoteIP().toString().c_str());
			return false;
		}
		String got = h.substring(6);
		got.trim();
		ok = got == base64::encode(user + ":" + pass, false);
		who = "(basic)";
	}
	else if(h.length() > 7 && h.substring(0, 7).equalsIgnoreCase("Digest "))	{
		String d = h.substring(7);
		who = authParam(d, "username");
		String realm = authParam(d, "realm");
		String nonce = authParam(d, "nonce");
		String duri = authParam(d, "uri");
		String resp = authParam(d, "response");
		String qop = authParam(d, "qop");
		String nc = authParam(d, "nc");
		String cnonce = authParam(d, "cnonce");
		String alg = authParam(d, "algorithm");
		if(who == user && realm == AUTH_REALM && (alg.length() == 0 || alg.equalsIgnoreCase("MD5")) && resp.length() == 32)	{
			String ha1 = md5hex(user + ":" + realm + ":" + pass);
			String ha2 = md5hex(method + ":" + duri);
			String expect = qop.length() ? md5hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2)
			                             : md5hex(ha1 + ":" + nonce + ":" + ha2);
			if(resp.equalsIgnoreCase(expect))	{
				// right password; also check the challenge is one of ours and not too old
				int dash = nonce.indexOf('-');
				uint32_t ts = dash > 0 ? strtoul(nonce.substring(0, dash).c_str(), nullptr, 16) : 0;
				if(dash > 0 && nonce == makeNonce(ts) && (uint32_t)(millis() / 1000) - ts <= NONCE_LIFE)
					ok = true;
				else	{
					authStale = true;   // the client retries by itself with a fresh challenge
					return false;
				}
			}
		}
	}

	if(!ok)	{
		logger.web(LOG_SPARSE, "LOGIN FAILED for user '%s' (%s %s) from %s", who.c_str(), method.c_str(), uri.c_str(),
			client->remoteIP().toString().c_str());
		delay(300);   // slows down password guessing
	}
	return ok;
}

void ESPWebDAV::sendAuthChallenge() {
	String c = String("Digest realm=\"") + AUTH_REALM + "\", qop=\"auth\", algorithm=MD5, nonce=\""
	           + makeNonce(millis() / 1000) + "\"";
	if(authStale)
		c += ", stale=TRUE";
	sendHeader("WWW-Authenticate", c);
	if(isSecure)
		sendHeader("WWW-Authenticate", String("Basic realm=\"") + AUTH_REALM + "\", charset=\"UTF-8\"");
	send("401 Unauthorized", "text/plain", "Login required\n");
	logger.web(LOG_VERBOSE, "login requested from %s for %s %s", client->remoteIP().toString().c_str(), method.c_str(), uri.c_str());
}
