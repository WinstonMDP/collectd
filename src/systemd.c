#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "systemd/sd-bus.h"

int get_prop(sd_bus *bus, char const type[static 1], char const prop[static 1],
             void *var) {
  sd_bus_message *m = NULL;
  int r = sd_bus_get_property(
      bus, "org.freedesktop.systemd1",
      "/org/freedesktop/systemd1/unit/chronyd_2eservice",
      "org.freedesktop.systemd1.Service", prop, NULL, &m, type);
  if (r < 0) {
    return r;
  }
  r = sd_bus_message_read(m, type, var);
  sd_bus_message_unref(m);
  return r;
}

int submit(counter_t val, char const type_instance[static 1]) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1] = {{.counter = val}};
  vl.values = values;
  vl.values_len = 1;
  strncpy(vl.plugin, "systemd", sizeof(vl.plugin));
  strncpy(vl.type, "systemd_type", sizeof(vl.type));
  strncpy(vl.type_instance, type_instance, sizeof(vl.type_instance) - 1);
  return plugin_dispatch_values(&vl);
}

int submit_block(sd_bus *bus, char const accounting[static 1], size_t nprops,
                 char const *props[static nprops]) {
  bool accounting_var;
  int r = get_prop(bus, "b", accounting, &accounting_var);
  if (r < 0) {
    return r;
  }

  if (accounting_var) {
    for (size_t i = 0; i < nprops; ++i) {
      uint64_t value;
      get_prop(bus, "t", props[i], &value);
      if (r < 0) {
        // ERROR("Failed to get CPUUsageNSec property: %s\n", strerror(-r));
        return r;
      }

      r = submit(value, props[i]);
      if (r != 0) {
        // ERROR("Failed to dispatch: %s\n", STRERROR(r));
        return r;
      }
    }
  }
  return 0;
}

static int systemd_read(void) {
  sd_bus *bus = NULL;

  int r = sd_bus_open_system(&bus);
  if (r < 0) {
    ERROR("Failed to connect to system bus: %s\n", strerror(-r));
    goto defer;
  }

  r = submit_block(bus, "CPUAccounting", 1, (char const *[]){"CPUUsageNSec"});
  if (r != 0) {
    ERROR("Failed to submit CPU block: %s", STRERROR(r));
    goto defer;
  }

  r = submit_block(bus, "MemoryAccounting", 6,
                   (char const *[]){
                       "MemoryAvailable",
                       "MemoryCurrent",
                       "MemoryPeak",
                       "MemorySwapCurrent",
                       "MemoryZSwapCurrent",
                       "MemorySwapPeak",
                   });
  if (r != 0) {
    ERROR("Failed to submit Memory block: %s", STRERROR(r));
    goto defer;
  }

  r = submit_block(bus, "IOAccounting", 4,
                   (char const *[]){
                       "IOReadBytes",
                       "IOReadOperations",
                       "IOWriteBytes",
                       "IOWriteOperations",
                   });
  if (r != 0) {
    ERROR("Failed to submit IO block: %s", STRERROR(r));
    goto defer;
  }

  r = submit_block(bus, "IPAccounting", 4,
                   (char const *[]){
                       "IPEgressBytes",
                       "IPEgressPackets",
                       "IPIngressBytes",
                       "IPIngressPackets",
                   });
  if (r != 0) {
    ERROR("Failed to submit IO block: %s", STRERROR(r));
    goto defer;
  }

  r = submit_block(bus, "TasksAccounting", 1, (char const *[]){"TasksCurrent"});
  if (r != 0) {
    ERROR("Failed to submit IO block: %s", STRERROR(r));
    goto defer;
  }

  uint64_t value;
  get_prop(bus, "t", "NRestarts", &value);
  if (r < 0) {
    ERROR("Failed to get CPUUsageNSec property: %s\n", strerror(-r));
    goto defer;
  }

  r = submit(value, "NRestarts");
  if (r != 0) {
    ERROR("Failed to dispatch: %s\n", STRERROR(r));
    goto defer;
  }

defer:
  sd_bus_unref(bus);

  return EXIT_SUCCESS;
}

void module_register(void) { plugin_register_read("systemd", systemd_read); }
