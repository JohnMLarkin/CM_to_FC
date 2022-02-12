#include <mbed.h>
#include <rtos.h>
#include <CM_to_FC.h>

CM_to_FC::CM_to_FC(PinName tx, PinName rx) {
  _xbee = new XBeeAPIParser(tx, rx);
  _directoryEntries = 0;
  _registryEntries = 0;
  _timeout = 100ms;
  _linkedForData = 0;
  _rx_thread.start(callback(this, &CM_to_FC::_listen_for_rx));
}

void CM_to_FC::add_registry_entry(char n, char* ni, char len) {
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries<MAX_FC) {
      strcpy(_fcRegistry[_registryEntries].ni,ni);
      _fcRegistry[_registryEntries].index = n;
      _fcRegistry[_registryEntries].directoryIndex = 0xFF;
      _fcRegistry[_registryEntries].length = len;
      _fcRegistry[_registryEntries].dataUpdated = false;
      _registryEntries++;
    }
    _registry_mutex.unlock();
  }
}

void CM_to_FC::get_registry_entry(char i, char *podNum, char* ni, char *podBytes) {
  if (_registry_mutex.trylock_for(_timeout)) {
    if (i < _registryEntries) {
      *podNum = _fcRegistry[i].index;
      strcpy(ni, _fcRegistry[i].ni);
      *podBytes = _fcRegistry[i].length;
    }
  }
}

/** Broadcast code for "Primed for Launch"
 * 
 * @parameter char dataInterval - time (in seconds) between Iridium transmissions
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_launch_primed(char dataInterval) {
  char msg[2];
  msg[0] = 0x01;
  msg[1] = dataInterval;
  _xbee->txBroadcast(msg, 2);
}

/** Broadcast code for "Launch Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_launch_detected() {
  char msg[1];
  msg[0] = 0x02;
  _xbee->txBroadcast(msg, 1);
}

/** Broadcast code for "Descent Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_descent_detected() {
  char msg[1];
  msg[0] = 0x03;
  _xbee->txBroadcast(msg, 1);

}

/** Broadcast code for "Landing Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_landed() {
  char msg[1];
  msg[0] = 0x04;
  _xbee->txBroadcast(msg, 1);
}

void CM_to_FC::clear_registry() {
  _registryEntries = 0;
}

/** Broadcast code for invite to connect
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::invite() {
  char msg[1];
  msg[0] = 0x00;
  _xbee->txBroadcast(msg, 1);
}

void CM_to_FC::invite_registry() {
  char msg[1];
  msg[0] = 0x00;
  uint64_t addr;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      for (int i = 0; i < _registryEntries; i++) {
        if (_fcRegistry[i].directoryIndex == 0xFF) {
          addr = _xbee->get_address(_fcRegistry[i].ni);
          if (addr) _xbee->txAddressed(addr, msg, 1);
        }
      }
    }
    _registry_mutex.unlock();
  }
}

char CM_to_FC::registry_length() {
  return _registryEntries;
}

char CM_to_FC::directory_length() {
  return _directoryEntries;
}

void CM_to_FC::printDirectory() {
  if (_directory_mutex.trylock_for(_timeout)) {
    if (_directoryEntries>0) {
      printf("Directory:\r\n");
      printf("Index \tAddress \t\tConnectType \tGoodClock\r\n");
      for (int i = 0; i < _directoryEntries; i++) {
        printf("%d \t%016llX \t%d \t\t%d\r\n", i, _fcDirectory[i].address, _fcDirectory[i].connectType,_fcDirectory[i].goodClock);
      }
      printf("\r\n");
    }
    _directory_mutex.unlock();
  }
}

void CM_to_FC::printRegistry() {
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      printf("Registry:\r\n");
      printf("Identifier \tDirectory Index\r\n");
      for (int i = 0; i < _registryEntries; i++) {
        if (_fcRegistry[i].directoryIndex != 0xFF) {
          printf("%s \t%d\r\n", _fcRegistry[i].ni, _fcRegistry[i].directoryIndex);
        } else {
          printf("%s \tN/A\r\n", _fcRegistry[i].ni);
        }
      }
      printf("\r\n");
    }
    _registry_mutex.unlock();
  }
}

bool CM_to_FC::get_clock_status(char n, char* ni) {
  bool good_clock = false;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (n < _registryEntries) {
      strcpy(ni, _fcRegistry[n].ni);
      if (_fcRegistry[n].directoryIndex != 0xFF) {
        good_clock = _fcDirectory[_fcRegistry[n].directoryIndex].goodClock;
      }
    }
    _registry_mutex.unlock();
  }
  return good_clock;
}

void CM_to_FC::request_data(uint64_t addr) {
  char msg[1];
  msg[0] = 0x40;
  _xbee->txAddressed(addr, msg, 1);
}

void CM_to_FC::request_data_by_index(char n) {
  if (_registry_mutex.trylock_for(_timeout)) {
    if (n < _registryEntries) {
      char i = _fcRegistry[n].directoryIndex;
      if (i != 0xFF) {
        uint64_t addr = _fcDirectory[i].address;
        request_data(addr);
      }
    }
    _registry_mutex.unlock();
  }
}

void CM_to_FC::request_data_all() {
  uint8_t n;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      if (_directory_mutex.trylock_for(_timeout)) {
        for (int i = 0; i < _registryEntries; i++) {
          n = _fcRegistry[i].directoryIndex;
          if (n != 0xFF) {
            if ((_fcDirectory[n].connectType==0x02) && (_fcRegistry[i].length>0)) {
              request_data(_fcDirectory[n].address);
            }  
          } 
        }
        _directory_mutex.unlock();
      }
    }
    _registry_mutex.unlock();
  }  
}

void CM_to_FC::send_clock(uint64_t addr) {
  char msg[5];
  msg[0] = 0x20;
  uint32_t t = (uint32_t)time(NULL);
  for (int i = 0; i < 4; i++) {
    msg[i+1] = (t >> ((3-i)*8)) & 0xFF;
  }
  _xbee->txAddressed(addr, msg, 5);
}

void CM_to_FC::sync_registry() {
  Timer t;
  uint64_t addr;
  _linkedForData = 0;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_directory_mutex.trylock_for(_timeout)) {
      for (int i = 0; i < _registryEntries; i++) {
        if (_fcRegistry[i].directoryIndex==0xFF) {
          printf("Looking for %s\r\n", _fcRegistry[i].ni);
          t.start();
          addr = 0;
          while ((addr == 0) && (t.elapsed_time() < 10*_timeout)) {
            addr = _xbee->get_address(_fcRegistry[i].ni);
          }
          if (addr >= XBEE_MIN_ADDRESS) {
            printf("Its address is %016llX\r\n", addr);
            for (int j = 0; j < _directoryEntries; j++) {
              if (_fcDirectory[j].address==addr) {
                _fcRegistry[i].directoryIndex = j;
                printf("Found match at index %d\r\n", _fcRegistry[i].directoryIndex);
                if (_fcDirectory[i].connectType==0x02) _linkedForData++;
              }
            }
          }
        }
      }
    _directory_mutex.unlock();
    }
    _registry_mutex.unlock();
  }
}

void CM_to_FC::test_all_clocks() {
  if (_directory_mutex.trylock_for(_timeout)) {
    if (_directoryEntries>0) {
      for (int i = 0; i < _directoryEntries; i++) {
        if ((_fcDirectory[i].connectType==0x01) || (_fcDirectory[i].connectType==0x02))
          if (!_fcDirectory[i].goodClock) test_clock( _fcDirectory[i].address);
      }
    }
    _directory_mutex.unlock();
  }  
}

void CM_to_FC::test_clock(uint64_t addr) {
  char msg[5];
  msg[0] = 0x21;
  uint32_t t = (uint32_t)time(NULL);
  for (int i = 0; i < 4; i++) {
    msg[i+1] = (t >> ((3-i)*8)) & 0xFF;
  }
  _xbee->txAddressed(addr, msg, 5);
}

void CM_to_FC::_listen_for_rx() {
  int len;
  char msg[MAX_MSG_LENGTH];
  uint64_t sender = 0;
  _xbee->set_frame_alert_thread_id(osThreadGetId());
  while (true) {
    osSignalWait(0x01,osWaitForever);
    if (_xbee->readable()) {
      len = _xbee->rxPacket(msg, &sender);
      if (len > 0) {
        switch (msg[0]) {
          case 0x10:  // Response to invite
            _process_rsvp(sender, msg[1]);
            break;
          case 0x31:  // Response to clock test
            _process_clock_test(sender, msg[1]);
            break;
          case 0x50: // Incoming data
            _process_pod_data(sender, msg, len);
            break;
          default:
            // Nothing should fall into this category
            printf("Error! Unexpected rx msg code %0X\r\n", msg[0]);
        }
      }
    }
  }
}

void CM_to_FC::_process_clock_test(uint64_t addr, char testCode) {
  uint8_t directoryIndex = 0xFF;
  if (_directory_mutex.trylock_for(_timeout)) {  // Acquire exclusive access to the directory
    // Is this device in the directory?
    if (_directoryEntries>0) { // Directory entries exist so check
      int i = 0;
      while ((i < _directoryEntries) && (directoryIndex==0xFF)) {
        if (_fcDirectory[i].address == addr) directoryIndex = i;
        i++;
      }
      if (directoryIndex < _directoryEntries) { // Already listed
        _fcDirectory[directoryIndex].goodClock = (testCode==0x00);
      }
    }
    _directory_mutex.unlock();
  }
}

void CM_to_FC::_process_pod_data(uint64_t addr, char* payload, char len) {
  uint8_t registryIndex = 0xFF;
  uint8_t n;
  uint8_t k;
  printf("Time to process some pod data!\r\n");
  printf("Received from %016llX\r\n", addr);
  printf("Length of data is %d\r\n", len);
  for (int i = 0; i < len; i++) printf("%02X ", payload[i]);
  printf("\r\n");
  if (_registry_mutex.trylock_for(_timeout) && _directory_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      k = 0;
      while ((k < _registryEntries) && (registryIndex == 0xFF)) {
        n = _fcRegistry[k].directoryIndex;
        if (n != 0xFF) {
          if (_fcDirectory[n].address == addr) {
            registryIndex = k;  // registry matching addr
          }
        }
        k++;
      }
      printf("Registry index: %d\r\n", registryIndex);
      printf("Expected length of data: %d\r\n", _fcRegistry[registryIndex].length);
      if ((registryIndex < _registryEntries) && (_fcRegistry[registryIndex].length == len)) {
        for (int j = 1; j < len; j++)
          _fcRegistry[registryIndex].data[j-1] = payload[j];
        _fcRegistry[registryIndex].dataUpdated = true; 
      } else {
      }
    }
    _registry_mutex.unlock();
    _directory_mutex.unlock();
  }
}

void CM_to_FC::printPodData() {
  uint8_t n;
  if (_registry_mutex.trylock_for(_timeout) && _directory_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      printf("Pod Data\r\n");
      printf("--------\r\n");
      for (int i = 0; i < _registryEntries; i++) {
        n = _fcRegistry[i].directoryIndex;
        if (n != 0xFF) {
          if (_fcDirectory[n].connectType == 0x02) {
            printf("Pod %d: ", i+1);
            for (int j = 0; j < _fcRegistry[i].length; j++)
              printf("%02X ", _fcRegistry[i].data[j]);
            printf("\r\n");
          }
        }
      }
      printf("\r\n");
    }
    _directory_mutex.unlock();
    _registry_mutex.unlock();
  }
}

/**
 * get_pod_data
 * 
 * podNum:  number of pod (1-6)
 * 
 */
int CM_to_FC::get_pod_data(char podNum, char* data) {
  int len = 0;
  char i = 0xFF;  // location of desired pod in registry
  char k = 0;
  if (_registry_mutex.trylock_for(_timeout) && _directory_mutex.trylock_for(_timeout)) {
    while ((k < _registryEntries) && (i > _registryEntries)) {
      if (_fcRegistry[k].index == podNum) i = k;
      k++;
    }
    if (i < _registryEntries) {
      if (_fcRegistry[i].dataUpdated) {
        char n = _fcRegistry[i].directoryIndex;  // location of desired pod in directory
        if ((n != 0xFF) && (_fcDirectory[n].connectType == 0x02)) {
          for (int j = 0; j < (_fcRegistry[i].length-1); j++)
            data[j] = _fcRegistry[i].data[j];
          len = _fcRegistry[i].length-1;
          _fcRegistry[i].dataUpdated = false;
        }
      }
    }
    _directory_mutex.unlock();
    _registry_mutex.unlock();
  }
  return len;
}

int CM_to_FC::link_count() {
  return _linkedForData;
}

bool CM_to_FC::is_all_data_updated() {
  bool ready = true;
  for (int i = 0; i < _registryEntries; i++)
    if (_fcRegistry[i].length>0) {
      ready = ready && _fcRegistry[i].dataUpdated;
    }
  return ready;
}

char CM_to_FC::pod_index_to_number(char i) {
  if (i < _registryEntries) {
    return _fcRegistry[i].index;
  } else {
    return 0xFF;
  }
}

char CM_to_FC::pod_number_to_index(char podNum) {
  char i = 0xFF;  // location of desired pod in registry
  char k = 0;
  if (_registry_mutex.trylock_for(_timeout)) {
    while ((k < _registryEntries) && (i > _registryEntries)) {
      if (_fcRegistry[k].index == podNum) i = k;
      k++;
    }
    _registry_mutex.unlock();
  }
  return i;
}

void CM_to_FC::_process_rsvp(uint64_t addr, char connectType) {
  uint8_t directoryIndex = 0xFF;
  if (_directory_mutex.trylock_for(_timeout)) {  // Acquire exclusive access to the directory
    // Does a connection listing already exist for this device?
    if (_directoryEntries>0) { // Directory entries exist so check
      int i = 0;
      while ((i < _directoryEntries) && (directoryIndex==0xFF)) {
        if (_fcDirectory[i].address == addr) directoryIndex = i;
        i++;
      }
      if (directoryIndex < _directoryEntries) { // Already listed
        _fcDirectory[directoryIndex].connectType = connectType;
      }
    }
    if (directoryIndex == 0xFF) {
      if (_directoryEntries<MAX_FC) {
        directoryIndex = _directoryEntries;
        _directoryEntries++;
        _fcDirectory[directoryIndex].address = addr;
        _fcDirectory[directoryIndex].connectType = connectType;
      } 
    }
    if (directoryIndex < _directoryEntries) {
      switch (_fcDirectory[directoryIndex].connectType) {
        case 0x00:  // Decline any connection
          // Does nothing (for now)
          break;
        case 0x01:  // Send clock
          send_clock(addr);
          break;
        case 0x02:  // Send clock, expecting data during flight
          send_clock(addr);
          break;
      }
    }
    _directory_mutex.unlock();
  }
}


          