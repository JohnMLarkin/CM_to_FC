#include <mbed.h>
#include <rtos.h>
#include <CM_to_FC.h>

CM_to_FC::CM_to_FC(PinName tx, PinName rx) : _xbee(tx, rx) {
  _directoryEntries = 0;
  _registryEntries = 0;
  _timeout = 100;
  _rx_thread.start(callback(this, &CM_to_FC::_listen_for_rx));
}

void CM_to_FC::add_registry_entry(char* ni, char len) {
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries<MAX_FC) {
      strcpy(_fcRegistry[_registryEntries].ni,ni);
      _fcRegistry[_registryEntries].directoryIndex = 0xFF;
      _fcRegistry[_registryEntries].length = len;
      _registryEntries++;
    }
    _registry_mutex.unlock();
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
  _xbee.txBroadcast(msg, 2);
}

/** Broadcast code for "Launch Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_launch_detected() {
  char msg[1];
  msg[0] = 0x02;
  _xbee.txBroadcast(msg, 1);
}

/** Broadcast code for "Descent Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_descent_detected() {
  char msg[1];
  msg[0] = 0x03;
  _xbee.txBroadcast(msg, 1);

}

/** Broadcast code for "Landing Detected"
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::broadcast_landed() {
  char msg[1];
  msg[0] = 0x04;
  _xbee.txBroadcast(msg, 1);
}

/** Broadcast code for invite to connect
 * 
 *  Testing status:  Lab tested
 */
void CM_to_FC::invite() {
  char msg[1];
  msg[0] = 0x00;
  _xbee.txBroadcast(msg, 1);
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
        if (_fcRegistry[i].directoryIndex<MAX_FC) {
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

void CM_to_FC::request_data(uint64_t addr) {
  char msg[1];
  msg[0] = 0x40;
  _xbee.txAddressed(addr, msg, 1);
}

void CM_to_FC::request_data_all() {
  char n;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      if (_directory_mutex.trylock_for(_timeout)) {
        for (int i = 0; i < _registryEntries; i++) {
          n = _fcRegistry[i].directoryIndex;
          if (n != 0xFF) {
            if (_fcDirectory[n].connectType==0x02) {
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
  _xbee.txAddressed(addr, msg, 5);
}

void CM_to_FC::sync_registry() {
  uint64_t addr;
  if (_registry_mutex.trylock_for(_timeout)) {
    if (_directory_mutex.trylock_for(_timeout)) {
      for (int i = 0; i < _registryEntries; i++) {
        if (_fcRegistry[i].directoryIndex==0xFF) {
          addr = _xbee.get_address(_fcRegistry[i].ni);
          for (int j = 0; j < _directoryEntries; j++) {
            if (_fcDirectory[j].address==addr)
              _fcRegistry[i].directoryIndex = j;
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
  _xbee.txAddressed(addr, msg, 5);
}

void CM_to_FC::_listen_for_rx() {
  int len;
  char msg[MAX_MSG_LENGTH];
  uint64_t sender = 0;
  _xbee.set_frame_alert_thread_id(osThreadGetId());
  while (true) {
    osSignalWait(0x01,osWaitForever);
    if (_xbee.readable()) {
      len = _xbee.rxPacket(msg, &sender);
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
            printf("Error! Unexpect rx msg code %0X\r\n", msg[0]);
        }
      }
    }
  }
}

void CM_to_FC::_process_clock_test(uint64_t addr, char testCode) {
  char directoryIndex = 0xFF;
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
  char directoryIndex = 0xFF;
  char registryIndex = 0xFF;
  char n;
  char k;
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
      if ((registryIndex < _registryEntries) && (_fcRegistry[registryIndex].length == (len-1))) {
        for (int j = 0; j < (len-1); j++)
          _fcRegistry[registryIndex].data[j] = payload[j+1]; 
      } else {
      }
    }
    _registry_mutex.unlock();
    _directory_mutex.unlock();
  }
}

void CM_to_FC::printPodData() {
  char n;
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


int CM_to_FC::get_pod_data(char* data) {
  int len = 0;
  char n;
  if (_registry_mutex.trylock_for(_timeout) && _directory_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      for (int i = 0; i < _registryEntries; i++) {
        n = _fcRegistry[i].directoryIndex;
        if ((n != 0xFF) && (_fcDirectory[n].connectType == 0x02)) {
          for (int j = 0; j < _fcRegistry[i].length; j++)
            data[len+j] = _fcRegistry[i].data[j];
          len = len + _fcRegistry[i].length;
        }
      }
    }
    _directory_mutex.unlock();
    _registry_mutex.unlock();
  }
  return len;
}

int CM_to_FC::link_count() {
  int linked = 0;
  char n;
  if (_registry_mutex.trylock_for(_timeout) && _directory_mutex.trylock_for(_timeout)) {
    if (_registryEntries>0) {
      for (int i = 0; i < _registryEntries; i++) {
        n = _fcRegistry[i].directoryIndex;
        if ((n != 0xFF) && (_fcDirectory[n].connectType == 0x02)) linked++;
      }
    }
    _directory_mutex.unlock();
    _registry_mutex.unlock();
  }
  return linked;
}

void CM_to_FC::_process_rsvp(uint64_t addr, char connectType) {
  char directoryIndex = 0xFF;
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


          