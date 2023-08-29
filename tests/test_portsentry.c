#include <stdio.h>
#include <string.h>

#include "portsentry.h"
#include "portsentry_io.h"
#include "state_machine.h"

int gblConfigTriggerCount;

int test_state_machine(void);

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <test>\n", argv[0]);
    return 1;
  }

  if (strncmp(argv[1], "state_machine", 13) == 0) {
    if (!test_state_machine())
      return 1;
  } else {
    printf("Unknown test: %s\n", argv[1]);
    return 1;
  }

  return 0;
}

int test_state_machine(void) {
  char ip[16] = "192.168.0.1";

  // FIXME: Need to create better tests once this function is rewritten
  gblConfigTriggerCount = 2;
  if (CheckStateEngine(ip) == FALSE) {
    printf("1: pass\n");
  } else {
    printf("1: fail\n");
    return 1;
  }

  if (CheckStateEngine(ip) == TRUE) {
    printf("2: pass\n");
  } else {
    printf("2: fail\n");
    return 1;
  }

return 0;
}
