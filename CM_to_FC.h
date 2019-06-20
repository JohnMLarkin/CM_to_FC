/** Command Module interface for XBee communication with Flight Computer
 *
 *  @author John M. Larkin (jlarkin@whitworth.edu)
 *  @version 0.1
 *  @date 2019
 *  @copyright MIT License
 */

#ifndef CM_TO_FC_H
#define CM_TO_FC_H

#include "XBeeAPIParser.h"
#include "mbed.h"
#include "rtos.h"
#include <string> 
using namespace std;

#define MAX_FC 6
#define MAX_MSG_LENGTH 110
#define MAX_POD_DATA_BYTES 100

typedef struct {
  uint64_t address;
  char connectType;
  bool goodClock = false;
} directoryEntry_t;

typedef struct {
  char ni[22];
  uint8_t directoryIndex;
  uint8_t length;
  char data[MAX_POD_DATA_BYTES];
} registryEntry_t;

extern DigitalOut led2;;

class CM_to_FC
{
private:
  XBeeAPIParser _xbee;
  uint32_t _timeout;

  // Objects that should be guarded by mutex (for exclusive access)
  uint8_t _directoryEntries; // Number of associated FCs
  uint8_t _registryEntries; // Number of registered FCs
  directoryEntry_t _fcDirectory[MAX_FC];
  registryEntry_t _fcRegistry[MAX_FC];

  // RTOS management
  Mutex _registry_mutex;
  Mutex _directory_mutex;
  Thread _rx_thread;

  void _listen_for_rx();
  void _process_rsvp(uint64_t addr, char connectType);
  void _process_clock_test(uint64_t addr, char testCode);
  void _process_pod_data(uint64_t addr, char* payload, char len);

public:
  CM_to_FC(PinName tx, PinName rx);
  void invite();
  void invite_registry();
  void broadcast_launch_primed(char dataInterval);
  void broadcast_launch_detected();
  void broadcast_descent_detected();
  void broadcast_landed();
  void request_data(uint64_t addr);
  void request_data_all();
  void send_clock(uint64_t addr);
  void test_clock(uint64_t addr);
  void test_all_clocks();
  void add_registry_entry(char* ni, char len);
  void sync_registry();
  int  get_pod_data(char* data);
  int  link_count();
  char registry_length();

  void printDirectory();
  void printRegistry();
  void printPodData();
};

#endif