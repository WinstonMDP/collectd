#include "collectd.h"

#include "metric.h"
#include "plugin.h"

#include "systemd/sd-bus.h"
#include "utils/common/common.h"

typedef struct systemd_accounting_group systemd_accounting_group;
struct systemd_accounting_group {
  char const *accounting_flag;
  char **metrics;
};

systemd_accounting_group const groups[] = {
    {
        .accounting_flag = "MemoryAccounting",
        .metrics =
            (char *[]){
                "MemoryAvailable",
                "MemoryCurrent",
                "MemoryPeak",
                "MemorySwapCurrent",
                "MemoryZSwapCurrent",
                "MemorySwapPeak",
                NULL,
            },
    },
    {
        .accounting_flag = "IOAccounting",
        .metrics =
            (char *[]){
                "IOReadBytes",
                "IOReadOperations",
                "IOWriteBytes",
                "IOWriteOperations",
                NULL,
            },
    },
    {
        .accounting_flag = "CPUAccounting",
        .metrics =
            (char *[]){
                "CPUUsageNSec",
                NULL,
            },
    },
    {
        .accounting_flag = "IPAccounting",
        .metrics =
            (char *[]){
                "IPEgressBytes",
                "IPEgressPackets",
                "IPIngressBytes",
                "IPIngressPackets",
                NULL,
            },
    },
    {
        .accounting_flag = "TasksAccounting",
        .metrics =
            (char *[]){
                "TasksCurrent",
                NULL,
            },
    },
    {
        .accounting_flag = "true",
        .metrics =
            (char *[]){
                "NRestarts",
                NULL,
            },
    },
    {.accounting_flag = NULL},
};

// Err: negative
static int get_prop(sd_bus *bus, char const type[static 1],
                    char const prop[static 1], void *var, sd_bus_error *err) {
  sd_bus_message *m = NULL;
  int r = sd_bus_get_property(
      bus, "org.freedesktop.systemd1",
      "/org/freedesktop/systemd1/unit/avahi_2ddaemon_2eservice",
      "org.freedesktop.systemd1.Service", prop, err, &m, type);
  if (r < 0) {
    return r;
  }
  r = sd_bus_message_read(m, type, var);
  sd_bus_message_unref(m);
  return r;
}

static int systemd_read(void) {
  sd_bus *bus = NULL;
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;
  int r = sd_bus_open_system(&bus);
  if (r < 0) {
    ERROR("Failed to connect to system bus: %s", strerror(-r));
    goto defer;
  }
  for (systemd_accounting_group const *groups_it = groups;
       groups_it->accounting_flag != NULL; ++groups_it) {
    bool accounting_flag_var = true;
    if (strcmp(groups_it->accounting_flag, "true")) {
      r = get_prop(bus, "b", groups_it->accounting_flag, &accounting_flag_var,
                   &sd_bus_err);
      if (r < 0) {
        ERROR("Failed to get %s accounting flag: %s {%s}, %s",
              groups_it->accounting_flag, sd_bus_err.name, sd_bus_err.message,
              strerror(-r));
        goto defer;
      }
    }
    if (accounting_flag_var) {
      for (char **metrics_it = groups_it->metrics; *metrics_it != NULL;
           ++metrics_it) {
        uint64_t val;
        r = get_prop(bus, "t", *metrics_it, &val, &sd_bus_err);
        if (r < 0) {
          ERROR("Failed to get %s property: %s {%s}, %s", *metrics_it,
                sd_bus_err.name, sd_bus_err.message, strerror(-r));
          goto defer;
        }

        metric_family_t fam = {.name = *metrics_it,
                               .type = METRIC_TYPE_COUNTER};
        metric_family_metric_append(&fam, (metric_t){.value.counter = val});
        r = plugin_dispatch_metric_family(&fam);
        metric_family_metric_reset(&fam);
        if (r != 0) {
          ERROR("Failed to dispatch: %s", STRERROR(r));
          goto defer;
        }
      }
    }
  }

defer:
  sd_bus_unref(bus);
  sd_bus_error_free(&sd_bus_err);

  return EXIT_SUCCESS;
}

void module_register(void) { plugin_register_read("systemd", systemd_read); }
