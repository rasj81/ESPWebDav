#include "gcode.h"
#include "config.h"
#include "parser.h"
#include "network.h"
#include "serial.h"
#include <ESP8266WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "https.h"
#include "logger.h"
#include "firmware.h"
#include "sdControl.h"

Gcode gcode;

void Gcode::Handle() {
  // Get the serial input
  if (commands_in_queue < BUFSIZE) get_serial_commands();

  if (commands_in_queue) {
    process_next_command();

    // The queue may be reset by a command handler or by code invoked by idle() within a handler
    if (commands_in_queue) {
      --commands_in_queue;
      if (++cmd_queue_index_r >= BUFSIZE) cmd_queue_index_r = 0;
    }
  }
}

/**
 * Once a new command is in the ring buffer, call this to commit it
 */
void Gcode::_commit_command(bool say_ok) {
  send_ok[cmd_queue_index_w] = say_ok;
  if (++cmd_queue_index_w >= BUFSIZE) cmd_queue_index_w = 0;
  commands_in_queue++;
}

/**
 * Copy a command from RAM into the main command buffer.
 * Return true if the command was successfully added.
 * Return false for a full buffer, or if the 'command' is a comment.
 */
bool Gcode::_enqueuecommand(const char* cmd, bool say_ok) {
  if (*cmd == ';' || commands_in_queue >= BUFSIZE) return false;
  strcpy(command_queue[cmd_queue_index_w], cmd);
  _commit_command(say_ok);
  return true;
} 

/**
 * Get all commands waiting on the serial port and queue them.
 * Exit when the buffer is full or when no more characters are
 * left on the serial port.
 */
void Gcode::get_serial_commands() {
  static char serial_line_buffer[MAX_CMD_SIZE];
  static bool serial_comment_mode = false;

  /**
   * Loop while serial characters are incoming and the queue is not full
   */
  int c;
  while (commands_in_queue < BUFSIZE && (c = Serial.read()) >= 0) {

    char serial_char = c;

    /**
     * If the character ends the line
     */
    if (serial_char == '\n' || serial_char == '\r') {

      serial_comment_mode = false;                      // end of line == end of comment

      // Skip empty lines and comments
      if (!serial_count) { continue; }

      serial_line_buffer[serial_count] = 0;             // Terminate string
      serial_count = 0;                                 // Reset buffer

      char* command = serial_line_buffer;

      while (*command == ' ') command++;                // Skip leading spaces

      // Add the command to the queue
      _enqueuecommand(serial_line_buffer, true);
    }
    else if (serial_count >= MAX_CMD_SIZE - 1) {
      // Keep fetching, but ignore normal characters beyond the max length
      // The command will be injected when EOL is reached
    }
    else if (serial_char == '\\') {   // Handle escapes
      if ((c = MYSERIAL0.read()) >= 0 && !serial_comment_mode) // if we have one more character, copy it over
        serial_line_buffer[serial_count++] = (char)c;
      // otherwise do nothing
    }
    else { // it's not a newline, carriage return or escape char
      if (serial_char == ';') serial_comment_mode = true;
      if (!serial_comment_mode) serial_line_buffer[serial_count++] = serial_char;
    }
  } // queue has space, serial has data
}

void Gcode::printHelp() {
  SERIAL_ECHOLN("Serial commands:");
  SERIAL_ECHOLN("  M50              list all settings (same names as SETUP.INI)");
  SERIAL_ECHOLN("  M50 KEY=VALUE    change a setting, e.g. M50 SSID=MyWiFi  (type \\; for a ; in a value)");
  SERIAL_ECHOLN("  M51              save the settings to flash");
  SERIAL_ECHOLN("  M52              connect to Wi-Fi with the current settings");
  SERIAL_ECHOLN("  M53              Wi-Fi, server and card status");
  SERIAL_ECHOLN("  M54              scan for Wi-Fi networks");
  SERIAL_ECHOLN("  M55              show the date and time   M55 2026-11-05 14:30 sets it (no internet)");
  SERIAL_ECHOLN("  M56              HTTPS certificate info   M56 NEW makes a new certificate");
  SERIAL_ECHOLN("  M57              install FIRMWARE.BIN from the card now");
  SERIAL_ECHOLN("  M58              who may use the card   M58 PRINTER / M58 NETWORK switches it");
  SERIAL_ECHOLN("  M59              restart the board");
}

/**
 * M50: list settings, or M50 KEY=VALUE to change one (same names as SETUP.INI)
 */
void Gcode::gcode_M50() {
  String a = parser.string_arg ? parser.string_arg : "";
  a.trim();
  if(a.length() == 0) { config.printSettings(); return; }
  if(a.equalsIgnoreCase("HELP") || a == "?") { printHelp(); return; }
  int eq = a.indexOf('=');
  if(eq <= 0) {
    SERIAL_ECHOLN("Use M50 KEY=VALUE, e.g. M50 HOSTNAME=printer1 (M50 alone lists the settings)");
    return;
  }
  String key = a.substring(0, eq);
  key.trim();
  key.toUpperCase();
  String val = a.substring(eq + 1);
  val.trim();
  int r = config.setValue(key, val);
  if(r == SET_UNKNOWN) { Serial.printf("Unknown setting %s (M50 lists them)\n", key.c_str()); return; }
  if(r == SET_BAD)     { Serial.printf("Bad value for %s: '%s'\n", key.c_str(), val.c_str()); return; }
  if(r == SET_EMPTY)   { Serial.printf("%s can't be empty\n", key.c_str()); return; }

  if(key == "PASSWORD") Serial.printf("PASSWORD set (%d characters)\n", (int)strlen(config.password()));
  else if(key != "HOSTNAME") Serial.printf("%s=%s\n", key.c_str(), val.c_str());   // HOSTNAME prints itself

  // what takes effect right away, and what needs a reconnect
  if(key.startsWith("LOG_") || key == "PRINTER_WAIT") {
    config.applyLogLevels();
    SERIAL_ECHOLN("Changed now. M51 saves it.");
  }
  else if(key == "TZ") {
    if(network.isConnected()) network.applyTime();
    SERIAL_ECHOLN("Time zone changed now. M51 saves it.");
  }
  else if(key == "HTTPS")
    SERIAL_ECHOLN(config.https() ? "HTTPS starts in a moment (needs Wi-Fi). M51 saves it." : "HTTPS stops now. M51 saves it.");
  else if(key == "RENAME")
    SERIAL_ECHOLN("RENAME only matters inside SETUP.INI on the card.");
  else
    SERIAL_ECHOLN("M51 saves it, M52 reconnects to use it.");
}

/**
 * M51: save the settings to flash
 */
void Gcode::gcode_M51() {
  config.save();
  SERIAL_ECHOLN("Settings saved to flash.");
}

/**
 * M52: connect to Wi-Fi with the current settings
 */
void Gcode::gcode_M52() {
  if(!network.start()) {
    SERIAL_ECHOLN("Connect failed. Check the settings (M50), scan (M54), or see network.log on the card.");
    printHelp();
  }
}

/**
 * M53: status
 */
void Gcode::gcode_M53() {
  network.printStatus();
}

/**
 * M54: Wi-Fi scan
 */
void Gcode::gcode_M54() {
  network.scanReport();
}

/**
 * M55: show the date and time, or set it: M55 2026-11-05 14:30[:00] (local time)
 */
void Gcode::gcode_M55() {
  String a = parser.string_arg ? parser.string_arg : "";
  a.trim();
  if(a.length()) {
    int y, mo, d, h, mi, s = 0;
    if(sscanf(a.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5 || y < 2020 || mo < 1 || mo > 12 || d < 1 || d > 31) {
      SERIAL_ECHOLN("Use M55 YYYY-MM-DD HH:MM[:SS] (local time)");
      return;
    }
    struct tm t = {};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
    struct timeval tv = { mktime(&t), 0 };
    settimeofday(&tv, nullptr);
    SERIAL_ECHOLN("Clock set (internet time replaces it if available).");
  }
  time_t now = time(nullptr);
  if(now < 1700000000) {
    SERIAL_ECHOLN("Time: not set yet (no internet time). M55 YYYY-MM-DD HH:MM sets it.");
  }
  else {
    char buf[40];
    struct tm lt, ut;
    localtime_r(&now, &lt);
    gmtime_r(&now, &ut);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    Serial.printf("Local: %s\n", buf);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ut);
    Serial.printf("UTC:   %s\n", buf);
  }
  Serial.printf("TZ=%s\n", config.timeZone()[0] ? config.timeZone() : "(not set: UTC)");
  unsigned long up = millis() / 1000;
  Serial.printf("Uptime: %lud %02lu:%02lu:%02lu\n", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);
}

/**
 * M56: HTTPS certificate info, M56 NEW makes a new key and certificate
 */
void Gcode::gcode_M56() {
  String a = parser.string_arg ? parser.string_arg : "";
  a.trim();
  if(a.equalsIgnoreCase("NEW")) {
    https.renew();
  }
  else if(a.length()) {
    SERIAL_ECHOLN("Use M56 (info) or M56 NEW (new certificate)");
    return;
  }
  https.printInfo();
}

/**
 * M57: install FIRMWARE.BIN from the card (also done at every start)
 */
void Gcode::gcode_M57() {
  int r = firmwareFromCard();   // restarts on success
  if(r == 0) SERIAL_ECHOLN("No FIRMWARE.BIN on the card.");
  else if(r == -1) SERIAL_ECHOLN("The printer is using the card, try again in a few seconds.");
  else if(r == -2) SERIAL_ECHOLN("Could not read the card.");
}

/**
 * M58: card owner. PRINTER = the board leaves the card alone; NETWORK = shared (default)
 */
void Gcode::gcode_M58() {
  String a = parser.string_arg ? parser.string_arg : "";
  a.trim();
  a.toUpperCase();
  if(a == "PRINTER" || a == "P") {
    sdcontrol.reserveForPrinter(true);
    logger.net(LOG_NORMAL, "Card set to Printer mode (M58)");
  }
  else if(a == "NETWORK" || a == "N") {
    sdcontrol.reserveForPrinter(false);
    logger.net(LOG_NORMAL, "Card set to Network mode (M58)");
  }
  else if(a.length()) {
    SERIAL_ECHOLN("Use M58, M58 PRINTER or M58 NETWORK");
    return;
  }
  if(sdcontrol.reservedForPrinter())
    SERIAL_ECHOLN("Card: Printer mode. The board leaves the card alone; use Init media on the printer if it says no media.");
  else
    Serial.printf("Card: Network mode%s. The board uses the card when the printer hasn't for %lu s.\n",
      sdcontrol.printerActive() ? " (printer is using it right now)" : "", sdcontrol.waitMs() / 1000);
}

/**
 * M59: restart the board
 */
void Gcode::gcode_M59() {
  SERIAL_ECHOLN("Restarting...");
  logger.net(LOG_NORMAL, "Restart requested (M59)");
  Serial.flush();
  delay(200);
  ESP.restart();
}

/**
 * Process the parsed command and dispatch it to its handler
 */
void Gcode::process_parsed_command() {
  //SERIAL_ECHOLNPAIR("command_letter:", parser.command_letter);
  //SERIAL_ECHOLNPAIR("codenum:", parser.codenum);

  // Handle a known G, M, or T
  switch (parser.command_letter) {
    case 'G': switch (parser.codenum) {
      default: parser.unknown_command_error();
    }
    break;

    case 'M': switch (parser.codenum) {
      case 50: gcode_M50(); break;
      case 51: gcode_M51(); break;
      case 52: gcode_M52(); break;
      case 53: gcode_M53(); break;
      case 54: gcode_M54(); break;
      case 55: gcode_M55(); break;
      case 56: gcode_M56(); break;
      case 57: gcode_M57(); break;
      case 58: gcode_M58(); break;
      case 59: gcode_M59(); break;
      default: parser.unknown_command_error();
    }
    break;

    default: parser.unknown_command_error();
  }

  SERIAL_ECHOLN("ok");
}

void Gcode::process_next_command() {
  char * const current_command = command_queue[cmd_queue_index_r];
  current_command_sd_pos = command_sd_pos[cmd_queue_index_r];

  // Parse the next command in the queue
  parser.parse(current_command);
  process_parsed_command();
}
